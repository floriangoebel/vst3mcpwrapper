#include <gtest/gtest.h>

#include "controller.h"
#include "wrapperview.h"
#include "hostedplugin.h"
#include "helpers/controller_test_access.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3MCPWrapper;

//------------------------------------------------------------------------
// Test fixture — creates a Controller WITHOUT initialize() to avoid
// starting the MCP server.  createView() does not depend on initialize().
//------------------------------------------------------------------------
class ControllerCreateViewTest : public ::testing::Test {
protected:
    void SetUp () override
    {
        controller_ = new Controller ();
    }

    void TearDown () override
    {
        controller_->release ();
    }

    Controller* controller_ = nullptr;
};

//------------------------------------------------------------------------
// createView("editor") returns a non-null IPlugView (the drop zone view)
//------------------------------------------------------------------------
TEST_F (ControllerCreateViewTest, CreateViewEditorReturnsNonNull)
{
    IPlugView* view = controller_->createView (ViewType::kEditor);
    ASSERT_NE (view, nullptr);
    view->release ();
}

//------------------------------------------------------------------------
// createView with an unsupported view type returns nullptr
//------------------------------------------------------------------------
TEST_F (ControllerCreateViewTest, CreateViewUnsupportedTypeReturnsNull)
{
    EXPECT_EQ (controller_->createView ("some_unsupported_type"), nullptr);
}

//------------------------------------------------------------------------
// createView with nullptr name returns nullptr
//------------------------------------------------------------------------
TEST_F (ControllerCreateViewTest, CreateViewNullNameReturnsNull)
{
    EXPECT_EQ (controller_->createView (nullptr), nullptr);
}

//------------------------------------------------------------------------
// The returned view supports queryInterface for IPlugView
//------------------------------------------------------------------------
TEST_F (ControllerCreateViewTest, ReturnedViewSupportsIPlugView)
{
    IPlugView* view = controller_->createView (ViewType::kEditor);
    ASSERT_NE (view, nullptr);

    void* obj = nullptr;
    EXPECT_EQ (view->queryInterface (IPlugView::iid, &obj), kResultOk);
    EXPECT_NE (obj, nullptr);
    // Balance the addRef from queryInterface
    view->release ();

    view->release ();
}

//------------------------------------------------------------------------
// After createView, activeView_ is set to the returned view
//------------------------------------------------------------------------
TEST_F (ControllerCreateViewTest, ActiveViewIsSetAfterCreateView)
{
    IPlugView* view = controller_->createView (ViewType::kEditor);
    ASSERT_NE (view, nullptr);

    auto* activeView = ControllerTestAccess::activeView (*controller_);
    EXPECT_EQ (static_cast<IPlugView*> (activeView), view);

    view->release ();
}

//------------------------------------------------------------------------
// Creating a second view replaces the first in activeView_
//------------------------------------------------------------------------
TEST_F (ControllerCreateViewTest, SecondViewReplacesFirstInActiveView)
{
    IPlugView* view1 = controller_->createView (ViewType::kEditor);
    ASSERT_NE (view1, nullptr);

    IPlugView* view2 = controller_->createView (ViewType::kEditor);
    ASSERT_NE (view2, nullptr);

    auto* activeView = ControllerTestAccess::activeView (*controller_);
    EXPECT_EQ (static_cast<IPlugView*> (activeView), view2);
    EXPECT_NE (view1, view2);

    view1->release ();
    view2->release ();
}

//------------------------------------------------------------------------
// activeView_ is nullptr when no view has been created
//------------------------------------------------------------------------
TEST_F (ControllerCreateViewTest, ActiveViewIsNullInitially)
{
    auto* activeView = ControllerTestAccess::activeView (*controller_);
    EXPECT_EQ (activeView, nullptr);
}
