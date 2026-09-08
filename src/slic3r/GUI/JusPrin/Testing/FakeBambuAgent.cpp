#include "FakeBambuAgent.hpp"

#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/NetworkAgentFactory.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/MainFrame.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <sstream>

namespace Slic3r {

using json = nlohmann::json;

namespace {

constexpr int kHomeFlagSdcardNormal = 0x7 | (1 << 8); // XYZ homed + HAS_SDCARD_NORMAL
const std::string kAccessCode = "88888888";

std::string lower_ascii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int remaining_minutes_from_progress(int percent)
{
    if (percent >= 100)
        return 0;
    return std::max(1, (100 - percent) / 10);
}

} // namespace

void register_fake_bambu_agent()
{
    static std::once_flag once;
    std::call_once(once, [] {
        NetworkAgentFactory::register_printer_agent(
            JUSPRIN_FAKE_BAMBU_AGENT_ID, "JusPrin Fake Bambu",
            [](std::shared_ptr<ICloudServiceAgent> cloud, const std::string& log_dir) -> std::shared_ptr<IPrinterAgent> {
                auto agent = std::make_shared<FakeBambuAgent>(log_dir);
                if (cloud)
                    agent->set_cloud_agent(cloud);
                return agent;
            });
    });
}

bool fake_bambu_mode_enabled(const AppConfig* config)
{
    if (config == nullptr)
        return false;
    const std::string value = config->get("jusprin", "fake_printer");
    return value == "true" || value == "1";
}

std::string fake_bambu_printer_agent_id(const AppConfig* config)
{
    register_fake_bambu_agent();
    if (!fake_bambu_mode_enabled(config))
        return {};
    return JUSPRIN_FAKE_BAMBU_AGENT_ID;
}

namespace {
[[maybe_unused]] const auto register_at_load = (register_fake_bambu_agent(), 0);
}

FakeBambuAgent::FakeBambuAgent(std::string log_dir) : log_dir(std::move(log_dir))
{
    m_scenario = build_default_print_scenario();
    m_current  = m_scenario.empty() ? FakeBambuStatusStep{} : m_scenario.front();
}

FakeBambuAgent::~FakeBambuAgent()
{
    stop_heartbeat();
    stop_scenario_thread();
}

AgentInfo FakeBambuAgent::get_agent_info_static()
{
    return AgentInfo{JUSPRIN_FAKE_BAMBU_AGENT_ID, "JusPrin Fake Bambu", "1.0.0",
                     "In-process Bambu A1 mini stand-in for testing without hardware"};
}

std::vector<FakeBambuStatusStep> FakeBambuAgent::build_default_print_scenario()
{
    return {
        {200, "IDLE", 0, 0, 25.0, 0.0, 25.0, 0.0},
        {300, "PREPARE", 0, 0, 90.0, 210.0, 40.0, 60.0},
        {300, "PREPARE", 0, 0, 160.0, 210.0, 52.0, 60.0},
        {300, "PREPARE", 0, 0, 210.0, 210.0, 60.0, 60.0},
        {300, "RUNNING", 10, 9, 210.0, 210.0, 60.0, 60.0},
        {300, "RUNNING", 35, 6, 210.0, 210.0, 60.0, 60.0},
        {300, "RUNNING", 65, 3, 210.0, 210.0, 60.0, 60.0},
        {300, "RUNNING", 90, 1, 210.0, 210.0, 60.0, 60.0},
        {300, "FINISH", 100, 0, 210.0, 0.0, 60.0, 0.0},
    };
}

std::vector<std::pair<std::string, FakeBambuStatusStep>> FakeBambuAgent::build_named_presets()
{
    return {
        {"Idle", {0, "IDLE", 0, 0, 25.0, 0.0, 25.0, 0.0}},
        {"Heating", {0, "PREPARE", 0, 0, 150.0, 210.0, 45.0, 60.0}},
        {"Printing 25%", {0, "RUNNING", 25, 8, 210.0, 210.0, 60.0, 60.0}},
        {"Printing 75%", {0, "RUNNING", 75, 2, 210.0, 210.0, 60.0, 60.0}},
        {"Finished", {0, "FINISH", 100, 0, 210.0, 0.0, 60.0, 0.0}},
        {"Failed / error", {0, "FAILED", 40, 0, 210.0, 0.0, 60.0, 0.0}},
    };
}

std::string FakeBambuAgent::ssdp_announcement_json()
{
    json payload;
    payload["dev_name"]     = "JusPrin Fake A1 mini";
    payload["dev_id"]       = JUSPRIN_FAKE_BAMBU_DEV_ID;
    payload["dev_ip"]       = "127.0.0.1";
    payload["dev_type"]     = JUSPRIN_FAKE_BAMBU_PRINTER_TYPE;
    payload["dev_signal"]   = "0";
    payload["connect_type"] = "lan";
    payload["bind_state"]   = "free";
    payload["sec_link"]     = "secure";
    payload["ssdp_version"] = "v1";
    return payload.dump();
}

bool FakeBambuAgent::apply_control_json(const std::string& json_text, FakeBambuStatusStep& step, bool& offline)
{
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return false;

    if (j.contains("offline") && j["offline"].is_boolean())
        offline = j["offline"].get<bool>();

    std::string state = "idle";
    if (j.contains("state") && j["state"].is_string())
        state = lower_ascii(j["state"].get<std::string>());

    if (state == "idle")
        step.gcode_state = "IDLE";
    else if (state == "heating" || state == "prepare")
        step.gcode_state = "PREPARE";
    else if (state == "printing" || state == "running")
        step.gcode_state = "RUNNING";
    else if (state == "pause" || state == "paused")
        step.gcode_state = "PAUSE";
    else if (state == "finished" || state == "finish")
        step.gcode_state = "FINISH";
    else if (state == "failed" || state == "error")
        step.gcode_state = "FAILED";
    else
        return false;

    if (j.contains("progress") && j["progress"].is_number()) {
        const double progress = j["progress"].get<double>();
        step.mc_percent       = std::clamp(static_cast<int>(progress * 100.0 + 0.5), 0, 100);
    } else if (step.gcode_state == "FINISH") {
        step.mc_percent = 100;
    } else if (step.gcode_state == "IDLE" || step.gcode_state == "PREPARE") {
        step.mc_percent = 0;
    }

    step.mc_remaining_time = (step.gcode_state == "RUNNING" || step.gcode_state == "PAUSE") ?
                                 remaining_minutes_from_progress(step.mc_percent) :
                                 0;

    if (j.contains("nozzle") && j["nozzle"].is_number()) {
        step.nozzle_temper = j["nozzle"].get<double>();
        step.nozzle_target_temper = (step.gcode_state == "IDLE" || step.gcode_state == "FINISH" || step.gcode_state == "FAILED") ?
                                        0.0 :
                                        step.nozzle_temper;
    }
    if (j.contains("bed") && j["bed"].is_number()) {
        step.bed_temper = j["bed"].get<double>();
        step.bed_target_temper = (step.gcode_state == "IDLE" || step.gcode_state == "FINISH" || step.gcode_state == "FAILED") ?
                                     0.0 :
                                     step.bed_temper;
    }
    return true;
}

void FakeBambuAgent::set_scenario(std::vector<FakeBambuStatusStep> steps)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    m_scenario = std::move(steps);
}

std::string FakeBambuAgent::build_push_status_json(const FakeBambuStatusStep& step) const
{
    json print;
    print["command"]               = "push_status";
    print["msg"]                   = 0;
    print["gcode_state"]           = step.gcode_state;
    print["mc_percent"]            = step.mc_percent;
    print["mc_remaining_time"]     = step.mc_remaining_time;
    print["mc_print_stage"]        = (step.gcode_state == "RUNNING") ? 1 : (step.gcode_state == "PAUSE") ? 2 : (step.gcode_state == "FINISH") ? 3 : (step.gcode_state == "FAILED") ? 4 : 0;
    print["mc_print_error_code"]   = 0;
    print["print_error"]           = 0;
    print["subtask_name"]          = m_last_print_filename.empty() ? "fake_print.3mf" : m_last_print_filename;
    print["gcode_file"]            = print["subtask_name"];
    print["nozzle_temper"]         = step.nozzle_temper;
    print["nozzle_target_temper"]  = step.nozzle_target_temper;
    print["bed_temper"]            = step.bed_temper;
    print["bed_target_temper"]     = step.bed_target_temper;
    print["nozzle_diameter"]       = "0.4";
    print["nozzle_type"]           = "stainless_steel";
    print["home_flag"]             = kHomeFlagSdcardNormal;
    print["support_send_to_sd"]    = true;
    print["support_bed_leveling"]  = 1;
    print["support_mqtt_alive"]    = true;
    print["plate_idx"]             = 1;
    print["upgrade_state"]         = json{{"status", "IDLE"}, {"progress", "0"}, {"new_version_state", 0}};
    print["ams"] = json{{"ams", json::array()},
                        {"ams_exist_bits", "0"},
                        {"tray_exist_bits", "0"},
                        {"tray_is_bbl_bits", "0"},
                        {"tray_now", "255"},
                        {"tray_tar", "255"},
                        {"version", 2}};

    const auto now_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    json j;
    j["print"] = std::move(print);
    j["t_utc"] = now_ms;
    return j.dump();
}

std::string FakeBambuAgent::build_version_json() const
{
    json module;
    module["name"]         = "ota";
    module["sw_ver"]       = "01.04.00.00";
    module["hw_ver"]       = "AP05";
    module["sn"]           = JUSPRIN_FAKE_BAMBU_DEV_ID;
    module["product_name"] = "Bambu Lab A1 mini";

    json info;
    info["command"] = "get_version";
    info["result"]  = "success";
    info["module"]  = json::array({module});

    json j;
    j["info"] = std::move(info);
    return j.dump();
}

void FakeBambuAgent::emit_json(const std::string& dev_id, const std::string& payload)
{
    OnMessageFn local_fn;
    OnMessageFn cloud_fn;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        local_fn = on_local_message_fn;
        cloud_fn = on_message_fn;
    }
    if (local_fn)
        local_fn(dev_id, payload);
    else if (cloud_fn)
        cloud_fn(dev_id, payload);
}

void FakeBambuAgent::emit_status(const std::string& dev_id, const FakeBambuStatusStep& step)
{
    emit_json(dev_id, build_push_status_json(step));
}

void FakeBambuAgent::push_status(const FakeBambuStatusStep& step)
{
    std::string dev_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        m_current = step;
        m_offline = false;
        dev_id    = connected_or_default();
    }
    emit_status(dev_id, step);
}

FakeBambuStatusStep FakeBambuAgent::current_step() const
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return m_current;
}

std::string FakeBambuAgent::last_print_filename() const
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return m_last_print_filename;
}

std::string FakeBambuAgent::connected_or_default() const
{
    return connected_dev_id.empty() ? std::string(JUSPRIN_FAKE_BAMBU_DEV_ID) : connected_dev_id;
}

void FakeBambuAgent::wait_scenario_done()
{
    std::unique_lock<std::mutex> lock(m_scenario_done_mutex);
    m_scenario_done_cv.wait(lock, [this] { return !m_scenario_running.load(); });
}

void FakeBambuAgent::stop_scenario_thread()
{
    m_stop_requested = true;
    if (m_scenario_thread.joinable())
        m_scenario_thread.join();
}

void FakeBambuAgent::cancel_scenario() { stop_scenario_thread(); }

std::string FakeBambuAgent::control_file_path() const
{
    return (boost::filesystem::path(log_dir) / "jusprin" / "fake_printer.json").string();
}

bool FakeBambuAgent::reload_control_file()
{
    const auto path = boost::filesystem::path(control_file_path());
    if (!boost::filesystem::exists(path)) {
        std::lock_guard<std::mutex> lock(state_mutex);
        m_control_file_present = false;
        return false;
    }

    std::int64_t mtime = 0;
    try {
        mtime = static_cast<std::int64_t>(boost::filesystem::last_write_time(path));
    } catch (...) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (m_control_file_present && mtime == m_control_mtime)
            return true;
    }

    boost::nowide::ifstream in(path.string());
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();

    FakeBambuStatusStep step;
    bool                offline = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        step = m_current;
    }
    if (!apply_control_json(ss.str(), step, offline)) {
        BOOST_LOG_TRIVIAL(warning) << "FakeBambuAgent: ignored invalid control file " << path.string();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex);
        m_current              = step;
        m_offline              = offline;
        m_control_file_present = true;
        m_control_mtime        = mtime;
    }
    return true;
}

int FakeBambuAgent::run_scenario(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    std::vector<FakeBambuStatusStep> scenario;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        scenario = m_scenario;
    }

    if (update_fn)
        update_fn(PrintingStageCreate, 0, "Preparing...");

    std::string dev_id = params.dev_id.empty() ? std::string(JUSPRIN_FAKE_BAMBU_DEV_ID) : params.dev_id;
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
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            m_current = step;
        }
        emit_status(dev_id, step);
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

void FakeBambuAgent::start_heartbeat()
{
    std::lock_guard<std::mutex> lock(m_heartbeat_mutex);
    if (m_heartbeat_thread.joinable())
        return;
    m_heartbeat_stop = false;
    m_heartbeat_thread = std::thread(&FakeBambuAgent::heartbeat_loop, this);
}

void FakeBambuAgent::stop_heartbeat()
{
    {
        std::lock_guard<std::mutex> lock(m_heartbeat_mutex);
        m_heartbeat_stop = true;
    }
    m_heartbeat_cv.notify_all();
    if (m_heartbeat_thread.joinable())
        m_heartbeat_thread.join();
}

void FakeBambuAgent::heartbeat_loop()
{
    while (!m_heartbeat_stop.load()) {
        reload_control_file();
        announce_if_needed();

        FakeBambuStatusStep step;
        std::string         dev_id;
        bool                offline = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            step    = m_current;
            offline = m_offline;
            dev_id  = connected_or_default();
        }
        if (!offline)
            emit_status(dev_id, step);

        std::unique_lock<std::mutex> lock(m_heartbeat_mutex);
        m_heartbeat_cv.wait_for(lock, std::chrono::seconds(1), [this] { return m_heartbeat_stop.load(); });
    }
}

void FakeBambuAgent::announce_if_needed()
{
    OnMsgArrivedFn ssdp_fn;
    QueueOnMainFn  queue_fn;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (m_announced || !on_ssdp_msg_fn)
            return;
        ssdp_fn  = on_ssdp_msg_fn;
        queue_fn = queue_on_main_fn;
        m_announced = true;
        if (selected_machine.empty())
            selected_machine = JUSPRIN_FAKE_BAMBU_DEV_ID;
    }

    const std::string payload = ssdp_announcement_json();
    const std::string dev_id  = JUSPRIN_FAKE_BAMBU_DEV_ID;
    if (queue_fn) {
        queue_fn([dev_id]() {
            if (auto* config = GUI::wxGetApp().app_config) {
                config->set_str("access_code", dev_id, kAccessCode);
                config->set_str("user_access_code", dev_id, kAccessCode);
            }
        });
    }
    ssdp_fn(payload);
    if (queue_fn)
        queue_fn([dev_id]() { FakeBambuAgent::select_machine_on_main(dev_id); });
}

void FakeBambuAgent::select_machine_on_main(const std::string& dev_id)
{
    auto* devices = GUI::wxGetApp().getDeviceManager();
    if (devices == nullptr)
        return;
    if (devices->get_local_machine(dev_id) == nullptr && devices->get_my_machine(dev_id) == nullptr)
        return;
    if (auto* frame = GUI::wxGetApp().mainframe; frame && frame->m_monitor)
        frame->m_monitor->select_machine(dev_id);
    else
        devices->set_selected_machine(dev_id);
    BOOST_LOG_TRIVIAL(info) << "FakeBambuAgent: selected machine " << dev_id;
}

int FakeBambuAgent::handle_printer_command(const std::string& dev_id, const std::string& json_str)
{
    json j = json::parse(json_str, nullptr, false);
    if (j.is_discarded())
        return BAMBU_NETWORK_SUCCESS;

    if (j.contains("info") && j["info"].contains("command") && j["info"]["command"].is_string() &&
        j["info"]["command"].get<std::string>() == "get_version") {
        emit_json(dev_id, build_version_json());
        return BAMBU_NETWORK_SUCCESS;
    }

    if (j.contains("print") && j["print"].contains("command") && j["print"]["command"].is_string()) {
        const std::string cmd = j["print"]["command"].get<std::string>();
        if (cmd == "pushall" || cmd == "push_status") {
            FakeBambuStatusStep step;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                step = m_current;
            }
            emit_status(dev_id.empty() ? connected_or_default() : dev_id, step);
        }
    }
    return BAMBU_NETWORK_SUCCESS;
}

void FakeBambuAgent::set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    m_cloud_agent = std::move(cloud);
}

int FakeBambuAgent::send_message(std::string dev_id, std::string json_str, int qos, int flag)
{
    (void) qos;
    (void) flag;
    return handle_printer_command(dev_id, json_str);
}

int FakeBambuAgent::connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password, bool use_ssl)
{
    (void) dev_ip;
    (void) username;
    (void) password;
    (void) use_ssl;
    OnLocalConnectedFn  connect_fn;
    FakeBambuStatusStep initial_step;
    std::string         id;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        connected_dev_id = dev_id.empty() ? std::string(JUSPRIN_FAKE_BAMBU_DEV_ID) : dev_id;
        id               = connected_dev_id;
        connect_fn       = on_local_connect_fn;
        initial_step     = m_current;
        if (selected_machine.empty())
            selected_machine = connected_dev_id;
    }
    if (connect_fn)
        connect_fn(ConnectStatusOk, id, "connected");
    emit_status(id, initial_step);
    BOOST_LOG_TRIVIAL(info) << "FakeBambuAgent: connected " << id;
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::disconnect_printer()
{
    stop_scenario_thread();
    OnLocalConnectedFn connect_fn;
    std::string        dev_id;
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

int FakeBambuAgent::send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag)
{
    (void) qos;
    (void) flag;
    return handle_printer_command(dev_id, json_str);
}

int FakeBambuAgent::check_cert() { return BAMBU_NETWORK_SUCCESS; }

void FakeBambuAgent::install_device_cert(std::string dev_id, bool lan_only)
{
    (void) dev_id;
    (void) lan_only;
}

bool FakeBambuAgent::start_discovery(bool start, bool sending)
{
    (void) sending;
    if (start)
        announce_if_needed();
    return true;
}

int FakeBambuAgent::ping_bind(std::string ping_code)
{
    (void) ping_code;
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect)
{
    (void) dev_ip;
    (void) sec_link;
    (void) detect;
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::bind(std::string      dev_ip,
                         std::string      dev_id,
                         std::string      sec_link,
                         std::string      timezone,
                         bool             improved,
                         OnUpdateStatusFn update_fn)
{
    (void) dev_ip;
    (void) dev_id;
    (void) sec_link;
    (void) timezone;
    (void) improved;
    (void) update_fn;
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::unbind(std::string dev_id)
{
    (void) dev_id;
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::request_bind_ticket(std::string* ticket)
{
    if (ticket)
        *ticket = "";
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_server_callback(OnServerErrFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_server_err_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

std::string FakeBambuAgent::get_user_selected_machine()
{
    std::lock_guard<std::mutex> lock(state_mutex);
    return selected_machine;
}

int FakeBambuAgent::set_user_selected_machine(std::string dev_id)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    selected_machine = std::move(dev_id);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    (void) wait_fn;
    return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));
}

int FakeBambuAgent::start_local_print_with_record(PrintParams      params,
                                                  OnUpdateStatusFn update_fn,
                                                  WasCancelledFn   cancel_fn,
                                                  OnWaitFn         wait_fn)
{
    (void) wait_fn;
    return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));
}

int FakeBambuAgent::start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn)
{
    (void) params;
    (void) cancel_fn;
    (void) wait_fn;
    if (update_fn)
        update_fn(PrintingStageFinished, 100, "Gcode sent to SD card");
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        m_last_print_filename = params.filename.empty() ? params.task_name : params.filename;
        BOOST_LOG_TRIVIAL(info) << "FakeBambuAgent: start_local_print file=" << m_last_print_filename
                                << " dest=" << params.dst_file;
    }

    if (cancel_fn && cancel_fn()) {
        if (update_fn)
            update_fn(PrintingStageERROR, 0, "Cancelled");
        return BAMBU_NETWORK_SUCCESS;
    }

    bool control_owns_state = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        control_owns_state = m_control_file_present;
        if (!control_owns_state) {
            m_current.gcode_state          = "RUNNING";
            m_current.mc_percent           = 0;
            m_current.mc_remaining_time    = 10;
            m_current.nozzle_temper        = 210.0;
            m_current.nozzle_target_temper = 210.0;
            m_current.bed_temper           = 60.0;
            m_current.bed_target_temper    = 60.0;
            m_offline                      = false;
        }
    }

    if (control_owns_state) {
        if (update_fn) {
            update_fn(PrintingStageCreate, 0, "Preparing...");
            update_fn(PrintingStageUpload, 50, "Uploading G-code...");
            update_fn(PrintingStageSending, 80, "Starting print...");
            update_fn(PrintingStageFinished, 100, "Print started");
        }
        FakeBambuStatusStep step;
        std::string         dev_id;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            step   = m_current;
            dev_id = connected_or_default();
        }
        emit_status(dev_id, step);
        return BAMBU_NETWORK_SUCCESS;
    }

    stop_scenario_thread();
    m_stop_requested = false;
    {
        std::lock_guard<std::mutex> lock(m_scenario_done_mutex);
        m_scenario_running = true;
    }
    m_scenario_thread = std::thread(&FakeBambuAgent::run_scenario, this, std::move(params), std::move(update_fn), std::move(cancel_fn));
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    return start_local_print(std::move(params), std::move(update_fn), std::move(cancel_fn));
}

int FakeBambuAgent::set_on_ssdp_msg_fn(OnMsgArrivedFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_ssdp_msg_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_on_printer_connected_fn(OnPrinterConnectedFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_printer_connected_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_on_subscribe_failure_fn(GetSubscribeFailureFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_subscribe_failure_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_on_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_message_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_on_user_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_user_message_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_on_local_connect_fn(OnLocalConnectedFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_local_connect_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_on_local_message_fn(OnMessageFn fn)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    on_local_message_fn = std::move(fn);
    return BAMBU_NETWORK_SUCCESS;
}

int FakeBambuAgent::set_queue_on_main_fn(QueueOnMainFn fn)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        queue_on_main_fn = std::move(fn);
    }
    if (queue_on_main_fn)
        start_heartbeat();
    return BAMBU_NETWORK_SUCCESS;
}

} // namespace Slic3r
