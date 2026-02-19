# VST3 MCP Wrapper

## Project Goal

A VST3 effect plugin that acts as a transparent wrapper/host for any third-party VST3 plugin. It loads inside a DAW like a normal effect, hosts another VST3 plugin internally (passing through all audio, MIDI, and GUI), and exposes the hosted plugin's parameters via MCP (Model Context Protocol). This allows an LLM agent to read and control any VST3 plugin's parameters remotely.

The vision: one wrapper instance per plugin slot in a DAW session, giving an AI agent full control over mixing and mastering.

## Architecture

```
DAW  <-->  VST3MCPWrapper (our plugin)  <-->  Hosted VST3 Plugin (e.g. Neutron 5 EQ)
                  |
                  +--> MCP Server (HTTP on 127.0.0.1:8771)
                            |
                        LLM Agent
```

**Dual-component VST3 plugin:**
- **Processor** (`source/processor.h/cpp`): Owns the hosted plugin's `IComponent` and `IAudioProcessor`. Handles audio/MIDI passthrough. Drains the parameter change queue and injects changes into `ProcessData::inputParameterChanges`. Wraps `setState`/`getState` with a custom format that includes the hosted plugin path. Handles `"LoadPlugin"` messages from the controller.
- **Controller** (`source/controller.h/cpp`): Owns the hosted plugin's `IEditController`. Implements `IComponentHandler` so the hosted plugin's GUI can route parameter changes back through us. Returns hosted GUI via `createView` when a plugin is loaded, or a drop zone `WrapperPlugView` when empty. Connects hosted component ↔ controller via `IConnectionPoint`/`ConnectionProxy`. Runs the MCP server. Provides `loadPlugin()` entry point for drag-and-drop and MCP tools.
- **HostedPluginModule** (`source/hostedplugin.h/cpp`): Singleton that holds the shared `VST3::Hosting::Module`, factory, hosted `IComponent` reference, and a thread-safe parameter change queue. Supports `load()`/`unload()` for dynamic plugin switching.
- **WrapperPlugView** (`source/wrapperview.h`, `source/wrapperview.mm`): Custom `IPlugView` implementation with a native macOS `NSView` drop zone. Accepts `.vst3` bundle drag-and-drop from Finder. Shown when no hosted plugin is loaded.

### Parameter Change Flow

```
MCP set_parameter ──┐
                    ├──> pushParamChange() ──> [queue] ──> Processor::process()
GUI performEdit ────┘                                      injects into ProcessData::inputParameterChanges
                                                           ──> hostedProcessor_->process()
```

MCP `set_parameter` also calls `setParamNormalized` on the hosted controller to update the GUI immediately.

## Source Files

| File | Purpose |
|---|---|
| `source/processor.h/cpp` | Audio processor — dynamic hosted component loading, audio/MIDI passthrough, wrapper state format, parameter injection |
| `source/controller.h/cpp` | Edit controller — IComponentHandler, dynamic plugin loading, IConnectionPoint, MCP server, view management |
| `source/hostedplugin.h/cpp` | Shared singleton — module/factory, load/unload, param change queue, hosted component sharing, UTF-16 helper |
| `source/wrapperview.h` | WrapperPlugView — IPlugView drop zone interface |
| `source/wrapperview.mm` | Drop zone NSView — Objective-C++ drag-and-drop implementation |
| `source/pluginids.h` | FUID definitions for processor and controller |
| `source/version.h` | Plugin version and metadata strings |
| `source/factory.cpp` | VST3 plugin factory registration (not distributable) |
| `resource/Info.plist.in` | macOS bundle Info.plist template |
| `.mcp.json` | Project MCP server config — connects Claude Code to the running plugin |
| `.vscode/tasks.json` | VS Code build tasks (Cmd+Shift+B to build) |

## Build

macOS only. C++20. CMake 3.25+. Dependencies fetched automatically via FetchContent.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target VST3MCPWrapper
```

Output: `build/VST3/Debug/VST3MCPWrapper.vst3`

Point your DAW's VST3 scan path to `build/VST3/Debug/` to load the plugin directly from the build folder.

### Dependencies

- **VST3 SDK** (`v3.7.12_build_20`) — plugin framework, hosting utilities (`sdk`, `sdk_hosting`)
- **cpp-mcp** (`main`) — MCP server library (CMake target: `mcp`)

### CMake Quirks

- `macmain.cpp` must be explicitly added to plugin sources behind `if(APPLE)` — not part of `sdk` library
- `module_mac.mm` requires `-fobjc-arc` compile flag for VST3 plugin hosting on macOS
- cpp-mcp library CMake target name is `mcp` (not `cpp_mcp`)
- `SMTG_CREATE_PLUGIN_LINK OFF` — prevents symlink conflicts with existing VST3 folder
- Custom `Info.plist.in` needed for correct `CFBundlePackageType` (`BNDL`) on macOS
- `PkgInfo` file generated via post-build step for macOS bundle recognition
- `memorystream.cpp` is not in any SDK library target — use header-only `ResizableMemoryIBStream` instead

## MCP Server

Runs on `127.0.0.1:8771` using Streamable HTTP transport (cpp-mcp library). Started when the controller initializes, stopped on terminate.

The MCP server is configured as a project-level MCP server in `.mcp.json`, so Claude Code can connect directly to the hosted plugin when it's running in a DAW. The plugin must be loaded in the DAW before the tools become available.

### Tools

| Tool | Description |
|---|---|
| `list_parameters` | Lists all hosted plugin parameters (id, title, units, value, display) |
| `get_parameter` | Get a single parameter's current value by ID |
| `set_parameter` | Set a parameter's normalized value (0.0–1.0) by ID. Updates both GUI and audio processor. |
| `list_available_plugins` | Lists all VST3 plugins installed on the system |
| `load_plugin` | Load a VST3 plugin by file path (dispatched to main thread) |
| `get_loaded_plugin` | Get the currently loaded plugin path |

## VST3 Hosting Notes

- `beginEdit`/`endEdit`/`performEdit` are on `IComponentHandler` (host callback), not `IEditController`
- `setParamNormalized` on `IEditController` updates GUI only — parameter changes to the processor must go through `ProcessData::inputParameterChanges`
- `ParameterChanges`/`ParameterValueQueue` from `sdk_hosting` are concrete implementations of `IParameterChanges`/`IParamValueQueue`
- `ConnectionProxy` from `sdk_hosting` wraps `IConnectionPoint` for safe bidirectional messaging between component and controller
- Not all plugins implement `IConnectionPoint` — connection is attempted but failure is non-fatal

### Plugin Loading Flow

```
Drag-and-drop / MCP load_plugin
   ↓
Controller::loadPlugin(path)
   ├── teardownHostedController()
   ├── HostedPluginModule::load(path)
   ├── setupHostedController()
   ├── sendMessage("LoadPlugin") → Processor::notify()
   │                                  ├── unloadHostedPlugin()
   │                                  ├── loadHostedPlugin(path)
   │                                  └── sendMessage("PluginLoaded") → ack
   └── restartComponent(kIoChanged) → DAW recreates view
```

### State Persistence Format

```
[4 bytes] magic: "VMCW"
[4 bytes] version: uint32 = 1
[4 bytes] pathLen: uint32
[pathLen bytes] UTF-8 plugin path
[remaining] hosted component state
```

## Known Limitations (MVP)

- **Single instance only** — singleton architecture, one MCP server on a fixed port
- **No error handling** for MCP server port conflicts

## Conventions

- All source code in `source/`, resources in `resource/`
- Namespace: `VST3MCPWrapper`
- Not distributable (`flags = 0` in factory) — processor and controller share state via singleton
- No VSTGUI dependency — drop zone uses native NSView, hosted GUI is forwarded directly
- Prefer `IPtr<>` for VST3 COM pointers
- Keep the build output in `build/`, never copy plugin binaries to system paths during development
- Use `utf16ToUtf8()` from `hostedplugin.h` for VST3 string conversions
