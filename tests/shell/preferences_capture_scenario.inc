// --preferences-capture <output-directory>: the Preferences window as a
// person goes through it. It is opened with the app's own shortcut command,
// and each section is clicked in the list at its left, as a person clicks it.
// For each, the harness reads back the section's text (HARNESS SETUP
// PREFERENCES lines, with the choices of its lists), checks the words the app
// guide quotes, and writes preferences-<section>.png from the page itself.
// Then Help › JusPrin guide…'s window, which must list every guide page.

    struct PreferencesStop
    {
        const char*              id;
        int                      index; // in the list at the left, as Preferences.tsx orders it
        std::vector<std::string> words;
    };

    void verify_preferences_capture()
    {
        using namespace std::chrono_literals;
        static const std::vector<PreferencesStop> stops = {
            {"account", 0, {"Account", "JusPrin account", "Continue with Google", "Sign up free"}},
            {"agent", 1, {"BYOAI", "Your own API key", "Add key", "Connect an AI tool you already use", "Look again", "New chats start with"}},
            {"appearance", 2, {"Appearance", "Language", "Theme", "Light", "Dark", "Units", "Millimetres", "Inches"}},
            {"view3d", 3, {"3D view", "Controls", "Mouse", "Touchpad", "Zoom toward the pointer", "Reverse scroll-wheel zoom",
                           "Turn freely", "Mouse buttons", "Left-drag", "Middle-drag", "Right-drag", "Turn the view", "Pan"}},
            {"projects", 4, {"Projects & files", "Version history", "Show folder"}},
            {"updates", 5, {"Updates", "Check now", "What to check for", "New JusPrin versions"}},
        };
        auto at       = std::make_shared<std::size_t>(0);
        auto phase    = std::make_shared<int>(0);
        auto waited   = std::make_shared<int>(0);
        auto deadline = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + 120s);
        auto opened   = std::make_shared<bool>(false);
        auto nav      = std::make_shared<std::string>();
        m_modal_gate  = [this, at, phase, waited, deadline, opened, nav](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            *opened = true;
            if (std::chrono::steady_clock::now() > *deadline || *at == stops.size())
                return true;
            if (!window->page_connected())
                return false;
            wxWebView* view = window->web_view().webview();
            const PreferencesStop& stop = stops[*at];
            switch (*phase) {
            case 0:
                // The list at the left, then the section, as a person clicks it.
                WebView::RunScript(view, wxString::Format(
                    "(function () { var items = document.querySelectorAll('.prefs-nav-item');"
                    " document.title = 'prefs-nav:' + Array.prototype.map.call(items, function (b) { return b.innerText.split('\\n')[0]; }).join('|')"
                    "  + '|' + ((document.querySelector('.prefs-orca-link') || {}).innerText || '');"
                    " if (items[%d]) items[%d].click(); })()", stop.index, stop.index));
                *phase = 1;
                *waited = 0;
                return false;
            case 1:
                if (nav->empty() && view->GetCurrentTitle().StartsWith("prefs-nav:"))
                    *nav = ui_text(view->GetCurrentTitle().Mid(10));
                // Let the section render and settle: four ticks of the closer, 0.8 s.
                if (++*waited < 4)
                    return false;
                {
                    // A page title is cut at about a thousand characters, so the
                    // page itself says which words are missing, ahead of its text.
                    std::string words;
                    for (const std::string& word : stop.words)
                        words += (words.empty() ? "'" : ", '") + word + "'";
                    WebView::RunScript(view, wxString::FromUTF8(
                        "(function () { var main = document.querySelector('.prefs-content');"
                        " var choices = Array.prototype.map.call(main ? main.querySelectorAll('option, [role=radio], button') : [],"
                        "   function (e) { return e.innerText || e.textContent; }).join(' | ');"
                        " var text = (main ? main.innerText.replace(/\\n+/g, ' | ') : '') + ' || ' + choices;"
                        " var missing = [" + words + "].filter(function (w) { return text.indexOf(w) < 0; });"
                        " document.title = 'prefs:" + std::string(stop.id) + ":' + missing.join('|') + '::' + text; })()"));
                }
                *phase = 2;
                *waited = 0;
                return false;
            default: {
                const std::string title  = ui_text(view->GetCurrentTitle());
                const std::string prefix = "prefs:" + std::string(stop.id) + ":";
                if (title.rfind(prefix, 0) != 0) {
                    if (++*waited > 25)
                        *at = stops.size();
                    return false;
                }
                const std::string rest = title.substr(prefix.size());
                const std::size_t split = rest.find("::");
                std::cout << "HARNESS SETUP PREFERENCES " << stop.id << " " << rest.substr(split + 2) << std::endl;
                std::vector<std::string> missing;
                for (std::size_t from = 0; from < split;) {
                    const std::size_t bar = std::min(rest.find('|', from), split);
                    missing.push_back(rest.substr(from, bar - from));
                    from = bar + 1;
                }
                for (const std::string& word : missing)
                    std::cout << "HARNESS PREFERENCES MISSING " << stop.id << " '" << word << "'" << std::endl;
                check(missing.empty(), std::string("preferences_") + stop.id + "_shows_its_settings");
                capture_web_view(view, std::string("preferences-") + stop.id);
                WebView::RunScript(view, "document.title = ''");
                ++*at;
                *phase = 0;
                return false;
            }
            }
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        // The shortcut's own command: Cmd+, on the Mac, Ctrl+, elsewhere.
        installed_shell()->request_preferences();
        auto gone = [opened] {
            if (!*opened)
                return false;
            for (wxWindow* window : wxTopLevelWindows)
                if (dynamic_cast<Preferences::PreferencesWindow*>(window) != nullptr)
                    return false;
            return true;
        };
        wait_until(gone, "preferences_capture_closed", [self = shared_from_this(), at, nav] {
            self->stop_answering_modals();
            std::cout << "HARNESS SETUP PREFERENCES LIST " << *nav << std::endl;
            self->check(nav->find("Account|BYOAI|Appearance|3D view|Projects & files|Updates|More preferences") == 0,
                        "preferences_lists_its_sections");
            self->check(*at == stops.size(), "preferences_capture_went_through_every_section");
            self->verify_guide_window_lists_the_pages();
        });
    }

    // Help › JusPrin guide…: the window lists every page docs/guide/README.md
    // lists, in its order, by each page's title; writes guide-window.png.
    void verify_guide_window_lists_the_pages()
    {
        std::vector<std::string> titles;
        {
            const fs::path    guide = fs::path(JUSPRIN_SOURCE_DIR) / "docs" / "guide";
            std::ifstream     index((guide / "README.md").string());
            const std::regex  listed(R"(^- \[[^\]]+\]\(([a-z0-9-]+)\.md\)$)");
            std::smatch       match;
            for (std::string line; std::getline(index, line);)
                if (std::regex_match(line, match, listed)) {
                    std::ifstream page((guide / (match[1].str() + ".md")).string());
                    for (std::string text; std::getline(page, text);)
                        if (text.rfind("# ", 0) == 0) {
                            titles.push_back(text.substr(2));
                            break;
                        }
                }
        }
        installed_shell()->open_guide();
        wxWindow* window = nullptr;
        for (wxWindow* each : wxTopLevelWindows)
            if (each->GetName() == "JusPrin guide" && each->IsShown())
                window = each;
        wxWebView* view = window != nullptr ? find_web_view(window) : nullptr;
        check(view != nullptr, "guide_window_opens");
        if (view == nullptr) {
            finish();
            return;
        }
        std::string expected;
        for (const std::string& title : titles)
            expected += (expected.empty() ? "" : "|") + title;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        auto ticks = std::make_shared<unsigned>(0);
        wait_until(
            [view, expected, until] {
                return ui_text(view->GetCurrentTitle()) == "guide:" + expected || std::chrono::steady_clock::now() > until;
            },
            "guide_window_listed",
            [self = shared_from_this(), view, window, expected, count = titles.size()] {
                const std::string shown = ui_text(view->GetCurrentTitle());
                std::cout << "HARNESS SETUP GUIDE WINDOW " << shown << std::endl;
                self->check(count >= 2 && shown == "guide:" + expected, "guide_window_lists_every_page_in_order");
                self->capture_web_view(view, "guide-window");
                window->Close();
                self->finish();
            },
            [view, ticks] {
                if ((*ticks)++ % 10 == 0)
                    WebView::RunScript(view,
                        "document.title = 'guide:' + Array.prototype.map.call(document.querySelectorAll('.guide-page'),"
                        " function (b) { return b.innerText.trim(); }).join('|')");
            });
    }

    static wxWebView* find_web_view(wxWindow* root)
    {
        for (wxWindow* child : root->GetChildren()) {
            if (auto* view = dynamic_cast<wxWebView*>(child))
                return view;
            if (wxWebView* inner = find_web_view(child))
                return inner;
        }
        return nullptr;
    }

    // --setup-command-fails <output-directory>: Preferences › BYOAI, Connect…
    // on Claude Code, whose own command fails. A stand-in `claude` comes first
    // on the app's PATH, so the person's real Claude Code is never run; it
    // prints an error and exits 1. The harness clicks through the screens as
    // a person does, reads back the error box above the buttons, and writes
    // setup-command-fails.png.
    void verify_setup_command_fails()
    {
        using namespace std::chrono_literals;
        const fs::path bin = fs::path(data_dir()) / "stand-in-bin";
        fs::create_directories(bin);
        {
            boost::nowide::ofstream script((bin / "claude").string());
            script << "#!/bin/sh\necho 'error: the stand-in claude always fails' >&2\nexit 1\n";
        }
        fs::permissions(bin / "claude", fs::perms::owner_all);
        wxString path;
        wxGetEnv("PATH", &path);
        wxSetEnv("PATH", wxString::FromUTF8(bin.string()) + ":" + path);

        struct Run
        {
            int                                   step{0};
            std::chrono::steady_clock::time_point at{};
            std::string                           error;
            bool                                  done{false};
        };
        auto run     = std::make_shared<Run>();
        m_modal_gate = [this, run](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            if (run->done)
                return true;
            if (!window->page_connected())
                return false;
            wxWebView* page = window->web_view().webview();
            const auto now  = std::chrono::steady_clock::now();
            if (run->at == std::chrono::steady_clock::time_point{})
                run->at = now;
            if (now < run->at + 1s)
                return false;
            run->at = now;
            switch (run->step) {
            case 0:
                // BYOAI, second in the list at the left.
                WebView::RunScript(page, "document.querySelectorAll('.prefs-nav-item')[1].click()");
                ++run->step;
                return false;
            case 1:
                WebView::RunScript(page, "(function () { if (document.title === 'tool-opened') return;"
                                         " var b = document.querySelector('[data-testid=agent-tool-claude] .agent-row-action');"
                                         " if (b) { b.click(); document.title = 'tool-opened'; } })()");
                if (ui_text(page->GetCurrentTitle()) == "tool-opened")
                    ++run->step;
                return false;
            case 2:
                WebView::RunScript(page, "(function () { if (document.title === 'connect-pressed') return;"
                                         " var b = document.querySelector('[data-testid=setup-local-prepare] .setup-actions .primary');"
                                         " if (b && !b.disabled) { b.click(); document.title = 'connect-pressed'; } })()");
                if (ui_text(page->GetCurrentTitle()) == "connect-pressed")
                    ++run->step;
                return false;
            default:
                // The box's own text and buttons, without its Technical
                // output; looked for each second, for up to ten seconds.
                WebView::RunScript(page, "(function () { var e = document.querySelector('[data-testid=setup-local-error]');"
                                         " if (!e) return;"
                                         " var copy = Array.prototype.some.call(e.querySelectorAll('button'), function (b) { return b.innerText === 'Copy'; });"
                                         " document.title = 'setup-error:' + e.innerText.replace(/\\n+/g, ' | ') + (copy ? ' [Copy]' : ''); })()");
                if (ui_text(page->GetCurrentTitle()).rfind("setup-error:", 0) == 0) {
                    run->error = ui_text(page->GetCurrentTitle()).substr(12);
                    capture_web_view(page, "setup-command-fails");
                    run->done = true;
                }
                if (++run->step > 13)
                    run->done = true;
                return false;
            }
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->request_preferences();
        wait_until([this] { return preferences_closed(); }, "setup_command_preferences_closed", [self = shared_from_this(), run] {
            self->stop_answering_modals();
            std::cout << "HARNESS SETUP COMMAND ERROR: " << run->error << std::endl;
            self->check(!run->error.empty(), "setup_command_failure_is_shown");
            self->check(run->error.find("The setup command failed.") != std::string::npos, "setup_command_failure_has_its_title");
            self->check(run->error.find("Copy the command and run it yourself.") != std::string::npos,
                        "setup_command_failure_says_what_to_do");
            self->check(run->error.find("[Copy]") != std::string::npos, "setup_command_failure_offers_copy");
            self->check(run->error.find("Client CLI") == std::string::npos && run->error.find("Could not start") == std::string::npos,
                        "setup_command_failure_shows_no_raw_text");
            self->finish();
        });
    }
