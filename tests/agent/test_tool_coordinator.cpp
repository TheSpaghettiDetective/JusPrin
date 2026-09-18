// State-machine, approval-policy, idempotency, staleness, and execution
// contract tests for the ToolExecutionCoordinator against the fake
// workspace. Every command outcome is checked in both directions: success
// implies the workspace changed, and refusal or failure implies it did not.
// GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpProtocol.hpp"
#include "../jusprin_support/FakeProductState.hpp"
#include "slic3r/GUI/JusPrin/Agent/IntentChecks.hpp"
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

    // Every printed copy: plate_layout's quantity adds instances.
    std::size_t object_count() const
    {
        std::size_t count = 0;
        for (const Workspace::WorkspacePlate& plate : workspace.snapshot().plates)
            for (const Workspace::WorkspaceObject& object : plate.objects)
                count += object.instances.size();
        return count;
    }

    // One more copy of the cube: the canonical mutation these tests drive.
    ToolRequest duplicate_cube_request() const
    {
        ToolRequest request;
        request.tool           = "plate_layout";
        request.arguments_json = json{{"sessionId", std::to_string(workspace.snapshot().session.value())},
                                      {"objects", json::array({json{{"objectId", std::to_string(cube_id().value())},
                                                                    {"quantity", object_count() + 1}}})}}
                                     .dump();
        return request;
    }

    // The canonical Destructive call these tests drive.
    ToolRequest delete_cube_request() const
    {
        ToolRequest request;
        request.tool           = "project_delete_items";
        request.arguments_json = json{{"sessionId", std::to_string(workspace.snapshot().session.value())},
                                      {"items", json::array({json{{"objectId", std::to_string(cube_id().value())}}})}}
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
    // The printer panel's two state tools join them: one draws the card it
    // was asked for, the other offers what to do next, and the person
    // adding or changing a printer approves that on its own card.
    CHECK(exempt == std::vector<std::string>{"activity_cancel", "plan_set", "printer_identify", "printer_suggest",
                                             "slice_start"});
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
    CHECK(duplicate.title == "Lay out: cube-a x2");

    const ToolActivity& inspect = harness.coordinator.propose(ToolRequest{"workspace_inspect", "{}"}, "read-caller");
    CHECK(inspect.action_class == ActionClass::ReadOnly);
    CHECK_FALSE(inspect.requires_approval);
}

// WP7: a chip's tap is the person's approval already. The owner (AgentHost)
// signals that with pre_approved on the one call it names, never as a
// registry-wide policy change.
TEST_CASE("a pre-approved mutation skips its card and runs on its own", "[tools][policy][pre-approved]")
{
    Harness harness;
    const ToolActivity& proposed =
        harness.coordinator.propose(harness.duplicate_cube_request(), "chip-caller", {}, ToolSource::Agent, {}, true);
    CHECK(proposed.action_class == ActionClass::Mutation);
    CHECK_FALSE(proposed.requires_approval);
    harness.pump_to_completion(proposed.action_id);
    CHECK(harness.coordinator.find(proposed.action_id)->state == ToolState::Succeeded);
    CHECK(harness.object_count() == 2);
}

// The shortcut is for a call the person already asked for by name, not a
// blanket bypass: a Destructive action still asks, pre-approved or not.
TEST_CASE("pre-approved never waives a Destructive action's own card", "[tools][policy][pre-approved]")
{
    Harness harness;
    const ToolActivity& proposed =
        harness.coordinator.propose(harness.delete_cube_request(), "chip-caller", {}, ToolSource::Agent, {}, true);
    CHECK(proposed.action_class == ActionClass::Destructive);
    CHECK(proposed.requires_approval);
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

    const ToolActivity& proposed = coordinator.propose(ToolRequest{"workspace_inspect", "{}"}, "m-1");
    const std::string action_id = proposed.action_id;
    while (!tool_state_terminal(coordinator.find(action_id)->state))
        coordinator.pump();
    CHECK(first == second);
    REQUIRE(first.back() == ToolState::Succeeded);
    CHECK(self_notifications == 1);

    const std::size_t first_before = first.size();
    first_subscription.reset();
    const ToolActivity& next = coordinator.propose(ToolRequest{"workspace_inspect", "{}"}, "m-2");
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
    CHECK(result["objects"]["items"][0]["instanceCount"] == 2);

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
        json{{"sessionId", std::to_string(harness.workspace.snapshot().session.value())},
             {"objects", json::array({json{{"objectId", "999999999"}, {"quantity", 2}}})}}.dump();
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
    request.tool           = "workspace_inspect";
    request.arguments_json = "{}";

    const std::string action_id = harness.coordinator.propose(request, "m-2").action_id;
    const ToolActivity* started = harness.coordinator.find(action_id);
    CHECK_FALSE(started->requires_approval);
    CHECK(started->state == ToolState::Running);

    harness.pump_to_completion(action_id);
    const ToolActivity* done = harness.coordinator.find(action_id);
    REQUIRE(done->state == ToolState::Succeeded);
    const json result = json::parse(done->result_json);
    CHECK(result["selection"]["items"] == json::array({std::to_string(harness.cube_id().value())}));
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

TEST_CASE("object_import resolves an attachment ID and adds an object", "[tools][import]")
{
    Harness harness;

    // A stand-in for the stored blob the host would have written.
    const std::filesystem::path model =
        std::filesystem::temp_directory_path() / "jusprin-coordinator-import.stl";
    std::ofstream(model, std::ios::binary) << "solid cube\nendsolid cube\n";
    harness.coordinator.set_attachment_path_resolver(
        [&](const std::string& id) { return id == "a-1" ? model.string() : std::string(); });

    ToolRequest request;
    request.tool           = "object_import";
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
    CHECK(result["objectIds"].size() == 1);

    std::filesystem::remove(model);
}

TEST_CASE("object_import fails when the attachment can no longer be resolved", "[tools][import]")
{
    Harness harness;
    harness.coordinator.set_attachment_path_resolver([](const std::string&) { return std::string(); });

    ToolRequest request;
    request.tool           = "object_import";
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
                                 {"1000C002", "The nozzle is too soft for this filament", true, ""},
                                 {"10018003", "Traditional timelapse may mark the surface", false, "", "timelapse"}};
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
    CHECK_FALSE(findings["findings"]["items"][1].contains("appliesWhen"));
    CHECK(findings["findings"]["items"][2]["appliesWhen"] == "timelapse");
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

TEST_CASE("a save names its file on the card and writes nothing before approval", "[tools][project]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const std::filesystem::path folder = std::filesystem::temp_directory_path() /
        ("jusprin-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const std::string target = (folder / "benchy.3mf").u8string();

    // A project that has never been saved has no file to save to.
    const ToolActivity unsaved = h.coordinator.propose({"project_save", "{}"}, "m-1");
    CHECK(unsaved.state == ToolState::Failed);
    CHECK(unsaved.error->code == "unavailable_operation");
    // G-code is an export, refused before any card.
    const ToolActivity gcode = h.coordinator.propose({"project_save", json{{"path", (folder / "benchy.GCODE").u8string()}}.dump()}, "m-0");
    CHECK(gcode.state == ToolState::Failed);
    CHECK(gcode.error->message.find("export_file") != std::string::npos);

    // A path is bound at proposal time and shown on the card; nothing is
    // written while the card waits, and nothing at all if it is rejected.
    const ToolActivity rejected = h.coordinator.propose({"project_save", json{{"path", target}}.dump()}, "m-2");
    REQUIRE(rejected.state == ToolState::Pending);
    CHECK(rejected.requires_approval);
    CHECK(rejected.title == "Save the project to " + target);
    const std::string rejected_id = rejected.action_id;
    CHECK_FALSE(std::filesystem::exists(folder / "benchy.3mf"));
    REQUIRE(h.coordinator.reject(rejected_id));
    h.coordinator.pump();
    CHECK_FALSE(std::filesystem::exists(folder / "benchy.3mf"));
    CHECK(store.flushes == 0);

    const std::string approved = h.coordinator.propose({"project_save", json{{"path", target}}.dump()}, "m-3").action_id;
    REQUIRE(h.coordinator.approve(approved));
    h.pump_to_completion(approved);
    REQUIRE(h.coordinator.find(approved)->state == ToolState::Succeeded);
    const auto result = json::parse(h.coordinator.find(approved)->result_json);
    CHECK(registry.validate_output(*registry.find("project_save"), result));
    CHECK(result["path"] == target);
    CHECK(result["projectDirty"] == false);
    CHECK(std::filesystem::exists(folder / "benchy.3mf"));
    // The conversation travels in the project, so it went to disk first.
    CHECK(store.flushes == 1);

    // Once the project has a file, a bare save goes there -- and the card says
    // it replaces what is there.
    const ToolActivity again = h.coordinator.propose({"project_save", "{}"}, "m-4");
    REQUIRE(again.state == ToolState::Pending);
    CHECK(again.title == "Save the project, replacing " + target);

    CHECK_FALSE(registry.validate_call(*registry.find("project_save"), R"({"path":""})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("project_save"), R"({"path":"x.3mf","overwrite":true})").valid());
    std::filesystem::remove_all(folder);
}

TEST_CASE("history is read by step and restored to either side of one", "[tools][history]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    auto run = [&h](const char* tool, const json& arguments) {
        const std::string id = h.coordinator.propose({tool, arguments.dump()}, "m-1").action_id;
        if (h.coordinator.find(id)->state == ToolState::Pending)
            REQUIRE(h.coordinator.approve(id));
        h.pump_to_completion(id);
        return *h.coordinator.find(id);
    };
    const auto cube = std::to_string(h.cube_id().value());
    run("plate_layout", json{{"sessionId", session}, {"objects", json::array({json{{"objectId", cube}, {"quantity", 2}}})}});
    run("plate_layout", json{{"sessionId", session}, {"objects", json::array({json{{"objectId", cube}, {"quantity", 3}}})}});
    REQUIRE(h.object_count() == 3);

    const auto read = run("workspace_inspect", json{{"sections", {"history"}}});
    REQUIRE(read.state == ToolState::Succeeded);
    const auto inspected = json::parse(read.result_json);
    CHECK(registry.validate_output(*registry.find("workspace_inspect"), inspected));
    const auto& steps = inspected["history"]["steps"]["items"];
    REQUIRE(steps.size() == 2);
    CHECK(steps[0]["label"] == "Lay out plates");
    CHECK(steps[1]["applied"] == true);
    CHECK(inspected["history"]["restorable"] == true);
    const std::string first = steps[0]["stepId"];

    // The card names the step; nothing moves while it waits.
    const ToolActivity proposed = h.coordinator.propose(
        {"history_restore", json{{"sessionId", session}, {"stepId", first}, {"point", "before"}}.dump()}, "m-2");
    REQUIRE(proposed.state == ToolState::Pending);
    CHECK(proposed.title == "Go back to before \xe2\x80\x9c" "Lay out plates\xe2\x80\x9d");
    CHECK(h.object_count() == 3);
    REQUIRE(h.coordinator.approve(proposed.action_id));
    h.pump_to_completion(proposed.action_id);
    const ToolActivity back = *h.coordinator.find(proposed.action_id);
    REQUIRE(back.state == ToolState::Succeeded);
    CHECK(h.object_count() == 1);
    const auto back_result = json::parse(back.result_json);
    CHECK(registry.validate_output(*registry.find("history_restore"), back_result));
    CHECK(back_result["history"]["steps"]["items"][0]["applied"] == false);
    CHECK(back_result["notReversed"]["items"].empty());

    // The same id still names the step after an undo, so the agent can go
    // forward again -- and what the history does not carry is said to stay.
    PlanRecord plan;
    plan.headline = "Protect the face";
    store.set_plan(plan);
    const auto forward = run("history_restore", json{{"sessionId", session}, {"stepId", first}, {"point", "after"}});
    REQUIRE(forward.state == ToolState::Succeeded);
    CHECK(h.object_count() == 2);
    CHECK(json::parse(forward.result_json)["notReversed"]["items"] == json::array({"the plan"}));

    const auto again = run("history_restore", json{{"sessionId", session}, {"stepId", first}, {"point", "after"}});
    CHECK(again.error->code == "no_change");
    const auto gone = h.coordinator.propose({"history_restore", json{{"sessionId", session}, {"stepId", "999"}, {"point", "after"}}.dump()}, "m-3");
    CHECK(gone.error->code == "stale_id");
    const auto other = h.coordinator.propose({"history_restore", json{{"sessionId", "9999"}, {"stepId", first}, {"point", "after"}}.dump()}, "m-4");
    CHECK(other.error->code == "stale_id");

    h.workspace.set_history_restorable_for_testing(false);
    const auto blocked = run("history_restore", json{{"sessionId", session}, {"stepId", first}, {"point", "before"}});
    CHECK(blocked.error->code == "unavailable_operation");
    CHECK(h.object_count() == 2);

    CHECK_FALSE(registry.validate_call(*registry.find("history_restore"), json{{"sessionId", session}, {"stepId", first}, {"point", "middle"}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(*registry.find("workspace_inspect"), R"({"sections":["history","history"]})").valid());
}

TEST_CASE("the printer section sets what is configured beside what the machine says", "[tools][printer]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    auto inspect = [&h, &registry]() {
        const std::string id = h.coordinator.propose({"workspace_inspect", R"({"sections":["printer"]})"}, "m-1").action_id;
        h.coordinator.pump();
        REQUIRE(h.coordinator.find(id)->state == ToolState::Succeeded);
        const auto result = json::parse(h.coordinator.find(id)->result_json);
        CHECK(registry.validate_output(*registry.find("workspace_inspect"), result));
        return result["printer"];
    };

    Workspace::ConfiguredPrinter configured;
    configured.preset           = "Bambu Lab A1 mini 0.2 nozzle";
    configured.model            = "N1";
    configured.nozzle_diameters = {0.2};
    configured.plate_type       = "Textured PEI Plate";
    configured.filaments        = {{"Bambu PETG HF", "PETG"}};
    h.workspace.set_configured_printer_for_testing(configured);

    // Nothing connected: the configured side alone, facts filed under the preset.
    auto printer = inspect();
    CHECK(printer["configured"]["plateType"] == "Textured PEI Plate");
    CHECK_FALSE(printer.contains("observed"));
    CHECK(printer["factKey"] == "preset:Bambu Lab A1 mini 0.2 nozzle");
    CHECK(printer["mismatches"].empty());
    CHECK(printer["plateObservable"] == false);

    Workspace::PrinterDevice other;
    other.id = "B2"; other.name = "Garage"; other.model = "N1"; other.activity = "idle";
    Workspace::PrinterDevice device;
    device.id = "FAKE001"; device.name = "Desk"; device.model = "N1"; device.activity = "printing";
    device.selected = true; device.nozzle_diameter = 0.4; device.progress_percent = 42; device.job = "benchy";
    device.bed_temperature = 60.; device.materials = {"Bambu PLA Basic"}; device.material_types = {"PLA"};
    h.workspace.set_printers_for_testing({other, device});
    store.confirm_printer_facts("device:FAKE001", {{"plate", "Smooth PEI Plate"}});

    printer = inspect();
    CHECK(printer["observed"]["id"] == "FAKE001");
    CHECK(printer["observed"]["progressPercent"] == 42);
    CHECK(printer["observed"]["job"] == "benchy");
    CHECK(printer["factKey"] == "device:FAKE001");
    REQUIRE(printer["confirmedFacts"].size() == 1);
    CHECK(printer["confirmedFacts"][0]["value"] == "Smooth PEI Plate");
    const auto& mismatches = printer["mismatches"];
    REQUIRE(mismatches.size() == 3);
    CHECK(mismatches[0] == json{{"what", "plate"}, {"configured", "Textured PEI Plate"}, {"observed", "Smooth PEI Plate"}, {"source", "user_confirmed"}});
    CHECK(mismatches[1] == json{{"what", "nozzle"}, {"configured", "0.2"}, {"observed", "0.4"}, {"source", "device"}});
    CHECK(mismatches[2] == json{{"what", "filament"}, {"configured", "PETG"}, {"observed", "PLA"}, {"source", "device"}});

    // What agrees is not a mismatch, whatever the case.
    configured.nozzle_diameters = {0.4};
    configured.filaments        = {{"Generic PLA", "pla"}};
    configured.plate_type       = "smooth pei plate";
    h.workspace.set_configured_printer_for_testing(configured);
    CHECK(inspect()["mismatches"].empty());
}

TEST_CASE("a printer setup is previewed, confirmed on its card, and read back", "[tools][printer][setup]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    Workspace::ConfiguredPrinter configured;
    configured.preset           = "A1 mini 0.4";
    configured.model            = "N1";
    configured.nozzle_diameters = {0.4};
    configured.plate_type       = "Textured PEI Plate";
    configured.filaments        = {{"Generic PLA", "PLA"}};
    h.workspace.set_configured_printer_for_testing(configured);
    h.workspace.set_setup_for_testing("0.20mm Standard", {"Textured PEI Plate", "Cool Plate"},
                                      {{"Generic PLA", "PLA"}, {"Generic PETG", "PETG"}});
    h.workspace.set_presets_for_testing(Workspace::PresetKind::Printer, {{"A1 mini 0.4"}, {"A1 mini 0.2"}});
    h.workspace.set_presets_for_testing(Workspace::PresetKind::Filament, {{"Generic PLA"}, {"Generic PETG"}});
    Workspace::PresetEntry wrong{"0.08mm Fine"};
    wrong.compatible = false;
    h.workspace.set_presets_for_testing(Workspace::PresetKind::Process, {{"0.20mm Standard"}, wrong});
    Workspace::PrinterDevice device;
    device.id = "FAKE001"; device.model = "N1"; device.selected = true; device.nozzle_diameter = 0.4;
    device.material_types = {"PLA"};
    h.workspace.set_printers_for_testing({device});

    auto run = [&h](const char* tool, const json& arguments) {
        const std::string id = h.coordinator.propose({tool, arguments.dump()}, "m-1").action_id;
        if (h.coordinator.find(id)->state == ToolState::Pending)
            REQUIRE(h.coordinator.approve(id));
        h.pump_to_completion(id);
        return *h.coordinator.find(id);
    };

    // The preview changes nothing, and says what would still disagree.
    const auto revision = h.workspace.snapshot().revision;
    const auto previewed = run("printer_setup_preview", json{{"printerPreset", "A1 mini 0.2"}, {"filamentPresets", {"Generic PETG"}}});
    REQUIRE(previewed.state == ToolState::Succeeded);
    const auto preview = json::parse(previewed.result_json);
    CHECK(registry.validate_output(*registry.find("printer_setup_preview"), preview));
    CHECK(preview["valid"] == true);
    CHECK(preview["resulting"]["filamentPresets"] == json::array({"Generic PETG"}));
    REQUIRE(preview["mismatches"].size() == 1);
    CHECK(preview["mismatches"][0]["what"] == "filament");
    CHECK(h.workspace.snapshot().revision == revision);
    CHECK(h.workspace.setup_applies == 0);

    const auto invalid = run("printer_setup_preview", json{{"processPreset", "0.08mm Fine"}, {"plateType", "Glass"}});
    const auto refused = json::parse(invalid.result_json);
    CHECK(refused["valid"] == false);
    CHECK(refused["issues"].size() == 2);
    CHECK(run("printer_setup", json{{"plateType", "Glass"}}).error->code == "unsupported_plate");

    // Unsaved edits are refused until the caller says to drop them, and the
    // card names them.
    h.workspace.m_process_dirty = true;
    h.workspace.m_process_incompatible_after_printer = true;
    CHECK(run("printer_setup", json{{"printerPreset", "A1 mini 0.2"}}).error->code == "unsaved_edits");
    CHECK(h.workspace.setup_applies == 0);
    const ToolActivity card = h.coordinator.propose(
        {"printer_setup", json{{"printerPreset", "A1 mini 0.2"}, {"plateType", "Cool Plate"}, {"unsavedEdits", "discard"},
                               {"confirmFacts", {{{"fact", "plate"}, {"value", "Cool Plate"}, {"hours", 8}}}}}.dump()}, "m-2");
    REQUIRE(card.state == ToolState::Pending);
    CHECK(card.title == "Set up A1 mini 0.2, Cool Plate; replaces process 0.20mm Standard; "
                        "discards 2 unsaved edits in 0.20mm Standard; plate: Cool Plate");
    CHECK(h.workspace.setup_applies == 0);
    REQUIRE(h.coordinator.approve(card.action_id));
    h.pump_to_completion(card.action_id);
    const ToolActivity done = *h.coordinator.find(card.action_id);
    REQUIRE(done.state == ToolState::Succeeded);
    const auto result = json::parse(done.result_json);
    CHECK(registry.validate_output(*registry.find("printer_setup"), result));
    CHECK(result["printer"]["configured"]["preset"] == "A1 mini 0.2");
    CHECK(result["processPreset"] == "substitute process");
    CHECK(result["substituted"][0]["kind"] == "process");
    CHECK(result["discarded"][0]["count"] == 2);
    REQUIRE(result["printer"]["confirmedFacts"].size() == 1);
    CHECK(result["printer"]["mismatches"].empty());
    CHECK(h.workspace.setup_applies == 1);

    // A setup proposed against presets that have since changed does not apply.
    const ToolActivity stale = h.coordinator.propose({"printer_setup", json{{"printerPreset", "A1 mini 0.4"}}.dump()}, "m-3");
    REQUIRE(stale.state == ToolState::Pending);
    h.workspace.set_setup_for_testing("0.20mm Standard", {"Textured PEI Plate"}, {});
    h.workspace.m_process_incompatible_after_printer = false;
    h.workspace.m_process_dirty = false;
    REQUIRE(h.coordinator.approve(stale.action_id));
    h.pump_to_completion(stale.action_id);
    CHECK(h.coordinator.find(stale.action_id)->error->code == "stale_workspace");
    CHECK(h.workspace.setup_applies == 1);

    // Facts alone need no preset change.
    const auto facts = run("printer_setup", json{{"confirmFacts", {{{"fact", "bed_clear"}, {"value", "yes"}}}}});
    REQUIRE(facts.state == ToolState::Succeeded);
    CHECK(facts.title == "Record printer facts; bed_clear: yes");
    CHECK(json::parse(facts.result_json)["printer"]["confirmedFacts"].size() == 2);

    CHECK_FALSE(registry.validate_call(*registry.find("printer_setup"), "{}").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("printer_setup"), R"({"printerPreset":"x","unsavedEdits":"keep"})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("printer_setup_preview"), R"({"printerPreset":"x","unsavedEdits":"discard"})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("printer_setup"), R"({"confirmFacts":[{"fact":"plate","value":"x","hours":0}]})").valid());
}

TEST_CASE("opening a project names it on the card, reads nothing before approval, and reports what was asked", "[tools][project]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const std::filesystem::path folder = std::filesystem::temp_directory_path() /
        ("jusprin-open-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const std::string model = (folder / "bracket.stl").u8string();
    std::ofstream(folder / "bracket.stl") << "solid bracket\nendsolid bracket\n";
    auto propose = [&h](const json& arguments) {
        return ToolActivity(h.coordinator.propose({"project_open", arguments.dump()}, "m-1"));
    };

    CHECK(propose(json{{"path", (folder / "missing.stl").u8string()}}).error->code == "invalid_argument");
    CHECK(propose(json{{"path", "bracket.stl"}}).error->code == "invalid_argument");

    // A dirty project is not replaced without the user's word.
    auto setup = h.workspace.snapshot().setup;
    setup.project_dirty = true;
    setup.project_name  = "Backpack";
    h.workspace.set_setup(setup);
    CHECK(propose(json{{"path", model}}).error->code == "unsaved_work");

    const ToolActivity rejected = propose(json{{"path", model}, {"unsavedWork", "discard"}});
    REQUIRE(rejected.state == ToolState::Pending);
    CHECK(rejected.title == "Open " + model + ", discarding unsaved changes to Backpack");
    REQUIRE(h.coordinator.reject(rejected.action_id));
    CHECK(h.workspace.opens == 0);

    h.workspace.m_open_decisions = {{"Object too small", "yes"}};
    const ToolActivity approved = propose(json{{"path", model}, {"unsavedWork", "discard"}, {"unitConversion", "convertIfTiny"},
                                               {"oversized", "scaleToFit"}});
    REQUIRE(h.coordinator.approve(approved.action_id));
    for (int tick = 0; tick < 10 && h.coordinator.find(approved.action_id) != nullptr; ++tick)
        h.coordinator.pump();
    // The open's record leaves with the project it closed; its last event
    // carries the result.
    const auto last = std::find_if(h.events.rbegin(), h.events.rend(),
                                   [&](const ToolActivity& event) { return event.action_id == approved.action_id; });
    REQUIRE(last != h.events.rend());
    const ToolActivity done = *last;
    REQUIRE(done.state == ToolState::Succeeded);
    const auto result = json::parse(done.result_json);
    CHECK(registry.validate_output(*registry.find("project_open"), result));
    CHECK(result["projectName"] == "bracket");
    CHECK(result["objectCount"] == 1);
    CHECK(result["decisions"] == json::array({json{{"question", "Object too small"}, {"answer", "yes"}}}));
    CHECK(result["sessionId"] != std::to_string(approved.session));
    CHECK(h.workspace.last_open.units == Workspace::UnitChoice::ConvertIfTiny);
    CHECK(h.workspace.last_open.scale_oversized);
    CHECK(h.workspace.last_open.discard_unsaved);
    CHECK(store.flushes == 1);

    // The host forgets the old project's activities while the open is still
    // running; the open itself survives to report.
    h.workspace.on_open_for_testing = [&h] { h.coordinator.clear(); };
    const ToolActivity earlier  = propose(json{{"path", model}});
    const ToolActivity reopened = propose(json{{"path", model}});
    REQUIRE(h.coordinator.approve(reopened.action_id));
    for (int tick = 0; tick < 10 && h.coordinator.find(reopened.action_id) != nullptr; ++tick)
        h.coordinator.pump();
    // Its subscribers heard the result; the record then left with the project
    // it belonged to, so the new project's ids cannot meet it.
    const auto states = h.states_of(reopened.action_id);
    REQUIRE_FALSE(states.empty());
    CHECK(states.back() == ToolState::Succeeded);
    CHECK(h.coordinator.find(reopened.action_id) == nullptr);
    CHECK(h.coordinator.find(earlier.action_id) == nullptr);
    CHECK(h.coordinator.executing_action_id().empty());
    h.workspace.on_open_for_testing = nullptr;

    const ToolActivity fresh = propose(json{{"new", true}});
    REQUIRE(fresh.state == ToolState::Pending);
    CHECK(fresh.title == "Start a new project");

    const auto& definition = *registry.find("project_open");
    CHECK_FALSE(registry.validate_call(definition, "{}").valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"path", model}, {"new", true}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(definition, R"({"new":false})").valid());
    CHECK_FALSE(registry.validate_call(definition, R"({"new":true,"unitConversion":"inches"})").valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"path", model}, {"oversized", "shrink"}}.dump()).valid());
    CHECK(propose(json{{"path", (folder / "x.3mf").u8string()}}).error->code == "invalid_argument");
    std::ofstream(folder / "x.3mf") << "x";
    CHECK(propose(json{{"path", (folder / "x.3mf").u8string()}, {"unitConversion", "inches"}}).error->code == "invalid_argument");
    std::filesystem::remove_all(folder);
}

TEST_CASE("the project section reports the file's own words with their source", "[tools][project]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    h.workspace.set_project_path_for_testing("C:/prints/bracket.3mf", true);
    h.workspace.m_details.designer    = "Ada";
    h.workspace.m_details.license     = "CC-BY-NC-4.0";
    h.workspace.m_details.attachments = {{"Model Pictures/front.png", "Model Pictures", 2048}};
    h.workspace.m_details.backup_current = false;
    const std::string id = h.coordinator.propose({"workspace_inspect", R"({"sections":["project"]})"}, "m-1").action_id;
    h.coordinator.pump();
    REQUIRE(h.coordinator.find(id)->state == ToolState::Succeeded);
    const auto result = json::parse(h.coordinator.find(id)->result_json);
    CHECK(registry.validate_output(*registry.find("workspace_inspect"), result));
    const auto& project = result["project"];
    CHECK(project["path"] == "C:/prints/bracket.3mf");
    CHECK(project["dirty"] == true);
    CHECK(project["details"]["license"] == json{{"value", "CC-BY-NC-4.0"}, {"provenance", "project_file"}});
    CHECK_FALSE(project["details"].contains("description"));
    CHECK(project["attachments"]["items"][0]["attachmentId"] == "Model Pictures/front.png");
    CHECK(project["backupCurrent"] == false);
    CHECK_FALSE(result.contains("plates"));
}

TEST_CASE("object analysis reports geometry, and a measure fails once its handles expire", "[tools][geometry]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    Workspace::ObjectAnalysis analysis;
    Workspace::MeshFacts mesh;
    mesh.size = {20, 20, 20};
    mesh.volume = 8000;
    mesh.facets = 12;
    mesh.parts = 1;
    mesh.units_suspicion = "inches";
    analysis.mesh = mesh;
    Workspace::ObjectFeatures features;
    features.faces = {{"f1-21-0-0-0", 400, {0, 0, -1}, {10, 10, 0}, {20, 20}}};
    features.holes = {{"f1-21-0-1-0", 5.2, {10, 10, 20}, {0, 0, 1}}};
    analysis.features = features;
    Workspace::ObjectFit fit;
    fit.instances = {{0, h.workspace.snapshot().plates[0].id, true}};
    analysis.fit = fit;
    Workspace::FeatureMeasurement measurement;
    measurement.distance = 20;
    measurement.angle = 180;
    analysis.measurement = measurement;
    h.workspace.set_analysis_for_testing(h.cube_id(), analysis);

    auto run = [&h](const json& arguments) {
        const std::string id = h.coordinator.propose({"object_analyze", arguments.dump()}, "m-1").action_id;
        h.coordinator.pump();
        return *h.coordinator.find(id);
    };
    const auto read = run(json{{"sessionId", session}, {"objectId", cube}, {"include", {"mesh", "features", "fit"}}});
    REQUIRE(read.state == ToolState::Succeeded);
    CHECK_FALSE(read.requires_approval);
    const auto result = json::parse(read.result_json);
    CHECK(registry.validate_output(*registry.find("object_analyze"), result));
    CHECK(result["mesh"]["unitsSuspicion"] == "inches");
    CHECK(result["features"]["holes"][0]["diameterMm"] == 5.2);
    CHECK(result["fit"]["instances"][0]["inside"] == true);
    CHECK_FALSE(result.contains("measurement"));

    const json measure{{"sessionId", session}, {"objectId", cube}, {"include", {"measure"}},
                       {"measure", {{"from", "f1-21-0-0-0"}, {"to", "f1-21-0-1-0"}}}};
    const auto measured = run(measure);
    REQUIRE(measured.state == ToolState::Succeeded);
    CHECK(json::parse(measured.result_json)["measurement"]["distanceMm"] == 20);

    // Any change to the project expires the handles.
    h.workspace.record_step_for_testing("Move");
    CHECK(run(measure).error->code == "feature_expired");

    CHECK(run(json{{"sessionId", session}, {"objectId", "999"}, {"include", {"mesh"}}}).state == ToolState::Failed);
    const auto& definition = *registry.find("object_analyze");
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"objectId", cube}, {"include", {"measure"}}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"objectId", cube}, {"include", {"mesh"}},
                                                        {"measure", {{"from", "a"}, {"to", "b"}}}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"objectId", cube}, {"include", {"regions", "regions"}}}.dump()).valid());

    const std::string inspect = h.coordinator.propose({"workspace_inspect", R"({"sections":["objects"]})"}, "m-2").action_id;
    h.coordinator.pump();
    const auto objects = json::parse(h.coordinator.find(inspect)->result_json);
    CHECK(registry.validate_output(*registry.find("workspace_inspect"), objects));
    REQUIRE(objects["objects"]["items"].size() == 1);
    CHECK(objects["objects"]["items"][0]["objectId"] == cube);
}

TEST_CASE("orientation candidates are scored as given, or as Orca proposes", "[tools][geometry]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    Workspace::ObjectAnalysis analysis;
    analysis.orientations = std::vector<Workspace::OrientationOption>{{{0, 0, 1}, 1.5, 0, 400, {"f1-21-0-0-0"}},
                                                                      {{1, 0, 0}, 9.25, 120.5, 20, {}}};
    h.workspace.set_analysis_for_testing(h.cube_id(), analysis);
    auto run = [&h](const json& arguments) {
        const std::string id = h.coordinator.propose({"object_analyze", arguments.dump()}, "m-1").action_id;
        h.coordinator.pump();
        return *h.coordinator.find(id);
    };
    const auto own = run(json{{"sessionId", session}, {"objectId", cube}, {"include", {"orientations"}}});
    REQUIRE(own.state == ToolState::Succeeded);
    const auto result = json::parse(own.result_json);
    CHECK(registry.validate_output(*registry.find("object_analyze"), result));
    CHECK(result["orientations"][0]["facesDown"][0] == "f1-21-0-0-0");
    CHECK(h.workspace.last_candidates.empty());

    const auto given = run(json{{"sessionId", session}, {"objectId", cube}, {"include", {"orientations"}},
                                {"candidates", {{{"up", {0, 0, 1}}}, {{"faceDown", "f1-21-0-0-0"}}}}});
    REQUIRE(given.state == ToolState::Succeeded);
    REQUIRE(h.workspace.last_candidates.size() == 2);
    CHECK((*h.workspace.last_candidates[0].up)[2] == 1);
    CHECK(h.workspace.last_candidates[1].face_down == "f1-21-0-0-0");

    const auto& definition = *registry.find("object_analyze");
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"objectId", cube}, {"include", {"mesh"}},
                                                        {"candidates", {{{"up", {0, 0, 1}}}}}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"objectId", cube}, {"include", {"orientations"}},
                                                        {"candidates", {{{"up", {0, 1}}}}}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"objectId", cube}, {"include", {"orientations"}},
                                                        {"candidates", {{{"up", {0, 0, 1}}, {"faceDown", "x"}}}}}.dump()).valid());
}

TEST_CASE("a placement names mirror and scale on its card, and auto-orient reports through the slicing section", "[tools][geometry]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    auto propose = [&h](const json& arguments) { return ToolActivity(h.coordinator.propose({"object_place", arguments.dump()}, "m-1")); };

    const ToolActivity card = propose(json{{"sessionId", session}, {"objectId", cube}, {"scale", {2, 2, 2}},
                                           {"mirrorAxis", "x"}, {"rotateDegrees", {0, 0, 90}}, {"position", {100, 90}}});
    REQUIRE(card.state == ToolState::Pending);
    CHECK(card.title == "Place cube-a: scale x2 y2 z2, mirror in x, rotate 0/0/90 degrees, move to 100, 90");
    REQUIRE(h.coordinator.approve(card.action_id));
    h.pump_to_completion(card.action_id);
    const ToolActivity placed = *h.coordinator.find(card.action_id);
    REQUIRE(placed.state == ToolState::Succeeded);
    const auto result = json::parse(placed.result_json);
    CHECK(registry.validate_output(*registry.find("object_place"), result));
    CHECK(result["transform"]["positionMm"][0] == 100);
    CHECK_FALSE(result.contains("handle"));
    CHECK(h.workspace.last_placement.mirror_axis == "x");
    CHECK((*h.workspace.last_placement.scale)[1] == 2);

    // Auto-orient returns a handle; its end is read from the slicing section.
    const ToolActivity orient = propose(json{{"sessionId", session}, {"objectId", cube}, {"autoOrient", true}});
    REQUIRE(h.coordinator.approve(orient.action_id));
    h.pump_to_completion(orient.action_id);
    const auto oriented = json::parse(h.coordinator.find(orient.action_id)->result_json);
    REQUIRE(oriented["handle"] == orient.action_id);
    auto jobs = [&h]() {
        const std::string id = h.coordinator.propose({"workspace_inspect", R"({"sections":["slicing"]})"}, "m-2").action_id;
        h.coordinator.pump();
        const auto inspected = json::parse(h.coordinator.find(id)->result_json);
        CHECK(ToolRegistry::instance().validate_output(*ToolRegistry::instance().find("workspace_inspect"), inspected));
        return inspected["slicing"]["jobs"];
    };
    CHECK(jobs()[0]["state"] == "running");
    h.workspace.finish_job_for_testing(orient.action_id, "finished");
    CHECK(jobs()[0] == json{{"handle", orient.action_id}, {"kind", "orient"}, {"state", "finished"}, {"notPlaced", json::array()}});

    const auto& definition = *registry.find("object_place");
    const auto invalid = [&](json extra) {
        json arguments{{"sessionId", session}, {"objectId", cube}};
        arguments.update(extra);
        return !registry.validate_call(definition, arguments.dump()).valid();
    };
    CHECK(invalid(json::object()));
    CHECK(invalid(json{{"faceDown", "f1-21-0-0-0"}, {"autoOrient", true}}));
    CHECK(invalid(json{{"scale", {2, 2, 2}}, {"unitsFix", "inches"}}));
    CHECK(invalid(json{{"scale", {2, 0, 2}}}));
    CHECK(invalid(json{{"scaleTo", {{"axis", "w"}, {"sizeMm", 10}}}}));
    CHECK(invalid(json{{"position", {1, 2, 3}}}));
    CHECK(invalid(json{{"dropToBed", false}}));
    CHECK(invalid(json{{"instance", 0}}));
    CHECK_FALSE(invalid(json{{"instance", 0}, {"dropToBed", true}}));
}

TEST_CASE("a layout names every change on its card and arranges as a job", "[tools][layout]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const auto snapshot = h.workspace.snapshot();
    const std::string session = std::to_string(snapshot.session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    const std::string plate   = std::to_string(snapshot.plates[0].id.value());
    const ToolActivity card = h.coordinator.propose(
        {"plate_layout", json{{"sessionId", session},
                              {"objects", {{{"objectId", cube}, {"quantity", 3}, {"name", "leg"}}}},
                              {"plates", {{{"name", "Second"}, {"bedType", "Cool Plate"}}}},
                              {"arrange", {{"spacingMm", 5}}}}.dump()}, "m-1");
    REQUIRE(card.state == ToolState::Pending);
    CHECK(card.title == "Lay out: cube-a x3, rename cube-a to leg, add a plate, name a new plate Second, "
                        "a new plate on Cool Plate, arrange all plates");
    REQUIRE(h.coordinator.approve(card.action_id));
    h.pump_to_completion(card.action_id);
    const ToolActivity done = *h.coordinator.find(card.action_id);
    REQUIRE(done.state == ToolState::Succeeded);
    const auto result = json::parse(done.result_json);
    CHECK(registry.validate_output(*registry.find("plate_layout"), result));
    CHECK(result["objects"]["items"][0]["instanceCount"] == 3);
    CHECK(result["objects"]["items"][0]["name"] == "leg");
    CHECK(result["addedPlateIds"].size() == 1);
    CHECK(result["handle"] == card.action_id);
    REQUIRE(h.workspace.last_layout.arrange);
    CHECK(*h.workspace.last_layout.arrange->spacing == 5);
    CHECK(h.workspace.snapshot().jobs.back().kind == "arrange");

    const auto& definition = *registry.find("plate_layout");
    const auto invalid = [&](json extra) {
        json arguments{{"sessionId", session}};
        arguments.update(extra);
        return !registry.validate_call(definition, arguments.dump()).valid();
    };
    CHECK(invalid(json::object()));
    CHECK(invalid(json{{"objects", {{{"objectId", cube}}}}}));
    CHECK(invalid(json{{"objects", {{{"objectId", cube}, {"quantity", 0}}}}}));
    CHECK(invalid(json{{"objects", {{{"objectId", cube}, {"quantity", 2}}, {{"objectId", cube}, {"quantity", 3}}}}}));
    CHECK(invalid(json{{"plates", {{{"plateId", plate}}}}}));
    CHECK(invalid(json{{"arrange", {{"spacingMm", -1}}}}));
    CHECK_FALSE(invalid(json{{"arrange", json::object()}}));
    CHECK_FALSE(invalid(json{{"plates", json::array({json::object()})}}));
}

TEST_CASE("a file import names its path, reads nothing before approval, and a delete names what goes", "[tools][import][delete]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    const std::filesystem::path folder = std::filesystem::temp_directory_path() /
        ("jusprin-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const std::string model = (folder / "bracket.stl").u8string();
    std::ofstream(folder / "bracket.stl") << "solid bracket\nendsolid bracket\n";

    CHECK(h.coordinator.propose({"object_import_file", json{{"sessionId", session}, {"path", (folder / "none.stl").u8string()}}.dump()}, "m-1")
              .error->code == "invalid_argument");
    const ToolActivity card = h.coordinator.propose(
        {"object_import_file", json{{"sessionId", session}, {"path", model}, {"unitConversion", "inches"}}.dump()}, "m-1");
    REQUIRE(card.state == ToolState::Pending);
    CHECK(card.title == "Import " + model);
    REQUIRE(h.coordinator.reject(card.action_id));
    CHECK(h.workspace.last_import.path.empty());

    h.workspace.m_open_decisions = {{"Object too small", "no"}};
    const ToolActivity approved = h.coordinator.propose(
        {"object_import_file", json{{"sessionId", session}, {"path", model}, {"unitConversion", "inches"}}.dump()}, "m-2");
    REQUIRE(h.coordinator.approve(approved.action_id));
    h.pump_to_completion(approved.action_id);
    const ToolActivity imported = *h.coordinator.find(approved.action_id);
    REQUIRE(imported.state == ToolState::Succeeded);
    const auto result = json::parse(imported.result_json);
    CHECK(registry.validate_output(*registry.find("object_import_file"), result));
    CHECK(result["objectIds"].size() == 1);
    CHECK(result["decisions"][0]["answer"] == "no");
    CHECK(h.workspace.last_import.path == model);
    CHECK(h.workspace.last_import.units == Workspace::UnitChoice::Inches);
    const std::string bracket = result["objectIds"][0];

    const ToolActivity removal = h.coordinator.propose(
        {"project_delete_items", json{{"sessionId", session},
                                      {"items", {{{"objectId", bracket}}, {{"objectId", cube}, {"instance", 0}},
                                                 {{"plateId", std::to_string(h.workspace.snapshot().plates[0].id.value())}}}}}.dump()}, "m-3");
    REQUIRE(removal.state == ToolState::Pending);
    CHECK(removal.action_class == ActionClass::Destructive);
    CHECK(removal.title == "Delete bracket, copy 1 of cube-a, Plate 1");
    REQUIRE(h.coordinator.approve(removal.action_id));
    h.pump_to_completion(removal.action_id);
    const ToolActivity removed = *h.coordinator.find(removal.action_id);
    REQUIRE(removed.state == ToolState::Succeeded);
    CHECK(registry.validate_output(*registry.find("project_delete_items"), json::parse(removed.result_json)));
    REQUIRE(h.workspace.last_delete.size() == 3);
    CHECK(h.workspace.last_delete[0].kind == Workspace::DeleteItem::Kind::Object);
    CHECK(h.workspace.last_delete[1].kind == Workspace::DeleteItem::Kind::Instance);
    CHECK(h.workspace.last_delete[2].kind == Workspace::DeleteItem::Kind::Plate);

    const auto& definition = *registry.find("project_delete_items");
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"items", json::array()}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session},
                                                        {"items", {{{"objectId", cube}, {"partId", "1"}, {"instance", 0}}}}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(definition, json{{"sessionId", session}, {"items", {{{"plateId", "1"}, {"objectId", cube}}}}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(*registry.find("object_import_file"), json{{"sessionId", session}, {"path", model}, {"oversized", "huge"}}.dump()).valid());
    std::filesystem::remove_all(folder);
}

TEST_CASE("settings search can keep only writable or unsaved settings", "[tools][settings]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    h.workspace.set_setting_for_testing("brim_width", "8");
    auto search = [&h](const json& arguments) {
        const std::string id = h.coordinator.propose({"settings_search", arguments.dump()}, "m-1").action_id;
        h.coordinator.pump();
        const auto result = json::parse(h.coordinator.find(id)->result_json);
        CHECK(ToolRegistry::instance().validate_output(*ToolRegistry::instance().find("settings_search"), result));
        std::vector<std::string> keys;
        for (const auto& item : result["items"]) keys.push_back(item["key"]);
        return keys;
    };
    CHECK(search(json{{"query", ""}, {"changedOnly", true}}) == std::vector<std::string>{"brim_width"});
    const auto writable = search(json{{"query", ""}, {"writable", true}, {"limit", 25}});
    CHECK(std::find(writable.begin(), writable.end(), "notes") == writable.end());
    CHECK(std::find(writable.begin(), writable.end(), "layer_height") != writable.end());
    CHECK_FALSE(registry.validate_call(*registry.find("settings_search"), R"({"query":"","writable":"yes"})").valid());
}

TEST_CASE("settings tools read and write one object's overrides", "[tools][settings]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const std::string cube = std::to_string(h.cube_id().value());
    const json target{{"objectId", cube}};
    auto run = [&h](const std::string& tool, const json& arguments) {
        const std::string id = h.coordinator.propose({tool, arguments.dump()}, "m-1").action_id;
        h.coordinator.pump();
        return *h.coordinator.find(id);
    };
    auto read_walls = [&](const json& arguments) {
        const auto result = json::parse(run("settings_get", arguments).result_json);
        CHECK(registry.validate_output(*registry.find("settings_get"), result));
        return result["items"][0];
    };

    CHECK(read_walls(json{{"keys", {"wall_loops"}}, {"target", target}}) ==
          json{{"key", "wall_loops"}, {"value", "2"}, {"type", "integer"}, {"label", "Wall loops"}, {"unit", ""},
               {"differsFromPreset", false}, {"differsFromSystem", false}, {"writable", true}, {"overridden", false}});

    const auto scope = json::parse(run("settings_preview_patch", json{{"changes", {{"skirt_loops", 2}}}, {"target", target}}).result_json);
    CHECK_FALSE(scope["valid"].get<bool>());
    CHECK(scope["issues"][0]["code"] == "unsupported_scope");

    const auto snapshot = h.workspace.snapshot();
    json apply{{"changes", {{"wall_loops", 4}}}, {"target", target},
               {"expectedSessionId", std::to_string(snapshot.session.value())}, {"expectedRevision", snapshot.revision}};
    const ToolActivity pending = h.coordinator.propose({"settings_apply_patch", apply.dump()}, "m-2");
    REQUIRE(pending.state == ToolState::Pending);
    CHECK(pending.title == "Change 1 settings of " + snapshot.plates[0].objects[0].name + ": wall_loops");
    REQUIRE(h.coordinator.approve(pending.action_id));
    h.pump_to_completion(pending.action_id);
    const auto applied = json::parse(h.coordinator.find(pending.action_id)->result_json);
    CHECK(registry.validate_output(*registry.find("settings_apply_patch"), applied));
    CHECK(applied["projectUndo"] == true);
    CHECK(applied["changes"][0] == json{{"key", "wall_loops"}, {"before", "2"}, {"after", "4"}});
    CHECK(h.workspace.snapshot().can_undo);

    CHECK(read_walls(json{{"keys", {"wall_loops"}}, {"target", target}})["overridden"] == true);
    CHECK(read_walls(json{{"keys", {"wall_loops"}}, {"target", target}})["value"] == "4");
    CHECK_FALSE(read_walls(json{{"keys", {"wall_loops"}}}).contains("overridden"));
    CHECK(read_walls(json{{"keys", {"wall_loops"}}})["value"] == "2");

    const auto missing = run("settings_get", json{{"keys", {"wall_loops"}}, {"target", {{"objectId", "987654"}}}});
    CHECK(missing.state == ToolState::Failed);
    CHECK(missing.error->code == "missing_object");

    for (const char* bad : {R"({"keys":["wall_loops"],"target":{"objectId":5}})",
                            R"({"keys":["wall_loops"],"target":{"objectId":"5","plate":"1"}})",
                            R"({"changes":{"wall_loops":3},"target":{}})"})
        CHECK_FALSE(registry.validate_call(*registry.find(std::string(bad).find("keys") != std::string::npos ? "settings_get" : "settings_preview_patch"), bad).valid());
}

TEST_CASE("region annotations are approved, read back with their status, regenerated and deleted", "[tools][regions]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    h.workspace.set_analysis_for_testing(h.cube_id(), {});
    auto run = [&h](const std::string& tool, const json& arguments) {
        const ToolActivity proposed = h.coordinator.propose({tool, arguments.dump()}, "m-1");
        if (proposed.state == ToolState::Pending)
            REQUIRE(h.coordinator.approve(proposed.action_id));
        h.pump_to_completion(proposed.action_id);
        return *h.coordinator.find(proposed.action_id);
    };
    auto regions = [&]() {
        const auto done = run("object_analyze", json{{"sessionId", session}, {"objectId", cube}, {"include", {"regions"}}});
        REQUIRE(done.state == ToolState::Succeeded);
        const auto result = json::parse(done.result_json);
        CHECK(registry.validate_output(*registry.find("object_analyze"), result));
        return result["regions"]["items"];
    };

    const json annotate{{"sessionId", session},
                        {"regions", {{{"objectId", cube}, {"kind", "precision_hole"}, {"geometry", {{"type", "hole"}, {"handle", "f1-21-0-1-0"}}}},
                                     {{"objectId", cube}, {"kind", "hidden"}, {"geometry", {{"type", "face"}, {"handle", "f1-21-0-0-0"}}}}}}};
    const ToolActivity pending = h.coordinator.propose({"region_annotate", annotate.dump()}, "m-2");
    REQUIRE(pending.state == ToolState::Pending);
    CHECK(pending.title.find("r1 precision hole, hole of 5 mm on") != std::string::npos);
    CHECK(pending.title.find("support blocker volume") != std::string::npos);
    CHECK(pending.title.find("r2 hidden") != std::string::npos);
    REQUIRE(h.coordinator.approve(pending.action_id));
    h.pump_to_completion(pending.action_id);
    const auto annotated = *h.coordinator.find(pending.action_id);
    REQUIRE(annotated.state == ToolState::Succeeded);
    const auto result = json::parse(annotated.result_json);
    CHECK(registry.validate_output(*registry.find("region_annotate"), result));
    CHECK(result["projectUndo"] == true);
    CHECK(result["regions"][1]["artifacts"] == json::array({"seam enforcer paint on 3 facets"}));
    REQUIRE(store.regions().size() == 2);
    CHECK(store.regions()[0].provenance == "user_confirmed");

    auto listed = regions();
    REQUIRE(listed.size() == 2);
    CHECK(listed[0]["regionId"] == "r1");
    CHECK(listed[0]["bindingLost"] == false);
    CHECK(listed[0]["artifactsMissing"] == false);

    // Undo took the artifacts, a mesh edit took the geometry; the records stay.
    h.workspace.drop_region_artifacts_for_testing("r1");
    h.workspace.unbind_region_for_testing("r2");
    listed = regions();
    CHECK(listed[0]["artifactsMissing"] == true);
    CHECK(listed[1]["bindingLost"] == true);

    // Regenerating by id keeps the record and brings the artifacts back.
    CHECK(run("region_annotate", json{{"sessionId", session}, {"regions", {{{"regionId", "r1"}}}}}).state == ToolState::Succeeded);
    CHECK(regions()[0]["artifactsMissing"] == false);
    CHECK(store.regions().size() == 2);

    const auto refused = h.coordinator.propose({"region_annotate", json{{"sessionId", session},
        {"regions", {{{"objectId", cube}, {"kind", "precision_hole"}, {"geometry", {{"type", "face"}, {"handle", "f1-21-0-0-0"}}}}}}}.dump()}, "m-3");
    CHECK(refused.state == ToolState::Failed);
    CHECK(refused.error->code == "invalid_argument");
    CHECK(h.coordinator.propose({"region_annotate", json{{"sessionId", session}, {"regions", {{{"regionId", "r9"}}}}}.dump()}, "m-4")
              .error->code == "invalid_argument");
    for (const json& bad : {json{{"sessionId", session}, {"regions", json::array()}},
                            json{{"sessionId", session}, {"regions", {{{"objectId", cube}, {"kind", "hidden"}}}}},
                            json{{"sessionId", session}, {"regions", {{{"objectId", cube}, {"kind", "hidden"},
                                                                     {"geometry", {{"type", "direction"}, {"vector", {0, 0, 1}}}}}}}},
                            json{{"sessionId", session}, {"regions", {{{"objectId", cube}, {"kind", "reinforce"},
                                                                     {"geometry", {{"type", "box"}, {"center", {0, 0, 0}}}}}}}}})
        CHECK_FALSE(registry.validate_call(*registry.find("region_annotate"), bad.dump()).valid());
    CHECK(registry.validate_call(*registry.find("region_annotate"),
                                 json{{"sessionId", session}, {"regions", {{{"objectId", cube}, {"kind", "reinforce"}, {"settings", {{"wall_loops", 6}}},
                                                                           {"geometry", {{"type", "box"}, {"center", {0, 0, 0}}, {"sizeMm", {5, 5, 5}}}}}}}}.dump())
              .arguments_json.find("\"wall_loops\":\"6\"") != std::string::npos);

    const ToolActivity removal = h.coordinator.propose({"project_delete_items", json{{"sessionId", session}, {"items", {{{"regionId", "r2"}}}}}.dump()}, "m-5");
    CHECK(removal.title.find("region r2 (hidden") != std::string::npos);
    REQUIRE(h.coordinator.approve(removal.action_id));
    h.pump_to_completion(removal.action_id);
    const auto removed = json::parse(h.coordinator.find(removal.action_id)->result_json);
    CHECK(registry.validate_output(*registry.find("project_delete_items"), removed));
    CHECK(removed["removedRegions"] == json::array({"r2"}));
    REQUIRE(store.regions().size() == 1);
    CHECK(store.regions()[0].id == "r1");
    CHECK(run("project_delete_items", json{{"sessionId", session}, {"items", {{{"regionId", "r2"}}}}}).error->code == "invalid_argument");
}

TEST_CASE("region records survive the project document round trip", "[tools][regions][persistence]")
{
    ProjectStateDocument document;
    Workspace::RegionRecord record;
    record.id          = "r3";
    record.kind        = "no_support";
    record.object_name = "tee";
    record.part_facets = {224};
    record.geometry.type     = "hole";
    record.geometry.center   = {0, 0, 28};
    record.geometry.normal   = {0, -1, 0};
    record.geometry.diameter = 10;
    record.geometry.length   = 20;
    record.artifacts = {{"volume", "support_blocker", "", "JusPrin r3 support blocker", 0, {}},
                        {"paint", "seam", "blocker", "", 0, {4, 5}}};
    const auto stored = document.set_regions({record}, "2026-09-16T00:00:00Z");
    REQUIRE(stored[0].seq > 0);
    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) != ProjectStateDocument::LoadResult::Corrupt);
    const auto loaded = reloaded.regions();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == "r3");
    CHECK(loaded[0].geometry.center[2] == 28);
    CHECK(loaded[0].geometry.length == 20);
    CHECK(loaded[0].artifacts[1].facets == std::vector<int>{4, 5});
    CHECK(loaded[0].artifacts[0].name == "JusPrin r3 support blocker");
    CHECK(loaded[0].updated_at == "2026-09-16T00:00:00Z");
}

TEST_CASE("objects are divided after a preview, merged and repaired, and unbound regions are listed", "[tools][reshape]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    auto run = [&h, &registry](const std::string& tool, const json& arguments) {
        const ToolActivity proposed = h.coordinator.propose({tool, arguments.dump()}, "m-1");
        if (proposed.state == ToolState::Pending)
            REQUIRE(h.coordinator.approve(proposed.action_id));
        h.pump_to_completion(proposed.action_id);
        const ToolActivity done = *h.coordinator.find(proposed.action_id);
        if (done.state == ToolState::Succeeded)
            CHECK(registry.validate_output(*registry.find(tool), json::parse(done.result_json)));
        return std::make_pair(proposed, done);
    };
    const json plane{{"sessionId", session}, {"objectId", cube},
                     {"plane", {{"point", {0, 0, 10}}, {"normal", {0, 0, 1}}}}};

    const auto [previewed, preview] = run("object_divide_preview", plane);
    CHECK_FALSE(previewed.requires_approval);
    const auto previewed_result = json::parse(preview.result_json);
    CHECK(previewed_result["pieces"].size() == 2);
    CHECK_FALSE(previewed_result["pieces"][0].contains("objectId"));
    CHECK(previewed_result["overhangAreaAfterMm2"] == 25);
    CHECK(h.workspace.snapshot().plates[0].objects.size() == 1);

    const auto [proposed, divided] = run("object_divide", plane);
    CHECK(proposed.title == "Cut " + h.workspace.snapshot().plates[0].objects[0].name +
                                " by the plane through (0, 0, 10) facing (0, 0, 1), keeping both pieces: 20 x 20 x 10 mm, 20 x 20 x 10 mm");
    const auto divided_result = json::parse(divided.result_json);
    REQUIRE(divided_result["pieces"].size() == 2);
    CHECK(divided_result["pieces"][1].contains("objectId"));
    CHECK(h.workspace.snapshot().plates[0].objects.size() == 2);

    const std::string upper = divided_result["pieces"][1]["objectId"];
    const auto [merge_card, merged] = run("object_merge", json{{"sessionId", session}, {"objectIds", {cube, upper}}});
    CHECK(merge_card.title.find("Merge ") == 0);
    CHECK(merge_card.title.find(" and ") != std::string::npos);
    const std::string assembly = json::parse(merged.result_json)["objectId"];
    CHECK(h.workspace.snapshot().plates[0].objects.size() == 1);
    CHECK(std::to_string(h.workspace.snapshot().plates[0].objects[0].id.value()) == assembly);

    const auto [repair_card, repaired] = run("object_repair", json{{"sessionId", session}, {"objectId", assembly}});
    CHECK(repair_card.title == "Repair the mesh of Assembly");
    const auto repaired_result = json::parse(repaired.result_json);
    CHECK(repaired_result["changed"] == true);
    CHECK(repaired_result["openEdges"] == json{{"before", 2}, {"after", 0}});
    CHECK(json::parse(run("object_repair", json{{"sessionId", session}, {"objectId", assembly}}).second.result_json)["changed"] == false);

    // A region bound to the divided object is reported by the preview.
    h.workspace.set_analysis_for_testing(h.workspace.snapshot().plates[0].objects[0].id, {});
    CHECK(run("region_annotate", json{{"sessionId", session},
                                      {"regions", {{{"objectId", assembly}, {"kind", "hidden"},
                                                    {"geometry", {{"type", "face"}, {"handle", "f1-0-0-0-0"}}}}}}}).second.state == ToolState::Succeeded);
    const json split{{"sessionId", session}, {"objectId", assembly}, {"shells", "objects"}};
    CHECK(json::parse(run("object_divide_preview", split).second.result_json)["regionsUnbound"] == json::array({"r1"}));
    h.workspace.unbind_region_for_testing("r1");
    CHECK(json::parse(run("object_divide", split).second.result_json)["regionsUnbound"] == json::array());

    for (const json& bad : {json{{"sessionId", session}, {"objectId", cube}},
                            json{{"sessionId", session}, {"objectId", cube}, {"shells", "objects"}, {"plane", {{"point", {0, 0, 0}}, {"normal", {0, 0, 1}}}}},
                            json{{"sessionId", session}, {"objectId", cube}, {"plane", {{"point", {0, 0}}, {"normal", {0, 0, 1}}}}},
                            json{{"sessionId", session}, {"objectId", cube}, {"plane", {{"point", {0, 0, 0}}, {"normal", {0, 0, 1}}, {"keep", "middle"}}}}})
        CHECK_FALSE(registry.validate_call(*registry.find("object_divide"), bad.dump()).valid());
    CHECK_FALSE(registry.validate_call(*registry.find("object_merge"), json{{"sessionId", session}, {"objectIds", {cube, cube}}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(*registry.find("object_merge"), json{{"sessionId", session}, {"objectIds", {cube}}}.dump()).valid());
}

TEST_CASE("the slice report checks supports, seams, the first layer and islands against the regions", "[tools][slicing][report][regions]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const auto plate = h.workspace.snapshot().plates.at(0).id;
    Workspace::RegionRecord hole;
    hole.id   = "r1";
    hole.kind = "precision_hole";
    store.set_regions({hole});

    Workspace::SliceReport report;
    report.supports    = Workspace::SliceSupports{true, 40, {{h.cube_id(), "Cube", "r1", "region r1 (precision hole)", 12.345, 6, {1, 2, 3}},
                                                             {std::nullopt, "Cube", "", "hole of 5 mm", 3, 2, {0, 0, 0}}}, false};
    report.seams       = Workspace::SliceSeams{120, {{"r2", "hidden", "Cube", 118}}};
    report.first_layer = Workspace::SliceFirstLayer{0.2, {{h.cube_id(), "Cube", 400, true}}};
    report.islands     = Workspace::SliceIslands{{{h.cube_id(), "Cube", 12.4, 30, false, {5, 5, 12.4}}}, false};
    h.workspace.set_slice_report_for_testing(plate, report);
    h.workspace.set_plate_sliced(plate, true);

    const auto read = [&h](json arguments) {
        const std::string action = h.coordinator.propose({"slice_report", arguments.dump()}, "m-1").action_id;
        h.pump_to_completion(action);
        return json::parse(h.coordinator.find(action)->result_json);
    };
    const auto checked = read(json{{"sections", {"supports", "seams", "firstLayer", "islands"}}});
    CHECK(registry.validate_output(*registry.find("slice_report"), checked));
    CHECK_FALSE(checked.contains("summary"));
    CHECK(checked["supports"]["contacts"][0] == json{{"objectId", std::to_string(h.cube_id().value())}, {"object", "Cube"},
                                                     {"regionId", "r1"}, {"target", "region r1 (precision hole)"},
                                                     {"areaMm2", 12.35}, {"layers", 6}, {"at", {1, 2, 3}}});
    CHECK(checked["supports"]["contacts"][1]["objectId"] == "");
    CHECK(checked["seams"]["regions"][0]["seams"] == 118);
    CHECK(checked["firstLayer"]["objects"][0]["brim"] == true);
    CHECK(checked["islands"]["items"][0]["supported"] == false);
    // The regions went to the workspace with the request; a summary-only
    // read asks for no checks.
    CHECK(h.workspace.last_slice_request.regions.size() == 1);
    CHECK(h.workspace.last_slice_request.supports);
    const auto plain = read(json::object());
    CHECK_FALSE(plain.contains("supports"));
    CHECK_FALSE(h.workspace.last_slice_request.supports);
    CHECK(h.workspace.last_slice_request.regions.empty());
    CHECK_FALSE(registry.validate_call(*registry.find("slice_report"), R"({"sections":["supports","supports"]})").valid());
}

TEST_CASE("a settings patch survives model edits after its preview, but not a settings edit", "[tools][settings]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const auto preview = h.workspace.snapshot();
    const json patch{{"changes", {{"wall_loops", 4}}}, {"expectedSessionId", std::to_string(preview.session.value())},
                     {"expectedRevision", preview.revision}};
    // A copy is added after the preview; the patch still proposes and applies.
    const ToolActivity copy = h.coordinator.propose(h.duplicate_cube_request(), "m-1");
    REQUIRE(h.coordinator.approve(copy.action_id));
    h.pump_to_completion(copy.action_id);
    REQUIRE(h.workspace.snapshot().revision > preview.revision);
    const ToolActivity pending = h.coordinator.propose({"settings_apply_patch", patch.dump()}, "m-2");
    REQUIRE(pending.state == ToolState::Pending);
    // Another model edit while the card is up leaves it pending.
    const ToolActivity another = h.coordinator.propose(h.duplicate_cube_request(), "m-3");
    REQUIRE(h.coordinator.approve(another.action_id));
    h.pump_to_completion(another.action_id);
    CHECK(h.coordinator.find(pending.action_id)->state == ToolState::Pending);
    REQUIRE(h.coordinator.approve(pending.action_id));
    h.pump_to_completion(pending.action_id);
    CHECK(h.coordinator.find(pending.action_id)->state == ToolState::Succeeded);

    // A settings edit after the preview is stale, pending or not.
    const auto second = h.workspace.snapshot();
    const json again{{"changes", {{"wall_loops", 5}}}, {"expectedSessionId", std::to_string(second.session.value())},
                     {"expectedRevision", second.revision}};
    const ToolActivity waiting = h.coordinator.propose({"settings_apply_patch", again.dump()}, "m-4");
    REQUIRE(waiting.state == ToolState::Pending);
    h.workspace.set_setting_for_testing("brim_width", "7");
    CHECK(h.coordinator.find(waiting.action_id)->error->code == "stale_revision");
    CHECK(h.coordinator.propose({"settings_apply_patch", again.dump()}, "m-5").error->code == "stale_workspace");

    // A refused call says what is wrong with it.
    const auto refused = registry.validate_call(*registry.find("settings_preview_patch"), json{{"changes", {{"wall_loops", 3}}}, {"intent", "x"}}.dump());
    REQUIRE_FALSE(refused.valid());
    CHECK(refused.error->message.find("Not a parameter of this tool: intent.") != std::string::npos);
    CHECK(registry.validate_call(*registry.find("settings_preview_patch"), "{}").error->message.find("Missing: changes.") != std::string::npos);
    const auto opening = registry.validate_call(*registry.find("project_open"), R"({"path":"C:/a.stl","new":true,"oversized":"shrink"})");
    CHECK(opening.error->message.find(R"(oversized must be one of ["keep","scaleToFit"].)") != std::string::npos);
    CHECK(opening.error->message.find("Give exactly one of path and new.") != std::string::npos);
}

TEST_CASE("a slice started with wait returns when the run ends, without holding up other calls", "[tools][slicing]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const auto plate = h.workspace.snapshot().plates.at(0).id;
    const ToolActivity started = h.coordinator.propose({"slice_start", json{{"plateId", std::to_string(plate.value())}, {"wait", true}}.dump()}, "m-1");
    for (int i = 0; i < 20; ++i)
        h.coordinator.pump();
    CHECK(h.coordinator.find(started.action_id)->state == ToolState::Running);
    CHECK(h.workspace.slice_starts == 1);

    // A read proposed meanwhile still runs.
    const ToolActivity read = h.coordinator.propose({"workspace_inspect", "{}"}, "m-2");
    h.pump_to_completion(read.action_id);
    CHECK(h.coordinator.find(read.action_id)->state == ToolState::Succeeded);
    CHECK(h.coordinator.find(started.action_id)->state == ToolState::Running);

    h.workspace.finish_slice_for_testing(true);
    h.pump_to_completion(started.action_id);
    const ToolActivity done = *h.coordinator.find(started.action_id);
    REQUIRE(done.state == ToolState::Succeeded);
    const auto result = json::parse(done.result_json);
    CHECK(registry.validate_output(*registry.find("slice_start"), result));
    CHECK(result["finished"] == true);
    CHECK(result["slicing"]["running"] == false);
    CHECK_FALSE(registry.validate_call(*registry.find("slice_start"), R"({"wait":"yes"})").valid());
}

TEST_CASE("the print intent's limits are read from its words", "[tools][intent][report]")
{
    CHECK(intent_seconds("under five hours") == 5 * 3600.0);
    CHECK(intent_seconds("2 h 30 min") == 2 * 3600.0 + 1800);
    CHECK(intent_seconds("an hour and a half") == 5400.0);
    CHECK(intent_seconds("90 minutes") == 5400.0);
    CHECK_FALSE(intent_seconds("decorative"));
    CHECK_FALSE(intent_seconds("0.4 mm nozzle"));
    CHECK(intent_grams("less than 50 g") == 50.0);
    CHECK(intent_grams("0.2 kg at most") == 200.0);
    CHECK(intent_money("at most $2.50") == 2.5);
    CHECK(intent_money("under 3 euros") == 3.0);
    CHECK(intent_money("\xE2\x82\xAC" "4") == 4.0);
    CHECK(intent_is_floor("at least 2 hours of cooling"));
}

TEST_CASE("the slice report measures the slice against the print intent", "[tools][slicing][report][intent]")
{
    Harness h;
    FakeProductState store;
    h.coordinator.set_product_state(&store);
    const auto& registry = ToolRegistry::instance();
    const auto plate = h.workspace.snapshot().plates.at(0).id;
    store.set_print_intent({{"how long it may take", "under one hour", "", Provenance::UserConfirmed},
                            {"filament", "less than 50 g", "", Provenance::UserConfirmed},
                            {"what it is for", "decorative", "", Provenance::UserConfirmed},
                            {"deadline", "", "When do you need it?", Provenance::AgentInferred}});
    Workspace::SliceReport report;
    report.print_time_seconds = 4500;
    report.total_grams        = 23.5;
    h.workspace.set_slice_report_for_testing(plate, report);
    h.workspace.set_plate_sliced(plate, true);
    const std::string action = h.coordinator.propose({"slice_report", json{{"sections", {"summary", "intent"}}}.dump()}, "m-1").action_id;
    h.pump_to_completion(action);
    const auto result = json::parse(h.coordinator.find(action)->result_json);
    CHECK(registry.validate_output(*registry.find("slice_report"), result));
    REQUIRE(result["intent"]["checks"].size() == 2);
    const auto& time = result["intent"]["checks"][0]["kind"] == "time" ? result["intent"]["checks"][0] : result["intent"]["checks"][1];
    CHECK(time == json{{"field", "how long it may take"}, {"value", "under one hour"}, {"kind", "time"},
                       {"limit", 3600}, {"actual", 4500}, {"within", false}});
    CHECK(result["intent"]["unchecked"] == json::array({"what it is for"}));
}

TEST_CASE("pictures, attachments, slice detail, cancels and exports", "[tools][outputs]")
{
    Harness h;
    const auto& registry = ToolRegistry::instance();
    const auto  snapshot = h.workspace.snapshot();
    const std::string session = std::to_string(snapshot.session.value());
    const auto plate = snapshot.plates.at(0).id;
    auto run = [&h, &registry](const std::string& tool, const json& arguments) {
        const ToolActivity proposed = h.coordinator.propose({tool, arguments.dump()}, "m-1");
        if (proposed.state == ToolState::Pending)
            REQUIRE(h.coordinator.approve(proposed.action_id));
        h.pump_to_completion(proposed.action_id);
        const ToolActivity done = *h.coordinator.find(proposed.action_id);
        if (done.state == ToolState::Succeeded)
            CHECK(registry.validate_output(*registry.find(tool), json::parse(done.result_json)));
        return std::make_pair(proposed, done);
    };

    // A picture comes back beside its result.
    const auto [render_card, rendered] = run("view_render", json{{"view", "front"}, {"widthPx", 640}, {"heightPx", 480}});
    CHECK_FALSE(render_card.requires_approval);
    REQUIRE(rendered.image);
    CHECK(rendered.image->mime_type == "image/png");
    CHECK(rendered.image->base64 == "iVBORw0KGgo=");
    CHECK(json::parse(rendered.result_json)["view"] == "front");
    CHECK_FALSE(registry.validate_call(*registry.find("view_render"), R"({"view":"back"})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("view_render"), R"({"widthPx":4000})").valid());

    // Attachments: a picture as an image, text as text, a missing one refused.
    h.workspace.m_attachment_contents["Model Pictures/cover.png"] = {"Model Pictures/cover.png", "Model Pictures", "image/png", 9, "image", "cover", 2, 1, false};
    h.workspace.m_attachment_contents["Others/notes.txt"] = {"Others/notes.txt", "Others", "text/plain", 5, "text", "hello", 0, 0, false};
    const auto picture = run("project_attachment_read", json{{"id", "Model Pictures/cover.png"}}).second;
    REQUIRE(picture.image);
    CHECK(picture.image->base64 == "Y292ZXI=");
    CHECK(json::parse(picture.result_json)["widthPx"] == 2);
    const auto notes = run("project_attachment_read", json{{"id", "Others/notes.txt"}}).second;
    CHECK_FALSE(notes.image);
    CHECK(json::parse(notes.result_json)["text"] == "hello");
    CHECK(run("project_attachment_read", json{{"id", "../secret.txt"}}).second.error->code == "invalid_argument");

    // Slice detail says when there is no slice, then pages the layers.
    CHECK(json::parse(run("slice_inspect", json{{"view", "layers"}}).second.result_json)["valid"] == false);
    h.workspace.set_plate_sliced(plate, true);
    const auto layers = json::parse(run("slice_inspect", json{{"view", "layers"}, {"count", 2}}).second.result_json);
    CHECK(layers["layerCount"] == 3);
    CHECK(layers["layers"].size() == 2);
    CHECK(layers["next"] == 2);
    CHECK(layers["layers"][1]["speedMmS"] == json{{"before", 20}, {"after", 60}});
    const auto gcode = json::parse(run("slice_inspect", json{{"view", "gcode"}}).second.result_json);
    CHECK(gcode["gcode"] == "G28\nG1 X10\nM104 S0\n");
    CHECK_FALSE(gcode.contains("next"));

    // A cancel needs no card and stops only what the tools started.
    const auto started = run("slice_start", json::object()).second;
    const std::string slice_handle = json::parse(started.result_json)["handle"];
    const auto [cancel_card, cancelled] = run("activity_cancel", json{{"handle", slice_handle}});
    CHECK_FALSE(cancel_card.requires_approval);
    CHECK(json::parse(cancelled.result_json)["kind"] == "slice");
    CHECK(json::parse(cancelled.result_json)["cancelled"] == true);
    CHECK_FALSE(h.workspace.snapshot().slicing.running);
    CHECK(json::parse(run("activity_cancel", json{{"handle", slice_handle}}).second.result_json)["cancelled"] == false);
    // A slice the person starts afterwards is not stopped by the old handle.
    REQUIRE(h.workspace.start_slice(std::nullopt, false).succeeded());
    CHECK(json::parse(run("activity_cancel", json{{"handle", slice_handle}}).second.result_json)["cancelled"] == false);
    CHECK(h.workspace.snapshot().slicing.running);
    h.workspace.finish_slice_for_testing(false);
    const ToolActivity waiting = h.coordinator.propose(h.duplicate_cube_request(), "m-2");
    const auto dropped = json::parse(run("activity_cancel", json{{"handle", waiting.action_id}}).second.result_json);
    CHECK(dropped["kind"] == "proposal");
    CHECK(h.coordinator.find(waiting.action_id)->state == ToolState::Cancelled);
    CHECK(run("activity_cancel", json{{"handle", "nothing"}}).second.error->code == "invalid_argument");

    // An export writes only after approval, and never over a file unasked.
    const auto folder = std::filesystem::temp_directory_path() / "jusprin-export-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::string target = (folder / "plate.gcode").u8string();
    h.workspace.set_plate_sliced(plate, true);
    std::ofstream(std::filesystem::u8path(target)) << "old";
    const json exporting{{"sessionId", session}, {"kind", "gcode"}, {"path", target}};
    CHECK(h.coordinator.propose({"export_file", exporting.dump()}, "m-3").error->code == "invalid_argument");
    json replacing = exporting;
    replacing["overwrite"] = true;
    const ToolActivity card = h.coordinator.propose({"export_file", replacing.dump()}, "m-4");
    REQUIRE(card.state == ToolState::Pending);
    CHECK(card.title == "Export the G-code to " + target + ", replacing it");
    REQUIRE(h.coordinator.reject(card.action_id));
    const auto contents = [&target] {
        std::ifstream file(std::filesystem::u8path(target));
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    };
    CHECK(contents() == "old");
    CHECK(h.workspace.exports == 0);
    h.workspace.m_details.license = "CC BY-NC-SA 4.0";
    const ToolActivity licensed = h.coordinator.propose({"export_file", replacing.dump()}, "m-5");
    CHECK(licensed.title.find("the project's license is CC BY-NC-SA 4.0") != std::string::npos);
    REQUIRE(h.coordinator.approve(licensed.action_id));
    h.pump_to_completion(licensed.action_id);
    const auto exported = json::parse(h.coordinator.find(licensed.action_id)->result_json);
    CHECK(registry.validate_output(*registry.find("export_file"), exported));
    CHECK(exported["licenseRestricted"] == true);
    CHECK(exported["files"] == json::array({target}));
    CHECK(contents() == "gcode");
    CHECK(exported["sliceWarnings"] == json::array());

    // The card and the result name what the slice itself says is wrong.
    Workspace::SliceReport warned;
    warned.valid    = true;
    warned.findings = {{"", "It seems object cube-a has floating cantilever.", false, "cube-a"},
                       {"10018003", "Traditional timelapse may mark the surface", false, "", "timelapse"}};
    warned.conflict = "Conflicts of G-code paths at Z = 4.20mm";
    h.workspace.set_slice_report_for_testing(plate, warned);
    const ToolActivity warning_card = h.coordinator.propose({"export_file", replacing.dump()}, "m-6");
    CHECK(warning_card.title.find("the slice has 2 warnings, first: It seems object cube-a has floating cantilever.") != std::string::npos);
    REQUIRE(h.coordinator.approve(warning_card.action_id));
    h.pump_to_completion(warning_card.action_id);
    const auto warned_result = json::parse(h.coordinator.find(warning_card.action_id)->result_json);
    CHECK(registry.validate_output(*registry.find("export_file"), warned_result));
    CHECK(warned_result["sliceWarnings"] == json::array({"It seems object cube-a has floating cantilever.", "Conflicts of G-code paths at Z = 4.20mm"}));
    CHECK(registry.find("export_file")->action_class == ActionClass::Destructive);
    CHECK_FALSE(registry.validate_call(*registry.find("export_file"), json{{"sessionId", session}, {"kind", "project_3mf"}, {"path", target}, {"plateId", "1"}}.dump()).valid());
    CHECK_FALSE(registry.validate_call(*registry.find("export_file"), json{{"sessionId", session}, {"kind", "gcode"}, {"path", target}, {"objectIds", {"1"}}}.dump()).valid());
    std::filesystem::remove_all(folder);
}

TEST_CASE("calls sharing a plan id wait for one decision and run in order", "[tools][plan]")
{
    Harness h;
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    const auto copies = [&](int quantity, const char* plan) {
        json arguments{{"sessionId", session}, {"objects", json::array({json{{"objectId", cube}, {"quantity", quantity}}})}};
        if (plan != nullptr) arguments["planId"] = plan;
        return ToolRequest{"plate_layout", arguments.dump()};
    };
    const ToolActivity first  = h.coordinator.propose(copies(2, "copies"), "m-1");
    const ToolActivity second = h.coordinator.propose(copies(3, "copies"), "m-2");
    REQUIRE(first.state == ToolState::Pending);
    REQUIRE(second.state == ToolState::Pending);
    CHECK(first.plan_id == "copies");
    CHECK(json::parse(first.arguments_json)["planId"] == "copies");

    SECTION("one approval runs every member in order")
    {
        REQUIRE(h.coordinator.approve(first.action_id));
        CHECK(h.coordinator.find(second.action_id)->state == ToolState::Running);
        h.pump_to_completion(second.action_id);
        CHECK(h.coordinator.find(first.action_id)->state == ToolState::Succeeded);
        CHECK(h.coordinator.find(second.action_id)->state == ToolState::Succeeded);
        CHECK(h.object_count() == 3);
        std::vector<std::string> finished;
        for (const ToolActivity& event : h.events)
            if (event.state == ToolState::Succeeded) finished.push_back(event.action_id);
        CHECK(finished == std::vector<std::string>{first.action_id, second.action_id});

        const ToolActivity inspect = h.coordinator.propose({"workspace_inspect", R"({"sections":["activities"]})"}, "m-3");
        h.pump_to_completion(inspect.action_id);
        const auto activities = json::parse(h.coordinator.find(inspect.action_id)->result_json)["activities"];
        REQUIRE(activities.size() == 2);
        CHECK(activities[1] == json{{"actionId", second.action_id}, {"tool", "plate_layout"}, {"title", second.title},
                                    {"state", "succeeded"}, {"planId", "copies"}});
        CHECK(ToolRegistry::instance().validate_output(*ToolRegistry::instance().find("workspace_inspect"),
                                                       json::parse(h.coordinator.find(inspect.action_id)->result_json)));
    }
    SECTION("one rejection rejects every member")
    {
        REQUIRE(h.coordinator.reject(second.action_id));
        CHECK(h.coordinator.find(first.action_id)->state == ToolState::Rejected);
        CHECK(h.object_count() == 1);
    }
    SECTION("an outside change stops the plan at its next member")
    {
        const ToolActivity alone = h.coordinator.propose(h.duplicate_cube_request(), "m-4");
        REQUIRE(h.coordinator.approve(first.action_id));
        REQUIRE(h.workspace.rename_object(h.cube_id(), "Edited after approval").succeeded());
        h.pump_to_completion(second.action_id);
        CHECK(h.coordinator.find(first.action_id)->error->code == "stale_revision");
        CHECK(h.coordinator.find(second.action_id)->error->code == "plan_step_failed");
        CHECK(h.object_count() == 1);
        // A call outside the plan was not approved with it.
        CHECK(h.coordinator.find(alone.action_id)->state == ToolState::Failed);
    }
    SECTION("a decided or broken plan takes no new members")
    {
        REQUIRE(h.coordinator.reject(first.action_id));
        const ToolActivity late = h.coordinator.propose(copies(4, "copies"), "m-6");
        CHECK(late.state == ToolState::Failed);
        CHECK(late.error->code == "plan_closed");
        CHECK(h.coordinator.propose(copies(4, "fresh"), "m-7").state == ToolState::Pending);
    }
    SECTION("a member proposed alone is not part of the plan")
    {
        const ToolActivity alone = h.coordinator.propose(copies(4, nullptr), "m-5");
        REQUIRE(h.coordinator.approve(first.action_id));
        CHECK(h.coordinator.find(alone.action_id)->state == ToolState::Pending);
    }
}

TEST_CASE("a plan belongs to the client that proposed it", "[tools][plan]")
{
    Harness h;
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string cube    = std::to_string(h.cube_id().value());
    const auto copies = [&](int quantity) {
        return ToolRequest{"plate_layout", json{{"sessionId", session}, {"planId", "plan-1"},
                                                {"objects", json::array({json{{"objectId", cube}, {"quantity", quantity}}})}}.dump()};
    };
    // The same id from the in-app agent and from an MCP client: the external
    // card lists only the MCP member, so approving it must run only that one.
    const ToolActivity in_app     = h.coordinator.propose(copies(2), "m-1", {}, ToolSource::Agent, "chat-1");
    const ToolActivity other_chat = h.coordinator.propose(copies(5), "m-2", {}, ToolSource::Agent, "chat-2");
    const ToolActivity external   = h.coordinator.propose(copies(3), "mcp-1", {}, ToolSource::Mcp);
    REQUIRE(in_app.state == ToolState::Pending);
    REQUIRE(other_chat.state == ToolState::Pending);
    REQUIRE(external.state == ToolState::Pending);
    CHECK(in_app.plan_scope == "chat-1");
    CHECK(external.plan_scope.empty());
    REQUIRE(h.coordinator.approve(external.action_id));
    CHECK(h.coordinator.find(external.action_id)->state == ToolState::Running);
    CHECK(h.coordinator.find(in_app.action_id)->state == ToolState::Pending);
    CHECK(h.coordinator.find(other_chat.action_id)->state == ToolState::Pending);
    // Another chat's decision is its own too.
    REQUIRE(h.coordinator.reject(other_chat.action_id));
    CHECK(h.coordinator.find(in_app.action_id)->state == ToolState::Pending);
    // The first chat's plan is still undecided, and still takes members.
    const ToolActivity joined = h.coordinator.propose(copies(4), "m-3", {}, ToolSource::Agent, "chat-1");
    CHECK(joined.state == ToolState::Pending);
    REQUIRE(h.coordinator.reject(in_app.action_id));
    CHECK(h.coordinator.find(joined.action_id)->state == ToolState::Rejected);
    h.pump_to_completion(external.action_id);
    CHECK(h.coordinator.find(external.action_id)->state == ToolState::Succeeded);
    CHECK(h.object_count() == 3);
}

TEST_CASE("a slice ending leaves waiting cards and approved plans alone", "[tools][slicing][plan]")
{
    Harness h;
    const std::string session = std::to_string(h.workspace.snapshot().session.value());
    const std::string plate   = std::to_string(h.workspace.snapshot().plates.at(0).id.value());

    SECTION("a card waiting for the person survives the agent's own slice")
    {
        const ToolActivity waiting = h.coordinator.propose(h.duplicate_cube_request(), "m-1");
        const ToolActivity started = h.coordinator.propose({"slice_start", json{{"plateId", plate}}.dump()}, "m-2");
        h.pump_to_completion(started.action_id);
        REQUIRE(h.coordinator.find(started.action_id)->state == ToolState::Succeeded);
        h.workspace.finish_slice_for_testing(true);
        CHECK(h.coordinator.find(waiting.action_id)->state == ToolState::Pending);
    }
    SECTION("a plan goes on after its own slice")
    {
        const std::string cube = std::to_string(h.cube_id().value());
        const ToolActivity slice = h.coordinator.propose(
            {"slice_start", json{{"plateId", plate}, {"wait", true}, {"planId", "sliced"}}.dump()}, "m-1");
        const ToolActivity after = h.coordinator.propose(
            {"plate_layout", json{{"sessionId", session}, {"planId", "sliced"},
                                  {"objects", json::array({json{{"objectId", cube}, {"quantity", 2}}})}}.dump()}, "m-2");
        REQUIRE(h.coordinator.approve(slice.action_id));
        for (int i = 0; i < 20; ++i)
            h.coordinator.pump();
        REQUIRE(h.coordinator.find(slice.action_id)->state == ToolState::Running);
        h.workspace.finish_slice_for_testing(true);
        h.pump_to_completion(after.action_id);
        CHECK(h.coordinator.find(slice.action_id)->state == ToolState::Succeeded);
        CHECK(h.coordinator.find(after.action_id)->state == ToolState::Succeeded);
        CHECK(h.object_count() == 2);
    }
    SECTION("an export card waiting on a slice fails when that slice goes")
    {
        const auto id = h.workspace.snapshot().plates.at(0).id;
        h.workspace.set_plate_sliced(id, true);
        const std::string target = (std::filesystem::temp_directory_path() / "jusprin-slice-change.gcode").u8string();
        std::filesystem::remove(std::filesystem::u8path(target));
        const ToolActivity gcode = h.coordinator.propose(
            {"export_file", json{{"sessionId", session}, {"kind", "gcode"}, {"path", target}}.dump()}, "m-1");
        const ToolActivity stl = h.coordinator.propose(
            {"export_file", json{{"sessionId", session}, {"kind", "stl"},
                                 {"path", (std::filesystem::temp_directory_path() / "jusprin-slice-change.stl").u8string()}}.dump()}, "m-2");
        REQUIRE(gcode.state == ToolState::Pending);
        REQUIRE(stl.state == ToolState::Pending);
        h.workspace.set_plate_sliced(id, false);
        CHECK(h.coordinator.find(gcode.action_id)->error->code == "stale_revision");
        CHECK(h.coordinator.find(stl.action_id)->state == ToolState::Pending);
    }
    SECTION("a slice is still no reason to keep a stale card")
    {
        const ToolActivity waiting = h.coordinator.propose(h.duplicate_cube_request(), "m-1");
        REQUIRE(h.workspace.rename_object(h.cube_id(), "Edited").succeeded());
        CHECK(h.coordinator.find(waiting.action_id)->error->code == "stale_revision");
    }
}

TEST_CASE("a plan id is checked before any tool sees it", "[tools][plan][registry]")
{
    const auto& registry = ToolRegistry::instance();
    for (const ToolDefinition& definition : registry.definitions())
        CHECK(definition.input_schema["properties"].contains("planId") ==
              (definition.action_class != ActionClass::ReadOnly && definition.name != "plan_set"));
    const auto refused = registry.validate_call(*registry.find("workspace_inspect"), R"({"planId":"p"})");
    REQUIRE_FALSE(refused.valid());
    CHECK(refused.error->message == "workspace_inspect does not join a plan; only changes to the project take a planId.");
    CHECK_FALSE(registry.validate_call(*registry.find("plan_set"), R"({"headline":"h","planId":"p"})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("plate_layout"), R"({"planId":""})").valid());
    CHECK_FALSE(registry.validate_call(*registry.find("plate_layout"), R"({"planId":7})").valid());
}
