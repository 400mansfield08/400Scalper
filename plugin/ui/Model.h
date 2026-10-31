#pragma once
// 400Scalper UI — shared state for every control: colors, text helpers, parameter editing,
// and the per-frame analysis (scope history ring, spectrum, meters, gain reduction).
// Everything here runs on the UI thread. The audio engine is only read through its
// lock-free taps (VizHistory, SpectrumTap, PeakMeter, spectral display mask).

#include "IControl.h"
#include "Layout.h"
#include "core/Processor.h"
#include "core/Settings.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <complex>
#include <map>
#include <string>
#include <vector>

namespace scalper::ui {

using namespace iplug;
using namespace iplug::igraphics;

constexpr const char* kFont = "SuiGeneris";
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Colors
inline IColor rgb (uint32_t c, int a = 255) { return IColor (a, int ((c >> 16) & 255), int ((c >> 8) & 255), int (c & 255)); }
inline IColor withAlpha (const IColor& c, float a) { return IColor (int (std::lround (std::clamp (a, 0.f, 1.f) * 255.f)), c.R, c.G, c.B); }

namespace col {
    inline const IColor bg      = rgb (0x0D0D0D);
    inline const IColor hdr     = rgb (0x090909);
    inline const IColor line    = rgb (0x1F1F1F);
    inline const IColor well    = rgb (0x070707);   // scope / meter / spectrum backgrounds
    inline const IColor gutter  = rgb (0x0A0A0A);
    inline const IColor text    = rgb (0xE6E6E6);
    inline const IColor muted   = rgb (0x8C8C8C);
    inline const IColor btn     = rgb (0xBDBDBD);
    inline const IColor b333    = rgb (0x333333);
    inline const IColor b262    = rgb (0x262626);
    inline const IColor b3a     = rgb (0x3A3A3A);
    inline const IColor panel   = rgb (0x121212);
    inline const IColor panel14 = rgb (0x141414);
    inline const IColor panel16 = rgb (0x161616);
    inline const IColor panel1a = rgb (0x1A1A1A);
    inline const IColor panel1c = rgb (0x1C1C1C);
    inline const IColor grey    = rgb (0x8A8A8A);   // output trace
    inline const IColor grey9a  = rgb (0x9A9A9A);
    inline const IColor dim     = rgb (0x5E5E5E);
    inline const IColor off     = rgb (0x2A2A2A);
}

// HSV helpers (h in degrees, s/v 0..1) — the accent color picker and the complementary hue.
inline void hsv2rgb (double h, double s, double v, int out[3])
{
    auto f = [&] (double n) { const double k = std::fmod (n + h / 60.0, 6.0); return v - v * s * std::max (0.0, std::min ({ k, 4.0 - k, 1.0 })); };
    out[0] = int (std::lround (f (5) * 255)); out[1] = int (std::lround (f (3) * 255)); out[2] = int (std::lround (f (1) * 255));
}
inline void rgb2hsv (int r8, int g8, int b8, double& h, double& s, double& v)
{
    const double r = r8 / 255.0, g = g8 / 255.0, b = b8 / 255.0;
    const double mx = std::max ({ r, g, b }), mn = std::min ({ r, g, b }), d = mx - mn;
    h = 0;
    if (d > 0) h = mx == r ? std::fmod ((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h = std::fmod (h * 60 + 360, 360.0);
    s = mx > 0 ? d / mx : 0; v = mx;
}
inline std::string toHex (const IColor& c)
{
    char buf[8]; std::snprintf (buf, sizeof (buf), "#%02X%02X%02X", c.R, c.G, c.B); return buf;
}
inline bool fromHex (const std::string& in, IColor& out)
{
    std::string s = in;
    while (! s.empty() && s.front() == ' ') s.erase (s.begin());
    while (! s.empty() && s.back() == ' ') s.pop_back();
    if (! s.empty() && s[0] == '#') s.erase (s.begin());
    if (s.size() != 6) return false;
    for (char ch : s) if (! std::isxdigit ((unsigned char) ch)) return false;
    const unsigned long n = std::strtoul (s.c_str(), nullptr, 16);
    out = IColor (255, int ((n >> 16) & 255), int ((n >> 8) & 255), int (n & 255));
    return true;
}

// ---------------------------------------------------------------------------
// Text with CSS-style letter spacing (em). Glyph widths are cached per size.
class TextKit
{
public:
    float charW (IGraphics& g, float size, const std::string& ch)
    {
        auto key = std::make_pair (int (size * 10), ch);
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
        IRECT r;
        const float w = g.MeasureText (IText (size, COLOR_WHITE, kFont), ch.c_str(), r);
        cache[key] = w;
        return w;
    }
    static std::vector<std::string> split (const char* s)   // UTF-8 code points
    {
        std::vector<std::string> out;
        for (const unsigned char* p = (const unsigned char*) s; *p;)
        {
            int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : 4;
            std::string c;
            for (int i = 0; i < n && p[i]; ++i) c += char (p[i]);
            p += c.size();
            out.push_back (c);
        }
        return out;
    }
    float width (IGraphics& g, const char* s, float size, float spacingEm = 0.f)
    {
        if (spacingEm == 0.f) { IRECT r; return g.MeasureText (IText (size, COLOR_WHITE, kFont), s, r); }
        float w = 0; int n = 0;
        for (auto& c : split (s)) { w += charW (g, size, c); ++n; }
        return w + spacingEm * size * float (std::max (0, n - 1));
    }
    // Shortens s with an ellipsis so it fits in maxW.
    std::string fit (IGraphics& g, const std::string& s, float size, float spacingEm, float maxW)
    {
        if (width (g, s.c_str(), size, spacingEm) <= maxW) return s;
        auto cps = split (s.c_str());
        while (! cps.empty())
        {
            cps.pop_back();
            std::string t; for (auto& c : cps) t += c;
            t += "\xE2\x80\xA6";
            if (width (g, t.c_str(), size, spacingEm) <= maxW) return t;
        }
        return "";
    }
    // Draws text vertically centred on cy. align: Near = x is left, Center = x is centre, Far = x is right.
    void draw (IGraphics& g, const char* s, float x, float cy, float size, const IColor& c, float spacingEm = 0.f, EAlign align = EAlign::Near)
    {
        const float w = width (g, s, size, spacingEm);
        float left = align == EAlign::Near ? x : align == EAlign::Center ? x - w * 0.5f : x - w;
        if (spacingEm == 0.f)
        {
            g.DrawText (IText (size, c, kFont, EAlign::Near, EVAlign::Middle), s, IRECT (left, cy - size, left + w + 2, cy + size));
            return;
        }
        for (auto& ch : split (s))
        {
            const float cw = charW (g, size, ch);
            g.DrawText (IText (size, c, kFont, EAlign::Near, EVAlign::Middle), ch.c_str(), IRECT (left, cy - size, left + cw + 2, cy + size));
            left += cw + spacingEm * size;
        }
    }
private:
    std::map<std::pair<int, std::string>, float> cache;
};

// ---------------------------------------------------------------------------
inline double dbToA (double db) { return std::pow (10.0, db / 20.0); }
inline std::string fmtDb (double v)   // "+1.5 dB", "−6.0 dB", "0.0 dB"
{
    const double r = std::round (v * 10) / 10;
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%s%.1f dB", r > 0 ? "+" : (r < 0 ? "\xE2\x88\x92" : ""), std::fabs (r));
    return buf;
}
inline std::string fmtPct (double v) { char b[16]; std::snprintf (b, sizeof (b), "%d %%", int (std::lround (v))); return b; }
inline std::string fmtHz (double f)
{
    char b[16];
    if (f >= 10000) std::snprintf (b, sizeof (b), "%dk", int (std::lround (f / 1000)));
    else if (f >= 1000) { const double k = std::round (f / 100) / 10; if (k == std::floor (k)) std::snprintf (b, sizeof (b), "%dk", int (k)); else std::snprintf (b, sizeof (b), "%.1fk", k); }
    else std::snprintf (b, sizeof (b), "%d", int (std::lround (f)));
    return b;
}
constexpr double kFMin = 20.0, kFMax = 20000.0;
inline float fToX (double f, float w) { return float (std::log (f / kFMin) / std::log (kFMax / kFMin)) * w; }
inline double xToF (float x, float w) { return kFMin * std::pow (kFMax / kFMin, std::clamp (double (x / std::max (1.f, w)), 0.0, 1.0)); }

// ---------------------------------------------------------------------------
class Model
{
public:
    Model (IEditorDelegate& d, Processor& p, std::atomic<int>& selectedBand)
        : dlg (d), proc (p), selRef (selectedBand)
    {
        IColor c;
        if (! fromHex (settings::loadAccent(), c)) fromHex (settings::kDefaultAccent, c);
        applyAccent (c, false);
        for (auto& v : specDb) v = -120.f;
        for (int i = 0; i < kFft; ++i) hann[size_t (i)] = float (0.5 - 0.5 * std::cos (2 * kPi * i / (kFft - 1)));
        lastFrame = clock::now();
    }

    IEditorDelegate& dlg;
    Processor& proc;
    TextKit tk;
    Rects L;                       // current layout
    std::string version;           // shown in the footer
    bool pickerOpen = false;

    // ---- parameters (real units) ----
    double P (int i) const { return dlg.GetParam (i)->Value(); }
    int nBands() const { return std::clamp (int (std::lround (P (kBands))), 1, kMaxBands); }
    bool spectral() const { return P (kMode) >= 0.5; }
    bool soloOn (int b) const { return P (kSolo1 + b) >= 0.5; }
    std::vector<double> xovers() const { std::vector<double> a; for (int i = 0; i < nBands() - 1; ++i) a.push_back (P (kXover1 + i)); return a; }

    void begin (int i) { dlg.BeginInformHostOfParamChangeFromUI (i); }
    void end (int i)   { dlg.EndInformHostOfParamChangeFromUI (i); }
    void set (int i, double v)
    {
        v = clampParam (i, v);
        const IParam* p = dlg.GetParam (i);
        dlg.SendParameterValueFromUI (i, p->ToNormalized (v));
    }
    void edit (int i, double v) { begin (i); set (i, v); end (i); }   // one undo step

    // ---- selected band (saved with the plugin state) ----
    int sel() const { return std::clamp (selRef.load(), 0, nBands() - 1); }
    void selectBand (int b) { selRef.store (std::clamp (b, 0, nBands() - 1)); ringBand = -1; }
    int thrId() const { return kThr1 + (spectral() ? 0 : sel()); }
    int kneeId() const { return kKnee1 + (spectral() ? 0 : sel()); }

    std::string bandRange (int i) const
    {
        const auto xs = xovers();
        const bool hasLo = i > 0, hasHi = i < int (xs.size());
        if (! hasLo && ! hasHi) return "FULL";
        if (! hasLo) return "< " + fmtHz (xs[size_t (i)]) + " Hz";
        if (! hasHi) return "> " + fmtHz (xs[size_t (i - 1)]) + " Hz";
        return fmtHz (xs[size_t (i - 1)]) + "\xE2\x80\x93" + fmtHz (xs[size_t (i)]) + " Hz";
    }

    // ---- multiband layout edits (crossovers + per-band settings move together) ----
    struct Band { double thr, knee; bool solo; };
    void writeLayout (const std::vector<double>& xs, const std::vector<Band>& bands)
    {
        std::vector<int> ids { kBands, kXover1, kXover2, kXover3 };
        for (int b = 0; b < kMaxBands; ++b) { ids.push_back (kThr1 + b); ids.push_back (kKnee1 + b); ids.push_back (kSolo1 + b); }
        for (int id : ids) begin (id);
        for (size_t i = 0; i < xs.size(); ++i) set (kXover1 + int (i), xs[i]);
        for (size_t b = 0; b < bands.size(); ++b)
        {
            set (kThr1 + int (b), bands[b].thr);
            set (kKnee1 + int (b), bands[b].knee);
            set (kSolo1 + int (b), bands[b].solo ? 1 : 0);
        }
        for (size_t b = bands.size(); b < size_t (kMaxBands); ++b) set (kSolo1 + int (b), 0);
        set (kBands, double (bands.size()));
        for (int id : ids) end (id);
    }
    std::vector<Band> currentBands() const
    {
        std::vector<Band> v;
        for (int b = 0; b < nBands(); ++b) v.push_back ({ P (kThr1 + b), P (kKnee1 + b), soloOn (b) });
        return v;
    }
    int addCrossover (double f)
    {
        auto xs = xovers(); auto bands = currentBands();
        if (xs.size() >= size_t (kMaxXovers)) return -1;
        f = std::clamp (f, 30.0, 16000.0);
        size_t i = 0; while (i < xs.size() && xs[i] < f) ++i;
        if ((i > 0 && f / xs[i - 1] < 1.26) || (i < xs.size() && xs[i] / f < 1.26)) return -1;
        const int s = sel();
        xs.insert (xs.begin() + long (i), f);
        bands.insert (bands.begin() + long (i), bands[i]);
        writeLayout (xs, bands);
        selectBand (int (i) == s ? int (i) : s + (s > int (i) ? 1 : 0));
        return int (i);
    }
    void removeCrossover (int i)
    {
        auto xs = xovers(); auto bands = currentBands();
        if (i < 0 || i >= int (xs.size())) return;
        const int s = sel();
        xs.erase (xs.begin() + i);
        bands.erase (bands.begin() + i + 1);   // the merged band keeps the lower band's settings
        writeLayout (xs, bands);
        selectBand (s > i ? s - 1 : s);
    }
    void singleBand()
    {
        auto bands = currentBands();
        Band keep = bands[size_t (sel())]; keep.solo = false;
        writeLayout ({}, { keep });
        selectBand (0);
    }
    void moveCrossover (int i, double f)
    {
        const auto xs = xovers();
        if (i < 0 || i >= int (xs.size())) return;
        const double lo = i > 0 ? xs[size_t (i - 1)] * 1.26 : 30.0;
        const double hi = i < int (xs.size()) - 1 ? xs[size_t (i + 1)] / 1.26 : 16000.0;
        set (kXover1 + i, std::clamp (f, lo, hi));
    }

    // ---- accent ----
    IColor accent, comp;
    std::string accentHex;
    void applyAccent (const IColor& c, bool save = true)
    {
        accent = c;
        accentHex = toHex (c);
        double h, s, v; rgb2hsv (c.R, c.G, c.B, h, s, v);
        int o[3]; hsv2rgb (std::fmod (h + 180.0, 360.0), s, v, o);
        comp = IColor (255, o[0], o[1], o[2]);
        if (save) pendingSave = true, saveAt = clock::now() + std::chrono::milliseconds (250);
    }

    // ---- scope view ----
    double zoom = 1.0, timeWin = 3.0;
    bool follow = true, frozen = false;
    static constexpr double kZoomMin = 0.25, kZoomMax = 16.0;
    void setZoom (double z) { zoom = std::clamp (z, kZoomMin, kZoomMax); }
    // FIT: zoom so the loudest input in the visible time window fills the scope.
    void fitZoom()
    {
        double pk = 0;
        const uint64_t span = std::min<uint64_t> (now - haveFrom, uint64_t (timeWin * vizRate));
        for (uint64_t i = 1; i <= span && cap; ++i)
        {
            const auto& e = ring[size_t ((now - i) % cap)];
            pk = std::max ({ pk, double (std::fabs (e.inMin)), double (std::fabs (e.inMax)), double (std::fabs (e.outMin)), double (std::fabs (e.outMax)) });
        }
        setZoom (pk > 1e-4 ? 1.15 / (1.05 * pk) : 1.0);
    }
    void setTime (double t) { timeWin = std::clamp (t, 0.05, 6.0); }

    // ---- scope history (selected band), same layout as the audio-side VizHistory ----
    std::vector<VizEntry> ring;
    size_t cap = 0;
    uint64_t now = 0, haveFrom = 0;
    int ringBand = -1;
    double vizRate = 12000.0, engineSr = 48000.0;

    // ---- per-frame analysis ----
    static constexpr int kFft = 2048;
    std::array<float, kFft / 2> specDb {};
    std::array<float, 128> mask {};      // spectral loud mask (log grid 20 Hz .. 20 kHz)
    double grDb = 0.0, clipAct = 0.0;
    struct Meter { double level = -120, hold = -120; double holdAt = 0; } mIn, mOut;
    double dt = 1.0 / 60.0, tMs = 0.0;
    bool wasSpectral = false;
    int selBeforeSpectral = 0;

    void tick()
    {
        const auto t = clock::now();
        dt = std::min (0.1, std::chrono::duration<double> (t - lastFrame).count());
        lastFrame = t;
        tMs = std::chrono::duration<double, std::milli> (t - start).count();

        if (pendingSave && t >= saveAt) { pendingSave = false; settings::saveAccent (accentHex); }

        // Spectral mode is one clipper over the whole signal: it shows band 1.
        const bool spec = spectral();
        if (spec != wasSpectral)
        {
            if (spec) { selBeforeSpectral = selRef.load(); } else { selRef.store (std::min (selBeforeSpectral, nBands() - 1)); }
            wasSpectral = spec; ringBand = -1;
        }
        if (selRef.load() >= nBands()) { selRef.store (nBands() - 1); ringBand = -1; }
        const int pm = proc.procMode.load();
        if (pm != lastProcMode) { lastProcMode = pm; ringBand = -1; }

        engineSr = proc.currentSampleRate.load();
        vizRate = proc.viz.rate();
        const int band = spec ? 0 : sel();
        // The history restarts when the host re-activates the plugin (e.g. the restart after a
        // mode switch). Start over instead of waiting for the new count to catch up.
        const uint32_t ep = proc.viz.getEpoch();
        bool fresh = ringBand != band || cap != size_t (std::ceil (vizRate * 6.5)) || ep != lastEpoch;
        lastEpoch = ep;
        if (fresh)
        {
            cap = size_t (std::ceil (vizRate * 6.5));
            ring.assign (cap, VizEntry { 0, 0, 0, 0 });
            now = 0; haveFrom = 0; ringBand = band;
        }
        if (! frozen)
        {
            uint64_t from = 0;
            uint64_t newNow = proc.viz.read (band, fresh ? 0 : now, cap, scratch, from);
            if (! fresh && newNow < now)   // went backwards without an epoch change: resync
            {
                std::fill (ring.begin(), ring.end(), VizEntry { 0, 0, 0, 0 });
                newNow = proc.viz.read (band, 0, cap, scratch, from);
                fresh = true;
            }
            if (fresh) { now = from; haveFrom = from; }
            if (from > now) for (uint64_t i = now; i < from; ++i) ring[size_t (i % cap)] = { 0, 0, 0, 0 };
            uint64_t idx = from;
            for (auto& e : scratch) ring[size_t ((idx++) % cap)] = e;
            now = std::max (now, newNow);

            updMeter (mIn, proc.meterIn.take());
            updMeter (mOut, proc.meterOut.take());
            analyse();
        }
        for (int i = 0; i < 128; ++i) mask[size_t (i)] = proc.spectral.displayMask[size_t (i)].load (std::memory_order_relaxed);

        if (! frozen && tMs - lastTxt > 120)
        {
            lastTxt = tMs;
            double gr = 0;
            const uint64_t span = std::min<uint64_t> (now - haveFrom, uint64_t (vizRate * 0.3));
            for (uint64_t i = 1; i <= span && cap; ++i)
            {
                const auto& e = ring[size_t ((now - i) % cap)];
                const double a = std::max (std::fabs (e.inMin), std::fabs (e.inMax)), b = std::max (std::fabs (e.outMin), std::fabs (e.outMax));
                if (a > 1e-4 && b > 1e-5 && a > b) gr = std::max (gr, 20 * std::log10 (a / b));
            }
            if (P (kBypass) >= 0.5) gr = 0;
            grDb = gr;
            clipAct = std::min (1.0, gr / 2);
        }
    }

    static std::string fmtPk (double db)
    {
        if (db <= -60) return "\xE2\x88\x92\xE2\x88\x9E";
        const double r = std::round (db * 10) / 10;
        char b[16]; std::snprintf (b, sizeof (b), "%s%.1f", r > 0 ? "+" : (db < -0.05 ? "\xE2\x88\x92" : ""), std::fabs (r));
        return b;
    }

private:
    using clock = std::chrono::steady_clock;
    std::atomic<int>& selRef;
    clock::time_point lastFrame, start = clock::now(), saveAt;
    bool pendingSave = false;
    double lastTxt = -1e9;
    int lastProcMode = -1;
    uint32_t lastEpoch = 0;
    std::vector<VizEntry> scratch;
    std::array<float, kFft> hann {}, buf {};
    std::vector<std::complex<float>> fftBuf = std::vector<std::complex<float>> (kFft);

    void updMeter (Meter& m, float peak)
    {
        const double db = peak > 1e-6f ? 20 * std::log10 (double (peak)) : -120;
        m.level = db > m.level ? db : std::max (db, m.level - 24 * dt);
        if (db >= m.hold || tMs - m.holdAt > 1500) { m.hold = std::max (db, m.level); m.holdAt = tMs; }
    }

    void analyse()
    {
        proc.spectrum.read (buf.data(), kFft);
        for (int i = 0; i < kFft; ++i) fftBuf[size_t (i)] = { buf[size_t (i)] * hann[size_t (i)], 0.f };
        // in-place radix-2 FFT
        const int n = kFft;
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (fftBuf[size_t (i)], fftBuf[size_t (j)]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = -2 * kPi / len;
            const std::complex<float> wl (float (std::cos (ang)), float (std::sin (ang)));
            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w (1.f, 0.f);
                for (int j = 0; j < len / 2; ++j)
                {
                    const auto u = fftBuf[size_t (i + j)], v = fftBuf[size_t (i + j + len / 2)] * w;
                    fftBuf[size_t (i + j)] = u + v; fftBuf[size_t (i + j + len / 2)] = u - v;
                    w *= wl;
                }
            }
        }
        for (int k = 1; k < kFft / 2; ++k)
        {
            const double mag = std::abs (fftBuf[size_t (k)]) / (kFft / 4.0);
            const double db = 20 * std::log10 (mag + 1e-9) + 4.5 * std::log2 (k * engineSr / kFft / 1000.0);   // +4.5 dB/oct tilt
            float& s = specDb[size_t (k)];
            s = db > s ? float (s + (db - s) * 0.6) : float (std::max (db, double (s) - 40 * dt));
        }
    }
};

} // namespace scalper::ui
