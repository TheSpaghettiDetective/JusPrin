#pragma once

// What the Project tab shows, as plain data. Like LeftPaneModel it has no wx
// and no Orca types, and it states only what the owners recorded: the model's
// metadata comes from the 3MF's project details. A field nobody recorded is absent here, never blank,
// zero, or guessed, so the pane has nothing to omit by hand.

#include "slic3r/GUI/JusPrin/Workspace/Workspace.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

// The model's own words about itself, as 3MF carries them. The saved project's
// name is a different fact and is never a fallback for `title`.
struct ModelInfoView
{
    std::string title, designer, license, copyright, origin, description;
    std::string profile_title, profile_description;

    bool empty() const
    {
        return title.empty() && designer.empty() && license.empty() && copyright.empty() && origin.empty() &&
               description.empty() && profile_title.empty() && profile_description.empty();
    }
};

struct AttachmentGroup
{
    std::string                              folder; // Orca's category, such as "Model Pictures"
    std::vector<Workspace::ProjectAttachment> files;
};

// The Details view: only sections the file backs with data.
struct ProjectDetailsView
{
    ModelInfoView                model;
    std::vector<AttachmentGroup> attachments;
    bool                         attachments_truncated{false};

    bool empty() const { return model.empty() && attachments.empty(); }
};

ProjectDetailsView build_details_view(const Workspace::ProjectDetails& details);

} // namespace Slic3r::GUI::JusPrin
