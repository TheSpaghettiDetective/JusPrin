#pragma once

#include "slic3r/Utils/IPrinterAgent.hpp"
#include "slic3r/Utils/ICloudServiceAgent.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r {

class AppConfig;

inline constexpr char JUSPRIN_FAKE_BAMBU_AGENT_ID[]    = "jusprin-fake-bambu";
inline constexpr char JUSPRIN_FAKE_BAMBU_DEV_ID[]      = "FAKE001";
inline constexpr char JUSPRIN_FAKE_BAMBU_PRINTER_TYPE[] = "N1";

void        register_fake_bambu_agent();
bool        fake_bambu_mode_enabled(const AppConfig* config);
std::string fake_bambu_printer_agent_id(const AppConfig* config);

struct FakeBambuStatusStep
{
    int         delay_ms             = 0;
    std::string gcode_state          = "IDLE";
    int         mc_percent           = 0;
    int         mc_remaining_time    = 0;
    double      nozzle_temper        = 25.0;
    double      nozzle_target_temper = 0.0;
    double      bed_temper           = 25.0;
    double      bed_target_temper    = 0.0;
};

// In-process stand-in for BBLPrinterAgent. A real Bambu preset stays selected;
// switch_printer_agent substitutes this implementation when jusprin.fake_printer
// is set. Status is driven by <datadir>/jusprin/fake_printer.json.
class FakeBambuAgent : public IPrinterAgent
{
public:
    explicit FakeBambuAgent(std::string log_dir);
    ~FakeBambuAgent() override;

    static AgentInfo get_agent_info_static();
    AgentInfo        get_agent_info() override { return get_agent_info_static(); }

    static std::vector<FakeBambuStatusStep>                               build_default_print_scenario();
    static std::vector<std::pair<std::string, FakeBambuStatusStep>>       build_named_presets();
    static std::string                                                    ssdp_announcement_json();
    static bool apply_control_json(const std::string& json_text, FakeBambuStatusStep& step, bool& offline);

    void        set_scenario(std::vector<FakeBambuStatusStep> steps);
    void        push_status(const FakeBambuStatusStep& step);
    void        wait_scenario_done();
    void        cancel_scenario();
    bool        reload_control_file();
    std::string last_print_filename() const;
    FakeBambuStatusStep current_step() const;

    void set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud) override;

    int  send_message(std::string dev_id, std::string json_str, int qos, int flag) override;
    int  connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password, bool use_ssl) override;
    int  disconnect_printer() override;
    int  send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag) override;

    int  check_cert() override;
    void install_device_cert(std::string dev_id, bool lan_only) override;

    bool start_discovery(bool start, bool sending) override;

    int ping_bind(std::string ping_code) override;
    int bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect) override;
    int bind(std::string          dev_ip,
             std::string          dev_id,
             std::string          sec_link,
             std::string          timezone,
             bool                 improved,
             OnUpdateStatusFn     update_fn) override;
    int unbind(std::string dev_id) override;
    int request_bind_ticket(std::string* ticket) override;
    int set_server_callback(OnServerErrFn fn) override;

    std::string get_user_selected_machine() override;
    int         set_user_selected_machine(std::string dev_id) override;

    int start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;
    int start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;

    int set_on_ssdp_msg_fn(OnMsgArrivedFn fn) override;
    int set_on_printer_connected_fn(OnPrinterConnectedFn fn) override;
    int set_on_subscribe_failure_fn(GetSubscribeFailureFn fn) override;
    int set_on_message_fn(OnMessageFn fn) override;
    int set_on_user_message_fn(OnMessageFn fn) override;
    int set_on_local_connect_fn(OnLocalConnectedFn fn) override;
    int set_on_local_message_fn(OnMessageFn fn) override;
    int set_queue_on_main_fn(QueueOnMainFn fn) override;

    FilamentSyncMode get_filament_sync_mode() const override { return FilamentSyncMode::subscription; }

private:
    std::string control_file_path() const;
    std::string build_push_status_json(const FakeBambuStatusStep& step) const;
    std::string build_version_json() const;
    void        emit_status(const std::string& dev_id, const FakeBambuStatusStep& step);
    void        emit_json(const std::string& dev_id, const std::string& payload);
    int         handle_printer_command(const std::string& dev_id, const std::string& json_str);
    int         run_scenario(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
    void        stop_scenario_thread();
    void        start_heartbeat();
    void        stop_heartbeat();
    void        heartbeat_loop();
    void        announce_if_needed();
    static void select_machine_on_main(const std::string& dev_id);
    std::string connected_or_default() const;

    std::string                         log_dir;
    std::string                         selected_machine;
    std::string                         connected_dev_id;
    std::string                         m_last_print_filename;
    std::shared_ptr<ICloudServiceAgent> m_cloud_agent;

    FakeBambuStatusStep                m_current;
    std::vector<FakeBambuStatusStep>   m_scenario;
    bool                               m_offline{false};
    bool                               m_announced{false};
    bool                               m_control_file_present{false};
    std::int64_t                       m_control_mtime{0};

    std::thread               m_scenario_thread;
    std::atomic<bool>         m_scenario_running{false};
    std::atomic<bool>         m_stop_requested{false};
    std::mutex                m_scenario_done_mutex;
    std::condition_variable   m_scenario_done_cv;

    std::thread               m_heartbeat_thread;
    std::atomic<bool>         m_heartbeat_stop{false};
    std::mutex                m_heartbeat_mutex;
    std::condition_variable   m_heartbeat_cv;

    OnMsgArrivedFn            on_ssdp_msg_fn;
    OnPrinterConnectedFn      on_printer_connected_fn;
    GetSubscribeFailureFn     on_subscribe_failure_fn;
    OnMessageFn               on_message_fn;
    OnMessageFn               on_user_message_fn;
    OnLocalConnectedFn        on_local_connect_fn;
    OnMessageFn               on_local_message_fn;
    QueueOnMainFn             queue_on_main_fn;
    OnServerErrFn             on_server_err_fn;

    mutable std::mutex state_mutex;
};

} // namespace Slic3r
