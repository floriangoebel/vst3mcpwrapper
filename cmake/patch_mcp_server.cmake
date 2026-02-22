# Patch cpp-mcp server to handle OAuth discovery requests gracefully.
#
# Claude Code's MCP client probes /.well-known/oauth-protected-resource before
# connecting via SSE. cpp-mcp returns 404 with an empty body, which Claude Code
# fails to parse ("Invalid OAuth error response"). Adding a proper JSON 404
# response lets the client proceed to the SSE connection.

file(READ "${FILE}" content)

# Insert well-known endpoint handler right after the CORS handler in start()
string(REPLACE
    "    // Setup JSON-RPC endpoint"
    "    // Handle OAuth discovery probes (Claude Code checks this before SSE)
    http_server_->Get(\"/.well-known/oauth-authorization-server\", [](const httplib::Request&, httplib::Response& res) {
        res.set_header(\"Content-Type\", \"application/json\");
        res.status = 404;
        res.set_content(\"{}\", \"application/json\");
    });
    http_server_->Get(\"/.well-known/oauth-protected-resource\", [](const httplib::Request&, httplib::Response& res) {
        res.set_header(\"Content-Type\", \"application/json\");
        res.status = 404;
        res.set_content(\"{}\", \"application/json\");
    });

    // Setup JSON-RPC endpoint"
    content "${content}"
)

file(WRITE "${FILE}" "${content}")
