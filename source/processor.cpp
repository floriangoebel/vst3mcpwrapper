#include "processor.h"
#include "pluginids.h"
#include "hostedplugin.h"

#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace VST3MCPWrapper {

static constexpr char kStateMagic[4] = {'V', 'M', 'C', 'W'};
static constexpr uint32 kStateVersion = 1;
static constexpr uint32 kMaxPathLen = 4096;

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

    processorReady_ = true;
    return true;
}

void Processor::unloadHostedPlugin() {
    processorReady_ = false;

    if (hostedComponent_) {
        if (hostedProcessing_) {
            hostedProcessor_->setProcessing(false);
            hostedProcessing_ = false;
        }
        if (hostedActive_) {
            hostedComponent_->setActive(false);
            hostedActive_ = false;
        }

        HostedPluginModule::instance().setHostedComponent(nullptr);
        hostedComponent_->terminate();
        hostedProcessor_ = nullptr;
        hostedComponent_ = nullptr;
    }
    currentPluginPath_.clear();
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    wrapperActive_ = state;
    if (hostedComponent_) {
        hostedComponent_->setActive(state);
        hostedActive_ = state;
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::setProcessing(TBool state) {
    wrapperProcessing_ = state;
    if (hostedProcessor_) {
        hostedProcessor_->setProcessing(state);
        hostedProcessing_ = state;
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
    if (processorReady_ && hostedProcessor_ && hostedActive_) {
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

    // Try to read wrapper state format
    char magic[4] = {};
    int32 numBytesRead = 0;
    if (state->read(magic, sizeof(magic), &numBytesRead) != kResultOk || numBytesRead != sizeof(magic))
        return kResultFalse;

    if (std::memcmp(magic, kStateMagic, sizeof(magic)) != 0) {
        // Not our format — could be legacy state, ignore
        return kResultOk;
    }

    uint32 version = 0;
    if (state->read(&version, sizeof(version), &numBytesRead) != kResultOk
        || numBytesRead != sizeof(version))
        return kResultFalse;

    uint32 pathLen = 0;
    if (state->read(&pathLen, sizeof(pathLen), &numBytesRead) != kResultOk
        || numBytesRead != sizeof(pathLen))
        return kResultFalse;

    if (pathLen > kMaxPathLen)
        return kResultFalse;

    std::string pluginPath;
    if (pathLen > 0) {
        pluginPath.resize(pathLen);
        if (state->read(pluginPath.data(), pathLen, &numBytesRead) != kResultOk || numBytesRead != static_cast<int32>(pathLen))
            return kResultFalse;
    }

    // Load the plugin if needed
    if (!pluginPath.empty() && pluginPath != currentPluginPath_) {
        unloadHostedPlugin();
        loadHostedPlugin(pluginPath);
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

    int32 numBytesWritten = 0;

    // Write magic
    state->write(const_cast<char*>(kStateMagic), sizeof(kStateMagic), &numBytesWritten);

    // Write version
    uint32 version = kStateVersion;
    state->write(&version, sizeof(version), &numBytesWritten);

    // Write plugin path
    uint32 pathLen = static_cast<uint32>(currentPluginPath_.size());
    state->write(&pathLen, sizeof(pathLen), &numBytesWritten);
    if (pathLen > 0) {
        state->write(const_cast<char*>(currentPluginPath_.data()), pathLen, &numBytesWritten);
    }

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
            std::string path(static_cast<const char*>(data), size);

            unloadHostedPlugin();
            loadHostedPlugin(path);

            // Replay activation and processing state. On first load, these were
            // never set because setActive()/setProcessing() were called by the DAW
            // before any hosted component existed — use wrapper flags to replay.
            if (wrapperActive_ && hostedComponent_) {
                hostedComponent_->setActive(true);
                hostedActive_ = true;
            }
            if (wrapperProcessing_ && hostedProcessor_) {
                hostedProcessor_->setProcessing(true);
                hostedProcessing_ = true;
            }

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
