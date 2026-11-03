#pragma once

// --dialog-witness <directory>: says on stderr which of OrcaSlicer's dialogs
// reached the screen, pictures each one, and then closes it with the answer
// that changes least, as a person who dismissed it would. A dialog the app
// answered or routed never shows, so it is never witnessed: this is the
// before-and-after evidence for the dialog router.
//
// Modal dialogs are seen through the app's own hook, after its listeners have
// passed them. Modeless dialogs, which Show() rather than ShowModal(), are
// found by a timer that looks at the top-level windows. A native message box
// or file picker is drawn outside wxWidgets, where the timer cannot close it:
// it is answered as it opens, and still counted as shown.

#include "slic3r/GUI/JusPrin/Workspace/DialogListeners.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"

#include <boost/filesystem/path.hpp>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef __APPLE__
bool snapshot_native_window(void* native_view, const char* path);
#endif

namespace JusPrinTest {

class DialogWitness : public Slic3r::GUI::JusPrin::Workspace::UnansweredDialogObserver
{
public:
    explicit DialogWitness(boost::filesystem::path directory) : m_directory(std::move(directory))
    {
        Register();
    }
    // Outlives the app at exit, so its timer is left to the process.
    ~DialogWitness() override { m_timer.release(); }
    // Once the app exists: a timer cannot be made before it.
    void start()
    {
        if (m_timer)
            return;
        m_timer = std::make_unique<wxTimer>();
        m_timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) { tick(); });
        m_timer->Start(300);
    }

    // Every dialog seen so far, as "title | text", in order.
    const std::vector<std::string>& shown() const { return m_shown; }

protected:
    int Enter(wxDialog* dialog) override
    {
        start();
        report("modal", *dialog);
        if (dynamic_cast<wxMessageDialogBase*>(dialog) != nullptr) {
            int answer = wxID_CANCEL;
            Slic3r::GUI::JusPrin::Workspace::least_change_answer(Slic3r::GUI::JusPrin::Workspace::read_dialog(*dialog), answer);
            std::cerr << "HARNESS DIALOG CLOSED native '" << dialog->GetTitle().ToUTF8().data() << "' answer=" << answer << '\n';
            return answer;
        }
        if (dynamic_cast<wxFileDialogBase*>(dialog) != nullptr || dynamic_cast<wxDirDialogBase*>(dialog) != nullptr) {
            std::cerr << "HARNESS DIALOG CLOSED picker answer=" << wxID_CANCEL << '\n';
            return wxID_CANCEL;
        }
        return wxID_NONE;
    }

private:
    void report(const char* kind, wxDialog& dialog)
    {
        if (!m_seen.insert(&dialog).second)
            return;
        dialog.Bind(wxEVT_DESTROY, [this, &dialog](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == &dialog) {
                m_seen.erase(&dialog);
                m_pictured.erase(&dialog);
            }
            event.Skip();
        });
        const auto facts = Slic3r::GUI::JusPrin::Workspace::read_dialog(dialog);
        std::string text = facts.text;
        for (char& c : text)
            if (c == '\n')
                c = ' ';
        m_shown.push_back(facts.title + " | " + text);
        std::cerr << "HARNESS DIALOG SHOWN " << kind << " #" << ++m_count << " title='" << facts.title << "' text='" << text
                  << "'\n";
    }

    void tick()
    {
        for (wxWindow* window : wxTopLevelWindows) {
            auto* dialog = dynamic_cast<wxDialog*>(window);
            // A progress window asks nothing and closes itself.
            if (dialog == nullptr || !dialog->IsShown() || dynamic_cast<Slic3r::GUI::ProgressDialog*>(dialog) != nullptr)
                continue;
            if (!dialog->IsModal())
                report("modeless", *dialog);
            if (m_pictured.insert(dialog).second) {
#ifdef __APPLE__
                const std::string png = (m_directory / ("dialog-" + std::to_string(m_count) + ".png")).string();
                std::cerr << "HARNESS DIALOG PICTURED " << png << ' '
                          << (snapshot_native_window(dialog->GetHandle(), png.c_str()) ? "ok" : "failed") << '\n';
#endif
                return; // closed on the next tick, once pictured
            }
            int answer = wxID_CANCEL;
            Slic3r::GUI::JusPrin::Workspace::least_change_answer(Slic3r::GUI::JusPrin::Workspace::read_dialog(*dialog), answer);
            std::cerr << "HARNESS DIALOG CLOSED '" << dialog->GetTitle().ToUTF8().data() << "' answer=" << answer << '\n';
            if (dialog->IsModal())
                dialog->EndModal(answer);
            else
                dialog->Hide();
            return;
        }
    }

    boost::filesystem::path m_directory;
    std::unique_ptr<wxTimer> m_timer;
    std::set<wxDialog*>     m_seen, m_pictured;
    std::vector<std::string> m_shown;
    int                     m_count{0};
};

} // namespace JusPrinTest
