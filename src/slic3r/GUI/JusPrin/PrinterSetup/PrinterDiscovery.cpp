#include "PrinterDiscovery.hpp"

#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"

#include <algorithm>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

std::vector<DiscoveredPrinter> discover_printers()
{
    std::vector<DiscoveredPrinter> result;
    DeviceManager* devices = wxGetApp().getDeviceManager();
    if (!devices) return result;
    for (const auto& [id, machine] : devices->get_my_machine_list()) {
        if (!machine || (!machine->is_online() && !machine->is_connected())) continue;
        result.push_back({id, machine->get_dev_name(), machine->get_dev_ip(), machine->printer_type,
                          machine->connection_type(), true});
    }
    std::sort(result.begin(), result.end(), [](const DiscoveredPrinter& a, const DiscoveredPrinter& b) {
        if (a.connected != b.connected) return a.connected > b.connected;
        if (a.name != b.name) return a.name < b.name;
        return a.stable_id < b.stable_id;
    });
    return result;
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
