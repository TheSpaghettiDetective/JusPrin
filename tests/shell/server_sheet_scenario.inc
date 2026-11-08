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
