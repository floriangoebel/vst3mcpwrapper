#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace VST3MCPWrapper {
namespace Testing {

// Fill a TChar array from a char16_t literal string.
// Works with both fixed-size arrays and pointers.
inline void fillTChar (Steinberg::Vst::TChar* dest, const char16_t* src, size_t maxLen = 128)
{
    size_t i = 0;
    while (i < maxLen - 1 && src[i] != 0) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = 0;
}

} // namespace Testing
} // namespace VST3MCPWrapper
