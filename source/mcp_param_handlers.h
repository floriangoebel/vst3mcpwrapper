#pragma once

#include "hostedplugin.h"
#include "mcp_message.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <string>

namespace VST3MCPWrapper {

using namespace Steinberg;
using namespace Steinberg::Vst;

// Check if a parameter ID exists in the hosted controller's parameter list.
inline bool isValidParamId(IEditController* ctrl, ParamID targetId) {
    int32 count = ctrl->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        ParameterInfo info;
        if (ctrl->getParameterInfo(i, info) == kResultOk && info.id == targetId)
            return true;
    }
    return false;
}

inline mcp::json handleListParameters(IEditController* ctrl) {
    if (!ctrl) {
        return {
            {"content", {{{"type", "text"}, {"text", "No hosted plugin loaded"}}}},
            {"isError", true}
        };
    }

    int32 paramCount = ctrl->getParameterCount();
    mcp::json paramList = mcp::json::array();

    for (int32 i = 0; i < paramCount; ++i) {
        ParameterInfo info;
        if (ctrl->getParameterInfo(i, info) == kResultOk) {
            std::string title = utf16ToUtf8(info.title);
            std::string units = utf16ToUtf8(info.units);
            ParamValue value = ctrl->getParamNormalized(info.id);

            String128 displayStr;
            std::string display;
            if (ctrl->getParamStringByValue(info.id, value, displayStr) == kResultOk) {
                display = utf16ToUtf8(displayStr);
            }

            paramList.push_back({
                {"id", info.id},
                {"title", title},
                {"units", units},
                {"normalizedValue", value},
                {"displayValue", display},
                {"defaultNormalizedValue", info.defaultNormalizedValue},
                {"stepCount", info.stepCount},
                {"canAutomate", (info.flags & ParameterInfo::kCanAutomate) != 0}
            });
        }
    }

    return {
        {"content", {{{"type", "text"}, {"text", paramList.dump(2)}}}}
    };
}

inline mcp::json handleGetParameter(IEditController* ctrl, ParamID paramId) {
    if (!ctrl) {
        return {
            {"content", {{{"type", "text"}, {"text", "No hosted plugin loaded"}}}},
            {"isError", true}
        };
    }

    if (!isValidParamId(ctrl, paramId)) {
        return {
            {"content", {{{"type", "text"}, {"text", "Parameter ID " + std::to_string(paramId) + " not found"}}}},
            {"isError", true}
        };
    }

    ParamValue value = ctrl->getParamNormalized(paramId);

    String128 displayStr;
    std::string display;
    if (ctrl->getParamStringByValue(paramId, value, displayStr) == kResultOk) {
        display = utf16ToUtf8(displayStr);
    }

    mcp::json result = {
        {"id", paramId},
        {"normalizedValue", value},
        {"displayValue", display}
    };

    return {
        {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
    };
}

inline mcp::json handleSetParameter(IEditController* ctrl, ParamID paramId, ParamValue value) {
    if (!ctrl) {
        return {
            {"content", {{{"type", "text"}, {"text", "No hosted plugin loaded"}}}},
            {"isError", true}
        };
    }

    if (!isValidParamId(ctrl, paramId)) {
        return {
            {"content", {{{"type", "text"}, {"text", "Parameter ID " + std::to_string(paramId) + " not found"}}}},
            {"isError", true}
        };
    }

    value = std::clamp(value, 0.0, 1.0);

    // Update the hosted controller's internal state (for GUI)
    ctrl->setParamNormalized(paramId, value);

    // Queue the change for the audio processor
    HostedPluginModule::instance().pushParamChange(paramId, value);

    // Read back to confirm
    ParamValue newValue = ctrl->getParamNormalized(paramId);
    String128 displayStr;
    std::string display;
    if (ctrl->getParamStringByValue(paramId, newValue, displayStr) == kResultOk) {
        display = utf16ToUtf8(displayStr);
    }

    mcp::json result = {
        {"id", paramId},
        {"normalizedValue", newValue},
        {"displayValue", display}
    };

    return {
        {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
    };
}

} // namespace VST3MCPWrapper
