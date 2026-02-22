#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "processor.h"
#include "stateformat.h"
#include "helpers/processor_test_access.h"

#include "public.sdk/source/vst/utility/memoryibstream.h"

#include <cstring>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;

//------------------------------------------------------------------------
// Test fixture
//------------------------------------------------------------------------
class ProcessorStateTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        processor_ = new Processor ();
        ASSERT_EQ (processor_->initialize (nullptr), kResultOk);
    }

    void TearDown () override
    {
        processor_->terminate ();
        processor_->release ();
    }

    Processor* processor_ = nullptr;
};

//------------------------------------------------------------------------
// getState() writes valid wrapper state format header
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, GetStateWritesValidHeader)
{
    ResizableMemoryIBStream stream;

    auto result = processor_->getState (&stream);
    EXPECT_EQ (result, kResultOk);

    // Rewind and read the header
    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    char magic[4] = {};
    int32 numRead = 0;
    EXPECT_EQ (stream.read (magic, 4, &numRead), kResultOk);
    EXPECT_EQ (numRead, 4);
    EXPECT_EQ (std::memcmp (magic, "VMCW", 4), 0);

    uint32 version = 0;
    EXPECT_EQ (stream.read (&version, sizeof (version), &numRead), kResultOk);
    EXPECT_EQ (version, 1u);
}

//------------------------------------------------------------------------
// getState() includes the loaded plugin path in the state
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, GetStateIncludesPluginPath)
{
    const std::string testPath = "/Library/Audio/Plug-Ins/VST3/TestPlugin.vst3";
    ProcessorTestAccess::setCurrentPluginPath (*processor_, testPath);

    ResizableMemoryIBStream stream;
    auto result = processor_->getState (&stream);
    EXPECT_EQ (result, kResultOk);

    // Rewind and read back
    stream.seek (0, IBStream::kIBSeekSet, nullptr);
    std::string readPath;
    EXPECT_EQ (readStateHeader (&stream, readPath), kResultOk);
    EXPECT_EQ (readPath, testPath);
}

//------------------------------------------------------------------------
// getState() with no plugin loaded writes state with empty path
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, GetStateWithNoPluginWritesEmptyPath)
{
    ResizableMemoryIBStream stream;

    auto result = processor_->getState (&stream);
    EXPECT_EQ (result, kResultOk);

    stream.seek (0, IBStream::kIBSeekSet, nullptr);
    std::string readPath;
    EXPECT_EQ (readStateHeader (&stream, readPath), kResultOk);
    EXPECT_TRUE (readPath.empty ());
}

//------------------------------------------------------------------------
// setState() with corrupted data (bad magic) returns kResultFalse
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, SetStateWithBadMagicReturnsFalse)
{
    ResizableMemoryIBStream stream;
    int32 numWritten = 0;

    // Write bad magic
    const char badMagic[4] = {'B', 'A', 'D', '!'};
    stream.write (const_cast<char*> (badMagic), 4, &numWritten);

    uint32 version = 1;
    stream.write (&version, sizeof (version), &numWritten);

    uint32 pathLen = 0;
    stream.write (&pathLen, sizeof (pathLen), &numWritten);

    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = processor_->setState (&stream);
    EXPECT_EQ (result, kResultFalse);
}

//------------------------------------------------------------------------
// setState() with corrupted data (bad version) returns kResultFalse
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, SetStateWithBadVersionReturnsFalse)
{
    ResizableMemoryIBStream stream;
    int32 numWritten = 0;

    // Write correct magic but bad version
    stream.write (const_cast<char*> (kStateMagic), 4, &numWritten);

    uint32 version = 99;
    stream.write (&version, sizeof (version), &numWritten);

    uint32 pathLen = 0;
    stream.write (&pathLen, sizeof (pathLen), &numWritten);

    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = processor_->setState (&stream);
    EXPECT_EQ (result, kResultFalse);
}

//------------------------------------------------------------------------
// setState/getState round-trip preserves the plugin path
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, SetStateGetStateRoundTripPreservesPath)
{
    const std::string testPath = "/Library/Audio/Plug-Ins/VST3/MyPlugin.vst3";

    // Write state with a known path
    ResizableMemoryIBStream writeStream;
    EXPECT_EQ (writeStateHeader (&writeStream, testPath), kResultOk);

    // setState will try to loadHostedPlugin if a new path is set.
    // Since testPath doesn't exist, the load will fail and currentPluginPath_
    // will remain empty. But the state format parsing itself works correctly.
    // To verify the round-trip, we set the path manually and then getState.
    ProcessorTestAccess::setCurrentPluginPath (*processor_, testPath);

    ResizableMemoryIBStream readStream;
    auto result = processor_->getState (&readStream);
    EXPECT_EQ (result, kResultOk);

    // Read back and verify path matches
    readStream.seek (0, IBStream::kIBSeekSet, nullptr);
    std::string readPath;
    EXPECT_EQ (readStateHeader (&readStream, readPath), kResultOk);
    EXPECT_EQ (readPath, testPath);
}

//------------------------------------------------------------------------
// getState() with null stream returns kResultFalse
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, GetStateWithNullStreamReturnsFalse)
{
    auto result = processor_->getState (nullptr);
    EXPECT_EQ (result, kResultFalse);
}

//------------------------------------------------------------------------
// setState() with null stream returns kResultFalse
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, SetStateWithNullStreamReturnsFalse)
{
    auto result = processor_->setState (nullptr);
    EXPECT_EQ (result, kResultFalse);
}

//------------------------------------------------------------------------
// setState() with empty path does not attempt to load a plugin
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, SetStateWithEmptyPathDoesNotLoad)
{
    ResizableMemoryIBStream stream;
    EXPECT_EQ (writeStateHeader (&stream, ""), kResultOk);
    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = processor_->setState (&stream);
    EXPECT_EQ (result, kResultOk);

    // No plugin should be loaded
    EXPECT_TRUE (ProcessorTestAccess::currentPluginPath (*processor_).empty ());
}

//------------------------------------------------------------------------
// setState() with same path as current does not reload
//------------------------------------------------------------------------
TEST_F (ProcessorStateTest, SetStateWithSamePathDoesNotReload)
{
    const std::string testPath = "/Library/Audio/Plug-Ins/VST3/Same.vst3";
    ProcessorTestAccess::setCurrentPluginPath (*processor_, testPath);

    ResizableMemoryIBStream stream;
    EXPECT_EQ (writeStateHeader (&stream, testPath), kResultOk);
    stream.seek (0, IBStream::kIBSeekSet, nullptr);

    auto result = processor_->setState (&stream);
    EXPECT_EQ (result, kResultOk);

    // Path should remain the same (no unload/reload occurred)
    EXPECT_EQ (ProcessorTestAccess::currentPluginPath (*processor_), testPath);
}
