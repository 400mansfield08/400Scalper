#pragma once
// 400Scalper — 4x linear-phase oversampler built from two polyphase halfband FIR stages.
//
// Each stage uses a Kaiser-windowed halfband lowpass (length 4k+3, so the centre tap is
// 0.5 and every other tap is zero). Polyphase form:
//   up:    y[2m]   = 2 * sum_{i even} h[i] x[m - i/2]
//          y[2m+1] = x[m - (c-1)/2]                          (c = centre index, odd)
//   down:  z[m] = sum_{i even} h[i] v_even[m - i/2] + 0.5 * v_odd[m - (c+1)/2]
//
// Stage 1 (base <-> 2x): 59 taps, ~77 dB stopband, passband to ~20 kHz at 48 kHz.
// Stage 2 (2x <-> 4x):   19 taps, ~72 dB stopband (wide transition at the higher rate).
// Round-trip latency = (N1-1)/2 + (N2-1)/4 base samples = 29 + 4.5 = 33.5.

#include <vector>
#include <cmath>
#include <cstddef>

namespace scalper {

inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= q / (double (k) * double (k));
        sum += term;
        if (term < sum * 1.0e-16) break;
    }
    return sum;
}

// Returns the even-index taps h[0], h[2], ... h[N-1] of a halfband filter of length N (N = 4k+3).
inline std::vector<double> designHalfbandEvenTaps (int N, double beta)
{
    const int c = (N - 1) / 2;
    std::vector<double> even;
    double sum = 0.0;
    const double i0b = besselI0 (beta);
    for (int i = 0; i < N; i += 2)
    {
        const int m = i - c; // odd
        const double sinc = std::sin (3.14159265358979323846 * m / 2.0) / (3.14159265358979323846 * m / 2.0);
        const double r = double (m) / double (c);
        const double win = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        const double h = 0.5 * sinc * win;
        even.push_back (h);
        sum += h;
    }
    for (auto& h : even) h *= 0.5 / sum; // even taps sum to 0.5 -> unity DC gain
    return even;
}

// Simple fixed-size delay history with contiguous read (double-write trick).
struct History
{
    std::vector<double> buf;
    int size = 0, pos = 0;

    void init (int n) { size = n; buf.assign (size_t (2 * n), 0.0); pos = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    void push (double x)
    {
        pos = (pos == 0 ? size : pos) - 1;
        buf[size_t (pos)] = x;
        buf[size_t (pos + size)] = x;
    }
    // element k samples ago (0 = newest)
    const double* recent() const { return buf.data() + pos; }
};

struct HalfbandStage
{
    std::vector<double> even; // even-phase taps
    int c = 0;                // centre index
    History upHist, downEven, downOdd;

    void init (int N, double beta)
    {
        even = designHalfbandEvenTaps (N, beta);
        c = (N - 1) / 2;
        const int ne = int (even.size());
        upHist.init (ne + c);
        downEven.init (ne + 1);
        downOdd.init ((c + 1) / 2 + 2);
    }

    void reset() { upHist.reset(); downEven.reset(); downOdd.reset(); }

    void up (double x, double& y0, double& y1)
    {
        upHist.push (x);
        const double* r = upHist.recent();
        double acc = 0.0;
        for (size_t k = 0; k < even.size(); ++k) acc += even[k] * r[k];
        y0 = 2.0 * acc;
        y1 = r[(c - 1) / 2];
    }

    double down (double v0, double v1)
    {
        downEven.push (v0);
        downOdd.push (v1);
        const double* e = downEven.recent();
        double acc = 0.0;
        for (size_t k = 0; k < even.size(); ++k) acc += even[k] * e[k];
        return acc + 0.5 * downOdd.recent()[(c + 1) / 2];
    }
};

struct Oversampler4x
{
    HalfbandStage s1, s2;
    static constexpr int factor = 4;

    Oversampler4x()
    {
        s1.init (59, 7.5);
        s2.init (19, 7.0);
    }

    void reset() { s1.reset(); s2.reset(); }

    // x -> out[4]
    void up (double x, double* out)
    {
        double a, b;
        s1.up (x, a, b);
        s2.up (a, out[0], out[1]);
        s2.up (b, out[2], out[3]);
    }

    double down (const double* in)
    {
        const double a = s2.down (in[0], in[1]);
        const double b = s2.down (in[2], in[3]);
        return s1.down (a, b);
    }

    // Round-trip latency in base-rate samples (fractional).
    double latency() const { return double (s1.c) + double (s2.c) / 2.0; }
};

} // namespace scalper
