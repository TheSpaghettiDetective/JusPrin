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

    // A click on the canvas where there is no object, no plate and nothing of
    // the overlay: the left edge, halfway down. The view must be the whole
    // plate ("iso", zoom_to_bed), which leaves that margin free.
    PrintIssues::OverlayRect empty_canvas_spot()
    {
        return {8.f, float(prepare_canvas().get_canvas_size().get_height()) / 2, 4.f, 4.f};
    }

    // Escape, typed with the canvas focused.
    void press_canvas_escape()
    {
        wxGLCanvas* target = prepare_canvas().get_wxglcanvas();
        wxKeyEvent event(wxEVT_CHAR);
        event.SetEventObject(target);
        event.m_keyCode = WXK_ESCAPE;
        target->GetEventHandler()->ProcessEvent(event);
    }

    // The button held down on the empty canvas and moved: how the view is turned.
    void drag_on_empty_canvas()
    {
        prepare_canvas().render();
        const PrintIssues::OverlayRect spot = empty_canvas_spot();
        const double scale = prepare_canvas().get_scale();
        const wxPoint from(int(spot.x / scale), int(spot.y / scale));
        send_canvas_mouse(wxEVT_MOTION, from, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, from, true);
        for (int step = 1; step <= 4; ++step)
            send_canvas_mouse(wxEVT_MOTION, from + wxPoint(10 * step, 6 * step), true);
        send_canvas_mouse(wxEVT_LEFT_UP, from + wxPoint(40, 24), false);
    }

    // Whether OrcaSlicer's selection is the issue's copy, and nothing else.
    bool selection_is(const Issue& issue)
    {
        const Selection& selection = prepare_canvas().get_selection();
        const int object_index = selection.get_object_idx();
        return selection.is_single_full_instance() && object_index >= 0 && issue_object(object_index)->id().id == issue.object &&
               (issue.instance == 0 || issue_object(object_index)->instances[selection.get_instance_idx()]->id().id == issue.instance);
    }

    std::size_t ignored_count()
    {
        std::size_t count = 0;
        for (const Issue& issue : issues().issues)
            count += issue.ignored ? 1 : 0;
        return count;
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
                        if (m_modal_gate && !m_modal_gate(*dialog))
                            return;
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
    void stop_answering_modals()
    {
        m_modal_closer_timer.Stop();
        m_modal_gate = {};
    }

    // Answers the next Preferences window as a person who looked at it does:
    // once its page has loaded, after `inside` has done what they did there,
    // it is closed. Records the section it opened on. Any other dialog is
    // answered as answer_modals answers it.
    void close_preferences_when_loaded(std::function<void()> inside = {})
    {
        m_preferences_section.reset();
        m_modal_gate = [this, inside = std::move(inside)](wxDialog& dialog) {
            auto* window = dynamic_cast<Preferences::PreferencesWindow*>(&dialog);
            if (window == nullptr)
                return true;
            if (!window->page_connected())
                return false;
            m_preferences_section = window->request().section;
            if (inside)
                inside();
            return true;
        };
        m_modals_before = m_modals_answered;
        answer_modals(wxID_CANCEL);
    }

    // The window was answered and is gone: open_preferences has returned and
    // has done what it does once the window closes. Timers fire inside the
    // window's own loop too, so being answered is not enough.
    bool preferences_closed() const
    {
        if (m_modals_answered <= m_modals_before)
            return false;
        for (wxWindow* window : wxTopLevelWindows)
            if (dynamic_cast<Preferences::PreferencesWindow*>(window) != nullptr)
                return false;
        return true;
    }

    // What checking a key in Preferences › Agent leaves in the config: the
    // agent on, consent given, a key stored. The key reaches no provider; the
    // run's endpoint is a closed local port. Returns what was there before.
    static std::map<std::string, std::string> store_placeholder_agent_key()
    {
        AppConfig& config = *wxGetApp().app_config;
        const std::map<std::string, std::string> before =
            config.has_section("jusprin_agent") ? config.get_section("jusprin_agent") : std::map<std::string, std::string>{};
        // Strings by type: AppConfig::set takes a bare literal as a bool.
        config.set("jusprin_agent", "openai_api_key", std::string("harness-placeholder-key"));
        config.set("jusprin_agent", "provider", std::string("openai"));
        config.set("jusprin_agent", "enabled", std::string("true"));
        config.set("jusprin_agent", "cloud_consent", std::string("true"));
        return before;
    }

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
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.pill.empty() && frame.pill_text.empty(), "with_no_issue_there_is_no_pill");
            self->check(self->issue_overlay().announcements() == 0, "nothing_is_announced_on_a_clean_plate");
            self->capture_issues("01-clean");
        }});

        steps.push_back({"overlap_warning", [self] { self->move_object(1, Vec3d(-50, 0, 0)); }, [self] {
            if (!self->orca_caught_up() || self->issues().issues.empty())
                return false;
            // The frame that first shows the issue starts the ring; a little
            // later it is partway out.
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            if (!self->m_ring_started) {
                self->m_ring_started = std::chrono::steady_clock::now();
                return false;
            }
            if (std::chrono::steady_clock::now() - *self->m_ring_started < std::chrono::milliseconds(300))
                return false;
            self->m_ring_seen = frame.attention;
            self->capture_issues("01b-new-issue-announced");
            return true;
        }, [self] {
            const PrintIssues::IssueOverlayFrame arrived = self->issue_frame();
            self->check(!arrived.pill.empty() && arrived.pill_text == "1 print issue", "the_pill_appears_with_the_first_issue");
            self->check(self->m_ring_seen && self->issue_overlay().announcements() == 1, "a_new_issue_is_announced_with_a_ring_on_the_pill");
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

        steps.push_back({"announcement_ends", {}, [self] { return !self->issue_frame().attention; }, [self] {
            self->check(self->issue_overlay().announcements() == 1, "the_ring_runs_once_and_stops");
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
            self->check(self->issue_overlay().announcements() == 1 && !self->issue_frame().attention,
                        "an_edit_that_keeps_the_issue_announces_nothing");
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

        // -- 2b. Closing the bubble ---------------------------------------------
        // Closing hides it: the issue, the count and the selection are as they were.
        steps.push_back({"close_button", [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && !frame.close.empty(), "the_bubble_has_a_close_button");
            self->check(!rects_overlap(frame.close, frame.ask) && frame.close.x >= frame.bubble_rect.x && frame.close.x + frame.close.w <= frame.bubble_rect.x + frame.bubble_rect.w &&
                            frame.close.y >= frame.bubble_rect.y,
                        "the_close_button_is_inside_the_bubble_and_clear_of_the_buttons");
            self->m_pill_right_after = frame.pill_text;
            self->click_overlay(frame.close);
        }, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(!frame.bubble && self->issue_overlay().selected().empty(), "the_close_button_closes_the_bubble");
            self->check(warning != nullptr && frame.pill_text == self->m_pill_right_after, "closing_changes_neither_the_issue_nor_the_count");
            self->check(warning != nullptr && self->selection_is(*warning), "the_close_button_leaves_the_object_selected");
            self->capture_issues("06b-closed");
        }});
        // One issue: the pill is the way back, with no list of one in between.
        steps.push_back({"pill_opens_the_only_issue", [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(self->issues().issues.size() == 1 && !frame.pill_opens_list, "with_one_issue_the_pill_offers_no_list");
            self->click_overlay(frame.pill);
        }, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_warning_id && !frame.list_open, "with_one_issue_the_pill_opens_its_bubble");
            self->check(frame.pager_size <= 1 && frame.next.empty() && frame.previous.empty(), "one_issue_has_no_pager");
            self->capture_issues("06c-reopened-from-pill");
            self->click_overlay(frame.pill);
        }});
        steps.push_back({"pill_closes_it_again", {}, {}, [self] {
            self->check(!self->issue_frame().bubble, "a_second_press_on_the_pill_closes_the_bubble");
        }});
        // The bubble belongs to the selection: letting go of the object closes it.
        steps.push_back({"escape_closes_the_bubble", [self] {
            self->issue_overlay().select(self->m_warning_id);
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(self->issue_frame().bubble && warning != nullptr && self->selection_is(*warning), "bubble_open_before_escape");
            self->press_canvas_escape();
        }, {}, [self] {
            self->check(self->prepare_canvas().get_selection().is_empty(), "escape_deselects_as_it_does_in_orca");
            self->check(!self->issue_frame().bubble && self->issue_overlay().selected().empty(), "escape_closes_the_bubble");
            self->check(self->issues().find(self->m_warning_id) != nullptr, "escape_leaves_the_issue");
        }});
        steps.push_back({"turning_the_view_keeps_the_bubble", [self] {
            self->issue_overlay().select(self->m_warning_id);
            self->issue_frame();
            self->drag_on_empty_canvas();
        }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(warning != nullptr && self->selection_is(*warning), "a_drag_on_the_empty_canvas_keeps_the_selection");
            self->check(self->issue_frame().bubble, "a_drag_on_the_empty_canvas_keeps_the_bubble");
            self->prepare_canvas().select_view("iso");
            self->prepare_canvas().zoom_to_bed();
        }});
        steps.push_back({"empty_click_closes_the_bubble", [self] {
            self->check(self->issue_frame().bubble, "bubble_open_before_the_empty_click");
            self->click_overlay(self->empty_canvas_spot());
        }, {}, [self] {
            self->check(self->prepare_canvas().get_selection().is_empty(), "a_click_on_the_empty_canvas_deselects_as_it_does_in_orca");
            self->check(!self->issue_frame().bubble && self->issue_overlay().selected().empty(), "a_click_on_the_empty_canvas_closes_the_bubble");
        }});
        steps.push_back({"another_object_closes_the_bubble", [self] {
            self->issue_overlay().select(self->m_warning_id);
            self->check(self->issue_frame().bubble, "bubble_open_before_the_other_object");
            // A click on the other cube, on the side away from the warning's.
            const Issue* warning = self->issues().find(self->m_warning_id);
            for (size_t index = 0; warning != nullptr && index < self->m_plater->model().objects.size(); ++index) {
                const ModelObject* other = self->issue_object(index);
                if (other->id().id == warning->object)
                    continue;
                const auto mine   = self->project_copy_rect(warning->object, warning->instance);
                const auto theirs = self->project_copy_rect(other->id().id, other->instances[0]->id().id);
                self->check(mine.has_value() && theirs.has_value(), "both_cubes_are_on_screen");
                if (mine && theirs)
                    self->click_overlay({theirs->x + theirs->w * (theirs->x < mine->x ? 0.2f : 0.8f) - 2.f, theirs->y + theirs->h / 2 - 2.f, 4.f, 4.f});
                break;
            }
        }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(warning != nullptr && !self->prepare_canvas().get_selection().is_empty() && !self->selection_is(*warning),
                        "the_other_object_is_selected");
            self->check(!self->issue_frame().bubble && self->issue_overlay().selected().empty(), "selecting_another_object_closes_the_bubble");
            self->issue_overlay().select(self->m_warning_id);
        }});

        // -- 3. OrcaSlicer's own notices are not drawn, and they are the list ------
        steps.push_back({"native_notices", [self] {
            NotificationManager& notices = *self->m_plater->get_notification_manager();
            // Not drawn, but alive underneath: OrcaSlicer pushed its warning.
            self->check(notices.get_notification_count() > 0, "orca_still_keeps_its_own_notices_underneath");
            self->check(self->drawn_notices(self->prepare_canvas()) == 0, "orcas_notices_are_not_drawn_while_the_shell_is_installed");
            self->capture_issues("07-native-notices-hidden");
            // OrcaSlicer's list is the source of truth: when it lets go of
            // the warning, so does the issue list.
            notices.close_notification_of_type(NotificationType::ValidateWarning);
        }, {}, [self] {
            self->issue_monitor().refresh_now();
            self->check(self->issues().find(self->m_warning_id) == nullptr, "an_issue_goes_when_orca_lets_go_of_its_notice");
        }});
        // OrcaSlicer says it again the next time an edit makes it validate,
        // and the same issue is back on the same copy.
        steps.push_back({"notice_returns", [self] { self->move_object(1, Vec3d(0, 1, 0)); }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(warning != nullptr && warning->instance == self->m_warning_copy, "the_issue_returns_with_orcas_next_validation");
            self->issue_frame();
            self->check(self->issue_overlay().announcements() == 2, "an_issue_that_comes_back_is_announced_again");
            // The bubble went with the issue; the person picks it again.
            self->issue_overlay().select(self->m_warning_id);
            std::cerr << "HARNESS MONITOR comparisons=" << self->issue_monitor().comparisons() << " rebuilds=" << self->issue_monitor().rebuilds()
                      << " window_active=" << self->m_frame->IsActive() << '\n';
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
            self->check(context.is_object() && context["issue"].value("id", "") == self->m_warning_id &&
                            context["target"].value("copyId", std::uint64_t(0)) == self->m_warning_copy,
                        "ask_context_names_the_issue_and_the_copy_by_id");
            self->check(context.is_object() && context["workspace"].value("sessionId", "") == std::to_string(workspace.session.value()) &&
                            context["workspace"].value("revision", std::uint64_t(0)) == workspace.revision,
                        "ask_context_carries_the_current_session_and_revision");
            self->check(self->m_issue_agent->requests.size() == self->m_requests_before + 1, "ask_started_one_agent_request");
            if (self->m_issue_agent->requests.size() > self->m_requests_before) {
                const Agent::AgentRequest& request = self->m_issue_agent->requests.back();
                // The context is its own block beside the person's words
                // (36d33312ed), and the request carries the print-issues skill.
                self->check(request.user_text.rfind("Explain this issue and my options.", 0) == 0 &&
                                request.issue_context.find(self->m_warning_id) != std::string::npos,
                            "ask_request_gives_the_model_the_structured_context");
                self->check(std::any_of(request.skills.begin(), request.skills.end(),
                                        [](const Agent::AgentSkillUse& skill) { return skill.name == "print-issues"; }),
                            "ask_request_carries_the_print_issues_skill");
                self->check(request.session.tool_names.empty(), "ask_turn_keeps_the_ordinary_tool_set");
                self->check(request.workspace.revision == workspace.revision, "ask_request_carries_the_workspace_snapshot");
            }
            self->check(self->persistence().draft() == "half-typed question", "ask_leaves_the_composer_draft_alone");
            self->check(!installed_shell()->is_agent_pane_collapsed(), "ask_opens_the_closed_chat_pane");
            self->check(!self->issue_frame().bubble && self->issue_overlay().selected().empty(), "ask_closes_the_bubble_once_the_chat_has_it");
            self->check(self->issues().find(self->m_warning_id) != nullptr, "ask_leaves_the_issue");
            self->capture_issues("08-asked");
            self->capture_chat_pane("08-asked-chat");
        }});

        steps.push_back({"ask_again_unchanged", [self] {
            self->m_user_messages_before = self->user_message_count();
            self->m_last_outcome = installed_shell()->submit_issue(self->m_warning_id, "Explain this issue and my options.").outcome;
        }, {}, [self] {
            std::cerr << "HARNESS CHAT ask_again outcome=" << self->m_last_outcome << '\n';
            self->check(self->m_last_outcome == "duplicate" && self->user_message_count() == self->m_user_messages_before,
                        "second_press_with_nothing_changed_adds_no_second_message");
        }});

        // -- 5. The busy path -------------------------------------------------
        // Ask AI with nothing changed is the same message (above), so a turn
        // to hold open is started with another sentence about the issue.
        steps.push_back({"issue_turn_held_open", [self] {
            self->m_issue_agent->hold    = true; // keep the reply open to see the busy state
            self->m_user_messages_before = self->user_message_count();
            self->m_requests_before      = self->m_issue_agent->requests.size();
            self->m_last_outcome = installed_shell()->submit_issue(self->m_warning_id, "Is this still a problem?").outcome;
        }, [self] { return self->user_message_count() > self->m_user_messages_before; }, [self] {
            self->check(self->m_last_outcome == "started", "another_question_about_the_issue_starts_a_turn");
            self->check(self->m_issue_agent->requests.size() == self->m_requests_before + 1 &&
                            self->m_issue_agent->requests.back().session.tool_names.empty(),
                        "an_issue_turn_keeps_the_ordinary_tool_set_and_no_more");
            // While the reply is open a bubble opened again has its button
            // off, and a press that got through anyway is refused.
            self->check(self->project_host().busy(), "agent_is_busy_with_the_issue_turn");
            self->issue_overlay().select(self->m_warning_id);
            const PrintIssues::IssueOverlayFrame busy_frame = self->issue_frame();
            self->check(busy_frame.bubble && !busy_frame.actions_enabled, "buttons_are_disabled_while_a_reply_is_running");
            const std::size_t before = self->user_message_count();
            self->check(installed_shell()->submit_issue(self->m_warning_id, "Explain this issue and my options.").outcome == "busy" &&
                            self->user_message_count() == before,
                        "a_press_during_a_reply_sends_nothing");
            // The host itself queues a message that arrives mid-reply, as it
            // does for one from the page.
            self->check(self->project_host().submit_issue_message("issues-queued", "Queued while busy.", "{}") ==
                            Agent::AgentHost::SubmitResult::Queued,
                        "host_queues_a_message_that_arrives_mid_reply");
            self->capture_issues("09-issue-turn-busy");
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

        // -- 5b. Ignoring a warning ---------------------------------------------
        steps.push_back({"ignore_warning", [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_warning_id && !frame.ignore.empty(), "a_warning_offers_ignore");
            self->check(!rects_overlap(frame.ignore, frame.ask) && frame.ignore.y + frame.ignore.h <= frame.bubble_rect.y + frame.bubble_rect.h,
                        "ignore_sits_inside_the_bubble_under_the_buttons");
            self->m_generation_before    = self->issues().generation;
            self->m_announcements_before = self->issue_overlay().announcements();
            self->capture_issues("09c-ignore-offered");
            self->click_overlay(frame.ignore);
        }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            std::cerr << "HARNESS IGNORE pill='" << frame.pill_text << "' ignored=" << self->ignored_count() << '\n';
            self->check(warning != nullptr && warning->ignored, "an_ignored_warning_stays_in_the_list_marked");
            self->check(self->issues().generation > self->m_generation_before, "ignoring_is_a_change_listeners_hear_about");
            self->check(frame.pill_text == "1 warning ignored", "the_pill_says_what_was_ignored_not_that_all_is_clear");
            self->check(!frame.bubble, "ignoring_closes_the_bubble");
            self->check(self->m_plater->get_notification_manager()->get_notification_count() > 0, "orca_still_holds_its_own_warning");
            self->check(installed_shell()->status_row()->action_state().can_slice, "ignoring_changes_nothing_about_slicing");
            self->capture_issues("09d-ignored");
        }});
        steps.push_back({"ignored_survives_an_edit", [self] { self->move_object(1, Vec3d(0, -1, 0)); }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(warning != nullptr && !warning->stale && warning->ignored, "an_edit_that_keeps_the_warning_keeps_it_ignored");
        }});
        steps.push_back({"ignored_bubble", [self] { self->click_overlay(self->issue_frame().pill); }, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_warning_id && !frame.ignore.empty(), "the_pill_still_leads_to_an_ignored_warning");
            self->capture_issues("09e-ignored-bubble");
            self->click_overlay(frame.ignore);
        }});
        steps.push_back({"stop_ignoring", {}, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(warning != nullptr && !warning->ignored && frame.pill_text == "1 print issue", "stop_ignoring_counts_the_warning_again");
            self->check(frame.bubble && frame.issue_id == self->m_warning_id, "stop_ignoring_keeps_the_bubble_open");
            self->check(self->issue_overlay().announcements() == self->m_announcements_before,
                        "ignoring_and_its_undo_announce_nothing");
        }});
        // A slice keeps the ignore, and what the slice itself finds is counted
        // on its own, above the ignored warning in the list.
        steps.push_back({"ignored_survives_a_slice", [self] {
            self->issue_monitor().set_ignored(self->m_warning_id, true);
            self->check(self->ignored_count() == 1, "ignored_again");
            installed_shell()->status_row()->request_slice();
        }, [self] {
            return !self->m_plater->is_background_process_slicing() && self->issues().checked &&
                   self->m_plater->get_partplate_list().get_curr_plate()->is_slice_result_valid();
        }, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(warning != nullptr && warning->ignored, "a_slice_keeps_the_warning_ignored");
            self->check(self->issues().issues.size() == 2 && self->ignored_count() == 1, "the_slice_reports_a_finding_of_its_own");
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.pill_text == "1 print issue" && frame.pill_opens_list, "the_pill_counts_only_what_is_not_ignored");
            self->click_overlay(frame.pill);
        }});
        steps.push_back({"ignored_is_last_in_the_list", {}, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.list_open && frame.rows.size() == 2 && frame.rows.back().first == self->m_warning_id &&
                            frame.rows.front().second.y < frame.rows.back().second.y,
                        "an_ignored_warning_is_listed_below_the_ones_that_count");
            self->capture_issues("09f-ignored-in-list");
            self->click_overlay(frame.rows.front().second);
        }});
        steps.push_back({"ignored_is_off_the_pager", {}, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id != self->m_warning_id && frame.pager_size == 1 && frame.next.empty(),
                        "the_pager_does_not_walk_onto_an_ignored_warning");
        }});
        // An ignore lasts while OrcaSlicer keeps reporting the warning.
        steps.push_back({"ignored_then_fixed", [self] {
            self->move_object(1, Vec3d(60, 0, 0));
        }, {}, [self] {
            self->check(self->issues().checked && self->issues().issues.empty(), "the_fix_clears_the_ignored_warning");
            self->check(self->issue_frame().pill.empty(), "with_nothing_reported_the_pill_is_gone");
        }});
        steps.push_back({"warning_returns_unignored", [self] { self->move_object(1, Vec3d(-60, 0, 0)); }, {}, [self] {
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->check(warning != nullptr && !warning->ignored, "a_warning_that_comes_back_is_shown_again");
            self->issue_overlay().select(self->m_warning_id);
        }});

        // -- 6. Agent unavailable: the status still works ----------------------
        // With no agent the bubble has one button, which opens Preferences on
        // its Agent section. Closing the window without an agent changes
        // nothing; closing it with one brings Ask AI back, and
        // nothing is sent for the person either way.
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
            const auto submission = installed_shell()->submit_issue(self->m_warning_id, "Explain this issue and my options.");
            std::cerr << "HARNESS CHAT unavailable outcome=" << submission.outcome << '\n';
            self->check(submission.outcome == "agent_not_set_up" && self->user_message_count() == self->m_user_messages_before,
                        "with_no_agent_nothing_is_sent_to_the_chat");
            installed_shell()->set_agent_pane_collapsed(true);
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && !frame.set_up.empty() && frame.ask.empty(),
                        "with_no_agent_the_bubble_offers_only_setup");
            self->check(std::abs(frame.set_up.w - (frame.bubble_rect.w - 2 * (frame.set_up.x - frame.bubble_rect.x))) < 1.f,
                        "the_setup_button_spans_the_bubble");
            self->capture_issues("06a-no-agent");
            self->close_preferences_when_loaded();
            self->click_overlay(frame.set_up);
        }});
        steps.push_back({"no_agent_preferences_cancelled", {}, [self] { return self->preferences_closed(); }, [self] {
            self->check(self->m_preferences_section == std::optional<std::string>("agent"),
                        "setup_opens_preferences_on_its_agent_section");
            self->check(installed_shell()->is_agent_pane_collapsed() && self->user_message_count() == self->m_user_messages_before,
                        "closing_preferences_without_an_agent_changes_nothing");
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_warning_id && !frame.set_up.empty(),
                        "the_bubble_still_offers_setup_after_a_cancel");
            // This time the person checks a key in the window before closing it.
            self->close_preferences_when_loaded([self] { self->m_agent_settings_before = store_placeholder_agent_key(); });
            self->click_overlay(frame.set_up);
        }});
        steps.push_back({"no_agent_preferences_set_up", {}, [self] { return self->preferences_closed(); }, [self] {
            self->stop_answering_modals();
            self->check(self->project_host().availability() == Agent::AgentAvailability::Ready,
                        "closing_preferences_with_an_agent_reloads_the_chat");
            if (self->project_host().availability() != Agent::AgentAvailability::Ready)
                std::cerr << "HARNESS DETAIL agent after Preferences: "
                          << Agent::load_agent_runtime(wxGetApp().app_config).unavailable_reason << '\n';
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_warning_id && frame.set_up.empty() && !frame.ask.empty(),
                        "ask_ai_returns_once_an_agent_is_there");
            self->check(installed_shell()->is_agent_pane_collapsed() && self->user_message_count() == self->m_user_messages_before,
                        "setting_the_agent_up_sends_nothing_for_the_person");
            self->capture_issues("06b-agent-set-up");
            // The rest of the run talks to the stand-in again.
            wxGetApp().app_config->set_section("jusprin_agent", self->m_agent_settings_before);
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
            self->m_last_outcome = installed_shell()->submit_issue(self->m_warning_id, "Explain this issue and my options.").outcome;
            self->m_checked_right_after = self->issues().checked;
            const Issue* warning = self->issues().find(self->m_warning_id);
            self->m_stale_right_after = warning != nullptr && warning->stale;
        }});

        steps.push_back({"move_apart", [self] { self->move_object(1, Vec3d(60, 0, 0)); }, {}, [self] {
            std::cerr << "HARNESS CLEAR right_after checked=" << self->m_checked_right_after << " count=" << self->m_count_right_after
                      << " pill='" << self->m_pill_right_after << "'\n";
            // The pill going is the all-clear, so it must not go early.
            self->check(!self->m_checked_right_after && !self->m_pill_right_after.empty(),
                        "the_pill_stays_until_orca_revalidates_the_fix");
            self->check(self->issues().checked && self->issues().issues.empty(), "native_revalidation_clears_the_issue");
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(!frame.bubble && self->issue_overlay().selected().empty(), "bubble_is_removed_when_the_issue_clears");
            self->check(installed_shell()->submit_issue(self->m_warning_id, "Explain this issue and my options.").outcome == "expired",
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
                self->check(self->issue_frame().bubble && self->issue_frame().ignore.empty(), "a_blocker_offers_no_ignore");
                self->issue_monitor().set_ignored(blocker->id, true);
                self->check(self->ignored_count() == 0, "a_blocker_cannot_be_ignored");
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
            // No object to let go of: Escape closes the bubble itself, and
            // what happens to be selected stays selected.
            self->m_plater->select_object(0);
            self->check(self->issue_frame().bubble, "a_plate_wide_bubble_does_not_follow_the_selection");
            self->press_canvas_escape();
            self->check(!self->issue_frame().bubble, "escape_closes_a_plate_wide_bubble");
            self->check(!self->prepare_canvas().get_selection().is_empty(), "that_escape_is_spent_and_deselects_nothing");
            self->issue_overlay().select(blocker->id);
            self->click_overlay(self->issue_frame().close);
        }});
        steps.push_back({"plate_wide_close_button", {}, {}, [self] {
            self->check(!self->issue_frame().bubble, "the_close_button_closes_a_plate_wide_bubble");
            self->answer_modals(wxID_YES);
            self->set_process_option("spiral_mode", new ConfigOptionBool(false));
            self->stop_answering_modals();
        }});

        // -- 13. Plate switch --------------------------------------------------
        steps.push_back({"overlap_again", [self] { self->move_object(1, Vec3d(-2, 0, 0)); }, {}, [self] {
            const Issue* warning = self->issue_from(IssueSource::Validation, IssueSeverity::Warning);
            self->check(warning != nullptr, "overlap_warning_back_on_plate_one");
            self->m_plate_one = self->issues().plate;
            if (warning != nullptr)
                self->issue_monitor().set_ignored(warning->id, true);
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
            const Issue* warning = self->issue_from(IssueSource::Validation, IssueSeverity::Warning);
            self->check(warning != nullptr && warning->ignored, "looking_at_another_plate_does_not_forget_an_ignore");
            if (warning != nullptr)
                self->issue_monitor().set_ignored(warning->id, false);
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
        steps.push_back({"slice_overhang", [self] {
            // The first workspace change that shows the plate sliced is the
            // earliest a slice_start result could be built: the summary must
            // already be there.
            self->m_warnings_at_slice = -1;
            self->m_warnings_probe = installed_shell()->workspace()->subscribe([self](const Workspace::WorkspaceChanged&) {
                if (self->m_warnings_at_slice != -1)
                    return;
                const auto workspace = installed_shell()->workspace()->snapshot();
                for (const auto& plate : workspace.plates)
                    if (plate.active && plate.sliced && !workspace.slicing.running)
                        self->m_warnings_at_slice = plate.slice_warnings ? 1 : 0;
            });
            installed_shell()->status_row()->request_slice();
        }, [self] {
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
            self->m_warnings_probe.reset();
            std::cerr << "HARNESS ISSUES warnings at the first sliced change: " << self->m_warnings_at_slice << '\n';
            self->check(self->m_warnings_at_slice == 1, "the_first_sliced_change_already_summarizes_the_warnings");
            // The workspace the chat reads carries the warning too: the
            // count, and the first in OrcaSlicer's words with its object.
            const Workspace::WorkspaceSnapshot workspace = installed_shell()->workspace()->snapshot();
            const auto active = std::find_if(workspace.plates.begin(), workspace.plates.end(),
                                             [](const Workspace::WorkspacePlate& plate) { return plate.active; });
            const bool summarized = active != workspace.plates.end() && active->slice_warnings.has_value();
            self->check(summarized, "the_workspace_summarizes_the_slice_warnings");
            if (summarized && step != nullptr) {
                const Workspace::SliceWarnings& warnings = *active->slice_warnings;
                std::cerr << "HARNESS ISSUES slice warnings count=" << warnings.count << " severity=" << warnings.severity
                          << " message='" << warnings.message << "' object=" << warnings.object_name << '\n';
                self->check(warnings.count >= 1 && warnings.message == step->message &&
                                warnings.object && warnings.object->value() == step->object,
                            "the_summary_names_the_warning_and_its_object");
                // Read again with nothing new: the same record, the same number.
                const auto again = installed_shell()->workspace()->snapshot();
                const auto plate = std::find_if(again.plates.begin(), again.plates.end(),
                                                [](const Workspace::WorkspacePlate& candidate) { return candidate.active; });
                self->check(plate != again.plates.end() && plate->slice_warnings &&
                                plate->slice_warnings->sequence == warnings.sequence,
                            "reading_again_keeps_the_warnings_sequence");
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
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.pager_size == int(self->issues().issues.size()) && !frame.next.empty() && !frame.previous.empty(),
                        "the_bubble_pages_through_every_issue");
            self->check(!rects_overlap(frame.next, frame.previous) && !rects_overlap(frame.next, frame.ask) &&
                            !rects_overlap(frame.previous, frame.ignore) &&
                            frame.next.y + frame.next.h <= frame.bubble_rect.y + frame.bubble_rect.h,
                        "the_pager_sits_inside_the_bubble_clear_of_the_other_controls");
            self->check(frame.pill_opens_list, "with_several_issues_the_pill_opens_a_list");
            self->m_group_first = frame.issue_id;
            self->m_pager_first = frame.pager_index;
            self->capture_issues("18-several-issues");
            self->click_overlay(frame.next);
        }});
        steps.push_back({"page_forward", {}, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id != self->m_group_first &&
                            frame.pager_index == (self->m_pager_first + 1) % frame.pager_size,
                        "the_pager_moves_to_the_next_issue");
            const Issue* shown = self->issues().find(frame.issue_id);
            self->check(shown != nullptr && (shown->object == 0 || self->selection_is(*shown)), "paging_selects_the_next_issues_object");
            self->capture_issues("19-paged");
            self->click_overlay(frame.previous);
        }});
        steps.push_back({"page_back", {}, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_group_first, "the_pager_moves_back");
            self->click_overlay(frame.pill);
        }});
        steps.push_back({"list_opens", {}, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.list_open && frame.rows.size() == self->issues().issues.size(), "the_list_shows_every_issue");
            self->check(!frame.bubble, "opening_the_list_closes_the_bubble");
            self->check(!self->prepare_canvas().get_selection().is_empty(), "opening_the_list_leaves_the_selection");
            self->capture_issues("19b-list");
            self->press_canvas_escape();
        }});
        steps.push_back({"escape_closes_the_list", {}, {}, [self] {
            self->check(!self->issue_frame().list_open, "escape_closes_the_list");
            self->check(!self->prepare_canvas().get_selection().is_empty(), "that_escape_is_spent_and_deselects_nothing");
            self->click_overlay(self->issue_frame().pill);
        }});
        steps.push_back({"pill_closes_the_list", {}, {}, [self] {
            self->check(self->issue_frame().list_open, "the_pill_opens_the_list_again");
            self->click_overlay(self->issue_frame().pill);
        }});
        steps.push_back({"pill_opens_the_list_once_more", {}, {}, [self] {
            self->check(!self->issue_frame().list_open, "a_second_press_on_the_pill_closes_the_list");
            self->click_overlay(self->issue_frame().pill);
        }});
        steps.push_back({"press_outside_closes_the_list", {}, {}, [self] {
            self->check(self->issue_frame().list_open, "list_open_before_the_press_outside");
            self->click_overlay(self->empty_canvas_spot());
            self->check(!self->issue_frame().list_open, "a_press_outside_closes_the_list");
            self->click_overlay(self->issue_frame().pill);
        }});
        steps.push_back({"list_open_for_the_pick", {}, {}, [self] {
            self->check(self->issue_frame().list_open, "list_open_before_the_pick");
        }});
        steps.push_back({"pick_from_list", [self] {
            // The row of an issue on another object than the last one shown.
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            const Issue* shown = self->issues().find(self->m_group_first);
            for (const auto& [id, rect] : frame.rows) {
                const Issue* row = self->issues().find(id);
                if (row != nullptr && shown != nullptr && row->object != 0 && row->object != shown->object) {
                    self->m_group_first = id;
                    self->click_overlay(rect);
                    return;
                }
            }
            self->check(false, "a_row_for_the_other_object_exists");
        }, {}, [self] {
            const PrintIssues::IssueOverlayFrame frame = self->issue_frame();
            self->check(frame.bubble && frame.issue_id == self->m_group_first, "a_list_row_opens_the_bubble_on_its_object");
            self->check(!frame.list_open, "picking_a_row_closes_the_list");
            const Issue* shown = self->issues().find(frame.issue_id);
            self->check(shown != nullptr && self->selection_is(*shown), "a_list_row_highlights_its_object");
            self->capture_issues("20-picked-from-list");
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
            self->check(installed_shell()->submit_issue(self->m_deleted_issue, "Explain this issue and my options.").outcome == "expired",
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
            self->check(installed_shell()->submit_issue(self->m_step_issue, "Explain this issue and my options.").outcome !=  "started",
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
            self->check(installed_shell()->submit_issue(self->m_warning_id, "Explain this issue and my options.").outcome == "expired",
                        "an_issue_from_the_old_project_cannot_be_submitted");
            std::cerr << "HARNESS MONITOR validate_runs=" << self->issue_monitor().validate_runs() << '\n';
        }});

        run_issue_steps();
    }

    std::vector<IssueStep>    m_issue_steps;
    std::size_t               m_issue_step{0};
    RecordingAgent*           m_issue_agent{nullptr};
    Workspace::WorkspaceSubscription m_warnings_probe;
    int                              m_warnings_at_slice{-1}; // -1 not seen, 0 absent, 1 present
    std::string               m_warning_id, m_last_outcome, m_pill_right_after, m_group_first, m_deleted_issue, m_step_issue,
                              m_failure_issue;
    std::uint64_t             m_warning_copy{0}, m_deleted_object{0}, m_plate_one{0}, m_old_session{0};
    std::pair<float, float>   m_last_anchor{0, 0}, m_anchor_before{0, 0};
    std::size_t               m_user_messages_before{0}, m_requests_before{0}, m_count_right_after{0};
    std::uint64_t             m_generation_before{0}, m_announcements_before{0};
    std::optional<std::chrono::steady_clock::time_point> m_ring_started;
    bool                      m_ring_seen{false};
    int                       m_pager_first{0};
    int                       m_check_print_notices{-1}, m_check_print_stored{0}, m_modals_answered{0}, m_modals_before{0};
    std::string               m_last_modal_title;
    const wxDialog*           m_modal_pictured{nullptr};
    bool                      m_checked_right_after{false}, m_stale_right_after{false}, m_buttons_right_after{false}, m_was_slicing_at_edit{false};
    bool                      m_modal_closer_bound{false};
    int                       m_modal_answer{wxID_OK};
    // See answer_modals: may hold a dialog back for another tick.
    std::function<bool(wxDialog&)> m_modal_gate;
    std::optional<std::string>     m_preferences_section;
    std::map<std::string, std::string> m_agent_settings_before;
    wxEvtHandler              m_modal_closer;
    wxTimer                   m_modal_closer_timer{&m_modal_closer};
    std::chrono::steady_clock::time_point m_slice_started;
