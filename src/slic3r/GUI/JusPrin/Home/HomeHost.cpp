#include "HomeHost.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

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
    ProjectList listed = m_backend.recent_projects();
    snapshot.projects = std::move(listed.projects);
    snapshot.projects_problem = std::move(listed.problem);
    snapshot.printers = ordered_printers();
    return snapshot;
}

std::vector<PrinterEntry> HomeHost::ordered_printers() const
{
    std::vector<PrinterEntry> printers = m_backend.printers();
    // Lead the column with it rather than wherever the backend's own
    // (alphabetical, not chronological) order puts it.
    const auto match = std::find_if(printers.begin(), printers.end(),
                                    [this](const PrinterEntry& printer) { return !m_added.empty() && printer.name == m_added; });
    if (match != printers.end())
        std::rotate(printers.begin(), match, match + 1);
    return printers;
}

namespace {

// The printers as the page receives them. A live refresh compares this, so a
// difference the page would not render is no reason to send.
std::string printers_json(const std::vector<PrinterEntry>& printers)
{
    Snapshot only;
    only.printers = printers;
    return state_payload(only).at("printers").dump();
}

} // namespace

void HomeHost::send_state(Snapshot snapshot)
{
    const bool listed = std::any_of(snapshot.printers.begin(), snapshot.printers.end(),
                                    [this](const PrinterEntry& printer) { return !m_added.empty() && printer.name == m_added; });
    snapshot.highlight_printer = listed ? m_added : std::string();
    m_sent_printers            = printers_json(snapshot.printers);
    m_sent_projects_problem    = snapshot.projects_problem;
    send("state", state_payload(snapshot));
}

void HomeHost::push_state(const std::string& added)
{
    if (!m_connected)
        return;
    m_added = added;
    send_state(collect());
}

bool HomeHost::refresh_if_changed()
{
    if (!m_connected)
        return false;
    std::vector<PrinterEntry> printers = ordered_printers();
    const bool same_printers = printers_json(printers) == m_sent_printers;
    if (same_printers && m_sent_projects_problem.empty())
        return false;
    ProjectList listed = m_backend.recent_projects();
    if (same_printers && listed.problem == m_sent_projects_problem)
        return false;
    Snapshot snapshot;
    snapshot.dark     = m_backend.dark();
    snapshot.projects = std::move(listed.projects);
    snapshot.projects_problem = std::move(listed.problem);
    snapshot.printers = std::move(printers);
    send_state(std::move(snapshot));
    return true;
}

void HomeHost::push_appearance(bool dark)
{
    if (!m_connected)
        return;
    send("appearance", json{{"appearance", dark ? "dark" : "light"}});
}

void HomeHost::ask_confirmation(const Confirmation& question)
{
    const json payload{{"requestId", question.request_id}, {"title", question.title}, {"body", question.body},
                       {"confirm", question.confirm}, {"cancel", question.cancel}};
    if (!m_connected) {
        m_held_confirmation = payload.dump();
        return;
    }
    send("confirm", payload);
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
        if (!m_held_confirmation.empty())
            send("confirm", json::parse(std::exchange(m_held_confirmation, {})));
        return;
    }

    if (!m_connected)
        return; // nothing but hello is answered before the handshake

    if (type == "state_request")
        push_state();
    else if (type == "confirm_answer") {
        if (m_confirmation_listener)
            m_confirmation_listener(payload.value("requestId", std::string()), payload.value("confirmed", false));
    }
    else if (type == "open_project")
        m_backend.open_project(payload.value("id", std::string()));
    else if (type == "new_project")
        m_backend.new_project();
    else if (type == "import_project")
        m_backend.import_model();
    else if (type == "launch_monitor")
        m_backend.launch_monitor(payload.value("id", std::string()));
    else if (type == "open_preferences")
        m_backend.open_preferences();
    else if (type == "add_printer") {
        m_backend.add_printer();
        push_state();
    }
    else if (type == "open_printer_settings" || type == "rename_printer" || type == "remove_printer" || type == "connect_printer" ||
             type == "calibrate_printer") {
        const std::string id = payload.value("id", std::string());
        const std::string problem =
            type == "connect_printer"       ? m_backend.connect_printer(id)
            : type == "calibrate_printer"   ? m_backend.calibrate_printer(id)
            : type == "open_printer_settings" ? m_backend.open_printer_settings(id)
            : type == "rename_printer"      ? m_backend.rename_printer(id, payload.value("name", std::string()))
                                            : m_backend.remove_printer(id);
        if (!problem.empty())
            send("printer_error", json{{"id", id}, {"message", problem}}, correlation);
        // Each can change the rail, including when the person cancelled
        // halfway through an Orca prompt, so the page always gets it back.
        push_state();
    }
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
