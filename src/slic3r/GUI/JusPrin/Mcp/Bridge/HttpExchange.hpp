#pragma once
#include "../McpProtocol.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <functional>

namespace Slic3r::GUI::JusPrin::Mcp::Bridge {

// One request owns one connection. All methods/callbacks run on the bridge's
// io_context thread; closing it is the HTTP cancellation signal.
class HttpExchange : public std::enable_shared_from_this<HttpExchange>
{
public:
    using Message = std::function<void(nlohmann::json, bool)>;
    using Failure = std::function<void(const std::string&)>;
    HttpExchange(boost::asio::io_context& io, unsigned short port, HttpRequest request,
                 std::chrono::milliseconds timeout, Message message, Failure failure);
    void start();
    void cancel();
private:
    void read_body();
    void consume(std::string_view bytes);
    void deliver(const std::string& bytes);
    void fail(const std::string& reason);
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::steady_timer m_deadline;
    unsigned short m_port;
    std::chrono::milliseconds m_timeout;
    boost::beast::http::request<boost::beast::http::string_body> m_request;
    boost::beast::http::response_parser<boost::beast::http::buffer_body> m_parser;
    boost::beast::flat_buffer m_buffer;
    std::array<char, 8192> m_chunk{};
    std::string m_body, m_event;
    nlohmann::json m_id;
    Message m_message;
    Failure m_failure;
    bool m_closed{false}, m_streaming{false};
};

HttpRequest wire_request(const nlohmann::json& rpc);

} // namespace Slic3r::GUI::JusPrin::Mcp::Bridge
