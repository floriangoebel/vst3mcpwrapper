#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "processor.h"
#include "messageids.h"
#include "hostedplugin.h"
#include "mocks/mock_vst3.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;
using namespace VST3MCPWrapper::Testing;

//------------------------------------------------------------------------
// Test access helper for Processor private members
//------------------------------------------------------------------------
namespace VST3MCPWrapper {

class ProcessorTestAccess {
public:
    static const std::string& currentPluginPath (const Processor& p)
    {
        return p.currentPluginPath_;
    }
    static void setHostedComponent (Processor& p, IComponent* comp)
    {
        p.hostedComponent_ = comp;
    }
    static void setHostedProcessor (Processor& p, IAudioProcessor* proc)
    {
        p.hostedProcessor_ = proc;
    }
    static bool processorReady (const Processor& p)
    {
        return p.processorReady_.load ();
    }
};

} // namespace VST3MCPWrapper

//------------------------------------------------------------------------
// Test fixture
//------------------------------------------------------------------------
class MessageRoutingTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        processor_ = new Processor ();
        ASSERT_EQ (processor_->initialize (nullptr), kResultOk);

        // Drain any stale param changes from singleton
        std::vector<ParamChange> drain;
        HostedPluginModule::instance ().drainParamChanges (drain);
    }

    void TearDown () override
    {
        // Clear any injected mocks before terminate
        ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
        ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
        processor_->terminate ();
        processor_->release ();
    }

    Processor* processor_ = nullptr;
};

//------------------------------------------------------------------------
// LoadPlugin message with valid path attribute extracts the path correctly
//------------------------------------------------------------------------
TEST_F (MessageRoutingTest, LoadPluginMessageExtractsPath)
{
    // Inject a mock hosted component so unloadHostedPlugin() has something to clear
    MockComponent mockComp;
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    // Expect unloadHostedPlugin to call setProcessing(false), setActive(false), terminate()
    // on the mock hosted component (since hostedProcessing_ and hostedActive_ are false by
    // default, these won't be called — only terminate will be called)
    EXPECT_CALL (mockComp, terminate ()).WillOnce (::testing::Return (kResultOk));

    // Set up a LoadPlugin message with a path
    MockMessage msg;
    MockAttributeList attrs;

    std::string testPath = "/path/to/test.vst3";
    const void* pathData = testPath.data ();
    uint32 pathSize = static_cast<uint32> (testPath.size ());

    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return (MessageIds::kLoadPlugin));
    EXPECT_CALL (msg, getAttributes ())
        .WillRepeatedly (::testing::Return (&attrs));
    EXPECT_CALL (attrs, getBinary (::testing::StrEq ("path"), ::testing::_, ::testing::_))
        .WillOnce (::testing::DoAll (
            ::testing::SetArgReferee<1> (pathData),
            ::testing::SetArgReferee<2> (pathSize),
            ::testing::Return (kResultOk)));

    tresult result = processor_->notify (&msg);

    // notify returns kResultOk for LoadPlugin messages regardless of load success
    EXPECT_EQ (result, kResultOk);

    // The previously injected mock component was cleared by unloadHostedPlugin()
    // loadHostedPlugin() failed (path doesn't exist), so no new component was loaded
    EXPECT_FALSE (ProcessorTestAccess::processorReady (*processor_));

    // Clear mocks (already cleared by unloadHostedPlugin, but be safe)
    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// Unrecognized message IDs are ignored (no crash, returns kResultFalse)
//------------------------------------------------------------------------
TEST_F (MessageRoutingTest, UnrecognizedMessageIdReturnsResultFalse)
{
    MockMessage msg;

    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return ("SomeUnknownMessage"));

    tresult result = processor_->notify (&msg);

    // AudioEffect::notify returns kResultFalse for unrecognized messages
    EXPECT_EQ (result, kResultFalse);
}

//------------------------------------------------------------------------
// Message with missing path attribute is handled gracefully (no crash)
//------------------------------------------------------------------------
TEST_F (MessageRoutingTest, LoadPluginMessageMissingPathAttribute)
{
    MockMessage msg;
    MockAttributeList attrs;

    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return (MessageIds::kLoadPlugin));
    EXPECT_CALL (msg, getAttributes ())
        .WillRepeatedly (::testing::Return (&attrs));
    // getBinary fails — path attribute not set
    EXPECT_CALL (attrs, getBinary (::testing::StrEq ("path"), ::testing::_, ::testing::_))
        .WillOnce (::testing::Return (kResultFalse));

    tresult result = processor_->notify (&msg);

    // Returns kResultOk (entered LoadPlugin branch) but no loading occurred
    EXPECT_EQ (result, kResultOk);
    EXPECT_TRUE (ProcessorTestAccess::currentPluginPath (*processor_).empty ());
}

//------------------------------------------------------------------------
// Message with empty path attribute (size = 0) is handled gracefully
//------------------------------------------------------------------------
TEST_F (MessageRoutingTest, LoadPluginMessageEmptyPathAttribute)
{
    MockMessage msg;
    MockAttributeList attrs;

    const void* emptyData = "";
    uint32 zeroSize = 0;

    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return (MessageIds::kLoadPlugin));
    EXPECT_CALL (msg, getAttributes ())
        .WillRepeatedly (::testing::Return (&attrs));
    // getBinary succeeds but size is 0
    EXPECT_CALL (attrs, getBinary (::testing::StrEq ("path"), ::testing::_, ::testing::_))
        .WillOnce (::testing::DoAll (
            ::testing::SetArgReferee<1> (emptyData),
            ::testing::SetArgReferee<2> (zeroSize),
            ::testing::Return (kResultOk)));

    tresult result = processor_->notify (&msg);

    // Returns kResultOk but the size > 0 check prevents loading
    EXPECT_EQ (result, kResultOk);
    EXPECT_TRUE (ProcessorTestAccess::currentPluginPath (*processor_).empty ());
}

//------------------------------------------------------------------------
// LoadPlugin message with getBinary returning kResultOk but data=nullptr
// does not crash (defense-in-depth for malformed IMessage implementations)
//------------------------------------------------------------------------
TEST_F (MessageRoutingTest, LoadPluginMessageNullDataPointerNoCrash)
{
    MockMessage msg;
    MockAttributeList attrs;

    const void* nullData = nullptr;
    uint32 pathSize = 10; // non-zero size but null data pointer

    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return (MessageIds::kLoadPlugin));
    EXPECT_CALL (msg, getAttributes ())
        .WillRepeatedly (::testing::Return (&attrs));
    // getBinary returns kResultOk but data pointer is nullptr
    EXPECT_CALL (attrs, getBinary (::testing::StrEq ("path"), ::testing::_, ::testing::_))
        .WillOnce (::testing::DoAll (
            ::testing::SetArgReferee<1> (nullData),
            ::testing::SetArgReferee<2> (pathSize),
            ::testing::Return (kResultOk)));

    tresult result = processor_->notify (&msg);

    // Returns kResultOk (entered LoadPlugin branch) but no loading occurred
    EXPECT_EQ (result, kResultOk);
    EXPECT_TRUE (ProcessorTestAccess::currentPluginPath (*processor_).empty ());
}

//------------------------------------------------------------------------
// Null message returns kResultFalse
//------------------------------------------------------------------------
TEST_F (MessageRoutingTest, NullMessageReturnsResultFalse)
{
    tresult result = processor_->notify (nullptr);
    EXPECT_EQ (result, kResultFalse);
}

//------------------------------------------------------------------------
// UnloadPlugin message unloads the hosted plugin
//------------------------------------------------------------------------
TEST_F (MessageRoutingTest, UnloadPluginMessageUnloadsPlugin)
{
    // Inject a mock hosted component so unloadHostedPlugin has something to clear
    MockComponent mockComp;
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    EXPECT_CALL (mockComp, terminate ()).WillOnce (::testing::Return (kResultOk));

    MockMessage msg;
    EXPECT_CALL (msg, getMessageID ())
        .WillRepeatedly (::testing::Return (MessageIds::kUnloadPlugin));

    tresult result = processor_->notify (&msg);

    EXPECT_EQ (result, kResultOk);
    EXPECT_FALSE (ProcessorTestAccess::processorReady (*processor_));
    EXPECT_TRUE (ProcessorTestAccess::currentPluginPath (*processor_).empty ());

    // Mocks already cleared by unloadHostedPlugin
    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}
