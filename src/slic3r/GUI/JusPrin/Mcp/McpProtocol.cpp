#include "McpProtocol.hpp"
#include "McpDiscoveryFile.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Mcp {
namespace {
using nlohmann::json;

std::string header(const HttpRequest& request, const char* name)
{
    const auto found = request.headers.find(name);
    return found == request.headers.end() ? std::string() : found->second;
}

std::optional<std::string> decoded_name(const std::string& value)
{
    if (value.compare(0, 9, "=?base64?") != 0) {
        for (unsigned char c : value)
            if ((c < 0x20 && c != '\t') || c > 0x7e) return std::nullopt;
        return value;
    }
    if (value.size() < 15 || value.compare(value.size() - 2, 2, "?=") != 0)
        return std::nullopt;
    const std::string encoded = value.substr(9, value.size() - 11);
    if (encoded.size() % 4 != 0)
        return std::nullopt;
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        unsigned bits = 0, padding = 0;
        for (unsigned j = 0; j < 4; ++j) {
            const char c = encoded[i + j];
            if (c == '=') {
                if (i + 4 != encoded.size() || j < 2) return std::nullopt;
                ++padding;
                bits <<= 6;
            } else {
                const auto digit = alphabet.find(c);
                if (padding || digit == std::string_view::npos) return std::nullopt;
                bits = (bits << 6) | unsigned(digit);
            }
        }
        // Unused low bits must be zero: reject alternate encodings of a name.
        if ((padding == 2 && (bits & 0xffff)) || (padding == 1 && (bits & 0xff)))
            return std::nullopt;
        decoded += char(bits >> 16);
        if (padding < 2) decoded += char(bits >> 8);
        if (padding == 0) decoded += char(bits);
    }
    return decoded;
}

bool valid_id(const json& id) { return id.is_string() || id.is_number_integer(); }

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t");
    return first == std::string::npos ? std::string() : value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

bool accepts(const std::string& accept, const std::string& media_type)
{
    std::size_t begin = 0;
    while (begin < accept.size()) {
        const auto end = accept.find(',', begin);
        const std::string item = lowercase(trim(accept.substr(begin, end - begin)));
        const auto semicolon = item.find(';');
        if (trim(item.substr(0, semicolon)) == media_type) {
            const auto quality_at = item.find("q=");
            if (quality_at == std::string::npos) return true;
            const std::string text = trim(item.substr(quality_at + 2, item.find(';', quality_at) - quality_at - 2));
            const auto quality = json::parse(text, nullptr, false);
            if (quality.is_number() && quality.get<double>() > 0 && quality.get<double>() <= 1) return true;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return false;
}
}

json rpc_error(const json& id, int code, const std::string& message, json data)
{
    json result{{"jsonrpc", "2.0"}, {"error", {{"code", code}, {"message", message}}}};
    if (!id.is_null()) result["id"] = id;
    if (!data.is_null()) result["error"]["data"] = std::move(data);
    return result;
}

json rpc_result(const json& id, json result)
{
    result["resultType"] = "complete";
    result["_meta"]["io.modelcontextprotocol/serverInfo"] = {{"name", "jusprin"}, {"version", mcp_build_version()}};
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

ParsedRequest parse_request(const HttpRequest& http, const std::string& origin)
{
    json id;
    auto error = [&](unsigned status, int code, const std::string& message, json data = nullptr) {
        return ParsedRequest{std::nullopt, {status, rpc_error(id, code, message, std::move(data))}};
    };
    if (http.headers.count("origin") && header(http, "origin") != origin)
        return error(403, -32600, "Origin is not approved for this local endpoint.");
    if (http.target != "/mcp") return error(404, -32600, "MCP endpoint not found.");
    if (http.method != "POST") return error(405, -32600, "Use POST. Supported protocol: 2026-07-28.");
    if (lowercase(trim(header(http, "content-type").substr(0, header(http, "content-type").find(';')))) != "application/json")
        return error(415, -32600, "Content-Type must be application/json.");
    if (!accepts(header(http, "accept"), "application/json") || !accepts(header(http, "accept"), "text/event-stream"))
        return error(406, -32600, "Accept must include application/json and text/event-stream.");
    if (http.body.size() > kBodyLimit) return error(413, -32600, "Request body exceeds the limit.");

    // Reject excessive nesting before invoking the recursive DOM parser. This
    // scan counts structure only outside strings; JSON syntax is parsed below.
    std::size_t depth = 0;
    bool quoted = false, escaped = false;
    for (char c : http.body) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > kDepthLimit) return error(400, -32600, "JSON nesting exceeds the limit.");
        } else if ((c == '}' || c == ']') && depth > 0) --depth;
    }
    const json body = json::parse(http.body, nullptr, false);
    if (body.is_discarded()) return error(400, -32700, "Malformed JSON.");
    if (!body.is_object()) return error(400, -32600, "Expected a single JSON-RPC request.");
    if (body.contains("id") && valid_id(body["id"])) id = body["id"];
    if (body.value("jsonrpc", json()) != "2.0" || id.is_null() ||
        !body.contains("method") || !body["method"].is_string() || body.contains("result") || body.contains("error"))
        return error(400, -32600, "Expected a JSON-RPC request with a string or integer ID.");
    const std::string method = body["method"].get<std::string>();
    if (header(http, "mcp-method") != method || header(http, "mcp-protocol-version").empty())
        return error(400, -32020, "Missing or mismatched MCP headers. Supported protocol: 2026-07-28.");
    const json params = body.value("params", json());
    if (!params.is_object() || !params.contains("_meta") || !params["_meta"].is_object())
        return error(400, -32602, "Per-request protocol metadata is required.");
    const json& meta = params["_meta"];
    const json version = meta.value("io.modelcontextprotocol/protocolVersion", json());
    if (!version.is_string() || !meta.value("io.modelcontextprotocol/clientCapabilities", json()).is_object())
        return error(400, -32602, "Protocol version and client capabilities are required.");
    if (header(http, "mcp-protocol-version") != version.get<std::string>())
        return error(400, -32020, "MCP-Protocol-Version does not match request metadata.");
    if (version != kProtocolVersion)
        return error(400, -32022, "Unsupported protocol version.", {{"supported", json::array({kProtocolVersion})}, {"requested", version}});
    if (meta.contains("progressToken") && !valid_id(meta["progressToken"]))
        return error(400, -32602, "progressToken must be a string or integer.");
    if (meta.contains("io.modelcontextprotocol/clientInfo")) {
        const auto& info = meta["io.modelcontextprotocol/clientInfo"];
        if (!info.is_object() || !info.value("name", json()).is_string() || !info.value("version", json()).is_string())
            return error(400, -32602, "Invalid clientInfo.");
    }
    if (method != "server/discover" && method != "tools/list" && method != "tools/call")
        return error(404, -32601, "Method not found. Supported protocol: 2026-07-28.");
    if (method == "tools/call") {
        if (!params.value("name", json()).is_string()) return error(400, -32602, "Tool name must be a string.");
        const auto name = decoded_name(header(http, "mcp-name"));
        if (!http.headers.count("mcp-name") || !name || *name != params["name"].get<std::string>())
            return error(400, -32020, "Missing, malformed, or mismatched Mcp-Name header.");
        if (params.contains("arguments") && !params["arguments"].is_object())
            return error(400, -32602, "Tool arguments must be an object.", {{"code", "invalid_arguments"}});
    }
    return {Request{id, method, params}, {}};
}

Reply discovery(const Request& request)
{
    return {200, rpc_result(request.id, {{"supportedVersions", json::array({kProtocolVersion})},
                                         {"capabilities", {{"tools", {{"listChanged", false}}}}},
                                         {"ttlMs", 0},
                                         {"cacheScope", "private"},
                                         {"instructions", "Inspect the live JusPrin workspace before choosing object IDs. Mutations wait for approval in the JusPrin Agent panel. A workspace_unavailable error means you should open JusPrin and a project. Closing a response cancels pending work."}})};
}

Reply list_tools(const Request& request, std::size_t page_size)
{
    const auto definitions = Agent::ToolRegistry::instance().exposed(Agent::ToolExposure::Mcp);
    std::size_t offset = 0;
    if (request.params.contains("cursor")) {
        const json& cursor = request.params["cursor"];
        if (!cursor.is_string()) return {400, rpc_error(request.id, -32602, "Invalid catalog cursor.")};
        const std::string text = cursor.get<std::string>();
        constexpr std::string_view prefix = "jusprin-v1:";
        if (text.compare(0, prefix.size(), prefix) != 0)
            return {400, rpc_error(request.id, -32602, "Invalid catalog cursor.")};
        const auto parsed = std::from_chars(text.data() + prefix.size(), text.data() + text.size(), offset);
        if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || offset >= definitions.size())
            return {400, rpc_error(request.id, -32602, "Invalid catalog cursor.")};
    }
    json tools = json::array();
    const std::size_t end = std::min(definitions.size(), offset + std::clamp<std::size_t>(page_size, 1, 25));
    for (; offset < end; ++offset) {
        const Agent::ToolDefinition& tool = definitions[offset];
        const bool read_only = tool.action_class == Agent::ActionClass::ReadOnly;
        tools.push_back({{"name", tool.name}, {"title", tool.title}, {"description", tool.description},
                         {"inputSchema", tool.input_schema}, {"outputSchema", tool.output_schema},
                         {"annotations", {{"readOnlyHint", read_only}, {"destructiveHint", tool.action_class == Agent::ActionClass::Destructive},
                                           {"idempotentHint", read_only}, {"openWorldHint", false}}}});
    }
    json result{{"tools", std::move(tools)}, {"ttlMs", 0}, {"cacheScope", "private"}};
    if (end < definitions.size()) result["nextCursor"] = "jusprin-v1:" + std::to_string(end);
    return {200, rpc_result(request.id, std::move(result))};
}

json tool_error(const std::string& code, const std::string& message, json details)
{
    return {{"error", {{"code", code}, {"message", message}, {"details", std::move(details)}}}};
}

json tool_result(json content, bool is_error)
{
    return {{"resultType", "complete"}, {"isError", is_error},
            {"content", json::array({{{"type", "text"}, {"text", content.dump()}}})},
            {"structuredContent", std::move(content)}};
}

json activity_result(const Agent::ToolActivity& activity, const Workspace::WorkspaceSnapshot& snapshot)
{
    using Agent::ToolState;
    json result;
    if (activity.state == ToolState::Succeeded) {
        const auto content = json::parse(activity.result_json);
        const auto* definition = Agent::ToolRegistry::instance().find(activity.tool);
        if (!definition || !Agent::ToolRegistry::instance().validate_output(*definition, content))
            throw std::logic_error("Tool result violates its canonical output schema: " + activity.tool);
        result = tool_result(content);
    } else {
        std::string code = "execution_failed", message = "Tool execution failed.";
        if (activity.state == ToolState::Rejected) { code = "approval_rejected"; message = "The user rejected this action in JusPrin."; }
        else if (activity.state == ToolState::Cancelled) { code = "cancelled"; message = "The action was cancelled."; }
        else if (activity.error) { code = activity.error->code; message = activity.error->message; }
        json details = activity.error ? json::parse(activity.error->details_json) : json::object();
        if (code == "stale_revision" || code == "stale_id") {
            std::string expected_session = std::to_string(activity.session);
            if (code == "stale_id") {
                const json arguments = json::parse(activity.arguments_json);
                expected_session = arguments.value("sessionId", expected_session);
            }
            code = "stale_workspace";
            details = {{"expectedSessionId", expected_session}, {"expectedRevision", activity.expected_revision},
                       {"currentSessionId", std::to_string(snapshot.session.value())}, {"currentRevision", snapshot.revision}};
        } else if (code == "unavailable_operation") code = "workspace_unavailable";
        result = tool_result(tool_error(code, message, std::move(details)), true);
    }
    result["_meta"]["io.jusprin/activity"] = {{"actionId", activity.action_id},
                                              {"sessionId", std::to_string(snapshot.session.value())},
                                              {"revision", snapshot.revision}};
    return result;
}

std::string sse_event(const json& message) { return "event: message\ndata: " + message.dump() + "\n\n"; }
} // namespace Slic3r::GUI::JusPrin::Mcp
