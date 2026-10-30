#pragma once
// 400Scalper — the whole audio engine, independent of any plugin format.
// The iPlug2 plugin (CLAP today, VST3/AU/AAX later) and the tests both drive this class.
//
//   prepare(sr, mode)   on activate / sample-rate change
//   process(...)        audio thread, any block size, float or double buffers
//   latencySamples(m)   latency of mode m (Spectral adds the ear filter bank's frame)
//
// Only one clipping mode runs at a time. When the Mode parameter changes, the audio fades out
// (10 ms), swaps engine, resets it and fades back in. The plugin layer reports the new latency
// to the host separately (host restart).

#include "Params.h"
#include "Shared.h"
#include "../dsp/Engine.h"
#include "../dsp/Spectral.h"
#include <atomic>
#include <algorithm>
#include <cmath>

namespace scalper {

class Processor
{
public:
    // ---- shared with the UI (lock-free) ----
    VizHistory viz;
    SpectrumTap spectrum;
    PeakMeter meterIn, meterOut;
    SpectralSplitter spectral;                 // the UI reads its displayMask
    std::atomic<int> procMode { 0 };           // mode the audio is running right now
    std::atomic<double> currentSampleRate { 48000.0 };

    static int sampleLatency() { return ChannelEngine::latencySamples(); }

    int latencySamples (int mode) const
    {
        return ChannelEngine::latencySamples() + ceiling.latencySamples()
             + (mode == 1 ? spectral.latencySamples() : 0);
    }

    void prepare (double sr, const ParamValues& p)
    {
        sampleRate = sr;
        currentSampleRate.store (sr);
        for (auto& e : engines) e.prepare (sr);
        ceiling.prepare (sr);
        spectral.prepare (sr);
        for (auto& c : specClip) c.prepare (sr);
        for (auto& d : specVizIn) d.init (ChannelEngine::latencySamples() + ceiling.latencySamples());
        for (auto& d : deltaDelay) d.init (ceiling.latencySamples());
        const int L = ceiling.latencySamples();
        for (auto& band : vizDelay) for (auto& d : band) d.init (L);
        viz.prepare (sr);
        vizCount = 0;

        procMode.store (p[kMode] >= 0.5 ? 1 : 0);
        pendingMode = -1;
        modeGain = 1.0;
        modeFadeStep = 1.0 / (0.010 * sr);           // 10 ms out, 10 ms in
        smoothCoef = 1.0 - std::exp (-1.0 / (0.010 * sr));
        detailSm = p[kDetail] / 100.0;
        inGain  = dbToLin (p[kInGain]);
        outGain = dbToLin (p[kOutGain]);
        for (int b = 0; b < kMaxBands; ++b) thr[b] = dbToLin (p[kThr1 + b]);
        prepared = true;
    }

    void reset()
    {
        for (auto& e : engines) e.reset();
        ceiling.reset();
        for (auto& band : vizDelay) for (auto& d : band) d.reset();
        spectral.reset();
        for (auto& c : specClip) c.reset();
        for (auto& d : specVizIn) d.reset();
        for (auto& d : deltaDelay) d.reset();
    }

    // in/out: arrays of channel pointers. Mono in -> both engines see the same signal.
    template <typename T>
    void process (const T* const* in, int nIn, T* const* out, int nOut, int nFrames, const ParamValues& p)
    {
        if (! prepared || nOut < 1) return;
        if (nIn < 1)
        {
            for (int c = 0; c < nOut; ++c) std::fill (out[c], out[c] + nFrames, T (0));
            return;
        }

        // ---- parameter snapshot (once per block; smoothed per sample below) ----
        const double tIn = dbToLin (p[kInGain]), tOut = dbToLin (p[kOutGain]);
        int numBands = std::clamp (int (std::lround (p[kBands])), 1, kMaxBands);
        const int slopeDb = slopeDbFromParam (p[kSlope]);
        for (int i = 0; i < kMaxXovers; ++i)
        {
            engines[0].setCrossover (i, p[kXover1 + i]);
            engines[1].setCrossover (i, p[kXover1 + i]);
        }
        double tThr[kMaxBands], knee[kMaxBands];
        bool anySolo = false;
        for (int b = 0; b < kMaxBands; ++b)
        {
            tThr[b] = dbToLin (p[kThr1 + b]);
            knee[b] = p[kKnee1 + b] / 100.0;
            if (b < numBands && p[kSolo1 + b] >= 0.5) anySolo = true;
        }
        BandSettings settings[kMaxBands];
        for (int b = 0; b < kMaxBands; ++b)
            settings[b].audible = numBands == 1 || ! anySolo || p[kSolo1 + b] >= 0.5;
        const double tDetail = p[kDetail] / 100.0;
        const bool deltaOn = p[kDelta] >= 0.5;
        const int wantedMode = p[kMode] >= 0.5 ? 1 : 0;
        if (wantedMode == 1) numBands = 1;   // spectral mode is one clipper over the whole signal

        ClipCurve curves[kMaxBands];
        const bool stereoIn = nIn >= 2;
        float bi[2][kMaxBands], bo[2][kMaxBands];

        for (int i = 0; i < nFrames; ++i)
        {
            inGain  += (tIn  - inGain)  * smoothCoef;
            outGain += (tOut - outGain) * smoothCoef;
            for (int b = 0; b < numBands; ++b)
            {
                thr[b] += (tThr[b] - thr[b]) * smoothCoef;
                curves[b].set (thr[b], knee[b]);
            }

            const double xL = double (in[0][i]) * inGain;
            const double xR = (stereoIn ? double (in[1][i]) : double (in[0][i])) * inGain;

            // ---- mode switching: only one clipper ever runs ----
            if (pendingMode < 0 && wantedMode != procMode.load (std::memory_order_relaxed))
                pendingMode = wantedMode;
            if (pendingMode >= 0)
            {
                modeGain -= modeFadeStep;
                if (modeGain <= 0.0)
                {
                    modeGain = 0.0;
                    procMode.store (pendingMode, std::memory_order_relaxed);
                    pendingMode = -1;
                    reset();
                }
            }
            else if (modeGain < 1.0)
                modeGain = std::min (1.0, modeGain + modeFadeStep);

            double yL, yR, twinL = 0.0, twinR = 0.0;
            if (procMode.load (std::memory_order_relaxed) == 1)
            {
                // ===== SPECTRAL (the sample-mode engines are not run) =====
                detailSm += (tDetail - detailSm) * smoothCoef;
                double xdL, xdR, dL, dR;
                spectral.process (xL, xR, xdL, xdR, dL, dR);
                yL = specClip[0].process (xdL, dL, thr[0], knee[0], detailSm, twinL);
                yR = specClip[1].process (xdR, dR, thr[0], knee[0], detailSm, twinR);
                ceiling.process (yL, yR, thr[0], true);

                const float iMn = specVizIn[0].process (float (std::min (xdL, xdR)));
                const float iMx = specVizIn[1].process (float (std::max (xdL, xdR)));
                accumulate (0, iMn, iMx, float (std::min (yL, yR)), float (std::max (yL, yR)));
                if (++vizCount >= VizHistory::kDecim)
                {
                    for (int b = 1; b < kMaxBands; ++b) vizAcc[b] = { 0, 0, 0, 0 };
                    viz.push (vizAcc);
                    vizCount = 0;
                }
            }
            else
            {
                // ===== SAMPLE (the spectral engine is not run) =====
                yL = engines[0].process (xL, numBands, slopeDb, curves, settings, bi[0], bo[0], twinL);
                if (stereoIn) yR = engines[1].process (xR, numBands, slopeDb, curves, settings, bi[1], bo[1], twinR);
                else { yR = yL; twinR = twinL; for (int b = 0; b < numBands; ++b) { bi[1][b] = bi[0][b]; bo[1][b] = bo[0][b]; } }

                ceiling.process (yL, yR, thr[0], numBands == 1);

                for (int b = 0; b < numBands; ++b)
                {
                    const float iMn = vizDelay[b][0].process (std::min (bi[0][b], bi[1][b]));
                    const float iMx = vizDelay[b][1].process (std::max (bi[0][b], bi[1][b]));
                    float oMn = vizDelay[b][2].process (std::min (bo[0][b], bo[1][b]));
                    float oMx = vizDelay[b][3].process (std::max (bo[0][b], bo[1][b]));
                    if (numBands == 1) { oMn = float (std::min (yL, yR)); oMx = float (std::max (yL, yR)); }
                    accumulate (b, iMn, iMx, oMn, oMx);
                }
                if (++vizCount >= VizHistory::kDecim)
                {
                    for (int b = numBands; b < kMaxBands; ++b) vizAcc[b] = { 0, 0, 0, 0 };
                    viz.push (vizAcc);
                    vizCount = 0;
                }
            }

            // DELTA: the unclipped twin (same filters, same delay) minus the output = exactly what
            // the clipper and ceiling removed. Silent when nothing is being clipped.
            {
                const double dTL = deltaDelay[0].process (float (twinL)), dTR = deltaDelay[1].process (float (twinR));
                if (deltaOn) { yL = dTL - yL; yR = dTR - yR; }
            }
            yL *= modeGain; yR *= modeGain;
            const double oL = yL * outGain, oR = yR * outGain;
            meterIn.raise (float (std::max (std::fabs (xL), std::fabs (xR))));
            meterOut.raise (float (std::max (std::fabs (oL), std::fabs (oR))));
            spectrum.push (float (0.5 * (xL + xR)));

            out[0][i] = T (oL);
            if (nOut >= 2) out[1][i] = T (oR);
            for (int c = 2; c < nOut; ++c) out[c][i] = T (0);
        }
    }

    static double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

private:
    void accumulate (int b, float iMn, float iMx, float oMn, float oMx)
    {
        auto& a = vizAcc[b];
        if (vizCount == 0) a = { iMn, iMx, oMn, oMx };
        else
        {
            a.inMin = std::min (a.inMin, iMn); a.inMax = std::max (a.inMax, iMx);
            a.outMin = std::min (a.outMin, oMn); a.outMax = std::max (a.outMax, oMx);
        }
    }

    bool prepared = false;
    double sampleRate = 48000.0;
    ChannelEngine engines[2];
    SpectralClipChannel specClip[2];
    IntDelay specVizIn[2];
    IntDelay deltaDelay[2];
    StereoCeiling ceiling;
    double detailSm = 1.0;
    int pendingMode = -1;
    double modeGain = 1.0, modeFadeStep = 0.002;
    double inGain = 1.0, outGain = 1.0, thr[kMaxBands] { 0.5, 0.5, 0.5, 0.5 };
    double smoothCoef = 0.001;
    IntDelay vizDelay[kMaxBands][4];
    VizEntry vizAcc[kMaxBands] {};
    int vizCount = 0;
};

} // namespace scalper
