/**
 * @file test_plugin_scanning.cpp
 * @brief Tests for VST3 plugin scanning on Linux and the MCP handler wrapper.
 *
 * Verifies that VST3::Hosting::Module::getModulePaths() works on Linux and
 * that handleListAvailablePlugins() correctly formats the results.
 *
 * The SDK's module_linux.cpp scans these directories (in order):
 *   1. $HOME/.vst3/
 *   2. /usr/lib/vst3/
 *   3. /usr/local/lib/vst3/
 *   4. /proc/self/exe/../vst3/  (application-relative)
 */

#include <gtest/gtest.h>

#include "mcp_plugin_handlers.h"
#include "public.sdk/source/vst/hosting/module.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test: getModulePaths() returns without crashing (possibly empty)
// ---------------------------------------------------------------------------
TEST(PluginScanning, GetModulePathsDoesNotCrash) {
    std::vector<std::string> paths;
    ASSERT_NO_THROW(paths = VST3::Hosting::Module::getModulePaths());
    // Paths may be empty if no plugins are installed — that's fine.
}

// ---------------------------------------------------------------------------
// Test: getModulePaths() returns only .vst3 entries
// ---------------------------------------------------------------------------
TEST(PluginScanning, AllReturnedPathsHaveVst3Extension) {
    auto paths = VST3::Hosting::Module::getModulePaths();
    for (const auto& p : paths) {
        EXPECT_NE(p.find(".vst3"), std::string::npos)
            << "Path does not contain .vst3: " << p;
    }
}

// ---------------------------------------------------------------------------
// Test: getModulePaths() discovers a plugin placed in ~/.vst3/
// ---------------------------------------------------------------------------
class PluginScanWithDummyBundle : public ::testing::Test {
protected:
    fs::path dummyBundle_;
    bool createdVst3Dir_ = false;

    void SetUp() override {
        const char* home = std::getenv("HOME");
        ASSERT_NE(home, nullptr) << "HOME environment variable not set";

        fs::path vst3Dir = fs::path(home) / ".vst3";
        createdVst3Dir_ = !fs::exists(vst3Dir);
        fs::create_directories(vst3Dir);

        // Create a minimal dummy .vst3 directory bundle
        dummyBundle_ = vst3Dir / "DummyTestPlugin.vst3";
        fs::create_directories(dummyBundle_ / "Contents" / "x86_64-linux");

        // Create a dummy .so file so it looks like a real bundle
        std::ofstream(dummyBundle_ / "Contents" / "x86_64-linux" / "DummyTestPlugin.so");
    }

    void TearDown() override {
        if (!dummyBundle_.empty() && fs::exists(dummyBundle_)) {
            fs::remove_all(dummyBundle_);
        }
        if (createdVst3Dir_) {
            // Only remove ~/.vst3/ if we created it
            const char* home = std::getenv("HOME");
            if (home) {
                fs::path vst3Dir = fs::path(home) / ".vst3";
                if (fs::is_empty(vst3Dir)) {
                    fs::remove(vst3Dir);
                }
            }
        }
    }
};

TEST_F(PluginScanWithDummyBundle, DiscoversPluginInUserVst3Dir) {
    auto paths = VST3::Hosting::Module::getModulePaths();

    bool found = false;
    for (const auto& p : paths) {
        if (p.find("DummyTestPlugin.vst3") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "getModulePaths() did not discover DummyTestPlugin.vst3 in ~/.vst3/";
}

// ---------------------------------------------------------------------------
// Test: handleListAvailablePlugins() formats empty list correctly
// ---------------------------------------------------------------------------
TEST(PluginScanning, HandleListAvailablePluginsEmpty) {
    std::vector<std::string> empty;
    auto result = VST3MCPWrapper::handleListAvailablePlugins(empty);

    ASSERT_TRUE(result.contains("content"));
    ASSERT_TRUE(result["content"].is_array());
    ASSERT_EQ(result["content"].size(), 1u);
    EXPECT_EQ(result["content"][0]["type"], "text");

    // The text field should be a JSON-encoded empty array
    auto parsed = mcp::json::parse(result["content"][0]["text"].get<std::string>());
    EXPECT_TRUE(parsed.is_array());
    EXPECT_EQ(parsed.size(), 0u);
}

// ---------------------------------------------------------------------------
// Test: handleListAvailablePlugins() formats a list of paths correctly
// ---------------------------------------------------------------------------
TEST(PluginScanning, HandleListAvailablePluginsWithPaths) {
    std::vector<std::string> paths = {
        "/home/user/.vst3/PluginA.vst3",
        "/usr/lib/vst3/PluginB.vst3",
        "/usr/local/lib/vst3/PluginC.vst3"
    };
    auto result = VST3MCPWrapper::handleListAvailablePlugins(paths);

    ASSERT_TRUE(result.contains("content"));
    ASSERT_EQ(result["content"].size(), 1u);
    EXPECT_EQ(result["content"][0]["type"], "text");

    auto parsed = mcp::json::parse(result["content"][0]["text"].get<std::string>());
    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 3u);
    EXPECT_EQ(parsed[0], "/home/user/.vst3/PluginA.vst3");
    EXPECT_EQ(parsed[1], "/usr/lib/vst3/PluginB.vst3");
    EXPECT_EQ(parsed[2], "/usr/local/lib/vst3/PluginC.vst3");
}

// ---------------------------------------------------------------------------
// Test: handleListAvailablePlugins() wraps live scan results without error
// ---------------------------------------------------------------------------
TEST(PluginScanning, HandleListAvailablePluginsWithLiveScan) {
    auto paths = VST3::Hosting::Module::getModulePaths();
    mcp::json result;
    ASSERT_NO_THROW(result = VST3MCPWrapper::handleListAvailablePlugins(paths));

    ASSERT_TRUE(result.contains("content"));
    ASSERT_EQ(result["content"].size(), 1u);
    EXPECT_EQ(result["content"][0]["type"], "text");

    // Verify the text is valid JSON
    auto parsed = mcp::json::parse(result["content"][0]["text"].get<std::string>());
    EXPECT_TRUE(parsed.is_array());
    EXPECT_EQ(parsed.size(), paths.size());
}
