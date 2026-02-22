#include "processor.h"
#include "pluginids.h"
#include "hostedplugin.h"
#include "stateformat.h"

#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace VST3MCPWrapper {

Processor::Processor() {
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    hostContext_ = context;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("Event In"));

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() {
    unloadHostedPlugin();
    return AudioEffect::terminate();
}

bool Processor::loadHostedPlugin(const std::string& path) {
    auto& pluginModule = HostedPluginModule::instance();
    std::string error;
    if (!pluginModule.load(path, error))
        return false;

    auto factory = pluginModule.getFactory();
    if (!factory)
        return false;

    auto component = factory->createInstance<IComponent>(pluginModule.getEffectClassID());
    if (!component)
        return false;

    if (component->initialize(hostContext_) != kResultOk)
        return false;

    FUnknownPtr<IAudioProcessor> proc(component);
    if (!proc) {
        component->terminate();
        return false;
    }

    hostedComponent_ = component;
    hostedProcessor_ = IPtr<IAudioProcessor>(proc);
    currentPluginPath_ = path;

    // Activate only the buses that match our wrapper's layout (1 audio in, 1 audio out,
    // 1 event in). Deactivate any extra buses (e.g. sidechain) since we don't provide
    // ProcessData buffers for them.
    for (int32 i = 0; i < component->getBusCount(kAudio, kInput); ++i)
        component->activateBus(kAudio, kInput, i, i == 0);
    for (int32 i = 0; i < component->getBusCount(kAudio, kOutput); ++i)
        component->activateBus(kAudio, kOutput, i, i == 0);
    for (int32 i = 0; i < component->getBusCount(kEvent, kInput); ++i)
        component->activateBus(kEvent, kInput, i, i == 0);
    for (int32 i = 0; i < component->getBusCount(kEvent, kOutput); ++i)
        component->activateBus(kEvent, kOutput, i, false);

    // Extract controller class ID for the controller to use
    TUID controllerCID;
    if (component->getControllerClassId(controllerCID) == kResultOk) {
        pluginModule.setControllerClassID(controllerCID);
    }

    // Share the hosted component so the controller can connect to it
    pluginModule.setHostedComponent(hostedComponent_);

    // Replay stored bus arrangements
    if (!storedInputArr_.empty() || !storedOutputArr_.empty()) {
        hostedProcessor_->setBusArrangements(
            storedInputArr_.empty() ? nullptr : storedInputArr_.data(),
            static_cast<int32>(storedInputArr_.size()),
            storedOutputArr_.empty() ? nullptr : storedOutputArr_.data(),
            static_cast<int32>(storedOutputArr_.size()));
    }

    // Replay current processing setup if we have one
    if (currentSetup_.sampleRate > 0) {
        hostedProcessor_->setupProcessing(currentSetup_);
    }

    processorReady_.store(true, std::memory_order_relaxed);
    return true;
}

void Processor::replayDawStateOntoHosted() {
    if (wrapperActive_.load(std::memory_order_relaxed) && hostedComponent_) {
        hostedComponent_->setActive(true);
        hostedActive_.store(true, std::memory_order_relaxed);
    }
    if (wrapperProcessing_.load(std::memory_order_relaxed) && hostedProcessor_) {
        hostedProcessor_->setProcessing(true);
        hostedProcessing_.store(true, std::memory_order_relaxed);
    }
}

void Processor::unloadHostedPlugin() {
    processorReady_.store(false, std::memory_order_relaxed);

    if (hostedComponent_) {
        if (hostedProcessing_.load(std::memory_order_relaxed)) {
            hostedProcessor_->setProcessing(false);
            hostedProcessing_.store(false, std::memory_order_relaxed);
        }
        if (hostedActive_.load(std::memory_order_relaxed)) {
            hostedComponent_->setActive(false);
            hostedActive_.store(false, std::memory_order_relaxed);
        }

        HostedPluginModule::instance().setHostedComponent(nullptr);
        hostedComponent_->terminate();
        hostedProcessor_ = nullptr;
        hostedComponent_ = nullptr;
    }
    currentPluginPath_.clear();
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    wrapperActive_.store(state, std::memory_order_relaxed);
    if (hostedComponent_) {
        hostedComponent_->setActive(state);
        hostedActive_.store(state, std::memory_order_relaxed);
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::setProcessing(TBool state) {
    wrapperProcessing_.store(state, std::memory_order_relaxed);
    if (hostedProcessor_) {
        hostedProcessor_->setProcessing(state);
        hostedProcessing_.store(state, std::memory_order_relaxed);
    }
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    // Store for replay when loading a hosted plugin mid-session
    storedInputArr_.assign(inputs, inputs + numIns);
    storedOutputArr_.assign(outputs, outputs + numOuts);

    if (hostedProcessor_) {
        hostedProcessor_->setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    currentSetup_ = setup;
    if (hostedProcessor_) {
        hostedProcessor_->setupProcessing(setup);
    }
    return AudioEffect::setupProcessing(setup);
}

uint32 PLUGIN_API Processor::getLatencySamples() {
    if (hostedProcessor_)
        return hostedProcessor_->getLatencySamples();
    return 0;
}

uint32 PLUGIN_API Processor::getTailSamples() {
    if (hostedProcessor_)
        return hostedProcessor_->getTailSamples();
    return 0;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    if (hostedProcessor_) {
        return hostedProcessor_->canProcessSampleSize(symbolicSampleSize);
    }
    // Only support 32-bit float when no hosted plugin
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    if (processorReady_.load(std::memory_order_relaxed) && hostedProcessor_ && hostedActive_.load(std::memory_order_relaxed)) {
        // Drain pending parameter changes from MCP/GUI and inject into ProcessData
        auto& pluginModule = HostedPluginModule::instance();
        drainBuffer_.clear();
        pluginModule.drainParamChanges(drainBuffer_);

        if (!drainBuffer_.empty()) {
            // Merge DAW automation changes with our queued MCP/GUI changes
            int32 dawParamCount = data.inputParameterChanges
                ? data.inputParameterChanges->getParameterCount() : 0;
            ParameterChanges mergedChanges(
                dawParamCount + static_cast<int32>(drainBuffer_.size()));

            // Copy DAW automation changes first
            if (data.inputParameterChanges) {
                for (int32 i = 0; i < dawParamCount; ++i) {
                    auto* srcQueue = data.inputParameterChanges->getParameterData(i);
                    if (!srcQueue) continue;
                    int32 index;
                    auto* dstQueue = mergedChanges.addParameterData(
                        srcQueue->getParameterId(), index);
                    if (!dstQueue) continue;
                    int32 pointCount = srcQueue->getPointCount();
                    for (int32 p = 0; p < pointCount; ++p) {
                        int32 sampleOffset;
                        ParamValue value;
                        if (srcQueue->getPoint(p, sampleOffset, value) == kResultOk) {
                            int32 pointIndex;
                            dstQueue->addPoint(sampleOffset, value, pointIndex);
                        }
                    }
                }
            }

            // Add queued MCP/GUI changes (appended after DAW points for same param)
            for (auto& change : drainBuffer_) {
                int32 index;
                auto* queue = mergedChanges.addParameterData(change.id, index);
                if (queue) {
                    int32 pointIndex;
                    queue->addPoint(0, change.value, pointIndex);
                }
            }

            auto* origInputChanges = data.inputParameterChanges;
            data.inputParameterChanges = &mergedChanges;
            auto result = hostedProcessor_->process(data);
            data.inputParameterChanges = origInputChanges;
            return result;
        }

        return hostedProcessor_->process(data);
    }

    // Passthrough: copy input to output
    if (data.numInputs > 0 && data.numOutputs > 0) {
        int32 numChannels = data.outputs[0].numChannels;
        int32 numSamples = data.numSamples;
        bool is64bit = (data.symbolicSampleSize == kSample64);

        for (int32 ch = 0; ch < numChannels; ++ch) {
            if (ch < data.inputs[0].numChannels) {
                void* dst = is64bit
                    ? static_cast<void*>(data.outputs[0].channelBuffers64[ch])
                    : static_cast<void*>(data.outputs[0].channelBuffers32[ch]);
                const void* src = is64bit
                    ? static_cast<const void*>(data.inputs[0].channelBuffers64[ch])
                    : static_cast<const void*>(data.inputs[0].channelBuffers32[ch]);
                if (dst != src) {
                    size_t bytes = numSamples * (is64bit ? sizeof(double) : sizeof(float));
                    std::memcpy(dst, src, bytes);
                }
            } else {
                size_t bytes = numSamples * (is64bit ? sizeof(double) : sizeof(float));
                if (is64bit)
                    std::memset(data.outputs[0].channelBuffers64[ch], 0, bytes);
                else
                    std::memset(data.outputs[0].channelBuffers32[ch], 0, bytes);
            }
        }
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (!state)
        return kResultFalse;

    // Read and validate wrapper state header
    std::string pluginPath;
    if (readStateHeader(state, pluginPath) != kResultOk)
        return kResultFalse;

    // Load the plugin if needed
    if (!pluginPath.empty() && pluginPath != currentPluginPath_) {
        unloadHostedPlugin();
        loadHostedPlugin(pluginPath);

        // Replay activation and processing state — setState() can be called
        // while the wrapper is already active (e.g., preset recall, undo).
        // Without this, the hosted plugin is loaded but never activated,
        // causing audio to silently fall through to passthrough.
        replayDawStateOntoHosted();
    }

    // Forward remaining state to hosted component
    if (hostedComponent_) {
        return hostedComponent_->setState(state);
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (!state)
        return kResultFalse;

    // Write wrapper state header
    tresult headerResult = writeStateHeader(state, currentPluginPath_);
    if (headerResult != kResultOk)
        return headerResult;

    // Write hosted component state
    if (hostedComponent_) {
        return hostedComponent_->getState(state);
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::notify(IMessage* message) {
    if (!message)
        return kResultFalse;

    if (strcmp(message->getMessageID(), "LoadPlugin") == 0) {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes()->getBinary("path", data, size) == kResultOk && size > 0) {
            if (!data) {
                fprintf(stderr, "VST3MCPWrapper: getBinary returned kResultOk but data is nullptr\n");
                return kResultOk;
            }
            std::string path(static_cast<const char*>(data), size);

            unloadHostedPlugin();
            loadHostedPlugin(path);

            // Replay activation and processing state. On first load, these were
            // never set because setActive()/setProcessing() were called by the DAW
            // before any hosted component existed — use wrapper flags to replay.
            replayDawStateOntoHosted();

            // Send acknowledgment back to controller
            if (auto msg = owned(allocateMessage())) {
                msg->setMessageID("PluginLoaded");
                msg->getAttributes()->setBinary("path", path.data(), static_cast<uint32>(path.size()));
                sendMessage(msg);
            }
        }
        return kResultOk;
    }

    if (strcmp(message->getMessageID(), "UnloadPlugin") == 0) {
        unloadHostedPlugin();
        return kResultOk;
    }

    return AudioEffect::notify(message);
}

} // namespace VST3MCPWrapper
