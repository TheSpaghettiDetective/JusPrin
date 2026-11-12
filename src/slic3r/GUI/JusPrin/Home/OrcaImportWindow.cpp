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

OrcaImportWindow::OrcaImportWindow(MainFrame& frame, const ShellTheme& theme, Workspace::IWorkspace& workspace, const ImportApp& app)
    : wxDialog(&frame, wxID_ANY, wxString::Format(_L("Import from %s"), wxString::FromUTF8(app.name)), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE)
    , m_app(app)
    , m_importer(frame)
    , m_looked_in(import_app_folder(app))
{
    SetName(wxString::Format("JusPrin Import from %s", wxString::FromUTF8(app.name)));
    SetClientSize(FromDIP(wxSize(theme.metrics().orca_import.width, theme.metrics().orca_import.height)));
    SetSizer(new wxBoxSizer(wxVERTICAL));
    look(std::nullopt);

    m_view = new AgentWebView(this, theme, workspace, Agent::load_agent_runtime(wxGetApp().app_config).availability, {}, {},
                              AgentPageMode::OrcaImport);
    m_view->SetName(GetTitle());
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

// The page's handlers call into this window, so the view goes while the
// window is still whole rather than with wx's children after it.
OrcaImportWindow::~OrcaImportWindow() { delete m_view; }

bool OrcaImportWindow::imported() const
{
    return m_result && std::any_of(m_result->begin(), m_result->end(),
                                   [](const OrcaImportItem& item) { return item.outcome == OrcaImportItem::Outcome::Imported; });
}

void OrcaImportWindow::look(const std::optional<fs::path>& picked)
{
    m_source = m_importer.detect(m_app, picked);
    m_offer  = orca_import_offer(m_app, m_source.presets, m_source.ticked, m_importer.catalog(),
                                 [this](const std::string& base) { return m_importer.missing_base(base) != nullptr; });
}

json OrcaImportWindow::state() const
{
    json payload{{"kind", "orca_import"}, {"app", m_app.name}, {"error", m_error}};
    if (m_result) {
        payload["screen"] = "imported";
        json lines        = json::array();
        for (const OrcaImportItem& item : *m_result)
            if (item.outcome != OrcaImportItem::Outcome::AlreadyThere)
                lines.push_back({{"kind", orca_import_kind(item.kind)},
                                 {"name", item.name},
                                 {"renamedTo", item.renamed_to},
                                 {"imported", item.outcome == OrcaImportItem::Outcome::Imported},
                                 {"reason", orca_import_detail(m_app, item)}});
        payload["result"] = std::move(lines);
        return payload;
    }
    if (m_source.root.empty()) {
        payload["screen"] = "no_folder";
        payload["folder"] = shown_path(m_looked_in);
        return payload;
    }
    payload["folder"] = shown_path(fs::u8path(m_source.root));
    if (!m_source.has_printers()) {
        payload["screen"] = "no_printers";
        return payload;
    }
    payload["screen"] = m_offer.nothing_new() ? "nothing_new" : "found";
    json printers     = json::array();
    for (const auto& [name, there] : m_offer.printers)
        printers.push_back({{"name", name}, {"already", there}});
    json skipped = json::array();
    for (const OrcaImportItem& item : m_offer.skipped)
        skipped.push_back({{"kind", orca_import_kind(item.kind)},
                           {"name", item.name},
                           {"renamedTo", ""},
                           {"imported", false},
                           {"reason", orca_import_reason(m_app, item)}});
    payload["printers"]    = std::move(printers);
    payload["newPrinters"] = m_offer.new_printers;
    payload["filaments"]   = m_offer.filaments;
    payload["processes"]   = m_offer.processes;
    payload["skipped"]     = std::move(skipped);
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
    if (m_importer.installs_profiles(m_app, chosen.profile_printers, chosen.files) &&
        !wxGetApp().check_and_save_current_preset_changes(wxString::Format(_L("Import from %s"), wxString::FromUTF8(m_app.name)),
                                                          _L("Importing these printers loads their profiles, which replaces "
                                                             "unsaved changes to the project's settings."),
                                                          false))
        return;
    m_error.clear();
    m_result = m_importer.bring(m_source, chosen.profile_printers, chosen.files, OrcaImporter::Selection::Kept);
    SetTitle(wxString::Format(_L("Imported from %s"), wxString::FromUTF8(m_app.name)));
    m_view->host().refresh_page_state();
}

void OrcaImportWindow::choose_folder()
{
    const OrcaImporter::FolderChoice choice = m_importer.choose_folder(m_app, this);
    if (choice.folder) {
        m_error.clear();
        look(choice.folder);
    } else if (!choice.problem.empty()) {
        m_error = choice.problem;
    }
    m_view->host().refresh_page_state();
}

} // namespace Slic3r::GUI::JusPrin::Home
