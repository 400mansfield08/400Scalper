#pragma once
// 400Scalper — soft-knee clipper with first-order antiderivative anti-aliasing (ADAA).
//
// Curve (odd-symmetric, per sample):
//   |x| <= ks           : y = x                              (ks = T * (1 - k))
//   |x| >  ks           : y = sign(x) * (ks + w * tanh((|x| - ks) / w)),  w = T - ks
//   k == 0              : hard clip at T
// The shoulder approaches T but never exceeds it.
//
// ADAA (Parker, Zavalishin & Le Bivic, DAFx-16):
//   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]),  F = antiderivative of the curve.
// Falls back to f(midpoint) when consecutive samples are (nearly) equal.
// In the linear region this reduces to (x[n] + x[n-1]) / 2: a half-sample delay and a
// gentle high-frequency droop, which the plugin runs at 4x oversampling and compensates
// with a tiny FIR (see DroopComp).

#include <cmath>
#include <algorithm>

namespace scalper {

struct ClipCurve
{
    double T  = 0.5;  // threshold (linear amplitude, > 0)
    double ks = 0.35; // knee start
    double w  = 0.15; // knee width (T - ks)

    void set (double thresholdLinear, double knee01)
    {
        T  = std::max (1.0e-6, thresholdLinear);
        knee01 = std::clamp (knee01, 0.0, 1.0);
        ks = T * (1.0 - knee01);
        w  = T - ks;
    }

    double f (double x) const
    {
        const double a = std::fabs (x);
        if (a <= ks) return x;
        const double s = x < 0 ? -1.0 : 1.0;
        if (w <= 1.0e-12) return s * T;
        return s * (ks + w * std::tanh ((a - ks) / w));
    }

    // log(cosh(z)) without overflow
    static double logCosh (double z)
    {
        z = std::fabs (z);
        return z + std::log1p (std::exp (-2.0 * z)) - 0.69314718055994530942;
    }

    // Antiderivative (even function)
    double F (double x) const
    {
        const double a = std::fabs (x);
        if (a <= ks) return 0.5 * x * x;
        const double base = 0.5 * ks * ks + ks * (a - ks);
        if (w <= 1.0e-12) return base;
        return base + w * w * logCosh ((a - ks) / w);
    }
};

struct AdaaClipper
{
    double x1 = 0.0, F1 = 0.0;

    void reset() { x1 = 0.0; F1 = 0.0; }

    double process (const ClipCurve& c, double x)
    {
        const double Fx = c.F (x);
        const double dx = x - x1;
        double y;
        if (std::fabs (dx) < 1.0e-6)
            y = c.f (0.5 * (x + x1));
        else
            y = (Fx - F1) / dx;
        x1 = x; F1 = Fx;
        return y;
    }

    // Same, for a curve that may change every sample (Spectral mode): both antiderivative
    // terms are evaluated with the *current* curve, so a parameter change can never be
    // divided by a tiny dx and spike.
    double processVarying (const ClipCurve& c, double x)
    {
        const double dx = x - x1;
        double y;
        if (std::fabs (dx) < 1.0e-6)
            y = c.f (0.5 * (x + x1));
        else
            y = (c.F (x) - c.F (x1)) / dx;
        x1 = x; F1 = 0.0;
        return y;
    }
};

// 3-tap symmetric FIR that undoes the ADAA two-point-average droop across the audio
// band when running at 4x oversampling. Fitted so gain is 1 at DC and flat to ~20 kHz
// at a 4x rate (residual < 0.05 dB). Adds one sample of delay at the oversampled rate.
struct DroopComp
{
    double a = -0.1352, b = 1.2704;
    double z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }

    // Refit for the actual oversampled rate so the correction targets 20 kHz.
    void prepare (double oversampledRate)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * std::min (20000.0, 0.45 * oversampledRate / 4.0) / oversampledRate;
        const double target = 1.0 / std::cos (0.5 * w0);
        // b + 2a = 1  and  b + 2a cos(w0) = target
        a = (1.0 - target) / (2.0 * (1.0 - std::cos (w0)));
        b = 1.0 - 2.0 * a;
    }

    double process (double x)
    {
        const double y = a * x + b * z1 + a * z2;
        z2 = z1; z1 = x;
        return y;
    }
};

} // namespace scalper
