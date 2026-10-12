#include "NoticeKinds.hpp"

#include "slic3r/GUI/NotificationManager.hpp"

// A kind missing from the table below is an error here, not a warning.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic error "-Wswitch"
#elif defined(_MSC_VER)
#pragma warning(error : 4062)
#endif

namespace Slic3r::GUI::JusPrin::PrintIssues {

// Every kind OrcaSlicer defines is in exactly one of the five lists below.
// The last two are what a person inside the shell does NOT see; the third is
// seen without its bar or its buttons.

// Print problems. Each enters the issue list, with its object when a lookup
// can name one (see PrintIssueMonitor).
#define JUSPRIN_ISSUE_KINDS(X) \
    X(ValidateError) \
    X(ValidateWarning) \
    X(SlicingError) \
    X(SlicingSeriousWarning) \
    X(SlicingWarning) \
    X(PlaterError) \
    X(LeftExtruderUnprintableError) \
    X(RightExtruderUnprintableError) \
    X(PlaterWarning) \
    X(BBLSliceEmptyLayer) \
    X(BBLNeedSupportON) \
    X(BBLGcodeOverlap) \
    X(BBLFlushingVolumeZero) \
    X(BBLFilamentPrintableError) \
    X(BBLSliceLimitError) \
    X(BBLSliceMultiExtruderHeightOutside) \
    X(BBLBedFilamentIncompatible) \
    X(BBLMixUsePLAAndPETG) \
    X(BBLNozzleFilamentIncompatible)

// The outcome of something the person just did, or a remark from the tool they
// are using. Drawn as a short confirmation above the status pill, in
// OrcaSlicer's words, with a close button.
// MISSING: whatever action OrcaSlicer attached to its own notification. For
// ExportFinished that is "open folder" and "eject".
#define JUSPRIN_RESULT_KINDS(X) \
    X(CustomNotification) \
    X(ExportFinished) \
    X(Mouse3dDisconnected) \
    X(PresetUpdateFinished) \
    X(BBLGeneralError) \
    X(EmptyColorChangeCode) \
    X(CustomSupportsAndSeamRemovedAfterRepair) \
    X(EmptyAutoColorChange) \
    X(SignDetected) \
    X(QuitSLAManualMode) \
    X(MmSegmentationExceededExtrudersLimit) \
    X(UpdatedItemsInfo) \
    X(SimplifySuggestion) \
    X(UnknownFont) \
    X(CgalFinished) \
    X(BBLPlateInfo) \
    X(BBL3MFInfo) \
    X(BBLSeqPrintInfo) \
    X(BBLPreviewOnlyMode)

// Work that is still running. Each becomes an entry in the chat (see
// WorkFeed), with a Cancel where the work runs on the job worker. Slicing is
// not here: it is followed through its own events, which carry a percentage
// and an outcome.
// MISSING: a percentage, and how the work ended. OrcaSlicer's notification
// says only that it is running, so an entry that goes is recorded as done
// unless its Cancel was pressed. Pause and open for a download are not offered.
#define JUSPRIN_PROGRESS_KINDS(X) \
    X(ProgressBar) /* declared by OrcaSlicer, never pushed */ \
    X(PrintHostUpload) /* an upload to a network printer: bar, cancel, error state */ \
    X(ProgressIndicator) /* the bar of the background job worker: arrange, orient, fill the plate, text and SVG, SLA import */ \
    X(ExportOngoing) /* "Exporting." while a file is written */ \
    X(URLDownload) /* a download from a link: bar, pause, cancel, open */

// NOT SHOWN ANYWHERE YET. Application and account state: it belongs on Home
// or in Preferences, not in a project, and that place does not exist. Each
// stays in OrcaSlicer's list and reappears if the person switches to the
// classic view.
#define JUSPRIN_APP_KINDS(X) \
    X(NewAppAvailable) /* a new version of the application */ \
    X(NewAlphaAvailable) /* declared by OrcaSlicer, never pushed */ \
    X(NewBetaAvailable) /* declared by OrcaSlicer, never pushed */ \
    X(PresetUpdateAvailable) /* printer profiles can update; its link opens the update dialog */ \
    X(DesktopIntegrationSuccess) /* Linux desktop integration */ \
    X(DesktopIntegrationFail) /* Linux desktop integration */ \
    X(UndoDesktopIntegrationSuccess) /* Linux desktop integration */ \
    X(UndoDesktopIntegrationFail) /* Linux desktop integration */ \
    X(BBLPluginInstallHint) /* the network plug-in is missing; its link installs it */ \
    X(BBLPluginUpdateAvailable) /* a newer network plug-in; its link opens the update */ \
    X(BBLPrinterConfigUpdateAvailable) /* newer printer configuration; its link opens the update */ \
    X(BBLUserPresetExceedLimit) /* too many presets stored in the cloud */ \
    X(OrcaSharedProfilesAvailable) /* shared profiles exist for this printer; two links */ \
    X(OrcaCloudAPIError) /* declared by OrcaSlicer, never pushed */ \
    X(OrcaSyncConflict) /* a preset sync conflict; its two buttons are the only way to resolve it */

// NOT SHOWN, on purpose.
#define JUSPRIN_NOT_SHOWN_KINDS(X) \
    X(SlicingProgress) /* the header shows slicing progress and its cancel */ \
    X(DidYouKnowHint) /* the tips shown at start */ \
    X(ArrangeOngoing) /* "Arranging..." beside the job worker's own ProgressIndicator, which already reports the same arrange */ \
    X(BBLObjectInfo) /* the object size card; with open edges it offers Repair, which is then lost */

#define JUSPRIN_NOTICE_KINDS(X) \
    JUSPRIN_ISSUE_KINDS(X##_ISSUE) JUSPRIN_RESULT_KINDS(X##_RESULT) JUSPRIN_PROGRESS_KINDS(X##_PROGRESS) \
    JUSPRIN_APP_KINDS(X##_APP) JUSPRIN_NOT_SHOWN_KINDS(X##_NOT_SHOWN)

NoticeRoute notice_route(NotificationType kind)
{
    switch (kind) {
#define ROUTE_ISSUE(name)     case NotificationType::name: return NoticeRoute::Issue;
#define ROUTE_RESULT(name)    case NotificationType::name: return NoticeRoute::Result;
#define ROUTE_PROGRESS(name)  case NotificationType::name: return NoticeRoute::Progress;
#define ROUTE_APP(name)       case NotificationType::name: return NoticeRoute::App;
#define ROUTE_NOT_SHOWN(name) case NotificationType::name: return NoticeRoute::NotShown;
        JUSPRIN_NOTICE_KINDS(ROUTE)
    case NotificationType::NotificationTypeCount: break;
    }
    return NoticeRoute::NotShown;
}

const char* notice_kind_name(NotificationType kind)
{
    switch (kind) {
#define NAME_ISSUE(name)     case NotificationType::name: return #name;
#define NAME_RESULT(name)    NAME_ISSUE(name)
#define NAME_PROGRESS(name)  NAME_ISSUE(name)
#define NAME_APP(name)       NAME_ISSUE(name)
#define NAME_NOT_SHOWN(name) NAME_ISSUE(name)
        JUSPRIN_NOTICE_KINDS(NAME)
    case NotificationType::NotificationTypeCount: break;
    }
    return "";
}

bool notice_comes_in_several(NotificationType kind)
{
    return kind == NotificationType::PlaterWarning || kind == NotificationType::SlicingWarning;
}

} // namespace Slic3r::GUI::JusPrin::PrintIssues
