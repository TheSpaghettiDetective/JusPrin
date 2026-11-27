    // --orca-import-cancel: Printer › Import printers from OrcaSlicer…, while
    // the project has a settings change that is not saved. The person presses
    // Import, the app asks about the unsaved change before it loads other
    // presets, and the person presses Cancel. The window is back on its list
    // with the buttons it opened with, to press again.
    void verify_orca_import_cancel()
    {
        using namespace std::chrono_literals;
        // An OrcaSlicer folder with three printers ticked that JusPrin does
        // not have yet.
        const fs::path orca = fs::path(data_dir()) / "orca-with-printers";
        fs::create_directories(orca);
        {
            boost::nowide::ofstream conf((orca / "OrcaSlicer.conf").string());
            conf << R"({"models": [{"vendor": "BBL", "model": "Bambu Lab A1 mini", "nozzle_diameter": "0.4"},)"
                 << R"({"vendor": "Creality", "model": "Creality K1C", "nozzle_diameter": "0.4"},)"
                 << R"({"vendor": "BBL", "model": "Bambu Lab P1S", "nozzle_diameter": "0.4"}]})"
                 << "\n# MD5 checksum 0123\n";
        }
        wxSetEnv("JUSPRIN_ORCA_DATADIR", wxString::FromUTF8(orca.string()));
        DynamicPrintConfig changed;
        changed.set_deserialize_strict("sparse_infill_density", "5%");
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(changed);

        struct Run
        {
            std::chrono::steady_clock::time_point at{};
            bool                                  pressed{false};
            bool                                  asked{false};
            int                                   ticks_since{0};
            std::string                           opened;
            std::string                           after;
        };
        auto run     = std::make_shared<Run>();
        m_modal_gate = [run](wxDialog& dialog) {
            auto* window = dynamic_cast<Home::OrcaImportWindow*>(&dialog);
            if (window == nullptr) {
                // The unsaved-changes question, answered Cancel.
                run->asked       = true;
                run->ticks_since = 0;
                return true;
            }
            wxWebView* page = nullptr;
            for (wxWindow* child : window->GetChildren())
                if (auto* view = dynamic_cast<AgentWebView*>(child))
                    page = view->webview();
            const auto now = std::chrono::steady_clock::now();
            if (page == nullptr || now < run->at + 1s)
                return false;
            run->at = now;
            // The window's buttons, each with whether it is greyed out, as
            // the page's title held them a second ago.
            const std::string title   = ui_text(page->GetCurrentTitle());
            const std::string buttons = title.rfind("buttons:", 0) == 0 ? title.substr(8) : std::string();
            WebView::RunScript(page, "document.title = 'buttons:' + Array.prototype.map.call(document.querySelectorAll('button'),"
                                     " function (b) { return b.innerText.trim() + (b.disabled ? ' (greyed out)' : ''); }).join(' | ')");
            if (!run->pressed) {
                if (buttons.find("Import ") == std::string::npos)
                    return false;
                run->opened  = buttons;
                run->pressed = true;
                WebView::RunScript(page, "document.querySelector('button.primary').click()");
                return false;
            }
            // Three seconds after the question was answered, or half a
            // minute after the press if it was never asked.
            if (++run->ticks_since < (run->asked ? 3 : 30))
                return false;
            run->after = buttons;
            return true;
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
        installed_shell()->open_orca_import();
        stop_answering_modals();
        std::cout << "HARNESS ORCA IMPORT opened with: " << run->opened << "\nHARNESS ORCA IMPORT after Cancel: " << run->after << std::endl;
        check(run->opened.find("(greyed out)") == std::string::npos, "orca_import_opens_with_its_buttons_to_press");
        check(run->asked, "orca_import_asks_about_the_unsaved_change");
        check(run->after == run->opened, "orca_import_cancelled_question_returns_to_the_list");

        // A shipped profile that cannot be installed is a broken install,
        // which is ours: the import stops with an error, as printer setup's
        // does, and the import's own handler reports it. The data folder's
        // profile folder refusing writes makes the install fail.
        {
            Home::OrcaImporter                importer(*wxGetApp().mainframe);
            const Home::OrcaProfileSource     source = importer.detect(Home::kOrcaSlicer, std::filesystem::u8path(orca.string()));
            std::vector<Home::OrcaImportItem> ticked;
            for (const Home::OrcaImportItem& item : source.ticked)
                if (item.outcome != Home::OrcaImportItem::Outcome::AlreadyThere)
                    ticked.push_back(item);
            const std::filesystem::path system = std::filesystem::u8path(data_dir()) / "system";
            std::filesystem::permissions(system, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
            std::string thrown;
            bool        returned = false;
            try {
                importer.bring(source, ticked, {}, Home::OrcaImporter::Selection::Kept);
                returned = true;
            } catch (const std::exception& error) {
                thrown = error.what();
            }
            std::filesystem::permissions(system, std::filesystem::perms::owner_all);
            std::cout << "HARNESS ORCA IMPORT broken install: " << ticked.size() << " to install, "
                      << (returned ? std::string("returned") : "threw '" + thrown + "'") << std::endl;
            check(!ticked.empty() && !returned && !thrown.empty(), "orca_import_with_a_broken_install_stops");
        }
        finish();
    }
