#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "processor.h"
#include "hostedplugin.h"
#include "mocks/mock_vst3.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <cstring>
#include <vector>

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
    static bool wrapperActive (const Processor& p) { return p.wrapperActive_; }
    static bool wrapperProcessing (const Processor& p) { return p.wrapperProcessing_; }
    static bool hostedActive (const Processor& p) { return p.hostedActive_.load (); }
    static bool hostedProcessing (const Processor& p) { return p.hostedProcessing_.load (); }
    static bool processorReady (const Processor& p) { return p.processorReady_.load (); }

    static void setHostedComponent (Processor& p, IComponent* comp)
    {
        p.hostedComponent_ = comp;
    }
    static void setHostedProcessor (Processor& p, IAudioProcessor* proc)
    {
        p.hostedProcessor_ = proc;
    }
    static void setProcessorReady (Processor& p, bool ready)
    {
        p.processorReady_ = ready;
    }
    static void setHostedActive (Processor& p, bool active)
    {
        p.hostedActive_ = active;
    }
};

} // namespace VST3MCPWrapper

//------------------------------------------------------------------------
// Helper: manage audio buffers for ProcessData
//------------------------------------------------------------------------
struct TestAudioBuffers {
    std::vector<std::vector<float>> float32;   // [channel][sample]
    std::vector<std::vector<double>> float64;  // [channel][sample]
    std::vector<float*> ptrs32;
    std::vector<double*> ptrs64;
    AudioBusBuffers bus{};

    TestAudioBuffers (int numChannels, int numSamples, bool is64bit)
    {
        bus.numChannels = numChannels;
        bus.silenceFlags = 0;

        if (is64bit) {
            float64.resize (numChannels, std::vector<double> (numSamples, 0.0));
            ptrs64.resize (numChannels);
            for (int ch = 0; ch < numChannels; ++ch)
                ptrs64[ch] = float64[ch].data ();
            bus.channelBuffers64 = ptrs64.data ();
        }
        else {
            float32.resize (numChannels, std::vector<float> (numSamples, 0.0f));
            ptrs32.resize (numChannels);
            for (int ch = 0; ch < numChannels; ++ch)
                ptrs32[ch] = float32[ch].data ();
            bus.channelBuffers32 = ptrs32.data ();
        }
    }
};

//------------------------------------------------------------------------
// Test fixture
//------------------------------------------------------------------------
class ProcessorProcessTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        processor_ = new Processor ();
        ASSERT_EQ (processor_->initialize (nullptr), kResultOk);

        // Drain any leftover param changes from other tests sharing the singleton
        auto& pluginModule = HostedPluginModule::instance ();
        std::vector<ParamChange> junk;
        pluginModule.drainParamChanges (junk);
    }

    void TearDown () override
    {
        // Clear any injected mocks before terminate
        ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
        ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
        processor_->terminate ();
        processor_->release ();

        // Drain any param changes left by the test
        auto& pluginModule = HostedPluginModule::instance ();
        std::vector<ParamChange> junk;
        pluginModule.drainParamChanges (junk);
    }

    Processor* processor_ = nullptr;
};

//------------------------------------------------------------------------
// Passthrough: 32-bit float input copied to output unchanged
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, Passthrough32BitCopiesInputToOutput)
{
    const int numSamples = 256;
    const int numChannels = 2;

    TestAudioBuffers input (numChannels, numSamples, false);
    TestAudioBuffers output (numChannels, numSamples, false);

    // Fill input with known values
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            input.float32[ch][s] = static_cast<float> (ch * 1000 + s) / 10000.0f;

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    // Verify output matches input
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            EXPECT_FLOAT_EQ (output.float32[ch][s], input.float32[ch][s])
                << "Mismatch at ch=" << ch << " s=" << s;
}

//------------------------------------------------------------------------
// Passthrough: 64-bit double input copied to output unchanged
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, Passthrough64BitCopiesInputToOutput)
{
    const int numSamples = 128;
    const int numChannels = 2;

    TestAudioBuffers input (numChannels, numSamples, true);
    TestAudioBuffers output (numChannels, numSamples, true);

    // Fill input with known values
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            input.float64[ch][s] = static_cast<double> (ch * 1000 + s) / 10000.0;

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample64;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    // Verify output matches input
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            EXPECT_DOUBLE_EQ (output.float64[ch][s], input.float64[ch][s])
                << "Mismatch at ch=" << ch << " s=" << s;
}

//------------------------------------------------------------------------
// Channel count mismatch: extra output channels are zeroed
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, ExtraOutputChannelsAreZeroed)
{
    const int numSamples = 64;
    const int inputChannels = 1;   // mono input
    const int outputChannels = 2;  // stereo output

    TestAudioBuffers input (inputChannels, numSamples, false);
    TestAudioBuffers output (outputChannels, numSamples, false);

    // Fill input with non-zero values
    for (int s = 0; s < numSamples; ++s)
        input.float32[0][s] = 0.5f;

    // Fill output with non-zero sentinel to verify zeroing
    for (int ch = 0; ch < outputChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            output.float32[ch][s] = 999.0f;

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    // First channel: should be copied from input
    for (int s = 0; s < numSamples; ++s)
        EXPECT_FLOAT_EQ (output.float32[0][s], 0.5f) << "Mismatch at s=" << s;

    // Second channel: should be zeroed (no corresponding input channel)
    for (int s = 0; s < numSamples; ++s)
        EXPECT_FLOAT_EQ (output.float32[1][s], 0.0f) << "Not zeroed at s=" << s;
}

//------------------------------------------------------------------------
// Channel mismatch with 64-bit: extra output channels are zeroed
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, ExtraOutputChannelsAreZeroed64Bit)
{
    const int numSamples = 64;
    const int inputChannels = 1;
    const int outputChannels = 2;

    TestAudioBuffers input (inputChannels, numSamples, true);
    TestAudioBuffers output (outputChannels, numSamples, true);

    for (int s = 0; s < numSamples; ++s)
        input.float64[0][s] = 0.75;

    for (int ch = 0; ch < outputChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            output.float64[ch][s] = 999.0;

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample64;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    for (int s = 0; s < numSamples; ++s)
        EXPECT_DOUBLE_EQ (output.float64[0][s], 0.75) << "Mismatch at s=" << s;

    for (int s = 0; s < numSamples; ++s)
        EXPECT_DOUBLE_EQ (output.float64[1][s], 0.0) << "Not zeroed at s=" << s;
}

//------------------------------------------------------------------------
// Empty input (numSamples = 0): no crash, output unchanged
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, EmptyInputNoCrash)
{
    const int numChannels = 2;

    TestAudioBuffers input (numChannels, 0, false);
    TestAudioBuffers output (numChannels, 0, false);

    ProcessData data{};
    data.numSamples = 0;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;

    // Should not crash
    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);
}

//------------------------------------------------------------------------
// No inputs or outputs: no crash
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, NoInputsNoOutputsNoCrash)
{
    ProcessData data{};
    data.numSamples = 128;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 0;
    data.numOutputs = 0;
    data.inputs = nullptr;
    data.outputs = nullptr;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);
}

//------------------------------------------------------------------------
// Parameter changes from the queue are injected into ProcessData
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, ParamChangesInjectedIntoProcessData)
{
    const int numSamples = 64;
    const int numChannels = 2;

    TestAudioBuffers input (numChannels, numSamples, false);
    TestAudioBuffers output (numChannels, numSamples, false);

    // Push parameter changes to the singleton queue
    auto& pluginModule = HostedPluginModule::instance ();
    pluginModule.pushParamChange (42, 0.75);
    pluginModule.pushParamChange (99, 0.25);

    // Set up mock hosted processor
    MockAudioProcessor mockProc;
    MockComponent mockComp;

    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);
    ProcessorTestAccess::setProcessorReady (*processor_, true);
    ProcessorTestAccess::setHostedActive (*processor_, true);

    // Verify parameter changes inside the mock callback, because the
    // ParameterChanges object is a local in process() and destroyed after return
    bool verified = false;
    EXPECT_CALL (mockProc, process (::testing::_))
        .WillOnce ([&verified] (ProcessData& d) -> tresult {
            auto* changes = d.inputParameterChanges;
            EXPECT_NE (changes, nullptr);
            if (!changes)
                return kResultOk;

            EXPECT_EQ (changes->getParameterCount (), 2);

            // Check first parameter change (id=42, value=0.75)
            auto* queue0 = changes->getParameterData (0);
            EXPECT_NE (queue0, nullptr);
            if (queue0) {
                EXPECT_EQ (queue0->getParameterId (), 42u);
                EXPECT_EQ (queue0->getPointCount (), 1);
                int32 sampleOffset;
                ParamValue value;
                EXPECT_EQ (queue0->getPoint (0, sampleOffset, value), kResultOk);
                EXPECT_EQ (sampleOffset, 0);
                EXPECT_DOUBLE_EQ (value, 0.75);
            }

            // Check second parameter change (id=99, value=0.25)
            auto* queue1 = changes->getParameterData (1);
            EXPECT_NE (queue1, nullptr);
            if (queue1) {
                EXPECT_EQ (queue1->getParameterId (), 99u);
                EXPECT_EQ (queue1->getPointCount (), 1);
                int32 sampleOffset;
                ParamValue value;
                EXPECT_EQ (queue1->getPoint (0, sampleOffset, value), kResultOk);
                EXPECT_EQ (sampleOffset, 0);
                EXPECT_DOUBLE_EQ (value, 0.25);
            }

            verified = true;
            return kResultOk;
        });

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;
    data.inputParameterChanges = nullptr;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);
    EXPECT_TRUE (verified) << "Mock process() was not called with parameter changes";

    // Clean up mocks before they go out of scope
    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
    ProcessorTestAccess::setProcessorReady (*processor_, false);
}

//------------------------------------------------------------------------
// Hosted processor without pending changes: forwards directly
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, HostedProcessorForwardsDirectlyWithNoQueuedChanges)
{
    const int numSamples = 64;
    const int numChannels = 2;

    TestAudioBuffers input (numChannels, numSamples, false);
    TestAudioBuffers output (numChannels, numSamples, false);

    MockAudioProcessor mockProc;
    MockComponent mockComp;

    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);
    ProcessorTestAccess::setProcessorReady (*processor_, true);
    ProcessorTestAccess::setHostedActive (*processor_, true);

    // No param changes queued — process should forward directly
    EXPECT_CALL (mockProc, process (::testing::_))
        .WillOnce (::testing::Return (kResultOk));

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;
    data.inputParameterChanges = nullptr;

    auto result = processor_->process (data);
    EXPECT_EQ (result, kResultOk);

    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
    ProcessorTestAccess::setProcessorReady (*processor_, false);
}

//------------------------------------------------------------------------
// Original inputParameterChanges restored after hosted process call
//------------------------------------------------------------------------
TEST_F (ProcessorProcessTest, OriginalInputParamChangesRestoredAfterProcess)
{
    const int numSamples = 64;
    const int numChannels = 2;

    TestAudioBuffers input (numChannels, numSamples, false);
    TestAudioBuffers output (numChannels, numSamples, false);

    auto& pluginModule = HostedPluginModule::instance ();
    pluginModule.pushParamChange (1, 0.5);

    MockAudioProcessor mockProc;
    MockComponent mockComp;

    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);
    ProcessorTestAccess::setProcessorReady (*processor_, true);
    ProcessorTestAccess::setHostedActive (*processor_, true);

    EXPECT_CALL (mockProc, process (::testing::_))
        .WillOnce (::testing::Return (kResultOk));

    // Use a real ParameterChanges as the original
    ParameterChanges originalChanges;
    int32 idx;
    auto* q = originalChanges.addParameterData (500, idx);
    int32 pIdx;
    q->addPoint (0, 0.33, pIdx);

    ProcessData data{};
    data.numSamples = numSamples;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input.bus;
    data.outputs = &output.bus;
    data.inputParameterChanges = &originalChanges;

    processor_->process (data);

    // After process, the original inputParameterChanges must be restored
    EXPECT_EQ (data.inputParameterChanges, &originalChanges);

    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
    ProcessorTestAccess::setProcessorReady (*processor_, false);
}
