#include "hostedplugin.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace VST3MCPWrapper {

HostedPluginModule& HostedPluginModule::instance() {
    static HostedPluginModule inst;
    return inst;
}

bool HostedPluginModule::load(const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (loaded_ && pluginPath_ == path)
        return true;

    // If a different plugin is loaded, reset state first
    if (loaded_) {
        hostedComponent_ = nullptr;
        hasControllerCID_ = false;
        std::memset(controllerCID_, 0, sizeof(TUID));
        module_.reset();
        loaded_ = false;
        pluginPath_.clear();
        {
            std::lock_guard<std::mutex> plock(paramChangeMutex_);
            pendingParamChanges_.clear();
        }
    }

    module_ = VST3::Hosting::Module::create(path, error);
    if (!module_)
        return false;

    auto factory = module_->getFactory();
    for (auto& classInfo : factory.classInfos()) {
        if (classInfo.category() == kVstAudioEffectClass) {
            effectClassID_ = classInfo.ID();
            pluginPath_ = path;
            loaded_ = true;
            return true;
        }
    }

    error = "No audio effect class found in plugin";
    module_.reset();
    return false;
}

void HostedPluginModule::unload() {
    std::lock_guard<std::mutex> lock(mutex_);
    hostedComponent_ = nullptr;
    hasControllerCID_ = false;
    std::memset(controllerCID_, 0, sizeof(TUID));
    effectClassID_ = {};
    module_.reset();
    loaded_ = false;
    pluginPath_.clear();
    {
        std::lock_guard<std::mutex> plock(paramChangeMutex_);
        pendingParamChanges_.clear();
    }
}

bool HostedPluginModule::isLoaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_;
}

std::optional<VST3::Hosting::PluginFactory> HostedPluginModule::getFactory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!module_)
        return std::nullopt;
    return module_->getFactory();
}

std::string HostedPluginModule::getPluginPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pluginPath_;
}

VST3::UID HostedPluginModule::getEffectClassID() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return effectClassID_;
}

bool HostedPluginModule::hasControllerClassID() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasControllerCID_;
}

void HostedPluginModule::getControllerClassID(TUID dest) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(dest, controllerCID_, sizeof(TUID));
}

void HostedPluginModule::setControllerClassID(const TUID& cid) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(controllerCID_, cid, sizeof(TUID));
    hasControllerCID_ = true;
}

void HostedPluginModule::setHostedComponent(IPtr<IComponent> component) {
    std::lock_guard<std::mutex> lock(mutex_);
    hostedComponent_ = component;
}

IPtr<IComponent> HostedPluginModule::getHostedComponent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hostedComponent_;
}

void HostedPluginModule::pushParamChange(ParamID id, ParamValue value) {
    std::lock_guard<std::mutex> lock(paramChangeMutex_);
    pendingParamChanges_.push_back({id, value});
}

void HostedPluginModule::drainParamChanges(std::vector<ParamChange>& dest) {
    std::lock_guard<std::mutex> lock(paramChangeMutex_);
    dest.swap(pendingParamChanges_);
    pendingParamChanges_.clear();
}

std::string utf16ToUtf8(const TChar* str, int maxLen) {
    std::string result;
    for (int i = 0; i < maxLen && str[i] != 0; ++i) {
        char16_t ch = static_cast<char16_t>(str[i]);
        if (ch < 0x80) {
            result += static_cast<char>(ch);
        } else if (ch < 0x800) {
            result += static_cast<char>(0xC0 | (ch >> 6));
            result += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            result += static_cast<char>(0xE0 | (ch >> 12));
            result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    return result;
}

} // namespace VST3MCPWrapper
