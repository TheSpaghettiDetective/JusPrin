#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Shell/ProjectPaneModel.hpp"

using namespace Slic3r::GUI::JusPrin;
using Slic3r::GUI::JusPrin::Workspace::ProjectAttachment;
using Slic3r::GUI::JusPrin::Workspace::ProjectDetails;

TEST_CASE("A project with no model metadata has an empty details view", "[projectpane]")
{
    ProjectDetails details;
    details.backup_current = false; // unrelated state never counts as metadata
    CHECK(build_details_view(details).empty());
    CHECK(build_details_view(details).model.empty());
}

TEST_CASE("Only the metadata the file carries is present, and the title is never invented", "[projectpane]")
{
    ProjectDetails details;
    details.designer = "Ada";
    const ProjectDetailsView view = build_details_view(details);
    CHECK_FALSE(view.empty());
    CHECK(view.model.designer == "Ada");
    CHECK(view.model.title.empty());
    CHECK(view.model.license.empty());
}

TEST_CASE("Attachments group by Orca's category in the order they arrive", "[projectpane]")
{
    ProjectDetails details;
    details.attachments = {{"Metadata/a.png", "Model Pictures", 10}, {"Metadata/bom.csv", "Bill of Materials", 4},
                           {"Metadata/b.png", "Model Pictures", 12}};
    details.attachments_truncated = true;
    const ProjectDetailsView view = build_details_view(details);
    REQUIRE(view.attachments.size() == 2);
    CHECK(view.attachments[0].folder == "Model Pictures");
    CHECK(view.attachments[0].files.size() == 2);
    CHECK(view.attachments[1].folder == "Bill of Materials");
    CHECK(view.attachments_truncated);
    CHECK_FALSE(view.empty());
}
