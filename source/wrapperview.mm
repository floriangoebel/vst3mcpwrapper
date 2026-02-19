#include "wrapperview.h"
#include "controller.h"

#import <Cocoa/Cocoa.h>

using namespace Steinberg;

// ---- DropZoneView (Objective-C) ----

@interface DropZoneView : NSView <NSDraggingDestination>
@property (nonatomic, assign) VST3MCPWrapper::Controller* controller;
@property (nonatomic, assign) BOOL isDragHighlighted;
@end

@implementation DropZoneView

- (instancetype)initWithFrame:(NSRect)frameRect controller:(VST3MCPWrapper::Controller*)ctrl {
    self = [super initWithFrame:frameRect];
    if (self) {
        _controller = ctrl;
        _isDragHighlighted = NO;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }
    return self;
}

- (void)drawRect:(NSRect)dirtyRect {
    // Dark background
    [[NSColor colorWithCalibratedRed:0.15 green:0.15 blue:0.17 alpha:1.0] setFill];
    NSRectFill(dirtyRect);

    NSRect bounds = self.bounds;

    // Dashed border
    NSBezierPath* borderPath = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 20, 20)
                                                               xRadius:12 yRadius:12];
    if (_isDragHighlighted) {
        [[NSColor colorWithCalibratedRed:0.3 green:0.5 blue:0.9 alpha:1.0] setStroke];
    } else {
        [[NSColor colorWithCalibratedRed:0.4 green:0.4 blue:0.45 alpha:1.0] setStroke];
    }
    CGFloat dashPattern[] = {8.0, 4.0};
    [borderPath setLineDash:dashPattern count:2 phase:0];
    [borderPath setLineWidth:2.0];
    [borderPath stroke];

    // Text
    NSString* text = @"Drop a .vst3 plugin here";
    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:16 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:0.7 green:0.7 blue:0.75 alpha:1.0]
    };
    NSSize textSize = [text sizeWithAttributes:attrs];
    NSPoint textPoint = NSMakePoint(
        (bounds.size.width - textSize.width) / 2,
        (bounds.size.height - textSize.height) / 2
    );
    [text drawAtPoint:textPoint withAttributes:attrs];
}

// --- NSDraggingDestination ---

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSPasteboard* pb = [sender draggingPasteboard];
    NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[[NSURL class]]
                                              options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    for (NSURL* url in urls) {
        if ([url.pathExtension isEqualToString:@"vst3"]) {
            _isDragHighlighted = YES;
            [self setNeedsDisplay:YES];
            return NSDragOperationCopy;
        }
    }
    return NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    if (_isDragHighlighted) {
        return NSDragOperationCopy;
    }
    return NSDragOperationNone;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    _isDragHighlighted = NO;
    [self setNeedsDisplay:YES];
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    return YES;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard* pb = [sender draggingPasteboard];
    NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[[NSURL class]]
                                              options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    for (NSURL* url in urls) {
        if ([url.pathExtension isEqualToString:@"vst3"]) {
            std::string path = [url.path UTF8String];
            if (_controller) {
                _controller->loadPlugin(path);
            }
            return YES;
        }
    }
    return NO;
}

- (void)concludeDragOperation:(id<NSDraggingInfo>)sender {
    _isDragHighlighted = NO;
    [self setNeedsDisplay:YES];
}

@end

// ---- WrapperPlugView (C++) ----

namespace VST3MCPWrapper {

WrapperPlugView::WrapperPlugView(Controller* controller)
    : controller_(controller) {}

WrapperPlugView::~WrapperPlugView() {
    removeHostedView();
    removeDropZone();
    // Clear the controller's reference to us
    if (controller_) {
        controller_->activeView_ = nullptr;
    }
}

// --- In-place view switching ---

void WrapperPlugView::switchToHostedView(IPlugView* hostedView) {
    if (!hostedView || !parentNSView_)
        return;

    // Remove the drop zone
    removeDropZone();

    // Remove any previous hosted view
    removeHostedView();

    hostedView_ = owned(hostedView);

    // Set ourselves as the hosted view's frame so we intercept resize requests
    hostedView_->setFrame(this);

    // Attach the hosted view to the same parent
    hostedView_->attached(parentNSView_, kPlatformTypeNSView);

    // Ask the DAW to resize to the hosted plugin's preferred size
    if (hostFrame_) {
        ViewRect hostedSize;
        if (hostedView_->getSize(&hostedSize) == kResultOk) {
            hostFrame_->resizeView(this, &hostedSize);
        }
    }
}

void WrapperPlugView::switchToDropZone() {
    if (!parentNSView_)
        return;

    removeHostedView();

    // Recreate the drop zone
    NSView* parentView = (__bridge NSView*)parentNSView_;
    NSRect frame = NSMakeRect(0, 0, kDefaultWidth, kDefaultHeight);
    DropZoneView* dropZone = [[DropZoneView alloc] initWithFrame:frame controller:controller_];
    dropZone.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [parentView addSubview:dropZone];
    dropZoneView_ = (__bridge_retained void*)dropZone;

    // Resize back to default
    if (hostFrame_) {
        ViewRect defaultSize = {0, 0, kDefaultWidth, kDefaultHeight};
        hostFrame_->resizeView(this, &defaultSize);
    }
}

void WrapperPlugView::removeDropZone() {
    if (dropZoneView_) {
        auto* view = (__bridge_transfer NSView*)dropZoneView_;
        [view removeFromSuperview];
        dropZoneView_ = nullptr;
    }
}

void WrapperPlugView::removeHostedView() {
    if (hostedView_) {
        hostedView_->setFrame(nullptr);
        hostedView_->removed();
        hostedView_ = nullptr;
    }
}

// --- IPlugView ---

tresult PLUGIN_API WrapperPlugView::isPlatformTypeSupported(FIDString type) {
    if (strcmp(type, kPlatformTypeNSView) == 0)
        return kResultTrue;
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::attached(void* parent, FIDString type) {
    if (strcmp(type, kPlatformTypeNSView) != 0)
        return kResultFalse;

    parentNSView_ = parent;

    // If a hosted plugin is already loaded (e.g., session restore), show its view
    auto ctrl = controller_->getHostedController();
    if (ctrl) {
        auto* hostedPlugView = ctrl->createView("editor");
        if (hostedPlugView) {
            hostedView_ = owned(hostedPlugView);
            hostedView_->setFrame(this);
            hostedView_->attached(parent, type);
            return kResultOk;
        }
    }

    // No hosted plugin — show drop zone
    NSView* parentView = (__bridge NSView*)parent;
    NSRect frame = NSMakeRect(0, 0, kDefaultWidth, kDefaultHeight);
    DropZoneView* dropZone = [[DropZoneView alloc] initWithFrame:frame controller:controller_];
    dropZone.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [parentView addSubview:dropZone];
    dropZoneView_ = (__bridge_retained void*)dropZone;

    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::removed() {
    removeHostedView();
    removeDropZone();
    parentNSView_ = nullptr;
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::onWheel(float distance) {
    if (hostedView_) return hostedView_->onWheel(distance);
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::onKeyDown(char16 key, int16 keyCode, int16 modifiers) {
    if (hostedView_) return hostedView_->onKeyDown(key, keyCode, modifiers);
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::onKeyUp(char16 key, int16 keyCode, int16 modifiers) {
    if (hostedView_) return hostedView_->onKeyUp(key, keyCode, modifiers);
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::getSize(ViewRect* size) {
    if (!size)
        return kResultFalse;
    if (hostedView_)
        return hostedView_->getSize(size);
    size->left = 0;
    size->top = 0;
    size->right = kDefaultWidth;
    size->bottom = kDefaultHeight;
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::onSize(ViewRect* newSize) {
    if (hostedView_)
        return hostedView_->onSize(newSize);
    if (dropZoneView_ && newSize) {
        auto* view = (__bridge NSView*)dropZoneView_;
        NSRect frame = NSMakeRect(0, 0, newSize->right - newSize->left, newSize->bottom - newSize->top);
        [view setFrame:frame];
    }
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::onFocus(TBool state) {
    if (hostedView_) return hostedView_->onFocus(state);
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::setFrame(IPlugFrame* frame) {
    hostFrame_ = frame;
    return kResultOk;
}

tresult PLUGIN_API WrapperPlugView::canResize() {
    if (hostedView_) return hostedView_->canResize();
    return kResultFalse;
}

tresult PLUGIN_API WrapperPlugView::checkSizeConstraint(ViewRect* rect) {
    if (hostedView_) return hostedView_->checkSizeConstraint(rect);
    if (!rect) return kResultFalse;
    rect->right = rect->left + kDefaultWidth;
    rect->bottom = rect->top + kDefaultHeight;
    return kResultOk;
}

// --- IPlugFrame ---

tresult PLUGIN_API WrapperPlugView::resizeView(IPlugView* view, ViewRect* newSize) {
    // The hosted plugin wants to resize. Forward to the DAW's frame with ourselves as the view.
    if (hostFrame_ && newSize) {
        return hostFrame_->resizeView(this, newSize);
    }
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
