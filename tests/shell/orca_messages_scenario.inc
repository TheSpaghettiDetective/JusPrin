    // --orca-messages <directory>: what OrcaSlicer tells the person while the
    // JusPrin view is on screen (OrcaMessages.hpp). Included inside class
    // Scenario after the dialog routing scenario, whose tool runner and
    // helpers it uses.
    //
    // Each case makes OrcaSlicer speak through its real code and reads three
    // things: which of its windows opened (the dialog witness), what the view
    // took instead with the reason it carries, and where the view showed it
    // (an entry in the project chat, or nothing).
    // With --classic-messages the view takes nothing, as in the classic view,
    // and each case checks that OrcaSlicer's own window opens.

    std::size_t m_om_taken_mark{0};
    std::size_t m_om_opened_mark{0};
    std::size_t m_om_shown_mark{0};
    std::size_t m_om_work_mark{0};
    bool        m_om_went{false};
    std::string m_om_other_printer;
    std::string m_om_named;
    std::string m_om_version_at_switch;
    std::string m_om_closed_project;
    std::string m_om_switched_to;
    int         m_om_reported_before{0};
    std::size_t m_om_clean_switch_rows{0};
    std::vector<Workspace::WorkspaceEdit> m_om_edits;
    std::optional<Workspace::WorkspaceSubscription> m_om_edit_feed;

    static bool om_taking() { return OrcaMessages::taking(); }

    void om_mark()
    {
        m_om_taken_mark  = OrcaMessages::taken_messages().size();
        m_om_opened_mark = g_dialog_witness->shown().size();
        m_om_shown_mark  = OrcaMessages::shown_windows().size();
        m_om_work_mark   = persistence().document().work().size();
    }

    static std::string om_one_line(std::string text)
    {
        for (char& c : text)
            if (c == '\n' || c == '\r')
                c = ' ';
        return text.size() > 260 ? text.substr(0, 260) + "..." : text;
    }

    // Prints what happened since the mark. Returns the taken message with
    // `words` in it, if any, and whether a window with `words` opened.
    const OrcaMessages::TakenMessage* om_report(const std::string& name, const std::string& words, bool& opened)
    {
        std::cerr << "HARNESS MESSAGE CASE " << name << " taking=" << (om_taking() ? "on" : "off") << '\n';
        const OrcaMessages::TakenMessage* match = nullptr;
        const auto&                       taken = OrcaMessages::taken_messages();
        for (std::size_t index = m_om_taken_mark; index < taken.size(); ++index) {
            const OrcaMessages::TakenMessage& message = taken[index];
            std::cerr << "HARNESS MESSAGE TAKEN from=" << OrcaMessages::from_name(message.from)
                      << " cause=" << OrcaMessages::cause_name(message.cause.kind) << " printer='" << message.cause.printer << "' detail='"
                      << message.cause.detail << "' told_elsewhere=" << message.cause.told_elsewhere << " title='" << message.title
                      << "' text='" << om_one_line(message.text) << "'\n";
            if (match == nullptr && (message.text.find(words) != std::string::npos || message.title.find(words) != std::string::npos))
                match = &message;
        }
        opened = false;
        for (const std::string& dialog : dialogs_shown_since(m_om_opened_mark)) {
            std::cerr << "HARNESS MESSAGE OPENED [" << om_one_line(dialog) << "]\n";
            opened = opened || dialog.find(words) != std::string::npos;
        }
        const auto& shown = OrcaMessages::shown_windows();
        for (std::size_t index = m_om_shown_mark; index < shown.size(); ++index)
            std::cerr << "HARNESS MESSAGE LET THROUGH title='" << shown[index].title << "' class=" << shown[index].window_class
                      << " kind=" << OrcaMessages::window_kind_name(shown[index].kind) << " why='" << shown[index].why << "'\n";
        for (const Agent::WorkEntry& entry : om_new_entries())
            std::cerr << "HARNESS MESSAGE ENTRY kind=" << entry.kind << " title='" << entry.title << "' state=" << entry.state << " detail='"
                      << om_one_line(entry.detail) << "'\n";
        return match;
    }

    // The project chat's entries since the mark.
    std::vector<Agent::WorkEntry> om_new_entries()
    {
        const auto work = persistence().document().work();
        return work.size() > m_om_work_mark ? std::vector<Agent::WorkEntry>(work.begin() + m_om_work_mark, work.end()) :
                                              std::vector<Agent::WorkEntry>();
    }

    // Whether OrcaSlicer's notification manager holds a canvas confirmation
    // with `words` in it. No taken message should become one.
    bool om_on_canvas(const std::string& words)
    {
        bool found = false;
        for (const NativeNotice& notice : native_notices())
            if (notice.type == int(NotificationType::CustomNotification)) {
                std::cerr << "HARNESS MESSAGE NOTICE level=" << notice.level << " text='" << om_one_line(notice.text) << "'\n";
                found = found || notice.text.find(words) != std::string::npos;
            }
        return found;
    }

    // A message the view should take: in the classic view OrcaSlicer's window
    // opens; in the JusPrin view no window opens and the message is taken
    // with its cause.
    const OrcaMessages::TakenMessage* om_taken_case(const std::string& name, const std::string& words, const char* from, const char* cause,
                                                    const std::string& printer = {})
    {
        bool                              opened = false;
        const OrcaMessages::TakenMessage* match  = om_report(name, words, opened);
        if (!om_taking()) {
            check(opened, name + "_classic_view_orca_window_opens");
            return nullptr;
        }
        check(!opened, name + "_no_orca_window_opens");
        check(match != nullptr, name + "_is_taken");
        if (match == nullptr)
            return nullptr;
        check(std::string(OrcaMessages::from_name(match->from)) == from, name + "_taken_at_the_expected_point");
        check(std::string(OrcaMessages::cause_name(match->cause.kind)) == cause, name + "_keeps_its_cause");
        if (!printer.empty())
            check(match->cause.printer == printer, name + "_names_the_printer");
        return match;
    }

    // A taken printer message is an entry in the project chat, titled with
    // the printer's name, failed, carrying OrcaSlicer's words.
    void om_entry_case(const std::string& name, const std::string& words)
    {
        if (!om_taking())
            return;
        const auto entries = om_new_entries();
        check(entries.size() == 1, name + "_makes_one_chat_entry");
        if (entries.empty())
            return;
        const Agent::WorkEntry& entry = entries.back();
        check(entry.kind == OrcaMessages::kPrinterEntryKind && entry.title == kFakeFixturePrinter, name + "_entry_names_the_printer");
        check(entry.state == "failed" && entry.detail.find(words) != std::string::npos, name + "_entry_says_what_orca_said");
        check(!installed_shell()->is_agent_pane_collapsed(), name + "_entry_opens_the_chat");
    }

    // A taken message that names no printer: one entry in the chat, titled
    // as OrcaSlicer's window would have been, and nothing on the canvas.
    void om_message_entry_case(const std::string& name, const std::string& title, const std::string& words)
    {
        if (!om_taking())
            return;
        check(!om_on_canvas(words), name + "_is_not_on_the_canvas");
        const auto entries = om_new_entries();
        check(entries.size() == 1, name + "_makes_one_chat_entry");
        if (entries.empty())
            return;
        const Agent::WorkEntry& entry = entries.back();
        check(entry.kind == OrcaMessages::kMessageEntryKind && entry.title == title, name + "_entry_has_orcas_title");
        check(entry.state == "failed" && entry.detail.find(words) != std::string::npos, name + "_entry_says_what_orca_said");
        check(!installed_shell()->is_agent_pane_collapsed(), name + "_entry_opens_the_chat");
    }

    // A window that must open either way.
    void om_shown_case(const std::string& name, const std::string& words)
    {
        bool                              opened = false;
        const OrcaMessages::TakenMessage* match  = om_report(name, words, opened);
        check(opened, name + "_opens");
        check(match == nullptr, name + "_is_not_taken");
    }

    // No modal window is open. The witness closes one window per tick and the
    // oldest first, so a case starts only once the last case's are gone.
    bool om_quiet()
    {
        for (const wxWindow* window : wxTopLevelWindows)
            if (const auto* dialog = dynamic_cast<const wxDialog*>(window); dialog != nullptr && dialog->IsShown() && dialog->IsModal())
                return false;
        return orca_caught_up();
    }
    std::function<bool()> om_quiet_and(std::function<bool()> also = {})
    {
        return [self = shared_from_this(), also] { return (!also || also()) && self->om_quiet(); };
    }

    // Pictures the screen for the report: the canvas through the harness's
    // own capture, and, with --orca-messages-pause, a pause long enough to
    // picture the whole window from outside.
    void om_picture(const std::string& name)
    {
        capture_issues("messages-" + name);
        // The chat pane and Home are web views the canvas picture leaves out.
        if (wxWebView* chat = installed_shell()->agent_pane()->web_view().webview(); chat != nullptr && chat->IsShownOnScreen())
            capture_web_view(chat, "chat-messages-" + name);
        if (wxWebView* home = installed_shell()->home_view()->webview(); home != nullptr && home->IsShownOnScreen())
            capture_web_view(home, "home-messages-" + name);
        if (g_orca_messages_pause.count() == 0)
            return;
        std::cerr << "HARNESS MESSAGE PICTURE " << name << std::endl;
        wait_for([] { return false; }, g_orca_messages_pause);
    }

    // Writes an STL of boxes and imports it as the AI's tool does.
    void om_import_boxes(const std::string& name, bool second_shell)
    {
        const std::string path = (fs::path(data_dir()) / name).string();
        {
            std::ofstream out(path);
            out << "solid shells\n";
            write_box(out, {-10, -10, 0}, {10, 10, 10});
            if (second_shell)
                write_box(out, {-10, -10, 20}, {10, 10, 30});
            out << "endsolid shells\n";
        }
        nlohmann::json import = session_arguments();
        import["path"]        = path;
        run_dialog_tool("object_import_file", import);
    }

    static bool om_dirty(Preset::Type type)
    {
        PresetBundle& bundle = *wxGetApp().preset_bundle;
        switch (type) {
        case Preset::TYPE_PRINTER: return bundle.printers.current_is_dirty();
        case Preset::TYPE_FILAMENT: return bundle.filaments.current_is_dirty();
        default: return bundle.prints.current_is_dirty();
        }
    }
    static bool om_printer_dirty() { return om_dirty(Preset::TYPE_PRINTER); }

    // Leaves the selected profile of `type` with an unsaved edit, as a
    // person's or the AI's change does.
    void om_edit(Preset::Type type, const std::string& value)
    {
        PresetBundle& bundle = *wxGetApp().preset_bundle;
        if (type == Preset::TYPE_PRINTER) {
            const std::string printer = bundle.printers.get_edited_preset().name;
            patch_settings({{"scope", "printer"}, {"target", {{"preset", printer}}}, {"changes", {{"printable_height", value}}}});
        } else if (type == Preset::TYPE_FILAMENT) {
            const std::string filament = bundle.filaments.get_edited_preset().name;
            patch_settings({{"scope", "filament"}, {"target", {{"preset", filament}}}, {"changes", {{"filament_notes", value}}}});
        } else {
            patch_settings({{"scope", "process"}, {"changes", {{"sparse_infill_density", value + "%"}}}});
        }
        std::cerr << "HARNESS MESSAGE NOTE edited type=" << int(type) << " dirty=" << om_dirty(type) << '\n';
    }

    // Another visible profile of `collection` than the selected one.
    static std::string om_other_preset(const PresetCollection& collection)
    {
        const std::string current = collection.get_edited_preset().name;
        for (const Preset& preset : collection)
            if (preset.is_visible && preset.is_compatible && !preset.is_default && preset.name != current)
                return preset.name;
        return {};
    }

    // Starts recording the chat's activity list; om_one_switch reads it.
    void om_watch_edits()
    {
        m_om_edits.clear();
        m_om_edit_feed.emplace(installed_shell()->workspace()->subscribe_edits(
            [self = shared_from_this()](const Workspace::WorkspaceEdit& edit) { self->m_om_edits.push_back(edit); }));
    }
    // The switch reads as the same switch without edits would: profile
    // switches only, `rows` of them. The dropped edits are part of the
    // switch, not settings the person changed.
    void om_one_switch(const std::string& name, std::size_t rows)
    {
        for (const auto& row : m_om_edits)
            std::cerr << "HARNESS MESSAGE NOTE " << name << "_activity kind=" << int(row.kind) << " label=" << row.label << " key=" << row.key
                      << '\n';
        m_om_edit_feed.reset();
        if (!om_taking())
            return;
        check(m_om_edits.size() == rows &&
                  std::none_of(m_om_edits.begin(), m_om_edits.end(), [](const auto& row) { return row.kind != Workspace::EditKind::Preset; }),
              name + "_reads_as_a_switch_without_edits_would");
    }

    // A switch with an unsaved edit: in the classic view OrcaSlicer asks; in
    // the JusPrin view nothing opens, the switch happens and the edit is
    // dropped, kept in the version saved first.
    void om_switch_case(const std::string& name, Preset::Type type, const std::string& expected, std::size_t rows = 1)
    {
        bool              opened = false;
        const auto*       match  = om_report(name, "Transfer or discard", opened);
        PresetBundle&     bundle = *wxGetApp().preset_bundle;
        const std::string now    = type == Preset::TYPE_PRINTER  ? bundle.printers.get_edited_preset().name :
                                   type == Preset::TYPE_FILAMENT ? bundle.filaments.get_edited_preset().name :
                                                                   bundle.prints.get_edited_preset().name;
        std::cerr << "HARNESS MESSAGE NOTE " << name << " now='" << now << "' dirty=" << om_dirty(type) << '\n';
        if (!om_taking()) {
            check(opened, name + "_classic_view_orca_asks");
            m_om_edit_feed.reset();
            return;
        }
        check(!opened, name + "_asks_nothing");
        check(match != nullptr && match->from == OrcaMessages::TakenMessage::From::UnsavedChanges, name + "_version_saved_and_edits_dropped");
        check(now == expected && !om_dirty(type), name + "_switched");
        om_one_switch(name, rows);
    }

    // What the person would get back by rewinding: the selected profiles,
    // the value each om_edit changes, which profiles hold unsaved changes,
    // and the objects.
    struct OmProjectState
    {
        std::string printer, filament, process, height, notes, density;
        bool        printer_dirty{false}, filament_dirty{false}, process_dirty{false};
        std::size_t objects{0};
        // The same profiles, values and objects, and every unsaved change
        // `before` had is unsaved again. A restore also marks the printer
        // changed in two settings nobody touched (the report's open items),
        // so a profile marked changed only here is logged, not compared.
        bool brings_back(const OmProjectState& before) const
        {
            return printer == before.printer && filament == before.filament && process == before.process && height == before.height &&
                   notes == before.notes && density == before.density && (printer_dirty || !before.printer_dirty) &&
                   (filament_dirty || !before.filament_dirty) && (process_dirty || !before.process_dirty) && objects == before.objects;
        }
    };
    OmProjectState m_om_before;

    OmProjectState om_state() const
    {
        PresetBundle&  bundle = *wxGetApp().preset_bundle;
        OmProjectState state;
        state.printer        = bundle.printers.get_edited_preset().name;
        state.filament       = bundle.filaments.get_edited_preset().name;
        state.process        = bundle.prints.get_edited_preset().name;
        state.height         = bundle.printers.get_edited_preset().config.opt_serialize("printable_height");
        state.notes          = bundle.filaments.get_edited_preset().config.opt_serialize("filament_notes");
        state.density        = bundle.prints.get_edited_preset().config.opt_serialize("sparse_infill_density");
        state.printer_dirty  = om_dirty(Preset::TYPE_PRINTER);
        state.filament_dirty = om_dirty(Preset::TYPE_FILAMENT);
        state.process_dirty  = om_dirty(Preset::TYPE_PRINT);
        state.objects        = m_plater->model().objects.size();
        return state;
    }
    static void om_log_state(const std::string& name, const OmProjectState& state)
    {
        std::cerr << "HARNESS MESSAGE STATE " << name << " printer='" << state.printer << "' dirty=" << state.printer_dirty << " height="
                  << state.height << " filament='" << state.filament << "' dirty=" << state.filament_dirty << " notes='" << state.notes
                  << "' process='" << state.process << "' dirty=" << state.process_dirty << " density=" << state.density
                  << " objects=" << state.objects << " slots=[";
        for (const std::string& filament : wxGetApp().preset_bundle->filament_presets)
            std::cerr << filament << ";";
        std::cerr << "]\n";
    }

    // Before a switch or a close that drops unsaved changes: what a rewind
    // must bring back.
    void om_note_before(const std::string& name)
    {
        m_om_before = om_state();
        om_log_state(name + "_before", m_om_before);
    }

    // Kenneth, 2026-11-18: "the user will be able to rewind to everything
    // right before the switch, even if the unsaved setting changes were
    // dropped." The person opens Version history after the app's next save,
    // finds the version the project was on as the switch returned, and
    // presses Restore beside it; the project is then as it was before.
    void om_rewind_case(const std::string& name, const std::string& version)
    {
        auto*     autosave = installed_shell()->autosave();
        LeftPane* pane     = installed_shell()->left_pane();
        check(!version.empty(), name + "_left_a_version");
        if (version.empty() || autosave == nullptr || pane == nullptr)
            return;
        // The project as it is after the switch is saved a moment later.
        autosave->save_now();
        const bool was_collapsed = installed_shell()->is_left_pane_collapsed();
        if (was_collapsed) {
            installed_shell()->set_left_pane_collapsed(false);
            wxYield();
        }
        pane->show_project();
        pane->open_project_view(ProjectView::Versions);
        pane->snapshot();
        // Version history lists every version but the current one, newest
        // first, each row a Restore button named with the time it was saved.
        std::vector<std::string> listed;
        const auto               history = autosave->history();
        const std::string        current = autosave->current_version();
        for (auto row = history.rbegin(); row != history.rend(); ++row)
            if (row->id != current)
                listed.push_back(row->id);
        std::vector<wxString> buttons;
        for (const wxString& label : pane->action_labels())
            if (label.StartsWith(_L("Restore") + " "))
                buttons.push_back(label);
        const auto found = std::find(listed.begin(), listed.end(), version);
        std::cerr << "HARNESS MESSAGE NOTE " << name << "_history versions=" << history.size() << " listed=" << listed.size()
                  << " buttons=" << buttons.size() << " position=" << (found - listed.begin()) << " current_is_it=" << (current == version)
                  << '\n';
        check(found != listed.end() && buttons.size() == listed.size(), name + "_version_is_in_version_history");
        if (found == listed.end() || buttons.size() != listed.size()) {
            if (was_collapsed)
                installed_shell()->set_left_pane_collapsed(true);
            return;
        }
        const std::size_t position = std::size_t(found - listed.begin());
        const wxString    label    = buttons[position];
        std::size_t       occurrence = 0;
        for (std::size_t row = 0; row < position; ++row)
            occurrence += buttons[row] == label ? 1 : 0;
        std::size_t same_minute = 0;
        for (const wxString& button : buttons)
            same_minute += button == label ? 1 : 0;
        std::cerr << "HARNESS MESSAGE NOTE " << name << "_row label='" << label.ToUTF8().data() << "' row=" << position + 1
                  << " rows_with_the_same_label=" << same_minute << '\n';
        save_pane(name + "-version-history", pane->snapshot());
        pane->set_restore_confirmation([] { return true; });
        check(pane->press_action(label, occurrence), name + "_restore_pressed");
        pane->set_restore_confirmation({});
        wxYield();
        if (was_collapsed)
            installed_shell()->set_left_pane_collapsed(true);
        const OmProjectState after = om_state();
        om_log_state(name + "_rewound", after);
        // What the restore marks changed beyond what the person had.
        {
            PresetBundle& bundle = *wxGetApp().preset_bundle;
            const auto    extra  = [&name](const char* profile, bool before, const PresetCollection& presets) {
                if (before || !presets.current_is_dirty())
                    return;
                std::cerr << "HARNESS MESSAGE NOTE " << name << "_rewind_also_marks_changed " << profile << ":";
                for (const std::string& key : presets.current_dirty_options()) {
                    std::cerr << " " << key;
                    const DynamicPrintConfig& now     = presets.get_edited_preset().config;
                    const DynamicPrintConfig& profile = presets.get_selected_preset().config;
                    if (now.has(key) && profile.has(key))
                        std::cerr << " now='" << now.opt_serialize(key) << "' profile='" << profile.opt_serialize(key) << "'";
                }
                std::cerr << '\n';
            };
            extra("printer", m_om_before.printer_dirty, bundle.printers);
            extra("filament", m_om_before.filament_dirty, bundle.filaments);
            extra("process", m_om_before.process_dirty, bundle.prints);
        }
        check(after.brings_back(m_om_before), name + "_rewinds_to_everything_before");
        // The rewound changes are unsaved again; drop them so the next case
        // starts from saved profiles.
        PresetBundle& bundle = *wxGetApp().preset_bundle;
        bundle.printers.discard_current_changes();
        bundle.filaments.discard_current_changes();
        bundle.prints.discard_current_changes();
        wxGetApp().load_current_presets(false);
    }

    void begin_orca_messages()
    {
        check(g_dialog_witness != nullptr, "dialog_witness_installed");
        if (g_dialog_witness == nullptr)
            return finish();
        g_dialog_witness->start();
        std::cerr << "HARNESS MESSAGE TAKING " << (om_taking() ? "on" : "off") << '\n';
        connect_fixture_to_fake_printer([self = shared_from_this()] { self->run_orca_messages(); });
    }

    void run_orca_messages()
    {
        auto  self  = shared_from_this();
        auto& steps = m_issue_steps;
        const std::string device = JUSPRIN_FAKE_BAMBU_DEV_ID;

        // The printer replies FAIL to "project_file". OrcaSlicer's
        // MachineObject::parse_json calls GUI_App::push_notification with the
        // printer, which queues the box titled Info.
        const auto refuse_print_job = [device, self] {
            MachineObject* printer = wxGetApp().getDeviceManager()->get_my_machine(device);
            self->check(printer != nullptr, "om_fake_printer_known_to_orca");
            if (printer != nullptr)
                printer->parse_json("lan", R"({"print":{"command":"project_file","result":"FAIL","sequence_id":"0"}})");
        };

        // -- A chat is going on: the person asked something and the AI
        //    answered. ------------------------------------------------------
        steps.push_back({"om_chat_going_on", [self] {
            installed_shell()->set_agent_pane_collapsed(false);
            self->check(self->project_host().submit_issue_message("om-chat", "Is my printer ready to print?", "{}") ==
                            Agent::AgentHost::SubmitResult::Started,
                        "om_chat_starts");
        }, [self] { return !self->project_host().busy() && self->om_quiet(); }, {}});

        // -- The printer refuses a print job, with that chat on screen. ------
        steps.push_back({"om_print_job_refused", [self, refuse_print_job] {
            self->om_mark();
            refuse_print_job();
        }, self->om_quiet_and(), [self, device] {
            self->om_taken_case("print_job_refused", "Failed to start print job", "printer_notification", "printer_message", device);
            self->om_entry_case("print_job_refused", "Failed to start print job");
            self->om_picture("printer-message-in-a-chat");
        }});

        // -- The network plug-in tells the app to update. It names no
        //    printer: GUI_App::process_network_msg handles these messages
        //    only when the device id is empty, and opens a box with only OK
        //    and no parent. -------------------------------------------------
        steps.push_back({"om_plugin_message", [self] {
            self->om_mark();
            wxGetApp().process_network_msg("", "update_studio");
        }, self->om_quiet_and(), [self] {
            const auto* taken = self->om_taken_case("plugin_message", "try updating", "box", "plugin_message");
            if (taken == nullptr)
                return;
            self->check(taken->cause.detail == "update_studio", "plugin_message_keeps_the_plug_ins_code");
            self->om_message_entry_case("plugin_message", "JusPrin info", "try updating");
        }});

        // -- The printer refuses the connection with code -1, from the call
        //    the header's printer menu makes: no chat waits for the answer. --
        const auto connect_started = std::make_shared<std::chrono::steady_clock::time_point>();
        steps.push_back({"om_connect_failed", [self, connect_started] {
            self->om_mark();
            const auto fake = std::dynamic_pointer_cast<FakeBambuAgent>(wxGetApp().getAgent()->get_printer_agent());
            self->check(fake != nullptr, "om_fake_agent");
            if (fake == nullptr)
                return;
            wxGetApp().getDeviceManager()->set_selected_machine("");
            wxYield();
            fake->fail_next_connect("-1");
            *connect_started = std::chrono::steady_clock::now();
            self->check(SetupCommands::select_named_printer(*self->m_plater, kFakeFixturePrinter), "om_header_connect_selects_the_printer");
        }, self->om_quiet_and([device, connect_started] {
            const auto last = Printers::last_connection_event(device);
            return last && last->kind == Printers::ConnectionEvent::Kind::Failed && last->at >= *connect_started;
        }), [self, device] {
            self->om_taken_case("connect_failed", "failed!", "app_dialog", "printer_connection", device);
            self->om_entry_case("connect_failed", "failed!");
        }});

        // -- An upload to a print host fails: a real job to a closed port.
        //    The queue's thread posts EVT_PRINTHOST_ERROR and
        //    PrintHostQueueDialog::on_error calls show_error. The upload's
        //    own entry says it failed, so the box is not shown again. -------
        const auto upload_from = std::make_shared<std::size_t>(0);
        steps.push_back({"om_host_upload_error", [self, upload_from] {
            self->om_mark();
            *upload_from = OrcaMessages::taken_messages().size() + g_dialog_witness->shown().size();
            DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
            config.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint));
            config.set_key_value("print_host", new ConfigOptionString("127.0.0.1:9"));
            config.set_key_value("printhost_apikey", new ConfigOptionString("harness"));
            PrintHostJob job(&config);
            self->check(job.printhost != nullptr, "om_print_host_made");
            if (job.printhost == nullptr)
                return;
            job.upload_data.source_path = (fs::path(data_dir()) / "messages-one-shell.stl").string();
            { std::ofstream out(job.upload_data.source_path.string()); out << "solid shells\n"; write_box(out, {-10, -10, 0}, {10, 10, 10}); out << "endsolid shells\n"; }
            job.upload_data.upload_path = "orca-messages.gcode";
            job.upload_data.post_action = PrintHostPostUploadAction::None;
            // JusPrin's Print starts the recorder listening before any upload
            // is queued; so does this.
            installed_shell()->gcode_history()->listen_to_print_host_queue();
            wxGetApp().printhost_job_queue().enqueue(std::move(job));
        }, self->om_quiet_and([upload_from] { return OrcaMessages::taken_messages().size() + g_dialog_witness->shown().size() > *upload_from; }),
        [self] {
            const auto* taken = self->om_taken_case("host_upload_error", "Error uploading to print host", "error_box", "host_upload", "127.0.0.1:9");
            if (taken == nullptr)
                return;
            // The upload's own entry in the chat ends failed with
            // OrcaSlicer's words; nothing else says it.
            const auto entries = self->om_new_entries();
            self->check(std::none_of(entries.begin(), entries.end(),
                                     [](const auto& entry) { return entry.kind != "upload"; }),
                        "host_upload_error_makes_no_second_entry");
            self->check(!self->om_on_canvas("Error uploading to print host"), "host_upload_error_is_not_on_the_canvas");
            const auto upload = std::find_if(entries.begin(), entries.end(), [](const auto& entry) { return entry.kind == "upload"; });
            self->check(upload != entries.end() && upload->state == "failed" && upload->detail.find("Error uploading to print host") != std::string::npos,
                        "host_upload_error_ends_the_upload_entry_failed");
        }});

        // -- Split, as OrcaSlicer's own right-click menu runs it, on an object
        //    with one shell: "The selected object couldn't be split." No
        //    JusPrin code is at the source, so there is no cause. -----------
        steps.push_back({"om_split_refused", [self] {
            self->om_import_boxes("messages-one-shell.stl", false);
            self->om_mark();
            self->check(self->m_plater->select_object(self->m_plater->model().objects.size() - 1), "om_object_selected");
            self->m_plater->split_object();
        }, self->om_quiet_and(), [self] {
            if (self->om_taken_case("split_refused", "couldn't be split", "box", "none") == nullptr)
                return;
            self->om_message_entry_case("split_refused", "Warning", "couldn't be split");
            self->om_picture("message-without-a-cause-in-a-chat");
        }});

        // -- The same command on two shells, one floating: OrcaSlicer asks
        //    "Disable Auto-Drop...?". A question stays the person's. --------
        steps.push_back({"om_split_question", [self] {
            self->om_import_boxes("messages-two-shells.stl", true);
            self->om_mark();
            self->check(self->m_plater->select_object(self->m_plater->model().objects.size() - 1), "om_object_selected");
            self->m_plater->split_object();
        }, self->om_quiet_and(), [self] { self->om_shown_case("split_question", "Auto-Drop"); }});

        // -- OrcaSlicer's settings window is open and its code reports a bad
        //    value the way Field.cpp does. The box belongs with that window. -
        steps.push_back({"om_message_in_settings_window", [self] {
            ParamsDialog* settings = wxGetApp().params_dialog();
            self->check(settings != nullptr, "om_settings_window_exists");
            if (settings == nullptr)
                return;
            settings->Popup();
            self->om_mark();
            Slic3r::GUI::show_error(settings, _L("Invalid numeric."));
        }, self->om_quiet_and(), [self] { self->om_shown_case("message_in_settings_window", "Invalid numeric"); }});

        // -- With the settings window still open, the person picks another
        //    process in its drop-down while the process has an unsaved edit:
        //    the drop-down calls Tab::select_preset. ------------------------
        steps.push_back({"om_edit_before_settings_switch", [self] { self->om_edit(Preset::TYPE_PRINT, "37"); }, {}, {}});
        steps.push_back({"om_settings_window_switch", [self] {
            self->m_om_switched_to = om_other_preset(wxGetApp().preset_bundle->prints);
            self->check(!self->m_om_switched_to.empty() && om_dirty(Preset::TYPE_PRINT), "om_settings_switch_has_an_edit_and_another_process");
            self->om_mark();
            self->om_watch_edits();
            wxGetApp().get_tab(Preset::TYPE_PRINT)->select_preset(self->m_om_switched_to);
        }, self->om_quiet_and(), [self] {
            self->om_switch_case("settings_window_switch", Preset::TYPE_PRINT, self->m_om_switched_to);
            if (ParamsDialog* settings = wxGetApp().params_dialog())
                settings->Hide();
        }});

        // -- File > Import: Plater::add_file opens the file open's scope and
        //    then the picker. The picker is the person's. -------------------
        steps.push_back({"om_import_picker", [self] {
            self->om_mark();
            self->m_plater->add_file();
        }, self->om_quiet_and(), [self] {
            // The system's picker has no title or text of wx's to read: any
            // window that reached the witness in this step is the picker.
            bool opened = false;
            self->om_report("import_picker", "Choose one or more files", opened);
            const std::size_t reached = self->dialogs_shown_since(self->m_om_opened_mark).size();
            // Without the taking, the file open's listener cancels the picker
            // before it is drawn, as the JusPrin view did before.
            if (om_taking())
                self->check(reached == 1, "import_picker_reaches_the_person");
            else
                self->check(reached == 0, "import_picker_without_taking_is_cancelled_unseen");
        }});

        // -- A filament switch with an unsaved filament edit, through the call
        //    the header's filament menu makes. ------------------------------
        steps.push_back({"om_edit_before_filament_switch", [self] { self->om_edit(Preset::TYPE_FILAMENT, "messages check"); }, {}, {}});
        steps.push_back({"om_filament_switch", [self] {
            self->m_om_switched_to = om_other_preset(wxGetApp().preset_bundle->filaments);
            self->check(!self->m_om_switched_to.empty() && om_dirty(Preset::TYPE_FILAMENT), "om_filament_switch_has_an_edit_and_another_filament");
            self->om_note_before("filament_switch");
            self->om_mark();
            self->om_watch_edits();
            SetupCommands::select_filament_preset(*self->m_plater, 0, self->m_om_switched_to);
            self->m_om_version_at_switch = installed_shell()->autosave()->current_version();
        }, self->om_quiet_and(), [self] { self->om_switch_case("filament_switch", Preset::TYPE_FILAMENT, self->m_om_switched_to); }});
        steps.push_back({"om_rewind_filament_switch", [self] {
            if (om_taking())
                self->om_rewind_case("filament_switch", self->m_om_version_at_switch);
        }, self->om_quiet_and(), {}});

        // -- A process switch with an unsaved process edit, through JusPrin's
        //    own call (SetupCommands::select_process_preset), which the import
        //    window makes to put back the project's process. -----------------
        steps.push_back({"om_edit_before_process_switch", [self] { self->om_edit(Preset::TYPE_PRINT, "36"); }, {}, {}});
        steps.push_back({"om_process_switch", [self] {
            self->m_om_switched_to = om_other_preset(wxGetApp().preset_bundle->prints);
            self->check(!self->m_om_switched_to.empty() && om_dirty(Preset::TYPE_PRINT), "om_process_switch_has_an_edit_and_another_process");
            self->om_note_before("process_switch");
            self->om_mark();
            self->om_watch_edits();
            self->check(SetupCommands::select_process_preset(*self->m_plater, self->m_om_switched_to), "om_process_switch_finds_the_process");
            self->m_om_version_at_switch = installed_shell()->autosave()->current_version();
        }, self->om_quiet_and(), [self] { self->om_switch_case("process_switch", Preset::TYPE_PRINT, self->m_om_switched_to); }});
        steps.push_back({"om_rewind_process_switch", [self] {
            if (om_taking())
                self->om_rewind_case("process_switch", self->m_om_version_at_switch);
        }, self->om_quiet_and(), {}});

        // -- Home's printer card opens the settings of a printer that is not
        //    selected: it selects that printer first
        //    (Printers::open_named_printer_settings), with an unsaved edit on
        //    the selected one. -------------------------------------------------
        steps.push_back({"om_home_card_fixture", [self] {
            // A named printer is made from the selected system profile.
            const Preset* system = wxGetApp().preset_bundle->printers.get_selected_preset_parent();
            self->check(system != nullptr && SetupCommands::select_printer_preset(*self->m_plater, system->name), "om_home_card_system_profile");
            self->m_om_named = Printers::add_named_printer(*self->m_plater, "Messages Spare", {});
            // A switch between the two printers with no edits: what the
            // activity list shows for one.
            self->om_watch_edits();
            self->check(SetupCommands::select_named_printer(*self->m_plater, kFakeFixturePrinter), "om_home_card_back_on_the_fake_printer");
        }, self->om_quiet_and(), [self] {
            self->m_om_clean_switch_rows = self->m_om_edits.size();
            for (const auto& row : self->m_om_edits)
                std::cerr << "HARNESS MESSAGE NOTE clean_printer_switch_activity kind=" << int(row.kind) << " label=" << row.label << '\n';
            self->m_om_edit_feed.reset();
        }});
        steps.push_back({"om_edit_before_home_card", [self] { self->om_edit(Preset::TYPE_PRINTER, "173"); }, {}, {}});
        steps.push_back({"om_home_card_settings", [self] {
            self->check(om_printer_dirty(), "om_home_card_has_an_edit");
            self->om_mark();
            self->om_watch_edits();
            const wxString refused = Printers::open_named_printer_settings(*self->m_plater, self->m_om_named);
            self->check(refused.empty(), "om_home_card_settings_opens");
        }, self->om_quiet_and(), [self] {
            self->om_switch_case("home_card_settings", Preset::TYPE_PRINTER, self->m_om_named, self->m_om_clean_switch_rows);
            if (ParamsDialog* settings = wxGetApp().params_dialog())
                settings->Hide();
            self->check(SetupCommands::select_named_printer(*self->m_plater, kFakeFixturePrinter), "om_home_card_back_again");
        }});

        // -- The question OrcaSlicer asks before it reloads profiles, on the
        //    path the import window takes, which reads the recorded choice. -
        steps.push_back({"om_edit_before_reload", [self] { self->om_edit(Preset::TYPE_PRINTER, "172"); }, {}, {}});
        steps.push_back({"om_unsaved_before_reload", [self] {
            self->check(om_printer_dirty(), "om_reload_has_an_edit");
            self->om_mark();
            self->m_om_went = wxGetApp().check_and_save_current_preset_changes(_L("Import from OrcaSlicer"),
                                                                                _L("Importing these printers loads their profiles."), false);
        }, self->om_quiet_and(), [self] {
            bool        opened = false;
            const auto* match  = self->om_report("unsaved_before_reload", "unsaved changes", opened);
            std::cerr << "HARNESS MESSAGE NOTE went_on=" << self->m_om_went << " dirty=" << om_printer_dirty() << '\n';
            if (!om_taking()) {
                self->check(opened, "unsaved_before_reload_classic_view_orca_asks");
                return;
            }
            self->check(!opened, "unsaved_before_reload_asks_nothing");
            self->check(match != nullptr && self->m_om_went && !om_printer_dirty(), "unsaved_before_reload_goes_on_with_edits_dropped");
        }});

        // -- The same question on the path the setup wizard's Finish takes
        //    (ConfigWizard::priv::check_and_install_missing_materials ends in
        //    GUI_App::check_and_keep_current_preset_changes). ----------------
        steps.push_back({"om_edit_before_wizard", [self] { self->om_edit(Preset::TYPE_PRINTER, "174"); }, {}, {}});
        steps.push_back({"om_unsaved_before_wizard", [self] {
            self->check(om_printer_dirty(), "om_wizard_has_an_edit");
            self->om_mark();
            bool apply_kept = false;
            self->m_om_went = wxGetApp().check_and_keep_current_preset_changes(_L("Configuration is editing from ConfigWizard"),
                                                                                _L("Custom printer was installed and it will be activated."),
                                                                                ActionButtons::KEEP | ActionButtons::SAVE, &apply_kept);
        }, self->om_quiet_and(), [self] {
            bool        opened = false;
            const auto* match  = self->om_report("unsaved_before_wizard", "unsaved changes", opened);
            std::cerr << "HARNESS MESSAGE NOTE went_on=" << self->m_om_went << " dirty=" << om_printer_dirty() << '\n';
            if (!om_taking()) {
                self->check(opened, "unsaved_before_wizard_classic_view_orca_asks");
                return;
            }
            self->check(!opened, "unsaved_before_wizard_asks_nothing");
            self->check(match != nullptr && self->m_om_went && !om_printer_dirty(), "unsaved_before_wizard_goes_on_with_edits_dropped");
        }});

        // -- A switch when no version can be saved: nothing changes, nothing
        //    of OrcaSlicer's opens, and the failure is reported as
        //    unexpected. ------------------------------------------------------
        const auto unavailable = std::make_shared<std::unique_ptr<UnavailableFolder>>();
        steps.push_back({"om_unsaveable_switch", [self, unavailable] {
            // The edit and the switch in one step, so autosave has no turn
            // between them to save the edit.
            PresetCollection& prints = wxGetApp().preset_bundle->prints;
            prints.get_edited_preset().config.set_key_value("sparse_infill_density", new ConfigOptionPercent(38));
            wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
            self->check(om_dirty(Preset::TYPE_PRINT), "om_unsaveable_switch_has_an_edit");
            self->m_om_switched_to = om_other_preset(prints);
            self->om_mark();
            self->m_om_reported_before = unexpected_errors_logged();
            *unavailable = std::make_unique<UnavailableFolder>(std::filesystem::path(data_dir()) / "jusprin" / "projects" /
                                                               self->persistence().document().project_id() / "pending");
            SetupCommands::select_process_preset(*self->m_plater, self->m_om_switched_to);
            unavailable->reset();
        }, self->om_quiet_and(), [self] {
            bool opened = false;
            self->om_report("unsaveable_switch", "Transfer or discard", opened);
            if (!om_taking()) {
                self->check(opened, "unsaveable_switch_classic_view_orca_asks");
                return;
            }
            self->check(!opened, "unsaveable_switch_asks_nothing");
            self->check(wxGetApp().preset_bundle->prints.get_edited_preset().name != self->m_om_switched_to && om_dirty(Preset::TYPE_PRINT),
                        "unsaveable_switch_changes_nothing");
            self->check(unexpected_errors_logged() == self->m_om_reported_before + 1, "unsaveable_switch_reports_it");
            wxGetApp().preset_bundle->prints.discard_current_changes();
            wxGetApp().load_current_presets(false);
        }});

        // -- Code switches the main window to a page the JusPrin view hides,
        //    with the call MainFrame::jump_to_monitor and the "Go to" keys
        //    make. The Calibration page stands in for the Device page, which
        //    is not in the page list without the Bambu plug-in. -------------
        steps.push_back({"om_page_switch", [self] {
            self->om_mark();
            wxGetApp().mainframe->m_tabpanel->SelectPageByName(TAB_ID_CALIBRATION);
        }, self->om_quiet_and(), [self] {
            bool              opened = false;
            const auto*       match  = self->om_report("page_switch", TAB_ID_CALIBRATION, opened);
            const std::string page   = wxGetApp().mainframe->m_tabpanel->GetSelectedPageName().ToUTF8().data();
            std::cerr << "HARNESS MESSAGE NOTE page_now=" << page << '\n';
            if (!om_taking()) {
                self->check(page == TAB_ID_CALIBRATION, "page_switch_classic_view_shows_the_page");
                wxGetApp().mainframe->select_tab(wxString(TAB_ID_PREPARE));
                return;
            }
            self->check(page == TAB_ID_PREPARE && match != nullptr, "page_switch_is_refused");
        }});

        // -- The key that steps through OrcaSlicer's pages (Ctrl+Tab), pressed
        //    three times on the main window. --------------------------------
        const auto pages = std::make_shared<std::string>();
        steps.push_back({"om_ctrl_tab", [self, pages] {
            self->om_mark();
            for (int press = 0; press < 3; ++press) {
                wxKeyEvent key(wxEVT_CHAR_HOOK);
                key.m_keyCode = WXK_TAB;
#ifdef __APPLE__
                key.SetRawControlDown(true);
#else
                key.SetControlDown(true);
#endif
                key.SetEventObject(wxGetApp().mainframe);
                wxGetApp().mainframe->GetEventHandler()->ProcessEvent(key);
                *pages += std::string(wxGetApp().mainframe->m_tabpanel->GetSelectedPageName().ToUTF8().data()) + " ";
            }
        }, self->om_quiet_and(), [self, pages] {
            bool opened = false;
            self->om_report("ctrl_tab", "", opened);
            std::cerr << "HARNESS MESSAGE NOTE pages_after_each_press=" << *pages << '\n';
            const bool hidden_page = pages->find(TAB_ID_PREVIEW) != std::string::npos || pages->find(TAB_ID_MONITOR) != std::string::npos ||
                                     pages->find(TAB_ID_CALIBRATION) != std::string::npos || pages->find(TAB_ID_MULTI_DEVICE) != std::string::npos;
            if (om_taking())
                self->check(!hidden_page, "ctrl_tab_reaches_no_hidden_page");
            else
                self->check(hidden_page, "ctrl_tab_classic_view_reaches_a_hidden_page");
            wxGetApp().mainframe->select_tab(wxString(TAB_ID_PREPARE));
        }});

        // -- A start G-code the placeholder parser refuses: a critical slicing
        //    error after slice_start has returned. Plater's handler of the
        //    finished slice calls show_error; the print issues say it. ------
        steps.push_back({"om_slice_error_setup", [self] {
            const std::string printer = wxGetApp().preset_bundle->printers.get_edited_preset().name;
            self->patch_settings({{"scope", "printer"}, {"target", {{"preset", printer}}}, {"changes", {{"machine_start_gcode", "{if}"}}}});
        }, {}, {}});
        steps.push_back({"om_slice_error", [self] {
            self->om_mark();
            self->m_slice_started = std::chrono::steady_clock::now();
            self->run_dialog_tool("slice_start", nlohmann::json::object());
        }, self->slice_ended(), [self] {
            const auto* taken = self->om_taken_case("slice_error", "start_gcode", "error_box", "slice");
            if (taken == nullptr)
                return;
            self->check(taken->cause.told_elsewhere, "slice_error_is_told_by_the_print_issues");
            self->check(!self->om_on_canvas("start_gcode"), "slice_error_is_not_a_second_confirmation");
            const auto entries = self->om_new_entries();
            self->check(std::none_of(entries.begin(), entries.end(),
                                     [](const auto& entry) { return entry.kind == OrcaMessages::kPrinterEntryKind; }),
                        "slice_error_makes_no_printer_entry");
        }});
        steps.push_back({"om_slice_error_undo", [self] {
            const std::string printer = wxGetApp().preset_bundle->printers.get_edited_preset().name;
            wxGetApp().preset_bundle->printers.discard_current_changes();
            wxGetApp().load_current_presets(false);
            std::cerr << "HARNESS MESSAGE NOTE printer '" << printer << "' start G-code edit dropped\n";
        }, self->om_quiet_and(), {}});

        // -- The printer refuses a print job while the person is on Home,
        //    with the chat pane collapsed. ---------------------------------
        steps.push_back({"om_on_home", [self] {
            installed_shell()->set_agent_pane_collapsed(true);
            wxGetApp().mainframe->select_tab(wxString(TAB_ID_HOME));
        }, self->om_quiet_and(), {}});
        steps.push_back({"om_print_job_refused_on_home", [self, refuse_print_job] {
            self->om_mark();
            refuse_print_job();
        }, self->om_quiet_and(), [self, device] {
            self->om_taken_case("print_job_refused_on_home", "Failed to start print job", "printer_notification", "printer_message", device);
            self->om_entry_case("print_job_refused_on_home", "Failed to start print job");
            self->check(wxGetApp().mainframe->m_tabpanel->GetSelectedPageName() == TAB_ID_HOME, "print_job_refused_on_home_stays_on_home");
            self->om_picture("printer-message-on-home");
        }});
        // -- A message with no cause while on Home, with the words and the
        //    call TaskManager uses. ------------------------------------------
        steps.push_back({"om_no_cause_on_home", [self] {
            self->om_mark();
            installed_shell()->set_agent_pane_collapsed(true);
            wxGetApp().show_dialog(_L("The printing task exceeds the limit, supporting a maximum of 6 printers."));
        }, self->om_quiet_and(), [self] {
            if (self->om_taken_case("no_cause_on_home", "exceeds the limit", "app_dialog", "none") == nullptr)
                return;
            self->om_message_entry_case("no_cause_on_home", _u8L("Info"), "exceeds the limit");
            self->check(wxGetApp().mainframe->m_tabpanel->GetSelectedPageName() == TAB_ID_HOME, "no_cause_on_home_stays_on_home");
            self->om_picture("message-without-a-cause-on-home");
        }});
        steps.push_back({"om_back_from_home", [self] {
            wxGetApp().mainframe->select_tab(wxString(TAB_ID_PREPARE));
        }, self->om_quiet_and(), [self] { self->om_picture("messages-after-home"); }});

        // -- A project close when no version can be saved: the same rule as
        //    a switch. The close does not happen, nothing opens, and the
        //    failure is reported as unexpected. ------------------------------
        const auto unavailable_close = std::make_shared<std::unique_ptr<UnavailableFolder>>();
        steps.push_back({"om_unsaveable_close", [self, unavailable_close] {
            PresetCollection& prints = wxGetApp().preset_bundle->prints;
            prints.get_edited_preset().config.set_key_value("sparse_infill_density", new ConfigOptionPercent(33));
            wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
            self->check(om_dirty(Preset::TYPE_PRINT), "om_unsaveable_close_has_an_edit");
            self->m_om_switched_to     = self->persistence().document().project_id();
            self->m_om_reported_before = unexpected_errors_logged();
            self->om_mark();
            *unavailable_close = std::make_unique<UnavailableFolder>(std::filesystem::path(data_dir()) / "jusprin" / "projects" /
                                                                     self->m_om_switched_to / "pending");
            self->m_om_went = self->m_plater->new_project(true, true) != wxID_CANCEL;
            unavailable_close->reset();
        }, self->om_quiet_and(), [self] {
            bool opened = false;
            self->om_report("unsaveable_close", "Transfer or discard", opened);
            self->check(!opened, "unsaveable_close_asks_nothing");
            self->check(!self->m_om_went, "unsaveable_close_does_not_close");
            self->check(self->persistence().document().project_id() == self->m_om_switched_to && om_dirty(Preset::TYPE_PRINT),
                        "unsaveable_close_changes_nothing");
            self->check(unexpected_errors_logged() == self->m_om_reported_before + 1, "unsaveable_close_reports_it");
            wxGetApp().preset_bundle->prints.discard_current_changes();
            wxGetApp().load_current_presets(false);
        }});

        // -- A project closed with unsaved changes in all three profiles,
        //    through the same function as a switch. The person opens the
        //    project again from Home, carries on, and rewinds. ---------------
        steps.push_back({"om_edits_before_close", [self] {
            if (!om_taking())
                return;
            self->om_edit(Preset::TYPE_PRINTER, "169");
            self->om_edit(Preset::TYPE_FILAMENT, "close check");
            self->om_edit(Preset::TYPE_PRINT, "31");
        }, self->om_quiet_and(), {}});
        steps.push_back({"om_close_with_edits", [self] {
            if (!om_taking())
                return;
            self->om_note_before("close");
            self->m_om_closed_project = self->persistence().document().project_id();
            self->om_mark();
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "close_with_edits_closes");
        }, self->om_quiet_and(), [self] {
            if (!om_taking())
                return;
            bool opened = false;
            self->om_report("close_with_edits", "Transfer or discard", opened);
            self->check(!opened, "close_with_edits_asks_nothing");
            self->check(self->persistence().document().project_id() != self->m_om_closed_project, "close_with_edits_left_the_project");
        }});
        steps.push_back({"om_reopen_after_close", [self] {
            if (!om_taking())
                return;
            auto* autosave = installed_shell()->autosave();
            self->check(autosave->open_managed_project(self->m_om_closed_project), "close_project_opens_again");
            // The version the close saved is the project's newest.
            const auto history = autosave->history();
            self->m_om_version_at_switch = history.empty() ? std::string() : history.back().id;
            om_log_state("close_reopened", self->om_state());
            std::cerr << "HARNESS MESSAGE NOTE close_reopened newest=" << self->m_om_version_at_switch
                      << " current=" << autosave->current_version() << '\n';
        }, self->om_quiet_and(), {}});
        steps.push_back({"om_edit_after_reopen", [self] {
            // The person carries on in the reopened project, so the version
            // the close left becomes an earlier one.
            if (om_taking())
                self->om_edit(Preset::TYPE_PRINT, "29");
        }, self->om_quiet_and(), {}});
        steps.push_back({"om_rewind_close", [self] {
            if (om_taking())
                self->om_rewind_case("close", self->m_om_version_at_switch);
        }, self->om_quiet_and(), {}});

        // -- The printer refuses a print job in a new project, whose chat is
        //    empty. ----------------------------------------------------------
        const auto new_project = [self](const std::string& name) {
            self->answer_modals(wxID_NO);
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, name);
            self->stop_answering_modals();
        };
        // -- A message with no cause in a new project, whose chat is empty. --
        steps.push_back({"om_empty_chat_for_no_cause", [new_project] { new_project("om_new_project_for_no_cause"); }, self->om_quiet_and(), {}});
        steps.push_back({"om_no_cause_in_an_empty_chat", [self] {
            self->om_mark();
            wxGetApp().show_dialog(_L("The printing task exceeds the limit, supporting a maximum of 6 printers."));
        }, self->om_quiet_and(), [self] {
            if (self->om_taken_case("no_cause_in_an_empty_chat", "exceeds the limit", "app_dialog", "none") == nullptr)
                return;
            self->om_message_entry_case("no_cause_in_an_empty_chat", _u8L("Info"), "exceeds the limit");
            self->om_picture("message-without-a-cause-in-an-empty-chat");
        }});
        steps.push_back({"om_empty_chat", [new_project] { new_project("om_new_project"); }, self->om_quiet_and(), {}});
        steps.push_back({"om_print_job_refused_in_an_empty_chat", [self, refuse_print_job] {
            self->om_mark();
            refuse_print_job();
        }, self->om_quiet_and(), [self, device] {
            self->om_taken_case("print_job_refused_in_an_empty_chat", "Failed to start print job", "printer_notification", "printer_message", device);
            self->om_entry_case("print_job_refused_in_an_empty_chat", "Failed to start print job");
            self->om_picture("printer-message-in-an-empty-chat");
        }});

        // -- The copy of one nozzle's settings to the other on a two-nozzle
        //    printer (Tab::sync_excluder) asks with the same window class as
        //    the unsaved-changes question. It is a question about a copy,
        //    and stays the person's. ----------------------------------------
        steps.push_back({"om_two_nozzles", [self] {
            std::string error;
            self->check(SetupCommands::install_and_select_printer(*self->m_plater, "BBL", "Bambu Lab H2D", "0.4", {}, error),
                        "om_two_nozzle_printer_selected");
            if (!error.empty())
                std::cerr << "HARNESS MESSAGE NOTE h2d error='" << error << "'\n";
        }, self->om_quiet_and(), {}});
        steps.push_back({"om_copy_nozzle_question", [self] {
            // The window as Tab::sync_excluder builds it, with the H2D's own
            // nozzle names. The button that calls sync_excluder copies only
            // from fields of the settings page on screen, which the harness
            // cannot press, so the window is opened here the way that code
            // opens it.
            PresetBundle&     bundle = *wxGetApp().preset_bundle;
            const std::string type   = bundle.printers.get_edited_preset().get_printer_type(&bundle);
            const std::string left   = DevPrinterConfigUtil::get_toolhead_display_name(type, 0, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase);
            const std::string right  = DevPrinterConfigUtil::get_toolhead_display_name(type, 1, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase);
            const wxString    title  = wxString::Format(_L("Modify parameters of %s"), _L(left));
            const wxString    header = wxString::Format(_L("Do you want to modify the following parameters of the %s to that of the %s?"),
                                                        _L(left), _L(right));
            std::cerr << "HARNESS MESSAGE NOTE copy_nozzle printer_type=" << type << " title='" << title.ToUTF8().data() << "'\n";
            DynamicConfig changed;
            changed.set_key_value("outer_wall_speed", bundle.prints.get_edited_preset().config.option("outer_wall_speed")->clone());
            self->om_mark();
            wxGetApp().CallAfter([title, header, changed]() mutable {
                UnsavedChangesDialog dialog(title, header, &changed, 0, 1, true, NozzleVolumeType::nvtStandard);
                dialog.ShowModal();
            });
        }, self->om_quiet_and(), [self] {
            bool opened = false;
            self->om_report("copy_nozzle_question", "Modify parameters of", opened);
            self->check(opened, "copy_nozzle_question_opens");
        }});

        // -- A printer switch with an unsaved process edit, where the process
        //    does not fit the new printer: OrcaSlicer asks about the process
        //    edits in the printer branch of Tab::select_preset. The H2D's
        //    process does not fit the A1 mini. ---------------------------------
        steps.push_back({"om_edit_before_printer_switch_process", [self] { self->om_edit(Preset::TYPE_PRINT, "34"); }, {}, {}});
        steps.push_back({"om_printer_switch_process_edit", [self] {
            self->check(om_dirty(Preset::TYPE_PRINT), "om_printer_switch_process_edit_has_an_edit");
            self->m_om_switched_to = wxGetApp().preset_bundle->prints.get_edited_preset().name;
            self->om_mark();
            // In the classic view the window is answered Cancel, and the
            // switch is refused.
            const bool switched = SetupCommands::select_named_printer(*self->m_plater, kFakeFixturePrinter);
            if (om_taking())
                self->check(switched, "om_printer_switch_process_edit_switched");
        }, self->om_quiet_and(), [self] {
            bool              opened  = false;
            const auto*       match   = self->om_report("printer_switch_process_edit", "Transfer or discard", opened);
            const std::string process = wxGetApp().preset_bundle->prints.get_edited_preset().name;
            std::cerr << "HARNESS MESSAGE NOTE printer_switch_process_edit process='" << process << "' dirty=" << om_dirty(Preset::TYPE_PRINT)
                      << '\n';
            if (!om_taking()) {
                self->check(opened, "printer_switch_process_edit_classic_view_orca_asks");
                return;
            }
            self->check(!opened, "printer_switch_process_edit_asks_nothing");
            self->check(match != nullptr && match->from == OrcaMessages::TakenMessage::From::UnsavedChanges,
                        "printer_switch_process_edit_version_saved_and_edits_dropped");
            self->check(process != self->m_om_switched_to && !om_dirty(Preset::TYPE_PRINT), "printer_switch_process_edit_process_follows_the_printer");
        }});

        // -- A printer switch with unsaved changes in the printer, the
        //    filament and the process, to a printer that keeps the same
        //    filament and process: OrcaSlicer's Discard drops all three. The
        //    person rewinds. ----------------------------------------------------
        steps.push_back({"om_edits_before_printer_switch", [self] {
            if (!om_taking())
                return;
            // A project with objects: the blank project has no versions.
            self->om_import_boxes("rewind-boxes.stl", false);
            self->om_edit(Preset::TYPE_PRINTER, "171");
            self->om_edit(Preset::TYPE_FILAMENT, "rewind check");
            self->om_edit(Preset::TYPE_PRINT, "38");
        }, self->om_quiet_and(), {}});
        steps.push_back({"om_printer_switch_all_edits", [self] {
            if (!om_taking())
                return;
            self->om_note_before("printer_switch");
            self->check(self->m_om_before.printer_dirty && self->m_om_before.filament_dirty && self->m_om_before.process_dirty,
                        "printer_switch_has_three_edits");
            self->om_mark();
            self->check(SetupCommands::select_named_printer(*self->m_plater, self->m_om_named), "printer_switch_all_edits_switched");
            self->m_om_version_at_switch = installed_shell()->autosave()->current_version();
        }, self->om_quiet_and(), [self] {
            if (!om_taking())
                return;
            bool opened = false;
            self->om_report("printer_switch_all_edits", "Transfer or discard", opened);
            const OmProjectState after = self->om_state();
            om_log_state("printer_switch_after", after);
            self->check(!opened, "printer_switch_all_edits_asks_nothing");
            self->check(after.printer == self->m_om_named && !after.printer_dirty && !after.filament_dirty && !after.process_dirty,
                        "printer_switch_all_edits_drops_all_three");
        }});
        steps.push_back({"om_rewind_printer_switch", [self] {
            if (om_taking())
                self->om_rewind_case("printer_switch", self->m_om_version_at_switch);
        }, self->om_quiet_and(), {}});

        steps.push_back({"om_summary", [self] {
            std::cerr << "HARNESS MESSAGE SUMMARY taken=" << OrcaMessages::taken_messages().size()
                      << " let_through=" << OrcaMessages::shown_windows().size() << " opened=" << g_dialog_witness->shown().size() << '\n';
        }, {}, {}});

        run_issue_steps();
    }
