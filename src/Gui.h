#pragma once
// 400Scalper — plugin window: the HTML UI hosted in an embedded web view (choc::ui::WebView).

#include <clap/clap.h>
#include <memory>
#include <string>
#include <cstdint>

namespace choc::ui { class WebView; }

namespace scalper {

class Plugin;

class Gui
{
public:
    explicit Gui (Plugin& p);
    ~Gui();

    bool create();
    bool setParent (const clap_window* window);
    bool setSize (uint32_t w, uint32_t h);
    bool show();
    bool hide();

    uint32_t width = 900, height = 580;
    static constexpr uint32_t kMinW = 640, kMinH = 420, kMaxW = 4000, kMaxH = 3000;

    // JSON builders (exposed for tests)
    static std::string initJson (Plugin& p);
    static std::string pollJson (Plugin& p, int band, double since, int maxEntries, bool wantSpectrum);

private:
    Plugin& plugin;
    std::unique_ptr<choc::ui::WebView> web;
    void* parentHandle = nullptr;
};

const clap_plugin_gui* getGuiExtension();

} // namespace scalper
