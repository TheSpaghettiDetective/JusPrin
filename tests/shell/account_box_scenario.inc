// --account-box: the box the JusPrin server fills on Preferences › Account,
// in the real web view, where wxWidgets gives the app's bridge (window.wx) to
// every frame. The box's HTML carries scripts that would close the window
// through that bridge and mark the page. The frame runs no scripts, so the box
// is measured and shown, the page stays unmarked, the window stays open and
// the bridge keeps working. Giving the frame allow-scripts fails this.

    // The box as the server would send it, plus what no box may do.
    struct AccountBoxSource final : Agent::IChatModelSource
    {
        int asked{0};
        std::vector<std::string> configured_providers() const override { return {}; }
        std::string default_provider() const override { return "openai"; }
        Agent::AgentServicePtr make_agent(const Agent::ChatModelChoice&) override { return nullptr; }
        void list_models(const std::string&, ModelsDone done) override { done(false, {}, false, {}); }
        void fetch_account_box(AccountBoxDone done) override
        {
            ++asked;
            done(std::string(
                "<!doctype html><html><body style=\"margin:0\"><div style=\"height:77px\">Double credits</div>"
                "<script>window.wx.postMessage(JSON.stringify({protocol: 'jusprin-agent-bridge', version: 1,"
                " id: 'box-1', type: 'shell_action', payload: {action: 'close'}}));</script>"
                "<script>parent.__boxScriptRan = true;</script></body></html>"));
        }
    };

    void verify_account_box()
    {
        using namespace std::chrono_literals;
        auto source   = std::make_shared<AccountBoxSource>();
        auto step     = std::make_shared<int>(0);
        auto deadline = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + 60s);
        auto shown    = std::make_shared<std::string>();
        auto opened   = std::make_shared<bool>(false);
        m_modal_gate  = [source, step, deadline, shown, opened](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            *opened = true;
            if (std::chrono::steady_clock::now() > *deadline)
                return true;
            if (!window->page_connected()) {
                if (*step > 0)
                    std::cout << "HARNESS ACCOUNT BOX PAGE DISCONNECTED at step " << *step << std::endl;
                return false;
            }
            AgentWebView& view = window->web_view();
            switch (*step) {
            case 0:
                view.host().set_chat_model_source(source);
                // Account asks again whenever the window comes back to the front.
                WebView::RunScript(view.webview(), "window.dispatchEvent(new Event('focus'))");
                *step = 1;
                return false;
            case 1: {
                // The title the last probe left, then the next probe.
                const std::string title = view.webview()->GetCurrentTitle().ToUTF8().data();
                WebView::RunScript(view.webview(),
                                   "(function () { var f = document.querySelector('iframe.prefs-box');"
                                   " document.title = 'box:' + (f ? (f.hasAttribute('data-measuring') ? 'measuring' : f.style.height) : 'none')"
                                   " + ':' + (window.__boxScriptRan ? 'ran' : 'quiet'); })()");
                if (title != *shown)
                    std::cout << "HARNESS ACCOUNT BOX PROBE " << title << " connected=" << window->page_connected() << std::endl;
                if (title.rfind("box:77px", 0) != 0) {
                    *shown = title;
                    return false;
                }
                *shown = title;
                // The bridge still carries the page's messages after the frame loaded.
                WebView::RunScript(view.webview(), "window.dispatchEvent(new Event('focus'))");
                *step = 2;
                return false;
            }
            default:
                return source->asked >= 2;
            }
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->request_preferences("account");
        // Gone however it closed: a box script that closes it must fail the
        // checks below, not leave this waiting for a window to answer.
        auto gone = [opened] {
            if (!*opened)
                return false;
            for (wxWindow* window : wxTopLevelWindows)
                if (dynamic_cast<Preferences::PreferencesWindow*>(window) != nullptr)
                    return false;
            return true;
        };
        wait_until(gone, "account_box_preferences_closed",
                   [self = shared_from_this(), source, step, shown] {
                       self->stop_answering_modals();
                       std::cout << "HARNESS ACCOUNT BOX " << *shown << " asked=" << source->asked << std::endl;
                       self->check(*step == 2 && shown->rfind("box:77px", 0) == 0, "account_box_measured_and_shown");
                       self->check(*shown == "box:77px:quiet", "account_box_runs_no_script");
                       self->check(self->m_modals_answered == self->m_modals_before + 1,
                                   "account_box_script_could_not_close_preferences");
                       self->check(source->asked >= 2, "account_box_leaves_the_bridge_working");
                       self->finish();
                   });
    }

    // Remove on a saved key, pressed while the settings file cannot be
    // written, then a removal of a key that is not there. Neither can the
    // person do anything about, so each is reported and the report's window
    // is the only one shown.
    void verify_remove_key_fails()
    {
        using namespace std::chrono_literals;
        AppConfig& config = *wxGetApp().app_config;
        store_placeholder_agent_key();
        config.save();
        struct Run
        {
            int  step{0};
            std::chrono::steady_clock::time_point at{};
            std::unique_ptr<UnavailableFolder> folder;
            std::shared_ptr<SeenDialogs> seen;
            int  reported_before{0};
            int  reported_after_remove{0};
            int  reported_after_missing{0};
            size_t seen_after_remove{0};
            bool on_disk_after_remove{false};
            bool page_lists_after_remove{false};
        };
        auto run = std::make_shared<Run>();
        const std::string conf = config.config_path();
        auto key_on_disk = [conf] {
            std::ifstream file(conf);
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return text.find("harness-placeholder-key") != std::string::npos;
        };
        m_modal_gate = [run, key_on_disk](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            if (!window->page_connected())
                return false;
            wxWebView* page = window->web_view().webview();
            auto ask = [page](const std::string& script) {
                wxString out;
                page->RunScript(wxString::FromUTF8(script), &out);
                return std::string(out.ToUTF8().data());
            };
            const auto now = std::chrono::steady_clock::now();
            switch (run->step) {
            case 0: {
                // The page lists the key first.
                if (ask("!!document.querySelector('button[aria-label^=\"Remove \"][aria-label$=\" key\"]')") != "true")
                    return false;
                run->seen            = std::make_shared<SeenDialogs>();
                run->reported_before = unexpected_errors_logged();
                run->folder          = std::make_unique<UnavailableFolder>(std::filesystem::path(data_dir()));
                ask("document.querySelector('button[aria-label^=\"Remove \"][aria-label$=\" key\"]').click()");
                run->at   = now;
                run->step = 1;
                return false;
            }
            case 1: {
                if (now < run->at + 1500ms)
                    return false;
                run->folder.reset();
                run->reported_after_remove   = unexpected_errors_logged();
                run->seen_after_remove       = run->seen->shown.size();
                run->on_disk_after_remove    = key_on_disk();
                run->page_lists_after_remove =
                    ask("!!document.querySelector('button[aria-label^=\"Remove \"][aria-label$=\" key\"]')") == "true";
                if (const char* dir = std::getenv("PRINTER_LIVE_CAPTURE_DIR"))
                    snapshot_web_view(page->GetNativeBackend(), (std::filesystem::path(dir) / "remove-key-fails-light.png").string().c_str());
                // A removal of a key that is not stored, which the page never sends.
                const std::string envelope = nlohmann::json{{"protocol", Agent::Protocol::kName}, {"version", Agent::Protocol::kVersion},
                                                            {"id", "remove-missing-key"}, {"type", Agent::Protocol::kSetupRemoveKey},
                                                            {"payload", {{"provider", "anthropic"}}}}.dump();
                ask("window.wx.postMessage(" + nlohmann::json(envelope).dump() + ")");
                run->at   = now;
                run->step = 2;
                return false;
            }
            default:
                if (now < run->at + 1500ms)
                    return false;
                run->reported_after_missing = unexpected_errors_logged();
                return true;
            }
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->request_preferences("agent");
        wait_until([this] { return preferences_closed(); }, "remove_key_fails_preferences_closed", [self = shared_from_this(), run] {
            self->stop_answering_modals();
            if (run->seen)
                run->seen->Unregister();
            const auto& shown = run->seen ? run->seen->shown : std::vector<std::string>{};
            auto report_window = [](const std::string& text) { return text.find("Something went wrong on our side") != std::string::npos; };
            std::cout << "HARNESS REMOVE KEY: on disk after the failed removal=" << run->on_disk_after_remove
                      << " page still lists it=" << run->page_lists_after_remove << " windows=" << shown.size() << std::endl;
            self->check(run->step == 2, "remove_key_fails_both_pressed");
            self->check(run->reported_after_remove == run->reported_before + 1, "remove_key_write_failure_is_reported");
            self->check(run->seen_after_remove == 1 && report_window(shown.front()), "remove_key_write_failure_shows_only_the_report_window");
            self->check(run->reported_after_missing == run->reported_after_remove + 1, "remove_missing_key_is_reported");
            self->check(shown.size() == run->seen_after_remove + 1 && report_window(shown.back()),
                        "remove_missing_key_shows_only_the_report_window");
            self->finish();
        });
    }

    // Requests from Preferences that fail where the person can do nothing:
    // a setting the app does not have and a sign-in link it does not know,
    // which no page sends; the account's model list answering while the
    // settings file cannot be written, so the formats it names are not kept;
    // then Sign out on a server that does not revoke the token. Each is
    // reported, and the report's window is the only one shown.
    void verify_preferences_requests_fail()
    {
        using namespace std::chrono_literals;
        // The stand-in answers no revoke, and no model list until asked.
        auto gateway = std::make_shared<StandInGateway>();
        gateway->set_models_listed(false);
        wxSetEnv("JUSPRIN_SERVER_URL", gateway->address());
        AppConfig& config = *wxGetApp().app_config;
        config.set("jusprin_agent", "jusprin_api_key", std::string("harness-email-token"));
        config.set("jusprin_agent", "jusprin_account", std::string("alex@example.com"));
        config.set("jusprin_agent", "provider", std::string("jusprin"));
        config.set("jusprin_agent", "enabled", std::string("true"));
        config.set("jusprin_agent", "cloud_consent", std::string("true"));
        struct Run
        {
            int  step{0};
            std::chrono::steady_clock::time_point at{};
            std::shared_ptr<SeenDialogs> seen;
            std::vector<int>    reported;
            std::vector<size_t> windows;
            std::shared_ptr<StandInGateway> gateway;
            // The settings folder's and file's own permissions, while
            // neither can be written.
            std::optional<std::pair<std::filesystem::perms, std::filesystem::perms>> writable;
        };
        auto run     = std::make_shared<Run>();
        run->gateway = gateway;
        auto post = [](wxWebView* page, const std::string& type, const nlohmann::json& payload) {
            const std::string envelope = nlohmann::json{{"protocol", Agent::Protocol::kName}, {"version", Agent::Protocol::kVersion},
                                                        {"id", "preferences-requests-" + type}, {"type", type},
                                                        {"payload", payload}}.dump();
            WebView::RunScript(page, "window.wx.postMessage(" + nlohmann::json(envelope).dump() + ")");
        };
        m_modal_gate = [run, post](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            if (!window->page_connected())
                return false;
            wxWebView* page = window->web_view().webview();
            const auto now  = std::chrono::steady_clock::now();
            if (run->step > 0 && now < run->at + 2s)
                return false;
            if (run->step == 0) {
                // The account's own reads settle first.
                if (run->at == std::chrono::steady_clock::time_point{}) {
                    run->at = now;
                    return false;
                }
                if (now < run->at + 3s)
                    return false;
                run->seen = std::make_shared<SeenDialogs>();
            }
            const std::filesystem::path settings = wxGetApp().app_config->config_path();
            if (run->writable) {
                std::filesystem::permissions(data_dir(), run->writable->first, std::filesystem::perm_options::replace);
                std::filesystem::permissions(settings, run->writable->second, std::filesystem::perm_options::replace);
                run->writable.reset();
            }
            run->reported.push_back(unexpected_errors_logged());
            run->windows.push_back(run->seen->shown.size());
            run->at = now;
            switch (run->step++) {
            case 0:
                post(page, Agent::Protocol::kPreferencesAction,
                     {{"action", "set"}, {"name", "no_such_setting"}, {"value", true}});
                return false;
            case 1:
                post(page, Agent::Protocol::kAccountOpenLink, {{"link", "no_such_link"}});
                return false;
            case 2:
                // The list that failed when the window opened is asked again.
                run->gateway->set_models_listed(true);
                // Read-only: the settings file's lock still opens, and
                // neither a new copy beside the file nor the file itself can
                // be written.
                run->writable = {std::filesystem::status(data_dir()).permissions(),
                                 std::filesystem::status(settings).permissions()};
                std::filesystem::permissions(settings, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace);
                std::filesystem::permissions(data_dir(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                             std::filesystem::perm_options::replace);
                post(page, Agent::Protocol::kRefreshAccount, nlohmann::json::object());
                return false;
            case 3:
                post(page, Agent::Protocol::kSetupRemoveKey, {{"provider", "jusprin"}});
                return false;
            default:
                return true;
            }
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->request_preferences("account");
        wait_until([this] { return preferences_closed(); }, "preferences_requests_closed", [self = shared_from_this(), run] {
            self->stop_answering_modals();
            if (run->seen)
                run->seen->Unregister();
            const std::vector<std::string> shown = run->seen ? run->seen->shown : std::vector<std::string>{};
            for (const std::string& window : shown)
                std::cout << "HARNESS PREFERENCES REQUESTS WINDOW: " << window << std::endl;
            for (const auto& entry : fs::directory_iterator(fs::path(data_dir()) / "log")) {
                std::ifstream file(entry.path().string());
                for (std::string line; std::getline(file, line);)
                    if (line.find("Unexpected error while") != std::string::npos ||
                        line.find("Failed to write the configuration") != std::string::npos)
                        std::cout << "HARNESS PREFERENCES REQUESTS LOG: " << line << std::endl;
            }
            self->check(run->reported.size() == 5, "preferences_requests_all_sent");
            if (run->reported.size() != 5)
                return self->finish();
            const char* names[] = {"unknown_setting", "unknown_link", "unsaved_model_formats", "unrevoked_sign_out"};
            for (int i = 0; i < 4; ++i) {
                const std::string name = names[i];
                self->check(run->reported[i + 1] == run->reported[i] + 1, name + "_is_reported");
                self->check(run->windows[i + 1] == run->windows[i] + 1 &&
                                shown[run->windows[i]].find("Something went wrong on our side") != std::string::npos,
                            name + "_shows_only_the_report_window");
            }
            self->finish();
        });
    }
