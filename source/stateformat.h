#pragma once

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>
#include <string>

namespace VST3MCPWrapper {

// Wrapper state persistence format constants.
// Used by both Processor (setState/getState) and Controller (setComponentState).
static constexpr char kStateMagic[4] = {'V', 'M', 'C', 'W'};
static constexpr Steinberg::uint32 kStateVersion = 1;
static constexpr Steinberg::uint32 kMaxPathLen = 4096;

// Write the wrapper state header to a stream.
// Format: [4 bytes magic] [4 bytes version] [4 bytes pathLen] [pathLen bytes path]
inline Steinberg::tresult writeStateHeader(Steinberg::IBStream* state, const std::string& pluginPath) {
    using namespace Steinberg;
    if (!state)
        return kResultFalse;

    int32 numBytesWritten = 0;

    if (state->write(const_cast<char*>(kStateMagic), sizeof(kStateMagic), &numBytesWritten) != kResultOk)
        return kResultFalse;

    uint32 version = kStateVersion;
    if (state->write(&version, sizeof(version), &numBytesWritten) != kResultOk)
        return kResultFalse;

    uint32 pathLen = static_cast<uint32>(pluginPath.size());
    if (state->write(&pathLen, sizeof(pathLen), &numBytesWritten) != kResultOk)
        return kResultFalse;

    if (pathLen > 0) {
        if (state->write(const_cast<char*>(pluginPath.data()), pathLen, &numBytesWritten) != kResultOk)
            return kResultFalse;
    }

    return kResultOk;
}

// Read and validate the wrapper state header from a stream.
// Returns kResultOk on success with pluginPath populated.
// Returns kResultFalse on invalid magic, unsupported version, bad path length, or truncated data.
inline Steinberg::tresult readStateHeader(Steinberg::IBStream* state, std::string& pluginPath) {
    using namespace Steinberg;
    if (!state)
        return kResultFalse;

    int32 numBytesRead = 0;

    char magic[4] = {};
    if (state->read(magic, sizeof(magic), &numBytesRead) != kResultOk || numBytesRead != sizeof(magic))
        return kResultFalse;

    if (std::memcmp(magic, kStateMagic, sizeof(magic)) != 0)
        return kResultFalse;

    uint32 version = 0;
    if (state->read(&version, sizeof(version), &numBytesRead) != kResultOk
        || numBytesRead != sizeof(version))
        return kResultFalse;

    if (version != kStateVersion)
        return kResultFalse;

    uint32 pathLen = 0;
    if (state->read(&pathLen, sizeof(pathLen), &numBytesRead) != kResultOk
        || numBytesRead != sizeof(pathLen))
        return kResultFalse;

    if (pathLen > kMaxPathLen)
        return kResultFalse;

    pluginPath.clear();
    if (pathLen > 0) {
        pluginPath.resize(pathLen);
        if (state->read(pluginPath.data(), pathLen, &numBytesRead) != kResultOk
            || numBytesRead != static_cast<int32>(pathLen))
            return kResultFalse;
    }

    return kResultOk;
}

} // namespace VST3MCPWrapper
