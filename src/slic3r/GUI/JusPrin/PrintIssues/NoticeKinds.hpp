#pragma once

// What JusPrin does with each kind of OrcaSlicer notification.
//
// Inside the shell OrcaSlicer's own notifications are not drawn, so a kind
// that has no route here is shown nowhere. The table in NoticeKinds.cpp names
// every kind, and a kind OrcaSlicer adds stops the build there until someone
// decides where it goes.

#include "PrintIssue.hpp"

namespace Slic3r::GUI {
enum class NotificationType;
}

namespace Slic3r::GUI::JusPrin::PrintIssues {

NoticeRoute notice_route(NotificationType kind);
// The kind's enumerator, as OrcaSlicer spells it.
const char* notice_kind_name(NotificationType kind);
// Whether OrcaSlicer holds several notifications of this print-problem kind
// at once: plate warnings by their words, slicing warnings by message id.
bool notice_comes_in_several(NotificationType kind);

} // namespace Slic3r::GUI::JusPrin::PrintIssues
