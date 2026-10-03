#include "ProjectPaneModel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Slic3r::GUI::JusPrin {

namespace {

// Days from the civil date to 1970-01-01 (Howard Hinnant's algorithm), so no
// time zone database or locale is involved in a UTC difference.
std::int64_t days_from_civil(std::int64_t year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned     yoe = static_cast<unsigned>(year - era * 400);
    const unsigned     doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned     doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::optional<std::int64_t> epoch_seconds(const std::string& text)
{
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != 'Z')
        return std::nullopt;
    for (std::size_t index = 0; index < text.size(); ++index)
        if (index != 4 && index != 7 && index != 10 && index != 13 && index != 16 && index != 19 &&
            !std::isdigit(static_cast<unsigned char>(text[index])))
            return std::nullopt;
    int year, month, day, hour, minute, second;
    char zone = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &year, &month, &day, &hour, &minute, &second, &zone) != 7 ||
        zone != 'Z' || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59)
        return std::nullopt;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int days_in_month[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (day > days_in_month[month - 1])
        return std::nullopt;
    return days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 + minute * 60 + second;
}

PrintOutcome outcome_of(const std::string& text)
{
    if (text == "completed") return PrintOutcome::Completed;
    if (text == "failed") return PrintOutcome::Failed;
    if (text == "cancelled") return PrintOutcome::Cancelled;
    return PrintOutcome::Unrecorded;
}

bool has_facts(const Agent::PhysicalPrintRecord& record)
{
    return !record.started_at.empty() || !record.ended_at.empty() || !record.outcome.empty() || !record.plate_name.empty() ||
           !record.printer.empty() || !record.material.empty() || !record.failure.empty() || record.stopped_percent.has_value() ||
           record.statistics.print_time_seconds > 0.0 || record.statistics.material_grams > 0.0 ||
           record.statistics.material_cost > 0.0;
}

} // namespace

std::optional<std::int64_t> seconds_between(const std::string& start, const std::string& end)
{
    const auto from = epoch_seconds(start);
    const auto to   = epoch_seconds(end);
    if (!from || !to || *to < *from)
        return std::nullopt;
    return *to - *from;
}

ProjectDetailsView build_details_view(const Workspace::ProjectDetails& details)
{
    ProjectDetailsView view;
    view.model.title               = details.title;
    view.model.designer            = details.designer;
    view.model.license             = details.license;
    view.model.copyright           = details.copyright;
    view.model.origin              = details.origin;
    view.model.description         = details.description;
    view.model.profile_title       = details.profile_title;
    view.model.profile_description = details.profile_description;

    for (const Workspace::ProjectAttachment& attachment : details.attachments) {
        auto group = std::find_if(view.attachments.begin(), view.attachments.end(),
                                  [&](const AttachmentGroup& candidate) { return candidate.folder == attachment.folder; });
        if (group == view.attachments.end()) {
            view.attachments.push_back({attachment.folder, {}});
            group = std::prev(view.attachments.end());
        }
        group->files.push_back(attachment);
    }
    const auto category_order = [](const std::string& folder) {
        if (folder == "Model Pictures") return 0;
        if (folder == "Bill of Materials") return 1;
        if (folder == "Assembly Guide") return 2;
        if (folder == "Others") return 3;
        return 4;
    };
    std::stable_sort(view.attachments.begin(), view.attachments.end(), [&](const AttachmentGroup& left, const AttachmentGroup& right) {
        return category_order(left.folder) < category_order(right.folder);
    });
    view.attachments_truncated = details.attachments_truncated;
    return view;
}

PrintHistoryView build_print_history(const std::vector<Agent::PhysicalPrintRecord>& records)
{
    PrintHistoryView view;
    view.total = records.size();
    for (auto record = records.rbegin(); record != records.rend(); ++record) {
        if (!has_facts(*record)) {
            ++view.count_only;
            continue;
        }
        PrintHistoryEntry entry;
        entry.id         = record->id;
        entry.outcome    = outcome_of(record->outcome);
        entry.recorded_outcome = record->outcome;
        entry.started_at = record->started_at;
        entry.ended_at   = record->ended_at;
        entry.duration_seconds = seconds_between(record->started_at, record->ended_at);
        entry.plate_name = record->plate_name;
        entry.printer    = record->printer;
        entry.material   = record->material;
        entry.failure    = record->failure;
        entry.stopped_percent = record->stopped_percent;
        if (record->statistics.print_time_seconds > 0.0)
            entry.estimated_seconds = record->statistics.print_time_seconds;
        if (record->statistics.material_grams > 0.0)
            entry.estimated_grams = record->statistics.material_grams;
        // The ledger records no currency; the pane shows dollars.
        if (record->statistics.material_cost > 0.0)
            entry.estimated_cost = record->statistics.material_cost;
        view.detailed.push_back(std::move(entry));
    }
    return view;
}

} // namespace Slic3r::GUI::JusPrin
