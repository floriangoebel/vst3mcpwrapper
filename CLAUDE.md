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
- **Controller** (`source/controller.h/cpp`): Owns the hosted plugin's `IEditController`. Implements `IComponentHandler` so the hosted plugin's GUI can route parameter changes back through us — `performEdit` queues changes via `pushParamChange()` but deliberately does **not** forward to the DAW's `componentHandler` (the wrapper exposes no parameters of its own, so DAW automation cannot record hosted plugin gestures; parameter values reach the audio processor via our queue → `ProcessData::inputParameterChanges` path instead). `beginEdit`/`endEdit`/`restartComponent` forward to the DAW's host handler. Returns hosted GUI via `createView` when a plugin is loaded, or a drop zone `WrapperPlugView` when empty. Connects hosted component ↔ controller via `IConnectionPoint`/`ConnectionProxy`. Runs the MCP server. Provides `loadPlugin()` entry point for drag-and-drop and MCP tools.
- **HostedPluginModule** (`source/hostedplugin.h/cpp`): Singleton that holds the shared `VST3::Hosting::Module`, factory, hosted `IComponent` reference, and a thread-safe parameter change queue. Supports `load()`/`unload()` for dynamic plugin switching.
- **WrapperPlugView** (`source/wrapperview.h`, `source/wrapperview.mm` / `source/wrapperview_linux.cpp`): Custom `IPlugView` implementation. On macOS, provides a native `NSView` drop zone that accepts `.vst3` bundle drag-and-drop from Finder. On Linux, a no-op stub (headless — plugin loading via MCP only). Shown when no hosted plugin is loaded.

### Parameter Change Flow

```
MCP set_parameter ──┐
                    ├──> pushParamChange() ──> [mutex-guarded queue] ──> Processor::process()
GUI performEdit ────┘                           try_lock on drain         injects into
                                                                          ProcessData::inputParameterChanges
                                                                          ──> hostedProcessor_->process()
```

MCP `set_parameter` also calls `setParamNormalized` on the hosted controller to update the GUI immediately. The audio thread uses `try_lock` to drain the queue — it never blocks. The drain buffer (`drainBuffer_`) is pre-reserved to 256 entries in the Processor constructor to avoid heap allocation on the first `process()` call. The queue is capped at 10,000 entries (`kMaxParamQueueSize`) to prevent unbounded memory growth from a misbehaving client; overflow is logged once per episode.

## Source Files

| File | Purpose |
|---|---|
| `source/processor.h/cpp` | Audio processor — dynamic hosted component loading, audio/MIDI passthrough, wrapper state format, parameter injection |
| `source/controller.h/cpp` | Edit controller — IComponentHandler, dynamic plugin loading, IConnectionPoint, MCP server, view management |
| `source/hostedplugin.h/cpp` | Shared singleton — module/factory, load/unload, param change queue, hosted component sharing, UTF-16 helper |
| `source/wrapperview.h` | WrapperPlugView — IPlugView drop zone interface (cross-platform header), includes inline COM methods (`addRef`/`release`/`queryInterface`) and `clearController()` for safe teardown |
| `source/wrapperview.mm` | macOS WrapperPlugView — Drop zone NSView with Objective-C++ drag-and-drop |
| `source/wrapperview_linux.cpp` | Linux WrapperPlugView — No-op stub, returns `kResultFalse` from `isPlatformTypeSupported` (headless/MCP-only) |
| `source/messageids.h` | Constexpr message ID constants (`kLoadPlugin`, `kUnloadPlugin`, `kPluginLoaded`) — eliminates string literal duplication between processor and controller |
| `source/pluginids.h` | FUID definitions for processor and controller |
| `source/mcp_param_handlers.h` | Extracted MCP parameter tool handlers: `isValidParamId()`, `handleListParameters()`, `handleGetParameter()`, `handleSetParameter()` — testable inline functions used by controller's MCP server |
| `source/mcp_plugin_handlers.h` | Extracted MCP plugin management tool handlers: `handleGetLoadedPlugin()`, `handleListAvailablePlugins()`, `buildLoadPluginResponse()`, `handleUnloadPluginNotLoaded()`, `handleUnloadPluginSuccess()`, `handleShuttingDown()`, `handleTimeout()` — testable inline functions used by controller's MCP server |
| `source/dispatcher.h` | Platform-independent thread dispatch abstraction: `MainThreadDispatcher` class with `dispatch<R>()` (returns future), `shutdown()`, `isAlive()`. Template methods in header, platform-specific `postImpl()` in .mm/.cpp |
| `source/dispatcher_mac.mm` | macOS dispatcher implementation: `postImpl()` uses `dispatch_async(dispatch_get_main_queue())` |
| `source/dispatcher_linux.cpp` | Linux dispatcher implementation: `postImpl()` uses a dedicated worker thread with condition variable task queue |
| `source/stateformat.h` | Shared state persistence format: constants (magic, version, max path length) and `writeStateHeader()`/`readStateHeader()` helper functions. Both validate `numBytesWritten`/`numBytesRead` after each stream operation to detect partial I/O. |
| `source/logging.h` | Cross-platform logging macros: `WRAPPER_LOG` / `WRAPPER_LOG_ERROR` — uses `os_log` on macOS, `fprintf(stderr)` on Linux. Eliminates repeated `#ifdef __APPLE__` blocks. |
| `source/version.h` | Plugin version and metadata strings |
| `source/factory.cpp` | VST3 plugin factory registration (not distributable) |
| `resource/Info.plist.in` | macOS bundle Info.plist template |
| `cmake/patch_mcp_version.cmake` | Build-time patch for cpp-mcp protocol version (2024-11-05 → 2025-03-26) |
| `cmake/validate_bundle_linux.cmake` | CTest validation script — checks Linux VST3 bundle layout (directory structure + ELF shared object) |
| `tests/helpers/processor_test_access.h` | Shared `ProcessorTestAccess` class — unified friend accessor for all Processor private members, used by all processor test files |
| `tests/helpers/controller_test_access.h` | Shared `ControllerTestAccess` class — friend accessor for Controller private members (`activeView_`, `currentPluginPath_`, `hostedController_`), used by controller view and state tests |
| `tests/helpers/test_helpers.h` | Shared `fillTChar()` helper — fills `TChar` arrays from `char16_t` literals, used by UTF-16 and MCP param tests |
| `.mcp.json` | Project MCP server config — connects Claude Code to the running plugin |
| `.vscode/tasks.json` | VS Code build tasks (Cmd+Shift+B to build) |

## Build

macOS and Linux. C++20. CMake 3.25+. Dependencies fetched automatically via FetchContent.

### Prerequisites

- **macOS**: Xcode Command Line Tools (`xcode-select --install`)
- **Linux (Ubuntu/Debian)**: `build-essential` (GCC, make), `cmake` (3.25+), `pkg-config`. No X11 or GUI dev headers needed — VSTGUI is disabled on Linux.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target VST3MCPWrapper
```

To build and run tests:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build --target VST3MCPWrapper_Tests
ctest --test-dir build --output-on-failure
```

### Output

`build/VST3/Debug/VST3MCPWrapper.vst3`

- **macOS**: A macOS bundle containing `Contents/MacOS/VST3MCPWrapper`
- **Linux**: A directory tree containing `Contents/x86_64-linux/VST3MCPWrapper.so` (plus `Contents/Resources/moduleinfo.json` auto-generated by SDK)

### DAW Setup

Point your DAW's VST3 scan path to `build/VST3/Debug/` to load the plugin directly from the build folder.

On Linux, symlink the bundle to the standard VST3 scan directory:

```bash
ln -s $(pwd)/build/VST3/Debug/VST3MCPWrapper.vst3 ~/.vst3/
```

Linux DAWs known to scan `~/.vst3/`: Bitwig Studio, REAPER, Ardour.

Linux VST3 scan paths (from SDK `module_linux.cpp`): `~/.vst3/`, `/usr/lib/vst3/`, `/usr/local/lib/vst3/`

### Dependencies

- **VST3 SDK** (`v3.7.12_build_20`) — plugin framework, hosting utilities (`sdk`, `sdk_hosting`)
- **cpp-mcp** (`main`) — MCP server library (CMake target: `mcp`). Patched at build time (`cmake/patch_mcp_version.cmake`) to advertise MCP protocol version `2025-03-26` instead of `2024-11-05` — required for Claude Code compatibility.

### CMake Quirks

- `OBJCXX` language is enabled conditionally via `enable_language(OBJCXX)` inside `if(APPLE)` — avoids issues on Linux where OBJCXX is unavailable
- `macmain.cpp` / `linuxmain.cpp` must be explicitly added to plugin sources behind `if(APPLE)` / `else()` — not part of `sdk` library
- `module_mac.mm` (macOS) / `module_linux.cpp` (Linux) provide platform-specific VST3 module loading — gated behind `if(APPLE)` / `else()`
- `module_mac.mm`, `wrapperview.mm`, `dispatcher_mac.mm` require `-fobjc-arc` compile flag on macOS
- `wrapperview.mm` (macOS) / `wrapperview_linux.cpp` (Linux) provide platform-specific view implementations
- cpp-mcp library CMake target name is `mcp` (not `cpp_mcp`)
- `SMTG_CREATE_PLUGIN_LINK OFF` — prevents symlink conflicts with existing VST3 folder
- Custom `Info.plist.in` needed for correct `CFBundlePackageType` (`BNDL`) on macOS
- `PkgInfo` file generated via post-build step for macOS bundle recognition
- Ad-hoc code signing via post-build `codesign --force --sign -` — required for hosts with hardened runtime (e.g. Ableton Live)
- `memorystream.cpp` is not in any SDK library target — use header-only `ResizableMemoryIBStream` instead
- cpp-mcp is patched via `PATCH_COMMAND` + `cmake/patch_mcp_version.cmake` to fix protocol version mismatch with Claude Code. If the fetched source is cached, delete `build/_deps/cpp_mcp-*` and re-run `cmake -B build` to re-apply the patch.
- On Linux, `SMTG_ENABLE_VSTGUI_SUPPORT OFF` must be set (not just `SMTG_ADD_VSTGUI OFF`) to fully disable VSTGUI and avoid X11 dev header requirements
- On Linux, the `mcp` static library needs `POSITION_INDEPENDENT_CODE ON` since VST3 plugins are `.so` shared objects
- Linux VST3 bundle layout is validated via `cmake/validate_bundle_linux.cmake` CTest test (runs with `ctest`)
- Both plugin and test targets compile with `-Wall -Wextra -Wno-unused-parameter` (PRIVATE) — warnings in our code are treated as bugs, SDK/dependency warnings are unaffected

## MCP Server

Runs on `127.0.0.1:8771` using SSE transport (cpp-mcp library). The server exposes a `/sse` endpoint for Server-Sent Events and a `/message` endpoint for client POST requests. Started when the controller initializes, stopped on terminate. Server start is wrapped in try-catch — if the port is already in use (or any other error), the failure is logged to stderr and the plugin continues without MCP (audio passthrough still works).

The MCP server is configured as a project-level MCP server in `.mcp.json`, so Claude Code can connect directly to the hosted plugin when it's running in a DAW. The plugin must be loaded in the DAW before the tools become available.

### Tools

| Tool | Description |
|---|---|
| `list_parameters` | Lists all hosted plugin parameters (id, title, units, normalizedValue, displayValue, defaultNormalizedValue, stepCount, canAutomate) |
| `get_parameter` | Get a single parameter's current value by ID. Validates ID exists. |
| `set_parameter` | Set a parameter's normalized value (0.0–1.0) by ID. Validates ID exists and value is finite (rejects NaN/Infinity). Updates both GUI and audio processor. |
| `list_available_plugins` | Lists all VST3 plugins installed on the system |
| `load_plugin` | Load a VST3 plugin by file path. Dispatched via `MainThreadDispatcher` + future. Returns success or error. |
| `unload_plugin` | Unload the currently hosted plugin and return to the drop zone |
| `get_loaded_plugin` | Get the currently loaded plugin path |

All parameter tools validate that the requested ID exists before acting. Invalid IDs return `isError: true`. `set_parameter` additionally validates that the value is finite (`std::isfinite`) — NaN and Infinity values are rejected with `isError: true`.

## VST3 Hosting Notes

- `beginEdit`/`endEdit`/`performEdit` are on `IComponentHandler` (host callback), not `IEditController`
- `setParamNormalized` on `IEditController` updates GUI only — parameter changes to the processor must go through `ProcessData::inputParameterChanges`
- `ParameterChanges`/`ParameterValueQueue` from `sdk_hosting` are concrete implementations of `IParameterChanges`/`IParamValueQueue`
- `ConnectionProxy` from `sdk_hosting` wraps `IConnectionPoint` for safe bidirectional messaging between component and controller
- Not all plugins implement `IConnectionPoint` — connection is attempted but failure is non-fatal
- **Single-component plugins** (where `IComponent` also implements `IEditController`) are supported — `setupHostedController` detects this via `queryInterface` when `getControllerClassId` fails, and uses the component as the controller directly. The processor and controller sides each create their own instance; parameter changes flow through the same `IComponentHandler` → param queue → `process()` path as separate-component plugins.

### Plugin Loading Flow

```
Drag-and-drop / MCP load_plugin
   ↓
Controller::loadPlugin(path)
   ├── teardownHostedController()
   ├── HostedPluginModule::load(path)
   ├── setupHostedController()
   │     ├── Create temp IComponent, call getControllerClassId()
   │     ├── If OK → create separate IEditController from factory
   │     └── If fails → queryInterface for IEditController (single-component plugin)
   ├── sendMessage("LoadPlugin") → Processor::notify()
   │                                  ├── unloadHostedPlugin()
   │                                  ├── loadHostedPlugin(path)
   │                                  ├── replay setActive/setProcessing
   │                                  └── sendMessage("PluginLoaded") → Controller::notify()
   │                                                                      ├── connectHostedComponents()
   │                                                                      └── syncComponentState()
   └── restartComponent(kIoChanged) → DAW recreates view
```

### State Persistence Format

```
[4 bytes] magic: "VMCW"
[4 bytes] version: uint32 = 1
[4 bytes] pathLen: uint32 (capped at 4096)
[pathLen bytes] UTF-8 plugin path
[remaining] hosted component state
```

### Threading Safety

- **Audio thread** uses `try_lock` to drain the parameter queue — never blocks
- **MCP load/unload** use `MainThreadDispatcher` (abstracts `dispatch_async` on macOS / worker thread on Linux) + `std::promise/std::future` with a shared `alive` flag and 5-second timeout — prevents deadlock during shutdown (dispatched blocks check `alive` before accessing the controller; handlers time out if the main thread is blocked in `stop()`)
- **Processor stores DAW state** — bus arrangements, activation, and processing flags are stored and replayed when loading a plugin mid-session (`wrapperActive_`, `wrapperProcessing_`, `storedInputArr_`/`storedOutputArr_`, `currentSetup_`). Replay happens in both the `notify("LoadPlugin")` path (runtime loading) and the `setState()` path (preset recall, undo).
- **`processorReady_` uses release/acquire ordering** — it is a publication guard: when the store becomes visible (`true`), the audio thread must also see the fully-constructed `hostedProcessor_`, `hostedComponent_`, and all setup done in `loadHostedPlugin()`. The `release` store in `loadHostedPlugin()`/`unloadHostedPlugin()` synchronizes-with the `acquire` load in `process()`, establishing a happens-before relationship.
- **`wrapperActive_`/`wrapperProcessing_`/`hostedActive_`/`hostedProcessing_` use `memory_order_relaxed`** — these are independent boolean flags that don't guard other non-atomic writes. `wrapperActive_`/`wrapperProcessing_` are written on the main thread (`setActive`/`setProcessing`) and read on the message thread (`notify`); `hostedActive_`/`hostedProcessing_` are written on the main/message thread and read on the audio thread in `process()`.
- **`setProcessing` forwarding** — the wrapper overrides `setProcessing()` to forward to the hosted processor. This is critical: `AudioEffect::setProcessing()` is a no-op (`kNotImplemented`), so without forwarding, hosted plugins never get told to start processing.
- **Bus activation** — only the first audio input, first audio output, and first event input bus are activated on the hosted plugin. Extra buses (sidechain, etc.) are explicitly deactivated since the wrapper doesn't provide ProcessData buffers for them.
- **Latency/tail forwarding** — `getLatencySamples()`/`getTailSamples()` forward to hosted plugin
- **`MainThreadDispatcher` naming caveat** — on macOS, tasks genuinely execute on the main thread via GCD `dispatch_get_main_queue()`. On Linux, tasks execute on a dedicated worker thread (not the actual main thread) — the name reflects macOS semantics where the abstraction originated. Correctness is unaffected: the key requirement is serialization of load/unload operations, not main-thread identity.
- **`activeView_` is a raw observer pointer** — Controller creates `WrapperPlugView` and stores a non-ref-counted pointer in `activeView_`. The DAW owns the view's lifetime via COM ref-counting. `Controller::terminate()` breaks the back-pointer via `clearController()` to prevent use-after-free during unusual teardown orders (see US-006 in this review).
- **Dual `currentPluginPath_` copies** — both Processor and Controller maintain independent `currentPluginPath_` members. The Processor's copy is accessed only on the message thread (in `notify`, `setState`, `getState`) so it needs no synchronization. The Controller's copy is guarded by `hostedControllerMutex_` because the MCP server thread reads it via `getCurrentPluginPath()`.

See `ARCHITECTURE.md` for detailed threading model, hosting lifecycle, and multi-instance roadmap.

## Known Limitations (MVP)

- **Single instance only** — singleton architecture, one MCP server on a fixed port
- **Linux is headless** — no GUI drop zone on Linux (`WrapperPlugView` is a no-op stub that returns `kResultFalse` from `isPlatformTypeSupported`). Plugin loading is MCP-only via `load_plugin` tool. Audio passthrough and all MCP tools work normally. Hosted plugin GUIs are not displayed.
- **No DAW automation recording** — `performEdit()` queues parameter changes for the audio processor but does not forward to the DAW's `componentHandler`. The wrapper exposes no parameters of its own, so DAW automation cannot record hosted plugin parameter gestures. Parameter control is MCP-only.

## Ralph (Autonomous Agent Loop)

Ralph is an iterative autonomous coding agent that works through a PRD (Product Requirements Document) one user story at a time. Each iteration picks the highest-priority incomplete story, implements it, runs tests, commits, and updates progress. A shell script loops until all stories pass or max iterations are reached.

### File Layout

| File | Purpose |
|---|---|
| `scripts/ralph-workspace/ralph.sh` | Runner script — loops Claude Code or Amp through iterations |
| `scripts/ralph-workspace/CLAUDE.md` | Agent prompt — instructions fed to the agent each iteration |
| `scripts/ralph-workspace/prd.json` | Current PRD — user stories with acceptance criteria and pass/fail status |
| `scripts/ralph-workspace/progress.txt` | Accumulated progress log + Codebase Patterns section at top |
| `scripts/ralph-workspace/.last-branch` | Tracks the previous PRD's branch name for archive detection |
| `scripts/ralph-workspace/archive/` | Archived PRDs and progress logs from previous runs |

### PRD Format

```json
{
  "project": "VST3MCPWrapper",
  "branchName": "ralph/feature-name",
  "description": "What this batch of work is about",
  "userStories": [
    {
      "id": "US-001",
      "title": "Short title",
      "description": "As a developer, I want...",
      "acceptanceCriteria": ["Criterion 1", "Criterion 2"],
      "priority": 1,
      "passes": false,
      "notes": "Implementation hints for the agent"
    }
  ]
}
```

Stories are processed in `priority` order (lowest number first). The agent sets `passes: true` after each story's tests pass.

### Usage

```bash
# Run with Claude Code (default 10 iterations)
./scripts/ralph-workspace/ralph.sh --tool claude

# Run with Amp
./scripts/ralph-workspace/ralph.sh --tool amp

# Custom iteration limit
./scripts/ralph-workspace/ralph.sh --tool claude 20
```

The script requires a `scripts/ralph` submodule for the Amp prompt template. Claude Code mode reads from `scripts/ralph-workspace/CLAUDE.md` directly.

### Workflow

1. Write a PRD in `scripts/ralph-workspace/prd.json` with `branchName` set to a `ralph/*` branch
2. Run `ralph.sh` — it creates the branch, implements stories one per iteration, commits each
3. When all stories have `passes: true`, the agent outputs `<promise>COMPLETE</promise>` and the loop exits
4. Review the branch, squash any checkpoint commits, merge to main

### Archive

When the `branchName` in `prd.json` changes between runs, `ralph.sh` automatically archives the previous PRD and progress log to `scripts/ralph-workspace/archive/<date>-<branch>/` and resets `progress.txt` for the new run.

### Progress and Patterns

`progress.txt` serves two purposes:
- **Codebase Patterns** section at the top — general reusable knowledge consolidated across stories (build commands, mock patterns, SDK quirks). Read by the agent at the start of each iteration.
- **Per-story entries** appended at the bottom — what was implemented, files changed, learnings for future iterations.

## Conventions

- **Documentation must stay in sync with code.** Any code change must include corresponding updates to CLAUDE.md, ARCHITECTURE.md, and any other affected documentation. The repository must always be in an internally consistent and correct state — no stale descriptions, no outdated diagrams, no mismatched tool descriptions.
- All source code in `source/`, resources in `resource/`
- Namespace: `VST3MCPWrapper`
- Not distributable (`flags = 0` in factory) — processor and controller share state via singleton
- No VSTGUI dependency — drop zone uses native NSView (macOS), no-op stub (Linux), hosted GUI forwarded directly
- macOS-specific code (`#import <Cocoa/Cocoa.h>`, `os_log`, Obj-C++) lives in `.mm` files; gate `#include <os/log.h>` and `os_log` calls behind `#ifdef __APPLE__` in `.cpp` files
- Prefer `IPtr<>` for VST3 COM pointers
- Keep the build output in `build/`, never copy plugin binaries to system paths during development
- Use `utf16ToUtf8()` from `hostedplugin.h` for VST3 string conversions
