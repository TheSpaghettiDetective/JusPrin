#include "ProjectPaneModel.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin {

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

} // namespace Slic3r::GUI::JusPrin
