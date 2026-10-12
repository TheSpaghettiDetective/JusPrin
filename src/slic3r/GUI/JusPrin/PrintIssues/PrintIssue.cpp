#include "PrintIssue.hpp"

#include <algorithm>
#include <functional>

namespace Slic3r::GUI::JusPrin::PrintIssues {

const char* source_name(IssueSource source)
{
    switch (source) {
    case IssueSource::Notice:       return "notice";
    case IssueSource::Validation:   return "validation";
    case IssueSource::Placement:    return "placement";
    case IssueSource::SliceStep:    return "slice_step";
    case IssueSource::SliceResult:  return "slice_result";
    case IssueSource::SliceFailure: return "slice_failure";
    }
    return "";
}

const char* severity_name(IssueSeverity severity) { return severity == IssueSeverity::Blocker ? "blocker" : "warning"; }

bool Notice::operator==(const Notice& other) const
{
    return kind == other.kind && route == other.route && level == other.level && text == other.text;
}

bool PrintIssue::operator==(const PrintIssue& other) const
{
    return id == other.id && source == other.source && severity == other.severity && code == other.code &&
           setting == other.setting && message == other.message && plate == other.plate && object == other.object &&
           instance == other.instance && object_name == other.object_name && stale == other.stale && ignored == other.ignored;
}

const PrintIssue* PrintIssueSnapshot::find(const std::string& id) const
{
    const auto found = std::find_if(issues.begin(), issues.end(), [&id](const PrintIssue& issue) { return issue.id == id; });
    return found == issues.end() ? nullptr : &*found;
}

bool PrintIssueSnapshot::same_content(const PrintIssueSnapshot& other) const
{
    return session == other.session && plate == other.plate && checked == other.checked && slicing == other.slicing &&
           sliced == other.sliced && issues == other.issues && notices == other.notices;
}

std::vector<PrintIssue> issues_from(const std::vector<Notice>& held, std::vector<PrintIssue> found, std::uint64_t plate)
{
    const auto holds = [&held](int kind) {
        return std::any_of(held.begin(), held.end(), [kind](const Notice& notice) { return notice.kind == kind; });
    };
    std::vector<PrintIssue> issues;
    for (PrintIssue& issue : found)
        if (issue.unprompted || (issue.answers >= 0 && holds(issue.answers)))
            issues.push_back(std::move(issue));

    const std::size_t answered = issues.size();
    for (const Notice& notice : held) {
        if (notice.route != NoticeRoute::Issue)
            continue;
        if (std::any_of(issues.begin(), issues.begin() + answered, [&notice](const PrintIssue& issue) { return issue.answers == notice.kind; }))
            continue;
        PrintIssue issue;
        issue.source   = IssueSource::Notice;
        issue.severity = notice.level == NoticeLevel::Error ? IssueSeverity::Blocker : IssueSeverity::Warning;
        issue.code     = notice.kind_name;
        issue.message  = notice.body;
        issue.plate    = plate;
        issue.answers  = notice.kind;
        // One of a kind is that kind. Where OrcaSlicer keeps several apart by
        // their words, so does this: its words now against its words before.
        issue.id = "notice:" + notice.kind_name + (notice.several ? ":" + std::to_string(std::hash<std::string>{}(notice.text)) : std::string());
        issues.push_back(std::move(issue));
    }
    return issues;
}

void mark_ignored(std::vector<PrintIssue>& issues, std::set<std::string>& ignored, bool settled)
{
    std::set<std::string> reported;
    for (PrintIssue& issue : issues) {
        issue.ignored = issue.severity == IssueSeverity::Warning && ignored.count(issue.id) != 0;
        if (issue.ignored)
            reported.insert(issue.id);
    }
    if (settled)
        ignored = std::move(reported);
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
