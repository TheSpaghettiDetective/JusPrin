#pragma once

// What Bambu's network plug-in last answered about a LAN connection, per
// device. Upstream's handler (the set_on_local_connect_fn lambda in
// GUI_App::on_init_network_callbacks) acts on the answer -- it clears the
// selection, and erases a wrong access code -- and posts it only to the Select
// Machine dialog, when that is open. The printer panel's backend reads the
// answer here instead, so a failed attempt is reported as what the plug-in
// said and not as what the handler left behind: before this, a wrong access
// code read "The printer is no longer available", and every other failure
// "The selected printer changed".
//
// GUI thread only: the handler runs in a CallAfter, and the backend reads on
// the GUI thread too.

#include <chrono>
#include <optional>
#include <string>

namespace Slic3r::GUI::JusPrin::Printers {

struct ConnectionEvent
{
    enum class Kind { Ok, Failed, Lost };
    Kind                                  kind;
    std::string                           code; // the plug-in's message; "5" is a wrong access code
    std::chrono::steady_clock::time_point at;
};

// Called from upstream's handler for every answer, before it acts on it.
// `connect_status` is a ConnectStatus value.
void record_connection_event(int connect_status, const std::string& device_id, const std::string& message);

// The last answer about a device, if the plug-in has given one.
std::optional<ConnectionEvent> last_connection_event(const std::string& device_id);

// Part of a sentence saying what a TCP connection to a port did just now: how
// long it took to answer, that it was turned down, or that nothing answered
// within `limit`. Blocks for up to `limit`, so call it off the GUI thread.
std::string look_at_port(const std::string& address, int port, std::chrono::milliseconds limit);

} // namespace Slic3r::GUI::JusPrin::Printers
