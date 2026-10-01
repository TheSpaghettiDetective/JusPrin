#pragma once

// Listens to OrcaSlicer's own dialogs. Every Orca dialog opens through
// DPIAware::ShowModal, which first asks answer_modal (GUI_Utils.hpp); that
// hands the dialog to the listeners registered here, innermost first. A
// listener answers the dialog, and it is never shown, or passes it on; when
// every listener has passed, Orca shows it as usual.
//
// A listener learns what the window itself shows: its title, its message and
// which of the standard buttons it offers. Nothing is matched against the
// words, which are Orca's and in the interface language.
//
// GUI thread only.

#include "Workspace.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r::GUI::JusPrin::Workspace {

struct DialogFacts
{
    std::string title;
    std::string text;
    bool        ok{false}, yes{false}, no{false}, cancel{false};
    // Orca's STEP import settings, recognised by type: its Cancel abandons
    // the whole load, so it is the one dialog whose least change is OK.
    bool        step_import{false};
};

DialogFacts read_dialog(wxWindow& dialog);

// The choice that changes least, from the buttons alone: No, else Cancel,
// else OK, else Yes (Orca offers a lone Yes only where it acts regardless).
// A dialog with none of them is cancelled and marked unrecognised.
OrcaMessage least_change_answer(const DialogFacts& facts, int& answer);

class ScopedDialogListener
{
public:
    // Returns the id ShowModal would have returned, or nothing to pass.
    using Listener = std::function<std::optional<int>(wxWindow& dialog, const DialogFacts& facts)>;

    explicit ScopedDialogListener(Listener listener);
    ~ScopedDialogListener();
    ScopedDialogListener(const ScopedDialogListener&)            = delete;
    ScopedDialogListener& operator=(const ScopedDialogListener&) = delete;

private:
    friend bool dispatch_dialog(wxWindow& dialog, int& answer);
    Listener              m_listener;
    ScopedDialogListener* m_outer{nullptr};
};

// Answers every dialog with the least change and writes each one down: the
// file load's listener, and the safety net around a tool's Orca calls.
class ScopedLeastChangeAnswers
{
public:
    explicit ScopedLeastChangeAnswers(bool prefer_cancel = false);
    const std::vector<OrcaMessage>& messages() const { return m_messages; }
    std::vector<OrcaMessage>        take_messages() { return std::move(m_messages); }
    void                            add_message(OrcaMessage message) { m_messages.push_back(std::move(message)); }

private:
    std::vector<OrcaMessage> m_messages;
    ScopedDialogListener     m_listener; // after m_messages, which it writes to
};

} // namespace Slic3r::GUI::JusPrin::Workspace
