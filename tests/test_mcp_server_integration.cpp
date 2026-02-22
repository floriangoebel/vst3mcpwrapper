/**
 * @file test_mcp_server_integration.cpp
 * @brief Integration tests for the MCP server on Linux.
 *
 * Verifies that the cpp-mcp server library works correctly: starting,
 * accepting SSE connections, handling tool calls via HTTP POST, stopping
 * cleanly, and releasing the port for rebinding.
 *
 * Uses port 18771 to avoid conflicts with the real plugin server (8771).
 */

#include <gtest/gtest.h>

#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_message.h"
#include "mcp_sse_client.h"
#include "httplib.h"

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

using json = mcp::json;

static constexpr int kTestPort = 18771;
static constexpr const char* kTestHost = "127.0.0.1";
static const std::string kTestURL = "http://127.0.0.1:18771";

// ---------------------------------------------------------------------------
// Helper: create a configured server with a simple echo tool registered
// ---------------------------------------------------------------------------
static std::unique_ptr<mcp::server> createTestServer() {
    mcp::server::configuration conf;
    conf.host = kTestHost;
    conf.port = kTestPort;
    conf.name = "TestMCPServer";
    conf.version = "0.1.0";
    auto srv = std::make_unique<mcp::server>(conf);

    // Register a simple "echo" tool
    auto echoTool = mcp::tool_builder("echo")
        .with_description("Echoes the input message back")
        .with_string_param("message", "The message to echo", true)
        .build();

    srv->register_tool(echoTool,
        [](const json& params, const std::string& /*session_id*/) -> json {
            std::string msg = params["message"].get<std::string>();
            // The server wraps this return value into tool_result["content"],
            // so return just the content array.
            return json::array({{
                {"type", "text"},
                {"text", "echo: " + msg}
            }});
        });

    return srv;
}

// ---------------------------------------------------------------------------
// Test: SSE endpoint accepts connections and sends an endpoint event
// ---------------------------------------------------------------------------
TEST(MCPServerIntegration, SSEEndpointAcceptsConnections) {
    auto server = createTestServer();
    server->start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect to /sse and capture the first endpoint event
    httplib::Client client(kTestHost, kTestPort);
    client.set_read_timeout(3, 0);

    std::promise<std::string> endpointPromise;
    auto endpointFuture = endpointPromise.get_future();
    std::atomic<bool> gotEndpoint{false};

    std::thread sseThread([&]() {
        client.Get("/sse", [&](const char* data, size_t len) {
            std::string chunk(data, len);
            if (!gotEndpoint.load() && chunk.find("endpoint") != std::string::npos) {
                // Extract the data line value
                size_t dataPos = chunk.find("data: ");
                if (dataPos != std::string::npos) {
                    std::string value = chunk.substr(dataPos + 6);
                    value = value.substr(0, value.find("\r\n"));
                    gotEndpoint.store(true);
                    try { endpointPromise.set_value(value); } catch (...) {}
                }
            }
            return !gotEndpoint.load(); // stop after getting endpoint
        });
    });

    auto status = endpointFuture.wait_for(std::chrono::seconds(3));
    ASSERT_EQ(status, std::future_status::ready)
        << "Timed out waiting for SSE endpoint event";

    std::string endpoint = endpointFuture.get();
    EXPECT_FALSE(endpoint.empty());
    EXPECT_NE(endpoint.find("/message"), std::string::npos)
        << "Endpoint should contain '/message', got: " << endpoint;

    if (sseThread.joinable()) sseThread.join();

    server->stop();
}

// ---------------------------------------------------------------------------
// Test: Tool can be called via the MCP protocol (sse_client)
// ---------------------------------------------------------------------------
TEST(MCPServerIntegration, ToolCallViaMCP) {
    auto server = createTestServer();
    server->start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    mcp::sse_client client(kTestURL);
    bool initialized = client.initialize("TestClient", "1.0.0");
    ASSERT_TRUE(initialized) << "Failed to initialize MCP client";

    json result = client.call_tool("echo", {{"message", "hello linux"}});

    EXPECT_TRUE(result.contains("content"));
    EXPECT_FALSE(result["isError"].get<bool>());
    EXPECT_EQ(result["content"][0]["type"], "text");
    EXPECT_EQ(result["content"][0]["text"], "echo: hello linux");

    server->stop();
}

// ---------------------------------------------------------------------------
// Test: Server stops cleanly without hanging
// ---------------------------------------------------------------------------
TEST(MCPServerIntegration, ServerStopsCleanly) {
    auto server = createTestServer();
    server->start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto start = std::chrono::steady_clock::now();
    server->stop();
    auto elapsed = std::chrono::steady_clock::now() - start;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_LT(ms, 3000) << "Server stop took " << ms << "ms (expected < 3s)";
}

// ---------------------------------------------------------------------------
// Test: Port is released after stop — can rebind immediately
// ---------------------------------------------------------------------------
TEST(MCPServerIntegration, PortReleasedAfterStop) {
    // First server lifecycle
    {
        auto server = createTestServer();
        server->start(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server->stop();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Second server lifecycle on the same port — should succeed
    {
        auto server = createTestServer();
        server->start(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify it's actually serving
        mcp::sse_client client(kTestURL);
        bool initialized = client.initialize("TestClient", "1.0.0");
        EXPECT_TRUE(initialized) << "Could not connect to server on rebound port";

        server->stop();
    }
}

// ---------------------------------------------------------------------------
// Test: Multiple tool calls succeed in sequence
// ---------------------------------------------------------------------------
TEST(MCPServerIntegration, MultipleToolCalls) {
    auto server = createTestServer();
    server->start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    mcp::sse_client client(kTestURL);
    ASSERT_TRUE(client.initialize("TestClient", "1.0.0"));

    for (int i = 0; i < 5; ++i) {
        std::string msg = "call_" + std::to_string(i);
        json result = client.call_tool("echo", {{"message", msg}});
        EXPECT_EQ(result["content"][0]["text"], "echo: " + msg);
    }

    server->stop();
}

// ---------------------------------------------------------------------------
// Test: Binding to an already-in-use port doesn't crash
// ---------------------------------------------------------------------------
TEST(MCPServerIntegration, DuplicatePortBindDoesNotCrash) {
    auto server1 = createTestServer();
    server1->start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify server1 is running
    {
        mcp::sse_client client(kTestURL);
        ASSERT_TRUE(client.initialize("TestClient", "1.0.0"))
            << "First server should be running";
    }

    // Second server on same port — should not crash the process.
    // Depending on the platform and library, start() may throw, fail silently,
    // or succeed partially. The key assertion is: no crash, no std::terminate.
    {
        auto server2 = createTestServer();
        try {
            server2->start(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } catch (const std::exception&) {
            // Expected — duplicate bind may throw
        } catch (...) {
            // Expected — duplicate bind may throw
        }
        // Whether it started or failed, stopping should not crash
        server2->stop();

        // If we reach here, the duplicate bind didn't crash — test passes
    }

    // Original server should still be functional
    {
        mcp::sse_client client(kTestURL);
        bool initialized = client.initialize("TestClient", "1.0.0");
        EXPECT_TRUE(initialized)
            << "First server should still be operational after duplicate bind attempt";
    }

    server1->stop();
}

// ---------------------------------------------------------------------------
// Test: Server responds to ping
// ---------------------------------------------------------------------------
TEST(MCPServerIntegration, PingResponds) {
    auto server = createTestServer();
    server->start(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    mcp::sse_client client(kTestURL);
    ASSERT_TRUE(client.initialize("TestClient", "1.0.0"));

    bool pong = client.ping();
    EXPECT_TRUE(pong);

    server->stop();
}
