#pragma once

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/smartpointer.h"

#include <atomic>

namespace VST3MCPWrapper {

class Controller;

// Custom IPlugView that wraps either a drop zone (no plugin loaded) or
// the hosted plugin's view. Supports in-place switching when a plugin
// is loaded via drag-and-drop or MCP.
//
// Also implements IPlugFrame to intercept the hosted plugin's resize
// requests and forward them to the DAW's frame with our view as the target.
class WrapperPlugView : public Steinberg::IPlugView,
                        public Steinberg::IPlugFrame {
public:
    explicit WrapperPlugView(Controller* controller);
    ~WrapperPlugView();

    // Switch from drop zone to the hosted plugin's view (in-place)
    void switchToHostedView(Steinberg::IPlugView* hostedView);
    // Switch back to drop zone (when plugin is unloaded)
    void switchToDropZone();

    // IPlugView
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onWheel(float distance) override;
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode,
                                             Steinberg::int16 modifiers) override;
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key, Steinberg::int16 keyCode,
                                           Steinberg::int16 modifiers) override;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override;
    Steinberg::tresult PLUGIN_API canResize() override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

    // IPlugFrame — intercepts hosted plugin's resize requests
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
                                              Steinberg::ViewRect* newSize) override;

    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;

private:
    void removeDropZone();
    void removeHostedView();

    Controller* controller_;
    Steinberg::IPlugFrame* hostFrame_ = nullptr;       // DAW's plug frame
    Steinberg::IPtr<Steinberg::IPlugView> hostedView_;   // Hosted plugin's view (ref-counted)
    void* parentNSView_ = nullptr;                      // Cached parent NSView* from attached()
    void* dropZoneView_ = nullptr;                      // Our DropZoneView NSView*
    std::atomic<Steinberg::uint32> refCount_{1};

    static constexpr int kDefaultWidth = 400;
    static constexpr int kDefaultHeight = 300;
};

} // namespace VST3MCPWrapper
