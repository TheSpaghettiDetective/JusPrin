// State-machine, approval-policy, idempotency, staleness, and execution
// contract tests for the ToolExecutionCoordinator against the fake
// workspace. Every command outcome is checked in both directions: success
// implies the workspace changed, and refusal or failure implies it did not.
// GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpProtocol.hpp"
#include "../jusprin_support/FakeProductState.hpp"
#include "../jusprin_support/FakeWorkspace.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;

namespace {

Workspace::WorkspaceSnapshot one_plate_snapshot()
{
    Workspace::WorkspaceSnapshot snapshot;
    snapshot.setup.project_name = "Coordinator Fixture";

    Workspace::WorkspacePlate plate;
    plate.id     = Workspace::PlateId(Workspace::ProjectSessionId(1), 11);
    plate.name   = "Plate 1";
    plate.active = true;

    Workspace::WorkspaceObject cube;
    cube.id   = Workspace::ObjectId(Workspace::ProjectSessionId(1), 21);
    cube.name = "cube-a";
    cube.instances.push_back({});
    plate.objects = {cube};

    snapshot.plates       = {plate};
    snapshot.active_plate = plate.id;
    return snapshot;
}

struct Harness
{
    Workspace::FakeWorkspace  workspace;
    ToolExecutionCoordinator  coordinator;
    std::vector<ToolActivity> events;
    ToolActivitySubscription  subscription;

    Harness() : workspace(one_plate_snapshot()), coordinator(workspace)
    {
        subscription = coordinator.subscribe([this](const ToolActivity& activity) { events.push_back(activity); });
    }

    Workspace::ObjectId cube_id() const { return workspace.snapshot().plates.at(0).objects.at(0).id; }

    std::size_t object_count() const
    {
        std::size_t count = 0;
        for (const Workspace::WorkspacePlate& plate : workspace.snapshot().plates)
            count += plate.objects.size();
        return count;
    }

    ToolRequest duplicate_cube_request() const
    {
        ToolRequest request;
        request.tool           = "duplicate_object";
        request.arguments_json = json{{"sessionId", std::to_string(workspace.snapshot().session.value())},
                                      {"objectId", std::to_string(cube_id().value())}}
                                     .dump();
        return request;
    }

    void pump_to_completion(const std::string& action_id, int limit = 1000)
    {
        while (!tool_state_terminal(coordinator.find(action_id)->state) && limit-- > 0)
            coordinator.pump();
    }

    std::vector<ToolState> states_of(const std::string& action_id) const
    {
        std::vector<ToolState> states;
        for (const ToolActivity& event : events)
            if (event.action_id == action_id)
                states.push_back(event.state);
        return states;
    }
};

} // namespace

TEST_CASE("approval policy follows the handoff", "[tools][policy]")
{
    // Read-only actions run without approval; every durable mutation asks
    // first; destructive actions may never use a remembered approval.
    STATIC_CHECK(!approval_required(ActionClass::ReadOnly));
    STATIC_CHECK(approval_required(ActionClass::Mutation));
    STATIC_CHECK(approval_required(ActionClass::Destructive));
    STATIC_CHECK(!remembered_approval_allowed(ActionClass::Destructive));
    STATIC_CHECK(!remembered_approval_allowed(ActionClass::ReadOnly));

    // The computation-only exemption lifts the card from a mutation and from
    // nothing else: a destructive action keeps its card whatever it declares.
    STATIC_CHECK(!approval_required(ActionClass::Mutation, true));
    STATIC_CHECK(approval_required(ActionClass::Mutation, false));
    STATIC_CHECK(approval_required(ActionClass::Destructive, true));
    STATIC_CHECK(!approval_required(ActionClass::ReadOnly, true));

    // Exactly which shipped tools claim it, so adding one is a visible diff
    // here. activity_cancel joins them when it lands.
    std::vector<std::string> exempt;
    for (const ToolDefinition& definition : ToolRegistry::instance().definitions()) {
        INFO(definition.name);
        if (definition.computation_only)
            exempt.push_back(definition.name);
        CHECK(approval_required(definition.action_class, definition.computation_only) ==
              (definition.action_class != ActionClass::ReadOnly && !definition.computation_only));
    }
    CHECK(exempt == std::vector<std::string>{"plan_set", "slice_start"});
}

TEST_CASE("Settings approval captures the preview and rejects invalid or stale patches", "[tools][settings]")
{
    Harness h;
    auto request = [&](json changes) {
        const auto snapshot = h.workspace.snapshot();
        return ToolRequest{"settings_apply_patch", json{{"changes", changes}, {"expectedSessionId", std::to_string(snapshot.session.value())},
                                                       {"expectedRevision", snapshot.revision}}.dump()};
    };
    auto bad = h.coordinator.propose(request({{"wall_loops", 4}, {"brim_width", -1}}), "bad");
    REQUIRE(bad.state == ToolState::Failed);
    REQUIRE(bad.error->code == "invalid_setting_value");
    REQUIRE(json::parse(bad.error->details_json)["issues"][0]["key"] == "brim_width");
    REQUIRE(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
    REQUIRE(h.states_of(bad.action_id) == std::vector<ToolState>{ToolState::Failed});

    auto pending = h.coordinator.propose(request({{"wall_loops", 4}}), "pending");
    REQUIRE(pending.state == ToolState::Pending);
    REQUIRE(json::parse(pending.arguments_json)["confirmedChanges"][0] == json{{"key", "wall_loops"}, {"before", "2"}, {"after", "4"}});
    REQUIRE(pending.title == "Change 1 process settings: wall_loops");
    SECTION("reject") { REQUIRE(h.coordinator.reject(pending.action_id)); }
    SECTION("cancel") { REQUIRE(h.coordinator.cancel(pending.action_id)); }
    SECTION("setting event") {
        h.workspace.set_setting_for_testing("brim_width", "7");
        REQUIRE(h.coordinator.find(pending.action_id)->error->code == "stale_revision");
    }
    SECTION("replacement") {
        h.workspace.replace_project(one_plate_snapshot());
        REQUIRE(h.coordinator.find(pending.action_id)->error->code == "stale_revision");
    }
    SECTION("unannounced edit") {
        h.workspace.set_setting_for_testing("wall_loops", "3", false);
        REQUIRE(h.coordinator.approve(pending.action_id));
        h.pump_to_completion(pending.action_id);
        REQUIRE(h.coordinator.find(pending.action_id)->error->code == "stale_workspace");
        REQUIRE(h.workspace.read_settings({"wall_loops"}).items[0].value == "3");
        return;
    }
    REQUIRE(h.workspace.read_settings({"wall_loops"}).items[0].value == "2");
}

TEST_CASE("Settings tools share terminal activities across adapters and return atomic results", "[tools][settings][mcp]")
{
    Harness h;
    std::vector<ToolActivity> external;
    auto sub = h.coordinator.subscribe([&](const auto& a) { if (tool_state_terminal(a.state)) external.push_back(a); });
    const auto initial = h.workspace.snapshot();
    const auto read_id = h.coordinator.propose({"settings_get", R"({"keys":["wall_loops","sparse_infill_density"]})"}, "read").action_id;
    h.pump_to_completion(read_id);
    REQUIRE(h.coordinator.find(read_id)->state == ToolState::Succeeded);
    const auto apply_id = h.coordinator.propose({"settings_apply_patch", json{{"changes", {{"wall_loops", 4}, {"sparse_infill_density", "25%"}}},
        {"expectedSessionId", std::to_string(initial.session.value())}, {"expectedRevision", initial.revision}}.dump()}, "mcp", {}, ToolSource::Mcp).action_id;
    REQUIRE(h.coordinator.approve(apply_id));
    h.pump_to_completion(apply_id);
    const auto activity = *h.coordinator.find(apply_id);
    REQUIRE(activity.state == ToolState::Succeeded);
    const auto result = json::parse(activity.result_json);
    REQUIRE(result["applied"] == true);
    REQUIRE(result["changes"].size() == 2);
    REQUIRE(result["projectUndo"] == false);
    REQUIRE(result["processPresetDirty"] == true);
    REQUIRE(result["revision"] == initial.revision + 1);
    REQUIRE(external.back().result_json == h.events.back().result_json);
    REQUIRE(Mcp::activity_result(activity, h.workspace.snapshot())["structuredContent"] == result);
}

TEST_CASE("approved mutation cannot execute after a content change", "[tools][stale][mcp]")
{
    Harness harness;
    const auto id = harness.coordinator.propose(harness.duplicate_cube_request(), "mcp-race").action_id;
    REQUIRE(harness.coordinator.approve(id));
    REQUIRE(harness.workspace.rename_object(harness.cube_id(), "Edited after approval").succeeded());
    harness.coordinator.pump();
    REQUIRE(harness.coordinator.find(id)->state == ToolState::Failed);
    CHECK(harness.object_count() == 1);
    REQUIRE(harness.coordinator.find(id)->error.has_value());
    CHECK(harness.coordinator.find(id)->error->code == "stale_revision");
}

TEST_CASE("coordinator policy comes from the registry", "[tools][policy][registry]")
{
    Harness harness;
    const ToolActivity& duplicate = harness.coordinator.propose(harness.duplicate_cube_request(), "hostile-caller");
    CHECK(duplicate.action_class == ActionClass::Mutation);
    CHECK(duplicate.requires_approval);
    CHECK(duplicate.title == ToolRegistry::instance().find("duplicate_object")->title);

    const ToolActivity& inspect = harness.coordinator.propose(ToolRequest{"inspect_selection", "{}"}, "read-caller");
    CHECK(inspect.action_class == ActionClass::ReadOnly);
    CHECK_FALSE(inspect.requires_approval);
}

TEST_CASE("coordinator rejects hostile call metadata before proposal execution", "[tools][policy][registry]")
{
    Harness harness;
    ToolRequest request = harness.duplicate_cube_request();
    json arguments = json::parse(request.arguments_json);
    arguments["actionClass"] = "read_only";
    request.arguments_json = arguments.dump();

    const ToolActivity& rejected = harness.coordinator.propose(request, "hostile-caller");
    CHECK(rejected.action_class == ActionClass::Mutation);
    CHECK(rejected.requires_approval);
    CHECK(rejected.state == ToolState::Failed);
    REQUIRE(rejected.error);
    CHECK(rejected.error->code == "invalid_arguments");
    CHECK(harness.object_count() == 1);
}

TEST_CASE("activity subscriptions coexist and unsubscribe independently", "[tools][subscriptions]")
{
    Workspace::FakeWorkspace workspace(one_plate_snapshot());
    ToolExecutionCoordinator coordinator(workspace);
    std::vector<ToolState> first;
    std::vector<ToolState> second;
    int self_notifications = 0;
    ToolActivitySubscription first_subscription =
        coordinator.subscribe([&](const ToolActivity& activity) { first.push_back(activity.state); });
    ToolActivitySubscription second_subscription =
        coordinator.subscribe([&](const ToolActivity& activity) { second.push_back(activity.state); });
    ToolActivitySubscription self_subscription;
    self_subscription = coordinator.subscribe([&](const ToolActivity&) {
        ++self_notifications;
        self_subscription.reset();
    });

    const ToolActivity& proposed = coordinator.propose(ToolRequest{"inspect_selection", "{}"}, "m-1");
    const std::string action_id = proposed.action_id;
    while (!tool_state_terminal(coordinator.find(action_id)->state))
        coordinator.pump();
    CHECK(first == second);
    REQUIRE(first.back() == ToolState::Succeeded);
    CHECK(self_notifications == 1);

    const std::size_t first_before = first.size();
    first_subscription.reset();
    const ToolActivity& next = coordinator.propose(ToolRequest{"inspect_selection", "{}"}, "m-2");
    const std::string next_id = next.action_id;
    while (!tool_state_terminal(coordinator.find(next_id)->state))
        coordinator.pump();
    CHECK(first.size() == first_before);
    CHECK(second.size() > first.size());
}

TEST_CASE("a mutation waits for approval and then executes through the workspace", "[tools][lifecycle]")
{
    Harness harness;
    const std::size_t objects_before  = harness.object_count();
    const std::uint64_t revision_before = harness.workspace.snapshot().revision;

    const ToolActivity& proposed = harness.coordinator.propose(harness.duplicate_cube_request(), "m-2");
    const std::string   action_id = proposed.action_id;
    CHECK(proposed.state == ToolState::Pending);
    CHECK(proposed.requires_approval);
    CHECK(proposed.correlation_id == "m-2");
    CHECK(proposed.expected_revision == revision_before);

    // Proposing must not touch the project.
    CHECK(harness.object_count() == objects_before);
    CHECK_FALSE(harness.workspace.snapshot().can_undo);

    REQUIRE(harness.coordinator.approve(action_id));
    harness.pump_to_completion(action_id);

    const ToolActivity* done = harness.coordinator.find(action_id);
    REQUIRE(done != nullptr);
    REQUIRE(done->state == ToolState::Succeeded);

    // Success must agree with authoritative state in both directions.
    CHECK(harness.object_count() == objects_before + 1);
    CHECK(harness.workspace.snapshot().can_undo);
    const json result = json::parse(done->result_json);
    CHECK(result["revision"].get<std::uint64_t>() > revision_before);
    CHECK(result.contains("newObjectId"));

    // The lifecycle passed through every advertised state with progress.
    const std::vector<ToolState> states = harness.states_of(action_id);
    REQUIRE(states.size() >= 4);
    CHECK(states.front() == ToolState::Pending);
    CHECK(states.at(1) == ToolState::Approved);
    CHECK(states.at(2) == ToolState::Running);
    CHECK(states.back() == ToolState::Succeeded);
}

TEST_CASE("rejection leaves the project untouched", "[tools][lifecycle]")
{
    Harness harness;
    const std::size_t objects_before = harness.object_count();

    const std::string action_id = harness.coordinator.propose(harness.duplicate_cube_request(), "m-2").action_id;
    REQUIRE(harness.coordinator.reject(action_id));
    CHECK(harness.coordinator.find(action_id)->state == ToolState::Rejected);

    for (int i = 0; i < 10; ++i)
        harness.coordinator.pump();
    CHECK(harness.object_count() == objects_before);
    CHECK_FALSE(harness.workspace.snapshot().can_undo);

    SECTION("a rejected action cannot be approved afterwards") {
        CHECK_FALSE(harness.coordinator.approve(action_id));
        CHECK(harness.coordinator.find(action_id)->state == ToolState::Rejected);
        CHECK(harness.object_count() == objects_before);
    }
}

TEST_CASE("decisions are idempotent and cannot run an action twice", "[tools][idempotency]")
{
    Harness harness;
    const std::size_t objects_before = harness.object_count();

    const std::string action_id = harness.coordinator.propose(harness.duplicate_cube_request(), "m-2").action_id;
    REQUIRE(harness.coordinator.approve(action_id));
    CHECK_FALSE(harness.coordinator.approve(action_id)); // duplicate approval while running
    harness.pump_to_completion(action_id);
    REQUIRE(harness.coordinator.find(action_id)->state == ToolState::Succeeded);
    CHECK(harness.object_count() == objects_before + 1);

    // Replayed decisions after completion change nothing and execute nothing.
    CHECK_FALSE(harness.coordinator.approve(action_id));
    CHECK_FALSE(harness.coordinator.reject(action_id));
    CHECK_FALSE(harness.coordinator.cancel(action_id));
    for (int i = 0; i < 10; ++i)
        harness.coordinator.pump();
    CHECK(harness.object_count() == objects_before + 1);
    CHECK(harness.coordinator.find(action_id)->state == ToolState::Succeeded);
}

TEST_CASE("cancellation stops an action before anything durable happens", "[tools][lifecycle]")
{
    Harness harness;
    const std::size_t objects_before = harness.object_count();

    SECTION("while pending") {
        const std::string action_id = harness.coordinator.propose(harness.duplicate_cube_request(), "m-2").action_id;
        REQUIRE(harness.coordinator.cancel(action_id));
        CHECK(harness.coordinator.find(action_id)->state == ToolState::Cancelled);
        CHECK_FALSE(harness.coordinator.approve(action_id));
    }

    SECTION("while running, before the execution tick") {
        const std::string action_id =
            harness.coordinator.propose(harness.duplicate_cube_request(), "m-2", ToolExecutionPacing{10}).action_id;
        REQUIRE(harness.coordinator.approve(action_id));
        harness.coordinator.pump();
        harness.coordinator.pump();
        REQUIRE(harness.coordinator.find(action_id)->state == ToolState::Running);
        CHECK(harness.coordinator.find(action_id)->progress_current > 0);
        REQUIRE(harness.coordinator.cancel(action_id));
        CHECK(harness.coordinator.find(action_id)->state == ToolState::Cancelled);
    }

    for (int i = 0; i < 20; ++i)
        harness.coordinator.pump();
    CHECK(harness.object_count() == objects_before);
    CHECK_FALSE(harness.workspace.snapshot().can_undo);
}

TEST_CASE("a workspace change invalidates pending proposals as stale", "[tools][stale]")
{
    Harness harness;
    const std::size_t objects_before = harness.object_count();
    const std::string action_id = harness.coordinator.propose(harness.duplicate_cube_request(), "m-2").action_id;

    SECTION("a content change marks the proposal stale before any decision") {
        REQUIRE(harness.workspace.rename_object(harness.cube_id(), "renamed-cube").succeeded());
        const ToolActivity* stale = harness.coordinator.find(action_id);
        REQUIRE(stale->state == ToolState::Failed);
        REQUIRE(stale->error.has_value());
        CHECK(stale->error->code == "stale_revision");

        CHECK_FALSE(harness.coordinator.approve(action_id));
        for (int i = 0; i < 10; ++i)
            harness.coordinator.pump();
        CHECK(harness.object_count() == objects_before);
    }

    SECTION("a selection change does not invalidate the pinned proposal") {
        REQUIRE(harness.workspace.select_object(harness.cube_id()).succeeded());
        CHECK(harness.coordinator.find(action_id)->state == ToolState::Pending);
        REQUIRE(harness.coordinator.approve(action_id));
        harness.pump_to_completion(action_id);
        CHECK(harness.coordinator.find(action_id)->state == ToolState::Succeeded);
        CHECK(harness.object_count() == objects_before + 1);
    }

    SECTION("executing one approved action marks other pending proposals stale") {
        const std::string second = harness.coordinator.propose(harness.duplicate_cube_request(), "m-4").action_id;
        REQUIRE(harness.coordinator.approve(action_id));
        harness.pump_to_completion(action_id);
        REQUIRE(harness.coordinator.find(action_id)->state == ToolState::Succeeded);
        const ToolActivity* stale = harness.coordinator.find(second);
        REQUIRE(stale->state == ToolState::Failed);
        CHECK(stale->error->code == "stale_revision");
        CHECK(harness.object_count() == objects_before + 1);
    }
}

TEST_CASE("an execution failure is reported and changes nothing", "[tools][failure]")
{
    Harness harness;
    const std::size_t objects_before = harness.object_count();

    ToolRequest request = harness.duplicate_cube_request();
    request.arguments_json =
        json{{"sessionId", std::to_string(harness.workspace.snapshot().session.value())}, {"objectId", "999999999"}}.dump();
    const std::string action_id = harness.coordinator.propose(request, "m-2").action_id;
    REQUIRE(harness.coordinator.approve(action_id));
    harness.pump_to_completion(action_id);

    const ToolActivity* failed = harness.coordinator.find(action_id);
    REQUIRE(failed->state == ToolState::Failed);
    REQUIRE(failed->error.has_value());
    CHECK(failed->error->code == "missing_object");
    CHECK(harness.object_count() == objects_before);
    CHECK_FALSE(harness.workspace.snapshot().can_undo);
    // A failed execution is terminal for the action; no retry can re-run it.
    CHECK_FALSE(harness.coordinator.approve(action_id));
}

TEST_CASE("a read-only action runs without approval", "[tools][policy]")
{
    Harness harness;
    REQUIRE(harness.workspace.select_object(harness.cube_id()).succeeded());

    ToolRequest request;
    request.tool           = "inspect_selection";
    request.arguments_json = "{}";

    const std::string action_id = harness.coordinator.propose(request, "m-2").action_id;
    const ToolActivity* started = harness.coordinator.find(action_id);
    CHECK_FALSE(started->requires_approval);
    CHECK(started->state == ToolState::Running);

    harness.pump_to_completion(action_id);
    const ToolActivity* done = harness.coordinator.find(action_id);
    REQUIRE(done->state == ToolState::Succeeded);
    const json result = json::parse(done->result_json);
    CHECK(result["selection"] == json::array({"cube-a"}));
    CHECK_FALSE(harness.workspace.snapshot().can_undo);
}

TEST_CASE("the executed change participates in the authoritative history", "[tools][history]")
{
    Harness harness;
    const std::size_t objects_before = harness.object_count();

    const std::string action_id = harness.coordinator.propose(harness.duplicate_cube_request(), "m-2").action_id;
    REQUIRE(harness.coordinator.approve(action_id));
    harness.pump_to_completion(action_id);
    REQUIRE(harness.coordinator.find(action_id)->state == ToolState::Succeeded);
    REQUIRE(harness.object_count() == objects_before + 1);

    REQUIRE(harness.workspace.undo().succeeded());
    CHECK(harness.object_count() == objects_before);
    REQUIRE(harness.workspace.redo().succeeded());
    CHECK(harness.object_count() == objects_before + 1);
}

TEST_CASE("an unknown tool fails cleanly", "[tools][failure]")
{
    Harness harness;
    ToolRequest request;
    request.tool           = "launch_missiles";
    request.arguments_json = "{}";

    const std::string action_id = harness.coordinator.propose(request, "m-2").action_id;
    harness.pump_to_completion(action_id);
    const ToolActivity* failed = harness.coordinator.find(action_id);
    REQUIRE(failed->state == ToolState::Failed);
    CHECK(failed->error->code == "unknown_tool");
}

TEST_CASE("import_model resolves an attachment ID and adds an object", "[tools][import]")
{
    Harness harness;

    // A stand-in for the stored blob the host would have written.
    const std::filesystem::path model =
        std::filesystem::temp_directory_path() / "jusprin-coordinator-import.stl";
    std::ofstream(model, std::ios::binary) << "solid cube\nendsolid cube\n";
    harness.coordinator.set_attachment_path_resolver(
        [&](const std::string& id) { return id == "a-1" ? model.string() : std::string(); });

    ToolRequest request;
    request.tool           = "import_model";
    request.arguments_json = json{{"sessionId", std::to_string(harness.workspace.snapshot().session.value())},
                                  {"attachmentId", "a-1"}}
                                 .dump();

    const std::size_t   before   = harness.object_count();
    const ToolActivity& proposed = harness.coordinator.propose(request, "m-1");
    CHECK(proposed.requires_approval);
    REQUIRE(harness.coordinator.approve(proposed.action_id));
    harness.pump_to_completion(proposed.action_id);

    CHECK(harness.coordinator.find(proposed.action_id)->state == ToolState::Succeeded);
    CHECK(harness.object_count() == before + 1);
    const json result = json::parse(harness.coordinator.find(proposed.action_id)->result_json);
    CHECK(result["imported"] == true);

    std::filesystem::remove(model);
}

TEST_CASE("import_model fails when the attachment can no longer be resolved", "[tools][import]")
{
    Harness harness;
    harness.coordinator.set_attachment_path_resolver([](const std::string&) { return std::string(); });

    ToolRequest request;
    request.tool           = "import_model";
    request.arguments_json = json{{"sessionId", std::to_string(harness.workspace.snapshot().session.value())},
                                  {"attachmentId", "a-404"}}
                                 .dump();

    const std::size_t   before   = harness.object_count();
    const ToolActivity& proposed = harness.coordinator.propose(request, "m-1");
    REQUIRE(harness.coordinator.approve(proposed.action_id));
    harness.pump_to_completion(proposed.action_id);

    CHECK(harness.coordinator.find(proposed.action_id)->state == ToolState::Failed);
    CHECK(harness.coordinator.find(proposed.action_id)->error->code == "unavailable_operation");
    CHECK(harness.object_count() == before);
}

TEST_CASE("chat deletion forgets only terminal activities", "[tools][conversations]")
{
    Harness harness;
    const auto first = harness.coordinator.propose(harness.duplicate_cube_request(), "chat-message").action_id;
    CHECK_THROWS_AS(harness.coordinator.forget_terminal_activities({"chat-message"}), std::logic_error);
    REQUIRE(harness.coordinator.reject(first));
    const auto other = harness.coordinator.propose(harness.duplicate_cube_request(), "other-chat").action_id;
    harness.coordinator.forget_terminal_activities({"chat-message"});
    CHECK(harness.coordinator.find(first) == nullptr);
    REQUIRE(harness.coordinator.find(other));
    CHECK(harness.coordinator.find(other)->state == ToolState::Pending);
}

TEST_CASE("the intent is confirmed on its card and the plan is not", "[tools][intent][plan]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();

    auto call = [&h](const char* tool, json arguments) -> const ToolActivity& {
        return h.coordinator.propose({tool, arguments.dump()}, "m-1");
    };

    // The plan is the agent's own words: it runs with no card, and one pump
    // takes it all the way to a result.
    const std::string plan_action = call("plan_set", json{{"headline", "Protect the visible face"},
                                                          {"decisions", json::array({json{{"topic", "orientation"},
                                                                                          {"statement", "Front face down."},
                                                                                          {"alternative", "Flat: faster, visible seam."}}})},
                                                          {"assumptions", json::array({"PLA on a smooth plate"})}})
                                        .action_id;
    CHECK_FALSE(h.coordinator.find(plan_action)->requires_approval);
    // No card, so it never waits: it is already on its way by the time propose
    // returns, and a pump finishes it.
    CHECK(h.coordinator.find(plan_action)->state != ToolState::Pending);
    h.coordinator.pump();
    REQUIRE(h.coordinator.find(plan_action)->state == ToolState::Succeeded);
    const auto plan_result = json::parse(h.coordinator.find(plan_action)->result_json);
    CHECK(plan_result["plan"]["headline"] == "Protect the visible face");
    CHECK(plan_result["plan"]["decisions"][0]["alternative"] == "Flat: faster, visible seam.");
    CHECK(plan_result["projectUndo"] == false);
    CHECK(registry.validate_output(*registry.find("plan_set"), plan_result));

    // An intent answer waits for the card, and the card says what the agent
    // understood rather than naming a field.
    const std::string intent_action = call("intent_update", json{{"fields", json::array({
                                                json{{"field", "useCase"}, {"value", "decorative"}},
                                                json{{"field", "maxPrintTime"}, {"question", "How long may it take?"}}})}})
                                          .action_id;
    CHECK(h.coordinator.find(intent_action)->requires_approval);
    CHECK(h.coordinator.find(intent_action)->state == ToolState::Pending);
    CHECK(h.coordinator.find(intent_action)->title.find("decorative") != std::string::npos);
    CHECK(store.writes == 1); // the plan only; nothing recorded before approval
    REQUIRE(h.coordinator.approve(intent_action));
    h.coordinator.pump();
    REQUIRE(h.coordinator.find(intent_action)->state == ToolState::Succeeded);
    const auto intent_result = json::parse(h.coordinator.find(intent_action)->result_json);
    CHECK(registry.validate_output(*registry.find("intent_update"), intent_result));
    // An answer the user approved is confirmed; a question with no answer is
    // still the agent's own and is what it does not know.
    REQUIRE(intent_result["intent"]["fields"].size() == 2);
    CHECK(intent_result["intent"]["fields"][1]["field"] == "useCase");
    CHECK(intent_result["intent"]["fields"][1]["provenance"] == "user_confirmed");
    CHECK(intent_result["intent"]["fields"][0]["provenance"] == "agent_inferred");
    CHECK(intent_result["intent"]["openQuestions"] == json::array({"maxPrintTime"}));

    // An answer the agent only assumed says so, even through the same card.
    const std::string assumed = call("intent_update", json{{"fields", json::array({json{{"field", "material"},
                                                                                        {"value", "PLA"},
                                                                                        {"assumed", true}}})}}).action_id;
    REQUIRE(h.coordinator.approve(assumed));
    h.coordinator.pump();
    const auto after = json::parse(h.coordinator.find(assumed)->result_json);
    CHECK(after["intent"]["fields"][0]["field"] == "material");
    CHECK(after["intent"]["fields"][0]["provenance"] == "agent_inferred");

    // Both are readable back through the one read, and neither moved the
    // revision: a pending settings change would still be valid.
    const auto before_revision = h.workspace.snapshot().revision;
    const std::string read = call("workspace_inspect", json{{"sections", json::array({"intent", "plan"})}}).action_id;
    h.coordinator.pump();
    const auto sections = json::parse(h.coordinator.find(read)->result_json);
    CHECK(registry.validate_output(*registry.find("workspace_inspect"), sections));
    CHECK(sections["intent"]["fields"].size() == 3);
    CHECK(sections["plan"]["assumptions"] == json::array({"PLA on a smooth plate"}));
    CHECK_FALSE(sections.contains("projectName")); // the summary was not asked for
    CHECK(h.workspace.snapshot().revision == before_revision);
}

TEST_CASE("workspace_inspect without sections is what it always was", "[tools][inspect]")
{
    Harness h;
    const std::string action = h.coordinator.propose({"workspace_inspect", "{}"}, "m-1").action_id;
    h.coordinator.pump();
    const auto result = json::parse(h.coordinator.find(action)->result_json);
    CHECK(result.contains("projectName"));
    CHECK(result.contains("plates"));
    CHECK(result.contains("history"));
    CHECK_FALSE(result.contains("intent"));
    CHECK_FALSE(result.contains("plan"));

    // Without a store those sections fail rather than reporting an empty
    // record that would read as "the user wants nothing".
    const std::string missing = h.coordinator.propose({"workspace_inspect", R"({"sections":["intent"]})"}, "m-2").action_id;
    h.coordinator.pump();
    REQUIRE(h.coordinator.find(missing)->state == ToolState::Failed);
    CHECK(h.coordinator.find(missing)->error->code == "unavailable_operation");
}

TEST_CASE("slicing starts through Orca's own run and is read back, not waited on", "[tools][slicing]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const auto plate = h.workspace.snapshot().plates.at(0).id;

    // Slicing replaces a computed result and nothing else, so it needs no card.
    const auto& started = h.coordinator.propose({"slice_start", json{{"plateId", std::to_string(plate.value())}}.dump()}, "m-1");
    const std::string handle = started.action_id;
    CHECK_FALSE(started.requires_approval);
    h.pump_to_completion(handle);
    REQUIRE(h.coordinator.find(handle)->state == ToolState::Succeeded);
    const auto result = json::parse(h.coordinator.find(handle)->result_json);
    CHECK(registry.validate_output(*registry.find("slice_start"), result));
    CHECK(result["handle"] == handle);
    CHECK(result["started"] == true);
    CHECK(result["slicing"]["running"] == true);
    CHECK(result["slicing"]["plateId"] == std::to_string(plate.value()));
    CHECK(h.workspace.slice_starts == 1);

    // The call returned; the run has not finished. That is what the section is
    // for, and the handle says which run the reader is watching.
    const auto read = [&h](const char* correlation) {
        const std::string action = h.coordinator.propose({"workspace_inspect", R"({"sections":["slicing"]})"}, correlation).action_id;
        h.pump_to_completion(action);
        return json::parse(h.coordinator.find(action)->result_json)["slicing"];
    };
    auto during = read("m-2");
    CHECK(during["running"] == true);
    CHECK(during["handle"] == handle);
    CHECK(during["plates"][0]["sliced"] == false);

    // A second run while one is in flight is refused: nothing in Orca records
    // who started the first, so it may be the user's.
    const auto& refused = h.coordinator.propose({"slice_start", "{}"}, "m-3");
    h.pump_to_completion(refused.action_id);
    REQUIRE(h.coordinator.find(refused.action_id)->state == ToolState::Failed);
    CHECK(h.coordinator.find(refused.action_id)->error->code == "unavailable_operation");
    CHECK(h.workspace.slice_starts == 1);

    // Taking it over is a decision only the user can make, so preempt brings
    // the card back even though slicing is otherwise computation-only.
    const auto& preempting = h.coordinator.propose({"slice_start", R"({"preempt":true})"}, "m-4");
    CHECK(preempting.requires_approval);
    CHECK(preempting.state == ToolState::Pending);
    const std::string preempt_action = preempting.action_id;
    REQUIRE(h.coordinator.approve(preempt_action));
    h.pump_to_completion(preempt_action);
    CHECK(h.coordinator.find(preempt_action)->state == ToolState::Succeeded);
    CHECK(h.workspace.slice_starts == 2);

    h.workspace.finish_slice_for_testing(true);
    auto after = read("m-5");
    CHECK(after["running"] == false);
    CHECK(after["plates"][0]["sliced"] == true);
    CHECK_FALSE(after.contains("plateId"));
}

TEST_CASE("the slice report says what the plate holds, or that it holds nothing", "[tools][slicing][report]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const auto plate = h.workspace.snapshot().plates.at(0).id;
    const auto read = [&h](const char* correlation, json arguments) {
        const std::string action = h.coordinator.propose({"slice_report", arguments.dump()}, correlation).action_id;
        h.pump_to_completion(action);
        return json::parse(h.coordinator.find(action)->result_json);
    };

    // Not sliced is an answer, not a failure -- and it carries no summary, so
    // nobody reads a print that takes no time and costs nothing.
    const auto empty = read("m-1", json::object());
    CHECK(registry.validate_output(*registry.find("slice_report"), empty));
    CHECK(empty["valid"] == false);
    CHECK_FALSE(empty.contains("summary"));

    Workspace::SliceReport report;
    report.print_time_seconds = 4500;
    report.total_grams        = 23.5;
    report.filaments          = {{0, 7800.0, 23.5, 0.47, true, 0.0, 0.0, 120.0}};
    report.has_cost           = true;
    report.total_cost         = 0.47;
    report.filament_changes   = 2;
    report.findings           = {{"", "Supports are enabled but nothing needs them", false, "cube-a"},
                                 {"1000C002", "The nozzle is too soft for this filament", true, ""}};
    report.conflict           = "Conflicts of G-code paths at Z = 4.20mm (cube-a <-> cube-b)";
    h.workspace.set_slice_report_for_testing(plate, report);
    h.workspace.set_plate_sliced(plate, true);

    const auto summary = read("m-2", json::object());
    CHECK(registry.validate_output(*registry.find("slice_report"), summary));
    CHECK(summary["valid"] == true);
    CHECK(summary["summary"]["printTimeSeconds"] == 4500);
    CHECK(summary["summary"]["filaments"][0]["lengthMm"] == 7800.0);
    CHECK(summary["summary"]["hasCost"] == true);
    // Summary is the default, so a caller checking a print does not pay for
    // findings it did not ask for.
    CHECK_FALSE(summary.contains("findings"));
    CHECK_FALSE(summary.contains("material"));

    const auto findings = read("m-3", json{{"sections", json::array({"findings", "material"})}});
    CHECK(registry.validate_output(*registry.find("slice_report"), findings));
    CHECK_FALSE(findings.contains("summary"));
    // Critical first: a bounded list must lose the mildest findings, never the
    // ones that stop the print.
    CHECK(findings["findings"]["items"][0]["code"] == "1000C002");
    CHECK(findings["findings"]["items"][0]["critical"] == true);
    CHECK(findings["findings"]["items"][1]["object"] == "cube-a");
    CHECK(findings["findings"]["conflict"] == report.conflict);
    CHECK(findings["findings"]["toolpathOutsideBed"] == false);
    CHECK(findings["material"]["filamentChanges"] == 2);
    CHECK(findings["material"]["supportMm3"] == 120.0);

    // A slice being replaced is not a report: the numbers on screen are the
    // ones it is about to overwrite.
    REQUIRE(h.workspace.start_slice(plate, false).succeeded());
    CHECK(read("m-4", json::object())["valid"] == false);
}

TEST_CASE("presets are listed by kind, filtered, and paged", "[tools][presets]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    h.workspace.set_presets_for_testing(Workspace::PresetKind::Filament,
                                        {{"Generic PLA @BBL", "Generic PLA", "BBL", true, true, true},
                                         {"Generic PETG @BBL", "Generic PETG", "BBL", true, false, true},
                                         {"My PLA tuned", "My PLA tuned", "", false, false, true},
                                         {"Generic ABS @Other", "Generic ABS", "Other", true, false, false}});
    const auto list = [&h](const char* correlation, json arguments) {
        const std::string action = h.coordinator.propose({"presets_list", arguments.dump()}, correlation).action_id;
        h.pump_to_completion(action);
        return json::parse(h.coordinator.find(action)->result_json);
    };

    // Compatible only by default: an incompatible preset is not something the
    // user can choose, so offering it would be a wrong answer.
    const auto compatible = list("m-1", json{{"kind", "filament"}});
    CHECK(registry.validate_output(*registry.find("presets_list"), compatible));
    CHECK(compatible["total"] == 3);
    CHECK(compatible["items"].size() == 3);
    CHECK(compatible["items"][0]["selected"] == true);
    CHECK(compatible["items"][0]["label"] == "Generic PLA");
    CHECK(compatible["items"][2]["system"] == false);
    CHECK(compatible["items"][2]["vendor"] == "");

    const auto everything = list("m-2", json{{"kind", "filament"}, {"compatibleOnly", false}});
    CHECK(everything["total"] == 4);
    CHECK(everything["items"][3]["compatible"] == false);

    const auto filtered = list("m-3", json{{"kind", "filament"}, {"query", "PETG"}});
    CHECK(filtered["total"] == 1);
    CHECK(filtered["items"][0]["name"] == "Generic PETG @BBL");

    // A page says how much it is a page of, so the agent knows to follow the
    // cursor rather than treating one page as the catalogue.
    const auto page = list("m-4", json{{"kind", "filament"}, {"limit", 2}});
    CHECK(page["items"].size() == 2);
    CHECK(page["total"] == 3);
    CHECK(page["truncated"] == true);
    const auto rest = list("m-5", json{{"kind", "filament"}, {"limit", 2}, {"cursor", page["nextCursor"]}});
    CHECK(rest["items"].size() == 1);
    CHECK(rest["truncated"] == false);
    CHECK(rest["items"][0]["name"] == "My PLA tuned");

    // A kind this fixture knows nothing about is an empty list, not a failure.
    const auto printers = list("m-6", json{{"kind", "printer"}});
    CHECK(printers["items"].empty());
    CHECK(printers["total"] == 0);

    CHECK_FALSE(registry.validate_call(*registry.find("presets_list"), R"({"kind":"sla"})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("presets_list"), R"({"kind":"filament","limit":99})").valid());
}

TEST_CASE("printers are listed with what they reported and when", "[tools][printers]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    Workspace::PrinterDevice busy{"FAKE001", "Fake A1 mini", "N1", "lan", "printing"};
    busy.nozzle_diameter = 0.4;
    busy.materials       = {"Bambu PLA Basic"};
    busy.observed_at_ms  = 1789000000000;
    // A printer the app has heard nothing from is still a printer the user
    // has, and reports nothing rather than zeros.
    const Workspace::PrinterDevice silent{"OLD002", "Shelf printer", "X1", "cloud", "offline"};
    h.workspace.set_printers_for_testing({busy, silent});

    const std::string action = h.coordinator.propose({"printer_list", "{}"}, "m-1").action_id;
    h.pump_to_completion(action);
    const auto result = json::parse(h.coordinator.find(action)->result_json);
    CHECK(registry.validate_output(*registry.find("printer_list"), result));
    REQUIRE(result["items"].size() == 2);
    CHECK(result["items"][0]["activity"] == "printing");
    CHECK(result["items"][0]["nozzleDiameter"] == 0.4);
    CHECK(result["items"][0]["materials"][0] == "Bambu PLA Basic");
    // A moment a person or a model can read, not an epoch count.
    CHECK(result["items"][0]["observedAt"] == "2026-09-10T00:26:40Z");
    CHECK(result["items"][1]["activity"] == "offline");
    CHECK_FALSE(result["items"][1].contains("nozzleDiameter"));
    CHECK_FALSE(result["items"][1].contains("observedAt"));
    CHECK(result["items"][1]["materials"].empty());
    CHECK(result["truncated"] == false);

    CHECK_FALSE(registry.validate_call(*registry.find("printer_list"), R"({"connected":true})").valid());
}
