#include "ConnectionEvents.hpp"

#include "slic3r/Utils/bambu_networking.hpp"

#include <map>

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

} // namespace Slic3r::GUI::JusPrin::Printers
