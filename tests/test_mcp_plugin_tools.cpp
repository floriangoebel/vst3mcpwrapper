#include <gtest/gtest.h>

#include "mcp_plugin_handlers.h"

using namespace VST3MCPWrapper;

namespace {

// ============================================================
// get_loaded_plugin
// ============================================================

TEST(MCPPluginTools, GetLoadedPluginWithPluginLoaded) {
    std::string path = "/Library/Audio/Plug-Ins/VST3/MyPlugin.vst3";
    auto result = handleGetLoadedPlugin(path);

    EXPECT_FALSE(result.contains("isError"));

    auto contentText = result["content"][0]["text"].get<std::string>();
    auto data = mcp::json::parse(contentText);

    EXPECT_TRUE(data["loaded"].get<bool>());
    EXPECT_EQ(data["path"].get<std::string>(), path);
}

TEST(MCPPluginTools, GetLoadedPluginNoPluginLoaded) {
    auto result = handleGetLoadedPlugin("");

    EXPECT_FALSE(result.contains("isError"));

    auto contentText = result["content"][0]["text"].get<std::string>();
    auto data = mcp::json::parse(contentText);

    EXPECT_FALSE(data["loaded"].get<bool>());
    EXPECT_EQ(data["path"].get<std::string>(), "none");
}

// ============================================================
// list_available_plugins
// ============================================================

TEST(MCPPluginTools, ListAvailablePluginsReturnsJsonArray) {
    std::vector<std::string> paths = {
        "/Library/Audio/Plug-Ins/VST3/PluginA.vst3",
        "/Library/Audio/Plug-Ins/VST3/PluginB.vst3",
        "/Library/Audio/Plug-Ins/VST3/PluginC.vst3"
    };

    auto result = handleListAvailablePlugins(paths);
    EXPECT_FALSE(result.contains("isError"));

    auto contentText = result["content"][0]["text"].get<std::string>();
    auto pluginList = mcp::json::parse(contentText);

    ASSERT_TRUE(pluginList.is_array());
    ASSERT_EQ(pluginList.size(), 3u);
    EXPECT_EQ(pluginList[0].get<std::string>(), paths[0]);
    EXPECT_EQ(pluginList[1].get<std::string>(), paths[1]);
    EXPECT_EQ(pluginList[2].get<std::string>(), paths[2]);
}

TEST(MCPPluginTools, ListAvailablePluginsEmptyReturnsEmptyArray) {
    std::vector<std::string> paths;

    auto result = handleListAvailablePlugins(paths);
    EXPECT_FALSE(result.contains("isError"));

    auto contentText = result["content"][0]["text"].get<std::string>();
    auto pluginList = mcp::json::parse(contentText);

    ASSERT_TRUE(pluginList.is_array());
    EXPECT_EQ(pluginList.size(), 0u);
}

// ============================================================
// load_plugin (response building)
// ============================================================

TEST(MCPPluginTools, LoadPluginInvalidPathReturnsError) {
    std::string path = "/nonexistent/path/plugin.vst3";
    std::string error = "Module not found at path";

    auto result = buildLoadPluginResponse(path, error);

    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());

    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_NE(content.find("Failed to load plugin"), std::string::npos);
    EXPECT_NE(content.find(error), std::string::npos);
}

TEST(MCPPluginTools, LoadPluginSuccessResponse) {
    std::string path = "/Library/Audio/Plug-Ins/VST3/MyPlugin.vst3";
    std::string error; // empty = success

    auto result = buildLoadPluginResponse(path, error);

    EXPECT_FALSE(result.contains("isError"));

    auto contentText = result["content"][0]["text"].get<std::string>();
    auto data = mcp::json::parse(contentText);

    EXPECT_EQ(data["status"].get<std::string>(), "loaded");
    EXPECT_EQ(data["path"].get<std::string>(), path);
}

// ============================================================
// unload_plugin
// ============================================================

TEST(MCPPluginTools, UnloadPluginNoPluginLoadedReturnsError) {
    auto result = handleUnloadPluginNotLoaded();

    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());

    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_NE(content.find("No plugin"), std::string::npos);
}

TEST(MCPPluginTools, UnloadPluginSuccessResponse) {
    auto result = handleUnloadPluginSuccess();

    EXPECT_FALSE(result.contains("isError"));

    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_EQ(content, "Plugin unloaded");
}

// ============================================================
// Common error responses
// ============================================================

TEST(MCPPluginTools, ShuttingDownResponse) {
    auto result = handleShuttingDown();

    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());

    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_NE(content.find("shutting down"), std::string::npos);
}

TEST(MCPPluginTools, TimeoutResponse) {
    auto result = handleTimeout("Load plugin");

    EXPECT_TRUE(result.contains("isError"));
    EXPECT_TRUE(result["isError"].get<bool>());

    auto content = result["content"][0]["text"].get<std::string>();
    EXPECT_EQ(content, "Load plugin timed out");
}

} // anonymous namespace
