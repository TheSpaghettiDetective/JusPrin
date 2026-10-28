// Contract tests for the project state document (schema round-trip,
// unknown-field preservation, migration, corruption) and for
// ProjectPersistence against the fake workspace (project-boundary adoption,
// attachments). GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"
#include "slic3r/GUI/JusPrin/Agent/ProjectStateDocument.hpp"
#include "../jusprin_support/FakeWorkspace.hpp"

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

ProjectPersistence::Config test_config()
{
    ProjectPersistence::Config config;
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
    activity.call_id        = "call_9";
    activity.server         = "jusprin-native";
    activity.tool           = "plate_layout";
    activity.state          = ToolState::Succeeded;
    document.upsert_activity(activity, kT);

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
    CHECK(reloaded.activities()[0].source == ToolSource::Agent);
    CHECK(reloaded.activities()[0].call_id == "call_9");

    SECTION("counters continue after a reload so IDs stay unique") {
        const std::string next_id = reloaded.allocate_message_id();
        CHECK(next_id == "m-3");
    }
}

TEST_CASE("external tool activity source survives storage", "[project-state][schema][mcp]")
{
    ProjectStateDocument document;
    ToolActivity activity;
    activity.action_id = document.allocate_action_id();
    activity.correlation_id = "mcp-42";
    activity.source = ToolSource::Mcp;
    activity.state = ToolState::Cancelled;
    document.upsert_activity(activity, kT);
    ProjectStateDocument restored;
    REQUIRE(restored.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    REQUIRE(restored.activities().size() == 1);
    CHECK(restored.activities()[0].source == ToolSource::Mcp);
    // A record with no source is not guessed at.
    auto unsourced = nlohmann::json::parse(document.dump());
    unsourced["toolActivities"][0].erase("source");
    REQUIRE(restored.load(unsourced.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK_THROWS(restored.activities());
}

TEST_CASE("chat metadata follows activity and manual titles survive reload", "[project-state][conversations]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const auto first = document.active_conversation_id();
    const auto second = document.create_conversation({}, kT);
    CHECK(document.conversations().front().id == second);
    document.append_message(first, user_message(document.allocate_message_id(), "Backpack frame", "first"),
                            "2026-09-03T12:00:00Z");
    CHECK(document.conversations().front().id == first);
    CHECK(document.conversations().front().preview == "Backpack frame");
    CHECK(document.conversations().front().updated_at == "2026-09-03T12:00:00Z");
    REQUIRE(document.rename_conversation(first, "Backpack frame print", true));
    CHECK_FALSE(document.needs_conversation_title(first));
    REQUIRE(document.rename_conversation(first, "  My frame  "));
    CHECK_FALSE(document.rename_conversation(first, "Automatic overwrite", true));
    CHECK_FALSE(document.rename_conversation(first, " \t "));
    CHECK_FALSE(document.rename_conversation(first, "two\nlines"));
    CHECK_FALSE(document.rename_conversation(first, std::string(121, 'x')));
    ProjectStateDocument loaded;
    REQUIRE(loaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(loaded.conversations().front().title == "My frame");
    CHECK_FALSE(loaded.needs_conversation_title(first));
    CHECK(loaded.needs_conversation_title(second));
}

// A chat's model choice is its own and survives reopening the project; an
// older project, which has none, leaves the chat on the app's default.
TEST_CASE("a chat keeps its own model choice across a reload", "[project-state][conversations]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const auto first  = document.active_conversation_id();
    const auto second = document.create_conversation({}, kT);
    CHECK_FALSE(document.chat_model(first));

    const ChatModelChoice opus{"anthropic", "claude-opus-5-5", "high"};
    REQUIRE(document.set_chat_model(first, opus));
    CHECK(document.chat_model(first) == opus);
    CHECK_FALSE(document.chat_model(second));
    // Switching mid-chat replaces the choice.
    const ChatModelChoice mini{"openai", "gpt-5.4-mini", ""};
    REQUIRE(document.set_chat_model(first, mini));
    CHECK(document.chat_model(first) == mini);
    // A choice must name a provider and a model, and a chat that exists.
    CHECK_FALSE(document.set_chat_model(first, ChatModelChoice{"openai", "", "low"}));
    CHECK_FALSE(document.set_chat_model("c-missing", opus));

    ProjectStateDocument loaded;
    REQUIRE(loaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(loaded.chat_model(first) == mini);
    CHECK_FALSE(loaded.chat_model(second));
}

TEST_CASE("deleting chats removes their content and preserves manufacturing history", "[project-state][conversations]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const auto first = document.active_conversation_id();
    AttachmentRecord attachment;
    attachment.id = document.allocate_attachment_id();
    attachment.state = "sent";
    attachment.stored_name = "notes.txt";
    document.add_attachment(attachment, kT);
    auto message = user_message(document.allocate_message_id(), "delete these notes", "client-1");
    message.attachment_ids = {attachment.id};
    document.append_message(first, message, kT);
    ToolActivity activity;
    activity.action_id = document.allocate_action_id();
    activity.correlation_id = message.id;
    activity.state = ToolState::Succeeded;
    document.upsert_activity(activity, kT);
    GcodeRecord gcode;
    gcode.hash = sha256_hex("gcode");
    GcodeDestination send;
    send.kind = GcodeDestination::Kind::Send;
    document.add_gcode_print(gcode.hash, GcodePrint{"", document.add_gcode_destination(gcode, send, kT)}, kT);
    const auto second = document.create_conversation("Keep this chat", kT);
    REQUIRE(document.set_active_conversation(first));
    const auto removed = document.delete_conversation(first, kT);
    REQUIRE(removed);
    CHECK(*removed == std::vector<std::string>{attachment.relative_dir()});
    CHECK(document.active_conversation_id() != second);
    CHECK(document.viewed_conversation_id() == document.active_conversation_id());
    CHECK(document.messages(first).empty());
    CHECK(document.attachments().empty());
    CHECK(document.activities().empty());
    REQUIRE(document.gcode_records().size() == 1);
    CHECK(document.gcode_records()[0].conversation_id == first);
    CHECK(document.gcode_records()[0].prints.size() == 1);
    CHECK_FALSE(document.delete_conversation(first, kT));
    REQUIRE(document.delete_conversation(second, kT));
    REQUIRE(document.conversations().size() == 1);
    CHECK(document.conversations().front().title == "New chat");
    CHECK(document.active_conversation_id() != first);
    CHECK(document.active_conversation_id() != second);
    ProjectStateDocument loaded;
    REQUIRE(loaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(loaded.messages(first).empty());
    CHECK(loaded.gcode_records().size() == 1);
}

TEST_CASE("viewing history never changes authority and checkpoints bind a chat to a model", "[project-state][conversations]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const std::string first = document.active_conversation_id();
    REQUIRE(document.save_chat_checkpoint(first, "v-1", nlohmann::json{{"estimate", "saved"}}, kT));
    const auto saved = document.chat_checkpoint(first);
    REQUIRE(saved);
    CHECK((*saved)["versionId"] == "v-1");
    // The checkpoint holds what only it can; the chat keeps its intent and plan.
    CHECK_FALSE(saved->contains("planning"));

    const std::string second = document.create_conversation("Strong print", kT);
    REQUIRE(document.set_viewed_conversation(first));
    CHECK(document.active_conversation_id() == second);
    CHECK(document.viewed_conversation_id() == first);
    CHECK_FALSE(document.save_chat_checkpoint(first, "v-2", nlohmann::json::object(), kT));

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(reloaded.active_conversation_id() == second);
    CHECK(reloaded.viewed_conversation_id() == first);
    REQUIRE(reloaded.chat_checkpoint(first));
    CHECK((*reloaded.chat_checkpoint(first))["summary"]["estimate"] == "saved");
}

TEST_CASE("the setup a purpose was recorded under travels with the intent", "[project-state][intent]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const std::string first = document.active_conversation_id();
    CHECK(document.purpose_setup().empty());

    const json setup{{"printerPreset", "Bambu Lab X1 Carbon 0.4 nozzle"}, {"filamentPreset", "Bambu PLA Basic @BBL X1C"}};
    document.set_purpose_setup(setup);
    CHECK(document.purpose_setup() == setup);
    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(reloaded.purpose_setup() == setup);

    // A new chat starts from the active chat's intent, the setup included,
    // and then owns its copy.
    const std::string second = document.create_conversation({}, kT);
    CHECK(document.purpose_setup() == setup);
    document.set_purpose_setup(json::object());
    CHECK(document.purpose_setup().empty());
    REQUIRE(document.set_active_conversation(first));
    CHECK(document.purpose_setup() == setup);
    (void) second;
}

TEST_CASE("each chat owns its intent and plan, and the active chat's are the project's", "[project-state][intent][plan]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const std::string first = document.active_conversation_id();
    REQUIRE(document.set_setup_intent(first, "Load-bearing bracket"));
    PlanRecord plan;
    plan.headline = "Four walls and gyroid infill for strength";
    document.set_plan(plan, kT);

    SECTION("a new chat copies the intent and starts with no plan") {
        const std::string second = document.create_conversation({}, kT);
        CHECK(document.setup_intent(second) == "Load-bearing bracket");
        CHECK(document.plan().headline.empty());
        CHECK(document.plan().seq == 0);

        // The copy is the new chat's own: changing it leaves the first alone.
        REQUIRE(document.set_setup_intent(second, "Quick fit check"));
        PlanRecord quick;
        quick.headline = "Coarse layers and low infill for a quick fit check";
        document.set_plan(quick, kT);
        CHECK(document.plan(first).headline == "Four walls and gyroid infill for strength");
        CHECK(document.setup_intent(first) == "Load-bearing bracket");

        // Switching the active chat switches the project's intent and plan.
        REQUIRE(document.set_active_conversation(first));
        CHECK(document.plan().headline == "Four walls and gyroid infill for strength");
        REQUIRE(document.set_active_conversation(second));
        CHECK(document.plan().headline == "Coarse layers and low infill for a quick fit check");

        // Each is stored once, on its chat: nothing at the top of the document.
        const json raw = json::parse(document.dump());
        CHECK_FALSE(raw.contains("plan"));
        CHECK_FALSE(raw.contains("purposeSetup"));
        CHECK(raw["conversations"][0]["plan"]["headline"] == "Four walls and gyroid infill for strength");
        CHECK(raw["conversations"][1]["plan"]["headline"] == "Coarse layers and low infill for a quick fit check");
    }

    SECTION("only the active chat's plan is ever marked") {
        const std::string second = document.create_conversation({}, kT);
        PlanRecord other;
        other.headline = "Fine layers with supports away from visible surfaces";
        document.set_plan(other, kT);
        CHECK(document.mark_plan_needs_reassessment("Sparse infill density"));
        CHECK(document.plan(second).validity == "needs_reassessment");
        CHECK(document.plan(first).validity == "current");
    }

    SECTION("a chat with no plan has nothing to mark") {
        document.create_conversation({}, kT);
        CHECK_FALSE(document.mark_plan_needs_reassessment("Sparse infill density"));
        CHECK(document.plan().validity == "current");
    }

    SECTION("deleting the active chat starts its replacement from its intent, with no plan") {
        const auto removed = document.delete_conversation(first, kT);
        REQUIRE(removed);
        const std::string replacement = document.active_conversation_id();
        REQUIRE(replacement != first);
        CHECK(document.setup_intent(replacement) == "Load-bearing bracket");
        CHECK(document.plan().headline.empty());
    }

    SECTION("deleting an inactive chat leaves the active one's as they are") {
        const std::string second = document.create_conversation({}, kT);
        PlanRecord other;
        other.headline = "Spiral vase with a continuous single wall";
        document.set_plan(other, kT);
        REQUIRE(document.delete_conversation(first, kT));
        CHECK(document.active_conversation_id() == second);
        CHECK(document.plan().headline == "Spiral vase with a continuous single wall");
    }
}

TEST_CASE("a model version's regions are put back whole", "[project-state][regions]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const json regions = json::array({{{"id", "r-1"}, {"kind", "reinforce"}, {"seq", 3}, {"futureField", 1}}});
    const auto before = document.doc_revision();
    document.restore_regions(regions);
    CHECK(json::parse(document.dump())["regions"] == regions);
    CHECK(document.doc_revision() > before);
    CHECK_THROWS_AS(document.restore_regions(json::object()), std::invalid_argument);
}

TEST_CASE("a restore counts until the model next replies", "[project-state][changes]")
{
    ProjectStateDocument document;
    document.initialize_identity("p", "l", kT);
    const std::string chat = document.active_conversation_id();
    const auto reply = [&document, &chat](const std::string& id) {
        ConversationMessage message;
        message.id   = id;
        message.role = MessageRole::Assistant;
        document.append_message(chat, message, kT);
    };
    const auto restore = [&document]() {
        ChangeEntry change;
        change.kind = "restore";
        document.add_change(change, kT);
    };
    reply("m-1");
    CHECK_FALSE(document.restored_since_reply("m-2"));
    restore();
    CHECK(document.restored_since_reply("m-2"));
    // The reply being written is not one the model has given yet.
    reply("m-2");
    CHECK(document.restored_since_reply("m-2"));
    CHECK_FALSE(document.restored_since_reply("m-3"));
    restore();
    CHECK(document.restored_since_reply("m-3"));
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

TEST_CASE("a document of any other schema version is not read", "[project-state][schema]")
{
    ProjectStateDocument written;
    written.initialize_identity("p-old", "l-old", kT);
    CHECK(json::parse(written.dump())["schemaVersion"] == 1);
    for (const int version : {0, 2, 3, 4}) {
        json other = json::parse(written.dump());
        other["schemaVersion"] = version;
        ProjectStateDocument document;
        CHECK(document.load(other.dump()) == ProjectStateDocument::LoadResult::Corrupt);
        CHECK_FALSE(document.has_identity());
    }
}

TEST_CASE("manufacturing hashes ignore presentation state and change with manufacturing input",
          "[project-state][history][hash]")
{
    Workspace::WorkspaceSnapshot snapshot = small_snapshot();
    const std::string original = *manufacturing_input_hash(snapshot, 0);
    CHECK(original.size() == 64);
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    snapshot.setup.project_dirty = true;
    snapshot.plates[0].objects[0].name = "display name only";
    CHECK(manufacturing_input_hash(snapshot, 0) == original);

    snapshot.plates[0].objects[0].instances[0].position[0] = 12.5;
    CHECK(manufacturing_input_hash(snapshot, 0) != original);
    CHECK_FALSE(manufacturing_input_hash(snapshot, 99).has_value());
}

namespace {

GcodeDestination export_to(const std::string& path)
{
    GcodeDestination destination;
    destination.kind      = GcodeDestination::Kind::Export;
    destination.path      = path;
    destination.file_hash = sha256_hex("exported " + path);
    return destination;
}

GcodeDestination send_to(const std::string& printer, bool start)
{
    GcodeDestination destination;
    destination.kind            = GcodeDestination::Kind::Send;
    destination.printer_name    = printer;
    destination.printer_id      = "dev-" + printer;
    destination.transport       = "bambu_print";
    destination.start_requested = start;
    destination.delivered_at    = "2026-10-20T10:02:00Z";
    return destination;
}

} // namespace

TEST_CASE("a G-code record keeps where it went, and is created by the first time it leaves", "[project-state][history]")
{
    ProjectStateDocument document;
    document.initialize_identity("project-1", "lineage-1", kT);
    auto said = user_message(document.allocate_message_id(), "print it", "client-1");
    document.append_message(document.active_conversation_id(), said, kT);

    GcodeRecord facts;
    facts.hash                     = sha256_hex("plate 2 gcode");
    facts.plate_index              = 1;
    facts.plate_name               = "Plate 2";
    facts.printer                  = "Bambu Lab X1 Carbon 0.4 nozzle";
    facts.material                 = "Bambu PETG HF @BBL X1C";
    facts.process                  = "0.20mm Strength @BBL X1C";
    facts.manufacturing_input_hash = sha256_hex("input");
    facts.statistics               = {3600.0, 1234.5, 12.3, 0.42, 88};
    CHECK(document.add_gcode_destination(facts, export_to("C:/prints/bracket-v3.gcode.3mf"), kT) == "d-1");

    // The same bytes leaving again join the record; the facts it was made
    // with stay as they were.
    GcodeRecord later = facts;
    later.plate_name  = "renamed since";
    const std::uint64_t before = document.doc_revision();
    CHECK(document.add_gcode_destination(later, send_to("Garage X1C", true), "2026-10-20T10:01:00Z") == "d-2");
    CHECK(document.doc_revision() == before + 1);
    CHECK(document.gcode_record_count() == 1);

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    const std::optional<GcodeRecord> stored = reloaded.gcode_record(facts.hash);
    REQUIRE(stored);
    CHECK(stored->created_at == kT);
    CHECK(stored->plate_index == 1);
    CHECK(stored->plate_name == "Plate 2");
    CHECK(stored->printer == facts.printer);
    CHECK(stored->material == facts.material);
    CHECK(stored->process == facts.process);
    CHECK(stored->manufacturing_input_hash == facts.manufacturing_input_hash);
    CHECK(stored->statistics.layer_count == 88);
    // It left from this chat, after the person's message.
    CHECK(stored->conversation_id == document.active_conversation_id());
    CHECK(stored->after_id == said.id);
    REQUIRE(stored->destinations.size() == 2);
    const GcodeDestination& exported = stored->destinations[0];
    CHECK(exported.kind == GcodeDestination::Kind::Export);
    CHECK(exported.at == kT);
    CHECK(exported.path == "C:/prints/bracket-v3.gcode.3mf");
    CHECK(exported.file_hash == sha256_hex("exported C:/prints/bracket-v3.gcode.3mf"));
    CHECK(exported.after_id == said.id);
    const GcodeDestination& sent = stored->destinations[1];
    CHECK(sent.kind == GcodeDestination::Kind::Send);
    CHECK(sent.at == "2026-10-20T10:01:00Z");
    CHECK(sent.printer_name == "Garage X1C");
    CHECK(sent.printer_id == "dev-Garage X1C");
    CHECK(sent.transport == "bambu_print");
    CHECK(sent.start_requested);
    CHECK(sent.delivered_at == "2026-10-20T10:02:00Z");
    CHECK(gcode_display_name(*stored) == "bracket-v3.gcode.3mf");

    // A different G-code is a record of its own.
    GcodeRecord other = facts;
    other.hash        = sha256_hex("plate 1 gcode");
    document.add_gcode_destination(other, send_to("Garage X1C", false), kT);
    CHECK(document.gcode_record_count() == 2);
    CHECK(gcode_display_name(*document.gcode_record(other.hash)) == "Plate 2");
}

TEST_CASE("a print follows a send of its G-code and finishes once", "[project-state][history]")
{
    ProjectStateDocument document;
    document.initialize_identity("project-1", "lineage-1", kT);
    GcodeRecord facts;
    facts.hash = sha256_hex("gcode");
    const std::string exported = document.add_gcode_destination(facts, export_to("C:/out.gcode"), kT);
    const std::string sent     = document.add_gcode_destination(facts, send_to("Garage X1C", true), kT);

    // A print needs a recorded G-code and one of its sends; an export cannot
    // be printed from.
    CHECK_THROWS_AS(document.add_gcode_print(sha256_hex("unknown"), GcodePrint{"", sent}, kT), std::logic_error);
    CHECK_THROWS_AS(document.add_gcode_print(facts.hash, GcodePrint{"", exported}, kT), std::logic_error);
    CHECK_THROWS_AS(document.add_gcode_print(facts.hash, GcodePrint{"", "d-9"}, kT), std::logic_error);
    CHECK(document.gcode_record(facts.hash)->prints.empty());

    GcodePrint print;
    print.destination_id = sent;
    print.printer_name   = "Garage X1C";
    print.printer_id     = "dev-Garage X1C";
    print.job_id         = "job-7";
    const std::string id = document.add_gcode_print(facts.hash, print, "2026-10-20T10:05:00Z");
    CHECK(id == "p-1");

    CHECK_THROWS_AS(document.finish_gcode_print(facts.hash, "p-9", kT), std::logic_error);
    CHECK_THROWS_AS(document.finish_gcode_print(sha256_hex("unknown"), id, kT), std::logic_error);
    const std::uint64_t before = document.doc_revision();
    document.finish_gcode_print(facts.hash, id, "2026-10-20T12:40:00Z");
    // A printer repeats its state; the first report is when it finished.
    document.finish_gcode_print(facts.hash, id, "2026-10-20T12:41:00Z");
    CHECK(document.doc_revision() == before + 1);

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    const GcodeRecord stored = *reloaded.gcode_record(facts.hash);
    REQUIRE(stored.prints.size() == 1);
    CHECK(stored.prints[0].id == id);
    CHECK(stored.prints[0].destination_id == sent);
    CHECK(stored.prints[0].printer_name == "Garage X1C");
    CHECK(stored.prints[0].job_id == "job-7");
    CHECK(stored.prints[0].started_at == "2026-10-20T10:05:00Z");
    CHECK(stored.prints[0].finished_at == "2026-10-20T12:40:00Z");
}

// What the Print button notes and the document keeps once the printer takes
// the job: the plate as the workspace had it.
TEST_CASE("a G-code record is made from the plate as the workspace has it", "[project-state][history]")
{
    Workspace::WorkspaceSnapshot snapshot = small_snapshot();
    snapshot.setup.printer_preset  = "Bambu Lab A1 0.4 nozzle";
    snapshot.setup.filament_preset = "Bambu PLA Basic @BBL A1";
    snapshot.setup.process_preset  = "0.20mm Standard @BBL A1";
    Workspace::SliceEstimate estimate;
    estimate.print_time_seconds = 2520;
    estimate.has_time           = true;
    estimate.material_grams     = 18.5;
    estimate.has_material       = true;
    estimate.material_cost      = 0.0; // a filament with no price set
    snapshot.plates[0].estimate = estimate;

    const std::optional<GcodeRecord> record = gcode_record(snapshot, 0);
    REQUIRE(record);
    CHECK_FALSE(gcode_record(snapshot, 1).has_value());
    CHECK(record->hash.empty()); // the G-code's bytes are the caller's to hash
    CHECK(record->plate_index == 0);
    CHECK(record->plate_name == "Plate 1");
    CHECK(record->printer == "Bambu Lab A1 0.4 nozzle");
    CHECK(record->material == "Bambu PLA Basic @BBL A1");
    CHECK(record->process == "0.20mm Standard @BBL A1");
    CHECK(record->manufacturing_input_hash == *manufacturing_input_hash(snapshot, 0));
    CHECK(record->statistics.print_time_seconds == 2520.0);
    CHECK(record->statistics.material_grams == 18.5);
    CHECK(record->statistics.material_cost == 0.0);
}

TEST_CASE("a G-code record needs the hash of its bytes", "[project-state][history]")
{
    ProjectStateDocument document;
    document.initialize_identity("project-1", "lineage-1", kT);
    CHECK_THROWS_AS(document.add_gcode_destination(GcodeRecord{}, export_to("C:/out.gcode"), kT), std::logic_error);
    CHECK(document.gcode_record_count() == 0);
}

// Sent events and physical prints were written only by tests and the Phase 6
// era before the G-code record replaced them; a document holding them loads
// and drops them.
TEST_CASE("a document with Sent events and physical prints loads without them", "[project-state][history]")
{
    ProjectStateDocument document;
    document.initialize_identity("project-1", "lineage-1", kT);
    json older = json::parse(document.dump());
    older.erase("gcodes");
    older["counters"].erase("nextDestination");
    older["counters"]["nextSent"] = 2;
    older["sent"] = json::array({json{{"id", "s-1"}, {"seq", 1}, {"printer", "X1C"}}});
    older["physicalPrints"] = json::array({json{{"id", "p-1"}, {"seq", 2}, {"sentId", "s-1"}, {"outcome", "failed"}}});
    ProjectStateDocument loaded;
    REQUIRE(loaded.load(older.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(loaded.has_identity());
    CHECK(loaded.gcode_records().empty());
    const json reloaded = json::parse(loaded.dump());
    CHECK_FALSE(reloaded.contains("sent"));
    CHECK_FALSE(reloaded.contains("physicalPrints"));
    CHECK_FALSE(reloaded["counters"].contains("nextSent"));
    // The new list starts when the first G-code leaves.
    GcodeRecord facts;
    facts.hash = sha256_hex("gcode");
    CHECK(loaded.add_gcode_destination(facts, export_to("C:/out.gcode"), kT) == "d-1");
    CHECK(loaded.gcode_record_count() == 1);
}

TEST_CASE("the project keeps the most recently written G-code copies", "[project-state][history]")
{
    const fs::path project = fs::temp_directory_path() / "jusprin-kept-gcode-test";
    fs::remove_all(project);
    const fs::path sources = project / "sources";
    fs::create_directories(sources);
    const auto source = [&](int n) {
        const fs::path path = sources / ("plate" + std::to_string(n) + ".gcode");
        std::ofstream(path, std::ios::binary) << "G1 X" << n << "\n";
        return path;
    };
    const auto hash_of = [](int n) { return sha256_hex("gcode " + std::to_string(n)); };
    const auto age = [&](int n, int seconds) {
        fs::last_write_time(project / "gcode" / (hash_of(n) + ".gcode"),
                            fs::file_time_type::clock::now() - std::chrono::seconds(seconds));
    };

    const std::string first = keep_gcode_copy(project.u8string(), hash_of(1), source(1).u8string(), 2);
    CHECK(first == (project / "gcode" / (hash_of(1) + ".gcode")).u8string());
    CHECK(read_text(first) == "G1 X1\n");
    age(1, 300);
    keep_gcode_copy(project.u8string(), hash_of(2), source(2).u8string(), 2);
    age(2, 200);
    // The first G-code leaves again: its copy is the newest now.
    keep_gcode_copy(project.u8string(), hash_of(1), source(1).u8string(), 2);
    keep_gcode_copy(project.u8string(), hash_of(3), source(3).u8string(), 2);
    CHECK_FALSE(kept_gcode_copy(project.u8string(), hash_of(1)).empty());
    CHECK(kept_gcode_copy(project.u8string(), hash_of(2)).empty());
    CHECK_FALSE(kept_gcode_copy(project.u8string(), hash_of(3)).empty());

    CHECK_THROWS_AS(keep_gcode_copy(project.u8string(), "../escape", source(1).u8string(), 2), std::logic_error);
    CHECK_THROWS(keep_gcode_copy(project.u8string(), hash_of(4), (sources / "missing.gcode").u8string(), 2));
    fs::remove_all(project);
}

// A Bambu printer takes a .gcode.3mf: a send to one keeps the package it
// uploaded, byte for byte, beside the G-code's copy, and Print again sends it.
TEST_CASE("a Bambu send's package is kept beside its G-code and goes with it", "[project-state][history]")
{
    const fs::path project = fs::path(unique_temp_dir("kept-package"));
    const fs::path sources = project / "sources";
    fs::create_directories(sources);
    const auto write = [&](const std::string& name, const std::string& bytes) {
        std::ofstream(sources / name, std::ios::binary) << bytes;
        return (sources / name).u8string();
    };
    const auto hash_of = [](int n) { return sha256_hex("gcode " + std::to_string(n)); };
    const std::string dir = project.u8string();

    // Only beside a kept copy.
    CHECK_THROWS_AS(keep_gcode_package(dir, hash_of(1), write("a.gcode.3mf", "PK first"), {}), std::logic_error);
    CHECK_FALSE(kept_gcode_package(dir, hash_of(1)).has_value());

    keep_gcode_copy(dir, hash_of(1), write("1.gcode", "G1 X1\n"), 2);
    // A cloud send: the package and its config.
    const std::string first = keep_gcode_package(dir, hash_of(1), write("a.gcode.3mf", "PK first"), write("a.config.3mf", "PK config"));
    CHECK(first == sha256_hex("PK first"));
    auto kept = kept_gcode_package(dir, hash_of(1));
    REQUIRE(kept.has_value());
    CHECK(read_text(kept->package) == "PK first");
    CHECK(read_text(kept->config) == "PK config");

    // A later LAN send of the same G-code: its package replaces the first, and
    // the cloud send's config, which belonged to that package, goes.
    CHECK(keep_gcode_package(dir, hash_of(1), write("b.gcode.3mf", "PK second"), {}) == sha256_hex("PK second"));
    kept = kept_gcode_package(dir, hash_of(1));
    REQUIRE(kept.has_value());
    CHECK(read_text(kept->package) == "PK second");
    CHECK(kept->config.empty());

    // Pruning a G-code's copy takes its package with it.
    fs::last_write_time(project / "gcode" / (hash_of(1) + ".gcode"), fs::file_time_type::clock::now() - std::chrono::seconds(300));
    keep_gcode_copy(dir, hash_of(2), write("2.gcode", "G1 X2\n"), 1);
    CHECK(kept_gcode_copy(dir, hash_of(1)).empty());
    CHECK_FALSE(kept_gcode_package(dir, hash_of(1)).has_value());
    CHECK_FALSE(fs::exists(project / "gcode" / (hash_of(1) + ".gcode.3mf")));
    fs::remove_all(project);
}

TEST_CASE("the kept package must hash as the newest send that kept one said", "[project-state][history]")
{
    GcodeRecord record;
    CHECK(kept_package_hash(record).empty());
    GcodeDestination exported = export_to("C:/out.gcode");
    GcodeDestination first;
    first.kind         = GcodeDestination::Kind::Send;
    first.package_hash = sha256_hex("PK first");
    GcodeDestination host;
    host.kind = GcodeDestination::Kind::Send;
    GcodeDestination second = first;
    second.package_hash     = sha256_hex("PK second");
    record.destinations     = {exported, first, second, host};
    CHECK(kept_package_hash(record) == sha256_hex("PK second"));

    // It round-trips with the send that kept it.
    ProjectStateDocument document;
    GcodeRecord          facts;
    facts.hash = sha256_hex("gcode");
    document.add_gcode_destination(facts, second, kT);
    ProjectStateDocument loaded;
    REQUIRE(loaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(loaded.gcode_record(facts.hash)->destinations.at(0).package_hash == sha256_hex("PK second"));
}

TEST_CASE("a file's hash is the hash of its bytes", "[project-state][history]")
{
    const fs::path path = fs::temp_directory_path() / "jusprin-hash-test.gcode";
    std::string bytes(200000, 'x');
    bytes += "tail";
    std::ofstream(path, std::ios::binary) << bytes;
    CHECK(sha256_file_hex(path.u8string()) == sha256_hex(bytes));
    fs::remove(path);
    CHECK_THROWS(sha256_file_hex(path.u8string()));
}

// Documents written while the app kept builds and exported copies still load.
// The records are no longer read, and stay in the file untouched.
TEST_CASE("a document with build and exported-copy records still loads", "[project-state][history]")
{
    ProjectStateDocument document;
    document.initialize_identity("project-1", "lineage-1", kT);
    json older = json::parse(document.dump());
    older["builds"] = json::array({json{{"id", "b-1"}, {"seq", 1}}});
    older["exportedCopies"] = json::array({json{{"id", "e-1"}, {"seq", 2}, {"buildId", "b-1"}}});
    ProjectStateDocument loaded;
    REQUIRE(loaded.load(older.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(loaded.has_identity());
    CHECK(json::parse(loaded.dump())["builds"].size() == 1);
}

// Documents written before the timeline stopped saving project copies carry a
// revision list, a current-revision pointer and revisionId on every record.
// No such file shipped outside the team, so nothing migrates them: the extra
// fields are simply never read again, and the chat they sit beside is intact.
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

TEST_CASE("adoption creates identity and the state file", "[persistence][adoption]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());

    int replaced = 0;
    persistence.set_document_replaced_listener([&replaced]() { ++replaced; });
    persistence.attach();

    CHECK(replaced == 1);
    CHECK(persistence.document().has_identity());
    CHECK(persistence.document().conversations().size() == 1);
    CHECK(fs::is_regular_file(persistence.state_file_path()));
    // JusPrin keeps no copy of the project: the JusPrin data dir holds the
    // state file alone.
    CHECK(std::distance(fs::directory_iterator(persistence.jusprin_data_dir()), fs::directory_iterator()) == 1);
}

TEST_CASE("model edits write no project copies", "[persistence][adoption]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();

    REQUIRE(workspace.select_object(workspace.snapshot().plates[0].objects[0].id).succeeded());
    REQUIRE(workspace.rename_object(workspace.snapshot().plates[0].objects[0].id, "renamed").succeeded());
    persistence.flush();
    CHECK(std::distance(fs::directory_iterator(persistence.jusprin_data_dir()), fs::directory_iterator()) == 1);
}

TEST_CASE("saved state is adopted on reopen", "[persistence][adoption]")
{
    Workspace::FakeWorkspace first_workspace(small_snapshot());
    ProjectPersistence first(first_workspace, test_config());
    first.attach();
    const std::string project_id   = first.document().project_id();
    const std::string conversation = first.document().active_conversation_id();
    first.document().append_message(conversation, user_message(first.document().allocate_message_id(), "saved message", "c-1"),
                                    kT);
    first.flush();
    const std::string saved_state = read_text(first.state_file_path());

    // A reopened project carries the saved state.json in its extracted
    // auxiliary dir.
    Workspace::FakeWorkspace second_workspace(small_snapshot());
    write_text(fs::path(second_workspace.auxiliary_data_dir()) / "JusPrin" / "state.json", saved_state);
    ProjectPersistence second(second_workspace, test_config());
    second.attach();

    CHECK(second.document().project_id() == project_id);
    const std::vector<ConversationMessage> messages = second.document().messages(conversation);
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].text == "saved message");
}

TEST_CASE("managed import moves semantic state and attachments into the local store", "[persistence][managed]")
{
    const std::string root = unique_temp_dir("managed-projects");
    ProjectStateDocument legacy;
    legacy.initialize_identity("p-imported", "l-imported", kT);
    const std::string conversation = legacy.active_conversation_id();
    legacy.append_message(conversation, user_message(legacy.allocate_message_id(), "old conversation", "c-1"), kT);
    legacy.set_draft("unfinished draft");
    const std::string saved = legacy.dump();

    Workspace::FakeWorkspace workspace(small_snapshot());
    const fs::path auxiliary = fs::path(workspace.auxiliary_data_dir()) / "JusPrin";
    write_text(auxiliary / "state.json", saved);
    write_text(auxiliary / "attachments" / "a-1" / "note.txt", "attached");
    ProjectPersistence::Config config = test_config();
    config.managed_root = root;
    ProjectPersistence imported(workspace, config);
    imported.attach();
    CHECK(imported.document().project_id() == "p-imported");
    CHECK(imported.document().messages(conversation).size() == 1);
    CHECK(imported.draft() == "unfinished draft");
    CHECK(read_text(fs::path(imported.attachments_dir()) / "a-1" / "note.txt") == "attached");
    CHECK_FALSE(fs::exists(fs::path(root) / "p-imported" / "state.json"));
    CHECK(read_text(auxiliary / "state.json") == saved);

    // Importing the old archive again creates a distinct managed draft.
    write_text(fs::path(root) / "p-imported" / "HEAD", "committed-version\n");
    Workspace::FakeWorkspace copy_workspace(small_snapshot());
    write_text(fs::path(copy_workspace.auxiliary_data_dir()) / "JusPrin" / "state.json", saved);
    ProjectPersistence copied(copy_workspace, config);
    copied.attach();
    CHECK(copied.document().project_id() != "p-imported");
    CHECK(copied.document().lineage_id() == "l-imported");
    CHECK(copied.document().messages(conversation).size() == 1);
}

TEST_CASE("a corrupt state file is preserved for inspection and replaced", "[persistence][recovery]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    write_text(fs::path(workspace.auxiliary_data_dir()) / "JusPrin" / "state.json", "{{{ not json");
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();

    CHECK(persistence.document().has_identity());
    CHECK(fs::is_regular_file(persistence.state_file_path()));
    CHECK(fs::is_regular_file(persistence.state_file_path() + ".corrupt"));
}

TEST_CASE("an in-place reset starts a new project boundary", "[persistence][lifecycle]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
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
    ProjectPersistence persistence(workspace, test_config());
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
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();
    const std::string old_identity = persistence.document().project_id();

    workspace.replace_project(small_snapshot());
    CHECK(persistence.document().project_id() != old_identity);
    CHECK(persistence.document().conversations().size() == 1);
}

TEST_CASE("a clean-sharing copy carries no JusPrin state", "[persistence][clean-share]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
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

TEST_CASE("a note keeps the colour that leads it across a round-trip", "[project-state]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    ConversationMessage note;
    note.id     = document.allocate_message_id();
    note.role   = MessageRole::Note;
    note.state  = MessageState::Complete;
    note.text   = "Filament is now Prusament PETG.";
    note.swatch = "#E67E22";
    document.append_message(document.active_conversation_id(), note, kT);
    ConversationMessage plain = note;
    plain.id     = document.allocate_message_id();
    plain.swatch = {};
    document.append_message(document.active_conversation_id(), plain, kT);

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    const auto messages = reloaded.messages(reloaded.active_conversation_id());
    REQUIRE(messages.size() == 2);
    CHECK(messages[0].swatch == "#E67E22");
    CHECK(messages[1].swatch.empty());
    // An absent colour is absent in the file too, not an empty string.
    const std::string dumped = document.dump();
    CHECK(dumped.find("swatch") == dumped.rfind("swatch"));
}

TEST_CASE("attachment blobs are written under the project and cleaned up", "[persistence][attachments]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();

    REQUIRE(persistence.write_attachment_blob("attachments/a-1/notes.txt", "hello"));
    const fs::path blob = fs::path(persistence.jusprin_data_dir()) / "attachments" / "a-1" / "notes.txt";
    REQUIRE(fs::is_regular_file(blob));
    CHECK(read_text(blob) == "hello");

    persistence.remove_attachment_dir("attachments/a-1");
    CHECK_FALSE(fs::exists(blob));
}

TEST_CASE("the change log records each edit after the conversation item it follows", "[persistence][changes]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    std::vector<ChangeEntry> pushed;
    persistence.set_change_listener([&pushed](const ChangeEntry& change) { pushed.push_back(change); });
    persistence.attach();
    ProjectStateDocument& document     = persistence.document();
    const std::string     conversation = document.active_conversation_id();
    const auto            cube         = workspace.snapshot().plates[0].objects[0].id;

    // Before any conversation item.
    REQUIRE(workspace.rename_object(cube, "first").succeeded());
    // After a message.
    const std::string message = document.allocate_message_id();
    document.append_message(conversation, user_message(message, "hello", "c-1"), kT);
    REQUIRE(workspace.rename_object(cube, "second").succeeded());
    // After the tool activity that message submitted, by the Agent.
    ToolActivity activity;
    activity.action_id      = document.allocate_action_id();
    activity.correlation_id = message;
    activity.state          = ToolState::Running;
    document.upsert_activity(activity, kT);
    {
        const Workspace::IWorkspace::AgentEdit agent(workspace);
        REQUIRE(workspace.duplicate_object(cube).succeeded());
    }
    // Selection and slicing are not edits.
    REQUIRE(workspace.select_object(cube).succeeded());
    workspace.set_plate_sliced(workspace.snapshot().plates[0].id, true);

    const std::vector<ChangeEntry> changes = document.changes();
    REQUIRE(changes.size() == 3);
    CHECK(changes[0].kind == "step");
    CHECK(changes[0].label == "Rename Object");
    CHECK(changes[0].actor == "person");
    CHECK(changes[0].after_id.empty());
    CHECK(changes[1].after_id == message);
    CHECK(changes[2].after_id == activity.action_id);
    CHECK(changes[2].actor == "agent");
    for (const ChangeEntry& change : changes)
        CHECK(change.conversation_id == conversation);
    CHECK(changes[0].seq < changes[1].seq);
    CHECK(changes[1].seq < changes[2].seq);
    CHECK(pushed.size() == 3);

    SECTION("the log survives a reload") {
        ProjectStateDocument reloaded;
        REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
        REQUIRE(reloaded.changes().size() == 3);
        CHECK(reloaded.changes()[2].actor == "agent");
        CHECK(reloaded.changes()[2].after_id == activity.action_id);
    }
}

TEST_CASE("only edits the in-app agent did not make put its plan in doubt", "[persistence][plan]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();
    ProjectStateDocument& document = persistence.document();
    const auto cube = workspace.snapshot().plates[0].objects[0].id;
    const auto pin = [&document]() {
        PlanRecord plan;
        plan.headline = "Four walls and gyroid infill for strength";
        document.set_plan(plan, kT);
        REQUIRE(document.plan().validity == "current");
    };
    pin();

    SECTION("the in-app agent carrying out its plan leaves it current") {
        {
            const Workspace::IWorkspace::AgentEdit agent(workspace);
            REQUIRE(workspace.duplicate_object(cube).succeeded());
            REQUIRE(workspace.rename_object(cube, "bracket").succeeded());
        }
        CHECK(document.changes().size() == 2);
        CHECK(document.plan().validity == "current");
        CHECK(document.plan().invalidated_by.empty());
    }

    SECTION("a person's edit marks it") {
        REQUIRE(workspace.rename_object(cube, "bracket").succeeded());
        CHECK(document.plan().validity == "needs_reassessment");
        CHECK(document.plan().invalidated_by == "Rename Object");
    }

    SECTION("a person's Undo and Redo mark it, even of the agent's own step") {
        {
            const Workspace::IWorkspace::AgentEdit agent(workspace);
            REQUIRE(workspace.rename_object(cube, "bracket").succeeded());
        }
        REQUIRE(document.plan().validity == "current");
        REQUIRE(workspace.undo().succeeded());
        CHECK(document.plan().validity == "needs_reassessment");
        CHECK(document.changes().back().kind == "undo");
        pin();
        REQUIRE(workspace.redo().succeeded());
        CHECK(document.plan().validity == "needs_reassessment");
        CHECK(document.changes().back().kind == "redo");
    }

    SECTION("another agent's edit through MCP marks it") {
        {
            const Workspace::IWorkspace::AgentEdit external(workspace, Workspace::EditActor::ExternalTool);
            REQUIRE(workspace.rename_object(cube, "bracket").succeeded());
        }
        CHECK(document.changes().back().external);
        CHECK(document.plan().validity == "needs_reassessment");
    }
}

TEST_CASE("an external tool's edit keeps the client's name through a reload", "[persistence][changes][mcp]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();
    const auto cube = workspace.snapshot().plates.front().objects.front().id;
    {
        const Workspace::IWorkspace::AgentEdit external(workspace, Workspace::EditActor::ExternalTool, "Claude Code");
        REQUIRE(workspace.rename_object(cube, "bracket").succeeded());
    }
    // The name belongs to the scope that gave it: the person's next edit has none.
    REQUIRE(workspace.rename_object(cube, "bracket 2").succeeded());
    const auto& changes = persistence.document().changes();
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].external);
    CHECK(changes[0].client == "Claude Code");
    CHECK(changes[1].actor == "person");
    CHECK(changes[1].client.empty());

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(persistence.document().dump()) == ProjectStateDocument::LoadResult::Loaded);
    REQUIRE(reloaded.changes().size() == 2);
    CHECK(reloaded.changes()[0].client == "Claude Code");
    CHECK(reloaded.changes()[1].client.empty());
    // Only a named entry stores the field, so entries from older builds and
    // unnamed clients read as they always did.
    const auto stored = nlohmann::json::parse(persistence.document().dump())["changes"];
    CHECK(stored[0]["client"] == "Claude Code");
    CHECK_FALSE(stored[1].contains("client"));
}

TEST_CASE("restoring a model version marks the active chat's plan", "[persistence][plan][managed]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence::Config config = test_config();
    config.managed_root = unique_temp_dir("managed");
    ProjectPersistence persistence(workspace, config);
    persistence.attach();
    PlanRecord plan;
    plan.headline = "Four walls and gyroid infill for strength";
    persistence.document().set_plan(plan, kT);
    persistence.record_managed_restore(persistence.document().dump(), "v-2", "v-1");
    CHECK(persistence.document().plan().validity == "needs_reassessment");
    CHECK(persistence.document().plan().invalidated_by == "An earlier model version was restored");
    CHECK(persistence.document().changes().back().kind == "restore");
}

TEST_CASE("settings changes and preset switches are logged", "[persistence][changes]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();

    workspace.set_setting_for_testing("wall_loops", "5");
    workspace.set_process_preset_for_testing("Strong");

    const std::vector<ChangeEntry> changes = persistence.document().changes();
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].kind == "setting");
    CHECK_FALSE(changes[0].label.empty());
    CHECK(changes[0].to == "5");
    CHECK(changes[0].from != "5");
    CHECK(changes[0].preset == "Fixture process");
    CHECK(changes[0].key == "wall_loops");
    CHECK(changes[1].kind == "preset");
    CHECK(changes[1].label == "Strong");
    // Only a setting carries values in the saved entry.
    const json saved = json::parse(persistence.document().dump())["changes"];
    CHECK(saved[0].contains("from"));
    CHECK(saved[0]["key"] == "wall_loops");
    CHECK_FALSE(saved[1].contains("from"));
    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(persistence.document().dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(reloaded.changes()[0].key == "wall_loops");
}

TEST_CASE("a chat's print intent from an earlier build still loads, and is not copied", "[project-state][intent]")
{
    // setup_record kept model-chosen requirement fields in printIntent until
    // 2026-10-25. A document that holds them loads, keeps them untouched as
    // it keeps any field it does not read, and a new chat does not copy them.
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    json raw = json::parse(document.dump());
    raw["conversations"][0]["setupIntent"] = "Load-bearing bracket";
    raw["conversations"][0]["printIntent"] = json::array({json{{"field", "load"}, {"value", "5 kg"},
                                                               {"provenance", "user_confirmed"}, {"seq", 3},
                                                               {"updatedAt", kT}}});
    ProjectStateDocument loaded;
    REQUIRE(loaded.load(raw.dump()) == ProjectStateDocument::LoadResult::Loaded);
    CHECK(loaded.setup_intent(loaded.active_conversation_id()) == "Load-bearing bracket");

    const std::string second = loaded.create_conversation({}, kT);
    CHECK(loaded.setup_intent(second) == "Load-bearing bracket");
    const json round = json::parse(loaded.dump());
    CHECK(round["conversations"][0]["printIntent"] == raw["conversations"][0]["printIntent"]);
    CHECK_FALSE(round["conversations"][1].contains("printIntent"));
}

TEST_CASE("the plan is replaced whole and survives a round trip", "[project-state][plan]")
{
    ProjectStateDocument document;
    document.initialize_identity("p-1", "l-1", kT);
    CHECK(document.plan().headline.empty());
    CHECK(document.plan().seq == 0);

    PlanRecord plan;
    plan.headline    = "Protect the visible face, accept a longer print";
    plan.decisions   = {{"orientation", "Front face down, seam at the rear.", "high", "Flat on the bed: faster, visible seam."}};
    plan.assumptions = {"PLA on a smooth plate"};
    plan.risks       = {"Supports may mark the underside"};
    const auto stored = document.set_plan(plan, kT);
    CHECK(stored.seq > 0);
    CHECK(stored.updated_at == kT);

    ProjectStateDocument reloaded;
    REQUIRE(reloaded.load(document.dump()) == ProjectStateDocument::LoadResult::Loaded);
    const PlanRecord read = reloaded.plan();
    CHECK(read.headline == plan.headline);
    REQUIRE(read.decisions.size() == 1);
    CHECK(read.decisions[0].topic == "orientation");
    CHECK(read.decisions[0].alternative == "Flat on the bed: faster, visible seam.");
    CHECK(read.assumptions == plan.assumptions);
    CHECK(read.risks == plan.risks);

    // A second statement replaces the first; it is one plan, not a log.
    PlanRecord replacement;
    replacement.headline = "Strength first";
    const auto after = reloaded.set_plan(replacement, kT);
    CHECK(after.seq > stored.seq);
    CHECK(reloaded.plan().decisions.empty());
    CHECK(reloaded.plan().assumptions.empty());
}

TEST_CASE("intent and plan writes need an active chat to hold them", "[project-state][intent][plan]")
{
    ProjectStateDocument document;
    CHECK(document.plan().headline.empty());
    CHECK_THROWS_AS(document.set_plan(PlanRecord{}, kT), std::logic_error);
    CHECK_THROWS_AS(document.set_purpose_setup(json{{"printerPreset", "X1C"}}), std::logic_error);
}

TEST_CASE("every ledger subscriber hears a ledger change, and releasing one leaves the others", "[persistence][ledger]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();

    int first = 0, second = 0;
    auto first_subscription  = persistence.subscribe_ledger([&first]() { ++first; });
    auto second_subscription = persistence.subscribe_ledger([&second]() { ++second; });

    persistence.notify_ledger_changed();
    CHECK(first == 1);
    CHECK(second == 1);

    first_subscription.reset();
    persistence.notify_ledger_changed();
    CHECK(first == 1);
    CHECK(second == 2);
}

TEST_CASE("a ledger subscriber may release itself or another during dispatch", "[persistence][ledger]")
{
    Workspace::FakeWorkspace workspace(small_snapshot());
    ProjectPersistence persistence(workspace, test_config());
    persistence.attach();

    int self_calls = 0, other_calls = 0;
    Workspace::WorkspaceSubscription self, other;
    other = persistence.subscribe_ledger([&other_calls]() { ++other_calls; });
    self  = persistence.subscribe_ledger([&]() {
        ++self_calls;
        other.reset();
        self.reset();
    });

    persistence.notify_ledger_changed();
    persistence.notify_ledger_changed();
    CHECK(self_calls == 1);
    // The other observer was registered first, so it ran before being released.
    CHECK(other_calls == 1);
}

TEST_CASE("a ledger subscription outliving its persistence does nothing", "[persistence][ledger]")
{
    Workspace::WorkspaceSubscription survivor;
    {
        Workspace::FakeWorkspace workspace(small_snapshot());
        ProjectPersistence persistence(workspace, test_config());
        survivor = persistence.subscribe_ledger([]() {});
    }
    REQUIRE_NOTHROW(survivor.reset());
}
