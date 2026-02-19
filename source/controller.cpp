#include "controller.h"
#include "hostedplugin.h"
#include "wrapperview.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"

#include "mcp_server.h"
#include "mcp_tool.h"

#include <dispatch/dispatch.h>
#include <os/log.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace VST3MCPWrapper {

static constexpr int kMCPServerPort = 8771;

// State format constants (must match processor.cpp)
static constexpr char kStateMagic[4] = {'V', 'M', 'C', 'W'};

// ---- MCP Server ----
struct Controller::MCPServer {
    std::unique_ptr<mcp::server> server;
    std::thread serverThread;

    void start(Controller* controller) {
        mcp::server::configuration conf;
        conf.host = "127.0.0.1";
        conf.port = kMCPServerPort;
        conf.name = "VST3 MCP Wrapper";
        conf.version = "0.1.0";

        server = std::make_unique<mcp::server>(conf);

        // --- list_parameters tool ---
        auto listParamsTool = mcp::tool_builder("list_parameters")
            .with_description("List all parameters of the hosted VST3 plugin with their IDs, names, and current values")
            .build();

        server->register_tool(listParamsTool,
            [controller](const mcp::json& params, const std::string& session_id) -> mcp::json {
                auto ctrl = controller->getHostedController();
                if (!ctrl) {
                    return {
                        {"content", {{{"type", "text"}, {"text", "No hosted plugin loaded"}}}},
                        {"isError", true}
                    };
                }

                int32 paramCount = ctrl->getParameterCount();
                mcp::json paramList = mcp::json::array();

                for (int32 i = 0; i < paramCount; ++i) {
                    ParameterInfo info;
                    if (ctrl->getParameterInfo(i, info) == kResultOk) {
                        std::string title = utf16ToUtf8(info.title);
                        std::string units = utf16ToUtf8(info.units);
                        ParamValue value = ctrl->getParamNormalized(info.id);

                        String128 displayStr;
                        std::string display;
                        if (ctrl->getParamStringByValue(info.id, value, displayStr) == kResultOk) {
                            display = utf16ToUtf8(displayStr);
                        }

                        paramList.push_back({
                            {"id", info.id},
                            {"title", title},
                            {"units", units},
                            {"normalizedValue", value},
                            {"displayValue", display},
                            {"defaultNormalizedValue", info.defaultNormalizedValue},
                            {"stepCount", info.stepCount},
                            {"canAutomate", (info.flags & ParameterInfo::kCanAutomate) != 0}
                        });
                    }
                }

                return {
                    {"content", {{{"type", "text"}, {"text", paramList.dump(2)}}}}
                };
            });

        // --- get_parameter tool ---
        auto getParamTool = mcp::tool_builder("get_parameter")
            .with_description("Get the current value of a specific parameter by its ID")
            .with_number_param("id", "The parameter ID", true)
            .build();

        server->register_tool(getParamTool,
            [controller](const mcp::json& params, const std::string& session_id) -> mcp::json {
                auto ctrl = controller->getHostedController();
                if (!ctrl) {
                    return {
                        {"content", {{{"type", "text"}, {"text", "No hosted plugin loaded"}}}},
                        {"isError", true}
                    };
                }

                ParamID paramId = params["id"].get<uint32>();
                ParamValue value = ctrl->getParamNormalized(paramId);

                String128 displayStr;
                std::string display;
                if (ctrl->getParamStringByValue(paramId, value, displayStr) == kResultOk) {
                    display = utf16ToUtf8(displayStr);
                }

                mcp::json result = {
                    {"id", paramId},
                    {"normalizedValue", value},
                    {"displayValue", display}
                };

                return {
                    {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
                };
            });

        // --- set_parameter tool ---
        auto setParamTool = mcp::tool_builder("set_parameter")
            .with_description("Set the normalized value (0.0 to 1.0) of a specific parameter by its ID")
            .with_number_param("id", "The parameter ID", true)
            .with_number_param("value", "The normalized value between 0.0 and 1.0", true)
            .build();

        server->register_tool(setParamTool,
            [controller](const mcp::json& params, const std::string& session_id) -> mcp::json {
                auto ctrl = controller->getHostedController();
                if (!ctrl) {
                    return {
                        {"content", {{{"type", "text"}, {"text", "No hosted plugin loaded"}}}},
                        {"isError", true}
                    };
                }

                ParamID paramId = params["id"].get<uint32>();
                ParamValue value = params["value"].get<double>();

                // Clamp to valid range
                if (value < 0.0) value = 0.0;
                if (value > 1.0) value = 1.0;

                // Update the hosted controller's internal state (for GUI)
                ctrl->setParamNormalized(paramId, value);

                // Queue the change for the audio processor
                HostedPluginModule::instance().pushParamChange(paramId, value);

                // Read back to confirm
                ParamValue newValue = ctrl->getParamNormalized(paramId);
                String128 displayStr;
                std::string display;
                if (ctrl->getParamStringByValue(paramId, newValue, displayStr) == kResultOk) {
                    display = utf16ToUtf8(displayStr);
                }

                mcp::json result = {
                    {"id", paramId},
                    {"normalizedValue", newValue},
                    {"displayValue", display}
                };

                return {
                    {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
                };
            });

        // --- list_available_plugins tool ---
        auto listPluginsTool = mcp::tool_builder("list_available_plugins")
            .with_description("List all VST3 plugins installed on the system")
            .build();

        server->register_tool(listPluginsTool,
            [](const mcp::json& params, const std::string& session_id) -> mcp::json {
                auto paths = VST3::Hosting::Module::getModulePaths();
                mcp::json pluginList = mcp::json::array();
                for (const auto& path : paths) {
                    pluginList.push_back(path);
                }
                return {
                    {"content", {{{"type", "text"}, {"text", pluginList.dump(2)}}}}
                };
            });

        // --- load_plugin tool ---
        auto loadPluginTool = mcp::tool_builder("load_plugin")
            .with_description("Load a VST3 plugin by its file path. Use list_available_plugins to see available plugins.")
            .with_string_param("path", "Full path to the .vst3 plugin bundle", true)
            .build();

        server->register_tool(loadPluginTool,
            [controller](const mcp::json& params, const std::string& session_id) -> mcp::json {
                std::string path = params["path"].get<std::string>();

                // Dispatch to main thread since loadPlugin touches VST3 lifecycle objects
                std::string pathCopy = path;
                Controller* ctrl = controller;
                dispatch_async(dispatch_get_main_queue(), ^{
                    ctrl->loadPlugin(pathCopy);
                });

                mcp::json result = {
                    {"status", "loading"},
                    {"path", path}
                };
                return {
                    {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
                };
            });

        // --- get_loaded_plugin tool ---
        auto getLoadedTool = mcp::tool_builder("get_loaded_plugin")
            .with_description("Get the currently loaded VST3 plugin path")
            .build();

        server->register_tool(getLoadedTool,
            [controller](const mcp::json& params, const std::string& session_id) -> mcp::json {
                std::string path = controller->getCurrentPluginPath();
                mcp::json result = {
                    {"loaded", !path.empty()},
                    {"path", path.empty() ? "none" : path}
                };
                return {
                    {"content", {{{"type", "text"}, {"text", result.dump(2)}}}}
                };
            });

        // Start server in background thread
        serverThread = std::thread([this]() {
            server->start(true);
        });
    }

    void stop() {
        if (server) {
            server->stop();
        }
        if (serverThread.joinable()) {
            serverThread.join();
        }
        server.reset();
    }
};

Controller::Controller() = default;
Controller::~Controller() = default;

tresult PLUGIN_API Controller::queryInterface(const TUID iid, void** obj) {
    // Expose IComponentHandler so the hosted controller can call back to us
    if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid)) {
        addRef();
        *obj = static_cast<IComponentHandler*>(this);
        return kResultOk;
    }
    return EditController::queryInterface(iid, obj);
}

IPtr<IEditController> Controller::getHostedController() const {
    std::lock_guard<std::mutex> lock(hostedControllerMutex_);
    return hostedController_;
}

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    tresult result = EditController::initialize(context);
    if (result != kResultOk)
        return result;

    hostContext_ = context;

    // Start MCP server (works even without a hosted plugin)
    startMCPServer();

    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    stopMCPServer();

    teardownHostedController();

    return EditController::terminate();
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    // Always return our wrapper view — it handles both drop zone and hosted plugin
    auto* view = new WrapperPlugView(this);
    activeView_ = view;
    return view;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if (!state)
        return kResultOk;

    // Try to read wrapper state format to extract plugin path
    char magic[4] = {};
    int32 numBytesRead = 0;
    if (state->read(magic, sizeof(magic), &numBytesRead) != kResultOk || numBytesRead != sizeof(magic))
        return kResultOk;

    if (std::memcmp(magic, kStateMagic, sizeof(magic)) != 0)
        return kResultOk;

    uint32 version = 0;
    if (state->read(&version, sizeof(version), &numBytesRead) != kResultOk)
        return kResultOk;

    uint32 pathLen = 0;
    if (state->read(&pathLen, sizeof(pathLen), &numBytesRead) != kResultOk)
        return kResultOk;

    std::string pluginPath;
    if (pathLen > 0) {
        pluginPath.resize(pathLen);
        if (state->read(pluginPath.data(), pathLen, &numBytesRead) != kResultOk)
            return kResultOk;
    }

    // Load the plugin if needed
    if (!pluginPath.empty() && pluginPath != currentPluginPath_) {
        teardownHostedController();
        auto& pluginModule = HostedPluginModule::instance();
        std::string error;
        pluginModule.load(pluginPath, error);
        setupHostedController();
        currentPluginPath_ = pluginPath;
    }

    // Forward remaining state to hosted controller
    auto ctrl = getHostedController();
    if (ctrl) {
        return ctrl->setComponentState(state);
    }

    return kResultOk;
}

// --- IComponentHandler ---
// These are called by the hosted plugin's GUI when the user changes parameters.

tresult PLUGIN_API Controller::beginEdit(ParamID id) {
    // No-op: we don't track edit gestures
    return kResultOk;
}

tresult PLUGIN_API Controller::performEdit(ParamID id, ParamValue valueNormalized) {
    // Queue the change for the audio processor
    HostedPluginModule::instance().pushParamChange(id, valueNormalized);
    return kResultOk;
}

tresult PLUGIN_API Controller::endEdit(ParamID id) {
    // No-op: we don't track edit gestures
    return kResultOk;
}

tresult PLUGIN_API Controller::restartComponent(int32 flags) {
    // The hosted plugin requests a restart. Forward to our host if available.
    if (componentHandler) {
        return componentHandler->restartComponent(flags);
    }
    return kResultOk;
}

// --- IConnectionPoint ---

tresult PLUGIN_API Controller::notify(IMessage* message) {
    if (!message)
        return kResultFalse;

    if (strcmp(message->getMessageID(), "PluginLoaded") == 0) {
        // Processor has finished loading the plugin — acknowledgment received
        return kResultOk;
    }

    return EditController::notify(message);
}

// --- Dynamic plugin loading ---

void Controller::loadPlugin(const std::string& path) {
    os_log(OS_LOG_DEFAULT, "[VST3MCPWrapper] loadPlugin called with path: %{public}s", path.c_str());

    teardownHostedController();

    auto& pluginModule = HostedPluginModule::instance();
    std::string error;
    if (!pluginModule.load(path, error)) {
        os_log_error(OS_LOG_DEFAULT, "[VST3MCPWrapper] Failed to load module: %{public}s", error.c_str());
        return;
    }
    os_log(OS_LOG_DEFAULT, "[VST3MCPWrapper] Module loaded successfully");

    bool controllerOk = setupHostedController();
    os_log(OS_LOG_DEFAULT, "[VST3MCPWrapper] setupHostedController: %{public}s", controllerOk ? "success" : "failed");

    // Tell the processor to load the same plugin
    sendLoadMessage(path);

    currentPluginPath_ = path;

    // Switch the active view in-place (drop zone → hosted plugin GUI)
    auto ctrl = getHostedController();
    os_log(OS_LOG_DEFAULT, "[VST3MCPWrapper] activeView_=%p hostedController=%p", activeView_, ctrl.get());
    if (activeView_ && ctrl) {
        auto* hostedPlugView = ctrl->createView("editor");
        os_log(OS_LOG_DEFAULT, "[VST3MCPWrapper] hosted createView returned: %p", hostedPlugView);
        if (hostedPlugView) {
            activeView_->switchToHostedView(hostedPlugView);
            os_log(OS_LOG_DEFAULT, "[VST3MCPWrapper] switchToHostedView completed");
        }
    }

    // Also tell the DAW about the I/O change (bus arrangements may differ)
    if (componentHandler) {
        componentHandler->restartComponent(kIoChanged);
    }
}

bool Controller::isPluginLoaded() const {
    return getHostedController() != nullptr;
}

std::string Controller::getCurrentPluginPath() const {
    std::lock_guard<std::mutex> lock(hostedControllerMutex_);
    return currentPluginPath_;
}

// --- Private helpers ---

void Controller::teardownHostedController() {
    disconnectHostedComponents();

    IPtr<IEditController> ctrl;
    {
        std::lock_guard<std::mutex> lock(hostedControllerMutex_);
        ctrl = hostedController_;
        hostedController_ = nullptr;
    }
    if (ctrl) {
        ctrl->setComponentHandler(nullptr);
        ctrl->terminate();
    }
    currentPluginPath_.clear();
}

bool Controller::setupHostedController() {
    auto& pluginModule = HostedPluginModule::instance();
    if (!pluginModule.isLoaded())
        return false;

    // If the processor hasn't set the controller CID yet, find it ourselves
    if (!pluginModule.hasControllerClassID()) {
        auto factory = pluginModule.getFactory();
        if (!factory)
            return false;
        auto component = factory->createInstance<IComponent>(pluginModule.getEffectClassID());
        if (component) {
            if (component->initialize(hostContext_) == kResultOk) {
                TUID cid;
                if (component->getControllerClassId(cid) == kResultOk) {
                    pluginModule.setControllerClassID(cid);
                }
                component->terminate();
            }
        }
    }

    if (!pluginModule.hasControllerClassID())
        return false;

    TUID cid;
    pluginModule.getControllerClassID(cid);
    VST3::UID controllerUID = VST3::UID::fromTUID(cid);

    auto factory = pluginModule.getFactory();
    if (!factory)
        return false;

    auto ctrl = factory->createInstance<IEditController>(controllerUID);
    if (!ctrl)
        return false;

    if (ctrl->initialize(hostContext_) != kResultOk)
        return false;

    ctrl->setComponentHandler(this);

    {
        std::lock_guard<std::mutex> lock(hostedControllerMutex_);
        hostedController_ = ctrl;
    }

    connectHostedComponents();
    syncComponentState();

    return true;
}

void Controller::sendLoadMessage(const std::string& path) {
    if (auto msg = owned(allocateMessage())) {
        msg->setMessageID("LoadPlugin");
        msg->getAttributes()->setBinary("path", path.data(), static_cast<uint32>(path.size()));
        sendMessage(msg);
    }
}

void Controller::connectHostedComponents() {
    auto hostedComponent = HostedPluginModule::instance().getHostedComponent();
    auto ctrl = getHostedController();
    if (!hostedComponent || !ctrl)
        return;

    auto compICP = FUnknownPtr<IConnectionPoint>(hostedComponent);
    auto contrICP = FUnknownPtr<IConnectionPoint>(ctrl);
    if (!compICP || !contrICP)
        return;

    componentCP_ = owned(new ConnectionProxy(compICP));
    controllerCP_ = owned(new ConnectionProxy(contrICP));

    componentCP_->connect(contrICP);
    controllerCP_->connect(compICP);
}

void Controller::disconnectHostedComponents() {
    if (componentCP_) {
        componentCP_->disconnect();
        componentCP_ = nullptr;
    }
    if (controllerCP_) {
        controllerCP_->disconnect();
        controllerCP_ = nullptr;
    }
}

void Controller::syncComponentState() {
    auto hostedComponent = HostedPluginModule::instance().getHostedComponent();
    auto ctrl = getHostedController();
    if (!hostedComponent || !ctrl)
        return;

    // Get the processor's state and send it to the controller
    ResizableMemoryIBStream stream;
    if (hostedComponent->getState(&stream) == kResultOk) {
        stream.rewind();
        ctrl->setComponentState(&stream);
    }
}

void Controller::startMCPServer() {
    mcpServer_ = std::make_unique<MCPServer>();
    mcpServer_->start(this);
}

void Controller::stopMCPServer() {
    if (mcpServer_) {
        mcpServer_->stop();
        mcpServer_.reset();
    }
}

} // namespace VST3MCPWrapper
