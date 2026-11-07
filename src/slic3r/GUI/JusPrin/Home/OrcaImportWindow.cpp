// First: slic3r/GUI/I18N.hpp defines _L only while the _ macro is still
// undefined, so it has to precede any header that brings one in.
#include "slic3r/GUI/I18N.hpp"

#include "OrcaImportWindow.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <nlohmann/json.hpp>

#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Home {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// "~/Library/Application Support/OrcaSlicer": the folder as the person knows
// it, under their home folder.
std::string shown_path(const fs::path& path)
{
    const std::string text = path.u8string();
    const std::string home = into_u8(wxGetHomeDir());
    if (!home.empty() && text.compare(0, home.size(), home) == 0 && (text.size() == home.size() || text[home.size()] == '/'))
        return "~" + text.substr(home.size());
    return text;
}

} // namespace

OrcaImportWindow::OrcaImportWindow(MainFrame& frame, const ShellTheme& theme, Workspace::IWorkspace& workspace)
    : wxDialog(&frame, wxID_ANY, _L("Import from OrcaSlicer"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_importer(frame)
    , m_looked_in(fs::u8path(data_dir()).parent_path() / "OrcaSlicer")
{
    SetName("JusPrin Import from OrcaSlicer");
    SetClientSize(FromDIP(wxSize(theme.metrics().orca_import.width, theme.metrics().orca_import.height)));
    SetSizer(new wxBoxSizer(wxVERTICAL));
    look(std::nullopt);

    // Its own document, never the project's: the window holds no chat.
    Agent::ProjectPersistence::Config storage;
    storage.in_memory = true;
    m_persistence     = std::make_unique<Agent::ProjectPersistence>(workspace, std::move(storage));
    m_view = new AgentWebView(this, theme, workspace, *m_persistence, Agent::load_agent_runtime(wxGetApp().app_config).availability,
                              {}, {}, AgentPageMode::OrcaImport);
    m_view->SetName(_L("Import from OrcaSlicer"));
    GetSizer()->Add(m_view, 1, wxEXPAND);

    Agent::AgentHost& host = m_view->host();
    host.set_session_state_provider([this] { return state(); });
    host.set_page_message_handler([this](const std::string& type, const json& payload) { return on_page_message(type, payload); });

    const bool dark = GUI_App::dark_mode();
    SetBackgroundColour(theme.palette(dark).surface_raised);
    m_view->apply_appearance(dark);
    wxGetApp().UpdateDlgDarkUI(this);
    CentreOnParent();
}

OrcaImportWindow::~OrcaImportWindow()
{
    // The host inside the view holds this window's document by reference,
    // and wx would destroy the view only after the document is gone.
    if (m_view != nullptr) {
        m_view->host().set_session_state_provider({});
        m_view->host().set_page_message_handler({});
        delete m_view;
        m_view = nullptr;
    }
    m_persistence.reset();
}

bool OrcaImportWindow::imported() const
{
    return m_result && std::any_of(m_result->begin(), m_result->end(),
                                   [](const OrcaImportItem& item) { return item.outcome == OrcaImportItem::Outcome::Imported; });
}

void OrcaImportWindow::look(const std::optional<fs::path>& picked)
{
    m_source = m_importer.detect(picked);
    m_printers.clear();
    m_new_printers = m_new_filaments = m_new_processes = 0;
    // scan_orca_presets lists the printers' files first, then the
    // filaments' and the processes'.
    const OrcaImportCatalog  catalog   = m_importer.catalog();
    const OrcaPresetFiles&   presets   = m_source.presets;
    const std::size_t        filaments = presets.printers.size() + presets.filaments.size();
    for (std::size_t index = 0; index < presets.files.size(); ++index) {
        const bool there = orca_preset_in_jusprin(presets.files[index], catalog);
        if (index < presets.printers.size())
            m_printers.emplace_back(presets.printers[index], there);
        int& count = index < presets.printers.size() ? m_new_printers : index < filaments ? m_new_filaments : m_new_processes;
        count += there ? 0 : 1;
    }
    for (const OrcaImportItem& item : m_source.ticked) {
        const bool there = item.outcome == OrcaImportItem::Outcome::AlreadyThere;
        m_printers.emplace_back(item.name, there);
        m_new_printers += there ? 0 : 1;
    }
}

json OrcaImportWindow::state() const
{
    json payload{{"kind", "orca_import"}, {"error", m_error}};
    if (m_result) {
        payload["screen"] = "imported";
        json lines        = json::array();
        for (const OrcaImportItem& item : *m_result)
            if (item.outcome != OrcaImportItem::Outcome::AlreadyThere)
                lines.push_back({{"kind", orca_import_kind(item.kind)},
                                 {"name", item.name},
                                 {"renamedTo", item.renamed_to},
                                 {"imported", item.outcome == OrcaImportItem::Outcome::Imported},
                                 {"reason", orca_import_reason(item)}});
        payload["result"] = std::move(lines);
        return payload;
    }
    if (m_source.root.empty()) {
        payload["screen"] = "no_folder";
        payload["folder"] = shown_path(m_looked_in);
        return payload;
    }
    payload["folder"] = shown_path(fs::u8path(m_source.root));
    if (m_printers.empty()) {
        payload["screen"] = "no_printers";
        return payload;
    }
    const bool nothing_new = m_new_printers + m_new_filaments + m_new_processes == 0;
    payload["screen"]       = nothing_new ? "nothing_new" : "found";
    json printers           = json::array();
    for (const auto& [name, there] : m_printers)
        printers.push_back({{"name", name}, {"already", there}});
    payload["printers"]     = std::move(printers);
    payload["newPrinters"]  = m_new_printers;
    payload["newFilaments"] = m_new_filaments;
    return payload;
}

bool OrcaImportWindow::on_page_message(const std::string& type, const json& payload)
{
    if (type != Agent::Protocol::kShellAction || !payload.is_object())
        return false;
    const std::string action = payload.value("action", std::string());
    // Each runs once the page's message is answered: closing destroys the
    // view that sent it, and the other two open windows of their own.
    if (action == "close")
        CallAfter([this] { EndModal(wxID_OK); });
    else if (action == "import")
        CallAfter([this, printers = payload.at("printers").get<std::vector<std::string>>()] { import(printers); });
    else if (action == "choose_folder")
        CallAfter([this] { choose_folder(); });
    else
        return false;
    return true;
}

void OrcaImportWindow::import(const std::vector<std::string>& printers)
{
    const OrcaImportChoice chosen = orca_import_chosen(m_source.presets, m_source.ticked, printers);
    // Installing a printer profile reloads every preset, so the project's
    // unsaved preset changes are saved or dropped first, as OrcaSlicer asks
    // before it loads other presets.
    if (m_importer.installs_profiles(chosen.profile_printers, chosen.files) &&
        !wxGetApp().check_and_save_current_preset_changes(_L("Import from OrcaSlicer"),
                                                          _L("Importing these printers loads their profiles, which replaces "
                                                             "unsaved changes to the project's settings."),
                                                          false))
        return;
    m_error.clear();
    m_result = m_importer.bring(chosen.profile_printers, chosen.files, m_source.selected, OrcaImporter::Selection::Kept);
    SetTitle(_L("Imported from OrcaSlicer"));
    m_view->host().refresh_page_state();
}

void OrcaImportWindow::choose_folder()
{
    const OrcaImporter::FolderChoice choice = m_importer.choose_folder(this);
    if (choice.folder) {
        m_error.clear();
        look(choice.folder);
    } else if (!choice.problem.empty()) {
        m_error = choice.problem;
    }
    m_view->host().refresh_page_state();
}

} // namespace Slic3r::GUI::JusPrin::Home
