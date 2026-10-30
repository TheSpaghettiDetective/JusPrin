// Where each kind of OrcaSlicer notification goes inside the shell. Linked
// against the application library, because the kinds are OrcaSlicer's.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/PrintIssues/NoticeKinds.hpp"
#include "slic3r/GUI/NotificationManager.hpp"

#include <set>
#include <string>

using namespace Slic3r::GUI;
using namespace Slic3r::GUI::JusPrin::PrintIssues;

TEST_CASE("every notification kind has a name and a route", "[print-issues][notices]")
{
    std::set<std::string> names;
    for (int kind = 0; kind < int(NotificationType::NotificationTypeCount); ++kind) {
        const std::string name = notice_kind_name(NotificationType(kind));
        CAPTURE(kind);
        CHECK_FALSE(name.empty());
        CHECK(names.insert(name).second);
        // Calls the route for every kind; it cannot fail, since NotShown is
        // both the last route and the fallback. That every kind is routed
        // exactly once is enforced when NoticeKinds.cpp compiles (-Wswitch
        // as an error, and duplicate cases).
        CHECK(int(notice_route(NotificationType(kind))) <= int(NoticeRoute::NotShown));
    }
}

TEST_CASE("print problems go to the issue list and outcomes do not", "[print-issues][notices]")
{
    for (const NotificationType kind : {NotificationType::ValidateError, NotificationType::ValidateWarning, NotificationType::SlicingError,
                                        NotificationType::SlicingWarning, NotificationType::PlaterError, NotificationType::PlaterWarning,
                                        NotificationType::BBLFlushingVolumeZero, NotificationType::BBLMixUsePLAAndPETG,
                                        NotificationType::LeftExtruderUnprintableError, NotificationType::BBLBedFilamentIncompatible}) {
        CAPTURE(notice_kind_name(kind));
        CHECK(notice_route(kind) == NoticeRoute::Issue);
    }
    // The original handoff's rule: a failed operation is not a print problem.
    for (const NotificationType kind : {NotificationType::CustomNotification, NotificationType::BBLGeneralError, NotificationType::ExportFinished,
                                        NotificationType::BBLPlateInfo, NotificationType::PrintHostUpload}) {
        CAPTURE(notice_kind_name(kind));
        CHECK(notice_route(kind) != NoticeRoute::Issue);
    }
    CHECK(notice_route(NotificationType::SlicingProgress) == NoticeRoute::NotShown); // the header shows it
    CHECK(notice_comes_in_several(NotificationType::PlaterWarning));
    CHECK_FALSE(notice_comes_in_several(NotificationType::ValidateError));
}
