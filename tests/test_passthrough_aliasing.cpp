#include <gtest/gtest.h>

#include "processor.h"
#include "helpers/processor_test_access.h"

#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;

//------------------------------------------------------------------------
// Test fixture
//------------------------------------------------------------------------
class PassthroughAliasingTest : public ::testing::Test {
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
// Aliased buffers (input == output): passthrough skips memcpy, data unchanged
//------------------------------------------------------------------------
TEST_F (PassthroughAliasingTest, AliasedBuffers32BitSkipsCopy)
{
    const int numSamples = 256;
    const int numChannels = 2;

    // Allocate shared buffers for both input and output
    std::vector<std::vector<float>> sharedData (numChannels, std::vector<float> (numSamples));
    std::vector<float*> ptrs (numChannels);

    for (int ch = 0; ch < numChannels; ++ch) {
        for (int s = 0; s < numSamples; ++s)
            sharedData[ch][s] = static_cast<float> (ch * 1000 + s) / 10000.0f;
        ptrs[ch] = sharedData[ch].data ();
    }

    // Both input and output point to the same buffers (aliased)
    AudioBusBuffers bus{};
    bus.numChannels = numChannels;
    bus.silenceFlags = 0;
    bus.channelBuffers32 = ptrs.data ();

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &bus;
    data.outputs = &bus;  // Same bus — aliased

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    // Data should be unchanged (memcpy was skipped because dst == src)
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            EXPECT_FLOAT_EQ (sharedData[ch][s], static_cast<float> (ch * 1000 + s) / 10000.0f)
                << "Data corrupted at ch=" << ch << " s=" << s;
}

//------------------------------------------------------------------------
// Aliased buffers 64-bit: passthrough skips memcpy, data unchanged
//------------------------------------------------------------------------
TEST_F (PassthroughAliasingTest, AliasedBuffers64BitSkipsCopy)
{
    const int numSamples = 128;
    const int numChannels = 2;

    std::vector<std::vector<double>> sharedData (numChannels, std::vector<double> (numSamples));
    std::vector<double*> ptrs (numChannels);

    for (int ch = 0; ch < numChannels; ++ch) {
        for (int s = 0; s < numSamples; ++s)
            sharedData[ch][s] = static_cast<double> (ch * 1000 + s) / 10000.0;
        ptrs[ch] = sharedData[ch].data ();
    }

    AudioBusBuffers bus{};
    bus.numChannels = numChannels;
    bus.silenceFlags = 0;
    bus.channelBuffers64 = ptrs.data ();

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample64;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &bus;
    data.outputs = &bus;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            EXPECT_DOUBLE_EQ (sharedData[ch][s], static_cast<double> (ch * 1000 + s) / 10000.0)
                << "Data corrupted at ch=" << ch << " s=" << s;
}

//------------------------------------------------------------------------
// Per-channel aliasing: ch0 aliased, ch1 separate — both correct
//------------------------------------------------------------------------
TEST_F (PassthroughAliasingTest, PerChannelAliasing32Bit)
{
    const int numSamples = 64;

    // Channel 0: shared buffer (aliased)
    std::vector<float> sharedCh0 (numSamples, 0.5f);
    // Channel 1: separate input and output
    std::vector<float> inputCh1 (numSamples, 0.75f);
    std::vector<float> outputCh1 (numSamples, 0.0f);

    float* inputPtrs[2] = {sharedCh0.data (), inputCh1.data ()};
    float* outputPtrs[2] = {sharedCh0.data (), outputCh1.data ()};

    AudioBusBuffers inputBus{};
    inputBus.numChannels = 2;
    inputBus.channelBuffers32 = inputPtrs;

    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outputPtrs;

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputBus;
    data.outputs = &outputBus;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    // Ch0: aliased — data unchanged
    for (int s = 0; s < numSamples; ++s)
        EXPECT_FLOAT_EQ (sharedCh0[s], 0.5f) << "Ch0 corrupted at s=" << s;

    // Ch1: separate — output should match input
    for (int s = 0; s < numSamples; ++s)
        EXPECT_FLOAT_EQ (outputCh1[s], 0.75f) << "Ch1 mismatch at s=" << s;
}
