#include "Scalper.h"
#include "IPlug_include_in_plug_src.h"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string>

#if IPLUG_EDITOR
 #include "ui/ScalperUI.h"
 #include "FontData.h"      // generated: the Sui Generis font embedded as bytes
#endif

using namespace scalper;
#if IPLUG_EDITOR
using namespace iplug::igraphics;
#endif

namespace {
constexpr int kStateMagic = 0x34305353;   // "SS04"
constexpr int kStateVersion = 1;

// Display helpers for the host's generic parameter view.
void displayDb (double v, WDL_String& s) { s.SetFormatted (32, "%+.1f", std::fabs (v) < 0.05 ? 0.0 : v); }
void displayHz (double v, WDL_String& s)
{
    if (v >= 1000.0) s.SetFormatted (32, "%.2f k", v / 1000.0); else s.SetFormatted (32, "%.0f ", v);
}
}

Scalper::Scalper (const InstanceInfo& info)
    : iplug::Plugin (info, MakeConfig (kNumParams, 1))
{
    const auto& d = paramDefs();
    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamDef& p = d[size_t (i)];
        const int flags = p.automatable ? 0 : IParam::kFlagCannotAutomate;
        switch (p.unit)
        {
            case Unit::dB:
                GetParam (i)->InitDouble (p.name, p.def, p.min, p.max, 0.01, "dB", flags, p.group, IParam::ShapeLinear(), IParam::kUnitDB, displayDb);
                break;
            case Unit::Hz:
                GetParam (i)->InitDouble (p.name, p.def, p.min, p.max, 0.01, "Hz", flags, p.group, IParam::ShapeExp(), IParam::kUnitFrequency, displayHz);
                break;
            case Unit::Percent:
                GetParam (i)->InitDouble (p.name, p.def, p.min, p.max, 0.01, "%", flags, p.group, IParam::ShapeLinear(), IParam::kUnitPercentage);
                break;
            case Unit::Count:
                GetParam (i)->InitInt (p.name, int (p.def), int (p.min), int (p.max), "", flags, p.group);
                break;
            case Unit::Slope:
                GetParam (i)->InitEnum (p.name, int (p.def), { "6 dB/oct", "12 dB/oct", "24 dB/oct" }, flags, p.group);
                break;
            case Unit::Mode:
                GetParam (i)->InitEnum (p.name, int (p.def), { "Sample", "Spectral" }, flags, p.group);
                break;
            case Unit::Toggle:
                GetParam (i)->InitBool (p.name, p.def >= 0.5, "", flags, p.group, "Off", "On");
                break;
        }
    }

#if IPLUG_EDITOR
    model = std::make_unique<ui::Model> (*this, proc, selectedBand);
    model->version = PLUG_VERSION_STR;
    ui = std::make_unique<ui::ScalperUI> (*model, kSuiGenerisTtf, int (kSuiGenerisTtfSize));

    mMakeGraphicsFunc = [&]() {
        return MakeGraphics (*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.f);
    };
    mLayoutFunc = [&] (IGraphics* pGraphics) { ui->layout (pGraphics); };
#endif
}

Scalper::~Scalper() = default;

ParamValues Scalper::snapshot() const
{
    ParamValues v;
    for (int i = 0; i < kNumParams; ++i) v[i] = GetParam (i)->Value();
    return v;
}

#if IPLUG_DSP
void Scalper::OnReset()
{
    // Called from activate (and on sample-rate changes). Report the latency of the mode we
    // start in without notifying the host: it reads the latency after activation anyway.
    const ParamValues p = snapshot();
    proc.prepare (GetSampleRate(), p);
    const int mode = p[kMode] >= 0.5 ? 1 : 0;
    reportedMode.store (mode);
    IPlugProcessor::SetLatency (proc.latencySamples (mode));
}

void Scalper::ProcessBlock (sample** inputs, sample** outputs, int nFrames)
{
    const ParamValues p = snapshot();
    const int nIn = std::max (1, std::min (2, NInChansConnected()));
    const int nOut = std::max (1, std::min (2, NOutChansConnected()));
    proc.process (inputs, nIn, outputs, nOut, nFrames, p);
}
#endif

void Scalper::OnIdle()
{
    // Main thread. Spectral mode has more latency than Sample mode: when the Mode parameter no
    // longer matches what the host was told, ask for a restart (CLAP) so it re-reads it.
    // The audio has already switched (with a short fade) inside the Processor.
    const int wanted = GetParam (kMode)->Value() >= 0.5 ? 1 : 0;
    if (wanted != reportedMode.load())
    {
        reportedMode.store (wanted);
        SetLatency (proc.latencySamples (wanted));
    }
}

//------------------------------------------------------------------------------
// State: magic, version, all parameters, selected band.
// Solo and Delta are monitoring controls: they are reset to off when a state is loaded.
bool Scalper::SerializeState (IByteChunk& chunk) const
{
    chunk.Put (&kStateMagic);
    chunk.Put (&kStateVersion);
    if (! SerializeParams (chunk)) return false;
    const int sel = selectedBand.load();
    chunk.Put (&sel);
    return true;
}

int Scalper::UnserializeState (const IByteChunk& chunk, int startPos)
{
    int magic = 0, version = 0;
    int pos = chunk.Get (&magic, startPos);
    if (pos < 0 || magic != kStateMagic) return unserializeLegacy (chunk, startPos);
    pos = chunk.Get (&version, pos);
    if (pos < 0) return -1;
    pos = UnserializeParams (chunk, pos);
    if (pos < 0) return pos;
    int sel = 0;
    const int p2 = chunk.Get (&sel, pos);
    if (p2 >= 0) { selectedBand.store (std::clamp (sel, 0, kMaxBands - 1)); pos = p2; }
    for (int b = 0; b < kMaxBands; ++b) GetParam (kSolo1 + b)->Set (0.0);
    GetParam (kDelta)->Set (0.0);
    return pos;
}

// v0.1 / v0.2 (raw CLAP build) saved plain text "id=value" lines with different ids.
int Scalper::unserializeLegacy (const IByteChunk& chunk, int startPos)
{
    const int size = chunk.Size() - startPos;
    if (size <= 0) return -1;
    const std::string data (reinterpret_cast<const char*> (chunk.GetData()) + startPos, size_t (size));
    if (data.rfind ("400Scalper", 0) != 0) return -1;
    auto mapId = [] (int old) -> int {
        if (old >= 0 && old <= 6) return old;                 // in, out, bands, slope, xovers
        if (old >= 10 && old <= 13) return kThr1 + (old - 10);
        if (old >= 20 && old <= 23) return kKnee1 + (old - 20);
        if (old == 40) return kMode;
        if (old == 41) return kDetail;
        return -1;                                            // solo / delta / unknown: not restored
    };
    size_t lineStart = data.find ('\n');
    while (lineStart != std::string::npos && lineStart < data.size())
    {
        const size_t next = data.find ('\n', lineStart + 1);
        const std::string line = data.substr (lineStart + 1, next == std::string::npos ? std::string::npos : next - lineStart - 1);
        lineStart = next;
        const auto eq = line.find ('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr (0, eq);
        const char* first = line.c_str() + eq + 1;
        const char* last = line.c_str() + line.size();
        while (last > first && (last[-1] == '\r' || last[-1] == ' ')) --last;
        double v = 0;
        if (std::from_chars (first, last, v).ec != std::errc()) continue;
        if (key == "sel") { selectedBand.store (std::clamp (int (v), 0, kMaxBands - 1)); continue; }
        const int idx = mapId (std::atoi (key.c_str()));
        if (idx >= 0) GetParam (idx)->Set (clampParam (idx, v));
    }
    for (int b = 0; b < kMaxBands; ++b) GetParam (kSolo1 + b)->Set (0.0);
    GetParam (kDelta)->Set (0.0);
    return chunk.Size();
}
