#include "LoadFacts.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {
constexpr double kTolerance = 1e-3; // mm

struct Box
{
    double min_x{std::numeric_limits<double>::max()}, min_y{min_x};
    double max_x{std::numeric_limits<double>::lowest()}, max_y{max_x};

    void merge(const BedPoint& p)
    {
        min_x = std::min(min_x, p.x), min_y = std::min(min_y, p.y);
        max_x = std::max(max_x, p.x), max_y = std::max(max_y, p.y);
    }
    bool valid() const { return min_x <= max_x && min_y <= max_y; }
};

double distance_to_segment(const BedPoint& p, const BedPoint& a, const BedPoint& b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double length2 = dx * dx + dy * dy;
    double t = length2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Inside or on the outline.
bool inside(const std::vector<BedPoint>& outline, const BedPoint& p)
{
    bool in = false;
    for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++) {
        const BedPoint& a = outline[i];
        const BedPoint& b = outline[j];
        if (distance_to_segment(p, a, b) <= kTolerance)
            return true;
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

// A rectangle lies in the printable area when its corners do and no corner
// of the outline pokes into it. That is exact for the convex outlines
// printers use (rectangles and circles) and for notched rectangles.
bool inside(const std::vector<BedPoint>& outline, const Box& rect)
{
    for (const BedPoint& corner : {BedPoint{rect.min_x, rect.min_y}, BedPoint{rect.max_x, rect.min_y},
                                   BedPoint{rect.max_x, rect.max_y}, BedPoint{rect.min_x, rect.max_y}})
        if (!inside(outline, corner))
            return false;
    return std::none_of(outline.begin(), outline.end(), [&rect](const BedPoint& p) {
        return p.x > rect.min_x + kTolerance && p.x < rect.max_x - kTolerance && p.y > rect.min_y + kTolerance &&
               p.y < rect.max_y - kTolerance;
    });
}

// Overlap with positive area between a convex outline and a box, by the
// separating axis test: the box's two axes and each edge's normal.
bool overlaps(const std::vector<BedPoint>& outline, const Box& box)
{
    const BedPoint corners[] = {{box.min_x, box.min_y}, {box.max_x, box.min_y}, {box.max_x, box.max_y}, {box.min_x, box.max_y}};
    std::vector<BedPoint> axes{{1, 0}, {0, 1}};
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const BedPoint& a = outline[i];
        const BedPoint& b = outline[(i + 1) % outline.size()];
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (length > kTolerance)
            axes.push_back({(a.y - b.y) / length, (b.x - a.x) / length});
    }
    for (const BedPoint& axis : axes) {
        double min_a = std::numeric_limits<double>::max(), max_a = std::numeric_limits<double>::lowest();
        double min_b = min_a, max_b = max_a;
        for (const BedPoint& p : outline) {
            const double d = p.x * axis.x + p.y * axis.y;
            min_a = std::min(min_a, d), max_a = std::max(max_a, d);
        }
        for (const BedPoint& p : corners) {
            const double d = p.x * axis.x + p.y * axis.y;
            min_b = std::min(min_b, d), max_b = std::max(max_b, d);
        }
        if (max_a <= min_b + kTolerance || max_b <= min_a + kTolerance)
            return false;
    }
    return true;
}

std::vector<Box> excluded_boxes(const std::vector<BedPoint>& points)
{
    std::vector<Box> boxes;
    for (std::size_t first = 0; first + 4 <= points.size(); first += 4) {
        Box box;
        for (std::size_t i = first; i < first + 4; ++i)
            box.merge(points[i]);
        // A box with no area (the filler some profiles write) intersects
        // nothing, as Orca's polygon intersection finds.
        if (box.max_x - box.min_x > kTolerance && box.max_y - box.min_y > kTolerance)
            boxes.push_back(box);
    }
    return boxes;
}

std::string trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r");
    const auto last  = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(text);
    while (std::getline(stream, part, separator))
        parts.push_back(trim(part));
    return parts;
}

double leading_number(const std::string& text)
{
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        return 0;
    }
}

void set_filaments(const std::vector<std::string>& types, const std::vector<std::string>& colours, LoadedFileFacts& facts)
{
    facts.filaments.clear();
    for (std::size_t i = 0; i < std::max(types.size(), colours.size()); ++i)
        facts.filaments.push_back({i < types.size() ? types[i] : std::string(), i < colours.size() ? colours[i] : std::string()});
}
} // namespace

BedSize bed_size(const BedShape& bed)
{
    Box box;
    for (const BedPoint& p : bed.area)
        box.merge(p);
    if (!box.valid())
        return {0, 0, bed.height};
    return {box.max_x - box.min_x, box.max_y - box.min_y, bed.height};
}

bool fits_bed(const BedShape& bed, const std::vector<BedPoint>& footprint, double height)
{
    if (bed.area.size() < 3 || footprint.empty() || height > bed.height + kTolerance)
        return false;
    Box area;
    for (const BedPoint& p : bed.area)
        area.merge(p);
    const std::vector<Box> excluded = excluded_boxes(bed.excluded);
    const double center_x = (area.min_x + area.max_x) / 2, center_y = (area.min_y + area.max_y) / 2;
    for (const bool turned : {false, true}) {
        std::vector<BedPoint> outline;
        Box extent;
        for (const BedPoint& p : footprint) {
            outline.push_back(turned ? BedPoint{-p.y, p.x} : p);
            extent.merge(outline.back());
        }
        const double w = extent.max_x - extent.min_x, d = extent.max_y - extent.min_y;
        // The centre and the four corners of the area's bounding box: an
        // excluded area is a corner or an edge strip, so one of these is the
        // placement that avoids it whenever any does.
        const std::pair<double, double> origins[] = {{center_x - w / 2, center_y - d / 2},
                                                     {area.min_x, area.min_y},
                                                     {area.max_x - w, area.min_y},
                                                     {area.min_x, area.max_y - d},
                                                     {area.max_x - w, area.max_y - d}};
        for (const auto& [x, y] : origins) {
            const Box rect{x, y, x + w, y + d};
            if (!inside(bed.area, rect))
                continue;
            std::vector<BedPoint> placed;
            for (const BedPoint& p : outline)
                placed.push_back({p.x - extent.min_x + x, p.y - extent.min_y + y});
            if (std::none_of(excluded.begin(), excluded.end(), [&placed](const Box& box) { return overlaps(placed, box); }))
                return true;
        }
    }
    return false;
}

bool fits_bed(const BedShape& bed, double width, double depth, double height)
{
    return fits_bed(bed, {{0, 0}, {width, 0}, {width, depth}, {0, depth}}, height);
}

std::string application_from_model_head(const std::string& head)
{
    static const std::regex pattern(R"(<metadata\s+name\s*=\s*"Application"\s*>([^<]*)<)");
    std::smatch match;
    return std::regex_search(head, match, pattern) ? match[1].str() : std::string();
}

void read_project_settings(const std::string& json_text, LoadedFileFacts& facts)
{
    const nlohmann::json settings = nlohmann::json::parse(json_text, nullptr, false);
    if (!settings.is_object())
        return;
    const auto strings = [&settings](const char* key) {
        std::vector<std::string> values;
        const auto found = settings.find(key);
        if (found == settings.end())
            return values;
        if (found->is_string())
            values.push_back(found->get<std::string>());
        else if (found->is_array())
            for (const auto& value : *found)
                if (value.is_string())
                    values.push_back(value.get<std::string>());
        return values;
    };
    if (const auto model = strings("printer_model"); !model.empty())
        facts.printer_model = model.front();
    if (const auto nozzles = strings("nozzle_diameter"); !nozzles.empty())
        facts.nozzle_mm = leading_number(nozzles.front());
    set_filaments(strings("filament_type"), strings("filament_colour"), facts);
}

void read_prusa_config(const std::string& ini_text, LoadedFileFacts& facts)
{
    std::vector<std::string> types, colours;
    for (const std::string& line : split(ini_text, '\n')) {
        if (line.empty() || line.front() != ';')
            continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string name  = trim(line.substr(1, equals - 1));
        const std::string value = trim(line.substr(equals + 1));
        if (name == "printer_model")
            facts.printer_model = value;
        else if (name == "nozzle_diameter")
            facts.nozzle_mm = leading_number(value);
        else if (name == "filament_type")
            types = split(value, ';');
        else if (name == "filament_colour")
            colours = split(value, ';');
    }
    if (!types.empty() || !colours.empty())
        set_filaments(types, colours, facts);
}

} // namespace Slic3r::GUI::JusPrin::Workspace
