#pragma once
// 400Scalper UI — the scope: orange input behind the grey output (orange shows only where the
// clipper removed level), dB gutter, draggable threshold, TIME / ZOOM / FOLLOW / FREEZE / FIT.
// FOLLOW on = the view scrolls with the playhead. Off = still view with a sweeping playhead.

#include "Widgets.h"

namespace scalper::ui {

class Scope : public Base
{
public:
    using Base::Base;

    struct Geom { float W, H, c, s, sz, cz; double T; };
    Geom geom() const
    {
        const float W = mRECT.W() - 2 - 40, H = mRECT.H() - 2;
        const float c = H / 2, s = (H / 2 - 12) / 1.15f;
        const double T = dbToA (m.P (m.thrId()));
        const float z = float (m.zoom);
        return { W, H, c, s, s * z, c, T };   // zoom scales symmetrically around the centre line
    }

    void Draw (IGraphics& g) override
    {
        box (g, mRECT, col::well, col::line, 6);
        const IRECT in = mRECT.GetPadded (-1);
        const Geom G = geom();
        const float ox = in.L, oy = in.T;
        auto Y = [&] (double v) { return oy + G.cz - float (v) * G.sz; };

        g.PathClipRegion (IRECT (in.L, in.T, in.L + G.W, in.B));
        // grid
        const auto ticks = tickList (G);
        g.PathClear();
        for (auto& t : ticks)
            for (double a : { dbToA (t.first), -dbToA (t.first) })
            {
                const float y = std::round (Y (a)) + 0.5f;
                if (y > oy && y < oy + G.H) { g.PathMoveTo (ox, y); g.PathLineTo (ox + G.W, y); }
            }
        g.PathStroke (IPattern (rgb (0x161616)), 1.f);
        if (G.cz > 0 && G.cz < G.H) g.DrawLine (rgb (0x1E1E1E), ox, std::round (oy + G.cz) + 0.5f, ox + G.W, std::round (oy + G.cz) + 0.5f);

        // traces
        if (m.cap)
        {
            const double winE = std::max (4.0, std::round (m.timeWin * m.vizRate));
            const int cols = int (std::floor (G.W));
            const double per = winE / std::max (1, cols);
            const int stride = std::max (1, int (std::floor (per / 48)));
            const bool sweep = ! m.follow;
            const uint64_t now = m.now;
            const uint64_t winI = uint64_t (winE);
            int64_t base; double phX = -1;
            if (sweep) { base = int64_t (now - now % winI); phX = double (now - uint64_t (base)) / per; }
            else base = int64_t (now) - int64_t (winI);
            const int64_t lo = std::max<int64_t> (int64_t (m.haveFrom), int64_t (now) - int64_t (m.cap) + 1);
            auto cyc = [&] (float y) { return std::clamp (y, oy - 2, oy + G.H + 2); };
            // Bypassed: the output IS the input, so draw just the input, in grey.
            const bool byp = m.P (kBypass) >= 0.5;
            for (int pass = byp ? 1 : 0; pass < 2; ++pass)
            {
                g.PathClear();
                for (int i = 0; i < cols; ++i)
                {
                    if (sweep && i > phX && i <= phX + 6) continue;
                    const int64_t j0 = int64_t (std::floor (i * per)), j1 = std::max (j0 + 1, int64_t (std::floor ((i + 1) * per)));
                    float mn = 1e9f, mx = -1e9f;
                    for (int64_t j = j0; j < j1; j += stride)
                    {
                        int64_t t = base + j;
                        if (sweep && t >= int64_t (now)) t -= int64_t (winI);
                        if (t < lo || t >= int64_t (now)) continue;
                        const auto& e = m.ring[size_t (uint64_t (t) % m.cap)];
                        const bool useIn = pass == 0 || byp;
                        const float a = useIn ? e.inMin : e.outMin, b = useIn ? e.inMax : e.outMax;
                        mn = std::min (mn, a); mx = std::max (mx, b);
                    }
                    if (mx < -1e8f) continue;
                    const float y0 = cyc (Y (mx)), y1 = cyc (Y (mn));
                    g.PathRect (IRECT (ox + i, y0, ox + i + 1.2f, y0 + std::max (1.f, y1 - y0)));
                }
                g.PathFill (IPattern (pass == 0 ? m.accent : col::grey));
            }
            if (sweep) g.FillRect (withAlpha (col::text, 0.7f), IRECT (ox + std::floor (float (phX)), oy, ox + std::floor (float (phX)) + 1, oy + G.H));
        }
        // threshold lines
        for (double a : { G.T, -G.T })
        {
            const float y = std::round (Y (a)) + 0.5f;
            if (y > oy && y < oy + G.H) dashedH (g, withAlpha (col::text, 0.55f), ox, ox + G.W, y);
        }
        if (m.frozen) g.FillRect (IColor (10, 255, 255, 255), IRECT (ox, oy, ox + G.W, oy + G.H));
        g.PathClipRegion();

        // gutter + ticks
        const IRECT gut (in.L + G.W, in.T, in.R, in.B);
        g.FillRect (col::gutter, gut);
        g.FillRect (rgb (0x1A1A1A), IRECT (gut.L, gut.T, gut.L + 1, gut.B));
        for (auto& t : ticks)
        {
            char b[16];
            if (t.first == 0) std::snprintf (b, sizeof (b), "0");
            else if (t.first > 0) std::snprintf (b, sizeof (b), "+%g", t.first);
            else std::snprintf (b, sizeof (b), "\xE2\x88\x92%g", std::fabs (t.first));
            m.tk.draw (g, b, gut.R - 6, t.second + oy, 9, col::muted, 0.f, EAlign::Far);
        }

        // threshold handle label
        {
            const float ly = oy + G.c - float (G.T) * G.sz;
            thrHit = IRECT();
            if (ly >= oy + 10 && ly <= oy + G.c - 4)   // hidden when zoomed past it
            {
            std::string txt = fmtDb (m.P (m.thrId()));
            if (m.nBands() > 1 && ! m.spectral()) txt = "B" + std::to_string (m.sel() + 1) + "  " + txt;
            const float w = m.tk.width (g, txt.c_str(), 10) + 14;
            const IRECT lab (ox + G.W - 6 - w, ly - 17, ox + G.W - 6, ly + 1);
            box (g, lab, col::panel1c, col::b333, 3);
            m.tk.draw (g, txt.c_str(), lab.MW(), lab.MH(), 10, col::text, 0.f, EAlign::Center);
            thrHit = IRECT (ox, ly - 10, ox + G.W, ly + 10);
            }
        }

        // scope tag (multiband / spectral)
        if (m.nBands() > 1 || m.spectral() || m.P (kBypass) >= 0.5)
        {
            std::string tag;
            if (m.P (kBypass) >= 0.5) tag = "BYPASSED";
            else if (m.spectral()) tag = "SPECTRAL \xC2\xB7 DETAIL " + std::to_string (int (std::lround (m.P (kDetail)))) + "%";
            else tag = "BAND " + std::to_string (m.sel() + 1) + " \xC2\xB7 " + m.bandRange (m.sel());
            const float w = m.tk.width (g, tag.c_str(), 9, 0.08f) + 14;
            const IRECT r (ox + 8, oy + 8, ox + 8 + w, oy + 8 + 18);
            box (g, r, IColor (235, 18, 18, 18), col::b333, 3);
            m.tk.draw (g, tag.c_str(), r.MW(), r.MH(), 9, m.accent, 0.08f, EAlign::Center);
        }

        drawTools (g, ox, oy + G.H);
    }

    // ---- mouse ----
    void OnMouseDown (float x, float y, const IMouseMod& mod) override
    {
        drag = None;
        if (mod.R) return;
        if (tSlider.Contains (x, y)) { drag = Time; setSlider (Time, x); return; }
        if (zSlider.Contains (x, y)) { drag = Zoom; setSlider (Zoom, x); return; }
        if (followBtn.Contains (x, y)) { m.follow = ! m.follow; return; }
        if (freezeBtn.Contains (x, y)) { m.frozen = ! m.frozen; return; }
        if (fitBtn.Contains (x, y)) { m.fitZoom(); return; }
        if (toolsArea.Contains (x, y)) return;
        if (thrHit.Contains (x, y))
        {
            drag = Thr; dragId = m.thrId();
            dragA = dbToA (m.P (dragId)); dragY = 0;
            m.begin (dragId);
        }
    }
    void OnMouseDrag (float x, float, float, float dY, const IMouseMod&) override
    {
        if (drag == Time || drag == Zoom) { setSlider (drag, x); return; }
        if (drag != Thr) return;
        dragY += dY;
        const double a = std::max (0.0631, dragA - dragY / geom().sz);
        m.set (dragId, 20 * std::log10 (a));
    }
    void OnMouseUp (float, float, const IMouseMod&) override
    {
        if (drag == Thr) m.end (dragId);
        drag = None;
    }
    void OnMouseDblClick (float x, float y, const IMouseMod&) override
    {
        if (thrHit.Contains (x, y)) m.edit (m.thrId(), -6.0);
    }
    void OnMouseWheel (float, float, const IMouseMod& mod, float d) override
    {
        const double f = d > 0 ? 1.15 : 1 / 1.15;
        if (mod.S) m.setTime (m.timeWin / f); else m.setZoom (m.zoom * f);
    }
    void OnMouseOver (float x, float y, const IMouseMod& mod) override
    {
        IControl::OnMouseOver (x, y, mod);
        if (toolsArea.Contains (x, y)) cursor (ECursor::HAND);
        else cursor (thrHit.Contains (x, y) ? ECursor::SIZENS : ECursor::ARROW);
    }
    void OnMouseOut() override { IControl::OnMouseOut(); cursor (ECursor::ARROW); }

private:
    enum Drag { None, Thr, Time, Zoom } drag = None;
    int dragId = -1;
    double dragA = 0, dragY = 0;
    IRECT thrHit, tSlider, zSlider, followBtn, freezeBtn, fitBtn, toolsArea;

    std::vector<std::pair<double, float>> tickList (const Geom& G) const
    {
        std::vector<std::pair<double, float>> out;
        float lastY = -1e9f;
        for (double db : { 18.0, 12.0, 6.0, 3.0, 0.0, -1.0, -2.0, -3.0, -4.5, -6.0, -9.0, -12.0, -18.0, -24.0, -36.0, -48.0, -60.0 })
        {
            const float y = G.cz - float (dbToA (db)) * G.sz;
            if (y < 8 || y > G.cz - 10 || y > G.H - 10 || y - lastY < 24) continue;
            out.push_back ({ db, y });
            lastY = y;
        }
        return out;
    }

    void setSlider (Drag which, float x)
    {
        const IRECT& r = which == Time ? tSlider : zSlider;
        const double t = std::clamp ((x - r.L - 6) / std::max (1.f, r.W() - 12), 0.f, 1.f);
        if (which == Time) m.setTime (0.05 * std::pow (120.0, t));
        else m.setZoom (Model::kZoomMin * std::pow (Model::kZoomMax / Model::kZoomMin, t));
    }

    void drawSlider (IGraphics& g, const IRECT& r, double t)
    {
        const float cy = r.MH(), tx = r.L + 6 + float (t) * (r.W() - 12);
        g.FillRoundRect (col::b333, IRECT (r.L, cy - 1.5f, r.R, cy + 1.5f), 1.5f);
        g.FillRoundRect (m.accent, IRECT (r.L, cy - 1.5f, tx, cy + 1.5f), 1.5f);
        g.FillCircle (col::text, tx, cy, 6);
    }

    void drawTools (IGraphics& g, float left, float bottom)
    {
        const bool compact = m.L.compact;
        const float sw = compact ? 56.f : 84.f, gp = compact ? 4.f : 6.f, pad = compact ? 6.f : 8.f;
        const float valW = compact ? 36.f : 44.f, zValW = compact ? 24.f : 30.f;
        const float h = 30, y0 = bottom - 8 - h, cy = y0 + h / 2;
        const IColor glass (235, 18, 18, 18);
        float x = left + 8;

        auto group = [&] (const char* lbl, IRECT& slider, double t, const std::string& val, float vw) {
            const float lw = m.tk.width (g, lbl, 9, 0.08f);
            const float w = pad + lw + gp + sw + gp + vw + pad;
            box (g, IRECT (x, y0, x + w, y0 + h), glass, col::b262, 5);
            m.tk.draw (g, lbl, x + pad, cy, 9, col::muted, 0.08f);
            slider = IRECT (x + pad + lw + gp, cy - 10, x + pad + lw + gp + sw, cy + 10);
            drawSlider (g, slider, t);
            m.tk.draw (g, val.c_str(), x + w - pad, cy, 9, col::text, 0.08f, EAlign::Far);
            x += w + (compact ? 4.f : 6.f);
        };
        char tt[16];
        if (m.timeWin < 1) std::snprintf (tt, sizeof (tt), "%d ms", int (std::lround (m.timeWin * 1000))); else std::snprintf (tt, sizeof (tt), "%.1f s", m.timeWin);
        char zt[16]; std::snprintf (zt, sizeof (zt), "%g\xC3\x97", m.zoom < 1 ? std::round (m.zoom * 100) / 100 : std::round (m.zoom * 10) / 10);
        const float x0 = x;
        group ("TIME", tSlider, std::log (m.timeWin / 0.05) / std::log (120.0), tt, valW);
        group ("ZOOM", zSlider, std::log (m.zoom / Model::kZoomMin) / std::log (Model::kZoomMax / Model::kZoomMin), zt, zValW);

        auto button = [&] (const char* lbl, IRECT& r, bool pressed) {
            const float w = m.tk.width (g, lbl, 9, 0.08f) + 2 * (compact ? 6.f : 8.f);
            r = IRECT (x, y0, x + w, y0 + h);
            box (g, r, glass, pressed ? m.accent : col::b262, 5);
            m.tk.draw (g, lbl, r.MW(), cy, 9, pressed ? m.accent : col::btn, 0.08f, EAlign::Center);
            x += w + (compact ? 4.f : 6.f);
        };
        button ("FOLLOW", followBtn, m.follow);
        button ("FREEZE", freezeBtn, m.frozen);
        button ("FIT", fitBtn, false);
        toolsArea = IRECT (x0, y0, x, y0 + h);
    }
};

} // namespace scalper::ui
