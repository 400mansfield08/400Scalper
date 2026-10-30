#pragma once
// 400Scalper UI — builds the control tree and keeps it laid out (window size and mode).

#include "Scope.h"
#include "Bands.h"

namespace scalper::ui {

class ScalperUI
{
public:
    ScalperUI (Model& model, const void* fontData, int fontSize) : m (model), font (fontData), fontBytes (fontSize) {}

    // Called from the plugin's layout function: first time attaches, afterwards re-lays out.
    void layout (IGraphics* g)
    {
        if (g->NControls() == 0) attach (g);
        relayout (g, true);
    }

private:
    Model& m;
    const void* font;
    int fontBytes;
    Background* bg = nullptr;
    Header* header = nullptr;
    Meter *meterIn = nullptr, *meterOut = nullptr;
    Scope* scope = nullptr;
    Knob *knIn = nullptr, *knThr = nullptr, *knKnee = nullptr, *knDetail = nullptr, *knOut = nullptr;
    DeltaPill* delta = nullptr;
    Curve* curve = nullptr;
    BandPanel* bands = nullptr;
    SpecClip* specClip = nullptr;
    FooterLink* link = nullptr;
    Picker* picker = nullptr;
    bool pickerShown = false;

    template <typename C, typename... A> C* add (IGraphics* g, A&&... args)
    {
        auto* c = new C (m, std::forward<A> (args)...);
        g->AttachControl (c);
        return c;
    }

    void attach (IGraphics* g)
    {
        g->SetLayoutOnResize (true);
        g->LoadFont (kFont, const_cast<void*> (font), fontBytes);
        g->EnableMouseOver (true);

        bg       = add<Background> (g);
        header   = add<Header> (g);
        meterIn  = add<Meter> (g, false);
        scope    = add<Scope> (g);
        meterOut = add<Meter> (g, true);
        knIn     = add<Knob> (g, KnobKind::In);
        bands    = add<BandPanel> (g);
        delta    = add<DeltaPill> (g);
        knThr    = add<Knob> (g, KnobKind::Threshold);
        knKnee   = add<Knob> (g, KnobKind::Knee);
        knDetail = add<Knob> (g, KnobKind::Detail);
        curve    = add<Curve> (g);
        specClip = add<SpecClip> (g);
        knOut    = add<Knob> (g, KnobKind::Out);
        link     = add<FooterLink> (g);
        g->AttachCornerResizer (EUIResizerMode::Size, true, IColor (0, 0, 0, 0), rgb (0x3A3A3A), m.accent, 14.f);
        picker   = add<Picker> (g);

        g->SetDisplayTickFunc ([this, g] {
            m.tick();
            relayout (g, false);
        });
    }

    // Knob labels (e.g. THRESHOLD) are wider than the knob column, like in the mockup: draw
    // into a wider rect, but only take mouse input over the column itself.
    static void setKnob (Knob* k, const IRECT& r)
    {
        k->SetTargetAndDrawRECTs (r);
        k->SetRECT (r.GetHPadded (18));
    }

    void relayout (IGraphics* g, bool force)
    {
        const Rects L = computeLayout (float (g->Width()), float (g->Height()), m.spectral());
        if (! force && L.same (m.L) && pickerShown == m.pickerOpen) return;
        m.L = L;
        pickerShown = m.pickerOpen;
        const IRECT all (0, 0, L.W, L.H);
        bg->SetTargetAndDrawRECTs (all);
        header->SetTargetAndDrawRECTs (L.hdr);
        meterIn->SetTargetAndDrawRECTs (L.meterIn);
        scope->SetTargetAndDrawRECTs (L.scope);
        meterOut->SetTargetAndDrawRECTs (L.meterOut);
        setKnob (knIn, L.knIn);
        delta->SetTargetAndDrawRECTs (L.delta);
        setKnob (knThr, L.knThr);
        setKnob (knKnee, L.knKnee);
        curve->SetTargetAndDrawRECTs (L.curve);
        setKnob (knOut, L.knOut);
        bands->Hide (L.spectral);
        knDetail->Hide (! L.spectral);
        specClip->Hide (! L.spectral);
        if (! L.spectral) bands->SetTargetAndDrawRECTs (L.mb);
        else { setKnob (knDetail, L.knDetail); specClip->SetTargetAndDrawRECTs (L.sc); }
        link->SetTargetAndDrawRECTs (IRECT (L.W - 24 - 110, L.footer.T, L.W - 24, L.footer.B));
        picker->Hide (! m.pickerOpen);
        picker->SetTargetAndDrawRECTs (m.pickerOpen ? all : IRECT());
        g->SetAllControlsDirty();
    }
};

} // namespace scalper::ui
