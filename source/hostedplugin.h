#pragma once

#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace VST3MCPWrapper {

struct ParamChange {
    Steinberg::Vst::ParamID id;
    Steinberg::Vst::ParamValue value;
};

// Holds the hosted plugin's module + factory, shared between processor and controller.
// The processor owns the IComponent/IAudioProcessor.
// The controller creates its own IEditController from the same factory.
//
// All public methods are thread-safe.
class HostedPluginModule {
public:
    static HostedPluginModule& instance();

    bool load(const std::string& path, std::string& error);
    void unload();
    bool isLoaded() const;

    // Returns a copy of the factory. Thread-safe.
    // Returns nullopt if not loaded.
    std::optional<VST3::Hosting::PluginFactory> getFactory() const;

    std::string getPluginPath() const;

    VST3::UID getEffectClassID() const;

    bool hasControllerClassID() const;
    void getControllerClassID(Steinberg::TUID dest) const;
    void setControllerClassID(const Steinberg::TUID& cid);

    // Hosted component sharing between processor and controller.
    // The processor sets this after creating the component.
    // The controller reads it to connect via IConnectionPoint and sync state.
    void setHostedComponent(Steinberg::IPtr<Steinberg::Vst::IComponent> component);
    Steinberg::IPtr<Steinberg::Vst::IComponent> getHostedComponent() const;

    // Thread-safe parameter change queue.
    // Writers (MCP thread, GUI thread) push changes.
    // Audio thread drains them in process().
    void pushParamChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);
    void drainParamChanges(std::vector<ParamChange>& dest);

private:
    HostedPluginModule() = default;
    void resetState(); // Caller must hold mutex_

    mutable std::mutex mutex_;
    VST3::Hosting::Module::Ptr module_;
    std::string pluginPath_;
    VST3::UID effectClassID_;
    Steinberg::TUID controllerCID_ = {};
    bool hasControllerCID_ = false;
    bool loaded_ = false;
    Steinberg::IPtr<Steinberg::Vst::IComponent> hostedComponent_;

    std::mutex paramChangeMutex_;
    std::vector<ParamChange> pendingParamChanges_;
};

// Convert VST3 UTF-16 (TChar/char16_t) string to UTF-8 std::string.
std::string utf16ToUtf8(const Steinberg::Vst::TChar* str, int maxLen = 128);

} // namespace VST3MCPWrapper
