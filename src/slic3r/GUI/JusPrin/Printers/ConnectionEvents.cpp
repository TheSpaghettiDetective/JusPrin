#include "ConnectionEvents.hpp"

#include "slic3r/GUI/JusPrin/OrcaMessages/OrcaMessages.hpp"

#include "slic3r/Utils/bambu_networking.hpp"

#include <map>
#include <optional>
#include <set>

namespace Slic3r::GUI::JusPrin::Printers {

namespace {

std::map<std::string, ConnectionEvent>& events()
{
    static std::map<std::string, ConnectionEvent> store;
    return store;
}

std::set<std::string>& expected()
{
    static std::set<std::string> store;
    return store;
}

} // namespace

void expect_connection_answer(const std::string& device_id) { expected().insert(device_id); }

void record_connection_event(int connect_status, const std::string& device_id, const std::string& message)
{
    const auto kind = connect_status == ConnectStatusOk   ? ConnectionEvent::Kind::Ok :
                      connect_status == ConnectStatusLost ? ConnectionEvent::Kind::Lost :
                                                            ConnectionEvent::Kind::Failed;
    events()[device_id] = ConnectionEvent{kind, kind == ConnectionEvent::Kind::Failed ? message : std::string(),
                                          std::chrono::steady_clock::now()};
    // A lost connection is the disconnect a selection of the same printer
    // starts with, not the answer a chat waits for.
    const bool chat_waits = kind != ConnectionEvent::Kind::Lost && expected().erase(device_id) != 0;
    // What OrcaSlicer's handler says next about this answer is about this
    // printer.
    if (kind == ConnectionEvent::Kind::Failed)
        OrcaMessages::note_cause({OrcaMessages::Cause::Kind::PrinterConnection, device_id, message, chat_waits});
}

std::optional<ConnectionEvent> last_connection_event(const std::string& device_id)
{
    const auto found = events().find(device_id);
    if (found == events().end())
        return std::nullopt;
    return found->second;
}

} // namespace Slic3r::GUI::JusPrin::Printers
