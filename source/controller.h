#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <memory>
#include <mutex>
#include <string>

namespace Steinberg {
namespace Vst {
class ConnectionProxy;
}
}

namespace VST3MCPWrapper {

class Controller : public Steinberg::Vst::EditController,
                   public Steinberg::Vst::IComponentHandler {
public:
    Controller();
    ~Controller() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    // IEditController
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;

    // IComponentHandler — receives parameter changes from the hosted plugin's GUI
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                               Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

    // IConnectionPoint — receive messages from processor
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // COM aggregation: expose both IEditController and IComponentHandler
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    REFCOUNT_METHODS(EditController)

    // Thread-safe access to hosted controller (used by MCP handlers)
    Steinberg::IPtr<Steinberg::Vst::IEditController> getHostedController() const;

    // Dynamic plugin loading — called from drop zone view and MCP tools
    // Returns empty string on success, error message on failure.
    std::string loadPlugin(const std::string& path);
    void unloadPlugin();
    bool isPluginLoaded() const;
    std::string getCurrentPluginPath() const;

private:
    struct MCPServer;
    std::unique_ptr<MCPServer> mcpServer_;

    void startMCPServer();
    void stopMCPServer();

    void connectHostedComponents();
    void disconnectHostedComponents();
    void syncComponentState();

    void teardownHostedController();
    bool setupHostedController();
    void sendLoadMessage(const std::string& path);

    Steinberg::FUnknown* hostContext_ = nullptr;
    std::string currentPluginPath_;

    // Raw observer pointer — Controller creates WrapperPlugView, but the DAW
    // owns the view's lifetime via COM ref-counting.  Controller::terminate()
    // clears this and the view's back-pointer so neither side writes through a
    // dangling pointer during unusual teardown orders.
    class WrapperPlugView* activeView_ = nullptr;
    friend class WrapperPlugView;
    friend class ControllerTestAccess;

    mutable std::mutex hostedControllerMutex_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> hostedController_;

    Steinberg::IPtr<Steinberg::Vst::ConnectionProxy> componentCP_;
    Steinberg::IPtr<Steinberg::Vst::ConnectionProxy> controllerCP_;
};

} // namespace VST3MCPWrapper
