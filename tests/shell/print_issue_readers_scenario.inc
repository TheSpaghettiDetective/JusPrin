    // The print-issue readers scenario (--print-issue-readers). Included inside
    // class Scenario, after print_issues_scenario.inc, whose helpers it uses.
    //
    // Each step produces one condition OrcaSlicer warns about that the lifecycle
    // scenario's one-nozzle printer cannot reach (two nozzles, mixed
    // materials, paths outside the plate, a failed slice that names its
    // object), through the same calls OrcaSlicer's own controls make. It
    // prints three things side by side: what the monitor reports, what
    // OrcaSlicer's notification manager holds (its own words for the same
    // fact), and whether OrcaSlicer itself refuses to slice or print.
    // JUSPRIN_READER_STEPS picks groups by name ("A4 C E"); empty runs all.

    static const char* notice_level_name(NotificationManager::NotificationLevel level)
    {
        using L = NotificationManager::NotificationLevel;
        switch (level) {
        case L::ErrorNotificationLevel:          return "error";
        case L::SeriousWarningNotificationLevel: return "serious";
        case L::WarningNotificationLevel:        return "warning";
        default:                                 return "info";
        }
    }

    struct NativeNotice { int type; std::string level, text; };
    std::vector<NativeNotice> native_notices()
    {
        std::vector<NativeNotice> found;
        m_plater->get_notification_manager()->for_each_notification(
            [&found](NotificationType type, NotificationManager::NotificationLevel level, const std::string& text) {
                found.push_back({int(type), notice_level_name(level), text});
            });
        return found;
    }
    const NativeNotice* native_notice(NotificationType type)
    {
        m_native = native_notices();
        for (const NativeNotice& notice : m_native)
            if (notice.type == int(type))
                return &notice;
        return nullptr;
    }

    static std::string one_line(std::string text)
    {
        std::replace(text.begin(), text.end(), '\n', '|');
        return text;
    }

    void dump_readers(const std::string& label)
    {
        PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
        const GCodeProcessorResult* result = plate->get_slice_result();
        const PrintActionState actions = installed_shell()->status_row()->action_state();
        std::cerr << "HARNESS READERS " << label << " can_slice=" << actions.can_slice << " can_print=" << actions.can_print
                  << " slice_valid=" << plate->is_slice_result_valid() << " ready_for_print=" << plate->is_slice_result_ready_for_print()
                  << " apply_invalid=" << plate->is_apply_result_invalid();
        if (plate->is_slice_result_valid() && result != nullptr)
            std::cerr << " toolpath_outside_flag=" << result->toolpath_outside << " check_error_code=" << result->gcode_check_result.error_code
                      << " plate_surface_conflicts=" << result->filament_printable_reuslt.conflict_filament.size()
                      << " processor_warnings=" << result->warnings.size();
        if (plate->is_slice_result_valid() && result != nullptr) {
            const BoundingBoxf3 paths = Workspace::extrusion_bounds(*result);
            const BoundingBoxf3 box   = plate->get_bounding_box();
            std::cerr << " paths_x=" << paths.min.x() << ".." << paths.max.x() << " paths_y=" << paths.min.y() << ".." << paths.max.y()
                      << " paths_top=" << paths.max.z() << " plate_x=" << box.min.x() << ".." << box.max.x()
                      << " all_paths_inside=" << m_plater->build_volume().all_paths_inside(*result, paths);
        }
        std::cerr << '\n';
        if (plate->is_slice_result_valid() && result != nullptr)
            for (const GCodeProcessorResult::SliceWarning& warning : result->warnings)
                std::cerr << "HARNESS PROCESSOR_WARNING level=" << warning.level << " code=" << warning.error_code << " msg='" << warning.msg << "'\n";
        for (const NativeNotice& notice : native_notices())
            std::cerr << "HARNESS NATIVE  type=" << notice.type << " level=" << notice.level << " text='" << one_line(notice.text) << "'\n";
        dump_issues(label);
    }

    const Issue* issue_coded(const std::string& code)
    {
        for (const Issue& issue : issues().issues)
            if (issue.code == code)
                return &issue;
        return nullptr;
    }

    // "Blocks printing" is OrcaSlicer's own answer: the monitor holds a
    // blocker exactly when OrcaSlicer refuses to slice, or refuses to print
    // the slice it has. For steps whose condition OrcaSlicer may or may not
    // raise, so the step checks something either way.
    bool blockers_match_orca()
    {
        PartPlate* plate   = m_plater->get_partplate_list().get_curr_plate();
        const bool refuses = !installed_shell()->status_row()->action_state().can_slice ||
                             (plate->is_slice_result_valid() && !plate->is_slice_result_ready_for_print());
        const bool blocks  = std::any_of(issues().issues.begin(), issues().issues.end(),
                                         [](const Issue& issue) { return issue.severity == PrintIssues::IssueSeverity::Blocker; });
        std::cerr << "HARNESS BLOCKERS orca_refuses=" << refuses << " monitor_blocks=" << blocks << '\n';
        return refuses == blocks;
    }

    // OrcaSlicer's notice text carries "Error:\n" or "Warning:\n" ahead of the
    // sentence; the monitor carries the sentence. True when the rest is equal.
    bool same_words(const NativeNotice* notice, const Issue* issue, const std::string& name)
    {
        if (notice == nullptr || issue == nullptr) {
            std::cerr << "HARNESS WORDS " << name << " UNVERIFIED native=" << (notice != nullptr) << " monitor=" << (issue != nullptr) << '\n';
            return false;
        }
        const std::size_t newline = notice->text.find('\n');
        const std::string native  = newline == std::string::npos ? notice->text : notice->text.substr(newline + 1);
        const bool        same    = native == issue->message;
        std::cerr << "HARNESS WORDS " << name << (same ? " SAME" : " DIFFERENT") << "\n   native : '" << one_line(native) << "'\n   monitor: '"
                  << one_line(issue->message) << "'\n";
        return same;
    }

    // One filament slot through the fork's own setup command, by the first
    // installed filament whose short name contains the words asked for.
    bool choose_filament(std::size_t slot, const std::string& words)
    {
        for (const SetupCommands::FilamentInfo& filament : SetupCommands::compatible_filaments())
            if (std::string(filament.alias.ToUTF8().data()).find(words) != std::string::npos) {
                const bool done = SetupCommands::select_filament_preset(*m_plater, slot, filament.preset_name);
                std::cerr << "HARNESS FILAMENT slot=" << slot + 1 << " preset='" << filament.preset_name << "' selected=" << done << '\n';
                return done;
            }
        std::cerr << "HARNESS FILAMENT slot=" << slot + 1 << " no installed filament matches '" << words << "'\n";
        return false;
    }

    // Switches filaments on in the list the "Add/Remove filaments" page edits,
    // then reloads the presets and keeps the printer that was selected.
    void enable_filaments(std::initializer_list<const char*> names)
    {
        AppConfig*        config  = wxGetApp().app_config;
        const std::string printer = SetupCommands::current_printer().preset_name;
        for (const char* name : names)
            config->set(AppConfig::SECTION_FILAMENTS, name, "true");
        wxGetApp().preset_bundle->load_presets(*config, ForwardCompatibilitySubstitutionRule::Enable,
                                               {std::string(), std::string(), std::string(), std::string()});
        const bool kept = SetupCommands::select_printer_preset(*m_plater, printer);
        if (wxGetApp().mainframe)
            wxGetApp().mainframe->update_side_preset_ui();
        std::cerr << "HARNESS FILAMENTS enabled, printer kept=" << kept << " now='" << SetupCommands::current_printer().preset_name
                  << "' installed_for_it=" << SetupCommands::compatible_filaments().size() << '\n';
    }

    // The object list's own "change filament" on one object.
    void give_filament(std::size_t object, int filament)
    {
        wxGetApp().obj_list()->select_items({{m_plater->model().objects[object], nullptr}});
        wxGetApp().obj_list()->set_extruder_for_selected_items(filament);
    }

    // What the nozzle-mapping dialog does when its OK is pressed.
    void map_filaments(FilamentMapMode mode, const std::vector<int>& nozzles)
    {
        PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
        plate->set_filament_map_mode(mode);
        if (mode == fmmManual)
            plate->set_filament_maps(nozzles);
        plate->update_slice_result_valid_state(false);
        m_plater->set_plater_dirty(true);
        m_plater->update();
        prepare_canvas().reload_scene(true);
    }

    void two_cubes(const std::string& label)
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, label + "_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        check(m_plater->load_files(std::vector<std::string>{cube},
                  LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1, label + "_cube_loaded");
        check(m_plater->duplicate_object(0) == 1, label + "_second_cube");
        PartPlateList& plates = m_plater->get_partplate_list();
        plates.get_plate(0)->add_instance(0, 0, true);
        plates.get_plate(0)->add_instance(1, 0, true);
        m_plater->canvas3D()->reload_scene(true, true);
        const BoundingBoxf3 box = plates.get_curr_plate()->get_bounding_box();
        place(0, box.center().x() - 40, box.center().y());
        place(1, box.center().x() + 40, box.center().y());
    }

    void place(std::size_t object, double x, double y)
    {
        const Vec3d at = issue_object(object)->instances[0]->get_offset();
        move_object(object, Vec3d(x - at.x(), y - at.y(), 0));
    }

    std::function<bool()> slice_ended()
    {
        return [this] {
            PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
            return !m_plater->is_background_process_slicing() && issues().checked &&
                   std::chrono::steady_clock::now() - m_slice_started > std::chrono::seconds(3) &&
                   (plate->is_slice_result_valid() || issue_from(PrintIssues::IssueSource::SliceFailure, PrintIssues::IssueSeverity::Blocker) != nullptr ||
                    !installed_shell()->status_row()->action_state().can_slice);
        };
    }
    void slice_now()
    {
        answer_modals(wxID_OK);
        m_slice_started = std::chrono::steady_clock::now();
        installed_shell()->status_row()->request_slice();
    }

    void begin_issue_readers()
    {
        using PrintIssues::IssueSeverity;
        using PrintIssues::IssueSource;
        auto  self  = shared_from_this();
        auto& steps = m_issue_steps;
        const std::string only = wxGetenv("JUSPRIN_READER_STEPS") ? std::string(wxString(wxGetenv("JUSPRIN_READER_STEPS")).ToUTF8().data()) : "";
        const auto wanted = [only](const std::string& group) { return only.empty() || only.find(group) != std::string::npos; };

        // ---- A. Single nozzle (the harness's stock X1 Carbon) ------------------------------
        if (wanted("A1")) {
        steps.push_back({"a1_brim_over_the_edge", [self] {
            self->two_cubes("a1");
            self->m_plater->remove(1);
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            // Inside the plate by 1 mm, with a 12 mm brim that is not.
            self->place(0, box.max.x() - 11, box.center().y());
            self->set_process_option("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
            self->set_process_option("brim_width", new ConfigOptionFloat(12));
        }, {}, [self] { self->dump_readers("a1 before slice"); }});
        steps.push_back({"a1_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->dump_readers("a1 sliced");
            const Issue* outside = self->issue_coded("toolpath_outside_plate");
            std::cerr << "HARNESS RESULT a1 toolpath_outside_reported=" << (outside != nullptr) << '\n';
            // OrcaSlicer trims a brim at the plate's edge, so this slice has
            // no path outside and prints (orca-integration-guide.md); a4 puts
            // a skirt there instead. Either way the monitor must agree.
            self->check(self->blockers_match_orca(), "a1_the_monitor_blocks_exactly_when_orca_refuses");
        }});
        }

        if (wanted("A4")) {
        steps.push_back({"a4_skirt_over_the_edge", [self] {
            self->two_cubes("a4");
            self->m_plater->remove(1);
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(0, box.max.x() - 11, box.center().y());
            self->set_process_option("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
            self->set_process_option("skirt_loops", new ConfigOptionInt(3));
            self->set_process_option("skirt_distance", new ConfigOptionFloat(6));
        }, {}, [self] { self->dump_readers("a4 before slice"); }});
        steps.push_back({"a4_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->dump_readers("a4 sliced");
            PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
            const Issue* outside = self->issue_coded("toolpath_outside_plate");
            std::cerr << "HARNESS RESULT a4 toolpath_outside_reported=" << (outside != nullptr) << " orca_ready_for_print="
                      << plate->is_slice_result_ready_for_print() << " can_print=" << installed_shell()->status_row()->action_state().can_print << '\n';
            self->check(outside != nullptr, "a4_toolpath_outside_reported");
            self->check(outside != nullptr && outside->severity == IssueSeverity::Blocker && !plate->is_slice_result_ready_for_print(),
                        "a4_toolpath_outside_is_a_blocker_orca_refuses_to_print");
            self->m_readers_sliced = plate->is_slice_result_valid();
        }});
        steps.push_back({"a4_orcas_own_words", [self] {
            // Cleared first, so an unsliced step compares nothing rather than
            // an earlier step's notices.
            self->m_native.clear();
            if (!self->m_readers_sliced)
                return;
            self->m_frame->CallAfter([self] {
                for (wxWindow* window : wxTopLevelWindows)
                    if (auto* dialog = dynamic_cast<wxDialog*>(window); dialog != nullptr && dialog->IsModal()) {
                        for (wxWindow* child : dialog->GetChildren())
                            if (auto* preview = dynamic_cast<Preview*>(child); preview != nullptr && preview->get_canvas3d() != nullptr)
                                preview->get_canvas3d()->render();
                        self->m_native = self->native_notices();
                        dialog->EndModal(wxID_CANCEL);
                    }
            });
            installed_shell()->status_row()->request_action(PrintAction::CheckPrint);
        }, {}, [self] {
            const NativeNotice* native = nullptr;
            for (const NativeNotice& notice : self->m_native) {
                std::cerr << "HARNESS NATIVE_IN_CHECK_PRINT type=" << notice.type << " level=" << notice.level << " text='" << one_line(notice.text) << "'\n";
                if (notice.type == int(NotificationType::SlicingError))
                    native = &notice;
            }
            self->check(self->same_words(native, self->issue_coded("toolpath_outside_plate"), "a4_toolpath_outside"),
                        "a4_toolpath_outside_in_orcas_words");
        }});
        }

        if (wanted("A5")) {
        // The same skirt just outside the edge, inside the 2 mm OrcaSlicer's
        // own post-slice check allows.
        steps.push_back({"a5_skirt_just_over_the_edge", [self] {
            self->two_cubes("a5");
            self->m_plater->remove(1);
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(0, box.max.x() - 11, box.center().y());
            self->set_process_option("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
            self->set_process_option("skirt_loops", new ConfigOptionInt(1));
            self->set_process_option("skirt_distance", new ConfigOptionFloat(2));
        }, {}, {}});
        steps.push_back({"a5_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->dump_readers("a5 sliced");
            PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
            const Issue* outside = self->issue_coded("toolpath_outside_plate");
            self->check(outside != nullptr && outside->severity == IssueSeverity::Warning && plate->is_slice_result_ready_for_print(),
                        "a5_a_path_orca_still_prints_is_a_warning_not_a_blocker");
            std::cerr << "HARNESS RESULT a5 monitor_blocker=" << (outside != nullptr && outside->severity == IssueSeverity::Blocker)
                      << " monitor_warning=" << (outside != nullptr && outside->severity == IssueSeverity::Warning)
                      << " orca_ready_for_print=" << plate->is_slice_result_ready_for_print()
                      << " can_print=" << installed_shell()->status_row()->action_state().can_print << '\n';
        }});
        }

        if (wanted("A6")) {
        steps.push_back({"a6_taller_than_the_printer", [self] {
            self->answer_modals(wxID_NO);
            self->two_cubes("a6");
            self->m_plater->remove(1);
            const double top = self->m_plater->build_volume().printable_height();
            const std::string path = (fs::path(data_dir()) / "issues-tall.stl").string();
            {
                std::ofstream out(path);
                out << "solid tall\n";
                write_box(out, {-6, -6, 0}, {6, 6, top + 2});
                out << "endsolid tall\n";
            }
            self->m_plater->load_files(std::vector<std::string>{path}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
            self->m_plater->get_partplate_list().get_plate(0)->add_instance(1, 0, true);
            self->m_plater->canvas3D()->reload_scene(true, true);
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(1, box.center().x() + 40, box.center().y());
            self->stop_answering_modals();
            std::cerr << "HARNESS A6 printable_height=" << top << '\n';
        }, {}, [self] {
            self->dump_readers("a6 before slice");
            const bool can_slice = installed_shell()->status_row()->action_state().can_slice;
            std::cerr << "HARNESS RESULT a6 orca_lets_it_slice=" << can_slice << '\n';
            // OrcaSlicer's placement check counts a body over the height
            // limit as outside the plate and will not slice it.
            const Issue* outside = self->issue_coded("partly_outside_plate");
            self->check(outside != nullptr && outside->severity == IssueSeverity::Blocker && !can_slice,
                        "a6_taller_than_the_printer_is_a_blocker_orca_will_not_slice");
            self->check(outside != nullptr && outside->object == self->issue_object(1)->id().id, "a6_the_blocker_names_the_tall_object");
        }});
        }

        if (wanted("A2")) {
        steps.push_back({"a2_plate_surface", [self] {
            self->two_cubes("a2");
            self->m_plater->remove(1);
            // First-layer bed temperature 0 on every plate type, other layers
            // left alone: validation passes, the sliced first layer does not.
            DynamicPrintConfig diff;
            for (const char* key : {"hot_plate_temp_initial_layer", "textured_plate_temp_initial_layer", "cool_plate_temp_initial_layer",
                                    "eng_plate_temp_initial_layer", "supertack_plate_temp_initial_layer", "textured_cool_plate_temp_initial_layer"})
                diff.set_key_value(key, new ConfigOptionInts{0});
            wxGetApp().get_tab(Preset::TYPE_FILAMENT)->load_config(diff);
        }, {}, [self] { self->dump_readers("a2 before slice"); }});
        steps.push_back({"a2_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->dump_readers("a2 sliced");
            const Issue* surface = self->issue_coded("filament_unprintable_on_plate");
            std::cerr << "HARNESS RESULT a2 plate_surface_reported=" << (surface != nullptr) << '\n';
            self->check(surface != nullptr, "a2_plate_surface_reported");
            if (surface != nullptr) {
                self->check(surface->severity == IssueSeverity::Blocker, "a2_plate_surface_is_a_blocker");
                self->check(!self->m_plater->get_partplate_list().get_curr_plate()->is_slice_result_ready_for_print(),
                            "a2_orca_itself_refuses_to_print");
            }
        }});
        }

        if (wanted("A3")) {
        steps.push_back({"a3_failure_names_its_object", [self] {
            self->answer_modals(wxID_NO);
            self->two_cubes("a3");
            self->m_plater->remove(1);
            // A body resting on a point too small to extrude: the first layer is empty.
            const std::string path = (fs::path(data_dir()) / "issues-point.stl").string();
            {
                std::ofstream out(path);
                out << "solid point\n";
                write_box(out, {-0.05, -0.05, 0}, {0.05, 0.05, 0.5});
                write_box(out, {-10, -10, 0.5}, {10, 10, 6});
                out << "endsolid point\n";
            }
            self->check(self->m_plater->load_files(std::vector<std::string>{path},
                            LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1, "a3_point_loaded");
            self->m_plater->get_partplate_list().get_plate(0)->add_instance(1, 0, true);
            self->m_plater->canvas3D()->reload_scene(true, true);
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(1, box.center().x() + 40, box.center().y());
            self->stop_answering_modals();
        }, {}, [self] { self->dump_readers("a3 before slice"); }});
        steps.push_back({"a3_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->dump_readers("a3 sliced");
            const Issue* failure = self->issue_from(IssueSource::SliceFailure, IssueSeverity::Blocker);
            std::cerr << "HARNESS RESULT a3 failure=" << (failure != nullptr) << " object=" << (failure ? failure->object : 0)
                      << " expected_object=" << (self->m_plater->model().objects.size() > 1 ? self->issue_object(1)->id().id : 0) << '\n';
            self->check(failure != nullptr, "a3_slice_failure_reported");
            self->check(failure != nullptr && self->m_plater->model().objects.size() > 1 && failure->object == self->issue_object(1)->id().id,
                        "a3_the_failure_names_the_object_orca_named");
        }});
        }

        // ---- B. Nozzle and filament (X1 Carbon, 0.6 nozzle) -------------------------------
        if (wanted("B1")) {
        steps.push_back({"b1_nozzle_and_filament", [self] {
            std::string error;
            const bool installed = SetupCommands::install_and_select_printer(*self->m_plater, "BBL", "Bambu Lab X1 Carbon", "0.6", {}, error);
            std::cerr << "HARNESS PRINTER x1c_06 installed=" << installed << " error='" << error << "' preset='"
                      << SetupCommands::current_printer().preset_name << "'\n";
            self->enable_filaments({"Bambu PLA Silk @BBL X1C"});
            self->two_cubes("b1");
            self->m_silk_chosen = self->choose_filament(0, "PLA Silk");
        }, {}, [self] {
            self->dump_readers("b1");
            const Issue* issue = self->issue_coded("BBLNozzleFilamentIncompatible");
            std::cerr << "HARNESS RESULT b1 silk_selectable=" << self->m_silk_chosen << " reported=" << (issue != nullptr) << '\n';
            self->check(self->m_silk_chosen, "b1_pla_silk_selected");
            self->check(issue != nullptr, "b1_nozzle_and_filament_reported");
            if (issue != nullptr) {
                self->check(issue->severity == IssueSeverity::Warning, "b1_nozzle_and_filament_is_a_warning");
                self->check(installed_shell()->status_row()->action_state().can_slice, "b1_orca_still_slices");
                self->check(self->same_words(self->native_notice(NotificationType::BBLNozzleFilamentIncompatible), issue, "b1_nozzle_filament"),
                            "b1_nozzle_filament_in_orcas_words");
            }
        }});
        }

        if (wanted("B1")) {
        steps.push_back({"b2_pla_with_petg_one_nozzle", [self] {
            // The X1 Carbon's 0.6 nozzle takes PETG Basic from the preset
            // named for the 0.8 nozzle; "@BBL X1C" alone is 0.4 only.
            self->enable_filaments({"Bambu PETG Basic @BBL X1C 0.8 nozzle", "Bambu PLA Basic @BBL X1C"});
            self->two_cubes("b2");
            if (SetupCommands::filament_slots().size() < 2)
                SetupCommands::add_filament_slot(*self->m_plater);
            self->check(self->choose_filament(0, "PLA Basic") && self->choose_filament(1, "PETG Basic"), "b2_pla_and_petg_selected");
            self->give_filament(1, 2);
        }, {}, [self] {
            self->dump_readers("b2");
            std::cerr << "HARNESS RESULT b2 before_any_reload reported=" << (self->issue_coded("BBLMixUsePLAAndPETG") != nullptr)
                      << " orca_notice=" << (self->native_notice(NotificationType::BBLMixUsePLAAndPETG) != nullptr) << '\n';
            self->prepare_canvas().reload_scene(true);
            self->issue_monitor().refresh_now();
            const Issue* issue = self->issue_coded("BBLMixUsePLAAndPETG");
            std::cerr << "HARNESS RESULT b2 after_reload reported=" << (issue != nullptr)
                      << " orca_notice=" << (self->native_notice(NotificationType::BBLMixUsePLAAndPETG) != nullptr) << '\n';
            self->check(issue != nullptr, "b2_pla_with_petg_reported");
            if (issue != nullptr) {
                self->check(issue->severity == IssueSeverity::Warning && issue->object == 0, "b2_pla_with_petg_is_a_plate_wide_warning");
                // As in b3, slicing is refused by a separate validation blocker
                // (the two filaments' nozzle temperatures), not by this warning.
                self->check(self->same_words(self->native_notice(NotificationType::BBLMixUsePLAAndPETG), issue, "b2_pla_petg"),
                            "b2_pla_petg_in_orcas_words");
            }
        }});
        }

        if (wanted("B3")) {
        // One nozzle that both materials pass through (X1 Carbon, 0.4 nozzle).
        steps.push_back({"b3_pla_with_petg_one_nozzle", [self] {
            std::string error;
            SetupCommands::install_and_select_printer(*self->m_plater, "BBL", "Bambu Lab X1 Carbon", "0.4", {}, error);
            self->enable_filaments({"Bambu PETG Basic @BBL X1C", "Bambu PLA Basic @BBL X1C"});
            self->two_cubes("b3");
            if (SetupCommands::filament_slots().size() < 2)
                SetupCommands::add_filament_slot(*self->m_plater);
            self->choose_filament(0, "PLA Basic");
            self->choose_filament(1, "PETG Basic");
            self->give_filament(1, 2);
        }, {}, [self] {
            self->dump_readers("b3");
            const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
            PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
            std::cerr << "HARNESS B3DIAG filament_types=";
            for (const std::string& type : config.option<ConfigOptionStrings>("filament_type")->values) std::cerr << type << ',';
            std::cerr << " used_filaments=";
            for (const int filament : plate->get_extruders(true)) std::cerr << filament << ',';
            std::cerr << " tool_change_time=" << config.opt_float("machine_tool_change_time")
                      << " orca_check_passes=" << plate->check_mixture_of_pla_and_petg(config) << '\n';
            const Issue* before = self->issue_coded("BBLMixUsePLAAndPETG");
            std::cerr << "HARNESS RESULT b3 before_any_reload reported=" << (before != nullptr)
                      << " orca_notice=" << (self->native_notice(NotificationType::BBLMixUsePLAAndPETG) != nullptr) << '\n';
            self->prepare_canvas().reload_scene(true);
            self->issue_monitor().refresh_now();
            const Issue* issue = self->issue_coded("BBLMixUsePLAAndPETG");
            std::cerr << "HARNESS RESULT b3 after_reload reported=" << (issue != nullptr)
                      << " orca_notice=" << (self->native_notice(NotificationType::BBLMixUsePLAAndPETG) != nullptr) << '\n';
            self->check(issue != nullptr, "b3_pla_with_petg_reported");
            if (issue != nullptr) {
                self->check(issue->severity == IssueSeverity::Warning && issue->object == 0, "b3_pla_with_petg_is_a_plate_wide_warning");
                // Slicing is refused here by a separate validation blocker (the two
                // filaments' nozzle temperatures), not by this warning.
                self->check(self->same_words(self->native_notice(NotificationType::BBLMixUsePLAAndPETG), issue, "b3_pla_petg"),
                            "b3_pla_petg_in_orcas_words");
            }
        }});
        }

        // ---- C. Two nozzles (Bambu Lab H2D) ---------------------------------------------
        if (wanted("C")) {
        steps.push_back({"c0_two_nozzle_printer", [self] {
            std::string error;
            const bool installed = SetupCommands::install_and_select_printer(*self->m_plater, "BBL", "Bambu Lab H2D", "0.4", {}, error);
            const SetupCommands::PrinterInfo printer = SetupCommands::current_printer();
            std::cerr << "HARNESS PRINTER h2d installed=" << installed << " error='" << error << "' preset='" << printer.preset_name
                      << "' nozzles=" << printer.extruder_count << '\n';
            self->check(installed && printer.extruder_count == 2, "c0_two_nozzle_printer_selected");
            self->enable_filaments({"Bambu PETG Basic @BBL H2D 0.4 nozzle", "Bambu PVA @BBL H2D", "Bambu TPU 95A HF @BBL H2D"});
            self->two_cubes("c0");
            if (SetupCommands::filament_slots().size() < 2)
                SetupCommands::add_filament_slot(*self->m_plater);
            self->check(SetupCommands::filament_slots().size() >= 2, "c0_two_filament_slots");
        }, {}, [self] {
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            std::cerr << "HARNESS PLATE x=" << box.min.x() << ".." << box.max.x() << " y=" << box.min.y() << ".." << box.max.y() << '\n';
            self->dump_readers("c0");
        }});

        steps.push_back({"c1_pla_with_petg", [self] {
            self->choose_filament(0, "PLA Basic");
            self->choose_filament(1, "PETG");
            self->give_filament(1, 2);
        }, {}, [self] {
            self->dump_readers("c1");
            std::cerr << "HARNESS RESULT c1 before_any_reload reported=" << (self->issue_coded("BBLMixUsePLAAndPETG") != nullptr)
                      << " orca_notice=" << (self->native_notice(NotificationType::BBLMixUsePLAAndPETG) != nullptr) << '\n';
            self->prepare_canvas().reload_scene(true);
            self->issue_monitor().refresh_now();
            self->dump_readers("c1 after a scene reload and a forced recompute");
            bool orca_check_passes = true;
            {
                const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
                PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
                orca_check_passes = plate->check_mixture_of_pla_and_petg(config);
                std::cerr << "HARNESS C1DIAG filament_types=";
                for (const std::string& type : config.option<ConfigOptionStrings>("filament_type")->values) std::cerr << type << ',';
                std::cerr << " used_filaments=";
                for (const int filament : plate->get_extruders(true)) std::cerr << filament << ',';
                std::cerr << " tool_change_time=" << config.opt_float("machine_tool_change_time")
                          << " orca_check_passes=" << orca_check_passes << '\n';
            }
            const Issue* issue = self->issue_coded("BBLMixUsePLAAndPETG");
            std::cerr << "HARNESS RESULT c1 reported=" << (issue != nullptr) << '\n';
            // OrcaSlicer skips this warning on a printer with a tool change
            // time, which the H2D has (orca-integration-guide.md), so the
            // monitor is checked against OrcaSlicer's own answer, not for a
            // warning.
            self->check((issue != nullptr) == !orca_check_passes, "c1_reported_exactly_when_orcas_own_check_fails");
            if (issue != nullptr) {
                self->check(issue->severity == IssueSeverity::Warning && issue->object == 0, "c1_pla_with_petg_is_a_plate_wide_warning");
                self->check(installed_shell()->status_row()->action_state().can_slice, "c1_orca_still_slices");
                self->check(self->same_words(self->native_notice(NotificationType::BBLMixUsePLAAndPETG), issue, "c1_pla_petg"),
                            "c1_pla_petg_in_orcas_words");
            }
        }});

        steps.push_back({"c2_pva_with_petg", [self] { self->m_pva_chosen = self->choose_filament(0, "PVA"); }, {}, [self] {
            self->dump_readers("c2");
            const Issue* issue = self->issue_coded("PlaterWarning");
            std::cerr << "HARNESS RESULT c2 pva_selectable=" << self->m_pva_chosen << " reported=" << (issue != nullptr) << '\n';
            self->check(self->m_pva_chosen, "c2_pva_selected");
            self->check(issue != nullptr, "c2_mixture_reported");
            if (issue != nullptr) {
                self->check(issue->severity == IssueSeverity::Warning, "c2_mixture_is_a_warning");
                self->check(self->same_words(self->native_notice(NotificationType::PlaterWarning), issue, "c2_mixture"), "c2_mixture_in_orcas_words");
            }
        }});

        steps.push_back({"c3_tpu_on_the_wrong_nozzle", [self] {
            self->choose_filament(0, "PLA Basic");
            self->m_tpu_chosen = self->choose_filament(1, "TPU 95A HF");
            self->map_filaments(fmmManual, {1, 1}); // both on the left nozzle
        }, {}, [self] {
            self->dump_readers("c3");
            const Issue* issue = self->issue_coded("BBLFilamentPrintableError");
            std::cerr << "HARNESS RESULT c3 tpu_selectable=" << self->m_tpu_chosen << " reported=" << (issue != nullptr) << '\n';
            self->check(self->m_tpu_chosen, "c3_tpu_selected");
            self->check(issue != nullptr, "c3_filament_not_printable_reported");
            if (issue != nullptr) {
                self->check(issue->severity == IssueSeverity::Blocker, "c3_filament_not_printable_is_a_blocker");
                self->check(!installed_shell()->status_row()->action_state().can_slice, "c3_orca_itself_refuses_to_slice");
                self->check(self->same_words(self->native_notice(NotificationType::BBLFilamentPrintableError), issue, "c3_filament_not_printable"),
                            "c3_filament_not_printable_in_orcas_words");
            }
            std::cerr << "HARNESS RESULT c3 two_tpu_reader=" << (self->issue_coded("SlicingError") != nullptr) << '\n';
        }});

        steps.push_back({"c3b_two_tpu_filaments", [self] {
            self->choose_filament(0, "TPU 95A HF");
            self->map_filaments(fmmManual, {2, 2});
        }, {}, [self] {
            self->dump_readers("c3b");
            std::cerr << "HARNESS RESULT c3b two_tpu_filaments_used reported=" << (self->issue_coded("SlicingError") != nullptr)
                      << " can_slice=" << installed_shell()->status_row()->action_state().can_slice << '\n';
            // OrcaSlicer does not limit how many TPU filaments a plate uses
            // (check_tpu_printable_status returns true), and c3's blocker
            // must have gone with the TPU off the left nozzle.
            self->check(self->blockers_match_orca(), "c3b_the_monitor_blocks_exactly_when_orca_refuses");
        }});

        steps.push_back({"c4_out_of_a_nozzles_reach", [self] {
            self->choose_filament(0, "PLA Basic");
            self->choose_filament(1, "PLA Basic");
            self->map_filaments(fmmManual, {1, 2}); // filament 2 on the right nozzle
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            // The right nozzle starts 25 mm in from the left edge.
            self->place(1, box.min.x() + 12, box.center().y());
        }, {}, [self] {
            self->dump_readers("c4");
            const Issue* issue = self->issue_coded("right_nozzle_unprintable");
            std::cerr << "HARNESS RESULT c4 reported=" << (issue != nullptr) << '\n';
            self->check(issue != nullptr, "c4_out_of_reach_reported");
            if (issue != nullptr) {
                self->check(issue->severity == IssueSeverity::Blocker, "c4_out_of_reach_is_a_blocker");
                self->check(!installed_shell()->status_row()->action_state().can_slice, "c4_orca_itself_refuses_to_slice");
                self->check(self->same_words(self->native_notice(NotificationType::RightExtruderUnprintableError), issue, "c4_right_nozzle"),
                            "c4_right_nozzle_in_orcas_words");
                std::cerr << "HARNESS RESULT c4 target_object=" << issue->object << " cube=" << self->issue_object(1)->id().id << '\n';
                self->check(issue->object == self->issue_object(1)->id().id, "c4_out_of_reach_names_the_object_orca_named");
            }
        }});

        steps.push_back({"c5_flushing_volume_zero", [self] {
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(1, box.center().x() + 40, box.center().y());
            self->map_filaments(fmmManual, {1, 1});
            // What the flushing-volumes dialog writes when its values are
            // accepted, with every value zero.
            auto& project = wxGetApp().preset_bundle->project_config;
            auto* matrix  = project.option<ConfigOptionFloats>("flush_volumes_matrix");
            std::fill(matrix->values.begin(), matrix->values.end(), 0.);
            wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
            self->m_plater->update_project_dirty_from_presets();
            self->prepare_canvas().reload_scene(true);
        }, {}, [self] {
            self->dump_readers("c5");
            const NativeNotice* notice = self->native_notice(NotificationType::BBLFlushingVolumeZero);
            const Issue* flushing  = self->issue_coded("BBLFlushingVolumeZero");
            const bool   monitored = flushing != nullptr;
            self->check(notice != nullptr && flushing != nullptr && flushing->severity == IssueSeverity::Warning && flushing->object == 0,
                        "c5_flushing_volume_zero_is_a_plate_wide_warning");
            self->check(self->same_words(notice, flushing, "c5_flushing"), "c5_flushing_in_orcas_words");
            std::cerr << "HARNESS RESULT c5 orca_holds_flushing_notice=" << (notice != nullptr) << " monitor_reports_it=" << monitored
                      << " can_slice=" << installed_shell()->status_row()->action_state().can_slice << '\n';
        }});
        }

        if (wanted("D")) {
        // ---- D. Two nozzles, after slicing ------------------------------------------------
        steps.push_back({"d0_two_nozzle_printer", [self] {
            std::string error;
            SetupCommands::install_and_select_printer(*self->m_plater, "BBL", "Bambu Lab H2D", "0.4", {}, error);
            self->two_cubes("d0");
            if (SetupCommands::filament_slots().size() < 2)
                SetupCommands::add_filament_slot(*self->m_plater);
            self->choose_filament(0, "PLA Basic");
            self->choose_filament(1, "PLA Basic");
            self->give_filament(1, 2);
            self->map_filaments(fmmManual, {1, 2});
            // Filament 2 is on the right nozzle, which starts 25 mm in. The cube
            // is inside its reach by 1 mm; its 12 mm brim is not.
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(1, box.min.x() + 36, box.center().y());
            self->set_process_option("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
            self->set_process_option("brim_width", new ConfigOptionFloat(12));
        }, {}, [self] { self->dump_readers("d0 before slice"); }});
        steps.push_back({"d1_slice_brim_out_of_reach", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->dump_readers("d1 sliced");
            PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
            const Issue* area = self->issue_coded("nozzle_printable_area");
            std::cerr << "HARNESS RESULT d1 nozzle_area_reported=" << (area != nullptr)
                      << " prime_tower_reported=" << (self->issue_coded("PlaterWarning") != nullptr) << '\n';
            // OrcaSlicer trims a brim at a nozzle's reach as it does at the
            // plate's edge (orca-integration-guide.md), so this slice prints
            // and d2 finds no words to compare. Either way the monitor must
            // agree with OrcaSlicer.
            self->check(self->blockers_match_orca(), "d1_the_monitor_blocks_exactly_when_orca_refuses");
            self->m_readers_sliced = plate->is_slice_result_valid();
        }});
        // Check print loads the slice into OrcaSlicer's preview, which is where
        // OrcaSlicer itself words these: its notices are then in the manager.
        steps.push_back({"d2_orcas_own_words_after_slicing", [self] {
            self->m_native.clear();
            if (!self->m_readers_sliced)
                return;
            self->m_frame->CallAfter([self] {
                for (wxWindow* window : wxTopLevelWindows)
                    if (auto* dialog = dynamic_cast<wxDialog*>(window); dialog != nullptr && dialog->IsModal()) {
                        for (wxWindow* child : dialog->GetChildren())
                            if (auto* preview = dynamic_cast<Preview*>(child); preview != nullptr && preview->get_canvas3d() != nullptr)
                                preview->get_canvas3d()->render();
                        self->m_native = self->native_notices();
                        dialog->EndModal(wxID_CANCEL);
                    }
            });
            installed_shell()->status_row()->request_action(PrintAction::CheckPrint);
        }, {}, [self] {
            for (const NativeNotice& notice : self->m_native)
                std::cerr << "HARNESS NATIVE_IN_CHECK_PRINT type=" << notice.type << " level=" << notice.level << " text='" << one_line(notice.text) << "'\n";
            const NativeNotice* native = nullptr;
            for (const NativeNotice& notice : self->m_native)
                if (notice.type == int(NotificationType::BBLSliceLimitError))
                    native = &notice;
            self->same_words(native, self->issue_coded("nozzle_printable_area"), "d2_nozzle_area");
        }});

        steps.push_back({"d3_prime_tower_off_the_plate", [self] {
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(1, box.center().x() + 40, box.center().y());
            self->set_process_option("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
            // Where the canvas's own move handler records the tower's place.
            DynamicConfig& project = wxGetApp().preset_bundle->project_config;
            ConfigOptionFloat x(box.size().x() - 8), y(box.size().y() / 2);
            project.option<ConfigOptionFloats>("wipe_tower_x", true)->set_at(&x, 0, 0);
            project.option<ConfigOptionFloats>("wipe_tower_y", true)->set_at(&y, 0, 0);
            self->prepare_canvas().reload_scene(true);
            self->m_plater->update();
        }, {}, [self] { self->dump_readers("d3 before slice"); }});
        steps.push_back({"d4_slice_with_the_tower_there", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->prepare_canvas().reload_scene(true);
            self->issue_monitor().refresh_now();
            self->dump_readers("d4 sliced");
            const Issue* tower = self->issue_coded("PlaterWarning");
            std::cerr << "HARNESS RESULT d4 prime_tower_reported=" << (tower != nullptr) << '\n';
            self->check(tower != nullptr, "d4_prime_tower_reported");
            if (tower != nullptr)
                self->check(self->same_words(self->native_notice(NotificationType::PlaterWarning), tower, "d4_prime_tower"),
                            "d4_prime_tower_in_orcas_words");
            self->m_readers_sliced = self->m_plater->get_partplate_list().get_curr_plate()->is_slice_result_valid();
        }});
        steps.push_back({"d4b_orcas_own_words_for_that_slice", [self] {
            self->m_native.clear();
            if (!self->m_readers_sliced)
                return;
            self->m_frame->CallAfter([self] {
                for (wxWindow* window : wxTopLevelWindows)
                    if (auto* dialog = dynamic_cast<wxDialog*>(window); dialog != nullptr && dialog->IsModal()) {
                        for (wxWindow* child : dialog->GetChildren())
                            if (auto* preview = dynamic_cast<Preview*>(child); preview != nullptr && preview->get_canvas3d() != nullptr)
                                preview->get_canvas3d()->render();
                        self->m_native = self->native_notices();
                        dialog->EndModal(wxID_CANCEL);
                    }
            });
            installed_shell()->status_row()->request_action(PrintAction::CheckPrint);
        }, {}, [self] {
            const NativeNotice *area = nullptr, *outside = nullptr;
            for (const NativeNotice& notice : self->m_native) {
                std::cerr << "HARNESS NATIVE_IN_CHECK_PRINT type=" << notice.type << " level=" << notice.level << " text='" << one_line(notice.text) << "'\n";
                if (notice.type == int(NotificationType::BBLSliceLimitError)) area = &notice;
                if (notice.type == int(NotificationType::SlicingError)) outside = &notice;
            }
            self->check(area != nullptr && self->issue_coded("nozzle_printable_area") != nullptr, "d4b_nozzle_area_reported_by_both");
            // The words are printed, not checked: the monitor lists each
            // filament once ("Filaments 1 are placed in the left nozzle"),
            // where OrcaSlicer repeats one per object ("Filaments 1, 1 are").
            self->same_words(area, self->issue_coded("nozzle_printable_area"), "d4b_nozzle_area");
            self->check(self->same_words(outside, self->issue_coded("toolpath_outside_plate"), "d4b_toolpath_outside"),
                        "d4b_toolpath_outside_in_orcas_words");
        }});

        steps.push_back({"d5_taller_than_the_left_nozzle_reaches", [self] {
            self->answer_modals(wxID_NO);
            self->two_cubes("d5");
            self->m_plater->remove(1);
            // The left nozzle prints to 320 mm, the printer to 325 mm.
            const std::string path = (fs::path(data_dir()) / "issues-column.stl").string();
            {
                std::ofstream out(path);
                out << "solid column\n";
                write_box(out, {-6, -6, 0}, {6, 6, 322});
                out << "endsolid column\n";
            }
            self->check(self->m_plater->load_files(std::vector<std::string>{path},
                            LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1, "d5_column_loaded");
            self->m_plater->get_partplate_list().get_plate(0)->add_instance(1, 0, true);
            self->m_plater->canvas3D()->reload_scene(true, true);
            const BoundingBoxf3 box = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box();
            self->place(1, box.center().x() + 40, box.center().y());
            self->set_process_option("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
            self->map_filaments(fmmManual, {1, 2});
            self->stop_answering_modals();
        }, {}, [self] { self->dump_readers("d5 before slice"); }});
        steps.push_back({"d6_slice_the_column", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->dump_readers("d6 sliced");
            const Issue* height = self->issue_coded("nozzle_printable_height");
            std::cerr << "HARNESS RESULT d6 nozzle_height_reported=" << (height != nullptr)
                      << " toolpath_above_reported=" << (self->issue_coded("toolpath_above_print_height") != nullptr) << '\n';
            // OrcaSlicer's placement check already refuses a body taller than
            // the left nozzle's reach (left_nozzle_unprintable), so the slice
            // does not run and d7 finds no words to compare. Either way the
            // monitor must agree with OrcaSlicer.
            self->check(self->blockers_match_orca(), "d6_the_monitor_blocks_exactly_when_orca_refuses");
            self->m_readers_sliced = self->m_plater->get_partplate_list().get_curr_plate()->is_slice_result_valid();
        }});
        steps.push_back({"d7_orcas_own_words_for_the_column", [self] {
            self->m_native.clear();
            if (!self->m_readers_sliced)
                return;
            self->m_frame->CallAfter([self] {
                for (wxWindow* window : wxTopLevelWindows)
                    if (auto* dialog = dynamic_cast<wxDialog*>(window); dialog != nullptr && dialog->IsModal()) {
                        for (wxWindow* child : dialog->GetChildren())
                            if (auto* preview = dynamic_cast<Preview*>(child); preview != nullptr && preview->get_canvas3d() != nullptr)
                                preview->get_canvas3d()->render();
                        self->m_native = self->native_notices();
                        dialog->EndModal(wxID_CANCEL);
                    }
            });
            installed_shell()->status_row()->request_action(PrintAction::CheckPrint);
        }, {}, [self] {
            const NativeNotice* native = nullptr;
            for (const NativeNotice& notice : self->m_native) {
                std::cerr << "HARNESS NATIVE_IN_CHECK_PRINT type=" << notice.type << " level=" << notice.level << " text='" << one_line(notice.text) << "'\n";
                if (notice.type == int(NotificationType::BBLSliceMultiExtruderHeightOutside))
                    native = &notice;
            }
            self->same_words(native, self->issue_coded("nozzle_printable_height"), "d7_nozzle_height");
        }});
        }

        // ---- E. What suppressing the draw does to the notices themselves ------------------
        if (wanted("E")) {
        steps.push_back({"e1_a_result_is_confirmed_on_the_canvas", [self] {
            self->two_cubes("e1");
            self->m_plater->get_notification_manager()->push_exporting_finished_notification("/tmp/example/plate_1.gcode.3mf", "/tmp/example", false);
        }, {}, [self] {
            const NativeNotice* notice = self->native_notice(NotificationType::ExportFinished);
            const int           orca_draws = self->drawn_notices(self->prepare_canvas());
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            bool held = false, drawn = false;
            for (const PrintIssues::Notice& confirmation : self->issues().notices)
                held |= confirmation.kind == int(NotificationType::ExportFinished) && notice != nullptr && confirmation.text == notice->text;
            for (const auto& confirmation : frame.notices)
                drawn |= confirmation.kind == int(NotificationType::ExportFinished) && !confirmation.rect.empty();
            std::cerr << "HARNESS RESULT e1 orca_holds=" << (notice != nullptr) << " orca_draws=" << orca_draws << " jusprin_holds=" << held
                      << " jusprin_draws=" << drawn << " in_issue_list=" << self->issues().issues.size() << " text='"
                      << (notice ? one_line(notice->text) : "") << "'\n";
            self->check(notice != nullptr && orca_draws == 0, "e1_orca_holds_the_result_and_draws_nothing");
            self->check(held && drawn, "e1_jusprin_draws_the_result_orca_holds");
            self->check(self->issue_coded("ExportFinished") == nullptr, "e1_a_result_is_not_a_print_issue");
            self->capture_issues("e1-export-finished");
        }});
        steps.push_back({"e1b_closing_the_confirmation_closes_orcas_notice", [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            for (const auto& confirmation : frame.notices)
                if (confirmation.kind == int(NotificationType::ExportFinished))
                    self->click_overlay(confirmation.close);
        }, {}, [self] {
            self->issue_monitor().refresh_now();
            bool held = false;
            for (const PrintIssues::Notice& confirmation : self->issues().notices)
                held |= confirmation.kind == int(NotificationType::ExportFinished);
            std::cerr << "HARNESS RESULT e1b orca_holds=" << (self->native_notice(NotificationType::ExportFinished) != nullptr)
                      << " jusprin_holds=" << held << '\n';
            self->check(!held && self->native_notice(NotificationType::ExportFinished) == nullptr, "e1b_the_close_button_closes_orcas_own_notice");
        }});
        // A plate warning closed the way opening a project closes it is only
        // hidden; the manager deletes it at the end of its draw. With the draw
        // skipped it stays, and OrcaSlicer will not push the same words again.
        steps.push_back({"e2_plate_warning_closed_while_suppressed", [self] {
            NotificationManager& notices = *self->m_plater->get_notification_manager();
            GLCanvas3D&          canvas  = self->prepare_canvas();
            const auto run = [&](bool suppressed, const std::string& first, const std::string& second) {
                canvas.set_legacy_overlays_hidden(suppressed);
                notices.push_plater_warning_notification(first);
                canvas.render();
                notices.close_notification_of_type(NotificationType::PlaterWarning); // project open, first time
                canvas.render();
                notices.push_plater_warning_notification(second);
                canvas.render();
                notices.close_notification_of_type(NotificationType::PlaterWarning); // project open, second time
                canvas.render();
                // Back in the stock presentation, the first warning is true again.
                canvas.set_legacy_overlays_hidden(false);
                canvas.render();
                notices.push_plater_warning_notification(first);
                const int with_warning = self->drawn_notices(canvas);
                int held = 0;
                for (const NativeNotice& notice : self->native_notices())
                    held += notice.type == int(NotificationType::PlaterWarning) ? 1 : 0;
                notices.remove_notification_of_type(NotificationType::PlaterWarning);
                notices.remove_notification_of_type(NotificationType::PlaterWarning);
                notices.remove_notification_of_type(NotificationType::PlaterWarning);
                // Whatever else OrcaSlicer draws here is not the warning.
                const int drawn = with_warning - self->drawn_notices(canvas);
                std::cerr << "HARNESS RESULT e2 suppressed_before=" << suppressed << " first_warning_drawn_again=" << drawn
                          << " plate_warnings_still_held=" << held << '\n';
                return drawn;
            };
            self->m_drawn_control    = run(false, "Control warning one.", "Control warning two.");
            self->m_drawn_suppressed = run(true, "Suppressed warning one.", "Suppressed warning two.");
            canvas.set_legacy_overlays_hidden(true);
        }, {}, [self] {
            std::cerr << "HARNESS RESULT e2 control_drawn=" << self->m_drawn_control << " after_suppression_drawn=" << self->m_drawn_suppressed << '\n';
            // The fork skips only the drawing loop, so the end of
            // render_notifications still deletes the closed warning and the
            // same words come back as they do without suppression.
            self->check(self->m_drawn_control > 0 && self->m_drawn_suppressed == self->m_drawn_control,
                        "e2_a_warning_closed_while_suppressed_comes_back_like_the_control");
        }});
        }

        // ---- F. Work that runs outside the chat is kept in the chat --------------------
        if (wanted("F")) {
        steps.push_back({"f1_a_slice_is_an_entry_in_the_chat", [self] {
            std::string error;
            SetupCommands::install_and_select_printer(*self->m_plater, "BBL", "Bambu Lab X1 Carbon", "0.4", {}, error);
            // An earlier group may have left a plate type this filament cannot
            // print on, which OrcaSlicer refuses to slice.
            for (const SetupCommands::BedTypeChoice& bed : SetupCommands::bed_types())
                if (std::string(bed.label.ToUTF8().data()).find("Textured PEI") != std::string::npos)
                    SetupCommands::select_bed_type(*self->m_plater, bed.value);
            self->two_cubes("f1");
            self->m_work_before = self->persistence().document().work().size();
            self->m_last_item   = self->persistence().document().last_item_id(self->persistence().document().active_conversation_id());
            installed_shell()->set_agent_pane_collapsed(true);
            self->slice_now();
        }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            const Agent::WorkEntry* slice = self->dump_work("f1");
            self->check(slice != nullptr && slice->kind == "slice" && slice->state == "done", "f1_the_slice_is_saved_as_done");
            if (slice != nullptr) {
                self->check(slice->after_id == self->m_last_item, "f1_the_entry_keeps_the_place_it_started_at");
                self->check(!slice->ended_at.empty(), "f1_the_entry_says_when_it_ended");
            }
            self->check(!installed_shell()->agent_pane_collapsed(), "f1_the_chat_panel_opened_when_the_work_started");
        }});
        // A setting that makes the next slice a new one; OrcaSlicer applies it
        // before the slice is asked for.
        steps.push_back({"f2_change_a_setting", [self] {
            self->set_process_option("sparse_infill_density", new ConfigOptionPercent(100));
        }, {}, {}});
        steps.push_back({"f2_a_cancelled_slice_says_so", [self] {
            self->m_work_before = self->persistence().document().work().size();
            self->m_slice_started = std::chrono::steady_clock::now();
            installed_shell()->status_row()->request_slice();
        }, [self] {
            // Stop it through the entry's own Cancel as soon as the entry exists.
            const auto work = self->persistence().document().work();
            if (work.size() > self->m_work_before && work.back().state == "running" && !self->m_cancel_sent) {
                self->m_cancel_sent = true;
                installed_shell()->work_feed()->cancel(work.back().id);
            }
            // A slice OrcaSlicer refused never starts; the check below then says so.
            if (std::chrono::steady_clock::now() - self->m_slice_started > std::chrono::seconds(30))
                return true;
            return work.size() > self->m_work_before && work.back().state != "running" && !self->m_plater->is_background_process_slicing();
        }, [self] {
            const Agent::WorkEntry* slice = self->dump_work("f2");
            std::cerr << "HARNESS RESULT f2 cancel_sent=" << self->m_cancel_sent << " state=" << (slice ? slice->state : "") << '\n';
            self->check(self->m_cancel_sent && slice != nullptr && slice->state == "cancelled", "f2_the_entrys_cancel_stops_the_slice_and_the_entry_says_cancelled");
        }});
        steps.push_back({"f3_a_failed_slice_says_why", [self] {
            self->answer_modals(wxID_NO);
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "f3_new_project");
            const std::string path = (fs::path(data_dir()) / "issues-flake-f3.stl").string();
            {
                std::ofstream out(path);
                out << "solid flake\n";
                write_box(out, {-10, -10, 0}, {10, 10, 0.02});
                out << "endsolid flake\n";
            }
            self->m_plater->load_files(std::vector<std::string>{path}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false);
            self->m_plater->get_partplate_list().get_plate(0)->add_instance(0, 0, true);
            self->m_plater->canvas3D()->reload_scene(true, true);
            self->m_plater->changed_object(0);
            self->stop_answering_modals();
            self->m_work_before = self->persistence().document().work().size();
        }, {}, {}});
        steps.push_back({"f3_slice", [self] { self->slice_now(); }, [self] {
            return !self->m_plater->is_background_process_slicing() && self->issues().checked &&
                   std::chrono::steady_clock::now() - self->m_slice_started > std::chrono::seconds(3);
        }, [self] {
            self->stop_answering_modals();
            const Agent::WorkEntry* slice = self->dump_work("f3");
            self->check(slice != nullptr && slice->state == "failed" && !slice->detail.empty(), "f3_a_failed_slice_is_saved_with_its_reason");
        }});
        steps.push_back({"f4_an_arrange_is_an_entry_too", [self] {
            self->two_cubes("f4");
            self->m_work_before = self->persistence().document().work().size();
            // The wait below gives the arrange two seconds from here, not from f3's slice.
            self->m_slice_started = std::chrono::steady_clock::now();
            self->m_plater->arrange();
        }, [self] {
            return self->m_plater->get_ui_job_worker().is_idle() && self->issues().checked &&
                   std::chrono::steady_clock::now() - self->m_slice_started > std::chrono::seconds(2);
        }, [self] {
            self->issue_monitor().refresh_now();
            for (const NativeNotice& notice : self->native_notices())
                std::cerr << "HARNESS NATIVE  type=" << notice.type << " level=" << notice.level << " text='" << one_line(notice.text) << "'\n";
            const Agent::WorkEntry* last = self->dump_work("f4");
            self->check(self->persistence().document().work().size() - self->m_work_before == 1 && last != nullptr && last->kind == "job" &&
                            last->state == "done", "f4_one_arrange_is_one_entry_saved_as_done");
            std::cerr << "HARNESS RESULT f4 entries_added=" << self->persistence().document().work().size() - self->m_work_before
                      << " last_kind=" << (last ? last->kind : "") << " last_title='" << (last ? last->title : "") << "' last_state="
                      << (last ? last->state : "") << '\n';
        }});
        }

        steps.push_back({"readers_done", [self] {
            self->answer_modals(wxID_NO);
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "readers_final_new_project");
            self->stop_answering_modals();
        }, {}, {}});
        run_issue_steps();
    }

    // Prints the work log and returns the newest entry added since `m_work_before`.
    const Agent::WorkEntry* dump_work(const std::string& label)
    {
        m_work = persistence().document().work();
        for (const Agent::WorkEntry& entry : m_work)
            std::cerr << "HARNESS WORK " << label << " seq=" << entry.seq << " id=" << entry.id << " kind=" << entry.kind << " state=" << entry.state
                      << " after='" << entry.after_id << "' title='" << entry.title << "' detail='" << one_line(entry.detail) << "'\n";
        return m_work.size() > m_work_before ? &m_work.back() : nullptr;
    }

    std::vector<Agent::WorkEntry> m_work;
    std::size_t                   m_work_before{0};
    std::string                   m_last_item;
    bool                          m_cancel_sent{false};
    std::vector<NativeNotice> m_native;
    bool m_silk_chosen{false}, m_pva_chosen{false}, m_tpu_chosen{false}, m_readers_sliced{false};
    int  m_drawn_control{-1}, m_drawn_suppressed{-1};
