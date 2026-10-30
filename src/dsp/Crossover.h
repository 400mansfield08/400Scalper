#pragma once
// 400Scalper — multiband splitter (up to 4 bands / 3 crossovers).
//
// Slopes:
//   6 dB/oct  : first-order complementary split (low = LP1, high = x - low). Sums exactly.
//   12 dB/oct : Linkwitz-Riley 2 (two cascaded 1st-order Butterworth). High band is
//               polarity-inverted so low + high = first-order allpass.
//   24 dB/oct : Linkwitz-Riley 4 (two cascaded 2nd-order Butterworth, Q = 1/sqrt2).
//               low + high = second-order allpass.
// For LR2/LR4 the tree splits the remainder at each crossover, and every band already
// split off below gets a matching allpass so all bands stay phase-aligned and sum flat
// (magnitude) to the input.

#include <cmath>
#include <array>
#include <algorithm>

namespace scalper {

constexpr int kMaxBands = 4;
constexpr int kMaxXovers = kMaxBands - 1;

// Topology-preserving-transform one-pole (gives LP and HP from the same state).
struct OnePole
{
    double g = 0.0, s = 0.0;
    void setCutoff (double f, double fs) { const double t = std::tan (3.14159265358979323846 * f / fs); g = t / (1.0 + t); }
    void reset() { s = 0.0; }
    double lp (double x) { const double v = (x - s) * g; const double y = v + s; s = y + v; return y; }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    enum Type { LP, HP, AP };
    void set (Type t, double f, double fs, double Q = 0.70710678118654752)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
        const double cs = std::cos (w0), sn = std::sin (w0), al = sn / (2.0 * Q);
        const double a0 = 1.0 + al;
        double nb0, nb1, nb2;
        switch (t)
        {
            case LP: nb0 = (1 - cs) / 2; nb1 = 1 - cs;     nb2 = (1 - cs) / 2; break;
            case HP: nb0 = (1 + cs) / 2; nb1 = -(1 + cs);  nb2 = (1 + cs) / 2; break;
            default: nb0 = 1 - al;       nb1 = -2 * cs;    nb2 = 1 + al;       break;
        }
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0;
        a1 = -2 * cs / a0; a2 = (1 - al) / a0;
    }
};

// One crossover point: lowpass/highpass pair plus allpass sections for the bands below.
struct CrossoverPoint
{
    // 6 dB
    OnePole p6;
    // 12 dB (LR2): two cascaded one-poles for LP, two for HP
    OnePole lp12a, lp12b, hp12a, hp12b;
    std::array<OnePole, kMaxBands> ap12;   // first-order allpass = lp - hp
    // 24 dB (LR4)
    Biquad lp24a, lp24b, hp24a, hp24b;
    std::array<Biquad, kMaxBands> ap24;

    void set (double f, double fs)
    {
        p6.setCutoff (f, fs);
        lp12a.setCutoff (f, fs); lp12b.setCutoff (f, fs); hp12a.setCutoff (f, fs); hp12b.setCutoff (f, fs);
        for (auto& a : ap12) a.setCutoff (f, fs);
        lp24a.set (Biquad::LP, f, fs); lp24b.set (Biquad::LP, f, fs);
        hp24a.set (Biquad::HP, f, fs); hp24b.set (Biquad::HP, f, fs);
        for (auto& a : ap24) a.set (Biquad::AP, f, fs);
    }

    void reset()
    {
        p6.reset(); lp12a.reset(); lp12b.reset(); hp12a.reset(); hp12b.reset();
        for (auto& a : ap12) a.reset();
        lp24a.reset(); lp24b.reset(); hp24a.reset(); hp24b.reset();
        for (auto& a : ap24) a.reset();
    }
};

struct MultibandSplitter
{
    std::array<CrossoverPoint, kMaxXovers> pts;
    std::array<double, kMaxXovers> freqs { 120.0, 1000.0, 6000.0 };
    double fs = 48000.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int i = 0; i < kMaxXovers; ++i) pts[size_t (i)].set (freqs[size_t (i)], fs);
        reset();
    }
    void reset() { for (auto& p : pts) p.reset(); }

    void setFreq (int i, double f)
    {
        f = std::clamp (f, 20.0, 0.45 * fs);
        if (std::fabs (f - freqs[size_t (i)]) < 1.0e-3) return;
        freqs[size_t (i)] = f;
        pts[size_t (i)].set (f, fs);
    }

    // slopeDb: 6, 12 or 24. numBands 1..4. Writes numBands outputs.
    void process (double x, int numBands, int slopeDb, double* bands)
    {
        if (numBands <= 1) { bands[0] = x; return; }
        double rem = x;
        for (int i = 0; i < numBands - 1; ++i)
        {
            auto& p = pts[size_t (i)];
            double lo, hi;
            if (slopeDb == 6)
            {
                lo = p.p6.lp (rem);
                hi = rem - lo;
            }
            else if (slopeDb == 12)
            {
                lo = p.lp12b.lp (p.lp12a.lp (rem));
                const double h1 = rem - p.hp12a.lp (rem);
                const double h2 = h1 - p.hp12b.lp (h1);
                hi = -h2; // LR2: invert high band so low + high = allpass
                for (int j = 0; j < i; ++j)
                {
                    const double l = p.ap12[size_t (j)].lp (bands[j]);
                    bands[j] = l - (bands[j] - l); // first-order allpass: lp - hp
                }
            }
            else
            {
                lo = p.lp24b.process (p.lp24a.process (rem));
                hi = p.hp24b.process (p.hp24a.process (rem));
                for (int j = 0; j < i; ++j)
                    bands[j] = p.ap24[size_t (j)].process (bands[j]);
            }
            bands[i] = lo;
            rem = hi;
        }
        bands[numBands - 1] = rem;
    }
};

} // namespace scalper
