// 400Scalper DSP tests: latency, transparency below threshold, ceiling, multiband sum.
#include "../src/dsp/Engine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <complex>

using namespace scalper;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   " __VA_ARGS__); std::printf ("\n"); } } while (0)

static const double PI = 3.14159265358979323846;

// Gain (dB) and phase of y relative to x at frequency f, via single-bin DFT over the tail.
static std::complex<double> bin (const std::vector<double>& v, double f, double fs, size_t start)
{
    // Blackman-Harris windowed single-bin DFT (low leakage)
    std::complex<double> acc {};
    const double N = double (v.size() - start);
    for (size_t n = start; n < v.size(); ++n)
    {
        const double t = double (n - start) / N;
        const double w = 0.35875 - 0.48829 * std::cos (2 * PI * t) + 0.14128 * std::cos (4 * PI * t) - 0.01168 * std::cos (6 * PI * t);
        acc += w * v[n] * std::polar (1.0, -2.0 * PI * f * double (n) / fs);
    }
    return acc;
}

static double gainDb (const std::vector<double>& in, const std::vector<double>& out, double f, double fs)
{
    const size_t start = out.size() / 2;
    return 20.0 * std::log10 (std::abs (bin (out, f, fs, start)) / std::abs (bin (in, f, fs, start)));
}

static double tw_ = 0.0;

int main()
{
    const double fs = 48000.0;

    // 1. Oversampler alone: unity gain across the band, latency 33.5 samples.
    {
        for (double f : { 100.0, 1000.0, 10000.0, 18000.0, 20000.0 })
        {
            Oversampler4x os;
            std::vector<double> in (16384), out (16384);
            for (size_t n = 0; n < in.size(); ++n)
            {
                in[n] = 0.5 * std::sin (2 * PI * f * double (n) / fs);
                double up[4]; os.up (in[n], up); out[n] = os.down (up);
            }
            const double g = gainDb (in, out, f, fs);
            CHECK (std::fabs (g) < (f <= 18000 ? 0.05 : 0.3), "oversampler gain at %.0f Hz = %+.4f dB", f, g);
        }
        Oversampler4x os;
        std::vector<double> out (200);
        for (size_t n = 0; n < out.size(); ++n) { double up[4]; os.up (n == 0 ? 1.0 : 0.0, up); out[n] = os.down (up); }
        size_t pk = 0; for (size_t n = 1; n < out.size(); ++n) if (std::fabs (out[n]) > std::fabs (out[pk])) pk = n;
        // Linear phase -> symmetric impulse centred on 33.5: the two largest taps are 33 and 34.
        CHECK (pk == 33 || pk == 34, "oversampler impulse peak at %zu (expected 33/34, latency %.1f)", pk, os.latency());
    }

    // 2. Full engine below threshold is transparent (single band).
    {
        for (double f : { 50.0, 1000.0, 10000.0, 18000.0 })
        {
            ChannelEngine eng; eng.prepare (fs);
            ClipCurve c[4]; c[0].set (1.0, 0.3);
            BandSettings s[4];
            float bi[4], bo[4];
            std::vector<double> in (16384), out (16384);
            for (size_t n = 0; n < in.size(); ++n)
            {
                in[n] = 0.25 * std::sin (2 * PI * f * double (n) / fs); // -12 dBFS, threshold 0 dBFS, knee starts at 0.7
                out[n] = eng.process (in[n], 1, 24, c, s, bi, bo, tw_);
            }
            const double g = gainDb (in, out, f, fs);
            CHECK (std::fabs (g) < 0.1, "engine transparent at %.0f Hz: %+.4f dB", f, g);
        }
    }

    // 3. Ceiling: hot signal never exceeds threshold in single-band mode (clipper + look-ahead ceiling).
    {
        for (double knee : { 0.0, 0.3, 1.0 })
        {
            ChannelEngine eng; eng.prepare (fs);
            StereoCeiling ceil; ceil.prepare (fs);
            ClipCurve c[4]; c[0].set (0.5, knee);
            BandSettings s[4];
            float bi[4], bo[4];
            double mx = 0.0;
            unsigned seed = 1;
            for (int n = 0; n < 96000; ++n)
            {
                seed = seed * 1103515245u + 12345u;
                const double noise = ((seed >> 9) / double (1u << 23) - 0.5);
                const double x = 2.0 * std::sin (2 * PI * 997.0 * n / fs) + 0.5 * std::sin (2 * PI * 7300.0 * n / fs) + noise;
                double l = eng.process (x, 1, 24, c, s, bi, bo, tw_), r = l;
                ceil.process (l, r, 0.5, true);
                mx = std::max (mx, std::fabs (l));
            }
            CHECK (mx <= 0.5 + 1e-12, "ceiling holds (knee %.0f%%): peak %.9f <= 0.5", knee * 100, mx);
        }
    }

    // 4. Multiband sums flat when nothing clips.
    for (int slope : { 6, 12, 24 })
    {
        double worst = 0.0;
        for (double f : { 40.0, 110.0, 130.0, 500.0, 1000.0, 3000.0, 6000.0, 12000.0 })
        {
            ChannelEngine eng; eng.prepare (fs);
            eng.setCrossover (0, 120.0); eng.setCrossover (1, 1000.0); eng.setCrossover (2, 6000.0);
            ClipCurve c[4]; for (auto& cc : c) cc.set (4.0, 0.0);
            BandSettings s[4];
            float bi[4], bo[4];
            std::vector<double> in (32768), out (32768);
            for (size_t n = 0; n < in.size(); ++n)
            {
                in[n] = 0.3 * std::sin (2 * PI * f * double (n) / fs);
                out[n] = eng.process (in[n], 4, slope, c, s, bi, bo, tw_);
            }
            worst = std::max (worst, std::fabs (gainDb (in, out, f, fs)));
        }
        CHECK (worst < 0.1, "4-band %d dB/oct sums flat: worst %.4f dB", slope, worst);
    }

    // 5. Aliasing: hard clip of a 9.1 kHz sine driven 12 dB past the threshold. Measure the
    //    audible aliased components (5th/7th/9th/11th harmonics folded back) vs. a naive clipper.
    {
        const double f = 9100.0;
        auto aliasLevel = [&] (int mode) {
            ChannelEngine eng; eng.prepare (fs);
            StereoCeiling ceil; ceil.prepare (fs);
            ClipCurve c[4]; c[0].set (0.25, 0.0);
            BandSettings s[4]; float bi[4], bo[4];
            std::vector<double> out (65536);
            for (size_t n = 0; n < out.size(); ++n)
            {
                const double x = 1.0 * std::sin (2 * PI * f * double (n) / fs);
                if (mode == 0) { out[n] = std::clamp (x, -0.25, 0.25); continue; }
                double l = eng.process (x, 1, 24, c, s, bi, bo, tw_), r = l;
                ceil.process (l, r, 0.25, mode == 2);
                out[n] = l;
            }
            double worst = -300;
            for (double a : { 2500.0, 4100.0, 14100.0, 15700.0 })
                worst = std::max (worst, 20 * std::log10 (std::abs (bin (out, a, fs, out.size() / 2)) / (out.size() / 2 * 0.35875 / 2) + 1e-15));
            return worst;
        };
        const double naive = aliasLevel (0), noCeil = aliasLevel (1), withCeil = aliasLevel (2);
        std::printf ("      aliasing: naive %.1f dB | 400Scalper %.1f dB | with ceiling %.1f dB\n", naive, noCeil, withCeil);
        CHECK (withCeil < -80.0, "aliasing with ceiling below -80 dB (%.1f dB)", withCeil);
    }

    std::printf (failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
