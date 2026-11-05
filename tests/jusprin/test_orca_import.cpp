#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Home/OrcaImport.hpp"

#include <chrono>
#include <fstream>
#include <set>
#include <thread>

#include <nlohmann/json.hpp>

using namespace Slic3r::GUI::JusPrin::Home;
namespace fs = std::filesystem;
using Kind    = OrcaImportItem::Kind;
using Outcome = OrcaImportItem::Outcome;
using Reason  = OrcaImportItem::Reason;

namespace {

// A folder of its own under the system's temp folder, removed afterwards.
struct TempFolder
{
    fs::path path;
    TempFolder()
    {
        path = fs::temp_directory_path() /
               ("jusprin-orca-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempFolder() { fs::remove_all(path); }
};

std::string write(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << text;
    return file.u8string();
}

std::string preset(const fs::path& file, const nlohmann::json& json) { return write(file, json.dump()); }

nlohmann::json printer(const std::string& name, const std::string& base = "Bambu Lab X1 Carbon 0.4 nozzle")
{
    return {{"name", name}, {"version", "2.3.0.4"}, {"printer_settings_id", name}, {"inherits", base}};
}

std::string text_of(const std::string& file)
{
    std::ifstream stream(file);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("an orca import keeps each file's reason and renames what JusPrin already has", "[home][onboarding][orca_import]")
{
    TempFolder orca, staging;
    const fs::path account = orca.path / "user" / "default";
    const std::string mine     = preset(account / "machine" / "My X1C.json", printer("My X1C"));
    const std::string taken    = preset(account / "machine" / "Shop X1C.json", printer("Shop X1C"));
    const std::string orphan   = preset(account / "machine" / "Old.json", printer("Old", "Some Retired Printer"));
    const std::string broken   = write(account / "filament" / "Broken PLA.json", "{ not json");
    const std::string no_ver   = preset(account / "filament" / "Unversioned.json",
                                        {{"name", "Unversioned"}, {"filament_settings_id", "Unversioned"}});
    const std::string not_one  = preset(account / "process" / "Notes.json", {{"name", "Notes"}, {"version", "1"}});
    const std::string original = text_of(taken);

    std::set<std::string> base_lookups;
    OrcaImportCatalog catalog;
    catalog.has      = [](Kind kind, const std::string& name) { return kind == Kind::Printer && name == "Shop X1C"; };
    catalog.has_base = [&](Kind, const std::string& base) {
        base_lookups.insert(base);
        return base == "Bambu Lab X1 Carbon 0.4 nozzle";
    };

    std::vector<OrcaImportItem> items = plan_orca_import({mine, taken, orphan, broken, no_ver, not_one}, catalog, staging.path);
    REQUIRE(items.size() == 6);

    CHECK(items[0].name == "My X1C");
    CHECK(items[0].outcome == Outcome::Pending);
    CHECK(items[0].file == mine);
    CHECK(items[0].renamed_to.empty());

    // Taken: imported from a renamed copy in the staging folder.
    CHECK(items[1].outcome == Outcome::Pending);
    CHECK(items[1].renamed_to == "Shop X1C (Orca copy)");
    CHECK(items[1].source == taken);
    REQUIRE(fs::path(items[1].file).parent_path() == staging.path);
    const nlohmann::json copy = nlohmann::json::parse(text_of(items[1].file));
    CHECK(copy["name"] == "Shop X1C (Orca copy)");
    CHECK(copy["printer_settings_id"] == "Shop X1C (Orca copy)");
    CHECK(copy["inherits"] == "Bambu Lab X1 Carbon 0.4 nozzle");
    // OrcaSlicer's own file is only read.
    CHECK(text_of(taken) == original);

    CHECK(items[2].outcome == Outcome::NotImported);
    CHECK(items[2].reason == Reason::MissingBase);
    CHECK(items[2].base == "Some Retired Printer");

    CHECK(items[3].kind == Kind::Filament);
    CHECK(items[3].name == "Broken PLA");
    CHECK(items[3].reason == Reason::Unreadable);
    CHECK(items[4].reason == Reason::NoVersion);
    CHECK(items[5].kind == Kind::Process);
    CHECK(items[5].reason == Reason::NotAPreset);
    CHECK(base_lookups.count("Some Retired Printer") == 1);

    SECTION("import_presets' answer settles the rest")
    {
        // It imported the renamed copy but not "My X1C".
        settle_orca_import(items, {items[1].file});
        CHECK(items[0].outcome == Outcome::NotImported);
        CHECK(items[0].reason == Reason::NotLoaded);
        CHECK(items[1].outcome == Outcome::Imported);
        // A reason found before the import stays.
        CHECK(items[2].reason == Reason::MissingBase);
    }
}

TEST_CASE("an orca import numbers a second copy and finds bases among its own files", "[home][onboarding][orca_import]")
{
    TempFolder orca, staging;
    const fs::path filaments = orca.path / "user" / "default" / "filament";
    const auto filament = [&](const std::string& name, const std::string& base) {
        return preset(filaments / (name + ".json"),
                      {{"name", name}, {"version", "1.0"}, {"filament_settings_id", name}, {"inherits", base}});
    };
    const std::string base  = filament("My Base PLA", "Generic PLA @System");
    const std::string child = filament("My PLA", "My Base PLA");

    OrcaImportCatalog catalog;
    // JusPrin has "My PLA" and already a first copy of it.
    catalog.has      = [](Kind, const std::string& name) { return name == "My PLA" || name == "My PLA (Orca copy)"; };
    catalog.has_base = [](Kind, const std::string& name) { return name == "Generic PLA @System"; };

    const std::vector<OrcaImportItem> items = plan_orca_import({base, child}, catalog, staging.path);
    CHECK(items[0].outcome == Outcome::Pending);
    CHECK(items[1].outcome == Outcome::Pending);
    CHECK(items[1].renamed_to == "My PLA (Orca copy 2)");
    CHECK(nlohmann::json::parse(text_of(items[1].file))["filament_settings_id"] == "My PLA (Orca copy 2)");
}

TEST_CASE("an orca account folder is found from what the person picks", "[home][onboarding][orca_import]")
{
    TempFolder orca;
    const fs::path root = orca.path / "OrcaSlicer";
    preset(root / "user" / "default" / "machine" / "Old.json", printer("Old"));
    // Saved later, under a Bambu account's id.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    preset(root / "user" / "1234" / "machine" / "New.json", printer("New"));
    const fs::path account = root / "user" / "1234";

    CHECK(orca_account_folder(root) == account);
    CHECK(orca_account_folder_for_pick(root) == account);
    CHECK(orca_account_folder_for_pick(root / "user") == account);
    CHECK(orca_account_folder_for_pick(account) == account);
    CHECK(orca_account_folder_for_pick(account / "machine") == account);
    CHECK_FALSE(orca_account_folder_for_pick(orca.path).has_value());

    const OrcaPresetFiles presets = scan_orca_presets(account);
    CHECK(presets.printers == std::vector<std::string>{"New"});
    CHECK(presets.files.size() == 1);

    SECTION("OrcaSlicer.conf names the account, after its JSON a checksum line")
    {
        write(root / "OrcaSlicer.conf", R"({"app": {"preset_folder": "default"}})" "\n# MD5 checksum 0123\n");
        CHECK(orca_account_folder(root) == root / "user" / "default");
    }
    SECTION("an unreadable OrcaSlicer.conf names nothing")
    {
        write(root / "OrcaSlicer.conf", "{ broken");
        CHECK_FALSE(orca_account_folder(root).has_value());
    }
}
