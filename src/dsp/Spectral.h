#pragma once
// 400Scalper — Spectral mode: detail-preserving clipper.
//
// 1. Ear filter bank: short STFT (N ≈ 10.7 ms, hop N/4, periodic sqrt-Hann analysis and
//    synthesis windows, weighted overlap-add). Bins are grouped into 26 Mel-spaced bands
//    with triangular weights. Stereo-linked: both channels share one band analysis.
// 2. Per frame, bands within rangeDb (15 dB) of the loudest band are LOUD (6 dB soft edge,
//    instant attack, 60 ms release). The loud mask is widened ±3 bins so the window
//    leakage of a big low tone is not mistaken for detail.
// 3. D (detail) is resynthesised from the non-loud bins. Because the loud and detail
//    masks are complementary and sqrt-Hann² at hop N/4 sums to exactly 2, x = L + D
//    (perfect reconstruction, latency N-1 samples).
// 4. Clip (per channel, at 4x oversampling):
//       dD  = min(d·D, 0.8·T)                 (detail never takes more than 80% of T)
//       env = max(|dD|, env·release)          (instant attack, 15 ms release, env >= |dD|)
//       h   = T - env                         (room left for the loud part)
//       y   = dD + softclip(xd - dD, ceiling h)
//    => |y| <= |dD| + h <= T, below threshold y = xd, and the detail rides through the
//       clipped region untouched. The plugin's look-ahead ceiling follows as a safety net
//       for the oversampling filters' overshoot.

#include "Clipper.h"
#include "Oversampler.h"
#include "Engine.h"
#include <vector>
#include <array>
#include <complex>
#include <cmath>
#include <atomic>

namespace scalper {

// Minimal in-place radix-2 complex FFT.
class FFT
{
public:
    void prepare (int n)
    {
        N = n;
        rev.assign (size_t (n), 0);
        for (int i = 0, j = 0; i < n; ++i)
        {
            rev[size_t (i)] = j;
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
        }
        tw.resize (size_t (n / 2));
        for (int i = 0; i < n / 2; ++i) tw[size_t (i)] = std::polar (1.0, -2.0 * 3.14159265358979323846 * i / n);
    }
    void forward (std::complex<double>* a) const { run (a, false); }
    void inverse (std::complex<double>* a) const { run (a, true); for (int i = 0; i < N; ++i) a[i] /= double (N); }
private:
    void run (std::complex<double>* a, bool inv) const
    {
        for (int i = 0; i < N; ++i) if (i < rev[size_t (i)]) std::swap (a[i], a[rev[size_t (i)]]);
        for (int len = 2; len <= N; len <<= 1)
        {
            const int step = N / len, half = len / 2;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const auto w = inv ? std::conj (tw[size_t (j * step)]) : tw[size_t (j * step)];
                    const auto t = a[i + j + half] * w;
                    a[i + j + half] = a[i + j] - t;
                    a[i + j] += t;
                }
        }
    }
    int N = 0;
    std::vector<int> rev;
    std::vector<std::complex<double>> tw;
};

class SpectralSplitter
{
public:
    static constexpr int kBands = 26;
    static constexpr int kDisplayPoints = 128;   // loud mask on a log-frequency grid for the UI
    double rangeDb = 15.0;     // bands within this many dB of the loudest are "loud"

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        N = 1 << int (std::lround (std::log2 (0.0107 * fs)));    // 512 @ 44.1/48 kHz, 1024 @ 88.2/96 kHz
        H = N / 4;
        nb = N / 2 + 1;
        fft.prepare (N);
        win.resize (size_t (N));
        for (int i = 0; i < N; ++i) win[size_t (i)] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / N));

        // Mel bands 20 Hz .. min(20 kHz, 0.98 Nyquist), triangular weights per bin
        auto mel  = [] (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); };
        auto imel = [] (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); };
        const double fmax = std::min (20000.0, 0.49 * fs);
        double edges[kBands + 2];
        for (int i = 0; i < kBands + 2; ++i) edges[i] = imel (mel (20.0) + (mel (fmax) - mel (20.0)) * i / (kBands + 1));
        weights.assign (size_t (kBands), std::vector<double> (size_t (nb), 0.0));
        for (int b = 0; b < kBands; ++b)
        {
            const double lo = edges[b], c = edges[b + 1], hi = edges[b + 2];
            double s = 0.0;
            for (int k = 0; k < nb; ++k)
            {
                const double f = k * fs / N;
                double w = 0.0;
                if (f > lo && f < hi) w = f <= c ? (f - lo) / (c - lo) : (hi - f) / (hi - c);
                weights[size_t (b)][size_t (k)] = w;
                s += w;
            }
            if (s == 0.0) weights[size_t (b)][size_t (std::min (nb - 1, int (std::lround (c * N / fs))))] = 1.0; // narrower than a bin
            centers[size_t (b)] = c;
        }
        wsum.assign (size_t (nb), 0.0);
        for (int k = 0; k < nb; ++k)
        {
            double s = 0.0;
            for (int b = 0; b < kBands; ++b) s += weights[size_t (b)][size_t (k)];
            wsum[size_t (k)] = s > 0.0 ? s : 1.0;
        }
        binMask.assign (size_t (nb), 0.0);
        dil.assign (size_t (nb), 0.0);
        relPerHop = std::exp (-double (H) / (0.060 * fs));
        for (auto& c : ch) c.prepare (N, H);
        frame.assign (size_t (N), {});
        frame2.assign (size_t (N), {});
        reset();
    }

    void reset()
    {
        for (auto& c : ch) c.reset();
        mask.fill (0.0);
        bandDb.fill (-120.0);
        hop = 0;
        for (auto& m : displayMask) m.store (0.0f);
    }

    int latencySamples() const { return N - 1; }
    int fftSize() const { return N; }
    double sampleRate() const { return fs; }

    // One stereo frame in: returns the input delayed by the latency (xd) and the detail part (D).
    void process (double xl, double xr, double& xdl, double& xdr, double& dl, double& dr)
    {
        ch[0].push (xl); ch[1].push (xr);
        if (++hop >= H) { hop = 0; analyse(); ch[0].outPos = ch[1].outPos = 0; }   // new frame: read its first H samples next
        dl = ch[0].pop(); dr = ch[1].pop();
        xdl = ch[0].delayDry (xl); xdr = ch[1].delayDry (xr);
    }

    // UI: loud mask (0..1) sampled on a 20 Hz..20 kHz log grid
    std::array<std::atomic<float>, kDisplayPoints> displayMask {};

private:
    struct Chan
    {
        std::vector<double> in, acc, dry;
        int inPos = 0, outPos = 0, dryPos = 0, N = 512, H = 128;
        void prepare (int n, int h)
        {
            N = n; H = h;
            in.assign (size_t (N), 0.0);
            acc.assign (size_t (N + H), 0.0);
            dry.assign (size_t (N - 1), 0.0);
        }
        void reset()
        {
            std::fill (in.begin(), in.end(), 0.0);
            std::fill (acc.begin(), acc.end(), 0.0);
            std::fill (dry.begin(), dry.end(), 0.0);
            inPos = outPos = dryPos = 0;
        }
        void push (double x) { in[size_t (inPos)] = x; inPos = (inPos + 1) % N; }
        double pop()
        {
            const double d = acc[size_t (outPos)];
            if (++outPos == H)
            {
                std::copy (acc.begin() + H, acc.end(), acc.begin());
                std::fill (acc.begin() + N, acc.end(), 0.0);
                outPos = 0;
            }
            return d;
        }
        double delayDry (double x)
        {
            const double y = dry[size_t (dryPos)];
            dry[size_t (dryPos)] = x;
            if (++dryPos >= N - 1) dryPos = 0;
            return y;
        }
    };

    void analyse()
    {
        // windowed frames for both channels
        for (int i = 0; i < N; ++i)
        {
            frame[size_t (i)]  = { ch[0].in[size_t ((ch[0].inPos + i) % N)] * win[size_t (i)], 0.0 };
            frame2[size_t (i)] = { ch[1].in[size_t ((ch[1].inPos + i) % N)] * win[size_t (i)], 0.0 };
        }
        fft.forward (frame.data());
        fft.forward (frame2.data());

        // stereo-linked band levels
        double mx = -120.0;
        for (int b = 0; b < kBands; ++b)
        {
            const auto& w = weights[size_t (b)];
            double e = 0.0, s = 0.0;
            for (int k = 0; k < nb; ++k)
                if (w[size_t (k)] != 0.0)
                {
                    e += w[size_t (k)] * (std::norm (frame[size_t (k)]) + std::norm (frame2[size_t (k)]));
                    s += w[size_t (k)];
                }
            bandDb[size_t (b)] = 10.0 * std::log10 (e / (s > 0.0 ? s : 1.0) + 1e-20);
            mx = std::max (mx, bandDb[size_t (b)]);
        }
        for (int b = 0; b < kBands; ++b)
        {
            const double t = std::clamp ((bandDb[size_t (b)] - (mx - rangeDb)) / 6.0 + 0.5, 0.0, 1.0);
            mask[size_t (b)] = t > mask[size_t (b)] ? t : mask[size_t (b)] * relPerHop + t * (1.0 - relPerHop);
        }
        for (int k = 0; k < nb; ++k)
        {
            double m = 0.0;
            for (int b = 0; b < kBands; ++b) m += weights[size_t (b)][size_t (k)] * mask[size_t (b)];
            binMask[size_t (k)] = m / wsum[size_t (k)];
        }
        for (int k = 0; k < nb; ++k)
        {
            double m = 0.0;
            for (int j = std::max (0, k - 3); j <= std::min (nb - 1, k + 3); ++j) m = std::max (m, binMask[size_t (j)]);
            dil[size_t (k)] = m;
        }

        // keep only the detail bins, restore Hermitian symmetry, resynthesise both channels
        auto resynth = [&] (std::vector<std::complex<double>>& X, Chan& c)
        {
            for (int k = 0; k < nb; ++k)
            {
                X[size_t (k)] *= (1.0 - dil[size_t (k)]);
                if (k > 0 && k < N / 2) X[size_t (N - k)] = std::conj (X[size_t (k)]);
            }
            fft.inverse (X.data());
            for (int i = 0; i < N; ++i) c.acc[size_t (i)] += X[size_t (i)].real() * win[size_t (i)] * 0.5; // sqrt-Hann², hop N/4 -> sum 2
        };
        resynth (frame, ch[0]);
        resynth (frame2, ch[1]);

        // display mask on a log grid
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, double (i) / (kDisplayPoints - 1));
            const double kf = f * N / fs;
            const int k0 = std::min (nb - 1, int (kf)), k1 = std::min (nb - 1, k0 + 1);
            const double u = kf - std::floor (kf);
            displayMask[size_t (i)].store (float (dil[size_t (k0)] * (1.0 - u) + dil[size_t (k1)] * u), std::memory_order_relaxed);
        }
    }

    double fs = 48000.0;
    int N = 512, H = 128, nb = 257, hop = 0;
    FFT fft;
    std::vector<double> win, wsum, binMask, dil;
    std::vector<std::vector<double>> weights;
    std::array<double, kBands> centers {}, mask {}, bandDb {};
    double relPerHop = 0.9;
    std::array<Chan, 2> ch;
    std::vector<std::complex<double>> frame, frame2;
};

// Per-channel spectral clipper at 4x. The detail path goes through the same linear
// stages as the clipper's linear region (upsample, 2-point average = ADAA's linear
// response, droop comp, downsample) so the two parts recombine exactly below threshold.
class SpectralClipChannel
{
public:
    void prepare (double fs)
    {
        osR.reset(); osD.reset(); clip.reset(); comp.reset(); d1 = 0.0; env = 0.0;
        comp.prepare (fs * Oversampler4x::factor);
        envRel = std::exp (-1.0 / (0.015 * fs));
        // The upsampler delays r by ~16.75 base samples before it reaches the clipper, while h
        // is computed now. Taking the minimum h over that window means the clipper never
        // uses a ceiling looser than the one that belongs to the sample it is clipping.
        hHist.assign (18, 1.0); hPos = 0;
    }
    void reset() { osR.reset(); osD.reset(); clip.reset(); comp.reset(); d1 = 0.0; env = 0.0; std::fill (hHist.begin(), hHist.end(), 1.0); hPos = 0; }

    // xd: delayed input, D: detail, T: threshold (linear), knee 0..1, detail 0..1
    double process (double xd, double D, double T, double knee, double detail)
    {
        double dD = detail * D;
        const double cap = 0.8 * T;
        if (std::fabs (dD) > cap) dD = dD > 0 ? cap : -cap;
        env = std::max (std::fabs (dD), env * envRel);
        const double h = T - env;                      // env <= 0.8T, so h >= 0.2T
        // use the smallest h seen over the delay window -> never looser than the aligned value
        hHist[size_t (hPos)] = h; hPos = (hPos + 1) % int (hHist.size());
        double hUse = h;
        for (double v : hHist) hUse = std::min (hUse, v);
        curve.set (hUse, knee);

        const double r = xd - dD;
        double u[4], v[4];
        osR.up (r, u);
        osD.up (dD, v);
        for (int i = 0; i < 4; ++i)
        {
            const double a = clip.processVarying (curve, u[i]);
            const double b = 0.5 * (v[i] + d1);        // same linear response as ADAA
            d1 = v[i];
            u[i] = comp.process (a + b);
        }
        return osR.down (u);
    }

private:
    Oversampler4x osR, osD;
    AdaaClipper clip;
    DroopComp comp;
    ClipCurve curve;
    std::vector<double> hHist;
    int hPos = 0;
    double d1 = 0.0, env = 0.0, envRel = 0.999;
};

} // namespace scalper
