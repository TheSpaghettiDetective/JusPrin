#pragma once

// Naming rules for a named printer: an Orca user printer profile that stands
// for one machine. Orca has no printer object of its own, so the profile's
// name is the printer's name, and it has to obey the rules Orca applies to any
// profile name.
//
// GUI-free and Orca-free so the rules are testable without a PresetBundle; the
// caller supplies what only the preset collection knows.

#include <functional>
#include <string>

namespace Slic3r::GUI::JusPrin::Printers {

using NamePredicate = std::function<bool(const std::string&)>;

enum class NameProblem
{
    None,
    Empty,
    EdgeSpace,        // leading or trailing space
    IllegalCharacter, // one of kIllegalNameCharacters
    Reserved,         // a name or suffix Orca keeps for itself
    Taken,            // another profile already has this name
};

// SavePresetDialog::Item::update's list. Upstream keeps its rules inline in
// the dialog, so they cannot be called; this is the same set, and the dialog
// is where a rebase looks for changes to it.
constexpr const char* kIllegalNameCharacters = "<>[]:/\\|?*\"";

// `reserved` answers for names Orca refuses (placeholders, preset aliases, the
// "(modified)" suffix); `taken` for names another profile already uses.
// Renaming a printer to its own name is not a problem, so `taken` must not
// report the printer being renamed.
NameProblem check_printer_name(const std::string& name, const NamePredicate& reserved, const NamePredicate& taken);

// `base` when it is free, otherwise "base (2)", "base (3)", ... A new printer
// never overwrites a profile; SavePresetDialog would, after a warning nobody
// sees when the name is chosen for them.
std::string first_free_printer_name(const std::string& base, const NamePredicate& taken);

} // namespace Slic3r::GUI::JusPrin::Printers
