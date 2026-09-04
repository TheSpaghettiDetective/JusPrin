#include "HttpExchange.hpp"

namespace Slic3r::GUI::JusPrin::Mcp::Bridge {
namespace net = boost::asio;
namespace http = boost::beast::http;
using Error = boost::system::error_code;
using nlohmann::json;
namespace {
std::string encoded_name(const std::string& name)
{
    const bool plain = name.compare(0, 9, "=?base64?") != 0 &&
        std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 0x20 && c <= 0x7e; });
    if (plain) return name;
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "=?base64?";
    for (std::size_t i = 0; i < name.size(); i += 3) {
        unsigned bits = unsigned(static_cast<unsigned char>(name[i])) << 16;
        if (i + 1 < name.size()) bits |= unsigned(static_cast<unsigned char>(name[i + 1])) << 8;
        if (i + 2 < name.size()) bits |= static_cast<unsigned char>(name[i + 2]);
        out += alphabet[(bits >> 18) & 63]; out += alphabet[(bits >> 12) & 63];
        out += i + 1 < name.size() ? alphabet[(bits >> 6) & 63] : '=';
        out += i + 2 < name.size() ? alphabet[bits & 63] : '=';
    }
    return out + "?=";
}
}

HttpRequest wire_request(const json& rpc)
{
    HttpRequest wire;
    wire.body = rpc.dump();
    wire.headers = {{"content-type", "application/json"}, {"accept", "application/json, text/event-stream"},
                    {"mcp-method", rpc["method"].get<std::string>()}};
    const auto params = rpc.value("params", json::object());
    const auto meta = params.is_object() ? params.value("_meta", json::object()) : json::object();
    if (meta.is_object()) {
        const auto version = meta.value("io.modelcontextprotocol/protocolVersion", json());
        if (version.is_string()) wire.headers["mcp-protocol-version"] = version.get<std::string>();
    }
    if (params.is_object() && params.value("name", json()).is_string())
        wire.headers["mcp-name"] = encoded_name(params["name"].get<std::string>());
    return wire;
}

HttpExchange::HttpExchange(net::io_context& io, unsigned short port, HttpRequest request,
                           std::chrono::milliseconds timeout, Message message, Failure failure)
    : m_socket(io), m_deadline(io), m_port(port), m_timeout(timeout), m_message(std::move(message)), m_failure(std::move(failure))
{
    m_request.version(11);
    m_request.method(http::verb::post);
    m_request.target("/mcp");
    m_request.set(http::field::host, "127.0.0.1:" + std::to_string(port));
    m_request.set(http::field::connection, "close");
    for (const auto& field : request.headers) m_request.set(field.first, field.second);
    m_request.body() = std::move(request.body);
    m_id = json::parse(m_request.body())["id"];
    m_request.prepare_payload();
    m_parser.body_limit(512 * 1024);
    m_parser.header_limit(kHeaderLimit);
}

void HttpExchange::start()
{
    m_deadline.expires_after(m_timeout);
    m_deadline.async_wait([self = shared_from_this()](Error error) {
        if (!error) self->fail("The local MCP request timed out.");
    });
    m_socket.async_connect({net::ip::make_address_v4("127.0.0.1"), m_port}, [self = shared_from_this()](Error error) {
        if (self->m_closed) return;
        if (error) { self->fail(error.message()); return; }
        http::async_write(self->m_socket, self->m_request, [self](Error error, std::size_t) {
            if (self->m_closed) return;
            if (error) { self->fail(error.message()); return; }
            http::async_read_header(self->m_socket, self->m_buffer, self->m_parser, [self](Error error, std::size_t) {
                if (self->m_closed) return;
                if (error) { self->fail(error.message()); return; }
                const auto media = self->m_parser.get()[http::field::content_type];
                self->m_streaming = media.substr(0, 17) == "text/event-stream";
                if (!self->m_streaming && media.substr(0, 16) != "application/json") {
                    self->fail("The local endpoint did not return MCP JSON or SSE."); return;
                }
                self->read_body();
            });
        });
    });
}

void HttpExchange::read_body()
{
    if (m_closed) return;
    m_parser.get().body().data = m_chunk.data();
    m_parser.get().body().size = m_chunk.size();
    http::async_read_some(m_socket, m_buffer, m_parser, [self = shared_from_this()](Error error, std::size_t) {
        if (self->m_closed) return;
        const auto received = self->m_chunk.size() - self->m_parser.get().body().size;
        self->consume(std::string_view(self->m_chunk.data(), received));
        if (self->m_closed) return;
        if (error == http::error::need_buffer) error.clear();
        if (error) { self->fail(error.message()); return; }
        if (self->m_parser.is_done()) {
            if (!self->m_streaming) self->deliver(self->m_body);
            if (!self->m_closed) self->fail("MCP response ended without a terminal result.");
        } else self->read_body();
    });
}

void HttpExchange::consume(std::string_view bytes)
{
    m_body.append(bytes);
    if (m_body.size() + m_event.size() > 256 * 1024) { fail("MCP response exceeds the limit."); return; }
    if (!m_streaming) return;
    for (auto end = m_body.find('\n'); end != std::string::npos; end = m_body.find('\n')) {
        std::string line = m_body.substr(0, end);
        m_body.erase(0, end + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() && !m_event.empty()) {
            const auto event = std::move(m_event); m_event.clear();
            deliver(event);
            if (m_closed) return;
        } else if (line.compare(0, 5, "data:") == 0) {
            if (!m_event.empty()) m_event += '\n';
            m_event += line.substr(line.size() > 5 && line[5] == ' ' ? 6 : 5);
        }
    }
}

void HttpExchange::deliver(const std::string& bytes)
{
    const auto value = json::parse(bytes, nullptr, false);
    if (!value.is_object() || value.value("jsonrpc", json()) != "2.0") { fail("Invalid MCP response envelope."); return; }
    const bool terminal = value.contains("result") || value.contains("error");
    if (terminal) {
        if (value.contains("result") == value.contains("error") ||
            (value.contains("result") && !value["result"].is_object()) ||
            (value.contains("error") && (!value["error"].is_object() ||
                !value["error"].value("code", json()).is_number_integer() || !value["error"].value("message", json()).is_string()))) {
            fail("Invalid MCP result or error envelope."); return;
        }
    } else if (value.contains("id") || !value.value("method", json()).is_string() ||
               !value.value("params", json::object()).is_object()) {
        fail("Invalid MCP notification envelope."); return;
    }
    if (terminal && value.value("id", json()) != m_id) { fail("MCP response ID does not match the request."); return; }
    if (terminal) cancel();
    m_message(value, terminal);
}

void HttpExchange::cancel()
{
    if (m_closed) return;
    m_closed = true;
    m_deadline.cancel();
    Error ignored;
    m_socket.shutdown(net::ip::tcp::socket::shutdown_both, ignored);
    m_socket.close(ignored);
}

void HttpExchange::fail(const std::string& reason)
{
    if (m_closed) return;
    cancel();
    m_failure(reason);
}
} // namespace Slic3r::GUI::JusPrin::Mcp::Bridge
