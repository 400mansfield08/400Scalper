#pragma once
// 400Scalper UI — window layout. Mirrors the approved HTML mockup (v7): a 48 px header,
// the scope row with IN / OUT meters, the 178 px control row and a 20 px footer.
// Below 820 px wide the control row switches to its compact sizes.

#include "IGraphicsStructs.h"
#include <algorithm>
#include <vector>

namespace scalper::ui {

using iplug::igraphics::IRECT;

struct Rects
{
    float W = 0, H = 0;
    bool spectral = false, compact = false;
    IRECT hdr, mid, foot, footer;
    IRECT meterIn, scope, meterOut;
    IRECT knIn, knThr, knKnee, knDetail, knOut;     // knob columns below the DELTA row
    IRECT delta;                                     // the DELTA pill row above THRESHOLD
    IRECT curve;                                     // CURVE label + square
    IRECT mb, sc;                                    // band panel (Sample) / spectrum (Spectral)
    float knobW = 80, curveS = 84;

    bool same (const Rects& o) const { return W == o.W && H == o.H && spectral == o.spectral; }
};

constexpr float kHdrH = 48, kFootH = 178, kFooterH = 20;
constexpr int kMinW = 640, kMinH = 440, kMaxW = 4000, kMaxH = 3000;
constexpr int kDefaultW = 900, kDefaultH = 600;

inline Rects computeLayout (float W, float H, bool spectral)
{
    Rects r;
    r.W = W; r.H = H; r.spectral = spectral;
    r.compact = W <= 820;
    r.hdr    = IRECT (0, 0, W, kHdrH);
    r.footer = IRECT (0, H - kFooterH, W, H);
    r.foot   = IRECT (0, r.footer.T - kFootH, W, r.footer.T);
    r.mid    = IRECT (0, kHdrH, W, r.foot.T);

    // ---- scope row: 14 px top padding, 16 px sides, 10 px gaps, 34 px meters ----
    const IRECT in (r.mid.L + 16, r.mid.T + 14, r.mid.R - 16, r.mid.B);
    r.meterIn  = IRECT (in.L, in.T, in.L + 34, in.B);
    r.meterOut = IRECT (in.R - 34, in.T, in.R, in.B);
    r.scope    = IRECT (r.meterIn.R + 10, in.T, r.meterOut.L - 10, in.B);

    // ---- control row ----
    const float pad = r.compact ? 10.f : 14.f, gap = r.compact ? 6.f : 8.f;
    r.knobW = r.compact ? 66.f : 80.f;
    r.curveS = r.compact ? 70.f : 84.f;
    enum Kind { In, Mb, Thr, Knee, Detail, Curve, Sc, Out };
    std::vector<Kind> items = spectral
        ? std::vector<Kind> { In, Thr, Knee, Detail, Curve, Sc, Out }
        : std::vector<Kind> { In, Mb, Thr, Knee, Curve, Out };
    float fixed = 0;
    for (Kind k : items) if (k != Mb && k != Sc) fixed += (k == Curve ? r.curveS : r.knobW);
    const float flexMin = spectral ? 150.f : (r.compact ? 160.f : 220.f);
    const float flexW = std::max (flexMin, W - 2 * pad - fixed - gap * float (items.size() - 1));

    const float knobColH = 123.f;                                 // pill 16 + 6 + label + knob + value
    const float knobTop = r.foot.T + (kFootH - knobColH) * 0.5f;
    const float panelTop = r.foot.T + (kFootH - 150.f) * 0.5f;
    const float curveTop = r.foot.T + (kFootH - (18.f + r.curveS)) * 0.5f;
    float x = pad;
    auto knob = [&] (IRECT& dst) { dst = IRECT (x, knobTop + 22, x + r.knobW, knobTop + knobColH); x += r.knobW + gap; };
    r.knDetail = r.mb = r.sc = IRECT();
    for (Kind k : items)
    {
        switch (k)
        {
            case In:     knob (r.knIn); break;
            case Thr:    r.delta = IRECT (x, knobTop, x + r.knobW, knobTop + 16); knob (r.knThr); break;
            case Knee:   knob (r.knKnee); break;
            case Detail: knob (r.knDetail); break;
            case Out:    knob (r.knOut); break;
            case Curve:  r.curve = IRECT (x, curveTop, x + r.curveS, curveTop + 18 + r.curveS); x += r.curveS + gap; break;
            case Mb:     r.mb = IRECT (x, panelTop, x + flexW, panelTop + 150); x += flexW + gap; break;
            case Sc:     r.sc = IRECT (x, panelTop, x + flexW, panelTop + 150); x += flexW + gap; break;
        }
    }
    return r;
}

} // namespace scalper::ui
