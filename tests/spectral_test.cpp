// 400Scalper — Spectral mode tests.
#include "../src/dsp/Spectral.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <complex>

using namespace scalper;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   " __VA_ARGS__); std::printf ("\n"); } } while (0)
static const double PI = 3.14159265358979323846, fs = 48000.0;

static std::complex<double> bin (const std::vector<double>& v, double f, size_t start)
{
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
static double ampOf (const std::vector<double>& v, double f) { const size_t s = v.size() / 2; return 2.0 * std::abs (bin (v, f, s)) / (double (v.size() - s) * 0.35875); }

struct Chain
{
    SpectralSplitter split;
    SpectralClipChannel cl, cr;
    StereoCeiling ceil;
    double pre = 0.0;   // peak before the look-ahead ceiling
    Chain() { split.prepare (fs); cl.prepare (fs); cr.prepare (fs); ceil.prepare (fs); }
    double run (double x, double T, double knee, double detail)
    {
        double xdl, xdr, dl, dr;
        split.process (x, x, xdl, xdr, dl, dr);
        double yl = cl.process (xdl, dl, T, knee, detail);
        double yr = cr.process (xdr, dr, T, knee, detail);
        pre = std::max (pre, std::fabs (yl));
        ceil.process (yl, yr, T, true);
        return yl;
    }
};

int main()
{
    // 1. Perfect reconstruction of the split (everything treated as detail -> D == x delayed)
    {
        SpectralSplitter s; s.prepare (fs); s.rangeDb = -1000.0;
        std::vector<double> xs, ds;
        for (int n = 0; n < 30000; ++n)
        {
            const double x = 0.5 * std::sin (n * 0.031) + 0.2 * std::sin (n * 0.7) + 0.05 * std::sin (n * 2.9);
            double xdl, xdr, dl, dr;
            s.process (x, x, xdl, xdr, dl, dr);
            xs.push_back (x); ds.push_back (dl);
        }
        const int L = s.latencySamples();
        double e = 0;
        for (size_t n = 8000; n < xs.size(); ++n) e = std::max (e, std::fabs (ds[n] - xs[n - size_t (L)]));
        CHECK (e < 1e-9, "split reconstructs perfectly at %d samples (%.2f ms): err %.1e", L, 1000.0 * L / fs, e);
    }

    // 2. Below threshold the whole spectral chain is transparent (loud + detail paths aligned)
    for (double f : { 60.0, 1000.0, 4000.0, 10000.0, 18000.0 })
    {
        Chain c;
        std::vector<double> in, out;
        for (int n = 0; n < 40000; ++n)
        {
            // two tones so there is always a loud part and a detail part
            const double x = 0.3 * std::sin (2 * PI * 80.0 * n / fs) + 0.05 * std::sin (2 * PI * f * n / fs);
            in.push_back (x);
            out.push_back (c.run (x, 1.0, 0.3, 1.0));
        }
        const double g = 20 * std::log10 (ampOf (out, f) / 0.05);
        CHECK (std::fabs (g) < 0.1, "transparent below threshold at %.0f Hz: %+.3f dB", f, g);
    }

    // 3. Detail preservation: 4 kHz at 0.05 riding on a 60 Hz tone hard-clipped at 0.4
    {
        auto run = [&] (double detail) {
            Chain c; std::vector<double> out;
            for (int n = 0; n < 48000; ++n)
                out.push_back (c.run (std::sin (2 * PI * 60 * n / fs) + 0.05 * std::sin (2 * PI * 4000 * n / fs), 0.4, 0.0, detail));
            return ampOf (out, 4000.0) / 0.05;
        };
        const double plain = run (0.0), spectral = run (1.0);
        CHECK (spectral > 0.9 && plain < 0.4, "4 kHz detail kept: DETAIL 0%% -> %.0f%%, DETAIL 100%% -> %.0f%%", 100 * plain, 100 * spectral);
    }

    // 4. Ceiling + no spikes on a drum-like signal, driven hard, several knee/detail settings
    {
        std::vector<double> sig (size_t (fs * 3));
        unsigned seed = 1234567;
        double hp = 0, prev = 0;
        for (size_t i = 0; i < sig.size(); ++i)
        {
            const double t = i / fs; const int beat = int (t / 0.5); const double tk = t - beat * 0.5;
            const double kick = (beat % 4 == 0 ? 1.05 : 0.9) * std::exp (-tk * 7) * std::sin (2 * PI * (45 * tk + 80 * (1 - std::exp (-30 * tk)) / 30));
            seed = seed * 1103515245u + 12345u; const double nn = ((seed >> 9) / double (1u << 23)) * 2 - 1;
            hp = 0.6 * (hp + nn - prev); prev = nn;
            const double snare = beat % 2 == 1 ? 0.55 * nn * std::exp (-tk * 16) + 0.3 * std::sin (2 * PI * 190 * tk) * std::exp (-tk * 24) : 0;
            sig[i] = kick + snare + 0.22 * hp * std::exp (-std::fmod (t, 0.25) * 45) + 0.22 * std::sin (2 * PI * 55 * t) * (1 - 0.85 * std::exp (-tk * 7));
        }
        for (double knee : { 0.0, 0.3, 1.0 })
            for (double detail : { 0.0, 1.0 })
            {
                Chain c; double peak = 0;
                for (double x : sig) peak = std::max (peak, std::fabs (c.run (x * 4.0, 0.5, knee, detail)));   // +12 dB
                CHECK (peak <= 0.5 + 1e-12 && c.pre < 0.5 * 1.6, "knee %3.0f%% detail %3.0f%%: output peak %.6f <= 0.5, before ceiling %.3f (no spikes)", knee * 100, detail * 100, peak, c.pre);
            }
    }

    // 5. Aliasing stays low (9.1 kHz sine driven 12 dB over a hard threshold)
    {
        Chain c; std::vector<double> out;
        for (int n = 0; n < 65536; ++n) out.push_back (c.run (std::sin (2 * PI * 9100.0 * n / fs), 0.25, 0.0, 1.0));
        double worst = -300;
        for (double a : { 2500.0, 4100.0, 14100.0, 15700.0 }) worst = std::max (worst, 20 * std::log10 (ampOf (out, a) + 1e-15));
        CHECK (worst < -80.0, "audible aliasing %.1f dB (below -80 dB)", worst);
    }

    std::printf (failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
