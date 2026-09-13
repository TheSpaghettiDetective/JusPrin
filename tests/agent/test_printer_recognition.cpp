#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterRecognition.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::PrinterSetup;

namespace {

class FakeTransport final : public Agent::IAgentHttpTransport
{
public:
    bool post(Agent::AgentHttpRequest request, EventFn event) override
    {
        last = std::move(request);
        callback = std::move(event);
        return true;
    }
    void cancel() override { ++cancels; }
    Agent::AgentHttpRequest last;
    EventFn callback;
    int cancels{0};
};

PrinterCandidate candidate()
{
    return {"local-a1", "BBL", "Bambu Lab", "Bambu Lab A1 mini", "Bambu Lab A1 mini", "N1", "0.4",
            "Bambu Lab A1 mini 0.4 nozzle", "180 × 180 × 180 mm", "Generic PLA", "Textured PEI Plate", ""};
}

PrinterCandidate candidate(std::string id, std::string model, std::string variant)
{
    return {std::move(id), "BBL", "Bambu Lab", model, model, model, std::move(variant),
            model + " nozzle", "180 × 180 × 180 mm", "Generic PLA", "Textured PEI Plate", ""};
}

} // namespace

TEST_CASE("printer recognition uses a stateless strict Responses request", "[printer-recognition]")
{
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    OpenAIPrinterRecognition service({"secret", "test-model", "https://example.invalid/responses"},
                                     std::move(transport));
    const PrinterCandidate local = candidate();
    REQUIRE(service.start(7, {"small Bambu", "image", "image/png", "printer.png"}, {&local}));
    const auto body = nlohmann::json::parse(fake->last.body);
    CHECK(body.at("store") == false);
    CHECK(body.at("model") == "test-model");
    CHECK(body.at("max_output_tokens") == 512);
    CHECK(body.at("text").at("format").at("type") == "json_schema");
    CHECK(body.at("text").at("format").at("strict") == true);
    CHECK(body.at("text").at("format").at("schema").at("additionalProperties") == false);
    CHECK(body.at("text").at("format").at("schema").at("properties").at("candidate_ids").at("maxItems") == 3);
    const auto& required = body.at("text").at("format").at("schema").at("required");
    CHECK(std::find(required.begin(), required.end(), "confidence") != required.end());
    CHECK(std::find(required.begin(), required.end(), "unresolved_correction") != required.end());
    const auto& content = body.at("input").front().at("content");
    REQUIRE(content.size() == 2);
    CHECK(content.back().at("image_url").get<std::string>().rfind("data:image/png;base64,", 0) == 0);
    const auto prompt = content.front().at("text").get<std::string>();
    CHECK(prompt.find("\"default_variant\":true") != std::string::npos);
    CHECK(fake->last.authorization == "Bearer secret");
    CHECK(fake->last.response_size_limit == 2u * 1024u * 1024u);

    const nlohmann::json recognized = {{"disposition", "recognized"}, {"candidate_ids", {"local-a1"}},
                                       {"evidence_summary", "A1 mini"}, {"assumption", "0.4 mm nozzle"},
                                       {"confidence", 0.92}, {"unresolved_correction", "AMS"}};
    const nlohmann::json response = {{"output", {{{"content", {{{"type", "output_text"},
                                                                   {"text", recognized.dump()}}}}}}}};
    fake->callback({Agent::AgentHttpEvent::Kind::Data, response.dump(), {}, 200});
    fake->callback({Agent::AgentHttpEvent::Kind::Complete, {}, {}, 200});
    const auto event = service.poll();
    REQUIRE(event);
    REQUIRE(event->result);
    CHECK(event->generation == 7);
    CHECK(event->result->candidate_ids == std::vector<std::string>{"local-a1"});
    CHECK(event->result->confidence == Catch::Approx(0.92));
    CHECK(event->result->unresolved_correction == "AMS");
}

TEST_CASE("a recognition result without a confidence reads as uncertain", "[printer-recognition]")
{
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    OpenAIPrinterRecognition service({"secret", "test-model", "endpoint"}, std::move(transport));
    const PrinterCandidate local = candidate();
    REQUIRE(service.start(8, {"A1 mini", {}, {}, {}}, {&local}));
    const nlohmann::json recognized = {{"disposition", "recognized"}, {"candidate_ids", {"local-a1"}}};
    const nlohmann::json response = {{"output_text", recognized.dump()}};
    fake->callback({Agent::AgentHttpEvent::Kind::Data, response.dump(), {}, 200});
    fake->callback({Agent::AgentHttpEvent::Kind::Complete, {}, {}, 200});
    const auto event = service.poll();
    REQUIRE(event);
    REQUIRE(event->result);
    CHECK(event->result->confidence == 0.);
}

TEST_CASE("printer recognition maps provider authentication failures", "[printer-recognition]")
{
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    OpenAIPrinterRecognition service({"secret", "test-model", "endpoint"}, std::move(transport));
    const PrinterCandidate local = candidate();
    REQUIRE(service.start(9, {"A1 mini", {}, {}, {}}, {&local}));
    fake->callback({Agent::AgentHttpEvent::Kind::Error, {}, "denied", 401});
    const auto event = service.poll();
    REQUIRE(event);
    REQUIRE(event->error);
    CHECK(event->error->code == "authentication");
    CHECK_FALSE(event->error->retryable);
}

TEST_CASE("printer recognition keeps malformed and transport failures visible", "[printer-recognition]")
{
    const PrinterCandidate local = candidate();
    SECTION("malformed response") {
        auto transport = std::make_unique<FakeTransport>();
        auto* fake = transport.get();
        OpenAIPrinterRecognition service({"secret", "test-model", "endpoint"}, std::move(transport));
        REQUIRE(service.start(10, {"A1 mini", {}, {}, {}}, {&local}));
        fake->callback({Agent::AgentHttpEvent::Kind::Data, "not-json", {}, 200});
        fake->callback({Agent::AgentHttpEvent::Kind::Complete, {}, {}, 200});
        const auto event = service.poll();
        REQUIRE(event);
        REQUIRE(event->error);
        CHECK(event->error->code == "malformed_response");
    }
    SECTION("network failure") {
        auto transport = std::make_unique<FakeTransport>();
        auto* fake = transport.get();
        OpenAIPrinterRecognition service({"secret", "test-model", "endpoint"}, std::move(transport));
        REQUIRE(service.start(11, {"A1 mini", {}, {}, {}}, {&local}));
        fake->callback({Agent::AgentHttpEvent::Kind::Error, {}, "offline", 0});
        const auto event = service.poll();
        REQUIRE(event);
        REQUIRE(event->error);
        CHECK(event->error->code == "network");
        CHECK(event->error->retryable);
    }
}
