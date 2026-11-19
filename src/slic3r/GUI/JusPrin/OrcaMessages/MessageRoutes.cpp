#include "MessageRoutes.hpp"
#include "OrcaMessages.hpp"

#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"

#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::OrcaMessages {

std::string printer_name(const std::string& device_id)
{
    for (const Printers::NamedPrinter& printer : Printers::named_printers())
        if (printer.device_id == device_id)
            return printer.name;
    if (DeviceManager* devices = wxGetApp().getDeviceManager())
        if (const MachineObject* machine = devices->get_my_machine(device_id); machine != nullptr && !machine->get_dev_name().empty())
            return machine->get_dev_name();
    return device_id;
}

void route(const TakenMessage& message, const Routes& routes)
{
    if (message.cause.told_elsewhere)
        return;
    using From = TakenMessage::From;
    if (message.from != From::Box && message.from != From::ErrorBox && message.from != From::AppDialog &&
        message.from != From::PrinterNotification)
        return;
    if (message.cause.kind == Cause::Kind::HostUpload && routes.fail_upload(message.cause.detail, message.text))
        return;
    const bool about_a_printer = (message.cause.kind == Cause::Kind::PrinterConnection || message.cause.kind == Cause::Kind::PrinterMessage) &&
                                 !message.cause.printer.empty();
    // What OrcaSlicer said is something that did not go as asked, which the
    // person reads after the fact: the entry ends as it begins.
    const std::string id = about_a_printer ? routes.begin_entry(kPrinterEntryKind, printer_name(message.cause.printer)) :
                                             routes.begin_entry(kMessageEntryKind, message.title.empty() ? _u8L("Info") : message.title);
    routes.finish_entry(id, "failed", message.text);
}

} // namespace Slic3r::GUI::JusPrin::OrcaMessages
