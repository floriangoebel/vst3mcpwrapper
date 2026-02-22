#pragma once

#include "processor.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#include <string>
#include <vector>

namespace VST3MCPWrapper {

// Unified test access helper for Processor private members.
// Used by all Processor test files to avoid duplicating friend accessors.
class ProcessorTestAccess {
public:
    // --- Getters ---
    static bool wrapperActive (const Processor& p) { return p.wrapperActive_.load (std::memory_order_relaxed); }
    static bool wrapperProcessing (const Processor& p) { return p.wrapperProcessing_.load (std::memory_order_relaxed); }
    static bool hostedActive (const Processor& p) { return p.hostedActive_.load (); }
    static bool hostedProcessing (const Processor& p) { return p.hostedProcessing_.load (); }
    static bool processorReady (const Processor& p) { return p.processorReady_.load (); }

    static const std::string& currentPluginPath (const Processor& p)
    {
        return p.currentPluginPath_;
    }

    static const std::vector<Steinberg::Vst::SpeakerArrangement>& storedInputArr (const Processor& p)
    {
        return p.storedInputArr_;
    }
    static const std::vector<Steinberg::Vst::SpeakerArrangement>& storedOutputArr (const Processor& p)
    {
        return p.storedOutputArr_;
    }
    static const Steinberg::Vst::ProcessSetup& currentSetup (const Processor& p) { return p.currentSetup_; }

    // --- Setters ---
    static void setHostedComponent (Processor& p, Steinberg::Vst::IComponent* comp)
    {
        p.hostedComponent_ = comp;
    }
    static void setHostedProcessor (Processor& p, Steinberg::Vst::IAudioProcessor* proc)
    {
        p.hostedProcessor_ = Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> (proc);
    }
    static void setProcessorReady (Processor& p, bool ready)
    {
        p.processorReady_ = ready;
    }
    static void setHostedActive (Processor& p, bool active)
    {
        p.hostedActive_ = active;
    }
    static void setHostedProcessing (Processor& p, bool processing)
    {
        p.hostedProcessing_ = processing;
    }
    static void setCurrentPluginPath (Processor& p, const std::string& path)
    {
        p.currentPluginPath_ = path;
    }

    static void callReplayDawState (Processor& p) { p.replayDawStateOntoHosted (); }
};

} // namespace VST3MCPWrapper
