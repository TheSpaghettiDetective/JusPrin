#include "GcodeHistoryRecorder.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Jobs/PrintJob.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Exception.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/SelectMachine.hpp"
#include "slic3r/GUI/SendToPrinter.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterDiscovery.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectVersionStore.hpp"
#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"
#include "slic3r/Utils/PrintHost.hpp"

#include <boost/filesystem/operations.hpp>
#include <wx/utils.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

namespace {

using Agent::GcodeDestination;
using Agent::PrintOutcomeTracker;

constexpr int kPollMilliseconds = 2000;

// The Bambu jobs' finished events, which the Plater exposes only by number.
wxEventTypeTag<wxCommandEvent> print_finished(Plater& plater) { return wxEventTypeTag<wxCommandEvent>(plater.get_print_finished_event()); }
wxEventTypeTag<wxCommandEvent> send_finished(Plater& plater) { return wxEventTypeTag<wxCommandEvent>(plater.get_send_finished_event()); }

PrintOutcomeTracker::PrinterState printer_state(const MachineObject& machine)
{
    if (MachineObject::is_in_printing_status(machine.print_status))
        return PrintOutcomeTracker::PrinterState::Printing;
    if (machine.print_status == "FINISH")
        return PrintOutcomeTracker::PrinterState::Finished;
    return PrintOutcomeTracker::PrinterState::Other;
}

// Whether one of OrcaSlicer's send dialogs is on screen: a Bambu job of this
// project finishes while the dialog that sent it is still showing.
template<class Dialog> bool shown()
{
    for (wxWindow* window : wxTopLevelWindows)
        if (auto* dialog = dynamic_cast<Dialog*>(window); dialog != nullptr && dialog->IsShown())
            return true;
    return false;
}

// The printer's id for its current job where it reports one ("0" is a print
// from its own storage), else the file it is printing.
std::string job_key(const MachineObject& machine)
{
    for (const std::string* id : {&machine.job_id_, &machine.subtask_id_, &machine.task_id_})
        if (!id->empty() && *id != "0")
            return *id;
    return machine.subtask_name;
}

// The print host's printer as the person named it: the physical printer they
// set up, else the printer profile.
std::string print_host_printer_name()
{
    PresetBundle& presets = *wxGetApp().preset_bundle;
    return presets.physical_printers.has_selection() ? presets.physical_printers.get_selected_printer_name()
                                                     : presets.printers.get_selected_preset_name();
}

} // namespace

GcodeHistoryRecorder::GcodeHistoryRecorder(Plater& plater, Agent::ProjectPersistence& persistence, Workspace::IWorkspace& workspace,
                                           std::filesystem::path managed_root, ProjectRecords project_records)
    : m_plater(plater)
    , m_persistence(persistence)
    , m_workspace(workspace)
    , m_managed_root(std::move(managed_root))
    , m_project_records(std::move(project_records))
    , m_poll(this)
{
    // Bound after the Plater's own handlers, so these run first and pass the
    // event on to them.
    m_plater.Bind(EVT_GLTOOLBAR_PRINT_PLATE, &GcodeHistoryRecorder::on_print_current, this);
    m_plater.Bind(EVT_GLTOOLBAR_SEND_GCODE, &GcodeHistoryRecorder::on_print_current, this);
    m_plater.Bind(EVT_GLTOOLBAR_SEND_TO_PRINTER, &GcodeHistoryRecorder::on_print_current, this);
    m_plater.Bind(EVT_GLTOOLBAR_PRINT_ALL, &GcodeHistoryRecorder::on_print_all, this);
    m_plater.Bind(EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL, &GcodeHistoryRecorder::on_print_all, this);
    m_plater.Bind(EVT_PRINT_FROM_SDCARD_VIEW, &GcodeHistoryRecorder::on_print_elsewhere, this);
    m_plater.Bind(EVT_GLTOOLBAR_PRINT_MULTI_MACHINE, &GcodeHistoryRecorder::on_print_elsewhere, this);
    m_plater.Bind(EVT_OUTPUT_EXPORTED, &GcodeHistoryRecorder::on_output_exported, this);
    m_plater.Bind(print_finished(m_plater), &GcodeHistoryRecorder::on_bambu_print_finished, this);
    m_plater.Bind(send_finished(m_plater), &GcodeHistoryRecorder::on_bambu_send_finished, this);

    Bind(wxEVT_TIMER, &GcodeHistoryRecorder::on_poll, this, m_poll.GetId());
    m_poll.Start(kPollMilliseconds);
}

GcodeHistoryRecorder::~GcodeHistoryRecorder()
{
    m_poll.Stop();
    unhook_print_host_queue();
    m_plater.Unbind(EVT_GLTOOLBAR_PRINT_PLATE, &GcodeHistoryRecorder::on_print_current, this);
    m_plater.Unbind(EVT_GLTOOLBAR_SEND_GCODE, &GcodeHistoryRecorder::on_print_current, this);
    m_plater.Unbind(EVT_GLTOOLBAR_SEND_TO_PRINTER, &GcodeHistoryRecorder::on_print_current, this);
    m_plater.Unbind(EVT_GLTOOLBAR_PRINT_ALL, &GcodeHistoryRecorder::on_print_all, this);
    m_plater.Unbind(EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL, &GcodeHistoryRecorder::on_print_all, this);
    m_plater.Unbind(EVT_PRINT_FROM_SDCARD_VIEW, &GcodeHistoryRecorder::on_print_elsewhere, this);
    m_plater.Unbind(EVT_GLTOOLBAR_PRINT_MULTI_MACHINE, &GcodeHistoryRecorder::on_print_elsewhere, this);
    m_plater.Unbind(EVT_OUTPUT_EXPORTED, &GcodeHistoryRecorder::on_output_exported, this);
    m_plater.Unbind(print_finished(m_plater), &GcodeHistoryRecorder::on_bambu_print_finished, this);
    m_plater.Unbind(send_finished(m_plater), &GcodeHistoryRecorder::on_bambu_send_finished, this);
}

void GcodeHistoryRecorder::hook_print_host_queue()
{
    PrintHostJobQueue* queue = &wxGetApp().printhost_job_queue();
    if (queue == m_print_host_queue)
        return;
    unhook_print_host_queue();
    m_print_host_queue = queue;
    // The upload queue runs on its own thread: the observer copies what it
    // needs and hands it to the GUI thread.
    m_print_host_queue->set_enqueue_observer([this](std::size_t id, const PrintHostJob& job) {
        CallAfter([this, id, host = job.printhost->get_host(), file_name = into_u8(from_path(job.upload_data.upload_path.filename())),
                   start = job.upload_data.post_action == PrintHostPostUploadAction::StartPrint] {
            on_print_host_queued(id, host, file_name, start);
        });
    });
    m_print_host_dialog = wxGetApp().mainframe->printhost_queue_dlg();
    m_print_host_dialog->Bind(EVT_PRINTHOST_PROGRESS, &GcodeHistoryRecorder::on_print_host_progress, this);
    m_print_host_dialog->Bind(EVT_PRINTHOST_ERROR, &GcodeHistoryRecorder::on_print_host_ended, this);
    m_print_host_dialog->Bind(EVT_PRINTHOST_CANCEL, &GcodeHistoryRecorder::on_print_host_ended, this);
}

void GcodeHistoryRecorder::unhook_print_host_queue()
{
    // A queue made again since is not this recorder's to change.
    if (m_print_host_queue != nullptr && m_print_host_queue == &wxGetApp().printhost_job_queue())
        m_print_host_queue->set_enqueue_observer({});
    m_print_host_queue = nullptr;
    if (m_print_host_dialog) {
        m_print_host_dialog->Unbind(EVT_PRINTHOST_PROGRESS, &GcodeHistoryRecorder::on_print_host_progress, this);
        m_print_host_dialog->Unbind(EVT_PRINTHOST_ERROR, &GcodeHistoryRecorder::on_print_host_ended, this);
        m_print_host_dialog->Unbind(EVT_PRINTHOST_CANCEL, &GcodeHistoryRecorder::on_print_host_ended, this);
    }
    m_print_host_dialog = nullptr;
    m_uploads.clear();
}

std::vector<GcodeHistoryRecorder::Leaving> GcodeHistoryRecorder::leaving(int plate) const
{
    PartPlateList& plates = m_plater.get_partplate_list();
    const Workspace::WorkspaceSnapshot snapshot = m_workspace.snapshot();
    std::vector<int> indices;
    if (plate == PLATE_ALL_IDX) {
        // Every plate that holds a slice went out in the one file.
        for (int index = 0; index < plates.get_plate_count(); ++index)
            if (plates.get_plate(index)->is_slice_result_valid())
                indices.push_back(index);
    } else {
        indices.push_back(plate == PLATE_CURRENT_IDX ? plates.get_curr_plate_index() : plate);
    }

    std::vector<Leaving> result;
    for (int index : indices) {
        PartPlate* part_plate = plates.get_plate(index);
        if (part_plate == nullptr || !part_plate->is_slice_result_valid() || part_plate->get_slice_result() == nullptr)
            throw std::logic_error("plate " + std::to_string(index) + " left the project without a slice");
        const GCodeProcessorResult& sliced = *part_plate->get_slice_result();
        // The plate's G-code as the slicer wrote it, as the export_file tool
        // copies it: what each route then wraps or post-processes differs.
        const std::string path = !sliced.filename.empty() ? sliced.filename : part_plate->get_tmp_gcode_path();
        std::optional<Agent::GcodeRecord> facts = Agent::gcode_record(snapshot, std::size_t(index));
        if (!facts)
            throw std::logic_error("plate " + std::to_string(index) + " is not in the workspace snapshot");
        facts->hash = Agent::sha256_file_hex(path);
        // The preview's layer count: a move's layer id counts from the first.
        for (const GCodeProcessorResult::MoveVertex& move : sliced.moves)
            facts->statistics.layer_count = std::max<std::uint64_t>(facts->statistics.layer_count, move.layer_id + 1);
        result.push_back({std::move(*facts), path});
    }
    return result;
}

void GcodeHistoryRecorder::record(const std::vector<Leaving>& leaving, GcodeDestination destination,
                                  const std::optional<Agent::KeptPackage>& uploaded)
{
    Agent::ProjectStateDocument& document = m_persistence.document();
    if (!document.has_identity())
        throw std::logic_error("a G-code left a project that has no identity");
    const std::string project_dir = this->project_dir(document.project_id());
    const std::string now         = m_persistence.timestamp();
    for (const Leaving& item : leaving) {
        Agent::keep_gcode_copy(project_dir, item.facts.hash, item.gcode_path);
        GcodeDestination kept = destination;
        // A package of several plates would print the first again, not this one.
        if (uploaded && leaving.size() == 1)
            kept.package_hash = Agent::keep_gcode_package(project_dir, item.facts.hash, uploaded->package, uploaded->config);
        const std::string id = document.add_gcode_destination(item.facts, kept, now);
        await_start(document.project_id(), item.facts.hash, id, kept);
    }
    m_persistence.commit();
    m_persistence.notify_ledger_changed();
    if (m_changed)
        m_changed();
}

void GcodeHistoryRecorder::await_start(const std::string& project_id, const std::string& hash, const std::string& destination_id,
                                       const GcodeDestination& destination)
{
    if (destination.kind != GcodeDestination::Kind::Send || !destination.start_requested || destination.transport != "bambu_print")
        return;
    MachineObject* machine = wxGetApp().getDeviceManager()->get_my_machine(destination.printer_id);
    PrintOutcomeTracker::AwaitedSend send{project_id, hash, destination_id, destination.printer_id, destination.printer_name};
    if (machine != nullptr) {
        send.state_at_send   = printer_state(*machine);
        send.job_key_at_send = job_key(*machine);
    }
    m_tracker.await(std::move(send), PrintOutcomeTracker::Clock::now());
}

void GcodeHistoryRecorder::record_reprint(const Reprint& reprint, GcodeDestination destination)
{
    destination.package_hash = reprint.package_hash;
    std::string id;
    update_project(reprint.project_id, [&](Agent::ProjectStateDocument& document) {
        const std::optional<Agent::GcodeRecord> record = document.gcode_record(reprint.hash);
        if (!record)
            throw std::logic_error("project " + reprint.project_id + " has no record of the G-code it printed again");
        id = document.add_gcode_destination(*record, destination, m_persistence.timestamp());
    });
    // Printed again: the newest of the kept copies.
    const std::string dir = project_dir(reprint.project_id);
    Agent::keep_gcode_copy(dir, reprint.hash, Agent::kept_gcode_copy(dir, reprint.hash));
    await_start(reprint.project_id, reprint.hash, id, destination);
}

std::string GcodeHistoryRecorder::project_dir(const std::string& project_id) const
{
    return (m_managed_root / std::filesystem::u8path(project_id)).u8string();
}

Agent::GcodeRecord GcodeHistoryRecorder::project_record(const std::string& project_id, const std::string& hash) const
{
    const Agent::ProjectStateDocument& open = m_persistence.document();
    std::optional<Agent::GcodeRecord>  record;
    if (open.has_identity() && open.project_id() == project_id) {
        record = open.gcode_record(hash);
    } else {
        Workspace::ProjectVersionStore store(m_managed_root, project_id);
        Agent::ProjectStateDocument    document;
        if (document.load(store.current_document()) != Agent::ProjectStateDocument::LoadResult::Loaded)
            throw std::runtime_error("project " + project_id + " has no readable document");
        record = document.gcode_record(hash);
    }
    if (!record)
        throw std::logic_error("project " + project_id + " has no record of G-code " + hash);
    return *record;
}

bool GcodeHistoryRecorder::intact(const std::string& path, const std::string& expected) const
{
    const std::filesystem::path file = std::filesystem::u8path(path);
    const std::uintmax_t        size = std::filesystem::file_size(file);
    const auto                  written = std::filesystem::last_write_time(file);
    FileCheck&                  check = m_file_checks[path];
    if (check.hash.empty() || check.size != size || check.written != written)
        check = {size, written, Agent::sha256_file_hex(path)};
    return !expected.empty() && check.hash == expected;
}

bool GcodeHistoryRecorder::kept_copy_intact(const std::string& project_id, const std::string& hash) const
{
    const std::string copy = kept_copy(project_id, hash);
    return !copy.empty() && intact(copy, hash);
}

namespace {

// The print hosts whose upload OrcaSlicer sends through its standard dialog.
// The others draw their own (ElegooLink, Creality Print, Flashforge's local
// API) or take a .gcode.3mf (use_3mf), which only a Bambu send keeps.
bool standard_print_host(const DynamicPrintConfig& printer)
{
    if (printer.opt_string("print_host").empty())
        return false;
    const auto* type    = printer.option<ConfigOptionEnum<PrintHostType>>("host_type");
    const auto* use_3mf = printer.option<ConfigOptionBool>("use_3mf");
    if (use_3mf != nullptr && use_3mf->value)
        return false;
    return type == nullptr || (type->value != htElegooLink && type->value != htCrealityPrint && type->value != htFlashforge);
}

} // namespace

GcodeHistoryRecorder::PrintAgain GcodeHistoryRecorder::print_again_state(const std::string& project_id,
                                                                         const Agent::GcodeRecord& record) const
{
    const std::string dir  = project_dir(project_id);
    const std::string copy = Agent::kept_gcode_copy(dir, record.hash);
    if (copy.empty())
        return PrintAgain::NotKept;
    PresetBundle& presets = *wxGetApp().preset_bundle;
    if (presets.use_bbl_network()) {
        const std::optional<Agent::KeptPackage> package = Agent::kept_gcode_package(dir, record.hash);
        if (!package)
            return PrintAgain::NotForThisPrinter;
        return intact(package->package, Agent::kept_package_hash(record)) ? PrintAgain::Ready : PrintAgain::Changed;
    }
    const DynamicPrintConfig& printer = presets.printers.get_edited_preset().config;
    if (printer.opt_string("print_host").empty())
        return PrintAgain::NoPrinter;
    if (!standard_print_host(printer))
        return PrintAgain::NotForThisPrinter;
    return intact(copy, record.hash) ? PrintAgain::Ready : PrintAgain::Changed;
}

void GcodeHistoryRecorder::print_again(const std::string& project_id, const std::string& hash)
{
    const Agent::GcodeRecord record = project_record(project_id, hash);
    if (print_again_state(project_id, record) != PrintAgain::Ready)
        throw std::logic_error("Print again of G-code " + hash + " was asked for when it is not ready");
    const std::string dir = project_dir(project_id);
    if (!wxGetApp().preset_bundle->use_bbl_network()) {
        send_again_to_print_host(project_id, hash, Agent::kept_gcode_copy(dir, hash), record);
        return;
    }

    // Select Machine in its print-from-storage mode reads the plate, its
    // picture and its filaments from a parsed .gcode.3mf, as the printer's
    // file browser opens it; the dialog's new-print flag uploads the local
    // file instead of printing one on the printer by name.
    const Agent::KeptPackage package = *Agent::kept_gcode_package(dir, hash);
    DynamicPrintConfig       config;
    Model                    model;
    PlateDataPtrs            plates;
    Semver                   version;
    std::ifstream            file(std::filesystem::u8path(package.package), std::ios::binary);
    if (!load_gcode_3mf_from_stream(file, &config, &model, &plates, &version) || plates.empty())
        throw std::runtime_error("the kept package of G-code " + hash + " could not be read");
    std::string name = std::filesystem::u8path(Agent::gcode_display_name(record)).stem().u8string();
    m_plater.update_print_required_data(config, model, plates, name + ".gcode.3mf", package.package);
    auto* dialog = dynamic_cast<SelectMachineDialog*>(m_plater.get_select_machine_dialog());
    if (dialog == nullptr)
        throw std::logic_error("OrcaSlicer made no Select Machine dialog for the print-required data");
    dialog->set_required_data_new_print(package.config);
    m_reprint         = Reprint{project_id, hash, Agent::kept_package_hash(record)};
    m_opening_reprint = true;
    SimpleEvent open(EVT_PRINT_FROM_SDCARD_VIEW);
    m_plater.GetEventHandler()->ProcessEvent(open);
    m_opening_reprint = false;
    dialog->set_required_data_new_print(std::nullopt);
}

void GcodeHistoryRecorder::send_again_to_print_host(const std::string& project_id, const std::string& hash, const std::string& copy,
                                                    const Agent::GcodeRecord& record)
{
    // OrcaSlicer's standard upload dialog, as Plater::send_gcode_legacy shows
    // it, for the kept copy rather than an export of the plate.
    PrintHostJob upload(&wxGetApp().preset_bundle->printers.get_edited_preset().config);
    if (upload.empty())
        throw std::logic_error("Print again was offered for a printer with no print host");
    wxArrayString groups, storage_paths, storage_names;
    {
        wxBusyCursor wait;
        upload.printhost->get_groups(groups);
        try {
            upload.printhost->get_storage(storage_paths, storage_names);
        } catch (const Slic3r::IOError& error) {
            // As OrcaSlicer's own send says it: the host could not be asked.
            show_error(&m_plater, error.what(), false);
            return;
        }
    }
    std::string name = std::filesystem::u8path(Agent::gcode_display_name(record)).stem().u8string() + ".gcode";
    AppConfig&  config = *wxGetApp().app_config;
    PrintHostSendDialog dialog(boost::filesystem::path(name), upload.printhost->get_post_upload_actions(), groups, storage_paths,
                               storage_names, config.get_bool("open_device_tab_post_upload"));
    dialog.init();
    if (dialog.ShowModal() != wxID_OK)
        return;
    config.set_bool("open_device_tab_post_upload", dialog.switch_to_device_tab());
    upload.switch_to_device_tab        = dialog.switch_to_device_tab();
    upload.upload_data.upload_path     = dialog.filename();
    upload.upload_data.post_action     = dialog.post_action();
    upload.upload_data.group           = dialog.group();
    upload.upload_data.storage         = dialog.storage();
    upload.upload_data.extended_info   = dialog.extendedInfo();
    // The queue deletes what it uploads: it gets a copy, the kept file stays.
    const boost::filesystem::path sent = boost::filesystem::temp_directory_path() /
                                         boost::filesystem::unique_path("jusprin-print-again-%%%%%%.gcode");
    std::filesystem::copy_file(std::filesystem::u8path(copy), std::filesystem::path(sent.wstring()));
    upload.upload_data.source_path = sent;
    hook_print_host_queue();
    m_reprint = Reprint{project_id, hash, {}};
    wxGetApp().printhost_job_queue().enqueue(std::move(upload));
}

void GcodeHistoryRecorder::update_project(const std::string& project_id,
                                          const std::function<void(Agent::ProjectStateDocument&)>& edit)
{
    Agent::ProjectStateDocument& open = m_persistence.document();
    if (open.has_identity() && open.project_id() == project_id) {
        edit(open);
        m_persistence.commit();
        m_persistence.notify_ledger_changed();
    } else {
        // A print of a project that is not open finishes all the same: its
        // document is written where it lies.
        Workspace::ProjectVersionStore store(m_managed_root, project_id);
        Agent::ProjectStateDocument    document;
        if (document.load(store.current_document()) != Agent::ProjectStateDocument::LoadResult::Loaded)
            throw std::runtime_error("project " + project_id + " has no readable document to record a print in");
        edit(document);
        store.publish_document(document.dump());
    }
    if (m_changed)
        m_changed();
}

std::string GcodeHistoryRecorder::kept_copy(const std::string& project_id, const std::string& hash) const
{
    return Agent::kept_gcode_copy((m_managed_root / std::filesystem::u8path(project_id)).u8string(), hash);
}

void GcodeHistoryRecorder::export_kept_copy(const std::string& project_id, const std::string& hash,
                                            const std::filesystem::path& target)
{
    const std::string copy = kept_copy(project_id, hash);
    if (copy.empty())
        throw std::logic_error("project " + project_id + " keeps no copy of G-code " + hash + " to export");
    std::filesystem::copy_file(std::filesystem::u8path(copy), target, std::filesystem::copy_options::overwrite_existing);
    GcodeDestination destination;
    destination.kind      = GcodeDestination::Kind::Export;
    destination.path      = target.u8string();
    destination.file_hash = Agent::sha256_file_hex(destination.path);
    update_project(project_id, [&](Agent::ProjectStateDocument& document) {
        const std::optional<Agent::GcodeRecord> record = document.gcode_record(hash);
        if (!record)
            throw std::logic_error("project " + project_id + " has no record of G-code " + hash);
        document.add_gcode_destination(*record, destination, m_persistence.timestamp());
    });
}

std::vector<Home::LivePrint> live_prints()
{
    std::vector<Home::LivePrint> live;
    DeviceManager* devices = wxGetApp().getDeviceManager();
    if (devices == nullptr)
        return live;
    for (const auto& [id, machine] : devices->get_my_machine_list())
        if (machine != nullptr && PrinterSetup::has_recent_printer_data(*machine) &&
            printer_state(*machine) == PrintOutcomeTracker::PrinterState::Printing)
            live.push_back({id, job_key(*machine), machine->mc_print_percent, machine->mc_left_time});
    return live;
}

void GcodeHistoryRecorder::on_print_current(wxEvent& event)
{
    event.Skip();
    hook_print_host_queue();
    m_send_plates = PLATE_CURRENT_IDX;
    m_reprint.reset();
}

void GcodeHistoryRecorder::on_print_all(wxEvent& event)
{
    event.Skip();
    hook_print_host_queue();
    m_send_plates = PLATE_ALL_IDX;
    m_reprint.reset();
}

void GcodeHistoryRecorder::on_print_elsewhere(wxEvent& event)
{
    event.Skip();
    m_send_plates.reset();
    // Print again opens Select Machine through the same event as the
    // printer's file browser does; only the browser's is not a reprint.
    if (!m_opening_reprint)
        m_reprint.reset();
}

void GcodeHistoryRecorder::on_output_exported(wxCommandEvent& event)
{
    event.Skip();
    GcodeDestination destination;
    destination.kind = GcodeDestination::Kind::Export;
    destination.path = into_u8(event.GetString());
    // OrcaSlicer reports a sliced 3MF export finished even when writing it
    // failed; with no file there, nothing left the project.
    if (!std::filesystem::exists(std::filesystem::u8path(destination.path)))
        return;
    destination.file_hash = Agent::sha256_file_hex(destination.path);
    record(leaving(event.GetInt()), std::move(destination));
}

void GcodeHistoryRecorder::on_bambu_print_finished(wxCommandEvent& event)
{
    event.Skip();
    record_bambu_job(event, true);
}

void GcodeHistoryRecorder::on_bambu_send_finished(wxCommandEvent& event)
{
    event.Skip();
    record_bambu_job(event, false);
}

void GcodeHistoryRecorder::record_bambu_job(const wxCommandEvent& event, bool starts_print)
{
    // A Print again's job: its record's, once Select Machine sent it.
    if (m_reprint) {
        if (!starts_print || !shown<SelectMachineDialog>())
            return;
        const Reprint reprint = *std::exchange(m_reprint, std::nullopt);
        MachineObject* machine = wxGetApp().getDeviceManager()->get_my_machine(into_u8(event.GetString()));
        if (machine == nullptr)
            throw std::logic_error("a Bambu job finished on a printer the device list does not hold");
        GcodeDestination destination;
        destination.kind            = GcodeDestination::Kind::Send;
        destination.printer_name    = machine->get_dev_name();
        destination.printer_id      = machine->get_dev_id();
        destination.transport       = "bambu_print";
        destination.start_requested = true;
        destination.delivered_at    = m_persistence.timestamp();
        record_reprint(reprint, std::move(destination));
        return;
    }
    // A job this project's plates did not start: a print from the printer's
    // own storage, a send made outside the print and send actions, or the
    // test file OrcaSlicer sends to check a printer's IP address.
    if (!m_send_plates || !(starts_print ? shown<SelectMachineDialog>() : shown<SendToPrinterDialog>()))
        return;
    const int plate = *m_send_plates;
    m_send_plates.reset();

    DeviceManager& devices = *wxGetApp().getDeviceManager();
    // A print names its printer; an upload to storage names its file and was
    // made to the selected printer.
    MachineObject* machine = starts_print ? devices.get_my_machine(into_u8(event.GetString())) : devices.get_selected_machine();
    if (machine == nullptr)
        throw std::logic_error("a Bambu job finished on a printer the device list does not hold");
    GcodeDestination destination;
    destination.kind            = GcodeDestination::Kind::Send;
    destination.printer_name    = machine->get_dev_name();
    destination.printer_id      = machine->get_dev_id();
    destination.transport       = starts_print ? "bambu_print" : "bambu_storage";
    destination.file_name       = starts_print ? std::string() : into_u8(event.GetString());
    destination.start_requested = starts_print;
    destination.delivered_at    = m_persistence.timestamp();
    if (!starts_print) {
        record(leaving(plate), std::move(destination));
        return;
    }
    // The package the print uploaded, and for a cloud printer the config
    // 3MF it also sent (SelectMachineDialog::on_send_print): kept as they
    // went, so Print again sends the same bytes. The next send overwrites
    // both files, so they are kept now.
    PrintPrepareData job;
    m_plater.get_print_job_data(&job);
    const std::string package = into_u8(from_path(job._3mf_path));
    if (!std::filesystem::exists(std::filesystem::u8path(package)))
        throw std::logic_error("a Bambu print finished without the package it uploaded: " + package);
    const std::string config = machine->is_lan_mode_printer() ? std::string() : into_u8(from_path(job._3mf_config_path));
    record(leaving(plate), std::move(destination), Agent::KeptPackage{package, config});
}

void GcodeHistoryRecorder::on_print_host_queued(std::size_t id, std::string host, std::string file_name, bool start)
{
    if (!m_send_plates && !m_reprint)
        return;
    PendingUpload upload;
    if (m_reprint) {
        // A Print again uploads its record's kept copy.
        upload.reprint = std::exchange(m_reprint, std::nullopt);
    } else {
        // The upload's G-code was exported from the plates as they are now.
        upload.leaving = leaving(*m_send_plates);
        m_send_plates.reset();
    }
    upload.destination.kind            = GcodeDestination::Kind::Send;
    upload.destination.printer_name    = print_host_printer_name();
    upload.destination.printer_id      = std::move(host);
    upload.destination.transport       = "print_host";
    upload.destination.file_name       = std::move(file_name);
    upload.destination.start_requested = start;
    m_uploads[id]                      = std::move(upload);
}

void GcodeHistoryRecorder::on_print_host_progress(wxEvent& base)
{
    base.Skip();
    auto& event = static_cast<PrintHostQueueDialog::Event&>(base);
    const auto upload = m_uploads.find(event.job_id);
    if (event.progress < 100 || upload == m_uploads.end())
        return;
    PendingUpload done = std::move(upload->second);
    m_uploads.erase(upload);
    done.destination.delivered_at = m_persistence.timestamp();
    if (done.reprint)
        record_reprint(*done.reprint, std::move(done.destination));
    else
        record(done.leaving, std::move(done.destination));
}

void GcodeHistoryRecorder::on_print_host_ended(wxEvent& base)
{
    base.Skip();
    // An upload that failed or was cancelled never left.
    m_uploads.erase(static_cast<PrintHostQueueDialog::Event&>(base).job_id);
}

void GcodeHistoryRecorder::follow_unfinished_prints()
{
    m_followed_saved_prints = true;
    for (const auto& [project_id, records] : m_project_records())
        for (const Agent::GcodeRecord& record : records)
            for (const Agent::GcodePrint& print : record.prints)
                if (print.finished_at.empty())
                    m_tracker.follow({project_id, record.hash, print.id, print.printer_id, print.job_id});
}

void GcodeHistoryRecorder::on_poll(wxTimerEvent&)
{
    DeviceManager* devices = wxGetApp().getDeviceManager();
    if (devices == nullptr)
        return;
    // Prints that were running when the app last closed are followed again
    // once, the first time a printer is there to report on them.
    const auto machines = devices->get_my_machine_list();
    if (!m_followed_saved_prints && !machines.empty())
        follow_unfinished_prints();
    if (m_tracker.idle())
        return;

    std::vector<PrintOutcomeTracker::PrinterReport> reports;
    for (const auto& [id, machine] : machines)
        if (machine != nullptr && PrinterSetup::has_recent_printer_data(*machine))
            reports.push_back({id, printer_state(*machine), job_key(*machine)});

    PrintOutcomeTracker::Writer writer;
    writer.start = [this](const PrintOutcomeTracker::AwaitedSend& send, const std::string& key) {
        std::string print_id;
        update_project(send.project_id, [&](Agent::ProjectStateDocument& document) {
            Agent::GcodePrint print;
            print.destination_id = send.destination_id;
            print.printer_name   = send.printer_name;
            print.printer_id     = send.printer_id;
            print.job_id         = key;
            print_id             = document.add_gcode_print(send.hash, std::move(print), m_persistence.timestamp());
        });
        return print_id;
    };
    writer.finish = [this](const PrintOutcomeTracker::FollowedPrint& print) {
        update_project(print.project_id, [&](Agent::ProjectStateDocument& document) {
            document.finish_gcode_print(print.hash, print.print_id, m_persistence.timestamp());
        });
    };
    m_tracker.observe(reports, writer, PrintOutcomeTracker::Clock::now());
}

} // namespace Slic3r::GUI::JusPrin
