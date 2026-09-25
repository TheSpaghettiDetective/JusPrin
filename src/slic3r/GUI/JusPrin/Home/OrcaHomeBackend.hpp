#pragma once

// The one place Home meets OrcaSlicer. Everything the gallery and the printer
// rail know is read here and handed on as the fork's own structs; every action
// Home offers is performed here through the upstream entry point that already
// performs it.
//
// Keeping this in one file is the point: MainFrame and the device layer are
// where upstream is actively moving code, so the fork re-derives this file at
// a rebase and nothing else.

#include "HomeBackend.hpp"
#include "PrinterWindow.hpp"

#include <wx/weakref.h>

#include <functional>
#include <map>

namespace Slic3r { namespace GUI {
class MainFrame;
}} // namespace Slic3r::GUI

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Workspace {
class SpoolStore;
}}}} // namespace Slic3r::GUI::JusPrin::Workspace

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class OrcaHomeBackend final : public IHomeBackend
{
public:
    // `spools` may be null: the shell owns the one store, and Home renders
    // without swatches rather than opening a second writer to the same file.
    OrcaHomeBackend(MainFrame& frame, Workspace::SpoolStore* spools);
    ~OrcaHomeBackend() override;

    // How Home opens the printer conversation, which replaces its printers
    // column: an empty name adds a printer, a name changes that one. The
    // shell owns the panel; Home only says what the person asked about.
    using OpenConversation = std::function<void(const std::string& printer_name, bool connect)>;
    void set_conversation_opener(OpenConversation open) { m_open_conversation = std::move(open); }

    bool                      dark() const override;
    std::vector<ProjectEntry> recent_projects() const override;
    std::vector<PrinterEntry> printers() const override;

    void open_project(const std::string& project_id) override;
    void new_project() override;
    void import_model() override;
    void launch_monitor(const std::string& printer_id) override;
    void add_printer() override;

    std::string open_printer_settings(const std::string& printer_id) override;
    std::string connect_printer(const std::string& printer_id) override;
    std::string rename_printer(const std::string& printer_id, const std::string& new_name) override;
    std::string remove_printer(const std::string& printer_id) override;

private:
    // A print host's own page (Mainsail, Fluidd, OctoPrint) in a window of
    // its own; the project's selected printer stays as it is.
    void open_printer_window(const std::string& name);

    MainFrame&                                         m_frame;
    Workspace::SpoolStore*                             m_spools{nullptr};
    OpenConversation                                   m_open_conversation;
    std::map<std::string, wxWeakRef<PrinterWindow>>    m_printer_windows; // by printer name
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
