#include <gtest/gtest.h>
#include "stateformat.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"

using namespace Steinberg;
using namespace VST3MCPWrapper;

// --- Round-trip tests ---

TEST(StateFormat, RoundTripWithPath) {
    const std::string path = "/Library/Audio/Plug-Ins/VST3/MyPlugin.vst3";

    ResizableMemoryIBStream stream;
    ASSERT_EQ(writeStateHeader(&stream, path), kResultOk);

    stream.rewind();

    std::string readPath;
    ASSERT_EQ(readStateHeader(&stream, readPath), kResultOk);
    EXPECT_EQ(readPath, path);
}

TEST(StateFormat, RoundTripWithEmptyPath) {
    const std::string path;

    ResizableMemoryIBStream stream;
    ASSERT_EQ(writeStateHeader(&stream, path), kResultOk);

    stream.rewind();

    std::string readPath;
    ASSERT_EQ(readStateHeader(&stream, readPath), kResultOk);
    EXPECT_TRUE(readPath.empty());
}

TEST(StateFormat, RoundTripWithLongPath) {
    // Path at max allowed length
    const std::string path(kMaxPathLen, 'x');

    ResizableMemoryIBStream stream;
    ASSERT_EQ(writeStateHeader(&stream, path), kResultOk);

    stream.rewind();

    std::string readPath;
    ASSERT_EQ(readStateHeader(&stream, readPath), kResultOk);
    EXPECT_EQ(readPath, path);
}

// --- Invalid magic ---

TEST(StateFormat, InvalidMagicRejected) {
    ResizableMemoryIBStream stream;
    int32 written = 0;

    // Write wrong magic
    char badMagic[4] = {'B', 'A', 'D', '!'};
    stream.write(badMagic, sizeof(badMagic), &written);

    // Write valid version and pathLen
    uint32 version = kStateVersion;
    stream.write(&version, sizeof(version), &written);
    uint32 pathLen = 0;
    stream.write(&pathLen, sizeof(pathLen), &written);

    stream.rewind();

    std::string readPath;
    EXPECT_EQ(readStateHeader(&stream, readPath), kResultFalse);
}

// --- Unsupported version ---

TEST(StateFormat, UnsupportedVersionRejected) {
    ResizableMemoryIBStream stream;
    int32 written = 0;

    // Write valid magic
    stream.write(const_cast<char*>(kStateMagic), sizeof(kStateMagic), &written);

    // Write unsupported version
    uint32 badVersion = 99;
    stream.write(&badVersion, sizeof(badVersion), &written);

    // Write valid pathLen
    uint32 pathLen = 0;
    stream.write(&pathLen, sizeof(pathLen), &written);

    stream.rewind();

    std::string readPath;
    EXPECT_EQ(readStateHeader(&stream, readPath), kResultFalse);
}

// --- Path length exceeding 4096 ---

TEST(StateFormat, PathLengthExceedingMaxRejected) {
    ResizableMemoryIBStream stream;
    int32 written = 0;

    // Write valid magic and version
    stream.write(const_cast<char*>(kStateMagic), sizeof(kStateMagic), &written);
    uint32 version = kStateVersion;
    stream.write(&version, sizeof(version), &written);

    // Write pathLen exceeding max
    uint32 pathLen = kMaxPathLen + 1;
    stream.write(&pathLen, sizeof(pathLen), &written);

    stream.rewind();

    std::string readPath;
    EXPECT_EQ(readStateHeader(&stream, readPath), kResultFalse);
}

// --- Truncated stream ---

TEST(StateFormat, TruncatedStreamMagicOnly) {
    ResizableMemoryIBStream stream;
    int32 written = 0;

    // Write only magic, nothing else
    stream.write(const_cast<char*>(kStateMagic), sizeof(kStateMagic), &written);

    stream.rewind();

    std::string readPath;
    EXPECT_EQ(readStateHeader(&stream, readPath), kResultFalse);
}

TEST(StateFormat, TruncatedStreamMissingPath) {
    ResizableMemoryIBStream stream;
    int32 written = 0;

    // Write valid header claiming 20 bytes of path data
    stream.write(const_cast<char*>(kStateMagic), sizeof(kStateMagic), &written);
    uint32 version = kStateVersion;
    stream.write(&version, sizeof(version), &written);
    uint32 pathLen = 20;
    stream.write(&pathLen, sizeof(pathLen), &written);

    // Write only 5 bytes of path data (less than the 20 declared)
    const char partialPath[] = "hello";
    stream.write(const_cast<char*>(partialPath), 5, &written);

    stream.rewind();

    std::string readPath;
    EXPECT_EQ(readStateHeader(&stream, readPath), kResultFalse);
}

TEST(StateFormat, TruncatedStreamPartialMagic) {
    ResizableMemoryIBStream stream;
    int32 written = 0;

    // Write only 2 bytes of magic
    stream.write(const_cast<char*>(kStateMagic), 2, &written);

    stream.rewind();

    std::string readPath;
    EXPECT_EQ(readStateHeader(&stream, readPath), kResultFalse);
}

TEST(StateFormat, EmptyStream) {
    ResizableMemoryIBStream stream;

    std::string readPath;
    EXPECT_EQ(readStateHeader(&stream, readPath), kResultFalse);
}

// --- Additional data after path (simulating hosted component state) ---

TEST(StateFormat, AdditionalDataAfterPath) {
    const std::string path = "/path/to/plugin.vst3";
    const std::string hostedState = "HOSTED_COMPONENT_STATE_DATA_HERE";

    ResizableMemoryIBStream stream;
    int32 written = 0;

    // Write the state header
    ASSERT_EQ(writeStateHeader(&stream, path), kResultOk);

    // Append additional hosted component state
    stream.write(const_cast<char*>(hostedState.data()),
                 static_cast<int32>(hostedState.size()), &written);

    stream.rewind();

    // Read back the header - should succeed and leave stream positioned after the path
    std::string readPath;
    ASSERT_EQ(readStateHeader(&stream, readPath), kResultOk);
    EXPECT_EQ(readPath, path);

    // Verify the remaining data (hosted component state) is still readable
    std::string remaining;
    remaining.resize(hostedState.size());
    int32 numRead = 0;
    ASSERT_EQ(stream.read(remaining.data(), static_cast<int32>(hostedState.size()), &numRead), kResultOk);
    EXPECT_EQ(numRead, static_cast<int32>(hostedState.size()));
    EXPECT_EQ(remaining, hostedState);
}

// --- Null stream ---

TEST(StateFormat, WriteNullStreamFails) {
    EXPECT_EQ(writeStateHeader(nullptr, "test"), kResultFalse);
}

TEST(StateFormat, ReadNullStreamFails) {
    std::string path;
    EXPECT_EQ(readStateHeader(nullptr, path), kResultFalse);
}
