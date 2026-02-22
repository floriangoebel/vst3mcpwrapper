#include "wrapperview.h"
#include "controller.h"

using namespace Steinberg;

namespace VST3MCPWrapper {

WrapperPlugView::WrapperPlugView(Controller* controller)
    : controller_(controller) {}

WrapperPlugView::~WrapperPlugView() {
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

// --- FUnknown ---

tresult PLUGIN_API WrapperPlugView::queryInterface(const TUID iid, void** obj) {
    if (FUnknownPrivate::iidEqual(iid, IPlugView::iid) ||
        FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
        addRef();
        *obj = static_cast<IPlugView*>(this);
        return kResultOk;
    }
    if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid)) {
        addRef();
        *obj = static_cast<IPlugFrame*>(this);
        return kResultOk;
    }
    *obj = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API WrapperPlugView::addRef() {
    return ++refCount_;
}

uint32 PLUGIN_API WrapperPlugView::release() {
    if (--refCount_ == 0) {
        delete this;
        return 0;
    }
    return refCount_;
}

} // namespace VST3MCPWrapper
