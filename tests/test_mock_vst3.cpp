#include <gtest/gtest.h>
#include "mocks/mock_vst3.h"

using namespace VST3MCPWrapper::Testing;
using namespace Steinberg;

TEST (MockVST3, MockComponentCanBeInstantiated)
{
    MockComponent mock;
    EXPECT_NE (nullptr, &mock);
}

TEST (MockVST3, MockAudioProcessorCanBeInstantiated)
{
    MockAudioProcessor mock;
    EXPECT_NE (nullptr, &mock);
}

TEST (MockVST3, MockEditControllerCanBeInstantiated)
{
    MockEditController mock;
    EXPECT_NE (nullptr, &mock);
}

TEST (MockVST3, MockMessageCanBeInstantiated)
{
    MockMessage mock;
    EXPECT_NE (nullptr, &mock);
}

TEST (MockVST3, MockAttributeListCanBeInstantiated)
{
    MockAttributeList mock;
    EXPECT_NE (nullptr, &mock);
}

TEST (MockVST3, MockComponentRefCounting)
{
    MockComponent mock;
    EXPECT_EQ (2u, mock.addRef ());
    EXPECT_EQ (1u, mock.release ());
}

TEST (MockVST3, MockComponentQueryInterface)
{
    MockComponent mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IComponent::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release (); // balance the addRef from queryInterface

    obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::FUnknown::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();

    // Unknown interface should fail
    Steinberg::TUID bogusIid = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    obj = nullptr;
    EXPECT_EQ (kNoInterface, mock.queryInterface (bogusIid, &obj));
    EXPECT_EQ (nullptr, obj);
}

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
}

TEST (MockVST3, MockMessageQueryInterface)
{
    MockMessage mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IMessage::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();
}

TEST (MockVST3, MockAttributeListQueryInterface)
{
    MockAttributeList mock;

    void* obj = nullptr;
    EXPECT_EQ (kResultOk, mock.queryInterface (Steinberg::Vst::IAttributeList::iid, &obj));
    EXPECT_NE (nullptr, obj);
    mock.release ();
}
