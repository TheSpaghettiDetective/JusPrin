#pragma once

#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <map>
#include <optional>

namespace Slic3r::GUI::JusPrin::Mcp {

inline constexpr const char* kProtocolVersion = "2026-07-28";
inline constexpr unsigned short kPreferredPort = 47301;
inline constexpr std::size_t kBodyLimit = 64 * 1024;
inline constexpr std::size_t kHeaderLimit = 8 * 1024;
inline constexpr std::size_t kDepthLimit = 16;
inline constexpr std::size_t kClientNameLimit = 48; // bytes of a client's name kept in the change log

struct HttpRequest
{
    std::string method{"POST"};
    std::string target{"/mcp"};
    // Transport folds field names to lowercase and rejects duplicate fields.
    std::map<std::string, std::string> headers;
    std::string body;
};

struct Request
{
    nlohmann::json id;
    std::string method;
    nlohmann::json params;
};

struct Reply
{
    unsigned status{200};
    nlohmann::json body;
};

struct ParsedRequest
{
    std::optional<Request> request;
    Reply error;
};

// No live state, sockets, or GUI access in the wire codec.
ParsedRequest parse_request(const HttpRequest& http, const std::string& origin);
nlohmann::json rpc_error(const nlohmann::json& id, int code, const std::string& message,
                         nlohmann::json data = nullptr);
nlohmann::json rpc_result(const nlohmann::json& id, nlohmann::json result);
Reply discovery(const Request& request);
Reply list_tools(const Request& request, std::size_t page_size = 25);
nlohmann::json tool_error(const std::string& code, const std::string& message,
                          nlohmann::json details = nlohmann::json::object());
nlohmann::json activity_result(const Agent::ToolActivity& activity, const Workspace::WorkspaceSnapshot& snapshot);
nlohmann::json tool_result(nlohmann::json content, bool is_error = false);
std::string sse_event(const nlohmann::json& message);
// The name a request's client gives itself in its clientInfo, for the change
// log: its title, else its name, trimmed and capped. Empty when it gave none.
std::string mcp_client_name(const nlohmann::json& params);

} // namespace Slic3r::GUI::JusPrin::Mcp
