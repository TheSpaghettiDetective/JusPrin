// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "OrcaHomeBackend.hpp"

#include "slic3r/GUI/BindDialog.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/SelectMachinePop.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterDiscovery.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SpoolStore.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/filename.h>

#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

namespace {

// Thumbnails travel as base64 data: URLs, so the whole gallery's images sit in
// one envelope. The cap bounds that payload; projects past it keep their card
// and its frame, without an image.
constexpr int kThumbnailCap = 30;

std::string utf8(const std::wstring& text) { return std::string(wxString(text).ToUTF8()); }

// "C:/models/garage bracket.3mf" -> "garage bracket", the form a print job on
// a device carries, and the form a card is titled by.
std::string file_stem(const std::wstring& path)
{
    const std::wstring name = path.substr(path.find_last_of(L"/\\") + 1);
    const size_t       dot  = name.find_last_of(L'.');
    return utf8(dot == std::wstring::npos ? name : name.substr(0, dot));
}

// Upstream's own predicate, not a copy of it. The first version of this listed
// RUNNING, PAUSE and SLICING and left out PREPARE, so a printer that was
// preparing a job showed on Home as idle. One implementation cannot drift from
// itself.
bool machine_is_printing(const MachineObject& machine)
{
    return MachineObject::is_in_printing_status(machine.print_status);
}

// The separator the design uses inside a status line, as the shell's status
// row does. Kept local for the same reason it is local there: it is a
// punctuation constant, not an interface.
const wxString& middle_dot()
{
    static const wxString s = wxString::FromUTF8(" \xC2\xB7 ");
    return s;
}

// "2h 14m left", "14m left", or empty when the device has not estimated yet.
// The card omits the clause rather than showing a zero.
wxString remaining_text(int seconds)
{
    if (seconds <= 0)
        return wxEmptyString;
    const int minutes = seconds / 60;
    if (minutes >= 60)
        return wxString::Format(_L("%dh %dm left"), minutes / 60, minutes % 60);
    return wxString::Format(_L("%dm left"), std::max(minutes, 1));
}

// A card id says what the card stands for, so an action never has to guess
// whether a name is a printer or a device serial.
const std::string kNamedPrefix  = "named:";
const std::string kDevicePrefix = "device:";

// The rest of `id` after `prefix`, or nothing when it has another prefix.
std::optional<std::string> strip(const std::string& id, const std::string& prefix)
{
    if (id.compare(0, prefix.size(), prefix) != 0)
        return std::nullopt;
    return id.substr(prefix.size());
}

std::string utf8(const wxString& text) { return std::string(text.ToUTF8()); }

// "X1 Carbon" for the model a profile or a device names: the profile's
// "Bambu Lab X1 Carbon", or a device's own id, "BL-P001". The loaded vendor
// profiles answer, as the catalogue would without reading its files again.
// The brand stays when the rest is only a number: "2.4 350" names no printer,
// "Voron 2.4 350" does. A profile model no vendor lists keeps its own name; a
// device id, nothing.
std::string model_name(const std::string& printer_model, const std::string& device_model_id = {})
{
    for (const auto& [id, vendor] : wxGetApp().preset_bundle->vendors)
        for (const VendorProfile::PrinterModel& model : vendor.models)
            if (printer_model.empty() ? !device_model_id.empty() && model.model_id == device_model_id
                                      : model.id == printer_model) {
                const std::string rest = PrinterSetup::split_brand(vendor.name, model.id).second;
                return !rest.empty() && std::isdigit(static_cast<unsigned char>(rest.front())) ? model.id : rest;
            }
    return printer_model;
}

// "X1 Carbon · 0.4 mm", or whichever half is known.
std::string model_text(const std::string& model, double nozzle)
{
    wxString text = wxString::FromUTF8(model);
    if (nozzle > 0.)
        text += (text.empty() ? wxString() : middle_dot()) + wxString::Format("%g mm", nozzle);
    return utf8(text);
}

// "192.168.1.42:7125" for "http://192.168.1.42:7125/": the host and port as
// saved, without the scheme, a path, or any credentials written into it.
std::string host_address(const std::string& print_host)
{
    std::string address = print_host;
    if (const size_t scheme = address.find("://"); scheme != std::string::npos)
        address = address.substr(scheme + 3);
    address = address.substr(0, address.find('/'));
    if (const size_t at = address.rfind('@'); at != std::string::npos)
        address = address.substr(at + 1);
    return address.empty() ? print_host : address;
}

double reported_nozzle(MachineObject& machine)
{
    const DevExtderSystem* extruders = machine.GetExtderSystem();
    return extruders != nullptr ? extruders->GetNozzleDiameter(0) : 0.;
}

// What the device reports about itself and its job. The connection line is
// the device's only when it has something verified to say; otherwise the
// card keeps what it had.
void describe_device(MachineObject& machine, PrinterEntry& printer)
{
    const bool printing        = machine_is_printing(machine);
    const bool connected       = PrinterSetup::has_recent_printer_data(machine);
    printer.state              = !connected ? PrinterState::Offline
                                 : printing ? PrinterState::Printing
                                            : PrinterState::Idle;
    printer.can_launch_monitor = connected;
    if (printing) {
        printer.progress_percent = machine.mc_print_percent;
        wxString status          = _L("Printing") + middle_dot() + wxString::Format("%d%%", machine.mc_print_percent);
        const wxString remaining = remaining_text(machine.mc_left_time);
        if (!remaining.empty())
            status += middle_dot() + remaining;
        printer.status_text = std::string(status.ToUTF8());
    }
    const std::string kind = machine.connection_type();
    if (connected) {
        printer.connection_state  = ConnectionState::Online;
        printer.connection_kind   = kind;
        printer.connection_text   = utf8(_L("Online") + middle_dot() + (kind == "lan" ? _L("LAN") : _L("Cloud")));
        printer.connection_action = "";
    } else if (PrinterSetup::has_verified_printer_connection(machine.get_dev_id())) {
        printer.connection_state  = ConnectionState::Offline;
        printer.connection_kind   = kind;
        printer.connection_text   = utf8(_L("Offline"));
        printer.connection_action = "reconnect";
    }
}

MachineObject* my_machine(const std::string& device_id)
{
    DeviceManager* devices = wxGetApp().getDeviceManager();
    return devices != nullptr ? devices->get_my_machine(device_id) : nullptr;
}

// The filament type a spool's preset names ("PLA"), or empty.
std::string filament_type(const std::string& filament_preset)
{
    const Preset* preset = wxGetApp().preset_bundle->filaments.find_preset(filament_preset, false, true);
    if (preset == nullptr)
        return {};
    const auto* type = preset->config.option<ConfigOptionStrings>("filament_type");
    return type != nullptr && !type->values.empty() ? type->values.front() : std::string();
}

std::string gone() { return utf8(_L("This printer no longer exists.")); }

} // namespace

OrcaHomeBackend::OrcaHomeBackend(MainFrame& frame, Workspace::SpoolStore* spools)
    : m_frame(frame), m_spools(spools)
{}

bool OrcaHomeBackend::dark() const { return wxGetApp().dark_mode(); }

std::vector<ProjectEntry> OrcaHomeBackend::recent_projects() const
{
    // Which project each printing machine is running, so a card can say so.
    // This is the only per-project status the codebase can answer today; the
    // rest is the file's own modified time until a project-state store exists.
    std::map<std::string, std::string> printing_by_stem;
    if (DeviceManager* devices = wxGetApp().getDeviceManager()) {
        for (const auto& entry : devices->get_my_machine_list()) {
            MachineObject* machine = entry.second;
            if (machine == nullptr)
                continue;
            if (machine_is_printing(*machine) && !machine->subtask_name.empty())
                printing_by_stem.emplace(machine->subtask_name, machine->get_dev_name());
        }
    }

    std::vector<ProjectEntry>    projects;
    boost::property_tree::wptree recent;
    m_frame.get_recent_projects(recent, kThumbnailCap);
    size_t index = 0;
    for (const auto& node : recent) {
        const auto&  item = node.second;
        ProjectEntry project;
        project.id              = std::to_string(index++);
        const std::wstring path = item.get<std::wstring>(L"path", L"");
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
            // is sliced, so the card shows when it was last written, and says
            // which fact it is showing rather than dropping a bare timestamp
            // where the design puts a state.
            project.status_kind = ProjectStatusKind::Unknown;
            // The recent list stamps "YYYY-MM-DD HH:MM:SS"; a status line does
            // not need the second.
            wxString when = wxString(item.get<std::wstring>(L"time", L""));
            if (when.length() == 19 && when[16] == ':')
                when = when.Left(16);
            if (!when.empty())
                project.status_text = std::string(wxString::Format(_L("Edited %s"), when).ToUTF8());
        }
        projects.push_back(std::move(project));
    }
    return projects;
}

std::vector<PrinterEntry> OrcaHomeBackend::printers() const
{
    std::map<std::string, MachineObject*> machines;
    if (DeviceManager* devices = wxGetApp().getDeviceManager()) {
        for (const auto& entry : devices->get_my_machine_list())
            if (entry.second != nullptr)
                machines.emplace(entry.first, entry.second);
    }

    // What each connected device has loaded, read once for the whole rail.
    // A connected device with nothing loaded has an empty entry: it is the
    // printer saying so, and the card believes it over what was remembered.
    std::map<std::string, std::vector<SpoolEntry>> loaded;
    if (!machines.empty())
        for (const PrinterSetup::DiscoveredPrinter& device : PrinterSetup::discover_printers())
            if (device.connected) {
                auto& spools = loaded[device.stable_id];
                for (const PrinterSetup::DiscoveredPrinter::Spool& spool : device.spools)
                    spools.push_back(SpoolEntry{spool.material, spool.colour});
            }

    // Every card says something about its connection; one with nothing
    // verified says so in words, and draws no dot.
    const auto settle_connection = [](PrinterEntry& printer) {
        if (printer.connection_state != ConnectionState::None)
            return;
        printer.connection_text   = utf8(_L("Not connected"));
        printer.connection_kind   = "";
        printer.connection_action = "";
    };

    // The printers the person added, each with the device it stands for when
    // that device is here. The device's own reading of the nozzle wins: a
    // printer whose hardware was changed says so.
    std::vector<PrinterEntry> printers;
    std::set<std::string>     represented;
    for (const Printers::NamedPrinter& named : Printers::named_printers()) {
        PrinterEntry printer;
        printer.id                = kNamedPrefix + named.name;
        printer.name              = named.name;
        printer.kind              = PrinterKind::Named;
        printer.can_open_settings = true;
        printer.can_rename        = true;
        printer.can_remove        = true;
        std::string model;
        if (const auto* preset = wxGetApp().preset_bundle->printers.find_preset(named.name, false, true)) {
            model = model_name(preset->config.opt_string("printer_model"));
            // A print host's address is saved only once the host has answered
            // (or typed into Orca's own settings dialog, which is not tested:
            // an accepted gap). Nothing here says whether it answers now.
            const std::string host = preset->config.opt_string("print_host");
            if (!host.empty()) {
                printer.connection_state   = ConnectionState::Connected;
                printer.connection_kind    = "host";
                printer.address            = host_address(host);
                printer.connection_text    = utf8(_L("Connected") + middle_dot() + wxString::FromUTF8(printer.address));
            }
        }
        double nozzle = named.nozzle;
        if (const auto found = machines.find(named.device_id); !named.device_id.empty() && found != machines.end()) {
            describe_device(*found->second, printer);
            if (printer.can_launch_monitor) {
                wxGetApp().app_config->set("jusprin_verified_connections", named.device_id, "true");
                if (const double reported = reported_nozzle(*found->second); reported > 0.)
                    nozzle = reported;
            }
            represented.insert(named.device_id);
        } else if (PrinterSetup::has_verified_printer_connection(named.device_id)) {
            // Connected before, and the app has not heard of the device since.
            printer.connection_state  = ConnectionState::Offline;
            printer.connection_text   = utf8(_L("Offline"));
            printer.connection_action = "reconnect";
        }
        settle_connection(printer);
        printer.model_text = model_text(model, nozzle);
        // A connected printer says what it holds; otherwise the spools the
        // person described for it stand in.
        if (const auto held = loaded.find(named.device_id); !named.device_id.empty() && held != loaded.end())
            printer.spools = held->second;
        else if (m_spools != nullptr)
            for (const Workspace::Spool& spool : m_spools->spools_for(named.name))
                printer.spools.push_back(SpoolEntry{filament_type(spool.filament_preset), spool.colour});
        printers.push_back(std::move(printer));
    }

    // Devices no named printer stands for. Their name and binding are the
    // device's, so renaming and removing them are upstream's device dialogs,
    // offered where upstream's device list offers them.
    for (const auto& [id, machine] : machines) {
        if (represented.count(id) > 0)
            continue;
        PrinterEntry printer;
        printer.id         = kDevicePrefix + id;
        printer.name       = machine->get_dev_name();
        printer.kind       = PrinterKind::Device;
        printer.can_rename = !machine->is_lan_mode_printer() && machine->is_online();
        printer.can_remove = !machine->is_lan_mode_printer();
        describe_device(*machine, printer);
        settle_connection(printer);
        printer.model_text = model_text(model_name({}, machine->printer_type), reported_nozzle(*machine));
        if (const auto held = loaded.find(id); held != loaded.end())
            printer.spools = held->second;
        printers.push_back(std::move(printer));
    }
    return printers;
}

void OrcaHomeBackend::open_project(const std::string& project_id)
{
    if (project_id.empty())
        return;
    const size_t                 wanted = static_cast<size_t>(std::stoul(project_id));
    boost::property_tree::wptree recent;
    m_frame.get_recent_projects(recent, 0);
    size_t at = 0;
    for (const auto& node : recent) {
        if (at++ != wanted)
            continue;
        const wxString path(node.second.get<std::wstring>(L"path", L""));
        m_frame.open_recent_project(wanted, path);
        // Opening a project is a move to the workspace, but only if it opens.
        // open_recent_project queues the load behind can_load_project(), which
        // asks about unsaved changes and abandons the load when the answer is
        // Cancel. Queueing the move behind that lambda -- CallAfter is FIFO --
        // and confirming the project that is now open is the one that was
        // asked for keeps a cancelled open on Home instead of navigating out
        // from under it.
        m_frame.CallAfter([this, path] {
            Plater* plater = wxGetApp().plater();
            if (plater != nullptr && wxFileName(plater->get_project_filename()).SameAs(wxFileName(path)))
                m_frame.select_tab(size_t(MainFrame::tp3DEditor));
        });
        return;
    }
}

void OrcaHomeBackend::new_project()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    // Cancelling the unsaved-changes question leaves the old project in place,
    // so it leaves Home in place too.
    if (plater->new_project() != wxID_CANCEL)
        m_frame.select_tab(size_t(MainFrame::tp3DEditor));
}

void OrcaHomeBackend::import_model()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    // add_model imports into the project that is open and runs its own modal
    // file dialog. It returns void, so the object count before and after is
    // what says whether anything arrived: cancelling the dialog leaves Home
    // where it was, as cancelling New does.
    const size_t before = plater->model().objects.size();
    plater->add_model();
    if (plater->model().objects.size() != before)
        m_frame.select_tab(size_t(MainFrame::tp3DEditor));
}

void OrcaHomeBackend::launch_monitor(const std::string& printer_id)
{
    // A named printer is monitored through the device it stands for.
    std::string device_id = strip(printer_id, kDevicePrefix).value_or(std::string());
    if (const auto name = strip(printer_id, kNamedPrefix))
        for (const Printers::NamedPrinter& named : Printers::named_printers())
            if (named.name == *name)
                device_id = named.device_id;
    if (device_id.empty())
        return; // the card changed under the click; the refreshed rail says so
    // Upstream's own path to the Monitor, as the device popup and the
    // multi-machine page use. Selecting the tab and the machine by hand
    // instead left Home on screen: jump_to_monitor also guards on the Monitor
    // panel existing and selects the machine inside it. Upstream may still
    // decline the switch -- it vetoes the Monitor tab when the network plugin
    // is missing -- and that refusal is upstream's to make.
    m_frame.jump_to_monitor(device_id);
}

void OrcaHomeBackend::add_printer()
{
    // The conversation replaces the printers column in place; the shell draws
    // it and owns the session.
    if (m_open_conversation)
        m_open_conversation({}, false);
}

std::string OrcaHomeBackend::open_printer_settings(const std::string& printer_id)
{
    const auto name = strip(printer_id, kNamedPrefix);
    if (!name)
        return gone();
    // "Printer settings…" opens that printer's own conversation, where "Set
    // it up myself" still leads to OrcaSlicer's settings window.
    if (m_open_conversation)
        m_open_conversation(*name, false);
    return {};
}

std::string OrcaHomeBackend::connect_printer(const std::string& printer_id)
{
    const auto name = strip(printer_id, kNamedPrefix);
    if (!name)
        return gone();
    const auto saved = Printers::named_printers();
    if (std::none_of(saved.begin(), saved.end(), [&](const auto& printer) { return printer.name == *name; }))
        return gone();
    if (m_open_conversation)
        m_open_conversation(*name, true);
    return {};
}

std::string OrcaHomeBackend::rename_printer(const std::string& printer_id, const std::string& new_name)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return gone();
    if (const auto name = strip(printer_id, kNamedPrefix))
        return utf8(Printers::rename_named_printer(*plater, m_spools, *name, new_name));
    // A device keeps its own name. Upstream's dialog, as the device list
    // opens it, validates and sends the new one.
    MachineObject* machine = my_machine(strip(printer_id, kDevicePrefix).value_or(std::string()));
    if (machine == nullptr)
        return gone();
    EditDevNameDialog dialog(plater);
    dialog.set_machine_obj(machine);
    dialog.ShowModal();
    return {};
}

std::string OrcaHomeBackend::remove_printer(const std::string& printer_id)
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return gone();
    if (const auto name = strip(printer_id, kNamedPrefix))
        return utf8(Printers::remove_named_printer(*plater, m_spools, *name));
    // Removing a device unbinds it from the account, through upstream's
    // dialog, which confirms and reports for itself, as the device list does.
    const std::string device_id = strip(printer_id, kDevicePrefix).value_or(std::string());
    MachineObject*    machine   = my_machine(device_id);
    if (machine == nullptr)
        return gone();
    UnBindMachineDialog dialog(plater);
    dialog.update_machine_info(machine);
    if (dialog.ShowModal() == wxID_OK)
        wxGetApp().getDeviceManager()->set_selected_machine("");
    return {};
}

}}}} // namespace Slic3r::GUI::JusPrin::Home
