#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "controller.h"
#include "hostedplugin.h"
#include "mocks/mock_vst3.h"

#include "pluginterfaces/vst/ivstcomponent.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;
using namespace VST3MCPWrapper::Testing;

//------------------------------------------------------------------------
// Test fixture — creates a Controller WITHOUT initialize() to avoid
// starting the MCP server (heavyweight, binds port 8771). The
// IComponentHandler methods don't need MCP or bus setup.
//------------------------------------------------------------------------
class ControllerComponentHandlerTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        controller_ = new Controller ();

        // Drain any leftover param changes from previous tests
        std::vector<ParamChange> discard;
        HostedPluginModule::instance ().drainParamChanges (discard);
    }

    void TearDown () override
    {
        controller_->release ();

        // Drain any leftover param changes for test isolation
        std::vector<ParamChange> discard;
        HostedPluginModule::instance ().drainParamChanges (discard);
    }

    Controller* controller_ = nullptr;
};

//------------------------------------------------------------------------
// performEdit queues a parameter change via HostedPluginModule
//------------------------------------------------------------------------
TEST_F (ControllerComponentHandlerTest, PerformEditQueuesParamChange)
{
    auto* handler = static_cast<IComponentHandler*> (controller_);

    handler->performEdit (42, 0.75);

    // Drain the queue and verify the change is there
    std::vector<ParamChange> drained;
    HostedPluginModule::instance ().drainParamChanges (drained);

    ASSERT_EQ (drained.size (), 1u);
    EXPECT_EQ (drained[0].id, 42u);
    EXPECT_DOUBLE_EQ (drained[0].value, 0.75);
}

//------------------------------------------------------------------------
// restartComponent forwards to the DAW's IComponentHandler
//------------------------------------------------------------------------
TEST_F (ControllerComponentHandlerTest, RestartComponentForwardsToDawHandler)
{
    MockComponentHandler mockDawHandler;
    controller_->setComponentHandler (&mockDawHandler);

    EXPECT_CALL (mockDawHandler, restartComponent (kIoChanged))
        .WillOnce (::testing::Return (kResultOk));

    auto* handler = static_cast<IComponentHandler*> (controller_);
    EXPECT_EQ (handler->restartComponent (kIoChanged), kResultOk);

    // Clear before mock goes out of scope (IPtr holds a reference)
    controller_->setComponentHandler (nullptr);
}

//------------------------------------------------------------------------
// restartComponent returns kResultOk when no DAW handler is set
//------------------------------------------------------------------------
TEST_F (ControllerComponentHandlerTest, RestartComponentReturnsOkWithoutDawHandler)
{
    auto* handler = static_cast<IComponentHandler*> (controller_);
    EXPECT_EQ (handler->restartComponent (kIoChanged), kResultOk);
}

//------------------------------------------------------------------------
// beginEdit forwards to the DAW's IComponentHandler
//------------------------------------------------------------------------
TEST_F (ControllerComponentHandlerTest, BeginEditForwardsToDawHandler)
{
    MockComponentHandler mockDawHandler;
    controller_->setComponentHandler (&mockDawHandler);

    EXPECT_CALL (mockDawHandler, beginEdit (7))
        .WillOnce (::testing::Return (kResultOk));

    auto* handler = static_cast<IComponentHandler*> (controller_);
    EXPECT_EQ (handler->beginEdit (7), kResultOk);

    controller_->setComponentHandler (nullptr);
}

//------------------------------------------------------------------------
// endEdit forwards to the DAW's IComponentHandler
//------------------------------------------------------------------------
TEST_F (ControllerComponentHandlerTest, EndEditForwardsToDawHandler)
{
    MockComponentHandler mockDawHandler;
    controller_->setComponentHandler (&mockDawHandler);

    EXPECT_CALL (mockDawHandler, endEdit (7))
        .WillOnce (::testing::Return (kResultOk));

    auto* handler = static_cast<IComponentHandler*> (controller_);
    EXPECT_EQ (handler->endEdit (7), kResultOk);

    controller_->setComponentHandler (nullptr);
}

//------------------------------------------------------------------------
// beginEdit/endEdit return kResultOk when no DAW handler is set
//------------------------------------------------------------------------
TEST_F (ControllerComponentHandlerTest, BeginEndEditReturnOkWithoutDawHandler)
{
    auto* handler = static_cast<IComponentHandler*> (controller_);
    EXPECT_EQ (handler->beginEdit (7), kResultOk);
    EXPECT_EQ (handler->endEdit (7), kResultOk);
}
