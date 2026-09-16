#pragma once

// Answers OrcaSlicer's own dialogs on behalf of an operation the person has
// already decided through an approval card. Orca asks its questions in
// modals deep inside its load paths; rather than a flag at every site, the
// one place they all open (DPIAware::ShowModal) asks the innermost scope
// here, and the scope records each question and the answer it gave.
//
// GUI thread only. Scopes nest; the innermost answers.

#include <wx/string.h>

#include <functional>
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r::GUI::JusPrin::Workspace {

struct ModalRecord
{
    std::string title; // UTF-8, as the dialog is titled in the current language
    int         answer{0};
};

class ScopedModalAnswers
{
public:
    // Returns the wx id the dialog's ShowModal would have returned.
    using Decide = std::function<int(wxWindow& dialog, const wxString& title)>;

    explicit ScopedModalAnswers(Decide decide);
    ~ScopedModalAnswers();
    ScopedModalAnswers(const ScopedModalAnswers&)            = delete;
    ScopedModalAnswers& operator=(const ScopedModalAnswers&) = delete;

    const std::vector<ModalRecord>& records() const { return m_records; }

private:
    friend bool answer_through_scope(wxWindow& dialog, int& answer);
    Decide                   m_decide;
    std::vector<ModalRecord> m_records;
    ScopedModalAnswers*      m_outer{nullptr};
};

} // namespace Slic3r::GUI::JusPrin::Workspace
