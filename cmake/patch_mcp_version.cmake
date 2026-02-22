# Patch cpp-mcp to advertise MCP protocol version 2025-03-26.
#
# cpp-mcp hardcodes "2024-11-05" and does a strict equality check, rejecting
# clients that send the current spec version "2025-03-26" (including Claude Code).
# The 2025-03-26 spec is backward-compatible at the JSON-RPC layer — new features
# (batching, session IDs, resumability) are optional and unused over SSE transport.
#
# See: https://github.com/hkr04/cpp-mcp/issues/10

file(READ "${FILE}" content)
string(REPLACE
    "\"2024-11-05\""
    "\"2025-03-26\""
    content "${content}"
)
file(WRITE "${FILE}" "${content}")
