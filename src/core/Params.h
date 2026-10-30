#pragma once
// 400Scalper — parameter table (format-agnostic).
// The index IS the parameter id seen by the host. Never reorder: only append.

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <array>
#include "../dsp/Crossover.h"   // kMaxBands, kMaxXovers

namespace scalper {

enum ParamIdx : int
{
    kInGain = 0,
    kOutGain,
    kBands,                         // 1..4
    kSlope,                         // 0 = 6 dB, 1 = 12 dB, 2 = 24 dB
    kXover1, kXover2, kXover3,      // Hz
    kThr1, kThr2, kThr3, kThr4,     // dB
    kKnee1, kKnee2, kKnee3, kKnee4, // %
    kSolo1, kSolo2, kSolo3, kSolo4, // 0/1 (monitoring only, not saved)
    kMode,                          // 0 = Sample, 1 = Spectral (changes latency)
    kDetail,                        // Spectral mode detail preservation, %
    kDelta,                         // listen to what is being clipped (monitoring only, not saved)
    kNumParams
};

enum class Unit { dB, Hz, Percent, Count, Slope, Toggle, Mode };

struct ParamDef
{
    const char* name;
    const char* group;
    double min, max, def;
    Unit unit;
    bool automatable;
    bool saved;
};

inline const std::array<ParamDef, kNumParams>& paramDefs()
{
    static const std::array<ParamDef, kNumParams> defs { {
        { "In Gain",         "Global",    -24.0, 24.0,      0.0, Unit::dB,      true,  true  },
        { "Out Gain",        "Global",    -24.0, 24.0,      0.0, Unit::dB,      true,  true  },
        { "Bands",           "Multiband",   1.0,  4.0,      1.0, Unit::Count,   true,  true  },
        { "Crossover Slope", "Multiband",   0.0,  2.0,      2.0, Unit::Slope,   true,  true  },
        { "Crossover 1",     "Multiband",  30.0, 16000.0, 120.0, Unit::Hz,      true,  true  },
        { "Crossover 2",     "Multiband",  30.0, 16000.0, 1000.0, Unit::Hz,     true,  true  },
        { "Crossover 3",     "Multiband",  30.0, 16000.0, 6000.0, Unit::Hz,     true,  true  },
        { "Threshold 1",     "Band 1",    -24.0,  0.0,     -6.0, Unit::dB,      true,  true  },
        { "Threshold 2",     "Band 2",    -24.0,  0.0,     -6.0, Unit::dB,      true,  true  },
        { "Threshold 3",     "Band 3",    -24.0,  0.0,     -6.0, Unit::dB,      true,  true  },
        { "Threshold 4",     "Band 4",    -24.0,  0.0,     -6.0, Unit::dB,      true,  true  },
        { "Knee 1",          "Band 1",      0.0, 100.0,    30.0, Unit::Percent, true,  true  },
        { "Knee 2",          "Band 2",      0.0, 100.0,    30.0, Unit::Percent, true,  true  },
        { "Knee 3",          "Band 3",      0.0, 100.0,    30.0, Unit::Percent, true,  true  },
        { "Knee 4",          "Band 4",      0.0, 100.0,    30.0, Unit::Percent, true,  true  },
        { "Solo 1",          "Band 1",      0.0,  1.0,      0.0, Unit::Toggle,  false, false },
        { "Solo 2",          "Band 2",      0.0,  1.0,      0.0, Unit::Toggle,  false, false },
        { "Solo 3",          "Band 3",      0.0,  1.0,      0.0, Unit::Toggle,  false, false },
        { "Solo 4",          "Band 4",      0.0,  1.0,      0.0, Unit::Toggle,  false, false },
        { "Mode",            "Global",      0.0,  1.0,      0.0, Unit::Mode,    true,  true  },
        { "Detail",          "Spectral",    0.0, 100.0,   100.0, Unit::Percent, true,  true  },
        { "Delta",           "Global",      0.0,  1.0,      0.0, Unit::Toggle,  false, false },
    } };
    return defs;
}

inline bool isStepped (int i)
{
    const Unit u = paramDefs()[size_t (i)].unit;
    return u == Unit::Count || u == Unit::Slope || u == Unit::Toggle || u == Unit::Mode;
}

inline double clampParam (int index, double v)
{
    const auto& d = paramDefs()[size_t (index)];
    if (std::isnan (v)) v = d.def;
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    if (isStepped (index)) v = std::round (v);
    return v;
}

inline int slopeDbFromParam (double v) { return v < 0.5 ? 6 : (v < 1.5 ? 12 : 24); }

// A plain snapshot of every parameter in real units, as the audio engine reads them.
struct ParamValues
{
    double v[kNumParams];
    ParamValues() { for (int i = 0; i < kNumParams; ++i) v[i] = paramDefs()[size_t (i)].def; }
    double operator[] (int i) const { return v[i]; }
    double& operator[] (int i) { return v[i]; }
};

} // namespace scalper
