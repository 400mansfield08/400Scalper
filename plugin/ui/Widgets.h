#pragma once
// 400Scalper UI — header, meters, knobs, DELTA pill, curve, footer, accent picker.

#include "Model.h"

namespace scalper::ui {

// Every control redraws each frame (the UI is a live display) and shares the Model.
class Base : public IControl
{
public:
    explicit Base (Model& model) : IControl (IRECT()), m (model) {}
    bool IsDirty() override { return ! IsHidden(); }
protected:
    Model& m;
    void cursor (ECursor c) { if (GetUI()) GetUI()->SetMouseCursor (c); }
};

inline void box (IGraphics& g, const IRECT& r, const IColor& fill, const IColor& border, float radius)
{
    g.FillRoundRect (fill, r, radius);
    g.DrawRoundRect (border, r.GetPadded (-0.5f), std::max (0.f, radius - 0.5f), nullptr, 1.f);
}

inline IStrokeOptions roundCaps()
{
    IStrokeOptions o; o.mCapOption = ELineCap::Round; o.mJoinOption = ELineJoin::Round; return o;
}

inline void dashedH (IGraphics& g, const IColor& c, float x0, float x1, float y, float on = 4, float off = 4)
{
    g.PathClear();
    for (float x = x0; x < x1; x += on + off) { g.PathMoveTo (x, y); g.PathLineTo (std::min (x + on, x1), y); }
    g.PathStroke (IPattern (c), 1.f);
}

// The Always 400 "A" mark (traced from Always400Logo.png), drawn into r.
inline void drawLogo (IGraphics& g, const IRECT& r, const IColor& c)
{
    const float vx = 328, vy = 225, vw = 1343, vh = 980;
    const float s = std::min (r.W() / vw, r.H() / vh);
    const float ox = r.L + (r.W() - vw * s) * 0.5f, oy = r.T + (r.H() - vh * s) * 0.5f;
    auto P = [&] (float x, float y, bool move) { if (move) g.PathMoveTo (ox + (x - vx) * s, oy + (y - vy) * s); else g.PathLineTo (ox + (x - vx) * s, oy + (y - vy) * s); };
    static const float a[][2] = { {328,1160},{1263,225},{1308,225},{1671,1155},{1671,1205},{1320,1205},{1320,1157},{1088,1157},{1267,978},{1267,916},{978,1205},{328,1205} };
    static const float b[][2] = { {1402,225},{1671,225},{1671,916} };
    g.PathClear();
    for (size_t i = 0; i < sizeof (a) / sizeof (a[0]); ++i) P (a[i][0], a[i][1], i == 0);
    g.PathClose();
    for (size_t i = 0; i < sizeof (b) / sizeof (b[0]); ++i) P (b[i][0], b[i][1], i == 0);
    g.PathClose();
    g.PathFill (IPattern (c));
}

// ---------------------------------------------------------------------------
// Window background: panels, borders, Spectral-mode styling of the control row, footer strip.
class Background : public Base
{
public:
    using Base::Base;
    void Draw (IGraphics& g) override
    {
        const Rects& L = m.L;
        g.FillRect (col::bg, IRECT (0, 0, L.W, L.H));
        g.FillRect (col::hdr, L.hdr);
        g.FillRect (col::line, IRECT (0, L.hdr.B - 1, L.W, L.hdr.B));
        if (m.spectral())
        {
            g.FillRect (rgb (0x111111), L.foot);
            g.FillRect (m.accent, IRECT (0, L.foot.T, L.W, L.foot.T + 1));
            m.tk.draw (g, "SPECTRAL MODE", 14, L.foot.T + 12, 9, m.accent, 0.14f);
        }
        if ((m.spectral() ? 1 : 0) != m.proc.procMode.load())
            m.tk.draw (g, "SWITCHING MODE\xE2\x80\xA6", L.W - 14, L.foot.T + 12, 9, col::muted, 0.1f, EAlign::Far);

        // footer
        g.FillRect (col::hdr, L.footer);
        g.FillRect (col::line, IRECT (0, L.footer.T, L.W, L.footer.T + 1));
        m.tk.draw (g, ("v" + m.version).c_str(), 14, L.footer.MH() + 0.5f, 9, col::dim, 0.1f);
    }
};

// Footer link: Always400.com
class FooterLink : public Base
{
public:
    using Base::Base;
    void Draw (IGraphics& g) override
    {
        m.tk.draw (g, "Always400.com", mRECT.R, mRECT.MH() + 0.5f, 9, GetMouseIsOver() ? col::text : col::dim, 0.1f, EAlign::Far);
    }
    void OnMouseOver (float x, float y, const IMouseMod& mod) override { IControl::OnMouseOver (x, y, mod); cursor (ECursor::HAND); }
    void OnMouseOut() override { IControl::OnMouseOut(); cursor (ECursor::ARROW); }
    void OnMouseDown (float, float, const IMouseMod&) override { if (GetUI()) GetUI()->OpenURL ("https://always400.com"); }
};

// ---------------------------------------------------------------------------
// Header: 400SCALPER wordmark (opens the hidden accent picker), SAMPLE / SPECTRAL, CLIP readout, logo.
class Header : public Base
{
public:
    using Base::Base;
    void Draw (IGraphics& g) override
    {
        const IRECT& r = mRECT;
        const float cy = r.MH();
        // wordmark
        const float wf = 22, ws = 0.02f;
        const float w400 = m.tk.width (g, "400", wf, ws), wSc = m.tk.width (g, "SCALPER", wf, ws);
        const float wordW = w400 + ws * wf + wSc;
        wordRect = IRECT (r.L + 14, cy - 17, r.L + 14 + wordW + 12, cy + 17);
        m.tk.draw (g, "400", r.L + 20, cy, wf, m.accent, ws);
        m.tk.draw (g, "SCALPER", r.L + 20 + w400 + ws * wf, cy, wf, col::text, ws);

        // CLIP readout + logo (right)
        const float logoW = 31, logoH = 22;
        const IRECT logo (r.R - 20 - logoW, cy - logoH / 2, r.R - 20, cy + logoH / 2);
        drawLogo (g, logo, col::text);
        char grTxt[32];
        if (m.grDb > 0.05) std::snprintf (grTxt, sizeof (grTxt), "\xE2\x88\x92%.1f dB", m.grDb); else std::snprintf (grTxt, sizeof (grTxt), "0.0 dB");
        const float valW = std::max (64.f, m.tk.width (g, grTxt, 11, 0.08f));
        const float valR = logo.L - 16;
        m.tk.draw (g, grTxt, valR, cy, 11, col::text, 0.08f, EAlign::Far);
        const std::string clipLbl = m.nBands() > 1 && ! m.spectral() ? "CLIP B" + std::to_string (m.sel() + 1) : "CLIP";
        const float lblW = m.tk.width (g, clipLbl.c_str(), 11, 0.08f);
        const float lblX = valR - valW - 8 - lblW;
        m.tk.draw (g, clipLbl.c_str(), lblX, cy, 11, col::muted, 0.08f);
        g.FillCircle (m.grDb > 0.1 ? m.accent : col::off, lblX - 8 - 4, cy, 4);
        // BYPASS (left of the CLIP readout): power icon + label, filled with the accent when on
        const bool byp = m.P (kBypass) >= 0.5;
        const bool iconOnly = m.L.compact;   // narrow windows: power icon only
        const float bpTextW = m.tk.width (g, "BYPASS", 10, 0.08f);
        const float bpW = iconOnly ? 32.f : 10 + 12 + 7 + bpTextW + 11, bpH = 28;
        const float bpGap = m.L.compact ? 12.f : 18.f;
        bypassBtn = IRECT (lblX - 16 - bpGap - bpW, cy - bpH / 2, lblX - 16 - bpGap, cy + bpH / 2);
        const bool bpHover = GetMouseIsOver() && bypassBtn.Contains (hx, hy);
        if (byp) box (g, bypassBtn, m.accent, m.accent, 6);
        else box (g, bypassBtn, col::panel, bpHover ? rgb (0x4A4A4A) : col::b262, 6);
        const IColor ic = byp ? col::bg : (bpHover ? col::text : col::muted);
        const float icx = iconOnly ? bypassBtn.MW() : bypassBtn.L + 10 + 6, icy = cy;
        g.PathClear(); g.PathArc (icx, icy, 5.5f, 40, 320); g.PathStroke (IPattern (ic), 1.6f, roundCaps());
        g.DrawLine (ic, icx, icy - 7, icx, icy - 1.5f, nullptr, 1.6f);
        if (! iconOnly) m.tk.draw (g, "BYPASS", icx + 6 + 7, cy, 10, ic, 0.08f);
        const float clipL = bypassBtn.L;

        // SAMPLE / SPECTRAL segmented switch, spaced evenly between wordmark and readout
        const float segPad = m.L.compact ? 14.f : 24.f;
        const float bw0 = m.tk.width (g, "SAMPLE", 11, 0.08f) + segPad, bw1 = m.tk.width (g, "SPECTRAL", 11, 0.08f) + segPad;
        const float segW = 3 + bw0 + 2 + bw1 + 3, segH = 34;
        const float freeL = wordRect.R - 6, freeR = clipL;
        const float sx = freeL + std::max (0.f, (freeR - freeL - segW) * 0.5f);
        const IRECT seg (sx, cy - segH / 2, sx + segW, cy + segH / 2);
        box (g, seg, col::panel, col::b262, 6);
        btn[0] = IRECT (seg.L + 3, seg.T + 4, seg.L + 3 + bw0, seg.B - 4);
        btn[1] = IRECT (btn[0].R + 2, seg.T + 4, btn[0].R + 2 + bw1, seg.B - 4);
        const bool spec = m.spectral();
        for (int i = 0; i < 2; ++i)
        {
            const bool on = (i == 1) == spec;
            if (on) g.FillRoundRect (col::b262, btn[i], 4);
            m.tk.draw (g, i ? "SPECTRAL" : "SAMPLE", btn[i].MW(), cy, 11, on ? m.accent : col::muted, 0.08f, EAlign::Center);
        }
    }
    void OnMouseDown (float x, float y, const IMouseMod&) override
    {
        if (wordRect.Contains (x, y)) { m.pickerOpen = ! m.pickerOpen; return; }
        if (bypassBtn.Contains (x, y)) { m.edit (kBypass, m.P (kBypass) >= 0.5 ? 0 : 1); return; }
        if (btn[0].Contains (x, y) && m.spectral()) m.edit (kMode, 0);
        if (btn[1].Contains (x, y) && ! m.spectral()) m.edit (kMode, 1);
    }
    void OnMouseOver (float x, float y, const IMouseMod& mod) override
    {
        IControl::OnMouseOver (x, y, mod);
        hx = x; hy = y;
        cursor (wordRect.Contains (x, y) || btn[0].Contains (x, y) || btn[1].Contains (x, y) || bypassBtn.Contains (x, y) ? ECursor::HAND : ECursor::ARROW);
    }
    void OnMouseOut() override { IControl::OnMouseOut(); hx = hy = -1; cursor (ECursor::ARROW); }
private:
    IRECT wordRect, btn[2], bypassBtn;
    float hx = -1, hy = -1;
};

// ---------------------------------------------------------------------------
class Meter : public Base
{
public:
    Meter (Model& model, bool isOut) : Base (model), out (isOut) {}
    void Draw (IGraphics& g) override
    {
        const IRECT& r = mRECT;
        const auto& mt = out ? m.mOut : m.mIn;
        m.tk.draw (g, out ? "OUT" : "IN", r.MW(), r.T + 6, 9, col::muted, 0.1f, EAlign::Center);
        const IRECT well (r.MW() - 11, r.T + 18, r.MW() + 11, r.B - 18);
        box (g, well, col::well, col::line, 3);
        const IRECT in = well.GetPadded (-1);
        const float h = in.H();
        auto Y = [&] (double db) { return in.T + h - float ((std::clamp (db, -48.0, 6.0) + 48) / 54) * h; };
        const float y0 = Y (0), yl = Y (mt.level), yb = std::max (yl, y0);
        if (yb < in.B) g.FillRect (col::grey9a, IRECT (in.L + 3, yb, in.R - 3, in.B));
        if (yl < y0) g.FillRect (m.accent, IRECT (in.L + 3, yl, in.R - 3, y0));
        if (mt.hold > -48)
        {
            const float yh = std::round (Y (mt.hold));
            g.FillRect (mt.hold > 0 ? m.accent : col::text, IRECT (in.L + 3, yh, in.R - 3, yh + 2));
        }
        for (double db : { 0.0, -6.0, -12.0, -24.0 })
        {
            const float y = std::round (Y (db));
            g.FillRect (col::b3a, IRECT (in.L, y, in.L + 2, y + 1));
            g.FillRect (col::b3a, IRECT (in.R - 2, y, in.R, y + 1));
        }
        m.tk.draw (g, Model::fmtPk (mt.hold).c_str(), r.MW(), r.B - 6, 9, col::text, 0.f, EAlign::Center);
    }
private:
    bool out;
};

// ---------------------------------------------------------------------------
// House knob: 64 px arc knob with label and value. Drag up/down (Shift = fine),
// double-click resets, mouse wheel steps.
enum class KnobKind { In, Threshold, Knee, Detail, Out };

class Knob : public Base
{
public:
    Knob (Model& model, KnobKind k) : Base (model), kind (k) {}

    int paramId() const
    {
        switch (kind)
        {
            case KnobKind::In: return kInGain;
            case KnobKind::Out: return kOutGain;
            case KnobKind::Threshold: return m.thrId();
            case KnobKind::Knee: return m.kneeId();
            case KnobKind::Detail: return kDetail;
        }
        return kInGain;
    }
    const char* label() const
    {
        switch (kind)
        {
            case KnobKind::In: return "IN";
            case KnobKind::Out: return "OUT";
            case KnobKind::Threshold: return "THRESHOLD";
            case KnobKind::Knee: return "KNEE";
            case KnobKind::Detail: return "DETAIL";
        }
        return "";
    }
    bool bipolar() const { return kind == KnobKind::In || kind == KnobKind::Out; }
    bool percent() const { return kind == KnobKind::Knee || kind == KnobKind::Detail; }

    void Draw (IGraphics& g) override
    {
        const IRECT& r = mRECT;
        const int id = paramId();
        const auto& d = paramDefs()[size_t (id)];
        const double v = m.P (id);
        const IColor arcCol = kind == KnobKind::Detail ? m.comp : m.accent;
        const float sz = m.L.compact ? 9.f : 10.f, sp = m.L.compact ? 0.06f : 0.12f;
        m.tk.draw (g, label(), r.MW(), r.T + 6, sz, kind == KnobKind::Detail ? m.comp : col::muted, sp, EAlign::Center);

        const float cx = r.MW(), cy = r.T + 18 + 32;
        knobRect = IRECT (cx - 32, cy - 32, cx + 32, cy + 32);
        const float a = float (-135.0 + 270.0 * (v - d.min) / (d.max - d.min));
        g.PathClear(); g.PathArc (cx, cy, 27, -135, 135); g.PathStroke (IPattern (col::b262), 4, roundCaps());
        float a0 = -135, a1 = a;
        if (bipolar()) { a0 = std::min (0.f, a); a1 = std::max (0.f, a); }
        if (a1 - a0 >= 0.5f) { g.PathClear(); g.PathArc (cx, cy, 27, a0, a1); g.PathStroke (IPattern (arcCol), 4, roundCaps()); }
        g.FillCircle (col::panel1a, cx, cy, 19);
        g.DrawCircle (col::b333, cx, cy, 18.5f, nullptr, 1);
        const float rad = a * float (kPi) / 180.f;
        g.PathClear();
        g.PathMoveTo (cx + 7 * std::sin (rad), cy - 7 * std::cos (rad));
        g.PathLineTo (cx + 16 * std::sin (rad), cy - 16 * std::cos (rad));
        g.PathStroke (IPattern (col::text), 2.5f, roundCaps());

        const std::string txt = percent() ? fmtPct (v) : fmtDb (v);
        m.tk.draw (g, txt.c_str(), r.MW(), cy + 32 + 6 + 7, 11, col::text, 0.f, EAlign::Center);
    }

    void OnMouseDown (float, float, const IMouseMod& mod) override
    {
        if (mod.R) return;
        dragId = paramId();
        dragV = m.P (dragId);
        dragAcc = 0;
        m.begin (dragId);
    }
    void OnMouseDrag (float, float, float, float dY, const IMouseMod& mod) override
    {
        if (dragId < 0) return;
        const auto& d = paramDefs()[size_t (dragId)];
        dragAcc += -dY / (mod.S ? 800.0 : 200.0) * (d.max - d.min);
        m.set (dragId, dragV + dragAcc);
    }
    void OnMouseUp (float, float, const IMouseMod&) override
    {
        if (dragId >= 0) m.end (dragId);
        dragId = -1;
    }
    void OnMouseDblClick (float, float, const IMouseMod&) override
    {
        const int id = paramId();
        m.edit (id, paramDefs()[size_t (id)].def);
    }
    void OnMouseWheel (float, float, const IMouseMod& mod, float d) override
    {
        const int id = paramId();
        const auto& pd = paramDefs()[size_t (id)];
        const double step = (pd.max - pd.min) / (mod.S ? 480.0 : 96.0);
        m.edit (id, m.P (id) + (d > 0 ? step : -step));
    }
    void OnMouseOver (float x, float y, const IMouseMod& mod) override { IControl::OnMouseOver (x, y, mod); cursor (ECursor::SIZENS); }
    void OnMouseOut() override { IControl::OnMouseOut(); cursor (ECursor::ARROW); }

private:
    KnobKind kind;
    IRECT knobRect;
    int dragId = -1;
    double dragV = 0, dragAcc = 0;
};

// ---------------------------------------------------------------------------
// Small toggle pill above a knob: Δ DELTA (above THRESHOLD) and AUTO GAIN (above IN).
class Pill : public Base
{
public:
    Pill (Model& model, int param, const char* text) : Base (model), paramId (param), label (text) {}
    void setLabel (const char* text) { label = text; }
    void Draw (IGraphics& g) override
    {
        const float w = m.tk.width (g, label, 8, 0.12f) + 14;
        pill = IRECT (mRECT.MW() - w / 2, mRECT.T, mRECT.MW() + w / 2, mRECT.T + 16);
        const bool on = m.P (paramId) >= 0.5;
        const bool hover = GetMouseIsOver() && pill.Contains (hx, hy);
        if (on) box (g, pill, m.accent, m.accent, 8);
        else box (g, pill, col::panel14, hover ? rgb (0x4A4A4A) : col::b333, 8);
        m.tk.draw (g, label, pill.MW(), pill.MH() + 0.5f, 8, on ? col::bg : (hover ? col::text : col::muted), 0.12f, EAlign::Center);
    }
    void OnMouseDown (float x, float y, const IMouseMod&) override
    {
        if (pill.Contains (x, y)) m.edit (paramId, m.P (paramId) >= 0.5 ? 0 : 1);
    }
    void OnMouseOver (float x, float y, const IMouseMod& mod) override
    {
        IControl::OnMouseOver (x, y, mod);
        hx = x; hy = y;
        cursor (pill.Contains (x, y) ? ECursor::HAND : ECursor::ARROW);
    }
    void OnMouseOut() override { IControl::OnMouseOut(); hx = hy = -1; cursor (ECursor::ARROW); }
private:
    int paramId;
    const char* label;
    IRECT pill;
    float hx = -1, hy = -1;
};

// ---------------------------------------------------------------------------
// Transfer curve of the selected band (accent = input, grey = clipped output).
class Curve : public Base
{
public:
    using Base::Base;
    void Draw (IGraphics& g) override
    {
        const IRECT& r = mRECT;
        const float S = m.L.curveS;
        m.tk.draw (g, "CURVE", r.MW(), r.T + 6, 10, col::muted, 0.12f, EAlign::Center);
        const IRECT sq (r.MW() - S / 2, r.T + 18, r.MW() + S / 2, r.T + 18 + S);
        box (g, sq, col::well, col::line, 4);
        const double T = dbToA (m.P (m.thrId())), k = m.P (m.kneeId()) / 100.0;
        auto clip = [&] (double x) {
            const double a = std::fabs (x), ks = T * (1 - k);
            if (a <= ks) return x;
            if (k <= 1e-4) return std::copysign (T, x);
            const double w = T - ks;
            return std::copysign (ks + w * std::tanh ((a - ks) / w), x);
        };
        const float pad = 7, inner = S - 2 * pad, maxIn = 1.25f;
        auto X = [&] (double v) { return sq.L + pad + float (v) / maxIn * inner; };
        auto Y = [&] (double v) { return sq.B - pad - float (v) / maxIn * inner; };
        g.DrawLine (rgb (0x1C1C1C), X (0), Y (T), X (maxIn), Y (T), nullptr, 1);
        g.DrawLine (m.accent, X (0), Y (0), X (maxIn), Y (maxIn), nullptr, 2);
        g.PathClear();
        for (int i = 0; i <= 80; ++i)
        {
            const double v = i / 80.0 * maxIn;
            if (i == 0) g.PathMoveTo (X (v), Y (clip (v))); else g.PathLineTo (X (v), Y (clip (v)));
        }
        g.PathStroke (IPattern (col::grey), 2.5f, roundCaps());
    }
};

// ---------------------------------------------------------------------------
// Hidden accent color picker (click the wordmark): hue/saturation wheel, brightness, hex, reset.
// While open it covers the whole window so a click anywhere else closes it.
class Picker : public Base
{
public:
    using Base::Base;

    IRECT panel() const { return IRECT (14, 48, 14 + 208, 48 + 298); }

    void Draw (IGraphics& g) override
    {
        const IRECT p = panel();
        // shadow
        for (int i = 3; i >= 1; --i) g.FillRoundRect (IColor (40, 0, 0, 0), p.GetPadded (float (i * 3)).GetVShifted (6), 8 + float (i) * 2);
        box (g, p, col::panel14, col::b333, 6);
        m.tk.draw (g, "ACCENT COLOR", p.L + 12, p.T + 12 + 6, 10, col::muted, 0.12f);

        syncFromAccent();
        wheel = IRECT (p.L + 12, p.T + 34, p.L + 12 + 184, p.T + 34 + 184);
        drawWheel (g);

        // brightness
        const float ry = wheel.B + 10 + 10;
        m.tk.draw (g, "BRIGHT", p.L + 12, ry, 9, col::muted, 0.1f);
        slider = IRECT (p.L + 12 + 54, ry - 10, p.R - 12, ry + 10);
        const float t = float ((V - 0.2) / 0.8);
        const float tx = slider.L + 6 + t * (slider.W() - 12);
        g.FillRoundRect (col::b333, IRECT (slider.L, ry - 1.5f, slider.R, ry + 1.5f), 1.5f);
        g.FillRoundRect (m.accent, IRECT (slider.L, ry - 1.5f, tx, ry + 1.5f), 1.5f);
        g.FillCircle (col::text, tx, ry, 6);

        // swatch, hex, reset
        const float by = ry + 10 + 10;
        const IRECT sw (p.L + 12, by, p.L + 12 + 24, by + 24);
        box (g, sw, m.accent, col::b333, 4);
        hexBox = IRECT (sw.R + 8, by - 1, sw.R + 8 + 76, by + 25);
        box (g, hexBox, col::bg, col::b333, 4);
        m.tk.draw (g, m.accentHex.c_str(), hexBox.L + 6, hexBox.MH(), 11, col::text, 0.04f);
        const float rw = m.tk.width (g, "RESET", 9, 0.08f) + 16;
        resetBtn = IRECT (hexBox.R + 8, by - 1, hexBox.R + 8 + rw, by + 25);
        box (g, resetBtn, col::panel1a, col::b333, 4);
        m.tk.draw (g, "RESET", resetBtn.MW(), resetBtn.MH(), 9, col::btn, 0.08f, EAlign::Center);
    }

    void OnMouseDown (float x, float y, const IMouseMod&) override
    {
        if (! panel().Contains (x, y)) { m.pickerOpen = false; return; }
        if (wheel.Contains (x, y)) { dragging = 1; pickWheel (x, y); }
        else if (slider.GetPadded (4).Contains (x, y)) { dragging = 2; pickSlider (x); }
        else if (hexBox.Contains (x, y) && GetUI())
            GetUI()->CreateTextEntry (*this, IText (11, col::text, kFont, EAlign::Near, EVAlign::Middle, 0, col::bg, col::text), hexBox.GetPadded (-2), m.accentHex.c_str());
        else if (resetBtn.Contains (x, y)) { IColor c; fromHex (settings::kDefaultAccent, c); setAccent (c, true); }
    }
    void OnMouseDrag (float x, float y, float, float, const IMouseMod&) override
    {
        if (dragging == 1) pickWheel (x, y);
        if (dragging == 2) pickSlider (x);
    }
    void OnMouseUp (float, float, const IMouseMod&) override { dragging = 0; }
    void OnTextEntryCompletion (const char* str, int) override
    {
        IColor c;
        if (str && fromHex (str, c)) setAccent (c, true);
    }

private:
    double H = 0, S = 1, V = 1;
    std::string syncedHex;
    IRECT wheel, slider, hexBox, resetBtn;
    int dragging = 0;

    void syncFromAccent()
    {
        if (syncedHex == m.accentHex) return;
        rgb2hsv (m.accent.R, m.accent.G, m.accent.B, H, S, V);
        V = std::max (0.2, V);
        syncedHex = m.accentHex;
    }
    void setAccent (const IColor& c, bool resync)
    {
        m.applyAccent (c);
        if (resync) syncedHex.clear(); else syncedHex = m.accentHex;
    }
    void fromHSV()
    {
        int o[3]; hsv2rgb (H, S, V, o);
        setAccent (IColor (255, o[0], o[1], o[2]), false);
    }
    void pickWheel (float x, float y)
    {
        const float dx = x - wheel.MW(), dy = y - wheel.MH();
        H = std::fmod (std::atan2 (dy, dx) * 180.0 / kPi + 450.0, 360.0);
        S = std::min (1.0, std::sqrt (dx * dx + dy * dy) / 92.0);
        fromHSV();
    }
    void pickSlider (float x)
    {
        const double t = std::clamp ((x - slider.L - 6) / std::max (1.f, slider.W() - 12), 0.f, 1.f);
        V = 0.2 + 0.8 * t;
        fromHSV();
    }
    void drawWheel (IGraphics& g)
    {
        const float cx = wheel.MW(), cy = wheel.MH(), R = 92;
        const int n = 180;
        const int vv = int (std::lround (V * 255));
        // fully saturated hue ring, then a radial wash from grey (s = 0) to transparent (s = 1):
        // lerp (hue, grey, 1 - r) is exactly the HSV color at saturation r.
        for (int i = 0; i < n; ++i)
        {
            const double hue = 360.0 * i / n;   // 0 deg = up, clockwise
            int o[3]; hsv2rgb (hue, 1.0, V, o);
            const float a0 = float (hue - 360.0 / n * 0.5 - 0.5), a1 = float (hue + 360.0 / n * 0.5 + 0.5);
            g.PathClear();
            g.PathMoveTo (cx, cy);
            g.PathArc (cx, cy, R, a0, a1);
            g.PathClose();
            g.PathFill (IPattern (IColor (255, o[0], o[1], o[2])));
        }
        g.PathClear();
        g.PathCircle (cx, cy, R + 0.5f);
        g.PathFill (IPattern::CreateRadialGradient (cx, cy, R, { IColorStop (IColor (255, vv, vv, vv), 0.f), IColorStop (IColor (0, vv, vv, vv), 1.f) }));
        // marker
        const double a = (H - 90) * kPi / 180.0, rr = S * R;
        const float mx = cx + float (rr * std::cos (a)), my = cy + float (rr * std::sin (a));
        g.FillCircle (m.accent, mx, my, 6);
        g.DrawCircle (IColor (153, 0, 0, 0), mx, my, 8, nullptr, 1);
        g.DrawCircle (COLOR_WHITE, mx, my, 6.5f, nullptr, 2);
    }
};

} // namespace scalper::ui
