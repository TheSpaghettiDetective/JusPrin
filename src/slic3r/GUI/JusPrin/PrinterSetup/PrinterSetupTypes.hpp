#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

struct DiscoveredPrinter
{
    std::string stable_id;
    std::string name;
    std::string ip_address;
    std::string device_model_id;
    std::string connection;
    bool connected{false};
    // What the printer reported about itself; zero or empty when it has not said.
    double nozzle_diameter{0.};
    std::string ams_name;
    struct Spool
    {
        std::string name;
        std::string colour; // "#RRGGBB"
    };
    std::vector<Spool> spools;
};

struct PrinterEvidence
{
    std::string description;
    std::string image_bytes;
    std::string image_mime;
    std::string image_name;
    std::optional<DiscoveredPrinter> discovered_device;
};

struct PrinterCandidate
{
    // Opaque, session-local identifier exposed to the recognition provider.
    std::string id;
    std::string vendor_id;
    std::string vendor_name;
    std::string model_id;
    std::string model_name;
    std::string device_model_id;
    std::string variant;
    std::string preset_name;
    std::string build_volume;
    std::string default_material;
    std::string default_plate;
    std::string artwork_path;
};

enum class RecognitionDisposition { Recognized, Ambiguous, NoMatch };

struct RecognitionResult
{
    RecognitionDisposition disposition{RecognitionDisposition::NoMatch};
    std::vector<std::string> candidate_ids;
    std::string evidence_summary;
    std::string assumption;
    // Provider self-assessment in [0, 1]. Diagnostic input only: the controller
    // combines it with the disposition and the validated candidate count.
    double confidence{0.};
    // The part of a user correction the returned candidate cannot represent.
    std::string unresolved_correction;
};

struct RecognitionError
{
    std::string code;
    std::string message;
    bool retryable{false};
};

struct RecognitionEvent
{
    std::uint64_t generation{0};
    std::optional<RecognitionResult> result;
    std::optional<RecognitionError> error;
};

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
