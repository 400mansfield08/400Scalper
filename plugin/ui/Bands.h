#pragma once
// 400Scalper UI — the multiband panel (Sample mode) and the SPECTRUM display (Spectral mode).
//
// Band panel (ShaperBox style): drag in from the ‹ › edge tabs or right-click to add a band
// (max 4), drag crossovers, double-click a crossover to remove it, click a band to select it,
// S = solo. Header shows the selected band and the 6 / 12 / 24 dB slope switch.

#include "Widgets.h"

namespace scalper::ui {

inline void drawFreqGrid (IGraphics& g, Model& m, const IRECT& r)
{
    for (double f : { 100.0, 1000.0, 10000.0 })
        g.FillRect (rgb (0x151515), IRECT (r.L + std::round (fToX (f, r.W())), r.T, r.L + std::round (fToX (f, r.W())) + 1, r.B));
    const char* lbl[] = { "100", "1k", "10k" };
    const double fr[] = { 100.0, 1000.0, 10000.0 };
    for (int i = 0; i < 3; ++i) m.tk.draw (g, lbl[i], r.L + fToX (fr[i], r.W()) + 3, r.T + 7, 8, col::dim);
}

inline float levelAt (const Model& m, float x, float w)
{
    const double f = xToF (x + 0.5f, w), kf = f * Model::kFft / m.engineSr;
    const int k0 = std::clamp (int (std::floor (kf)), 1, Model::kFft / 2 - 2);
    const double t = std::clamp (kf - std::floor (kf), 0.0, 1.0);
    return float (m.specDb[size_t (k0)] * (1 - t) + m.specDb[size_t (k0 + 1)] * t);
}

class BandPanel : public Base
{
public:
    using Base::Base;

    void Draw (IGraphics& g) override
    {
        const IRECT& r = mRECT;
        const int n = m.nBands(), sel = m.sel();
        const auto xs = m.xovers();

        // ---- header ----
        const float hy = r.T + 11;
        const char* sl[] = { "6 dB", "12 dB", "24 dB" };
        float bw[3], total = 4;
        for (int i = 0; i < 3; ++i) { bw[i] = m.tk.width (g, sl[i], 9, 0.04f) + 10; total += bw[i] + (i ? 1 : 0); }
        const IRECT seg (r.R - total, hy - 12, r.R, hy + 12);

        std::string head = n > 1 ? "BAND " : "BANDS ";
        const float hw = m.tk.width (g, head.c_str(), 9, 0.1f);
        m.tk.draw (g, head.c_str(), r.L, hy, 9, col::muted, 0.1f);
        const std::string selTxt = n > 1 ? std::to_string (sel + 1) + " \xC2\xB7 " + m.bandRange (sel) : "\xC2\xB7 OFF";
        m.tk.draw (g, m.tk.fit (g, selTxt, 9, 0.1f, seg.L - 6 - (r.L + hw + 2)).c_str(), r.L + hw + 2, hy, 9, m.accent, 0.1f);

        box (g, seg, col::panel, col::b262, 4);
        const int slope = int (std::lround (m.P (kSlope)));
        float sx = seg.L + 2;
        for (int i = 0; i < 3; ++i)
        {
            slopeBtn[i] = IRECT (sx, seg.T + 3, sx + bw[i], seg.B - 3);
            if (i == slope) g.FillRoundRect (col::b262, slopeBtn[i], 3);
            m.tk.draw (g, sl[i], slopeBtn[i].MW(), hy, 9, i == slope ? m.accent : col::muted, 0.04f, EAlign::Center);
            sx += bw[i] + 1;
        }

        // ---- spectrum box ----
        spec = IRECT (r.L, r.T + 27, r.R, r.B);
        box (g, spec, col::well, col::line, 4);
        const IRECT in = spec.GetPadded (-1);
        const float w = in.W(), h = in.H();
        g.PathClipRegion (in);
        edges.clear();
        edges.push_back (0);
        for (double f : xs) edges.push_back (fToX (f, w));
        edges.push_back (w);
        if (n > 1)
        {
            g.FillRect (withAlpha (m.accent, 0.10f), IRECT (in.L + edges[size_t (sel)], in.T, in.L + edges[size_t (sel + 1)], in.B));
            g.FillRect (m.accent, IRECT (in.L + edges[size_t (sel)], in.T, in.L + edges[size_t (sel + 1)], in.T + 2));
        }
        drawFreqGrid (g, m, in);
        auto yOf = [&] (double db) { return in.T + h - 14 - float ((std::clamp (db, -72.0, 6.0) + 72) / 78) * (h - 22); };
        g.PathClear();
        g.PathMoveTo (in.L, in.B);
        for (float x = 0; x <= w; x += 1) g.PathLineTo (in.L + x, yOf (levelAt (m, x, w)));
        g.PathLineTo (in.R, in.B);
        g.PathClose();
        g.PathFill (IPattern (withAlpha (col::grey, 0.35f)));
        g.PathClear();
        for (float x = 0; x <= w; x += 1) { if (x == 0) g.PathMoveTo (in.L, yOf (levelAt (m, 0, w))); else g.PathLineTo (in.L + x, yOf (levelAt (m, x, w))); }
        g.PathStroke (IPattern (col::grey9a), 1.f);

        bool anySolo = false;
        for (int b = 0; b < n; ++b) anySolo |= m.soloOn (b);
        if (n > 1 && anySolo)
            for (int b = 0; b < n; ++b)
                if (! m.soloOn (b)) g.FillRect (IColor (158, 7, 7, 7), IRECT (in.L + edges[size_t (b)], in.T, in.L + edges[size_t (b + 1)], in.B));

        // band numbers + solo buttons
        for (int b = 0; b < kMaxBands; ++b) soloBtn[b] = IRECT();
        if (n > 1)
            for (int b = 0; b < n; ++b)
            {
                const float mid = in.L + (edges[size_t (b)] + edges[size_t (b + 1)]) * 0.5f;
                m.tk.draw (g, std::to_string (b + 1).c_str(), mid, in.B - 8, 9, b == sel ? m.accent : col::muted, 0.f, EAlign::Center);
                const float sr = in.L + edges[size_t (b + 1)] - 3;
                soloBtn[b] = IRECT (sr - 18, in.T + 3, sr, in.T + 21);
                const bool on = m.soloOn (b);
                box (g, soloBtn[b], on ? m.accent : col::panel16, on ? m.accent : col::b3a, 3);
                m.tk.draw (g, "S", soloBtn[b].MW(), soloBtn[b].MH(), 9, on ? col::bg : col::btn, 0.f, EAlign::Center);
            }

        // crossover handles
        for (size_t i = 0; i < xs.size(); ++i)
        {
            const float x = in.L + edges[i + 1];
            g.FillRect (withAlpha (col::text, 0.75f), IRECT (x - 1, in.T, x + 1, in.B));
            const std::string t = fmtHz (xs[i]);
            const float tw = m.tk.width (g, t.c_str(), 8) + 8;
            const bool flip = edges[i + 1] > w - 46;
            const IRECT lab = flip ? IRECT (x - 3 - tw, in.T + 24, x - 3, in.T + 37) : IRECT (x + 3, in.T + 24, x + 3 + tw, in.T + 37);
            box (g, lab, col::panel1c, col::b333, 2);
            m.tk.draw (g, t.c_str(), lab.MW(), lab.MH(), 8, col::text, 0.f, EAlign::Center);
        }

        // edge tabs (drag in to add a band)
        edgeL = edgeR = IRECT();
        if (n < kMaxBands)
        {
            edgeL = IRECT (in.L, in.MH() - 17, in.L + 14, in.MH() + 17);
            edgeR = IRECT (in.R - 14, in.MH() - 17, in.R, in.MH() + 17);
            g.FillRoundRect (col::panel16, edgeL, 0, 4, 4, 0);
            g.DrawRoundRect (col::b333, edgeL.GetPadded (-0.5f), 0, 4, 4, 0);
            g.FillRoundRect (col::panel16, edgeR, 4, 0, 0, 4);
            g.DrawRoundRect (col::b333, edgeR.GetPadded (-0.5f), 4, 0, 0, 4);
            m.tk.draw (g, "\xE2\x80\xB9", edgeL.MW(), edgeL.MH(), 11, col::btn, 0.f, EAlign::Center);
            m.tk.draw (g, "\xE2\x80\xBA", edgeR.MW(), edgeR.MH(), 11, col::btn, 0.f, EAlign::Center);
        }

        // context menu
        if (ctxOpen) drawCtx (g, in);
        g.PathClipRegion();
    }

    // ---- mouse ----
    void OnMouseDown (float x, float y, const IMouseMod& mod) override
    {
        drag = None;
        for (int i = 0; i < 3; ++i) if (slopeBtn[i].Contains (x, y)) { m.edit (kSlope, i); return; }
        if (! spec.Contains (x, y)) { ctxOpen = false; return; }
        const IRECT in = spec.GetPadded (-1);
        const float lx = x - in.L, w = in.W();

        if (ctxOpen)
        {
            for (int i = 0; i < 3; ++i)
                if (ctxItem[i].Contains (x, y) && ctxEnabled[i])
                {
                    if (i == 0) m.addCrossover (ctxF);
                    if (i == 1) m.removeCrossover (ctxIdx);
                    if (i == 2) m.singleBand();
                }
            ctxOpen = false;
            return;
        }

        const int xo = hitCrossover (lx);
        if (mod.R) { openCtx (lx, y - in.T, xo); return; }

        for (int b = 0; b < m.nBands(); ++b)
            if (soloBtn[b].Contains (x, y)) { m.edit (kSolo1 + b, m.soloOn (b) ? 0 : 1); return; }
        if (edgeL.Contains (x, y) || edgeR.Contains (x, y))
        {
            int idx = m.addCrossover (xToF (lx, w));
            if (idx < 0) idx = m.addCrossover (edgeL.Contains (x, y) ? 60.0 : 8000.0);
            if (idx >= 0) { drag = Xover; dragIdx = idx; m.begin (kXover1 + idx); }
            return;
        }
        if (xo >= 0) { drag = Xover; dragIdx = xo; m.begin (kXover1 + xo); return; }
        // select the band under the mouse
        for (int b = 0; b + 1 < int (edges.size()); ++b)
            if (lx >= edges[size_t (b)] && lx < edges[size_t (b + 1)]) { m.selectBand (b); break; }
    }
    void OnMouseDrag (float x, float, float, float, const IMouseMod&) override
    {
        if (drag != Xover) return;
        const IRECT in = spec.GetPadded (-1);
        m.moveCrossover (dragIdx, xToF (x - in.L, in.W()));
    }
    void OnMouseUp (float, float, const IMouseMod&) override
    {
        if (drag == Xover) m.end (kXover1 + dragIdx);
        drag = None;
    }
    void OnMouseDblClick (float x, float y, const IMouseMod& mod) override
    {
        const IRECT in = spec.GetPadded (-1);
        const int xo = spec.Contains (x, y) ? hitCrossover (x - in.L) : -1;
        if (xo >= 0 && ! ctxOpen) { m.removeCrossover (xo); return; }
        OnMouseDown (x, y, mod);   // otherwise behave like a click (e.g. fast solo toggles)
    }
    void OnMouseOver (float x, float y, const IMouseMod& mod) override
    {
        IControl::OnMouseOver (x, y, mod);
        hx = x; hy = y;
        const IRECT in = spec.GetPadded (-1);
        if (! ctxOpen && spec.Contains (x, y) && (hitCrossover (x - in.L) >= 0 || edgeL.Contains (x, y) || edgeR.Contains (x, y))) cursor (ECursor::SIZEWE);
        else if (spec.Contains (x, y) || slopeBtn[0].Contains (x, y) || slopeBtn[1].Contains (x, y) || slopeBtn[2].Contains (x, y)) cursor (ECursor::HAND);
        else cursor (ECursor::ARROW);
    }
    void OnMouseOut() override { IControl::OnMouseOut(); hx = hy = -1; cursor (ECursor::ARROW); }
    void Hide (bool hide) override { if (hide) ctxOpen = false; IControl::Hide (hide); }

private:
    enum Drag { None, Xover } drag = None;
    int dragIdx = -1;
    IRECT spec, slopeBtn[3], soloBtn[kMaxBands], edgeL, edgeR;
    std::vector<float> edges;
    bool ctxOpen = false;
    float hx = -1, hy = -1;
    float ctxX = 0, ctxY = 0;
    double ctxF = 1000;
    int ctxIdx = -1;
    IRECT ctxItem[3];
    bool ctxEnabled[3] {};

    int hitCrossover (float lx) const
    {
        for (size_t i = 1; i + 1 < edges.size(); ++i)
            if (std::fabs (lx - edges[i]) <= 8) return int (i - 1);
        return -1;
    }
    void openCtx (float lx, float ly, int xoIdx)
    {
        const IRECT in = spec.GetPadded (-1);
        ctxF = xToF (lx, in.W());
        ctxIdx = xoIdx;
        ctxEnabled[0] = m.nBands() < kMaxBands && xoIdx < 0;
        ctxEnabled[1] = xoIdx >= 0;
        ctxEnabled[2] = m.nBands() > 1;
        ctxX = lx; ctxY = ly;   // clamped when drawn (the width depends on the text)
        ctxOpen = true;
    }
    void drawCtx (IGraphics& g, const IRECT& in)
    {
        const char* items[] = { "ADD BAND HERE", "REMOVE CROSSOVER", "SINGLE BAND" };
        float mw = 120;
        for (auto* it : items) mw = std::max (mw, m.tk.width (g, it, 9, 0.06f) + 22);
        const float mh = 3 + 3 * 24 + 3;
        const float x0 = std::clamp (ctxX, 0.f, std::max (0.f, in.W() - mw)), y0 = std::clamp (ctxY, 0.f, std::max (0.f, in.H() - mh));
        const IRECT r (in.L + x0, in.T + y0, in.L + x0 + mw, in.T + y0 + mh);
        box (g, r, col::panel16, col::b333, 4);
        float yy = r.T + 3;
        for (int i = 0; i < 3; ++i)
        {
            ctxItem[i] = IRECT (r.L + 3, yy, r.R - 3, yy + 24);
            if (ctxEnabled[i] && ctxItem[i].Contains (hx, hy))
                g.FillRoundRect (col::b262, ctxItem[i], 3);
            m.tk.draw (g, items[i], ctxItem[i].L + 8, ctxItem[i].MH(), 9, ctxEnabled[i] ? col::text : rgb (0x5A5A5A), 0.06f);
            yy += 24;
        }
    }
};

// ---------------------------------------------------------------------------
// Spectral mode: what is being clipped (accent) vs. what DETAIL keeps (complementary hue).
class SpecClip : public Base
{
public:
    using Base::Base;
    void Draw (IGraphics& g) override
    {
        const IRECT& r = mRECT;
        const float hy = r.T + 11;
        m.tk.draw (g, "SPECTRUM", r.L, hy, 9, col::muted, 0.1f);
        // legend (right aligned)
        const float wDet = m.tk.width (g, "DETAIL KEPT", 9, 0.1f), wClip = m.tk.width (g, "CLIPPED", 9, 0.1f);
        float lx = r.R - wDet;
        m.tk.draw (g, "DETAIL KEPT", lx, hy, 9, col::muted, 0.1f);
        g.FillRoundRect (m.comp, IRECT (lx - 12, hy - 4, lx - 4, hy + 4), 2);
        lx -= 12 + 10 + wClip;
        if (lx - 12 > r.L + m.tk.width (g, "SPECTRUM", 9, 0.1f) + 6)
        {
            m.tk.draw (g, "CLIPPED", lx, hy, 9, col::muted, 0.1f);
            g.FillRoundRect (m.accent, IRECT (lx - 12, hy - 4, lx - 4, hy + 4), 2);
        }

        const IRECT bx (r.L, r.T + 27, r.R, r.B);
        box (g, bx, col::well, rgb (0x2A2A2A), 4);
        const IRECT in = bx.GetPadded (-1);
        const float w = in.W(), h = in.H();
        const int W = std::max (1, int (w));
        if (int (smMask.size()) != W) smMask.assign (size_t (W), 0.f);
        g.PathClipRegion (in);
        drawFreqGrid (g, m, in);
        auto yOf = [&] (double db) { return in.T + h - 4 - float ((std::clamp (db, -72.0, 6.0) + 72) / 78) * (h - 18); };
        const double d = m.P (kDetail) / 100.0, act = m.clipAct;
        const float k = float (std::min (1.0, m.dt * 15));
        for (int x = 0; x < W; ++x)
        {
            const float gpos = (x + 0.5f) / w * 127.f;
            const int g0 = std::clamp (int (std::floor (gpos)), 0, 127), g1 = std::min (127, g0 + 1);
            const float u = gpos - float (g0);
            const float mv = m.mask[size_t (g0)] * (1 - u) + m.mask[size_t (g1)] * u;
            if (! m.frozen) smMask[size_t (x)] += (mv - smMask[size_t (x)]) * k;
            const double mm = smMask[size_t (x)];
            const double loud = mm * act, keep = (1 - mm) * d, rest = std::max (0.0, 1 - loud - keep);
            auto mix = [&] (int a, int b) { return int (std::lround (a * loud + b * keep + 110 * rest)); };
            const IColor c (217, mix (m.accent.R, m.comp.R), mix (m.accent.G, m.comp.G), mix (m.accent.B, m.comp.B));
            const float y = yOf (levelAt (m, float (x), w));
            g.FillRect (c, IRECT (in.L + x, y, in.L + x + 1.05f, in.B));
        }
        g.PathClear();
        for (int x = 0; x < W; ++x) { const float y = yOf (levelAt (m, float (x), w)); if (x == 0) g.PathMoveTo (in.L, y); else g.PathLineTo (in.L + x, y); }
        g.PathStroke (IPattern (withAlpha (col::text, 0.6f)), 1.f);
        g.PathClipRegion();
    }
private:
    std::vector<float> smMask;
};

} // namespace scalper::ui
