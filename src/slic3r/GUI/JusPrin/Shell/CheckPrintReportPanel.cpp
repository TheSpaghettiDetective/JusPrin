#include "CheckPrintReportPanel.hpp"
#include "HeaderControls.hpp"

#include "slic3r/GUI/I18N.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>

#include <algorithm>
#include <cmath>

namespace Slic3r::GUI::JusPrin {

namespace {

// The summary's label column. Only the Check print report has this table, so
// the width is written here rather than named in the token file.
constexpr int kSummaryLabelWidthDip = 72;

const wxString& middle_dot() { static const wxString s = wxString::FromUTF8(" \xC2\xB7 "); return s; }
const wxString& ellipsis()   { static const wxString s = wxString::FromUTF8("\xE2\x80\xA6"); return s; }
const wxString& square_mm()  { static const wxString s = wxString::FromUTF8(" mm\xC2\xB2"); return s; }

double text_width(wxGraphicsContext& gc, const wxString& text)
{
    double width = 0, height = 0;
    gc.GetTextExtent(text, &width, &height);
    return width;
}

wxString fit(wxGraphicsContext& gc, const wxString& text, double max_width)
{
    if (text_width(gc, text) <= max_width)
        return text;
    wxString cut = text;
    while (!cut.empty()) {
        cut.RemoveLast();
        if (text_width(gc, cut + ellipsis()) <= max_width)
            return cut + ellipsis();
    }
    return ellipsis();
}

// Splits `text` into lines no wider than `max_width`: at its own line breaks
// (OrcaSlicer's warnings carry some), then at spaces. A word wider than a
// line is cut with an ellipsis.
std::vector<wxString> wrap(wxGraphicsContext& gc, const wxString& text, double max_width)
{
    std::vector<wxString> lines;
    for (wxString paragraph : wxSplit(text, '\n', '\0')) {
        paragraph.Trim().Trim(false);
        if (paragraph.empty())
            continue;
        wxString line;
        for (const wxString& word : wxSplit(paragraph, ' ', '\0')) {
            if (word.empty())
                continue;
            const wxString candidate = line.empty() ? word : line + " " + word;
            if (text_width(gc, candidate) <= max_width || line.empty()) {
                line = candidate;
                continue;
            }
            lines.push_back(fit(gc, line, max_width));
            line = word;
        }
        if (!line.empty())
            lines.push_back(fit(gc, line, max_width));
    }
    return lines;
}

wxString utf8(const std::string& text) { return wxString::FromUTF8(text); }

wxString millimetres(double value) { return wxString::Format(_L("%.1f mm"), value); }

wxString area_text(double mm2)
{
    // Whole square millimetres with a thousands separator, the way the design writes 1,980 mm².
    const long long whole = std::llround(mm2);
    wxString digits = wxString::Format("%lld", whole);
    for (int at = int(digits.length()) - 3; at > 0; at -= 3)
        digits.insert(at, ",");
    return (mm2 > 0 && mm2 < 1 ? wxString::Format("%.1f", mm2) : digits) + square_mm();
}

wxString duration_text(std::uint32_t seconds)
{
    if (seconds < 60)
        return wxString::Format(_L("%d s"), int(seconds));
    const int minutes = int((seconds + 30) / 60);
    if (minutes < 60)
        return wxString::Format(_L("%d min"), minutes);
    return wxString::Format(_L("%d h %d min"), minutes / 60, minutes % 60);
}

wxString cost_text(double cost, const std::string& currency)
{
    if (currency == "USD")
        return wxString::Format("$%.2f", cost);
    if (currency.empty())
        return wxString::Format("%.2f", cost);
    return wxString::Format("%.2f %s", cost, utf8(currency));
}

wxString weight_and_length(const FilamentLine& line)
{
    wxString text = wxString::Format(_L("%.0f g"), line.grams);
    if (line.length_mm > 0)
        text += middle_dot() + wxString::Format(_L("%.1f m"), line.length_mm / 1000.0);
    return text;
}

wxString clean_name(PrintCheck check, bool supports_generated)
{
    switch (check) {
    case PrintCheck::Supports:      return supports_generated ? _L("supports") : _L("supports (none generated)");
    case PrintCheck::Seams:         return _L("seams on marked faces");
    case PrintCheck::Islands:       return _L("unsupported islands");
    case PrintCheck::CrossingPaths: return _L("crossing paths");
    case PrintCheck::OutsidePlate:  return _L("paths outside the plate");
    case PrintCheck::OrcaWarnings:  return _L("OrcaSlicer warnings");
    case PrintCheck::FirstLayer:    return _L("first layer");
    }
    return wxEmptyString;
}

} // namespace

// -- CheckPrintReportPanel ----------------------------------------------------

CheckPrintReportPanel::CheckPrintReportPanel(wxWindow* parent, const ShellTheme& theme)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS | wxTAB_TRAVERSAL)
    , m_theme(theme)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetName(_L("Print report"));
    Bind(wxEVT_PAINT, &CheckPrintReportPanel::paint, this);
    Bind(wxEVT_LEFT_DOWN, &CheckPrintReportPanel::on_left_down, this);
    Bind(wxEVT_MOTION, &CheckPrintReportPanel::on_motion, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        if (m_hovered) {
            m_hovered.reset();
            Refresh();
        }
        event.Skip();
    });
    Bind(wxEVT_MOUSEWHEEL, &CheckPrintReportPanel::on_wheel, this);
    Bind(wxEVT_KEY_DOWN, &CheckPrintReportPanel::on_key, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { clamp_scroll(); Refresh(); event.Skip(); });
}

void CheckPrintReportPanel::set_report(CheckPrintReportView view, Context context)
{
    m_view    = std::move(view);
    m_context = std::move(context);
    m_collapsed.assign(m_view.sections.size(), false);
    m_selected.reset();
    m_hovered.reset();
    m_scroll = 0;
    Refresh();
}

void CheckPrintReportPanel::set_dark(bool dark)
{
    m_dark = dark;
    Refresh();
}

void CheckPrintReportPanel::select(RowRef row)
{
    if (row.first >= m_view.sections.size() || row.second >= m_view.sections[row.first].rows.size())
        return;
    m_selected = row;
    const CheckRow& selected = m_view.sections[row.first].rows[row.second];
    SetName(wxString::Format(_L("Print report, %s"), row_message(selected)));
    scroll_into_view(row);
    Refresh();
    if (m_on_select)
        m_on_select(selected);
}

void CheckPrintReportPanel::toggle_section(std::size_t section)
{
    if (section >= m_collapsed.size())
        return;
    m_collapsed[section] = !m_collapsed[section];
    if (m_collapsed[section] && m_selected && m_selected->first == section)
        m_selected.reset();
    clamp_scroll();
    Refresh();
}

bool CheckPrintReportPanel::is_collapsed(std::size_t section) const
{
    return section < m_collapsed.size() && m_collapsed[section];
}

wxString CheckPrintReportPanel::section_title(PrintCheck check)
{
    switch (check) {
    case PrintCheck::Supports:      return _L("Supports");
    case PrintCheck::Seams:         return _L("Seams");
    case PrintCheck::OrcaWarnings:  return _L("OrcaSlicer warnings");
    case PrintCheck::FirstLayer:    return _L("First layer");
    case PrintCheck::Islands:       return _L("Unsupported islands");
    case PrintCheck::CrossingPaths: return _L("Crossing paths");
    case PrintCheck::OutsidePlate:  return _L("Paths outside the plate");
    }
    return wxEmptyString;
}

wxString CheckPrintReportPanel::closing_line(const CheckPrintReportView& view)
{
    if (view.clean.empty())
        return wxEmptyString;
    wxString names;
    for (std::size_t index = 0; index < view.clean.size(); ++index) {
        if (index > 0)
            names += ", ";
        names += clean_name(view.clean[index], view.supports_generated);
    }
    return wxString::Format(_L("Checked, nothing found: %s."), names);
}

wxString CheckPrintReportPanel::section_count(const CheckSection& section) const
{
    const int rows = int(section.rows.size());
    switch (section.check) {
    case PrintCheck::Supports: {
        // "2 objects · 96 layers", as the design writes it.
        const wxString layers = wxString::Format(_L_PLURAL("%d layer", "%d layers", int(section.support_layers)),
                                                 int(section.support_layers));
        if (section.support_objects == 0)
            return layers;
        return wxString::Format(_L_PLURAL("%d object", "%d objects", int(section.support_objects)), int(section.support_objects)) +
               middle_dot() + layers;
    }
    case PrintCheck::Seams: {
        std::size_t seams = 0;
        for (const CheckRow& row : section.rows)
            seams += row.count;
        return wxString::Format(_L_PLURAL("%d on a marked face", "%d on marked faces", int(seams)), int(seams));
    }
    case PrintCheck::FirstLayer:
        return wxString::Format(_L_PLURAL("%d object", "%d objects", rows), rows);
    default:
        return section.truncated ? wxString::Format(_L("%d or more"), rows) : wxString::Format("%d", rows);
    }
}

wxString CheckPrintReportPanel::row_message(const CheckRow& row) const
{
    switch (row.kind) {
    case CheckRowKind::ToolpathOutside:   return _L("A toolpath runs outside the printable area");
    case CheckRowKind::PathConflict:      return utf8(row.message);
    case CheckRowKind::OrcaWarning:       return utf8(row.message);
    case CheckRowKind::SupportInRegion:
        return wxString::Format(_L("Support touches %s, which you asked to keep clear"), utf8(row.region_label));
    case CheckRowKind::SupportInHole:     return wxString::Format(_L("Support inside the %s"), utf8(row.region_label));
    case CheckRowKind::SupportClear:      return _L("No support in a hole or a region you kept clear");
    case CheckRowKind::UnsupportedIsland: return wxString::Format(_L("Printed in the air: %s"), area_text(row.area));
    case CheckRowKind::SeamsOnRegion:
        return wxString::Format(_L_PLURAL("%d seam on %s", "%d seams on %s", int(row.count)), int(row.count), utf8(row.region_label));
    case CheckRowKind::FirstLayerContact:
        return utf8(row.object) + middle_dot() + wxString::Format(_L("%s on the plate"), area_text(row.area));
    }
    return wxEmptyString;
}

wxString CheckPrintReportPanel::row_detail(const CheckRow& row) const
{
    const wxString object = row.object.empty() ? _L("Whole plate") : utf8(row.object);
    switch (row.kind) {
    case CheckRowKind::ToolpathOutside:
    case CheckRowKind::PathConflict:      return _L("Whole plate");
    case CheckRowKind::OrcaWarning:
        return row.applies_when == "timelapse" ? object + middle_dot() + _L("only when recording a timelapse") : object;
    case CheckRowKind::SupportInRegion:
    case CheckRowKind::SupportInHole: {
        wxString detail = object;
        if (row.z)
            detail += middle_dot() + wxString::Format(_L("around %s"), millimetres(*row.z));
        if (row.layers > 0)
            detail += middle_dot() + wxString::Format(_L_PLURAL("%d layer", "%d layers", int(row.layers)), int(row.layers));
        return detail;
    }
    case CheckRowKind::SupportClear:
        return wxString::Format(_L_PLURAL("%d layer of support", "%d layers of support", int(row.count)), int(row.count));
    case CheckRowKind::UnsupportedIsland:
        return row.z ? object + middle_dot() + millimetres(*row.z) : object;
    case CheckRowKind::SeamsOnRegion:     return object;
    case CheckRowKind::FirstLayerContact: return row.brim ? _L("Brim") : _L("No brim");
    }
    return wxEmptyString;
}

// -- Drawing ------------------------------------------------------------------

wxBitmap CheckPrintReportPanel::snapshot(int width)
{
    // Measure first, then draw at the measured height.
    int height = FromDIP(m_theme.metrics().group_header.height);
    {
        wxBitmap probe(width, 1);
        wxMemoryDC dc(probe);
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc)
            height = std::max(height, draw(*gc, 0, width));
    }
    wxBitmap bitmap(width, height);
    {
        wxMemoryDC dc(bitmap);
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(m_theme.palette(m_dark).surface_canvas));
            gc->DrawRectangle(0, 0, width, height);
            draw(*gc, 0, width);
        }
    }
    return bitmap;
}

void CheckPrintReportPanel::paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (gc)
        draw_view(*gc);
}

wxBitmap CheckPrintReportPanel::snapshot_view()
{
    const wxSize client = GetClientSize();
    wxBitmap     bitmap(std::max(1, client.x), std::max(1, client.y));
    {
        wxMemoryDC dc(bitmap);
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc)
            draw_view(*gc);
    }
    return bitmap;
}

void CheckPrintReportPanel::draw_view(wxGraphicsContext& gc_ref)
{
    wxGraphicsContext*  gc     = &gc_ref;
    const wxSize        client = GetClientSize();
    const ShellPalette& p      = m_theme.palette(m_dark);
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(p.surface_canvas));
    gc->DrawRectangle(0, 0, client.x, client.y);
    m_content_height = draw(*gc, -m_scroll, client.x);
    clamp_scroll();
    // The panel's edge against the preview.
    gc->SetBrush(wxBrush(p.border_subtle));
    gc->DrawRectangle(0, 0, FromDIP(1), client.y);
    // A report taller than the panel says so: a thin track at the right edge,
    // its thumb sized and placed by the part in view.
    if (const std::optional<std::pair<wxRect, wxRect>> bar = scrollbar()) {
        const double radius = FromDIP(m_theme.metrics().radius_small);
        gc->SetBrush(wxBrush(p.surface_subtle));
        gc->DrawRoundedRectangle(bar->first.x, bar->first.y, bar->first.width, bar->first.height, radius);
        gc->SetBrush(wxBrush(p.border_strong));
        gc->DrawRoundedRectangle(bar->second.x, bar->second.y, bar->second.width, bar->second.height, radius);
    }
}

std::optional<std::pair<wxRect, wxRect>> CheckPrintReportPanel::scrollbar() const
{
    const wxSize client = GetClientSize();
    if (m_content_height <= client.y || client.y <= 0)
        return std::nullopt;
    const ShellMetrics& m      = m_theme.metrics();
    const int           width  = FromDIP(m.space_1);
    const int           inset  = FromDIP(m.space_1);
    const wxRect        track(client.x - inset - width, inset, width, std::max(0, client.y - 2 * inset));
    const ScrollThumb   thumb = scrollbar_thumb(track.height, client.y, m_content_height, m_scroll, FromDIP(m.space_6));
    return std::make_pair(track, wxRect(track.x, track.y + thumb.offset, width, thumb.length));
}

void CheckPrintReportPanel::layout_now()
{
    const int width = GetClientSize().x > 0 ? GetClientSize().x : GetMinSize().x;
    if (width <= 0)
        return;
    wxBitmap   probe(width, 1);
    wxMemoryDC dc(probe);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (gc)
        m_content_height = draw(*gc, 0, width);
}

std::optional<wxRect> CheckPrintReportPanel::row_rect(RowRef row) const
{
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::Row && hit.section == row.first && hit.row == row.second)
            return wxRect(hit.rect.x, hit.rect.y - m_scroll, hit.rect.width, hit.rect.height);
    return std::nullopt;
}

void CheckPrintReportPanel::scroll_into_view(RowRef row)
{
    layout_now();
    const int pad = FromDIP(m_theme.metrics().space_4);
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::Row && hit.section == row.first && hit.row == row.second)
            m_scroll = scroll_to_show(hit.rect.y, hit.rect.y + hit.rect.height, m_scroll, GetClientSize().y, pad, m_content_height);
    clamp_scroll();
}

int CheckPrintReportPanel::draw(wxGraphicsContext& gc, int top, int width)
{
    const ShellMetrics& m   = m_theme.metrics();
    const int           pad = FromDIP(m.space_4);
    const int           gap = FromDIP(m.space_4);
    const int           inner = std::max(0, width - 2 * pad);
    m_hits.clear();
    m_drawn_lines.clear();

    int y = top + pad;
    if (!m_view.valid) {
        y += draw_wrapped(gc, _L("This plate has no current slice."), TextRole::BodySmall, m_theme.palette(m_dark).text_secondary,
                          pad, y, inner);
        return y + pad - top;
    }
    y += draw_summary(gc, pad, y, inner);
    for (std::size_t index = 0; index < m_view.sections.size(); ++index)
        y += gap + draw_section(gc, index, pad, y + gap, inner);
    if (!m_view.clean.empty())
        y += gap + draw_closing(gc, pad, y + gap, inner);
    // Hit areas are kept in content coordinates, whatever the scroll.
    for (Hit& hit : m_hits)
        hit.rect.y -= top;
    return y + pad - top;
}

int CheckPrintReportPanel::draw_wrapped(wxGraphicsContext& gc, const wxString& text, TextRole role, const wxColour& colour,
                                        int x, int y, int width)
{
    gc.SetFont(m_theme.font(role), colour);
    const int line_height = FromDIP(m_theme.type_style(role).line_height);
    int       height      = 0;
    for (const wxString& line : wrap(gc, text, width)) {
        double tw = 0, th = 0;
        gc.GetTextExtent(line, &tw, &th);
        gc.DrawText(line, x, y + height + std::round((line_height - th) / 2));
        m_drawn_lines.push_back(line);
        height += line_height;
    }
    return height;
}

int CheckPrintReportPanel::draw_summary(wxGraphicsContext& gc, int x, int y, int width)
{
    const ShellMetrics& m       = m_theme.metrics();
    const ShellPalette& p       = m_theme.palette(m_dark);
    const int           padding = FromDIP(m.space_3);
    const int           row_gap = FromDIP(m.space_1);
    const int           label_w = FromDIP(kSummaryLabelWidthDip);
    const int           column  = FromDIP(m.space_2);
    const int           value_w = std::max(0, width - 2 * padding - label_w - column);

    std::vector<std::pair<wxString, wxString>> rows;
    rows.push_back({_L("Time"), duration_text(m_view.summary.print_time_seconds)});
    for (std::size_t index = 0; index < m_view.summary.filaments.size(); ++index) {
        const FilamentLine& line = m_view.summary.filaments[index];
        wxString value = weight_and_length(line);
        if (line.extruder < m_context.filament_names.size() && !m_context.filament_names[line.extruder].empty())
            value = utf8(m_context.filament_names[line.extruder]) + middle_dot() + value;
        rows.push_back({index == 0 ? _L("Filament") : wxString(), value});
    }
    if (m_view.summary.has_cost)
        rows.push_back({_L("Cost"), cost_text(m_view.summary.cost, m_context.currency)});
    if (m_view.summary.filament_changes > 0)
        rows.push_back({_L("Changes"), wxString::Format(_L_PLURAL("%d filament change", "%d filament changes",
                                                                   int(m_view.summary.filament_changes)),
                                                        int(m_view.summary.filament_changes))});

    // Lay out first so the fill behind is drawn before the text.
    struct Laid { wxString label; std::vector<wxString> lines; };
    std::vector<Laid> laid;
    int content = 0;
    gc.SetFont(m_theme.font(TextRole::BodySmall), p.text_primary);
    for (const auto& [label, value] : rows) {
        laid.push_back({label, wrap(gc, value, value_w)});
        content += int(laid.back().lines.size()) * FromDIP(m_theme.type_style(TextRole::BodySmall).line_height);
    }
    content += row_gap * int(std::max<std::size_t>(rows.size(), 1) - 1);
    const int height = 2 * padding + content;

    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(p.surface_subtle));
    gc.DrawRoundedRectangle(x, y, width, height, FromDIP(m.radius_medium));

    const int body_line = FromDIP(m_theme.type_style(TextRole::BodySmall).line_height);
    int       row_y     = y + padding;
    for (const Laid& row : laid) {
        if (!row.label.empty()) {
            gc.SetFont(m_theme.font(TextRole::Label), p.text_secondary);
            double tw = 0, th = 0;
            gc.GetTextExtent(row.label, &tw, &th);
            // The label sits on the value's first line.
            gc.DrawText(row.label, x + padding, row_y + std::round((body_line - th) / 2));
            m_drawn_lines.push_back(row.label);
        }
        gc.SetFont(m_theme.font(TextRole::BodySmall), p.text_primary);
        for (std::size_t index = 0; index < row.lines.size(); ++index) {
            double tw = 0, th = 0;
            gc.GetTextExtent(row.lines[index], &tw, &th);
            gc.DrawText(row.lines[index], x + padding + label_w + column, row_y + int(index) * body_line + std::round((body_line - th) / 2));
            m_drawn_lines.push_back(row.lines[index]);
        }
        row_y += int(row.lines.size()) * body_line + row_gap;
    }
    return height;
}

int CheckPrintReportPanel::draw_section(wxGraphicsContext& gc, std::size_t index, int x, int y, int width)
{
    const ShellMetrics& m       = m_theme.metrics();
    const ShellPalette& p       = m_theme.palette(m_dark);
    const CheckSection& section = m_view.sections[index];
    const int           header  = FromDIP(m.group_header.height);
    const int           gap     = FromDIP(m.space_2);
    const int           icon    = FromDIP(m.button.icon.icon_size);

    // Header: title, count at the right, then the chevron.
    const wxString count = section_count(section);
    gc.SetFont(m_theme.font(TextRole::Label), p.text_secondary);
    double tw = 0, th = 0;
    gc.GetTextExtent(count, &tw, &th);
    const int chevron_x = x + width - icon;
    const int count_x   = chevron_x - gap - int(std::ceil(tw));
    gc.DrawText(count, count_x, y + std::round((header - th) / 2));
    gc.SetFont(m_theme.font(TextRole::BodySmallBold), p.text_primary);
    const wxString title = fit(gc, section_title(section.check), std::max(0, count_x - gap - x));
    gc.GetTextExtent(title, &tw, &th);
    gc.DrawText(title, x, y + std::round((header - th) / 2));
    m_drawn_lines.push_back(title + " | " + count);
    draw_header_icon(gc, is_collapsed(index) ? HeaderIcon::Right : HeaderIcon::Down, chevron_x, y + (header - icon) / 2.0, icon,
                     p.text_secondary);
    m_hits.push_back({Hit::Kind::Header, wxRect(x, y, width, header), index, 0});

    int height = header;
    if (is_collapsed(index))
        return height;
    for (std::size_t row = 0; row < section.rows.size(); ++row) {
        height += FromDIP(m.space_1);
        const RowRef ref{index, row};
        const int    row_y = y + height;
        const int    row_h = draw_row(gc, section.rows[row], m_selected == ref, m_hovered == ref, x, row_y, width);
        m_hits.push_back({Hit::Kind::Row, wxRect(x, row_y, width, row_h), index, row});
        height += row_h;
    }
    return height;
}

int CheckPrintReportPanel::draw_row(wxGraphicsContext& gc, const CheckRow& row, bool selected, bool hovered, int x, int y, int width)
{
    const ShellMetrics& m      = m_theme.metrics();
    const ShellPalette& p      = m_theme.palette(m_dark);
    const int           pad_x  = FromDIP(m.space_2);
    const int           pad_y  = FromDIP(m.space_1);
    const int           gap    = FromDIP(m.space_2);
    const int           dot    = FromDIP(m.status_dot.size);
    const int           text_x = x + pad_x + dot + gap;
    const int           text_w = std::max(0, width - 2 * pad_x - dot - gap);
    const int           body   = FromDIP(m_theme.type_style(TextRole::BodySmall).line_height);
    const int           label  = FromDIP(m_theme.type_style(TextRole::Label).line_height);

    const wxString message = row_message(row);
    const wxString detail  = row_detail(row);
    gc.SetFont(m_theme.font(TextRole::BodySmall), p.text_primary);
    const std::vector<wxString> message_lines = wrap(gc, message, text_w);
    gc.SetFont(m_theme.font(TextRole::Label), p.text_secondary);
    const std::vector<wxString> detail_lines = detail.empty() ? std::vector<wxString>{} : wrap(gc, detail, text_w);
    const int content = int(message_lines.size()) * body +
                        (detail_lines.empty() ? 0 : FromDIP(m.space_half) + int(detail_lines.size()) * label);
    const int height = 2 * pad_y + content;

    if (selected || hovered) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(selected ? p.surface_selected : p.surface_raised_hover));
        gc.DrawRoundedRectangle(x, y, width, height, FromDIP(m.radius_small));
    }
    if (row.severity != CheckSeverity::None) {
        gc.SetPen(*wxTRANSPARENT_PEN);
        gc.SetBrush(wxBrush(row.severity == CheckSeverity::Critical ? p.status_danger : p.status_warning));
        // Centred on the message's first line.
        gc.DrawEllipse(x + pad_x, y + pad_y + (body - dot) / 2.0, dot, dot);
    }
    int text_y = y + pad_y;
    gc.SetFont(m_theme.font(TextRole::BodySmall), p.text_primary);
    for (const wxString& line : message_lines) {
        double tw = 0, th = 0;
        gc.GetTextExtent(line, &tw, &th);
        gc.DrawText(line, text_x, text_y + std::round((body - th) / 2));
        m_drawn_lines.push_back(line);
        text_y += body;
    }
    if (!detail_lines.empty()) {
        text_y += FromDIP(m.space_half);
        gc.SetFont(m_theme.font(TextRole::Label), p.text_secondary);
        for (const wxString& line : detail_lines) {
            double tw = 0, th = 0;
            gc.GetTextExtent(line, &tw, &th);
            gc.DrawText(line, text_x, text_y + std::round((label - th) / 2));
            m_drawn_lines.push_back(line);
            text_y += label;
        }
    }
    return height;
}

int CheckPrintReportPanel::draw_closing(wxGraphicsContext& gc, int x, int y, int width)
{
    const ShellMetrics& m     = m_theme.metrics();
    const ShellPalette& p     = m_theme.palette(m_dark);
    const int           pad_x = FromDIP(m.space_2);
    const int           gap   = FromDIP(m.space_2);
    const int           icon  = FromDIP(m.button.icon.icon_size);
    const int           line  = FromDIP(m_theme.type_style(TextRole::Label).line_height);
    draw_header_icon(gc, HeaderIcon::Check, x + pad_x, y + (line - icon) / 2.0, icon, p.status_success);
    return std::max(icon, draw_wrapped(gc, closing_line(m_view), TextRole::Label, p.text_secondary, x + pad_x + icon + gap, y,
                                       std::max(0, width - 2 * pad_x - icon - gap)));
}

// -- Input --------------------------------------------------------------------

std::vector<CheckPrintReportPanel::RowRef> CheckPrintReportPanel::visible_rows() const
{
    std::vector<RowRef> rows;
    for (std::size_t section = 0; section < m_view.sections.size(); ++section)
        if (!is_collapsed(section))
            for (std::size_t row = 0; row < m_view.sections[section].rows.size(); ++row)
                rows.push_back({section, row});
    return rows;
}

void CheckPrintReportPanel::on_left_down(wxMouseEvent& event)
{
    SetFocus();
    const wxPoint point = event.GetPosition() + wxPoint(0, m_scroll);
    for (const Hit& hit : m_hits) {
        if (!hit.rect.Contains(point))
            continue;
        if (hit.kind == Hit::Kind::Header)
            toggle_section(hit.section);
        else
            select({hit.section, hit.row});
        return;
    }
    event.Skip();
}

void CheckPrintReportPanel::on_motion(wxMouseEvent& event)
{
    std::optional<RowRef> hovered;
    const wxPoint point = event.GetPosition() + wxPoint(0, m_scroll);
    for (const Hit& hit : m_hits)
        if (hit.kind == Hit::Kind::Row && hit.rect.Contains(point))
            hovered = RowRef{hit.section, hit.row};
    if (hovered != m_hovered) {
        m_hovered = hovered;
        SetCursor(hovered ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
        Refresh();
    }
    event.Skip();
}

void CheckPrintReportPanel::on_wheel(wxMouseEvent& event)
{
    const int lines = event.GetWheelRotation() / std::max(1, event.GetWheelDelta());
    m_scroll -= lines * FromDIP(m_theme.metrics().group_header.height);
    clamp_scroll();
    Refresh();
}

void CheckPrintReportPanel::on_key(wxKeyEvent& event)
{
    const int key = event.GetKeyCode();
    if (key != WXK_UP && key != WXK_DOWN) {
        event.Skip();
        return;
    }
    const std::vector<RowRef> rows = visible_rows();
    if (rows.empty())
        return;
    auto at = m_selected ? std::find(rows.begin(), rows.end(), *m_selected) : rows.end();
    if (at == rows.end())
        at = key == WXK_DOWN ? rows.begin() : rows.end() - 1;
    else if (key == WXK_DOWN && at + 1 != rows.end())
        ++at;
    else if (key == WXK_UP && at != rows.begin())
        --at;
    select(*at);
}

void CheckPrintReportPanel::clamp_scroll()
{
    m_scroll = std::clamp(m_scroll, 0, std::max(0, m_content_height - GetClientSize().y));
}

// -- CheckToggle --------------------------------------------------------------

CheckToggle::CheckToggle(wxWindow* parent, const ShellTheme& theme, const wxString& label)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS)
    , m_theme(theme)
{
    SetLabel(label);
    SetName(label);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetInitialSize(DoGetBestSize());
    Bind(wxEVT_PAINT, &CheckToggle::paint, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { SetFocus(); toggle(); });
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) { m_hovered = true; Refresh(); e.Skip(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { m_hovered = false; Refresh(); e.Skip(); });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { Refresh(); e.Skip(); });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_SPACE)
            toggle();
        else
            e.Skip();
    });
}

void CheckToggle::set_checked(bool checked)
{
    if (m_checked == checked)
        return;
    m_checked = checked;
    Refresh();
}

void CheckToggle::set_dark(bool dark)
{
    m_dark = dark;
    Refresh();
}

void CheckToggle::toggle()
{
    m_checked = !m_checked;
    Refresh();
    wxCommandEvent event(wxEVT_CHECKBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(m_checked ? 1 : 0);
    ProcessWindowEvent(event);
}

wxSize CheckToggle::DoGetBestSize() const
{
    const ShellMetrics& m = m_theme.metrics();
    wxClientDC dc(const_cast<CheckToggle*>(this));
    dc.SetFont(m_theme.font(TextRole::BodySmall));
    const wxSize text = dc.GetTextExtent(GetLabel());
    return wxSize(FromDIP(2 * m.space_2 + m.checkbox.size + m.checkbox.label_gap) + text.x, FromDIP(m.button.icon.height));
}

wxBitmap CheckToggle::snapshot()
{
    const wxSize size = GetSize();
    wxBitmap bitmap(size.x, size.y);
    {
        wxMemoryDC dc(bitmap);
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc)
            draw(*gc, size);
    }
    return bitmap;
}

void CheckToggle::paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (gc)
        draw(*gc, GetClientSize());
}

void CheckToggle::draw(wxGraphicsContext& gc, const wxSize& size)
{
    const ShellMetrics& m = m_theme.metrics();
    const ShellPalette& p = m_theme.palette(m_dark);
    gc.SetPen(*wxTRANSPARENT_PEN);
    gc.SetBrush(wxBrush(p.surface_raised));
    gc.DrawRectangle(0, 0, size.x, size.y);
    if (m_hovered) {
        gc.SetBrush(wxBrush(p.surface_raised_hover));
        gc.DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(m.radius_small));
    }
    const int box    = FromDIP(m.checkbox.size);
    const int glyph  = FromDIP(m.checkbox.glyph_size);
    const int left   = FromDIP(m.space_2);
    const int box_y  = (size.y - box) / 2;
    const int radius = FromDIP(m.checkbox.radius);
    gc.SetBrush(wxBrush(m_checked ? p.action_primary : p.surface_canvas));
    gc.SetPen(wxPen(m_checked ? p.action_primary : p.border_strong, 1));
    gc.DrawRoundedRectangle(left + 0.5, box_y + 0.5, box - 1, box - 1, radius);
    if (m_checked)
        draw_header_icon(gc, HeaderIcon::Check, left + (box - glyph) / 2.0, box_y + (box - glyph) / 2.0, glyph, p.action_primary_text);

    gc.SetFont(m_theme.font(TextRole::BodySmall), p.text_primary);
    double tw = 0, th = 0;
    gc.GetTextExtent(GetLabel(), &tw, &th);
    gc.DrawText(GetLabel(), left + box + FromDIP(m.checkbox.label_gap), std::round((size.y - th) / 2));

    if (HasFocus()) {
        gc.SetBrush(*wxTRANSPARENT_BRUSH);
        gc.SetPen(wxPen(p.border_focus, FromDIP(m.focus.width)));
        gc.DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, FromDIP(m.radius_small));
    }
}

} // namespace Slic3r::GUI::JusPrin
