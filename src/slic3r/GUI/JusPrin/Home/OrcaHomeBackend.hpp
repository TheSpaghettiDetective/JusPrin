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

    bool                      dark() const override;
    std::vector<ProjectEntry> recent_projects() const override;
    std::vector<PrinterEntry> printers() const override;

    void open_project(const std::string& project_id) override;
    void new_project() override;
    void import_model() override;
    void launch_monitor(const std::string& printer_id) override;
    void add_printer() override;

private:
    MainFrame&             m_frame;
    Workspace::SpoolStore* m_spools{nullptr};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
