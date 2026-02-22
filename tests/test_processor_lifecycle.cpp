#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "processor.h"
#include "mocks/mock_vst3.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

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
    static bool wrapperActive (const Processor& p) { return p.wrapperActive_.load (std::memory_order_relaxed); }
    static bool wrapperProcessing (const Processor& p) { return p.wrapperProcessing_.load (std::memory_order_relaxed); }
    static bool hostedActive (const Processor& p) { return p.hostedActive_.load (); }
    static bool hostedProcessing (const Processor& p) { return p.hostedProcessing_.load (); }

    static void setHostedComponent (Processor& p, IComponent* comp)
    {
        p.hostedComponent_ = comp;
    }
    static void setHostedProcessor (Processor& p, IAudioProcessor* proc)
    {
        p.hostedProcessor_ = proc;
    }

    static const std::vector<SpeakerArrangement>& storedInputArr (const Processor& p)
    {
        return p.storedInputArr_;
    }
    static const std::vector<SpeakerArrangement>& storedOutputArr (const Processor& p)
    {
        return p.storedOutputArr_;
    }
    static const ProcessSetup& currentSetup (const Processor& p) { return p.currentSetup_; }

    static void callReplayDawState (Processor& p) { p.replayDawStateOntoHosted (); }
};

} // namespace VST3MCPWrapper

//------------------------------------------------------------------------
// Test fixture
//------------------------------------------------------------------------
class ProcessorLifecycleTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        processor_ = new Processor ();
        ASSERT_EQ (processor_->initialize (nullptr), kResultOk);
    }

    void TearDown () override
    {
        // Clear any injected mocks before terminate to avoid calling into destroyed objects
        ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
        ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
        processor_->terminate ();
        processor_->release ();
    }

    Processor* processor_ = nullptr;
};

//------------------------------------------------------------------------
// setActive tests
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetActiveTrueStoresState)
{
    EXPECT_FALSE (ProcessorTestAccess::wrapperActive (*processor_));

    processor_->setActive (true);

    EXPECT_TRUE (ProcessorTestAccess::wrapperActive (*processor_));
}

TEST_F (ProcessorLifecycleTest, SetActiveFalseStoresState)
{
    processor_->setActive (true);
    EXPECT_TRUE (ProcessorTestAccess::wrapperActive (*processor_));

    processor_->setActive (false);

    EXPECT_FALSE (ProcessorTestAccess::wrapperActive (*processor_));
}

//------------------------------------------------------------------------
// setProcessing tests — storage without hosted processor
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetProcessingTrueStoresState)
{
    EXPECT_FALSE (ProcessorTestAccess::wrapperProcessing (*processor_));

    processor_->setProcessing (true);

    EXPECT_TRUE (ProcessorTestAccess::wrapperProcessing (*processor_));
}

TEST_F (ProcessorLifecycleTest, SetProcessingFalseStoresState)
{
    processor_->setProcessing (true);
    EXPECT_TRUE (ProcessorTestAccess::wrapperProcessing (*processor_));

    processor_->setProcessing (false);

    EXPECT_FALSE (ProcessorTestAccess::wrapperProcessing (*processor_));
}

//------------------------------------------------------------------------
// setProcessing tests — forwarding to hosted processor
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetProcessingTrueForwardsToHostedProcessor)
{
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    EXPECT_CALL (mockProc, setProcessing (true)).WillOnce (::testing::Return (kResultOk));

    processor_->setProcessing (true);

    EXPECT_TRUE (ProcessorTestAccess::wrapperProcessing (*processor_));
    EXPECT_TRUE (ProcessorTestAccess::hostedProcessing (*processor_));

    // Clear mock before it goes out of scope
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

TEST_F (ProcessorLifecycleTest, SetProcessingFalseForwardsToHostedProcessor)
{
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    {
        ::testing::InSequence seq;
        EXPECT_CALL (mockProc, setProcessing (true)).WillOnce (::testing::Return (kResultOk));
        EXPECT_CALL (mockProc, setProcessing (false)).WillOnce (::testing::Return (kResultOk));
    }

    processor_->setProcessing (true);
    processor_->setProcessing (false);

    EXPECT_FALSE (ProcessorTestAccess::wrapperProcessing (*processor_));
    EXPECT_FALSE (ProcessorTestAccess::hostedProcessing (*processor_));

    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// setBusArrangements stores arrangements for later replay
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetBusArrangementsStoresArrangements)
{
    EXPECT_TRUE (ProcessorTestAccess::storedInputArr (*processor_).empty ());
    EXPECT_TRUE (ProcessorTestAccess::storedOutputArr (*processor_).empty ());

    SpeakerArrangement inputs[] = {SpeakerArr::kStereo};
    SpeakerArrangement outputs[] = {SpeakerArr::kStereo, SpeakerArr::kMono};

    processor_->setBusArrangements (inputs, 1, outputs, 2);

    auto& storedIn = ProcessorTestAccess::storedInputArr (*processor_);
    auto& storedOut = ProcessorTestAccess::storedOutputArr (*processor_);

    ASSERT_EQ (storedIn.size (), 1u);
    EXPECT_EQ (storedIn[0], SpeakerArr::kStereo);

    ASSERT_EQ (storedOut.size (), 2u);
    EXPECT_EQ (storedOut[0], SpeakerArr::kStereo);
    EXPECT_EQ (storedOut[1], SpeakerArr::kMono);
}

//------------------------------------------------------------------------
// setupProcessing stores the ProcessSetup for later replay
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetupProcessingStoresSetup)
{
    ProcessSetup setup{};
    setup.sampleRate = 48000.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;

    processor_->setupProcessing (setup);

    auto& stored = ProcessorTestAccess::currentSetup (*processor_);
    EXPECT_DOUBLE_EQ (stored.sampleRate, 48000.0);
    EXPECT_EQ (stored.maxSamplesPerBlock, 512);
    EXPECT_EQ (stored.symbolicSampleSize, kSample32);
    EXPECT_EQ (stored.processMode, kRealtime);
}

//------------------------------------------------------------------------
// State replay: wrapperActive replays setActive(true) onto hosted component
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, ReplayActivatesHostedWhenWrapperActive)
{
    // Simulate DAW having called setActive(true) before plugin was loaded
    processor_->setActive (true);
    EXPECT_TRUE (ProcessorTestAccess::wrapperActive (*processor_));
    EXPECT_FALSE (ProcessorTestAccess::hostedActive (*processor_));

    // Inject mock component (simulating a successful plugin load)
    MockComponent mockComp;
    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);

    EXPECT_CALL (mockComp, setActive (true)).WillOnce (::testing::Return (kResultOk));

    // Trigger replay
    ProcessorTestAccess::callReplayDawState (*processor_);

    EXPECT_TRUE (ProcessorTestAccess::hostedActive (*processor_));

    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
}

//------------------------------------------------------------------------
// State replay: wrapperProcessing replays setProcessing(true) onto hosted
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, ReplayStartsProcessingWhenWrapperProcessing)
{
    // Simulate DAW having called setProcessing(true) before plugin was loaded
    processor_->setProcessing (true);
    EXPECT_TRUE (ProcessorTestAccess::wrapperProcessing (*processor_));
    EXPECT_FALSE (ProcessorTestAccess::hostedProcessing (*processor_));

    // Inject mock processor (simulating a successful plugin load)
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    EXPECT_CALL (mockProc, setProcessing (true)).WillOnce (::testing::Return (kResultOk));

    ProcessorTestAccess::callReplayDawState (*processor_);

    EXPECT_TRUE (ProcessorTestAccess::hostedProcessing (*processor_));

    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// State replay: both active and processing replayed in correct order
// (setActive before setProcessing)
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, ReplayActivatesBeforeStartsProcessing)
{
    // Set both wrapper flags
    processor_->setActive (true);
    processor_->setProcessing (true);

    // Inject both mocks
    MockComponent mockComp;
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    {
        ::testing::InSequence seq;
        EXPECT_CALL (mockComp, setActive (true)).WillOnce (::testing::Return (kResultOk));
        EXPECT_CALL (mockProc, setProcessing (true)).WillOnce (::testing::Return (kResultOk));
    }

    ProcessorTestAccess::callReplayDawState (*processor_);

    EXPECT_TRUE (ProcessorTestAccess::hostedActive (*processor_));
    EXPECT_TRUE (ProcessorTestAccess::hostedProcessing (*processor_));

    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// State replay: wrapperActive=false does NOT activate hosted component
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, ReplaySkipsActivationWhenWrapperNotActive)
{
    // wrapperActive_ is false (default), wrapperProcessing_ is false
    EXPECT_FALSE (ProcessorTestAccess::wrapperActive (*processor_));

    MockComponent mockComp;
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedComponent (*processor_, &mockComp);
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    // Neither setActive nor setProcessing should be called
    EXPECT_CALL (mockComp, setActive (::testing::_)).Times (0);
    EXPECT_CALL (mockProc, setProcessing (::testing::_)).Times (0);

    ProcessorTestAccess::callReplayDawState (*processor_);

    EXPECT_FALSE (ProcessorTestAccess::hostedActive (*processor_));
    EXPECT_FALSE (ProcessorTestAccess::hostedProcessing (*processor_));

    ProcessorTestAccess::setHostedComponent (*processor_, nullptr);
    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// setBusArrangements forwards to hosted processor when present
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetBusArrangementsForwardsToHostedProcessor)
{
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    SpeakerArrangement inputs[] = {SpeakerArr::kStereo};
    SpeakerArrangement outputs[] = {SpeakerArr::kStereo};

    EXPECT_CALL (mockProc, setBusArrangements (::testing::_, 1, ::testing::_, 1))
        .WillOnce (::testing::Return (kResultOk));

    processor_->setBusArrangements (inputs, 1, outputs, 1);

    // Also stored for replay
    EXPECT_EQ (ProcessorTestAccess::storedInputArr (*processor_).size (), 1u);
    EXPECT_EQ (ProcessorTestAccess::storedOutputArr (*processor_).size (), 1u);

    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// setBusArrangements rejects null inputs with positive count
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetBusArrangementsRejectsNullInputs)
{
    SpeakerArrangement outputs[] = {SpeakerArr::kStereo};
    EXPECT_EQ (processor_->setBusArrangements (nullptr, 1, outputs, 1), kInvalidArgument);
}

//------------------------------------------------------------------------
// setBusArrangements rejects null outputs with positive count
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetBusArrangementsRejectsNullOutputs)
{
    SpeakerArrangement inputs[] = {SpeakerArr::kStereo};
    EXPECT_EQ (processor_->setBusArrangements (inputs, 1, nullptr, 1), kInvalidArgument);
}

//------------------------------------------------------------------------
// setBusArrangements accepts null pointers with zero counts (reset case)
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, SetBusArrangementsAcceptsNullWithZeroCounts)
{
    EXPECT_NE (processor_->setBusArrangements (nullptr, 0, nullptr, 0), kInvalidArgument);
}

//------------------------------------------------------------------------
// canProcessSampleSize — defaults when no hosted plugin
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, CanProcessSampleSize32WithoutHostedPlugin)
{
    EXPECT_EQ (processor_->canProcessSampleSize (kSample32), kResultTrue);
}

TEST_F (ProcessorLifecycleTest, CanProcessSampleSize64WithoutHostedPlugin)
{
    EXPECT_EQ (processor_->canProcessSampleSize (kSample64), kResultFalse);
}

//------------------------------------------------------------------------
// canProcessSampleSize — forwards to hosted processor
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, CanProcessSampleSizeForwardsToHostedProcessor)
{
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    EXPECT_CALL (mockProc, canProcessSampleSize (kSample32))
        .WillOnce (::testing::Return (kResultTrue));
    EXPECT_CALL (mockProc, canProcessSampleSize (kSample64))
        .WillOnce (::testing::Return (kResultTrue));

    EXPECT_EQ (processor_->canProcessSampleSize (kSample32), kResultTrue);
    EXPECT_EQ (processor_->canProcessSampleSize (kSample64), kResultTrue);

    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// getLatencySamples — default when no hosted plugin
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, GetLatencySamplesReturnsZeroWithoutHostedPlugin)
{
    EXPECT_EQ (processor_->getLatencySamples (), 0u);
}

//------------------------------------------------------------------------
// getLatencySamples — forwards to hosted processor
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, GetLatencySamplesForwardsToHostedProcessor)
{
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    EXPECT_CALL (mockProc, getLatencySamples ()).WillOnce (::testing::Return (256));

    EXPECT_EQ (processor_->getLatencySamples (), 256u);

    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}

//------------------------------------------------------------------------
// getTailSamples — default when no hosted plugin
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, GetTailSamplesReturnsZeroWithoutHostedPlugin)
{
    EXPECT_EQ (processor_->getTailSamples (), 0u);
}

//------------------------------------------------------------------------
// getTailSamples — forwards to hosted processor
//------------------------------------------------------------------------
TEST_F (ProcessorLifecycleTest, GetTailSamplesForwardsToHostedProcessor)
{
    MockAudioProcessor mockProc;
    ProcessorTestAccess::setHostedProcessor (*processor_, &mockProc);

    EXPECT_CALL (mockProc, getTailSamples ()).WillOnce (::testing::Return (1024));

    EXPECT_EQ (processor_->getTailSamples (), 1024u);

    ProcessorTestAccess::setHostedProcessor (*processor_, nullptr);
}
