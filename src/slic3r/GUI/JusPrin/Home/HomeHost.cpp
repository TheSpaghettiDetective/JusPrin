#include "HomeHost.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

using nlohmann::json;

namespace {

constexpr int     kProtocolVersion = 1;
const char* const kProtocolName    = "jusprin-home-bridge";

} // namespace

HomeHost::HomeHost(IHomeBackend& backend, Send send) : m_backend(backend), m_send(std::move(send)) {}

void HomeHost::reset_page() { m_connected = false; }

void HomeHost::send(const std::string& type, const json& payload, const std::string& correlation)
{
    json envelope{
        {"protocol", kProtocolName},
        {"version", kProtocolVersion},
        {"id", "h-" + std::to_string(m_next_id++)},
        {"type", type},
        {"payload", payload},
    };
    if (!correlation.empty())
        envelope["correlationId"] = correlation;
    ++m_sent;
    if (m_send)
        m_send(envelope.dump());
}

Snapshot HomeHost::collect() const
{
    Snapshot snapshot;
    snapshot.dark     = m_backend.dark();
    snapshot.projects = m_backend.recent_projects();
    snapshot.printers = m_backend.printers();
    return snapshot;
}

void HomeHost::push_state()
{
    if (!m_connected)
        return;
    send("state", state_payload(collect()));
}

void HomeHost::push_appearance(bool dark)
{
    if (!m_connected)
        return;
    send("appearance", json{{"appearance", dark ? "dark" : "light"}});
}

void HomeHost::on_page_message(const std::string& text)
{
    ++m_received;
    json envelope;
    try {
        envelope = json::parse(text);
    } catch (const std::exception&) {
        // The page is the only writer on this transport and it sends JSON, so
        // a parse failure is a bug in the page rather than a state to recover
        // from. Dropping it keeps the bridge up; the page's own handshake
        // timeout is what reports a page that cannot talk at all.
        return;
    }
    if (envelope.value("protocol", std::string()) != kProtocolName)
        return; // another surface's traffic on the same transport

    const std::string type        = envelope.value("type", std::string());
    const std::string correlation = envelope.value("id", std::string());
    const json        payload     = envelope.value("payload", json::object());

    if (type == "hello") {
        const json versions    = payload.value("protocolVersions", json::array());
        const bool speaks_this = std::any_of(versions.begin(), versions.end(),
                                             [](const json& version) { return version == kProtocolVersion; });
        if (!speaks_this) {
            send("hello_reject", json{{"message", "This build speaks Home protocol version 1."}}, correlation);
            return;
        }
        m_connected = true;
        send("hello_ack", json{{"protocolVersion", kProtocolVersion}}, correlation);
        push_state();
        return;
    }

    if (!m_connected)
        return; // nothing but hello is answered before the handshake

    if (type == "state_request")
        push_state();
    else if (type == "open_project")
        m_backend.open_project(payload.value("id", std::string()));
    else if (type == "new_project")
        m_backend.new_project();
    else if (type == "import_project")
        m_backend.import_model();
    else if (type == "launch_monitor")
        m_backend.launch_monitor(payload.value("id", std::string()));
    else if (type == "add_printer") {
        m_backend.add_printer();
        push_state();
    }
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
