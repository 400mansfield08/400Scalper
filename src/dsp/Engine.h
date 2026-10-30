#pragma once
// 400Scalper — per-channel processing engine.
//
// Signal flow (per channel):
//   x * inGain -> [multiband split] -> per band: 4x up -> ADAA soft-knee clip -> droop comp
//   -> 4x down -> sum (respecting solo) -> [single band: final clamp at threshold] -> * outGain
//
// All bands go through identical oversampling, so they stay time-aligned.

#include "Clipper.h"
#include "Oversampler.h"
#include "Crossover.h"
#include <vector>
#include <cmath>

namespace scalper {

struct IntDelay
{
    std::vector<float> buf;
    int pos = 0;
    void init (int n) { buf.assign (size_t (std::max (1, n)), 0.0f); pos = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    float process (float x)
    {
        const float y = buf[size_t (pos)];
        buf[size_t (pos)] = x;
        if (++pos >= int (buf.size())) pos = 0;
        return y;
    }
};

struct BandSettings
{
    double thresholdLin = 0.5;
    double knee01 = 0.3;
    bool audible = true; // false when another band is soloed
};

struct BandChannel
{
    Oversampler4x os;
    AdaaClipper clip;
    DroopComp comp;
    IntDelay vizInDelay;

    void prepare (double fs, int vizDelay)
    {
        os.reset(); clip.reset(); comp.reset();
        comp.prepare (fs * Oversampler4x::factor);
        vizInDelay.init (vizDelay);
    }
    void reset() { os.reset(); clip.reset(); comp.reset(); vizInDelay.reset(); }

    double process (const ClipCurve& curve, double x)
    {
        double up[4];
        os.up (x, up);
        for (double& v : up) v = comp.process (clip.process (curve, v));
        return os.down (up);
    }
};

class ChannelEngine
{
public:
    // Total latency in base-rate samples (rounded): oversampler round trip 33.5,
    // ADAA half-sample + droop FIR one sample at the 4x rate (0.375) -> 33.875 -> 34.
    static int latencySamples() { return 34; }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        split.prepare (fs);
        for (auto& b : bands) b.prepare (fs, latencySamples());
    }

    void reset()
    {
        split.reset();
        for (auto& b : bands) b.reset();
    }

    void setCrossover (int i, double hz) { split.setFreq (i, hz); }

    // Processes one sample. bandIn / bandOut receive per-band pre-clip (latency-aligned)
    // and post-clip values for the visualiser.
    double process (double x, int numBands, int slopeDb, const ClipCurve* curves,
                    const BandSettings* settings, float* bandIn, float* bandOut)
    {
        double split_[kMaxBands];
        split.process (x, numBands, slopeDb, split_);
        double sum = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            const double y = bands[size_t (b)].process (curves[b], split_[b]);
            bandIn[b]  = bands[size_t (b)].vizInDelay.process (float (split_[b]));
            bandOut[b] = float (y);
            if (settings[b].audible) sum += y;
        }
        return sum;
    }

private:
    double fs = 48000.0;
    MultibandSplitter split;
    std::array<BandChannel, kMaxBands> bands;
};

// Stereo-linked look-ahead peak ceiling.
//
// Band-limited clipping overshoots the threshold after downsampling (Gibbs ripple and the
// removed ultrasonic content of clipped noise; +2..4 dB on drums). A hard clamp would put
// back the aliasing the oversampler removed, so instead this stage rides the gain down
// smoothly ahead of each overshoot:
//   target[n] = min(1, T / peak[n]);  m = sliding min over L+1 samples;
//   r = m with a release recovery;   g = moving average of r over L samples;
//   out[n] = x[n-L] * g[n]   ->  g[n] <= target[n-L], so |out| <= T is guaranteed.
// When disabled it is a pure L-sample delay, so latency never changes.
class StereoCeiling
{
public:
    void prepare (double sampleRate)
    {
        L = std::max (8, int (std::lround (0.0007 * sampleRate))); // ~0.7 ms look-ahead
        relCoef = 1.0 - std::exp (-1.0 / (0.050 * sampleRate));   // 50 ms release
        tgt.assign (size_t (L + 1), 1.0);
        avg.assign (size_t (L), 1.0);
        dl.assign (size_t (L), 0.0); dr.assign (size_t (L), 0.0);
        reset();
    }

    void reset()
    {
        std::fill (tgt.begin(), tgt.end(), 1.0);
        std::fill (avg.begin(), avg.end(), 1.0);
        std::fill (dl.begin(), dl.end(), 0.0);
        std::fill (dr.begin(), dr.end(), 0.0);
        tp = ap = dp = 0; avgSum = double (L); r = 1.0; gain = 1.0;
    }

    int latencySamples() const { return L; }
    double currentGain() const { return gain; }

    // Processes one stereo frame in place. T = ceiling (linear). enabled=false -> delay only.
    void process (double& l, double& rr, double T, bool enabled)
    {
        const double peak = std::max (std::fabs (l), std::fabs (rr));
        const double target = (enabled && peak > T) ? T / peak : 1.0;

        tgt[size_t (tp)] = target;
        if (++tp > L) tp = 0;
        double m = 1.0;
        for (double t : tgt) m = std::min (m, t);   // L is small (<= ~140 at 192 kHz)

        r = std::min (m, r + (1.0 - r) * relCoef);

        avgSum += r - avg[size_t (ap)];
        avg[size_t (ap)] = r;
        if (++ap >= L) ap = 0;
        gain = std::min (1.0, avgSum / double (L));

        const double xl = dl[size_t (dp)], xr = dr[size_t (dp)];
        dl[size_t (dp)] = l; dr[size_t (dp)] = rr;
        if (++dp >= L) dp = 0;

        l = xl * gain; rr = xr * gain;
        if (enabled) { l = std::clamp (l, -T, T); rr = std::clamp (rr, -T, T); } // rounding guard only
    }

private:
    int L = 34;
    double relCoef = 0.0005;
    std::vector<double> tgt, avg, dl, dr;
    int tp = 0, ap = 0, dp = 0;
    double avgSum = 0.0, r = 1.0, gain = 1.0;
};

} // namespace scalper
