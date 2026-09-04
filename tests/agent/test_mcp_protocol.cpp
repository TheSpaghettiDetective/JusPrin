#include <catch2/catch_all.hpp>
#include "slic3r/GUI/JusPrin/Mcp/McpProtocol.hpp"

using namespace Slic3r::GUI::JusPrin;
using nlohmann::json;

namespace {
Mcp::HttpRequest fixture(std::string method = "tools/list", json params = json::object())
{
    params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", Mcp::kProtocolVersion},
                       {"io.modelcontextprotocol/clientCapabilities", json::object()}};
    Mcp::HttpRequest http;
    http.headers = {{"content-type", "application/json"},
                    {"accept", "application/json, text/event-stream"},
                    {"mcp-protocol-version", Mcp::kProtocolVersion}, {"mcp-method", method}};
    if (params.contains("name")) http.headers["mcp-name"] = params["name"].get<std::string>();
    http.body = json{{"jsonrpc", "2.0"}, {"id", "request-1"}, {"method", method}, {"params", params}}.dump();
    return http;
}
Mcp::ParsedRequest parse(const Mcp::HttpRequest& http)
{
    return Mcp::parse_request(http, "http://127.0.0.1:12345");
}
}

TEST_CASE("MCP validates HTTP security before dispatch", "[mcp][protocol]")
{
    auto http = fixture();
    CHECK(parse(http).request.has_value()); // clientInfo is optional in 2026-07-28
    SECTION("no authentication required") { CHECK_FALSE(http.headers.count("authorization")); CHECK(parse(http).request.has_value()); }
    SECTION("foreign origin") { http.headers["origin"] = "https://evil.example"; CHECK(parse(http).error.status == 403); }
    SECTION("null origin") { http.headers["origin"] = "null"; CHECK(parse(http).error.status == 403); }
    SECTION("approved origin") { http.headers["origin"] = "http://127.0.0.1:12345"; CHECK(parse(http).request.has_value()); }
    SECTION("GET") { http.method = "GET"; CHECK(parse(http).error.status == 405); }
    SECTION("DELETE") { http.method = "DELETE"; CHECK(parse(http).error.status == 405); }
    SECTION("wrong path") { http.target = "/other"; CHECK(parse(http).error.status == 404); }
    SECTION("wrong media type") { http.headers["content-type"] = "text/plain"; CHECK(parse(http).error.status == 415); }
    SECTION("media type case and parameters") { http.headers["content-type"] = "Application/JSON; charset=UTF-8"; CHECK(parse(http).request.has_value()); }
    SECTION("accept both types") { http.headers["accept"] = "application/json"; CHECK(parse(http).error.status == 406); }
    SECTION("weighted accept") { http.headers["accept"] = "application/json;q=0.5, text/event-stream;q=1"; CHECK(parse(http).request.has_value()); }
    SECTION("unacceptable stream") { http.headers["accept"] = "application/json, text/event-stream;q=0"; CHECK(parse(http).error.status == 406); }
    SECTION("oversized") { http.body.assign(Mcp::kBodyLimit + 1, 'x'); CHECK(parse(http).error.status == 413); }
}

TEST_CASE("MCP rejects malformed envelopes metadata and mismatched headers", "[mcp][protocol]")
{
    auto http = fixture();
    int expected = -32602;
    const int scenario = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(scenario);
    if (scenario == 0) { http.body = "{"; expected = -32700; }
    if (scenario == 1) { http.body = "[]"; expected = -32600; }
    if (scenario == 2) { auto body = json::parse(http.body); body["id"] = nullptr; http.body = body.dump(); expected = -32600; }
    if (scenario == 3) { auto body = json::parse(http.body); body["params"]["_meta"].erase("io.modelcontextprotocol/clientCapabilities"); http.body = body.dump(); }
    if (scenario == 4) { auto body = json::parse(http.body); body["params"]["_meta"].erase("io.modelcontextprotocol/protocolVersion"); http.body = body.dump(); }
    if (scenario == 5) { auto body = json::parse(http.body); body["params"]["_meta"]["progressToken"] = false; http.body = body.dump(); }
    if (scenario == 6) { http.headers.erase("mcp-protocol-version"); expected = -32020; }
    if (scenario == 7) { http.headers["mcp-protocol-version"] = "2025-11-25"; expected = -32020; }
    if (scenario == 8) { http.headers["mcp-method"] = "tools/call"; expected = -32020; }
    if (scenario == 9) { http = fixture("initialize"); expected = -32601; }
    if (scenario == 10) {
        auto body = json::parse(http.body);
        body["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2025-11-25";
        http.body = body.dump(); http.headers["mcp-protocol-version"] = "2025-11-25"; expected = -32022;
    }
    if (scenario == 11) {
        json nested = json::object();
        for (unsigned i = 0; i < 40; ++i) nested = json{{"nested", nested}};
        http.body = nested.dump(); expected = -32600;
    }
    auto parsed = parse(http);
    REQUIRE_FALSE(parsed.request.has_value());
    CHECK(parsed.error.body["error"]["code"] == expected);
}

TEST_CASE("MCP decodes mirrored name headers before comparison", "[mcp][protocol]")
{
    auto http = fixture("tools/call", {{"name", "inspect_selection"}, {"arguments", json::object()}});
    http.headers["mcp-name"] = "=?base64?aW5zcGVjdF9zZWxlY3Rpb24=?=";
    CHECK(parse(http).request.has_value());
    http.headers["mcp-name"] = "=?base64?not!base64?=";
    CHECK(parse(http).error.body["error"]["code"] == -32020);
    http.headers.erase("mcp-name");
    CHECK(parse(http).error.body["error"]["code"] == -32020);
}

TEST_CASE("MCP base64 names require canonical padding and unused bits", "[mcp][protocol]")
{
    const auto pair = GENERATE(std::make_pair("a", "YQ=="), std::make_pair("ab", "YWI="),
                               std::make_pair("abc", "YWJj"), std::make_pair("abcd", "YWJjZA=="));
    auto http = fixture("tools/call", {{"name", pair.first}});
    http.headers["mcp-name"] = std::string("=?base64?") + pair.second + "?=";
    CHECK(parse(http).request.has_value());
    const auto invalid = GENERATE("YR==", "YWJ=", "YQ=A", "Y===", "=Q==", "YQ==YQ==", "YQ=", "Y Q=", "YQ\n=", "");
    http.headers["mcp-name"] = std::string("=?base64?") + invalid + "?=";
    CHECK_FALSE(parse(http).request.has_value());
}

TEST_CASE("MCP discovery and paged catalog are registry projections", "[mcp][registry]")
{
    const auto request = *parse(fixture()).request;
    auto discovered = Mcp::discovery(request).body;
    CHECK(discovered["result"]["resultType"] == "complete");
    CHECK(discovered["result"]["supportedVersions"] == json::array({Mcp::kProtocolVersion}));
    CHECK(discovered["result"]["capabilities"] == json{{"tools", {{"listChanged", false}}}});
    CHECK(discovered["result"]["ttlMs"] == 0);
    CHECK(discovered["result"]["cacheScope"] == "private");
    auto page_request = request;
    std::size_t count = 0;
    const auto definitions = Agent::ToolRegistry::instance().exposed(Agent::ToolExposure::Mcp);
    do {
        auto response = Mcp::list_tools(page_request, 1).body;
        CHECK(response["result"]["ttlMs"] == 0);
        CHECK(response["result"]["cacheScope"] == "private");
        REQUIRE(response["result"]["tools"].size() == 1);
        auto tool = response["result"]["tools"][0];
        const Agent::ToolDefinition& canonical = definitions.at(count++);
        CHECK(tool["name"] == canonical.name);
        CHECK(tool["inputSchema"] == canonical.input_schema);
        CHECK(tool["outputSchema"] == canonical.output_schema);
        CHECK(tool["annotations"]["readOnlyHint"] == (canonical.action_class == Agent::ActionClass::ReadOnly));
        if (!response["result"].contains("nextCursor")) break;
        page_request.params["cursor"] = response["result"]["nextCursor"];
    } while (count < 20);
    CHECK(count == definitions.size());
    page_request.params["cursor"] = "bogus";
    CHECK(Mcp::list_tools(page_request).body["error"]["code"] == -32602);
}

TEST_CASE("MCP terminal results preserve structured content and error identity", "[mcp][protocol]")
{
    Agent::ToolActivity activity;
    activity.tool = "inspect_selection";
    activity.action_id = "a-1";
    activity.state = Agent::ToolState::Succeeded;
    activity.result_json = R"({"selection":["cube"],"revision":4})";
    Workspace::WorkspaceSnapshot snapshot;
    snapshot.session = Workspace::ProjectSessionId(7);
    snapshot.revision = 4;
    auto result = Mcp::activity_result(activity, snapshot);
    CHECK_FALSE(result["isError"].get<bool>());
    CHECK(json::parse(result["content"][0]["text"].get<std::string>()) == result["structuredContent"]);
    activity.state = Agent::ToolState::Rejected;
    CHECK(Mcp::activity_result(activity, snapshot)["structuredContent"]["error"]["code"] == "approval_rejected");
    activity.state = Agent::ToolState::Cancelled;
    CHECK(Mcp::activity_result(activity, snapshot)["structuredContent"]["error"]["code"] == "cancelled");
    activity.state = Agent::ToolState::Failed;
    activity.error = Agent::ToolError{"stale_revision", "Changed"};
    activity.session = 7; activity.expected_revision = 2;
    auto stale = Mcp::activity_result(activity, snapshot)["structuredContent"]["error"];
    CHECK(stale["code"] == "stale_workspace");
    CHECK(stale["details"]["expectedRevision"] == 2);
    CHECK(stale["details"]["currentRevision"] == 4);
    activity.error = Agent::ToolError{"stale_id", "Old session"};
    activity.arguments_json = R"({"sessionId":"3","objectId":"42"})";
    CHECK(Mcp::activity_result(activity, snapshot)["structuredContent"]["error"]["details"]["expectedSessionId"] == "3");
    CHECK(Mcp::sse_event({{"id", 1}}) == "event: message\ndata: {\"id\":1}\n\n");
}
