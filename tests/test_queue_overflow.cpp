#include <gtest/gtest.h>

#include "hostedplugin.h"

#include <vector>

using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;

//------------------------------------------------------------------------
// Test fixture: resets the singleton param queue between tests
//------------------------------------------------------------------------
class QueueOverflowTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        auto& pm = HostedPluginModule::instance ();
        std::vector<ParamChange> drain;
        pm.drainParamChanges (drain);
    }

    void TearDown () override
    {
        auto& pm = HostedPluginModule::instance ();
        std::vector<ParamChange> drain;
        pm.drainParamChanges (drain);
    }
};

//------------------------------------------------------------------------
// Filling the queue to kMaxParamQueueSize drops further changes
//------------------------------------------------------------------------
TEST_F (QueueOverflowTest, OverflowDropsChanges)
{
    auto& pm = HostedPluginModule::instance ();

    // Fill up to the max (10,000)
    for (size_t i = 0; i < 10000; ++i)
        pm.pushParamChange (static_cast<ParamID> (i % 100), 0.5);

    // Push one more — should be dropped
    pm.pushParamChange (999, 0.99);

    std::vector<ParamChange> changes;
    pm.drainParamChanges (changes);

    EXPECT_EQ (changes.size (), 10000u);

    // Verify the overflow change was NOT included
    bool found999 = false;
    for (auto& c : changes) {
        if (c.id == 999 && c.value == 0.99)
            found999 = true;
    }
    EXPECT_FALSE (found999) << "Overflow change should have been dropped";
}

//------------------------------------------------------------------------
// Warn-once: overflow warning is logged once per episode.
// After draining, the flag resets implicitly when the queue is reloaded.
//------------------------------------------------------------------------
TEST_F (QueueOverflowTest, WarnOncePerOverflowEpisode)
{
    auto& pm = HostedPluginModule::instance ();

    // Fill queue to max
    for (size_t i = 0; i < 10000; ++i)
        pm.pushParamChange (static_cast<ParamID> (i), 0.5);

    // Push multiple overflow changes — only one warning should be logged.
    // We can't directly check stderr output, but we can verify the queue
    // stays at exactly kMaxParamQueueSize (no extra changes sneak in).
    pm.pushParamChange (1, 0.1);
    pm.pushParamChange (2, 0.2);
    pm.pushParamChange (3, 0.3);

    std::vector<ParamChange> changes;
    pm.drainParamChanges (changes);
    EXPECT_EQ (changes.size (), 10000u);
}

//------------------------------------------------------------------------
// After unload + reload, the overflow warning flag is reset
//------------------------------------------------------------------------
TEST_F (QueueOverflowTest, ReloadResetsOverflowFlag)
{
    auto& pm = HostedPluginModule::instance ();

    // Fill queue to max and trigger overflow
    for (size_t i = 0; i < 10001; ++i)
        pm.pushParamChange (static_cast<ParamID> (i), 0.5);

    // Drain
    std::vector<ParamChange> drain;
    pm.drainParamChanges (drain);
    EXPECT_EQ (drain.size (), 10000u);

    // Reload resets the queue and the overflow warning flag.
    // Since we can't actually load a real plugin, we use unload() which
    // calls resetState() that clears the queue and resets the flag.
    pm.unload ();

    // After reset, we should be able to push again without the warn flag
    // being stuck (i.e., changes are accepted normally)
    pm.pushParamChange (42, 0.42);

    drain.clear ();
    pm.drainParamChanges (drain);
    EXPECT_EQ (drain.size (), 1u);
    EXPECT_EQ (drain[0].id, 42u);
    EXPECT_DOUBLE_EQ (drain[0].value, 0.42);
}
