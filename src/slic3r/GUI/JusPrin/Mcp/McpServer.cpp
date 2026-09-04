#include "McpServer.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <array>
#include <cctype>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace Slic3r::GUI::JusPrin::Mcp {
namespace net = boost::asio;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;
using Error = boost::system::error_code;

struct McpServer::Impl
{
    struct Connection;
    ServerOptions options;
    net::io_context io;
    tcp::acceptor acceptor{io};
    std::thread worker;
    std::string origin, url;
    unsigned short port{0};
    bool stopping{false}; // worker-only
    std::uint64_t next_connection{1};
    std::map<std::uint64_t, std::shared_ptr<Connection>> connections;
    std::mutex inbox_mutex;
    std::vector<std::shared_ptr<PendingCall>> inbox;

    explicit Impl(ServerOptions config);
    void accept();
    void stop();
};

struct McpServer::Impl::Connection : std::enable_shared_from_this<Connection>
{
    Impl& owner;
    const std::uint64_t id;
    tcp::socket socket;
    net::steady_timer deadline;
    boost::beast::flat_buffer input{kBodyLimit + kHeaderLimit};
    http::request_parser<http::string_body> parser;
    std::shared_ptr<PendingCall> call;
    std::deque<std::string> output;
    std::array<char, 1> disconnect_buffer{};
    bool closed{false}, streaming{false}, terminal{false};

    Connection(Impl& server, tcp::socket accepted, std::uint64_t connection_id)
        : owner(server), id(connection_id), socket(std::move(accepted)), deadline(owner.io)
    {
        parser.body_limit(kBodyLimit);
        parser.header_limit(kHeaderLimit);
    }

    void arm_deadline(std::chrono::milliseconds duration)
    {
        deadline.expires_after(duration);
        deadline.async_wait([self = shared_from_this()](Error error) {
            if (!error) self->close(); // request timeout cancels pending approval too
        });
    }

    void start()
    {
        arm_deadline(owner.options.header_timeout);
        http::async_read(socket, input, parser, [self = shared_from_this()](Error error, std::size_t) {
            if (error) {
                if (error == http::error::body_limit || error == http::error::header_limit) {
                    const unsigned status = error == http::error::body_limit ? 413 : 431;
                    self->reply({status, rpc_error(nullptr, -32600, "HTTP request exceeds the limit.")});
                } else if (error != net::error::operation_aborted && error != http::error::end_of_stream) {
                    self->reply({400, rpc_error(nullptr, -32600, "Malformed HTTP request.")});
                } else self->close();
                return;
            }
            self->dispatch();
        });
    }

    void dispatch()
    {
        HttpRequest request;
        auto& wire = parser.get();
        request.method = std::string(wire.method_string());
        request.target = std::string(wire.target());
        request.body = std::move(wire.body());
        for (const auto& field : wire.base()) {
            std::string name(field.name_string());
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (!request.headers.emplace(name, std::string(field.value())).second) {
                reply({400, rpc_error(nullptr, -32020, "Duplicate HTTP header.")});
                return;
            }
        }
        auto parsed = parse_request(request, owner.origin);
        if (!parsed.request) { reply(std::move(parsed.error)); return; }
        Request rpc = std::move(*parsed.request);
        if (rpc.method == "server/discover") { reply(discovery(rpc)); return; }
        if (rpc.method == "tools/list") { reply(list_tools(rpc)); return; }
        const auto* definition = Agent::ToolRegistry::instance().find(rpc.params["name"].get<std::string>());
        if (!definition || !Agent::has_exposure(definition->exposure, Agent::ToolExposure::Mcp)) {
            reply({400, rpc_error(rpc.id, -32602, "This tool is not exposed by JusPrin MCP.", {{"code", "unknown_tool"}})});
            return;
        }
        streaming = Agent::approval_required(definition->action_class);
        call = std::make_shared<PendingCall>();
        call->connection_id = id;
        call->request = std::move(rpc);
        arm_deadline(owner.options.request_timeout);
        if (streaming)
            enqueue("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n"
                    "X-Accel-Buffering: no\r\nConnection: close\r\n\r\n");
        // Observe disconnect while approval is pending, even when there are no
        // progress notifications to write. No pipelined messages are accepted.
        socket.async_read_some(net::buffer(disconnect_buffer), [self = shared_from_this()](Error, std::size_t) {
            self->close();
        });
        std::lock_guard<std::mutex> lock(owner.inbox_mutex);
        if (owner.inbox.size() == owner.options.max_connections) { close(); return; }
        owner.inbox.push_back(call);
    }

    void reply(Reply response)
    {
        if (closed || terminal) return;
        const std::string body = response.body.dump();
        const auto status = static_cast<http::status>(response.status);
        std::string headers = "HTTP/1.1 " + std::to_string(response.status) + " " + std::string(http::obsolete_reason(status)) +
                              "\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n";
        if (response.status == 405) headers += "Allow: POST\r\n";
        terminal = true;
        enqueue(headers + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    }

    void message(nlohmann::json value, bool final)
    {
        if (closed || terminal) return;
        if (streaming) {
            terminal = final;
            enqueue(sse_event(value));
        } else if (final) reply({200, std::move(value)});
    }

    void enqueue(std::string bytes)
    {
        if (closed) return;
        // Sparse state transitions plus one final result fit well below this.
        // A slow reader must never accumulate an unbounded outgoing queue.
        if (output.size() >= 8 || bytes.size() > 256 * 1024) { close(); return; }
        const bool idle = output.empty();
        output.push_back(std::move(bytes));
        if (idle) write();
    }

    void write()
    {
        net::async_write(socket, net::buffer(output.front()), [self = shared_from_this()](Error error, std::size_t) {
            if (error || self->closed) { self->close(); return; }
            self->output.pop_front();
            if (!self->output.empty()) self->write();
            else if (self->terminal) self->close();
        });
    }

    void close()
    {
        if (closed) return;
        closed = true;
        if (call) call->cancelled.store(true);
        deadline.cancel();
        Error ignored;
        socket.shutdown(tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
        // Keep the outgoing buffer alive until any in-flight write completes.
        owner.connections.erase(id);
    }
};

McpServer::Impl::Impl(ServerOptions config) : options(config)
{
    if (options.max_connections == 0 || options.max_connections > 64 || options.request_timeout.count() <= 0 ||
        options.header_timeout.count() <= 0) throw std::invalid_argument("Invalid MCP server limits.");
    // No hostname resolution and no reuse-address fallback on collision.
    acceptor.open(tcp::v4());
    Error error;
    acceptor.bind({net::ip::make_address_v4("127.0.0.1"), options.port}, error);
    if (error == net::error::address_in_use && options.port != 0 && options.fallback_to_ephemeral)
        acceptor.bind({net::ip::make_address_v4("127.0.0.1"), 0}, error);
    if (error) throw boost::system::system_error(error, "Could not bind the local MCP listener");
    acceptor.listen(int(options.max_connections));
    port = acceptor.local_endpoint().port();
    origin = "http://127.0.0.1:" + std::to_string(port);
    url = origin + "/mcp";
    accept();
    worker = std::thread([this] { io.run(); });
}

void McpServer::Impl::accept()
{
    acceptor.async_accept([this](Error error, tcp::socket socket) {
        if (stopping) return;
        if (!error && connections.size() < options.max_connections) {
            const auto id = next_connection++;
            auto connection = std::make_shared<Connection>(*this, std::move(socket), id);
            connections.emplace(id, connection);
            connection->start();
        }
        // Excess connections are closed without allocating a request/parser.
        if (error != net::error::operation_aborted) accept();
    });
}

void McpServer::Impl::stop()
{
    if (!worker.joinable()) return;
    net::post(io, [this] {
        stopping = true;
        Error ignored;
        acceptor.close(ignored);
        while (!connections.empty()) {
            auto connection = connections.begin()->second;
            connection->close();
        }
    });
    worker.join();
}

McpServer::McpServer(ServerOptions options) : m_impl(std::make_unique<Impl>(options)) {}
McpServer::~McpServer() { stop(); }
const std::string& McpServer::url() const { return m_impl->url; }
unsigned short McpServer::port() const { return m_impl->port; }
void McpServer::stop() { m_impl->stop(); }

std::vector<std::shared_ptr<PendingCall>> McpServer::take_calls()
{
    std::lock_guard<std::mutex> lock(m_impl->inbox_mutex);
    std::vector<std::shared_ptr<PendingCall>> calls;
    calls.swap(m_impl->inbox);
    return calls;
}

void McpServer::send(std::uint64_t id, nlohmann::json message, bool terminal)
{
    net::post(m_impl->io, [impl = m_impl.get(), id, value = std::move(message), terminal]() mutable {
        const auto found = impl->connections.find(id);
        if (found != impl->connections.end()) found->second->message(std::move(value), terminal);
    });
}
} // namespace Slic3r::GUI::JusPrin::Mcp
