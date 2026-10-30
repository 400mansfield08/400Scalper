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
