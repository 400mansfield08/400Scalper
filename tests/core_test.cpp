// 400Scalper — engine-level tests of the format-agnostic Processor (what the plugin runs).
#include "core/Processor.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace scalper;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); ++failures; } else { std::printf ("ok:   " __VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kPi = 3.14159265358979323846;
static constexpr double fs = 48000.0;
static constexpr int N = 512;

struct Rig
{
    Processor proc;
    ParamValues p;
    std::vector<double> inL = std::vector<double> (N), inR = std::vector<double> (N), outL = std::vector<double> (N), outR = std::vector<double> (N);
    void prepare() { proc.prepare (fs, p); }
    template <typename Gen> void block (Gen&& gen)
    {
        for (int i = 0; i < N; ++i) gen (i, inL[size_t (i)], inR[size_t (i)]);
        const double* ins[2] = { inL.data(), inR.data() };
        double* outs[2] = { outL.data(), outR.data() };
        proc.process (ins, 2, outs, 2, N, p);
    }
};

int main()
{
    // ---- latency ----
    {
        Rig r; r.prepare();
        CHECK (r.proc.latencySamples (0) == 68, "Sample latency %d samples (%.2f ms)", r.proc.latencySamples (0), 1000.0 * r.proc.latencySamples (0) / fs);
        CHECK (r.proc.latencySamples (1) == 68 + 511, "Spectral latency %d samples (%.2f ms)", r.proc.latencySamples (1), 1000.0 * r.proc.latencySamples (1) / fs);
    }

    // ---- driven sine stays at the threshold ----
    {
        Rig r; r.p[kInGain] = 12.0; r.prepare();
        double peak = 0, ph = 0;
        for (int b = 0; b < 400; ++b)
        {
            r.block ([&] (int, double& l, double& rr) {
                const double x = 0.9 * std::sin (ph) + 0.1 * std::sin (7.3 * ph);
                ph += 2 * kPi * 220.0 / fs;
                l = x; rr = 0.8 * x; });
            if (b > 20) for (int i = 0; i < N; ++i) peak = std::max (peak, std::max (std::fabs (r.outL[size_t (i)]), std::fabs (r.outR[size_t (i)])));
        }
        CHECK (peak <= 0.50119 && peak > 0.45, "driven output peak %.5f stays at the -6 dB threshold (0.50119)", peak);
    }

    // ---- float buffers and mono input work too ----
    {
        Processor proc; ParamValues p; proc.prepare (fs, p);
        std::vector<float> in (N, 0.25f), out0 (N), out1 (N);
        const float* ins[1] = { in.data() };
        float* outs[2] = { out0.data(), out1.data() };
        for (int b = 0; b < 10; ++b) proc.process (ins, 1, outs, 2, N, p);
        CHECK (std::fabs (out0[N - 1] - 0.25f) < 1e-3 && std::fabs (out1[N - 1] - 0.25f) < 1e-3, "mono float in -> stereo out passes a quiet DC (%.4f, %.4f)", out0[N - 1], out1[N - 1]);
    }

    // ---- Spectral keeps more quiet detail under heavy clipping; mode switch is immediate ----
    {
        Rig r; r.p[kInGain] = 6.0; r.prepare();
        auto hfKept = [&] (int mode) {
            r.p[kMode] = mode;
            double a = 0, b = 0, ph = 0; long cnt = 0;
            for (int blk = 0; blk < 200; ++blk)
            {
                r.block ([&] (int, double& l, double& rr) {
                    const double x = 0.9 * std::sin (ph) + 0.03 * std::sin (62.5 * ph);   // 80 Hz + 5 kHz
                    ph += 2 * kPi * 80.0 / fs;
                    l = rr = x; });
                if (blk >= 100)
                    for (int i = 0; i < N; ++i, ++cnt)
                    {
                        const double t = 2 * kPi * 5000.0 * double (cnt) / fs;
                        a += r.outL[size_t (i)] * std::cos (t); b += r.outL[size_t (i)] * std::sin (t);
                    }
            }
            return 2.0 * std::hypot (a, b) / double (cnt);
        };
        const double kS = hfKept (0); const int pmS = r.proc.procMode.load();
        const double kP = hfKept (1); const int pmP = r.proc.procMode.load();
        CHECK (pmS == 0 && pmP == 1, "engine follows the Mode parameter (sample=%d, spectral=%d)", pmS, pmP);
        CHECK (kP > 2.0 * kS, "quiet 5 kHz detail: Sample %.4f vs Spectral %.4f (%.1f dB more)", kS, kP, 20 * std::log10 (kP / kS));
    }

    // ---- Spectral output respects the threshold ----
    {
        Rig r; r.p[kInGain] = 12.0; r.p[kMode] = 1; r.prepare();
        double peak = 0, ph = 0;
        for (int b = 0; b < 300; ++b)
        {
            r.block ([&] (int, double& l, double& rr) {
                const double x = 0.9 * std::sin (ph) + 0.05 * std::sin (40.0 * ph);
                ph += 2 * kPi * 110.0 / fs;
                l = rr = x; });
            if (b > 20) for (int i = 0; i < N; ++i) peak = std::max (peak, std::fabs (r.outL[size_t (i)]));
        }
        CHECK (peak <= 0.50119 + 1e-5 && peak > 0.15, "spectral output peak %.5f within the threshold (0.50119)", peak);
        bool anyMask = false;
        for (auto& m : r.proc.spectral.displayMask) if (m.load() > 0.5f) anyMask = true;
        CHECK (anyMask, "spectral display mask marks the loud region");
    }

    // ---- DELTA ----
    for (int mode : { 0, 1 })
    {
        auto deltaPeak = [&] (double amp) {
            Rig r; r.p[kMode] = mode; r.p[kDelta] = 1; r.prepare();
            double pk = 0, ph = 0;
            for (int b = 0; b < 200; ++b)
            {
                r.block ([&] (int, double& l, double& rr) {
                    const double x = amp * (0.7 * std::sin (ph) + 0.3 * std::sin (37.0 * ph));
                    ph += 2 * kPi * 80.0 / fs;
                    l = rr = x; });
                if (b >= 100) for (int i = 0; i < N; ++i) pk = std::max (pk, std::fabs (r.outL[size_t (i)]));
            }
            return pk;
        };
        const double quiet = deltaPeak (0.3), hot = deltaPeak (1.0);
        CHECK (quiet < 1e-4 && hot > 0.1, "%s DELTA: below threshold %.1f dBFS (silent), clipping %.1f dBFS",
               mode ? "Spectral" : "Sample", 20 * std::log10 (quiet + 1e-12), 20 * std::log10 (hot));
    }

    // ---- multiband + solo: a soloed low band removes the high tone ----
    {
        Rig r; r.p[kBands] = 2; r.p[kXover1] = 1000; r.p[kSolo1] = 1; r.prepare();
        double hi = 0, lo = 0, ph = 0; long cnt = 0;
        for (int b = 0; b < 150; ++b)
        {
            r.block ([&] (int, double& l, double& rr) {
                l = rr = 0.2 * std::sin (ph) + 0.2 * std::sin (40.0 * ph);   // 100 Hz + 4 kHz
                ph += 2 * kPi * 100.0 / fs; });
            if (b >= 50)
                for (int i = 0; i < N; ++i, ++cnt)
                {
                    const double t = 2 * kPi * double (cnt) / fs;
                    hi += r.outL[size_t (i)] * std::sin (4000.0 * t + 0.3);
                    lo += r.outL[size_t (i)] * r.outL[size_t (i)];
                }
        }
        const double rms = std::sqrt (lo / double (cnt));
        CHECK (rms > 0.1 && rms < 0.16, "solo band 1 passes the 100 Hz tone (rms %.3f)", rms);
    }

    // ---- BYPASS: output = input delayed by the reported latency, in both modes ----
    for (int mode : { 0, 1 })
    {
        Rig r; r.p[kMode] = mode; r.p[kBypass] = 1; r.p[kInGain] = 12; r.prepare();
        const int L = r.proc.latencySamples (mode);
        std::vector<double> hist;
        double err = 0; long n = 0;
        for (int b = 0; b < 40; ++b)
        {
            r.block ([&] (int, double& l, double& rr) { const double x = 0.9 * std::sin (0.031 * double (n++)); l = rr = x; hist.push_back (x); });
            for (int i = 0; i < N; ++i)
            {
                const long k = long (b) * N + i - L;
                if (b >= 4) err = std::max (err, std::fabs (r.outL[size_t (i)] - (k >= 0 ? hist[size_t (k)] : 0.0)));
            }
        }
        CHECK (err < 1e-6, "%s BYPASS = input delayed by %d samples (max error %.2g)", mode ? "Spectral" : "Sample", L, err);
    }

    // ---- AUTO GAIN: +12 dB In below the threshold leaves the level unchanged ----
    {
        auto level = [&] (bool autoGain) {
            Rig r; r.p[kInGain] = 12; r.p[kAutoGain] = autoGain ? 1 : 0; r.p[kThr1] = 0; r.prepare();
            double pk = 0, ph = 0;
            for (int b = 0; b < 60; ++b)
            {
                r.block ([&] (int, double& l, double& rr) { l = rr = 0.1 * std::sin (ph); ph += 2 * kPi * 300 / fs; });
                if (b > 20) for (int i = 0; i < N; ++i) pk = std::max (pk, std::fabs (r.outL[size_t (i)]));
            }
            return pk;
        };
        const double on = level (true), off = level (false);
        CHECK (std::fabs (on - 0.1) < 1e-3 && std::fabs (off - 0.398) < 5e-3, "AUTO GAIN: +12 dB In -> out peak %.4f (on) vs %.4f (off)", on, off);
    }

    // ---- viz history fills ----
    {
        Rig r; r.prepare();
        r.block ([&] (int i, double& l, double& rr) { l = rr = 0.1 * std::sin (i * 0.1); });
        std::vector<VizEntry> v; uint64_t from = 0;
        const uint64_t now = r.proc.viz.read (0, 0, 100000, v, from);
        CHECK (now == uint64_t (N / VizHistory::kDecim) && v.size() == size_t (now), "viz history has %llu entries", (unsigned long long) now);
        CHECK (r.proc.meterIn.take() > 0.09f, "input meter sees the signal");
    }

    std::printf (failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
