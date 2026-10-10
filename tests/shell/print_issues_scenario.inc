    // The print-issues scenario (--print-issues). Included inside class Scenario.
    //
    // Runs the real application through the lifecycle the feature has to
    // survive and prints what OrcaSlicer reported at each point. Every state
    // is produced by a native path (a committed move, the print tab's own
    // config load, the slice button's request), and every expectation is
    // read back from OrcaSlicer, the monitor and the chat document.

    using IssueSnapshot = PrintIssues::PrintIssueSnapshot;
    using Issue         = PrintIssues::PrintIssue;

    struct IssueStep
    {
        std::string           name;
        std::function<void()> act;
        std::function<bool()> done;   // empty: OrcaSlicer has caught up
        std::function<void()> verify;
        // Runs one loop turn after the action: OrcaSlicer has heard about the
        // edit and has not yet applied it to the print.
        std::function<void()> between;
    };

    // How many of OrcaSlicer's own notices a canvas draws: the notification
    // manager gives each an ImGui window of its own, named with this prefix.
    int drawn_notices(GLCanvas3D& canvas)
    {
        canvas.render();
        int count = 0;
        for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
            count += window->Active && std::string(window->Name).rfind("!!Ntfctn", 0) == 0 ? 1 : 0;
        return count;
    }

    PrintIssues::PrintIssueMonitor& issue_monitor() { return *installed_shell()->print_issues(); }
    PrintIssues::IssueOverlay&      issue_overlay() { return *installed_shell()->issue_overlay(); }
    const IssueSnapshot&            issues() { return issue_monitor().snapshot(); }
    Agent::AgentHost&               project_host() { return installed_shell()->agent_pane()->web_view().host(); }

    bool orca_caught_up()
    {
        return issues().checked && !m_plater->is_background_process_update_scheduled() &&
               !m_plater->is_background_process_slicing();
    }

    void dump_issues(const std::string& label)
    {
        const IssueSnapshot& s = issues();
        std::cerr << "HARNESS ISSUES " << label << " checked=" << s.checked << " slicing=" << s.slicing << " sliced=" << s.sliced
                  << " plate=" << s.plate << " session=" << s.session << " generation=" << s.generation << " count=" << s.issues.size()
                  << " native_popups=" << m_plater->get_notification_manager()->get_notification_count()
                  << " apply_invalid=" << m_plater->get_partplate_list().get_curr_plate()->is_apply_result_invalid()
                  << " notices_drawn=" << drawn_notices(prepare_canvas())
                  << " validate_runs=" << issue_monitor().validate_runs() << " validate_ms=" << issue_monitor().last_validate_ms() << '\n';
        for (const Issue& issue : s.issues) {
            std::string text = issue.message;
            std::replace(text.begin(), text.end(), '\n', '|');
            std::cerr << "HARNESS ISSUE   " << PrintIssues::severity_name(issue.severity) << (issue.stale ? " STALE" : "")
                      << " id=" << issue.id << " code=" << issue.code << " setting=" << issue.setting << " object=" << issue.object
                      << " copy=" << issue.instance << " name='" << issue.object_name << "' text='" << text << "'\n";
        }
    }

    const Issue* issue_from(PrintIssues::IssueSource source, PrintIssues::IssueSeverity severity)
    {
        for (const Issue& issue : issues().issues)
            if (issue.source == source && issue.severity == severity)
                return &issue;
        return nullptr;
    }

    ModelObject* issue_object(size_t index) { return m_plater->model().objects[index]; }

    // A committed move, the way releasing a drag commits one.
    void move_object(size_t index, const Vec3d& delta)
    {
        m_plater->select_object(index);
        Selection& selection = prepare_canvas().get_selection();
        selection.setup_cache();
        selection.translate(delta, TransformationType::World);
        prepare_canvas().do_move("Move Object");
    }

    // One process setting through the print tab, the owner the fork's own
    // settings command uses.
    template <class Option> void set_process_option(const std::string& key, Option* value)
    {
        DynamicPrintConfig diff;
        diff.set_key_value(key, value);
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff);
    }

    // The top centre of a copy's box on screen, computed here rather than
    // taken from the overlay, so the comparison is not the overlay with itself.
    std::optional<std::pair<float, float>> project_copy_top(std::uint64_t object_id, std::uint64_t copy_id)
    {
        const Model&  model = m_plater->model();
        BoundingBoxf3 box;
        for (const GLVolume* volume : prepare_canvas().get_volumes().volumes) {
            const int o = volume->object_idx(), i = volume->instance_idx();
            if (o < 0 || o >= int(model.objects.size()) || model.objects[o]->id().id != object_id || i < 0 ||
                i >= int(model.objects[o]->instances.size()))
                continue;
            if (copy_id != 0 && model.objects[o]->instances[i]->id().id != copy_id)
                continue;
            box.merge(volume->transformed_bounding_box());
        }
        if (!box.defined)
            return std::nullopt;
        const Camera& camera = m_plater->get_camera();
        const Eigen::Vector4d world(box.center().x(), box.center().y(), box.max.z(), 1.0);
        const Eigen::Vector4d eye  = camera.get_view_matrix().matrix() * world;
        const Eigen::Vector4d clip = camera.get_projection_matrix().matrix() * eye;
        if (clip.w() <= 1e-9)
            return std::nullopt;
        const auto& viewport = camera.get_viewport();
        return std::make_pair(float((clip.x() / clip.w() + 1.0) / 2.0 * viewport[2]),
                              float((1.0 - clip.y() / clip.w()) / 2.0 * viewport[3]));
    }

    // The screen rectangle a copy's box covers, from its eight corners.
    std::optional<PrintIssues::OverlayRect> project_copy_rect(std::uint64_t object_id, std::uint64_t copy_id)
    {
        const Model&  model = m_plater->model();
        BoundingBoxf3 box;
        for (const GLVolume* volume : prepare_canvas().get_volumes().volumes) {
            const int o = volume->object_idx(), i = volume->instance_idx();
            if (o < 0 || o >= int(model.objects.size()) || model.objects[o]->id().id != object_id || i < 0 ||
                i >= int(model.objects[o]->instances.size()))
                continue;
            if (copy_id != 0 && model.objects[o]->instances[i]->id().id != copy_id)
                continue;
            box.merge(volume->transformed_bounding_box());
        }
        if (!box.defined)
            return std::nullopt;
        const Camera& camera = m_plater->get_camera();
        const Eigen::Matrix4d to_clip = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
        const auto& viewport = camera.get_viewport();
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int corner = 0; corner < 8; ++corner) {
            const Eigen::Vector4d world(corner & 1 ? box.max.x() : box.min.x(), corner & 2 ? box.max.y() : box.min.y(),
                                        corner & 4 ? box.max.z() : box.min.z(), 1.0);
            const Eigen::Vector4d clip = to_clip * world;
            if (clip.w() <= 1e-9)
                return std::nullopt;
            const float x = float((clip.x() / clip.w() + 1.0) / 2.0 * viewport[2]);
            const float y = float((1.0 - clip.y() / clip.w()) / 2.0 * viewport[3]);
            x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
        return PrintIssues::OverlayRect{x0, y0, x1 - x0, y1 - y0};
    }

    static bool rects_overlap(const PrintIssues::OverlayRect& a, const PrintIssues::OverlayRect& b)
    {
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    }

    // Moves the view so the copy's top sits `pixels` below the canvas's top edge.
    void put_copy_near_top(std::uint64_t object_id, std::uint64_t copy_id, float pixels)
    {
        Camera& camera = m_plater->get_camera();
        const auto before = project_copy_top(object_id, copy_id);
        if (!before) return;
        const Vec3d up = camera.get_dir_up();
        camera.set_target(camera.get_target() + 10.0 * up);
        const auto probe = project_copy_top(object_id, copy_id);
        if (!probe || std::abs(probe->second - before->second) < 1e-3f) return;
        const double per_unit = (probe->second - before->second) / 10.0;
        camera.set_target(camera.get_target() + double(pixels - probe->second) / per_unit * up);
        prepare_canvas().set_as_dirty();
    }

    const PrintIssues::IssueOverlayFrame& issue_frame()
    {
        prepare_canvas().render();
        return issue_overlay().last_frame();
    }

    // The bubble is where the object is: its anchor is the projected top of
    // the target, and the bubble sits within reach of it, inside the canvas.
    void check_bubble_anchored(const std::string& name, const Issue& issue)
    {
        const PrintIssues::IssueOverlayFrame frame = issue_frame();
        check(frame.bubble && frame.issue_id == issue.id, name + "_bubble_shows_the_issue");
        const auto expected = project_copy_top(issue.object, issue.instance);
        check(expected.has_value(), name + "_target_projects_to_screen");
        if (!expected || !frame.bubble)
            return;
        const Size canvas = prepare_canvas().get_canvas_size();
        std::cerr << "HARNESS BUBBLE " << name << " anchor=" << frame.anchor_x << "," << frame.anchor_y << " expected="
                  << expected->first << "," << expected->second << " bubble=" << frame.bubble_rect.x << "," << frame.bubble_rect.y
                  << " " << frame.bubble_rect.w << "x" << frame.bubble_rect.h << " docked=" << frame.docked()
                  << " canvas=" << canvas.get_width() << "x" << canvas.get_height() << '\n';
        check(std::abs(frame.anchor_x - expected->first) <= 1.5f && std::abs(frame.anchor_y - expected->second) <= 1.5f,
              name + "_anchor_is_the_objects_projected_top");
        check(!frame.docked() && frame.target_in_view, name + "_bubble_is_beside_the_object");
        check(frame.bubble_rect.x >= 0 && frame.bubble_rect.y >= 0 &&
                  frame.bubble_rect.x + frame.bubble_rect.w <= float(canvas.get_width()) &&
                  frame.bubble_rect.y + frame.bubble_rect.h <= float(canvas.get_height()),
              name + "_bubble_stays_inside_the_canvas");
        m_last_anchor = {frame.anchor_x, frame.anchor_y};
    }

    // A pointer click at a rectangle the overlay drew, through the canvas's
    // own mouse handler and ImGui's hit test.
    void click_overlay(const PrintIssues::OverlayRect& rect)
    {
        prepare_canvas().render();
        const double scale = prepare_canvas().get_scale();
        const wxPoint at(int((rect.x + rect.w / 2) / scale), int((rect.y + rect.h / 2) / scale));
        send_canvas_mouse(wxEVT_MOTION, at, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, at, true);
        send_canvas_mouse(wxEVT_LEFT_UP, at, false);
    }

    std::vector<Agent::ConversationMessage> chat_messages()
    {
        const auto& document = persistence().document();
        return document.messages(document.active_conversation_id());
    }

    std::size_t user_message_count()
    {
        std::size_t count = 0;
        for (const Agent::ConversationMessage& message : chat_messages())
            count += message.role == Agent::MessageRole::User ? 1 : 0;
        return count;
    }

    std::optional<Agent::ConversationMessage> last_user_message()
    {
        std::optional<Agent::ConversationMessage> last;
        for (const Agent::ConversationMessage& message : chat_messages())
            if (message.role == Agent::MessageRole::User)
                last = message;
        return last;
    }

    // The canvas's own pixels, read back from the frame it just drew. A screen
    // blit photographs the desktop and comes back blank when the session
    // cannot record the screen; this needs neither.
    void capture_issues(const std::string& name)
    {
        if (m_state->capture_dir.empty())
            return;
        prepare_canvas().render();
        const Size size = prepare_canvas().get_canvas_size();
        const int  w = size.get_width(), h = size.get_height();
        std::vector<unsigned char> pixels(std::size_t(w) * h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_FRONT);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        wxImage image(w, h);
        unsigned char* rgb = image.GetData();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const unsigned char* from = &pixels[(std::size_t(h - 1 - y) * w + x) * 4];
                unsigned char*       to   = &rgb[(std::size_t(y) * w + x) * 3];
                to[0] = from[0]; to[1] = from[1]; to[2] = from[2];
            }
        fs::create_directories(m_state->capture_dir);
        const std::string file = (m_state->capture_dir / ("issues-" + name + ".png")).string();
        const bool ok = image.SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
        check(ok, "captured_issues_" + name);
        std::cout << "HARNESS ARTIFACT issues-" << name << " " << w << "x" << h << std::endl;
    }

    static void write_box(std::ostream& out, const Vec3d& min, const Vec3d& max)
    {
        const Vec3d v[8] = {{min.x(), min.y(), min.z()}, {max.x(), min.y(), min.z()}, {max.x(), max.y(), min.z()},
                            {min.x(), max.y(), min.z()}, {min.x(), min.y(), max.z()}, {max.x(), min.y(), max.z()},
                            {max.x(), max.y(), max.z()}, {min.x(), max.y(), max.z()}};
        const int faces[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                  {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
        for (const auto& face : faces) {
            out << "facet normal 0 0 0\n outer loop\n";
            for (int corner : face)
                out << "  vertex " << v[corner].x() << ' ' << v[corner].y() << ' ' << v[corner].z() << '\n';
            out << " endloop\nendfacet\n";
        }
    }

    // A slab held up by one thin leg: nothing under most of it.
    std::string write_overhang_stl()
    {
        const std::string path = (fs::path(data_dir()) / "issues-overhang.stl").string();
        std::ofstream out(path);
        out << "solid overhang\n";
        write_box(out, {-3, -3, 0}, {3, 3, 12});
        write_box(out, {-25, -25, 12}, {25, 25, 15});
        out << "endsolid overhang\n";
        return path;
    }

    // Answers any modal dialog the app opens while a step runs, and says so.
    // With captures on, the dialog is pictured on the tick it is first seen
    // and answered on the next.
    void answer_modals(int answer)
    {
        if (!m_modal_closer_bound) {
            m_modal_closer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
                for (wxWindow* window : wxTopLevelWindows)
                    if (auto* dialog = dynamic_cast<wxDialog*>(window); dialog != nullptr && dialog->IsModal()) {
                        if (!m_state->capture_dir.empty() && m_modal_pictured != dialog) {
                            m_modal_pictured = dialog;
                            blit_screen(dialog->GetScreenRect(), "issues-dialog-" + std::to_string(m_modals_answered + 1));
                            return;
                        }
                        m_last_modal_title = dialog->GetTitle().ToUTF8().data();
                        ++m_modals_answered;
                        std::cerr << "HARNESS MODAL '" << m_last_modal_title << "' answered " << m_modal_answer << '\n';
                        dialog->EndModal(m_modal_answer);
                    }
            });
            m_modal_closer_bound = true;
        }
        m_modal_answer = answer;
        m_modal_closer_timer.Start(200);
    }
    void stop_answering_modals() { m_modal_closer_timer.Stop(); }

    void run_issue_steps()
    {
        if (m_issue_step == m_issue_steps.size()) {
            finish();
            return;
        }
        const std::size_t index = m_issue_step++;
        std::cerr << "HARNESS STEP " << m_issue_steps[index].name << '\n';
        try {
            if (m_issue_steps[index].act)
                m_issue_steps[index].act();
        } catch (const std::exception& error) {
            fail("step " + m_issue_steps[index].name + " threw: " + error.what());
            return;
        }
        // What the monitor says on the loop turn right after the action,
        // before OrcaSlicer's deferred update: the "is it honest while it
        // does not know" evidence.
        wait_until_settled("issues_" + m_issue_steps[index].name + "_settled", [self = shared_from_this(), index] {
            self->dump_issues(self->m_issue_steps[index].name + " (right after)");
            if (self->m_issue_steps[index].between)
                self->m_issue_steps[index].between();
            auto done = self->m_issue_steps[index].done;
            self->wait_until(done ? done : std::function<bool()>([self] { return self->orca_caught_up(); }),
                             "issues_" + self->m_issue_steps[index].name + "_ready", [self, index] {
                self->wait_until_settled("issues_" + self->m_issue_steps[index].name + "_ready_settled", [self, index] {
                    self->dump_issues(self->m_issue_steps[index].name + " (settled)");
                    if (self->m_issue_steps[index].verify)
                        self->m_issue_steps[index].verify();
                    self->run_issue_steps();
                });
            });
        });
    }

    void begin_print_issues()
    {
        using PrintIssues::IssueSeverity;
        using PrintIssues::IssueSource;
        check(installed_shell()->print_issues() != nullptr && installed_shell()->issue_overlay() != nullptr, "issue_monitor_installed");
        if (installed_shell()->print_issues() == nullptr) {
            fail("no issue monitor");
            return;
        }
        auto self = shared_from_this();
        auto& steps = m_issue_steps;

        // -- 1. Two overlapping objects with the same name ---------------------
        steps.push_back({"fixture", [self] {
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "issues_new_project");
            const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            self->check(self->m_plater->load_files(std::vector<std::string>{cube},
                            LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
                        "issues_cube_loaded");
            self->check(self->m_plater->duplicate_object(0) == 1, "issues_second_cube");
            PartPlateList& plates = self->m_plater->get_partplate_list();
            self->check(plates.get_plate(0)->add_instance(0, 0, true) == 0, "issues_first_cube_centred");
            self->check(plates.get_plate(0)->add_instance(1, 0, true) == 0, "issues_second_cube_centred");
            self->m_plater->canvas3D()->reload_scene(true, true);
            // Far enough apart to be clean; the next step closes the gap.
            self->move_object(1, Vec3d(60, 0, 0));
        }, {}, [self] {
            self->check(self->issue_object(0)->name == self->issue_object(1)->name, "issues_two_objects_share_a_name");
            self->check(self->issues().checked && self->issues().issues.empty(), "clean_plate_is_checked_and_empty");
            self->check(self->issue_frame().pill_text == "No print issues", "pill_says_no_issues_only_after_a_check");
            self->capture_issues("01-clean");
        }});

        steps.push_back({"overlap_warning", [self] { self->move_object(1, Vec3d(-50, 0, 0)); }, {}, [self] {
            const Issue* warning = self->issue_from(IssueSource::Validation, IssueSeverity::Warning);
            self->check(warning != nullptr, "overlap_reports_a_validation_warning");
            self->check(self->issues().issues.size() == 1, "overlap_reports_exactly_one_issue");
            if (warning == nullptr) return;
            self->m_warning_id   = warning->id;
            self->m_warning_copy = warning->instance;
            self->check(warning->instance != 0 && warning->object != 0, "overlap_warning_names_one_copy_by_id");
            self->check(!self->m_plater->get_partplate_list().get_curr_plate()->is_apply_result_invalid(),
                        "overlap_warning_does_not_block_slicing");
            self->check(installed_shell()->status_row()->action_state().can_slice, "overlap_warning_leaves_slice_enabled");
        }});

        // -- 2. Highlight and bubble ------------------------------------------
        steps.push_back({"select_issue", [self] { self->issue_overlay().select(self->m_warning_id); }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            if (warning == nullptr) { self->check(false, "warning_still_reported"); return; }
            // Two objects carry the same name; the selection must be the one
            // whose copy id Orca named.
            const Selection& selection = self->prepare_canvas().get_selection();
            const int object_index = selection.get_object_idx();
            self->check(selection.is_single_full_instance() && object_index >= 0 &&
                            self->issue_object(object_index)->id().id == warning->object &&
                            self->issue_object(object_index)->instances[selection.get_instance_idx()]->id().id == warning->instance,
                        "highlight_selects_the_copy_orca_named_not_a_namesake");
            self->check_bubble_anchored("warning", *warning);
            self->m_anchor_before = self->m_last_anchor;
            self->capture_issues("02-warning-bubble");
        }});

        steps.push_back({"orbit_camera", [self] { self->prepare_canvas().select_view("left"); }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            if (warning == nullptr) { self->check(false, "warning_survives_orbit"); return; }
            self->check_bubble_anchored("orbited", *warning);
            self->check(std::abs(self->m_last_anchor.first - self->m_anchor_before.first) > 2.f ||
                            std::abs(self->m_last_anchor.second - self->m_anchor_before.second) > 2.f,
                        "bubble_moved_with_the_camera");
            self->m_anchor_before = self->m_last_anchor;
            self->capture_issues("03-orbited");
        }});

        steps.push_back({"zoom_camera", [self] {
            self->prepare_canvas().select_view("iso");
            self->m_plater->get_camera().update_zoom(1.5);
            self->prepare_canvas().set_as_dirty();
        }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            if (warning == nullptr) { self->check(false, "warning_survives_zoom"); return; }
            self->check_bubble_anchored("zoomed", *warning);
            self->check(std::abs(self->m_last_anchor.first - self->m_anchor_before.first) > 2.f ||
                            std::abs(self->m_last_anchor.second - self->m_anchor_before.second) > 2.f,
                        "bubble_moved_with_the_zoom");
            self->capture_issues("04-zoomed");
        }});

        steps.push_back({"resize_window", [self] {
            self->m_frame->Maximize(false);
            self->m_frame->SetSize(self->m_frame->FromDIP(wxSize(1100, 720)));
        }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            if (warning == nullptr) { self->check(false, "warning_survives_resize"); return; }
            self->check_bubble_anchored("resized", *warning);
            self->capture_issues("05-resized");
        }});

        steps.push_back({"move_object_under_bubble", [self] {
            self->m_anchor_before = self->m_last_anchor;
            self->move_object(1, Vec3d(0, 8, 0)); // still overlapping, so the issue stays
        }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            if (warning == nullptr) { self->check(false, "warning_survives_the_move"); return; }
            self->check_bubble_anchored("moved", *warning);
            self->check(std::abs(self->m_last_anchor.first - self->m_anchor_before.first) > 2.f ||
                            std::abs(self->m_last_anchor.second - self->m_anchor_before.second) > 2.f,
                        "bubble_moved_with_the_object");
            self->capture_issues("05b-object-moved");
        }});

        // Kenneth's screenshot, 2026-10-09: an object near the top of the view,
        // where there is no room for the bubble above it.
        steps.push_back({"object_near_top_edge", [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            if (warning != nullptr)
                self->put_copy_near_top(warning->object, warning->instance, 60.f);
        }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            if (warning == nullptr) { self->check(false, "warning_survives_the_pan"); return; }
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            const auto object = self->project_copy_rect(warning->object, warning->instance);
            const auto top    = self->project_copy_top(warning->object, warning->instance);
            self->check(frame.bubble && object.has_value() && top.has_value(), "near_top_bubble_and_object_are_on_screen");
            if (!frame.bubble || !object || !top) return;
            const Size canvas = self->prepare_canvas().get_canvas_size();
            std::cerr << "HARNESS BUBBLE near_top object_top_y=" << top->second << " object=" << object->x << "," << object->y << " "
                      << object->w << "x" << object->h << " bubble=" << frame.bubble_rect.x << "," << frame.bubble_rect.y << " "
                      << frame.bubble_rect.w << "x" << frame.bubble_rect.h << " docked=" << frame.docked() << " side=" << PrintIssues::side_name(frame.side) << '\n';
            self->check(top->second < frame.bubble_rect.h, "near_top_there_is_no_room_above_the_object");
            self->check(!frame.docked() && frame.side != PrintIssues::BubbleSide::Above, "near_top_bubble_takes_another_side_of_the_object");
            self->check(!rects_overlap(frame.bubble_rect, *object), "near_top_bubble_does_not_cover_its_object");
            self->check(!rects_overlap(frame.bubble_rect, PrintIssues::OverlayRect{self->tool_strip_layout().strip.x,
                            self->tool_strip_layout().strip.y, self->tool_strip_layout().strip.w, self->tool_strip_layout().strip.h}),
                        "near_top_bubble_does_not_cover_the_tool_strip");
            self->check(frame.bubble_rect.x >= 0 && frame.bubble_rect.y >= 0 &&
                            frame.bubble_rect.x + frame.bubble_rect.w <= float(canvas.get_width()) &&
                            frame.bubble_rect.y + frame.bubble_rect.h <= float(canvas.get_height()),
                        "near_top_bubble_stays_inside_the_canvas");
            self->capture_issues("05c-object-near-top");
            self->prepare_canvas().select_view("iso");
            self->prepare_canvas().zoom_to_bed();
        }});

        steps.push_back({"zoom_until_out_of_view", [self] {
            Camera& camera = self->m_plater->get_camera();
            camera.set_target(camera.get_target() + Vec3d(400, 400, 0));
            self->prepare_canvas().set_as_dirty();
        }, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            std::cerr << "HARNESS BUBBLE out_of_view bubble=" << frame.bubble << " docked=" << frame.docked()
                      << " in_view=" << frame.target_in_view << '\n';
            self->check(frame.bubble && frame.docked() && !frame.target_in_view, "out_of_view_target_docks_the_bubble_instead_of_hiding_it");
            self->capture_issues("06-out-of-view");
            self->prepare_canvas().select_view("iso");
            self->prepare_canvas().zoom_to_bed();
        }});

        // -- 3. OrcaSlicer's own notices are not drawn, and are not the issue ----
        steps.push_back({"native_notices", [self] {
            NotificationManager& notices = *self->m_plater->get_notification_manager();
            // Not drawn, but alive underneath: OrcaSlicer pushed its warning.
            self->check(notices.get_notification_count() > 0, "orca_still_keeps_its_own_notices_underneath");
            self->check(self->drawn_notices(self->prepare_canvas()) == 0, "orcas_notices_are_not_drawn_while_the_shell_is_installed");
            // Closing OrcaSlicer's notice, as its close button did, changes
            // nothing about the issue.
            notices.close_notification_of_type(NotificationType::ValidateWarning);
        }, {}, [self] {
            self->issue_monitor().refresh_now();
            self->check(self->issues().find(self->m_warning_id) != nullptr, "issue_does_not_depend_on_orcas_notice");
            self->capture_issues("07-native-notices-hidden");
        }});

        // -- 4. Ask AI ---------------------------------------------------------
        steps.push_back({"ask_ai", [self] {
            self->persistence().set_draft("half-typed question");
            installed_shell()->set_agent_pane_collapsed(true);
            self->m_user_messages_before = self->user_message_count();
            self->m_requests_before      = self->m_issue_agent->requests.size();
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.actions_enabled && !frame.ask.empty(), "ask_button_is_drawn_and_enabled");
            self->click_overlay(frame.ask);
        }, [self] { return self->user_message_count() > self->m_user_messages_before && !self->project_host().busy(); }, [self] {
            const auto message = self->last_user_message();
            self->check(message.has_value(), "ask_added_a_user_message");
            if (!message) return;
            std::cerr << "HARNESS CHAT ask text='" << message->text << "' context=" << message->issue_context << '\n';
            self->check(message->text.rfind("Explain this issue and my options.", 0) == 0, "ask_message_is_visible_and_says_explain");
            const nlohmann::json context = nlohmann::json::parse(message->issue_context, nullptr, false);
            const auto workspace = installed_shell()->workspace()->snapshot();
            self->check(context.is_object() && context.value("intent", "") == "explain", "ask_context_says_explain");
            self->check(context.is_object() && context["issue"].value("id", "") == self->m_warning_id &&
                            context["target"].value("copyId", std::uint64_t(0)) == self->m_warning_copy,
                        "ask_context_names_the_issue_and_the_copy_by_id");
            self->check(context.is_object() && context["workspace"].value("sessionId", "") == std::to_string(workspace.session.value()) &&
                            context["workspace"].value("revision", std::uint64_t(0)) == workspace.revision,
                        "ask_context_carries_the_current_session_and_revision");
            self->check(self->m_issue_agent->requests.size() == self->m_requests_before + 1, "ask_started_one_agent_request");
            if (self->m_issue_agent->requests.size() > self->m_requests_before) {
                const Agent::AgentRequest& request = self->m_issue_agent->requests.back();
                self->check(request.user_text.find("print-issue context") != std::string::npos &&
                                request.user_text.find(self->m_warning_id) != std::string::npos,
                            "ask_request_gives_the_model_the_structured_context");
                bool read_only = !request.session.tool_names.empty();
                for (const std::string& name : request.session.tool_names) {
                    const Agent::ToolDefinition* tool = Agent::ToolRegistry::instance().find(name);
                    read_only = read_only && tool != nullptr && tool->action_class == Agent::ActionClass::ReadOnly;
                }
                std::cerr << "HARNESS CHAT ask tools=" << request.session.tool_names.size() << '\n';
                self->check(read_only, "ask_turn_is_offered_read_only_tools_only");
                self->check(request.workspace.revision == workspace.revision, "ask_request_carries_the_workspace_snapshot");
            }
            self->check(self->persistence().draft() == "half-typed question", "ask_leaves_the_composer_draft_alone");
            self->check(!installed_shell()->is_agent_pane_collapsed(), "ask_opens_the_closed_chat_pane");
            self->capture_issues("08-asked");
            self->capture_chat_pane("08-asked-chat");
        }});

        steps.push_back({"ask_again_unchanged", [self] {
            self->m_user_messages_before = self->user_message_count();
            self->m_last_outcome = installed_shell()->submit_issue(self->m_warning_id, PrintIssues::IssueIntent::Explain).outcome;
        }, {}, [self] {
            std::cerr << "HARNESS CHAT ask_again outcome=" << self->m_last_outcome << '\n';
            self->check(self->m_last_outcome == "duplicate" && self->user_message_count() == self->m_user_messages_before,
                        "second_press_with_nothing_changed_adds_no_second_message");
        }});

        // -- 5. Resolve with AI, and the busy path -----------------------------
        steps.push_back({"resolve_with_ai", [self] {
            self->m_issue_agent->hold    = true; // keep the reply open to see the busy state
            self->m_user_messages_before = self->user_message_count();
            self->m_requests_before      = self->m_issue_agent->requests.size();
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.actions_enabled && !frame.resolve.empty(), "resolve_button_is_drawn_and_enabled");
            self->click_overlay(frame.resolve);
        }, [self] { return self->user_message_count() > self->m_user_messages_before; }, [self] {
            const auto message = self->last_user_message();
            if (!message) { self->check(false, "resolve_added_a_user_message"); return; }
            std::cerr << "HARNESS CHAT resolve text='" << message->text << "'\n";
            self->check(message->text.rfind("Help fix this issue.", 0) == 0, "resolve_message_is_visible_and_says_fix");
            const nlohmann::json context = nlohmann::json::parse(message->issue_context, nullptr, false);
            self->check(context.is_object() && context.value("intent", "") == "resolve", "resolve_context_says_resolve");
            self->check(self->m_issue_agent->requests.size() == self->m_requests_before + 1 &&
                            self->m_issue_agent->requests.back().session.tool_names.empty(),
                        "resolve_turn_keeps_the_ordinary_tool_set_and_no_more");
            // While the reply is open the bubble's buttons are off, and a
            // press that got through anyway is refused.
            self->check(self->project_host().busy(), "agent_is_busy_with_the_resolve_turn");
            self->check(!self->issue_frame().actions_enabled, "buttons_are_disabled_while_a_reply_is_running");
            const std::size_t before = self->user_message_count();
            self->check(installed_shell()->submit_issue(self->m_warning_id, PrintIssues::IssueIntent::Explain).outcome == "busy" &&
                            self->user_message_count() == before,
                        "a_press_during_a_reply_sends_nothing");
            // The host itself queues a message that arrives mid-reply, as it
            // does for one from the page.
            self->check(self->project_host().submit_issue_message("issues-queued", "Queued while busy.", "{}") ==
                            Agent::AgentHost::SubmitResult::Queued,
                        "host_queues_a_message_that_arrives_mid_reply");
            self->capture_issues("09-resolve-busy");
            self->m_requests_before = self->m_issue_agent->requests.size();
            self->m_issue_agent->release();
        }});

        steps.push_back({"busy_releases", {}, [self] {
            return !self->project_host().busy() && self->m_issue_agent->requests.size() > self->m_requests_before;
        }, [self] {
            self->check(self->m_issue_agent->requests.back().user_text.rfind("Queued while busy.", 0) == 0,
                        "queued_message_is_answered_after_the_reply");
            self->check(self->issue_frame().actions_enabled, "buttons_return_when_the_reply_ends");
        }});

        // -- 6. Agent unavailable: the status still works ----------------------
        steps.push_back({"agent_unavailable", [self] {
            self->project_host().set_agent(nullptr, Agent::AgentAvailability::Unavailable);
            self->m_issue_agent = nullptr;
            self->issue_monitor().refresh_now();
            self->m_user_messages_before = self->user_message_count();
            // A nudge that keeps the overlap, so the evidence is a new one.
            self->move_object(1, Vec3d(1, 0, 0));
        }, {}, [self] {
            self->check(self->issues().checked && self->issues().find(self->m_warning_id) != nullptr,
                        "issue_status_works_with_no_agent");
            const auto submission = installed_shell()->submit_issue(self->m_warning_id, PrintIssues::IssueIntent::Explain);
            std::cerr << "HARNESS CHAT unavailable outcome=" << submission.outcome << '\n';
            self->check(submission.outcome == "agent_not_set_up" && self->user_message_count() == self->m_user_messages_before,
                        "with_no_agent_nothing_is_sent_to_the_chat");
            // A real press: the buttons stay live, and lead to the offer.
            installed_shell()->set_agent_pane_collapsed(true);
            self->m_modals_before = self->m_modals_answered;
            self->answer_modals(wxID_NO); // "Not now"
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.actions_enabled, "buttons_stay_live_with_no_agent");
            self->click_overlay(frame.ask);
        }});
        steps.push_back({"no_agent_offer_declined", {}, [self] { return self->m_modals_answered > self->m_modals_before; }, [self] {
            self->check(self->m_modals_answered == self->m_modals_before + 1, "a_press_with_no_agent_offers_to_set_it_up");
            self->check(installed_shell()->is_agent_pane_collapsed() && self->user_message_count() == self->m_user_messages_before,
                        "declining_the_offer_changes_nothing");
            self->m_modals_before = self->m_modals_answered;
            self->answer_modals(wxID_YES); // "Set up now"
            self->click_overlay(self->issue_frame().resolve);
        }});
        steps.push_back({"no_agent_offer_accepted", {}, [self] { return self->m_modals_answered > self->m_modals_before; }, [self] {
            self->stop_answering_modals();
            self->check(self->m_modals_answered == self->m_modals_before + 1, "the_other_button_makes_the_same_offer");
            self->check(!installed_shell()->is_agent_pane_collapsed(), "accepting_the_offer_opens_the_chat_panel_for_setup");
            self->check(self->user_message_count() == self->m_user_messages_before, "accepting_the_offer_sends_no_message_either");
            auto agent = std::make_unique<RecordingAgent>();
            self->m_issue_agent = agent.get();
            self->project_host().set_agent(std::move(agent), Agent::AgentAvailability::Ready);
        }});

        // -- 7. Correction, revalidation, undo and redo ------------------------
        steps.push_back({"unchecked_press", [self] {
            self->move_object(1, Vec3d(1, 0, 0)); // still overlapping
        }, {}, [self] {
            std::cerr << "HARNESS CHAT unchecked outcome=" << self->m_last_outcome << " checked_right_after=" << self->m_checked_right_after
                      << " stale_right_after=" << self->m_stale_right_after << '\n';
            self->check(self->m_last_outcome == "unchecked", "a_press_before_orca_revalidates_sends_nothing");
            self->check(!self->m_buttons_right_after, "the_buttons_are_off_until_orca_revalidates");
            self->check(!self->m_checked_right_after && self->m_stale_right_after, "an_edit_marks_the_issue_stale_until_orca_revalidates");
            self->check(self->issues().find(self->m_warning_id) != nullptr && !self->issues().find(self->m_warning_id)->stale,
                        "revalidation_makes_the_issue_current_again");
        }, [self] {
            // Orca has the edit and has not validated it: a press now.
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->m_buttons_right_after = frame.bubble && frame.actions_enabled;
            self->m_last_outcome = installed_shell()->submit_issue(self->m_warning_id, PrintIssues::IssueIntent::Resolve).outcome;
            self->m_checked_right_after = self->issues().checked;
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->m_stale_right_after = warning != nullptr && warning->stale;
        }});

        steps.push_back({"move_apart", [self] { self->move_object(1, Vec3d(60, 0, 0)); }, {}, [self] {
            std::cerr << "HARNESS CLEAR right_after checked=" << self->m_checked_right_after << " count=" << self->m_count_right_after
                      << " pill='" << self->m_pill_right_after << "'\n";
            self->check(!self->m_checked_right_after && self->m_pill_right_after != "No print issues",
                        "no_all_clear_is_shown_before_orca_revalidates_the_fix");
            self->check(self->issues().checked && self->issues().issues.empty(), "native_revalidation_clears_the_issue");
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(!frame.bubble && self->issue_overlay().selected().empty(), "bubble_is_removed_when_the_issue_clears");
            self->check(installed_shell()->submit_issue(self->m_warning_id, PrintIssues::IssueIntent::Resolve).outcome == "expired",
                        "a_cleared_issue_cannot_be_submitted");
            self->capture_issues("10-cleared");
        }, [self] {
            self->issue_monitor().refresh_now();
            self->m_checked_right_after = self->issues().checked;
            self->m_count_right_after   = self->issues().issues.size();
            self->m_pill_right_after    = self->issue_frame().pill_text;
            self->capture_issues("09b-fixed-but-not-yet-checked");
        }});

        steps.push_back({"undo", [self] { self->m_plater->undo(); }, {}, [self] {
            const Issue* warning = self->issue_from(IssueSource::Validation, IssueSeverity::Warning);
            self->check(warning != nullptr && warning->id == self->m_warning_id && warning->instance == self->m_warning_copy,
                        "undo_brings_the_same_issue_back_on_the_same_copy");
        }});
        steps.push_back({"redo", [self] { self->m_plater->redo(); }, {}, [self] {
            self->check(self->issues().checked && self->issues().issues.empty(), "redo_clears_it_again");
        }});

        // -- 8. A non-print error is not a print issue -------------------------
        steps.push_back({"operation_error", [self] {
            self->m_plater->get_notification_manager()->push_general_error_notification("Network error: the printer did not answer.");
            self->m_plater->get_notification_manager()->push_notification(NotificationType::CustomNotification,
                NotificationManager::NotificationLevel::ErrorNotificationLevel, "Download failed.");
        }, {}, [self] {
            self->check(self->issues().checked && self->issues().issues.empty(), "operation_errors_do_not_enter_the_issue_list");
            self->m_plater->get_notification_manager()->close_notification_of_type(NotificationType::BBLGeneralError);
            self->m_plater->get_notification_manager()->close_notification_of_type(NotificationType::CustomNotification);
        }});

        // -- 9. The sequential-print clearance blocker -------------------------
        steps.push_back({"sequential_blocker", [self] {
            self->move_object(1, Vec3d(-50, 0, 0));
            self->set_process_option("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
        }, {}, [self] {
            const Issue* blocker = self->issue_from(IssueSource::Validation, IssueSeverity::Blocker);
            self->check(blocker != nullptr, "by_object_overlap_reports_a_validation_blocker");
            self->check(self->m_plater->get_partplate_list().get_curr_plate()->is_apply_result_invalid(),
                        "orca_itself_marks_the_plate_invalid");
            self->check(!installed_shell()->status_row()->action_state().can_slice, "orca_itself_refuses_to_slice");
            if (blocker != nullptr) {
                self->issue_overlay().select(blocker->id);
                self->capture_issues("11-sequential-blocker");
            }
        }});

        // -- 10. Objects over the plate's edge ---------------------------------
        steps.push_back({"over_the_edge", [self] {
            self->set_process_option("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));
            // Both cubes over the plate's left edge, still overlapping.
            const double plate_min = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box().min.x();
            self->move_object(0, Vec3d(plate_min - self->issue_object(0)->instances[0]->get_offset().x() + 2, 0, 0));
            self->move_object(1, Vec3d(plate_min - self->issue_object(1)->instances[0]->get_offset().x() + 8, 0, 0));
        }, {}, [self] {
            std::size_t placement = 0, validation = 0;
            for (const Issue& issue : self->issues().issues) {
                placement += issue.source == IssueSource::Placement && issue.instance != 0 ? 1 : 0;
                validation += issue.source == IssueSource::Validation ? 1 : 0;
            }
            std::cerr << "HARNESS EDGE placement=" << placement << " validation=" << validation << '\n';
            self->check(placement == 2, "each_copy_over_the_edge_is_a_blocker_named_by_id");
            self->check(!installed_shell()->status_row()->action_state().can_slice, "object_over_the_edge_blocks_slicing_natively");
            if (const Issue* issue = self->issue_from(IssueSource::Placement, IssueSeverity::Blocker)) {
                self->issue_overlay().select(issue->id);
                self->capture_issues("12-over-the-edge");
            }
        }});

        // -- 11. A plate-wide blocker with no object ---------------------------
        steps.push_back({"plate_wide_blocker", [self] {
            // Back on the plate and apart, then vase mode: Orca's "select By
            // object to print multiple objects in spiral vase mode".
            const double plate_mid = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box().center().x();
            self->move_object(0, Vec3d(plate_mid - self->issue_object(0)->instances[0]->get_offset().x(), 0, 0));
            self->move_object(1, Vec3d(plate_mid + 12 - self->issue_object(1)->instances[0]->get_offset().x(), 0, 0));
            // The print tab asks whether to change the settings vase mode
            // needs; "Yes" keeps vase mode on.
            self->answer_modals(wxID_YES);
            self->set_process_option("spiral_mode", new ConfigOptionBool(true));
        }, {}, [self] {
            self->stop_answering_modals();
            const Issue* blocker = self->issue_from(IssueSource::Validation, IssueSeverity::Blocker);
            const Issue* warning = self->issue_from(IssueSource::Validation, IssueSeverity::Warning);
            self->check(blocker != nullptr && blocker->object == 0, "a_settings_blocker_has_no_object_target");
            self->check(warning != nullptr && warning->object != 0, "orca_reports_a_plate_wide_blocker_and_an_object_warning_together");
            if (blocker == nullptr) return;
            self->check(!blocker->setting.empty(), "orca_names_the_setting_instead");
            self->issue_overlay().select(blocker->id);
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.docked(), "a_plate_wide_issue_gets_a_docked_bubble_not_an_invented_object");
            self->capture_issues("15-plate-wide");
            self->answer_modals(wxID_YES);
            self->set_process_option("spiral_mode", new ConfigOptionBool(false));
            self->stop_answering_modals();
        }});

        // -- 13. Plate switch --------------------------------------------------
        steps.push_back({"overlap_again", [self] { self->move_object(1, Vec3d(-2, 0, 0)); }, {}, [self] {
            const Issue* warning = self->issue_from(IssueSource::Validation, IssueSeverity::Warning);
            self->check(warning != nullptr, "overlap_warning_back_on_plate_one");
            self->m_plate_one = self->issues().plate;
        }});
        steps.push_back({"switch_plate", [self] {
            PartPlateList& plates = self->m_plater->get_partplate_list();
            self->check(plates.create_plate(true) == 1, "second_plate_created");
            self->m_plater->select_plate(1);
        }, {}, [self] {
            self->check(self->issues().plate != self->m_plate_one, "issue_list_follows_the_active_plate");
            self->check(self->issue_from(IssueSource::Validation, IssueSeverity::Warning) == nullptr,
                        "plate_ones_warning_is_not_shown_as_plate_twos");
            self->capture_issues("16-plate-two");
        }});
        steps.push_back({"switch_back", [self] { self->m_plater->select_plate(0); }, {}, [self] {
            self->check(self->issues().plate == self->m_plate_one &&
                            self->issue_from(IssueSource::Validation, IssueSeverity::Warning) != nullptr,
                        "returning_to_plate_one_shows_its_warning_again");
        }});

        // -- 14. A slice warning on an object; several issues at once ----------
        steps.push_back({"overhang_fixture", [self] {
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "overhang_new_project");
            const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            const std::string stl  = self->write_overhang_stl();
            const auto strategy = LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence;
            self->check(self->m_plater->load_files(std::vector<std::string>{cube}, strategy, false).size() == 1, "overhang_cube_loaded");
            self->check(self->m_plater->load_files(std::vector<std::string>{stl}, strategy, false).size() == 1, "overhang_loaded");
            PartPlateList& plates = self->m_plater->get_partplate_list();
            self->check(plates.get_plate(0)->add_instance(0, 0, true) == 0, "overhang_cube_centred");
            self->check(plates.get_plate(0)->add_instance(1, 0, true) == 0, "overhang_centred");
            self->m_plater->canvas3D()->reload_scene(true, true);
            self->move_object(1, Vec3d(70, 0, 0));
        }, {}, [self] {
            self->issue_frame();
            self->check(self->issue_overlay().selected().empty(), "a_new_project_drops_the_old_projects_selection");
            self->check(self->issues().checked && self->issues().issues.empty(), "overhang_project_starts_clean");
        }});
        steps.push_back({"slice_overhang", [self] { installed_shell()->status_row()->request_slice(); }, [self] {
            PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
            return !self->m_plater->is_background_process_slicing() && self->issues().checked &&
                   (plate->is_slice_result_valid() || self->issue_from(IssueSource::SliceFailure, IssueSeverity::Blocker) != nullptr);
        }, [self] {
            const Issue* step = self->issue_from(IssueSource::SliceStep, IssueSeverity::Warning);
            self->check(step != nullptr && step->object == self->issue_object(1)->id().id && !step->stale,
                        "slicing_reports_a_current_warning_on_the_object");
            if (step != nullptr) {
                self->m_step_issue = step->id;
                self->issue_overlay().select(step->id);
                self->capture_issues("17-slice-warning");
            }
        }});
        // Check print is a second canvas inside the shell. OrcaSlicer's notices
        // must not come back there: the slice just left a warning of its own.
        steps.push_back({"check_print_notices", [self] {
            self->m_check_print_notices = -1;
            self->m_frame->CallAfter([self] {
                for (wxWindow* window : wxTopLevelWindows) {
                    auto* dialog = dynamic_cast<wxDialog*>(window);
                    if (dialog == nullptr || !dialog->IsModal())
                        continue;
                    for (wxWindow* child : dialog->GetChildren())
                        if (auto* preview = dynamic_cast<Preview*>(child); preview != nullptr && preview->get_canvas3d() != nullptr) {
                            self->m_check_print_stored = int(self->m_plater->get_notification_manager()->get_notification_count());
                            self->m_check_print_notices = self->drawn_notices(*preview->get_canvas3d());
                        }
                    dialog->EndModal(wxID_CANCEL);
                }
            });
            installed_shell()->status_row()->request_action(PrintAction::CheckPrint);
        }, {}, [self] {
            std::cerr << "HARNESS CHECKPRINT notices_drawn=" << self->m_check_print_notices << " notices_stored=" << self->m_check_print_stored << '\n';
            self->check(self->m_check_print_notices >= 0, "check_print_opened_with_a_preview_canvas");
            self->check(self->m_check_print_stored > 0, "orca_holds_notices_while_check_print_is_open");
            self->check(self->m_check_print_notices == 0, "orcas_notices_are_not_drawn_in_check_print_either");
        }});
        steps.push_back({"several_issues", [self] {
            // The overhang onto the cube, and a second cube over the plate's edge.
            self->move_object(1, Vec3d(-55, 0, 0));
            self->check(self->m_plater->duplicate_object(0) == 2, "several_third_object");
            const double plate_min = self->m_plater->get_partplate_list().get_curr_plate()->get_bounding_box().min.x();
            self->move_object(2, Vec3d(plate_min - self->issue_object(2)->instances[0]->get_offset().x() + 2, 0, 0));
        }, {}, [self] {
            std::map<std::uint64_t, int> per_object;
            for (const Issue& issue : self->issues().issues)
                ++per_object[issue.object];
            const std::uint64_t overhang = self->issue_object(1)->id().id;
            self->check(self->issues().issues.size() >= 3 && per_object.size() >= 2 && per_object[overhang] >= 2,
                        "two_objects_carry_issues_and_one_carries_two");
            const Issue* step = self->issues().find(self->m_step_issue);
            self->check(step != nullptr && !step->stale, "a_slice_warning_stays_current_after_a_move_that_does_not_invalidate_its_step");
            self->check(!self->issues().sliced, "the_slice_result_itself_is_no_longer_valid");
            self->issue_overlay().select(self->m_step_issue);
            self->issue_overlay().set_list_open(true);
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.group_size >= 2 && !frame.next.empty(), "one_bubble_pages_through_every_finding_on_its_object");
            self->check(frame.list_open && frame.rows.size() == self->issues().issues.size(), "the_list_shows_every_issue");
            self->m_group_first = frame.issue_id;
            self->capture_issues("18-several-issues");
        }});
        steps.push_back({"page_within_bubble", [self] { self->click_overlay(self->issue_frame().next); }, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id != self->m_group_first && frame.group_size >= 2,
                        "the_pager_shows_the_objects_other_finding");
            self->capture_issues("19-paged");
        }});
        steps.push_back({"pick_from_list", [self] {
            // The row of an issue on the other object.
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            const Issue* shown = self->issues().find(frame.issue_id);
            for (const auto& [id, rect] : frame.rows) {
                const Issue* row = self->issues().find(id);
                if (row != nullptr && shown != nullptr && row->object != shown->object) {
                    self->m_group_first = id;
                    self->click_overlay(rect);
                    return;
                }
            }
            self->check(false, "a_row_for_the_other_object_exists");
        }, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_group_first, "a_list_row_moves_the_bubble_to_its_object");
            const Issue* shown = self->issues().find(frame.issue_id);
            if (shown != nullptr) {
                const Selection& selection = self->prepare_canvas().get_selection();
                self->check(selection.get_object_idx() >= 0 &&
                                self->issue_object(selection.get_object_idx())->id().id == shown->object,
                            "a_list_row_highlights_its_object");
            }
            self->capture_issues("20-picked-from-list");
            self->issue_overlay().set_list_open(false);
        }});

        // -- Deleting the object an open bubble is on --------------------------
        steps.push_back({"delete_target", [self] {
            const Issue* shown = self->issues().find(self->issue_overlay().selected());
            if (shown == nullptr) { self->check(false, "bubble_open_before_delete"); return; }
            self->m_deleted_object = shown->object;
            self->m_deleted_issue  = shown->id;
            for (size_t index = 0; index < self->m_plater->model().objects.size(); ++index)
                if (self->issue_object(index)->id().id == shown->object) {
                    self->m_plater->delete_object_from_model(index);
                    break;
                }
        }, {}, [self] {
            self->check(self->m_count_right_after == 0, "a_deleted_objects_issues_go_at_once_even_before_revalidation");
            bool any = false;
            for (const Issue& issue : self->issues().issues)
                any = any || issue.object == self->m_deleted_object;
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(!any && !frame.bubble, "no_issue_or_bubble_survives_its_object");
            self->check(installed_shell()->submit_issue(self->m_deleted_issue, PrintIssues::IssueIntent::Resolve).outcome == "expired",
                        "an_issue_on_a_deleted_object_cannot_be_submitted");
            self->capture_issues("21-after-delete");
        }, [self] {
            self->issue_monitor().refresh_now();
            self->m_count_right_after = 0;
            for (const Issue& issue : self->issues().issues)
                self->m_count_right_after += issue.object == self->m_deleted_object ? 1 : 0;
        }});

        // -- A remedy, then a native re-check ----------------------------------
        steps.push_back({"enable_supports", [self] {
            self->answer_modals(wxID_YES);
            self->set_process_option("enable_support", new ConfigOptionBool(true));
        }, {}, [self] {
            self->stop_answering_modals();
            const Issue* step = self->issues().find(self->m_step_issue);
            std::cerr << "HARNESS SLICE supports_on step_issue=" << (step == nullptr ? "gone" : step->stale ? "stale" : "current") << '\n';
            self->check(step == nullptr || step->stale, "changing_the_setting_its_step_depends_on_stops_vouching_for_the_warning");
            self->check(installed_shell()->submit_issue(self->m_step_issue, PrintIssues::IssueIntent::Resolve).outcome !=  "started",
                        "a_stale_slice_warning_cannot_be_submitted");
            self->capture_issues("22-remedy-applied-not-yet-resliced");
        }});
        // -- A late result: the project is edited while its slice is running ----
        steps.push_back({"edit_during_slice", [self] { installed_shell()->status_row()->request_slice(); }, {}, [self] {
            std::cerr << "HARNESS LATE was_slicing_at_edit=" << self->m_was_slicing_at_edit << " sliced=" << self->issues().sliced
                      << " failure=" << (self->issue_from(IssueSource::SliceFailure, IssueSeverity::Blocker) != nullptr) << '\n';
            self->check(self->m_was_slicing_at_edit, "the_edit_landed_while_the_slice_was_running");
            // Whether such an edit cancels the slice is OrcaSlicer's call (a
            // plain move does not: it is folded into the running slice). The
            // monitor must report exactly what OrcaSlicer ended up with.
            PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
            self->check(self->issues().sliced == plate->is_slice_result_valid(), "the_monitor_reports_orcas_own_verdict_on_the_slice");
            self->check(self->issue_from(IssueSource::SliceFailure, IssueSeverity::Blocker) == nullptr,
                        "an_edit_during_a_slice_is_not_reported_as_a_failure");
            const Issue* step = self->issues().find(self->m_step_issue);
            self->check(step == nullptr || step->stale, "the_old_slices_warning_is_not_current_after_the_new_slice");
        }, [self] {
            self->m_was_slicing_at_edit = self->m_plater->is_background_process_slicing();
            self->move_object(0, Vec3d(0, 3, 0));
        }});
        steps.push_back({"reslice_with_supports", [self] { installed_shell()->status_row()->request_slice(); }, [self] {
            PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
            return !self->m_plater->is_background_process_slicing() && self->issues().checked &&
                   (plate->is_slice_result_valid() || self->issue_from(IssueSource::SliceFailure, IssueSeverity::Blocker) != nullptr);
        }, [self] {
            self->check(self->issues().sliced && self->issues().find(self->m_step_issue) == nullptr,
                        "only_the_new_slice_removes_the_warning");
            self->capture_issues("23-resliced");
        }});

        // -- 15. A slice that fails --------------------------------------------
        steps.push_back({"failing_fixture", [self] {
            self->answer_modals(wxID_NO);
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "failing_new_project");
            const std::string path = (fs::path(data_dir()) / "issues-flake.stl").string();
            {
                std::ofstream out(path);
                out << "solid flake\n";
                write_box(out, {-10, -10, 0}, {10, 10, 0.02}); // thinner than any layer
                out << "endsolid flake\n";
            }
            self->check(self->m_plater->load_files(std::vector<std::string>{path},
                            LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
                        "flake_loaded");
            self->check(self->m_plater->get_partplate_list().get_plate(0)->add_instance(0, 0, true) == 0, "flake_centred");
            self->m_plater->canvas3D()->reload_scene(true, true);
            self->m_plater->changed_object(0);
            self->stop_answering_modals();
        }, {}, {}});
        steps.push_back({"slice_fails", [self] {
            // Orca reports a critical slicing failure in a modal box; close it
            // so the run can continue. A non-critical one has no box.
            self->answer_modals(wxID_OK);
            self->m_slice_started = std::chrono::steady_clock::now();
            installed_shell()->status_row()->request_slice();
        }, [self] {
            return !self->m_plater->is_background_process_slicing() && self->issues().checked &&
                   std::chrono::steady_clock::now() - self->m_slice_started > std::chrono::seconds(3);
        }, [self] {
            self->stop_answering_modals();
            const Issue* failure = self->issue_from(IssueSource::SliceFailure, IssueSeverity::Blocker);
            const Issue* blocker = self->issue_from(IssueSource::Validation, IssueSeverity::Blocker);
            std::cerr << "HARNESS SLICE failing_fixture sliced=" << self->issues().sliced << " failure=" << (failure != nullptr)
                      << " validation_blocker=" << (blocker != nullptr) << '\n';
            self->check(!self->issues().sliced, "the_flake_does_not_produce_a_slice");
            self->check(failure != nullptr || blocker != nullptr, "the_reason_the_flake_cannot_print_is_in_the_issue_list");
            if (failure != nullptr) {
                self->m_failure_issue = failure->id;
                self->issue_overlay().select(failure->id);
                self->capture_issues("19-slice-failure");
            }
        }});
        steps.push_back({"edit_after_failure", [self] {
            if (!self->m_failure_issue.empty())
                self->move_object(0, Vec3d(5, 0, 0));
        }, {}, [self] {
            if (self->m_failure_issue.empty()) {
                std::cerr << "HARNESS SLICE no slice failure was produced; staleness of one is UNVERIFIED\n";
                return;
            }
            const Issue* failure = self->issues().find(self->m_failure_issue);
            self->check(failure != nullptr && failure->stale, "a_failure_goes_stale_when_the_project_changes");
        }});

        // -- 16. Project replacement -------------------------------------------
        steps.push_back({"replace_project", [self] {
            self->m_old_session = self->issues().session;
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "replacement_new_project");
        }, {}, [self] {
            self->check(self->issues().session != self->m_old_session && self->issues().issues.empty(),
                        "a_new_project_has_a_new_session_and_no_inherited_issues");
            self->check(installed_shell()->submit_issue(self->m_warning_id, PrintIssues::IssueIntent::Explain).outcome == "expired",
                        "an_issue_from_the_old_project_cannot_be_submitted");
            std::cerr << "HARNESS MONITOR validate_runs=" << self->issue_monitor().validate_runs() << '\n';
        }});

        run_issue_steps();
    }

    std::vector<IssueStep>    m_issue_steps;
    std::size_t               m_issue_step{0};
    RecordingAgent*           m_issue_agent{nullptr};
    std::string               m_warning_id, m_last_outcome, m_pill_right_after, m_group_first, m_deleted_issue, m_step_issue,
                              m_failure_issue;
    std::uint64_t             m_warning_copy{0}, m_deleted_object{0}, m_plate_one{0}, m_old_session{0};
    std::pair<float, float>   m_last_anchor{0, 0}, m_anchor_before{0, 0};
    std::size_t               m_user_messages_before{0}, m_requests_before{0}, m_count_right_after{0};
    int                       m_check_print_notices{-1}, m_check_print_stored{0}, m_modals_answered{0}, m_modals_before{0};
    std::string               m_last_modal_title;
    const wxDialog*           m_modal_pictured{nullptr};
    bool                      m_checked_right_after{false}, m_stale_right_after{false}, m_buttons_right_after{false}, m_was_slicing_at_edit{false};
    bool                      m_modal_closer_bound{false};
    int                       m_modal_answer{wxID_OK};
    wxEvtHandler              m_modal_closer;
    wxTimer                   m_modal_closer_timer{&m_modal_closer};
    std::chrono::steady_clock::time_point m_slice_started;
