#ifndef __FAKE_PRINTER_AGENT_HPP__
#define __FAKE_PRINTER_AGENT_HPP__

#include "IPrinterAgent.hpp"
#include "ICloudServiceAgent.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r {

// Agent id this implementation registers under, mirroring
// ORCA_PRINTER_AGENT_ID / BBL_PRINTER_AGENT_ID in NetworkAgentFactory.hpp.
static constexpr char FAKE_PRINTER_AGENT_ID[] = "fake";
// Device id the simulator answers to, shared by the shell hook that
// registers it and the manual console that drives it.
static constexpr char FAKE_PRINTER_DEV_ID[] = "FAKE001";

/**
 * One tick of a simulated print job. Mirrors the subset of Bambu's
 * "push_status" fields that the GUI (MachineObject::parse_json) actually
 * reads: gcode_state, mc_percent, mc_remaining_time and the temperature
 * pairs.
 */
struct FakePrinterStatusStep
{
    int         delay_ms             = 0;
    std::string gcode_state          = "IDLE"; // IDLE | PREPARE | RUNNING | PAUSE | FINISH | FAILED
    int         mc_percent           = 0;
    int         mc_remaining_time    = 0; // minutes
    double      nozzle_temper        = 25.0;
    double      nozzle_target_temper = 0.0;
    double      bed_temper           = 25.0;
    double      bed_target_temper    = 0.0;
};

/**
 * FakePrinterAgent - IPrinterAgent implementation backed by a scripted
 * status sequence instead of a real MQTT/LAN connection.
 *
 * Lets the GUI's device-monitor code paths (MachineObject, DevManager) be
 * exercised without physical hardware: connect_printer()/start_print() and
 * friends drive the same callbacks a real agent would, fed by canned
 * "push_status" JSON instead of a live printer.
 *
 * Two ways to drive it:
 * - Automated: start_print()/start_local_print() replay whatever scenario is
 *   installed via set_scenario() (defaults to build_default_print_scenario())
 *   on a background thread, one step per delay_ms, until it completes or is
 *   cancelled.
 * - Manual: push_status() sends a single caller-built step immediately, for
 *   interactive poking (a debug menu, a REPL, a single Catch2 assertion)
 *   without waiting on the scripted timeline.
 */
class FakePrinterAgent : public IPrinterAgent {
public:
    explicit FakePrinterAgent(std::string log_dir);
    ~FakePrinterAgent() override;

    static AgentInfo get_agent_info_static();
    AgentInfo get_agent_info() override { return get_agent_info_static(); }

    /** Default "connect -> heat -> print -> complete" scenario. */
    static std::vector<FakePrinterStatusStep> build_default_print_scenario();

    /** Named single-shot states for the manual-testing console (label -> step), e.g. "Idle", "Printing 25%". */
    static std::vector<std::pair<std::string, FakePrinterStatusStep>> build_named_presets();

    /** Install the scenario a subsequent start_print()/start_local_print() will replay. */
    void set_scenario(std::vector<FakePrinterStatusStep> steps);

    /** Push one status step to registered callbacks immediately, bypassing the scripted timeline. */
    void push_status(const FakePrinterStatusStep& step);

    /** Block until a scenario started by start_print()/start_local_print() has finished. Test-only convenience. */
    void wait_scenario_done();

    /** Stop a running scripted scenario without disconnecting. No-op if none is running. */
    void cancel_scenario();

    // ========================================================================
    // IPrinterAgent Interface Implementation
    // ========================================================================

    void set_cloud_agent(std::shared_ptr<ICloudServiceAgent> cloud) override;

    // Communication
    int send_message(std::string dev_id, std::string json_str, int qos, int flag) override;
    int connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password, bool use_ssl) override;
    int disconnect_printer() override;
    int send_message_to_printer(std::string dev_id, std::string json_str, int qos, int flag) override;

    // Certificates
    int check_cert() override;
    void install_device_cert(std::string dev_id, bool lan_only) override;

    // Discovery
    bool start_discovery(bool start, bool sending) override;

    // Binding
    int ping_bind(std::string ping_code) override;
    int bind_detect(std::string dev_ip, std::string sec_link, detectResult& detect) override;
    int bind(std::string dev_ip, std::string dev_id, std::string sec_link, std::string timezone, bool improved, OnUpdateStatusFn update_fn) override;
    int unbind(std::string dev_id) override;
    int request_bind_ticket(std::string* ticket) override;
    int set_server_callback(OnServerErrFn fn) override;

    // Machine Selection
    std::string get_user_selected_machine() override;
    int set_user_selected_machine(std::string dev_id) override;

    // Print Job Operations
    int start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_send_gcode_to_sdcard(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, OnWaitFn wait_fn) override;
    int start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;
    int start_sdcard_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn) override;

    // Callbacks
    int set_on_ssdp_msg_fn(OnMsgArrivedFn fn) override;
    int set_on_printer_connected_fn(OnPrinterConnectedFn fn) override;
    int set_on_subscribe_failure_fn(GetSubscribeFailureFn fn) override;
    int set_on_message_fn(OnMessageFn fn) override;
    int set_on_user_message_fn(OnMessageFn fn) override;
    int set_on_local_connect_fn(OnLocalConnectedFn fn) override;
    int set_on_local_message_fn(OnMessageFn fn) override;
    int set_queue_on_main_fn(QueueOnMainFn fn) override;

private:
    std::string build_push_status_json(const std::string& subtask_name, int plate_idx, const FakePrinterStatusStep& step, bool full) const;
    void emit_status(const std::string& dev_id, const FakePrinterStatusStep& step, bool full);
    int run_scenario(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
    void stop_scenario_thread();

    // Manual-testing console: reads commands from stdin when
    // JUSPRIN_FAKE_PRINTER is set in the environment, driving this
    // same instance through the methods above so a human can poke the fake
    // printer while watching the running app's device monitor react.
    void maybe_start_debug_console();
    void run_debug_console();
    std::thread m_console_thread;

    std::string log_dir;
    std::string selected_machine;
    std::string connected_dev_id;
    std::shared_ptr<ICloudServiceAgent> m_cloud_agent;

    std::vector<FakePrinterStatusStep> m_scenario;

    std::thread m_scenario_thread;
    std::atomic<bool> m_scenario_running{false};
    std::atomic<bool> m_stop_requested{false};
    std::mutex m_scenario_done_mutex;
    std::condition_variable m_scenario_done_cv;

    // Callbacks
    OnMsgArrivedFn on_ssdp_msg_fn;
    OnPrinterConnectedFn on_printer_connected_fn;
    GetSubscribeFailureFn on_subscribe_failure_fn;
    OnMessageFn on_message_fn;
    OnMessageFn on_user_message_fn;
    OnLocalConnectedFn on_local_connect_fn;
    OnMessageFn on_local_message_fn;
    QueueOnMainFn queue_on_main_fn;
    OnServerErrFn on_server_err_fn;

    mutable std::mutex state_mutex;
};

} // namespace Slic3r

#endif // __FAKE_PRINTER_AGENT_HPP__
