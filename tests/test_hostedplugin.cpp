#include <gtest/gtest.h>

#include "hostedplugin.h"

#include <cstring>

using namespace VST3MCPWrapper;
using namespace Steinberg;
using namespace Steinberg::Vst;

class HostedPluginModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& mod = HostedPluginModule::instance();
        mod.unload();
        // Drain any leftover param changes from other tests
        std::vector<ParamChange> drain;
        mod.drainParamChanges(drain);
    }

    void TearDown() override {
        auto& mod = HostedPluginModule::instance();
        mod.unload();
        std::vector<ParamChange> drain;
        mod.drainParamChanges(drain);
    }
};

// --- isLoaded tests ---

TEST_F(HostedPluginModuleTest, IsLoadedReturnsFalseOnFreshSingleton) {
    auto& mod = HostedPluginModule::instance();
    EXPECT_FALSE(mod.isLoaded());
}

// --- load with invalid path ---

TEST_F(HostedPluginModuleTest, LoadInvalidPathReturnsFalseWithError) {
    auto& mod = HostedPluginModule::instance();
    std::string error;
    bool result = mod.load("/nonexistent/path/to/plugin.vst3", error);
    EXPECT_FALSE(result);
    EXPECT_FALSE(error.empty());
}

TEST_F(HostedPluginModuleTest, IsLoadedRemainsFalseAfterFailedLoad) {
    auto& mod = HostedPluginModule::instance();
    std::string error;
    mod.load("/nonexistent/path/to/plugin.vst3", error);
    EXPECT_FALSE(mod.isLoaded());
}

// --- getPluginPath ---

TEST_F(HostedPluginModuleTest, GetPluginPathReturnsEmptyWhenNotLoaded) {
    auto& mod = HostedPluginModule::instance();
    EXPECT_TRUE(mod.getPluginPath().empty());
}

// --- Controller class ID ---

TEST_F(HostedPluginModuleTest, HasControllerClassIDReturnsFalseBeforeSet) {
    auto& mod = HostedPluginModule::instance();
    EXPECT_FALSE(mod.hasControllerClassID());
}

TEST_F(HostedPluginModuleTest, SetGetControllerClassIDRoundTrip) {
    auto& mod = HostedPluginModule::instance();

    // Create a known TUID
    TUID testCID;
    for (int i = 0; i < 16; ++i)
        testCID[i] = static_cast<char>(i + 1);

    mod.setControllerClassID(testCID);
    EXPECT_TRUE(mod.hasControllerClassID());

    TUID retrieved;
    mod.getControllerClassID(retrieved);
    EXPECT_EQ(std::memcmp(testCID, retrieved, sizeof(TUID)), 0);
}

TEST_F(HostedPluginModuleTest, SetControllerClassIDOverwritesPrevious) {
    auto& mod = HostedPluginModule::instance();

    TUID first;
    std::memset(first, 0xAA, sizeof(TUID));
    mod.setControllerClassID(first);

    TUID second;
    std::memset(second, 0xBB, sizeof(TUID));
    mod.setControllerClassID(second);

    TUID retrieved;
    mod.getControllerClassID(retrieved);
    EXPECT_EQ(std::memcmp(second, retrieved, sizeof(TUID)), 0);
}

// --- Hosted component ---

TEST_F(HostedPluginModuleTest, SetHostedComponentNullReturnsNullptr) {
    auto& mod = HostedPluginModule::instance();
    mod.setHostedComponent(nullptr);
    auto comp = mod.getHostedComponent();
    EXPECT_EQ(comp, nullptr);
}

TEST_F(HostedPluginModuleTest, GetHostedComponentReturnsNullptrInitially) {
    auto& mod = HostedPluginModule::instance();
    auto comp = mod.getHostedComponent();
    EXPECT_EQ(comp, nullptr);
}

// --- unload resets all state ---

TEST_F(HostedPluginModuleTest, UnloadResetsAllState) {
    auto& mod = HostedPluginModule::instance();

    // Set some state
    TUID testCID;
    std::memset(testCID, 0xFF, sizeof(TUID));
    mod.setControllerClassID(testCID);

    // Push a param change
    mod.pushParamChange(42, 0.5);

    // Unload should reset everything
    mod.unload();

    EXPECT_FALSE(mod.isLoaded());
    EXPECT_TRUE(mod.getPluginPath().empty());
    EXPECT_FALSE(mod.hasControllerClassID());
    EXPECT_EQ(mod.getHostedComponent(), nullptr);

    // Param queue should be cleared
    std::vector<ParamChange> drain;
    mod.drainParamChanges(drain);
    EXPECT_TRUE(drain.empty());

    // Controller class ID should be zeroed
    TUID zeroCID;
    std::memset(zeroCID, 0, sizeof(TUID));
    TUID retrieved;
    mod.getControllerClassID(retrieved);
    EXPECT_EQ(std::memcmp(zeroCID, retrieved, sizeof(TUID)), 0);
}

// --- getFactory ---

TEST_F(HostedPluginModuleTest, GetFactoryReturnsNulloptWhenNotLoaded) {
    auto& mod = HostedPluginModule::instance();
    auto factory = mod.getFactory();
    EXPECT_FALSE(factory.has_value());
}
