#pragma once

// The product state that has no Orca owner: what the user wants out of this
// print, and the plan the agent means to follow. The authority path is the
// same as for any other mutation -- registry, coordinator, approval policy --
// and only the last step differs: the owner is a JusPrin store rather than an
// Orca one.
//
// Writes here do not advance the workspace revision. Nothing about the model,
// the presets, or the plate changes, and `settings_apply_patch` requires its
// caller's revision to match exactly: an agent that recorded its plan between
// previewing a settings change and applying it would otherwise cancel its own
// call. Readers see a write on their next read.
//
// GUI-free: the implementation over the project document lives in AgentHost,
// and the tests supply their own.

#include "ProjectStateDocument.hpp"
#include "slic3r/GUI/JusPrin/Workspace/PrinterFactsStore.hpp"

#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Agent {

class IProductState
{
public:
    virtual ~IProductState() = default;

    virtual std::vector<IntentField> print_intent() const = 0;
    // Upserts by field name and returns the record as stored.
    virtual std::vector<IntentField> set_print_intent(const std::vector<IntentField>& fields) = 0;

    virtual PlanRecord plan() const = 0;
    // Replaces the plan whole and returns it as stored.
    virtual PlanRecord set_plan(PlanRecord record) = 0;

    // Facts the person stated about a physical printer, app-level rather than
    // project-level, still unexpired. `printer` is the identity they belong to.
    // Only call the two below when this is true.
    virtual bool has_printer_facts() const = 0;
    virtual std::vector<Workspace::PrinterFact> printer_facts(const std::string& printer) const = 0;
    virtual std::vector<Workspace::PrinterFact> confirm_printer_facts(
        const std::string& printer, const std::vector<Workspace::FactConfirmation>& confirmations) = 0;
};

} // namespace Slic3r::GUI::JusPrin::Agent
