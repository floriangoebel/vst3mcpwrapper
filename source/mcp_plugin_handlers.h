#pragma once

#include "mcp_message.h"

#include <string>
#include <vector>

namespace VST3MCPWrapper {

// Build response for get_loaded_plugin tool.
// Takes the current plugin path (empty if no plugin loaded).
inline mcp::json handleGetLoadedPlugin(const std::string& currentPath) {
    mcp::json result = {
        {"loaded", !currentPath.empty()},
        {"path", currentPath.empty() ? "none" : currentPath}
    };
    return {
        {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
    };
}

// Build response for list_available_plugins tool.
// Takes the list of plugin paths (from Module::getModulePaths()).
inline mcp::json handleListAvailablePlugins(const std::vector<std::string>& paths) {
    mcp::json pluginList = mcp::json::array();
    for (const auto& path : paths) {
        pluginList.push_back(path);
    }
    return {
        {"content", {{{"type", "text"}, {"text", pluginList.dump(2)}}}}
    };
}

// Build response for load_plugin tool after the load operation completes.
// path: the requested plugin path. error: empty on success, error message on failure.
inline mcp::json buildLoadPluginResponse(const std::string& path, const std::string& error) {
    if (!error.empty()) {
        return {
            {"content", {{{"type", "text"}, {"text", "Failed to load plugin: " + error}}}},
            {"isError", true}
        };
    }
    mcp::json result = {
        {"status", "loaded"},
        {"path", path}
    };
    return {
        {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
    };
}

// Build error response for unload_plugin when no plugin is loaded.
inline mcp::json handleUnloadPluginNotLoaded() {
    return {
        {"content", {{{"type", "text"}, {"text", "No plugin is currently loaded"}}}},
        {"isError", true}
    };
}

// Build success response for unload_plugin.
inline mcp::json handleUnloadPluginSuccess() {
    return {
        {"content", {{{"type", "text"}, {"text", "Plugin unloaded"}}}}
    };
}

// Build error response when the plugin is shutting down.
inline mcp::json handleShuttingDown() {
    return {
        {"content", {{{"type", "text"}, {"text", "Plugin is shutting down"}}}},
        {"isError", true}
    };
}

// Build error response for dispatch timeout.
inline mcp::json handleTimeout(const std::string& operation) {
    return {
        {"content", {{{"type", "text"}, {"text", operation + " timed out"}}}},
        {"isError", true}
    };
}

} // namespace VST3MCPWrapper
