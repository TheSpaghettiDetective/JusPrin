#pragma once

// One of the JusPrin server's pages in a sheet over the window that opened it
// (the canvas's "Plan and billing" boards): Credits and plans, or Billing and
// usage. Anything about dollars, payment or changing a plan is drawn by the
// server, so a price change never needs a release; the app draws only the
// sheet's title row and its loading and can't-load states.
//
// The page opens signed in as the app's account: the app asks the server for
// a link that signs in once (POST /ent/jusprin/v2/web_link) and loads it.
// Open in browser asks for a fresh link and opens it in the person's browser,
// for their saved cards or password manager. When the page goes to the done
// address (/ent/jusprin/v2/web/done) the sheet closes. However it closes,
// `closed` runs, so the opener reads the account again: a purchase may have
// finished without reaching the done address.
//
// The sheet is modal: Preferences is itself a modal window, and on macOS a
// window opened over a modal one without its own modal loop gets no clicks.
//
// A link the page opens in a new window goes to the person's browser. A
// pop-up that reports back to the page, such as PayPal's window in the
// checkout, opens in a window of the app's own over the sheet instead: it
// reaches the checkout through window.opener, which a page in the browser
// does not have, and the checkout would wait for it for ever. On macOS the
// page's user agent names Safari: PayPal opens no window for a page whose
// user agent names no browser.

#include "slic3r/GUI/JusPrin/Agent/WebViewRelease.hpp"

#include <wx/dialog.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <string>

class Button;
class wxPanel;
class wxStaticText;
class wxWebView;
class wxWebViewEvent;

namespace Slic3r::GUI::JusPrin {

class HeaderButton;
class ShellTheme;

class ServerSheet final : public wxDialog
{
public:
    // Shows `page` ("prices" or "billing") over `parent` and returns once the
    // sheet has closed, after `closed`. `from` says where in the app the click
    // was.
    static void open(wxWindow* parent, const std::string& page, const std::string& from, std::function<void()> closed);
    // The open sheet, or nullptr; for the shell harness.
    static ServerSheet* current();

    // What the sheet shows in place of the page: nothing, a loading line, or
    // that the page could not be loaded, with Try again.
    enum class State { Page, Loading, Failed };
    State state() const { return m_state; }
    wxWebView* web_view() const { return m_view; }

private:
    ServerSheet(wxWindow* parent, const ShellTheme& theme);
    ~ServerSheet() override;

    void     show_page(const std::string& page, const std::string& from);
    wxWindow* build_header();
    void     load();
    // Asks the server for a link to the page; `use` gets it, or an empty one.
    void     request_link(std::function<void(std::string url)> use);
    void     open_in_browser();
    void     set_state(State state);
    void     on_navigating(wxWebViewEvent& event);
    void     on_loaded(wxWebViewEvent& event);
    void     on_error(wxWebViewEvent& event);
    void     on_new_window(wxWebViewEvent& event);
    void     on_new_window_features(wxWebViewEvent& event);
    void     close();
    void     release();

    const ShellTheme&     m_theme;
    wxPanel*              m_body{nullptr};
    wxStaticText*         m_title{nullptr};
    HeaderButton*         m_browser{nullptr};
    HeaderButton*         m_close{nullptr};
    wxWebView*            m_view{nullptr};
    // The pop-up the page opened, while it is open.
    wxDialog*             m_popup{nullptr};
    wxPanel*              m_status{nullptr};
    wxStaticText*         m_status_title{nullptr};
    wxStaticText*         m_status_detail{nullptr};
    Button*               m_retry{nullptr};
    wxTimer               m_load_deadline{this};
    WebViewRelease        m_release{[this] { release(); }};
    State                 m_state{State::Loading};
    std::string           m_page;
    std::string           m_from;
    // A link request answered after the sheet moved on, or closed, is dropped.
    unsigned              m_request{0};
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    bool                  m_closing{false};
};

} // namespace Slic3r::GUI::JusPrin
