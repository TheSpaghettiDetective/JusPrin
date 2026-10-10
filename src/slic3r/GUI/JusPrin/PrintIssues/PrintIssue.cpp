#include "PrintIssue.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin::PrintIssues {

const char* source_name(IssueSource source)
{
    switch (source) {
    case IssueSource::Validation:   return "validation";
    case IssueSource::Placement:    return "placement";
    case IssueSource::PlateCheck:   return "plate_check";
    case IssueSource::SliceStep:    return "slice_step";
    case IssueSource::SliceResult:  return "slice_result";
    case IssueSource::SliceFailure: return "slice_failure";
    }
    return "";
}

const char* severity_name(IssueSeverity severity) { return severity == IssueSeverity::Blocker ? "blocker" : "warning"; }

bool PrintIssue::operator==(const PrintIssue& other) const
{
    return id == other.id && source == other.source && severity == other.severity && code == other.code &&
           setting == other.setting && message == other.message && plate == other.plate && object == other.object &&
           instance == other.instance && object_name == other.object_name && stale == other.stale;
}

const PrintIssue* PrintIssueSnapshot::find(const std::string& id) const
{
    const auto found = std::find_if(issues.begin(), issues.end(), [&id](const PrintIssue& issue) { return issue.id == id; });
    return found == issues.end() ? nullptr : &*found;
}

bool PrintIssueSnapshot::same_content(const PrintIssueSnapshot& other) const
{
    return session == other.session && plate == other.plate && checked == other.checked && slicing == other.slicing &&
           sliced == other.sliced && issues == other.issues;
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
