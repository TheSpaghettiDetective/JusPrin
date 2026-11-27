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
        PrinterSetup::PrinterPanel* panel = open_printer_chat(PrinterSetup::ConversationMode::Add, {}, "printer_chat_sheet");
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

// --delete-account-waits: Delete account in Preferences is refused while a
// reply runs in a printer chat, as it is while one runs in the project chat:
// the reply would be left holding the deleted account's sign-in. The refusal
// is reported as ours. A stand-in
// server on a loopback port counts the deletions it is asked for; the account
// and its token exist nowhere else. Once the reply ends, Delete account goes
// through, so the wait is the reply's and not the setup's.

    class StandInAccountServer
    {
    public:
        StandInAccountServer() : m_acceptor(m_io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0))
        {
            m_thread = std::thread([this] { serve(); });
        }
        ~StandInAccountServer()
        {
            m_stopping = true;
            boost::system::error_code    ignored;
            boost::asio::ip::tcp::socket wake(m_io);
            wake.connect(m_acceptor.local_endpoint(), ignored);
            m_thread.join();
        }
        std::string address() const { return "http://127.0.0.1:" + std::to_string(m_acceptor.local_endpoint().port()); }
        int         deletions() const { return m_deletions; }

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
                const std::string      line(boost::asio::buffers_begin(request.data()),
                                            boost::asio::buffers_begin(request.data()) + head);
                const bool             deletion = line.rfind("DELETE /ent/jusprin/v2/account ", 0) == 0;
                if (deletion)
                    ++m_deletions;
                // Everything else the app asks this account for is not here.
                const std::string reply = std::string("HTTP/1.1 ") + (deletion ? "204 No Content" : "404 Not Found") +
                                          "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                boost::asio::write(socket, boost::asio::buffer(reply), error);
                socket.close(error);
            }
        }

        boost::asio::io_context        m_io;
        boost::asio::ip::tcp::acceptor m_acceptor;
        std::atomic<bool>              m_stopping{false};
        std::atomic<int>               m_deletions{0};
        std::thread                    m_thread;
    };

    // A model whose reply runs until `finish` is set.
    class EndlessReplyAgent final : public Agent::IAgentService
    {
    public:
        explicit EndlessReplyAgent(std::shared_ptr<bool> finish) : m_finish(std::move(finish)) {}
        bool ready() const override { return true; }
        bool busy() const override { return m_active; }
        bool start(const Agent::AgentRequest&) override { return m_active = true; }
        bool continue_after_tool(const Agent::AgentToolResult&) override { return true; }
        void cancel() override { m_active = false; }
        std::optional<Agent::AgentEvent> poll() override
        {
            if (!m_active || !*m_finish)
                return std::nullopt;
            m_active = false;
            return Agent::AgentEvent::completed();
        }

    private:
        std::shared_ptr<bool> m_finish;
        bool                  m_active{false};
    };

    // Presses Delete account in Preferences, as the page's confirmation does,
    // and closes the window once the server has had time to be asked.
    void delete_account_from_preferences(const std::string& name)
    {
        using namespace std::chrono_literals;
        auto sent = std::make_shared<std::optional<std::chrono::steady_clock::time_point>>();
        m_modal_gate = [sent, name](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            if (!window->page_connected())
                return false;
            if (!*sent) {
                // Through the web view's own handler, which reports what fails.
                wxWebView*     view = window->web_view().webview();
                wxWebViewEvent event(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, view->GetId(), wxString(), wxString());
                event.SetEventObject(view);
                event.SetString(wxString::FromUTF8(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                                                  {"version", Agent::Protocol::kVersion},
                                                                  {"id", name},
                                                                  {"type", Agent::Protocol::kDeleteAccount},
                                                                  {"payload", nlohmann::json::object()}}
                                                       .dump()));
                *sent = std::chrono::steady_clock::now();
                view->GetEventHandler()->ProcessEvent(event);
            }
            return std::chrono::steady_clock::now() > **sent + 3s;
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->request_preferences("account");
        const bool closed = wait_for([sent] {
            if (!*sent)
                return false;
            for (wxWindow* window : wxTopLevelWindows)
                if (dynamic_cast<Preferences::PreferencesWindow*>(window) != nullptr)
                    return false;
            return true;
        }, 60s);
        stop_answering_modals();
        check(closed, name + "_preferences_closed");
    }

    void verify_delete_account_waits()
    {
        using namespace std::chrono_literals;
        auto server = std::make_shared<StandInAccountServer>();
        wxSetEnv("JUSPRIN_SERVER_URL", wxString::FromUTF8(server->address()));

        PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
        panel->open(PrinterSetup::ConversationMode::Add);
        const bool loaded = wait_for([panel] {
            return panel->host() != nullptr && panel->host()->handshake_complete() && panel->instructions_ready();
        }, 60s);
        check(loaded, "delete_account_waits_printer_chat_loaded");
        if (!loaded)
            return finish();
        Agent::AgentHost* host   = panel->host();
        auto              finish_reply = std::make_shared<bool>(false);
        host->set_agent(std::make_unique<EndlessReplyAgent>(finish_reply), Agent::AgentAvailability::Ready);
        host->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                             {"version", Agent::Protocol::kVersion},
                                             {"id", "delete-account-waits"},
                                             {"type", "user_message"},
                                             {"payload", {{"clientMessageId", "delete-account-waits-c"}, {"text", "Add my printer"}}}}
                                  .dump());
        check(wait_for([host] { return host->busy(); }, 20s), "delete_account_waits_printer_reply_running");
        // Signed in only now: a chat signed in before its turn would build its
        // model from the account instead of the stand-in above.
        AppConfig* config = wxGetApp().app_config;
        config->set("jusprin_agent", "jusprin_api_key", std::string("delete-account-waits-token"));
        config->set("jusprin_agent", "jusprin_account", std::string("delete-account-waits@example.com"));
        config->set("jusprin_agent", "provider", std::string("jusprin"));
        config->set("jusprin_agent", "enabled", std::string("true"));
        config->set("jusprin_agent", "cloud_consent", std::string("true"));

        delete_account_from_preferences("delete_account_during_printer_reply");
        std::cout << "HARNESS DELETE ACCOUNT during printer reply: deletions=" << server->deletions() << std::endl;
        check(server->deletions() == 0, "delete_account_waits_for_the_printer_chat_reply");
        check(unexpected_errors_logged("handling the app page's delete_account message") == 1,
              "delete_account_during_a_reply_is_reported");

        *finish_reply = true;
        check(wait_for([host] { return !host->busy(); }, 20s), "delete_account_waits_printer_reply_ended");
        delete_account_from_preferences("delete_account_after_printer_reply");
        std::cout << "HARNESS DELETE ACCOUNT after printer reply: deletions=" << server->deletions() << std::endl;
        check(server->deletions() == 1, "delete_account_goes_through_once_the_reply_ends");
        finish();
    }

// --account-page-failures: whose failure it is when Credits and plans, or
// Delete account, gets no good answer from the JusPrin server. No answer at
// all (a port nothing listens on) is the person's network: the sheet says to
// check the internet connection. 401 is a sign-in that has ended: the sheet
// says to sign in again. Any other error answer is ours: it is reported, and
// the sheet closes. A stand-in server on a loopback port gives the 401 and
// the 500; with JUSPRIN_SERVER_URL set, the 401 comes from that server, asked
// with a token it never gave.

    class StandInStatusServer
    {
    public:
        explicit StandInStatusServer(int status)
            : m_status(status), m_acceptor(m_io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0))
        {
            m_thread = std::thread([this] { serve(); });
        }
        ~StandInStatusServer()
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
                boost::asio::read_until(socket, request, "\r\n\r\n", error);
                const std::string body  = R"({"error":"stand-in"})";
                const std::string reply = "HTTP/1.1 " + std::to_string(m_status) + " Stand-in\r\nContent-Type: application/json\r\nContent-Length: " +
                                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                boost::asio::write(socket, boost::asio::buffer(reply), error);
                socket.close(error);
            }
        }

        int                            m_status;
        boost::asio::io_context        m_io;
        boost::asio::ip::tcp::acceptor m_acceptor;
        std::atomic<bool>              m_stopping{false};
        std::thread                    m_thread;
    };

    // An address on a loopback port nothing listens on.
    static std::string unanswered_address()
    {
        boost::asio::io_context        io;
        boost::asio::ip::tcp::acceptor acceptor(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
        const unsigned short           port = acceptor.local_endpoint().port();
        acceptor.close();
        return "http://127.0.0.1:" + std::to_string(port);
    }

    // Opens Credits and plans against `server` and says what the sheet showed:
    // the sentence under "Couldn't load", or "closed" when it closed by itself.
    std::string open_sheet_against(const std::string& server)
    {
        using namespace std::chrono_literals;
        wxSetEnv("JUSPRIN_SERVER_URL", wxString::FromUTF8(server));
        struct Seen
        {
            std::string detail;
            bool        closed{false};
        };
        auto seen = std::make_shared<Seen>();
        m_modal_gate = [seen](wxDialog& dialog) {
            auto* sheet = dynamic_cast<ServerSheet*>(&dialog);
            if (sheet == nullptr)
                return true;
            if (sheet->state() != ServerSheet::State::Failed)
                return false;
            seen->detail = sheet->status_detail().ToUTF8().data();
            return true;
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        wxGetApp().mainframe->CallAfter([seen] {
            ServerSheet::open(wxGetApp().mainframe, "prices", "harness", [seen] { seen->closed = true; });
        });
        wait_for([seen] { return seen->closed; }, 60s);
        stop_answering_modals();
        return seen->closed && seen->detail.empty() ? "closed" : seen->detail;
    }

    void verify_account_page_failures()
    {
        const char* live = std::getenv("JUSPRIN_SERVER_URL");
        const std::string live_server = live != nullptr ? live : "";
        AppConfig* config = wxGetApp().app_config;
        config->set("jusprin_agent", "jusprin_api_key", std::string("account-page-failures-token"));
        config->set("jusprin_agent", "jusprin_account", std::string("account-page-failures@example.test"));
        config->set("jusprin_agent", "provider", std::string("jusprin"));
        config->set("jusprin_agent", "enabled", std::string("true"));
        config->set("jusprin_agent", "cloud_consent", std::string("true"));
        const std::string internet  = wxGetTranslation("Check your internet connection, then try again.").ToUTF8().data();
        const std::string signed_in = Agent::sign_in_ended();

        auto refusing = std::make_shared<StandInStatusServer>(401);
        auto failing  = std::make_shared<StandInStatusServer>(500);
        const std::string ended_at = live_server.empty() ? refusing->address() : live_server;
        const std::string none     = unanswered_address();

        const int reported = unexpected_errors_logged();
        const std::string no_answer = open_sheet_against(none);
        std::cout << "HARNESS ACCOUNT PAGE no answer: " << no_answer << std::endl;
        check(no_answer == internet, "sheet_without_an_answer_says_to_check_the_internet");
        check(unexpected_errors_logged() == reported, "sheet_without_an_answer_reports_nothing");

        const std::string ended = open_sheet_against(ended_at);
        std::cout << "HARNESS ACCOUNT PAGE 401 from " << (live_server.empty() ? "stand-in" : "JUSPRIN_SERVER_URL") << ": " << ended
                  << std::endl;
        check(ended == signed_in, "sheet_with_an_ended_sign_in_says_to_sign_in_again");
        check(unexpected_errors_logged() == reported, "sheet_with_an_ended_sign_in_reports_nothing");

        const std::string ours = open_sheet_against(failing->address());
        std::cout << "HARNESS ACCOUNT PAGE 500: " << ours << std::endl;
        check(ours == "closed", "sheet_with_a_server_error_closes");
        check(unexpected_errors_logged("opening an account page") == 1, "sheet_with_a_server_error_is_reported");

        // Delete account: only ours is reported, and nothing is signed out.
        // Preferences asks the same server for the account as it opens, so
        // only the reports made while deleting are counted.
        wxSetEnv("JUSPRIN_SERVER_URL", wxString::FromUTF8(none));
        delete_account_from_preferences("delete_account_without_an_answer");
        wxSetEnv("JUSPRIN_SERVER_URL", wxString::FromUTF8(ended_at));
        delete_account_from_preferences("delete_account_with_an_ended_sign_in");
        check(unexpected_errors_logged("deleting the account") == 0, "delete_account_person_failures_report_nothing");
        wxSetEnv("JUSPRIN_SERVER_URL", wxString::FromUTF8(failing->address()));
        delete_account_from_preferences("delete_account_with_a_server_error");
        std::cout << "HARNESS ACCOUNT PAGE delete reports: " << unexpected_errors_logged("deleting the account") << " of "
                  << unexpected_errors_logged() << std::endl;
        check(unexpected_errors_logged("deleting the account") == 1, "delete_account_server_error_is_reported");
        check(!config->get("jusprin_agent", "jusprin_api_key").empty(), "delete_account_failures_keep_the_sign_in");

        // The AI models list: our server refusing the request, other than an
        // ended sign-in or the update answer, is ours. The list is marked
        // failed, and the report says what the server answered.
        {
            using namespace std::chrono_literals;
            auto        not_found = std::make_shared<StandInStatusServer>(404);
            auto        result    = std::make_shared<std::optional<Agent::ModelListResult>>();
            auto        mutex     = std::make_shared<std::mutex>();
            SeenDialogs seen;
            const int   before = unexpected_errors_logged("reading the AI models list");
            Agent::fetch_model_list(*Agent::profile_for("jusprin"), "account-page-failures-token",
                                    not_found->address() + "/ent/jusprin/v2/responses", std::make_shared<std::atomic<bool>>(false),
                                    [result, mutex](Agent::ModelListResult listed) {
                                        std::lock_guard<std::mutex> lock(*mutex);
                                        *result = std::move(listed);
                                    });
            wait_for([&] { return unexpected_errors_logged("reading the AI models list") > before && !seen.shown.empty(); }, 20s);
            std::lock_guard<std::mutex> lock(*mutex);
            std::cout << "HARNESS ACCOUNT PAGE model list 404: " << (*result ? (*result)->error : std::string("no answer")) << ", reported "
                      << unexpected_errors_logged("reading the AI models list") - before << ", dialogs " << seen.shown.size() << std::endl;
            check(*result && !(*result)->ok, "model_list_refused_by_our_server_is_marked_failed");
            check(unexpected_errors_logged("reading the AI models list") == before + 1 && seen.shown.size() == 1,
                  "model_list_refused_by_our_server_is_reported");
        }
        finish();
    }

// --deactivated-sign-in: signing in with the password of an account an admin
// deactivated, in Preferences › Account against the server JUSPRIN_SERVER_URL
// names, as JUSPRIN_DEACTIVATED_EMAIL with JUSPRIN_DEACTIVATED_PASSWORD (a
// local test account; signing in by password sends no email). The page says
// the account is deactivated and where to write, and nothing is reported.

    void verify_deactivated_sign_in()
    {
        using namespace std::chrono_literals;
        const char* email    = std::getenv("JUSPRIN_DEACTIVATED_EMAIL");
        const char* password = std::getenv("JUSPRIN_DEACTIVATED_PASSWORD");
        if (std::getenv("JUSPRIN_SERVER_URL") == nullptr || email == nullptr || password == nullptr) {
            check(false, "deactivated_sign_in_has_a_server_and_an_account");
            return finish();
        }
        const std::string sentence = "This account has been deactivated. If you think this is a mistake, email jusprin-support@obico.io.";
        struct Run
        {
            int                                   step{0};
            std::chrono::steady_clock::time_point at;
            bool                                  shown{false};
        };
        auto run = std::make_shared<Run>();
        const int reported = unexpected_errors_logged();
        m_modal_gate = [run, sentence, email = std::string(email), password = std::string(password)](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            if (!window->page_connected())
                return false;
            const auto send = [window](const char* type, const nlohmann::json& payload) {
                window->web_view().host().on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                                                         {"version", Agent::Protocol::kVersion},
                                                                         {"id", std::string("deactivated-") + type},
                                                                         {"type", type},
                                                                         {"payload", payload}}
                                                              .dump());
            };
            const auto now = std::chrono::steady_clock::now();
            if (run->step == 0) {
                send(Agent::Protocol::kAccountEmail, {{"email", email}});
                run->step = 1;
                run->at   = now;
                return false;
            }
            if (run->step == 1) {
                if (now < run->at + 500ms)
                    return false;
                send(Agent::Protocol::kAccountPassword, {{"password", password}});
                run->step = 2;
                run->at   = now;
                return false;
            }
            wxString text;
            window->web_view().webview()->RunScript("document.body.textContent", &text);
            run->shown = text.ToUTF8().data() != nullptr && std::string(text.ToUTF8().data()).find(sentence) != std::string::npos;
            return run->shown || now > run->at + 20s;
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->request_preferences("account");
        wait_for([run] { return run->step == 2 && (run->shown || std::chrono::steady_clock::now() > run->at + 25s); }, 90s);
        wait_for([] {
            for (wxWindow* window : wxTopLevelWindows)
                if (dynamic_cast<Preferences::PreferencesWindow*>(window) != nullptr)
                    return false;
            return true;
        }, 30s);
        stop_answering_modals();
        std::cout << "HARNESS DEACTIVATED SIGN IN shown=" << run->shown << std::endl;
        check(run->shown, "deactivated_sign_in_says_so_and_where_to_write");
        check(unexpected_errors_logged() == reported, "deactivated_sign_in_reports_nothing");
        check(wxGetApp().app_config->get("jusprin_agent", "jusprin_api_key").empty(), "deactivated_sign_in_stores_nothing");
        finish();
    }
