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
namespace Slic3r::GUI::JusPrin::Workspace {
class ProjectAutosave;
}

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class OrcaHomeBackend final : public IHomeBackend
{
public:
    explicit OrcaHomeBackend(MainFrame& frame);
    ~OrcaHomeBackend() override;

    // How Home opens the printer conversation, which replaces its printers
    // column: an empty name adds a printer, a name changes that one. The
    // shell owns the panel; Home only says what the person asked about.
    using OpenConversation = std::function<void(const std::string& printer_name, bool connect)>;
    void set_conversation_opener(OpenConversation open) { m_open_conversation = std::move(open); }
    void set_autosave(Workspace::ProjectAutosave* autosave) { m_autosave = autosave; }

    bool dark() const override;
    std::vector<ProjectEntry> recent_projects() const override;
    std::vector<PrinterEntry> printers() const override;
    Snapshot::Onboarding onboarding() override;

    void open_project(const std::string& project_id) override;
    void new_project() override;
    void import_model() override;
    void launch_monitor(const std::string& printer_id) override;
    void add_printer() override;

    void begin_onboarding() override;
    void dismiss_onboarding() override;
    void defer_profile_import() override;
    void accept_partial_profile_import() override;
    std::string use_detected_profiles(const Snapshot::ProfileSelection& selection) override;
    void confirm_onboarding_setup() override;
    void back_onboarding() override;
    void choose_profile_bundle() override;
    void choose_profile_folder() override;
    std::string run_manual_setup() override;
    std::string choose_offline_example() override;
    void open_account_stub() override;
    void open_terms_stub() override;
    void open_privacy_stub() override;
    std::string open_onboarding_example() override;

    std::string open_printer_settings(const std::string& printer_id) override;
    std::string connect_printer(const std::string& printer_id) override;
    std::string rename_printer(const std::string& printer_id, const std::string& new_name) override;
    std::string remove_printer(const std::string& printer_id) override;

private:
    // A print host's own page (Mainsail, Fluidd, OctoPrint) in a window of
    // its own; the project's selected printer stays as it is.
    void open_printer_window(const std::string& name);
    OnboardingProgress onboarding_progress() const;
    OnboardingFacts onboarding_facts() const;
    bool has_usable_setup() const;
    void save_onboarding(const OnboardingProgress& progress);
    void complete_onboarding();

    MainFrame& m_frame;
    Workspace::ProjectAutosave* m_autosave{nullptr};
    OpenConversation m_open_conversation;
    std::map<std::string, wxWeakRef<PrinterWindow>> m_printer_windows; // by printer name
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
