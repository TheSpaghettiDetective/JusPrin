#include "HostLaneReader.hpp"

#include "NamedPrinters.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/Moonraker.hpp"
#include "slic3r/Utils/PrintHost.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

namespace Slic3r::GUI::JusPrin::Printers {

namespace {

// The reads in flight. A read runs on a thread nothing joins, and Http's curl
// is torn down with the process's statics; a read still inside curl then
// would crash the exit. So the app's exit stops new reads and cancels those
// in flight, and waits a moment for them to leave.
struct Reads
{
    std::atomic<bool> stopping{false};
    std::atomic<int>  running{0};
};

const std::shared_ptr<Reads>& reads()
{
    static const auto shared = std::make_shared<Reads>();
    return shared;
}

struct StopReadsAtExit
{
    // Its own reference: the static behind reads() may be gone first.
    std::shared_ptr<Reads> m_reads = reads();

    ~StopReadsAtExit()
    {
        m_reads->stopping = true;
        // Curl checks for cancellation about once a second while it waits.
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (m_reads->running > 0 && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

// Orca's Moonraker print host, for its address and credential handling only:
// the same URL, API key, CA file and revocation setting an upload uses.
class LaneReader final : public Moonraker
{
public:
    // Moonraker copies what it reads from the config, so the copy passed in
    // need not outlive the constructor.
    LaneReader(DynamicPrintConfig config, std::shared_ptr<Reads> reads) : Moonraker(&config), m_reads(std::move(reads)) {}

    HostReading read() const
    {
        HostReading reading;
        const auto [status, body] = get("server/database/item?namespace=lane_data");
        // Moonraker answers 404 for a namespace nothing has written: a printer
        // without such a unit, which still answered.
        if (status != 200 && status != 404)
            return reading;
        reading.reached = true;
        read_activity(reading);
        if (status == 200)
            if (const auto report = parse_lane_data(body); report && report->unit) {
                reading.loaded = report->loaded;
                return reading;
            }
        const auto [mmu_status, mmu_body] = get("printer/objects/query?mmu");
        // Klippy not ready answers 503, and says nothing either way.
        if (mmu_status == 200)
            if (const auto report = parse_happy_hare(mmu_body))
                reading.loaded = report->unit ? report->loaded : std::vector<HostLane>();
        return reading;
    }

private:
    // What Klipper is doing, how its last job ended, and its camera, from a
    // host that has answered.
    void read_activity(HostReading& reading) const
    {
        const auto [status, body] = get("printer/objects/query?webhooks&print_stats&display_status");
        if (status == 200)
            reading.klipper = parse_klipper_state(body);
        else if (status == 503)
            // Moonraker answers 503 while Klipper's own process is not
            // connected to it, which its /server/info calls "disconnected".
            reading.klipper = KlipperState{"disconnected"};
        if (const auto [history_status, history_body] = get("server/history/list?limit=1&order=desc"); history_status == 200)
            reading.last_job = parse_last_job(history_body);
        if (const auto [webcams_status, webcams_body] = get("server/webcams/list"); webcams_status == 200)
            reading.camera = parse_webcams(webcams_body, make_url(""));
    }

    // The HTTP status, zero when nothing answered, and the body.
    std::pair<unsigned, std::string> get(const std::string& path) const
    {
        unsigned    status = 0;
        std::string body;
        auto        http = Http::get(make_url(path));
        set_auth(http);
        http.timeout_connect(5)
            .timeout_max(10)
            .on_complete([&](std::string reply, unsigned code) {
                status = code;
                body   = std::move(reply);
            })
            .on_error([&](std::string reply, std::string, unsigned code) {
                status = code;
                body   = std::move(reply);
            })
            .on_progress([this](Http::Progress, bool& cancel) { cancel = m_reads->stopping; })
#ifdef WIN32
            .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
            .perform_sync();
        return {status, body};
    }

    std::shared_ptr<Reads> m_reads;
};

// Any other print host -- OctoPrint, PrusaLink and the like -- is asked only
// whether it answers at the page its printer window loads: any HTTP reply
// counts, a login page or a refused key included, since the page is there.
// What is loaded is not asked; each host says it differently, if at all.
HostReading probe_page(const DynamicPrintConfig& config, const std::shared_ptr<Reads>& in_flight)
{
    // Http reports an HTTP error with its status and a failure to get any
    // reply with status zero; a reply it reports neither way (a final 3xx)
    // is still a reply.
    bool answered = true;
    // get_print_host_webui takes a mutable config; it only reads it.
    DynamicPrintConfig copy = config;
    Http::get(PrintHost::get_print_host_webui(&copy))
        .timeout_connect(5)
        .timeout_max(10)
        .on_error([&](std::string, std::string, unsigned code) { answered = code != 0; })
        .on_progress([&](Http::Progress, bool& cancel) { cancel = in_flight->stopping; })
        .perform_sync();
    HostReading reading;
    reading.reached = answered;
    return reading;
}

// Shared with the reads in flight, which may finish after the app has
// started to exit.
const std::shared_ptr<HostReadings>& readings()
{
    static const auto shared = std::make_shared<HostReadings>();
    return shared;
}

// The named printer's saved profile, when it is connected to a print host.
const Preset* host_printer(const std::string& name)
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return nullptr;
    const Preset* preset = bundle->printers.find_preset(name, false, true);
    if (preset == nullptr || !is_named_printer(*preset))
        return nullptr;
    if (preset->config.opt_string("print_host").empty())
        return nullptr;
    return preset;
}

bool is_moonraker(const DynamicPrintConfig& config)
{
    const auto* type = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
    return type != nullptr && type->value == htMoonraker;
}

// A reading belongs to the settings it was taken with: a printer given
// another address or key starts again from nothing.
std::string host_key(const std::string& name, const DynamicPrintConfig& config)
{
    std::string key = name;
    for (const char* option : {"print_host", "printhost_apikey", "printhost_cafile"})
        key += '\n' + config.opt_string(option);
    return key;
}

} // namespace

std::optional<HostStatus> host_status(const std::string& name)
{
    const Preset* preset = host_printer(name);
    if (preset == nullptr)
        return std::nullopt;
    const std::string key = host_key(name, preset->config);
    const auto        now = HostReadings::Clock::now();
    // Constructed after Http's curl state, so destroyed before it.
    static StopReadsAtExit stop_at_exit;
    if (!reads()->stopping && readings()->begin(key, now)) {
        ++reads()->running;
        std::function<HostReading()> read;
        if (is_moonraker(preset->config)) {
            auto reader = std::make_shared<const LaneReader>(preset->config, reads());
            read        = [reader] { return reader->read(); };
        } else {
            read = [config = preset->config, in_flight = reads()] { return probe_page(config, in_flight); };
        }
        std::thread([shared = readings(), in_flight = reads(), key, read = std::move(read)] {
            shared->finish(key, read(), HostReadings::Clock::now());
            --in_flight->running;
        }).detach();
    }
    return readings()->status(key, now);
}

void refresh_host_readings()
{
    for (const NamedPrinter& printer : named_printers())
        host_status(printer.name);
}

void note_host_answered(const std::string& name)
{
    if (const Preset* preset = host_printer(name))
        readings()->note_answered(host_key(name, preset->config), HostReadings::Clock::now());
}

} // namespace Slic3r::GUI::JusPrin::Printers
