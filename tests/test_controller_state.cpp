#include <gtest/gtest.h>

#include "controller.h"
#include "stateformat.h"
#include "helpers/controller_test_access.h"

#include "public.sdk/source/vst/utility/memoryibstream.h"

#include <cstring>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;

//------------------------------------------------------------------------
// Test fixture — creates a Controller WITHOUT initialize() to avoid
// starting the MCP server.  setComponentState() does not depend on
// initialize() — it reads from the stream and attempts setupHostedController
// internally.
//------------------------------------------------------------------------
class ControllerSetComponentStateTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        controller_ = new Controller ();
    }

    void TearDown () override
    {
        controller_->release ();
    }

    Controller* controller_ = nullptr;
};

//------------------------------------------------------------------------
// setComponentState() with a null stream returns kResultOk
// (null is treated as non-fatal on the controller side)
//------------------------------------------------------------------------
TEST_F (ControllerSetComponentStateTest, NullStreamReturnsOk)
{
    auto result = controller_->setComponentState (nullptr);
    EXPECT_EQ (result, kResultOk);
}

//------------------------------------------------------------------------
// setComponentState() with an empty plugin path does not set up a
// hosted controller — returns kResultOk with no side effects.
//------------------------------------------------------------------------
TEST_F (ControllerSetComponentStateTest, EmptyPathDoesNotSetupController)
{
    ResizableMemoryIBStream stream;
    EXPECT_EQ (writeStateHeader (&stream, ""), kResultOk);
    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = controller_->setComponentState (&stream);
    EXPECT_EQ (result, kResultOk);

    // No hosted controller should be set up
    EXPECT_EQ (ControllerTestAccess::hostedController (*controller_), nullptr);
    EXPECT_TRUE (ControllerTestAccess::currentPluginPath (*controller_).empty ());
}

//------------------------------------------------------------------------
// setComponentState() with a valid header containing a nonexistent plugin
// path attempts to load the plugin — the load fails on Linux (no real
// VST3 at that path), so no hosted controller is set up and
// currentPluginPath_ remains empty.
//------------------------------------------------------------------------
TEST_F (ControllerSetComponentStateTest, NonexistentPluginPathFailsGracefully)
{
    const std::string fakePath = "/nonexistent/path/FakePlugin.vst3";

    ResizableMemoryIBStream stream;
    EXPECT_EQ (writeStateHeader (&stream, fakePath), kResultOk);
    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = controller_->setComponentState (&stream);
    EXPECT_EQ (result, kResultOk);

    // Load failed, so no hosted controller and path not stored
    EXPECT_EQ (ControllerTestAccess::hostedController (*controller_), nullptr);
    EXPECT_TRUE (ControllerTestAccess::currentPluginPath (*controller_).empty ());
}

//------------------------------------------------------------------------
// setComponentState() with a corrupt header (bad magic) returns kResultOk
// — header parsing failure is non-fatal on the controller side.
//------------------------------------------------------------------------
TEST_F (ControllerSetComponentStateTest, CorruptMagicReturnsOk)
{
    ResizableMemoryIBStream stream;
    int32 numWritten = 0;

    const char badMagic[4] = {'B', 'A', 'D', '!'};
    stream.write (const_cast<char*> (badMagic), 4, &numWritten);

    uint32 version = 1;
    stream.write (&version, sizeof (version), &numWritten);

    uint32 pathLen = 0;
    stream.write (&pathLen, sizeof (pathLen), &numWritten);

    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = controller_->setComponentState (&stream);
    EXPECT_EQ (result, kResultOk);

    // No side effects
    EXPECT_EQ (ControllerTestAccess::hostedController (*controller_), nullptr);
    EXPECT_TRUE (ControllerTestAccess::currentPluginPath (*controller_).empty ());
}

//------------------------------------------------------------------------
// setComponentState() with a corrupt header (bad version) returns kResultOk
//------------------------------------------------------------------------
TEST_F (ControllerSetComponentStateTest, CorruptVersionReturnsOk)
{
    ResizableMemoryIBStream stream;
    int32 numWritten = 0;

    stream.write (const_cast<char*> (kStateMagic), 4, &numWritten);

    uint32 version = 99;
    stream.write (&version, sizeof (version), &numWritten);

    uint32 pathLen = 0;
    stream.write (&pathLen, sizeof (pathLen), &numWritten);

    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = controller_->setComponentState (&stream);
    EXPECT_EQ (result, kResultOk);

    EXPECT_EQ (ControllerTestAccess::hostedController (*controller_), nullptr);
    EXPECT_TRUE (ControllerTestAccess::currentPluginPath (*controller_).empty ());
}

//------------------------------------------------------------------------
// setComponentState() with a truncated stream (incomplete header)
// returns kResultOk — parsing failure is non-fatal.
//------------------------------------------------------------------------
TEST_F (ControllerSetComponentStateTest, TruncatedStreamReturnsOk)
{
    ResizableMemoryIBStream stream;
    int32 numWritten = 0;

    // Write only the magic — missing version and path
    stream.write (const_cast<char*> (kStateMagic), 4, &numWritten);

    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = controller_->setComponentState (&stream);
    EXPECT_EQ (result, kResultOk);

    EXPECT_EQ (ControllerTestAccess::hostedController (*controller_), nullptr);
}

//------------------------------------------------------------------------
// setComponentState() with an empty stream (zero bytes) returns kResultOk
//------------------------------------------------------------------------
TEST_F (ControllerSetComponentStateTest, EmptyStreamReturnsOk)
{
    ResizableMemoryIBStream stream;

    auto result = controller_->setComponentState (&stream);
    EXPECT_EQ (result, kResultOk);

    EXPECT_EQ (ControllerTestAccess::hostedController (*controller_), nullptr);
}
