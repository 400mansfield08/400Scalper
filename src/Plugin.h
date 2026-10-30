#pragma once
// 400Scalper — CLAP plugin class.

#include <clap/clap.h>
#include <atomic>
#include <memory>
#include <string>
#include "Params.h"
#include "Shared.h"
#include "dsp/Engine.h"
#include "dsp/Spectral.h"

namespace scalper {

class Gui;

class Plugin
{
public:
    explicit Plugin (const clap_host* host);
    ~Plugin();

    clap_plugin clap {};
    const clap_host* host = nullptr;
    const clap_host_params* hostParams = nullptr;
    const clap_host_latency* hostLatency = nullptr;
    const clap_host_gui* hostGui = nullptr;

    // ---- lifecycle ----
    bool init();
    bool activate (double sampleRate, uint32_t minFrames, uint32_t maxFrames);
    void deactivate();
    void reset();
    clap_process_status process (const clap_process* p);

    // ---- params ----
    double getParam (clap_id id) const;
    void setParamFromHost (clap_id id, double value);
    void flushParams (const clap_input_events* in, const clap_output_events* out);

    // ---- state ----
    bool saveState (const clap_ostream* stream);
    bool loadState (const clap_istream* stream);

    uint32_t latency() const;

    // ---- shared with the UI ----
    std::atomic<double> values[kNumParams];
    UiEventQueue uiEvents;
    VizHistory viz;
    SpectrumTap spectrum;
    PeakMeter meterIn, meterOut;
    std::atomic<int> selectedBand { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<int> activeMode { 0 };          // mode whose latency was reported to the host (set in activate)
    std::atomic<int> procMode { 0 };            // mode the audio is actually running right now
    SpectralSplitter spectral;                   // also read by the UI (display mask)
    void uiSetParam (clap_id id, double value);   // UI thread
    void uiGesture (clap_id id, bool begin);      // UI thread

#if SCALPER_WITH_GUI
    std::unique_ptr<Gui> gui;
#endif

private:
    void handleInEvent (const clap_event_header* hdr);
    void drainUiEvents (const clap_output_events* out);

    double sampleRate = 48000.0;
    ChannelEngine engines[2];
    SpectralClipChannel specClip[2];
    IntDelay specVizIn[2];
    IntDelay deltaDelay[2];                      // twin -> aligned with the look-ahead ceiling's delay                       // aligns the spectral-mode "input" trace with the output
    double detailSm = 1.0;
    bool restartRequested = false;
    int pendingMode = -1;
    double modeGain = 1.0, modeFadeStep = 0.002;
    void resetEngines();
    StereoCeiling ceiling;

    // smoothed values
    double inGain = 1.0, outGain = 1.0, thr[kMaxBands] { 0.5, 0.5, 0.5, 0.5 };
    double smoothCoef = 0.001;

    // visualiser alignment (ceiling look-ahead) and decimation
    IntDelay vizDelay[kMaxBands][4];
    VizEntry vizAcc[kMaxBands] {};
    int vizCount = 0;
};

} // namespace scalper
