# VST3 MCP Wrapper

A VST3 plugin that wraps any third-party VST3 plugin and exposes its parameters via [MCP (Model Context Protocol)](https://modelcontextprotocol.io). Load it in your DAW like a normal effect, host another plugin inside it, and let an AI agent read and control every parameter remotely.

```
DAW  <-->  VST3MCPWrapper  <-->  Your Plugin (e.g. EQ, compressor, reverb)
                |
                +--> MCP Server (127.0.0.1:8771)
                          |
                      AI Agent
```

> **Alpha release** — macOS only, single instance. See [Limitations](#limitations).

## Prerequisites

- macOS (Apple Silicon or Intel)
- CMake 3.25+
- C++20 compiler (Xcode Command Line Tools or full Xcode)
- A DAW that supports VST3 plugins (REAPER, Logic Pro, Ableton Live, Bitwig, etc.)

## Build

```bash
git clone <repo-url>
cd vst3mcpwrapper
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target VST3MCPWrapper
```

The built plugin is at `build/VST3/Debug/VST3MCPWrapper.vst3`.

All dependencies (VST3 SDK, cpp-mcp) are fetched automatically — no manual downloads needed. First build takes a few minutes; subsequent builds are fast.

## Setup

### 1. Register the plugin with your DAW

Point your DAW's VST3 scan path to the build output directory:

| DAW | How to add the scan path |
|---|---|
| **REAPER** | Preferences → Plug-ins → VST → Edit path list → Add `<repo>/build/VST3/Debug` |
| **Logic Pro** | Uses system paths only — create a symlink: `ln -s <repo>/build/VST3/Debug/VST3MCPWrapper.vst3 ~/Library/Audio/Plug-Ins/VST3/` |
| **Ableton Live** | Preferences → Plug-ins → Custom VST3 Folder → set to `<repo>/build/VST3/Debug` |
| **Bitwig** | Settings → Locations → Add VST3 path → `<repo>/build/VST3/Debug` |

After adding the path, rescan plugins in your DAW.

### 2. Load the plugin

Insert **VST3 MCP Wrapper** as an effect on any track. You'll see a drop zone that says "Drop a .vst3 plugin here".

### 3. Host a plugin

Drag any `.vst3` bundle from Finder onto the drop zone. Common locations for VST3 plugins:

```
/Library/Audio/Plug-Ins/VST3/
~/Library/Audio/Plug-Ins/VST3/
```

The hosted plugin's GUI appears in place of the drop zone. Audio, MIDI, and parameter automation pass through transparently — the wrapper is invisible to the signal chain.

## Connecting an AI Agent

When the plugin is loaded in a DAW, it starts an MCP server on `http://127.0.0.1:8771`. Any MCP-compatible client can connect to it.

### Claude Code

The repo includes an `.mcp.json` that configures the plugin as a project-level MCP server. When you open the repo in Claude Code and the plugin is running in a DAW:

```bash
cd vst3mcpwrapper
claude   # Claude Code can now use list_parameters, set_parameter, etc.
```

### Custom MCP client

Connect via SSE transport:

1. **GET** `http://127.0.0.1:8771/sse` — opens an SSE stream. The server sends an `endpoint` event with the message URL.
2. **POST** to the message URL (e.g. `/message?session_id=...`) with JSON-RPC requests.
3. Responses arrive as `message` events on the SSE stream.

### Available MCP tools

| Tool | Description |
|---|---|
| `list_parameters` | List all hosted plugin parameters (id, title, value, units, etc.) |
| `get_parameter` | Get a parameter's current value by ID |
| `set_parameter` | Set a parameter's normalized value (0.0–1.0) by ID |
| `list_available_plugins` | List all VST3 plugins installed on the system |
| `load_plugin` | Load a VST3 plugin by file path |
| `unload_plugin` | Unload the current plugin, return to drop zone |
| `get_loaded_plugin` | Get the currently loaded plugin's path |

### Example: curl

```bash
# Connect and get session
curl -s -N http://127.0.0.1:8771/sse
# → event: endpoint
# → data: /message?session_id=<id>

# In another terminal, using the session ID from above:
SESSION="<id>"

# Initialize
curl -s "http://127.0.0.1:8771/message?session_id=$SESSION" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"0.1"}}}'

# Send initialized notification
curl -s "http://127.0.0.1:8771/message?session_id=$SESSION" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'

# List parameters
curl -s "http://127.0.0.1:8771/message?session_id=$SESSION" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_parameters","arguments":{}}}'

# Set a parameter (Mix to 50%)
curl -s "http://127.0.0.1:8771/message?session_id=$SESSION" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"set_parameter","arguments":{"id":1298757752,"value":0.5}}}'

# Responses arrive on the SSE stream (first terminal)
```

## How It Works

The wrapper is a dual-component VST3 plugin:

- **Processor** — owns the hosted plugin's audio component. Passes audio and MIDI through. Drains a parameter change queue on each audio buffer and injects changes into the hosted plugin's processing.
- **Controller** — owns the hosted plugin's edit controller. Runs the MCP server. Routes GUI parameter changes through the same queue. Returns the hosted plugin's GUI (or a drop zone when empty).
- **HostedPluginModule** — shared singleton that holds the loaded module, factory, and a thread-safe parameter change queue.

Parameter changes from MCP and the hosted GUI both flow through the same lock-free queue and are applied on the audio thread, ensuring consistent behavior regardless of the source.

The hosted plugin's state is persisted with the DAW session — the wrapper saves the plugin path and the hosted plugin's own state, and restores both on session load.

## Limitations

This is an alpha release with the following known limitations:

- **macOS only** — uses native Cocoa views and `dispatch_async` for thread coordination
- **Single instance** — only one wrapper instance can run at a time (singleton architecture, fixed MCP port). Loading a second instance in the same DAW session will conflict.
- **Fixed MCP port** — the server always binds to port 8771. No error handling if the port is already in use.
- **No preset management** — you can't list or load the hosted plugin's presets via MCP yet
- **Not code-signed** — you may need to allow unsigned plugins in your DAW or system settings

## Project Structure

```
source/
  processor.h/cpp      Audio processor, hosted component lifecycle, state format
  controller.h/cpp     Edit controller, MCP server, plugin loading, view management
  hostedplugin.h/cpp   Shared singleton, parameter queue, module/factory management
  wrapperview.h/mm     Drop zone NSView (Obj-C++), IPlugView/IPlugFrame proxy
  pluginids.h          FUID definitions
  version.h            Version strings
  factory.cpp          VST3 factory registration
resource/
  Info.plist.in        macOS bundle template
```

See [CLAUDE.md](CLAUDE.md) for developer conventions and [ARCHITECTURE.md](ARCHITECTURE.md) for the full technical design and multi-instance roadmap.

## License

This project uses the [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) under the Steinberg VST3 License and [cpp-mcp](https://github.com/hkr04/cpp-mcp) under the MIT License.
