// Contract tests for the project state document (schema round-trip,
// unknown-field preservation, migration, corruption) and for
// ProjectPersistence against the fake workspace (project-boundary adoption,
// revision capture, recovery merge, revert atomicity). GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace Slic3r::GUI::JusPrin;
using namespace Slic3r::GUI::JusPrin::Agent;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr const char* kT = "2026-08-30T00:00:00Z";

ConversationMessage user_message(const std::string& id, const std::string& text, const std::string& client_id)
{
    ConversationMessage message;
    message.id                = id;
    message.role              = MessageRole::User;
    message.state             = MessageState::Complete;
    message.text              = text;
    message.client_message_id = client_id;
    return message;
}

std::string read_text(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_text(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

Workspace::WorkspaceSnapshot small_snapshot()
{
    Workspace::WorkspaceSnapshot snapshot;
    snapshot.setup.project_name = "Persistence Fixture";
    Workspace::WorkspacePlate plate;
    plate.id     = Workspace::PlateId(Workspace::ProjectSessionId(1), 11);
    plate.name   = "Plate 1";
    plate.active = true;
    Workspace::WorkspaceObject cube;
    cube.id   = Workspace::ObjectId(Workspace::ProjectSessionId(1), 21);
    cube.name = "cube-a";
    cube.instances.push_back({});
    plate.objects         = {cube};
    snapshot.plates       = {plate};
    snapshot.active_plate = plate.id;
    return snapshot;
}

ProjectPersistence::Config config_with_recovery(const std::string& recovery_root)
{
    ProjectPersistence::Config config;
    config.recovery_root = recovery_root;
    config.clock         = []() { return std::string(kT); };
    config.uuid          = []() {
        static int next = 0;
        return "u" + std::to_string(++next);
    };
    return config;
}

std::string unique_temp_dir(const char* label)
{
    static int next = 0;
    const fs::path dir = fs::temp_directory_path() / (std::string("jusprin-state-tests-") + label + "-" + std::to_string(++next));
    fs::create_directories(dir);
    return dir.string();
}

} // namespace

TEST_CASE("the document round-trips its semantic state", "[project-state][schema]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    const std::string conversation = document.active_conversation_id();
    document.append_message(conversation, user_message(document.allocate_message_id(), "hello", "c-1"), kT);
    const std::string second = document.create_conversation("Second", kT);
    document.append_message(second, user_message(document.allocate_message_id(), "hi again", "c-2"), kT);

    ToolActivity activity;
    activity.action_id      = document.allocate_action_id();
    activity.correlation_id = "m-1";
    activity.server         = "jusprin-native";
    activity.tool           = "duplicate_object";
    activity.state          = ToolState::Succeeded;
    document.upsert_activity(activity, kT);

    document.add_revision("contents", "revisions/r-1.snapshot", conversation, kT);

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);

    CHECK(reloaded.project_id() == "p-1");
    CHECK(reloaded.lineage_id() == "l-1");
    CHECK(reloaded.conversations().size() == 2);
    CHECK(reloaded.active_conversation_id() == second);
    REQUIRE(reloaded.messages(conversation).size() == 1);
    CHECK(reloaded.messages(conversation)[0].text == "hello");
    CHECK(reloaded.client_message_lookup("c-2").has_value());
    REQUIRE(reloaded.activities().size() == 1);
    CHECK(reloaded.activities()[0].state == ToolState::Succeeded);
    REQUIRE(reloaded.revisions().size() == 1);
    CHECK(reloaded.current_revision_id() == reloaded.revisions()[0].id);

    SECTION("counters continue after a reload so IDs stay unique") {
        const std::string next_id = reloaded.allocate_message_id();
        CHECK(next_id == "m-3");
    }
}

TEST_CASE("unknown fields survive a load-edit-save cycle", "[project-state][schema]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    document.append_message(document.active_conversation_id(),
                            user_message(document.allocate_message_id(), "hello", "c-1"), kT);

    // A newer build stored fields this build does not know.
    json raw = json::parse(document.dump());
    raw["futureTopLevel"]                            = json{{"nested", true}};
    raw["conversations"][0]["futureConversationFlag"] = 42;
    raw["conversations"][0]["messages"][0]["futureAnnotation"] = "keep me";

    ProjectStateDocument edited;
    REQUIRE(edited.load(raw.dump()) == ProjectStateDocument::LoadResult::Loaded);
    // Edit the known parts...
    ConversationMessage message = edited.messages(edited.active_conversation_id())[0];
    message.text                = "hello, edited";
    edited.update_message(edited.active_conversation_id(), message);
    edited.append_message(edited.active_conversation_id(),
                          user_message(edited.allocate_message_id(), "another", "c-2"), kT);

    const json round = json::parse(edited.dump());
    CHECK(round["futureTopLevel"]["nested"] == true);
    CHECK(round["conversations"][0]["futureConversationFlag"] == 42);
    CHECK(round["conversations"][0]["messages"][0]["futureAnnotation"] == "keep me");
    CHECK(round["conversations"][0]["messages"][0]["text"] == "hello, edited");
}

TEST_CASE("corrupt, foreign, and future documents are refused safely", "[project-state][schema]")
{
    ProjectStateDocument document;
    CHECK(document.load("this is not json") == ProjectStateDocument::LoadResult::Corrupt);
    CHECK(document.load("[1,2,3]") == ProjectStateDocument::LoadResult::Corrupt);
    CHECK(document.load(json{{"noSchema", true}}.dump()) == ProjectStateDocument::LoadResult::Corrupt);
    CHECK(document.load(json{{"schemaVersion", ProjectStateDocument::kSchemaVersion + 1}}.dump()) ==
          ProjectStateDocument::LoadResult::Corrupt);
    // After every refusal the document is a usable fresh one.
    CHECK_FALSE(document.has_identity());
    document.initialize_identity("p-1", "l-1", kT);
    CHECK(document.has_identity());
}

TEST_CASE("an older schema is migrated and keeps its content", "[project-state][schema]")
{
    json old_doc = json{{"schemaVersion", 0},
                        {"project", json{{"projectId", "p-old"}, {"lineageId", "l-old"}, {"createdAt", kT}}},
                        {"conversations", json::array()}};
    ProjectStateDocument document;
    REQUIRE(document.load(old_doc.dump()) == ProjectStateDocument::LoadResult::Migrated);
    CHECK(document.project_id() == "p-old");
    CHECK(json::parse(document.dump())["schemaVersion"] == ProjectStateDocument::kSchemaVersion);
    // Structure the old version predates exists and works.
    document.create_conversation("New", kT);
    CHECK(document.conversations().size() == 1);
}

TEST_CASE("revert truncates later entries across conversations", "[project-state][revert]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    const std::string first = document.active_conversation_id();
    document.append_message(first, user_message(document.allocate_message_id(), "before", "c-1"), kT);
    document.add_revision("initial", "revisions/r-1.snapshot", first, kT);
    const std::string target = document.add_revision("contents", "revisions/r-2.snapshot", first, kT);

    document.append_message(first, user_message(document.allocate_message_id(), "after", "c-2"), kT);
    const std::string later = document.create_conversation("Later", kT);
    document.append_message(later, user_message(document.allocate_message_id(), "in later", "c-3"), kT);
    document.add_revision("transform", "revisions/r-3.snapshot", later, kT);

    const auto result = document.revert_to_revision(target);
    REQUIRE(result.has_value());
    CHECK(result->removed_snapshot_files == std::vector<std::string>{"revisions/r-3.snapshot"});
    CHECK(result->kept_snapshot_files ==
          std::vector<std::string>{"revisions/r-1.snapshot", "revisions/r-2.snapshot"});

    CHECK(document.conversations().size() == 1);
    REQUIRE(document.messages(first).size() == 1);
    CHECK(document.messages(first)[0].text == "before");
    CHECK(document.revisions().size() == 2);
    CHECK(document.current_revision_id() == target);
    CHECK(document.active_conversation_id() == first);

    SECTION("reverting to an unknown revision does nothing") {
        CHECK_FALSE(document.revert_to_revision("r-999").has_value());
    }
}

TEST_CASE("interrupted streams and runs are normalized on recovery", "[project-state][recovery]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    ConversationMessage streaming = user_message(document.allocate_message_id(), "partial reply", "");
    streaming.role                = MessageRole::Assistant;
    streaming.state               = MessageState::Streaming;
    document.append_message(document.active_conversation_id(), streaming, kT);

    ToolActivity running;
    running.action_id = document.allocate_action_id();
    running.state     = ToolState::Running;
    document.upsert_activity(running, kT);

    CHECK(document.normalize_interrupted_state());
    CHECK(document.messages(document.active_conversation_id())[0].state == MessageState::Stopped);
    CHECK(document.activities()[0].state == ToolState::Cancelled);
    CHECK_FALSE(document.normalize_interrupted_state());
}

TEST_CASE("adoption creates identity, initial revision, and both stores", "[persistence][adoption]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    const std::string recovery_root = unique_temp_dir("recovery");
    ProjectPersistence persistence(workspace, config_with_recovery(recovery_root));

    int replaced = 0;
    persistence.set_document_replaced_listener([&replaced]() { ++replaced; });
    persistence.attach();

    CHECK(replaced == 1);
    CHECK(persistence.document().has_identity());
    CHECK(persistence.document().conversations().size() == 1);
    REQUIRE(persistence.document().revisions().size() == 1);
    CHECK(persistence.document().revisions()[0].cause == "initial");
    CHECK(fs::is_regular_file(persistence.state_file_path()));
    CHECK(fs::is_regular_file(fs::path(persistence.jusprin_data_dir()) / persistence.document().revisions()[0].snapshot_file));
    CHECK(fs::is_regular_file(fs::path(persistence.recovery_dir()) / "state.json"));
    CHECK(persistence.stats().captures == 1);
}

TEST_CASE("manufacturing changes capture revisions; selection does not", "[persistence][revisions]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    const std::size_t revisions_before = persistence.document().revisions().size();

    REQUIRE(workspace.select_object(workspace.snapshot().plates[0].objects[0].id).succeeded());
    CHECK(persistence.document().revisions().size() == revisions_before);

    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "renamed").succeeded());
    REQUIRE(persistence.document().revisions().size() == revisions_before + 1);
    const RevisionInfo captured = persistence.document().revisions().back();
    CHECK(captured.cause.find("contents") != std::string::npos);
    CHECK(fs::is_regular_file(fs::path(persistence.jusprin_data_dir()) / captured.snapshot_file));
}

TEST_CASE("saved state is adopted on reopen and merged with newer recovery", "[persistence][recovery]")
{
    const std::string recovery_root = unique_temp_dir("recovery");

    // First session: chat state exists; a "save" snapshots state.json as of
    // that moment; then more work arrives (only the recovery mirror has it).
    Workspace::FakeWorkspace first_workspace(small_snapshot());
    ProjectPersistence first(first_workspace, config_with_recovery(recovery_root));
    first.attach();
    const std::string project_id   = first.document().project_id();
    const std::string conversation = first.document().active_conversation_id();
    first.document().append_message(conversation, user_message(first.document().allocate_message_id(), "saved message", "c-1"),
                                    kT);
    first.flush();
    const std::string saved_state = read_text(first.state_file_path()); // the explicit save

    ConversationMessage partial = user_message(first.document().allocate_message_id(), "reply after save", "");
    partial.role                = MessageRole::Assistant;
    partial.state               = MessageState::Streaming;
    first.document().append_message(conversation, partial, kT);
    first.set_draft("half-typed");
    first.flush();

    // Second session: a reopened project carries only the saved state.json in
    // its extracted auxiliary dir; the recovery store is local and newer.
    Workspace::FakeWorkspace second_workspace(small_snapshot());
    write_text(fs::path(second_workspace.auxiliary_data_dir()) / "JusPrin" / "state.json", saved_state);
    ProjectPersistence second(second_workspace, config_with_recovery(recovery_root));
    second.attach();

    CHECK(second.document().project_id() == project_id);
    const std::vector<ConversationMessage> messages = second.document().messages(conversation);
    REQUIRE(messages.size() == 2); // the recovery mirror won
    CHECK(messages[0].text == "saved message");
    // The interrupted reply is visible and honestly stopped.
    CHECK(messages[1].state == MessageState::Stopped);
    CHECK(second.draft() == "half-typed");

    SECTION("without a recovery mirror the saved state stands alone") {
        Workspace::FakeWorkspace third_workspace(small_snapshot());
        write_text(fs::path(third_workspace.auxiliary_data_dir()) / "JusPrin" / "state.json", saved_state);
        ProjectPersistence third(third_workspace, config_with_recovery(unique_temp_dir("other-recovery")));
        third.attach();
        CHECK(third.document().project_id() == project_id);
        CHECK(third.document().messages(conversation).size() == 1);
    }
}

TEST_CASE("a corrupt state file is preserved for inspection and replaced", "[persistence][recovery]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    write_text(fs::path(workspace.auxiliary_data_dir()) / "JusPrin" / "state.json", "{{{ not json");
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();

    CHECK(persistence.document().has_identity());
    CHECK(fs::is_regular_file(persistence.state_file_path()));
    CHECK(fs::is_regular_file(persistence.state_file_path() + ".corrupt"));
}

TEST_CASE("an in-place reset starts a new project boundary", "[persistence][lifecycle]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    const std::string old_identity = persistence.document().project_id();
    persistence.document().append_message(persistence.document().active_conversation_id(),
                                          user_message(persistence.document().allocate_message_id(), "before reset", "c-1"),
                                          kT);
    persistence.flush();

    workspace.reset_project_in_place();

    // A Project event without a directory move is ambiguous (Plater also
    // publishes replacement events before the new directory exists), so the
    // boundary resolves at the owner's next pump rather than in the event.
    CHECK(persistence.document().project_id() == old_identity);
    persistence.resolve_pending_boundary();

    CHECK(persistence.document().project_id() != old_identity);
    CHECK(persistence.document().messages(persistence.document().active_conversation_id()).empty());
    // The old conversation cannot leak into the new boundary from disk.
    const json on_disk = json::parse(read_text(persistence.state_file_path()));
    CHECK(on_disk["project"]["projectId"] == persistence.document().project_id());
}

TEST_CASE("a replacement whose event outruns its directory move still adopts the new project",
          "[persistence][lifecycle]")
{
    // Plater::new_project publishes its Project event before the model
    // adopts the new auxiliary directory. Simulated here: an in-place-looking
    // event, after which the directory turns out to have moved.
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    const std::string old_identity = persistence.document().project_id();
    persistence.document().append_message(persistence.document().active_conversation_id(),
                                          user_message(persistence.document().allocate_message_id(), "kept?", "c-1"), kT);
    persistence.flush();

    workspace.reset_project_in_place();          // Project event, directory unchanged: parked
    workspace.move_auxiliary_dir_for_testing();  // the directory move lands afterwards
    persistence.resolve_pending_boundary();      // the owner's pump

    // The settled directory is empty, so this is a fresh project — not a
    // re-adoption of the old state.json left in the previous directory.
    CHECK(persistence.document().project_id() != old_identity);
    CHECK(persistence.document().messages(persistence.document().active_conversation_id()).empty());
}

TEST_CASE("a project replacement adopts the new project's own state", "[persistence][lifecycle]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    const std::string old_identity = persistence.document().project_id();

    workspace.replace_project(small_snapshot());
    CHECK(persistence.document().project_id() != old_identity);
    CHECK(persistence.document().conversations().size() == 1);
}

TEST_CASE("revert failures leave the document and files untouched", "[persistence][revert]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "renamed").succeeded());
    const std::string target = persistence.document().revisions().front().id; // initial
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "renamed-again").succeeded());
    const std::size_t revisions_before = persistence.document().revisions().size();

    SECTION("missing checkpoint file") {
        fs::remove(fs::path(persistence.jusprin_data_dir()) /
                   persistence.document().find_revision(target)->snapshot_file);
        const auto result = persistence.revert_to_revision(target);
        CHECK_FALSE(result.ok);
        CHECK(persistence.document().revisions().size() == revisions_before);
        CHECK(workspace.snapshot().plates[0].objects[0].name == "renamed-again");
    }

    SECTION("unreadable checkpoint content") {
        write_text(fs::path(persistence.jusprin_data_dir()) /
                       persistence.document().find_revision(target)->snapshot_file,
                   "garbage");
        const auto result = persistence.revert_to_revision(target);
        CHECK_FALSE(result.ok);
        CHECK(persistence.document().revisions().size() == revisions_before);
        CHECK(workspace.snapshot().plates[0].objects[0].name == "renamed-again");
    }

    SECTION("reverting to the current revision is refused") {
        const auto result = persistence.revert_to_revision(persistence.document().current_revision_id());
        CHECK_FALSE(result.ok);
    }
}

TEST_CASE("a successful revert keeps earlier checkpoints usable", "[persistence][revert]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    const std::string initial = persistence.document().current_revision_id();
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "renamed-once").succeeded());
    const std::string middle = persistence.document().current_revision_id();
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "renamed-twice").succeeded());

    REQUIRE(persistence.revert_to_revision(middle).ok);
    CHECK(workspace.snapshot().plates[0].objects[0].name == "renamed-once");
    CHECK(persistence.document().revisions().size() == 2);
    // The earlier checkpoint travelled to the new auxiliary dir and still
    // supports a further revert.
    REQUIRE(persistence.revert_to_revision(initial).ok);
    CHECK(workspace.snapshot().plates[0].objects[0].name == "cube-a");

    SECTION("the reverted state is what a reopen would load") {
        const json on_disk = json::parse(read_text(persistence.state_file_path()));
        CHECK(on_disk["currentRevisionId"] == initial);
        CHECK(on_disk["revisions"].size() == 1);
    }
}

TEST_CASE("a clean-sharing copy carries no JusPrin state", "[persistence][clean-share]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();

    const fs::path copy = fs::path(unique_temp_dir("clean")) / "clean-copy.archive";
    REQUIRE(persistence.export_clean_copy(copy.string()).succeeded());
    REQUIRE(fs::is_regular_file(copy));
    // The archive holds the project content and nothing of the conversation.
    const std::string content = read_text(copy);
    CHECK(content.find("Persistence Fixture") != std::string::npos);
    CHECK(content.find("conversations") == std::string::npos);
    CHECK(content.find(persistence.document().project_id()) == std::string::npos);
}
