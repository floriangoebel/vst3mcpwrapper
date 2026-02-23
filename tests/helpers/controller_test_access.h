#pragma once

#include "controller.h"

namespace VST3MCPWrapper {

// Test access helper for Controller private members.
// Follows the same pattern as ProcessorTestAccess.
class ControllerTestAccess {
public:
    static WrapperPlugView* activeView (const Controller& c) { return c.activeView_; }

    static std::string currentPluginPath (const Controller& c)
    {
        std::lock_guard<std::mutex> lock (c.hostedControllerMutex_);
        return c.currentPluginPath_;
    }

    static Steinberg::IPtr<Steinberg::Vst::IEditController> hostedController (const Controller& c)
    {
        std::lock_guard<std::mutex> lock (c.hostedControllerMutex_);
        return c.hostedController_;
    }
};

} // namespace VST3MCPWrapper
