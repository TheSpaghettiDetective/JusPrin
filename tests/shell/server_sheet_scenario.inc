// --server-sheet: the server's pages in the app's sheet, and Delete account,
// against a real server: the one JUSPRIN_SERVER_URL names, signed in with the
// token in JUSPRIN_TEST_ACCOUNT_TOKEN (never printed). It deletes that
// account, so the token must be a throwaway account's on a test server.
//
// On Preferences › Account, Get more credits opens the sheet signed in through
// a page link and loads the server's page; the page going to the done address
// closes the sheet. Billing and usage does the same with the billing page.
// Delete account, confirmed, deletes the account on the server and signs out
// of it; signed out, the sheet opens Credits and plans as a guest.

    void verify_server_sheet()
    {
        using namespace std::chrono_literals;
        const char* server = std::getenv("JUSPRIN_SERVER_URL");
        const char* token  = std::getenv("JUSPRIN_TEST_ACCOUNT_TOKEN");
        if (server == nullptr || *server == '\0' || token == nullptr || *token == '\0') {
            std::cout << "HARNESS SERVER SHEET needs JUSPRIN_SERVER_URL and JUSPRIN_TEST_ACCOUNT_TOKEN" << std::endl;
            check(false, "server_sheet_has_a_server_and_an_account");
            finish();
            return;
        }
        AppConfig* config = wxGetApp().app_config;
        config->set("jusprin_agent", "jusprin_api_key", std::string(token));
        config->set("jusprin_agent", "jusprin_account", std::string("server-sheet@example.com"));
        config->set("jusprin_agent", "provider", std::string("jusprin"));
        config->set("jusprin_agent", "enabled", std::string("true"));
        config->set("jusprin_agent", "cloud_consent", std::string("true"));

        struct Seen
        {
            int         step{0};
            bool        opened{false};
            std::string probe;
            std::string sheet_url;
            std::string billing_url;
            std::string guest_url;
            bool        sheet_closed{false};
            bool        signed_out{false};
        };
        auto seen     = std::make_shared<Seen>();
        auto deadline = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + 90s);
        const std::string done_url = std::string(server) + "/ent/jusprin/v2/web/done";
        // What the Account page shows, as the page's title: whether Get more
        // credits and Delete account are there, and the confirmation's button.
        const char* probe =
            "(function () { var b = Array.from(document.querySelectorAll('button')).map(function (x) { return x.textContent; });"
            " document.title = 'account:' + (b.indexOf('Get more credits') >= 0 ? 'more' : '-') + ':'"
            " + (b.indexOf('Delete account…') >= 0 ? 'delete' : '-') + ':'"
            " + (document.querySelector('[role=dialog]') ? 'dialog' : '-'); })()";
        const auto click = [](wxWebView* view, const std::string& text) {
            WebView::RunScript(view, wxString::FromUTF8(
                "Array.from(document.querySelectorAll('button')).find(function (x) { return x.textContent === '" + text +
                "'; }).click()"));
        };
        // The sheet is modal over Preferences, and the harness asks about each
        // modal window in turn: the sheet is left open, and either one's turn
        // drives the steps, which run on the Preferences page.
        m_modal_gate = [this, seen, deadline, done_url, probe, click](wxDialog& dialog) {
            if (dynamic_cast<ServerSheet*>(&dialog) == nullptr && dynamic_cast<Preferences::PreferencesWindow*>(&dialog) == nullptr)
                return true;
            Preferences::PreferencesWindow* window = nullptr;
            for (wxWindow* top : wxTopLevelWindows)
                if (auto* found = dynamic_cast<Preferences::PreferencesWindow*>(top))
                    window = found;
            if (window == nullptr)
                return false;
            const bool preferences_turn = &dialog == window;
            seen->opened = true;
            if (std::chrono::steady_clock::now() > *deadline)
                return preferences_turn;
            if (!window->page_connected())
                return false;
            wxWebView* page = window->web_view().webview();
            const std::string title = page->GetCurrentTitle().ToUTF8().data();
            if (title != seen->probe)
                std::cout << "HARNESS SERVER SHEET step " << seen->step << " " << title << std::endl;
            seen->probe = title;
            ServerSheet* sheet = ServerSheet::current();
            switch (seen->step) {
            case 0:
                // The account's credits have arrived when Get more credits shows.
                if (title.rfind("account:more", 0) == 0) {
                    click(page, "Get more credits");
                    seen->step = 1;
                }
                break;
            case 1:
                if (sheet != nullptr && sheet->state() == ServerSheet::State::Page) {
                    if (!m_state->capture_dir.empty())
                        capture_web_view(sheet->web_view(), "server-prices");
                    seen->sheet_url = sheet->web_view()->GetCurrentURL().ToUTF8().data();
                    // What the server's checkout ends in.
                    WebView::RunScript(sheet->web_view(), wxString::FromUTF8("location.href = '" + done_url + "'"));
                    seen->step = 2;
                }
                break;
            case 2:
                if (sheet == nullptr) {
                    seen->sheet_closed = true;
                    click(page, "Billing and usage");
                    seen->step = 3;
                }
                break;
            case 3:
                if (sheet != nullptr && sheet->state() == ServerSheet::State::Page) {
                    if (!m_state->capture_dir.empty())
                        capture_web_view(sheet->web_view(), "server-billing");
                    seen->billing_url = sheet->web_view()->GetCurrentURL().ToUTF8().data();
                    WebView::RunScript(sheet->web_view(), wxString::FromUTF8("location.href = '" + done_url + "'"));
                    seen->step = 4;
                }
                break;
            case 4:
                if (sheet == nullptr) {
                    click(page, "Delete account…");
                    seen->step = 5;
                }
                break;
            case 5:
                if (title.find(":dialog") != std::string::npos) {
                    if (!m_state->capture_dir.empty())
                        capture_web_view(page, "delete-account");
                    click(page, "Delete account");
                    seen->step = 6;
                }
                break;
            case 6:
                if (title.rfind("account:-:-:-", 0) == 0 && Agent::cloud_account_token(wxGetApp().app_config).empty()) {
                    seen->signed_out = true;
                    // The welcome's See prices, with no account to sign in as.
                    window->CallAfter([window] { ServerSheet::open(window, "prices", "welcome", {}); });
                    seen->step = 7;
                }
                break;
            case 7:
                if (sheet != nullptr && sheet->state() == ServerSheet::State::Page) {
                    if (!m_state->capture_dir.empty())
                        capture_web_view(sheet->web_view(), "server-prices-guest");
                    seen->guest_url = sheet->web_view()->GetCurrentURL().ToUTF8().data();
                    WebView::RunScript(sheet->web_view(), wxString::FromUTF8("location.href = '" + done_url + "'"));
                    seen->step = 8;
                }
                break;
            default:
                if (sheet == nullptr)
                    return preferences_turn;
            }
            WebView::RunScript(page, probe);
            return false;
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->request_preferences("account");
        auto gone = [seen] {
            if (!seen->opened)
                return false;
            for (wxWindow* window : wxTopLevelWindows)
                if (dynamic_cast<Preferences::PreferencesWindow*>(window) != nullptr)
                    return false;
            return true;
        };
        wait_until(gone, "server_sheet_preferences_closed", [self = shared_from_this(), seen] {
            self->stop_answering_modals();
            // The page's address without the one-time link or a query.
            const std::string page = seen->sheet_url.substr(0, seen->sheet_url.find('?'));
            const std::string billing = seen->billing_url.substr(0, seen->billing_url.find('?'));
            std::cout << "HARNESS SERVER SHEET page " << page << " billing " << billing << " guest " << seen->guest_url
                      << " closed=" << seen->sheet_closed << " signed_out=" << seen->signed_out << std::endl;
            self->check(seen->step >= 2 && page.size() > 0 && page.find("/ent/jusprin/v2/web/prices") != std::string::npos,
                        "server_sheet_opens_the_prices_page_signed_in");
            self->check(seen->sheet_url.find("from=account") != std::string::npos, "server_sheet_says_where_the_click_was");
            self->check(seen->sheet_closed, "server_sheet_closes_at_the_done_address");
            self->check(billing.find("/ent/jusprin/v2/web/billing") != std::string::npos &&
                            seen->billing_url.find("from=account") != std::string::npos,
                        "billing_and_usage_opens_the_billing_page_signed_in");
            self->check(seen->signed_out, "delete_account_deletes_and_signs_out");
            self->check(seen->guest_url.find("/ent/jusprin/v2/web/prices?guest=1&from=welcome") != std::string::npos,
                        "signed_out_the_sheet_opens_prices_as_a_guest");
            self->finish();
        });
    }

// --printer-chat-sheet [capture-dir]: what the printer chat's page asks of
// the window, as the project chat's does. Its Get more credits and Billing
// and usage open the server's pages in the app's sheet, and Get the new
// version… opens Preferences on Updates. Signed out, so the prices page opens
// as a guest; with JUSPRIN_SERVER_URL set the sheet loads that server's page.

    void verify_printer_chat_sheet()
    {
        using namespace std::chrono_literals;
        struct Seen
        {
            int                        sheets{0};
            std::vector<std::string>   urls;
            std::optional<std::string> section;
        };
        auto seen = std::make_shared<Seen>();
        m_modal_gate = [this, seen](wxDialog& dialog) {
            if (auto* sheet = dynamic_cast<ServerSheet*>(&dialog)) {
                if (sheet->state() == ServerSheet::State::Loading)
                    return false;
                ++seen->sheets;
                seen->urls.push_back(sheet->web_view()->GetCurrentURL().ToUTF8().data());
                if (!m_state->capture_dir.empty() && sheet->state() == ServerSheet::State::Page)
                    capture_web_view(sheet->web_view(), "printer-chat-sheet-" + std::to_string(seen->sheets));
                return true;
            }
            if (auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog)) {
                if (!window->page_connected())
                    return false;
                seen->section = window->request().section;
            }
            return true;
        };
        PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
        panel->open(PrinterSetup::ConversationMode::Add);
        const bool loaded = wait_for([panel] {
            return panel->host() != nullptr && panel->host()->handshake_complete() && panel->instructions_ready();
        }, 60s);
        check(loaded, "printer_chat_sheet_page_loaded");
        if (!loaded)
            return finish();
        // The envelope the page sends for each button (AgentUI App.tsx).
        const auto press = [panel](const nlohmann::json& action) {
            panel->host()->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                                          {"version", Agent::Protocol::kVersion},
                                                          {"id", "printer-chat-sheet"},
                                                          {"type", Agent::Protocol::kShellAction},
                                                          {"payload", action}}
                                               .dump());
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        press({{"action", "open_server_page"}, {"page", "prices"}, {"from", "composer"}});
        check(wait_for([seen] { return seen->sheets >= 1; }, 45s), "printer_chat_get_more_credits_opens_the_sheet");
        press({{"action", "open_server_page"}, {"page", "billing"}, {"from", "composer"}});
        check(wait_for([seen] { return seen->sheets >= 2; }, 45s), "printer_chat_billing_and_usage_opens_the_sheet");
        press({{"action", "check_for_updates"}});
        check(wait_for([seen] { return seen->section.has_value(); }, 45s), "printer_chat_get_new_version_opens_preferences");
        stop_answering_modals();
        std::cout << "HARNESS PRINTER CHAT SHEET sheets=" << seen->sheets;
        for (const std::string& url : seen->urls)
            std::cout << " | " << url.substr(0, url.find('?')) << (url.find("from=composer") != std::string::npos ? " from=composer" : "");
        std::cout << " | preferences " << seen->section.value_or("(none)") << std::endl;
        if (std::getenv("JUSPRIN_SERVER_URL") != nullptr && seen->urls.size() >= 2) {
            check(seen->urls[0].find("/ent/jusprin/v2/web/prices") != std::string::npos &&
                      seen->urls[0].find("from=composer") != std::string::npos,
                  "printer_chat_sheet_shows_the_prices_page");
            check(seen->urls[1].find("from=composer") != std::string::npos, "printer_chat_sheet_shows_the_billing_request");
        }
        check(seen->section == std::optional<std::string>("updates"), "printer_chat_get_new_version_opens_updates");
        finish();
    }

// --server-sheet-popup [capture-dir]: a pop-up the sheet's page opens, as
// the checkout opens PayPal's window. A stand-in server on a loopback port
// answers the page link with a checkout page whose Pay opens a blank pop-up
// and later sends it to an approval page, as PayPal's button does. The page's
// user agent names a browser (PayPal opens no window otherwise), the pop-up
// opens in the app's own window, its page has window.opener, its Approve
// tells the checkout and closes the pop-up, and the checkout sees both.

    // The checkout and approval pages, and the page link that leads to them.
    class StandInCheckout
    {
    public:
        StandInCheckout() : m_acceptor(m_io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0))
        {
            m_thread = std::thread([this] { serve(); });
        }
        ~StandInCheckout()
        {
            m_stopping = true;
            boost::system::error_code    ignored;
            boost::asio::ip::tcp::socket wake(m_io);
            wake.connect(m_acceptor.local_endpoint(), ignored);
            m_thread.join();
        }
        std::string address() const { return "http://127.0.0.1:" + std::to_string(m_acceptor.local_endpoint().port()); }

    private:
        void serve()
        {
            for (;;) {
                boost::system::error_code    error;
                boost::asio::ip::tcp::socket socket(m_io);
                m_acceptor.accept(socket, error);
                if (m_stopping || error)
                    return;
                boost::asio::streambuf request;
                const size_t           head = boost::asio::read_until(socket, request, "\r\n\r\n", error);
                const std::string      text(boost::asio::buffers_begin(request.data()), boost::asio::buffers_begin(request.data()) + head);
                const size_t           start = text.find(' ') + 1;
                const std::string      path  = text.substr(start, text.find(' ', start) - start);
                std::string            type  = "text/html";
                std::string            body;
                if (path == "/ent/jusprin/v2/web_link") {
                    type = "application/json";
                    body = R"({"url":")" + address() + R"(/checkout"})";
                } else if (path == "/checkout") {
                    body = R"page(<!doctype html><title>checkout</title><button id="pay" onclick="pay()">Pay</button><script>
document.title = /Safari\//.test(navigator.userAgent) ? 'names-browser' : 'names-no-browser';
// The pop-up is sent on after a moment, as PayPal's is once its script has
// loaded: wx's macOS web view loads a pop-up's first request again once the
// window exists, which replaces an address given at once.
function pay() {
  var popup = window.open('', 'payment', 'width=420,height=520');
  if (!popup) { document.title = 'blocked'; return; }
  popup.document.write('<p>Loading</p>');
  setTimeout(function () { popup.location = location.origin + '/approve'; }, 1000);
  document.title = 'waiting';
  var seen = setInterval(function () { if (popup.closed) { clearInterval(seen); document.title += ':closed'; } }, 100);
}
window.addEventListener('message', function (e) { if (e.data === 'approved') document.title = 'approved'; });
</script>)page";
                } else if (path == "/approve") {
                    body = R"page(<!doctype html><title>approve</title><button id="ok">Approve</button><script>
document.title = window.opener ? 'has-opener' : 'no-opener';
document.getElementById('ok').onclick = function () { window.opener.postMessage('approved', '*'); window.close(); };
</script>)page";
                }
                const std::string reply = std::string("HTTP/1.1 ") + (body.empty() ? "404 Not Found" : "200 OK") +
                                          "\r\nContent-Type: " + type + "\r\nCache-Control: no-store\r\nContent-Length: " +
                                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                boost::asio::write(socket, boost::asio::buffer(reply), error);
                socket.close(error);
            }
        }

        boost::asio::io_context        m_io;
        boost::asio::ip::tcp::acceptor m_acceptor;
        std::atomic<bool>              m_stopping{false};
        std::thread                    m_thread;
    };

    void verify_server_sheet_popup()
    {
        using namespace std::chrono_literals;
        auto checkout = std::make_shared<StandInCheckout>();
        wxSetEnv("JUSPRIN_SERVER_URL", wxString::FromUTF8(checkout->address()));
        struct Seen
        {
            int         step{0};
            bool        in_app_window{false};
            std::string popup_title;
            std::string checkout_title;
            bool        names_browser{false};
            // The harness closes the sheet itself at the deadline.
            bool        closed_by_page{false};
            bool        sheet_closed{false};
        };
        auto seen     = std::make_shared<Seen>();
        auto deadline = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + 60s);
        const auto web_view_in = [](wxWindow* window) -> wxWebView* {
            for (wxWindow* child : window->GetChildren())
                if (auto* view = dynamic_cast<wxWebView*>(child))
                    return view;
            return nullptr;
        };
        // The sheet and its pop-up are both modal, and the harness asks the
        // sheet first and stops at the first that is not done: the sheet's
        // turn drives every step, and the sheet is closed at the end.
        m_modal_gate = [this, seen, deadline, web_view_in, done = checkout->address() + "/ent/jusprin/v2/web/done"](wxDialog&) {
            ServerSheet* sheet = ServerSheet::current();
            if (std::chrono::steady_clock::now() > *deadline)
                return true;
            if (sheet == nullptr || sheet->state() != ServerSheet::State::Page)
                return false;
            wxDialog* popup = nullptr;
            for (wxWindow* top : wxTopLevelWindows)
                if (top->GetName() == "Server sheet pop-up" && top->GetParent() == sheet)
                    popup = dynamic_cast<wxDialog*>(top);
            const std::string title = sheet->web_view()->GetCurrentTitle().ToUTF8().data();
            if (title != seen->checkout_title)
                std::cout << "HARNESS SERVER SHEET POPUP step " << seen->step << " checkout " << title << std::endl;
            seen->checkout_title = title;
            switch (seen->step) {
            case 0:
                seen->names_browser = title == "names-browser";
                WebView::RunScript(sheet->web_view(), "document.getElementById('pay').click()");
                seen->step = 1;
                break;
            case 1:
                if (popup != nullptr && popup->IsModal()) {
                    wxWebView* view = web_view_in(popup);
                    seen->in_app_window = view != nullptr;
                    seen->popup_title = view == nullptr ? std::string() : std::string(view->GetCurrentTitle().ToUTF8().data());
                    if (seen->popup_title == "has-opener" || seen->popup_title == "no-opener") {
                        std::cout << "HARNESS SERVER SHEET POPUP popup " << seen->popup_title << std::endl;
                        if (!m_state->capture_dir.empty())
                            capture_web_view(view, "server-sheet-popup");
                        WebView::RunScript(view, "document.getElementById('ok').click()");
                        seen->step = 2;
                    }
                }
                break;
            case 2:
                if (popup == nullptr && title.rfind("approved", 0) == 0 && title.find(":closed") != std::string::npos) {
                    WebView::RunScript(sheet->web_view(), wxString::FromUTF8("location.href = '" + done + "'"));
                    seen->step = 3;
                }
                break;
            default:
                break;
            }
            return false;
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        wxGetApp().mainframe->CallAfter([seen, deadline] {
            ServerSheet::open(wxGetApp().mainframe, "prices", "harness", [seen, deadline] {
                seen->closed_by_page = seen->step >= 3 && std::chrono::steady_clock::now() <= *deadline;
                seen->sheet_closed   = true;
            });
        });
        wait_until([seen, deadline] { return seen->sheet_closed || std::chrono::steady_clock::now() > *deadline + 5s; },
                   "server_sheet_popup_sheet_closed", [self = shared_from_this(), seen, checkout] {
                       self->stop_answering_modals();
                       std::cout << "HARNESS SERVER SHEET POPUP popup=" << seen->popup_title << " checkout=" << seen->checkout_title
                                 << " step=" << seen->step << std::endl;
                       self->check(seen->names_browser, "server_sheet_popup_user_agent_names_a_browser");
                       self->check(seen->in_app_window, "server_sheet_popup_opens_in_the_apps_window");
                       self->check(seen->popup_title == "has-opener", "server_sheet_popup_keeps_window_opener");
                       self->check(seen->step >= 3, "server_sheet_popup_reports_back_and_closes");
                       self->check(seen->closed_by_page, "server_sheet_popup_sheet_closes_at_the_done_address");
                       self->finish();
                   });
    }
