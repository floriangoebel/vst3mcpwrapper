#include <gtest/gtest.h>
#include "mocks/mock_vst3.h"

using namespace VST3MCPWrapper::Testing;
using namespace Steinberg;

// Bogus IID that no mock supports
static const Steinberg::TUID kBogusIid = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

// --- Ref counting ---

TEST (MockVST3, RefCountStartsAtOne)
{
    MockComponent comp;
    EXPECT_EQ (2u, comp.addRef ());    // 1 -> 2
    EXPECT_EQ (1u, comp.release ());   // 2 -> 1

    MockAudioProcessor proc;
    EXPECT_EQ (2u, proc.addRef ());
    EXPECT_EQ (1u, proc.release ());

    MockEditController ctrl;
    EXPECT_EQ (2u, ctrl.addRef ());
    EXPECT_EQ (1u, ctrl.release ());

    MockMessage msg;
    EXPECT_EQ (2u, msg.addRef ());
    EXPECT_EQ (1u, msg.release ());

    MockAttributeList attrs;
    EXPECT_EQ (2u, attrs.addRef ());
    EXPECT_EQ (1u, attrs.release ());
}

// --- MockComponent queryInterface ---

TEST (MockVST3, MockComponentQueryInterface)
{
    MockComponent mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IComponent::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release (); // balance the addRef from queryInterface

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::IPluginBase::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::FUnknown::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kNoInterface, mock.queryInterface (kBogusIid, &obj));
    EXPECT_EQ (nullptr, obj);
}

// --- MockAudioProcessor queryInterface ---

TEST (MockVST3, MockAudioProcessorQueryInterface)
{
    MockAudioProcessor mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IAudioProcessor::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::FUnknown::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kNoInterface, mock.queryInterface (kBogusIid, &obj));
    EXPECT_EQ (nullptr, obj);
}

// --- MockEditController queryInterface ---

TEST (MockVST3, MockEditControllerQueryInterface)
{
    MockEditController mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IEditController::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::IPluginBase::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::FUnknown::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kNoInterface, mock.queryInterface (kBogusIid, &obj));
    EXPECT_EQ (nullptr, obj);
}

// --- MockMessage queryInterface ---

TEST (MockVST3, MockMessageQueryInterface)
{
    MockMessage mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IMessage::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::FUnknown::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kNoInterface, mock.queryInterface (kBogusIid, &obj));
    EXPECT_EQ (nullptr, obj);
}

// --- MockAttributeList queryInterface ---

TEST (MockVST3, MockAttributeListQueryInterface)
{
    MockAttributeList mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IAttributeList::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::FUnknown::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    obj = nullptr;
    EXPECT_EQ (kNoInterface, mock.queryInterface (kBogusIid, &obj));
    EXPECT_EQ (nullptr, obj);
}
