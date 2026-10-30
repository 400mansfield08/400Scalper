// 400Scalper — JSON messages between the plugin and its HTML UI.
// Kept separate from the web view so they can be built and tested on any platform.

#include "Gui.h"
#include "Plugin.h"
#include "Settings.h"
#include <charconv>
#include <vector>

namespace scalper {

static void appendNum (std::string& s, double v)
{
    char buf[32];
    if (! std::isfinite (v)) v = 0.0;
    auto r = std::to_chars (buf, buf + sizeof (buf), float (v), std::chars_format::general, 5);
    s.append (buf, size_t (r.ptr - buf));
}

std::string Gui::initJson (Plugin& p)
{
    std::string s = "{\"params\":[";
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDefs()[size_t (i)];
        if (i) s += ',';
        s += "{\"id\":"; appendNum (s, d.id);
        s += ",\"min\":"; appendNum (s, d.min);
        s += ",\"max\":"; appendNum (s, d.max);
        s += ",\"def\":"; appendNum (s, d.def);
        s += ",\"v\":"; appendNum (s, p.values[i].load());
        s += '}';
    }
    s += "],\"sel\":"; appendNum (s, p.selectedBand.load());
    s += ",\"sr\":"; appendNum (s, p.currentSampleRate.load());
    s += ",\"vizRate\":"; appendNum (s, p.currentSampleRate.load() / VizHistory::kDecim);
    s += ",\"accent\":\""; s += settings::loadAccent(); s += "\"}";
    return s;
}

std::string Gui::pollJson (Plugin& p, int band, double since, int maxEntries, bool wantSpectrum)
{
    std::vector<VizEntry> entries;
    uint64_t from = 0;
    const uint64_t now = p.viz.read (band, since < 0 ? 0 : uint64_t (since), size_t (std::max (0, maxEntries)), entries, from);

    std::string s;
    s.reserve (64 + entries.size() * 4 * 8 + (wantSpectrum ? 2048 * 8 : 0));
    s += "{\"now\":"; s += std::to_string (now);
    s += ",\"from\":"; s += std::to_string (from);
    s += ",\"sr\":"; appendNum (s, p.currentSampleRate.load());
    s += ",\"p\":[";
    for (int i = 0; i < kNumParams; ++i) { if (i) s += ','; appendNum (s, p.values[i].load (std::memory_order_relaxed)); }
    s += "],\"am\":"; s += std::to_string (p.activeMode.load());
    if (p.activeMode.load() == 1)
    {
        s += ",\"sm\":[";
        for (int i = 0; i < SpectralSplitter::kDisplayPoints; ++i) { if (i) s += ','; appendNum (s, p.spectral.displayMask[size_t (i)].load (std::memory_order_relaxed)); }
        s += ']';
    }
    s += ",\"mi\":"; appendNum (s, p.meterIn.take());
    s += ",\"mo\":"; appendNum (s, p.meterOut.take());
    s += ",\"v\":[";
    bool first = true;
    for (const auto& e : entries)
    {
        for (float x : { e.inMin, e.inMax, e.outMin, e.outMax })
        {
            if (! first) s += ',';
            first = false;
            appendNum (s, x);
        }
    }
    s += ']';
    if (wantSpectrum)
    {
        float buf[2048];
        p.spectrum.read (buf, 2048);
        s += ",\"f\":[";
        for (int i = 0; i < 2048; ++i) { if (i) s += ','; appendNum (s, buf[i]); }
        s += ']';
    }
    s += '}';
    return s;
}

} // namespace scalper
