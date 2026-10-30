#pragma once
// 400Scalper — iPlug2 plugin class. The audio lives in scalper::Processor (src/core),
// the native UI in plugin/ui. Built as CLAP only for now (see plugin/CMakeLists.txt).

#include "IPlug_include_in_plug_hdr.h"
#include "core/Processor.h"
#include <atomic>
#include <memory>

#if IPLUG_EDITOR
namespace scalper::ui { class Model; class ScalperUI; }
#endif

using namespace iplug;

class Scalper final : public Plugin
{
public:
    explicit Scalper (const InstanceInfo& info);
    ~Scalper() override;

#if IPLUG_DSP
    void ProcessBlock (sample** inputs, sample** outputs, int nFrames) override;
    void OnReset() override;
#endif
    void OnIdle() override;

    bool SerializeState (IByteChunk& chunk) const override;
    int UnserializeState (const IByteChunk& chunk, int startPos) override;

#if defined CLAP_API
    // CLAP fixes on top of iPlug2's wrapper (found with clap-validator):
    // - state save must cope with hosts that accept only a few bytes per write() call
    // - stepped parameters (enum / int / bool) round values before showing them as text
    // - text typed into the host ("2.5k", "-3 dB", "24 dB/oct") converts back to the same value
    bool stateSave (const clap_ostream* stream) noexcept override;
    bool paramsValueToText (clap_id paramIdx, double value, char* display, uint32_t size) noexcept override;
    bool paramsTextToValue (clap_id paramIdx, const char* display, double* pValue) noexcept override;
#endif

    // shared with the UI
    scalper::Processor proc;
    std::atomic<int> selectedBand { 0 };

private:
    scalper::ParamValues snapshot() const;
    int unserializeLegacy (const IByteChunk& chunk, int startPos);
    std::atomic<int> reportedMode { 0 };   // mode whose latency the host currently knows

#if IPLUG_EDITOR
    std::unique_ptr<scalper::ui::Model> model;
    std::unique_ptr<scalper::ui::ScalperUI> ui;
#endif
};
