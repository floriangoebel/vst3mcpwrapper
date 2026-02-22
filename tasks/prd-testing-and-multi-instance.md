# PRD: Testing Infrastructure & Multi-Instance Support

## Introduction

VST3 MCP Wrapper is a VST3 effect plugin that hosts any third-party VST3 plugin internally and exposes its parameters via MCP (Model Context Protocol), enabling LLM agents to read and control plugin parameters remotely. The current MVP works as a single instance on a fixed port with no automated tests.

This PRD covers two sequential phases: (1) establishing comprehensive test coverage to harden the MVP, and (2) evolving from a singleton architecture to multi-instance support so multiple wrapper instances can run concurrently in a DAW session — each with its own MCP server.

The target platforms are macOS and Linux. The target users are developers building AI agents for music production workflows.

## Goals

- Establish a test infrastructure using Google Test (gtest/gmock) with CMake integration
- Achieve full test coverage of core logic, business logic, and MCP tool handlers
- Validate thread-safety properties (parameter queue, shutdown sequence, atomic state)
- Ensure state persistence correctness (serialization round-trips, corruption detection)
- Enable Linux builds alongside macOS
- Replace the singleton `HostedPluginModule` with a per-instance registry
- Support multiple concurrent MCP servers with dynamic port allocation
- Provide an instance discovery mechanism for agents to find all active wrapper instances

## User Stories

### Testing Infrastructure

#### US-001: Set up test framework and build integration
**Description:** As a developer, I want a test target in CMake so that I can write and run unit tests with `ctest`.

**Acceptance Criteria:**
- [ ] Google Test added via FetchContent (or reused from cpp-mcp dependency)
- [ ] `tests/CMakeLists.txt` created with a `VST3MCPWrapper_Tests` executable target
- [ ] Top-level CMakeLists.txt has `option(BUILD_TESTS "Build unit tests" ON)` gating `add_subdirectory(tests)`
- [ ] `cmake --build build --target VST3MCPWrapper_Tests` compiles successfully
- [ ] `ctest --test-dir build --output-on-failure` runs and reports results
- [ ] Test target links against `sdk`, `sdk_hosting`, `gtest`, `gmock`

---

#### US-002: Test UTF-16 to UTF-8 conversion
**Description:** As a developer, I want tests for `utf16ToUtf8()` so that parameter names and units display correctly for all languages.

**Acceptance Criteria:**
- [ ] Test ASCII characters (code points < 0x80) convert correctly
- [ ] Test 2-byte UTF-8 sequences (code points 0x80–0x7FF, e.g. accented characters)
- [ ] Test 3-byte UTF-8 sequences (code points 0x800+, e.g. CJK characters)
- [ ] Test null termination handling
- [ ] Test max length boundary (128 chars from VST3 `String128`)
- [ ] Test empty string input

---

#### US-003: Test state format serialization
**Description:** As a developer, I want tests for the wrapper state format so that preset recall, undo, and session restore work reliably.

**Acceptance Criteria:**
- [ ] Test round-trip: write state with a plugin path → read it back → path matches
- [ ] Test round-trip with empty path (no plugin loaded)
- [ ] Test invalid magic bytes are rejected
- [ ] Test unsupported version number is rejected
- [ ] Test path length exceeding 4096 bytes is rejected
- [ ] Test truncated stream (fewer bytes than header declares) is handled gracefully
- [ ] Test state with hosted component data appended after the path

---

#### US-004: Test parameter change queue
**Description:** As a developer, I want tests for `pushParamChange()` / `drainParamChanges()` so that parameter changes from MCP and GUI reach the audio processor correctly.

**Acceptance Criteria:**
- [ ] Test single push → drain returns the change
- [ ] Test multiple pushes → drain returns all in order
- [ ] Test drain clears the queue (second drain returns empty)
- [ ] Test concurrent pushes from multiple threads (no data loss or corruption)
- [ ] Test `try_lock` semantics: drain during an active push does not block (simulated contention)
- [ ] Run with Thread Sanitizer (TSan) enabled — no warnings

---

#### US-005: Test HostedPluginModule singleton state management
**Description:** As a developer, I want tests for `HostedPluginModule` load/unload state tracking so that plugin lifecycle transitions are correct.

**Acceptance Criteria:**
- [ ] `isLoaded()` returns false initially
- [ ] After `load()` with a valid path, `isLoaded()` returns true and `getPluginPath()` matches
- [ ] After `unload()`, `isLoaded()` returns false and `getPluginPath()` is empty
- [ ] `load()` with an invalid/nonexistent path returns false with an error message
- [ ] Class ID setters/getters (`setControllerClassID`, `getControllerClassID`, `hasControllerClassID`) work correctly
- [ ] `setHostedComponent()` / `getHostedComponent()` store and retrieve the component pointer

---

#### US-006: Test processor lifecycle state tracking
**Description:** As a developer, I want tests for the processor's DAW state tracking so that plugin loading mid-session replays the correct activation and processing state.

**Acceptance Criteria:**
- [ ] `setActive(true)` sets `wrapperActive_` to true
- [ ] `setActive(false)` sets `wrapperActive_` to false
- [ ] `setProcessing(true)` sets `wrapperProcessing_` to true
- [ ] `setProcessing(false)` sets `wrapperProcessing_` to false
- [ ] `setBusArrangements()` stores arrangements for later replay
- [ ] `setupProcessing()` stores the `ProcessSetup` for later replay
- [ ] When a hosted plugin is loaded while the wrapper is active+processing, the hosted plugin receives `setActive(true)` then `setProcessing(true)` in that order

---

#### US-007: Test processor audio passthrough
**Description:** As a developer, I want tests for `process()` audio forwarding so that audio passes through transparently when no plugin is loaded and correctly when one is.

**Acceptance Criteria:**
- [ ] With no hosted plugin: input audio is copied to output buffers unchanged (32-bit float)
- [ ] With no hosted plugin: input audio is copied to output buffers unchanged (64-bit double)
- [ ] Channel count mismatch between input and output is handled (extra output channels zeroed)
- [ ] Parameter changes from the queue are injected into `ProcessData::inputParameterChanges` before forwarding to hosted processor
- [ ] When hosted processor is loaded, `process()` is called on the hosted processor with the modified `ProcessData`

---

#### US-008: Test processor state persistence (setState / getState)
**Description:** As a developer, I want tests for `Processor::setState()` and `getState()` so that DAW session save/restore and undo work correctly.

**Acceptance Criteria:**
- [ ] `getState()` writes a valid wrapper state format to the stream
- [ ] `setState()` parses a valid state and extracts the plugin path
- [ ] `setState()` with a plugin path triggers hosted plugin loading
- [ ] `setState()` replays activation and processing state after loading
- [ ] `setState()` with an empty/missing path results in no hosted plugin (or unloads current)
- [ ] `setState()` with corrupted data (bad magic, bad version) returns `kResultFalse`

---

#### US-009: Test MCP parameter tools (list, get, set)
**Description:** As a developer, I want tests for the MCP parameter tool handlers so that agents can reliably read and write plugin parameters.

**Acceptance Criteria:**
- [ ] `list_parameters` returns JSON with all parameters (id, title, units, normalizedValue, displayValue, defaultNormalizedValue, stepCount, canAutomate)
- [ ] `get_parameter` with a valid ID returns the parameter's current value and metadata
- [ ] `get_parameter` with an invalid ID returns `isError: true` with a descriptive message
- [ ] `set_parameter` with a valid ID and value (0.0–1.0) updates the parameter
- [ ] `set_parameter` with an invalid ID returns `isError: true`
- [ ] `set_parameter` routes the change to both the GUI (`setParamNormalized`) and the audio processor (param queue)
- [ ] All tools return appropriate errors when no plugin is loaded

---

#### US-010: Test MCP plugin management tools (load, unload, get_loaded, list_available)
**Description:** As a developer, I want tests for the MCP plugin lifecycle tools so that agents can discover, load, and unload plugins reliably.

**Acceptance Criteria:**
- [ ] `list_available_plugins` returns a list of installed VST3 plugin paths
- [ ] `load_plugin` with a valid path returns success
- [ ] `load_plugin` with an invalid path returns `isError: true`
- [ ] `get_loaded_plugin` returns the current plugin path when loaded
- [ ] `get_loaded_plugin` returns an appropriate response when no plugin is loaded
- [ ] `unload_plugin` unloads the current plugin and returns success
- [ ] `unload_plugin` when no plugin loaded returns an appropriate response

---

#### US-011: Test message routing (IMessage notify)
**Description:** As a developer, I want tests for the processor's `notify()` handler so that controller-to-processor messages correctly trigger plugin loading.

**Acceptance Criteria:**
- [ ] "LoadPlugin" message with a valid path attribute triggers `loadHostedPlugin()`
- [ ] "LoadPlugin" message extracts the path from binary message attributes
- [ ] After loading, the processor sends a "PluginLoaded" acknowledgment message
- [ ] Unrecognized message IDs are ignored (no crash, no error)
- [ ] Missing path attribute in "LoadPlugin" message is handled gracefully

---

#### US-012: Test shutdown safety and alive flag
**Description:** As a developer, I want tests for the shutdown sequence so that termination never deadlocks or crashes.

**Acceptance Criteria:**
- [ ] Setting `alive` to false causes dispatched blocks to bail out without accessing the controller
- [ ] In-flight MCP handlers with `wait_for(5s)` time out gracefully when the main thread is blocked
- [ ] After `alive` is set to false, new MCP tool calls return errors rather than hanging
- [ ] The shutdown sequence completes within a bounded time (no deadlock)
- [ ] Run with Thread Sanitizer (TSan) — no data race warnings during shutdown

---

#### US-013: Add Linux build support
**Description:** As a developer, I want the project to build on Linux so that I can develop and test on both macOS and Linux.

**Acceptance Criteria:**
- [ ] CMakeLists.txt conditionally includes platform-specific sources (`module_mac.mm` / macOS frameworks on APPLE; `module_linux.cpp` / equivalent on Linux)
- [ ] `wrapperview.mm` (Objective-C++ NSView) is excluded on Linux; a stub or no-op `IPlugView` is provided
- [ ] `dispatch_async` (GCD, macOS-only) is abstracted behind a platform-independent mechanism for MCP load/unload dispatching
- [ ] The plugin builds successfully on Linux with GCC 12+ or Clang 15+
- [ ] All unit tests pass on Linux
- [ ] MCP server starts and responds to tool calls on Linux

---

### Multi-Instance Support (Phase 2)

#### US-014: Replace singleton with InstanceRegistry
**Description:** As a developer, I want each wrapper instance to have its own state so that multiple instances can coexist in a DAW session.

**Acceptance Criteria:**
- [ ] New `HostedPluginInstance` class holds all per-instance state (module, factory, param queue, MCP server port, plugin path, class IDs)
- [ ] New static `InstanceRegistry` maps instance IDs to `HostedPluginInstance` objects
- [ ] `Processor::initialize()` creates a unique instance ID and registers a new `HostedPluginInstance`
- [ ] `Processor::terminate()` removes the instance from the registry
- [ ] Controller retrieves its instance via IMessage (instance ID sent from processor)
- [ ] Two wrapper instances in the same process do not share state
- [ ] All existing unit tests are updated to use `HostedPluginInstance` instead of `HostedPluginModule`

---

#### US-015: Dynamic MCP port allocation
**Description:** As a developer, I want each wrapper instance to bind to an OS-assigned port so that multiple MCP servers can run simultaneously.

**Acceptance Criteria:**
- [ ] MCP server binds to port 0 (OS-assigned ephemeral port)
- [ ] After binding, the actual port number is stored in the `HostedPluginInstance`
- [ ] Two wrapper instances get different ports
- [ ] MCP tools work correctly on dynamically assigned ports
- [ ] Port is released when the instance is terminated

---

#### US-016: Instance discovery file
**Description:** As an agent developer, I want a discovery file listing all active wrapper instances so that my agent can find and connect to them.

**Acceptance Criteria:**
- [ ] Discovery file written to `~/Library/Application Support/VST3MCPWrapper/instances.json` (macOS) or `~/.config/VST3MCPWrapper/instances.json` (Linux)
- [ ] Each entry contains: `id`, `port`, `pid`, `pluginPath`, `pluginName`
- [ ] File is updated on instance creation, plugin load/unload, and instance termination
- [ ] Advisory file locking (`flock`) prevents concurrent write corruption
- [ ] Stale entries (PID no longer running) are cleaned up on read
- [ ] File is valid JSON at all times (atomic write via temp file + rename)

---

#### US-017: State format v2 with instance ID
**Description:** As a developer, I want the state format to include the instance ID so that DAW session restore maps instances correctly.

**Acceptance Criteria:**
- [ ] State format v2 header: magic "VMCW", version 2, instanceIdLen, instanceId, pathLen, pluginPath, hosted state
- [ ] `getState()` writes v2 format
- [ ] `setState()` reads both v1 (generates new instance ID) and v2 formats
- [ ] Instance ID length capped at 256 bytes
- [ ] Round-trip test: write v2 → read v2 → instance ID and path match
- [ ] Backward compatibility test: v1 state loads correctly, gets a new instance ID

---

#### US-018: Instance ID communication via IMessage
**Description:** As a developer, I want the processor to proactively send its instance ID to the controller so the controller can look up the correct registry entry.

**Acceptance Criteria:**
- [ ] `Processor::initialize()` sends an "InstanceID" message to the controller immediately after creating the registry entry
- [ ] `Controller::notify()` handles "InstanceID" messages and stores the instance ID
- [ ] Controller uses the instance ID to look up `HostedPluginInstance` in the registry
- [ ] If the controller receives an "InstanceID" for an unknown registry entry, it logs a warning (does not crash)

---

#### US-019: MCP tool: list_instances
**Description:** As an agent developer, I want a `list_instances` MCP tool so that my agent can discover all active wrapper instances.

**Acceptance Criteria:**
- [ ] New `list_instances` tool reads the discovery file
- [ ] Returns JSON array of instances with `id`, `port`, `pluginPath`, `pluginName`
- [ ] Stale entries (dead PIDs) are filtered out
- [ ] Works when discovery file doesn't exist yet (returns empty array)
- [ ] Works when discovery file is empty or has no instances (returns empty array)

---

## Functional Requirements

### Testing

- FR-1: The project must include a `tests/` directory with Google Test-based unit tests
- FR-2: Tests must be buildable via `cmake --build build --target VST3MCPWrapper_Tests`
- FR-3: Tests must be runnable via `ctest --test-dir build --output-on-failure`
- FR-4: Tests must cover: UTF-8 conversion, state format serialization, parameter queue, singleton state, processor lifecycle, audio passthrough, state persistence, MCP parameter tools, MCP plugin tools, message routing, shutdown safety
- FR-5: Thread-safety tests must run cleanly under Thread Sanitizer (TSan)
- FR-6: Test code must not require a real VST3 plugin binary — all VST3 interfaces must be mocked via gmock

### Linux Support

- FR-7: The build system must conditionally include platform-specific sources for macOS and Linux
- FR-8: macOS-specific code (Objective-C++, GCD, NSView) must be gated behind `#ifdef __APPLE__` or CMake `if(APPLE)`
- FR-9: A platform-independent thread dispatch mechanism must replace raw `dispatch_async` usage
- FR-10: All unit tests must pass on both macOS and Linux

### Multi-Instance

- FR-11: Each wrapper instance must have a unique instance ID generated at `Processor::initialize()`
- FR-12: `InstanceRegistry` must be a static (process-global) thread-safe map from instance ID to `HostedPluginInstance`
- FR-13: Each instance's MCP server must bind to an OS-assigned port (port 0)
- FR-14: A discovery file must list all active instances with ID, port, PID, plugin path, and plugin name
- FR-15: The discovery file must use advisory file locking to prevent corruption
- FR-16: Stale entries in the discovery file must be cleaned up based on PID liveness
- FR-17: State format v2 must include the instance ID and remain backward-compatible with v1
- FR-18: The processor must send its instance ID to the controller via IMessage during initialization
- FR-19: A `list_instances` MCP tool must read the discovery file and return active instances

## Non-Goals

- No GUI testing (WrapperPlugView / NSView drag-and-drop) — manual testing only
- No end-to-end DAW integration tests (those require a running DAW host)
- No CI/CD pipeline in this PRD (can be added later)
- No Windows support in this iteration
- No "session MCP server" aggregating instances behind a single endpoint (future Phase 3+)
- No MCP API enhancements (batch set, presets, fuzzy name match) — deferred to Phase 3
- No automatic reconnection or failover for MCP clients when instances restart

## Technical Considerations

- **Google Test availability:** The cpp-mcp dependency already pulls in googletest. Evaluate whether it can be reused directly or if a separate FetchContent declaration is cleaner.
- **Mocking VST3 interfaces:** `IComponent`, `IEditController`, `IAudioProcessor`, `IMessage`, `IParamValueQueue`, and `IParameterChanges` need gmock wrappers. These are COM-style interfaces with `addRef`/`release` — mocks must handle reference counting or use `IPtr<>` carefully.
- **Thread Sanitizer:** TSan must be enabled for concurrency tests. Add a CMake option or preset for TSan builds: `-DCMAKE_CXX_FLAGS="-fsanitize=thread"`.
- **Linux hosting:** The VST3 SDK provides `module_linux.cpp` for plugin enumeration on Linux. The NSView-based drop zone must be stubbed — a no-op `IPlugView` returning `kResultFalse` from `isPlatformTypeSupported` is sufficient.
- **GCD replacement:** `dispatch_async(dispatch_get_main_queue(), ...)` is macOS-only. Options: (a) `std::thread` + condition variable, (b) a simple single-thread executor, (c) platform abstraction layer. The MCP load/unload handlers need "dispatch to main thread" semantics — the exact mechanism is flexible as long as it supports the `alive` flag + timeout pattern.
- **State format v2 backward compatibility:** The version field in the header determines parsing. v1 states skip the instance ID field. Deserialization must handle both.
- **Discovery file location:** Use `~/Library/Application Support/` on macOS, `$XDG_CONFIG_HOME/` (defaulting to `~/.config/`) on Linux.
- **File locking:** `flock()` is available on both macOS and Linux. Write via temp file + atomic rename to prevent partial reads.

## Success Metrics

- All unit tests pass on macOS and Linux (`ctest` exit code 0)
- Thread Sanitizer reports zero data races in concurrency tests
- State format round-trip tests cover valid, corrupt, and boundary-condition inputs
- Two wrapper instances in the same DAW session each have independent MCP servers on different ports
- An agent can discover all active instances via `list_instances` or the discovery file and connect to each one individually
- No regressions in single-instance functionality after the multi-instance refactor

## Open Questions

- Should the `InstanceRegistry` use a monotonically incrementing integer ID, a UUID, or a human-readable name (e.g., track name if available)?
- Should the discovery file include the DAW process name or session name for disambiguation?
- How should the `.mcp.json` project config be updated to support discovery-based connection (multiple dynamic ports)?
- Should we provide a helper CLI tool or script that reads the discovery file and outputs MCP connection info?
- What is the minimum Linux distribution target (Ubuntu 22.04? Fedora 38?) and should we test in Docker?
