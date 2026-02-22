#include <gtest/gtest.h>

#include "wrapperview.h"
#include "controller.h"

using namespace Steinberg;
using namespace VST3MCPWrapper;

//------------------------------------------------------------------------
// WrapperPlugView Linux stub tests
//------------------------------------------------------------------------
class WrapperPlugViewLinuxTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        // Pass nullptr for controller — Linux stub doesn't use it for
        // the behaviors we're testing (platform support, size, etc.)
        view_ = new WrapperPlugView (nullptr);
    }

    void TearDown () override
    {
        view_->release ();
    }

    WrapperPlugView* view_ = nullptr;
};

//------------------------------------------------------------------------
// isPlatformTypeSupported returns kResultFalse for all types
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, IsPlatformTypeNotSupported)
{
    EXPECT_EQ (view_->isPlatformTypeSupported ("X11EmbedWindowID"), kResultFalse);
    EXPECT_EQ (view_->isPlatformTypeSupported ("HIView"), kResultFalse);
    EXPECT_EQ (view_->isPlatformTypeSupported ("NSView"), kResultFalse);
    EXPECT_EQ (view_->isPlatformTypeSupported ("HWND"), kResultFalse);
    EXPECT_EQ (view_->isPlatformTypeSupported (nullptr), kResultFalse);
}

//------------------------------------------------------------------------
// getSize returns default dimensions
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, GetSizeReturnsDefaults)
{
    ViewRect rect{};
    EXPECT_EQ (view_->getSize (&rect), kResultOk);
    EXPECT_EQ (rect.left, 0);
    EXPECT_EQ (rect.top, 0);
    EXPECT_EQ (rect.right, 400);
    EXPECT_EQ (rect.bottom, 300);
}

//------------------------------------------------------------------------
// getSize with null returns kResultFalse
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, GetSizeNullReturnsFalse)
{
    EXPECT_EQ (view_->getSize (nullptr), kResultFalse);
}

//------------------------------------------------------------------------
// checkSizeConstraint snaps to default size
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, CheckSizeConstraintSnapsToDefault)
{
    ViewRect rect{10, 20, 800, 600};
    EXPECT_EQ (view_->checkSizeConstraint (&rect), kResultOk);
    EXPECT_EQ (rect.left, 10);
    EXPECT_EQ (rect.top, 20);
    EXPECT_EQ (rect.right, 10 + 400);
    EXPECT_EQ (rect.bottom, 20 + 300);
}

//------------------------------------------------------------------------
// checkSizeConstraint with null returns kResultFalse
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, CheckSizeConstraintNullReturnsFalse)
{
    EXPECT_EQ (view_->checkSizeConstraint (nullptr), kResultFalse);
}

//------------------------------------------------------------------------
// canResize returns kResultFalse
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, CanResizeReturnsFalse)
{
    EXPECT_EQ (view_->canResize (), kResultFalse);
}

//------------------------------------------------------------------------
// attached returns kResultFalse (no platform support)
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, AttachedReturnsFalse)
{
    int dummy = 0;
    EXPECT_EQ (view_->attached (&dummy, "X11EmbedWindowID"), kResultFalse);
}

//------------------------------------------------------------------------
// removed returns kResultOk
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, RemovedReturnsOk)
{
    EXPECT_EQ (view_->removed (), kResultOk);
}

//------------------------------------------------------------------------
// queryInterface: IPlugView
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, QueryInterfaceIPlugView)
{
    void* obj = nullptr;
    EXPECT_EQ (view_->queryInterface (IPlugView::iid, &obj), kResultOk);
    EXPECT_NE (obj, nullptr);
    // Balance the addRef from queryInterface
    view_->release ();
}

//------------------------------------------------------------------------
// queryInterface: FUnknown
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, QueryInterfaceFUnknown)
{
    void* obj = nullptr;
    EXPECT_EQ (view_->queryInterface (FUnknown::iid, &obj), kResultOk);
    EXPECT_NE (obj, nullptr);
    view_->release ();
}

//------------------------------------------------------------------------
// queryInterface: IPlugFrame
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, QueryInterfaceIPlugFrame)
{
    void* obj = nullptr;
    EXPECT_EQ (view_->queryInterface (IPlugFrame::iid, &obj), kResultOk);
    EXPECT_NE (obj, nullptr);
    view_->release ();
}

//------------------------------------------------------------------------
// queryInterface: unsupported interface returns kNoInterface
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, QueryInterfaceUnsupported)
{
    static const TUID kBogusIid = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    void* obj = nullptr;
    EXPECT_EQ (view_->queryInterface (kBogusIid, &obj), kNoInterface);
    EXPECT_EQ (obj, nullptr);
}

//------------------------------------------------------------------------
// Reference counting: addRef / release
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, RefCounting)
{
    // Initial refCount is 1 (from constructor)
    EXPECT_EQ (view_->addRef (), 2u);
    EXPECT_EQ (view_->addRef (), 3u);
    EXPECT_EQ (view_->release (), 2u);
    EXPECT_EQ (view_->release (), 1u);
    // TearDown will call the final release()
}

//------------------------------------------------------------------------
// setFrame stores the frame (doesn't crash)
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, SetFrameAcceptsNull)
{
    EXPECT_EQ (view_->setFrame (nullptr), kResultOk);
}

//------------------------------------------------------------------------
// Input event methods return kResultFalse
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, InputEventsReturnFalse)
{
    EXPECT_EQ (view_->onWheel (1.0f), kResultFalse);
    EXPECT_EQ (view_->onKeyDown (0, 0, 0), kResultFalse);
    EXPECT_EQ (view_->onKeyUp (0, 0, 0), kResultFalse);
}

//------------------------------------------------------------------------
// resizeView returns kResultFalse
//------------------------------------------------------------------------
TEST_F (WrapperPlugViewLinuxTest, ResizeViewReturnsFalse)
{
    ViewRect rect{0, 0, 800, 600};
    EXPECT_EQ (view_->resizeView (nullptr, &rect), kResultFalse);
}
