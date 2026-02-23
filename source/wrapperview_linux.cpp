#include "wrapperview.h"
#include "controller.h"

using namespace Steinberg;

namespace VST3MCPWrapper {

WrapperPlugView::WrapperPlugView(Controller* controller)
    : controller_(controller) {}

WrapperPlugView::~WrapperPlugView() {
    // controller_ is cleared by Controller::terminate() if the controller is
    // destroyed first, so this null check guards against use-after-free.
    if (controller_) {
        controller_->activeView_ = nullptr;
    }
}

void WrapperPlugView::switchToHostedView(IPlugView* hostedView) {
    // No GUI on Linux — nothing to switch
}

void WrapperPlugView::switchToDropZone() {
    // No GUI on Linux — nothing to switch
}

void WrapperPlugView::removeDropZone() {}

void WrapperPlugView::removeHostedView() {
    if (hostedView_) {
        hostedView_->setFrame(nullptr);
        hostedView_->removed();
        hostedView_ = nullptr;
    }
}

// --- IPlugView ---

tresult PLUGIN_API WrapperPlugView::isPlatformTypeSupported(FIDString type) {
    // No platform view type supported on Linux
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::attached(void* parent, FIDString type) {
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::removed() {
    removeHostedView();
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::onWheel(float distance) {
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::onKeyDown(char16 key, int16 keyCode, int16 modifiers) {
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::onKeyUp(char16 key, int16 keyCode, int16 modifiers) {
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::getSize(ViewRect* size) {
    if (!size)
        return kResultFalse;
    size->left = 0;
    size->top = 0;
    size->right = kDefaultWidth;
    size->bottom = kDefaultHeight;
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::onSize(ViewRect* newSize) {
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::onFocus(TBool state) {
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::setFrame(IPlugFrame* frame) {
    hostFrame_ = frame;
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::canResize() {
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::checkSizeConstraint(ViewRect* rect) {
    if (!rect) return kResultFalse;
    rect->right = rect->left + kDefaultWidth;
    rect->bottom = rect->top + kDefaultHeight;
    return kResultOk;
}

// --- IPlugFrame ---

tresult PLUGIN_API WrapperPlugView::resizeView(IPlugView* view, ViewRect* newSize) {
    return kResultFalse;
}

} // namespace VST3MCPWrapper
