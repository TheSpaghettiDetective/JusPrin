    // The dialog routing scenario (--dialog-routing <directory>). Included
    // inside class Scenario after the print-issue scenarios, whose step runner
    // and slicing helpers it uses.
    //
    // The rule under test: a dialog that opens while a tool call runs is
    // answered with the least change and listed in that call's result; every
    // other dialog shows. Each case makes a dialog open and reads whether it
    // reached the screen (the dialog witness sees only what nothing answered)
    // and what the call's result says.

    // Runs one tool as the project chat's agent would and returns its record.
    const Agent::ToolActivity& run_dialog_tool(const char* tool, const nlohmann::json& arguments)
    {
        Agent::AgentHost& host = project_host();
        std::string message_id = "dialog-routing";
        for (const auto& message : host.conversation())
            if (message.role == Agent::MessageRole::User)
                message_id = message.id;
        const std::string action = host.tools().submit({tool, arguments.dump()}, message_id).action_id;
        // The shell's own timer pumps the tools, as in the app; pumping here
        // too would run a call again from inside its own load.
        wait_for([&] { return Agent::tool_state_terminal(host.tools().find(action)->state); }, std::chrono::seconds(60));
        const Agent::ToolActivity& done = *host.tools().find(action);
        std::cerr << "HARNESS DIALOGS tool " << tool << " state=" << int(done.state) << " result=" << done.result_json.substr(0, 600)
                  << (done.error ? " error=" + done.error->code + " " + done.error->message : std::string())
                  << " appDialogs=" << done.app_dialogs_json << '\n';
        return done;
    }

    // The dialogs that reached the screen since `from`.
    std::vector<std::string> dialogs_shown_since(std::size_t from) const
    {
        const auto& shown = g_dialog_witness->shown();
        return {shown.begin() + std::min(from, shown.size()), shown.end()};
    }

    // One case: whether a dialog with `words` in its title or text reached
    // the screen since `from`, against whether it should have.
    void dialog_case(const std::string& name, std::size_t from, const std::string& words, bool should_show = false)
    {
        const std::vector<std::string> shown = dialogs_shown_since(from);
        std::cerr << "HARNESS DIALOG CASE " << name << " shown=" << shown.size();
        for (const std::string& dialog : shown)
            std::cerr << " [" << dialog << "]";
        std::cerr << '\n';
        const bool on_screen = std::any_of(shown.begin(), shown.end(), [&](const std::string& dialog) {
            return dialog.find(words) != std::string::npos;
        });
        if (should_show)
            check(on_screen, "dialog_" + name + "_shows");
        else
            check(!on_screen, "dialog_" + name + "_not_shown_over_the_chat");
    }

    nlohmann::json session_arguments()
    {
        const auto snapshot = installed_workspace_snapshot();
        return {{"sessionId", std::to_string(snapshot.session.value())}};
    }

    // The object added last, which an import adds.
    std::string last_object_id()
    {
        const auto snapshot = installed_workspace_snapshot();
        std::uint64_t last = 0;
        for (const auto& plate : snapshot.plates)
            for (const auto& object : plate.objects)
                last = std::max<std::uint64_t>(last, object.id.value());
        return last == 0 ? std::string() : std::to_string(last);
    }

    // Previews a patch and applies it with the preview's session and revision.
    const Agent::ToolActivity& patch_settings(const nlohmann::json& patch)
    {
        const Agent::ToolActivity& preview = run_dialog_tool("settings_preview_patch", patch);
        nlohmann::json apply = patch;
        const auto     seen  = nlohmann::json::parse(preview.result_json.empty() ? "{}" : preview.result_json);
        apply["expectedSessionId"] = seen.value("sessionId", std::string());
        apply["expectedRevision"]  = seen.value("revision", 0);
        return run_dialog_tool("settings_apply_patch", apply);
    }

    // Each kind of modal dialog the app can open, under one listener: every
    // one must be answered by it, never shown. OrcaSlicer's own MessageDialog
    // passes DPIAware::ShowModal; a plain wxDialog, a native message box and
    // a file picker do not.
    void verify_dialog_kinds()
    {
        wxWindow* parent = wxGetApp().mainframe;
        struct Kind
        {
            const char*                 name;
            std::function<int()>        show;
            int                         expected;
        };
        const std::vector<Kind> kinds{
            {"orca_message", [parent] { return MessageDialog(parent, "Kind check: Orca's own question", "Kind check", wxYES_NO | wxICON_QUESTION).ShowModal(); }, wxID_NO},
            {"plain_dialog", [parent] {
                 wxDialog dialog(parent, wxID_ANY, "Kind check: plain dialog");
                 auto* sizer = new wxBoxSizer(wxVERTICAL);
                 sizer->Add(new wxStaticText(&dialog, wxID_ANY, "Kind check: a plain wxDialog"));
                 sizer->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL));
                 dialog.SetSizerAndFit(sizer);
                 return dialog.ShowModal();
             }, wxID_CANCEL},
            {"native_message", [parent] {
                 // A native box on purpose: the app's own code uses MessageDialog.
                 return wxMessageDialog(parent, "Kind check: a native message box", "Kind check", wxYES_NO | wxCANCEL).ShowModal();
             }, wxID_NO},
            {"file_picker", [parent] { return wxFileDialog(parent, "Kind check: a file picker").ShowModal(); }, wxID_CANCEL},
        };
        for (const Kind& kind : kinds) {
            const std::size_t from = g_dialog_witness->shown().size();
            Workspace::ScopedLeastChangeAnswers answers;
            const int answer = kind.show();
            const std::string name = std::string("kind_") + kind.name;
            std::cerr << "HARNESS DIALOG KIND " << kind.name << " answer=" << answer << " recorded=" << answers.messages().size() << '\n';
            check(g_dialog_witness->shown().size() == from, "dialog_" + name + "_not_shown_under_a_listener");
            check(answer == kind.expected && answers.messages().size() == 1, "dialog_" + name + "_answered_with_the_least_change");
        }
    }

    void begin_dialog_routing()
    {
        auto  self  = shared_from_this();
        auto& steps = m_issue_steps;
        check(g_dialog_witness != nullptr, "dialog_witness_installed");
        if (g_dialog_witness == nullptr)
            return finish();
        g_dialog_witness->start();
        const auto from = std::make_shared<std::size_t>(0);
        const auto mark = [self, from] { *from = g_dialog_witness->shown().size(); };

        steps.push_back({"dialogs_kinds", [self] { self->verify_dialog_kinds(); }, {}, {}});

        // -- Settings the app would warn about are refused by the tools, so
        //    no dialog opens. --------------------------------------------------
        steps.push_back({"dialogs_filament_flow", [self, mark] {
            mark();
            const std::string filament = wxGetApp().preset_bundle->filaments.get_edited_preset().name;
            self->patch_settings({{"scope", "filament"}, {"target", {{"preset", filament}}}, {"changes", {{"filament_max_volumetric_speed", "0.2,0.2"}}}});
        }, {}, [self, from] { self->dialog_case("filament_flow", *from, "max volumetric speed"); }});
        steps.push_back({"dialogs_elephant_foot", [self, mark] {
            mark();
            self->patch_settings({{"scope", "process"}, {"changes", {{"elefant_foot_compensation", "1.5"}}}});
        }, {}, [self, from] { self->dialog_case("elephant_foot", *from, "elephant foot"); }});

        // -- A start G-code the placeholder parser refuses: a critical slicing
        //    error, raised after slice_start has returned. It shows: nothing
        //    carries past the end of a call. --------------------------------
        steps.push_back({"dialogs_slice_error_setup", [self] {
            const std::string printer = wxGetApp().preset_bundle->printers.get_edited_preset().name;
            self->patch_settings({{"scope", "printer"}, {"target", {{"preset", printer}}}, {"changes", {{"machine_start_gcode", "{if}"}}}});
        }, {}, {}});
        steps.push_back({"dialogs_slice_error", [self, mark] {
            mark();
            self->m_slice_started = std::chrono::steady_clock::now();
            self->run_dialog_tool("slice_start", nlohmann::json::object());
        }, self->slice_ended(), [self, from] { self->dialog_case("slice_error_after_the_call", *from, "start_gcode", true); }});

        // -- Split into shells, one floating: OrcaSlicer asks whether to keep
        //    auto-drop (Plater::priv::split_object) inside the call. --------
        steps.push_back({"dialogs_split_floating", [self, mark] {
            const std::string path = (fs::path(data_dir()) / "dialogs-two-shells.stl").string();
            {
                std::ofstream out(path);
                out << "solid shells\n";
                write_box(out, {-10, -10, 0}, {10, 10, 10});
                write_box(out, {-10, -10, 20}, {10, 10, 30});
                out << "endsolid shells\n";
            }
            nlohmann::json import = self->session_arguments();
            import["path"]        = path;
            self->run_dialog_tool("object_import_file", import);
            mark();
            nlohmann::json divide = self->session_arguments();
            divide["objectId"]    = self->last_object_id();
            divide["shells"]      = "objects";
            const Agent::ToolActivity& done = self->run_dialog_tool("object_divide", divide);
            self->check(done.app_dialogs_json.find("Auto-Drop") != std::string::npos, "dialog_split_floating_is_in_the_call_result");
        }, {}, [self, from] { self->dialog_case("split_floating", *from, "Auto-Drop"); }});

        // -- A dialog of the person's own, opened while a tool call lets the
        //    event loop run: does the call's listener take it? Reported, not
        //    checked: it is the known risk of answering by call scope. -------
        steps.push_back({"dialogs_click_during_a_call", [self] {
            Agent::AgentHost& host = self->project_host();
            bool        opened = false;
            int         answer = 0;
            std::size_t before = 0;
            wxTimer     click;
            click.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
                if (opened || host.tools().executing_action_id().empty())
                    return;
                opened = true;
                before = g_dialog_witness->shown().size();
                answer = MessageDialog(wxGetApp().mainframe, "Click check: the person's own question", "Click check", wxYES_NO).ShowModal();
            });
            click.Start(5);
            nlohmann::json import = self->session_arguments();
            import["path"] = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            const Agent::ToolActivity& done = self->run_dialog_tool("object_import_file", import);
            click.Stop();
            std::cerr << "HARNESS NOTE click_during_a_call opened=" << opened
                      << " shown=" << (opened && g_dialog_witness->shown().size() > before) << " answer=" << answer
                      << " inResult=" << (done.app_dialogs_json.find("Click check") != std::string::npos) << '\n';
        }, {}, {}});

        run_issue_steps();
    }
