#pragma once

// File › Import › Import from OrcaSlicer…: the Agent page in its OrcaSlicer
// import mode, in a window over whatever screen is open (canvas "JusPrin v2",
// boards orca-import-02 to 05). It finds OrcaSlicer's folder as the first
// launch's Printers step does, lists its printers with the ones JusPrin
// already has marked and skipped, and brings the rest the same way, leaving
// the open project's printer, filaments and process as they were. Modal,
// like Preferences, so nothing changes underneath it.

#include "OrcaImporter.hpp"

#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <nlohmann/json_fwd.hpp>

#include <wx/dialog.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI {
class MainFrame;
}
namespace Slic3r::GUI::JusPrin {
class AgentWebView;
namespace Workspace { class IWorkspace; }
namespace Agent { class ProjectPersistence; }
}

namespace Slic3r::GUI::JusPrin::Home {

class OrcaImportWindow : public wxDialog
{
public:
    OrcaImportWindow(MainFrame& frame, const ShellTheme& theme, Workspace::IWorkspace& workspace);
    ~OrcaImportWindow() override;

    // Something was imported, so whatever lists printers is out of date.
    bool imported() const;

private:
    // Reads the folder `picked` names, otherwise OrcaSlicer's own.
    void           look(const std::optional<std::filesystem::path>& picked);
    nlohmann::json state() const;
    bool           on_page_message(const std::string& type, const nlohmann::json& payload);
    // Brings the printers named in `printers`, as the page lists them.
    void           import(const std::vector<std::string>& printers);
    void           choose_folder();

    OrcaImporter                               m_importer;
    // Where it looked when it found no folder.
    std::filesystem::path                      m_looked_in;
    OrcaProfileSource                          m_source;
    // Each printer the folder has, and whether JusPrin has it already.
    std::vector<std::pair<std::string, bool>>  m_printers;
    // What the folder has that JusPrin doesn't.
    int                                        m_new_printers{0};
    int                                        m_new_filaments{0};
    int                                        m_new_processes{0};
    std::optional<std::vector<OrcaImportItem>> m_result;
    std::string                                m_error;
    std::unique_ptr<Agent::ProjectPersistence> m_persistence;
    AgentWebView*                              m_view{nullptr};
};

} // namespace Slic3r::GUI::JusPrin::Home
