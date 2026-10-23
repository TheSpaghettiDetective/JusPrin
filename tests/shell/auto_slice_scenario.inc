    // The shell's own slicing (--auto-slice). Included inside class Scenario,
    // after the print-issues scenario, whose step runner and helpers it uses.
    //
    // The fixture is the default mode's: one cube on each of two plates. With
    // no Slice press the active plate is sliced once the project is quiet: the
    // header ends on Print, Prepare has an estimate, and the closed chat pane
    // stays closed. A move waits out the quiet period; a held Agent turn holds
    // the slice until it ends; a run the person cancels is not started again
    // until the project changes; the Agent's slice_start takes an automatic
    // run over, which then shows in the chat; and a plate moved to is sliced.

    bool                                  m_auto_ran{false};
    bool                                  m_auto_early{false};
    bool                                  m_auto_invalidated{false};
    std::chrono::steady_clock::time_point m_auto_mark;

    AutoSlice& auto_slice() { return *installed_shell()->auto_slice(); }

    std::chrono::steady_clock::duration since_auto_mark() const { return std::chrono::steady_clock::now() - m_auto_mark; }

    // What AutoSlice reads, printed every two seconds of a wait, so a slice
    // that never starts names the condition that held it.
    std::chrono::steady_clock::time_point m_auto_dumped;
    void dump_auto_facts()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_auto_dumped < std::chrono::seconds(2))
            return;
        m_auto_dumped     = now;
        PartPlate*  plate  = m_plater->get_partplate_list().get_curr_plate();
        GLCanvas3D& canvas = prepare_canvas();
        std::cerr << "HARNESS AUTO tab=" << m_notebook->GetSelection() << " iconized=" << m_frame->IsIconized()
                  << " agent_busy=" << project_host().busy() << " slicing=" << m_plater->is_background_process_slicing()
                  << " jobs_idle=" << m_plater->get_ui_job_worker().is_idle()
                  << " update_scheduled=" << m_plater->is_background_process_update_scheduled()
                  << " gizmo=" << int(canvas.get_gizmos_manager().get_current_type()) << " dragging=" << canvas.is_dragging()
                  << " mouse_dragging=" << canvas.is_mouse_dragging() << " can_slice=" << (plate && plate->can_slice())
                  << " apply_invalid=" << (plate && plate->is_apply_result_invalid())
                  << " printable=" << (plate && plate->has_printable_instances())
                  << " sliced=" << (plate && plate->is_slice_result_valid());
        const AutoSliceFacts& facts = auto_slice().last_facts();
        std::cerr << " | facts allowed=" << facts.allowed << " busy=" << facts.busy << " sliceable=" << facts.sliceable
                  << " sliced=" << facts.sliced << " tried=" << facts.tried
                  << " quiet_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(facts.quiet).count() << '\n';
    }

    // The wait for a plate the shell slices on its own, noting whether the
    // run it saw was the shell's.
    bool auto_sliced()
    {
        dump_auto_facts();
        if (auto_slice().running())
            m_auto_ran = true;
        return active_plate_sliced_and_idle();
    }

    // An edit through the path a person's drag takes, and the clock the
    // quiet period is measured on.
    void auto_edit(const Vec3d& delta)
    {
        m_auto_ran         = false;
        m_auto_early       = false;
        m_auto_invalidated = false;
        move_object(0, delta);
        m_auto_mark = std::chrono::steady_clock::now();
    }

    // A move invalidates the slice once OrcaSlicer applies it, a moment
    // later; until then the old slice is still valid, so it is noted here.
    void note_auto_progress()
    {
        const PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
        if (plate != nullptr && !plate->is_slice_result_valid())
            m_auto_invalidated = true;
        if (m_plater->is_background_process_slicing() && since_auto_mark() < kAutoSliceQuietPeriod)
            m_auto_early = true;
    }

    void begin_auto_slice()
    {
        check(installed_shell()->auto_slice() != nullptr, "auto_slice_installed");
        if (installed_shell()->auto_slice() == nullptr) {
            fail("no auto slice");
            return;
        }
        auto  self  = shared_from_this();
        auto& steps = m_issue_steps;

        steps.push_back({"auto_first_slice", [self] {
            self->m_frame->select_tab(size_t(MainFrame::tp3DEditor));
            self->m_plater->select_plate(0);
            if (!installed_shell()->is_agent_pane_collapsed())
                installed_shell()->toggle_agent_pane();
            self->m_auto_ran = false;
        // Nothing in this scenario presses Slice before the takeover step,
        // so a sliced plate is the shell's own doing. The 20 mm cube can
        // slice before this step begins, so whether the run was seen is not
        // asked here; the steps after an edit wait out the quiet period first.
        }, [self] { return self->auto_sliced(); }, [self] {
            self->check(installed_shell()->is_agent_pane_collapsed(), "auto_slice_leaves_the_closed_chat_pane_closed");
            const auto plate = self->active_workspace_plate();
            self->check(plate && plate->estimate.has_value() && plate->estimate_status == Workspace::EstimateStatus::Current,
                        "auto_slice_gives_prepare_a_current_estimate");
            self->check(primary_print_action(installed_shell()->status_row()->action_state()).primary.action == PrintAction::Print,
                        "auto_slice_leaves_the_header_on_print");
        }});

        steps.push_back({"auto_edit_waits_for_quiet", [self] { self->auto_edit(Vec3d(5, 0, 0)); }, [self] {
            self->note_auto_progress();
            return self->since_auto_mark() >= kAutoSliceQuietPeriod && self->m_auto_invalidated && self->auto_sliced();
        }, [self] {
            self->check(self->m_auto_invalidated, "auto_move_invalidates_the_slice");
            self->check(!self->m_auto_early, "auto_slice_waits_out_the_quiet_period");
            self->check(self->m_auto_ran, "auto_slice_reslices_after_the_move");
        }});

        // A reply held open is a running Agent turn; edits made during it are
        // not sliced until it ends.
        steps.push_back({"auto_turn_holds", [self] {
            self->m_issue_agent->hold = true;
            self->check(self->project_host().submit_issue_message("auto-slice-turn", "Hold this turn.", "{}") ==
                            Agent::AgentHost::SubmitResult::Started,
                        "auto_turn_starts");
            self->auto_edit(Vec3d(-5, 0, 0));
        }, [self] {
            if (self->m_plater->is_background_process_slicing())
                self->m_auto_early = true;
            return self->since_auto_mark() >= 2 * kAutoSliceQuietPeriod;
        }, [self] {
            self->check(self->project_host().busy(), "auto_turn_is_still_running");
            self->check(!self->m_auto_early, "auto_slice_waits_for_the_agent_turn");
            self->check(!self->active_plate_sliced_and_idle(), "auto_plate_stays_unsliced_during_the_turn");
            self->m_issue_agent->release();
        }});

        steps.push_back({"auto_turn_ends", {}, [self] { return !self->project_host().busy() && self->auto_sliced(); }, [self] {
            self->check(self->active_plate_sliced_and_idle(), "auto_slice_runs_once_the_turn_ends");
        }});

        // The cube at six times its size slices slowly enough to be caught
        // running. The scale is the scale tool's own change to the instance.
        steps.push_back({"auto_slow_cube", [self] {
            ModelObject* object = self->m_plater->model().objects.front();
            object->instances.front()->set_scaling_factor(Vec3d(6.0, 6.0, 6.0));
            object->ensure_on_bed();
            self->m_plater->update(false, true);
            self->m_auto_ran = false;
        }, [self] {
            self->dump_auto_facts();
            return self->auto_slice().running();
        }, [self] {
            self->check(self->m_plater->is_background_process_slicing(), "auto_slow_cube_is_slicing");
            // The header's Cancel, as the person presses it.
            installed_shell()->status_row()->request_action(PrintAction::Cancel);
            self->m_auto_mark = std::chrono::steady_clock::now();
        }});

        steps.push_back({"auto_cancel_is_respected", {}, [self] {
            if (self->m_plater->is_background_process_slicing())
                self->m_auto_early = true;
            return self->since_auto_mark() >= 2 * kAutoSliceQuietPeriod;
        }, [self] {
            self->check(!self->m_auto_early, "auto_slice_does_not_restart_a_cancelled_run");
            self->check(!self->active_plate_sliced_and_idle(), "auto_cancelled_plate_stays_unsliced");
        }});

        // A change after the cancel slices again; the Agent's slice_start
        // then takes the automatic run over instead of refusing it, and the
        // run, now the Agent's, gets its entry in the chat.
        steps.push_back({"auto_change_after_cancel", [self] {
            if (!installed_shell()->is_agent_pane_collapsed())
                installed_shell()->toggle_agent_pane();
            self->auto_edit(Vec3d(5, 0, 0));
        },
                         [self] { return self->auto_slice().running(); }, [self] {
            const auto snapshot = self->installed_workspace_snapshot();
            const auto taken    = installed_shell()->workspace()->start_slice(snapshot.active_plate, false);
            self->check(taken.succeeded(), "auto_run_is_taken_over_without_preempt");
            self->check(!self->auto_slice().running(), "auto_run_taken_over_is_no_longer_automatic");
            self->check(self->m_plater->is_background_process_slicing(), "auto_run_taken_over_keeps_slicing");
        }});

        steps.push_back({"auto_takeover_completes", {}, [self] { return self->active_plate_sliced_and_idle(); }, [self] {
            self->check(!installed_shell()->is_agent_pane_collapsed(), "auto_run_taken_over_shows_in_the_chat");
        }});

        steps.push_back({"auto_other_plate", [self] {
            self->m_plater->select_plate(1);
            self->m_auto_ran = false;
            self->m_auto_mark = std::chrono::steady_clock::now();
            self->m_auto_early = false;
        }, [self] {
            if (self->m_plater->is_background_process_slicing() && self->since_auto_mark() < kAutoSliceQuietPeriod)
                self->m_auto_early = true;
            return self->auto_sliced();
        }, [self] {
            self->check(self->m_plater->get_partplate_list().get_curr_plate_index() == 1, "auto_second_plate_is_active");
            self->check(!self->m_auto_early, "auto_plate_switch_waits_out_the_quiet_period");
            self->check(self->m_auto_ran, "auto_slice_slices_the_plate_moved_to");
        }});

        run_issue_steps();
    }
