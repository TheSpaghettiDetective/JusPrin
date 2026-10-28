#pragma once

#include <functional>
#include <wx/event.h>

class Button;
class Notebook;
class wxMenu;
class wxMenuItem;
class wxSizer;
class wxWindowDestroyEvent;

namespace Slic3r::GUI {
class MainFrame;
}

namespace Slic3r::GUI::JusPrin {

class ClassicViewBanner;
class ShellTheme;

// What JusPrin adds to stock OrcaSlicer while the shell is off: a banner above
// the Notebook and a row at the end of OrcaSlicer's View menu, both asking to
// go back to the JusPrin view. Neither touches the shell; each queues
// on_return on the frame, so the handler of a widget the return destroys has
// returned first.
class ClassicViewReturn : public wxEvtHandler
{
public:
    ClassicViewReturn(MainFrame& frame, Notebook& tabpanel, wxSizer& main_sizer, const ShellTheme& theme,
                      std::function<void()> on_return);
    ~ClassicViewReturn();

    ClassicViewReturn(const ClassicViewReturn&) = delete;
    ClassicViewReturn& operator=(const ClassicViewReturn&) = delete;

    // The banner first, then the menu row. Throws std::runtime_error when
    // OrcaSlicer's View menu is not found, with the banner left in place:
    // the banner depends on no menu. A broken precondition is a
    // std::logic_error.
    void install();
    void uninstall();

    // For the integration harness.
    wxWindow* banner() const;
    Button* back_button() const;
    wxMenu* view_menu() const { return m_view_menu; }
    wxMenuItem* menu_row() const { return m_menu_row; }

private:
    wxMenu* find_view_menu() const;
    void on_frame_destroy(wxWindowDestroyEvent& event);
    void on_menu_row(wxCommandEvent& event);

    MainFrame& m_frame;
    Notebook& m_tabpanel;
    wxSizer& m_main_sizer;
    const ShellTheme& m_theme;
    std::function<void()> m_on_return;
    bool m_installed{false};
    ClassicViewBanner* m_banner{nullptr};
    wxMenu* m_view_menu{nullptr};
    wxMenuItem* m_menu_separator{nullptr};
    wxMenuItem* m_menu_row{nullptr};
};

} // namespace Slic3r::GUI::JusPrin
