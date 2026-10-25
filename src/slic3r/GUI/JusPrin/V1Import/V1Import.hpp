#pragma once

// Bringing a JusPrin v1 setup into JusPrin 2 on first launch.
//
// JusPrin 2 has its own data folder ("JusPrin2"), so without this a person
// coming from v1 would start with no printers, presets or preferences. The
// first time JusPrin 2 starts with no settings file of its own, and v1's
// folder sits beside its own, the parts of v1's folder that belong to the
// person are copied: the user presets (printers, filaments, processes, and
// with the printers their network connections), Bambu's network settings,
// and v1's settings file, whose JusPrin account sign-in becomes JusPrin 2's.
// v1's folder is only read. What was copied is written down for the
// welcome-back screen, which shows it once.
//
// Runs before OrcaSlicer reads its settings or presets, from GUI_App's
// start-up, and so is plain file work: no GUI, no AppConfig. Paths are
// UTF-8 strings where they cross into OrcaSlicer, std::filesystem inside.

#include <nlohmann/json_fwd.hpp>

#include <filesystem>

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::V1Import {

// What the import copied, as the welcome-back screen lists it.
struct Report
{
    std::string              source;      // v1's folder
    std::string              imported_at; // UTC, ISO 8601
    std::vector<std::string> printers;    // user printer presets, by name
    std::vector<std::string> presets;     // user filament and process presets
    std::vector<std::string> connections; // printers with a network connection
    bool                     preferences{false};
    bool                     signed_in{false}; // v1's sign-in came along
    // Files that could not be copied, by their path inside v1's folder.
    std::vector<std::string> failed;
    // Still to be shown on the welcome-back screen.
    bool                     pending{true};
};

nlohmann::json to_json(const Report& report);
Report         report_from_json(const nlohmann::json& json);

// Where v1 keeps its folder: JUSPRIN_V1_DATADIR when set, otherwise "JusPrin"
// beside JusPrin 2's own folder, as the platform places both (only when
// JusPrin 2 runs on its default folder; a --datadir run never reaches for
// the person's real v1 setup). Empty when there is none to look at.
std::filesystem::path v1_folder(const std::filesystem::path& data_dir, bool datadir_redefined);

// Copies v1's setup into `data_dir` when JusPrin 2 has no settings file
// there yet and `v1` holds one, and writes the report. Nothing otherwise.
// `app_key` names the settings files ("JusPrin2" for JusPrin2.conf).
std::optional<Report> import_if_first_launch(const std::filesystem::path& data_dir, const std::filesystem::path& v1,
                                             const std::string& app_key);

// The start-up call: v1_folder() and import_if_first_launch() for this run.
void import_on_first_launch(const std::string& data_dir, bool datadir_redefined);

// The report a previous import left, if any.
std::optional<Report> read_report(const std::filesystem::path& data_dir);
// Records that the welcome-back screen has been shown.
void mark_shown(const std::filesystem::path& data_dir);

} // namespace Slic3r::GUI::JusPrin::V1Import
