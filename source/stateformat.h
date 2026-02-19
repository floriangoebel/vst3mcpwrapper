#pragma once

#include "pluginterfaces/base/ftypes.h"

namespace VST3MCPWrapper {

// Wrapper state persistence format constants.
// Used by both Processor (setState/getState) and Controller (setComponentState).
static constexpr char kStateMagic[4] = {'V', 'M', 'C', 'W'};
static constexpr Steinberg::uint32 kStateVersion = 1;
static constexpr Steinberg::uint32 kMaxPathLen = 4096;

} // namespace VST3MCPWrapper
