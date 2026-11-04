// Before anything that can pull in windows.h on Windows.
#include <boost/asio.hpp>

#include "ConnectionEvents.hpp"

#include "slic3r/Utils/bambu_networking.hpp"

#include <map>
#include <optional>

namespace Slic3r::GUI::JusPrin::Printers {

namespace {

std::map<std::string, ConnectionEvent>& events()
{
    static std::map<std::string, ConnectionEvent> store;
    return store;
}

} // namespace

void record_connection_event(int connect_status, const std::string& device_id, const std::string& message)
{
    const auto kind = connect_status == ConnectStatusOk   ? ConnectionEvent::Kind::Ok :
                      connect_status == ConnectStatusLost ? ConnectionEvent::Kind::Lost :
                                                            ConnectionEvent::Kind::Failed;
    events()[device_id] = ConnectionEvent{kind, kind == ConnectionEvent::Kind::Failed ? message : std::string(),
                                          std::chrono::steady_clock::now()};
}

std::optional<ConnectionEvent> last_connection_event(const std::string& device_id)
{
    const auto found = events().find(device_id);
    if (found == events().end())
        return std::nullopt;
    return found->second;
}

std::string look_at_port(const std::string& address, int port, std::chrono::milliseconds limit)
{
    namespace asio = boost::asio;
    boost::system::error_code bad;
    const auto ip = asio::ip::make_address(address, bad);
    if (bad)
        return "the app could not try, as that is not an IP address";

    asio::io_context       context;
    asio::ip::tcp::socket  socket(context);
    std::optional<boost::system::error_code> answer;
    const auto started = std::chrono::steady_clock::now();
    socket.async_connect(asio::ip::tcp::endpoint(ip, static_cast<unsigned short>(port)),
                         [&answer](const boost::system::error_code& result) { answer = result; });
    context.run_for(limit);
    if (!answer)
        return "nothing answered within " + std::to_string(limit.count() / 1000) + " seconds";
    if (*answer == asio::error::connection_refused)
        return "the printer turned the connection down at once";
    if (*answer)
        return "the app could not try, as this computer said \"" + answer->message() + "\"";
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    return "it answered in " + std::to_string(took.count()) + " ms";
}

} // namespace Slic3r::GUI::JusPrin::Printers
