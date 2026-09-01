// Contract tests for the project state document (schema round-trip,
// unknown-field preservation, migration, corruption) and for
// ProjectPersistence against the fake workspace (project-boundary adoption,
// revision capture, recovery merge, revert atomicity). GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/FakeWorkspace.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

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
    for (int attempt = 0; attempt < 10; ++attempt) {
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const fs::path dir = fs::temp_directory_path() /
                             (std::string("jusprin-state-tests-") + label + "-" + std::to_string(nonce) + "-" +
                              std::to_string(++next));
        std::error_code error;
        if (fs::create_directory(dir, error))
            return dir.string();
        if (error && error != std::errc::file_exists)
            throw std::runtime_error("Unable to create a project-state test directory: " + error.message());
    }
    throw std::runtime_error("Unable to allocate a unique project-state test directory");
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

TEST_CASE("manufacturing hashes ignore presentation state and change with manufacturing input",
          "[project-state][history][hash]")
{
    Workspace::WorkspaceSnapshot snapshot = small_snapshot();
    const std::string original = *manufacturing_input_hash(snapshot, 0);
    CHECK(original.size() == 64);
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(deterministic_output_hash("input", "ab", "c") !=
          deterministic_output_hash("input", "a", "bc"));

    snapshot.setup.project_dirty = true;
    snapshot.plates[0].objects[0].name = "display name only";
    CHECK(manufacturing_input_hash(snapshot, 0) == original);

    snapshot.plates[0].objects[0].instances[0].position[0] = 12.5;
    CHECK(manufacturing_input_hash(snapshot, 0) != original);
    CHECK_FALSE(manufacturing_input_hash(snapshot, 99).has_value());
}

TEST_CASE("build copy and print records serialize with immutable provenance", "[project-state][history]")
{
    ProjectStateDocument document;
    document.initialize_identity("project-1", "lineage-1", kT);
    const std::string conversation = document.active_conversation_id();
    const std::string revision = document.add_revision("initial", "revisions/r-1.snapshot", conversation, kT);

    BuildRecord build;
    build.project_id = document.project_id();
    build.revision_id = revision;
    build.conversation_id = conversation;
    build.plate_index = 0;
    build.plate_name = "Plate 1";
    build.printer = "JusPrin One";
    build.material = "PLA Matte";
    build.manufacturing_input_hash = sha256_hex("input");
    build.output_hash = sha256_hex("gcode");
    build.slicer_version = "JusPrin deterministic";
    build.configuration_provenance = "printer + process + filament presets";
    build.statistics = {3600.0, 1234.5, 12.3, 0.42, 88};
    build.warnings = {"Long warning text remains semantic and serializable."};
    const std::string build_id = document.add_build(build, kT);

    ExportedCopyRecord copy;
    copy.build_id = build_id;
    copy.conversation_id = conversation;
    copy.destination = "/tmp/phase-six.gcode";
    copy.expected_output_hash = build.output_hash;
    copy.observed_output_hash = build.output_hash;
    document.add_exported_copy(copy, kT);

    PhysicalPrintRecord print;
    print.build_id = build_id;
    print.project_id = document.project_id();
    print.revision_id = revision;
    print.conversation_id = conversation;
    print.plate_name = build.plate_name;
    print.printer = build.printer;
    print.material = build.material;
    print.outcome = "completed";
    print.manufacturing_input_hash = build.manufacturing_input_hash;
    print.output_hash = build.output_hash;
    print.gcode_hash = build.output_hash;
    print.statistics = build.statistics;
    document.add_physical_print(print, kT);

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    REQUIRE(reloaded.builds().size() == 1);
    REQUIRE(reloaded.exported_copies().size() == 1);
    REQUIRE(reloaded.physical_prints().size() == 1);
    CHECK(reloaded.builds()[0].manufacturing_input_hash == build.manufacturing_input_hash);
    CHECK(reloaded.builds()[0].statistics.layer_count == 88);
    CHECK(reloaded.exported_copies()[0].expected_output_hash == build.output_hash);
    CHECK(reloaded.physical_prints()[0].printer == "JusPrin One");
    CHECK(reloaded.physical_prints()[0].gcode_hash == build.output_hash);
    const json persisted = json::parse(reloaded.dump());
    CHECK_FALSE(persisted["builds"][0].contains("stale"));
    CHECK_FALSE(persisted["physicalPrints"][0].contains("timelineRemoved"));
}

TEST_CASE("revert removes later builds and copies but retains the physical print ledger",
          "[project-state][history][revert]")
{
    ProjectStateDocument document;
    document.initialize_identity("project-1", "lineage-1", kT);
    const std::string conversation = document.active_conversation_id();
    const std::string target = document.add_revision("initial", "revisions/r-1.snapshot", conversation, kT);
    const std::string later = document.add_revision("contents", "", conversation, kT); // no recoverable later snapshot

    BuildRecord build;
    build.project_id = document.project_id();
    build.revision_id = later;
    build.conversation_id = conversation;
    build.manufacturing_input_hash = sha256_hex("later input");
    build.output_hash = sha256_hex("later gcode");
    const std::string build_id = document.add_build(build, kT);
    ExportedCopyRecord copy;
    copy.build_id = build_id;
    copy.expected_output_hash = build.output_hash;
    document.add_exported_copy(copy, kT);
    PhysicalPrintRecord print;
    print.build_id = build_id;
    print.project_id = document.project_id();
    print.revision_id = later;
    print.outcome = "completed";
    print.output_hash = build.output_hash;
    print.gcode_hash = build.output_hash;
    document.add_physical_print(print, kT);

    REQUIRE(document.revert_to_revision(target).has_value());
    CHECK(document.builds().empty());
    CHECK(document.exported_copies().empty());
    REQUIRE(document.physical_prints().size() == 1);
    CHECK(document.physical_prints()[0].revision_id == later);
    CHECK_FALSE(document.find_revision(later).has_value());
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

namespace {

// Builds a staged attachment record with the fields the host would populate.
AttachmentRecord staged_attachment(const std::string& id, const std::string& name, const std::string& kind)
{
    AttachmentRecord record;
    record.id            = id;
    record.original_name = name;
    record.stored_name   = name;
    record.kind          = kind;
    record.mime          = "application/octet-stream";
    record.size_bytes    = 4;
    record.source        = "picker";
    record.state         = "staged";
    return record;
}

} // namespace

TEST_CASE("attachment records round-trip and preserve unknown fields", "[project-state][attachments]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);

    const std::string a1 = document.allocate_attachment_id();
    AttachmentRecord  one = staged_attachment(a1, "notes.txt", "text");
    one.preview_text      = "hello world";
    document.add_attachment(one, kT);
    const std::string a2 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a2, "photo.png", "image"), kT);

    CHECK(a1 == "a-1");
    CHECK(a2 == "a-2");
    REQUIRE(document.attachments().size() == 2);
    REQUIRE(document.find_attachment(a1).has_value());
    CHECK(document.find_attachment(a1)->preview_text == "hello world");
    CHECK(document.find_attachment(a1)->relative_path() == "attachments/a-1/notes.txt");

    // A newer build added a field this build does not know; it must survive.
    json raw                                 = json::parse(document.dump());
    raw["attachments"][0]["futureAttachFlag"] = 7;

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(raw.dump()) == ProjectStateDocument::LoadResult::Loaded);
    REQUIRE(reloaded.attachments().size() == 2);
    CHECK(reloaded.attachments()[1].kind == "image");

    // Update flips state and preserves the unknown field.
    AttachmentRecord edited = reloaded.find_attachment(a1).value();
    edited.state            = "sent";
    REQUIRE(reloaded.update_attachment(edited));
    const json round = json::parse(reloaded.dump());
    CHECK(round["attachments"][0]["futureAttachFlag"] == 7);
    CHECK(round["attachments"][0]["state"] == "sent");
}

TEST_CASE("staged attachments can be removed but sent ones are durable", "[project-state][attachments]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    const std::string a1 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a1, "a.txt", "text"), kT);

    // A staged attachment can be discarded; its blob directory is reported.
    const std::optional<std::string> dir = document.remove_staged_attachment(a1);
    REQUIRE(dir.has_value());
    CHECK(*dir == "attachments/a-1");
    CHECK(document.attachments().empty());

    // A sent attachment is history and cannot be removed this way.
    const std::string a2 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a2, "b.txt", "text"), kT);
    const std::string message_id = document.allocate_message_id();
    ConversationMessage message   = user_message(message_id, "see attached", "c-1");
    message.attachment_ids        = {a2};
    document.append_message(document.active_conversation_id(), message, kT);
    CHECK(document.mark_attachments_sent({a2}) == std::vector<std::string>{a2});
    CHECK_FALSE(document.remove_staged_attachment(a2).has_value());
    CHECK(document.find_attachment(a2)->state == "sent");

    // The reference survives a round-trip on the message.
    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    REQUIRE(reloaded.messages(reloaded.active_conversation_id()).size() == 1);
    CHECK(reloaded.messages(reloaded.active_conversation_id())[0].attachment_ids == std::vector<std::string>{a2});
}

TEST_CASE("revert keeps referenced attachments but drops composer attachments", "[project-state][attachments][revert]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    const std::string conversation = document.active_conversation_id();

    // Before the revision target: a1 sent-and-still-referenced, a3 staged and
    // never sent, a4 staged now but sent by a later (removed) message.
    const std::string a1 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a1, "a1.txt", "text"), kT);
    const std::string a3 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a3, "a3.txt", "text"), kT);
    const std::string a4 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a4, "a4.txt", "text"), kT);

    ConversationMessage kept = user_message(document.allocate_message_id(), "keep", "c-1");
    kept.attachment_ids      = {a1};
    document.append_message(conversation, kept, kT);
    document.mark_attachments_sent({a1});

    const std::string target = document.add_revision("initial", "revisions/r-1.snapshot", conversation, kT);

    // After the target: a2 created and sent, plus the message that also sends a4.
    const std::string a2 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a2, "a2.txt", "text"), kT);
    ConversationMessage later = user_message(document.allocate_message_id(), "later", "c-2");
    later.attachment_ids      = {a2, a4};
    document.append_message(conversation, later, kT);
    document.mark_attachments_sent({a2, a4});
    document.add_revision("contents", "revisions/r-2.snapshot", conversation, kT);

    const auto result = document.revert_to_revision(target);
    REQUIRE(result.has_value());
    // Kept: a1 (sent and still referenced). Dropped: a3 (unsent composer
    // state), a4 (sent by the truncated message), and a2 (created after the
    // target). IDs come from the monotonic allocator, so compare against the
    // variables, not literals.
    CHECK(result->kept_attachment_dirs == std::vector<std::string>{"attachments/" + a1});
    CHECK(result->removed_attachment_dirs ==
          std::vector<std::string>{"attachments/" + a3, "attachments/" + a4, "attachments/" + a2});

    std::vector<std::string> remaining;
    for (const AttachmentRecord& record : document.attachments())
        remaining.push_back(record.id);
    CHECK(remaining == std::vector<std::string>{a1});
}

TEST_CASE("a successful revert clears unsent recovery state", "[persistence][attachments][revert]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    const std::string target = persistence.document().current_revision_id();

    const std::string staged = persistence.document().allocate_attachment_id();
    persistence.document().add_attachment(staged_attachment(staged, "unsent.txt", "text"), kT);
    REQUIRE(persistence.write_attachment_blob("attachments/" + staged + "/unsent.txt", "UNSENT"));
    persistence.set_draft("half-written prompt");
    persistence.commit();
    persistence.flush();
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "later").succeeded());

    REQUIRE(persistence.revert_to_revision(target).ok);
    CHECK(persistence.draft().empty());
    CHECK_FALSE(persistence.document().find_attachment(staged).has_value());
    CHECK_FALSE(fs::exists(fs::path(persistence.jusprin_data_dir()) / "attachments" / staged));
    const json recovery_meta = json::parse(read_text(fs::path(persistence.recovery_dir()) / "recovery.json"));
    CHECK(recovery_meta["draft"] == "");
}

TEST_CASE("attachment blobs are written under the project and cleaned up", "[persistence][attachments]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();

    REQUIRE(persistence.write_attachment_blob("attachments/a-1/notes.txt", "hello"));
    const fs::path blob = fs::path(persistence.jusprin_data_dir()) / "attachments" / "a-1" / "notes.txt";
    REQUIRE(fs::is_regular_file(blob));
    CHECK(read_text(blob) == "hello");
    // Blobs live only in the project's auxiliary dir, not the recovery mirror.
    CHECK_FALSE(fs::exists(fs::path(persistence.recovery_dir()) / "attachments" / "a-1" / "notes.txt"));

    persistence.remove_attachment_dir("attachments/a-1");
    CHECK_FALSE(fs::exists(blob));
}

TEST_CASE("revert copies kept attachment blobs forward and drops orphaned ones", "[persistence][attachments][revert]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, config_with_recovery(unique_temp_dir("recovery")));
    persistence.attach();
    ProjectStateDocument& document = persistence.document();

    // A sent attachment that will survive a revert to the revision after it.
    const std::string a1 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a1, "keep.txt", "text"), kT);
    REQUIRE(persistence.write_attachment_blob("attachments/" + a1 + "/keep.txt", "KEEP"));
    ConversationMessage m1 = user_message(document.allocate_message_id(), "keep", "c-1");
    m1.attachment_ids      = {a1};
    document.append_message(document.active_conversation_id(), m1, kT);
    document.mark_attachments_sent({a1});
    persistence.commit();
    persistence.flush();

    // A manufacturing change captures the revision we will revert to.
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "r2").succeeded());
    const std::string target = persistence.document().revisions().back().id;

    // A later attachment that the revert must orphan.
    const std::string a2 = document.allocate_attachment_id();
    document.add_attachment(staged_attachment(a2, "drop.txt", "text"), kT);
    REQUIRE(persistence.write_attachment_blob("attachments/" + a2 + "/drop.txt", "DROP"));
    ConversationMessage m2 = user_message(document.allocate_message_id(), "later", "c-2");
    m2.attachment_ids      = {a2};
    document.append_message(document.active_conversation_id(), m2, kT);
    document.mark_attachments_sent({a2});
    persistence.commit();
    persistence.flush();
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "r3").succeeded());

    const ProjectPersistence::RevertResult reverted = persistence.revert_to_revision(target);
    REQUIRE(reverted.ok);

    // The kept blob now lives under the replacement project's auxiliary dir;
    // the orphaned one was never copied forward.
    const fs::path new_dir = fs::path(persistence.jusprin_data_dir());
    CHECK(read_text(new_dir / "attachments" / a1 / "keep.txt") == "KEEP");
    CHECK_FALSE(fs::exists(new_dir / "attachments" / a2 / "drop.txt"));
    CHECK(persistence.document().find_attachment(a1).has_value());
    CHECK_FALSE(persistence.document().find_attachment(a2).has_value());
}
