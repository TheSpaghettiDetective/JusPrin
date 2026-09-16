#include "PrinterDiscovery.hpp"

#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"

#include <algorithm>
#include <chrono>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

namespace {

// The nozzle and the loaded spools, as the device last reported them. The
// plate is not among them: MachineObject carries no build-plate type.
void read_reported_hardware(MachineObject& machine, DiscoveredPrinter& printer)
{
    if (const DevExtderSystem* extruders = machine.GetExtderSystem())
        printer.nozzle_diameter = extruders->GetNozzleDiameter(0);
    DevFilaSystem* filaments = machine.GetFilaSystem();
    if (!filaments) return;
    for (const auto& [ams_id, ams] : filaments->GetAmsList()) {
        if (!ams) continue;
        if (printer.ams_name.empty())
            printer.ams_name = ams->GetAmsType() == DevAms::AMS_LITE ? "AMS lite" : "AMS";
        for (const auto& [tray_id, tray] : ams->GetTrays()) {
            if (!tray || !tray->is_exists) continue;
            const std::string material = tray->sub_brands.empty() ? tray->get_display_filament_type() : tray->sub_brands;
            if (material.empty()) continue;
            printer.spools.push_back({(tray->is_bbl ? "Bambu " : "") + material,
                                      std::string(tray->get_color().GetAsString(wxC2S_HTML_SYNTAX).ToUTF8())});
        }
    }
}

} // namespace

std::vector<DiscoveredPrinter> discover_printers(bool include_unreachable)
{
    std::vector<DiscoveredPrinter> result;
    DeviceManager* devices = wxGetApp().getDeviceManager();
    if (!devices) return result;
    for (const auto& [id, machine] : devices->get_my_machine_list()) {
        if (!machine) continue;
        const bool reachable = machine->is_online() || machine->is_connected();
        if (!reachable && !include_unreachable) continue;
        DiscoveredPrinter printer{id, machine->get_dev_name(), machine->get_dev_ip(), machine->printer_type,
                                  machine->connection_type(), reachable};
        // Upstream's own predicate, never a copy of it: the list of states that
        // count as printing has been got wrong here before.
        printer.activity = !machine->is_connected()         ? PrinterActivity::Offline :
                           machine->is_in_printing()        ? PrinterActivity::Printing :
                                                              PrinterActivity::Idle;
        if (machine->last_update_time.time_since_epoch().count() != 0)
            printer.observed_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         machine->last_update_time.time_since_epoch())
                                         .count();
        read_reported_hardware(*machine, printer);
        result.push_back(std::move(printer));
    }
    std::sort(result.begin(), result.end(), [](const DiscoveredPrinter& a, const DiscoveredPrinter& b) {
        if (a.connected != b.connected) return a.connected > b.connected;
        if (a.name != b.name) return a.name < b.name;
        return a.stable_id < b.stable_id;
    });
    return result;
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
