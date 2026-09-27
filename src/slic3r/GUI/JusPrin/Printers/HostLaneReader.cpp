#include "HostLaneReader.hpp"

#include "NamedPrinters.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/Moonraker.hpp"

#include <atomic>
#include <chrono>
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

// Shared with the reads in flight, which may finish after the app has
// started to exit.
const std::shared_ptr<HostReadings>& readings()
{
    static const auto shared = std::make_shared<HostReadings>();
    return shared;
}

// The named printer's saved profile, when it is connected to a Moonraker host.
const Preset* moonraker_printer(const std::string& name)
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return nullptr;
    const Preset* preset = bundle->printers.find_preset(name, false, true);
    if (preset == nullptr || !is_named_printer(*preset))
        return nullptr;
    const auto* type = preset->config.option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (type == nullptr || type->value != htMoonraker || preset->config.opt_string("print_host").empty())
        return nullptr;
    return preset;
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
    const Preset* preset = moonraker_printer(name);
    if (preset == nullptr)
        return std::nullopt;
    const std::string key = host_key(name, preset->config);
    const auto        now = HostReadings::Clock::now();
    // Constructed after Http's curl state, so destroyed before it.
    static StopReadsAtExit stop_at_exit;
    if (!reads()->stopping && readings()->begin(key, now)) {
        ++reads()->running;
        auto reader = std::make_shared<const LaneReader>(preset->config, reads());
        std::thread([shared = readings(), in_flight = reads(), key, reader] {
            shared->finish(key, reader->read(), HostReadings::Clock::now());
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
    if (const Preset* preset = moonraker_printer(name))
        readings()->note_answered(host_key(name, preset->config), HostReadings::Clock::now());
}

} // namespace Slic3r::GUI::JusPrin::Printers
