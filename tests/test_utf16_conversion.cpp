#include <gtest/gtest.h>
#include "hostedplugin.h"

#include "pluginterfaces/vst/vsttypes.h"

using namespace Steinberg::Vst;

namespace VST3MCPWrapper {

// Helper to create a null-terminated TChar array from char16_t initializer list
template <std::size_t N>
static void fillTChar(TChar (&dest)[N], const char16_t* src, std::size_t srcLen) {
    for (std::size_t i = 0; i < srcLen && i < N; ++i)
        dest[i] = static_cast<TChar>(src[i]);
    if (srcLen < N)
        dest[srcLen] = 0;
}

// --- ASCII characters (code points < 0x80) ---

TEST(Utf16ToUtf8, AsciiCharacters) {
    const char16_t src[] = u"Hello, World!";
    TChar buf[128] = {};
    fillTChar(buf, src, 13);
    EXPECT_EQ(utf16ToUtf8(buf), "Hello, World!");
}

TEST(Utf16ToUtf8, AsciiAllPrintable) {
    // Test a range of printable ASCII
    const char16_t src[] = u"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    TChar buf[128] = {};
    fillTChar(buf, src, 62);
    EXPECT_EQ(utf16ToUtf8(buf), "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
}

// --- 2-byte UTF-8 sequences (code points 0x80-0x7FF) ---

TEST(Utf16ToUtf8, TwoByteSequences_AccentedChars) {
    // U+00E9 = é (Latin small letter e with acute)
    // UTF-8: 0xC3 0xA9
    TChar buf[128] = {};
    buf[0] = 0x00E9; // é
    buf[1] = 0;
    std::string result = utf16ToUtf8(buf);
    EXPECT_EQ(result, "\xC3\xA9");
    EXPECT_EQ(result, "é");
}

TEST(Utf16ToUtf8, TwoByteSequences_Mixed) {
    // "café" = c(0x63) a(0x61) f(0x66) é(0xE9)
    TChar buf[128] = {};
    buf[0] = 0x0063; // c
    buf[1] = 0x0061; // a
    buf[2] = 0x0066; // f
    buf[3] = 0x00E9; // é
    buf[4] = 0;
    EXPECT_EQ(utf16ToUtf8(buf), "café");
}

TEST(Utf16ToUtf8, TwoByteSequences_BoundaryLow) {
    // U+0080 is the first 2-byte code point
    TChar buf[128] = {};
    buf[0] = 0x0080;
    buf[1] = 0;
    std::string result = utf16ToUtf8(buf);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(result[0]), 0xC2);
    EXPECT_EQ(static_cast<uint8_t>(result[1]), 0x80);
}

TEST(Utf16ToUtf8, TwoByteSequences_BoundaryHigh) {
    // U+07FF is the last 2-byte code point
    TChar buf[128] = {};
    buf[0] = 0x07FF;
    buf[1] = 0;
    std::string result = utf16ToUtf8(buf);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(result[0]), 0xDF);
    EXPECT_EQ(static_cast<uint8_t>(result[1]), 0xBF);
}

// --- 3-byte UTF-8 sequences (code points 0x800+) ---

TEST(Utf16ToUtf8, ThreeByteSequences_CJK) {
    // U+4E16 = 世 (CJK character "world")
    // UTF-8: 0xE4 0xB8 0x96
    TChar buf[128] = {};
    buf[0] = 0x4E16; // 世
    buf[1] = 0;
    std::string result = utf16ToUtf8(buf);
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(result, "世");
}

TEST(Utf16ToUtf8, ThreeByteSequences_Boundary) {
    // U+0800 is the first 3-byte code point
    TChar buf[128] = {};
    buf[0] = 0x0800;
    buf[1] = 0;
    std::string result = utf16ToUtf8(buf);
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(static_cast<uint8_t>(result[0]), 0xE0);
    EXPECT_EQ(static_cast<uint8_t>(result[1]), 0xA0);
    EXPECT_EQ(static_cast<uint8_t>(result[2]), 0x80);
}

TEST(Utf16ToUtf8, ThreeByteSequences_Japanese) {
    // U+3042 = あ (Hiragana letter A)
    TChar buf[128] = {};
    buf[0] = 0x3042; // あ
    buf[1] = 0;
    std::string result = utf16ToUtf8(buf);
    EXPECT_EQ(result, "あ");
}

// --- Null termination handling ---

TEST(Utf16ToUtf8, NullTerminationMidString) {
    // Embedded null should stop conversion
    TChar buf[128] = {};
    buf[0] = 'A';
    buf[1] = 'B';
    buf[2] = 0;   // null terminator
    buf[3] = 'C'; // should not be included
    EXPECT_EQ(utf16ToUtf8(buf), "AB");
}

TEST(Utf16ToUtf8, NullTerminationAtStart) {
    TChar buf[128] = {};
    buf[0] = 0; // immediate null
    EXPECT_EQ(utf16ToUtf8(buf), "");
}

// --- Max length boundary (128 chars from VST3 String128) ---

TEST(Utf16ToUtf8, MaxLengthBoundary) {
    // Fill exactly 128 chars (default maxLen) with no null terminator
    TChar buf[129] = {};
    for (int i = 0; i < 128; ++i)
        buf[i] = 'X';
    buf[128] = 0; // sentinel beyond maxLen

    std::string result = utf16ToUtf8(buf);
    // Should convert exactly 128 characters
    EXPECT_EQ(result.size(), 128u);
    EXPECT_EQ(result, std::string(128, 'X'));
}

TEST(Utf16ToUtf8, MaxLengthStopsConversion) {
    // Fill 200 chars, but maxLen=128 should stop at 128
    TChar buf[200] = {};
    for (int i = 0; i < 200; ++i)
        buf[i] = 'Y';

    std::string result = utf16ToUtf8(buf, 128);
    EXPECT_EQ(result.size(), 128u);
}

TEST(Utf16ToUtf8, CustomMaxLength) {
    TChar buf[128] = {};
    buf[0] = 'A';
    buf[1] = 'B';
    buf[2] = 'C';
    buf[3] = 'D';
    buf[4] = 0;

    // With maxLen=2, only "AB" should be returned
    std::string result = utf16ToUtf8(buf, 2);
    EXPECT_EQ(result, "AB");
}

// --- Empty string input ---

TEST(Utf16ToUtf8, EmptyString) {
    TChar buf[1] = {0};
    EXPECT_EQ(utf16ToUtf8(buf), "");
}

TEST(Utf16ToUtf8, EmptyStringZeroMaxLen) {
    TChar buf[128] = {};
    buf[0] = 'A';
    // maxLen=0 should return empty
    EXPECT_EQ(utf16ToUtf8(buf, 0), "");
}

} // namespace VST3MCPWrapper
