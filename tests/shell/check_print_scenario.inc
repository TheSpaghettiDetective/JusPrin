    // The Check print scenario (--check-print, --check-print-capture <dir>).
    // Included inside class Scenario after the print-issue scenarios, whose
    // step runner and slicing helpers it uses.
    //
    // Three plates, each sliced through the header: one with nothing to flag,
    // one whose slice generated support, and one without support whose slab
    // is printed in the air. For each it opens the real window and reads the report panel's
    // contents, then drives the header's Legend switch, selects a finding, and
    // checks the window still closes itself when the slice under it goes
    // stale. With a capture directory it also saves the panel as the app
    // draws it and the window's own canvas pixels.

    CheckPrintDialog* check_print_dialog()
    {
        for (wxWindow* window : wxTopLevelWindows)
            if (auto* dialog = dynamic_cast<CheckPrintDialog*>(window); dialog != nullptr && dialog->IsModal())
                return dialog;
        return nullptr;
    }

    // Opens Check print, runs `inspect` inside its modal loop, then closes it
    // unless `close` is false (the window is expected to close itself).
    void with_check_print(const std::string& label, std::function<void(CheckPrintDialog&)> inspect, bool close = true)
    {
        m_cp_opened = false;
        m_frame->CallAfter([self = shared_from_this(), label, inspect, close] {
            CheckPrintDialog* dialog = self->check_print_dialog();
            self->check(dialog != nullptr, label + "_window_opens_modal");
            if (dialog == nullptr)
                return;
            self->m_cp_opened = true;
            dialog->preview()->get_canvas3d()->render();
            // The checks read the report as it drew itself. Windows paints a
            // new window only once its message queue is empty, after this
            // call, so the report draws now rather than after the checks.
            dialog->report()->Update();
            inspect(*dialog);
            if (close && dialog->IsModal())
                dialog->EndModal(wxID_CANCEL);
        });
        installed_shell()->status_row()->request_action(PrintAction::CheckPrint);
    }

    static std::string utf8(const wxString& text) { return text.ToUTF8().data(); }

    void dump_report(const std::string& label, const CheckPrintReportPanel& panel)
    {
        std::cerr << "HARNESS CHECKPRINT " << label << " valid=" << panel.view().valid << " sections=" << panel.view().sections.size()
                  << " clean=" << panel.view().clean.size() << '\n';
        for (const wxString& line : panel.drawn_lines())
            std::cerr << "HARNESS CHECKPRINT_LINE " << label << " '" << utf8(line) << "'\n";
    }

    static bool drew_line(const CheckPrintReportPanel& panel, const wxString& words)
    {
        for (const wxString& line : panel.drawn_lines())
            if (line.Contains(words))
                return true;
        return false;
    }

    // The closing line wraps where the platform's font puts the breaks, so it
    // is read joined back into one string.
    static wxString drawn_closing_line(const CheckPrintReportPanel& panel)
    {
        const wxString expected = CheckPrintReportPanel::closing_line(panel.view());
        wxString       joined;
        bool           started = false;
        for (const wxString& line : panel.drawn_lines()) {
            if (line.StartsWith(expected.BeforeFirst(':')))
                started = true;
            if (started)
                joined += (joined.empty() ? "" : " ") + line;
        }
        return joined;
    }

    static bool drew_closing_line(const CheckPrintReportPanel& panel)
    {
        const wxString expected = CheckPrintReportPanel::closing_line(panel.view());
        return !expected.empty() && drawn_closing_line(panel) == expected;
    }

    static std::vector<PrintCheck> section_checks(const CheckPrintReportView& view)
    {
        std::vector<PrintCheck> checks;
        for (const CheckSection& section : view.sections)
            checks.push_back(section.check);
        return checks;
    }

    static bool same_pixels(const wxBitmap& a, const wxBitmap& b)
    {
        const wxImage ia = a.ConvertToImage(), ib = b.ConvertToImage();
        return ia.GetWidth() == ib.GetWidth() && ia.GetHeight() == ib.GetHeight() &&
               std::memcmp(ia.GetData(), ib.GetData(), std::size_t(ia.GetWidth()) * ia.GetHeight() * 3) == 0;
    }

    void save_bitmap(const wxBitmap& bitmap, const std::string& name)
    {
        if (m_state->capture_dir.empty())
            return;
        fs::create_directories(m_state->capture_dir);
        const std::string file = (m_state->capture_dir / (name + ".png")).string();
        check(bitmap.ConvertToImage().SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG), "saved_" + name);
        std::cout << "HARNESS ARTIFACT " << name << " " << bitmap.GetWidth() << "x" << bitmap.GetHeight() << std::endl;
    }

    // The canvas's own pixels after a fresh frame, top row first.
    wxImage read_canvas(GLCanvas3D& canvas)
    {
        canvas.render();
        const Size size = canvas.get_canvas_size();
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
        return image;
    }

    // Pixels within a few steps of `colour` inside the rectangle.
    static int count_colour(const wxImage& image, const wxRect& rect, const wxColour& colour)
    {
        int found = 0;
        const wxRect inside = rect.Intersect(wxRect(0, 0, image.GetWidth(), image.GetHeight()));
        for (int y = inside.y; y < inside.GetBottom(); ++y)
            for (int x = inside.x; x < inside.GetRight(); ++x) {
                const unsigned char* px = image.GetData() + (std::size_t(y) * image.GetWidth() + x) * 3;
                if (std::abs(px[0] - colour.Red()) <= 6 && std::abs(px[1] - colour.Green()) <= 6 && std::abs(px[2] - colour.Blue()) <= 6)
                    ++found;
            }
        return found;
    }

    // The header's one switch, off when the window opens, driving OrcaSlicer's
    // legend and nothing else; the moves slider stays as OrcaSlicer shows it.
    void check_legend_switch(CheckPrintDialog& dialog, const std::string& label)
    {
        check(!dialog.legend_toggle()->is_checked(), label + "_switch_starts_off");
        check(!dialog.legend_shown(), label + "_legend_hidden_at_open");
        dialog.legend_toggle()->toggle();
        check(dialog.legend_toggle()->is_checked() && dialog.legend_shown(), label + "_switch_shows_the_legend");
        dialog.legend_toggle()->toggle();
        check(!dialog.legend_shown(), label + "_switch_hides_the_legend");
    }

    // A post with a slab floating 2 mm above it: the slab's first slice has
    // nothing under it, which is an unsupported island at a known height.
    std::string write_floating_stl()
    {
        const std::string path = (fs::path(data_dir()) / "check-print-floating.stl").string();
        std::ofstream out(path);
        out << "solid floating\n";
        write_box(out, {-5, -5, 0}, {5, 5, 10});
        write_box(out, {-15, -15, 12}, {15, 15, 15});
        out << "endsolid floating\n";
        return path;
    }

    // Loads one STL into a new project, centred on the first plate.
    void load_centred(const std::string& label, const std::string& stl)
    {
        answer_modals(wxID_NO);
        check(m_plater->new_project(true, true) != wxID_CANCEL, label + "_new_project");
        stop_answering_modals();
        check(m_plater->load_files(std::vector<std::string>{stl},
                  LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
              label + "_model_loaded");
        PartPlateList& plates = m_plater->get_partplate_list();
        check(plates.get_plate(0)->add_instance(0, 0, true) == 0, label + "_model_centred");
        m_plater->canvas3D()->reload_scene(true, true);
    }

    // A key as the window's char hook sees it, before the focused control.
    static void press(wxWindow& target, int key, bool shift = false, wxEventType type = wxEVT_CHAR_HOOK)
    {
        wxKeyEvent event(type);
        event.m_keyCode = key;
        event.SetShiftDown(shift);
        event.SetEventObject(&target);
        target.ProcessWindowEvent(event);
    }

    void self_check_focus(wxWindow* expected, const std::string& name)
    {
        wxWindow* focused = wxWindow::FindFocus();
        std::cerr << "HARNESS CHECKPRINT focus " << name << " is='" << (focused ? utf8(focused->GetName()) : std::string("none")) << "'\n";
        check(focused == expected, name);
    }

    // The Tab order, walked forwards from the canvas and back again. The order
    // itself is always checked; the focus actually moving is checked only when
    // macOS gives this app keyboard focus, which it does not while another app
    // is in front.
    void check_tab_order(CheckPrintDialog& dialog, const std::string& label)
    {
        const std::vector<wxWindow*> order = dialog.focus_order();
        bool forward = true, backward = true;
        for (std::size_t index = 0; index < order.size(); ++index) {
            forward  = forward && dialog.next_focus(order[index], false) == order[(index + 1) % order.size()];
            backward = backward && dialog.next_focus(order[index], true) == order[(index + order.size() - 1) % order.size()];
        }
        check(order.size() == 4 && order[1] == dialog.report() && order[2] == dialog.legend_toggle() &&
                  order[3] == static_cast<wxWindow*>(dialog.close_button()),
              label + "_tab_order_is_canvas_report_legend_close");
        check(forward, label + "_tab_order_wraps_forwards");
        check(backward, label + "_tab_order_wraps_backwards");

        order[0]->SetFocus();
        if (wxWindow::FindFocus() != order[0]) {
            std::cerr << "HARNESS CHECKPRINT focus UNOBSERVED: the app does not hold keyboard focus\n";
            return;
        }
        SimpleEvent canvas_tab(EVT_GLCANVAS_TAB);
        order[0]->ProcessWindowEvent(canvas_tab);
        self_check_focus(order[1], label + "_canvas_tab_moves_to_the_report");
        order[0]->SetFocus();
        bool moved = true;
        for (std::size_t step = 1; step <= order.size(); ++step) {
            press(dialog, WXK_TAB);
            moved = moved && wxWindow::FindFocus() == order[step % order.size()];
        }
        for (std::size_t step = 1; step <= order.size(); ++step) {
            press(dialog, WXK_TAB, true);
            moved = moved && wxWindow::FindFocus() == order[(order.size() - step) % order.size()];
        }
        check(moved, label + "_tab_and_shift_tab_move_the_focus");
    }

    void begin_check_print()
    {
        auto  self  = shared_from_this();
        auto& steps = m_issue_steps;

        // ---- A. Nothing to flag: one cube, no support --------------------------------------
        steps.push_back({"cp_a_plain_cube", [self] {
            self->two_cubes("cp_a");
            self->m_plater->remove(1);
            self->set_process_option("enable_support", new ConfigOptionBool(false));
            self->set_process_option("brim_type", new ConfigOptionEnum<BrimType>(btNoBrim));
        }, {}, {}});
        steps.push_back({"cp_a_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            // The "clean" checks below need a slice with nothing to report.
            // That this cube slices without a warning, crossing paths or a
            // path outside is the fixture's doing, so it is checked first:
            // a new OrcaSlicer warning then reads as a fixture change, not
            // as a Check print layout bug.
            {
                const Workspace::WorkspaceSnapshot workspace = installed_shell()->workspace()->snapshot();
                const Workspace::SliceReport report =
                    workspace.active_plate ? installed_shell()->workspace()->slice_report(*workspace.active_plate) : Workspace::SliceReport{};
                for (const Workspace::SliceFinding& finding : report.findings)
                    std::cerr << "HARNESS CHECKPRINT fixture_finding code=" << finding.code << " message='" << finding.message << "'\n";
                self->check(report.valid && report.findings.empty() && report.conflict.empty() && !report.toolpath_outside,
                            "cp_a_fixture_slices_without_a_shown_warning");
            }
            // Prepare's view zoomed in, as a person leaves it; the window
            // must open on the whole slice anyway and give this view back.
            self->m_plater->get_camera().set_zoom(self->m_plater->get_camera().get_zoom() * 6);
            Camera before = self->m_plater->get_camera();
            // OrcaSlicer's G-code listing preference on, its default.
            if (!wxGetApp().show_gcode_window())
                wxGetApp().toggle_show_gcode_window();
            // The pointer last seen on Prepare's canvas, where this window's
            // layer slider will be.
            ImGui::GetIO().MousePos = ImVec2(self->prepare_canvas().get_canvas_size().get_width() - 40.f, 120.f);
            self->with_check_print("cp_a", [self, &before](CheckPrintDialog& dialog) {
                CheckPrintReportPanel& panel  = *dialog.report();
                GLCanvas3D&            canvas = *dialog.preview()->get_canvas3d();
                const ShellMetrics&    m      = brand_theme()->metrics();
                self->check(canvas.legacy_overlays_hidden(), "cp_a_plate_strip_navigator_and_toolbar_hidden");
                self->check(!wxGetApp().show_gcode_window(), "cp_a_gcode_listing_off_at_open");
                self->check(ImGui::GetIO().MousePos.x < -1e30f && ImGui::GetCurrentContext()->HoveredWindow == nullptr,
                            "cp_a_no_hover_without_a_pointer");
                canvas.render();
                const Vec3d fitted = canvas.get_gcode_viewer().get_paths_bounding_box().center();
                std::cerr << "HARNESS CHECKPRINT camera target=" << self->m_plater->get_camera().get_target().transpose()
                          << " paths_centre=" << fitted.transpose() << '\n';
                self->check((self->m_plater->get_camera().get_target() - fitted).norm() < 0.01, "cp_a_opens_fitted_to_the_slice");
                self->check(dialog.GetMinClientSize() == dialog.FromDIP(wxSize(900, 600)), "cp_a_window_has_a_minimum_size");
                self->check_tab_order(dialog, "cp_a");
                // The quiet Close button takes the header's raised surface, in dark too.
                HeaderButton& close = *dialog.close_button();
                close.set_dark(true);
                const wxImage close_dark = close.snapshot().ConvertToImage();
                close.set_dark(GUI_App::dark_mode());
                const wxColour raised = brand_theme()->palette(true).surface_raised;
                self->check(close_dark.GetRed(1, 1) == raised.Red() && close_dark.GetGreen(1, 1) == raised.Green() &&
                                close_dark.GetBlue(1, 1) == raised.Blue(),
                            "cp_a_dark_close_button_sits_on_the_raised_header");
                // The C key inside the window is the person's own choice: it stays until close.
                wxGetApp().toggle_show_gcode_window();
                self->check(wxGetApp().show_gcode_window(), "cp_a_listing_can_be_switched_on_inside");
                wxGetApp().toggle_show_gcode_window();
                const wxBitmap         plain = panel.snapshot(panel.GetClientSize().x);
                self->dump_report("cp_a", panel);
                const CheckPrintReportView& view = panel.view();
                self->check(view.valid, "cp_a_report_reads_the_current_slice");
                self->check(section_checks(view) == std::vector<PrintCheck>{PrintCheck::FirstLayer}, "cp_a_only_the_first_layer_is_drawn");
                self->check(view.clean == closing_line_order(), "cp_a_every_check_is_named_clean");
                self->check(!view.supports_generated, "cp_a_no_support_generated");
                self->check(drew_closing_line(panel), "cp_a_closing_line_is_drawn_whole");
                self->check(drawn_closing_line(panel).Contains(_L("supports (none generated)")), "cp_a_closing_line_says_none_generated");
                self->check(drew_line(panel, _L("Time")) && drew_line(panel, _L("Filament")), "cp_a_summary_names_time_and_filament");
                const std::string context = utf8(dialog.context_label()->GetLabel());
                self->check(context.find("sliced for") != std::string::npos, "cp_a_header_names_the_printer");
                // The project by the window title's name, never an object's name.
                // The summary is "<name> — <save state>", as the window title shows it.
                const std::string project = utf8(installed_shell()->status_row()->project_summary().BeforeFirst('\n').BeforeFirst(wxUniChar(0x2014)).Trim());
                self->check(context.rfind(project, 0) == 0, "cp_a_header_starts_with_the_project_name");
                for (const ModelObject* object : self->m_plater->model().objects)
                    self->check(context.find(object->name) == std::string::npos, "cp_a_header_names_no_object");
                const wxBitmap toggle_off = dialog.legend_toggle()->snapshot();
                self->check_legend_switch(dialog, "cp_a");
                dialog.legend_toggle()->set_checked(true);
                const wxBitmap toggle_on = dialog.legend_toggle()->snapshot();
                dialog.legend_toggle()->set_checked(false);
                self->check(!same_pixels(toggle_off, toggle_on), "cp_a_checked_switch_is_drawn");
                self->save_bitmap(toggle_off, "check-print-switch-off");
                self->save_bitmap(toggle_on, "check-print-switch-on");

                // The first-layer row moves the layer slider to layer 1.
                IMSlider* layers = dialog.preview()->get_canvas3d()->get_gcode_viewer().get_layers_slider();
                const int top_before = layers->GetHigherValue();
                panel.select({0, 0});
                self->check(panel.selected() == std::optional<CheckPrintReportPanel::RowRef>({0, 0}), "cp_a_pressed_row_is_selected");
                self->check(layers->GetHigherValue() == 0 && top_before != 0, "cp_a_finding_moves_the_layer_slider");
                self->check(dialog.caption() && dialog.caption()->layer == 1, "cp_a_caption_names_layer_one");
                canvas.render();
                const int inset = int(std::round(m.space_3 * wxGetApp().imgui()->get_style_scaling()));
                std::cerr << "HARNESS CHECKPRINT caption_rect=" << (dialog.caption_rect() ? dialog.caption_rect()->x : -1) << ","
                          << (dialog.caption_rect() ? dialog.caption_rect()->y : -1) << " inset=" << inset << '\n';
                self->check(dialog.caption_rect() && dialog.caption_rect()->x == inset && dialog.caption_rect()->y == inset,
                            "cp_a_caption_sits_in_the_top_left_corner");
                self->check(dialog.caption_bold(), "cp_a_caption_title_is_bold");
                self->check(self->m_plater->get_camera().get_target() != before.get_target() ||
                                self->m_plater->get_camera().get_zoom() != before.get_zoom(),
                            "cp_a_finding_moves_the_camera");
                const wxBitmap selected = panel.snapshot(panel.GetClientSize().x);
                self->check(!same_pixels(plain, selected), "cp_a_selection_is_drawn");
                // The other appearance draws differently.
                panel.set_dark(!GUI_App::dark_mode());
                const wxBitmap dark = panel.snapshot(panel.GetClientSize().x);
                panel.set_dark(GUI_App::dark_mode());
                self->check(!same_pixels(selected, dark), "cp_a_other_appearance_panel_differs");
                self->save_bitmap(plain, "check-print-nothing-to-flag");
                self->save_bitmap(selected, "check-print-finding-selected");
                self->save_bitmap(dark, "check-print-nothing-to-flag-other-appearance");

                // OrcaSlicer's accent is the brand's action colour, read at use:
                // the sliders, COL_ORCA and ImGui's style table all carry it.
                const ShellTheme* theme  = brand_theme();
                const wxColour    action = theme->palette(GUI_App::dark_mode()).action_primary;
                const ColorRGBA   accent = ColorRGBA::ORCA();
                self->check(accent.r_uchar() == action.Red() && accent.g_uchar() == action.Green() && accent.b_uchar() == action.Blue(),
                            "cp_a_orca_accent_is_the_action_colour");
                const ImVec4 col_orca = ImGuiWrapper::COL_ORCA;
                self->check(std::lround(col_orca.x * 255) == action.Red() && std::lround(col_orca.z * 255) == action.Blue(),
                            "cp_a_col_orca_reads_the_accent");
                const ImVec4 header = ImGui::GetStyle().Colors[ImGuiCol_HeaderActive];
                self->check(std::lround(header.x * 255) == action.Red() && std::lround(header.z * 255) == action.Blue(),
                            "cp_a_imgui_style_carries_the_accent");
                const wxImage off = self->read_canvas(canvas);
                const int     w = off.GetWidth(), h = off.GetHeight();
                // ImGui is one context for every canvas; this frame used this canvas's size.
                self->check(ImGui::GetIO().DisplaySize.x == float(canvas.get_canvas_size().get_width()) &&
                                ImGui::GetIO().DisplaySize.y == float(canvas.get_canvas_size().get_height()),
                            "cp_a_overlays_drawn_at_this_canvas_size");
                const int right_band = count_colour(off, wxRect(w - w / 8, 0, w / 8, h), action);
                const ImGuiWindow* moves = ImGui::FindWindowByName("moves_slider");
                std::cerr << "HARNESS CHECKPRINT slider_pixels right=" << right_band << " canvas=" << w << "x" << h << '\n';
                self->check(right_band > 50, "cp_a_layer_slider_drawn_in_the_action_colour");
                self->check(moves != nullptr && moves->Active, "cp_a_moves_slider_shown_as_orcaslicer_shows_it");
                // The legend is drawn when switched on. ImGui draws a new
                // auto-sized window from its second frame.
                dialog.legend_toggle()->toggle();
                canvas.render();
                const wxImage legend_on = self->read_canvas(canvas);
                const ImGuiWindow* legend = ImGui::FindWindowByName("Legend");
                std::cerr << "HARNESS CHECKPRINT legend_x=" << (legend ? legend->Pos.x : -1.f) << " caption_right="
                          << (dialog.caption_rect() ? dialog.caption_rect()->GetRight() : -1) << '\n';
                self->check(legend != nullptr && legend->Active && dialog.caption_rect() &&
                                dialog.caption_rect()->GetRight() < int(legend->Pos.x),
                            "cp_a_caption_ends_before_the_legend");
                dialog.legend_toggle()->toggle();
                canvas.render();
                const wxImage legend_off = self->read_canvas(canvas);
                self->check(!same_pixels(wxBitmap(legend_on), wxBitmap(legend_off)), "cp_a_legend_is_drawn_when_switched_on");
                if (!self->m_state->capture_dir.empty()) {
                    self->save_bitmap(wxBitmap(off), "check-print-canvas-default");
                    self->save_bitmap(wxBitmap(legend_on), "check-print-canvas-legend-on");
                }
            });
            self->check(self->m_cp_opened, "cp_a_window_was_inspected");
            self->check(wxGetApp().show_gcode_window(), "cp_a_closing_restores_the_listing_preference");
            // Prepare's own overlays get Prepare's size back on its next frame.
            self->prepare_canvas().render();
            self->check(ImGui::GetIO().DisplaySize.x == float(self->prepare_canvas().get_canvas_size().get_width()) &&
                            ImGui::GetIO().DisplaySize.y == float(self->prepare_canvas().get_canvas_size().get_height()),
                        "cp_a_prepare_overlays_drawn_at_its_size_after_closing");
            self->check(self->m_plater->get_camera().get_target() == before.get_target() &&
                            self->m_plater->get_camera().get_zoom() == before.get_zoom(),
                        "cp_a_closing_puts_the_camera_back");
        }});

        // ---- Stale close: a setting change under the open window closes it ------------------
        steps.push_back({"cp_stale_close", [self] {
            self->m_cp_guard_fired = false;
            if (!self->m_cp_guard_bound) {
                self->m_cp_guard.Bind(wxEVT_TIMER, [self](wxTimerEvent&) {
                    if (CheckPrintDialog* dialog = self->check_print_dialog()) {
                        self->m_cp_guard_fired = true;
                        dialog->EndModal(wxID_CANCEL);
                    }
                });
                self->m_cp_guard_bound = true;
            }
            const bool listing = wxGetApp().show_gcode_window();
            self->with_check_print("cp_stale", [self](CheckPrintDialog&) {
                self->set_process_option("wall_loops", new ConfigOptionInt(4));
                self->m_cp_guard.StartOnce(5000);
            }, false);
            self->m_cp_guard.Stop();
            self->check(self->m_cp_opened && !self->m_cp_guard_fired, "cp_stale_window_closes_itself_when_the_slice_goes_stale");
            self->check(self->check_print_dialog() == nullptr, "cp_stale_window_is_gone");
            self->check(wxGetApp().show_gcode_window() == listing, "cp_stale_close_restores_the_listing_preference");
        }, {}, {}});

        // ---- B. Support generated: a slab on one thin leg ----------------------------------
        steps.push_back({"cp_b_overhang", [self] {
            self->load_centred("cp_b", self->write_overhang_stl());
            self->set_process_option("enable_support", new ConfigOptionBool(true));
            self->set_process_option("support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
        }, {}, {}});
        steps.push_back({"cp_b_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->with_check_print("cp_b", [self](CheckPrintDialog& dialog) {
                CheckPrintReportPanel& panel = *dialog.report();
                self->dump_report("cp_b", panel);
                const CheckPrintReportView& view = panel.view();
                self->check(view.supports_generated, "cp_b_support_generated");
                self->check(!view.sections.empty() && view.sections.front().check == PrintCheck::Supports, "cp_b_supports_lead");
                self->check(drew_line(panel, _L("Supports")), "cp_b_supports_section_is_drawn");
                self->check(std::find(view.clean.begin(), view.clean.end(), PrintCheck::Supports) == view.clean.end(),
                            "cp_b_supports_not_in_the_closing_line");
                self->check(drew_closing_line(panel), "cp_b_closing_line_is_drawn_whole");
                // Collapsing the section hides its rows and keeps its header.
                const std::size_t lines = panel.drawn_lines().size();
                panel.toggle_section(0);
                panel.snapshot(panel.GetClientSize().x);
                self->check(panel.is_collapsed(0) && panel.drawn_lines().size() < lines, "cp_b_section_collapses");
                panel.toggle_section(0);
                self->save_bitmap(panel.snapshot(panel.GetClientSize().x), "check-print-supports");
            });
            self->check(self->m_cp_opened, "cp_b_window_was_inspected");
        }});

        // ---- C. No support, a slab printed in the air: the finding leads -------------------
        steps.push_back({"cp_c_floating_slab", [self] {
            self->load_centred("cp_c", self->write_floating_stl());
            self->set_process_option("enable_support", new ConfigOptionBool(false));
        }, {}, {}});
        steps.push_back({"cp_c_slice", [self] { self->slice_now(); }, self->slice_ended(), [self] {
            self->stop_answering_modals();
            self->with_check_print("cp_c", [self](CheckPrintDialog& dialog) {
                CheckPrintReportPanel& panel = *dialog.report();
                self->dump_report("cp_c", panel);
                const CheckPrintReportView& view = panel.view();
                self->check(!view.supports_generated, "cp_c_no_support_generated");
                self->check(!view.sections.empty() && view.sections.front().check != PrintCheck::FirstLayer &&
                                view.sections.front().worst != CheckSeverity::None,
                            "cp_c_a_finding_leads");
                std::optional<CheckPrintReportPanel::RowRef> island;
                for (std::size_t section = 0; section < view.sections.size(); ++section)
                    if (view.sections[section].check == PrintCheck::Islands && !view.sections[section].rows.empty())
                        island = CheckPrintReportPanel::RowRef{section, 0};
                self->check(island.has_value(), "cp_c_floating_slab_is_an_island");
                self->check(drawn_closing_line(panel).Contains(_L("supports (none generated)")), "cp_c_closing_line_says_none_generated");
                self->check(drew_closing_line(panel), "cp_c_closing_line_is_drawn_whole");
                if (island) {
                    // The island's row moves the layer slider to its height.
                    const CheckRow& row = view.sections[island->first].rows[island->second];
                    panel.select(*island);
                    IMSlider* layers = dialog.preview()->get_canvas3d()->get_gcode_viewer().get_layers_slider();
                    std::cerr << "HARNESS CHECKPRINT cp_c island_z=" << (row.z ? *row.z : -1.0) << " slider_z=" << layers->GetHigherValueD()
                              << " caption_layer=" << (dialog.caption() ? int(dialog.caption()->layer) : -1) << '\n';
                    self->check(row.z && std::abs(layers->GetHigherValueD() - *row.z) < 0.05, "cp_c_layer_slider_moves_to_the_island");
                    self->check(dialog.caption() && dialog.caption()->layer > 1 && dialog.caption()->title == panel.row_message(row),
                                "cp_c_caption_names_the_island_and_its_layer");
                    if (!self->m_state->capture_dir.empty())
                        self->save_bitmap(wxBitmap(self->read_canvas(*dialog.preview()->get_canvas3d())), "check-print-canvas-island-selected");
                }
                self->save_bitmap(panel.snapshot(panel.GetClientSize().x), "check-print-island");

                // At the smallest window the report is taller than its panel:
                // a scrollbar shows, and a row reached from the keyboard is
                // scrolled into view.
                dialog.SetClientSize(dialog.GetMinClientSize());
                dialog.Layout();
                panel.SetFocus();
                // Down from the selected island until the selection stops moving: the last row.
                for (int step = 0; step < 32; ++step)
                    press(panel, WXK_DOWN, false, wxEVT_KEY_DOWN);
                const std::optional<CheckPrintReportPanel::RowRef> last = panel.selected();
                const std::optional<wxRect> shown = last ? panel.row_rect(*last) : std::nullopt;
                std::cerr << "HARNESS CHECKPRINT cp_c min_panel=" << panel.GetClientSize().y << " content=" << panel.content_height()
                          << " scroll=" << panel.scroll() << " row_y=" << (shown ? shown->y : -1) << '\n';
                self->check(panel.content_height() > panel.GetClientSize().y && panel.scrollbar().has_value(),
                            "cp_c_long_report_shows_a_scrollbar");
                self->check(last && last->first + 1 == view.sections.size() &&
                                last->second + 1 == view.sections.back().rows.size() && shown && panel.scroll() > 0 && shown->y >= 0 &&
                                shown->GetBottom() <= panel.GetClientSize().y,
                            "cp_c_keyboard_selection_scrolls_into_view");
                self->save_bitmap(panel.snapshot_view(), "check-print-panel-min-size-scrolled");
                if (!self->m_state->capture_dir.empty())
                    self->save_bitmap(wxBitmap(self->read_canvas(*dialog.preview()->get_canvas3d())), "check-print-canvas-min-size");

                // Escape from the report closes the window.
                self->m_cp_guard_fired = false;
                self->m_cp_guard.StartOnce(5000);
                press(dialog, WXK_ESCAPE);
            }, false);
            self->m_cp_guard.Stop();
            self->check(self->m_cp_opened && !self->m_cp_guard_fired && self->check_print_dialog() == nullptr,
                        "cp_c_escape_from_the_report_closes_the_window");
        }});

        steps.push_back({"cp_done", [self] {
            self->answer_modals(wxID_NO);
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "cp_final_new_project");
            self->stop_answering_modals();
        }, {}, {}});
        run_issue_steps();
    }

    bool     m_cp_opened{false};
    bool     m_cp_guard_fired{false};
    bool     m_cp_guard_bound{false};
    wxTimer  m_cp_guard;
