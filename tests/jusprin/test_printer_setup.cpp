#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterPhoto.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterSetupController.hpp"

#include <deque>
#include <algorithm>
#include <memory>

using namespace Slic3r::GUI::JusPrin::PrinterSetup;

namespace {

class FakeRecognition final : public IPrinterRecognitionService
{
public:
    bool ready() const override { return available; }
    bool start(std::uint64_t generation, const PrinterEvidence& evidence,
               const std::vector<const PrinterCandidate*>& choices) override
    {
        last_generation = generation;
        last_evidence = evidence;
        ++starts_seen;
        offered.clear();
        for (const auto* choice : choices) offered.push_back(choice->id);
        return starts;
    }
    void cancel() override { ++cancels; }
    std::optional<RecognitionEvent> poll() override
    {
        if (events.empty()) return std::nullopt;
        auto event = events.front(); events.pop_front(); return event;
    }
    bool available{true};
    bool starts{true};
    int cancels{0};
    int starts_seen{0};
    PrinterEvidence last_evidence;
    std::uint64_t last_generation{0};
    std::vector<std::string> offered;
    std::deque<RecognitionEvent> events;
};

std::vector<PrinterCandidate> fixtures()
{
    return {
        {"a1", "BBL", "Bambu Lab", "Bambu Lab A1 mini", "Bambu Lab A1 mini", "N1", "0.4",
         "Bambu Lab A1 mini 0.4 nozzle", "180 × 180 × 180 mm", "Generic PLA", "Textured PEI Plate", ""},
        {"a1-06", "BBL", "Bambu Lab", "Bambu Lab A1 mini", "Bambu Lab A1 mini", "N1", "0.6",
         "Bambu Lab A1 mini 0.6 nozzle", "180 × 180 × 180 mm", "Generic PLA", "Textured PEI Plate", ""},
        {"v2", "Creality", "Creality", "Ender-3 V2", "Ender-3 V2", "ender3v2", "0.4",
         "Creality Ender-3 V2 0.4 nozzle", "220 × 220 × 250 mm", "Generic PLA", "Textured PEI Plate", ""},
        {"neo", "Creality", "Creality", "Ender-3 V2 Neo", "Ender-3 V2 Neo", "ender3v2neo", "0.4",
         "Creality Ender-3 V2 Neo 0.4 nozzle", "220 × 220 × 250 mm", "Generic PLA", "Textured PEI Plate", ""}
    };
}

RecognitionEvent result_event(std::uint64_t generation, RecognitionDisposition disposition,
                              std::vector<std::string> ids, double confidence = 1., std::string unresolved = {})
{
    return {generation,
            RecognitionResult{disposition, std::move(ids), "evidence", "assumption", confidence, std::move(unresolved)},
            std::nullopt};
}

PrinterEvidence described(std::string description)
{
    PrinterEvidence evidence;
    evidence.description = std::move(description);
    return evidence;
}

} // namespace

TEST_CASE("the packaged printer catalogue resolves real profile details", "[printer-setup]")
{
    const PrinterCatalog catalog = PrinterCatalog::load(std::string(JUSPRIN_SOURCE_DIR) + "/resources");
    const auto found = std::find_if(catalog.candidates().begin(), catalog.candidates().end(), [](const auto& candidate) {
        return candidate.model_name == "Bambu Lab A1 mini" && candidate.variant == "0.4";
    });
    REQUIRE(found != catalog.candidates().end());
    CHECK(found->vendor_id == "BBL");
    CHECK(found->device_model_id == "N1");
    CHECK(found->preset_name == "Bambu Lab A1 mini 0.4 nozzle");
    CHECK(found->build_volume == "180 × 180 × 180 mm");
    CHECK_FALSE(found->default_material.empty());
}

TEST_CASE("recognition is validated against the local catalogue and ignores stale results", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    int applies = 0;
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition),
        [&](const PrinterCandidate&, std::string&) { ++applies; return true; });

    REQUIRE(controller.recognize({"Ender 3", {}, {}, {}}));
    const auto current = fake->last_generation;
    fake->events.push_back(result_event(current - 1, RecognitionDisposition::Recognized, {"a1"}));
    fake->events.push_back(result_event(current, RecognitionDisposition::Recognized, {"invented"}));
    controller.poll();
    CHECK(controller.state() == FlowState::Error);
    CHECK(applies == 0);
}

TEST_CASE("recognition rejects a real catalogue ID that was not offered in that request", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});
    REQUIRE(controller.recognize({"Bambu Lab A1 mini", {}, {}, {}}));
    REQUIRE(std::find(fake->offered.begin(), fake->offered.end(), "v2") == fake->offered.end());
    fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Recognized, {"v2"}));
    controller.poll();
    CHECK(controller.state() == FlowState::Error);
    CHECK(controller.candidates().empty());
}

TEST_CASE("ambiguous and network choices apply only after confirmation", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    std::string applied;
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition),
        [&](const PrinterCandidate& candidate, std::string&) { applied = candidate.id; return true; });

    REQUIRE(controller.recognize({"Ender 3", {}, {}, {}}));
    fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Ambiguous, {"v2", "neo"}));
    controller.poll();
    REQUIRE(controller.state() == FlowState::Ambiguous);
    CHECK(applied.empty());
    CHECK_FALSE(controller.choose("a1"));
    REQUIRE(controller.choose("neo"));
    CHECK(applied.empty());
    REQUIRE(controller.confirm());
    CHECK(applied == "neo");

    controller.start_over();
    const DiscoveredPrinter discovered{"device", "Workshop A1", "192.0.2.2", "N1", "LAN", true};
    REQUIRE(controller.use_discovered(discovered));
    REQUIRE(controller.evidence().discovered_device);
    CHECK(controller.evidence().discovered_device->stable_id == "device");
    CHECK(applied == "neo");
    REQUIRE(controller.confirm());
    CHECK(applied == "a1");
}

TEST_CASE("saying more about an ambiguous printer re-recognizes with the added detail", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});

    CHECK_FALSE(controller.clarify("it has a knob"));
    REQUIRE(controller.recognize(described("the ender with the touchscreen")));
    fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Ambiguous, {"v2", "neo"}));
    controller.poll();
    REQUIRE(controller.state() == FlowState::Ambiguous);

    CHECK_FALSE(controller.clarify(""));
    REQUIRE(controller.clarify("it has a knob"));
    CHECK(controller.state() == FlowState::Recognizing);
    CHECK(fake->last_evidence.description == "the ender with the touchscreen\nit has a knob");
}

TEST_CASE("corrections are re-resolved to an offered installable variant", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    std::string applied;
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition),
        [&](const PrinterCandidate& candidate, std::string&) { applied = candidate.id; return true; });

    REQUIRE(controller.recognize({"Bambu Lab A1 mini\nCorrection: 0.6 nozzle", {}, {}, {}}));
    CHECK(std::find(fake->offered.begin(), fake->offered.end(), "a1-06") != fake->offered.end());
    fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Recognized, {"a1-06"}));
    controller.poll();
    REQUIRE(controller.state() == FlowState::Recognized);
    CHECK(controller.candidates().front()->variant == "0.6");
    REQUIRE(controller.confirm());
    CHECK(applied == "a1-06");
}

TEST_CASE("no-match, unavailable service, and apply failures remain visible", "[printer-setup]")
{
    SECTION("unavailable") {
        auto recognition = std::make_unique<FakeRecognition>();
        recognition->available = false;
        PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});
        CHECK_FALSE(controller.recognize({"A1 mini", {}, {}, {}}));
        CHECK(controller.state() == FlowState::Error);
        CHECK_FALSE(controller.retryable());
    }
    SECTION("no match") {
        auto recognition = std::make_unique<FakeRecognition>();
        auto* fake = recognition.get();
        PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});
        REQUIRE(controller.recognize({"mystery printer", {}, {}, {}}));
        fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::NoMatch, {}));
        controller.poll();
        CHECK(controller.state() == FlowState::Error);
        CHECK_FALSE(controller.error().empty());
    }
    SECTION("apply failure") {
        auto recognition = std::make_unique<FakeRecognition>();
        auto* fake = recognition.get();
        int applies = 0;
        PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition),
            [&](const PrinterCandidate&, std::string& error) {
                ++applies;
                error = "installation failed";
                return false;
            });
        REQUIRE(controller.recognize({"A1 mini", {}, {}, {}}));
        fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Recognized, {"a1"}));
        controller.poll();
        CHECK_FALSE(controller.confirm());
        CHECK(controller.state() == FlowState::Error);
        CHECK(controller.retryable());
        CHECK(controller.error() == "installation failed");
        CHECK(applies == 1);
    }
}

TEST_CASE("start over cancels recognition and ignores its late callback", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});
    REQUIRE(controller.recognize({"Ender 3", {}, {}, {}}));
    const auto generation = fake->last_generation;
    controller.start_over();
    fake->events.push_back(result_event(generation, RecognitionDisposition::Recognized, {"v2"}));
    controller.poll();
    CHECK(controller.state() == FlowState::Initial);
    CHECK(controller.candidates().empty());
    CHECK(fake->cancels > 0);
}

TEST_CASE("a low-confidence single identification is offered as a choice, not presumed", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    int applies = 0;
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition),
        [&](const PrinterCandidate&, std::string&) { ++applies; return true; });

    SECTION("below the threshold") {
        REQUIRE(controller.recognize(described("Ender 3")));
        fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Recognized, {"v2"},
                                            kRecognitionConfidenceThreshold - 0.01));
        controller.poll();
        REQUIRE(controller.state() == FlowState::Ambiguous);
        CHECK(controller.uncertain());
        CHECK_FALSE(controller.confirm());
        REQUIRE(controller.choose("v2"));
        CHECK_FALSE(controller.uncertain());
        REQUIRE(controller.confirm());
        CHECK(applies == 1);
    }
    SECTION("at the threshold") {
        REQUIRE(controller.recognize(described("Ender 3")));
        fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Recognized, {"v2"},
                                            kRecognitionConfidenceThreshold));
        controller.poll();
        CHECK(controller.state() == FlowState::Recognized);
        CHECK_FALSE(controller.uncertain());
    }
    SECTION("an ambiguous answer with only one valid ID") {
        REQUIRE(controller.recognize(described("Ender 3")));
        fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Ambiguous, {"v2", "invented"}));
        controller.poll();
        REQUIRE(controller.state() == FlowState::Ambiguous);
        CHECK(controller.uncertain());
        REQUIRE(controller.candidates().size() == 1);
        CHECK(controller.candidates().front()->id == "v2");
    }
    CHECK(applies <= 1);
}

TEST_CASE("a correction keeps prior evidence and an unrepresentable one stays unresolved", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    auto* fake = recognition.get();
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});

    const DiscoveredPrinter device{"device-1", "Workshop A1", "192.0.2.2", "N1", "LAN", true};
    REQUIRE(controller.use_discovered(device));
    CHECK_FALSE(controller.correct(""));
    REQUIRE(controller.correct("it's the Combo with the AMS"));
    REQUIRE(fake->last_evidence.discovered_device);
    CHECK(fake->last_evidence.discovered_device->stable_id == "device-1");
    CHECK(fake->last_evidence.description.find("Current match: Bambu Lab A1 mini 0.4 mm nozzle") != std::string::npos);
    CHECK(fake->last_evidence.description.find("Correction: it's the Combo with the AMS") != std::string::npos);

    fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Recognized, {"a1"}, 1.,
                                        "the Combo with the AMS"));
    controller.poll();
    REQUIRE(controller.state() == FlowState::Recognized);
    CHECK(controller.unresolved_correction() == "the Combo with the AMS");
    CHECK(controller.evidence().discovered_device);

    // A second correction replaces the first instead of stacking both.
    REQUIRE(controller.correct("0.6 nozzle"));
    const std::string& description = fake->last_evidence.description;
    CHECK(description.find("Combo") == std::string::npos);
    CHECK(description.find("Correction: 0.6 nozzle") != std::string::npos);
    CHECK(controller.unresolved_correction().empty());
}

TEST_CASE("two network printers with the same nickname stay distinct by device ID", "[printer-setup]")
{
    std::vector<std::string> applied_devices;
    PrinterSetupController* self = nullptr;
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::make_unique<FakeRecognition>(),
        [&](const PrinterCandidate&, std::string&) {
            applied_devices.push_back(self->evidence().discovered_device->stable_id);
            return true;
        });
    self = &controller;
    const DiscoveredPrinter first{"01P00A000001", "Workshop", "192.0.2.2", "N1", "LAN", true};
    const DiscoveredPrinter second{"01P00A000002", "Workshop", "192.0.2.3", "N1", "LAN", true};

    REQUIRE(controller.use_discovered(first));
    REQUIRE(controller.confirm());
    controller.start_over();
    REQUIRE(controller.use_discovered(second));
    REQUIRE(controller.confirm());
    CHECK(applied_devices == std::vector<std::string>{"01P00A000001", "01P00A000002"});
}

TEST_CASE("confirm applies exactly once", "[printer-setup]")
{
    int applies = 0;
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::make_unique<FakeRecognition>(),
        [&](const PrinterCandidate&, std::string&) { ++applies; return true; });
    REQUIRE(controller.use_discovered({"device", "A1", "192.0.2.2", "N1", "LAN", true}));
    REQUIRE(controller.confirm());
    CHECK_FALSE(controller.confirm());
    CHECK_FALSE(controller.choose("a1"));
    CHECK_FALSE(controller.retry());
    CHECK(controller.state() == FlowState::Complete);
    CHECK(applies == 1);
}

TEST_CASE("retry repeats only the step that failed", "[printer-setup]")
{
    SECTION("apply failure returns to the held candidate without a new request") {
        auto recognition = std::make_unique<FakeRecognition>();
        auto* fake = recognition.get();
        int applies = 0;
        PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition),
            [&](const PrinterCandidate&, std::string& error) {
                if (++applies == 1) { error = "installation failed"; return false; }
                return true;
            });
        REQUIRE(controller.recognize(described("A1 mini")));
        fake->events.push_back(result_event(fake->last_generation, RecognitionDisposition::Recognized, {"a1"}));
        controller.poll();
        CHECK_FALSE(controller.confirm());
        REQUIRE(controller.retry());
        CHECK(controller.state() == FlowState::Recognized);
        CHECK(controller.error().empty());
        CHECK(fake->starts_seen == 1);
        REQUIRE(controller.confirm());
        CHECK(applies == 2);
    }
    SECTION("a retryable recognition failure resends the same evidence") {
        auto recognition = std::make_unique<FakeRecognition>();
        auto* fake = recognition.get();
        PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});
        REQUIRE(controller.recognize(described("A1 mini")));
        const auto first = fake->last_generation;
        fake->events.push_back({first, std::nullopt, RecognitionError{"network", "offline", true}});
        controller.poll();
        REQUIRE(controller.state() == FlowState::Error);
        REQUIRE(controller.retry());
        CHECK(controller.state() == FlowState::Recognizing);
        CHECK(fake->last_generation > first);
        CHECK(fake->last_evidence.description == "A1 mini");
    }
    SECTION("a non-retryable failure cannot be retried") {
        auto recognition = std::make_unique<FakeRecognition>();
        recognition->available = false;
        PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});
        CHECK_FALSE(controller.recognize(described("A1 mini")));
        CHECK_FALSE(controller.retry());
    }
}

TEST_CASE("start over releases a staged photo", "[printer-setup]")
{
    auto recognition = std::make_unique<FakeRecognition>();
    PrinterSetupController controller(PrinterCatalog(fixtures()), std::move(recognition), {});
    PrinterEvidence evidence;
    evidence.image_bytes = std::string("\xFF\xD8\xFF", 3) + std::string(64, 'x');
    evidence.image_mime = "image/jpeg";
    evidence.image_name = "printer.jpg";
    REQUIRE(controller.recognize(evidence));
    CHECK_FALSE(controller.evidence().image_bytes.empty());
    controller.start_over();
    CHECK(controller.evidence().image_bytes.empty());
    CHECK(controller.evidence().image_name.empty());
}

TEST_CASE("photo validation classifies by content and enforces the size cap", "[printer-setup]")
{
    const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + "body";
    const std::string jpeg = std::string("\xFF\xD8\xFF\xE0", 4) + "body";
    const std::string webp = std::string("RIFF\x10\x00\x00\x00WEBPVP8 ", 16);
    CHECK(check_photo(png).mime == "image/png");
    CHECK(check_photo(jpeg).mime == "image/jpeg");
    CHECK(check_photo(webp).mime == "image/webp");

    const PhotoCheck gif = check_photo("GIF89a....");
    CHECK(gif.mime.empty());
    CHECK_FALSE(gif.error.empty());
    CHECK(check_photo(std::string("RIFF\x10\x00\x00\x00WAVEfmt ", 16)).mime.empty());
    CHECK(check_photo({}).error.find("empty") != std::string::npos);

    std::string at_cap = png;
    at_cap.resize(kMaximumPhotoBytes, 'x');
    CHECK(check_photo(at_cap).mime == "image/png");
    at_cap.push_back('x');
    const PhotoCheck oversized = check_photo(at_cap);
    CHECK(oversized.mime.empty());
    CHECK(oversized.error.find("10 MB") != std::string::npos);
}
