// Contract tests for the Agent bridge host against the fake workspace: the
// versioned handshake, capability report, envelope identity, duplicate and
// stale handling, deterministic conversation streaming, reload
// reconstruction, and workspace context propagation including project-session
// invalidation. GUI-free.

// Keep the protocol first: it must compile without incidental standard-library includes.
#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/AgentHost.hpp"
#include "libslic3r/Exception.hpp"
#include "../jusprin_support/DeterministicMockAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentSetup.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderFactory.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProviderProfile.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolResults.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpConnections.hpp"
#include "../jusprin_support/FakeWorkspace.hpp"
#include "../jusprin_support/RequestLog.hpp"
#include "kept_turn.hpp"
#include "mcp_test_directory.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

Workspace::WorkspaceSnapshot two_object_snapshot()
{
    Workspace::WorkspaceSnapshot snapshot;
    snapshot.setup.project_name   = "Fixture Project";
    snapshot.setup.printer_preset = "Test Printer 0.4 nozzle";
    snapshot.setup.filament_preset = "Generic PLA";

    Workspace::WorkspacePlate plate;
    plate.id     = Workspace::PlateId(Workspace::ProjectSessionId(1), 11);
    plate.name   = "Plate 1";
    plate.active = true;

    Workspace::WorkspaceObject cube_a;
    cube_a.id   = Workspace::ObjectId(Workspace::ProjectSessionId(1), 21);
    cube_a.name = "cube-a";
    cube_a.instances.push_back({});
    Workspace::WorkspaceObject cube_b;
    cube_b.id   = Workspace::ObjectId(Workspace::ProjectSessionId(1), 22);
    cube_b.name = "cube-b";
    cube_b.instances.push_back({});
    plate.objects = {cube_a, cube_b};

    snapshot.plates       = {plate};
    snapshot.active_plate = plate.id;
    return snapshot;
}

ProjectPersistence::Config test_persistence_config()
{
    ProjectPersistence::Config config;
    config.clock = []() { return "2026-08-30T00:00:00Z"; };
    config.uuid  = []() {
        static int next_uuid = 0;
        return "test-" + std::to_string(++next_uuid);
    };
    return config;
}

// What the Agent page sends with project_instructions; its real text is in
// AgentUI/src/projectInstructions.ts.
constexpr const char* kTestProjectInstructions = "You are the project assistant under test.";
// What the page sends with skills; the real ones are AgentUI/src/skills/*/SKILL.md.
constexpr const char* kTestFileLoadedSkill = "Explain this file to the person.";
// The file-loaded skill as the host sends it when no purpose is recorded: the
// page's text, then a sentence of the host's own about the purpose. The
// sentence's words live in AgentHost.cpp, so tests check only that it follows.
bool file_loaded_text_adds_purpose_sentence(const std::string& text)
{
    const std::string skill = kTestFileLoadedSkill;
    return text.size() > skill.size() && text.rfind(skill, 0) == 0;
}

// What the page sends for a slice that did not succeed, and for what
// OrcaSlicer reported about the print. The page sends skills sorted by name.
constexpr const char* kTestSliceSkill = "Explain why the slice did not succeed.";
constexpr const char* kTestPrintIssuesSkill = "Read what OrcaSlicer reported about the print.";

json test_skills(const std::string& file_loaded_text = kTestFileLoadedSkill)
{
    return json{{"skills", json::array({json{{"name", "file-loaded"}, {"trigger", "file_loaded"},
                                             {"text", file_loaded_text}},
                                        json{{"name", "print-issues"}, {"trigger", "print_issues"},
                                             {"text", kTestPrintIssuesSkill}},
                                        json{{"name", "slice-failed"}, {"trigger", "slice_failed"}, {"text", kTestSliceSkill}}})}};
}

struct Harness
{
    Workspace::FakeWorkspace workspace;
    ProjectPersistence       persistence;
    AgentHost                host;
    std::vector<json>        sent;

    explicit Harness(AgentAvailability availability = AgentAvailability::Ready)
        : workspace(two_object_snapshot()), persistence(workspace, test_persistence_config()),
          host(workspace, persistence, availability, false, std::make_unique<DeterministicMockAgent>())
    {
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
        persistence.attach();
    }

    explicit Harness(Workspace::WorkspaceSnapshot initial)
        : workspace(std::move(initial)), persistence(workspace, test_persistence_config()),
          host(workspace, persistence, AgentAvailability::Ready, false, std::make_unique<DeterministicMockAgent>())
    {
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
        persistence.attach();
    }

    explicit Harness(AgentServicePtr agent, ProjectPersistence::Config config = test_persistence_config())
        : workspace(two_object_snapshot()), persistence(workspace, std::move(config)),
          host(workspace, persistence, AgentAvailability::Ready, false, std::move(agent))
    {
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
        persistence.attach();
    }

    // An unconfigured dock: no Agent service, but a setup service to reach one.
    explicit Harness(AgentSetupServicePtr setup)
        : workspace(two_object_snapshot()), persistence(workspace, test_persistence_config()),
          host(workspace, persistence, AgentAvailability::Unavailable, false, AgentServicePtr{}, std::move(setup))
    {
        host.set_send([this](const std::string& envelope) { sent.push_back(json::parse(envelope)); });
        persistence.attach();
    }

    json page_envelope(const std::string& type, json payload, int version = Protocol::kVersion)
    {
        static int next_id = 1;
        return json{{"protocol", Protocol::kName},
                    {"version", version},
                    {"id", "w-" + std::to_string(next_id++)},
                    {"type", type},
                    {"payload", std::move(payload)}};
    }

    void deliver(const std::string& type, json payload = json::object(), int version = Protocol::kVersion)
    {
        host.on_page_message(page_envelope(type, std::move(payload), version).dump());
    }

    // As the project page does on every connection: the hello, then the
    // project assistant's instructions and its skills, without which no
    // project turn starts.
    void handshake(bool with_instructions = true, bool with_skills = true)
    {
        deliver("hello", json{{"protocolVersions", json::array({Protocol::kVersion})},
                              {"capabilities", json::array({"streaming"})}});
        if (with_instructions)
            deliver("project_instructions", json{{"text", kTestProjectInstructions}});
        if (with_skills)
            deliver("skills", test_skills());
    }

    std::vector<json> of_type(const std::string& type) const
    {
        std::vector<json> result;
        for (const json& envelope : sent)
            if (envelope["type"] == type)
                result.push_back(envelope);
        return result;
    }

    const json* last_of_type(const std::string& type) const
    {
        const json* found = nullptr;
        for (const json& envelope : sent)
            if (envelope["type"] == type)
                found = &envelope;
        return found;
    }

    void pump_all(int limit = 1000)
    {
        while (host.stream_active() && limit-- > 0)
            host.pump_stream();
    }

    std::string send_user_message(const std::string& text, const std::string& client_id)
    {
        deliver("user_message", json{{"clientMessageId", client_id}, {"text", text}});
        const json* added = last_of_type("message_added");
        REQUIRE(added != nullptr);
        return (*added)["payload"]["message"]["id"].get<std::string>();
    }
};

} // namespace

namespace {

class ToolCallingAgent final : public IAgentService
{
public:
    bool ready() const override { return true; }
    bool busy() const override { return active; }
    bool start(const AgentRequest& request) override
    {
        last_request = request;
        requests.push_back(request);
        if (request.purpose == AgentRequest::Purpose::ConversationTitle) {
            events.push_back(AgentEvent::delta("Duplicate selected object"));
            events.push_back(AgentEvent::completed());
            active = true;
            return true;
        }
        ToolRequest tool;
        if (stale_patch) {
            // Refused at submission: the revision is not the workspace's.
            tool.tool           = "settings_apply_patch";
            tool.arguments_json = json{{"scope", "process"}, {"changes", {{"wall_loops", "4"}}},
                                       {"expectedSessionId", std::to_string(request.workspace.session.value())},
                                       {"expectedRevision", request.workspace.revision + 100}}.dump();
        } else {
            tool.tool = "plate_layout";
            tool.arguments_json = json{{"sessionId", std::to_string(request.workspace.session.value())},
                                       {"objects", json::array({json{{"objectId", std::to_string(request.workspace.selected_objects.front().value())},
                                                                     {"quantity", 2}}})}}.dump();
        }
        std::vector<RecordItem> sent = JusPrinTest::kept_person_side(request);
        sent.push_back(JusPrinTest::kept_call("provider-call-1", tool.tool, tool.arguments_json));
        JusPrinTest::keep(request, std::move(sent));
        events.push_back(AgentEvent::delta("I can do that."));
        events.push_back(AgentEvent::tool_call({"provider-call-1", std::move(tool), true}));
        active = true;
        return true;
    }
    bool continue_after_tool(const AgentToolResult& result) override
    {
        continuation = result;
        JusPrinTest::keep(last_request, {JusPrinTest::kept_result(result), JusPrinTest::kept_reply(" The native duplicate succeeded.")});
        events.push_back(AgentEvent::delta(" The native duplicate succeeded."));
        events.push_back(AgentEvent::completed());
        return true;
    }
    void cancel() override { active = false; events.clear(); }
    std::optional<AgentEvent> poll() override
    {
        if (events.empty())
            return std::nullopt;
        AgentEvent event = std::move(events.front());
        events.pop_front();
        if (event.kind == AgentEventKind::Completed || event.kind == AgentEventKind::Failed)
            active = false;
        return event;
    }

    AgentRequest last_request;
    std::vector<AgentRequest> requests;
    std::optional<AgentToolResult> continuation;
    std::deque<AgentEvent> events;
    bool active{false};
    bool stale_patch{false};
};

class RetryingAgent final : public IAgentService
{
public:
    bool ready() const override { return true; }
    bool busy() const override { return active; }
    bool start(const AgentRequest& request) override
    {
        requests.push_back(request);
        active = true;
        if (request.purpose == AgentRequest::Purpose::ConversationTitle) {
            events.push_back(AgentEvent::delta("Provider retry discussion"));
            events.push_back(AgentEvent::completed());
        } else if (request.attempt == 1)
            events.push_back(AgentEvent::failed({"provider_timeout", "The provider timed out.", true}));
        else {
            events.push_back(AgentEvent::delta("Recovered without duplicating the turn."));
            events.push_back(AgentEvent::completed());
        }
        return true;
    }
    bool continue_after_tool(const AgentToolResult&) override { return false; }
    void cancel() override { active = false; events.clear(); }
    std::optional<AgentEvent> poll() override
    {
        if (events.empty())
            return std::nullopt;
        AgentEvent event = std::move(events.front());
        events.pop_front();
        if (event.kind == AgentEventKind::Completed || event.kind == AgentEventKind::Failed)
            active = false;
        return event;
    }

    std::vector<AgentRequest> requests;
    std::deque<AgentEvent> events;
    bool active{false};
};

} // namespace

TEST_CASE("protocol constants agree with the shared protocol.json", "[agent][protocol]")
{
    std::ifstream file(std::string(JUSPRIN_SOURCE_DIR) + "/resources/jusprin/agent/protocol.json");
    REQUIRE(file.is_open());
    const json shared = json::parse(file);

    CHECK(shared["name"] == Protocol::kName);
    CHECK(shared["version"].get<int>() == Protocol::kVersion);

    std::vector<std::string> shared_capabilities = shared["capabilities"].get<std::vector<std::string>>();
    CHECK(shared_capabilities == Protocol::capabilities());

    const std::set<std::string> page_types(shared["pageMessageTypes"].begin(), shared["pageMessageTypes"].end());
    CHECK(page_types == std::set<std::string>{Protocol::kHello, Protocol::kStateRequest, Protocol::kUserMessage,
                                              Protocol::kStopGeneration, Protocol::kRetryMessage, Protocol::kToolCancel,
                                              Protocol::kWorkCancel, Protocol::kCreateConversation,
                                              Protocol::kSwitchConversation, Protocol::kRestoreConversation,
                                              Protocol::kRenameConversation, Protocol::kDeleteConversation,
                                              Protocol::kDraftUpdate, Protocol::kAttachFile, Protocol::kRemoveAttachment,
                                              Protocol::kSetupCheckKey, Protocol::kSetupCancel, Protocol::kMcpCatalog,
                                              Protocol::kMcpPreview, Protocol::kMcpConnect, Protocol::kRevealPath,
                                              Protocol::kPickAttachment, Protocol::kSetChatModel, Protocol::kPrinterAction, Protocol::kPrinterInstructions,
                                              Protocol::kPrinterOpening, Protocol::kFilamentInstructions,
                                              Protocol::kShellAction, Protocol::kProjectInstructions,
                                              Protocol::kSkills, Protocol::kSettingsCatalogRequest,
                                              Protocol::kSettingsAction, Protocol::kSaveProposal,
                                              Protocol::kUndoReceipt, Protocol::kSetupRemoveKey,
                                              Protocol::kPreferencesAction, Protocol::kAccountSignIn,
                                              Protocol::kAccountCheck, Protocol::kAccountCancel});

    const std::set<std::string> host_types(shared["hostMessageTypes"].begin(), shared["hostMessageTypes"].end());
    CHECK(host_types == std::set<std::string>{Protocol::kHelloAck, Protocol::kHelloReject, Protocol::kState, Protocol::kConversationsUpdated,
                                              Protocol::kContext, Protocol::kAppearance, Protocol::kAgentStatus,
                                              Protocol::kMessageAdded, Protocol::kAssistantStarted, Protocol::kAssistantDelta,
                                              Protocol::kAssistantCompleted, Protocol::kAssistantFailed,
                                              Protocol::kAssistantStopped, Protocol::kToolActivity,
                                              Protocol::kSetupStatus, Protocol::kMcpCatalog, Protocol::kMcpPreview,
                                              Protocol::kMcpStatus, Protocol::kChangeAdded, Protocol::kWorkUpdated, Protocol::kRevealItem,
                                              Protocol::kModelCatalog, Protocol::kChatModel,
                                              Protocol::kBridgeError, Protocol::kAttachmentUpdated,
                                              Protocol::kPrinterSession, Protocol::kSettingsCatalog, Protocol::kConfirm,
                                              Protocol::kSetupKeys, Protocol::kPreferences, Protocol::kAccountStatus});
}

TEST_CASE("settings_catalog_request answers with every setting in force", "[agent][all_settings]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(std::find(Protocol::capabilities().begin(), Protocol::capabilities().end(), "all_settings") !=
            Protocol::capabilities().end());

    Workspace::SettingsCatalog catalog;
    catalog.available    = true;
    catalog.process.name = "0.20mm Standard";
    Workspace::CatalogSetting walls;
    walls.key            = "wall_loops";
    walls.label          = "Wall loops";
    walls.page           = "Strength";
    walls.group          = "Walls";
    walls.value          = "4";
    walls.display        = "4";
    walls.preset         = "2";
    walls.preset_display = "2";
    catalog.process.settings.push_back(walls);
    Workspace::CatalogFilament filament;
    filament.slot     = 1;
    filament.preset   = "Generic PETG";
    filament.material = "PETG";
    filament.colour   = "#FF8000";
    Workspace::CatalogSetting retraction;
    retraction.key               = "filament_retraction_length";
    retraction.label             = "Retraction length";
    retraction.value             = "nil";
    retraction.display           = "Not set";
    retraction.inherited         = true;
    retraction.inherited_display = "0.8 mm";
    filament.settings.push_back(retraction);
    catalog.filaments.push_back(filament);
    Workspace::CatalogPlate plate;
    plate.id                   = Workspace::PlateId(harness.workspace.snapshot().session, 7);
    plate.name                 = "Plate 1";
    plate.active               = true;
    plate.bed_type             = "Textured PEI Plate";
    plate.own_bed_type         = true;
    plate.bed_temp_key         = "textured_plate_temp";
    plate.bed_temp_initial_key = "textured_plate_temp_initial_layer";
    catalog.plates.push_back(plate);
    Workspace::CatalogObject object;
    object.id    = Workspace::ObjectId(harness.workspace.snapshot().session, 3);
    object.name  = "bracket v3";
    object.plate = plate.id;
    object.overrides.push_back({"bracket v3 / Mounting tabs", "modifier", "wall_loops", "6", 0, "Wall loops", "6"});
    catalog.objects.push_back(object);
    harness.workspace.catalog = catalog;

    harness.deliver("settings_catalog_request");
    const json* answer = harness.last_of_type("settings_catalog");
    REQUIRE(answer != nullptr);
    CHECK((*answer)["correlationId"].is_string());
    const json& payload = (*answer)["payload"];
    CHECK(payload["available"] == true);
    CHECK(payload["process"]["name"] == "0.20mm Standard");
    REQUIRE(payload["process"]["settings"].size() == 1);
    const json& row = payload["process"]["settings"][0];
    CHECK(row["key"] == "wall_loops");
    CHECK(row["page"] == "Strength");
    CHECK(row["group"] == "Walls");
    CHECK(row["display"] == "4");
    CHECK(row["presetDisplay"] == "2");
    CHECK(!row.contains("inherited"));
    REQUIRE(payload["filaments"].size() == 1);
    CHECK(payload["filaments"][0]["slot"] == 1);
    CHECK(payload["filaments"][0]["colour"] == "#FF8000");
    const json& inherited = payload["filaments"][0]["settings"][0];
    CHECK(inherited["inherited"] == true);
    CHECK(inherited["inheritedDisplay"] == "0.8 mm");
    CHECK(!inherited.contains("preset"));
    REQUIRE(payload["plates"].size() == 1);
    CHECK(payload["plates"][0]["id"] == "7");
    CHECK(payload["plates"][0]["ownBedType"] == true);
    CHECK(payload["plates"][0]["bedTempKey"] == "textured_plate_temp");
    REQUIRE(payload["objects"].size() == 1);
    CHECK(payload["objects"][0]["id"] == "3");
    CHECK(payload["objects"][0]["plateId"] == "7");
    CHECK(payload["objects"][0]["overrides"][0]["target"] == "bracket v3 / Mounting tabs");
    CHECK(payload["objects"][0]["overrides"][0]["label"] == "Wall loops");

    // A non-FFF printer has no table; the page says so rather than drawing
    // an empty one.
    harness.workspace.catalog = {};
    harness.deliver("settings_catalog_request");
    CHECK((*harness.last_of_type("settings_catalog"))["payload"]["available"] == false);
}

TEST_CASE("settings_action makes the panel's edit as the person's own step", "[agent][all_settings]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(std::find(Protocol::capabilities().begin(), Protocol::capabilities().end(), "settings_actions") !=
            Protocol::capabilities().end());

    const Workspace::WorkspaceSnapshot before = harness.workspace.snapshot();
    const Workspace::ObjectId          id     = before.plates[0].objects[0].id;
    Workspace::CatalogObject object;
    object.id   = id;
    object.name = "bracket v3";
    object.overrides.push_back({"bracket v3 / Mounting tabs", "modifier", "wall_loops", "6", 0, "Wall loops", "6",
                                Workspace::OwnSettingAction::UseProjectValue});
    object.overrides.push_back({"bracket v3 / Screw holes", "support blocker", "", "", 0, "", "",
                                Workspace::OwnSettingAction::Remove});
    object.overrides.push_back({"bracket v3", "support painting", "", "", 0, "", ""});
    harness.workspace.catalog.available = true;
    harness.workspace.catalog.objects.push_back(object);

    // The catalog says which rows the page may offer each action on.
    harness.deliver("settings_catalog_request");
    const json listed = (*harness.last_of_type("settings_catalog"))["payload"]["objects"][0]["overrides"];
    CHECK(listed[0]["action"] == "use_project_value");
    CHECK(listed[1]["action"] == "remove");
    CHECK(!listed[2].contains("action"));

    const std::string session = std::to_string(before.session.value());
    const std::size_t changes = harness.of_type("change_added").size();
    harness.deliver("settings_action", json{{"sessionId", session}, {"objectId", std::to_string(id.value())},
                                            {"action", "use_project_value"}, {"kind", "modifier"},
                                            {"target", "bracket v3 / Mounting tabs"}, {"key", "wall_loops"}, {"value", "6"}});
    REQUIRE(harness.workspace.own_setting_edits.size() == 1);
    const Workspace::OwnSettingEdit& made = harness.workspace.own_setting_edits[0].second;
    CHECK(harness.workspace.own_setting_edits[0].first == id);
    CHECK(made.action == Workspace::OwnSettingAction::UseProjectValue);
    CHECK(made.kind == "modifier");
    CHECK(made.target == "bracket v3 / Mounting tabs");
    CHECK(made.key == "wall_loops");
    CHECK(made.value == "6");
    CHECK(harness.last_of_type("bridge_error") == nullptr);
    // One undo step, in the change log as the person's.
    REQUIRE(harness.of_type("change_added").size() == changes + 1);
    const json change = (*harness.last_of_type("change_added"))["payload"]["change"];
    CHECK(change["kind"] == "step");
    CHECK(change["actor"] == "person");
    CHECK(change["label"] == "Use project value for Wall loops");

    harness.deliver("settings_action", json{{"sessionId", session}, {"objectId", std::to_string(id.value())},
                                            {"action", "remove"}, {"kind", "support blocker"}});
    CHECK(harness.workspace.own_setting_edits.back().second.action == Workspace::OwnSettingAction::Remove);
    CHECK((*harness.last_of_type("change_added"))["payload"]["change"]["label"] == "Remove support blocker");
    CHECK(harness.workspace.catalog.objects[0].overrides.size() == 1);

    SECTION("a refusal comes back as a bridge error, and changes nothing")
    {
        harness.deliver("settings_action", json{{"sessionId", session}, {"objectId", std::to_string(id.value())},
                                                {"action", "remove"}, {"kind", "support blocker"}});
        const json* error = harness.last_of_type("bridge_error");
        REQUIRE(error != nullptr);
        CHECK((*error)["payload"]["code"] == "settings_action_failed");
        CHECK((*error)["payload"]["message"] == "bracket v3 no longer sets that itself");
        CHECK(harness.of_type("change_added").size() == changes + 2);
    }

    SECTION("a payload without an object or a known action is refused before the workspace sees it")
    {
        const std::size_t seen = harness.workspace.own_setting_edits.size();
        harness.deliver("settings_action", json{{"sessionId", session}, {"objectId", std::to_string(id.value())},
                                                {"action", "rename"}, {"kind", "modifier"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "invalid_payload");
        harness.deliver("settings_action", json{{"action", "remove"}, {"kind", "support blocker"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "invalid_payload");
        CHECK(harness.workspace.own_setting_edits.size() == seen);
    }
}

TEST_CASE("handshake negotiates version and reports capabilities, agent status, and state", "[agent][bridge]")
{
    Harness harness;
    REQUIRE_FALSE(harness.host.handshake_complete());

    harness.handshake();
    REQUIRE(harness.host.handshake_complete());

    const json* ack = harness.last_of_type("hello_ack");
    REQUIRE(ack != nullptr);
    CHECK((*ack)["payload"]["version"].get<int>() == Protocol::kVersion);
    CHECK((*ack)["payload"]["capabilities"].get<std::vector<std::string>>() == Protocol::capabilities());
    CHECK((*ack)["payload"]["agent"]["status"] == "ready");

    const json* state = harness.last_of_type("state");
    REQUIRE(state != nullptr);
    CHECK((*state)["payload"]["conversation"].empty());
    CHECK((*state)["payload"]["context"]["projectName"] == "Fixture Project");
    CHECK((*state)["payload"]["context"]["printer"]["preset"] == "Test Printer 0.4 nozzle");
    CHECK((*state)["payload"]["context"]["plates"][0]["objects"].size() == 2);
    // The envelope carries the workspace session identity.
    CHECK((*state)["sessionId"].get<std::string>() == std::to_string(harness.workspace.snapshot().session.value()));
}

TEST_CASE("an unsupported page version is rejected and other traffic requires the handshake", "[agent][bridge]")
{
    Harness harness;

    SECTION("hello with a foreign version list is rejected") {
        harness.deliver("hello", json{{"protocolVersions", json::array({99})}});
        REQUIRE_FALSE(harness.host.handshake_complete());
        const json* reject = harness.last_of_type("hello_reject");
        REQUIRE(reject != nullptr);
        CHECK((*reject)["payload"]["supportedVersions"] == json::array({Protocol::kVersion}));
    }

    SECTION("messages before hello get handshake_required") {
        harness.deliver("state_request");
        const json* error = harness.last_of_type("bridge_error");
        REQUIRE(error != nullptr);
        CHECK((*error)["payload"]["code"] == "handshake_required");
    }

    // The page and this host ship together, so a version the handshake did not
    // negotiate, or an envelope this host cannot read, means the page is broken.
    // Neither is a state a user can act on, so neither is reported to the page:
    // the fault propagates and the app's own handler logs it.
    SECTION("a non-hello envelope with a wrong version is a broken page") {
        harness.handshake();
        const std::size_t before = harness.sent.size();
        CHECK_THROWS_AS(harness.deliver("state_request", json::object(), 99), Slic3r::RuntimeError);
        CHECK(harness.sent.size() == before);
    }

    SECTION("an envelope this host cannot read is a broken page") {
        harness.handshake();
        const std::size_t before = harness.sent.size();
        CHECK_THROWS_AS(harness.host.on_page_message("this is not json"), Slic3r::RuntimeError);
        // Valid JSON, our protocol, but no id for the reply to correlate with.
        CHECK_THROWS_AS(harness.host.on_page_message(
                            json{{"protocol", Protocol::kName}, {"version", Protocol::kVersion},
                                 {"type", "state_request"}}.dump()),
                        Slic3r::RuntimeError);
        CHECK(harness.sent.size() == before);
    }

    SECTION("an unknown message type is still answered") {
        harness.handshake();
        harness.deliver("launch_missiles");
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "unknown_type");
    }

    SECTION("another protocol's envelope is filtered, not treated as a fault") {
        harness.handshake();
        harness.host.on_page_message(json{{"protocol", "someone-else"}, {"version", 1}, {"id", "x"},
                                          {"type", "state_request"}}.dump());
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "wrong_protocol");
    }
}

TEST_CASE("a payload missing a required field is a broken page", "[agent][protocol]")
{
    Harness harness;
    harness.handshake();

    // Each of these fields is one the page is required to send. Missing or
    // wrongly typed, it cannot be the user's doing and there is nothing for
    // them to act on, so the fault propagates instead of being described.
    const std::size_t before = harness.sent.size();
    CHECK_THROWS_AS(harness.deliver("user_message", json{{"clientMessageId", "c-1"}}), Slic3r::RuntimeError);
    CHECK_THROWS_AS(harness.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", 7}}),
                    Slic3r::RuntimeError);
    CHECK_THROWS_AS(harness.deliver("remove_attachment", json::object()), Slic3r::RuntimeError);
    CHECK_THROWS_AS(harness.deliver("rename_conversation", json{{"title", "t"}}), Slic3r::RuntimeError);
    CHECK_THROWS_AS(harness.deliver("reveal_path", json{{"toolId", "not-a-tool"}}), Slic3r::RuntimeError);
    CHECK(harness.sent.size() == before);

    // The message names itself in the diagnosis: "key not found" alone would
    // not say which of the page's messages carried the fault.
    try {
        harness.deliver("user_message", json{{"clientMessageId", "c-1"}});
        FAIL("expected a throw");
    } catch (const Slic3r::RuntimeError& error) {
        const std::string what = error.what();
        CHECK(what.find("user_message") != std::string::npos);
        CHECK(what.find("text") != std::string::npos);
    }
}

TEST_CASE("a user message streams a deterministic reply to completion", "[agent][conversation]")
{
    Harness harness;
    int document_boundaries = 0;
    harness.host.set_turn_boundary_callback([&document_boundaries] { ++document_boundaries; });
    harness.handshake();

    const std::string user_id = harness.send_user_message("what am I printing?", "c-1");
    REQUIRE(harness.host.stream_active());
    const json* started = harness.last_of_type("assistant_started");
    REQUIRE(started != nullptr);
    CHECK((*started)["payload"]["inReplyTo"] == user_id);

    harness.pump_all();

    REQUIRE_FALSE(harness.host.stream_active());
    REQUIRE(harness.last_of_type("assistant_completed") != nullptr);
    CHECK(document_boundaries == 1);

    // Deltas are sequenced from zero without gaps.
    const std::vector<json> deltas = harness.of_type("assistant_delta");
    REQUIRE_FALSE(deltas.empty());
    for (std::size_t i = 0; i < deltas.size(); ++i)
        CHECK(deltas[i]["payload"]["seq"].get<int>() == static_cast<int>(i));

    // The native conversation is authoritative and mentions the fixture.
    REQUIRE(harness.host.conversation().size() == 2);
    const ConversationMessage reply = harness.host.conversation().back();
    CHECK(reply.role == MessageRole::Assistant);
    CHECK(reply.state == MessageState::Complete);
    // Both messages carry the host's stamp, and the page is told it: the
    // thread times a turn from the person's message to the reply.
    CHECK_FALSE(harness.host.conversation().front().created_at.empty());
    CHECK_FALSE(reply.created_at.empty());
    const json* added = harness.last_of_type("message_added");
    REQUIRE(added != nullptr);
    CHECK_FALSE((*added)["payload"]["message"].value("createdAt", "").empty());
    CHECK_THAT(reply.text, Catch::Matchers::ContainsSubstring("Fixture Project"));
    CHECK_THAT(reply.text, Catch::Matchers::ContainsSubstring("cube-a"));

    // Every host envelope has a unique ID.
    std::set<std::string> ids;
    for (const json& envelope : harness.sent)
        ids.insert(envelope["id"].get<std::string>());
    CHECK(ids.size() == harness.sent.size());
}

TEST_CASE("a duplicated clientMessageId cannot create a second message", "[agent][conversation]")
{
    Harness harness;
    harness.handshake();

    harness.send_user_message("hello", "c-dup");
    harness.pump_all();
    const std::size_t message_count = harness.host.conversation().size();

    harness.deliver("user_message", json{{"clientMessageId", "c-dup"}, {"text", "hello"}});
    CHECK(harness.host.conversation().size() == message_count);
    CHECK_FALSE(harness.host.stream_active());
    // The duplicate is still acknowledged for the page's benefit.
    CHECK(harness.of_type("message_added").size() == 2);
}

TEST_CASE("stop ends the stream and the reply is marked stopped", "[agent][conversation]")
{
    Harness harness;
    harness.handshake();

    harness.send_user_message("/slow tell me everything", "c-slow");
    harness.host.pump_stream();
    harness.host.pump_stream();
    REQUIRE(harness.host.stream_active());

    const std::string reply_id = (*harness.last_of_type("assistant_started"))["payload"]["messageId"].get<std::string>();
    harness.deliver("stop_generation", json{{"messageId", reply_id}});

    CHECK_FALSE(harness.host.stream_active());
    REQUIRE(harness.last_of_type("assistant_stopped") != nullptr);
    CHECK(harness.host.conversation().back().state == MessageState::Stopped);

    const std::size_t deltas_after_stop = harness.of_type("assistant_delta").size();
    harness.host.pump_stream();
    CHECK(harness.of_type("assistant_delta").size() == deltas_after_stop);
}

TEST_CASE("failure and retry follow the deterministic scenarios", "[agent][conversation]")
{
    Harness harness;
    harness.handshake();

    SECTION("/fail fails on every attempt") {
        harness.send_user_message("/fail", "c-f");
        harness.pump_all();
        const json* failed = harness.last_of_type("assistant_failed");
        REQUIRE(failed != nullptr);
        CHECK((*failed)["payload"]["error"]["retryable"] == true);
        const std::string reply_id = (*failed)["payload"]["messageId"].get<std::string>();

        harness.deliver("retry_message", json{{"messageId", reply_id}});
        harness.pump_all();
        CHECK(harness.of_type("assistant_failed").size() == 2);
        CHECK(harness.host.conversation().back().attempt == 2);
    }

    SECTION("/flaky succeeds on the retry") {
        harness.send_user_message("/flaky", "c-y");
        harness.pump_all();
        const std::string reply_id = (*harness.last_of_type("assistant_failed"))["payload"]["messageId"].get<std::string>();

        harness.deliver("retry_message", json{{"messageId", reply_id}});
        harness.pump_all();
        REQUIRE(harness.last_of_type("assistant_completed") != nullptr);
        CHECK(harness.host.conversation().back().state == MessageState::Complete);
        CHECK_THAT(harness.host.conversation().back().text, Catch::Matchers::ContainsSubstring("attempt 2"));
    }

    SECTION("retrying a completed message is refused") {
        harness.send_user_message("hello", "c-ok");
        harness.pump_all();
        const std::string reply_id = (*harness.last_of_type("assistant_completed"))["payload"]["messageId"].get<std::string>();
        harness.deliver("retry_message", json{{"messageId", reply_id}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "invalid_retry");
    }
}

TEST_CASE("workspace changes push fresh context to the page", "[agent][context]")
{
    Harness harness;
    harness.handshake();
    const std::size_t context_events_before = harness.of_type("context").size();

    const Workspace::WorkspaceSnapshot before = harness.workspace.snapshot();
    REQUIRE(harness.workspace.select_object(before.plates[0].objects[1].id).succeeded());

    const std::vector<json> contexts = harness.of_type("context");
    REQUIRE(contexts.size() == context_events_before + 1);
    const json& context = contexts.back()["payload"]["context"];
    CHECK(context["selection"]["status"] == "objects");
    CHECK(context["selection"]["objectIds"] == json::array({"22"}));
    // The pushed context carries the committed revision.
    CHECK(context["revision"].get<std::uint64_t>() == harness.workspace.snapshot().revision);

    SECTION("changes made outside the bridge propagate the same way") {
        REQUIRE(harness.workspace.rename_object(before.plates[0].objects[0].id, "renamed-cube").succeeded());
        const std::vector<json> renamed_contexts = harness.of_type("context");
        const json renamed = renamed_contexts.back()["payload"]["context"];
        CHECK(renamed["plates"][0]["objects"][0]["name"] == "renamed-cube");
    }
}

TEST_CASE("project replacement invalidates the session in pushed context", "[agent][context]")
{
    Harness harness;
    harness.handshake();
    const std::string old_session = std::to_string(harness.workspace.snapshot().session.value());

    Workspace::WorkspaceSnapshot replacement;
    replacement.setup.project_name = "Replacement";
    harness.workspace.replace_project(replacement);

    const std::vector<json> contexts = harness.of_type("context");
    const json context = contexts.back()["payload"]["context"];
    CHECK(context["sessionId"].get<std::string>() != old_session);
    CHECK(context["projectName"] == "Replacement");
    CHECK(context["plates"].empty());
}

TEST_CASE("the setup card's facts travel with the context", "[agent][context][setup-card]")
{
    Harness harness;
    harness.handshake();
    const auto plate = harness.workspace.snapshot().plates.front().id;

    SECTION("nothing changed, nothing sliced")
    {
        const json context = harness.of_type("context").empty()
                                 ? (*harness.last_of_type("state"))["payload"]["context"]
                                 : harness.of_type("context").back()["payload"]["context"];
        // Absent, not zeroed: there is no honest estimate before a slice.
        CHECK(context["plates"][0]["estimate"].is_null());
        CHECK(context["presetDeltas"].empty());
        CHECK(context["setupIntent"] == "");
    }

    SECTION("a dirty setting becomes one delta with the value it moved from")
    {
        harness.workspace.set_setting_for_testing("wall_loops", "4");
        const json context = harness.of_type("context").back()["payload"]["context"];
        REQUIRE(context["presetDeltas"].size() == 1);
        CHECK(context["presetDeltas"][0]["key"] == "wall_loops");
        CHECK(context["presetDeltas"][0]["label"] == "Wall loops");
        CHECK(context["presetDeltas"][0]["preset"] == "2");
        CHECK(context["presetDeltas"][0]["value"] == "4");
        CHECK(context["presetDeltas"][0]["presetType"] == "process");
        CHECK(context["presetDeltas"][0]["presetName"] == "Fixture process");
        CHECK(context["presetDeltas"][0]["display"] == "2 \xE2\x86\x92 4");
        CHECK(context["presetDeltas"][0]["page"] == "Fixture page");
        CHECK(context["presetDeltas"][0]["group"] == "Fixture group");
    }

    SECTION("a sliced plate carries time and material, and money only when priced")
    {
        harness.workspace.set_plate_sliced(plate, true);
        Workspace::SliceEstimate estimate;
        estimate.print_time_seconds = 13800;
        estimate.material_grams     = 47.0;
        harness.workspace.set_plate_estimate_for_testing(plate, estimate);

        json context = harness.of_type("context").back()["payload"]["context"];
        CHECK(context["plates"][0]["estimate"]["printTimeSeconds"] == 13800);
        CHECK(context["plates"][0]["estimate"]["materialGrams"] == 47.0);
        // No filament price is the default, and a zero is not a price.
        CHECK(context["plates"][0]["estimate"]["materialCost"].is_null());

        estimate.material_cost = 1.12;
        estimate.has_cost      = true;
        harness.workspace.set_plate_estimate_for_testing(plate, estimate);
        context = harness.of_type("context").back()["payload"]["context"];
        CHECK(context["plates"][0]["estimate"]["materialCost"] == 1.12);
    }

    SECTION("the regional currency travels so a cost can be denominated")
    {
        const json initial = harness.of_type("context").empty()
                                 ? (*harness.last_of_type("state"))["payload"]["context"]
                                 : harness.of_type("context").back()["payload"]["context"];
        CHECK(initial["currency"] == "USD");

        // A machine whose OS names no currency leaves the field empty rather
        // than guessing one; the page then drops the money entirely.
        harness.workspace.set_currency_for_testing("");
        harness.workspace.set_plate_sliced(plate, true);
        CHECK(harness.of_type("context").back()["payload"]["context"]["currency"] == "");
    }

    SECTION("a superseded estimate is kept, and says what it is worth")
    {
        harness.workspace.set_plate_sliced(plate, true);
        Workspace::SliceEstimate estimate;
        estimate.print_time_seconds = 13800;
        estimate.material_grams     = 47.0;
        harness.workspace.set_plate_estimate_for_testing(plate, estimate);
        CHECK(harness.of_type("context").back()["payload"]["context"]["plates"][0]["estimateStatus"] == "current");

        // Recomputing: the number stays so the reader keeps the comparison.
        harness.workspace.set_plate_estimate_for_testing(plate, estimate, Workspace::EstimateStatus::Recomputing);
        harness.workspace.set_plate_sliced(plate, false);
        json context = harness.of_type("context").back()["payload"]["context"];
        CHECK(context["plates"][0]["estimateStatus"] == "recomputing");
        CHECK(context["plates"][0]["estimate"]["printTimeSeconds"] == 13800);

        // Stale: the same number, plus the action that invalidated it.
        harness.workspace.set_plate_estimate_for_testing(plate, estimate, Workspace::EstimateStatus::Stale,
                                                         "you moved the object");
        context = harness.of_type("context").back()["payload"]["context"];
        CHECK(context["plates"][0]["estimateStatus"] == "stale");
        CHECK(context["plates"][0]["invalidatedBy"] == "you moved the object");
        CHECK(context["plates"][0]["estimate"]["materialGrams"] == 47.0);
    }

    SECTION("un-slicing takes the estimate away with it")
    {
        harness.workspace.set_plate_sliced(plate, true);
        Workspace::SliceEstimate estimate;
        estimate.print_time_seconds = 600;
        harness.workspace.set_plate_estimate_for_testing(plate, estimate);
        REQUIRE(harness.of_type("context").back()["payload"]["context"]["plates"][0]["estimate"].is_object());

        harness.workspace.set_plate_sliced(plate, false);
        // Nothing said this estimate survives the slice, so it does not.
        CHECK(harness.of_type("context").back()["payload"]["context"]["plates"][0]["estimate"].is_null());
    }
}

TEST_CASE("applied setup facts preserve generic local overrides", "[agent][context][setup-card]")
{
    auto initial = two_object_snapshot();
    Workspace::AppliedSetup applied;
    applied.plate = initial.plates.front().id;
    applied.printable_objects = 2;
    applied.objects = {"cube-a", "cube-b"};
    applied.local_overrides = {{"cube-b / modifier", "modifier", "sparse_infill_density", "40%", 1,
                                "Sparse infill density", "40%"}};
    initial.applied_setup = std::move(applied);

    Harness harness(std::move(initial));
    harness.handshake();
    const json context = (*harness.last_of_type("state"))["payload"]["context"];
    CHECK(context["presetDeltas"].empty());
    CHECK(context["appliedSetup"]["printableObjects"] == 2);
    CHECK(context["appliedSetup"]["localOverrides"][0]["target"] == "cube-b / modifier");
    CHECK(context["appliedSetup"]["objects"] == json::array({"cube-a", "cube-b"}));
    CHECK(context["appliedSetup"]["localOverrides"][0]["object"] == 1);
    CHECK(context["appliedSetup"]["localOverrides"][0]["label"] == "Sparse infill density");
    CHECK(context["appliedSetup"]["localOverrides"][0]["display"] == "40%");
    CHECK_FALSE(context["appliedSetup"].contains("settings"));
}

TEST_CASE("an intent-only update titles its chat without applying settings", "[agent][context][setup-card]")
{
    Harness harness;
    harness.handshake();
    const std::string message_id = harness.send_user_message("make it strong", "c-intent");
    harness.pump_all();
    const auto before = harness.workspace.snapshot();
    const auto& activity = harness.host.tools().submit(
        {"setup_record", json{{"purpose", "A strong bracket"}}.dump()}, message_id);
    const std::string action = activity.action_id;
    REQUIRE(activity.state == ToolState::Running);
    for (int tick = 0; tick < 20 && !tool_state_terminal(harness.host.tools().find(action)->state); ++tick)
        harness.host.pump_tools();
    REQUIRE(harness.host.tools().find(action)->state == ToolState::Succeeded);
    CHECK(harness.workspace.snapshot().revision == before.revision);
    CHECK(harness.of_type("context").back()["payload"]["context"]["setupIntent"] == "A strong bracket");
    CHECK(harness.of_type("context").back()["payload"]["context"]["presetDeltas"].empty());
}

TEST_CASE("setup_record brings the card's title and headline together, in one push", "[agent][context][setup-card]")
{
    Harness harness;
    harness.handshake();
    const std::string chat = harness.persistence.document().active_conversation_id();
    REQUIRE(harness.persistence.document().rename_conversation(chat, "Bracket chat"));
    const std::string message_id = harness.send_user_message("make it strong", "c-plan");
    harness.pump_all();
    const auto run = [&](const json& arguments) {
        const std::string action = harness.host.tools().submit({"setup_record", arguments.dump()}, message_id).action_id;
        for (int tick = 0; tick < 20 && !tool_state_terminal(harness.host.tools().find(action)->state); ++tick)
            harness.host.pump_tools();
        REQUIRE(harness.host.tools().find(action)->state == ToolState::Succeeded);
    };
    CHECK((*harness.last_of_type("state"))["payload"]["context"]["planHeadline"] == "");

    // A purpose recorded before there is a strategy: the title alone.
    run(json{{"purpose", "A strong bracket"}});
    CHECK(harness.of_type("context").back()["payload"]["context"]["setupIntent"] == "A strong bracket");
    CHECK(harness.of_type("context").back()["payload"]["context"]["planHeadline"] == "");

    // A changed goal restates the title with the new plan, and one push
    // brings both to the card: setup_record changes nothing in the
    // workspace, so no other push would.
    const std::size_t pushes = harness.of_type("context").size();
    const auto revision = harness.workspace.snapshot().revision;
    run(json{{"purpose", "A load-bearing bracket"}, {"headline", "Four walls and gyroid infill for strength"}});
    CHECK(harness.workspace.snapshot().revision == revision);
    REQUIRE(harness.of_type("context").size() == pushes + 1);
    const json context = harness.of_type("context").back()["payload"]["context"];
    CHECK(context["setupIntent"] == "A load-bearing bracket");
    CHECK(context["planHeadline"] == "Four walls and gyroid infill for strength");
    CHECK(context["planValidity"] == "current");
    CHECK(context["planInvalidatedBy"] == "");
    // The chat's own title is a separate record, left as it was.
    CHECK(harness.persistence.document().conversations().front().title == "Bracket chat");

    // A call with no plan keeps the headline, and the plan stays current.
    const std::size_t pushes_before_kept = harness.of_type("context").size();
    run(json{{"purpose", "A load-bearing bracket"}});
    REQUIRE(harness.of_type("context").size() == pushes_before_kept + 1);
    const json kept = harness.of_type("context").back()["payload"]["context"];
    CHECK(kept["planHeadline"] == "Four walls and gyroid infill for strength");
    CHECK(kept["planValidity"] == "current");
    CHECK(kept["planInvalidatedBy"] == "");

    // A person's edit marks the plan on the card, saying what put it in
    // doubt; no delta says who wrote it.
    harness.workspace.set_setting_for_testing("wall_loops", "4");
    const json edited = harness.of_type("context").back()["payload"]["context"];
    REQUIRE(edited["presetDeltas"].size() == 1);
    CHECK_FALSE(edited["presetDeltas"][0].contains("origin"));
    CHECK(edited["planHeadline"] == "Four walls and gyroid infill for strength");
    CHECK(edited["planValidity"] == "needs_reassessment");
    CHECK_FALSE(edited["planInvalidatedBy"].get<std::string>().empty());

    // Nothing clears the mark but the agent recording the plan again: a
    // purpose alone leaves it, the same headline restated clears it.
    run(json{{"purpose", "A load-bearing bracket"}});
    CHECK(harness.of_type("context").back()["payload"]["context"]["planValidity"] == "needs_reassessment");
    run(json{{"headline", "Four walls and gyroid infill for strength"}});
    const json restated = harness.of_type("context").back()["payload"]["context"];
    CHECK(restated["planValidity"] == "current");
    CHECK(restated["setupIntent"] == "A load-bearing bracket");
}

TEST_CASE("reload reconstructs the page from native state, mid-stream included", "[agent][reload]")
{
    Harness harness;
    harness.handshake();

    harness.send_user_message("/slow walk me through it", "c-r");
    harness.host.pump_stream();
    harness.host.pump_stream();
    REQUIRE(harness.host.stream_active());
    const std::string partial_text = harness.host.conversation().back().text;
    REQUIRE_FALSE(partial_text.empty());

    // The page reloads: handshake is required again, and the paused stream
    // sends nothing while disconnected.
    harness.host.reset_page();
    REQUIRE_FALSE(harness.host.handshake_complete());
    const std::size_t deltas_before = harness.of_type("assistant_delta").size();
    harness.host.pump_stream();
    CHECK(harness.of_type("assistant_delta").size() == deltas_before);

    harness.deliver("state_request");
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "handshake_required");

    harness.handshake();
    const json* state = harness.last_of_type("state");
    REQUIRE(state != nullptr);
    CHECK((*state)["payload"]["conversation"].size() == 2);
    CHECK((*state)["payload"]["conversation"][1]["text"] == partial_text);
    CHECK((*state)["payload"]["streamingMessageId"] == harness.host.conversation().back().id);

    // The stream resumes after reconnection and completes.
    harness.pump_all();
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
}

TEST_CASE("agent availability is a separate, honest state", "[agent][availability]")
{
    Harness harness(AgentAvailability::Unavailable);
    harness.handshake();

    CHECK((*harness.last_of_type("hello_ack"))["payload"]["agent"]["status"] == "unavailable");

    harness.send_user_message("anyone home?", "c-u");
    const json* failed = harness.last_of_type("assistant_failed");
    REQUIRE(failed != nullptr);
    CHECK((*failed)["payload"]["error"]["code"] == "agent_unavailable");

    harness.host.set_availability(AgentAvailability::Ready);
    CHECK((*harness.last_of_type("agent_status"))["payload"]["status"] == "ready");
}

namespace {

// Answers every turn with one line, keeping each request it was given, and
// keeps what it sent in the chat's record.
class RecordingAgent final : public IAgentService
{
public:
    bool ready() const override { return true; }
    bool busy() const override { return active; }
    bool start(const AgentRequest& request) override
    {
        requests.push_back(request);
        const bool title = request.purpose == AgentRequest::Purpose::ConversationTitle;
        const std::string reply = title ? "A part" : "This part came from PrusaSlicer.";
        if (!title) {
            std::vector<RecordItem> sent = JusPrinTest::kept_person_side(request);
            sent.push_back(JusPrinTest::kept_reply(reply));
            JusPrinTest::keep(request, std::move(sent));
        }
        events.push_back(AgentEvent::delta(reply));
        events.push_back(AgentEvent::completed());
        active = true;
        return true;
    }
    bool continue_after_tool(const AgentToolResult&) override { return false; }
    void cancel() override { active = false; events.clear(); }
    std::optional<AgentEvent> poll() override
    {
        if (events.empty())
            return std::nullopt;
        AgentEvent event = std::move(events.front());
        events.pop_front();
        if (event.kind == AgentEventKind::Completed || event.kind == AgentEventKind::Failed)
            active = false;
        return event;
    }

    std::vector<AgentRequest> requests;
    std::deque<AgentEvent>    events;
    bool                      active{false};
};

// A Printables-style open: Orca said the file's settings don't carry over.
Workspace::LoadReport prusa_open(bool with_message = true)
{
    Workspace::LoadReport report;
    report.project_opened = true;
    Workspace::FileLoadRecord load;
    load.files         = {"C:/models/bracket.3mf"};
    load.with_settings = true;
    if (with_message)
        load.messages.push_back({"JusPrin - Load 3MF", "The 3MF is not supported by JusPrin, loading geometry data only.",
                                 {"ok"}, "ok", true, "geometry_only"});
    report.loads.push_back(std::move(load));
    report.selected_setup_before = {"Prusa MK4", "PLA", "Standard", "installed_system", "installed_system", "installed_system"};
    report.selected_setup_after = report.selected_setup_before;
    report.objects.emplace_back();
    report.objects.back().name = "bracket";
    report.details.designer = "Jane";
    return report;
}

std::size_t file_report_notes(const Harness& harness)
{
    std::size_t count = 0;
    for (const json& added : harness.of_type("message_added"))
        if (added["payload"]["message"].contains("fileReport"))
            ++count;
    return count;
}

} // namespace

TEST_CASE("a file the person opened becomes the Agent's first turn", "[agent][file-report]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();

    harness.host.on_file_loaded(prusa_open());

    const json* added = harness.last_of_type("message_added");
    REQUIRE(added != nullptr);
    const json& note = (*added)["payload"]["message"];
    CHECK(note["role"] == "note");
    CHECK(note["fileReportCard"] == false);
    CHECK(note["fileReport"]["startedBy"] == "user");
    CHECK(note["fileReport"]["messages"]["items"][0]["title"] == "JusPrin - Load 3MF");
    CHECK(note["fileReport"]["details"]["designer"]["value"] == "Jane");
    // The Agent speaks first. App policy and the file's source data have
    // distinct roles, and the report is the input to this specific turn.
    REQUIRE(harness.last_of_type("assistant_started") != nullptr);
    CHECK((*harness.last_of_type("assistant_started"))["payload"]["inReplyTo"] == note["id"]);
    REQUIRE_FALSE(agent->requests.empty());
    const AgentRequest request = agent->requests.back();
    // The page's skill for a loaded file, fired on this request with the
    // plate's picture and, since nothing says what the print is for, the
    // host's sentence about it. It is not history.
    REQUIRE(request.skills.size() == 1);
    CHECK(request.skills.front().name == "file-loaded");
    CHECK(file_loaded_text_adds_purpose_sentence(request.skills.front().text));
    REQUIRE(request.skills.front().picture != nullptr);
    CHECK(request.skills.front().picture->mime_type == "image/png");
    CHECK_FALSE(request.skills.front().picture->base64.empty());
    // The report is the person's side of this turn, from the note.
    CHECK(request.source == note["id"]);
    CHECK(request.unsent.empty());
    CHECK(request.user_text.find("loading geometry data only") != std::string::npos);
    CHECK(request.user_text.find("\"settingsOutcome\":\"geometry_only\"") != std::string::npos);
    harness.pump_all();
    CHECK(harness.last_of_type("assistant_completed") != nullptr);

    harness.send_user_message("What did the file recommend?", "c-followup");
    REQUIRE(agent->requests.size() >= 2);
    const auto& followup = agent->requests.back();
    CHECK(followup.user_text == "What did the file recommend?");
    // The report went once, as the turn's own words, and the record has it:
    // the follow-up sends it again from there and not as a message of its own.
    REQUIRE_FALSE(followup.record.items.empty());
    const RecordItem& reported = followup.record.items.front();
    CHECK(reported.kind == RecordItem::Kind::User);
    CHECK(reported.source == note["id"]);
    REQUIRE(reported.parts.size() == 1);
    CHECK(reported.parts.front().text == request.user_text);
    CHECK(followup.unsent.empty());
    CHECK(followup.skills.empty());
}

TEST_CASE("the opening report retains every raw dialog without code classification", "[agent][file-report]")
{
    Workspace::LoadReport report = prusa_open(/*with_message=*/false);
    report.ui_language = "de_DE";
    auto& messages = report.loads.front().messages;
    messages.push_back({"Compatibility", "A value was replaced", {"ok"}, "ok", true});
    messages.push_back({"Customized Preset", "Check embedded preset G-code", {"ok"}, "ok", true});
    messages.push_back({"Modified G-code", "Startup G-code differs", {"ok"}, "ok", true});
    messages.push_back({"Other", "A future loader notice", {"ok"}, "ok", true});
    Workspace::OrcaMessage warning;
    warning.text = "Invalid values in the file";
    warning.source = "notification";
    messages.push_back(std::move(warning));

    const json full = load_report_result(report);
    REQUIRE(full["messages"]["items"].size() == 5);
    CHECK(full["uiLanguage"] == "de_DE");
    CHECK_FALSE(full["loadFacts"].contains("noticeInterpretations"));
    CHECK(full["messages"]["items"][0]["text"] == "A value was replaced");
    CHECK(full["messages"]["items"][3]["text"] == "A future loader notice");
    CHECK(full["messages"]["items"][4]["source"] == "notification");
    CHECK(full["messages"]["items"][4]["answer"] == "");
    CHECK_FALSE(full["messages"]["items"][0].contains("kind"));

    report.objects.clear();
    CHECK(load_report_result(report)["loadFacts"]["settingsOutcome"] == "not_applied");
}

TEST_CASE("the opening report carries the app's own fit, mesh and file facts", "[agent][file-report]")
{
    Workspace::LoadReport report = prusa_open(/*with_message=*/false);
    // Orca pushes the plate-boundary error more than once in one load.
    Workspace::OrcaMessage boundary;
    boundary.text   = "Following objects are laid over the boundary of plate";
    boundary.source = "notification";
    report.loads.front().messages = {boundary, boundary, boundary};
    report.objects.back().id = Workspace::ObjectId(Workspace::ProjectSessionId(1), 507);
    report.setup_printers = {{"Bambu Lab A1", "Bambu Lab A1", 0.4, {256, 256, 256}},
                             {"Bambu Lab A1 mini 2", "Bambu Lab A1 mini", 0.4, {180, 180, 180}}};
    report.selected_bed = {180, 180, 180};
    Workspace::LoadedObjectFacts facts;
    facts.id                  = Workspace::ObjectId(Workspace::ProjectSessionId(1), 507);
    facts.placed_on_bed       = false;
    facts.fits_selected_bed   = false;
    facts.fits_setup_printers = {"Bambu Lab A1"};
    facts.triangles           = 12422;
    facts.shells              = 1;
    facts.customizations      = {"colourPainting"};
    report.object_facts.push_back(facts);
    Workspace::LoadedFileFacts file;
    file.file          = "bracket.3mf";
    file.application   = "BambuStudio-01.10.01.50";
    file.printer_model = "Bambu Lab P1S";
    file.nozzle_mm     = 0.4;
    file.filaments     = {{"PLA", "#FD8008"}};
    report.file_facts.push_back(file);

    const json full = load_report_result(report);
    REQUIRE(full["messages"]["items"].size() == 1);
    CHECK(full["setupPrinters"][0]["bedMm"] == json::array({256, 256, 256}));
    CHECK(full["selectedPrinterBedMm"] == json::array({180, 180, 180}));
    const json& row = full["objects"]["items"][0];
    CHECK(row["placedOnBed"] == false);
    CHECK(row["fitsSelectedBed"] == false);
    CHECK(row["fitsSetupPrinters"] == json::array({"Bambu Lab A1"}));
    CHECK(row["mesh"] == json{{"triangles", 12422}, {"shells", 1}, {"openEdges", 0}, {"repairedErrors", 0}});
    CHECK(row["customized"] == json::array({"colourPainting"}));
    REQUIRE(full["fileFacts"].size() == 1);
    CHECK(full["fileFacts"][0]["madeFor"] == json{{"printerModel", "Bambu Lab P1S"}, {"nozzleMm", 0.4}});
    CHECK(full["fileFacts"][0]["filaments"][0]["colour"] == "#FD8008");
    CHECK(full["fileFacts"][0]["application"] == "BambuStudio-01.10.01.50");

    // The registry's contract for an import result accepts it.
    const ToolRegistry&   registry    = ToolRegistry::instance();
    const ToolDefinition* import_tool = registry.find("object_import_file");
    REQUIRE(import_tool != nullptr);
    CHECK(registry.validate_output(*import_tool, json{{"objectIds", json::array({"507"})},
                                                      {"objects", objects_section_result(report.objects)},
                                                      {"report", full}, {"sessionId", "1"}, {"revision", 1}}));
}

TEST_CASE("a reopened project with its conversation is greeted only when Orca spoke", "[agent][file-report]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    harness.send_user_message("hello", "c-1");
    harness.pump_all();
    const std::size_t turns = agent->requests.size();

    harness.host.on_file_loaded(prusa_open(/*with_message=*/false));
    CHECK(file_report_notes(harness) == 0);
    CHECK(agent->requests.size() == turns);

    harness.host.on_file_loaded(prusa_open());
    CHECK(file_report_notes(harness) == 1);
    CHECK(agent->requests.size() > turns);
}

TEST_CASE("a file loaded during another reply keeps its own report turn", "[agent][file-report]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();

    harness.send_user_message("hello", "c-before-load");
    REQUIRE(agent->requests.size() == 1);
    harness.host.on_file_loaded(prusa_open());
    CHECK(agent->requests.size() == 1);
    const std::string note_id = (*harness.last_of_type("message_added"))["payload"]["message"]["id"].get<std::string>();

    harness.pump_all();
    REQUIRE(agent->requests.size() >= 2); // The first reply may also start title generation.
    CHECK(std::any_of(agent->requests.begin(), agent->requests.end(), [](const AgentRequest& request) {
        return request.user_text.find("\"settingsOutcome\":\"geometry_only\"") != std::string::npos;
    }));
    const std::vector<json> started = harness.of_type("assistant_started");
    CHECK(std::any_of(started.begin(), started.end(),
                      [&](const json& event) { return event["payload"]["inReplyTo"] == note_id; }));
}

TEST_CASE("a project request carries the page's instructions, unchanged on every request", "[agent][instructions]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const auto replies = [agent] {
        std::vector<AgentRequest> found;
        for (const AgentRequest& request : agent->requests)
            if (request.purpose == AgentRequest::Purpose::Reply)
                found.push_back(request);
        return found;
    };

    harness.send_user_message("Do I need supports?", "c-first");
    harness.pump_all();
    // A reload of the page sends the same words again.
    harness.deliver("project_instructions", json{{"text", kTestProjectInstructions}});
    harness.send_user_message("And a brim?", "c-second");
    harness.pump_all();

    // Neither empty nor too long replaces what the page sent before.
    const std::size_t errors = harness.of_type("bridge_error").size();
    harness.deliver("project_instructions", json{{"text", ""}});
    REQUIRE(harness.of_type("bridge_error").size() == errors + 1);
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "invalid_payload");
    harness.deliver("project_instructions", json{{"text", std::string(32 * 1024 + 1, 'x')}});
    CHECK(harness.of_type("bridge_error").size() == errors + 2);
    harness.send_user_message("Thanks.", "c-third");
    harness.pump_all();

    // The provider's prompt cache depends on every request of a conversation
    // carrying byte-identical instructions.
    REQUIRE(replies().size() == 3);
    for (const AgentRequest& request : replies())
        CHECK(request.session.instructions == kTestProjectInstructions);
}

TEST_CASE("a turn the app starts before the page has sent the project instructions waits for them",
          "[agent][instructions][file-report]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    // The page has connected and sent the file report's words, but not yet
    // the project assistant's, when a file finishes opening.
    harness.handshake(/*with_instructions=*/false);
    harness.host.on_file_loaded(prusa_open());

    REQUIRE(file_report_notes(harness) == 1);
    const std::string note_id = (*harness.last_of_type("message_added"))["payload"]["message"]["id"].get<std::string>();
    // Neither sent with no rules nor dropped: nothing has started or failed.
    CHECK(agent->requests.empty());
    CHECK(harness.last_of_type("assistant_started") == nullptr);
    CHECK(harness.last_of_type("assistant_failed") == nullptr);

    // Ask AI from a print issue, in the same moment, waits behind it.
    CHECK(harness.host.submit_issue_message("c-issue", "Why is this failing?", "{}") != AgentHost::SubmitResult::Refused);
    CHECK(agent->requests.empty());

    harness.deliver("project_instructions", json{{"text", kTestProjectInstructions}});
    REQUIRE(agent->requests.size() == 1);
    CHECK(agent->requests.front().session.instructions == kTestProjectInstructions);
    CHECK(agent->requests.front().user_text.find("\"settingsOutcome\":\"geometry_only\"") != std::string::npos);
    REQUIRE(harness.last_of_type("assistant_started") != nullptr);
    CHECK((*harness.last_of_type("assistant_started"))["payload"]["inReplyTo"] == note_id);

    harness.pump_all();
    std::vector<AgentRequest> replies;
    for (const AgentRequest& request : agent->requests)
        if (request.purpose == AgentRequest::Purpose::Reply)
            replies.push_back(request);
    REQUIRE(replies.size() == 2);
    CHECK(replies[1].user_text.find("Why is this failing?") == 0);
    CHECK(replies[1].session.instructions == kTestProjectInstructions);
    CHECK(harness.last_of_type("assistant_failed") == nullptr);
}

TEST_CASE("a session with tools of its own keeps its instructions over the project's", "[agent][instructions]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    AgentSessionProfile panel;
    panel.instructions = "You are the printer panel.";
    panel.tool_names   = {"printer_list"};
    harness.host.set_session_profile(panel);
    harness.handshake();
    harness.send_user_message("Add my printer", "c-panel");
    REQUIRE_FALSE(agent->requests.empty());
    CHECK(agent->requests.front().session.instructions == "You are the printer panel.");

    // Without its own, such a session's turn fails where the person can retry
    // it; it never borrows the project assistant's.
    harness.pump_all();
    panel.instructions.clear();
    harness.host.set_session_profile(panel);
    harness.send_user_message("Again", "c-panel-empty");
    REQUIRE(harness.last_of_type("assistant_failed") != nullptr);
    CHECK((*harness.last_of_type("assistant_failed"))["payload"]["error"]["code"] == "instructions_missing");
}

TEST_CASE("without the Agent, what Orca said becomes a card", "[agent][file-report]")
{
    Harness harness(AgentAvailability::Unavailable);
    harness.handshake();

    // Nothing said, nothing to show.
    harness.host.on_file_loaded(prusa_open(/*with_message=*/false));
    CHECK(file_report_notes(harness) == 0);

    harness.host.on_file_loaded(prusa_open());
    REQUIRE(file_report_notes(harness) == 1);
    const json& note = (*harness.last_of_type("message_added"))["payload"]["message"];
    CHECK(note["fileReportCard"] == true);
    CHECK(harness.last_of_type("assistant_started") == nullptr);
    // The card survives a reload of the page.
    harness.deliver("state_request");
    bool found = false;
    for (const json& message : (*harness.last_of_type("state"))["payload"]["conversation"])
        found = found || (message.contains("fileReport") && message["fileReportCard"] == true);
    CHECK(found);
}

TEST_CASE("an Agent the page has not told what to say about files shows the card", "[agent][file-report]")
{
    Harness harness{AgentServicePtr(std::make_unique<RecordingAgent>())};
    // The page's skills hold none for a loaded file.
    harness.handshake(/*with_instructions=*/true, /*with_skills=*/false);
    harness.deliver("skills", json{{"skills", json::array({json{{"name", "another-event"}, {"trigger", "another_event"},
                                                                {"text", "Not about files."}}})}});
    harness.host.on_file_loaded(prusa_open());
    REQUIRE(file_report_notes(harness) == 1);
    CHECK((*harness.last_of_type("message_added"))["payload"]["message"]["fileReportCard"] == true);
    CHECK(harness.last_of_type("assistant_started") == nullptr);
}

namespace {

// Every printed copy: the submitted change adds an instance.
std::size_t workspace_object_count(const Workspace::FakeWorkspace& workspace)
{
    std::size_t count = 0;
    for (const Workspace::WorkspacePlate& plate : workspace.snapshot().plates)
        for (const Workspace::WorkspaceObject& object : plate.objects)
            count += object.instances.size();
    return count;
}

// Streams the current reply to completion and returns the pending tool
// activity it submitted.
json submit_duplicate(Harness& harness, const std::string& client_id)
{
    harness.send_user_message("please duplicate the selected object", client_id);
    harness.pump_all();
    REQUIRE(harness.last_of_type("assistant_completed") != nullptr);
    const json* activity_event = harness.last_of_type("tool_activity");
    REQUIRE(activity_event != nullptr);
    return (*activity_event)["payload"]["activity"];
}

void pump_tools_to_completion(Harness& harness, int limit = 1000)
{
    while (harness.host.tools().any_running() && limit-- > 0)
        harness.host.pump_tools();
}

} // namespace

TEST_CASE("cancellation and deterministic failure surface over the bridge", "[agent][tools]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const std::size_t objects_before = workspace_object_count(harness.workspace);

    SECTION("a slow run reports progress and can be cancelled before execution") {
        harness.send_user_message("/toolslow", "c-t2");
        harness.pump_all();
        const std::string action_id =
            (*harness.last_of_type("tool_activity"))["payload"]["activity"]["actionId"].get<std::string>();
        harness.host.pump_tools();
        harness.host.pump_tools();
        const json running = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
        CHECK(running["state"] == "running");
        CHECK(running["progress"]["current"].get<int>() > 0);

        harness.deliver("tool_cancel", json{{"actionId", action_id}});
        CHECK((*harness.last_of_type("tool_activity"))["payload"]["activity"]["state"] == "cancelled");
        pump_tools_to_completion(harness);
        CHECK(workspace_object_count(harness.workspace) == objects_before);
        CHECK_FALSE(harness.workspace.snapshot().can_undo);
    }

    SECTION("/toolfail fails during execution and changes nothing") {
        harness.send_user_message("/toolfail", "c-t3");
        harness.pump_all();
        const std::string action_id =
            (*harness.last_of_type("tool_activity"))["payload"]["activity"]["actionId"].get<std::string>();
        pump_tools_to_completion(harness);
        const json failed = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
        CHECK(failed["state"] == "failed");
        CHECK(failed["error"]["code"] == "not_found");
        CHECK(workspace_object_count(harness.workspace) == objects_before);
    }

    SECTION("a native change before execution marks the action stale") {
        submit_duplicate(harness, "c-t4");
        REQUIRE(harness.workspace.rename_object(harness.workspace.snapshot().plates[0].objects[1].id, "renamed").succeeded());
        pump_tools_to_completion(harness);
        const json stale = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
        CHECK(stale["state"] == "failed");
        CHECK(stale["error"]["code"] == "stale_workspace");
        CHECK(stale["error"]["details"]["currentRevision"] > stale["error"]["details"]["expectedRevision"]);
        CHECK(workspace_object_count(harness.workspace) == objects_before);
    }
}

TEST_CASE("tool activities reconstruct after a reload and pause while disconnected", "[agent][tools][reload]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const std::size_t objects_before = workspace_object_count(harness.workspace);

    const json submitted = submit_duplicate(harness, "c-t5");
    const std::string action_id = submitted["actionId"].get<std::string>();
    harness.host.pump_tools();
    REQUIRE(harness.host.tools().any_running());

    // The page reloads mid-run: execution pauses while disconnected.
    harness.host.reset_page();
    for (int i = 0; i < 10; ++i)
        harness.host.pump_tools();
    CHECK(harness.host.tools().any_running());
    CHECK(workspace_object_count(harness.workspace) == objects_before);

    // The new handshake reconstructs the activity from native state and the
    // run resumes to completion.
    harness.handshake();
    const json* state = harness.last_of_type("state");
    REQUIRE(state != nullptr);
    REQUIRE((*state)["payload"]["toolActivities"].size() == 1);
    CHECK((*state)["payload"]["toolActivities"][0]["actionId"] == action_id);
    CHECK((*state)["payload"]["toolActivities"][0]["state"] == "running");

    pump_tools_to_completion(harness);
    CHECK((*harness.last_of_type("tool_activity"))["payload"]["activity"]["state"] == "succeeded");
    CHECK(workspace_object_count(harness.workspace) == objects_before + 1);
}

TEST_CASE("the call that opened a project is not shown in the project it opened", "[agent][tools][project]")
{
    Harness harness;
    harness.handshake();
    const ToolActivity opening = harness.host.tools().submit({"project_open", R"({"new":true})"}, "m-open");
    REQUIRE(opening.state == ToolState::Running);
    const std::size_t states_before = harness.of_type("state").size();
    pump_tools_to_completion(harness);
    REQUIRE(harness.workspace.opens == 1);
    const std::vector<json> states = harness.of_type("state");
    REQUIRE(states.size() > states_before);
    for (std::size_t index = states_before; index < states.size(); ++index)
        CHECK(states[index]["payload"]["toolActivities"].empty());
}

TEST_CASE("a read-only tool runs over the bridge", "[agent][tools]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());

    harness.send_user_message("/inspect", "c-t6");
    harness.pump_all();
    const json submitted = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
    CHECK(submitted["actionClass"] == "read_only");

    pump_tools_to_completion(harness);
    const json done = (*harness.last_of_type("tool_activity"))["payload"]["activity"];
    CHECK(done["state"] == "succeeded");
    CHECK(done["result"]["selection"]["items"] ==
          json::array({std::to_string(harness.workspace.snapshot().plates[0].objects[0].id.value())}));
    CHECK_FALSE(harness.workspace.snapshot().can_undo);
}

TEST_CASE("conversations are created and switched over the bridge", "[agent][conversations]")
{
    Harness harness;
    harness.handshake();

    // The adopted fresh project starts with one conversation.
    const json* initial = harness.last_of_type("state");
    REQUIRE(initial != nullptr);
    REQUIRE((*initial)["payload"]["conversations"].size() == 1);
    const std::string first = (*initial)["payload"]["activeConversationId"].get<std::string>();

    harness.send_user_message("hello in the first conversation", "c-c1");
    harness.pump_all();

    harness.deliver("create_conversation", json{{"title", "Second"}});
    const json* created = harness.last_of_type("state");
    REQUIRE((*created)["payload"]["conversations"].size() == 2);
    const std::string second = (*created)["payload"]["activeConversationId"].get<std::string>();
    CHECK(second != first);
    CHECK((*created)["payload"]["conversation"].empty());

    harness.send_user_message("hello in the second conversation", "c-c2");
    harness.pump_all();
    CHECK(harness.host.conversation().size() == 2);

    harness.deliver("switch_conversation", json{{"conversationId", first}});
    const json* switched = harness.last_of_type("state");
    CHECK((*switched)["payload"]["activeConversationId"] == second);
    CHECK((*switched)["payload"]["viewedConversationId"] == first);
    CHECK((*switched)["payload"]["conversation"][0]["text"] == "hello in the first conversation");

    harness.send_user_message("should not land in history", "c-old");
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "inactive_conversation");
    CHECK(harness.persistence.document().messages(first).size() == 2);

    SECTION("switching to an unknown conversation is refused") {
        harness.deliver("switch_conversation", json{{"conversationId", "c-999"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "unknown_conversation");
    }

    SECTION("changing conversations is refused while a reply streams") {
        harness.deliver("switch_conversation", json{{"conversationId", second}});
        harness.send_user_message("/slow tell me more", "c-c3");
        REQUIRE(harness.host.stream_active());
        harness.deliver("create_conversation", json::object());
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "busy");
        harness.deliver("switch_conversation", json{{"conversationId", first}});
        CHECK((*harness.last_of_type("state"))["payload"]["viewedConversationId"] == first);
        CHECK((*harness.last_of_type("state"))["payload"]["activeConversationId"] == second);
        harness.pump_all();
    }
}

TEST_CASE("switching chats with matching checkpoints continues without restoration", "[agent][conversations]")
{
    Harness harness;
    std::string current_version = "v-1";
    bool version_exists = true;
    bool save_succeeds = true;
    int restores = 0;
    harness.host.set_chat_checkpoint_callbacks(
        [&] { return current_version; }, [&] { return current_version; },
        [&](const std::string&) { return version_exists; }, [&] { return save_succeeds; },
        [&](const std::string&, const std::string&) { ++restores; return true; });
    harness.handshake();
    auto& document = harness.persistence.document();
    const std::string first = document.active_conversation_id();
    PlanRecord plan;
    plan.headline = "Original plan";
    document.set_plan(plan, harness.persistence.timestamp());
    harness.deliver("create_conversation", json::object());
    const std::string second = document.active_conversation_id();
    REQUIRE(second != first);
    const auto revision = harness.workspace.snapshot().revision;

    SECTION("unchanged chats can be continued in either direction") {
        harness.deliver("switch_conversation", json{{"conversationId", first}});
        CHECK(document.active_conversation_id() == first);
        CHECK(document.viewed_conversation_id() == first);
        CHECK((*harness.last_of_type("state"))["payload"]["chatResume"]["status"] == "active");
        CHECK(document.plan().headline == "Original plan");
        harness.send_user_message("Continue the original chat", "c-matching");
        harness.pump_all();
        REQUIRE_FALSE(document.messages(first).empty());
        CHECK(document.messages(first).front().text == "Continue the original chat");
        CHECK(document.messages(second).empty());
        harness.deliver("switch_conversation", json{{"conversationId", second}});
        CHECK(document.active_conversation_id() == second);
        CHECK(document.plan().headline.empty());
        CHECK(document.plan(first).headline == "Original plan");
    }
    SECTION("a changed project still requires restoration") {
        current_version = "v-2";
        harness.deliver("switch_conversation", json{{"conversationId", first}});
        CHECK(document.active_conversation_id() == second);
        CHECK((*harness.last_of_type("state"))["payload"]["chatResume"]["status"] == "changed");
    }
    SECTION("a missing checkpoint cannot become active") {
        version_exists = false;
        harness.deliver("switch_conversation", json{{"conversationId", first}});
        CHECK(document.active_conversation_id() == second);
        CHECK((*harness.last_of_type("state"))["payload"]["chatResume"]["status"] == "unavailable");
    }
    SECTION("browsing during a reply does not move its authority") {
        harness.send_user_message("/slow still working", "c-busy-switch");
        REQUIRE(harness.host.stream_active());
        harness.deliver("switch_conversation", json{{"conversationId", first}});
        CHECK(document.active_conversation_id() == second);
        CHECK(document.viewed_conversation_id() == first);
        harness.pump_all();
        CHECK(document.messages(first).empty());
    }
    SECTION("failed persistence cannot activate the selected chat") {
        save_succeeds = false;
        harness.deliver("switch_conversation", json{{"conversationId", first}});
        CHECK(document.active_conversation_id() == second);
        REQUIRE(harness.last_of_type("bridge_error") != nullptr);
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "save_failed");
    }
    SECTION("failed activation save rolls back authority and retains each plan") {
        harness.host.set_chat_checkpoint_callbacks(
            [&] { return current_version; }, [&] { return current_version; },
            [](const std::string&) { return true; },
            [&] { return document.active_conversation_id() == second; },
            [&](const std::string&, const std::string&) { ++restores; return true; });
        harness.deliver("switch_conversation", json{{"conversationId", first}});
        CHECK(document.active_conversation_id() == second);
        CHECK(document.plan().headline.empty());
        CHECK(document.plan(first).headline == "Original plan");
        REQUIRE(harness.last_of_type("bridge_error") != nullptr);
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "save_failed");
    }
    CHECK(restores == 0);
    CHECK(harness.workspace.snapshot().revision == revision);
}

TEST_CASE("inactive chat resume checks authority and revision, and each chat keeps its own plan", "[agent][conversations]")
{
    Harness harness;
    std::string current_version = "v-1";
    harness.host.set_chat_checkpoint_callbacks(
        [&] { return current_version; }, [&] { return current_version; },
        [](const std::string& version) { return version == "v-1" || version == "v-2"; },
        [] { return true; },
        [&](const std::string& version, const std::string& chat) {
            harness.persistence.document().set_active_conversation(chat);
            harness.persistence.document().set_viewed_conversation(chat);
            current_version = version;
            return true;
        });
    harness.handshake();
    auto& document = harness.persistence.document();
    const std::string first = document.active_conversation_id();
    REQUIRE(document.set_setup_intent(first, "Quick print"));
    PlanRecord quick;
    quick.headline = "Coarse layers and low infill for a quick fit check";
    document.set_plan(quick, harness.persistence.timestamp());
    REQUIRE(harness.host.checkpoint_active_chat());
    // The checkpoint holds the moment's card, without the chat's own records.
    CHECK_FALSE(document.chat_checkpoint(first)->contains("planning"));
    CHECK_FALSE((*document.chat_checkpoint(first))["summary"].contains("setupIntent"));
    CHECK_FALSE((*document.chat_checkpoint(first))["summary"].contains("planHeadline"));

    harness.deliver("create_conversation", json{{"title", "Strong"}});
    const std::string second = document.active_conversation_id();
    REQUIRE(first != second);
    // A new chat starts from the intent it came from, with no plan, and the
    // model is sent none.
    CHECK(document.setup_intent(second) == "Quick print");
    CHECK(document.plan().headline.empty());
    CHECK((*harness.last_of_type("state"))["payload"]["context"]["planHeadline"] == "");
    current_version = "v-2";
    REQUIRE(document.set_setup_intent(second, "Strong print"));
    PlanRecord strong;
    strong.headline = "Four walls and gyroid infill for strength";
    document.set_plan(strong, harness.persistence.timestamp());

    harness.deliver("switch_conversation", json{{"conversationId", first}});
    const json payload = (*harness.last_of_type("state"))["payload"];
    CHECK(payload["activeConversationId"] == second);
    CHECK(payload["chatResume"]["status"] == "changed");
    // The saved card reads its own chat's title and headline, never the
    // active chat's.
    CHECK(payload["chatResume"]["summary"]["setupIntent"] == "Quick print");
    CHECK(payload["chatResume"]["summary"]["planHeadline"] == "Coarse layers and low infill for a quick fit check");
    CHECK(payload["context"]["setupIntent"] == "Strong print");
    CHECK(payload["context"]["planHeadline"] == "Four walls and gyroid infill for strength");

    harness.deliver("restore_conversation", json{{"conversationId", first},
                                                  {"activeConversationId", second},
                                                  {"docRevision", payload["docRevision"].get<std::uint64_t>() - 1}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "stale_transition");
    CHECK(harness.persistence.document().active_conversation_id() == second);

    const json fresh = (*harness.last_of_type("state"))["payload"];
    harness.deliver("restore_conversation", json{{"conversationId", first},
                                                  {"activeConversationId", second},
                                                  {"docRevision", fresh["docRevision"]}});
    CHECK(harness.persistence.document().active_conversation_id() == first);
    CHECK(harness.persistence.document().viewed_conversation_id() == first);
    CHECK(current_version == "v-1");
    CHECK(harness.persistence.document().chat_checkpoint(second)->at("versionId") == "v-2");
    // Resuming copies nothing: the chat's plan never left it.
    CHECK(document.plan().headline == "Coarse layers and low infill for a quick fit check");
    CHECK(document.plan(second).headline == "Four walls and gyroid infill for strength");
    CHECK((*harness.last_of_type("state"))["payload"]["context"]["planHeadline"] ==
          "Coarse layers and low infill for a quick fit check");
}

TEST_CASE("failed chat restoration keeps project input blocked", "[agent][conversations]")
{
    Harness harness;
    std::string current_version = "v-1";
    harness.host.set_chat_checkpoint_callbacks(
        [&] { return current_version; }, [&] { return current_version; },
        [](const std::string&) { return true; }, [] { return true; },
        [](const std::string&, const std::string&) { return false; });
    harness.handshake();
    REQUIRE(harness.host.checkpoint_active_chat());
    const std::string first = harness.persistence.document().active_conversation_id();
    harness.deliver("create_conversation", json::object());
    const std::string second = harness.persistence.document().active_conversation_id();
    current_version = "v-2";
    harness.deliver("switch_conversation", json{{"conversationId", first}});
    const json state = (*harness.last_of_type("state"))["payload"];
    harness.deliver("restore_conversation", json{{"conversationId", first},
                                                  {"activeConversationId", second},
                                                  {"docRevision", state["docRevision"]}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "restore_failed");
    CHECK((*harness.last_of_type("state"))["payload"]["projectChatBlocked"] == true);
    CHECK(harness.persistence.document().active_conversation_id() == second);
    current_version = "v-1";
    harness.deliver("switch_conversation", json{{"conversationId", first}});
    CHECK(harness.persistence.document().active_conversation_id() == second);
    harness.deliver("switch_conversation", json{{"conversationId", second}});
    harness.deliver("user_message", json{{"clientMessageId", "blocked-send"},
                                         {"text", "try to continue"}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "recovery_required");
}

TEST_CASE("the draft is kept until it is sent", "[agent][draft]")
{
    Harness harness;
    harness.handshake();

    harness.deliver("draft_update", json{{"text", "half-typed thought"}});
    CHECK(harness.persistence.draft() == "half-typed thought");
    CHECK((*harness.last_of_type("state"))["payload"]["docRevision"] ==
          harness.persistence.document().doc_revision());
    harness.deliver("draft_update", json{{"append", "From earlier chat: saved excerpt"}});
    CHECK(harness.persistence.draft() == "half-typed thought\n\nFrom earlier chat: saved excerpt");

    harness.deliver("state_request");
    CHECK((*harness.last_of_type("state"))["payload"]["draft"] ==
          "half-typed thought\n\nFrom earlier chat: saved excerpt");

    const std::string first = harness.persistence.document().active_conversation_id();
    harness.deliver("create_conversation", json::object());
    harness.deliver("switch_conversation", json{{"conversationId", first}});
    harness.deliver("draft_update", json{{"text", "forged historical draft"}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "inactive_conversation");
    CHECK(harness.persistence.draft() == "half-typed thought\n\nFrom earlier chat: saved excerpt");
    harness.deliver("switch_conversation", json{{"conversationId", harness.persistence.document().active_conversation_id()}});

    harness.send_user_message("half-typed thought, finished", "c-d1");
    CHECK(harness.persistence.draft().empty());
    harness.pump_all();
}

TEST_CASE("the change log reaches the page in state, then entry by entry", "[agent][changes]")
{
    Harness harness;
    harness.handshake();
    CHECK((*harness.last_of_type("state"))["payload"]["changes"].empty());

    REQUIRE(harness.workspace.rename_object(harness.workspace.snapshot().plates[0].objects[0].id, "by hand").succeeded());
    REQUIRE(harness.last_of_type("change_added") != nullptr);
    // A copy: the envelope list grows (and may move) with the next delivery.
    const json change = (*harness.last_of_type("change_added"))["payload"]["change"];
    CHECK(change["kind"] == "step");
    CHECK(change["actor"] == "person");
    CHECK(change["conversationId"] == harness.persistence.document().active_conversation_id());

    harness.deliver("state_request");
    REQUIRE((*harness.last_of_type("state"))["payload"]["changes"].size() == 1);
    CHECK((*harness.last_of_type("state"))["payload"]["changes"][0]["seq"] == change["seq"]);
}

TEST_CASE("an Agent edit is logged as the Agent's, after its tool activity", "[agent][changes][tools]")
{
    Harness harness;
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const json submitted = submit_duplicate(harness, "c-change-agent");
    const std::string action_id = submitted["actionId"].get<std::string>();
    pump_tools_to_completion(harness);

    const json* added = harness.last_of_type("change_added");
    REQUIRE(added != nullptr);
    CHECK((*added)["payload"]["change"]["actor"] == "agent");
    CHECK((*added)["payload"]["change"]["afterId"] == action_id);
    CHECK(harness.of_type("change_added").size() == 1);
}

// A Calibrate… row's question (board 3): held until the page is there, sent
// once, and a newer one replaces one still waiting.
TEST_CASE("a confirmation question reaches the page once, after the handshake if it has to wait", "[agent][calibration]")
{
    Harness harness;
    harness.host.ask_confirmation({"calibrate-1", "Calibrate Gray PETG on Prusa MK4?", "One.", "Start", "Cancel"});
    harness.host.ask_confirmation({"calibrate-2", "Calibrate Gray PETG on Prusa MK4?", "Two.", "Start", "Cancel"});
    CHECK(harness.of_type("confirm").empty());

    harness.handshake();
    REQUIRE(harness.of_type("confirm").size() == 1);
    CHECK((*harness.last_of_type("confirm"))["payload"] ==
          json{{"requestId", "calibrate-2"}, {"title", "Calibrate Gray PETG on Prusa MK4?"}, {"body", "Two."},
               {"confirm", "Start"}, {"cancel", "Cancel"}});

    harness.host.ask_confirmation({"calibrate-3", "Calibrate Gray PETG on Prusa MK4?", "Three.", "Start", "Cancel"});
    CHECK(harness.of_type("confirm").size() == 2);
}

TEST_CASE("appearance changes reach a connected page", "[agent][appearance]")
{
    Harness harness;
    harness.handshake();
    CHECK((*harness.last_of_type("hello_ack"))["payload"]["appearance"] == "light");

    harness.host.set_appearance(true);
    REQUIRE(harness.last_of_type("appearance") != nullptr);
    CHECK((*harness.last_of_type("appearance"))["payload"]["appearance"] == "dark");
}

namespace {

// Minimal base64 encoder for building attach_file payloads in tests.
std::string b64(const std::string& in)
{
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int         val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

json attach(Harness& h, const std::string& client_id, const std::string& name, const std::string& bytes,
            const std::string& mime = "", const std::string& source = "picker")
{
    json payload{{"clientAttachmentId", client_id}, {"name", name}, {"source", source}, {"dataBase64", b64(bytes)}};
    if (!mime.empty())
        payload["mime"] = mime;
    h.deliver("attach_file", payload);
    const json* updated = h.last_of_type("attachment_updated");
    REQUIRE(updated != nullptr);
    return (*updated)["payload"]["attachment"];
}

} // namespace

// The composer's + menu asks the host's own file dialog for one kind; the
// host filters it to the extensions it files under that kind and stages what
// was chosen exactly as it stages a file the page read itself.
TEST_CASE("pick_attachment opens the dialog for one kind and stages the chosen files", "[agent][attachments]")
{
    Harness h;
    JusPrinTest::McpDirectory directory;
    const auto photo = directory.root / "bracket.png";
    const auto notes = directory.root / "notes.gcode";
    const auto other = directory.root / "readme.docx";
    { std::ofstream(photo, std::ios::binary) << std::string("\x89PNG\r\n", 6); }
    { std::ofstream(notes) << "G1 X0\n"; }
    { std::ofstream(other) << "not readable"; }

    std::vector<AgentHost::FilePickerRequest> requests;
    std::vector<std::string> answer;
    h.host.set_file_picker([&](const AgentHost::FilePickerRequest& request,
                               std::function<void(std::vector<std::string>)> done) {
        requests.push_back(request);
        done(answer);
    });
    h.handshake();
    // A filter is the classifier's own list for its kind: every extension it
    // offers is filed under that kind when attached.
    const auto files_as = [&h](const std::vector<std::string>& extensions, const std::string& kind) {
        REQUIRE_FALSE(extensions.empty());
        for (const std::string& extension : extensions) {
            INFO(extension);
            CHECK(attach(h, "ca-filter-" + extension, "probe." + extension, "x")["kind"] == kind);
        }
    };

    answer = {photo.u8string()};
    h.deliver("pick_attachment", json{{"kind", "image"}});
    REQUIRE(requests.size() == 1);
    CHECK(requests.back().description == "Photo or image");
    const json* updated = h.last_of_type("attachment_updated");
    REQUIRE(updated != nullptr);
    CHECK((*updated)["payload"]["attachment"]["name"] == "bracket.png");
    CHECK((*updated)["payload"]["attachment"]["kind"] == "image");
    CHECK((*updated)["payload"]["attachment"]["source"] == "picker");
    CHECK((*updated)["payload"]["attachment"]["state"] == "staged");
    files_as(requests.back().extensions, "image");

    // Every chosen file is judged by the attach_file rules, one update each.
    const auto before = h.sent.size();
    answer = {notes.u8string(), other.u8string(), (directory.root / "gone.png").u8string()};
    h.deliver("pick_attachment", json{{"kind", "gcode"}});
    const std::vector<std::string> gcode_extensions = requests.back().extensions;
    std::vector<json> attachments;
    for (std::size_t i = before; i < h.sent.size(); ++i)
        if (h.sent[i]["type"] == "attachment_updated")
            attachments.push_back(h.sent[i]["payload"]["attachment"]);
    REQUIRE(attachments.size() == 3);
    CHECK(attachments[0]["kind"] == "gcode");
    CHECK(attachments[0]["state"] == "staged");
    CHECK(attachments[0]["previewText"] == "G1 X0\n");
    CHECK(attachments[1]["state"] == "error");
    CHECK(attachments[1]["error"]["code"] == "unsupported_type");
    CHECK(attachments[2]["state"] == "error");
    CHECK(attachments[2]["error"]["code"] == "unreadable");
    files_as(gcode_extensions, "gcode");

    // The model filter is the classifier's own list, so nothing it offers is
    // a file the Agent cannot read.
    answer = {};
    h.deliver("pick_attachment", json{{"kind", "model"}});
    for (const char* extension : {"stl", "3mf", "step", "obj"})
        CHECK(std::count(requests.back().extensions.begin(), requests.back().extensions.end(), extension) == 1);

    // Cancelling the dialog sends nothing.
    const auto after_cancel = h.sent.size();
    h.deliver("pick_attachment", json{{"kind", "pdf"}});
    CHECK(h.sent.size() == after_cancel);
    files_as(requests.back().extensions, "pdf");

    // The menu's list is fixed; any other kind can only come from a broken page.
    CHECK_THROWS_AS(h.deliver("pick_attachment", json{{"kind", "anything"}}), Slic3r::RuntimeError);
    CHECK_THROWS_AS(h.deliver("pick_attachment", json::object()), Slic3r::RuntimeError);
}

namespace {

// Keys for some providers, an agent built per choice, and model lists that
// arrive when the test answers them.
struct FakeModelSource final : IChatModelSource
{
    std::vector<std::string>             providers{"openai", "anthropic"};
    std::string                          fallback{"openai"};
    bool                                 refuse_to_build{false};
    // Built agents keep a record of what they sent, as the real ones do.
    bool                                 recording{false};
    std::vector<ChatModelChoice>         built;
    std::map<std::string, ModelsDone>    asked;

    std::vector<std::string> configured_providers() const override { return providers; }
    std::string              default_provider() const override { return fallback; }
    AgentServicePtr make_agent(const ChatModelChoice& choice) override
    {
        if (refuse_to_build)
            return nullptr;
        built.push_back(choice);
        if (recording)
            return std::make_unique<RecordingAgent>();
        return std::make_unique<DeterministicMockAgent>();
    }
    void list_models(const std::string& provider, ModelsDone done) override { asked[provider] = std::move(done); }
    // An account's aliases and the format each is served in now.
    std::map<std::string, std::string> formats;
    std::string model_format(const std::string& provider, const std::string& model) const override
    {
        const auto found = formats.find(model);
        return provider == "jusprin" && found != formats.end() ? found->second : std::string();
    }
};

} // namespace

// JusPrin's gateway serves an alias in the format the account is assigned,
// and may assign the person another backend at any time. A chat stays on the
// format its first request went out in; chats started later take the new one.
TEST_CASE("a JusPrin chat keeps the format of its first request", "[agent][models][account]")
{
    Harness h;
    auto source       = std::make_shared<FakeModelSource>();
    source->providers = {"jusprin"};
    source->fallback  = "jusprin";
    source->formats["slicer-1"] = "anthropic";
    h.host.set_chat_model_source(source);
    h.handshake();
    const std::string first = h.persistence.document().active_conversation_id();

    // The handed agent is the default choice's, served as Anthropic; the first
    // request fixes the chat to that.
    h.send_user_message("first", "c-1");
    h.pump_all();
    CHECK(source->built.empty());
    CHECK(h.persistence.document().chat_model(first) == ChatModelChoice{"jusprin", "slicer-1", "", "anthropic"});

    // The account is moved to the Responses backend.
    source->formats["slicer-1"] = "responses";
    h.send_user_message("second", "c-2");
    h.pump_all();
    CHECK(source->built.empty());

    // A thinking level is a change within the same format.
    h.deliver("set_chat_model", json{{"provider", "jusprin"}, {"model", "slicer-1"}, {"effort", "thorough"}});
    CHECK(h.persistence.document().chat_model(first) == ChatModelChoice{"jusprin", "slicer-1", "thorough", "anthropic"});
    h.send_user_message("third", "c-3");
    h.pump_all();
    REQUIRE(source->built.size() == 1);
    CHECK(source->built.back() == ChatModelChoice{"jusprin", "slicer-1", "thorough", "anthropic"});

    // A new chat goes by the assignment it starts under.
    h.deliver("create_conversation", json{{"title", "Second"}});
    const std::string second = h.persistence.document().active_conversation_id();
    REQUIRE(second != first);
    h.send_user_message("fourth", "c-4");
    h.pump_all();
    REQUIRE(source->built.size() == 2);
    CHECK(source->built.back() == ChatModelChoice{"jusprin", "slicer-1", "", "responses"});
    CHECK(h.persistence.document().chat_model(second) == ChatModelChoice{"jusprin", "slicer-1", "", "responses"});
    // The first chat keeps its own.
    CHECK(h.persistence.document().chat_model(first) == ChatModelChoice{"jusprin", "slicer-1", "thorough", "anthropic"});
}

// Before the account's model list has named any format there is nothing to
// fix a chat to: the turn fails visibly and the list is asked for again.
TEST_CASE("a JusPrin chat waits for the account's model list", "[agent][models][account]")
{
    Harness h;
    auto source       = std::make_shared<FakeModelSource>();
    source->providers = {"jusprin"};
    source->fallback  = "jusprin";
    h.host.set_chat_model_source(source);
    h.handshake();
    // The first list failed, so it can be asked for again.
    REQUIRE(source->asked.count("jusprin") == 1);
    source->asked["jusprin"](false, {});
    source->asked.clear();

    h.send_user_message("hello", "c-1");
    h.pump_all();
    const json* failed = h.last_of_type("assistant_failed");
    REQUIRE(failed != nullptr);
    CHECK((*failed)["payload"]["error"]["code"] == "account_models_unknown");
    CHECK((*failed)["payload"]["error"]["retryable"] == true);
    CHECK(source->asked.count("jusprin") == 1);
    const std::string chat = h.persistence.document().active_conversation_id();
    CHECK_FALSE(h.persistence.document().chat_model(chat).has_value());
}

// The composer's pickers: each chat picks its provider, model and thinking
// level, switchable between turns, and every turn runs on an agent built for
// the chat's current choice. (A chat that has sent something switches within
// its wire format only, which the record tests below cover; the agents here
// keep no record.)
TEST_CASE("a chat's model choice decides the agent each turn runs on", "[agent][models]")
{
    Harness h;
    auto source = std::make_shared<FakeModelSource>();
    h.host.set_chat_model_source(source);
    h.handshake();

    const json* ack = h.last_of_type("hello_ack");
    REQUIRE(ack != nullptr);
    const auto capabilities = (*ack)["payload"]["capabilities"].get<std::vector<std::string>>();
    CHECK(std::count(capabilities.begin(), capabilities.end(), "model_choice") == 1);

    // A chat with no choice talks to the default provider's default model,
    // the one its row in provider_profiles() names.
    const json* state = h.last_of_type("state");
    REQUIRE(state != nullptr);
    CHECK((*state)["payload"]["chatModel"] == json{{"provider", "openai"}, {"model", openai_profile().model}, {"effort", ""}});

    // Every configured provider's list is asked for once, and the catalog
    // says which are still loading.
    REQUIRE(source->asked.size() == 2);
    const json* catalog = h.last_of_type("model_catalog");
    REQUIRE(catalog != nullptr);
    CHECK((*catalog)["payload"]["defaultProvider"] == "openai");
    REQUIRE((*catalog)["payload"]["providers"].size() == 2);
    CHECK((*catalog)["payload"]["providers"][0]["state"] == "loading");
    CHECK((*catalog)["payload"]["providers"][1]["label"] == profile_for("anthropic")->display_name);

    source->asked["anthropic"](true, {ModelOption{"claude-sonnet-5-5", "Claude Sonnet 5.5", {"low", "medium", "high"}, ""}});
    source->asked["openai"](false, {});
    catalog = h.last_of_type("model_catalog");
    CHECK((*catalog)["payload"]["providers"][0]["state"] == "failed");
    const json& anthropic = (*catalog)["payload"]["providers"][1];
    CHECK(anthropic["state"] == "ready");
    CHECK(anthropic["models"][0] == json{{"id", "claude-sonnet-5-5"}, {"label", "Claude Sonnet 5.5"},
                                         {"efforts", json::array({"low", "medium", "high"})}});

    // The agent the host was handed is the default choice's: a chat that
    // never picked a model keeps using it.
    h.send_user_message("first", "c-1");
    h.pump_all();
    CHECK(source->built.empty());
    CHECK(h.last_of_type("assistant_completed") != nullptr);

    // Switched mid-chat: the choice is the chat's, and the next turn uses it.
    h.deliver("set_chat_model", json{{"provider", "anthropic"}, {"model", "claude-sonnet-5-5"}, {"effort", "high"}});
    const json* chosen = h.last_of_type("chat_model");
    REQUIRE(chosen != nullptr);
    CHECK((*chosen)["payload"]["chatModel"] == json{{"provider", "anthropic"}, {"model", "claude-sonnet-5-5"}, {"effort", "high"}});
    const std::string chat = h.persistence.document().active_conversation_id();
    CHECK(h.persistence.document().chat_model(chat) == ChatModelChoice{"anthropic", "claude-sonnet-5-5", "high"});
    h.send_user_message("second", "c-2");
    h.pump_all();
    REQUIRE(source->built.size() == 1);
    CHECK(source->built.back() == ChatModelChoice{"anthropic", "claude-sonnet-5-5", "high"});
    // The same choice keeps the same agent.
    h.send_user_message("third", "c-3");
    h.pump_all();
    CHECK(source->built.size() == 1);

    // The pickers list configured providers only.
    CHECK_THROWS_AS(h.deliver("set_chat_model", json{{"provider", "openrouter"}, {"model", "x"}}), Slic3r::RuntimeError);

    // A provider whose key is gone leaves its chats on the default.
    source->providers = {"openai"};
    h.deliver("state_request", json::object());
    CHECK((*h.last_of_type("state"))["payload"]["chatModel"]["provider"] == "openai");
}

// What a chat has sent is kept in the wire format it was sent in, and every
// later request sends it again as it is. A model in another format cannot
// read it, so a chat that has talked fails its turn on one, whether the
// person picked it or a removed key left the chat on the default; a new chat
// can take any model.
TEST_CASE("a chat that has talked refuses a model in another wire format", "[agent][models][record]")
{
    Harness h;
    auto source       = std::make_shared<FakeModelSource>();
    source->recording = true;
    h.host.set_chat_model_source(source);
    h.handshake();
    const std::string chat = h.persistence.document().active_conversation_id();
    // Before the chat has sent anything, any model goes.
    h.deliver("set_chat_model", json{{"provider", "anthropic"}, {"model", "claude-sonnet-5-5"}, {"effort", ""}});
    h.deliver("set_chat_model", json{{"provider", "openai"}, {"model", "gpt-5.4"}, {"effort", ""}});
    CHECK(h.last_of_type("bridge_error") == nullptr);
    h.send_user_message("first", "c-1");
    h.pump_all();
    REQUIRE(h.persistence.document().chat_record(chat));
    REQUIRE(h.persistence.document().chat_record(chat)->format == "responses");

    // Another model in the same format, or another thinking level, goes.
    h.deliver("set_chat_model", json{{"provider", "openai"}, {"model", "gpt-5.5"}, {"effort", "high"}});
    CHECK(h.last_of_type("bridge_error") == nullptr);
    CHECK(h.persistence.document().chat_model(chat) == ChatModelChoice{"openai", "gpt-5.5", "high"});

    const std::size_t kept = h.persistence.document().chat_record(chat)->items.size();
    const auto refused = [&] {
        h.send_user_message("second", "c-2");
        const json* failed = h.last_of_type("assistant_failed");
        REQUIRE(failed != nullptr);
        CHECK((*failed)["payload"]["error"]["code"] == "chat_format_fixed");
        CHECK_FALSE(h.host.stream_active());
        // Nothing went to the other format.
        CHECK(h.persistence.document().chat_record(chat)->items.size() == kept);
    };
    SECTION("Anthropic reads another format: the turn says so")
    {
        h.deliver("set_chat_model", json{{"provider", "anthropic"}, {"model", "claude-sonnet-5-5"}, {"effort", ""}});
        refused();
    }
    SECTION("a provider whose key is gone leaves the chat on a default it cannot use")
    {
        source->providers = {"anthropic"};
        source->fallback  = "anthropic";
        refused();
    }
    SECTION("a new chat takes any model")
    {
        h.deliver("create_conversation", json::object());
        h.deliver("set_chat_model", json{{"provider", "anthropic"}, {"model", "claude-sonnet-5-5"}, {"effort", ""}});
        h.send_user_message("first", "c-3");
        CHECK(h.last_of_type("assistant_failed") == nullptr);
        CHECK(source->built.back().provider == "anthropic");
    }
}

TEST_CASE("a turn whose provider cannot be built fails with the reason", "[agent][models]")
{
    Harness h;
    auto source = std::make_shared<FakeModelSource>();
    h.host.set_chat_model_source(source);
    h.handshake();
    h.deliver("set_chat_model", json{{"provider", "anthropic"}, {"model", "claude-opus-5-5"}, {"effort", ""}});
    source->refuse_to_build = true;
    h.send_user_message("hello", "c-1");
    const json* failed = h.last_of_type("assistant_failed");
    REQUIRE(failed != nullptr);
    CHECK((*failed)["payload"]["error"]["code"] == "provider_not_set_up");
    CHECK_FALSE(h.host.stream_active());
}

TEST_CASE("pick_attachment without a dialog says so", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    h.deliver("pick_attachment", json{{"kind", "image"}});
    const json* error = h.last_of_type("bridge_error");
    REQUIRE(error != nullptr);
    CHECK((*error)["payload"]["code"] == "unsupported");
    CHECK(h.last_of_type("attachment_updated") == nullptr);
}

TEST_CASE("a text attachment is staged, decoded, and previewed", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json a = attach(h, "ca-1", "notes.txt", "hello world");
    const std::string id = a["id"].get<std::string>();
    REQUIRE_FALSE(id.empty());
    CHECK(a["kind"] == "text");
    CHECK(a["state"] == "staged");
    CHECK(a["name"] == "notes.txt");
    CHECK(a["previewText"] == "hello world");
    // The blob is on disk under the project's auxiliary directory.
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::path(h.persistence.jusprin_data_dir()) / "attachments" / id / "notes.txt"));
}

// F3 on printer-panel-review-fixes-handoff.md: decoding an arbitrary image
// needs wx, which this GUI-free target does not link (ImageThumbnail.cpp is
// not on agent_bridge_tests' source list). AgentHost::attachment_preview_data_url
// only ever calls whatever AgentHost::ImageThumbnailFn is wired in; these two
// cases cover the seam itself, not the real decoding (that has no GUI-free
// seam either -- ImageThumbnail.cpp has no test target of its own).
TEST_CASE("an image attachment gets no preview without a thumbnail maker wired", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json a = attach(h, "ca-img", "pic.png", std::string("\x89PNG\r\n", 6), "image/png");
    CHECK(a["kind"] == "image");
    CHECK(a["state"] == "staged");
    CHECK_FALSE(a.contains("previewDataUrl"));
    // The large data URL is not persisted in state.json; it is rebuilt on demand.
    std::ifstream     in(h.persistence.state_file_path());
    std::stringstream ss;
    ss << in.rdbuf();
    const json on_disk = json::parse(ss.str());
    CHECK_FALSE(on_disk["attachments"][0].contains("previewDataUrl"));
}

TEST_CASE("an image attachment's preview comes from the wired thumbnail maker, any original size",
          "[agent][attachments]")
{
    // The old code refused a preview outright above a 256 KiB cap on the
    // *original* file -- most real phone photos. There is no such cap here
    // any more: a 300 KiB attachment (comfortably over that old threshold)
    // still reaches the maker with its full, untouched bytes.
    Harness           h;
    const std::string original(300 * 1024, 'x');
    std::string       seen;
    h.host.set_image_thumbnail_maker([&seen](const std::string& bytes) {
        seen = bytes;
        return std::string("data:image/jpeg;base64,Zm9v");
    });
    h.handshake();
    const json a = attach(h, "ca-img", "pic.jpg", original, "image/jpeg");
    CHECK(a["kind"] == "image");
    REQUIRE(a.contains("previewDataUrl"));
    CHECK(a["previewDataUrl"] == "data:image/jpeg;base64,Zm9v");
    CHECK(seen == original);
}

// A photo goes to the model at the size models look at, not as taken: the
// wired shrink function decides, and its type and bytes replace the
// original's. A PDF is sent as it is.
TEST_CASE("an attached image reaches the model as the shrink function returns it", "[agent][attachments]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness h{AgentServicePtr(std::move(agent_owner))};
    std::vector<std::string> shrunk;
    h.host.set_image_for_model([&shrunk](const std::string& bytes) -> std::optional<std::pair<std::string, std::string>> {
        shrunk.push_back(bytes);
        return std::make_pair(std::string("image/jpeg"), std::string("small"));
    });
    h.handshake();
    const std::string photo(300 * 1024, 'x');
    const json image = attach(h, "ca-img", "photo.png", photo, "image/png");
    const json pdf   = attach(h, "ca-pdf", "manual.pdf", "%PDF-1.4 a manual", "application/pdf");
    h.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", "what is this"},
                                   {"attachmentIds", json::array({image["id"], pdf["id"]})}});
    h.pump_all();

    REQUIRE_FALSE(agent->requests.empty());
    const std::vector<AgentAttachmentContext>& sent = agent->requests.front().attachments;
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].kind == "image");
    CHECK(sent[0].mime == "image/jpeg");
    CHECK(sent[0].bytes == "small");
    CHECK(sent[1].kind == "pdf");
    CHECK(sent[1].bytes == "%PDF-1.4 a manual");
    CHECK(shrunk == std::vector<std::string>{photo});
}

TEST_CASE("an unsupported binary attachment is rejected visibly", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json a = attach(h, "ca-bin", "firmware.bin", std::string("\x00\x01\x02\x03", 4));
    CHECK(a["kind"] == "unsupported");
    CHECK(a["state"] == "error");
    CHECK(a["error"]["code"] == "unsupported_type");
}

TEST_CASE("resending the same client attachment id is idempotent", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    attach(h, "ca-dup", "a.txt", "one");
    attach(h, "ca-dup", "a.txt", "one");
    CHECK(h.persistence.document().attachments().size() == 1);
}

TEST_CASE("a project-reference attachment carries a summary and no blob", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    h.deliver("attach_file", json{{"clientAttachmentId", "ca-ref"},
                                  {"source", "project"},
                                  {"refKind", "model"},
                                  {"label", "cube-a (on Plate 1)"}});
    const json* updated = h.last_of_type("attachment_updated");
    REQUIRE(updated != nullptr);
    const json a = (*updated)["payload"]["attachment"];
    CHECK(a["kind"] == "model");
    CHECK(a["summary"] == "cube-a (on Plate 1)");
    CHECK(a["state"] == "staged");
}

TEST_CASE("a message carries staged attachments and the mock acknowledges them", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const std::string id = attach(h, "ca-1", "notes.txt", "hello world")["id"].get<std::string>();
    h.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", "what is this"}, {"attachmentIds", json::array({id})}});

    const json* added = h.last_of_type("message_added");
    REQUIRE(added != nullptr);
    CHECK((*added)["payload"]["message"]["attachments"] == json::array({id}));
    // The attachment is now durable history, not staged working state.
    REQUIRE(h.persistence.document().find_attachment(id).has_value());
    CHECK(h.persistence.document().find_attachment(id)->state == "sent");
    // The page's own staged list only drops an attachment once it hears this
    // back; the document alone changing state is not enough.
    const json* sent_update = h.last_of_type("attachment_updated");
    REQUIRE(sent_update != nullptr);
    CHECK((*sent_update)["payload"]["attachment"]["id"] == id);
    CHECK((*sent_update)["payload"]["attachment"]["state"] == "sent");

    h.pump_all();
    const json* completed = h.last_of_type("assistant_completed");
    REQUIRE(completed != nullptr);
    // The streamed reply names the attachment, proving the context reached the Agent.
    const std::vector<ConversationMessage> conversation =
        h.persistence.document().messages(h.persistence.document().active_conversation_id());
    REQUIRE(conversation.size() >= 2);
    CHECK(conversation.back().text.find("notes.txt") != std::string::npos);
}

TEST_CASE("an attachment-only message is accepted but an empty one is not", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json attached = attach(h, "ca-1", "notes.txt", "hello");

    SECTION("text may be empty when an attachment is present") {
        h.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", ""}, {"attachmentIds", json::array({attached["id"]})}});
        CHECK(h.last_of_type("message_added") != nullptr);
    }
    SECTION("a message with neither text nor attachment is refused") {
        const std::size_t before = h.of_type("bridge_error").size();
        h.deliver("user_message", json{{"clientMessageId", "c-2"}, {"text", ""}});
        REQUIRE(h.of_type("bridge_error").size() == before + 1);
        CHECK(h.of_type("bridge_error").back()["payload"]["code"] == "invalid_payload");
    }
}

TEST_CASE("removing a staged attachment deletes its record and blob", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const std::string id = attach(h, "ca-1", "notes.txt", "hello")["id"].get<std::string>();
    const auto blob = std::filesystem::path(h.persistence.jusprin_data_dir()) / "attachments" / id / "notes.txt";
    REQUIRE(std::filesystem::is_regular_file(blob));

    h.deliver("remove_attachment", json{{"attachmentId", id}});
    CHECK(h.persistence.document().attachments().empty());
    CHECK_FALSE(std::filesystem::exists(blob));
    const json* state = h.last_of_type("state");
    REQUIRE(state != nullptr);
    CHECK((*state)["payload"]["attachments"].empty());
}

TEST_CASE("reload reconstructs staged attachments from native state", "[agent][attachments]")
{
    Harness h;
    h.handshake();
    const json staged = attach(h, "ca-1", "notes.txt", "hello world");

    // The page reloads: a fresh handshake must rebuild the staged attachment
    // (with its decoded preview) from authoritative native state.
    h.host.reset_page();
    h.handshake();
    h.deliver("state_request");
    const json* state = h.last_of_type("state");
    REQUIRE(state != nullptr);
    const json attachments = (*state)["payload"]["attachments"];
    REQUIRE(attachments.size() == 1);
    CHECK(attachments[0]["id"] == staged["id"]);
    CHECK(attachments[0]["previewText"] == "hello world");
}

TEST_CASE("a sent model attachment is imported through a tool action", "[agent][attachments][import]")
{
    Harness h;
    h.handshake();

    // Attach a model file; it is stored but reaches the Agent only as a summary.
    const json a = attach(h, "ca-model", "widget.stl", "solid s\nendsolid s\n");
    CHECK(a["kind"] == "model");
    REQUIRE(a.contains("summary"));

    const std::size_t objects_before = h.workspace.snapshot().plates.at(0).objects.size();

    h.deliver("user_message", json{{"clientMessageId", "c-1"}, {"text", "add this"}, {"attachmentIds", json::array({a["id"]})}});
    h.pump_all(); // finish the streamed reply, which submits the import

    const json* activity = h.last_of_type("tool_activity");
    REQUIRE(activity != nullptr);
    const std::string action_id = (*activity)["payload"]["activity"]["actionId"].get<std::string>();
    CHECK((*activity)["payload"]["activity"]["tool"] == "object_import");
    // The coordinator resolves the attachment to its blob and imports it
    // through the workspace, adding one object.
    for (int i = 0; i < 100 && h.host.tools().any_running(); ++i)
        h.host.pump_tools();

    CHECK(h.workspace.snapshot().plates.at(0).objects.size() == objects_before + 1);
    const ToolActivity* record = h.host.tools().find(action_id);
    REQUIRE(record != nullptr);
    CHECK(record->state == ToolState::Succeeded);
}

TEST_CASE("a provider tool call continues from the structured native result", "[agent][provider][tools]")
{
    auto provider = std::make_unique<ToolCallingAgent>();
    ToolCallingAgent* scripted = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());
    const std::size_t copies_before = harness.workspace.snapshot().plates[0].objects[0].instances.size();

    harness.send_user_message("duplicate it", "provider-c-1");
    for (int i = 0; i < 10 && harness.host.tools().activities().empty(); ++i)
        harness.host.pump_stream();
    REQUIRE(harness.host.tools().activities().size() == 1);
    const std::string action_id = harness.host.tools().activities().back().action_id;
    CHECK(harness.host.tools().activities().back().state == ToolState::Running);
    CHECK(scripted->last_request.workspace.selected_objects.size() == 1);
    const json* started = harness.last_of_type("assistant_started");
    REQUIRE(started != nullptr);
    CHECK(scripted->last_request.request_id == harness.persistence.document().project_id() + "-" +
                                                   harness.persistence.document().active_conversation_id() + "-" +
                                                   (*started)["payload"]["messageId"].get<std::string>() + "-attempt-1");
    for (int i = 0; i < 20 && !scripted->continuation; ++i)
        harness.host.pump_tools();
    REQUIRE(scripted->continuation);
    CHECK(scripted->continuation->call_id == "provider-call-1");
    CHECK(scripted->continuation->state == "succeeded");
    const json result = json::parse(scripted->continuation->output_json);
    CHECK(result["state"] == "succeeded");
    CHECK(result.contains("workspaceRevision"));
    CHECK(result["workspace"]["plates"][0]["objects"][0]["instances"] == copies_before + 1);

    for (int i = 0; i < 20 && harness.host.stream_active(); ++i)
        harness.host.pump_stream();
    CHECK(harness.workspace.snapshot().plates[0].objects[0].instances.size() == copies_before + 1);
    REQUIRE(harness.host.conversation().size() == 3);
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
    CHECK(harness.host.conversation().back().text.find("native duplicate succeeded") != std::string::npos);
}

TEST_CASE("a change whose save fails reaches the model as the outcome every later turn replays", "[agent][provider][tools][versions]")
{
    auto provider = std::make_unique<ToolCallingAgent>();
    ToolCallingAgent* scripted = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();
    // The duplicate is made, then the project save after it fails.
    harness.host.tools().set_version_callbacks(
        [](const ToolActivity&) { return std::string("v-before"); },
        [](const ToolActivity&, const std::string&) { throw std::runtime_error("disk full"); });
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());

    harness.send_user_message("duplicate it", "save-fails-c-1");
    for (int i = 0; i < 20 && !scripted->continuation; ++i) {
        harness.host.pump_stream();
        harness.host.pump_tools();
    }
    REQUIRE(scripted->continuation);
    const json sent = json::parse(scripted->continuation->output_json);
    CHECK(scripted->continuation->state == "failed");
    CHECK(sent["state"] == "failed");
    CHECK(sent["error"]["code"] == "local_save_failed");
    harness.pump_all();

    // The stored record is the one the model was sent.
    const std::vector<ToolActivity> stored = harness.persistence.document().activities();
    REQUIRE(stored.size() == 1);
    CHECK(tool_state_name(stored.front().state) == scripted->continuation->state);

    // The next turn sends the same outcome again, from the chat's record.
    harness.send_user_message("and now?", "save-fails-c-2");
    for (int i = 0; i < 20 && scripted->requests.back().purpose != AgentRequest::Purpose::Reply; ++i)
        harness.host.pump_stream();
    const AgentRequest& next = scripted->requests.back();
    REQUIRE(next.purpose == AgentRequest::Purpose::Reply);
    const auto replayed = std::find_if(next.record.items.begin(), next.record.items.end(), [](const RecordItem& item) {
        return item.kind == RecordItem::Kind::Results && item.results.front().call_id == "provider-call-1";
    });
    REQUIRE(replayed != next.record.items.end());
    CHECK(replayed->results.front().output_json == scripted->continuation->output_json);
    CHECK(replayed->results.front().failed);
}

TEST_CASE("a provider tool call refused at submission still continues the turn", "[agent][provider][tools]")
{
    auto provider = std::make_unique<ToolCallingAgent>();
    ToolCallingAgent* scripted = provider.get();
    scripted->stale_patch = true;
    Harness harness(std::move(provider));
    harness.handshake();

    harness.send_user_message("four walls", "provider-stale-c-1");
    for (int i = 0; i < 10 && !scripted->continuation; ++i)
        harness.host.pump_stream();
    REQUIRE(scripted->continuation);
    CHECK(scripted->continuation->state == "failed");
    CHECK(json::parse(scripted->continuation->output_json)["error"]["code"] == "stale_workspace");
    for (int i = 0; i < 20 && harness.host.stream_active(); ++i)
        harness.host.pump_stream();
    CHECK_FALSE(harness.host.stream_active());
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
    CHECK(harness.host.conversation().back().text.find("native duplicate succeeded") != std::string::npos);
}

TEST_CASE("a provider retry reuses the native message without duplicating the turn", "[agent][provider][retry]")
{
    auto provider = std::make_unique<RetryingAgent>();
    RetryingAgent* scripted = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();

    harness.send_user_message("try the provider", "provider-retry-c-1");
    harness.pump_all();
    REQUIRE(scripted->requests.size() == 1);
    REQUIRE(harness.host.conversation().size() == 2);
    REQUIRE(harness.host.conversation().back().state == MessageState::Failed);
    const std::string assistant_id = harness.host.conversation().back().id;

    harness.deliver("retry_message", json{{"messageId", assistant_id}});
    harness.pump_all();
    REQUIRE(scripted->requests.size() == 3);
    CHECK(scripted->requests.back().purpose == AgentRequest::Purpose::ConversationTitle);
    CHECK(scripted->requests[0].request_id != scripted->requests[1].request_id);
    CHECK(scripted->requests[0].request_id.find(assistant_id + "-attempt-1") != std::string::npos);
    CHECK(scripted->requests[1].request_id.find(assistant_id + "-attempt-2") != std::string::npos);
    REQUIRE(harness.host.conversation().size() == 2);
    CHECK(harness.host.conversation().back().id == assistant_id);
    CHECK(harness.host.conversation().back().attempt == 2);
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
    CHECK(harness.host.conversation().back().text == "Recovered without duplicating the turn.");
}

TEST_CASE("provider attachment context is size bounded on a UTF-8 boundary", "[agent][provider][attachments]")
{
    auto provider = std::make_unique<ToolCallingAgent>();
    ToolCallingAgent* scripted = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();
    REQUIRE(harness.workspace.select_object(harness.workspace.snapshot().plates[0].objects[0].id).succeeded());

    std::string text(256u * 1024u - 1u, 'a');
    text += "\xE2\x82\xACtail";
    const json large = attach(harness, "large-text", "large.txt", text);
    harness.deliver("user_message", json{{"clientMessageId", "bounded-c-1"},
                                          {"text", "use the attachment"},
                                          {"attachmentIds", json::array({large["id"]})}});

    REQUIRE(scripted->last_request.attachments.size() == 1);
    const auto& supplied = scripted->last_request.attachments.front();
    CHECK(supplied.text.size() == 256u * 1024u - 1u);
    CHECK(supplied.text.back() == 'a');
    CHECK(supplied.summary.find("truncated") != std::string::npos);
}

namespace {

// Stands in for a real provider check: the test decides when the answer
// arrives and what it is, so the host's half of setup is exercised without a
// network.
class ScriptedSetup final : public IAgentSetupService
{
public:
    bool busy() const override { return m_busy; }

    bool start_check(const SetupCredentials& credentials) override
    {
        if (!accept_checks)
            return false;
        started.push_back(credentials);
        m_busy = true;
        return true;
    }

    void cancel() override
    {
        m_busy = false;
        m_pending.reset();
        ++cancels;
    }

    std::optional<SetupOutcome> poll() override
    {
        if (!m_pending)
            return std::nullopt;
        SetupOutcome outcome = std::move(*m_pending);
        m_pending.reset();
        m_busy = false;
        return outcome;
    }

    bool commit(const SetupCredentials& credentials) override
    {
        committed.push_back(credentials);
        if (commit_succeeds)
            stored[credentials.provider] = credentials.api_key;
        return commit_succeeds;
    }

    std::vector<SavedKey> saved_keys() const override
    {
        std::vector<SavedKey> saved;
        for (const ProviderProfile& profile : provider_profiles())
            if (const auto key = stored.find(profile.id); key != stored.end())
                saved.push_back(SavedKey{profile.id, api_key_hint(profile.id, key->second)});
        return saved;
    }

    bool remove_key(const std::string& provider) override { return stored.erase(provider) > 0; }

    void answer_verified(int elapsed_ms = 800)
    {
        SetupOutcome outcome;
        outcome.ok         = true;
        outcome.elapsed_ms = elapsed_ms;
        outcome.service    = std::make_unique<DeterministicMockAgent>();
        m_pending          = std::move(outcome);
    }

    void answer_rejected()
    {
        SetupOutcome outcome;
        outcome.error = AgentError{"invalid_credentials", "The OpenAI API key was rejected.", false};
        m_pending     = std::move(outcome);
    }

    std::vector<SetupCredentials> started;
    std::vector<SetupCredentials> committed;
    // The keys this "computer" holds, by provider.
    std::map<std::string, std::string> stored;
    bool                          accept_checks{true};
    bool                          commit_succeeds{true};
    int                           cancels{0};

private:
    std::optional<SetupOutcome> m_pending;
    bool                        m_busy{false};
};

json check_key_payload(const std::string& key = "sk-test-key")
{
    return json{{"provider", "openai"}, {"apiKey", key}};
}

} // namespace

TEST_CASE("a verified key stores the credential and connects the Agent in one step", "[agent][setup]")
{
    auto    setup = std::make_shared<ScriptedSetup>();
    Harness harness(setup);
    harness.handshake();
    REQUIRE(harness.host.availability() == AgentAvailability::Unavailable);

    harness.deliver("setup_check_key", check_key_payload());
    const json* checking = harness.last_of_type("setup_status");
    REQUIRE(checking != nullptr);
    CHECK((*checking)["payload"]["phase"] == "checking");
    CHECK((*checking)["payload"]["provider"] == "openai");
    REQUIRE(setup->started.size() == 1);
    CHECK(setup->started.front().api_key == "sk-test-key");
    // Nothing changes until the provider actually answers.
    CHECK(harness.host.availability() == AgentAvailability::Unavailable);

    setup->answer_verified(812);
    harness.host.pump_setup();

    const json* verified = harness.last_of_type("setup_status");
    REQUIRE(verified != nullptr);
    CHECK((*verified)["payload"]["phase"] == "verified");
    CHECK((*verified)["payload"]["elapsedMs"] == 812);
    CHECK_FALSE((*verified)["payload"].contains("warning"));

    // The key the page sent is what gets stored; the page never re-sends it.
    REQUIRE(setup->committed.size() == 1);
    CHECK(setup->committed.front().api_key == "sk-test-key");

    CHECK(harness.host.availability() == AgentAvailability::Ready);
    CHECK((*harness.last_of_type("agent_status"))["payload"]["status"] == "ready");

    // The dock is genuinely usable now, not merely reported as ready.
    harness.send_user_message("hello", "c-after-setup");
    harness.pump_all();
    REQUIRE(harness.last_of_type("assistant_completed") != nullptr);
}

TEST_CASE("a rejected key reports the reason and leaves the Agent unconfigured", "[agent][setup]")
{
    auto    setup = std::make_shared<ScriptedSetup>();
    Harness harness(setup);
    harness.handshake();

    harness.deliver("setup_check_key", check_key_payload("sk-wrong"));
    setup->answer_rejected();
    harness.host.pump_setup();

    const json* status = harness.last_of_type("setup_status");
    REQUIRE(status != nullptr);
    CHECK((*status)["payload"]["phase"] == "error");
    CHECK((*status)["payload"]["error"]["code"] == "invalid_credentials");
    CHECK(setup->committed.empty());
    CHECK(harness.host.availability() == AgentAvailability::Unavailable);
}

TEST_CASE("a verified key that cannot be stored still connects and says so", "[agent][setup]")
{
    auto setup             = std::make_shared<ScriptedSetup>();
    setup->commit_succeeds = false;
    Harness harness(setup);
    harness.handshake();

    harness.deliver("setup_check_key", check_key_payload());
    setup->answer_verified();
    harness.host.pump_setup();

    const json* status = harness.last_of_type("setup_status");
    REQUIRE(status != nullptr);
    CHECK((*status)["payload"]["phase"] == "verified");
    // The Agent works for this session; the user is told it will not survive
    // a restart rather than finding out at the next launch.
    REQUIRE((*status)["payload"].contains("warning"));
    CHECK(harness.host.availability() == AgentAvailability::Ready);
}

TEST_CASE("a provider this build cannot verify fails visibly instead of hanging", "[agent][setup]")
{
    auto setup           = std::make_shared<ScriptedSetup>();
    setup->accept_checks = false;
    Harness harness(setup);
    harness.handshake();

    harness.deliver("setup_check_key", json{{"provider", "anthropic"}, {"apiKey", "sk-ant-test"}});

    const json* status = harness.last_of_type("setup_status");
    REQUIRE(status != nullptr);
    CHECK((*status)["payload"]["phase"] == "error");
    CHECK((*status)["payload"]["error"]["code"] == "unsupported_provider");
    CHECK(harness.host.availability() == AgentAvailability::Unavailable);
}

TEST_CASE("setup rejects an empty key and a second concurrent check", "[agent][setup]")
{
    auto    setup = std::make_shared<ScriptedSetup>();
    Harness harness(setup);
    harness.handshake();

    harness.deliver("setup_check_key", json{{"provider", "openai"}, {"apiKey", ""}});
    REQUIRE(harness.last_of_type("bridge_error") != nullptr);
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "invalid_payload");
    CHECK(setup->started.empty());

    harness.deliver("setup_check_key", check_key_payload());
    harness.deliver("setup_check_key", check_key_payload("sk-second"));
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "setup_busy");
    CHECK(setup->started.size() == 1);
}

TEST_CASE("cancelling a check returns the dock to its offer", "[agent][setup]")
{
    auto    setup = std::make_shared<ScriptedSetup>();
    Harness harness(setup);
    harness.handshake();

    harness.deliver("setup_check_key", check_key_payload());
    harness.deliver("setup_cancel");

    CHECK(setup->cancels == 1);
    CHECK((*harness.last_of_type("setup_status"))["payload"]["phase"] == "idle");
    CHECK(harness.host.availability() == AgentAvailability::Unavailable);

    // A check can start again afterwards.
    harness.deliver("setup_check_key", check_key_payload("sk-again"));
    REQUIRE(setup->started.size() == 2);
}

TEST_CASE("a build with no setup service says so rather than accepting the key", "[agent][setup]")
{
    Harness harness(AgentAvailability::Unavailable);
    harness.handshake();

    harness.deliver("setup_check_key", check_key_payload());
    const json* error = harness.last_of_type("bridge_error");
    REQUIRE(error != nullptr);
    CHECK((*error)["payload"]["code"] == "setup_unavailable");
    CHECK(harness.last_of_type("setup_status") == nullptr);
}

namespace {

// Test keys only: none of these is anyone's real key.
const std::string kOpenAiKey    = "sk-proj-TESTONLY0000000000000000wxyz";
const std::string kAnthropicKey = "sk-ant-api03-TESTONLY000000000000000a3F9";

// Answers every request with the provider refusing the key, as ProviderAgent
// reports a 401.
class RejectingAgent final : public IAgentService
{
public:
    bool ready() const override { return true; }
    bool busy() const override { return m_busy; }
    bool start(const AgentRequest&) override { return m_busy = true; }
    bool continue_after_tool(const AgentToolResult&) override { return false; }
    void cancel() override { m_busy = false; }
    std::optional<AgentEvent> poll() override
    {
        if (!m_busy)
            return std::nullopt;
        m_busy = false;
        AgentEvent event;
        event.kind  = AgentEventKind::Failed;
        event.error = AgentError{"invalid_credentials", "The OpenAI API key was rejected.", false};
        return event;
    }

private:
    bool m_busy{false};
};

// The docked panel's models, read from the same stored keys the setup
// service holds, so removing a key there is seen here.
struct StoredKeysModelSource final : IChatModelSource
{
    explicit StoredKeysModelSource(std::shared_ptr<ScriptedSetup> keys) : setup(std::move(keys)) {}

    std::shared_ptr<ScriptedSetup> setup;
    std::string                    recorded{"openai"};
    bool                           keys_refused{false};
    std::vector<ChatModelChoice>   built;

    std::vector<std::string> configured_providers() const override
    {
        std::vector<std::string> providers;
        for (const SavedKey& saved : setup->saved_keys())
            providers.push_back(saved.provider);
        return providers;
    }
    // As AppConfig does once the recorded provider's key is removed.
    std::string default_provider() const override
    {
        const std::vector<std::string> providers = configured_providers();
        if (providers.empty() || std::find(providers.begin(), providers.end(), recorded) != providers.end())
            return recorded;
        return providers.front();
    }
    AgentServicePtr make_agent(const ChatModelChoice& choice) override
    {
        if (setup->stored.count(choice.provider) == 0)
            return nullptr;
        built.push_back(choice);
        if (keys_refused)
            return std::make_unique<RejectingAgent>();
        return std::make_unique<DeterministicMockAgent>();
    }
    void list_models(const std::string&, ModelsDone done) override { done(true, {}); }
};

std::vector<json> saved_keys_of(const Harness& harness)
{
    const json* keys = harness.last_of_type("setup_keys");
    REQUIRE(keys != nullptr);
    return (*keys)["payload"]["keys"].get<std::vector<json>>();
}

bool any_envelope_contains(const Harness& harness, const std::string& text)
{
    return std::any_of(harness.sent.begin(), harness.sent.end(),
                       [&text](const json& envelope) { return envelope.dump().find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("the page learns which providers have a key, and only a hint of each", "[agent][setup][saved_keys]")
{
    auto setup = std::make_shared<ScriptedSetup>();
    setup->stored = {{"openai", kOpenAiKey}, {"anthropic", kAnthropicKey}};
    Harness harness(setup);
    harness.handshake();

    const json* ack = harness.last_of_type("hello_ack");
    REQUIRE(ack != nullptr);
    const auto capabilities = (*ack)["payload"]["capabilities"].get<std::vector<std::string>>();
    CHECK(std::count(capabilities.begin(), capabilities.end(), "saved_keys") == 1);

    // In setup's order, the known start and last 4 characters only.
    const std::vector<json> keys = saved_keys_of(harness);
    REQUIRE(keys.size() == 2);
    CHECK(keys[0] == json{{"provider", "openai"}, {"hint", "sk-proj-…wxyz"}, {"rejected", false}});
    CHECK(keys[1] == json{{"provider", "anthropic"}, {"hint", "sk-ant-…a3F9"}, {"rejected", false}});
    CHECK_FALSE(any_envelope_contains(harness, "TESTONLY"));
}

TEST_CASE("a key that passes its check is listed as saved", "[agent][setup][saved_keys]")
{
    auto    setup = std::make_shared<ScriptedSetup>();
    Harness harness(setup);
    harness.handshake();
    CHECK(saved_keys_of(harness).empty());

    harness.deliver("setup_check_key", json{{"provider", "anthropic"}, {"apiKey", kAnthropicKey}});
    // Nothing is listed until the provider has answered.
    CHECK(saved_keys_of(harness).empty());
    setup->answer_verified();
    harness.host.pump_setup();

    const std::vector<json> keys = saved_keys_of(harness);
    REQUIRE(keys.size() == 1);
    CHECK(keys[0]["provider"] == "anthropic");
    CHECK(keys[0]["hint"] == "sk-ant-…a3F9");
    CHECK_FALSE(any_envelope_contains(harness, "TESTONLY"));
}

TEST_CASE("Remove deletes the provider's key and rebuilds the Agent from the keys left", "[agent][setup][saved_keys]")
{
    auto setup = std::make_shared<ScriptedSetup>();
    setup->stored = {{"openai", kOpenAiKey}, {"anthropic", kAnthropicKey}};
    Harness harness(setup);
    auto source = std::make_shared<StoredKeysModelSource>(setup);
    harness.host.set_chat_model_source(source);
    harness.host.set_agent(source->make_agent(ChatModelChoice{"openai", "gpt-5.4-mini", ""}), AgentAvailability::Ready);
    harness.handshake();
    source->built.clear();

    // The recorded provider's key goes: the Agent is rebuilt on the one left.
    harness.deliver("setup_remove_key", json{{"provider", "openai"}});
    CHECK(setup->stored.count("openai") == 0);
    std::vector<json> keys = saved_keys_of(harness);
    REQUIRE(keys.size() == 1);
    CHECK(keys[0]["provider"] == "anthropic");
    REQUIRE(source->built.size() == 1);
    CHECK(source->built.back().provider == "anthropic");
    CHECK(harness.host.availability() == AgentAvailability::Ready);
    harness.send_user_message("still here?", "c-after-remove");
    harness.pump_all();
    CHECK(harness.last_of_type("assistant_completed") != nullptr);

    // A provider with no key, or none this build knows, is refused.
    harness.deliver("setup_remove_key", json{{"provider", "openai"}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "remove_failed");

    // The last key goes: nothing is left to talk to.
    harness.deliver("setup_remove_key", json{{"provider", "anthropic"}});
    CHECK(setup->stored.empty());
    CHECK(saved_keys_of(harness).empty());
    CHECK(harness.host.availability() == AgentAvailability::Unavailable);
    CHECK((*harness.last_of_type("agent_status"))["payload"]["status"] == "unavailable");
}

TEST_CASE("a key is not removed while a reply or a key check is running", "[agent][setup][saved_keys]")
{
    auto setup = std::make_shared<ScriptedSetup>();
    setup->stored = {{"openai", kOpenAiKey}};
    Harness harness(setup);

    SECTION("a key check")
    {
        harness.handshake();
        harness.deliver("setup_check_key", json{{"provider", "openai"}, {"apiKey", "sk-proj-TESTONLY-new-key-000000"}});
        harness.deliver("setup_remove_key", json{{"provider", "openai"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "setup_busy");
        CHECK(setup->stored.count("openai") == 1);
    }
    SECTION("a reply")
    {
        auto source = std::make_shared<StoredKeysModelSource>(setup);
        harness.host.set_chat_model_source(source);
        harness.host.set_agent(source->make_agent(ChatModelChoice{"openai", openai_profile().model, ""}), AgentAvailability::Ready);
        harness.handshake();
        harness.send_user_message("hello", "c-streaming");
        REQUIRE(harness.host.stream_active());
        harness.deliver("setup_remove_key", json{{"provider", "openai"}});
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "busy");
        CHECK(setup->stored.count("openai") == 1);
        // The reply that was streaming finishes on the agent it started on.
        harness.pump_all();
        CHECK(harness.last_of_type("assistant_completed") != nullptr);
    }
}

TEST_CASE("a key the provider refuses shows as rejected until it is replaced", "[agent][setup][saved_keys]")
{
    auto setup = std::make_shared<ScriptedSetup>();
    setup->stored = {{"openai", kOpenAiKey}};
    Harness harness(setup);
    auto source = std::make_shared<StoredKeysModelSource>(setup);
    source->keys_refused = true;
    harness.host.set_chat_model_source(source);
    harness.host.set_agent(source->make_agent(ChatModelChoice{"openai", "gpt-5.4-mini", ""}), AgentAvailability::Ready);
    harness.handshake();
    CHECK(saved_keys_of(harness)[0]["rejected"] == false);

    harness.send_user_message("hello", "c-refused");
    harness.pump_all();
    REQUIRE(harness.last_of_type("assistant_failed") != nullptr);
    CHECK(saved_keys_of(harness)[0] == json{{"provider", "openai"}, {"hint", "sk-proj-…wxyz"}, {"rejected", true}});
    // A new page asks again and is told the same.
    harness.host.reset_page();
    harness.handshake();
    CHECK(saved_keys_of(harness)[0]["rejected"] == true);

    // Replace keeps the refused key until the new one passes, then lists the
    // new one as working.
    const std::string replacement = "sk-proj-TESTONLY1111111111111111abcd";
    harness.deliver("setup_check_key", json{{"provider", "openai"}, {"apiKey", replacement}});
    CHECK(setup->stored["openai"] == kOpenAiKey);
    setup->answer_verified();
    harness.host.pump_setup();
    CHECK(setup->stored["openai"] == replacement);
    CHECK(saved_keys_of(harness)[0] == json{{"provider", "openai"}, {"hint", "sk-proj-…abcd"}, {"rejected", false}});
}

TEST_CASE("a failed replacement leaves the saved key in place", "[agent][setup][saved_keys]")
{
    auto setup = std::make_shared<ScriptedSetup>();
    setup->stored = {{"openai", kOpenAiKey}};
    Harness harness(setup);
    harness.handshake();

    harness.deliver("setup_check_key", json{{"provider", "openai"}, {"apiKey", "sk-proj-TESTONLY-wrong-000000"}});
    setup->answer_rejected();
    harness.host.pump_setup();
    CHECK((*harness.last_of_type("setup_status"))["payload"]["phase"] == "error");
    CHECK(setup->stored["openai"] == kOpenAiKey);
    // The saved key was not the one refused.
    CHECK(saved_keys_of(harness)[0]["rejected"] == false);
    CHECK(saved_keys_of(harness)[0]["hint"] == "sk-proj-…wxyz");
}

namespace {

// The deterministic mock, also counting the title requests it is given and
// the times the host cancels it, so a test can tell an interrupted title
// from one that simply finished first.
class TitleCountingAgent final : public DeterministicMockAgent
{
public:
    bool start(const AgentRequest& request) override
    {
        if (request.purpose == AgentRequest::Purpose::ConversationTitle)
            ++titles;
        return DeterministicMockAgent::start(request);
    }
    void cancel() override
    {
        ++cancels;
        DeterministicMockAgent::cancel();
    }

    int titles{0};
    int cancels{0};
};

} // namespace

TEST_CASE("chat titles are metadata and user renames win over generation", "[agent][bridge][conversations]")
{
    auto agent_owner = std::make_unique<TitleCountingAgent>();
    TitleCountingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const auto id = harness.persistence.document().active_conversation_id();
    harness.send_user_message("Help print a backpack frame", "title-user");
    harness.pump_all();
    REQUIRE(harness.host.conversation().size() == 2);
    // The reply's end asked for a title, which is still in progress.
    REQUIRE(agent->titles == 1);
    const int cancels = agent->cancels;
    SECTION("a completed title is persisted and sent without a transcript message") {
        for (int i = 0; i < 5; ++i) harness.host.pump_stream();
        CHECK(harness.persistence.document().conversations().front().title == "Print setup discussion");
        CHECK(harness.host.conversation().size() == 2);
        const auto* update = harness.last_of_type("conversations_updated");
        REQUIRE(update);
        CHECK((*update)["payload"]["conversations"][0]["title"] == "Print setup discussion");
        harness.host.reset_page();
        harness.handshake();
        CHECK((*harness.last_of_type("state"))["payload"]["conversations"][0]["title"] == "Print setup discussion");
    }
    SECTION("rename interrupts a title in progress and survives later exchanges") {
        harness.deliver("rename_conversation", json{{"conversationId", id}, {"title", "My backpack"}});
        CHECK(agent->cancels == cancels + 1);
        for (int i = 0; i < 5; ++i) harness.host.pump_stream();
        harness.send_user_message("What material?", "title-followup");
        harness.pump_all();
        for (int i = 0; i < 5; ++i) harness.host.pump_stream();
        CHECK(harness.persistence.document().conversations().front().title == "My backpack");
        CHECK(harness.host.conversation().size() == 4);
        // A chat the person named is not titled again.
        CHECK(agent->titles == 1);
    }
    SECTION("a new user turn interrupts metadata generation") {
        harness.send_user_message("What material?", "title-followup");
        CHECK(agent->cancels == cancels + 1);
        CHECK(harness.host.stream_active());
        harness.pump_all();
        for (int i = 0; i < 5; ++i) harness.host.pump_stream();
        CHECK(harness.host.conversation().size() == 4);
        // The interrupted title is asked for again after the new reply.
        CHECK(agent->titles == 2);
        CHECK(harness.persistence.document().conversations().front().title == "Print setup discussion");
    }
}

TEST_CASE("chat deletion validates state and cannot affect another project", "[agent][bridge][conversations]")
{
    Harness harness;
    harness.handshake();
    const auto id = harness.persistence.document().active_conversation_id();
    harness.send_user_message("Print this frame", "delete-user");
    harness.deliver("delete_conversation", json{{"conversationId", id}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "busy");
    CHECK(harness.persistence.document().conversations().size() == 1);
    harness.pump_all();
    const auto revision = harness.workspace.snapshot().revision;
    harness.deliver("delete_conversation", json{{"conversationId", id}});
    REQUIRE(harness.persistence.document().conversations().size() == 1);
    CHECK(harness.persistence.document().active_conversation_id() != id);
    CHECK(harness.host.conversation().empty());
    CHECK(harness.workspace.snapshot().revision == revision);
    harness.deliver("rename_conversation", json{{"conversationId", id}, {"title", "Old chat"}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "rename_failed");
    harness.deliver("delete_conversation", json{{"conversationId", "another-project-chat"}});
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "unknown_conversation");
    CHECK(harness.persistence.document().conversations().size() == 1);
}

TEST_CASE("a title failure stays separate from a successful conversation", "[agent][bridge][conversations]")
{
    class FailingTitleAgent : public DeterministicMockAgent {
    public:
        bool start(const AgentRequest& request) override
        {
            fail_title = request.purpose == AgentRequest::Purpose::ConversationTitle;
            return DeterministicMockAgent::start(request);
        }
        std::optional<AgentEvent> poll() override
        {
            if (fail_title) {
                fail_title = false;
                cancel();
                return AgentEvent::failed({"provider_timeout", "Title request timed out", true});
            }
            return DeterministicMockAgent::poll();
        }
        bool fail_title{false};
    };
    Harness harness(std::make_unique<FailingTitleAgent>());
    harness.handshake();
    harness.send_user_message("Help with my print", "title-failure");
    harness.pump_all();
    harness.host.pump_stream();
    CHECK(harness.host.conversation().size() == 2);
    CHECK(harness.host.conversation().back().state == MessageState::Complete);
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "title_generation_failed");
    harness.deliver("rename_conversation", json{{"conversationId", harness.persistence.document().active_conversation_id()},
                                              {"title", "My print"}});
    CHECK(harness.persistence.document().conversations().front().title == "My print");
}

TEST_CASE("a chat title is made from the user's words, not the reply", "[agent][bridge][conversations]")
{
    // With the reply beside the user's message, gpt-5.4-mini titled an English
    // chat in Chinese; the title request carries only what the user wrote.
    class RecordingAgent : public DeterministicMockAgent {
    public:
        bool start(const AgentRequest& request) override
        {
            requests.push_back(request);
            return DeterministicMockAgent::start(request);
        }
        std::vector<AgentRequest> requests;
    };
    auto provider = std::make_unique<RecordingAgent>();
    RecordingAgent* recording = provider.get();
    Harness harness(std::move(provider));
    harness.handshake();
    const std::string text = "Turn on tree supports, only where they touch the build plate, and add a brim around the model.";
    harness.send_user_message(text, "title-language");
    harness.pump_all();
    REQUIRE(harness.host.conversation().size() == 2);
    REQUIRE_FALSE(harness.host.conversation().back().text.empty());

    REQUIRE(recording->requests.size() == 2);
    const AgentRequest& title = recording->requests.back();
    CHECK(title.purpose == AgentRequest::Purpose::ConversationTitle);
    // The person's words, and nothing of the chat's record.
    CHECK(title.user_text == text);
    CHECK(title.record.items.empty());
    CHECK(title.unsent.empty());
}

TEST_CASE("mcp_catalog lists stdio discovery entries without a live URL payload", "[agent][mcp_setup]")
{
    Harness harness;
    JusPrinTest::McpDirectory directory;
    const auto helper = directory.root / "jusprin-mcp";
    { std::ofstream(helper) << "x\n"; }
    std::filesystem::permissions(helper, std::filesystem::perms::owner_all);
    AgentHost::McpConnectSettings settings;
    settings.helper_path = helper.u8string();
    settings.launcher_path = helper.u8string();
    settings.home = directory.root / "home";
    settings.config_home = directory.root / "config";
    harness.host.configure_mcp_connect(settings);
    harness.host.start_mcp((directory.root / "discovery.json").u8string());
    harness.handshake();
    harness.deliver("mcp_catalog");
    const json* catalog = harness.last_of_type("mcp_catalog");
    REQUIRE(catalog != nullptr);
    CHECK((*catalog)["payload"]["helperPresent"] == true);
    // Every client McpConnections.hpp lists, in its order, each launching the
    // helper over stdio with the discovery file and none given a live URL.
    const json& tools = (*catalog)["payload"]["tools"];
    const auto entries = Mcp::connection_entries(helper.u8string(), (directory.root / "discovery.json").u8string(), false);
    REQUIRE(tools.size() == entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        INFO(entries[i].id);
        CHECK(tools[i]["id"] == entries[i].id);
        CHECK(tools[i]["cli"] == entries[i].cli);
        const auto text = tools[i]["text"].get<std::string>();
        CHECK(text.find("--discovery") != std::string::npos);
        CHECK(text.find("\"url\"") == std::string::npos);
        CHECK_FALSE(tools[i].contains("url"));
    }
}

TEST_CASE("mcp_connect writes a reviewed JusPrin JSON entry", "[agent][mcp_setup]")
{
    Harness harness;
    JusPrinTest::McpDirectory directory;
    const auto helper = directory.root / "jusprin-mcp";
    { std::ofstream(helper) << "x\n"; }
    std::filesystem::permissions(helper, std::filesystem::perms::owner_all);
    const auto home = directory.root / "home";
    std::filesystem::create_directories(home / ".cursor");
    const auto cursor_config = home / ".cursor" / "mcp.json";
    { std::ofstream(cursor_config) << "{}\n"; }
    AgentHost::McpConnectSettings settings;
    settings.helper_path = helper.u8string();
    settings.launcher_path = helper.u8string();
    settings.home = home;
    settings.config_home = directory.root / "config";
    harness.host.configure_mcp_connect(settings);
    harness.host.start_mcp((directory.root / "discovery.json").u8string());
    harness.handshake();
    harness.deliver("mcp_preview", json{{"toolId", "cursor"}});
    const json* preview = harness.last_of_type("mcp_preview");
    REQUIRE(preview != nullptr);
    CHECK((*preview)["payload"]["next"].get<std::string>().find("\"url\"") == std::string::npos);
    CHECK((*preview)["payload"]["next"].get<std::string>().find("command") != std::string::npos);
    // No JusPrin entry in the file yet, so there is no previous entry to show.
    // The field is absent rather than the string "null", which the page could
    // not tell from a real entry.
    CHECK_FALSE((*preview)["payload"].contains("previous"));
    harness.deliver("mcp_connect", json{{"toolId", "cursor"}});
    const json* status = harness.last_of_type("mcp_status");
    REQUIRE(status != nullptr);
    CHECK((*status)["payload"]["phase"] == "saved");
    std::ifstream in(cursor_config);
    const json written = json::parse(in);
    CHECK(written["mcpServers"]["jusprin"]["command"] == helper.u8string());
    CHECK_FALSE(written["mcpServers"]["jusprin"].contains("url"));
}

TEST_CASE("reveal_path resolves a tool id to the host's own config path", "[agent][mcp_setup]")
{
    Harness harness;
    JusPrinTest::McpDirectory directory;
    const auto home = directory.root / "home";
    std::filesystem::create_directories(home / ".cursor");
    const auto cursor_config = home / ".cursor" / "mcp.json";
    { std::ofstream(cursor_config) << "{}\n"; }
    AgentHost::McpConnectSettings settings;
    settings.home = home;
    settings.config_home = directory.root / "config";
    harness.host.configure_mcp_connect(settings);
    std::vector<std::string> revealed;
    harness.host.set_reveal_path_handler([&](const std::string& path) { revealed.push_back(path); });
    harness.handshake();

    harness.deliver("reveal_path", json{{"toolId", "cursor"}});
    REQUIRE(revealed.size() == 1);
    CHECK(revealed.front() == cursor_config.u8string());

    // A path the page invents never reaches the shell: only a tool id is
    // accepted, and only one the catalog knows. Neither refusal reaches the
    // shell, and neither is described to the page: the catalog is a fixed
    // table, so a name outside it can only come from a broken page.
    CHECK_THROWS_AS(harness.deliver("reveal_path", json{{"toolId", "not-a-tool"}}), Slic3r::RuntimeError);
    CHECK_THROWS_AS(harness.deliver("reveal_path", json{{"path", "/etc/passwd"}}), Slic3r::RuntimeError);
    CHECK(revealed.size() == 1);
}

TEST_CASE("mcp_connect reports a missing helper instead of writing settings", "[agent][mcp_setup]")
{
    Harness harness;
    JusPrinTest::McpDirectory directory;
    const auto home = directory.root / "home";
    std::filesystem::create_directories(home / ".cursor");
    { std::ofstream(home / ".cursor" / "mcp.json") << "{}\n"; }
    AgentHost::McpConnectSettings settings;
    settings.helper_path = (directory.root / "missing-helper").u8string();
    settings.home = home;
    settings.config_home = directory.root / "config";
    harness.host.configure_mcp_connect(settings);
    harness.handshake();
    harness.deliver("mcp_preview", json{{"toolId", "cursor"}});
    harness.deliver("mcp_connect", json{{"toolId", "cursor"}});
    const json* status = harness.last_of_type("mcp_status");
    REQUIRE(status != nullptr);
    CHECK((*status)["payload"]["phase"] == "error");
    CHECK((*status)["payload"]["error"]["code"] == "helper_missing");
}

namespace {

// The CLI is discovered by McpCatalog::command_on_path, which searches PATH for
// the command name platform-dressed: "claude.exe" split on ';' under Windows,
// bare "claude" split on ':' elsewhere. The fixture below has to match that
// shape on both, or the search looks for a name the test never created.
#ifdef _WIN32
constexpr char kPathSeparator = ';';
constexpr const char* kExecutableSuffix = ".exe";
#else
constexpr char kPathSeparator = ':';
constexpr const char* kExecutableSuffix = "";
#endif

void set_path_env(const std::string& value)
{
#ifdef _WIN32
    // setenv is POSIX; MSVC offers _putenv_s with the arguments reversed.
    _putenv_s("PATH", value.c_str());
#else
    setenv("PATH", value.c_str(), 1);
#endif
}

} // namespace

TEST_CASE("mcp_connect runs an injected CLI runner", "[agent][mcp_setup]")
{
    Harness harness;
    JusPrinTest::McpDirectory directory;
    const auto helper = directory.root / "jusprin-mcp";
    { std::ofstream(helper) << "x\n"; }
    std::filesystem::permissions(helper, std::filesystem::perms::owner_all);
    const auto bin = directory.root / "bin";
    std::filesystem::create_directories(bin);
    const auto claude = bin / (std::string("claude") + kExecutableSuffix);
    { std::ofstream(claude) << "#!/bin/sh\n"; }
    std::filesystem::permissions(claude, std::filesystem::perms::owner_all);
    const char* previous = std::getenv("PATH");
    const std::string restored = previous ? previous : "";
    const std::string path = bin.u8string() + kPathSeparator + restored;
    set_path_env(path);
    AgentHost::McpConnectSettings settings;
    settings.helper_path = helper.u8string();
    settings.launcher_path = helper.u8string();
    settings.home = directory.root / "home";
    settings.config_home = directory.root / "config";
    harness.host.configure_mcp_connect(settings);
    std::vector<std::string> seen;
    harness.host.set_mcp_cli_runner([&](const std::vector<std::string>& arguments, AgentHost::McpCliDone done) {
        seen = arguments;
        done(true, "ok");
    });
    harness.handshake();
    harness.deliver("mcp_connect", json{{"toolId", "claude"}});
    set_path_env(restored);
    REQUIRE(seen.size() >= 2);
    CHECK(seen[0] == claude.u8string());
    CHECK(std::find(seen.begin(), seen.end(), "--discovery") != seen.end());
    const json* status = harness.last_of_type("mcp_status");
    REQUIRE(status != nullptr);
    CHECK((*status)["payload"]["phase"] == "saved");
}

// -- Messages sent from a print-issue bubble --------------------------------

namespace {

std::string issue_context()
{
    return json{{"kind", "print_issue"}, {"issue", {{"id", "validation:warning::7:9"}}}}.dump();
}

std::size_t user_messages(const Harness& harness)
{
    std::size_t count = 0;
    for (const ConversationMessage& message : harness.host.conversation())
        count += message.role == MessageRole::User ? 1 : 0;
    return count;
}

} // namespace

TEST_CASE("a message sent from outside the page is an ordinary user message with its context", "[agent][print-issues]")
{
    auto agent = std::make_unique<RecordingAgent>();
    RecordingAgent* recorded = agent.get();
    Harness harness(std::move(agent));
    harness.handshake();
    harness.deliver("draft_update", json{{"text", "half-typed thought"}});

    const std::string text = "Help fix this issue.\n\n> Cube is too close to others.";
    CHECK(harness.host.submit_issue_message("issue-1", text, issue_context()) == AgentHost::SubmitResult::Started);

    const json* added = harness.last_of_type("message_added");
    REQUIRE(added != nullptr);
    const json& message = (*added)["payload"]["message"];
    CHECK(message["role"] == "user");
    CHECK(message["text"] == text);
    CHECK(message["clientMessageId"] == "issue-1");
    CHECK(message["issueContext"]["kind"] == "print_issue");

    REQUIRE_FALSE(recorded->requests.empty());
    const AgentRequest& request = recorded->requests.front();
    CHECK(request.purpose == AgentRequest::Purpose::Reply);
    // The person's words as they are, and the context beside them, for the
    // adapter to send as a block of its own.
    CHECK(request.user_text == text);
    CHECK(request.issue_context == issue_context());
    // Whichever button sent it, the turn is an ordinary one: every in-app tool.
    CHECK(request.session.tool_names.empty());
    // It did not come from the composer, so what was being typed is still there.
    CHECK(harness.persistence.draft() == "half-typed thought");

    // Durable like any message: the context is in the document, not only on the wire.
    const auto stored = harness.persistence.document().messages(harness.persistence.document().active_conversation_id());
    REQUIRE_FALSE(stored.empty());
    CHECK(json::parse(stored.front().issue_context)["issue"]["id"] == "validation:warning::7:9");

    harness.pump_all();
    const std::size_t before = user_messages(harness);
    CHECK(harness.host.submit_issue_message("issue-1", text, issue_context()) == AgentHost::SubmitResult::Duplicate);
    CHECK(user_messages(harness) == before);
}

TEST_CASE("a message from outside the page obeys the chat's own state", "[agent][print-issues]")
{
    Harness harness{std::make_unique<RecordingAgent>()};
    harness.handshake();

    // Mid-reply it waits its turn, as a page message would.
    CHECK(harness.host.submit_issue_message("issue-a", "Help fix this issue.", issue_context()) == AgentHost::SubmitResult::Started);
    CHECK(harness.host.busy());
    CHECK(harness.host.submit_issue_message("issue-b", "Explain this issue and my options.", issue_context()) ==
          AgentHost::SubmitResult::Queued);
    harness.pump_all();

    std::string refusal;
    CHECK(harness.host.submit_issue_message("", "text", issue_context(), &refusal) == AgentHost::SubmitResult::Refused);
    CHECK(refusal == "invalid_payload");

    // Reading an earlier chat: the page's own send is refused, and so is this.
    const std::string first = harness.persistence.document().active_conversation_id();
    harness.deliver("create_conversation", json::object());
    harness.deliver("switch_conversation", json{{"conversationId", first}});
    const std::size_t before = user_messages(harness);
    CHECK(harness.host.submit_issue_message("issue-c", "Help fix this issue.", issue_context(), &refusal) ==
          AgentHost::SubmitResult::Refused);
    CHECK(refusal == "inactive_conversation");
    CHECK(user_messages(harness) == before);
}

TEST_CASE("an earlier issue message keeps its context beside its words", "[agent][print-issues]")
{
    auto agent = std::make_unique<RecordingAgent>();
    RecordingAgent* recorded = agent.get();
    Harness harness(std::move(agent));
    harness.handshake();
    REQUIRE(harness.host.submit_issue_message("issue-1", "Explain this issue and my options.", issue_context()) ==
            AgentHost::SubmitResult::Started);
    harness.pump_all();
    harness.send_user_message("ok, fix it", "c-2");
    harness.pump_all();

    const AgentRequest* reply = nullptr;
    for (const AgentRequest& request : recorded->requests)
        if (request.purpose == AgentRequest::Purpose::Reply)
            reply = &request;
    REQUIRE(reply != nullptr);
    CHECK(reply->user_text == "ok, fix it");
    CHECK(reply->issue_context.empty());
    // The chip's message went with its context, and goes again as it went.
    const AgentRequest& chip = recorded->requests.front();
    CHECK(chip.user_text == "Explain this issue and my options.");
    CHECK(chip.issue_context == issue_context());
    REQUIRE_FALSE(reply->record.items.empty());
    const RecordItem& asked = reply->record.items.front();
    CHECK(asked.source == chip.source);
    REQUIRE(asked.parts.size() == 2);
    CHECK(asked.parts[0].text == "Explain this issue and my options.");
    CHECK(asked.parts[1].text == "<print_issue>\n" + issue_context() + "\n</print_issue>");
}

TEST_CASE("a turn carries the chat's intent and plan when the project has them", "[agent][workspace]")
{
    auto agent = std::make_unique<RecordingAgent>();
    RecordingAgent* recorded = agent.get();
    Harness harness(std::move(agent));
    harness.handshake();
    const auto last_reply = [&]() -> const AgentRequest& {
        const AgentRequest* reply = nullptr;
        for (const AgentRequest& request : recorded->requests)
            if (request.purpose == AgentRequest::Purpose::Reply)
                reply = &request;
        REQUIRE(reply != nullptr);
        return *reply;
    };

    harness.send_user_message("what is this?", "c-1");
    harness.pump_all();
    CHECK(last_reply().setup_title.empty());
    CHECK(last_reply().plan.headline.empty());

    ProjectStateDocument& document = harness.persistence.document();
    REQUIRE(document.set_setup_intent(document.active_conversation_id(), "Strong PETG shelf bracket"));
    PlanRecord plan;
    plan.headline    = "PETG, 4 walls, printed on its side";
    plan.assumptions = {"The shelf is under 10 kg."};
    document.set_plan(plan, "2026-10-15T10:01:00Z");

    harness.send_user_message("go ahead", "c-2");
    harness.pump_all();
    const AgentRequest& request = last_reply();
    CHECK(request.setup_title == "Strong PETG shelf bracket");
    CHECK(request.plan.headline == "PETG, 4 walls, printed on its side");
    CHECK(request.plan.assumptions == std::vector<std::string>{"The shelf is under 10 kg."});
}

// -- Work the application runs outside the chat ------------------------------

namespace {

json last_work(Harness& harness)
{
    const json* envelope = harness.last_of_type("work_updated");
    return envelope != nullptr ? (*envelope)["payload"]["work"] : json();
}

json state_work(Harness& harness)
{
    harness.deliver("state_request", json::object());
    return (*harness.last_of_type("state"))["payload"]["work"];
}

} // namespace

TEST_CASE("work the app runs is saved where it started and completed in place", "[agent][work]")
{
    Harness harness{std::make_unique<RecordingAgent>()};
    harness.handshake();
    harness.deliver("user_message", json{{"clientMessageId", "before"}, {"text", "Make it stronger."}});
    harness.pump_all();
    const std::string conversation = harness.persistence.document().active_conversation_id();
    const std::string last_before  = harness.persistence.document().last_item_id(conversation);
    REQUIRE_FALSE(last_before.empty());

    const std::string id = harness.host.begin_work("slice", "Slicing plate 1", true);
    json work = last_work(harness);
    CHECK(work["id"] == id);
    CHECK(work["state"] == "running");
    CHECK(work["kind"] == "slice");
    CHECK(work["title"] == "Slicing plate 1");
    CHECK(work["afterId"] == last_before);
    CHECK(work["cancellable"] == true);
    CHECK(work["progress"]["total"] == 0);

    harness.host.update_work(id, 40, 100, "Generating supports");
    work = last_work(harness);
    CHECK(work["progress"]["current"] == 40);
    CHECK(work["progress"]["total"] == 100);
    CHECK(work["detail"] == "Generating supports");

    // A message that arrives while it runs reads below it: the entry keeps
    // the place it started at.
    harness.deliver("user_message", json{{"clientMessageId", "during"}, {"text", "And lighter."}});
    harness.pump_all();
    CHECK(harness.persistence.document().last_item_id(conversation) != last_before);

    harness.host.finish_work(id, "done");
    work = last_work(harness);
    CHECK(work["state"] == "done");
    CHECK(work["afterId"] == last_before);
    CHECK_FALSE(work["endedAt"].get<std::string>().empty());
    CHECK(work["cancellable"] == false);
    CHECK_FALSE(work.contains("progress"));
    // The words of the last step are not the outcome.
    CHECK(work["detail"] == "");

    const json saved = state_work(harness);
    REQUIRE(saved.size() == 1);
    CHECK(saved[0]["state"] == "done");
    CHECK(saved[0]["seq"] == work["seq"]);
}

TEST_CASE("the page's cancel reaches the app only while the work can be stopped", "[agent][work]")
{
    Harness harness{std::make_unique<RecordingAgent>()};
    harness.handshake();
    std::vector<std::string> cancelled;
    harness.host.set_work_cancel_handler([&cancelled](const std::string& id) { cancelled.push_back(id); });

    const std::string arrange = harness.host.begin_work("job", "Arranging...", true);
    const std::string export_ = harness.host.begin_work("export", "Exporting.", false);
    harness.deliver("work_cancel", json{{"id", export_}});
    CHECK(cancelled.empty());
    harness.deliver("work_cancel", json{{"id", arrange}});
    REQUIRE(cancelled.size() == 1);
    CHECK(cancelled.front() == arrange);

    // The app says how it ended; a second press after that changes nothing.
    harness.host.finish_work(arrange, "cancelled");
    CHECK(last_work(harness)["state"] == "cancelled");
    harness.deliver("work_cancel", json{{"id", arrange}});
    CHECK(cancelled.size() == 1);
    CHECK(last_work(harness)["state"] == "cancelled");

    harness.host.finish_work(export_, "failed", "The disk is full.");
    CHECK(last_work(harness)["state"] == "failed");
    CHECK(last_work(harness)["detail"] == "The disk is full.");
    // Finishing twice, or something that never started, is ignored.
    harness.host.finish_work(export_, "done");
    harness.host.finish_work("work-999", "done");
    CHECK(last_work(harness)["state"] == "failed");
}

TEST_CASE("work cut off when its project was closed reads as interrupted", "[agent][work]")
{
    Harness harness{std::make_unique<RecordingAgent>()};
    harness.handshake();
    // Saved as running by a session that is gone: nothing here stands behind it.
    WorkEntry entry;
    entry.kind  = "slice";
    entry.title = "Slicing plate 1";
    harness.persistence.document().begin_work(entry, "2026-10-13T10:00:00Z");

    const json saved = state_work(harness);
    REQUIRE(saved.size() == 1);
    CHECK(saved[0]["state"] == "interrupted");
    CHECK(saved[0]["cancellable"] == false);
    CHECK_FALSE(saved[0].contains("progress"));
}

TEST_CASE("the work log survives a save", "[agent][work][persistence]")
{
    ProjectStateDocument written;
    WorkEntry entry;
    entry.kind  = "export";
    entry.title = "Exporting plate_1.gcode.3mf";
    const WorkEntry started = written.begin_work(entry, "2026-10-13T10:00:00Z");
    REQUIRE(written.finish_work(started.id, "done", "", "2026-10-13T10:00:05Z").has_value());
    CHECK_FALSE(written.finish_work(started.id, "failed", "", "2026-10-13T10:00:06Z").has_value());

    ProjectStateDocument read;
    read.load(written.dump());
    REQUIRE(read.work().size() == 1);
    CHECK(read.work()[0].id == started.id);
    CHECK(read.work()[0].state == "done");
    CHECK(read.work()[0].ended_at == "2026-10-13T10:00:05Z");
}

// --- Project skills ----------------------------------------------------------

TEST_CASE("a turn the app starts before the page has sent the skills waits for them", "[agent][skills][file-report]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake(/*with_instructions=*/true, /*with_skills=*/false);
    harness.host.on_file_loaded(prusa_open());
    REQUIRE(file_report_notes(harness) == 1);
    CHECK(agent->requests.empty());
    CHECK(harness.last_of_type("assistant_failed") == nullptr);

    harness.deliver("skills", test_skills());
    REQUIRE(agent->requests.size() == 1);
    REQUIRE(agent->requests.front().skills.size() == 1);
    CHECK(agent->requests.front().skills.front().text.rfind(kTestFileLoadedSkill, 0) == 0);
}

TEST_CASE("the skills message is taken whole or refused whole", "[agent][skills]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();

    const json skill{{"name", "file-loaded"}, {"trigger", "file_loaded"}, {"text", "Other words."}};
    const auto with = [&skill](const char* key, json value) {
        json changed = skill;
        changed[key] = std::move(value);
        return json{{"skills", json::array({changed})}};
    };
    json missing_trigger = skill;
    missing_trigger.erase("trigger");
    json described = skill;
    described["when"] = "When a file is loaded.";
    json oversized{{"skills", json::array()}};
    for (char letter = 'a'; letter <= 'r'; ++letter)
        oversized["skills"].push_back(json{{"name", std::string("skill-") + letter},
                                           {"trigger", std::string("event_") + letter},
                                           {"text", std::string(15 * 1024, letter)}});
    const std::vector<json> refused{
        json::array({skill}),
        json{{"skills", skill}},
        json{{"skills", json::array({skill})}, {"index", true}},
        json{{"skills", json::array({missing_trigger})}},
        json{{"skills", json::array({described})}},  // a key the protocol does not have
        with("name", "Model_Opened"),
        with("name", ""),
        with("trigger", "file-loaded"),
        with("trigger", 7),
        with("text", ""),
        with("text", std::string(16 * 1024 + 1, 'x')),
        json{{"skills", json::array({json{{"name", "b-skill"}, {"trigger", "b_event"}, {"text", "B."}},
                                     json{{"name", "a-skill"}, {"trigger", "a_event"}, {"text", "A."}}})}},
        json{{"skills", json::array({json{{"name", "a-skill"}, {"trigger", "file_loaded"}, {"text", "A."}},
                                     json{{"name", "b-skill"}, {"trigger", "file_loaded"}, {"text", "B."}}})}},
        oversized,
    };
    for (std::size_t index = 0; index < refused.size(); ++index) {
        INFO("payload " << index << ": " << refused[index].dump().substr(0, 200));
        const std::size_t errors = harness.of_type("bridge_error").size();
        harness.deliver("skills", refused[index]);
        REQUIRE(harness.of_type("bridge_error").size() == errors + 1);
        CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "invalid_payload");
    }

    // None of them replaced the skills received with the handshake.
    harness.host.on_file_loaded(prusa_open());
    REQUIRE_FALSE(agent->requests.empty());
    REQUIRE(agent->requests.back().skills.size() == 1);
    CHECK(agent->requests.back().skills.front().text.rfind(kTestFileLoadedSkill, 0) == 0);
    harness.pump_all();

    // A valid message replaces them, with any number of skills for other events.
    harness.deliver("skills", json{{"skills", json::array({json{{"name", "a-later-event"}, {"trigger", "later_event"},
                                                                {"text", "Not this one."}},
                                                           skill})}});
    harness.host.on_file_loaded(prusa_open());
    harness.pump_all();
    std::vector<AgentRequest> replies;
    for (const AgentRequest& request : agent->requests)
        if (request.purpose == AgentRequest::Purpose::Reply)
            replies.push_back(request);
    REQUIRE(replies.size() == 2);
    REQUIRE(replies.back().skills.size() == 1);
    CHECK(replies.back().skills.front().text.rfind("Other words.", 0) == 0);
}

TEST_CASE("the file-loaded skill pictures only a plate with something on it, and adds the purpose sentence "
          "only when none is recorded",
          "[agent][skills][file-report]")
{
    SECTION("an empty plate gets the sentence without the picture")
    {
        Workspace::WorkspaceSnapshot snapshot = two_object_snapshot();
        for (auto& plate : snapshot.plates)
            plate.objects.clear();
        auto agent_owner = std::make_unique<RecordingAgent>();
        RecordingAgent* agent = agent_owner.get();
        Harness harness{snapshot};
        harness.host.set_agent(AgentServicePtr(std::move(agent_owner)), AgentAvailability::Ready);
        harness.handshake();
        harness.host.on_file_loaded(prusa_open());
        REQUIRE_FALSE(agent->requests.empty());
        REQUIRE(agent->requests.back().skills.size() == 1);
        CHECK(agent->requests.back().skills.front().picture == nullptr);
        CHECK(file_loaded_text_adds_purpose_sentence(agent->requests.back().skills.front().text));
    }
    SECTION("a setup title recorded for the conversation leaves the sentence out")
    {
        auto agent_owner = std::make_unique<RecordingAgent>();
        RecordingAgent* agent = agent_owner.get();
        Harness harness{AgentServicePtr(std::move(agent_owner))};
        harness.handshake();
        ProjectStateDocument& document = harness.persistence.document();
        REQUIRE(document.set_setup_intent(document.active_conversation_id(), "A vase for the kitchen"));
        harness.host.on_file_loaded(prusa_open());
        REQUIRE_FALSE(agent->requests.empty());
        REQUIRE(agent->requests.back().skills.size() == 1);
        CHECK(agent->requests.back().skills.front().picture != nullptr);
        CHECK(agent->requests.back().skills.front().text == kTestFileLoadedSkill);
    }
}

namespace {

// Calls one tool on its first turn and records what the host hands back,
// keeping what it sent in the chat's record.
class OneCallAgent final : public IAgentService
{
public:
    std::function<ToolRequest(const AgentRequest&)> call;

    bool ready() const override { return true; }
    bool busy() const override { return active; }
    bool start(const AgentRequest& request) override
    {
        requests.push_back(request);
        active = true;
        turn   = request;
        std::vector<RecordItem> sent = JusPrinTest::kept_person_side(request);
        if (request.purpose == AgentRequest::Purpose::Reply && call && !called) {
            called = true;
            ToolRequest tool = call(request);
            sent.push_back(JusPrinTest::kept_call("call-load", tool.tool, tool.arguments_json));
            events.push_back(AgentEvent::tool_call({"call-load", std::move(tool), true}));
        } else {
            sent.push_back(JusPrinTest::kept_reply("Done."));
            events.push_back(AgentEvent::delta("Done."));
            events.push_back(AgentEvent::completed());
        }
        if (request.purpose == AgentRequest::Purpose::Reply)
            JusPrinTest::keep(request, std::move(sent));
        return true;
    }
    bool continue_after_tool(const AgentToolResult& result) override
    {
        results.push_back(result);
        // Optionally one more call before the reply.
        if (then_call && !then_called) {
            then_called = true;
            JusPrinTest::keep(turn, {JusPrinTest::kept_result(result),
                                     JusPrinTest::kept_call("call-then", then_call->tool, then_call->arguments_json)});
            events.push_back(AgentEvent::tool_call({"call-then", *then_call, true}));
            return true;
        }
        JusPrinTest::keep(turn, {JusPrinTest::kept_result(result), JusPrinTest::kept_reply("It is in.")});
        events.push_back(AgentEvent::delta("It is in."));
        events.push_back(AgentEvent::completed());
        return true;
    }
    void cancel() override { active = false; events.clear(); }
    std::optional<AgentEvent> poll() override
    {
        if (events.empty())
            return std::nullopt;
        AgentEvent event = std::move(events.front());
        events.pop_front();
        if (event.kind == AgentEventKind::Completed || event.kind == AgentEventKind::Failed)
            active = false;
        return event;
    }

    std::vector<AgentRequest>    requests;
    std::vector<AgentToolResult> results;
    std::deque<AgentEvent>       events;
    AgentRequest                 turn;
    bool                         active{false};
    bool                         called{false};
    std::optional<ToolRequest>   then_call;
    bool                         then_called{false};
};

// A model file in a folder of its own, removed with it even when a REQUIRE
// fails: two runs at once (two worktrees on one machine) never share it.
struct ModelFile
{
    JusPrinTest::McpDirectory directory;
    std::string               path;

    explicit ModelFile(const std::string& name) : path((directory.root / name).u8string())
    {
        std::ofstream(directory.root / name, std::ios::binary) << "solid part\nendsolid part\n";
    }
};

} // namespace

TEST_CASE("a model a tool call brings in fires the skill on the result that carries its report, and nowhere else",
          "[agent][skills][tools]")
{
    const ModelFile model_file("jusprin-skill-import.stl");
    const std::string model = model_file.path;
    // The id of the model the person attached, once attached.
    std::string attachment_id;
    struct Source
    {
        const char* tool;
        std::function<json(const AgentRequest&)> arguments;
        bool attached;  // the person attached the model to the message
    };
    const Source sources[] = {
        {"object_import_file",
         [model](const AgentRequest& request) {
             return json{{"sessionId", std::to_string(request.workspace.session.value())}, {"path", model}};
         },
         false},
        {"object_import",
         [&attachment_id](const AgentRequest& request) {
             return json{{"sessionId", std::to_string(request.workspace.session.value())}, {"attachmentId", attachment_id}};
         },
         true},
    };
    for (const Source& source : sources) {
        DYNAMIC_SECTION(source.tool)
        {
            auto agent_owner = std::make_unique<OneCallAgent>();
            OneCallAgent* agent = agent_owner.get();
            agent->call = [&source](const AgentRequest& request) {
                return ToolRequest{source.tool, source.arguments(request).dump()};
            };
            Harness harness{AgentServicePtr(std::move(agent_owner))};
            harness.handshake();
            if (source.attached) {
                const json attached = attach(harness, "ca-part", "part.stl", "solid s\nendsolid s\n");
                REQUIRE(attached["kind"] == "model");
                attachment_id = attached["id"].get<std::string>();
                harness.deliver("user_message", json{{"clientMessageId", "c-import"}, {"text", "Add the part file too."},
                                                     {"attachmentIds", json::array({attachment_id})}});
            } else
                harness.send_user_message("Add the part file too.", "c-import");
            // The person asked; the request carries no skill yet.
            REQUIRE(agent->requests.size() == 1);
            CHECK(agent->requests.front().skills.empty());
            harness.pump_all();
            pump_tools_to_completion(harness);
            harness.pump_all();

            REQUIRE(agent->results.size() == 1);
            const AgentToolResult& result = agent->results.front();
            CHECK(result.state == "succeeded");
            CHECK(json::parse(result.output_json)["result"].contains("report"));
            REQUIRE(result.skills.size() == 1);
            CHECK(result.skills.front().name == "file-loaded");
            CHECK(file_loaded_text_adds_purpose_sentence(result.skills.front().text));
            CHECK(result.skills.front().picture != nullptr);
            // The page never shows the skill, and the next turn fires none.
            for (const json& envelope : harness.sent)
                CHECK(envelope.dump().find(kTestFileLoadedSkill) == std::string::npos);
            harness.send_user_message("Thanks.", "c-after");
            harness.pump_all();
            const auto reply = std::find_if(agent->requests.rbegin(), agent->requests.rend(), [](const AgentRequest& request) {
                return request.purpose == AgentRequest::Purpose::Reply;
            });
            REQUIRE(reply != agent->requests.rend());
            const AgentRequest& later = *reply;
            CHECK(later.user_text == "Thanks.");
            CHECK(later.skills.empty());
        }
    }
}

// Opening a project replaces the conversation's document with the opened
// project's, which ends the turn that asked for it. The opened project's own
// chat then hears the load report, as it does when the person opens a file.
TEST_CASE("a project the Agent opens is spoken about in the opened project's chat", "[agent][skills][tools]")
{
    const ModelFile model_file("jusprin-skill-opened.stl");
    const std::string model = model_file.path;
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    agent->call = [&model](const AgentRequest&) { return ToolRequest{"project_open", json{{"path", model}}.dump()}; };
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    harness.send_user_message("Open the part file.", "c-open");
    const std::string asking_project = harness.persistence.document().project_id();
    harness.pump_all();
    pump_tools_to_completion(harness);
    REQUIRE(harness.workspace.opens == 1);
    // The turn that asked ended with the project it was in.
    CHECK(agent->results.empty());
    CHECK(harness.persistence.document().project_id() != asking_project);
    CHECK(file_report_notes(harness) == 0);

    // The next tick, with the opened project's document in place.
    harness.host.pump_tools();
    REQUIRE(file_report_notes(harness) == 1);
    const json note = (*harness.last_of_type("message_added"))["payload"]["message"];
    CHECK(note["fileReport"]["startedBy"] == "agent");
    CHECK(note["fileReport"]["projectOpened"] == true);
    CHECK(harness.host.conversation().size() >= 1);
    CHECK(harness.host.conversation().front().id == note["id"]);
    harness.pump_all();
    const auto reply = std::find_if(agent->requests.rbegin(), agent->requests.rend(), [](const AgentRequest& request) {
        return request.purpose == AgentRequest::Purpose::Reply;
    });
    REQUIRE(reply != agent->requests.rend());
    CHECK(reply->user_text.find("JusPrin file-load report") == 0);
    CHECK(reply->user_text.find("jusprin-skill-opened.stl") != std::string::npos);
    // Nothing of the chat that asked comes with it.
    CHECK(reply->record.items.empty());
    CHECK(reply->unsent.empty());
    REQUIRE(reply->skills.size() == 1);
    CHECK(file_loaded_text_adds_purpose_sentence(reply->skills.front().text));
    CHECK(reply->skills.front().picture != nullptr);
}

// The calibration skill as the page sends it, fired by the handover.
json skills_with_calibration()
{
    // Sorted by name, as the page sends them.
    json  skills = test_skills();
    json& list   = skills["skills"];
    const auto at = std::find_if(list.begin(), list.end(), [](const json& skill) { return skill["name"] > "calibration"; });
    list.insert(at, json{{"name", "calibration"}, {"trigger", "calibration_started"},
                         {"description", "Read to calibrate a filament."}, {"text", "Calibration skill text."}});
    return skills;
}

void set_fixture_presets(Workspace::FakeWorkspace& workspace)
{
    Workspace::WorkspaceSetup setup = workspace.snapshot().setup;
    setup.filament_preset = "Fixture PLA";
    setup.printer_preset  = "Fixture printer";
    workspace.set_setup(setup);
}

// The symptom path (board 2): the Agent calls calibration_generate from the
// person's own project. That chat keeps the last word, as an open's does;
// the calibration project opens with the handover line naming the symptom,
// and its first turn starts once the test is made, with the skill in force.
TEST_CASE("a test made from the person's own project opens its calibration project with the handover", "[agent][tools][calibration]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    agent->call = [](const AgentRequest&) {
        return ToolRequest{"calibration_generate",
                           json{{"test", "temperature"}, {"start", 250}, {"end", 230}, {"symptom", "hairs between the posts"}}.dump()};
    };
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    set_fixture_presets(harness.workspace);
    harness.handshake();
    harness.deliver("skills", skills_with_calibration());
    const std::string started_from = harness.workspace.snapshot().setup.project_name;
    harness.send_user_message("Start the temperature test", "c-calibrate");
    const std::string asking_project = harness.persistence.document().project_id();
    harness.pump_all();
    pump_tools_to_completion(harness);
    REQUIRE(harness.workspace.calibrations == 1);
    CHECK(agent->results.empty());
    CHECK(harness.persistence.document().project_id() != asking_project);
    CHECK(harness.workspace.snapshot().setup.project_name == "Calibration · Fixture PLA · Fixture printer");

    const auto conversation = harness.host.conversation();
    REQUIRE_FALSE(conversation.empty());
    const ConversationMessage& note = conversation.front();
    CHECK(note.role == MessageRole::Note);
    CHECK(note.text == "From " + started_from + " · Fixture PLA on Fixture printer · temporary test settings · hairs between the posts");
    // The chat takes the project's one name.
    const auto& document = harness.persistence.document();
    const auto  chats    = document.conversations();
    const auto  chat     = std::find_if(chats.begin(), chats.end(),
                                        [&](const ConversationInfo& info) { return info.id == document.active_conversation_id(); });
    REQUIRE(chat != chats.end());
    CHECK(chat->title == "Calibration · Fixture PLA · Fixture printer");
    // The project is marked, and keeps the test for when it is reopened.
    const auto record = document.calibration();
    REQUIRE(record.has_value());
    CHECK((*record)["filament"] == "Fixture PLA");
    CHECK((*record)["printer"] == "Fixture printer");
    CHECK((*record)["symptom"] == "hairs between the posts");
    CHECK((*record)["handover"] == note.id);
    CHECK((*record)["test"] == "temperature");
    CHECK((*record)["start"] == 250.);
    CHECK((*record)["end"] == 230.);

    harness.pump_all();
    const auto found = std::find_if(agent->requests.rbegin(), agent->requests.rend(),
                                    [](const AgentRequest& request) { return request.purpose == AgentRequest::Purpose::Reply; });
    REQUIRE(found != agent->requests.rend());
    const AgentRequest& reply = *found;
    CHECK(reply.user_text.empty());
    CHECK(reply.source == note.id);
    CHECK(reply.note.rfind("This is a calibration project, opened from " + started_from +
                               " to calibrate Fixture PLA on Fixture printer.", 0) == 0);
    CHECK(reply.note.find("hairs between the posts") != std::string::npos);
    REQUIRE(reply.skills.size() == 1);
    CHECK(reply.skills.front().trigger == "calibration_started");
    CHECK(reply.unsent.empty());
}

// The popup path (boards 3 and 4): the app opens the calibration project and
// its chat; the Agent's generate continues that same project, so its result
// comes back to the turn that asked and stays in this chat.
TEST_CASE("a test made in a calibration project continues the project and its chat", "[agent][tools][calibration]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    agent->call = [](const AgentRequest&) {
        return ToolRequest{"calibration_generate", json{{"test", "temperature"}, {"start", 230}, {"end", 190}}.dump()};
    };
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    set_fixture_presets(harness.workspace);
    harness.handshake();
    harness.deliver("skills", skills_with_calibration());
    // What the shell does on Start.
    Workspace::ProjectOpenRequest open;
    open.new_project = true;
    open.name        = calibration_project_name("Fixture PLA", "Fixture printer");
    Workspace::LoadReport report;
    REQUIRE(harness.workspace.open_project(open, report).succeeded());
    harness.host.open_calibration_chat({"Backpack hook", "Fixture PLA", "Fixture printer", {}, {}}, /*start=*/true);
    const std::string project = harness.persistence.document().project_id();
    const std::string chat_id = harness.persistence.document().active_conversation_id();
    const json        line    = (*harness.last_of_type("message_added"))["payload"]["message"];
    CHECK(line["text"] == "From Backpack hook · Fixture PLA on Fixture printer · temporary test settings");

    harness.pump_all();
    pump_tools_to_completion(harness);
    harness.pump_all();
    REQUIRE(harness.workspace.calibrations == 1);
    // One project and one chat, before and after; the name is kept.
    CHECK(harness.persistence.document().project_id() == project);
    CHECK(harness.persistence.document().active_conversation_id() == chat_id);
    CHECK(harness.workspace.snapshot().setup.project_name == "Calibration · Fixture PLA · Fixture printer");
    // The result reached the turn that asked, and the call is in this chat.
    REQUIRE(agent->results.size() == 1);
    CHECK(agent->results.front().state == "succeeded");
    const auto activities = harness.persistence.document().activities();
    CHECK(std::any_of(activities.begin(), activities.end(),
                      [](const ToolActivity& activity) { return activity.tool == "calibration_generate"; }));
    const auto conversation = harness.host.conversation();
    REQUIRE(conversation.size() >= 2);
    CHECK(conversation.front().id == line["id"]);
    CHECK(conversation.back().role == MessageRole::Assistant);
    const auto record = harness.persistence.document().calibration();
    REQUIRE(record.has_value());
    CHECK((*record)["test"] == "temperature");
    CHECK((*record)["startedFrom"] == "Backpack hook");
}

// From a Home card the printer is known and the spool is the person's to say:
// the project is named for the printer, and the Agent is told which spools
// the printer reports loaded, to offer them.
TEST_CASE("a calibration project from a Home card hands over the printer and its loaded spools", "[agent][calibration]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    harness.deliver("skills", skills_with_calibration());
    harness.host.open_calibration_chat({"Home", {}, "Workshop X1C", {}, {"PLA #FFFFFF", "PETG #808080"}}, /*start=*/true);
    const auto conversation = harness.host.conversation();
    REQUIRE_FALSE(conversation.empty());
    CHECK(conversation.front().text == "From Home · Workshop X1C · filament not chosen · temporary test settings");
    const auto& document = harness.persistence.document();
    const auto  chats    = document.conversations();
    const auto  chat     = std::find_if(chats.begin(), chats.end(),
                                        [&](const ConversationInfo& info) { return info.id == document.active_conversation_id(); });
    REQUIRE(chat != chats.end());
    CHECK(chat->title == "Calibration · Workshop X1C");
    harness.pump_all();
    const auto found = std::find_if(agent->requests.rbegin(), agent->requests.rend(),
                                    [](const AgentRequest& request) { return request.purpose == AgentRequest::Purpose::Reply; });
    REQUIRE(found != agent->requests.rend());
    const AgentRequest& reply = *found;
    CHECK(reply.note.find("which filament is not known yet. The printer reports these spools loaded: PLA #FFFFFF, PETG #808080.") !=
          std::string::npos);
    REQUIRE(reply.skills.size() == 1);
    CHECK(reply.skills.front().trigger == "calibration_started");
}

// Found driving the real app over MCP (2026-10-21): with earlier calls in the
// activity list, the host's clear() on the new project's document moved the
// generate call's record, and reading its arguments afterwards read freed
// memory. The result must come from what the call was asked.
TEST_CASE("a calibration test an MCP client makes after other calls reports what it made", "[agent][tools][calibration][mcp]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    for (int index = 0; index < 4; ++index)
        harness.host.tools().submit({"workspace_inspect", "{}"}, "mcp-read-" + std::to_string(index), ToolExecutionPacing{}, ToolSource::Mcp);
    pump_tools_to_completion(harness);
    std::optional<ToolActivity> last;
    const std::string id = harness.host.tools()
                               .submit({"calibration_generate", json{{"test", "temperature"}, {"start", 250}, {"end", 230}}.dump()},
                                       "mcp-calibrate", ToolExecutionPacing{}, ToolSource::Mcp)
                               .action_id;
    ToolActivitySubscription subscription = harness.host.tools().subscribe([&](const ToolActivity& activity) {
        if (activity.action_id == id && tool_state_terminal(activity.state)) last = activity;
    });
    pump_tools_to_completion(harness);
    harness.host.pump_tools();
    REQUIRE(last.has_value());
    REQUIRE(last->state == ToolState::Succeeded);
    const json result = json::parse(last->result_json);
    CHECK(result["test"] == "temperature");
    CHECK(result["applied"]["start"] == 250.);
    CHECK(harness.workspace.calibrations == 1);
    CHECK(agent->requests.empty());
}

TEST_CASE("a project an MCP client opens is not spoken about", "[agent][skills][tools][mcp]")
{
    const ModelFile model_file("jusprin-skill-mcp-opened.stl");
    const std::string model = model_file.path;
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const ToolActivity opening = harness.host.tools().submit({"project_open", json{{"path", model}}.dump()}, "mcp-open",
                                                             ToolExecutionPacing{}, ToolSource::Mcp);
    REQUIRE(opening.source == ToolSource::Mcp);
    pump_tools_to_completion(harness);
    harness.host.pump_tools();
    harness.pump_all();
    CHECK(harness.workspace.opens == 1);
    CHECK(file_report_notes(harness) == 0);
    CHECK(agent->requests.empty());
}

TEST_CASE("the Agent continuing an earlier project is not spoken about", "[agent][skills][tools]")
{
    const ModelFile model_file("jusprin-skill-earlier.stl");
    const std::string model = model_file.path;
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    agent->call = [&model](const AgentRequest&) {
        return ToolRequest{"project_open", json{{"path", model}, {"openedBefore", "existing"}}.dump()};
    };
    std::vector<std::string> continued;
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.host.tools().set_earlier_projects(
        [](const std::string&) {
            return std::optional<ToolExecutionCoordinator::EarlierProject>(
                ToolExecutionCoordinator::EarlierProject{"p-earlier", "Earlier", false, false});
        },
        [&continued](const std::string& id) {
            continued.push_back(id);
            return true;
        },
        {});
    harness.handshake();
    harness.send_user_message("Continue the part I opened before.", "c-earlier");
    harness.pump_all();
    pump_tools_to_completion(harness);
    harness.host.pump_tools();
    harness.pump_all();
    // The call took the continue path: the earlier project was reopened,
    // and OrcaSlicer opened no file.
    CHECK(continued == std::vector<std::string>{"p-earlier"});
    CHECK(harness.workspace.opens == 0);
    // OrcaSlicer loaded nothing, so there is nothing to report.
    CHECK(file_report_notes(harness) == 0);
}

TEST_CASE("skill_read reads only the skills the page described, and is offered only with some", "[agent][skills][tools]")
{
    const json skills{{"skills", json::array({
        json{{"name", "file-loaded"}, {"trigger", "file_loaded"}, {"text", kTestFileLoadedSkill}},
        json{{"name", "tune-strength"}, {"description", "When a part must carry load."}, {"text", "# Strength\nUse more walls."}},
    })}};
    auto read = [&skills](const std::string& name, bool with_skills) {
        auto agent_owner = std::make_unique<OneCallAgent>();
        OneCallAgent* agent = agent_owner.get();
        agent->call = [name](const AgentRequest&) { return ToolRequest{"skill_read", json{{"name", name}}.dump()}; };
        auto harness = std::make_unique<Harness>(AgentServicePtr(std::move(agent_owner)));
        harness->handshake(/*with_instructions=*/true, /*with_skills=*/false);
        harness->deliver("skills", with_skills ? skills : json{{"skills", json::array()}});
        harness->send_user_message("How do I make it stronger?", "c-read");
        harness->pump_all();
        pump_tools_to_completion(*harness);
        harness->pump_all();
        return std::make_pair(std::move(harness), agent);
    };
    SECTION("a described skill is listed and read")
    {
        auto [harness, agent] = read("tune-strength", true);
        REQUIRE_FALSE(agent->requests.empty());
        const auto& index = agent->requests.front().skill_index;
        REQUIRE(index.size() == 1);
        CHECK(index.front().name == "tune-strength");
        CHECK(index.front().description == "When a part must carry load.");
        CHECK(index.front().text.empty());
        REQUIRE(agent->results.size() == 1);
        CHECK(agent->results.front().state == "succeeded");
        CHECK(json::parse(agent->results.front().output_json)["result"] ==
              json{{"name", "tune-strength"}, {"text", "# Strength\nUse more walls."}});
    }
    SECTION("a skill the app fires on an event is not the model's to read")
    {
        auto [harness, agent] = read("file-loaded", true);
        REQUIRE(agent->results.size() == 1);
        CHECK(agent->results.front().state == "failed");
        const json output = json::parse(agent->results.front().output_json);
        CHECK(output["error"]["code"] == "unknown_skill");
        CHECK(output["error"]["message"].get<std::string>().find("[\"tune-strength\"]") != std::string::npos);
    }
    SECTION("with no described skill the list is empty")
    {
        auto [harness, agent] = read("tune-strength", false);
        REQUIRE_FALSE(agent->requests.empty());
        CHECK(agent->requests.front().skill_index.empty());
    }
}

// The page's own calibration skill, as skillParser.ts reads it, through the
// skills message: listed by its description and read whole.
TEST_CASE("the calibration skill the page ships is listed and read", "[agent][skills][calibration]")
{
    std::ifstream file(std::string(JUSPRIN_SOURCE_DIR) + "/src/slic3r/GUI/JusPrin/AgentUI/src/skills/calibration/SKILL.md",
                       std::ios::binary);
    REQUIRE(file.good());
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    const std::size_t end = text.find("\n---\n", 4);
    REQUIRE((text.rfind("---\n", 0) == 0 && end != std::string::npos));
    const std::string front = text.substr(4, end - 4);
    const std::size_t at = front.find("description: ");
    REQUIRE(at != std::string::npos);
    const std::string description = front.substr(at + 13, front.find('\n', at) - at - 13);
    std::string body = text.substr(end + 5);
    body.erase(0, body.find_first_not_of('\n'));
    body.erase(body.find_last_not_of(" \n") + 1);

    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    agent->call = [](const AgentRequest&) { return ToolRequest{"skill_read", json{{"name", "calibration"}}.dump()}; };
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake(/*with_instructions=*/true, /*with_skills=*/false);
    harness.deliver("skills", json{{"skills", json::array({json{{"name", "calibration"}, {"description", description},
                                                                {"text", body}}})}});
    CHECK(harness.of_type("bridge_error").empty());
    harness.send_user_message("My PETG strings everywhere.", "c-calibrate");
    harness.pump_all();
    pump_tools_to_completion(harness);
    harness.pump_all();
    REQUIRE_FALSE(agent->requests.empty());
    const auto& index = agent->requests.front().skill_index;
    REQUIRE(index.size() == 1);
    CHECK(index.front().description == description);
    REQUIRE(agent->results.size() == 1);
    CHECK(json::parse(agent->results.front().output_json)["result"] == json{{"name", "calibration"}, {"text", body}});
}

TEST_CASE("the skills message takes described skills and refuses bad descriptions", "[agent][skills]")
{
    Harness harness;
    harness.handshake();
    const auto refused = [&harness](const json& skill) {
        const std::size_t errors = harness.of_type("bridge_error").size();
        harness.deliver("skills", json{{"skills", json::array({skill})}});
        return harness.of_type("bridge_error").size() == errors + 1;
    };
    CHECK(refused(json{{"name", "neither"}, {"text", "No trigger, no description."}}));
    CHECK(refused(json{{"name", "two-lines"}, {"description", "One\nTwo"}, {"text", "x"}}));
    CHECK(refused(json{{"name", "long"}, {"description", std::string(301, 'x')}, {"text", "x"}}));
    CHECK(refused(json{{"name", "empty"}, {"description", ""}, {"text", "x"}}));
    CHECK(refused(json{{"name", "empty-trigger"}, {"trigger", ""}, {"description", "Fine."}, {"text", "x"}}));
    CHECK_FALSE(refused(json{{"name", "described"}, {"description", "When useful."}, {"text", "x"}}));
}

namespace {

Workspace::SliceFailure test_bed_refusal()
{
    Workspace::SliceFailure refusal;
    refusal.severity = "blocker";
    refusal.message  = "Plate 1: Cool Plate is not suggested for use printing filament 1 (Bambu PETG HF).";
    return refusal;
}

// The last request the model was asked to reply to.
const AgentRequest& last_reply(const OneCallAgent& agent)
{
    const auto reply = std::find_if(agent.requests.rbegin(), agent.requests.rend(), [](const AgentRequest& request) {
        return request.purpose == AgentRequest::Purpose::Reply;
    });
    REQUIRE(reply != agent.requests.rend());
    return *reply;
}

} // namespace

TEST_CASE("a slice_start OrcaSlicer refuses fires the slice-failed skill on its result, and on no later request",
          "[agent][skills][tools][slicing]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    agent->call = [](const AgentRequest&) { return ToolRequest{"slice_start", "{}"}; };
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.workspace.refusal = test_bed_refusal();
    harness.handshake();
    harness.send_user_message("Slice it.", "c-slice");
    REQUIRE(agent->requests.size() == 1);
    CHECK(agent->requests.front().skills.empty());
    harness.pump_all();
    pump_tools_to_completion(harness);
    harness.pump_all();

    REQUIRE(agent->results.size() == 1);
    const AgentToolResult& result = agent->results.front();
    CHECK(result.state == "succeeded");
    const json output = json::parse(result.output_json);
    CHECK(output["result"]["started"] == false);
    CHECK(output["result"]["reason"]["message"] == test_bed_refusal().message);
    CHECK(output["workspace"]["plates"][0]["sliceFailure"]["kind"] == "refused");
    REQUIRE(result.skills.size() == 1);
    CHECK(result.skills.front().name == "slice-failed");
    CHECK(result.skills.front().text == kTestSliceSkill);
    CHECK(result.skills.front().picture == nullptr);
    for (const json& envelope : harness.sent)
        CHECK(envelope.dump().find(kTestSliceSkill) == std::string::npos);

    // The model has read about it: the next turn still shows the failure in
    // the workspace, and carries no skill.
    harness.send_user_message("Is it ready to print?", "c-ready");
    harness.pump_all();
    const AgentRequest& later = last_reply(*agent);
    CHECK(later.user_text == "Is it ready to print?");
    CHECK(later.skills.empty());
    REQUIRE(later.workspace.plates.at(0).slice_failure);
}

TEST_CASE("a slice the person started that did not succeed fires the skill on the next request, once a failure",
          "[agent][skills][slicing]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const auto ask = [&](const std::string& text, const std::string& id) -> const AgentRequest& {
        harness.send_user_message(text, id);
        harness.pump_all();
        return last_reply(*agent);
    };

    // The person presses Slice, and OrcaSlicer refuses the plate.
    harness.workspace.refusal = test_bed_refusal();
    REQUIRE(harness.workspace.start_slice(std::nullopt, false).succeeded());
    const AgentRequest& first = ask("Is it ready to print?", "c-1");
    REQUIRE(first.skills.size() == 1);
    CHECK(first.skills.front().name == "slice-failed");
    CHECK(first.skills.front().text == kTestSliceSkill);
    CHECK(ask("What should I change?", "c-2").skills.empty());

    // Pressing Slice again with nothing changed is the same refusal.
    REQUIRE(harness.workspace.start_slice(std::nullopt, false).succeeded());
    CHECK(ask("I pressed it again.", "c-3").skills.empty());

    // A run that fails partway is a new failure, told once.
    harness.workspace.refusal.reset();
    REQUIRE(harness.workspace.start_slice(std::nullopt, false).succeeded());
    REQUIRE(harness.workspace.snapshot().slicing.running);
    Workspace::SliceFailure failure;
    failure.severity = "blocker";
    failure.message  = "Slicing failed: the object has no layers.";
    harness.workspace.fail_slice_for_testing(failure);
    const AgentRequest& failed = ask("And now?", "c-4");
    REQUIRE(failed.skills.size() == 1);
    CHECK(failed.skills.front().name == "slice-failed");
    CHECK(ask("Thanks.", "c-5").skills.empty());

    // A slice that succeeds says nothing.
    REQUIRE(harness.workspace.start_slice(std::nullopt, false).succeeded());
    harness.workspace.finish_slice_for_testing(true);
    CHECK(ask("Done?", "c-6").skills.empty());
}

namespace {

// A chat whose model slices the first plate when the person asks.
struct ModelSlice
{
    OneCallAgent*            agent{nullptr};
    std::unique_ptr<Harness> harness;

    ModelSlice()
    {
        auto owner = std::make_unique<OneCallAgent>();
        agent      = owner.get();
        agent->call = [](const AgentRequest& request) {
            return ToolRequest{"slice_start",
                               json{{"plateId", std::to_string(request.workspace.plates.at(0).id.value())}}.dump()};
        };
        harness = std::make_unique<Harness>(AgentServicePtr(std::move(owner)));
        harness->handshake();
    }

    // The person asks; the model slices, and its turn ends with the run in flight.
    void slice_and_end_turn()
    {
        harness->send_user_message("Slice it.", "c-slice");
        harness->pump_all();
        pump_tools_to_completion(*harness);
        harness->pump_all();
        REQUIRE(harness->workspace.snapshot().slicing.running);
        REQUIRE_FALSE(harness->host.stream_active());
    }

    // A timer tick, then whatever turn it started, to its end.
    void tick()
    {
        harness->host.pump_tools();
        harness->pump_all();
    }

    std::vector<std::string> notes() const
    {
        std::vector<std::string> texts;
        for (const ConversationMessage& message : harness->host.conversation())
            if (message.role == MessageRole::Note)
                texts.push_back(message.text);
        return texts;
    }

    std::vector<const AgentRequest*> app_turns() const
    {
        std::vector<const AgentRequest*> turns;
        for (const AgentRequest& request : agent->requests)
            if (request.purpose == AgentRequest::Purpose::Reply && !request.note.empty())
                turns.push_back(&request);
        return turns;
    }

    std::string plate_name() const { return harness->workspace.snapshot().plates.at(0).name; }
};

} // namespace

TEST_CASE("a slice the model started that ends after its turn starts one app turn", "[agent][slicing]")
{
    ModelSlice chat;
    chat.slice_and_end_turn();
    CHECK(chat.notes().empty());

    SECTION("it sliced")
    {
        chat.harness->workspace.finish_slice_for_testing(true);
        chat.tick();
        REQUIRE(chat.notes() == std::vector<std::string>{"The slice of " + chat.plate_name() + " finished."});
        REQUIRE(chat.app_turns().size() == 1);
        CHECK(chat.app_turns().front()->note == "The slice of " + chat.plate_name() + " finished.");
        CHECK(chat.app_turns().front()->skills.empty());
    }

    SECTION("it failed: the turn carries the slice-failed skill")
    {
        Workspace::SliceFailure failure;
        failure.severity = "blocker";
        failure.message  = "Slicing failed: the object has no layers.";
        chat.harness->workspace.fail_slice_for_testing(failure);
        chat.tick();
        REQUIRE(chat.notes() ==
                std::vector<std::string>{"OrcaSlicer stopped the slice of " + chat.plate_name() + ": " + failure.message});
        REQUIRE(chat.app_turns().size() == 1);
        REQUIRE(chat.app_turns().front()->skills.size() == 1);
        CHECK(chat.app_turns().front()->skills.front().name == "slice-failed");
    }

    // Once a run, whatever the workspace does next.
    chat.harness->workspace.finish_slice_for_testing(true);
    chat.tick();
    CHECK(chat.notes().size() == 1);
    CHECK(chat.app_turns().size() == 1);
}

TEST_CASE("the progress card of the model's slice sits below the call that started it", "[agent][slicing]")
{
    ModelSlice chat;
    chat.slice_and_end_turn();
    // OrcaSlicer's first progress arrives after the model has replied.
    const std::string handle = json::parse(chat.agent->results.front().output_json)["result"]["handle"];
    chat.harness->host.begin_work("slice", "Slicing plate 1", true);
    const std::vector<WorkEntry> work = chat.harness->persistence.document().work();
    REQUIRE(work.size() == 1);
    CHECK(work.back().after_id == handle);
    CHECK(work.back().conversation_id == chat.harness->persistence.document().active_conversation_id());

    // A slice the person starts goes after the chat's last item, as before.
    chat.harness->workspace.finish_slice_for_testing(true);
    chat.tick();
    REQUIRE(chat.harness->workspace.start_slice(std::nullopt, false).succeeded());
    chat.harness->host.begin_work("slice", "Slicing plate 1", true);
    const std::vector<ConversationMessage> messages = chat.harness->host.conversation();
    CHECK(chat.harness->persistence.document().work().back().after_id == messages.back().id);
}

TEST_CASE("a slice that ends during the model's turn is told when the turn ends, unless a tool result showed it",
          "[agent][slicing]")
{
    ModelSlice chat;
    SECTION("the turn ends with a reply: the note follows it")
    {
        chat.harness->send_user_message("Slice it.", "c-slice");
        chat.harness->host.pump_stream();
        pump_tools_to_completion(*chat.harness);
        // The model is answering the slice_start result when the run ends.
        REQUIRE(chat.harness->host.stream_active());
        chat.harness->workspace.finish_slice_for_testing(true);
        chat.harness->host.pump_tools();
        CHECK(chat.notes().empty());
        chat.harness->pump_all();
        chat.tick();
        CHECK(chat.notes().size() == 1);
        CHECK(chat.app_turns().size() == 1);
    }
    SECTION("a later tool result shows the outcome: no note")
    {
        chat.agent->then_call = ToolRequest{"workspace_inspect", "{}"};
        chat.harness->send_user_message("Slice it.", "c-slice");
        chat.harness->host.pump_stream();
        pump_tools_to_completion(*chat.harness);
        REQUIRE(chat.harness->host.stream_active());
        chat.harness->workspace.finish_slice_for_testing(true);
        chat.harness->host.pump_tools();
        chat.harness->pump_all();
        pump_tools_to_completion(*chat.harness);
        chat.harness->pump_all();
        REQUIRE(chat.agent->results.size() == 2);
        CHECK(json::parse(chat.agent->results.back().output_json)["workspace"]["plates"][0]["sliced"] == true);
        chat.tick();
        chat.tick();
        CHECK(chat.notes().empty());
        CHECK(chat.app_turns().empty());
    }
}

TEST_CASE("a slice that ends during another turn is told after it", "[agent][slicing]")
{
    ModelSlice chat;
    chat.slice_and_end_turn();
    chat.harness->send_user_message("Anything else?", "c-other");
    REQUIRE(chat.harness->host.stream_active());
    chat.harness->workspace.finish_slice_for_testing(true);
    chat.harness->host.pump_tools();
    CHECK(chat.notes().size() == 1);
    CHECK(chat.app_turns().empty());
    chat.harness->pump_all();
    REQUIRE(chat.app_turns().size() == 1);
    const auto other = std::find_if(chat.agent->requests.begin(), chat.agent->requests.end(),
                                    [](const AgentRequest& request) { return request.user_text == "Anything else?"; });
    REQUIRE(other != chat.agent->requests.end());
    CHECK(&*other < chat.app_turns().front());
}

TEST_CASE("nothing is told for a slice the person started, one cancelled, or one from a closed project", "[agent][slicing]")
{
    ModelSlice chat;
    SECTION("the person's slice, after the model's was told")
    {
        chat.slice_and_end_turn();
        chat.harness->workspace.finish_slice_for_testing(true);
        chat.tick();
        REQUIRE(chat.notes().size() == 1);
        chat.harness->workspace.set_plate_sliced(chat.harness->workspace.snapshot().plates.at(0).id, false);
        REQUIRE(chat.harness->workspace.start_slice(std::nullopt, false).succeeded());
        chat.harness->workspace.finish_slice_for_testing(true);
        chat.tick();
        CHECK(chat.notes().size() == 1);
        CHECK(chat.app_turns().size() == 1);
        return;
    }
    SECTION("a run activity_cancel stopped")
    {
        chat.slice_and_end_turn();
        const std::string handle = json::parse(chat.agent->results.front().output_json)["result"]["handle"];
        const ToolActivity& cancel = chat.harness->host.tools().submit(
            {"activity_cancel", json{{"handle", handle}}.dump()}, "cancel", {}, ToolSource::Agent, "call-cancel");
        const std::string cancel_id = cancel.action_id;
        pump_tools_to_completion(*chat.harness);
        REQUIRE(json::parse(chat.harness->host.tools().find(cancel_id)->result_json)["cancelled"] == true);
    }
    SECTION("a project replaced meanwhile")
    {
        chat.slice_and_end_turn();
        chat.harness->workspace.replace_project(two_object_snapshot());
    }
    chat.tick();
    chat.tick();
    CHECK(chat.notes().empty());
    CHECK(chat.app_turns().empty());
}

TEST_CASE("a call that brings no model fires no skill", "[agent][skills][tools]")
{
    SECTION("a read")
    {
        auto agent_owner = std::make_unique<OneCallAgent>();
        OneCallAgent* agent = agent_owner.get();
        agent->call = [](const AgentRequest&) { return ToolRequest{"workspace_inspect", "{}"}; };
        Harness harness{AgentServicePtr(std::move(agent_owner))};
        harness.handshake();
        harness.send_user_message("What is on the plate?", "c-read");
        harness.pump_all();
        pump_tools_to_completion(harness);
        harness.pump_all();
        REQUIRE(agent->results.size() == 1);
        CHECK(agent->results.front().skills.empty());
    }
    SECTION("an import of a file that is not there")
    {
        auto agent_owner = std::make_unique<OneCallAgent>();
        OneCallAgent* agent = agent_owner.get();
        // A folder of this run's own, so nothing another run left can be there.
        const JusPrinTest::McpDirectory empty;
        const std::string missing = (empty.root / "no-such-part.stl").u8string();
        agent->call = [missing](const AgentRequest& request) {
            return ToolRequest{"object_import_file",
                               json{{"sessionId", std::to_string(request.workspace.session.value())}, {"path", missing}}.dump()};
        };
        Harness harness{AgentServicePtr(std::move(agent_owner))};
        harness.handshake();
        harness.send_user_message("Add the part.", "c-missing");
        harness.pump_all();
        pump_tools_to_completion(harness);
        harness.pump_all();
        REQUIRE(agent->results.size() == 1);
        CHECK(agent->results.front().state == "failed");
        CHECK(agent->results.front().skills.empty());
    }
}

// -- The print-issues skill --------------------------------------------------

namespace {

Workspace::SliceWarnings test_warnings(std::uint64_t sequence, const char* message = "It seems object Slab has floating cantilever.")
{
    Workspace::SliceWarnings warnings;
    warnings.count       = 2;
    warnings.severity    = "warning";
    warnings.message     = message;
    warnings.setting     = "enable_support";
    warnings.object_name = "Slab";
    warnings.sequence    = sequence;
    return warnings;
}

bool carries_print_issues(const std::vector<AgentSkillUse>& skills)
{
    return std::any_of(skills.begin(), skills.end(), [](const AgentSkillUse& skill) {
        return skill.name == "print-issues" && skill.text == kTestPrintIssuesSkill && skill.picture == nullptr;
    });
}

const AgentRequest& last_reply(const std::vector<AgentRequest>& requests)
{
    const auto reply = std::find_if(requests.rbegin(), requests.rend(), [](const AgentRequest& request) {
        return request.purpose == AgentRequest::Purpose::Reply;
    });
    REQUIRE(reply != requests.rend());
    return *reply;
}

} // namespace

TEST_CASE("a print-issue chip's message carries the print-issues skill, and the next message does not",
          "[agent][skills][print-issues]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();

    REQUIRE(harness.host.submit_issue_message("issue-1", "Help fix this issue.\n\n> Layer height cannot exceed nozzle diameter.",
                                              issue_context()) == AgentHost::SubmitResult::Started);
    const AgentRequest chip = last_reply(agent->requests);
    CHECK(chip.issue_context == issue_context());
    REQUIRE(chip.skills.size() == 1);
    CHECK(carries_print_issues(chip.skills));
    harness.pump_all();

    harness.send_user_message("ok, go ahead", "c-after");
    harness.pump_all();
    const AgentRequest& later = last_reply(agent->requests);
    CHECK(later.user_text == "ok, go ahead");
    CHECK(later.skills.empty());
    // Never shown.
    for (const json& envelope : harness.sent)
        CHECK(envelope.dump().find(kTestPrintIssuesSkill) == std::string::npos);
}

TEST_CASE("an ordinary message on a clean slice carries no print-issues skill", "[agent][skills][print-issues]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const Workspace::PlateId plate = harness.workspace.snapshot().plates.front().id;
    harness.workspace.set_plate_warnings_for_testing(plate, std::nullopt);
    harness.send_user_message("is this good to print?", "c-clean");
    CHECK(last_reply(agent->requests).skills.empty());
    CHECK_FALSE(last_reply(agent->requests).workspace.plates.front().slice_warnings.has_value());
}

TEST_CASE("warnings from the person's slice fire the print-issues skill on the next request only, once per new set",
          "[agent][skills][print-issues]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const Workspace::PlateId plate = harness.workspace.snapshot().plates.front().id;

    // The person pressed Slice and it finished with warnings.
    harness.workspace.set_plate_warnings_for_testing(plate, test_warnings(1));
    harness.send_user_message("is this good to print?", "c-1");
    const AgentRequest first = last_reply(agent->requests);
    REQUIRE(first.skills.size() == 1);
    CHECK(carries_print_issues(first.skills));
    // The fact rides with it, in the request's workspace.
    REQUIRE(first.workspace.plates.front().slice_warnings.has_value());
    CHECK(first.workspace.plates.front().slice_warnings->message == "It seems object Slab has floating cantilever.");
    harness.pump_all();

    harness.send_user_message("and the time?", "c-2");
    CHECK(last_reply(agent->requests).skills.empty());
    CHECK(last_reply(agent->requests).workspace.plates.front().slice_warnings.has_value());
    harness.pump_all();

    // Sliced again with the same warnings: nothing new to be told.
    harness.workspace.set_plate_warnings_for_testing(plate, test_warnings(1));
    harness.send_user_message("sliced again", "c-3");
    CHECK(last_reply(agent->requests).skills.empty());
    harness.pump_all();

    // A slice that reports a warning the last did not: a new set.
    harness.workspace.set_plate_warnings_for_testing(plate, test_warnings(2, "Your model needs support! Please enable support material."));
    harness.send_user_message("and now?", "c-4");
    CHECK(carries_print_issues(last_reply(agent->requests).skills));
    harness.pump_all();
    harness.send_user_message("thanks", "c-5");
    CHECK(last_reply(agent->requests).skills.empty());
}

TEST_CASE("a chip's message with new warnings carries the print-issues skill once", "[agent][skills][print-issues]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    harness.workspace.set_plate_warnings_for_testing(harness.workspace.snapshot().plates.front().id, test_warnings(1));
    REQUIRE(harness.host.submit_issue_message("issue-1", "Explain this issue and my options.", issue_context()) ==
            AgentHost::SubmitResult::Started);
    CHECK(last_reply(agent->requests).skills.size() == 1);
    CHECK(carries_print_issues(last_reply(agent->requests).skills));
    harness.pump_all();
    // The warnings the chip's request carried count as told.
    harness.send_user_message("ok", "c-after");
    CHECK(last_reply(agent->requests).skills.empty());
}

TEST_CASE("warnings from a slice that ends while the model's call runs fire the print-issues skill on that result",
          "[agent][skills][print-issues][tools]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    agent->call = [](const AgentRequest&) { return ToolRequest{"workspace_inspect", json{{"sections", {"slicing"}}}.dump()}; };
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    harness.send_user_message("is it ready?", "c-read");
    REQUIRE(agent->requests.size() == 1);
    CHECK(agent->requests.front().skills.empty());
    harness.pump_all();
    // The slice finishes, with warnings, before the read runs.
    harness.workspace.set_plate_warnings_for_testing(harness.workspace.snapshot().plates.front().id, test_warnings(7));
    pump_tools_to_completion(harness);
    harness.pump_all();
    REQUIRE(agent->results.size() == 1);
    const AgentToolResult& result = agent->results.front();
    CHECK(result.state == "succeeded");
    REQUIRE(result.skills.size() == 1);
    CHECK(carries_print_issues(result.skills));
    const json output = json::parse(result.output_json);
    // The slicing section and the result's workspace both carry the summary.
    CHECK(output["result"]["slicing"]["plates"][0]["sliceWarnings"]["count"] == 2);
    CHECK(output["result"]["slicing"]["plates"][0]["sliceWarnings"]["setting"] == "enable_support");
    CHECK(output["workspace"]["plates"][0]["sliceWarnings"]["message"] == "It seems object Slab has floating cantilever.");

    harness.send_user_message("and now?", "c-after");
    harness.pump_all();
    CHECK(last_reply(agent->requests).skills.empty());
}

TEST_CASE("warnings from the model's own slice that ends after its turn ride on the app's turn",
          "[agent][skills][print-issues][slicing]")
{
    ModelSlice chat;
    chat.harness->workspace.warnings_after_slice = test_warnings(7);
    chat.slice_and_end_turn();
    chat.harness->workspace.finish_slice_for_testing(true);
    chat.tick();
    REQUIRE(chat.app_turns().size() == 1);
    const AgentRequest& turn = *chat.app_turns().front();
    REQUIRE(turn.skills.size() == 1);
    CHECK(carries_print_issues(turn.skills));
    CHECK(turn.workspace.plates.front().slice_warnings.has_value());
}

TEST_CASE("a refused plate and a warned one fire both skills on one request, each once", "[agent][skills][slicing]")
{
    auto agent_owner = std::make_unique<RecordingAgent>();
    RecordingAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    REQUIRE(harness.workspace.add_plate().succeeded());
    const std::vector<Workspace::WorkspacePlate> plates = harness.workspace.snapshot().plates;
    REQUIRE(plates.size() == 2);
    // Slice all: the first plate slices with warnings, OrcaSlicer refuses the second.
    harness.workspace.set_plate_warnings_for_testing(plates[0].id, test_warnings(50));
    harness.workspace.refusal = test_bed_refusal();
    REQUIRE(harness.workspace.start_slice(plates[1].id, false).succeeded());

    harness.send_user_message("is it ready?", "c-both");
    const AgentRequest first = last_reply(agent->requests);
    REQUIRE(first.skills.size() == 2);
    CHECK(first.skills[0].name == "slice-failed");
    CHECK(carries_print_issues(first.skills));
    REQUIRE(first.workspace.plates.size() == 2);
    CHECK(first.workspace.plates[0].slice_warnings.has_value());
    CHECK(first.workspace.plates[1].slice_failure.has_value());
    harness.pump_all();

    harness.send_user_message("and now?", "c-both-after");
    CHECK(last_reply(agent->requests).skills.empty());
}

TEST_CASE("the slice warnings summary, present and absent", "[agent][print-issues][workspace]")
{
    Workspace::WorkspaceSnapshot snapshot = two_object_snapshot();
    Workspace::WorkspacePlate& plate = snapshot.plates.front();
    plate.sliced = true;
    SECTION("absent: no key in the slicing section")
    {
        const json section = slicing_section_result(snapshot, "");
        CHECK_FALSE(section["plates"][0].contains("sliceWarnings"));
    }
    SECTION("present: OrcaSlicer's words, setting, object and the count")
    {
        plate.slice_warnings = test_warnings(3);
        plate.slice_warnings->object  = plate.objects.front().id;
        plate.slice_warnings->ignored = 1;
        const json warnings = slicing_section_result(snapshot, "")["plates"][0]["sliceWarnings"];
        CHECK(warnings == json{{"count", 2}, {"severity", "warning"},
                               {"message", "It seems object Slab has floating cantilever."},
                               {"setting", "enable_support"}, {"objectId", std::to_string(plate.objects.front().id.value())},
                               {"objectName", "Slab"}, {"ignored", 1}});
    }
}

namespace {

// What the page sends for the setup skill; the real one is
// AgentUI/src/skills/project-setup-changed/SKILL.md.
constexpr const char* kTestSetupSkill = "Check the setup against the purpose.";

json skills_with_setup_check()
{
    // Sorted by name, as the page sends them: after print-issues, before slice-failed.
    json skills = test_skills();
    json& list  = skills["skills"];
    list.insert(list.begin() + 2,
                json{{"name", "project-setup-changed"}, {"trigger", "setup_changed"}, {"text", kTestSetupSkill}});
    return skills;
}

// One turn in which the agent makes `request`; returns what the host handed
// back for it.
AgentToolResult run_call(Harness& harness, OneCallAgent& agent, ToolRequest request, const std::string& text)
{
    agent.called = false;
    agent.call   = [request](const AgentRequest&) { return request; };
    const std::size_t before = agent.results.size();
    static int next_client = 0;
    harness.send_user_message(text, "c-setup-" + std::to_string(++next_client));
    harness.pump_all();
    pump_tools_to_completion(harness);
    harness.pump_all();
    REQUIRE(agent.results.size() == before + 1);
    return agent.results.back();
}

// One turn with no call; returns the request the host built for it.
AgentRequest run_reply(Harness& harness, OneCallAgent& agent, const std::string& text)
{
    agent.called = true;
    static int next_client = 0;
    harness.send_user_message(text, "c-reply-" + std::to_string(++next_client));
    harness.pump_all();
    const auto reply = std::find_if(agent.requests.rbegin(), agent.requests.rend(), [](const AgentRequest& request) {
        return request.purpose == AgentRequest::Purpose::Reply;
    });
    REQUIRE(reply != agent.requests.rend());
    CHECK(reply->user_text == text);
    return *reply;
}

ToolRequest setup_call(json arguments) { return ToolRequest{"setup_record", arguments.dump()}; }

const json kPetg{{"purpose", "A strong hook"}};

bool fires_setup_skill(const AgentToolResult& result)
{
    return std::any_of(result.skills.begin(), result.skills.end(),
                       [](const AgentSkillUse& skill) { return skill.trigger == "setup_changed"; });
}

// A printer with two filaments and two plates the agent's printer_setup can
// switch between.
void with_printer_choices(Workspace::FakeWorkspace& workspace)
{
    Workspace::ConfiguredPrinter configured;
    configured.preset     = "Test Printer 0.4 nozzle";
    configured.plate_type = "Textured PEI Plate";
    configured.filaments  = {{"Generic PLA", "PLA"}};
    workspace.set_configured_printer_for_testing(configured);
    workspace.set_setup_for_testing("0.20mm Standard", {"Textured PEI Plate", "Cool Plate"},
                                    {{"Generic PLA", "PLA"}, {"Generic PETG", "PETG"}});
    workspace.set_presets_for_testing(Workspace::PresetKind::Printer, {{"Test Printer 0.4 nozzle"}});
    workspace.set_presets_for_testing(Workspace::PresetKind::Filament, {{"Generic PLA"}, {"Generic PETG"}});
}

// A setup change the person makes in the sidebar: no tool call, no chat.
void hand_setup(Workspace::FakeWorkspace& workspace, std::optional<std::string> filament,
                std::optional<std::string> plate = std::nullopt)
{
    Workspace::PrinterSetupRequest request;
    if (filament)
        request.filament_presets = std::vector<std::string>{*filament};
    request.plate_type = std::move(plate);
    Workspace::PrinterSetupPreview applied;
    REQUIRE(workspace.apply_printer_setup(request, applied).succeeded());
}

} // namespace

TEST_CASE("a purpose recorded for the first time or changed fires the setup skill on that result only",
          "[agent][skills][setup-skill]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    harness.deliver("skills", skills_with_setup_check());

    const AgentToolResult first = run_call(harness, *agent, setup_call(kPetg), "Make it a strong PETG hook.");
    CHECK(json::parse(first.output_json)["result"]["purposeChange"] == "first");
    REQUIRE(first.skills.size() == 1);
    CHECK(first.skills.front().name == "project-setup-changed");
    CHECK(first.skills.front().text == kTestSetupSkill);
    CHECK(first.skills.front().picture == nullptr);
    // The model reads the plan's real state in a result, and not the chat's
    // title, which is its own words.
    CHECK_FALSE(json::parse(first.output_json)["workspace"].contains("setupIntent"));

    // Never stored, so the next request of the chat does not carry it.
    CHECK(run_reply(harness, *agent, "Thanks.").skills.empty());
    for (const json& envelope : harness.sent)
        CHECK(envelope.dump().find(kTestSetupSkill) == std::string::npos);

    // The same purpose again is no new purpose.
    const AgentToolResult repeated = run_call(harness, *agent, setup_call(kPetg), "PETG, yes.");
    CHECK(json::parse(repeated.output_json)["result"]["purposeChange"] == "unchanged");
    CHECK_FALSE(fires_setup_skill(repeated));
    // Nor is a call that restates the purpose to change the plan.
    const AgentToolResult planned = run_call(
        harness, *agent, setup_call({{"purpose", "A strong hook"},
                                     {"headline", "Four walls and gyroid infill for strength"}}), "Plan it.");
    CHECK(json::parse(planned.output_json)["result"]["purposeChange"] == "unchanged");
    CHECK(json::parse(planned.output_json)["result"]["plan"]["headline"] == "Four walls and gyroid infill for strength");
    CHECK_FALSE(fires_setup_skill(planned));

    const AgentToolResult changed = run_call(
        harness, *agent, setup_call({{"purpose", "A hook for a wet bathroom"}}), "It goes in the shower.");
    CHECK(json::parse(changed.output_json)["result"]["purposeChange"] == "changed");
    CHECK(fires_setup_skill(changed));
}

TEST_CASE("the model reads its plan as current after its own changes and in doubt after a person's",
          "[agent][plan]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const auto validity = [](const AgentToolResult& result) {
        return json::parse(result.output_json)["workspace"]["planValidity"].get<std::string>();
    };

    const AgentToolResult planned = run_call(harness, *agent,
        setup_call({{"purpose", "A strong bracket"}, {"headline", "Four walls and gyroid infill for strength"}}),
        "Make it strong.");
    REQUIRE(planned.state == "succeeded");
    CHECK(validity(planned) == "current");
    const auto snapshot = harness.workspace.snapshot();
    const AgentToolResult applied = run_call(harness, *agent,
        ToolRequest{"settings_apply_patch", json{{"scope", "process"}, {"changes", {{"wall_loops", 4}}},
                                                 {"expectedSessionId", std::to_string(snapshot.session.value())},
                                                 {"expectedRevision", snapshot.revision}}.dump()},
        "Go ahead.");
    REQUIRE(applied.state == "succeeded");
    REQUIRE(harness.persistence.document().changes().back().actor == "agent");
    CHECK(validity(applied) == "current");
    CHECK(harness.persistence.document().plan().validity == "current");

    harness.workspace.set_setting_for_testing("brim_width", "8");
    REQUIRE(harness.persistence.document().changes().back().actor == "person");
    const AgentToolResult after = run_call(harness, *agent,
        setup_call({{"purpose", "A strong bracket"}}), "Anything else?");
    CHECK(validity(after) == "needs_reassessment");
    CHECK_FALSE(json::parse(after.output_json)["workspace"]["planInvalidatedBy"].get<std::string>().empty());
}

TEST_CASE("the model's own printer_setup never fires the setup skill, and its setup counts as checked",
          "[agent][skills][setup-skill]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    with_printer_choices(harness.workspace);
    harness.handshake();
    harness.deliver("skills", skills_with_setup_check());
    const ToolRequest petg{"printer_setup", json{{"filamentPresets", {"Generic PETG"}}}.dump()};

    SECTION("no purpose recorded")
    {
        const AgentToolResult switched = run_call(harness, *agent, petg, "Use PETG.");
        REQUIRE(switched.state == "succeeded");
        CHECK(switched.skills.empty());
    }
    SECTION("a purpose recorded: neither a filament nor a plate change fires it, then or later")
    {
        run_call(harness, *agent, setup_call(kPetg), "Make it a strong PETG hook.");
        const AgentToolResult switched = run_call(harness, *agent, petg, "Use PETG.");
        REQUIRE(switched.state == "succeeded");
        CHECK_FALSE(fires_setup_skill(switched));
        const AgentRequest next = run_reply(harness, *agent, "Thanks.");
        CHECK(next.skills.empty());
        CHECK_FALSE(next.setup_unchecked_change);

        const AgentToolResult plate = run_call(
            harness, *agent, ToolRequest{"printer_setup", json{{"plateType", "Cool Plate"}}.dump()}, "Cool plate.");
        REQUIRE(plate.state == "succeeded");
        CHECK_FALSE(fires_setup_skill(plate));
        CHECK_FALSE(run_reply(harness, *agent, "And now?").setup_unchecked_change);
    }
}

TEST_CASE("a restored version under a recorded purpose fires the setup skill on the next request only",
          "[agent][skills][setup-skill]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    harness.deliver("skills", skills_with_setup_check());
    // What ProjectPersistence::record_managed_restore writes in the log.
    const auto restore = [&harness]() {
        ChangeEntry change;
        change.kind  = "restore";
        change.actor = "person";
        change.label = "Restored an earlier project version";
        harness.persistence.document().add_change(change, "2026-10-17T00:00:00Z");
    };

    SECTION("no purpose recorded")
    {
        restore();
        CHECK(run_reply(harness, *agent, "What changed?").skills.empty());
    }
    SECTION("a purpose recorded")
    {
        run_call(harness, *agent, setup_call(kPetg), "Make it a strong PETG hook.");
        CHECK(run_reply(harness, *agent, "Thanks.").skills.empty());
        restore();
        const AgentRequest next = run_reply(harness, *agent, "What changed?");
        REQUIRE(next.skills.size() == 1);
        CHECK(next.skills.front().name == "project-setup-changed");
        CHECK(run_reply(harness, *agent, "And now?").skills.empty());
    }
    SECTION("a file loaded on the next request carries the file-loaded skill alone")
    {
        run_call(harness, *agent, setup_call(kPetg), "Make it a strong PETG hook.");
        restore();
        harness.host.on_file_loaded(prusa_open());
        harness.pump_all();
        const auto report = std::find_if(agent->requests.rbegin(), agent->requests.rend(), [](const AgentRequest& request) {
            return request.purpose == AgentRequest::Purpose::Reply;
        });
        REQUIRE(report != agent->requests.rend());
        CHECK(report->user_text.rfind("JusPrin file-load report", 0) == 0);
        REQUIRE(report->skills.size() == 1);
        CHECK(report->skills.front().name == "file-loaded");
        // The model has replied since the restore, so no later request fires it.
        CHECK(run_reply(harness, *agent, "And now?").skills.empty());
    }
}

TEST_CASE("a setup change is unchecked until the model is asked to check it, once", "[agent][skills][setup-skill]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    with_printer_choices(harness.workspace);
    harness.handshake();
    harness.deliver("skills", skills_with_setup_check());
    const auto fires = [](const AgentRequest& request) {
        return std::any_of(request.skills.begin(), request.skills.end(),
                           [](const AgentSkillUse& skill) { return skill.trigger == "setup_changed"; });
    };

    // No purpose: no change is unchecked, and nothing fires.
    hand_setup(harness.workspace, std::nullopt, "Cool Plate");
    const AgentRequest before = run_reply(harness, *agent, "Hello.");
    CHECK_FALSE(before.setup_unchecked_change);
    CHECK_FALSE(fires(before));

    // The purpose is recorded under the setup in force, which is checked.
    run_call(harness, *agent, setup_call(kPetg), "Make it a strong PETG hook.");
    const AgentRequest recorded = run_reply(harness, *agent, "Thanks.");
    CHECK_FALSE(recorded.setup_unchecked_change);
    CHECK_FALSE(fires(recorded));

    SECTION("the agent's own printer_setup fires nothing and counts as checked")
    {
        const AgentToolResult switched =
            run_call(harness, *agent, ToolRequest{"printer_setup", json{{"filamentPresets", {"Generic PETG"}}}.dump()}, "Use PETG.");
        CHECK_FALSE(fires_setup_skill(switched));
        for (const char* text : {"Thanks.", "Anything else?", "And now?"}) {
            const AgentRequest later = run_reply(harness, *agent, text);
            CHECK_FALSE(later.setup_unchecked_change);
            CHECK_FALSE(fires(later));
        }
    }
    SECTION("the person's own change fires the skill on their next message, once")
    {
        // As the sidebar does: no tool call, no chat.
        hand_setup(harness.workspace, "Generic PETG");
        const AgentRequest next = run_reply(harness, *agent, "I switched to PETG.");
        CHECK(next.setup_unchecked_change);
        CHECK(fires(next));
        for (const char* text : {"Thanks.", "And now?"}) {
            const AgentRequest later = run_reply(harness, *agent, text);
            CHECK_FALSE(later.setup_unchecked_change);
            CHECK_FALSE(fires(later));
        }
        // A change before a message that leads to a call is caught on that
        // message's request, and the call's result does not repeat it.
        hand_setup(harness.workspace, "Generic PLA");
        const AgentToolResult read = run_call(harness, *agent, ToolRequest{"workspace_inspect", "{}"}, "What is set up?");
        CHECK(fires(*std::find_if(agent->requests.rbegin(), agent->requests.rend(), [](const AgentRequest& request) {
            return request.purpose == AgentRequest::Purpose::Reply;
        })));
        CHECK_FALSE(fires_setup_skill(read));
    }
}

// The 2026-10-28 walkthrough (frames 2C-02 to 2C-05): after a plan, the
// person switched the filament from the header chip and sliced. The card's
// mark must name the change in their words, survive the slice, and go only
// when the model records the plan again, which it may leave to the turn that
// answers its question; every turn until then carries the setup skill.
TEST_CASE("a plan put in doubt by hand keeps its mark and the setup skill until the model records it again",
          "[agent][skills][setup-skill][plan]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    with_printer_choices(harness.workspace);
    harness.handshake();
    harness.deliver("skills", skills_with_setup_check());
    const auto fires = [](const AgentRequest& request) {
        return std::any_of(request.skills.begin(), request.skills.end(),
                           [](const AgentSkillUse& skill) { return skill.trigger == "setup_changed"; });
    };
    const auto card = [&harness] { return harness.of_type("context").back()["payload"]["context"]; };
    const auto last_turn = [agent]() -> const AgentRequest& {
        return *std::find_if(agent->requests.rbegin(), agent->requests.rend(), [](const AgentRequest& request) {
            return request.purpose == AgentRequest::Purpose::Reply;
        });
    };
    const std::string reason = "Filament changed to Generic PETG by hand";

    run_call(harness, *agent, setup_call({{"purpose", "A strong hook"}, {"headline", "Printed on its side for strength"}}),
             "Make it a strong hook.");
    REQUIRE(card()["planValidity"] == "current");
    CHECK_FALSE(fires(run_reply(harness, *agent, "Thanks.")));

    // The person picks another filament from the header chip: the card names
    // it without the preset library's suffix, and nothing starts a turn.
    std::size_t requests = agent->requests.size();
    hand_setup(harness.workspace, "Generic PETG");
    harness.workspace.report_preset_edit_for_testing("filament", "Generic PETG @System");
    harness.pump_all();
    CHECK(agent->requests.size() == requests);
    CHECK(card()["planValidity"] == "needs_reassessment");
    CHECK(card()["planInvalidatedBy"] == reason);
    // A setting edited after it does not hide the switch.
    harness.workspace.set_setting_for_testing("wall_loops", "4");
    CHECK(card()["planInvalidatedBy"] == reason);

    // The person slices: still no turn, and the mark stays.
    REQUIRE(harness.workspace.start_slice(std::nullopt, false).succeeded());
    harness.workspace.finish_slice_for_testing(true);
    harness.host.pump_tools();
    harness.pump_all();
    CHECK(agent->requests.size() == requests);
    CHECK(card()["planValidity"] == "needs_reassessment");

    // The card's Check plan: the turn carries the skill and reads the mark.
    const AgentRequest check = run_reply(harness, *agent, "Check the plan against what changed.");
    CHECK(check.setup_unchecked_change);
    CHECK(fires(check));
    CHECK(check.plan.validity == "needs_reassessment");
    CHECK(check.plan.invalidated_by == reason);
    // It recommended a change and recorded nothing: the mark stays.
    CHECK(card()["planValidity"] == "needs_reassessment");

    // The answer's turn finds the setup checked, yet still carries the skill
    // that tells it to record the plan, and recording it clears the mark.
    run_call(harness, *agent, setup_call({{"headline", "Printed on its side for strength, in PETG"}}), "Keep PETG.");
    CHECK_FALSE(last_turn().setup_unchecked_change);
    CHECK(fires(last_turn()));
    CHECK(card()["planValidity"] == "current");
    CHECK(card()["planInvalidatedBy"] == "");

    // Recorded again: the next turn carries nothing.
    const AgentRequest after = run_reply(harness, *agent, "Thanks.");
    CHECK_FALSE(fires(after));
    CHECK_FALSE(after.setup_unchecked_change);
    CHECK(after.plan.validity == "current");
}

TEST_CASE("the plan's mark says what changed and who changed it", "[agent][plan]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const auto reason = [&harness] { return harness.persistence.document().plan().invalidated_by; };
    const auto replan = [&] {
        run_call(harness, *agent, setup_call({{"headline", "Printed on its side for strength"}}), "Plan it.");
        REQUIRE(harness.persistence.document().plan().validity == "current");
    };

    replan();
    harness.workspace.report_preset_edit_for_testing("printer", "Bambu Lab A1 mini 0.4 nozzle");
    CHECK(reason() == "Printer changed to Bambu Lab A1 mini 0.4 nozzle by hand");
    // A printer switch brings filament and process switches with it: the
    // printer stays the reason, as it does over any later setting.
    harness.workspace.report_preset_edit_for_testing("filament", "Bambu PLA Basic @BBL A1M");
    harness.workspace.report_preset_edit_for_testing("process", "0.20mm Standard @BBL A1M");
    CHECK(reason() == "Printer changed to Bambu Lab A1 mini 0.4 nozzle by hand");

    replan();
    harness.workspace.report_preset_edit_for_testing("process", "0.16mm Optimal @BBL A1M");
    CHECK(reason() == "Print settings changed to 0.16mm Optimal by hand");
    // Two switches of one kind: the later one is what holds now.
    harness.workspace.report_preset_edit_for_testing("process", "0.20mm Standard @BBL A1M");
    CHECK(reason() == "Print settings changed to 0.20mm Standard by hand");

    replan();
    harness.workspace.record_step_for_testing("Move Object");
    CHECK(reason() == "Move Object, by hand");
}

// Board 5: the Agent proposes a preset with a preview; the person's Save
// preset saves exactly that, and the app's receipt starts the Agent's turn.
TEST_CASE("a preset the Agent proposes is saved by the person's Save preset, with a receipt", "[agent][tools][settings]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const json patch{{"scope", "filament"}, {"target", {{"preset", "Fixture PLA"}}}, {"changes", {{"nozzle_temperature", "225"}}},
                     {"persistAs", "Fixture PLA · MK4 calibrated"}};
    const AgentToolResult previewed = run_call(harness, *agent, ToolRequest{"settings_preview_patch", patch.dump()}, "Here it is.");
    REQUIRE(previewed.state == "succeeded");
    const auto& activities = harness.host.tools().activities();
    const auto preview = std::find_if(activities.begin(), activities.end(),
                                      [](const ToolActivity& item) { return item.tool == "settings_preview_patch"; });
    REQUIRE(preview != activities.end());
    const std::string preview_id = preview->action_id;
    const json* sent = harness.last_of_type("tool_activity");
    REQUIRE(sent != nullptr);
    CHECK((*sent)["payload"]["activity"]["proposal"]["save"] == "Save preset");

    const std::size_t requests = agent->requests.size();
    harness.deliver("save_proposal", json{{"actionId", preview_id}});
    CHECK(harness.last_of_type("bridge_error") == nullptr);
    pump_tools_to_completion(harness);
    harness.host.pump_tools();
    const json note = (*harness.last_of_type("message_added"))["payload"]["message"];
    CHECK(note["role"] == "note");
    CHECK(note["receipt"]["name"] == "Fixture PLA · MK4 calibrated");
    CHECK(note["receipt"]["detail"] == "Filament preset · nozzle temperature 225 °C");
    CHECK(harness.workspace.settings_for_testing().selected_filament == "Fixture PLA · MK4 calibrated");
    harness.pump_all();
    REQUIRE(agent->requests.size() > requests);
    CHECK(last_reply(*agent).note ==
          "The person pressed Save preset on your proposal. Saved Fixture PLA · MK4 calibrated: Filament preset · nozzle "
          "temperature 225 °C.");
    // The receipt survives a reload of the chat.
    const auto stored = harness.host.conversation();
    const auto receipt = std::find_if(stored.begin(), stored.end(), [&note](const ConversationMessage& message) {
        return message.id == note["id"];
    });
    REQUIRE(receipt != stored.end());
    CHECK_FALSE(receipt->receipt.empty());

    // Board 6's Undo: the copy is deleted, this project uses the filament it
    // was made from again, the receipt says so, and the Agent is told.
    const std::size_t before_undo = agent->requests.size();
    harness.deliver("undo_receipt", json{{"messageId", note["id"]}});
    CHECK(harness.last_of_type("bridge_error") == nullptr);
    auto& fixture = harness.workspace.settings_for_testing();
    CHECK(fixture.filaments.count("Fixture PLA · MK4 calibrated") == 0);
    CHECK(fixture.selected_filament == "Fixture PLA");
    const json undone = (*harness.last_of_type("message_added"))["payload"]["message"];
    CHECK(undone["id"] == note["id"]);
    CHECK(undone["receipt"]["undone"] == true);
    harness.pump_all();
    REQUIRE(agent->requests.size() > before_undo);
    // The receipt's note, rewritten after the model read it, starts a turn
    // and goes again as it reads now. It names the deleted copy and, after
    // it, the filament used again; its wording is the host's.
    const std::string undo_note = undone["text"].get<std::string>();
    const std::size_t copy_named = undo_note.find("Fixture PLA · MK4 calibrated");
    REQUIRE(copy_named != std::string::npos);
    CHECK(undo_note.rfind("Fixture PLA") > copy_named);
    CHECK(last_reply(*agent).source == note["id"]);
    CHECK(last_reply(*agent).note == undo_note);
    // A second press has nothing left to undo.
    harness.deliver("undo_receipt", json{{"messageId", note["id"]}});
    REQUIRE(harness.last_of_type("bridge_error") != nullptr);
    CHECK((*harness.last_of_type("bridge_error"))["payload"]["code"] == "receipt_closed");
}

TEST_CASE("a proposal the person answered another way saves nothing", "[agent][tools][settings]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    const json patch{{"scope", "filament"}, {"target", {{"preset", "Fixture PLA"}}}, {"changes", {{"nozzle_temperature", "225"}}},
                     {"persistAs", "Fixture PLA · MK4 calibrated"}};
    run_call(harness, *agent, ToolRequest{"settings_preview_patch", patch.dump()}, "Here it is.");
    const auto& activities = harness.host.tools().activities();
    const auto preview = std::find_if(activities.begin(), activities.end(),
                                      [](const ToolActivity& item) { return item.tool == "settings_preview_patch"; });
    REQUIRE(preview != activities.end());
    const std::string preview_id = preview->action_id;
    run_reply(harness, *agent, "Pick another block");
    harness.deliver("save_proposal", json{{"actionId", preview_id}});
    const json* error = harness.last_of_type("bridge_error");
    REQUIRE(error != nullptr);
    CHECK((*error)["payload"]["code"] == "proposal_closed");
    pump_tools_to_completion(harness);
    CHECK(harness.workspace.settings_for_testing().filaments.count("Fixture PLA · MK4 calibrated") == 0);
}

// A second test's result saved into the calibrated copy it already made:
// Undo puts that copy's earlier values back instead of deleting it.
TEST_CASE("undo on a preset saved in place puts its earlier values back", "[agent][tools][settings]")
{
    auto agent_owner = std::make_unique<OneCallAgent>();
    OneCallAgent* agent = agent_owner.get();
    Harness harness{AgentServicePtr(std::move(agent_owner))};
    harness.handshake();
    auto save = [&](const json& patch, const std::string& text) {
        run_call(harness, *agent, ToolRequest{"settings_preview_patch", patch.dump()}, text);
        const auto& activities = harness.host.tools().activities();
        const auto  preview    = std::find_if(activities.rbegin(), activities.rend(),
                                              [](const ToolActivity& item) { return item.tool == "settings_preview_patch"; });
        REQUIRE(preview != activities.rend());
        harness.deliver("save_proposal", json{{"actionId", preview->action_id}});
        pump_tools_to_completion(harness);
        harness.host.pump_tools();
        harness.pump_all();
        return (*harness.last_of_type("message_added"))["payload"]["message"];
    };
    const std::string copy = "Fixture PLA · MK4 calibrated";
    save(json{{"scope", "filament"}, {"target", {{"preset", "Fixture PLA"}}}, {"changes", {{"nozzle_temperature", "225"}}},
              {"persistAs", copy}}, "Here it is.");
    const json receipt = save(json{{"scope", "filament"}, {"target", {{"preset", copy}}}, {"changes", {{"nozzle_temperature", "220"}}},
                                   {"persistAs", copy}}, "And the second one.");
    REQUIRE(receipt.contains("receipt"));
    CHECK(receipt["receipt"]["undo"]["newCopy"] == false);
    auto& fixture = harness.workspace.settings_for_testing();
    CHECK(fixture.filaments.at(copy).saved.at("nozzle_temperature") == "220");
    harness.deliver("undo_receipt", json{{"messageId", receipt["id"]}});
    CHECK(harness.last_of_type("bridge_error") == nullptr);
    REQUIRE(fixture.filaments.count(copy) == 1);
    CHECK(fixture.filaments.at(copy).saved.at("nozzle_temperature") == "225");
    CHECK(fixture.selected_filament == copy);
    CHECK((*harness.last_of_type("message_added"))["payload"]["message"]["receipt"]["undone"] == true);
}

// The history guard. A provider bills the part of a request it has seen
// before (its prompt cache) at a fraction of the full price, and it finds
// that part only when the new request opens with it unchanged. So the first
// request of every turn must open with the whole of the last request the turn
// before sent: tools, system prompt, every message and item, in the same
// bytes. Each chat sends its record of what it sent and appends to it, and
// these cases fail when anything rewrites, drops or reorders an earlier part:
// the only difference allowed is the cache breakpoint that moves to the
// newest user message. Run them alone by their tag:
//   build/arm64/tests/agent/Release/agent_bridge_tests.app/Contents/MacOS/agent_bridge_tests "[history-prefix]"
namespace {

using nlohmann::ordered_json;

// One scripted response: words, then a call if it makes one. A refused
// request gets the HTTP status instead; a stream that fails is accepted and
// breaks off after its first words.
struct ScriptedReply
{
    std::string text;
    std::string call_id{};
    std::string tool{};
    json        input = json::object();
    unsigned    refused{0};
    bool        fails{false};
};

std::string sse(const json& event) { return "data: " + event.dump() + "\n\n"; }

// A Messages API stream: signed thinking, the words, then the call.
std::string anthropic_stream(const ScriptedReply& reply, const std::string& id)
{
    std::string out = sse(json{{"type", "message_start"},
                               {"message", json{{"id", "msg_" + id}, {"usage", json{{"input_tokens", 100}, {"output_tokens", 1}}}}}});
    const auto block = [&out](int index, const json& start, const json& delta) {
        out += sse(json{{"type", "content_block_start"}, {"index", index}, {"content_block", start}});
        out += sse(json{{"type", "content_block_delta"}, {"index", index}, {"delta", delta}});
        out += sse(json{{"type", "content_block_stop"}, {"index", index}});
    };
    block(0, json{{"type", "thinking"}, {"thinking", "Thinking " + id + "."}, {"signature", ""}},
          json{{"type", "signature_delta"}, {"signature", "signature-" + id}});
    block(1, json{{"type", "text"}, {"text", ""}}, json{{"type", "text_delta"}, {"text", reply.text}});
    if (reply.fails)
        return out + sse(json{{"type", "error"}, {"error", json{{"type", "overloaded_error"}, {"message", "Overloaded"}}}});
    if (!reply.call_id.empty())
        block(2, json{{"type", "tool_use"}, {"id", reply.call_id}, {"name", reply.tool}, {"input", json::object()}},
              json{{"type", "input_json_delta"}, {"partial_json", reply.input.dump()}});
    out += sse(json{{"type", "message_delta"}, {"delta", json{{"stop_reason", reply.call_id.empty() ? "end_turn" : "tool_use"}}},
                    {"usage", json{{"output_tokens", 10}}}});
    return out + sse(json{{"type", "message_stop"}});
}

// A Responses API stream: an encrypted reasoning item, the message, then the
// call, each with the item id and status the service gives it.
std::string responses_stream(const ScriptedReply& reply, const std::string& id)
{
    json output = json::array(
        {json{{"type", "reasoning"}, {"id", "rs_" + id}, {"summary", json::array()}, {"encrypted_content", "reasoning-" + id}},
         json{{"type", "message"}, {"id", "msg_" + id}, {"status", "completed"}, {"role", "assistant"},
              {"content", json::array({json{{"type", "output_text"}, {"text", reply.text}, {"annotations", json::array()}}})}}});
    if (!reply.call_id.empty())
        output.push_back(json{{"type", "function_call"}, {"id", "fc_" + id}, {"status", "completed"}, {"call_id", reply.call_id},
                              {"name", reply.tool}, {"arguments", reply.input.dump()}});
    if (reply.fails)
        return sse(json{{"type", "response.output_text.delta"}, {"delta", reply.text}}) +
               sse(json{{"type", "response.failed"}, {"response", json::object()}});
    return sse(json{{"type", "response.output_text.delta"}, {"delta", reply.text}}) +
           sse(json{{"type", "response.completed"},
                    {"response", json{{"output", output}, {"usage", json{{"input_tokens", 100}, {"output_tokens", 10}, {"total_tokens", 110}}}}}});
}

// What the guard needs to know of one wire format.
struct WireFormat
{
    std::string provider; // its ProviderProfile id
    std::function<std::string(const ScriptedReply&, const std::string& id)> stream;
    // The top-level fields that hold the prompt, in the order the provider
    // reads them. Every other top-level field (model, thinking or reasoning
    // settings, stream) must be the same on both requests.
    std::vector<std::string> prompt_fields;
    // Takes out of one part of the prompt what may differ from request to
    // request without changing what the provider caches.
    std::function<void(ordered_json& part)> forget_per_request;
};

WireFormat anthropic_wire()
{
    // Its three cache breakpoints: one on the last tool and one on the system
    // prompt, which stay where they are and are compared, and one on the
    // newest user content, which moves to the newest one on every request. A
    // breakpoint marks where to cache and is not part of the cached content
    // (moving one is the documented pattern for a growing chat:
    // platform.claude.com/docs/en/build-with-claude/prompt-caching), so the
    // moving one is left out of the messages.
    return {"anthropic", anthropic_stream, {"tools", "system", "messages"}, [](ordered_json& message) {
                if (message.contains("content") && message["content"].is_array())
                    for (ordered_json& block : message["content"])
                        block.erase("cache_control");
            }};
}

WireFormat responses_wire()
{
    // Nothing in its prompt differs by request. The Idempotency-Key header
    // does, but a header is not part of the prompt.
    return {"openai", responses_stream, {"tools", "instructions", "input"}, [](ordered_json&) {}};
}

// A provider service that answers each reply request at once with the next
// scripted response and keeps the body exactly as it was posted. A title
// request, the only kind sent without tools, is answered with a title and
// takes nothing from the script.
class RecordingProvider final : public IAgentHttpTransport
{
public:
    explicit RecordingProvider(WireFormat wire) : m_wire(std::move(wire)) {}

    bool post(AgentHttpRequest request, EventFn event) override
    {
        ScriptedReply reply{"A title"};
        if (json::parse(request.body).contains("tools")) {
            REQUIRE_FALSE(script.empty());
            reply = script.front();
            script.pop_front();
            replies.push_back(request.body);
        }
        if (reply.refused != 0) {
            // The service's error body, then the transport's error.
            event(AgentHttpEvent{AgentHttpEvent::Kind::Data, json{{"error", json{{"message", "refused"}}}}.dump(), {}, 0});
            event(AgentHttpEvent{AgentHttpEvent::Kind::Error, {}, "HTTP " + std::to_string(reply.refused), reply.refused});
            return true;
        }
        event(AgentHttpEvent{AgentHttpEvent::Kind::Data, m_wire.stream(reply, std::to_string(++m_responses)), {}, 0});
        event(AgentHttpEvent{AgentHttpEvent::Kind::Complete, {}, {}, 200});
        return true;
    }
    void cancel() override {}

    std::deque<ScriptedReply> script;
    std::vector<std::string>  replies;

private:
    WireFormat m_wire;
    int        m_responses{0};
};

// A request's prompt as a list of parts (each tool, each system block, each
// message or input item), each the bytes the service received for it, and
// the rest of the body.
struct Prompt
{
    std::vector<std::string> labels;
    std::vector<std::string> parts;
    std::string              rest;
};

Prompt prompt_of(const WireFormat& wire, const std::string& body)
{
    ordered_json parsed = ordered_json::parse(body);
    // Written again, the parsed body is the body itself, so each part below
    // is exactly the bytes the service received for it.
    REQUIRE(parsed.dump() == body);
    Prompt prompt;
    for (const std::string& field : wire.prompt_fields) {
        if (!parsed.contains(field))
            continue;
        ordered_json value = parsed[field];
        parsed.erase(field);
        if (!value.is_array())
            value = ordered_json::array({value});
        for (std::size_t i = 0; i < value.size(); ++i) {
            wire.forget_per_request(value[i]);
            prompt.labels.push_back(field + "[" + std::to_string(i) + "]");
            prompt.parts.push_back(value[i].dump());
        }
    }
    prompt.rest = parsed.dump();
    return prompt;
}

// `later` must open with every part of `earlier`, in order, and add to it.
// The first part that differs is reported whole.
void check_opens_with(const Prompt& earlier, const Prompt& later)
{
    CHECK(later.rest == earlier.rest);
    for (std::size_t i = 0; i < earlier.parts.size(); ++i) {
        const bool same = i < later.parts.size() && later.labels[i] == earlier.labels[i] && later.parts[i] == earlier.parts[i];
        if (!same) {
            FAIL_CHECK("first difference at " << earlier.labels[i] << "\n  the turn before sent:  " << earlier.parts[i]
                                              << "\n  this turn opened with: "
                                              << (i < later.parts.size() ? later.labels[i] + " " + later.parts[i] : "nothing"));
            return;
        }
    }
    CHECK(later.parts.size() > earlier.parts.size());
}

// One chat on the real host and loop, against a provider that records what
// it is sent. Each turn says how it starts and what the model answers.
struct PrefixChat
{
    RecordingProvider*       provider{nullptr};
    std::unique_ptr<Harness> harness;
    WireFormat               wire;
    std::vector<std::size_t> turn_starts;

    explicit PrefixChat(WireFormat format, ProjectPersistence::Config config = test_persistence_config())
        : wire(std::move(format))
    {
        auto owner = std::make_unique<RecordingProvider>(wire);
        provider   = owner.get();
        harness    = std::make_unique<Harness>(make_provider_agent(*profile_for(wire.provider), "key", "", "", std::move(owner)),
                                            std::move(config));
        harness->handshake();
    }

    std::string plate() const { return std::to_string(harness->workspace.snapshot().plates.at(0).id.value()); }

    // `start` begins the turn; the model answers it with `replies`, one per
    // request, and the turn runs to its end.
    void turn(const std::vector<ScriptedReply>& replies, const std::function<void()>& start)
    {
        turn_starts.push_back(provider->replies.size());
        provider->script.assign(replies.begin(), replies.end());
        start();
        for (int i = 0; i < 10; ++i) {
            harness->pump_all();
            pump_tools_to_completion(*harness);
        }
        REQUIRE(provider->script.empty());
        REQUIRE(provider->replies.size() == turn_starts.back() + replies.size());
    }

    void person_turn(const std::string& text, const std::vector<ScriptedReply>& replies)
    {
        turn(replies, [&] { harness->send_user_message(text, "c-prefix-" + std::to_string(turn_starts.size())); });
    }

    // The app starts a turn of its own on its next timer tick.
    void app_turn(const std::function<void()>& event, const std::vector<ScriptedReply>& replies)
    {
        turn(replies, [&] {
            event();
            harness->host.pump_tools();
        });
    }

    ScriptedReply slice_call() const { return {"I will slice it.", "call_slice", "slice_start", json{{"plateId", plate()}}}; }

    // The project is opened again: its document read back from what was
    // saved, picture bytes from their blobs on disk.
    void reopen()
    {
        ProjectStateDocument& document = harness->persistence.document();
        const std::string saved = document.dump();
        REQUIRE(document.load(saved) == ProjectStateDocument::LoadResult::Loaded);
    }

    void check_every_turn_opens_with_the_last() const
    {
        REQUIRE(turn_starts.size() >= 3);
        for (std::size_t n = 1; n < turn_starts.size(); ++n) {
            INFO("turn " << n + 1 << "'s first request against turn " << n << "'s last");
            check_opens_with(prompt_of(wire, provider->replies[turn_starts[n] - 1]),
                             prompt_of(wire, provider->replies[turn_starts[n]]));
        }
    }
};

// The person asks and the model slices (a call and its result); the slice
// finishes and the app starts a turn of its own; the person writes again.
void a_call_then_an_app_turn(const WireFormat& wire)
{
    PrefixChat chat(wire);
    chat.person_turn("Slice it.", {chat.slice_call(), {"It is slicing."}});
    REQUIRE(chat.harness->workspace.snapshot().slicing.running);
    chat.app_turn([&] { chat.harness->workspace.finish_slice_for_testing(true); }, {{"The slice is ready."}});
    chat.person_turn("Thanks.", {{"You are welcome."}});
    chat.check_every_turn_opens_with_the_last();
}

// The same with the moments that fire skills: a file opens (the file-loaded
// skill), the person asks for a slice, the slice fails (the slice-failed
// skill), and the person writes again.
void turns_that_fire_skills(const WireFormat& wire)
{
    PrefixChat chat(wire);
    chat.app_turn([&] { chat.harness->host.on_file_loaded(prusa_open()); }, {{"This is a bracket."}});
    chat.person_turn("Slice it.", {chat.slice_call(), {"It is slicing."}});
    REQUIRE(chat.harness->workspace.snapshot().slicing.running);
    chat.app_turn(
        [&] {
            Workspace::SliceFailure failure;
            failure.severity = "blocker";
            failure.message  = "Slicing failed: the object has no layers.";
            chat.harness->workspace.fail_slice_for_testing(failure);
        },
        {{"The slice failed: the object has no layers."}});
    chat.person_turn("Thanks.", {{"You are welcome."}});
    chat.check_every_turn_opens_with_the_last();
}

} // namespace

TEST_CASE("every turn's first request opens with the whole of the turn before's last", "[agent][cache][history-prefix]")
{
    SECTION("Anthropic Messages") { a_call_then_an_app_turn(anthropic_wire()); }
    SECTION("OpenAI Responses") { a_call_then_an_app_turn(responses_wire()); }
}

TEST_CASE("turns that fire skills open with the whole of the turn before's last", "[agent][cache][history-prefix]")
{
    SECTION("Anthropic Messages") { turns_that_fire_skills(anthropic_wire()); }
    SECTION("OpenAI Responses") { turns_that_fire_skills(responses_wire()); }
}

// A chat keeps what it sent in the project's document, so a reopened project
// goes on from the same bytes: pictures from their blobs, thinking and
// reasoning as the provider wrote them.
TEST_CASE("a reopened project's chat opens with the whole of the request before it closed", "[agent][cache][record]")
{
    for (const WireFormat& wire : {anthropic_wire(), responses_wire()}) {
        DYNAMIC_SECTION(wire.provider)
        {
            PrefixChat chat(wire);
            chat.app_turn([&] { chat.harness->host.on_file_loaded(prusa_open()); }, {{"This is a bracket."}});
            chat.person_turn("Slice it.", {chat.slice_call(), {"It is slicing."}});
            chat.reopen();
            chat.person_turn("Thanks.", {{"You are welcome."}});
            const std::size_t last = chat.turn_starts.back();
            // The file-loaded skill sent the plate's picture, kept as a blob.
            const std::string before = chat.provider->replies[last - 1];
            CHECK(before.find("image/png") != std::string::npos);
            check_opens_with(prompt_of(wire, before), prompt_of(wire, chat.provider->replies[last]));
            CHECK(chat.harness->persistence.document().dump().find("\"data\":") == std::string::npos);
        }
    }
}

namespace {

// A prompt's messages split into their content blocks, each labelled by its
// message and place in it.
Prompt by_block(const Prompt& prompt)
{
    Prompt blocks{{}, {}, prompt.rest};
    for (std::size_t i = 0; i < prompt.parts.size(); ++i) {
        const ordered_json part = ordered_json::parse(prompt.parts[i]);
        if (!part.contains("content") || !part["content"].is_array()) {
            blocks.labels.push_back(prompt.labels[i]);
            blocks.parts.push_back(prompt.parts[i]);
            continue;
        }
        for (std::size_t b = 0; b < part["content"].size(); ++b) {
            blocks.labels.push_back(prompt.labels[i] + part.value("role", "") + "[" + std::to_string(b) + "]");
            blocks.parts.push_back(part["content"][b].dump());
        }
    }
    return blocks;
}

std::size_t occurrences(const std::string& body, const std::string& words)
{
    std::size_t count = 0;
    for (std::size_t at = body.find(words); at != std::string::npos; at = body.find(words, at + words.size()))
        ++count;
    return count;
}

std::string reply_id_of_last(const Harness& harness, const std::string& type)
{
    const json* event = harness.last_of_type(type);
    REQUIRE(event != nullptr);
    return (*event)["payload"]["messageId"].get<std::string>();
}

} // namespace

// What a request carried joins the chat's record once the provider takes it.
// A refused request leaves nothing behind, whether the service was down or
// would never take it (a picture too large): the person's message goes again
// with the next turn, once.
TEST_CASE("a request the provider refuses leaves nothing in the chat's record", "[agent][record]")
{
    const unsigned status = GENERATE(503u, 400u, 413u);
    for (const WireFormat& wire : {anthropic_wire(), responses_wire()}) {
        DYNAMIC_SECTION(wire.provider << " " << status)
        {
            PrefixChat chat(wire);
            ProjectStateDocument& document = chat.harness->persistence.document();
            chat.person_turn("Hello.", {{"Hello."}});
            const std::size_t kept = document.chat_record(document.active_conversation_id())->items.size();
            chat.turn({{"", {}, {}, json::object(), status}}, [&] { chat.harness->send_user_message("Slice it.", "c-refused"); });
            CHECK(chat.harness->host.conversation().back().state == MessageState::Failed);
            CHECK(document.chat_record(document.active_conversation_id())->items.size() == kept);

            chat.person_turn("Try again.", {{"Done."}});
            const std::string& next = chat.provider->replies.back();
            CHECK(occurrences(next, "Slice it.") == 1);
            check_opens_with(prompt_of(wire, chat.provider->replies[0]), prompt_of(wire, next));
        }
    }
}

// A turn whose first request the provider took keeps that request's person
// side when a later request is refused, and nothing after it: the next turn
// never carries a call without its result.
TEST_CASE("a turn refused after its first request keeps only what the provider took", "[agent][record][history-prefix]")
{
    for (const WireFormat& wire : {anthropic_wire(), responses_wire()}) {
        DYNAMIC_SECTION(wire.provider)
        {
            PrefixChat chat(wire);
            chat.person_turn("Hello.", {{"Hello."}});
            chat.turn({chat.slice_call(), {"", {}, {}, json::object(), 400}},
                      [&] { chat.harness->send_user_message("Slice it.", "c-sliced"); });
            CHECK(chat.harness->host.conversation().back().state == MessageState::Failed);
            chat.person_turn("Thanks.", {{"You are welcome."}});
            const std::string& next = chat.provider->replies.back();
            CHECK(next.find("call_slice") == std::string::npos);
            CHECK(occurrences(next, "Slice it.") == 1);
            // The record ends on the person's side, and Anthropic takes
            // consecutive user messages as one: the next turn's words join
            // that message. Its cache is by content block, so the check is.
            check_opens_with(by_block(prompt_of(wire, chat.provider->replies[1])), by_block(prompt_of(wire, next)));
        }
    }
}

// A turn stopped before the provider answered leaves the record as it was:
// the next turn opens with the last request the provider took, and the
// stopped turn's message goes again with it.
TEST_CASE("a stopped turn leaves the next turn opening with the last request the provider took", "[agent][record][history-prefix]")
{
    for (const WireFormat& wire : {anthropic_wire(), responses_wire()}) {
        DYNAMIC_SECTION(wire.provider)
        {
            PrefixChat chat(wire);
            chat.person_turn("Hello.", {{"Hello."}});
            chat.provider->script = {{"Never read."}};
            chat.harness->send_user_message("Slice it.", "c-stopped");
            chat.harness->deliver("stop_generation", json{{"messageId", reply_id_of_last(*chat.harness, "assistant_started")}});
            chat.harness->pump_all();
            REQUIRE(chat.harness->host.conversation().back().state == MessageState::Stopped);
            chat.person_turn("Thanks.", {{"You are welcome."}});
            const std::string& next = chat.provider->replies.back();
            CHECK(occurrences(next, "Slice it.") == 1);
            CHECK(next.find("Never read.") == std::string::npos);
            check_opens_with(prompt_of(wire, chat.provider->replies[0]), prompt_of(wire, next));
        }
    }
}

// A stream the provider took and then broke off: the record holds the turn's
// request, so its retry is that request again, byte for byte, without the
// broken words.
TEST_CASE("a retry after the provider took the turn's request sends that request again", "[agent][record]")
{
    for (const WireFormat& wire : {anthropic_wire(), responses_wire()}) {
        DYNAMIC_SECTION(wire.provider)
        {
            PrefixChat chat(wire);
            chat.person_turn("Hello.", {{"Hello."}});
            chat.turn({{"Half an ans", {}, {}, json::object(), 0, true}},
                      [&] { chat.harness->send_user_message("Slice it.", "c-broken"); });
            const std::string failed = reply_id_of_last(*chat.harness, "assistant_failed");
            chat.turn({{"Done."}}, [&] { chat.harness->deliver("retry_message", json{{"messageId", failed}}); });
            const Prompt broken  = prompt_of(wire, chat.provider->replies[1]);
            const Prompt retried = prompt_of(wire, chat.provider->replies[2]);
            CHECK(retried.rest == broken.rest);
            CHECK(retried.parts == broken.parts);
            CHECK(chat.provider->replies[2].find("Half an ans") == std::string::npos);
        }
    }
}

// The record goes on past a reply; nothing can be added after an earlier one.
TEST_CASE("only the chat's last reply can be retried", "[agent][record]")
{
    PrefixChat chat(responses_wire());
    chat.turn({{"", {}, {}, json::object(), 503}}, [&] { chat.harness->send_user_message("Hello.", "c-down"); });
    const std::string failed = reply_id_of_last(*chat.harness, "assistant_failed");
    chat.person_turn("Hello again.", {{"Hello."}});
    chat.harness->deliver("retry_message", json{{"messageId", failed}});
    chat.harness->pump_all();
    REQUIRE(chat.harness->last_of_type("bridge_error") != nullptr);
    CHECK((*chat.harness->last_of_type("bridge_error"))["payload"]["code"] == "invalid_retry");
    CHECK(chat.provider->replies.size() == 2);
}

// The pictures a chat sent live in its own folder in a managed project. The
// version store prunes attachments/ to what the document's attachment index
// names after every version, and never reads a chat's record, so the chat's
// pictures stay out of it; they go with the chat. A picture gone from disk is
// an error, never an empty picture sent for ever.
TEST_CASE("a managed project's chat keeps its pictures through the version store's prune", "[agent][record][managed]")
{
    namespace fs = std::filesystem;
    ProjectPersistence::Config config = test_persistence_config();
    config.managed_root = (fs::temp_directory_path() / ("jusprin-record-" + std::to_string(std::random_device{}()))).string();
    PrefixChat chat(anthropic_wire(), config);
    chat.app_turn([&] { chat.harness->host.on_file_loaded(prusa_open()); }, {{"This is a bracket."}});
    ProjectStateDocument& document = chat.harness->persistence.document();
    const std::string     id       = document.active_conversation_id();
    const fs::path        root     = fs::path(chat.harness->persistence.attachments_dir()).parent_path();
    REQUIRE(fs::is_directory(root / chat_record_dir(id)));
    // What ProjectVersionStore::prune does to attachments/ after a version.
    std::set<std::string> indexed;
    for (const AttachmentRecord& attachment : document.attachments())
        indexed.insert(attachment.relative_path());
    if (fs::exists(root / "attachments"))
        for (const auto& entry : fs::recursive_directory_iterator(root / "attachments"))
            if (entry.is_regular_file() && !indexed.count(fs::relative(entry.path(), root).generic_u8string()))
                fs::remove(entry.path());

    SECTION("the next turn sends its picture again")
    {
        chat.reopen();
        chat.person_turn("Slice it.", {{"I will."}});
        CHECK(chat.provider->replies[1].find("image/png") != std::string::npos);
        check_opens_with(prompt_of(chat.wire, chat.provider->replies[0]), prompt_of(chat.wire, chat.provider->replies[1]));
    }
    SECTION("a picture gone from disk is an error")
    {
        for (const auto& entry : fs::directory_iterator(root / chat_record_dir(id)))
            fs::remove(entry.path());
        CHECK_THROWS_WITH(chat.harness->send_user_message("Slice it.", "c-gone"),
                          Catch::Matchers::ContainsSubstring("a picture a chat's record keeps is missing"));
    }
    SECTION("the pictures go with the chat")
    {
        chat.harness->deliver("create_conversation", json::object());
        chat.harness->deliver("delete_conversation", json{{"conversationId", id}});
        CHECK_FALSE(fs::exists(root / chat_record_dir(id)));
    }
    fs::remove_all(config.managed_root);
}

// OpenAI picks the machine that caches a request by the request's opening,
// which every JusPrin chat shares; prompt_cache_key keeps one chat's requests
// together and apart from every other chat's.
TEST_CASE("every request of a chat carries one prompt_cache_key, and another chat another", "[agent][cache][openai]")
{
    PrefixChat chat(responses_wire());
    const ProjectStateDocument& document = chat.harness->persistence.document();
    const std::string first = document.active_conversation_id();
    chat.person_turn("Slice it.", {chat.slice_call(), {"It is slicing."}});
    chat.app_turn([&] { chat.harness->workspace.finish_slice_for_testing(true); }, {{"The slice is ready."}});
    chat.harness->deliver("create_conversation", json::object());
    const std::string second = document.active_conversation_id();
    REQUIRE(second != first);
    chat.person_turn("Hello again.", {{"Hello."}});

    std::vector<std::string> keys;
    for (const std::string& body : chat.provider->replies)
        keys.push_back(json::parse(body).value("prompt_cache_key", ""));
    REQUIRE(keys.size() == 4);
    CHECK(keys[0] == document.project_id() + "/" + first);
    CHECK(keys[1] == keys[0]);
    CHECK(keys[2] == keys[0]);
    CHECK(keys[3] == document.project_id() + "/" + second);
}

// On Anthropic the tools open every request, so a tool list that changes
// between requests of a chat voids its whole prompt cache. object_import is
// offered on every turn, and a call that has nothing to import fails.
TEST_CASE("the tool list is the same on every request of a chat, whatever a message attaches", "[agent][cache][import]")
{
    PrefixChat chat(anthropic_wire());
    Harness& h = *chat.harness;
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    chat.person_turn("Hello.", {{"Hello."}});

    const json model = attach(h, "ca-model", "widget.stl", "solid s\nendsolid s\n");
    const std::size_t objects = h.workspace.snapshot().plates.at(0).objects.size();
    chat.turn({{"Adding it.", "call_import", "object_import", json{{"sessionId", session}, {"attachmentId", model["id"]}}},
               {"It is added."}},
              [&] {
                  h.deliver("user_message", json{{"clientMessageId", "c-attach"}, {"text", "Add this."},
                                                 {"attachmentIds", json::array({model["id"]})}});
              });
    REQUIRE(h.workspace.snapshot().plates.at(0).objects.size() == objects + 1);

    chat.person_turn("Add the other one.", {{"Adding it.", "call_none", "object_import",
                                             json{{"sessionId", session}, {"attachmentId", "a-9"}}},
                                            {"Please attach it."}});
    CHECK(h.workspace.snapshot().plates.at(0).objects.size() == objects + 1);
    const json last = json::parse(chat.provider->replies.back());
    const json results = last["messages"][last["messages"].size() - 1]["content"];
    REQUIRE(results.size() == 1);
    CHECK(results[0]["tool_use_id"] == "call_none");
    CHECK(results[0]["is_error"] == true);
    const json output = json::parse(results[0]["content"][0]["text"].get<std::string>());
    CHECK(output["error"]["code"] == "invalid_arguments");
    CHECK(output["error"]["message"].get<std::string>().find("ask them to attach it") != std::string::npos);

    REQUIRE(chat.provider->replies.size() == 5);
    const json tools = json::parse(chat.provider->replies.front())["tools"];
    CHECK(std::any_of(tools.begin(), tools.end(), [](const json& tool) { return tool["name"] == "object_import"; }));
    for (const std::string& body : chat.provider->replies)
        CHECK(json::parse(body)["tools"] == tools);
}

TEST_CASE("the request log writes each body as the transport received it, with the response's usage", "[agent][request-log]")
{
    struct KeepingTransport final : IAgentHttpTransport
    {
        std::vector<std::string> bodies;
        bool post(AgentHttpRequest request, EventFn event) override
        {
            bodies.push_back(request.body);
            // Anthropic's two usage lines, the first split across chunks, then
            // the Responses one; the null usage of response.created is skipped.
            event({AgentHttpEvent::Kind::Data, "event: message_start\ndata: {\"message\":{\"usage\":{\"input_"});
            event({AgentHttpEvent::Kind::Data, "tokens\":100}}}\n\ndata: {\"usage\":{\"output_tokens\":10}}\n\n"});
            event({AgentHttpEvent::Kind::Data, "data: {\"response\":{\"usage\":null}}\ndata: {\"response\":{\"usage\":{\"total_tokens\":110}}}\n"});
            event({AgentHttpEvent::Kind::Complete});
            return true;
        }
        void cancel() override {}
    };
    const JusPrinTest::McpDirectory directory;
    const std::filesystem::path     logged = directory.root / "requests";
    RequestLog                      log(logged, [] { return RequestLog::Chat{"p-1", "c-2"}; });
    auto                            owner = std::make_unique<KeepingTransport>();
    KeepingTransport*               inner = owner.get();
    const auto                      transport = log.transport(std::move(owner));
    const auto file = [&](const std::string& name) {
        std::ifstream in(logged / name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const std::vector<std::string> bodies{"{\"input\":[\"a\"],\"model\":\"m\"}", std::string("{\"input\":[\"\xc3\xa9\0\"]}\n", 18)};
    for (const std::string& body : bodies)
        CHECK(transport->post(AgentHttpRequest{"https://example.test", {}, body}, [](AgentHttpEvent) {}));
    REQUIRE(inner->bodies == bodies);
    CHECK(file("0001-request.json") == inner->bodies[0]);
    CHECK(file("0002-request.json") == inner->bodies[1]);
    CHECK(file("0002-usage.txt") == "p-1 c-2\n{\"input_tokens\":100}\n{\"output_tokens\":10}\n{\"total_tokens\":110}\n");
    CHECK(std::distance(std::filesystem::directory_iterator(logged), std::filesystem::directory_iterator()) == 4);
    // A run's numbering never continues an earlier run's.
    CHECK_THROWS_AS(RequestLog(logged, {}), std::runtime_error);
}
