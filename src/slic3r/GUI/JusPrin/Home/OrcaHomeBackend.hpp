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

#include <functional>
#include <optional>

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

    // How Home opens the printer conversation, which replaces its printers
    // column: an empty name adds a printer, a name changes that one. The
    // shell owns the panel; Home only says what the person asked about.
    using OpenConversation = std::function<void(const std::string& printer_name)>;
    void set_conversation_opener(OpenConversation open) { m_open_conversation = std::move(open); }

    // The printer a just-finished Add flow saved: printers() draws it first
    // and marks it just added, and printer_receipt() reads it for Home's
    // strip. The caller clears it (an empty name) once it has asked for the
    // one refresh this is for.
    void set_just_added_printer(const PrinterReceipt& receipt) { m_just_added_printer = receipt; }
    void clear_just_added_printer() { m_just_added_printer.reset(); }

    bool                           dark() const override;
    std::vector<ProjectEntry>      recent_projects() const override;
    std::vector<PrinterEntry>      printers() const override;
    std::optional<PrinterReceipt>  printer_receipt() const override { return m_just_added_printer; }

    void open_project(const std::string& project_id) override;
    void new_project() override;
    void import_model() override;
    void launch_monitor(const std::string& printer_id) override;
    void add_printer() override;

    std::string open_printer_settings(const std::string& printer_id) override;
    std::string rename_printer(const std::string& printer_id, const std::string& new_name) override;
    std::string remove_printer(const std::string& printer_id) override;

private:
    MainFrame&                     m_frame;
    Workspace::SpoolStore*         m_spools{nullptr};
    OpenConversation               m_open_conversation;
    std::optional<PrinterReceipt>  m_just_added_printer;
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
