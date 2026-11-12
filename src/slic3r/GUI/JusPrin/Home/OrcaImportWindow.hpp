#pragma once

// Printer › Import printers from OrcaSlicer… and Import printers from JusPrin 1…: the
// Agent page in its import mode, in a window over whatever screen is open
// (canvas "JusPrin v2", boards orca-import-02 to 05; JusPrin 1's "works the
// same way"). It finds the app's folder as the first launch's Printers step
// does, lists its printers with the ones JusPrin
// already has marked and skipped, and brings the rest the same way, leaving
// the open project's printer, filaments and process as they were. Modal,
// like Preferences, so nothing changes underneath it.

#include "OrcaImporter.hpp"

#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"

#include <nlohmann/json_fwd.hpp>

#include <wx/dialog.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI {
class MainFrame;
}
namespace Slic3r::GUI::JusPrin {
class AgentWebView;
namespace Workspace { class IWorkspace; }
}

namespace Slic3r::GUI::JusPrin::Home {

class OrcaImportWindow : public wxDialog
{
public:
    OrcaImportWindow(MainFrame& frame, const ShellTheme& theme, Workspace::IWorkspace& workspace, const ImportApp& app);
    ~OrcaImportWindow() override;

    // Something was imported, so whatever lists printers is out of date.
    bool imported() const;

private:
    // Reads the folder `picked` names, otherwise the app's own.
    void           look(const std::optional<std::filesystem::path>& picked);
    nlohmann::json state() const;
    bool           on_page_message(const std::string& type, const nlohmann::json& payload);
    // Brings the printers named in `printers`, as the page lists them.
    void           import(const std::vector<std::string>& printers);
    void           choose_folder();

    const ImportApp&                           m_app;
    OrcaImporter                               m_importer;
    // Where it looked when it found no folder.
    std::filesystem::path                      m_looked_in;
    OrcaProfileSource                          m_source;
    OrcaImportOffer                            m_offer;
    std::optional<std::vector<OrcaImportItem>> m_result;
    std::string                                m_error;
    AgentWebView*                              m_view{nullptr};
};

} // namespace Slic3r::GUI::JusPrin::Home
