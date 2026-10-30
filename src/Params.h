#pragma once
// 400Scalper — parameter table. IDs are stable: never renumber, only add.

#include <clap/clap.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <array>

namespace scalper {

enum ParamId : clap_id
{
    kInGain  = 0,
    kOutGain = 1,
    kBands   = 2,   // 1..4
    kSlope   = 3,   // 0 = 6 dB, 1 = 12 dB, 2 = 24 dB
    kXover1  = 4, kXover2 = 5, kXover3 = 6,   // Hz
    kThr1    = 10,  // 10..13  dB
    kKnee1   = 20,  // 20..23  %
    kSolo1   = 30,  // 30..33  0/1 (monitoring only, not saved)
    kMode    = 40,  // 0 = Sample, 1 = Spectral (changes latency -> host restart)
    kDetail  = 41,  // Spectral mode detail preservation, %
};

enum class Unit { dB, Hz, Percent, Count, Slope, Toggle, Mode };

struct ParamDef
{
    clap_id id;
    const char* name;
    const char* module;
    double min, max, def;
    Unit unit;
    bool stepped;
    bool automatable;
    bool saved;
};

inline const std::array<ParamDef, 21>& paramDefs()
{
    static const std::array<ParamDef, 21> defs { {
        { kInGain,  "In Gain",          "Global",       -24.0, 24.0,    0.0,  Unit::dB,      false, true,  true  },
        { kOutGain, "Out Gain",         "Global",       -24.0, 24.0,    0.0,  Unit::dB,      false, true,  true  },
        { kBands,   "Bands",            "Multiband",      1.0,  4.0,    1.0,  Unit::Count,   true,  true,  true  },
        { kSlope,   "Crossover Slope",  "Multiband",      0.0,  2.0,    2.0,  Unit::Slope,   true,  true,  true  },
        { kXover1,  "Crossover 1",      "Multiband",     30.0, 16000.0, 120.0, Unit::Hz,     false, true,  true  },
        { kXover2,  "Crossover 2",      "Multiband",     30.0, 16000.0, 1000.0, Unit::Hz,    false, true,  true  },
        { kXover3,  "Crossover 3",      "Multiband",     30.0, 16000.0, 6000.0, Unit::Hz,    false, true,  true  },
        { kThr1 + 0, "Threshold 1",     "Band 1",       -24.0,  0.0,   -6.0,  Unit::dB,      false, true,  true  },
        { kThr1 + 1, "Threshold 2",     "Band 2",       -24.0,  0.0,   -6.0,  Unit::dB,      false, true,  true  },
        { kThr1 + 2, "Threshold 3",     "Band 3",       -24.0,  0.0,   -6.0,  Unit::dB,      false, true,  true  },
        { kThr1 + 3, "Threshold 4",     "Band 4",       -24.0,  0.0,   -6.0,  Unit::dB,      false, true,  true  },
        { kKnee1 + 0, "Knee 1",         "Band 1",         0.0, 100.0,  30.0,  Unit::Percent, false, true,  true  },
        { kKnee1 + 1, "Knee 2",         "Band 2",         0.0, 100.0,  30.0,  Unit::Percent, false, true,  true  },
        { kKnee1 + 2, "Knee 3",         "Band 3",         0.0, 100.0,  30.0,  Unit::Percent, false, true,  true  },
        { kKnee1 + 3, "Knee 4",         "Band 4",         0.0, 100.0,  30.0,  Unit::Percent, false, true,  true  },
        { kSolo1 + 0, "Solo 1",         "Band 1",         0.0,  1.0,    0.0,  Unit::Toggle,  true,  false, false },
        { kSolo1 + 1, "Solo 2",         "Band 2",         0.0,  1.0,    0.0,  Unit::Toggle,  true,  false, false },
        { kSolo1 + 2, "Solo 3",         "Band 3",         0.0,  1.0,    0.0,  Unit::Toggle,  true,  false, false },
        { kSolo1 + 3, "Solo 4",         "Band 4",         0.0,  1.0,    0.0,  Unit::Toggle,  true,  false, false },
        { kMode,      "Mode",           "Global",         0.0,  1.0,    0.0,  Unit::Mode,    true,  true,  true  },
        { kDetail,    "Detail",         "Spectral",       0.0, 100.0, 100.0,  Unit::Percent, false, true,  true  },
    } };
    return defs;
}

constexpr int kNumParams = 21;

inline int paramIndex (clap_id id)
{
    const auto& d = paramDefs();
    for (int i = 0; i < kNumParams; ++i) if (d[size_t (i)].id == id) return i;
    return -1;
}

inline double clampParam (int index, double v)
{
    const auto& d = paramDefs()[size_t (index)];
    if (std::isnan (v)) v = d.def;
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    if (d.stepped) v = std::round (v);
    return v;
}

inline void formatParam (int index, double v, char* out, uint32_t size)
{
    const auto& d = paramDefs()[size_t (index)];
    switch (d.unit)
    {
        case Unit::dB:      std::snprintf (out, size, "%+.1f dB", v); break;
        case Unit::Hz:      if (v >= 1000.0) std::snprintf (out, size, "%.2f kHz", v / 1000.0); else std::snprintf (out, size, "%.0f Hz", v); break;
        case Unit::Percent: std::snprintf (out, size, "%.0f %%", v); break;
        case Unit::Count:   std::snprintf (out, size, "%d", int (std::lround (v))); break;
        case Unit::Slope:   std::snprintf (out, size, "%s", v < 0.5 ? "6 dB/oct" : (v < 1.5 ? "12 dB/oct" : "24 dB/oct")); break;
        case Unit::Toggle:  std::snprintf (out, size, "%s", v >= 0.5 ? "On" : "Off"); break;
        case Unit::Mode:    std::snprintf (out, size, "%s", v >= 0.5 ? "Spectral" : "Sample"); break;
    }
}

inline int slopeDbFromParam (double v) { return v < 0.5 ? 6 : (v < 1.5 ? 12 : 24); }

} // namespace scalper
