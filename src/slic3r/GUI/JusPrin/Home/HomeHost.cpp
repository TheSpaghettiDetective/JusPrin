#include "HomeHost.hpp"

// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/filename.h>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

using nlohmann::json;

namespace {

constexpr int     kProtocolVersion = 1;
const char* const kProtocolName    = "jusprin-home-bridge";

// Thumbnails travel as base64 data: URLs, so the whole gallery's images sit in
// one envelope. The cap bounds that payload; projects past it keep their card
// and its frame, without an image.
constexpr int kThumbnailCap = 30;

std::string utf8(const std::wstring& text) { return std::string(wxString(text).ToUTF8()); }

// "C:/models/garage bracket.3mf" -> "garage bracket", the form a print job on
// a device carries.
std::string file_stem(const std::wstring& path)
{
    const std::wstring name = path.substr(path.find_last_of(L"/\\") + 1);
    const size_t       dot  = name.find_last_of(L'.');
    return utf8(dot == std::wstring::npos ? name : name.substr(0, dot));
}

bool machine_is_printing(const MachineObject& machine)
{
    return machine.print_status == "RUNNING" || machine.print_status == "PAUSE" ||
           machine.print_status == "SLICING";
}

// The separator the design uses inside a status line, as the shell's status
// row does. Kept local for the same reason it is local there: it is a
// punctuation constant, not an interface.
const wxString& middle_dot()
{
    static const wxString s = wxString::FromUTF8(" \xC2\xB7 ");
    return s;
}

// "2h 14m", "14m", or empty when the device has not estimated yet. The card
// omits the clause rather than showing a zero.
wxString remaining_text(int seconds)
{
    if (seconds <= 0)
        return wxEmptyString;
    const int minutes = seconds / 60;
    if (minutes >= 60)
        return wxString::Format(_L("%dh %dm left"), minutes / 60, minutes % 60);
    return wxString::Format(_L("%dm left"), std::max(minutes, 1));
}

} // namespace

HomeHost::HomeHost(MainFrame& frame, Workspace::SpoolStore* spools, Send send)
    : m_frame(frame), m_spools(spools), m_send(std::move(send))
{}

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
    m_send(envelope.dump());
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

Snapshot HomeHost::collect() const
{
    Snapshot snapshot;
    snapshot.dark = wxGetApp().dark_mode();

    // Which project each printing machine is running, so a card can say so.
    // This is the only per-project status the codebase can answer today; the
    // rest is the file's own modified time until a project-state store exists.
    std::map<std::string, std::string>    printing_by_stem;
    std::map<std::string, MachineObject*> machines;
    if (DeviceManager* devices = wxGetApp().getDeviceManager()) {
        for (const auto& entry : devices->get_my_machine_list()) {
            MachineObject* machine = entry.second;
            if (machine == nullptr)
                continue;
            machines.emplace(entry.first, machine);
            if (machine_is_printing(*machine) && !machine->subtask_name.empty())
                printing_by_stem.emplace(machine->subtask_name, machine->get_dev_name());
        }
    }

    boost::property_tree::wptree recent;
    m_frame.get_recent_projects(recent, kThumbnailCap);
    size_t index = 0;
    for (const auto& node : recent) {
        const auto&  item = node.second;
        ProjectEntry project;
        project.id              = std::to_string(index++);
        const std::wstring path = item.get<std::wstring>(L"path", L"");
        // The recent list's project_name keeps the extension; a card is titled
        // by the project, so ".3mf" comes off.
        project.name            = file_stem(path);
        project.path            = utf8(path);
        project.thumbnail_url   = utf8(item.get<std::wstring>(L"image", L""));

        const auto printing = printing_by_stem.find(file_stem(path));
        if (printing != printing_by_stem.end()) {
            project.status_kind = ProjectStatusKind::Printing;
            project.status_text = std::string(
                wxString::Format(_L("Printing on %s"), wxString::FromUTF8(printing->second)).ToUTF8());
        } else {
            // Honest about what is known: nothing records whether this project
            // is sliced, so the card shows when it was last written. The line
            // says which fact it is showing rather than dropping a bare
            // timestamp where the design puts a state.
            project.status_kind = ProjectStatusKind::Unknown;
            // The recent list stamps "YYYY-MM-DD HH:MM:SS"; a status line does
            // not need the second.
            wxString when = wxString(item.get<std::wstring>(L"time", L""));
            if (when.length() == 19 && when[16] == ':')
                when = when.Left(16);
            if (!when.empty())
                project.status_text = std::string(wxString::Format(_L("Edited %s"), when).ToUTF8());
        }
        snapshot.projects.push_back(std::move(project));
    }

    for (const auto& entry : machines) {
        MachineObject* machine = entry.second;
        PrinterEntry   printer;
        printer.id                 = entry.first;
        printer.name               = machine->get_dev_name();
        const bool printing        = machine_is_printing(*machine);
        const bool connected       = machine->is_connected();
        printer.state              = !connected ? PrinterState::Offline
                                     : printing ? PrinterState::Printing
                                                : PrinterState::Idle;
        printer.can_launch_monitor = connected;
        if (printing) {
            printer.progress_percent = machine->mc_print_percent;
            wxString status          = _L("Printing") + middle_dot() +
                              wxString::Format("%d%%", machine->mc_print_percent);
            const wxString remaining = remaining_text(machine->mc_left_time);
            if (!remaining.empty())
                status += middle_dot() + remaining;
            printer.status_text = std::string(status.ToUTF8());
        }
        if (connected)
            printer.connection_text = std::string(_L("Connected").ToUTF8());
        // The nozzle the device reports, not the one the preset assumes: a
        // printer whose hardware was changed says so here.
        if (const DevExtderSystem* extruders = machine->GetExtderSystem()) {
            const float diameter = extruders->GetNozzleDiameter(0);
            if (diameter > 0.f)
                printer.nozzle_text =
                    std::string(wxString::Format(_L("%.1f mm nozzle"), diameter).ToUTF8());
        }
        snapshot.printers.push_back(std::move(printer));
    }

    // Printers Orca knows as presets but no device reports: they belong in the
    // column, without a job.
    if (const PresetBundle* presets = wxGetApp().preset_bundle) {
        for (const PhysicalPrinter& physical : presets->physical_printers) {
            const bool known = std::any_of(snapshot.printers.begin(), snapshot.printers.end(),
                                           [&](const PrinterEntry& seen) { return seen.name == physical.name; });
            if (known)
                continue;
            PrinterEntry printer;
            printer.id    = physical.name;
            printer.name  = physical.name;
            printer.state = PrinterState::Idle;
            snapshot.printers.push_back(std::move(printer));
        }
    }

    // Spools are remembered per Orca printer *preset*, and only one preset is
    // in force at a time, so the only printer whose reels the store can name is
    // the selected one. The rest show no swatch row rather than another
    // printer's filament.
    if (m_spools != nullptr) {
        std::string selected_dev;
        if (DeviceManager* devices = wxGetApp().getDeviceManager())
            if (MachineObject* selected = devices->get_selected_machine())
                selected_dev = selected->get_dev_id();
        if (const PresetBundle* presets = wxGetApp().preset_bundle; presets != nullptr && !selected_dev.empty()) {
            const std::string preset = presets->printers.get_selected_preset_name();
            for (PrinterEntry& printer : snapshot.printers) {
                if (printer.id != selected_dev)
                    continue;
                for (const Workspace::Spool& spool : m_spools->spools_for(preset))
                    printer.spools.push_back(SpoolEntry{spool.colour});
                if (!printer.spools.empty())
                    printer.material_label = presets->filaments.get_selected_preset().config.opt_string("filament_type", 0);
            }
        }
    }

    return snapshot;
}

void HomeHost::on_page_message(const std::string& text)
{
    ++m_received;
    json envelope;
    try {
        envelope = json::parse(text);
    } catch (const std::exception& error) {
        // The page is the only writer on this transport and it sends JSON; a
        // parse failure is a bug in the page, not a state to recover from, so
        // it is reported rather than retried.
        BOOST_LOG_TRIVIAL(error) << "JusPrin Home: unreadable page message: " << error.what();
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

    if (type == "state_request") {
        push_state();
    } else if (type == "open_project") {
        const std::string id = payload.value("id", std::string());
        if (id.empty())
            return;
        const size_t                 wanted = static_cast<size_t>(std::stoul(id));
        boost::property_tree::wptree recent;
        m_frame.get_recent_projects(recent, 0);
        size_t at = 0;
        for (const auto& node : recent) {
            if (at++ != wanted)
                continue;
            const wxString path(node.second.get<std::wstring>(L"path", L""));
            m_frame.open_recent_project(wanted, path);
            // Opening a project is a move to the workspace, but only if it
            // opens. open_recent_project queues the load behind
            // can_load_project(), which asks about unsaved changes and
            // abandons the load when the answer is Cancel. Queueing the move
            // behind that lambda -- CallAfter is FIFO -- and confirming the
            // project that is now open is the one that was asked for keeps a
            // cancelled open on Home instead of navigating out from under it.
            m_frame.CallAfter([this, path] {
                Plater* plater = wxGetApp().plater();
                if (plater != nullptr && wxFileName(plater->get_project_filename()).SameAs(wxFileName(path)))
                    m_frame.select_tab(size_t(MainFrame::tp3DEditor));
            });
            return;
        }
    } else if (type == "new_project") {
        if (Plater* plater = wxGetApp().plater()) {
            // Cancelling the same unsaved-changes question leaves the old
            // project in place, so it leaves Home in place too.
            if (plater->new_project() != wxID_CANCEL)
                m_frame.select_tab(size_t(MainFrame::tp3DEditor));
        }
    } else if (type == "import_project") {
        if (Plater* plater = wxGetApp().plater()) {
            // add_model imports into the project that is open and runs its own
            // modal file dialog, so the move waits for it to return.
            plater->add_model();
            m_frame.select_tab(size_t(MainFrame::tp3DEditor));
        }
    } else if (type == "launch_monitor") {
        const std::string dev_id = payload.value("id", std::string());
        if (DeviceManager* devices = wxGetApp().getDeviceManager(); devices != nullptr && !dev_id.empty())
            devices->set_selected_machine(dev_id);
        m_frame.select_tab(size_t(MainFrame::tpMonitor));
    } else if (type == "add_printer") {
        wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
        push_state();
    }
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
