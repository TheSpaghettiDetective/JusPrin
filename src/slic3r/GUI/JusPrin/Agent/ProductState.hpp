#pragma once

// The product state that has no Orca owner: what the user wants out of this
// print, and the plan the agent means to follow. The authority path is the
// same as for any other mutation -- registry and coordinator --
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

#include <optional>
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
    // The purpose in the person's words that intent_update records as
    // setupTitle, which belongs to the chat that holds `message_id`: nullopt
    // when no chat holds it, as for an external client's call.
    virtual std::optional<std::string> setup_title(const std::string& message_id) const = 0;
    // False when no chat holds the message or the document refuses the title.
    virtual bool set_setup_title(const std::string& message_id, const std::string& title) = 0;
    // The setup in force when the purpose was last recorded or changed
    // (ProjectStateDocument::purpose_setup).
    virtual nlohmann::json purpose_setup() const = 0;
    virtual void           set_purpose_setup(nlohmann::json setup) = 0;

    virtual PlanRecord plan() const = 0;
    // Replaces the plan whole and returns it as stored.
    virtual PlanRecord set_plan(PlanRecord record) = 0;

    // Region annotations, replaced whole; returns them as stored.
    virtual std::vector<Workspace::RegionRecord> regions() const = 0;
    virtual std::vector<Workspace::RegionRecord> set_regions(std::vector<Workspace::RegionRecord> records) = 0;

    // Writes whatever product state is still pending into the project's
    // auxiliary directory, so a save that happens now carries it.
    virtual void flush_to_project() = 0;

    // Facts the person stated about a physical printer, app-level rather than
    // project-level, still unexpired. `printer` is the identity they belong to.
    // Only call the two below when this is true.
    virtual bool has_printer_facts() const = 0;
    virtual std::vector<Workspace::PrinterFact> printer_facts(const std::string& printer) const = 0;
    virtual std::vector<Workspace::PrinterFact> confirm_printer_facts(
        const std::string& printer, const std::vector<Workspace::FactConfirmation>& confirmations) = 0;
};

} // namespace Slic3r::GUI::JusPrin::Agent
