#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <atomic>
#include <string>
#include <vector>

namespace VST3MCPWrapper {

struct ParamChange;

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;

    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::uint32 PLUGIN_API getTailSamples() override;

    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

private:
    bool loadHostedPlugin(const std::string& path);
    void unloadHostedPlugin();

    Steinberg::IPtr<Steinberg::Vst::IComponent> hostedComponent_;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> hostedProcessor_;
    bool wrapperActive_ = false;      // Whether the DAW has activated our processor
    bool wrapperProcessing_ = false;  // Whether the DAW has called setProcessing(true)
    std::atomic<bool> hostedActive_{false};       // Whether the hosted component is active
    std::atomic<bool> hostedProcessing_{false};   // Whether the hosted processor is processing
    std::atomic<bool> processorReady_{false};

    Steinberg::FUnknown* hostContext_ = nullptr;
    Steinberg::Vst::ProcessSetup currentSetup_{};
    std::string currentPluginPath_;

    // Stored bus arrangements for replay when loading a plugin mid-session
    std::vector<Steinberg::Vst::SpeakerArrangement> storedInputArr_;
    std::vector<Steinberg::Vst::SpeakerArrangement> storedOutputArr_;

    // Reusable buffer for draining parameter changes (avoids allocation in process())
    std::vector<ParamChange> drainBuffer_;
};

} // namespace VST3MCPWrapper
