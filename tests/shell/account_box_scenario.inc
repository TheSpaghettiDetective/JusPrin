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
        void list_models(const std::string&, ModelsDone done) override { done(false, {}); }
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
        auto source   = std::make_shared<AccountBoxSource>();
        auto step     = std::make_shared<int>(0);
        auto deadline = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + 60s);
        auto shown    = std::make_shared<std::string>();
        auto opened   = std::make_shared<bool>(false);
        m_modal_gate  = [this, source, step, deadline, shown, opened](wxDialog& dialog) {
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
