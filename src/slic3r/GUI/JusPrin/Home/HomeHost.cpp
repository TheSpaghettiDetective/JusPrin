#include "HomeHost.hpp"

#include "slic3r/GUI/JusPrin/Support/Base64.hpp"

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

json HomeHost::history_payload()
{
    return json{{"entries", m_backend.print_history()}, {"reportsOffered", m_backend.reports_offered()}};
}

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

Snapshot HomeHost::collect()
{
    Snapshot snapshot;
    snapshot.dark     = m_backend.dark();
    ProjectList listed = m_backend.recent_projects();
    snapshot.projects = std::move(listed.projects);
    snapshot.projects_problem = std::move(listed.problem);
    snapshot.printers = ordered_printers();
    snapshot.onboarding = m_backend.onboarding();
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
    // Read again: a printer card's history line counts the projects' records,
    // which the backend reads with the projects.
    snapshot.printers = ordered_printers();
    snapshot.onboarding = m_backend.onboarding();
    send_state(std::move(snapshot));
    return true;
}

bool HomeHost::poll()
{
    if (!m_backend.poll_onboarding() || !m_connected)
        return false;
    send_state(collect());
    return true;
}

void HomeHost::push_appearance(bool dark)
{
    if (!m_connected)
        return;
    send("appearance", json{{"appearance", dark ? "dark" : "light"}});
}

void HomeHost::show_screen(const json& screen)
{
    if (!m_connected) {
        m_held_screen = screen.dump();
        return;
    }
    send("screen", screen);
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
        if (!m_held_screen.empty())
            send("screen", json::parse(std::exchange(m_held_screen, {})));
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
    else if (type == "open_file")
        m_backend.open_file();
    else if (type == "open_dropped_files") {
        // A file the page could not encode never reaches here; one whose
        // bytes do not decode is left out, as the page left out its own.
        std::vector<DroppedFile> files;
        if (const auto listed = payload.find("files"); listed != payload.end() && listed->is_array())
            for (const json& file : *listed) {
                if (!file.is_object())
                    continue;
                DroppedFile dropped{file.value("name", std::string()), {}};
                if (!dropped.name.empty() &&
                    base64_decode(strip_data_url_prefix(file.value("dataBase64", std::string())), dropped.bytes))
                    files.push_back(std::move(dropped));
            }
        if (!files.empty())
            m_backend.open_dropped_files(files);
    }
    else if (type == "launch_monitor")
        m_backend.launch_monitor(payload.value("id", std::string()));
    else if (type == "open_preferences")
        m_backend.open_preferences();
    else if (type == "history_request")
        send("history", history_payload(), correlation);
    else if (type == "history_back")
        m_backend.leave_print_history();
    else if (type == "open_in_chat")
        m_backend.open_in_chat(payload.value("projectId", std::string()), payload.value("conversationId", std::string()),
                               payload.value("markerId", std::string()));
    else if (type == "print_again")
        m_backend.print_again(payload.value("projectId", std::string()), payload.value("hash", std::string()));
    else if (type == "export_gcode") {
        m_backend.export_gcode(payload.value("projectId", std::string()), payload.value("hash", std::string()),
                               payload.value("beside", std::string()));
        // The export is a new row, and the file it was beside is checked
        // again with the rest.
        send("history", history_payload(), correlation);
    }
    else if (type == "print_report_choose_photo") {
        json reply = json::object();
        if (const std::optional<PrintReportPhoto> photo = m_backend.choose_report_photo()) {
            reply["photo"] = json{{"id", photo->id}, {"name", photo->name}, {"sizeBytes", photo->size_bytes}};
            if (!photo->thumbnail.empty())
                reply["photo"]["thumbnail"] = photo->thumbnail;
        }
        send("print_report_photo", reply, correlation);
    }
    else if (type == "print_report_send") {
        const PrintReportForm form{payload.value("projectId", std::string()), payload.value("hash", std::string()),
                                   payload.value("outcome", std::string()), payload.value("text", std::string()),
                                   payload.value("photoId", std::string())};
        m_backend.send_print_report(form, [this, alive = std::weak_ptr<void>(m_alive), hash = form.hash](const PrintReportAnswer& answer) {
            if (alive.expired() || !m_connected)
                return;
            const char* state = answer.state == PrintReportAnswer::State::Sent        ? "sent" :
                                answer.state == PrintReportAnswer::State::AlreadySent ? "already_sent" : "failed";
            send("print_report_result", json{{"hash", hash}, {"state", state}, {"credits", answer.credits}});
            send("history", history_payload());
        });
    }
    else if (type == "add_printer") {
        m_backend.add_printer();
        push_state();
    }
    else if (type == "onboarding_sign_in" || type == "onboarding_sign_in_cancel" || type == "onboarding_leave_welcome" ||
             type == "onboarding_back" || type == "onboarding_start_fresh" || type == "onboarding_ask_agent" ||
             type == "agent_card_dismiss") {
        if (type == "onboarding_sign_in")
            m_backend.start_sign_in();
        else if (type == "onboarding_sign_in_cancel")
            m_backend.cancel_sign_in();
        else if (type == "onboarding_leave_welcome")
            m_backend.leave_welcome();
        else if (type == "onboarding_back")
            m_backend.step_back();
        else if (type == "onboarding_start_fresh")
            m_backend.start_fresh();
        else if (type == "onboarding_ask_agent")
            m_backend.ask_agent_for_printer();
        else
            m_backend.dismiss_account_card();
        push_state();
    }
    else if (type == "onboarding_sign_in_email" || type == "onboarding_sign_in_code" || type == "onboarding_sign_in_password" ||
             type == "onboarding_sign_in_send_code" || type == "onboarding_sign_in_reopen" || type == "onboarding_open_link") {
        if (type == "onboarding_sign_in_email")
            m_backend.sign_in_with_email(payload.value("email", std::string()));
        else if (type == "onboarding_sign_in_code")
            m_backend.submit_sign_in_code(payload.value("code", std::string()));
        else if (type == "onboarding_sign_in_password")
            m_backend.submit_sign_in_password(payload.value("password", std::string()));
        else if (type == "onboarding_sign_in_send_code")
            m_backend.send_sign_in_code();
        else if (type == "onboarding_sign_in_reopen")
            m_backend.reopen_sign_in_browser();
        else
            m_backend.open_sign_in_link(payload.value("link", std::string()));
        push_state();
    }
    else if (type == "onboarding_open_preferences") {
        // Preferences opens on the frame's next turn; how it closed comes
        // back through poll().
        const std::string door = payload.value("door", std::string());
        if (door == "key" || door == "tool")
            m_backend.open_agent_door(door);
    }
    else if (type == "onboarding_import" || type == "onboarding_assume_default") {
        const std::string problem = type == "onboarding_import"
                                        ? m_backend.import_orca_printers(payload.at("printers").get<std::vector<std::string>>())
                                        : m_backend.assume_default_printer();
        if (!problem.empty())
            send("onboarding_error", json{{"message", problem}}, correlation);
        push_state();
    }
    else if (type == "onboarding_import_continue") {
        m_backend.finish_orca_import();
        push_state();
    }
    else if (type == "onboarding_printer_form") {
        m_backend.printer_form_message(payload);
        push_state();
    }
    else if (type == "open_printer_settings" || type == "ask_about_printer_settings" || type == "rename_printer" ||
             type == "remove_printer" || type == "connect_printer" || type == "calibrate_printer") {
        const std::string id = payload.value("id", std::string());
        const std::string problem =
            type == "connect_printer"       ? m_backend.connect_printer(id)
            : type == "calibrate_printer"   ? m_backend.calibrate_printer(id)
            : type == "open_printer_settings" ? m_backend.open_printer_settings(id)
            : type == "ask_about_printer_settings" ? m_backend.ask_about_printer_settings(id)
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
