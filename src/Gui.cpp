// 400Scalper — CLAP GUI extension (Windows: WebView2 via choc).

#include "Gui.h"
#include "Plugin.h"
#include "Settings.h"
#include "UiResources.h"   // generated at build time: index.html + font bytes

#include <choc/gui/choc_WebView.h>

#if defined(_WIN32)
 #include <windows.h>
#endif

namespace scalper {

static double num (const choc::value::ValueView& args, uint32_t i, double def = 0.0)
{
    if (! args.isArray() || i >= args.size()) return def;
    const auto v = args[i];
    if (v.isFloat64() || v.isFloat32() || v.isInt32() || v.isInt64()) return v.getWithDefault<double> (def);
    if (v.isBool()) return v.getBool() ? 1.0 : 0.0;
    return def;
}

static std::string str (const choc::value::ValueView& args, uint32_t i)
{
    if (! args.isArray() || i >= args.size() || ! args[i].isString()) return {};
    return std::string (args[i].getString());
}

Gui::Gui (Plugin& p) : plugin (p) {}
Gui::~Gui() { web.reset(); }

bool Gui::create()
{
    choc::ui::WebView::Options opts;
    opts.enableDebugMode = false;
    opts.transparentBackground = false;
    opts.acceptsFirstMouseClick = true;
    opts.fetchResource = [] (const std::string& path) -> std::optional<choc::ui::WebView::Options::Resource>
    {
        if (path == "/" || path == "/index.html")
            return choc::ui::WebView::Options::Resource (std::string_view (reinterpret_cast<const char*> (ui::index_html), sizeof (ui::index_html)), "text/html");
        if (path == "/fonts/SuiGenerisRg-Regular.ttf")
            return choc::ui::WebView::Options::Resource (std::string_view (reinterpret_cast<const char*> (ui::font_ttf), sizeof (ui::font_ttf)), "font/ttf");
        return {};
    };
    // Keep the page inside the plugin: no navigating away, no popups.
    opts.shouldAllowRequest = [] (const choc::ui::WebView::Options::Request&) { return false; };

    // On Windows the WebView2 controller is created asynchronously, so functions can only be
    // bound once the view reports it is ready. The page waits for them before talking to us.
    Plugin* p = &plugin;
    opts.webviewIsReady = [p] (choc::ui::WebView& w)
    {
        w.addInitScript ("window.__SCALPER_PLUGIN__ = true;");
        w.evaluateJavascript ("window.__SCALPER_PLUGIN__ = true;");
        w.bind ("scalperInit", [p] (const choc::value::ValueView&) -> choc::value::Value {
            return choc::value::createString (Gui::initJson (*p));
        });
        w.bind ("scalperPoll", [p] (const choc::value::ValueView& a) -> choc::value::Value {
            return choc::value::createString (Gui::pollJson (*p, int (num (a, 0)), num (a, 1), int (num (a, 2, 4096)), num (a, 3) != 0.0));
        });
        w.bind ("scalperSet", [p] (const choc::value::ValueView& a) -> choc::value::Value {
            p->uiSetParam (clap_id (num (a, 0)), num (a, 1));
            return {};
        });
        w.bind ("scalperGesture", [p] (const choc::value::ValueView& a) -> choc::value::Value {
            p->uiGesture (clap_id (num (a, 0)), num (a, 1) != 0.0);
            return {};
        });
        w.bind ("scalperSetSel", [p] (const choc::value::ValueView& a) -> choc::value::Value {
            p->selectedBand.store (std::clamp (int (num (a, 0)), 0, kMaxBands - 1));
            return {};
        });
        w.bind ("scalperSetAccent", [] (const choc::value::ValueView& a) -> choc::value::Value {
            settings::saveAccent (str (a, 0));
            return {};
        });
    };

    web = std::make_unique<choc::ui::WebView> (opts);
    if (! web->loadedOK()) { web.reset(); return false; }
    return true;
}

bool Gui::setParent (const clap_window* window)
{
    if (! web || ! window) return false;
#if defined(_WIN32)
    auto child = static_cast<HWND> (web->getViewHandle());
    auto parent = static_cast<HWND> (window->win32);
    parentHandle = parent;
    LONG_PTR style = GetWindowLongPtrW (child, GWL_STYLE);
    style &= ~(LONG_PTR) (WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU);
    style |= WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
    SetWindowLongPtrW (child, GWL_STYLE, style);
    SetParent (child, parent);
    SetWindowPos (child, nullptr, 0, 0, int (width), int (height), SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    return true;
#else
    (void) window;
    return false;
#endif
}

bool Gui::setSize (uint32_t w, uint32_t h)
{
    width = std::clamp (w, kMinW, kMaxW);
    height = std::clamp (h, kMinH, kMaxH);
#if defined(_WIN32)
    if (web)
        SetWindowPos (static_cast<HWND> (web->getViewHandle()), nullptr, 0, 0, int (width), int (height), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
#endif
    return true;
}

bool Gui::show()
{
#if defined(_WIN32)
    if (web) ShowWindow (static_cast<HWND> (web->getViewHandle()), SW_SHOW);
#endif
    return web != nullptr;
}

bool Gui::hide()
{
#if defined(_WIN32)
    if (web) ShowWindow (static_cast<HWND> (web->getViewHandle()), SW_HIDE);
#endif
    return web != nullptr;
}

//==============================================================================
static Plugin* plug (const clap_plugin* p) { return static_cast<Plugin*> (p->plugin_data); }

static bool isSupportedApi (const char* api, bool floating)
{
#if defined(_WIN32)
    return ! floating && std::strcmp (api, CLAP_WINDOW_API_WIN32) == 0;
#else
    (void) api; (void) floating;
    return false;
#endif
}

static const clap_plugin_gui kGui = {
    [] (const clap_plugin*, const char* api, bool floating) -> bool { return isSupportedApi (api, floating); },
    [] (const clap_plugin*, const char** api, bool* floating) -> bool
    {
#if defined(_WIN32)
        *api = CLAP_WINDOW_API_WIN32; *floating = false; return true;
#else
        (void) api; (void) floating; return false;
#endif
    },
    [] (const clap_plugin* p, const char* api, bool floating) -> bool
    {
        if (! isSupportedApi (api, floating)) return false;
        auto* pl = plug (p);
        if (pl->gui) return true;
        pl->gui = std::make_unique<Gui> (*pl);
        if (! pl->gui->create()) { pl->gui.reset(); return false; }
        return true;
    },
    [] (const clap_plugin* p) { plug (p)->gui.reset(); },
    [] (const clap_plugin*, double) -> bool { return false; },   // web view handles DPI itself
    [] (const clap_plugin* p, uint32_t* w, uint32_t* h) -> bool
    {
        auto* g = plug (p)->gui.get();
        if (! g) return false;
        *w = g->width; *h = g->height;
        return true;
    },
    [] (const clap_plugin*) -> bool { return true; },
    [] (const clap_plugin*, clap_gui_resize_hints* hints) -> bool
    {
        hints->can_resize_horizontally = true;
        hints->can_resize_vertically = true;
        hints->preserve_aspect_ratio = false;
        hints->aspect_ratio_width = 0;
        hints->aspect_ratio_height = 0;
        return true;
    },
    [] (const clap_plugin*, uint32_t* w, uint32_t* h) -> bool
    {
        *w = std::clamp (*w, Gui::kMinW, Gui::kMaxW);
        *h = std::clamp (*h, Gui::kMinH, Gui::kMaxH);
        return true;
    },
    [] (const clap_plugin* p, uint32_t w, uint32_t h) -> bool
    {
        auto* g = plug (p)->gui.get();
        return g && g->setSize (w, h);
    },
    [] (const clap_plugin* p, const clap_window* window) -> bool
    {
        auto* g = plug (p)->gui.get();
        return g && g->setParent (window);
    },
    [] (const clap_plugin*, const clap_window*) -> bool { return false; },
    [] (const clap_plugin*, const char*) {},
    [] (const clap_plugin* p) -> bool { auto* g = plug (p)->gui.get(); return g && g->show(); },
    [] (const clap_plugin* p) -> bool { auto* g = plug (p)->gui.get(); return g && g->hide(); },
};

const clap_plugin_gui* getGuiExtension() { return &kGui; }

} // namespace scalper
