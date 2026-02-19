# VST3 MCP Wrapper — Architecture

This document describes the current architecture and the roadmap for multi-instance support and MCP API enhancements.

## Vision

One wrapper instance per plugin slot in a DAW session, giving an AI agent full read/write control over every plugin's parameters. The agent connects via MCP, discovers all active wrapper instances, and orchestrates mixing and mastering across the entire session.

```
DAW Session
├── Track 1: VST3MCPWrapper → EQ plugin     ──┐
├── Track 2: VST3MCPWrapper → Compressor     ──┤── MCP (per-instance servers)
├── Track 3: VST3MCPWrapper → Reverb         ──┤       │
└── Master:  VST3MCPWrapper → Limiter        ──┘       │
                                                   LLM Agent
```

Currently limited to a single instance (singleton architecture, fixed MCP port 8771). Multi-instance support is planned for Phase 2.

## Component Diagram

```
┌─────────────────────────── DAW Process ────────────────────────────┐
│                                                                     │
│  ┌─── VST3MCPWrapper Instance ───────────────────────────────────┐ │
│  │                                                                │ │
│  │  Processor                          Controller                 │ │
│  │  ┌──────────────────────┐          ┌──────────────────────┐   │ │
│  │  │ IAudioProcessor      │◄─IMsg───►│ IEditController      │   │ │
│  │  │ hosted IComponent    │          │ hosted IEditCtrl     │   │ │
│  │  │ hosted IAudioProc    │          │ IComponentHandler    │   │ │
│  │  │                      │          │                      │   │ │
│  │  │ process():           │          │ MCP Server :8771     │   │ │
│  │  │  drain queue (try_lock)         │  ┌─────────────────┐ │   │ │
│  │  │  merge params        │          │  │ list_parameters │ │   │ │
│  │  │  forward to hosted   │          │  │ get/set_param   │ │   │ │
│  │  └──────────┬───────────┘          │  │ load/unload     │ │   │ │
│  │             │                      │  └────────┬────────┘ │   │ │
│  │             │                      └───────────┼──────────┘   │ │
│  │             │                                  │              │ │
│  │  ┌──────────▼──────────────────────────────────▼───────────┐  │ │
│  │  │              HostedPluginModule (singleton)              │  │ │
│  │  │  Module + Factory   │   Param Queue (try_lock)          │  │ │
│  │  │  IComponent ref     │   Plugin path + class IDs         │  │ │
│  │  └─────────────────────────────────────────────────────────┘  │ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
                              │
                         MCP (HTTP)
                              │
                    ┌─────────▼─────────┐
                    │    LLM Agent       │
                    │  (Claude Code,     │
                    │   custom agent)    │
                    └────────────────────┘
```

## Threading Model

Four thread contexts exist:

```
┌─────────────────────────────────────────────────────┐
│ Audio Thread (real-time, never blocks)              │
│  • Processor::process()                             │
│  • Drains param queue via try_lock (never waits)    │
│  • Forwards ProcessData to hosted processor         │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│ Main Thread (all VST3 lifecycle, GUI)               │
│  • initialize / terminate / setActive               │
│  • loadPlugin / unloadPlugin                        │
│  • IComponentHandler callbacks from hosted GUI      │
│  • View attach / detach / resize                    │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│ MCP Server Thread (HTTP request handling)           │
│  • Tool handlers for list/get/set parameters        │
│  • Reads hosted controller state (IPtr copy under   │
│    mutex — acceptable, MCP is not real-time)         │
│  • Plugin load/unload dispatched to main thread     │
│    via dispatch_async + promise/future               │
└─────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────┐
│ DAW Message Thread (IMessage delivery)              │
│  • Processor::notify() / Controller::notify()       │
│  • Plugin load coordination messages                │
└─────────────────────────────────────────────────────┘
```

### Rules

1. **Audio thread never blocks.** No mutexes, no allocation, no syscalls. Parameter queue uses `try_lock` — if the lock is held by a producer, changes arrive next buffer (~1-5ms later).
2. **Main thread owns all VST3 lifecycle.** Plugin loading, component creation/destruction, view management — all on main thread.
3. **MCP thread reads, main thread writes.** The MCP thread reads parameter state from the hosted controller (thread-safe via `IPtr` copy under mutex). Any mutation (load/unload) is dispatched to the main thread via `dispatch_async` + `std::promise/std::future`. Dispatched blocks check a shared `alive` flag before accessing the controller — preventing use-after-free during shutdown.
4. **Shutdown is safe.** A shared `alive` flag (`std::shared_ptr<std::atomic<bool>>`) is set to `false` before `server->stop()`, so dispatched GCD blocks bail out instead of accessing the dying controller. In-flight MCP handlers use `wait_for` with a 5-second timeout, preventing deadlock if the main thread is blocked in `stop()`. After the server thread exits, teardown proceeds.

### Parameter Change Flow

```
MCP set_parameter ──┐
                    ├──> pushParamChange() ──> [mutex-guarded queue] ──> Processor::process()
GUI performEdit ────┘                           try_lock on drain         injects into
                                                                          ProcessData::inputParameterChanges
                                                                          ──> hostedProcessor_->process()
```

MCP `set_parameter` also calls `setParamNormalized` on the hosted controller to update the GUI immediately.

### Shutdown Sequence

```
Controller::terminate()       [main thread]
  │
  ├── 1. *alive = false                       ← signals dispatched blocks to bail out
  ├── 2. mcpServer_->server->stop()           ← stops accepting requests, waits for workers
  │       └── in-flight handlers time out     ← wait_for(5s) prevents deadlock with main thread
  │           dispatched blocks check alive      and skip controller access if shutting down
  ├── 3. mcpServer_->serverThread.join()      ← waits for server thread exit
  ├── 4. teardownHostedController()
  └── 5. EditController::terminate()
```

## VST3 Hosting Lifecycle

### Stored State for Replay

The processor stores DAW configuration so it can be replayed when a hosted plugin is loaded mid-session:

```cpp
ProcessSetup currentSetup_;                      // from setupProcessing()
std::vector<SpeakerArrangement> storedInputArr_;  // from setBusArrangements()
std::vector<SpeakerArrangement> storedOutputArr_; // from setBusArrangements()
```

### Loading Sequence

When a hosted plugin is loaded (via MCP, drag-and-drop, or session restore):

**Controller side** (`setupHostedController`):
```
1.  Create temp IComponent from factory
2.  component->getControllerClassId(cid)
    ├── OK → create separate IEditController from factory using cid
    └── Fail → queryInterface<IEditController>(component)  [single-component plugin]
3.  controller->initialize(hostContext)
4.  controller->setComponentHandler(this)
```

**Processor side** (`loadHostedPlugin`, triggered by "LoadPlugin" message):
```
1.  Module::create(path)               — load the .vst3 bundle
2.  factory.createInstance<IComponent>  — create the component
3.  component->initialize(hostContext)  — initialize
4.  QueryInterface<IAudioProcessor>     — get the processor interface
5.  activateBus(audio/event, in/out)    — activate all buses
6.  setBusArrangements(stored)          — replay DAW's bus config
7.  setupProcessing(currentSetup_)      — replay sample rate, block size
8.  setActive(true)  [if wrapper is active]
9.  processorReady_ = true              — audio thread starts forwarding
```

### Single-Component Plugins

Some plugins implement both `IComponent` and `IEditController` on the same class (no separate controller). The wrapper detects this in `setupHostedController` when `getControllerClassId` fails: it queries the component for `IEditController` via `queryInterface`. If found, that component instance becomes the controller. The processor independently creates its own component instance for audio processing. Parameter changes flow through the same queue mechanism as separate-component plugins.

### Latency and Tail

`getLatencySamples()` and `getTailSamples()` forward to the hosted plugin's processor. The controller calls `restartComponent(kIoChanged)` after loading, which triggers the DAW to re-query latency for delay compensation.

### Unloading Sequence

```
1.  processorReady_ = false             — audio thread stops forwarding
2.  setActive(false)  [if active]
3.  component->terminate()
4.  release all IPtr references
5.  clear stored plugin path
```

### State Format (v1)

```
[4 bytes]  magic: "VMCW"
[4 bytes]  version: uint32 = 1
[4 bytes]  pathLen: uint32 (capped at 4096)
[N bytes]  pluginPath: UTF-8 string
[remaining] hosted component state
```

## MCP API

### Current Tools

| Tool | Description |
|---|---|
| `list_parameters` | List all parameters with id, title, units, normalizedValue, displayValue, defaultNormalizedValue, stepCount, canAutomate |
| `get_parameter` | Get parameter by ID. Validates ID exists, returns error if not found. |
| `set_parameter` | Set parameter by ID + normalized value (0.0-1.0). Validates ID exists. Routes to both GUI and audio. |
| `list_available_plugins` | List all installed VST3 plugins on the system |
| `load_plugin` | Load by path. Dispatched to main thread, returns success or error. |
| `unload_plugin` | Unload hosted plugin, return to drop zone |
| `get_loaded_plugin` | Get current plugin path |

All parameter tools validate that the requested ID exists before acting. Invalid IDs return `isError: true` with a descriptive message.

---

## Roadmap

### Phase 2: Multi-Instance

**Problem:** A global singleton (`HostedPluginModule`) shares state between all instances. A fixed MCP port (8771) prevents multiple servers. Only one wrapper instance can function at a time.

#### Design: Instance Registry

Replace the singleton with a per-instance `HostedPluginInstance` and a static `InstanceRegistry` that maps instance IDs to their shared state.

```
InstanceRegistry (static)
├── instance-A → HostedPluginInstance { module, factory, paramQueue, mcpPort }
├── instance-B → HostedPluginInstance { module, factory, paramQueue, mcpPort }
└── instance-C → HostedPluginInstance { module, factory, paramQueue, mcpPort }
```

**Instance ID lifecycle:**

1. `Processor::initialize()` — generates a unique instance ID (incrementing counter), creates a `HostedPluginInstance` in the registry
2. `Processor::initialize()` — sends an `"InstanceID"` message to the controller via `IMessage` immediately after creation
3. `Processor::getState()` — writes the instance ID into the wrapper state header
4. `Controller::setComponentState()` — reads the instance ID from state, looks up the registry (handles session restore)
5. `Processor::terminate()` — removes the instance from the registry

The processor proactively sends the instance ID via IMessage rather than relying solely on state parsing, since `setComponentState` timing is not guaranteed relative to when the controller first needs access.

**HostedPluginInstance** replaces `HostedPluginModule` and holds:
- `VST3::Hosting::Module::Ptr` and factory
- Plugin path and class IDs
- `IComponent` reference (set by processor, read by controller)
- Parameter change queue (try_lock drain)
- MCP server (port, lifecycle)

#### State Format v2

```
[4 bytes]  magic: "VMCW"
[4 bytes]  version: uint32 = 2
[4 bytes]  instanceIdLen: uint32 (capped at 256)
[N bytes]  instanceId: UTF-8 string
[4 bytes]  pathLen: uint32 (capped at 4096)
[N bytes]  pluginPath: UTF-8 string
[remaining] hosted component state
```

Version 2 adds the instance ID. Version 1 states remain loadable (generate a new instance ID on restore).

#### MCP Port Allocation

Each instance's MCP server binds to an OS-assigned port (bind to port 0), then registers in a discovery file:

**Discovery file** at `~/Library/Application Support/VST3MCPWrapper/instances.json`:

```json
{
  "instances": [
    {
      "id": "instance-1",
      "port": 49152,
      "pid": 12345,
      "pluginPath": "/Library/.../Neutron.vst3",
      "pluginName": "Neutron 5 Equalizer"
    }
  ]
}
```

The discovery file requires advisory file locking (e.g., `flock`) since multiple DAW processes may write concurrently. Entries are cleaned up on `terminate()` and stale-checked by PID on read.

A future "session MCP server" could aggregate all instances behind a single endpoint, but per-instance servers are the simpler first step.

#### Items

- Replace singleton with InstanceRegistry + HostedPluginInstance
- Dynamic MCP port allocation (OS-assigned)
- Instance discovery file with file locking
- State format v2 with instance ID
- Proactive IMessage for instance ID (fresh instances)
- Update `.mcp.json` to support discovery-based connection

### Phase 3: MCP API Enhancements

| Tool | Description |
|---|---|
| `set_parameters` | Batch set: accepts array of `{id, value}` pairs. Single queue push. |
| `set_parameter_by_name` | Fuzzy name match to set value. Reduces round-trips for LLM agents. |
| `list_available_plugins` | Enhanced: include human-readable plugin name alongside path. |
| `list_presets` | List available factory/user presets for the hosted plugin |
| `load_preset` | Load a preset by name or index |
| `save_preset` | Save current state as a user preset |
| `get_parameter_info` | Detailed info: min/max display values, stepped values, string list |
| `list_instances` | List all active wrapper instances (reads discovery file) |

### Response Format Enhancement

Include previous value in `set_parameter` responses:

```json
{
  "id": 1234,
  "title": "High Cut Freq",
  "normalizedValue": 0.75,
  "displayValue": "15.0 kHz",
  "previousValue": 0.5
}
```
