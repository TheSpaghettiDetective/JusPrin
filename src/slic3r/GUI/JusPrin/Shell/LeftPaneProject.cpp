// The Project tab of the Plates / Project pane: the saved project's identity,
// what the 3MF says about the model, the physical-print history, and version
// history. Everything shown is read from the owner that holds it (project
// autosave, OrcaSlicer's project details, the physical-print ledger), and every
// action calls the same function the header's overflow menu used to call.

#include "LeftPane.hpp"

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Workspace/ProjectAutosave.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <wx/datetime.h>
#include <wx/dcmemory.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/graphics.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin {

using namespace Workspace;

namespace {

const wxString& middle_dot() { static const wxString s = wxString::FromUTF8(" \xC2\xB7 "); return s; }

void show_project_error(const wxString& title, const wxString& message, const std::string& detail)
{
    BOOST_LOG_TRIVIAL(error) << std::string(title.ToUTF8()) << ": " << detail;
    wxMessageBox(message, title, wxOK | wxICON_ERROR);
}

// A moment the ledger or the version store recorded, in the viewer's own time
// zone and format. Text that does not parse is shown as it was written.
wxString local_time(const std::string& iso)
{
    // The ledger and the version store write UTC with a trailing Z, which wx's
    // ISO parser does not take; the Z is what says the time is UTC.
    wxString text = wxString::FromUTF8(iso);
    if (text.EndsWith("Z"))
        text.RemoveLast();
    else
        return wxString::FromUTF8(iso);
    wxDateTime when;
    if (when.ParseISOCombined(text))
        return when.FromTimezone(wxDateTime::UTC).Format("%x %H:%M");
    return wxString::FromUTF8(iso);
}

wxString local_day(const std::string& iso)
{
    wxString text = wxString::FromUTF8(iso);
    if (!text.EndsWith("Z"))
        return text;
    text.RemoveLast();
    wxDateTime when;
    return when.ParseISOCombined(text) ? when.FromTimezone(wxDateTime::UTC).Format("%d %b") : wxString::FromUTF8(iso);
}

wxString duration_text(std::int64_t seconds)
{
    if (seconds >= 60)
        return wxString::Format(_L("%d h %02d"), int(seconds / 3600), int((seconds % 3600) / 60));
    return wxString::Format(_L("%d s"), int(seconds));
}

// Splits `text` into lines no wider than `max_width` at the spaces, using the
// font the context carries. A word wider than the line is cut by the fit.
std::vector<wxString> wrap(wxGraphicsContext& gc, const wxString& text, double max_width)
{
    std::vector<wxString> lines;
    wxString              line;
    wxString              word;
    const auto push_word = [&] {
        if (word.empty())
            return;
        const wxString candidate = line.empty() ? word : line + " " + word;
        double width = 0, height = 0;
        gc.GetTextExtent(candidate, &width, &height);
        if (width <= max_width || line.empty()) {
            line = candidate;
        } else {
            lines.push_back(line);
            line = word;
        }
        word.clear();
    };
    for (const wxUniChar ch : text) {
        if (ch == ' ' || ch == '\n') {
            push_word();
            if (ch == '\n') {
                lines.push_back(line);
                line.clear();
            }
        } else {
            word += ch;
        }
    }
    push_word();
    if (!line.empty() || lines.empty())
        lines.push_back(line);
    return lines;
}

} // namespace

void LeftPane::set_project_sources(ProjectSources sources)
{
    m_sources = std::move(sources);
    m_versions.clear();
    m_ledger_subscription.reset();
    if (m_sources.persistence != nullptr)
        m_ledger_subscription = m_sources.persistence->subscribe_ledger([this] { Refresh(); });
    Refresh();
}

void LeftPane::open_project_view(ProjectView view)
{
    if (view == ProjectView::Versions && m_sources.autosave != nullptr) {
        try {
            m_versions = m_sources.autosave->history();
        } catch (const std::exception& error) {
            show_project_error(_L("Version history unavailable"), _L("Version history couldn't be loaded. Try again."), error.what());
            return;
        }
    }
    m_navigation.open(view);
    m_scroll       = 0;
    m_action_focus = -1;
    Refresh();
}

void LeftPane::export_project()
{
    if (m_sources.autosave == nullptr)
        return;
    wxFileDialog dialog(this, _L("Export project 3MF"), {}, "JusPrin project.3mf", "3MF files (*.3mf)|*.3mf",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK)
        return;
    try {
        if (!m_sources.autosave->export_copy(std::filesystem::path(dialog.GetPath().ToStdWstring())))
            throw std::runtime_error(m_sources.autosave->error());
    } catch (const std::exception& error) {
        show_project_error(_L("Export failed"), _L("The project couldn't be exported. Try again."), error.what());
    }
}

void LeftPane::restore_version(const std::string& version_id)
{
    if (m_sources.autosave == nullptr)
        return;
    const bool confirmed = m_sources.confirm_restore ? m_sources.confirm_restore() : [this] {
        wxMessageDialog confirmation(this,
            _L("Restore this version of the model and project settings? The current model will be saved "
               "as a version first. Conversations and print records will stay current."),
            _L("Restore project version"), wxYES_NO | wxICON_QUESTION);
        confirmation.SetYesNoLabels(_L("Restore"), _L("Cancel"));
        return confirmation.ShowModal() == wxID_YES;
    }();
    if (!confirmed)
        return;
    if (!m_sources.autosave->restore(version_id))
        show_project_error(_L("Restore failed"), _L("This version couldn't be restored. Try again."),
                           m_sources.autosave->error());
    // A restore replaces the project, so whatever was open belongs to the old one.
    refresh_from_workspace();
}

// -- Drawing primitives -----------------------------------------------------

int LeftPane::draw_paragraph(wxGraphicsContext& gc, int y, int width, const wxString& text, TextRole role,
                             const wxColour& colour, int indent)
{
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int pad_x = FromDIP(pane.row_padding_x) + indent;
    gc.SetFont(m_theme.font(role), colour);
    const int line_height = FromDIP(m_theme.type_style(role).line_height);
    for (const wxString& line : wrap(gc, text, width - pad_x - FromDIP(pane.row_padding_x))) {
        double tw = 0, th = 0;
        gc.GetTextExtent(line, &tw, &th);
        gc.DrawText(line, pad_x, y + (line_height - th) / 2);
        y += line_height;
    }
    return y;
}

int LeftPane::draw_section_title(wxGraphicsContext& gc, int y, int width, const wxString& text)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    y += FromDIP(m_theme.metrics().space_2);
    y = draw_paragraph(gc, y, width, text.Upper(), TextRole::LabelBold, p.text_secondary);
    return y + FromDIP(pane.row_gap);
}

int LeftPane::draw_back_row(wxGraphicsContext& gc, int y, int width, const wxString& title)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int line  = FromDIP(pane.plate_row_height);
    const int glyph = FromDIP(pane.glyph_size);
    const int pad_x = FromDIP(pane.row_padding_x);
    const wxRect rect(0, y, width, line);
    draw_header_icon(gc, HeaderIcon::Back, pad_x, y + (line - glyph) / 2, glyph, p.text_secondary);
    gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
    double tw = 0, th = 0;
    gc.GetTextExtent(title, &tw, &th);
    gc.DrawText(title, pad_x + glyph + FromDIP(pane.plate_gap), y + (line - th) / 2);
    Hit hit{Hit::Kind::Action, rect, 0, PaneTab::Project};
    hit.label  = _L("Back to Project");
    hit.invoke = [this] { handle_escape(); };
    m_hits.push_back(std::move(hit));
    return y + line + FromDIP(pane.row_gap);
}

int LeftPane::draw_action_row(wxGraphicsContext& gc, int y, int width, const wxString& label, const wxString& detail,
                              bool chevron, std::function<void()> invoke, bool enabled, HeaderIcon glyph_icon)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int line  = FromDIP(pane.plate_row_height);
    const int glyph = FromDIP(pane.glyph_size);
    const int pad_x = FromDIP(pane.row_padding_x);
    const wxRect rect(0, y, width, line);

    int right = width - pad_x;
    if (chevron) {
        draw_header_icon(gc, glyph_icon, right - glyph, y + (line - glyph) / 2, glyph, p.text_secondary);
        right -= glyph + FromDIP(pane.object_gap);
    }
    double tw = 0, th = 0;
    if (!detail.empty()) {
        gc.SetFont(m_theme.font(TextRole::Metadata), p.text_secondary);
        gc.GetTextExtent(detail, &tw, &th);
        gc.DrawText(detail, right - tw, y + (line - th) / 2);
        right -= static_cast<int>(tw) + FromDIP(pane.plate_gap);
    }
    gc.SetFont(m_theme.font(TextRole::Label), enabled ? p.text_primary : p.action_disabled_text);
    gc.GetTextExtent(label, &tw, &th);
    gc.DrawText(label, pad_x, y + (line - th) / 2);
    gc.SetPen(wxPen(p.border_subtle, 1));
    gc.StrokeLine(pad_x, y + line - 0.5, width - pad_x, y + line - 0.5);

    if (enabled && invoke) {
        Hit hit{Hit::Kind::Action, rect, 0, PaneTab::Project};
        hit.label  = label;
        hit.invoke = std::move(invoke);
        m_hits.push_back(std::move(hit));
    }
    return y + line;
}

int LeftPane::draw_outline_button(wxGraphicsContext& gc, int y, int width, const wxString& label,
                                  std::function<void()> invoke, bool leading_plus)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    const int padding_x = FromDIP(pane.add_plate_padding_x);
    const wxRect button(padding_x, y, width - 2 * padding_x, FromDIP(pane.add_plate_button_height));
    gc.SetPen(wxPen(p.border_subtle, 1));
    gc.SetBrush(wxBrush(p.surface_canvas));
    gc.DrawRoundedRectangle(button.x + 0.5, button.y + 0.5, button.width - 1, button.height - 1,
                            FromDIP(m_theme.metrics().radius_compact));
    gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
    double tw = 0, th = 0;
    const wxString visible_label = leading_plus ? "+ " + label : label;
    gc.GetTextExtent(visible_label, &tw, &th);
    gc.DrawText(visible_label, button.x + (button.width - tw) / 2, button.y + (button.height - th) / 2);
    Hit hit{Hit::Kind::Action, button, 0, PaneTab::Project};
    hit.label  = label;
    hit.invoke = std::move(invoke);
    m_hits.push_back(std::move(hit));
    return y + button.height + FromDIP(pane.row_gap);
}

// -- Views ------------------------------------------------------------------
//
// Each view follows the layer structure of its Figma frame (26e1-26f): the
// Project root is an identity block, a Model section, and the project actions;
// every other view opens with a "Project" back control and its own title.

int LeftPane::draw_project(wxGraphicsContext& gc, int top, int width)
{
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    if (!m_details)
        m_details = m_workspace.project_details();
    int y = top + FromDIP(pane.list_padding_y);
    switch (m_navigation.view()) {
    case ProjectView::Root: y = draw_project_root(gc, y, width); break;
    case ProjectView::Details: y = draw_project_details(gc, y, width); break;
    case ProjectView::PrintHistory: y = draw_print_history(gc, y, width); break;
    case ProjectView::Versions: y = draw_versions(gc, y, width); break;
    }
    return y + FromDIP(pane.list_padding_y);
}

int LeftPane::draw_cover(wxGraphicsContext& gc, int y, int width, std::size_t count)
{
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    if (!m_details)
        return y;
    std::vector<wxBitmap> pictures;
    for (const ProjectAttachment& file : m_details->attachments) {
        if (file.folder != "Model Pictures" || pictures.size() == count)
            continue;
        const std::string path = m_workspace.auxiliary_data_dir() + "/" + file.id;
        boost::system::error_code error;
        const auto written = boost::filesystem::last_write_time(boost::filesystem::path(path), error);
        const std::string key = path + "|" + std::to_string(file.bytes) + "|" + std::to_string(error ? 0 : written);
        auto cached = m_pictures.find(key);
        if (cached == m_pictures.end()) {
            wxImage image;
            // A picture that will not decode is left out; the file list below it still names it.
            wxLogNull quiet;
            wxBitmap bitmap;
            if (image.LoadFile(wxString::FromUTF8(path)) && image.IsOk())
                bitmap = wxBitmap(image);
            cached = m_pictures.emplace(key, bitmap).first;
        }
        if (cached->second.IsOk())
            pictures.push_back(cached->second);
    }
    if (pictures.empty())
        return y;
    const int pad_x = FromDIP(pane.row_padding_x);
    const int gap   = FromDIP(m_theme.metrics().space_1);
    const int cell  = (width - 2 * pad_x - gap * int(pictures.size() - 1)) / int(pictures.size());
    // Pictures sit in 4:3 cells, shown whole.
    const int height = cell * 3 / 4;
    for (std::size_t index = 0; index < pictures.size(); ++index) {
        const wxBitmap& picture = pictures[index];
        const double scale = std::min(double(cell) / picture.GetWidth(), double(height) / picture.GetHeight());
        const double w = picture.GetWidth() * scale, h = picture.GetHeight() * scale;
        const double x = pad_x + double(index) * (cell + gap) + (cell - w) / 2.0;
        gc.SetPen(wxPen(m_theme.palette(m_dark).border_subtle, 1));
        gc.SetBrush(wxBrush(m_theme.palette(m_dark).surface_canvas));
        gc.DrawRoundedRectangle(pad_x + double(index) * (cell + gap), y, cell, height, FromDIP(m_theme.metrics().radius_standard));
        gc.DrawBitmap(picture, x, y + (height - h) / 2.0, w, h);
    }
    return y + height + FromDIP(pane.list_padding_y);
}

int LeftPane::draw_view_title(wxGraphicsContext& gc, int y, int width, const wxString& title)
{
    const ShellPalette& p = m_theme.palette(m_dark);
    y = draw_back_row(gc, y, width, _L("Project"));
    return draw_paragraph(gc, y, width, title, TextRole::BodySmallBold, p.text_primary);
}

int LeftPane::draw_project_root(wxGraphicsContext& gc, int y, int width)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;

    // The saved project's own name and state. This is the project, not the
    // model: the model's title below comes from the 3MF and is never a
    // substitute for it.
    wxString name = m_sources.autosave != nullptr ? wxString::FromUTF8(m_sources.autosave->project_name()) : wxString();
    if (name.empty())
        name = _L("Untitled");
    y = draw_paragraph(gc, y, width, name, TextRole::BodySmallBold, p.text_primary);
    if (m_sources.autosave != nullptr) {
        switch (m_sources.autosave->state()) {
        case ProjectAutosave::State::Saving: y = draw_paragraph(gc, y, width, _L("Saving…"), TextRole::Metadata, p.text_secondary); break;
        case ProjectAutosave::State::Saved: y = draw_paragraph(gc, y, width, _L("Saved"), TextRole::Metadata, p.text_secondary); break;
        case ProjectAutosave::State::Failed:
            y = draw_paragraph(gc, y, width, _L("Couldn't save"), TextRole::Metadata, p.status_danger);
            break;
        }
    }
    y += FromDIP(pane.list_padding_y);

    // Model: what the 3MF says, or an honest statement that it says nothing.
    const ProjectDetailsView details = build_details_view(*m_details);
    const bool has_cover = std::any_of(details.attachments.begin(), details.attachments.end(),
                                       [](const AttachmentGroup& group) { return group.folder == "Model Pictures"; });
    y = draw_section_title(gc, y, width, _L("Model"));
    if (details.model.empty() && !has_cover) {
        y = draw_paragraph(gc, y, width, _L("This 3MF has no model information."), TextRole::Label, p.text_secondary);
        y += FromDIP(pane.list_padding_y);
        if (m_sources.edit_project_info)
            y = draw_outline_button(gc, y, width, _L("Edit Project Info"), [this] { m_sources.edit_project_info(); }, true);
    } else {
        y = draw_cover(gc, y, width, 1);
        if (!details.model.title.empty())
            y = draw_paragraph(gc, y, width, wxString::FromUTF8(details.model.title), TextRole::LabelBold, p.text_primary);
        wxString attribution;
        if (!details.model.designer.empty())
            attribution = _L("by") + " " + wxString::FromUTF8(details.model.designer);
        if (!details.model.license.empty())
            attribution += (attribution.empty() ? wxString() : middle_dot()) + wxString::FromUTF8(details.model.license);
        if (!attribution.empty())
            y = draw_paragraph(gc, y, width, attribution, TextRole::Metadata, p.text_secondary);
    }
    y += FromDIP(pane.list_padding_y);

    // Project actions, in the order of the frame: prints, details, versions, export.
    const std::size_t prints = m_sources.persistence != nullptr ? m_sources.persistence->document().physical_print_count() : 0;
    if (prints > 0)
        y = draw_action_row(gc, y, width,
                            prints == 1 ? _L("Printed once") : wxString::Format(_L("Printed %d times"), int(prints)), {}, true,
                            [this] { open_project_view(ProjectView::PrintHistory); });
    y = draw_action_row(gc, y, width, _L("Details"), {}, true, [this] { open_project_view(ProjectView::Details); });
    y = draw_action_row(gc, y, width, _L("Version history"), {}, true, [this] { open_project_view(ProjectView::Versions); },
                        m_sources.autosave != nullptr);
    y = draw_action_row(gc, y, width, _L("Export project 3MF…"), {}, false, [this] { export_project(); },
                        m_sources.autosave != nullptr);
    return y;
}

namespace {

// Orca's attachment folders in the words of the design.
wxString attachment_group_name(const std::string& folder)
{
    if (folder == "Bill of Materials") return _L("Bill of materials");
    if (folder == "Assembly Guide") return _L("Assembly guide");
    if (folder == "Others") return _L("Others");
    return wxString::FromUTF8(folder);
}

} // namespace

int LeftPane::draw_disclosure_row(wxGraphicsContext& gc, int y, int width, const std::string& key, const wxString& label,
                                  const wxString& detail)
{
    const bool open = m_open_sections.count(key) != 0;
    return draw_action_row(gc, y, width, label, detail, true, [this, key, open] {
        if (open)
            m_open_sections.erase(key);
        else
            m_open_sections.insert(key);
        Refresh();
    }, true, open ? HeaderIcon::Down : HeaderIcon::Right);
}

int LeftPane::draw_project_details(wxGraphicsContext& gc, int y, int width)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    y = draw_view_title(gc, y, width, _L("Details"));
    y += FromDIP(pane.row_gap);
    const ProjectDetailsView view = build_details_view(*m_details);

    // Model: the title and who made it, with the long description folded.
    const ModelInfoView& model = view.model;
    if (!model.empty() || std::any_of(view.attachments.begin(), view.attachments.end(),
                                      [](const AttachmentGroup& group) { return group.folder == "Model Pictures"; })) {
        y = draw_section_title(gc, y, width, _L("Model"));
        y = draw_cover(gc, y, width, 3);
        if (!model.title.empty())
            y = draw_paragraph(gc, y, width, wxString::FromUTF8(model.title), TextRole::LabelBold, p.text_primary);
        wxString attribution;
        const auto add = [&attribution](const wxString& text) { attribution += (attribution.empty() ? wxString() : middle_dot()) + text; };
        if (!model.designer.empty()) add(wxString::FromUTF8(model.designer));
        if (!model.origin.empty()) add(wxString::FromUTF8(model.origin));
        if (!model.license.empty()) add(wxString::FromUTF8(model.license));
        if (!model.copyright.empty()) add(wxString::FromUTF8(model.copyright));
        if (!attribution.empty())
            y = draw_paragraph(gc, y, width, attribution, TextRole::Metadata, p.text_secondary);
        if (!model.description.empty()) {
            y = draw_disclosure_row(gc, y, width, "description", _L("Description"), {});
            if (m_open_sections.count("description") != 0)
                y = draw_paragraph(gc, y, width, wxString::FromUTF8(model.description), TextRole::Label, p.text_secondary);
        }
    }

    // Attachments: one disclosure row per category, with its file count.
    // The model's own pictures are its cover above, not a list of files.
    const bool has_files = std::any_of(view.attachments.begin(), view.attachments.end(),
                                       [](const AttachmentGroup& group) { return group.folder != "Model Pictures"; });
    if (has_files) {
        y = draw_section_title(gc, y, width, _L("Attachments"));
        for (const AttachmentGroup& group : view.attachments) {
            if (group.folder == "Model Pictures")
                continue;
            const std::string key = "attachments:" + group.folder;
            y = draw_disclosure_row(gc, y, width, key, attachment_group_name(group.folder),
                                    wxString::Format("%d", int(group.files.size())));
            if (m_open_sections.count(key) == 0)
                continue;
            for (const ProjectAttachment& file : group.files) {
                const std::size_t slash = file.id.find_last_of('/');
                const wxString    name  = wxString::FromUTF8(slash == std::string::npos ? file.id : file.id.substr(slash + 1));
                // Opened in the system's own program for the file, as OrcaSlicer's
                // Project page opens an attachment; the file stays where the
                // project keeps it.
                const wxString path = wxString::FromUTF8(m_workspace.auxiliary_data_dir() + "/" + file.id);
                y = draw_action_row(gc, y, width, name, {}, false, [path] {
                    if (!wxLaunchDefaultApplication(path))
                        show_project_error(_L("Couldn't open file"), _L("The file couldn't be opened."), std::string(path.ToUTF8()));
                });
            }
        }
        if (view.attachments_truncated)
            y = draw_paragraph(gc, y, width, _L("More files are attached than are listed here."), TextRole::Metadata, p.text_secondary);
    }

    // Profile: the print profile's own title and description.
    if (!model.profile_title.empty() || !model.profile_description.empty()) {
        y = draw_section_title(gc, y, width, _L("Profile"));
        y = draw_disclosure_row(gc, y, width, "profile",
                                model.profile_title.empty() ? wxString(_L("Profile")) : wxString::FromUTF8(model.profile_title), {});
        if (m_open_sections.count("profile") != 0 && !model.profile_description.empty())
            y = draw_paragraph(gc, y, width, wxString::FromUTF8(model.profile_description), TextRole::Label, p.text_secondary);
    }

    if (m_sources.edit_project_info) {
        y += FromDIP(pane.list_padding_y);
        y = draw_outline_button(gc, y, width, _L("Edit Project Info"), [this] { m_sources.edit_project_info(); });
    }
    return y;
}

int LeftPane::draw_print_history(wxGraphicsContext& gc, int y, int width)
{
    const ShellPalette&    p    = m_theme.palette(m_dark);
    const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
    y = draw_view_title(gc, y, width, _L("Print history"));
    if (m_sources.persistence == nullptr)
        return y;
    const PrintHistoryView history = build_print_history(m_sources.persistence->document().physical_prints());
    if (history.total == 0)
        return draw_paragraph(gc, y, width, _L("Nothing has been printed from this project yet."), TextRole::Label, p.text_secondary);

    y = draw_paragraph(gc, y, width,
                       history.total == 1 ? _L("Printed once") : wxString::Format(_L("Printed %d times"), int(history.total)),
                       TextRole::Label, p.text_secondary);
    y += FromDIP(pane.list_padding_y);

    if (history.detailed.empty())
        return draw_paragraph(gc, y, width,
                              _L("These prints were counted, but no details were kept. Prints sent from now on will be listed with date, plate, printer, time and result."),
                              TextRole::Label, p.text_secondary);

    const int inset = FromDIP(pane.row_padding_x);
    const int vertical = FromDIP(m_theme.metrics().space_2);
    const int gap = FromDIP(m_theme.metrics().space_1);
    const auto line_height = [this](TextRole role) { return FromDIP(m_theme.type_style(role).line_height); };
    const auto paragraph_height = [&](const wxString& text, TextRole role) {
        gc.SetFont(m_theme.font(role), p.text_primary);
        return int(wrap(gc, text, width - 2 * inset).size()) * line_height(role);
    };
    for (const PrintHistoryEntry& entry : history.detailed) {
        const wxString day = entry.started_at.empty() ? _L("Date unavailable (stubbed)") : local_day(entry.started_at);
        const wxString plate = entry.plate_name.empty() ? _L("Plate unavailable (stubbed)") : wxString::FromUTF8(entry.plate_name);
        const wxString heading = day + middle_dot() + plate;
        wxString outcome;
        wxColour outcome_colour = p.text_primary;
        switch (entry.outcome) {
        case PrintOutcome::Completed: outcome = _L("Finished"); break;
        case PrintOutcome::Failed: outcome = _L("Failed"); outcome_colour = p.status_danger; break;
        case PrintOutcome::Cancelled: outcome = _L("Stopped"); break;
        case PrintOutcome::Unrecorded:
            outcome = entry.recorded_outcome.empty() ? _L("Result unavailable (stubbed)")
                                                     : wxString::FromUTF8(entry.recorded_outcome);
            break;
        }
        gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
        double heading_width = 0, outcome_width = 0, unused = 0;
        gc.GetTextExtent(heading, &heading_width, &unused);
        gc.GetTextExtent(outcome, &outcome_width, &unused);
        const bool stacked = heading_width + outcome_width + gap > width - 2 * inset;

        const wxString printer = entry.printer.empty() ? _L("Printer unavailable (stubbed)") : wxString::FromUTF8(entry.printer);
        const wxString elapsed = entry.duration_seconds ? duration_text(*entry.duration_seconds) : _L("Time unavailable (stubbed)");
        const wxString facts = printer + middle_dot() + elapsed;
        wxString estimate;
        if (entry.estimated_seconds)
            estimate += duration_text(static_cast<std::int64_t>(*entry.estimated_seconds));
        if (entry.estimated_grams)
            estimate += (estimate.empty() ? wxString() : middle_dot()) + wxString::Format("%.0f g", *entry.estimated_grams);
        if (entry.estimated_cost)
            estimate += (estimate.empty() ? wxString() : middle_dot()) + wxString::Format("$%.2f", *entry.estimated_cost);
        const wxString estimate_line = estimate.empty() ? _L("Slice estimate unavailable (stubbed)") : _L("Slice estimate:") + " " + estimate;
        const wxString material = wxString::FromUTF8(entry.material);
        const wxString stopped = entry.stopped_percent ? wxString::Format(_L("Stopped at %d%%"), *entry.stopped_percent) : wxString();
        const wxString failure = wxString::FromUTF8(entry.failure);

        const int header_height = stacked ? paragraph_height(heading, TextRole::LabelBold) + line_height(TextRole::LabelBold)
                                          : line_height(TextRole::LabelBold);
        int row_height = 2 * vertical + header_height + gap + paragraph_height(facts, TextRole::Label) + gap +
                         paragraph_height(estimate_line, TextRole::Metadata);
        if (!material.empty()) row_height += gap + paragraph_height(material, TextRole::Metadata);
        if (!stopped.empty()) row_height += gap + paragraph_height(stopped, TextRole::Metadata);
        if (!failure.empty()) row_height += gap + paragraph_height(failure, TextRole::Metadata);
        int content_y = y + vertical;
        if (stacked) {
            content_y = draw_paragraph(gc, content_y, width, heading, TextRole::LabelBold, p.text_primary);
            content_y = draw_paragraph(gc, content_y, width, outcome, TextRole::LabelBold, outcome_colour);
        } else {
            gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
            gc.DrawText(heading, inset, content_y);
            gc.SetFont(m_theme.font(TextRole::LabelBold), outcome_colour);
            gc.DrawText(outcome, width - inset - outcome_width, content_y);
            content_y += header_height;
        }
        content_y += gap;
        content_y = draw_paragraph(gc, content_y, width, facts, TextRole::Label, p.text_secondary) + gap;
        content_y = draw_paragraph(gc, content_y, width, estimate_line, TextRole::Metadata, p.text_secondary);
        if (!material.empty()) content_y = draw_paragraph(gc, content_y + gap, width, material, TextRole::Metadata, p.text_secondary);
        if (!stopped.empty()) content_y = draw_paragraph(gc, content_y + gap, width, stopped, TextRole::Metadata, p.text_secondary);
        if (!failure.empty()) draw_paragraph(gc, content_y + gap, width, failure, TextRole::Metadata, p.text_secondary);
        y += row_height;
        gc.SetPen(wxPen(p.border_subtle, 1));
        gc.StrokeLine(inset, y + 0.5, width - inset, y + 0.5);
        y += FromDIP(pane.row_gap);
    }

    if (history.count_only > 0) {
        const wxString earlier = history.count_only == 1 ? wxString(_L("1 earlier print"))
                                                        : wxString::Format(_L("%d earlier prints"), int(history.count_only));
        gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
        double label_width = 0, th = 0;
        gc.GetTextExtent(earlier, &label_width, &th);
        gc.DrawText(earlier, inset, y);
        gc.SetFont(m_theme.font(TextRole::Metadata), p.text_secondary);
        const wxString count_only = _L("count only");
        double count_width = 0;
        gc.GetTextExtent(count_only, &count_width, &th);
        gc.DrawText(count_only, width - inset - count_width, y);
        y += line_height(TextRole::LabelBold);
    }
    return y;
}

int LeftPane::draw_versions(wxGraphicsContext& gc, int y, int width)
{
    const ShellPalette& p = m_theme.palette(m_dark);
    y = draw_view_title(gc, y, width, _L("Version history"));
    if (m_sources.autosave == nullptr)
        return y;
    const std::string current = m_sources.autosave->current_version();
    if (std::none_of(m_versions.begin(), m_versions.end(), [&current](const auto& version) { return version.id != current; }))
        return draw_paragraph(gc, y, width, _L("No earlier saved versions yet."), TextRole::Label, p.text_secondary);
    for (auto version = m_versions.rbegin(); version != m_versions.rend(); ++version) {
        if (version->id == current)
            continue;
        const std::string id         = version->id;
        const wxString    when       = local_time(version->created_at);
        // The row says when; only the button on it restores, so a click on the
        // time alone cannot ask to replace the model.
        const LeftPaneMetrics& pane = m_theme.metrics().left_pane;
        const int line  = FromDIP(pane.plate_row_height);
        const int pad_x = FromDIP(pane.row_padding_x);
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(p.surface_canvas));
        gc.DrawRoundedRectangle(0, y, width, line, FromDIP(m_theme.metrics().radius_standard));
        double tw = 0, th = 0;
        gc.SetFont(m_theme.font(TextRole::Label), p.text_primary);
        gc.GetTextExtent(when, &tw, &th);
        gc.DrawText(when, pad_x, y + (line - th) / 2);
        gc.SetFont(m_theme.font(TextRole::LabelBold), p.text_primary);
        const wxString word = _L("Restore");
        gc.GetTextExtent(word, &tw, &th);
        const int button_pad = FromDIP(m_theme.metrics().space_2);
        const wxRect button(width - pad_x - static_cast<int>(tw) - 2 * button_pad, y + FromDIP(m_theme.metrics().space_1),
                            static_cast<int>(tw) + 2 * button_pad, line - 2 * FromDIP(m_theme.metrics().space_1));
        gc.SetPen(wxPen(p.border_subtle, 1));
        gc.SetBrush(*wxTRANSPARENT_BRUSH);
        gc.DrawRoundedRectangle(button.x + 0.5, button.y + 0.5, button.width - 1, button.height - 1,
                                FromDIP(m_theme.metrics().radius_standard));
        gc.DrawText(word, button.x + button_pad, button.y + (button.height - th) / 2);
        Hit hit{Hit::Kind::Action, button, 0, PaneTab::Project};
        hit.label  = _L("Restore") + " " + when;
        hit.invoke = [this, id] { restore_version(id); };
        m_hits.push_back(std::move(hit));
        y += line + FromDIP(pane.row_gap);
    }
    return y;
}

} // namespace Slic3r::GUI::JusPrin
