#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "processor.h"
#include "hostedplugin.h"
#include "helpers/processor_test_access.h"
#include "mocks/mock_vst3.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;
using namespace VST3MCPWrapper::Testing;

//------------------------------------------------------------------------
// Test fixture
//------------------------------------------------------------------------
class UnloadCleanupTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        processor_ = new Processor ();
        ASSERT_EQ (processor_->initialize (nullptr), kResultOk);
    }

    void TearDown () override
    {
        ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
        ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
        processor_->terminate ();
        processor_->release ();
    }

    Processor* processor_ = nullptr;
};

//------------------------------------------------------------------------
// unloadHostedPlugin calls setProcessing(false), setActive(false), terminate
// in the correct order when both flags are true
//------------------------------------------------------------------------
TEST_F (UnloadCleanupTest, UnloadCallsCleanupInCorrectOrder)
{
    MockComponent mockComp;
    MockAudioProcessor mockProc;

    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    // Set the hosted flags directly — simulating a state where the DAW had
    // previously called setActive/setProcessing and they were forwarded.
    ProcessorTestAccess::setHostedActive (*processor_, true);
    ProcessorTestAccess::setHostedProcessing (*processor_, true);

    // Expect the cleanup sequence: setProcessing(false) before setActive(false)
    // before terminate()
    {
        ::testing::InSequence seq;
        EXPECT_CALL (mockProc, setProcessing (false)).WillOnce (::testing::Return (kResultOk));
        EXPECT_CALL (mockComp, setActive (false)).WillOnce (::testing::Return (kResultOk));
        EXPECT_CALL (mockComp, terminate ()).WillOnce (::testing::Return (kResultOk));
    }

    // Trigger unload via the UnloadPlugin message
    MockMessage msg;
    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return ("UnloadPlugin"));

    processor_->notify (&msg);

    // After unload, all flags should be false
    EXPECT_FALSE (ProcessorTestAccess::processorReady (*processor_));
    EXPECT_FALSE (ProcessorTestAccess::hostedActive (*processor_));
    EXPECT_FALSE (ProcessorTestAccess::hostedProcessing (*processor_));

    // Mocks already cleared by unloadHostedPlugin
    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// unloadHostedPlugin skips setProcessing when hostedProcessing_ is false
//------------------------------------------------------------------------
TEST_F (UnloadCleanupTest, UnloadSkipsSetProcessingWhenNotProcessing)
{
    MockComponent mockComp;
    MockAudioProcessor mockProc;

    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);
    ProcessorTestAccess::setHostedActive (*processor_, true);
    ProcessorTestAccess::setHostedProcessing (*processor_, false);

    // setProcessing should NOT be called (hostedProcessing_ is false)
    EXPECT_CALL (mockProc, setProcessing (::testing::_)).Times (0);
    // setActive(false) should be called
    EXPECT_CALL (mockComp, setActive (false)).WillOnce (::testing::Return (kResultOk));
    EXPECT_CALL (mockComp, terminate ()).WillOnce (::testing::Return (kResultOk));

    MockMessage msg;
    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return ("UnloadPlugin"));

    processor_->notify (&msg);

    EXPECT_FALSE (ProcessorTestAccess::hostedActive (*processor_));

    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// unloadHostedPlugin skips setActive when hostedActive_ is false
//------------------------------------------------------------------------
TEST_F (UnloadCleanupTest, UnloadSkipsSetActiveWhenNotActive)
{
    MockComponent mockComp;
    MockAudioProcessor mockProc;

    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);
    ProcessorTestAccess::setHostedActive (*processor_, false);
    ProcessorTestAccess::setHostedProcessing (*processor_, false);

    // Neither should be called
    EXPECT_CALL (mockProc, setProcessing (::testing::_)).Times (0);
    EXPECT_CALL (mockComp, setActive (::testing::_)).Times (0);
    EXPECT_CALL (mockComp, terminate ()).WillOnce (::testing::Return (kResultOk));

    MockMessage msg;
    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return ("UnloadPlugin"));

    processor_->notify (&msg);

    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}
