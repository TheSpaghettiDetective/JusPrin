#include <catch2/catch_all.hpp>

#include "slic3r/Utils/FakePrinterAgent.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/OrcaCloudServiceAgent.hpp"

namespace {

nlohmann::json flat_session_json(const nlohmann::json& fields)
{
    nlohmann::json session = {
        {"access_token", "test-token"},
        {"user_id", "test-user-id"}
    };
    session.update(fields);
    return session;
}

nlohmann::json nested_session_json(const nlohmann::json& metadata)
{
    return {
        {"access_token", "test-token"},
        {"user", {
            {"id", "test-user-id"},
            {"user_metadata", metadata}
        }}
    };
}

std::string resolved_display_name(const nlohmann::json& session)
{
    Slic3r::OrcaCloudServiceAgent agent("");
    REQUIRE(agent.set_user_session(session, false));
    return agent.get_user_nickname();
}

} // namespace

TEST_CASE("Check SSL certificates paths", "[Http][NotWorking]") {
    
    Slic3r::Http g = Slic3r::Http::get("https://github.com/");
    
    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });
    
    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });
    
    g.perform_sync();
    
    REQUIRE(status == 200);
}

TEST_CASE("Orca cloud flat session resolves display name consistently", "[OrcaCloudServiceAgent]")
{
    CHECK(resolved_display_name(flat_session_json({
        {"username", "orca_username"},
        {"display_name", "Display Name"},
        {"nickname", "Nickname"}
    })) == "Display Name");

    CHECK(resolved_display_name(flat_session_json({
        {"username", "orca_username"},
        {"nickname", "Nickname"}
    })) == "Nickname");

    CHECK(resolved_display_name(flat_session_json({
        {"username", "orca_username"},
        {"full_name", "Full Name"}
    })) == "Full Name");

    CHECK(resolved_display_name(flat_session_json({
        {"username", "orca_username"},
        {"name", "Provider Name"}
    })) == "Provider Name");

    CHECK(resolved_display_name(flat_session_json({
        {"username", "orca_username"}
    })) == "orca_username");
}

TEST_CASE("Orca cloud nested session resolves display name consistently", "[OrcaCloudServiceAgent]")
{
    CHECK(resolved_display_name(nested_session_json({
        {"username", "orca_username"},
        {"display_name", "Display Name"},
        {"nickname", "Nickname"}
    })) == "Display Name");

    CHECK(resolved_display_name(nested_session_json({
        {"username", "orca_username"},
        {"nickname", "Nickname"}
    })) == "Nickname");

    CHECK(resolved_display_name(nested_session_json({
        {"username", "orca_username"},
        {"full_name", "Full Name"}
    })) == "Full Name");

    CHECK(resolved_display_name(nested_session_json({
        {"username", "orca_username"},
        {"name", "Provider Name"}
    })) == "Provider Name");

    CHECK(resolved_display_name(nested_session_json({
        {"username", "orca_username"}
    })) == "orca_username");
}

TEST_CASE("FakePrinterAgent replays default scenario end-to-end", "[FakePrinterAgent]")
{
    Slic3r::FakePrinterAgent agent("");

    std::vector<nlohmann::json> received;
    agent.set_on_local_message_fn([&received](std::string /* dev_id */, std::string payload) {
        received.push_back(nlohmann::json::parse(payload));
    });

    bool connected = false;
    agent.set_on_local_connect_fn([&connected](int status, std::string, std::string) {
        connected = (status == Slic3r::ConnectStatusOk);
    });

    REQUIRE(agent.connect_printer("FAKE001", "127.0.0.1", "", "", false) == BAMBU_NETWORK_SUCCESS);
    CHECK(connected);
    REQUIRE(received.size() == 1);
    CHECK(received.back()["print"]["gcode_state"] == "IDLE");

    Slic3r::PrintParams params;
    params.dev_id = "FAKE001";
    int status_calls = 0;
    agent.start_local_print(params, [&status_calls](int, int, std::string) { ++status_calls; }, nullptr);
    agent.wait_scenario_done();

    REQUIRE(received.size() > 1);
    CHECK(received.back()["print"]["gcode_state"] == "FINISH");
    CHECK(received.back()["print"]["mc_percent"] == 100);
    CHECK(status_calls >= 2); // PrintingStageCreate + PrintingStageFinished

    // The scenario should visit each phase in order, ending on FINISH.
    std::vector<std::string> seen_states;
    for (auto& j : received) {
        std::string s = j["print"]["gcode_state"];
        if (seen_states.empty() || seen_states.back() != s)
            seen_states.push_back(s);
    }
    REQUIRE(seen_states.size() >= 4);
    CHECK(seen_states.front() == "IDLE");
    CHECK(seen_states.back() == "FINISH");
}

TEST_CASE("FakePrinterAgent push_status delivers a single custom step immediately", "[FakePrinterAgent]")
{
    Slic3r::FakePrinterAgent agent("");
    std::string last_payload;
    agent.set_on_local_message_fn([&last_payload](std::string, std::string payload) { last_payload = payload; });

    Slic3r::FakePrinterStatusStep step;
    step.gcode_state = "FAILED";
    step.mc_percent  = 42;
    agent.push_status(step);

    REQUIRE(!last_payload.empty());
    auto j = nlohmann::json::parse(last_payload);
    CHECK(j["print"]["gcode_state"] == "FAILED");
    CHECK(j["print"]["mc_percent"] == 42);
}

TEST_CASE("FakePrinterAgent cancellation stops the scripted scenario before it starts emitting", "[FakePrinterAgent]")
{
    Slic3r::FakePrinterAgent agent("");
    std::vector<std::string> states;
    agent.set_on_local_message_fn([&states](std::string, std::string payload) {
        states.push_back(nlohmann::json::parse(payload)["print"]["gcode_state"].get<std::string>());
    });
    agent.set_scenario({
        {10, "PREPARE", 0, 0, 25, 210, 25, 60},
        {10, "RUNNING", 50, 3, 210, 210, 60, 60},
        {10, "FINISH", 100, 0, 210, 0, 60, 0},
    });

    Slic3r::PrintParams params;
    params.dev_id = "FAKE001";
    int last_status = -1;
    agent.start_local_print(
        params,
        [&last_status](int status, int, std::string) { last_status = status; },
        []() { return true; }); // already-cancelled: scenario must not emit any step
    agent.wait_scenario_done();

    CHECK(last_status == Slic3r::PrintingStageERROR);
    CHECK(states.empty());
}

TEST_CASE("Http digest authentication", "[Http][NotWorking]") {
    Slic3r::Http g = Slic3r::Http::get("https://httpbingo.org/digest-auth/auth/guest/guest");

    g.auth_digest("guest", "guest");

    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });

    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });

    g.perform_sync();

    REQUIRE(status == 200);
}

TEST_CASE("Http basic authentication", "[Http][NotWorking]") {
    Slic3r::Http g = Slic3r::Http::get("https://httpbingo.org/basic-auth/guest/guest");

    g.auth_basic("guest", "guest");

    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });

    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });

    g.perform_sync();

    REQUIRE(status == 200);
}

