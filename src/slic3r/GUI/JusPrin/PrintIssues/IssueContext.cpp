#include "IssueContext.hpp"

#include <nlohmann/json.hpp>

#include <functional>

namespace Slic3r::GUI::JusPrin::PrintIssues {

IssueMessage make_issue_message(const PrintIssue& issue, const PrintIssueSnapshot& snapshot, const IssueMessageRequest& request)
{
    nlohmann::json context{
        {"kind", "print_issue"},
        {"issue", {{"id", issue.id}, {"source", source_name(issue.source)}, {"severity", severity_name(issue.severity)},
                   {"code", issue.code}, {"setting", issue.setting}, {"message", issue.message}}},
        {"target", {{"scope", issue.instance != 0 ? "copy" : issue.object != 0 ? "object" : "plate"}, {"plateId", issue.plate}}},
        {"evidence", {{"checked", snapshot.checked}, {"generation", snapshot.generation}, {"sliced", snapshot.sliced},
                      {"slicing", snapshot.slicing}}},
        {"workspace", {{"sessionId", std::to_string(request.workspace_session)}, {"revision", request.workspace_revision}}}};
    if (issue.object != 0) {
        context["target"]["objectId"]   = issue.object;
        context["target"]["objectName"] = issue.object_name;
    }
    if (issue.instance != 0)
        context["target"]["copyId"] = issue.instance;
    // The model should know the person has already waved this one through.
    if (issue.ignored)
        context["issue"]["ignored"] = true;

    std::string words = issue.message;
    while (!words.empty() && (words.back() == '\n' || words.back() == '\r' || words.back() == ' '))
        words.pop_back();
    // A quote block, so a message of several lines stays one quotation.
    std::string quoted = "> ";
    for (const char c : words) {
        quoted += c;
        if (c == '\n')
            quoted += "> ";
    }

    IssueMessage message;
    message.text         = request.request_sentence + "\n\n" + quoted;
    message.context_json = context.dump();
    // One message per issue, request, evidence and chat: a second press of the
    // same button while nothing has changed is the same question.
    message.client_message_id = "issue-" + std::to_string(std::hash<std::string>{}(request.request_sentence + '\n' + issue.id)) +
                                "-s" + std::to_string(snapshot.session) + "-g" +
                                std::to_string(snapshot.generation) + "-c" + request.conversation_id;
    return message;
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
