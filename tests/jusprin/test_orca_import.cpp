#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Home/OrcaImport.hpp"

#include <chrono>
#include <fstream>
#include <map>
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
    // JusPrin's "Shop X1C" is another printer, based on another profile.
    catalog.inherits = [](Kind, const std::string&) { return std::string("Bambu Lab P1S 0.4 nozzle"); };
    catalog.base = [&](Kind, const std::string& base) {
        base_lookups.insert(base);
        return base == "Bambu Lab X1 Carbon 0.4 nozzle" ? base : std::string();
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
    catalog.inherits = [](Kind, const std::string&) { return std::string("Generic PLA @System"); };
    catalog.base     = [](Kind, const std::string& name) { return name == "Generic PLA @System" ? name : std::string(); };

    const std::vector<OrcaImportItem> items = plan_orca_import({base, child}, catalog, staging.path);
    CHECK(items[0].outcome == Outcome::Pending);
    CHECK(items[1].outcome == Outcome::Pending);
    CHECK(items[1].renamed_to == "My PLA (Orca copy 2)");
    CHECK(nlohmann::json::parse(text_of(items[1].file))["filament_settings_id"] == "My PLA (Orca copy 2)");
}

TEST_CASE("an orca import leaves alone what JusPrin already has", "[home][orca_import]")
{
    TempFolder orca, staging;
    const fs::path account = orca.path / "user" / "default";
    const auto filament = [&](const std::string& name, const std::string& base) {
        return preset(account / "filament" / (name + ".json"),
                      {{"name", name}, {"version", "1.0"}, {"filament_settings_id", name}, {"inherits", base}});
    };
    // Imported before under its own name.
    const std::string shop   = preset(account / "machine" / "Shop X1C.json", printer("Shop X1C"));
    // Imported before as a renamed copy: JusPrin's own "My PLA" is another filament.
    const std::string mine   = filament("My PLA", "Generic PLA");
    // JusPrin's "Silk PLA" is another filament, and no copy was imported yet.
    const std::string silk   = filament("Silk PLA", "Generic PLA");
    const std::string broken = write(account / "filament" / "Broken.json", "{ not json");
    // Imported before, and saved inheriting the library preset OrcaSlicer
    // loads in place of the vendor's missing one.
    const std::string abs    = filament("Elegoo Generic ABS - Copy", "Elegoo Generic ABS");

    // JusPrin's presets by kind and name, with what each inherits.
    const std::map<std::pair<Kind, std::string>, std::string> jusprin{
        {{Kind::Printer, "Shop X1C"}, "Bambu Lab X1 Carbon 0.4 nozzle"},
        {{Kind::Filament, "My PLA"}, "Generic PLA @System"},
        {{Kind::Filament, "My PLA (Orca copy)"}, "Generic PLA"},
        {{Kind::Filament, "Silk PLA"}, "Generic PLA @System"},
        {{Kind::Filament, "Elegoo Generic ABS - Copy"}, "Generic ABS @System"},
        // A printer of the same name is not the filament.
        {{Kind::Printer, "Broken"}, ""},
    };
    OrcaImportCatalog catalog;
    catalog.has      = [&](Kind kind, const std::string& name) { return jusprin.count({kind, name}) != 0; };
    catalog.inherits = [&](Kind kind, const std::string& name) { return jusprin.at({kind, name}); };
    catalog.base     = [](Kind, const std::string& base) {
        return base == "Elegoo Generic ABS" ? std::string("Generic ABS @System") : base;
    };

    CHECK(orca_preset_in_jusprin(shop, catalog));
    CHECK(orca_preset_in_jusprin(abs, catalog));
    CHECK(orca_preset_in_jusprin(mine, catalog));
    CHECK_FALSE(orca_preset_in_jusprin(silk, catalog));
    CHECK_FALSE(orca_preset_in_jusprin(broken, catalog));

    std::vector<OrcaImportItem> items = plan_orca_import({shop, mine, silk}, catalog, staging.path);
    REQUIRE(items.size() == 3);
    CHECK(items[0].outcome == Outcome::AlreadyThere);
    CHECK(items[1].outcome == Outcome::AlreadyThere);
    CHECK(items[1].renamed_to.empty());
    CHECK(items[2].outcome == Outcome::Pending);
    CHECK(items[2].renamed_to == "Silk PLA (Orca copy)");
    // Only the one being imported has a renamed copy.
    CHECK(std::distance(fs::directory_iterator(staging.path), fs::directory_iterator()) == 1);

    SECTION("settling leaves them as they are")
    {
        settle_orca_import(items, {items[2].file});
        CHECK(items[0].outcome == Outcome::AlreadyThere);
        CHECK(items[2].outcome == Outcome::Imported);
    }
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

TEST_CASE("an orca folder with only ticked printers offers JusPrin's own profiles for them", "[home][onboarding][orca_import]")
{
    using Slic3r::GUI::JusPrin::PrinterSetup::PrinterCandidate;
    TempFolder orca;
    const fs::path root = orca.path / "OrcaSlicer";
    // As OrcaSlicer writes it: two nozzles of one model in one entry, and a
    // selected printer that is not ticked.
    write(root / "OrcaSlicer.conf", R"({
        "models": [
            {"vendor": "BBL", "model": "Bambu Lab A1 mini", "nozzle_diameter": "0.4"},
            {"vendor": "Creality", "model": "Creality K1C", "nozzle_diameter": "0.4;0.6"},
            {"vendor": "Nobody", "model": "Unknown Printer", "nozzle_diameter": "0.4"}
        ],
        "presets": {"machine": "Bambu Lab P1S 0.4 nozzle"}
    })" "\n# MD5 checksum 0123\n");

    const auto candidate = [](std::string vendor, std::string model, std::string variant, std::string preset) {
        PrinterCandidate profile;
        profile.vendor_id   = std::move(vendor);
        profile.model_id    = std::move(model);
        profile.variant     = std::move(variant);
        profile.preset_name = std::move(preset);
        return profile;
    };
    const std::vector<PrinterCandidate> profiles{
        candidate("BBL", "Bambu Lab A1 mini", "0.4", "Bambu Lab A1 mini 0.4 nozzle"),
        candidate("BBL", "Bambu Lab P1S", "0.4", "Bambu Lab P1S 0.4 nozzle"),
        candidate("Creality", "Creality K1C", "0.4", "Creality K1C 0.4 nozzle"),
        candidate("Creality", "Creality K1C", "0.6", "Creality K1C 0.6 nozzle"),
    };
    const auto names = [](const std::vector<OrcaImportItem>& items) {
        std::vector<std::string> names;
        for (const OrcaImportItem& item : items)
            names.push_back(item.name);
        return names;
    };

    CHECK(orca_selected_printer(root) == "Bambu Lab P1S 0.4 nozzle");
    REQUIRE(orca_ticked_printers(root).size() == 4);
    CHECK(orca_ticked_printers(root)[2].nozzle == "0.6");

    SECTION("no saved presets: every ticked printer JusPrin has, then the selected one")
    {
        const std::vector<OrcaImportItem> items =
            orca_profile_printers(root, std::nullopt, [&]() -> const auto& { return profiles; });
        CHECK(names(items) == std::vector<std::string>{"Bambu Lab A1 mini 0.4 nozzle", "Creality K1C 0.4 nozzle",
                                                       "Creality K1C 0.6 nozzle", "Bambu Lab P1S 0.4 nozzle"});
        REQUIRE(items.size() == 4);
        CHECK(items[2].vendor == "Creality");
        CHECK(items[2].model == "Creality K1C");
        CHECK(items[2].nozzle == "0.6");
        CHECK(items[2].kind == Kind::Printer);
        CHECK(items[2].outcome == Outcome::Pending);
    }
    SECTION("a saved preset based on a ticked printer brings it instead")
    {
        const fs::path account = root / "user" / "default";
        preset(account / "machine" / "My A1 mini.json", printer("My A1 mini", "Bambu Lab A1 mini 0.4 nozzle"));
        const std::vector<OrcaImportItem> items =
            orca_profile_printers(root, account, [&]() -> const auto& { return profiles; });
        CHECK(names(items) == std::vector<std::string>{"Creality K1C 0.4 nozzle", "Creality K1C 0.6 nozzle", "Bambu Lab P1S 0.4 nozzle"});
    }
    SECTION("a selected printer that is also ticked is listed once")
    {
        write(root / "OrcaSlicer.conf",
              R"({"models": [{"vendor": "BBL", "model": "Bambu Lab A1 mini", "nozzle_diameter": "0.4"}],
                  "presets": {"machine": "Bambu Lab A1 mini 0.4 nozzle"}})");
        CHECK(names(orca_profile_printers(root, std::nullopt, [&]() -> const auto& { return profiles; })) ==
              std::vector<std::string>{"Bambu Lab A1 mini 0.4 nozzle"});
    }
    SECTION("a folder that names no printer never reads the profiles")
    {
        write(root / "OrcaSlicer.conf", R"({"app": {"preset_folder": "default"}})");
        bool read = false;
        CHECK(orca_profile_printers(root, std::nullopt, [&]() -> const auto& {
                  read = true;
                  return profiles;
              }).empty());
        CHECK_FALSE(read);
    }
}

TEST_CASE("an orca import names the base profile of each saved printer once", "[home][onboarding][orca_import]")
{
    TempFolder orca;
    const fs::path account = orca.path / "user" / "default";
    const std::string ender  = preset(account / "machine" / "My Ender.json", printer("My Ender", "Creality Ender-3 V2 Neo 0.4 nozzle"));
    const std::string spare  = preset(account / "machine" / "Spare Ender.json", printer("Spare Ender", "Creality Ender-3 V2 Neo 0.4 nozzle"));
    const std::string qidi   = preset(account / "machine" / "My Qidi.json", printer("My Qidi", "Qidi X-Plus 4 0.4 nozzle"));
    const std::string orphan = preset(account / "machine" / "Orphan.json", {{"name", "Orphan"}, {"version", "2.3.0.4"}});
    // A filament names a filament base, which no printer install brings.
    const std::string pla = preset(account / "filament" / "My PLA.json",
                                   {{"name", "My PLA"}, {"version", "2.3.0.4"}, {"inherits", "Generic PLA @System"}});

    CHECK(orca_printer_bases({ender, pla, spare, qidi, orphan}) ==
          std::vector<std::string>{"Creality Ender-3 V2 Neo 0.4 nozzle", "Qidi X-Plus 4 0.4 nozzle"});
    CHECK(orca_printer_bases({}).empty());
}

TEST_CASE("an orca import brings only the printers the person left chosen, and every filament and process",
          "[home][onboarding][orca_import]")
{
    TempFolder orca;
    const fs::path account = orca.path / "user" / "default";
    preset(account / "machine" / "My X1C.json", printer("My X1C"));
    preset(account / "machine" / "Old Voron.json", printer("Old Voron", "Voron 0.1"));
    const std::string pla  = write(account / "filament" / "My PLA.json", "{}");
    const std::string fine = write(account / "process" / "Fine.json", "{}");
    const OrcaPresetFiles presets = scan_orca_presets(account);
    REQUIRE(presets.printers == std::vector<std::string>{"My X1C", "Old Voron"});

    OrcaImportItem a1;
    a1.name   = "Bambu Lab A1 mini 0.4 nozzle";
    a1.vendor = "BBL";
    OrcaImportItem p1s = a1;
    p1s.name           = "Bambu Lab P1S 0.4 nozzle";
    const std::vector<OrcaImportItem> profiles{a1, p1s};

    SECTION("every printer chosen")
    {
        const OrcaImportChoice choice =
            orca_import_chosen(presets, profiles, {"My X1C", "Old Voron", "Bambu Lab A1 mini 0.4 nozzle", "Bambu Lab P1S 0.4 nozzle"});
        CHECK(choice.files == presets.files);
        CHECK(choice.profile_printers.size() == 2);
    }
    SECTION("one saved printer and one profile printer left out")
    {
        const OrcaImportChoice choice = orca_import_chosen(presets, profiles, {"My X1C", "Bambu Lab P1S 0.4 nozzle"});
        CHECK(choice.files == std::vector<std::string>{(account / "machine" / "My X1C.json").u8string(), pla, fine});
        REQUIRE(choice.profile_printers.size() == 1);
        CHECK(choice.profile_printers.front().name == "Bambu Lab P1S 0.4 nozzle");
    }
    SECTION("no printer chosen still brings the filaments and processes")
    {
        const OrcaImportChoice choice = orca_import_chosen(presets, profiles, {});
        CHECK(choice.files == std::vector<std::string>{pla, fine});
        CHECK(choice.profile_printers.empty());
    }
}
