#include "FakePrinterAgent.hpp"
#include "NetworkAgentFactory.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Slic3r {

using json = nlohmann::json;

const std::string FakePrinterAgent_VERSION = "1.0.0";

FakePrinterAgent::FakePrinterAgent(std::string log_dir) : log_dir(std::move(log_dir))
{
    m_scenario = build_default_print_scenario();
    maybe_start_debug_console();
}

FakePrinterAgent::~FakePrinterAgent()
{
    stop_scenario_thread();
}

AgentInfo FakePrinterAgent::get_agent_info_static()
{
    return AgentInfo{"fake", "Fake Printer (Simulator)", FakePrinterAgent_VERSION,
                     "Scripted printer for exercising the UI without physical hardware"};
}

std::vector<FakePrinterStatusStep> FakePrinterAgent::build_default_print_scenario()
{
    std::vector<FakePrinterStatusStep> steps;

    // Connected, sitting idle.
    steps.push_back({200, "IDLE", 0, 0, 25.0, 0.0, 25.0, 0.0});

    // Heating nozzle and bed toward their targets.
    steps.push_back({300, "PREPARE", 0, 0, 90.0, 210.0, 40.0, 60.0});
    steps.push_back({300, "PREPARE", 0, 0, 160.0, 210.0, 52.0, 60.0});
    steps.push_back({300, "PREPARE", 0, 0, 210.0, 210.0, 60.0, 60.0});

    // Printing: progress climbs, remaining time counts down, temps hold.
    steps.push_back({300, "RUNNING", 10, 9, 210.0, 210.0, 60.0, 60.0});
    steps.push_back({300, "RUNNING", 35, 6, 210.0, 210.0, 60.0, 60.0});
    steps.push_back({300, "RUNNING", 65, 3, 210.0, 210.0, 60.0, 60.0});
    steps.push_back({300, "RUNNING", 90, 1, 210.0, 210.0, 60.0, 60.0});

    // Done: heaters off, full progress.
    steps.push_back({300, "FINISH", 100, 0, 210.0, 0.0, 60.0, 0.0});

    return steps;
}

std::vector<std::pair<std::string, FakePrinterStatusStep>> FakePrinterAgent::build_named_presets()
{
    return {
        {"Idle",            {0, "IDLE",    0,   0, 25.0,  0.0, 25.0,  0.0}},
        {"Heating",         {0, "PREPARE", 0,   0, 150.0, 210.0, 45.0, 60.0}},
        {"Printing 25%",    {0, "RUNNING", 25,  8, 210.0, 210.0, 60.0, 60.0}},
        {"Printing 75%",    {0, "RUNNING", 75,  2, 210.0, 210.0, 60.0, 60.0}},
        {"Finished",        {0, "FINISH",  100, 0, 210.0, 0.0,   60.0, 0.0}},
        {"Failed / error",  {0, "FAILED",  40,  0, 210.0, 0.0,   60.0, 0.0}},
    };
}

void FakePrinterAgent::set_scenario(std::vector<FakePrinterStatusStep> steps)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    m_scenario = std::move(steps);
}

std::string FakePrinterAgent::build_push_status_json(const std::string& subtask_name, int plate_idx, const FakePrinterStatusStep& step, bool full) const
{
    // Every step is sent as a full snapshot (msg:0) rather than the real
    // protocol's msg:1 diff encoding: MachineObject::parse_json treats msg:0
    // as "replace state wholesale", which is enough to drive the UI and
    // avoids depending on Bambu's private diff format.
    (void) full;
    json j;
    j["print"]["command"]           = "push_status";
    j["print"]["msg"]               = 0;
    j["print"]["gcode_state"]       = step.gcode_state;
    j["print"]["mc_percent"]        = step.mc_percent;
    j["print"]["mc_remaining_time"] = step.mc_remaining_time;
    j["print"]["subtask_name"]      = subtask_name;
    j["print"]["nozzle_temper"]        = step.nozzle_temper;
    j["print"]["nozzle_target_temper"] = step.nozzle_target_temper;
    j["print"]["bed_temper"]           = step.bed_temper;
    j["print"]["bed_target_temper"]    = step.bed_target_temper;
    j["print"]["plate_idx"]         = plate_idx;
    return j.dump();
}

void FakePrinterAgent::emit_status(const std::string& dev_id, const FakePrinterStatusStep& step, bool full)
{
    OnMessageFn local_fn;
    OnMessageFn cloud_fn;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        local_fn = on_local_message_fn;
        cloud_fn = on_message_fn;
    }
    std::string payload = build_push_status_json("fake_print.3mf", 1, step, full);
    if (local_fn)
        local_fn(dev_id, payload);
    else if (cloud_fn)
        cloud_fn(dev_id, payload);
}

void FakePrinterAgent::push_status(const FakePrinterStatusStep& step)
{
    std::string dev_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        dev_id = connected_dev_id;
    }
    emit_status(dev_id, step, true);
}

void FakePrinterAgent::wait_scenario_done()
{
    std::unique_lock<std::mutex> lock(m_scenario_done_mutex);
    m_scenario_done_cv.wait(lock, [this] { return !m_scenario_running.load(); });
}

void FakePrinterAgent::stop_scenario_thread()
{
    m_stop_requested = true;
    if (m_scenario_thread.joinable())
        m_scenario_thread.join();
}

void FakePrinterAgent::cancel_scenario()
{
    stop_scenario_thread();
}

void FakePrinterAgent::maybe_start_debug_console()
{
    if (!std::getenv("JUSPRIN_FAKE_PRINTER_CONSOLE"))
        return;
    // Detached rather than joined: it blocks on std::getline(std::cin, ...),
    // which nothing can interrupt short of closing stdin. This is only ever
    // enabled for a manual test session running for the app's whole
    // lifetime, so the thread simply dies with the process at exit.
    std::thread(&FakePrinterAgent::run_debug_console, this).detach();
}

namespace {
const std::string kConsoleDevId = "FAKE001";

// Reads one line and parses a leading integer. Returns -1 on EOF or a non-numeric line.
int read_menu_choice()
{
    std::string line;
    if (!std::getline(std::cin, line))
        return -1;
    try {
        return std::stoi(line);
    } catch (...) {
        return -1;
    }
}
} // namespace

void FakePrinterAgent::run_debug_console()
{
    const auto presets = build_named_presets();

    for (;;) {
        bool connected;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            connected = !connected_dev_id.empty();
        }

        std::cout << "\n=== Fake Printer Console (" << (connected ? "connected: " + kConsoleDevId : "disconnected") << ") ===\n"
                      "  1) Connect\n"
                      "  2) Disconnect\n"
                      "  3) Run full scenario (connect -> heat -> print -> finish)\n"
                      "  4) Stop running scenario\n"
                      "  5) Jump to a status preset\n"
                      "  6) Quit\n"
                      "Choose: "
                  << std::flush;

        int choice = read_menu_choice();
        switch (choice) {
        case 1:
            connect_printer(kConsoleDevId, "127.0.0.1", "", "", false);
            std::cout << "connected " << kConsoleDevId << std::endl;
            break;
        case 2:
            disconnect_printer();
            std::cout << "disconnected" << std::endl;
            break;
        case 3: {
            PrintParams params;
            params.dev_id = kConsoleDevId;
            start_local_print(params, nullptr, nullptr);
            std::cout << "scenario started" << std::endl;
            break;
        }
        case 4:
            cancel_scenario();
            std::cout << "scenario stopped" << std::endl;
            break;
        case 5: {
            std::cout << "  0) Back\n";
            for (size_t i = 0; i < presets.size(); ++i)
                std::cout << "  " << (i + 1) << ") " << presets[i].first << "\n";
            std::cout << "Choose: " << std::flush;
            int preset_choice = read_menu_choice();
            if (preset_choice >= 1 && static_cast<size_t>(preset_choice) <= presets.size()) {
                push_status(presets[preset_choice - 1].second);
                std::cout << "pushed: " << presets[preset_choice - 1].first << std::endl;
            }
            break;
        }
        case 6:
            return;
        default:
            std::cout << "not a valid choice" << std::endl;
            break;
        }
    }
}

int FakePrinterAgent::run_scenario(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    std::vector<FakePrinterStatusStep> scenario;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        scenario = m_scenario;
    }

    if (update_fn)
        update_fn(PrintingStageCreate, 0, "Preparing...");

    std::string dev_id = params.dev_id;
    for (const auto& step : scenario) {
        if (m_stop_requested || (cancel_fn && cancel_fn())) {
            if (update_fn)
                update_fn(PrintingStageERROR, 0, "Cancelled");
            {
                std::lock_guard<std::mutex> lock(m_scenario_done_mutex);
                m_scenario_running = false;
            }
            m_scenario_done_cv.notify_all();
            return BAMBU_NETWORK_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(step.delay_ms));
        emit_status(dev_id, step, true);
    }

    if (update_fn)
        update_fn(PrintingStageFinished, 100, "Print finished");

    {
        std::lock_guard<std::mutex> lock(m_scenario_done_mutex);
        m_scenario_running = false;
    }
    m_scenario_done_cv.notify_all();
    return BAMBU_NETWORK_SUCCESS;
}

// ============================================================================
// Communication
// ============================================================================

void FakePrinterAgent::set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    m_cloud_agent = cloud;
}

int FakePrinterAgent::send_message(std::string dev_id, std::string json_str, int qos, int flag)
{
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password, bool use_ssl)
{
    OnLocalConnectedFn connect_fn;
    FakePrinterStatusStep initial_step;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        connected_dev_id = dev_id;
        connect_fn       = on_local_connect_fn;
        initial_step     = m_scenario.empty() ? FakePrinterStatusStep{} : m_scenario.front();
    }
    if (connect_fn)
        connect_fn(ConnectStatusOk, dev_id, "connected");
    emit_status(dev_id, initial_step, true);
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::disconnect_printer()
{
    stop_scenario_thread();
    OnLocalConnectedFn connect_fn;
    std::string dev_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        connect_fn = on_local_connect_fn;
        dev_id     = connected_dev_id;
        connected_dev_id.clear();
    }
    if (connect_fn)
        connect_fn(ConnectStatusLost, dev_id, "disconnected");
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag)
{
    return BAMBU_NETWORK_SUCCESS;
}

// ============================================================================
// Certificates - Not simulated
// ============================================================================

int FakePrinterAgent::check_cert()
{
    return BAMBU_NETWORK_SUCCESS;
}

void FakePrinterAgent::install_device_cert(std::string dev_id, bool lan_only)
{
}

// ============================================================================
// Discovery - Not simulated
// ============================================================================

bool FakePrinterAgent::start_discovery(bool start, bool sending)
{
    return true;
}

// ============================================================================
// Binding - Not simulated
// ============================================================================

int FakePrinterAgent::ping_bind(std::string ping_code)
{
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect)
{
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::bind(
    std::string dev_ip, std::string dev_id, std::string sec_link, std::string timezone, bool improved, OnUpdateStatusFn update_fn)
{
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::unbind(std::string dev_id)
{
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::request_bind_ticket(std::string* ticket)
{
    if (ticket)
        *ticket = "";
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_server_callback(OnServerErrFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_server_err_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

// ============================================================================
// Machine Selection
// ============================================================================

std::string FakePrinterAgent::get_user_selected_machine()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return selected_machine;
}

int FakePrinterAgent::set_user_selected_machine(std::string dev_id)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    selected_machine = dev_id;
    return BAMBU_NETWORK_SUCCESS;
}

// ============================================================================
// Print Job Operations - Scripted
// ============================================================================

int FakePrinterAgent::start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));
}

int FakePrinterAgent::start_local_print_with_record(PrintParams      params,
                                                    OnUpdateStatusFn update_fn,
                                                    WasCancelledFn   cancel_fn,
                                                    OnWaitFn         wait_fn)
{
    return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));
}

int FakePrinterAgent::start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    if (update_fn)
        update_fn(PrintingStageFinished, 100, "Gcode sent to SD card");
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    stop_scenario_thread();
    m_stop_requested = false;
    {
        std::lock_guard<std::mutex> lock(m_scenario_done_mutex);
        m_scenario_running = true;
    }
    m_scenario_thread = std::thread(&FakePrinterAgent::run_scenario, this, std::move(params), std::move(update_fn), std::move(cancel_fn));
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));
}

// ============================================================================
// Callback Registration
// ============================================================================

int FakePrinterAgent::set_on_ssdp_msg_fn(OnMsgArrivedFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_ssdp_msg_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_on_printer_connected_fn(OnPrinterConnectedFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_printer_connected_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_on_subscribe_failure_fn(GetSubscribeFailureFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_subscribe_failure_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_on_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_message_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_on_user_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_user_message_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_on_local_connect_fn(OnLocalConnectedFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_local_connect_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_on_local_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_local_message_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakePrinterAgent::set_queue_on_main_fn(QueueOnMainFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    queue_on_main_fn = fn;
    return BAMBU_NETWORK_SUCCESS;
}

} // namespace Slic3r
