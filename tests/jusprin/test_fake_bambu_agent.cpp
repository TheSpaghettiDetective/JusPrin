// Contract tests for FakeBambuAgent: canned Bambu push_status / SSDP shapes
// and the JSON control file that drives them. The agent is linked from
// libslic3r_gui; these cases never construct a wx app.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Testing/FakeBambuAgent.hpp"
#include "slic3r/Utils/bambu_networking.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using nlohmann::json;

TEST_CASE("FakeBambuAgent SSDP announcement uses the A1 mini type code", "[FakeBambuAgent]")
{
    const json j = json::parse(Slic3r::FakeBambuAgent::ssdp_announcement_json());
    CHECK(j["dev_id"] == Slic3r::JUSPRIN_FAKE_BAMBU_DEV_ID);
    CHECK(j["dev_type"] == Slic3r::JUSPRIN_FAKE_BAMBU_PRINTER_TYPE);
    CHECK(j["connect_type"] == "lan");
    CHECK(j["bind_state"] == "free");
    CHECK(j["dev_ip"].get<std::string>().empty() == false);
}

TEST_CASE("FakeBambuAgent control JSON maps named states and progress", "[FakeBambuAgent]")
{
    Slic3r::FakeBambuStatusStep step;
    bool                        offline = false;

    REQUIRE(Slic3r::FakeBambuAgent::apply_control_json(
        R"({"state":"printing","progress":0.4,"nozzle":210,"bed":60,"offline":false})", step, offline));
    CHECK_FALSE(offline);
    CHECK(step.gcode_state == "RUNNING");
    CHECK(step.mc_percent == 40);
    CHECK(step.nozzle_temper == 210.0);
    CHECK(step.bed_temper == 60.0);

    REQUIRE(Slic3r::FakeBambuAgent::apply_control_json(R"({"state":"idle"})", step, offline));
    CHECK(step.gcode_state == "IDLE");

    REQUIRE(Slic3r::FakeBambuAgent::apply_control_json(R"({"state":"printing","offline":true})", step, offline));
    CHECK(offline);

    CHECK_FALSE(Slic3r::FakeBambuAgent::apply_control_json(R"({"state":"not-a-state"})", step, offline));
}

TEST_CASE("FakeBambuAgent replays default scenario end-to-end", "[FakeBambuAgent]")
{
    Slic3r::FakeBambuAgent agent("");

    std::vector<json> received;
    agent.set_on_local_message_fn([&received](std::string, std::string payload) {
        received.push_back(json::parse(payload));
    });

    bool connected = false;
    agent.set_on_local_connect_fn([&connected](int status, std::string, std::string) {
        connected = (status == Slic3r::ConnectStatusOk);
    });

    REQUIRE(agent.connect_printer("FAKE001", "127.0.0.1", "", "", false) == BAMBU_NETWORK_SUCCESS);
    CHECK(connected);
    REQUIRE(received.size() == 1);
    CHECK(received.back()["print"]["gcode_state"] == "IDLE");
    CHECK(received.back()["print"]["command"] == "push_status");
    CHECK(received.back()["print"]["nozzle_diameter"] == "0.4");

    Slic3r::PrintParams params;
    params.dev_id   = "FAKE001";
    params.filename = "/tmp/tape.gcode";
    int status_calls = 0;
    agent.start_local_print(params, [&status_calls](int, int, std::string) { ++status_calls; }, nullptr);
    agent.wait_scenario_done();

    CHECK(agent.last_print_filename() == "/tmp/tape.gcode");
    REQUIRE(received.size() > 1);
    CHECK(received.back()["print"]["gcode_state"] == "FINISH");
    CHECK(received.back()["print"]["mc_percent"] == 100);
    CHECK(status_calls >= 2);

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

TEST_CASE("FakeBambuAgent push_status delivers a custom step immediately", "[FakeBambuAgent]")
{
    Slic3r::FakeBambuAgent agent("");
    std::string            last_payload;
    agent.set_on_local_message_fn([&last_payload](std::string, std::string payload) { last_payload = payload; });

    Slic3r::FakeBambuStatusStep step;
    step.gcode_state = "FAILED";
    step.mc_percent  = 42;
    agent.push_status(step);

    REQUIRE_FALSE(last_payload.empty());
    auto j = json::parse(last_payload);
    CHECK(j["print"]["gcode_state"] == "FAILED");
    CHECK(j["print"]["mc_percent"] == 42);
}

TEST_CASE("FakeBambuAgent cancellation stops the scripted scenario before it emits", "[FakeBambuAgent]")
{
    Slic3r::FakeBambuAgent agent("");
    std::vector<std::string> states;
    agent.set_on_local_message_fn([&states](std::string, std::string payload) {
        states.push_back(json::parse(payload)["print"]["gcode_state"].get<std::string>());
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
        params, [&last_status](int status, int, std::string) { last_status = status; }, []() { return true; });
    agent.wait_scenario_done();

    CHECK(last_status == Slic3r::PrintingStageERROR);
    CHECK(states.empty());
}

TEST_CASE("FakeBambuAgent start_discovery announces an N1 device", "[FakeBambuAgent]")
{
    Slic3r::FakeBambuAgent agent("");
    std::string            ssdp;
    agent.set_on_ssdp_msg_fn([&ssdp](std::string payload) { ssdp = payload; });
    REQUIRE(agent.start_discovery(true, false));
    REQUIRE_FALSE(ssdp.empty());
    CHECK(json::parse(ssdp)["dev_type"] == "N1");
}

TEST_CASE("FakeBambuAgent replies to info.get_version", "[FakeBambuAgent]")
{
    Slic3r::FakeBambuAgent agent("");
    std::string            last_payload;
    agent.set_on_local_message_fn([&last_payload](std::string, std::string payload) { last_payload = payload; });

    json request;
    request["info"]["command"] = "get_version";
    REQUIRE(agent.send_message_to_printer("FAKE001", request.dump(), 0, 0) == BAMBU_NETWORK_SUCCESS);

    auto j = json::parse(last_payload);
    CHECK(j["info"]["command"] == "get_version");
    CHECK(j["info"]["result"] == "success");
    REQUIRE(j["info"]["module"].is_array());
    CHECK(j["info"]["module"][0]["name"] == "ota");
}

TEST_CASE("FakeBambuAgent reloads control file from the data directory", "[FakeBambuAgent]")
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("jusprin-fake-bambu-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir / "jusprin");
    {
        std::ofstream out((dir / "jusprin" / "fake_printer.json").string());
        out << R"({"state":"printing","progress":0.75,"nozzle":220,"bed":65})";
    }

    Slic3r::FakeBambuAgent agent(dir.string());
    REQUIRE(agent.reload_control_file());
    CHECK(agent.current_step().gcode_state == "RUNNING");
    CHECK(agent.current_step().mc_percent == 75);
    CHECK(agent.current_step().nozzle_temper == 220.0);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
