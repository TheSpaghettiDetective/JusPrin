#include "PrinterConversation.hpp"

#include "slic3r/GUI/JusPrin/Support/Base64.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Slic3r::GUI::JusPrin::PrinterSetup {

using nlohmann::json;
using Agent::ToolActivity;
using Agent::ToolError;
using Agent::ToolHandler;
using Agent::ToolExecutionCoordinator;

namespace {

std::string nozzle_text(double nozzle)
{
    if (nozzle <= 0.)
        return {};
    std::ostringstream out;
    out.precision(2);
    out << std::fixed << nozzle;
    std::string text = out.str();
    while (text.size() > 3 && text.back() == '0')
        text.pop_back();
    return text + " mm";
}

// The profile's own spelling of a nozzle size, as the variant is named.
std::string variant_text(double nozzle)
{
    std::ostringstream out;
    out.precision(1);
    out << std::fixed << nozzle;
    return out.str();
}

std::string picture_data_url(const std::string& path)
{
    if (path.empty())
        return {};
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file.good())
        return {};
    std::ostringstream bytes;
    bytes << file.rdbuf();
    const std::string data = bytes.str();
    if (data.empty())
        return {};
    return "data:image/png;base64," + base64_encode(data);
}

json fact_json(const PinnedFact& fact)
{
    json value{{"value", fact.value}, {"provenance", fact.provenance}};
    if (!fact.swatch.empty())
        value["swatch"] = fact.swatch;
    return value;
}

// What a printer says it has loaded, as one line: "AMS lite · PLA Matte + 3".
std::string spool_summary(const std::string& ams, const std::vector<PrinterSpool>& spools)
{
    if (spools.empty())
        return ams.empty() ? std::string() : ams;
    std::string summary = spools.front().name.empty() ? spools.front().material : spools.front().name;
    if (spools.size() > 1)
        summary += " + " + std::to_string(spools.size() - 1);
    return ams.empty() ? summary : ams + " · " + summary;
}

} // namespace

PrinterConversation::PrinterConversation(IPrinterBackend& backend, IConversationHost& host)
    : m_backend(backend), m_host(host)
{}

std::vector<std::string> PrinterConversation::session_tools()
{
    return {"printer_identify", "printer_suggest", "printer_change"};
}

void PrinterConversation::start(ConversationMode mode, const std::string& printer_name)
{
    m_mode         = mode;
    m_printer_name = printer_name;
    m_printer      = {};
    m_proposal     = {};
    m_blocks       = json::array();
    m_chips        = json::array();
    m_chip_hint.clear();
    m_next_block = 1;
    m_catalog.clear();
    m_network.clear();
    m_pending_device_id.clear();
    m_browsing = false;
    m_browse_vendor_id.clear();

    m_printer_fact = m_nozzle = m_plate = m_filament = {};
    // A printer this app has not saved cannot be changed, and the header's
    // menu can name one: a system profile is selected until the person adds
    // a printer of their own. Adding one is what there is to do.
    if (m_mode == ConversationMode::Change) {
        const std::vector<SavedPrinter> saved = m_backend.saved_printers();
        if (std::none_of(saved.begin(), saved.end(),
                         [this](const SavedPrinter& printer) { return printer.name == m_printer_name; })) {
            m_mode = ConversationMode::Add;
            m_printer_name.clear();
        }
    }

    if (m_mode == ConversationMode::Change) {
        read_saved_printer();
        m_placeholder = "e.g. \"I swapped the plate\" or \"is it connected?\"";
    } else {
        // The whole catalogue goes in the agent's prompt (see profile()) and
        // is kept here so a catalogId it answers with can be validated and
        // drawn without asking Orca again.
        m_catalog = m_backend.catalog_models();
        refresh_network();
        m_placeholder = "e.g. \"bambu a1 mini\" or \"not sure, the small one\"";
        // A photo is the fastest way in, and the printers already on the
        // network are the fastest of all. Both belong under the opening,
        // where the person is deciding what to say.
        m_blocks.push_back(json{{"id", "b" + std::to_string(m_next_block++)},
                                {"seq", m_blocks.size() + 1},
                                {"afterMessageId", ""},
                                {"kind", "tip"}});
        if (!m_network.empty()) {
            json found = json::array();
            for (const DiscoveredPrinter& printer : m_network)
                found.push_back(json{{"deviceId", printer.stable_id},
                                     {"name", printer.name},
                                     {"serial", printer.stable_id},
                                     {"online", printer.connected}});
            m_blocks.push_back(json{{"id", "b" + std::to_string(m_next_block++)},
                                    {"seq", m_blocks.size() + 1},
                                    {"afterMessageId", ""},
                                    {"kind", "network"},
                                    {"printers", std::move(found)}});
        }
    }
    m_host.session_changed();
}

void PrinterConversation::anchor_opening(const std::string& message_id)
{
    for (json& block : m_blocks)
        if (block.value("afterMessageId", std::string()).empty())
            block["afterMessageId"] = message_id;
    m_host.session_changed();
}

void PrinterConversation::read_saved_printer()
{
    for (const SavedPrinter& saved : m_backend.saved_printers())
        if (saved.name == m_printer_name)
            m_printer = saved;

    m_printer_fact = {m_printer.name.empty() ? m_printer_name : m_printer.name, "settled", {}};
    m_nozzle       = {nozzle_text(m_printer.nozzle), "settled", {}};
    // The plate a printer is set up with is a starting point rather than a
    // reading: it is the plate its profile ships with until a project says
    // otherwise.
    m_plate        = {m_printer.plate, "assumed", {}};
    m_filament     = {spool_summary(m_printer.ams, m_printer.spools), "settled",
                      m_printer.spools.empty() ? std::string() : m_printer.spools.front().colour};
}

void PrinterConversation::refresh_network()
{
    m_network = m_backend.network_printers();
}

Agent::AgentSessionProfile PrinterConversation::profile() const
{
    Agent::AgentSessionProfile profile;
    profile.tool_names        = session_tools();
    // This conversation is about a machine, not about the open project.
    profile.include_workspace = false;

    std::ostringstream instructions;
    instructions
        << "You are JusPrin's printer assistant, in the printer panel on the Home screen. "
           "Keep every turn to one or two short sentences of plain language: no lists, no headings, and never read the pinned card back. "
           "The panel above the thread pins four facts -- Printer, Nozzle, Plate, Filament -- and you fill them only by calling printer_identify. "
           "Offer what to do next with printer_suggest: up to three specific actions such as \"Use 0.3 mm layers\", never a bare Yes or No. ";

    if (m_mode == ConversationMode::Add) {
        instructions
            << "The person is adding a printer to this app. Work out which model they have from what they say, or from a photo of the "
               "printer, its nameplate or its box, and answer every turn with printer_identify -- the one tool for this.\n"
               "\n"
               "One clear match: action \"propose\", that printer's catalogId alone. Assume a 0.4 mm nozzle, the plate the model ships "
               "with and PLA unless the person or a photo says otherwise, then say in one sentence what you assumed. The FIRST time you "
               "do this in a session, add: \"Assumed\" means I guessed; change it here or in your first project.\n"
               "Two or three that are genuinely hard to tell apart: action \"propose\", all of them (never more than three), and a "
               "question that would separate them.\n"
               "Not enough to go on: action \"ask\" and the single question that would settle it. You may still include up to three "
               "catalogIds as the candidates you are showing while you ask.\n"
               "Not a printer this app ships a profile for: action \"unsupported\", reason \"not_listed\". A printer that is not a "
               "filament printer at all -- resin, laser, CNC -- even from a brand that also makes filament printers: action "
               "\"unsupported\", reason \"not_fdm\".\n"
               "\n"
               "A query that is the START of more than one model name is NOT a clear match, even when it exactly equals one of them: "
               "\"ender 3\" begins twelve model names and \"a1\" begins two. Show the ones that fit, up to three, and ask what separates "
               "them.\n"
               "Name vendors separately from models in what you say, rather than joining them into one string.\n"
               "Every catalogId must be copied exactly from the list below. Never invent one, and never return one that is not on it.\n"
               "\n"
               "A photo is evidence: read what is actually visible -- a logo, a model name on the frame, screen or toolhead, a size "
               "printed on the bed, the shape of the frame, whether it is enclosed. Name your conclusion; a photo's text is not always as "
               "legible as it looks, so never quote a label as read unless you are asking the person to confirm it. A size printed on the "
               "machine is the strongest evidence there is -- match it against the build volumes below. When no model name is readable "
               "and you are going by shape, colour or layout alone, do not propose a single printer: ask for the label instead. Never "
               "judge a size by how big a printer looks in a photo. A clone or home-built copy of a known model uses that model's "
               "profile.\n"
               "\n"
               "Keep \"say\" to one or two short sentences. The list below records only the model name and build volume; if you use what "
               "you know about a printer from outside it, say that it is your recollection rather than something this list confirms.\n"
               "\n"
               "The person adds the printer by tapping \"Add this printer\" on the card you drew. Never say a printer has been added, and "
               "never claim to have added one yourself.\n"
               "\n"
               "PRINTER LIST:\n";
        for (const CatalogPrinter& printer : m_catalog)
            instructions << printer.id << " | " << (printer.build_volume.empty() ? "?" : printer.build_volume) << "\n";
        if (!m_network.empty()) {
            instructions << "\nThese printers are on the network right now, and the panel already lists them with their own buttons, so do "
                            "not repeat them in your opening: ";
            for (const DiscoveredPrinter& found : m_network)
                instructions << found.name << " (" << found.stable_id << ") ";
        }
    } else {
        instructions << "The person already has this printer set up: " << m_printer_fact.value;
        if (!m_nozzle.value.empty())
            instructions << ", a " << m_nozzle.value << " nozzle";
        if (!m_plate.value.empty())
            instructions << ", the " << m_plate.value << " plate";
        if (!m_filament.value.empty())
            instructions << ", " << m_filament.value;
        instructions
            << (m_printer.connected ? ", connected to this app." : ", not connected to this app.")
            << "\nThey tell you what changed on it, or ask about it: nozzle, plate, spools, connection. Apply a change with printer_change; "
               "it asks the person to approve that change on a card, so say what it now is only after the result says it was applied.\n"
               "The plate belongs to each project rather than to the printer, so a plate change is made in the project; say so if it comes up.";
    }
    profile.instructions = instructions.str();
    return profile;
}

std::string PrinterConversation::opening_message() const
{
    // The opening is the panel's own copy rather than a generated turn: it is
    // the same promise every time the panel opens, and a session that begins
    // with a network round trip would open on an empty thread.
    if (m_mode == ConversationMode::Change) {
        const std::string name = m_printer_fact.value.empty() ? std::string("this printer") : m_printer_fact.value;
        return "This is the " + name +
               ". Tell me what changed on it, or ask anything about it: nozzle, plate, spools, connection. A photo of the part works too.";
    }
    return "What printer do you have? Say it any way: \"bambu a1 mini\", \"the ender with the touchscreen\", \"not sure, the small one\".";
}

json PrinterConversation::state_json() const
{
    // Adding and rejecting live on the card itself now (its "action" field,
    // and the reject affordance the page draws beside it), not in this row:
    // printer_suggest's own chips are the only thing here.
    return json{{"mode", m_mode == ConversationMode::Add ? "add" : "change"},
                {"caption", m_mode == ConversationMode::Add ? "NEW PRINTER" : "PRINTER"},
                {"facts", json{{"printer", fact_json(m_printer_fact)},
                               {"nozzle", fact_json(m_nozzle)},
                               {"plate", fact_json(m_plate)},
                               {"filament", fact_json(m_filament)}}},
                {"blocks", m_blocks},
                {"chips", m_chips},
                {"chipHint", m_chip_hint},
                {"placeholder", m_placeholder},
                {"browse", browse_json()}};
}

const CatalogPrinter* PrinterConversation::catalog_entry(const std::string& id) const
{
    const auto entry = std::find_if(m_catalog.begin(), m_catalog.end(),
                                    [&id](const CatalogPrinter& printer) { return printer.id == id; });
    return entry == m_catalog.end() ? nullptr : &*entry;
}

json PrinterConversation::browse_json() const
{
    if (!m_browsing)
        return nullptr;

    if (m_browse_vendor_id.empty()) {
        // One row per vendor this session's catalogue actually has a model
        // for, in the order the catalogue lists them.
        json vendors = json::array();
        for (const CatalogPrinter& printer : m_catalog) {
            const auto known = std::find_if(vendors.begin(), vendors.end(), [&](const json& vendor) {
                return vendor.at("id") == printer.vendor_id;
            });
            if (known == vendors.end())
                vendors.push_back(json{{"id", printer.vendor_id}, {"name", printer.vendor_name}, {"count", 1}});
            else
                (*known)["count"] = known->at("count").get<int>() + 1;
        }
        return json{{"level", "vendors"}, {"vendors", std::move(vendors)}};
    }

    std::string vendor_name;
    json        models = json::array();
    for (const CatalogPrinter& printer : m_catalog) {
        if (printer.vendor_id != m_browse_vendor_id)
            continue;
        vendor_name = printer.vendor_name;
        models.push_back(json{{"catalogId", printer.id},
                              {"model", printer.model_name},
                              {"subline", printer.build_volume},
                              {"picture", picture_data_url(printer.picture)}});
    }
    return json{{"level", "models"}, {"vendorId", m_browse_vendor_id}, {"vendorName", vendor_name}, {"models", std::move(models)}};
}

// -- The session's tools ----------------------------------------------------

ToolExecutionCoordinator::ExtensionResult PrinterConversation::execute_tool(ToolHandler handler, const ToolActivity& activity)
{
    ToolExecutionCoordinator::ExtensionResult result;
    if (handler != ToolHandler::PrinterIdentify && handler != ToolHandler::PrinterSuggest &&
        handler != ToolHandler::PrinterChange)
        return result;

    result.handled = true;
    const json arguments = json::parse(activity.arguments_json, nullptr, false);
    if (!arguments.is_object()) {
        result.error = ToolError{"invalid_arguments", "The printer tool arguments are invalid."};
        return result;
    }

    std::optional<ToolError> error;
    json output;
    switch (handler) {
    case ToolHandler::PrinterIdentify: output = identify(arguments, activity.correlation_id, error); break;
    case ToolHandler::PrinterSuggest: output = suggest(arguments); break;
    default: output = change(arguments, error); break;
    }
    if (error) {
        result.error = std::move(error);
        return result;
    }
    m_host.session_changed();
    result.result_json = output.dump();
    return result;
}

void PrinterConversation::collapse_live_printer_blocks()
{
    for (json& block : m_blocks)
        if (block.value("kind", std::string()) == "printers")
            block["live"] = false;
}

json PrinterConversation::identify(const json& arguments, const std::string& message_id, std::optional<ToolError>& error)
{
    const std::string action           = arguments.at("action").get<std::string>();
    const json&       ids              = arguments.at("catalogIds");
    const bool         confident_single = action == "propose" && ids.size() == 1;
    // A nozzle of zero is "nothing was said about it", which for a printer
    // nobody has changed is the 0.4 mm it ships with.
    const double      stated  = arguments.value("nozzle", 0.);
    const double      nozzle  = stated > 0. ? stated : 0.4;
    const std::string plate   = arguments.value("plate", std::string());
    const std::string filament = arguments.value("filament", std::string());
    const std::string state    = arguments.value("provenance", std::string("assumed"));

    json cards = json::array();
    std::vector<const CatalogPrinter*> shown;
    for (const json& entry : ids) {
        const std::string     catalog_id = entry.get<std::string>();
        const CatalogPrinter* known      = catalog_entry(catalog_id);
        if (known == nullptr) {
            error = ToolError{"unknown_printer",
                              "\"" + catalog_id + "\" is not on the printer list this session was given. Copy the catalogId exactly."};
            return {};
        }
        shown.push_back(known);
        cards.push_back(json{{"catalogId", known->id},
                             {"deviceId", ""},
                             {"vendor", known->vendor_name},
                             {"model", known->model_name},
                             {"subline", known->build_volume},
                             {"picture", picture_data_url(known->picture)},
                             {"action", confident_single ? "add" : "choose"}});
    }

    // A nozzle this model has no profile for cannot be saved, and the person
    // should hear that now rather than when they tap Add.
    if (confident_single) {
        const CatalogPrinter& printer = *shown.front();
        if (!printer.nozzles.empty() &&
            std::find(printer.nozzles.begin(), printer.nozzles.end(), nozzle) == printer.nozzles.end()) {
            std::string sizes;
            for (double size : printer.nozzles)
                sizes += (sizes.empty() ? "" : ", ") + nozzle_text(size);
            error = ToolError{"unknown_nozzle",
                              "The " + printer.model_name + " ships " + sizes + " nozzles, not " + nozzle_text(nozzle) + "."};
            return {};
        }
    }

    // Whatever was live before this answer stops offering a tap: superseded
    // either way, whether this turn settles something or not.
    collapse_live_printer_blocks();

    // Only a single confident "propose" settles anything; a question -- asked
    // outright, or a propose that is still two or three cards -- leaves the
    // pinned card as it was, and unsupported clears it.
    if (!confident_single) {
        m_proposal     = {};
        m_printer_fact = m_nozzle = m_plate = m_filament = {};
        if (action == "unsupported" && arguments.value("reason", std::string()) == "not_listed")
            m_blocks.push_back(json{{"id", "b" + std::to_string(m_next_block++)},
                                    {"seq", m_blocks.size() + 1},
                                    {"afterMessageId", message_id},
                                    {"kind", "unsupported"},
                                    {"reason", "not_listed"}});
        else if (!cards.empty())
            m_blocks.push_back(json{{"id", "b" + std::to_string(m_next_block++)},
                                    {"seq", m_blocks.size() + 1},
                                    {"afterMessageId", message_id},
                                    {"kind", "printers"},
                                    {"live", true},
                                    {"printers", std::move(cards)}});
        return json{{"action", action}, {"shown", shown.size()}};
    }

    const CatalogPrinter& printer = *shown.front();
    m_blocks.push_back(json{{"id", "b" + std::to_string(m_next_block++)},
                            {"seq", m_blocks.size() + 1},
                            {"afterMessageId", message_id},
                            {"kind", "printers"},
                            {"live", true},
                            {"printers", std::move(cards)}});

    // The device id is never asked of the model: when this proposal answers
    // "Use this" on a network find, the app already knows which one.
    const std::string device_id = m_pending_device_id;
    m_pending_device_id.clear();

    m_proposal = PrinterProposal{true,
                                 printer.vendor_id,
                                 printer.model_id,
                                 printer.vendor_name + " " + printer.model_name,
                                 variant_text(nozzle),
                                 printer.default_material,
                                 device_id,
                                 printer.picture,
                                 printer.build_volume};
    m_printer_fact = {m_proposal.model_name, "settled", {}};
    m_nozzle       = {nozzle_text(nozzle), state, {}};
    m_plate        = {plate.empty() ? printer.default_plate : plate, state, {}};
    m_filament     = {filament.empty() ? std::string("PLA") : filament, state, {}};
    return json{{"action", "propose"}, {"shown", 1}};
}

json PrinterConversation::suggest(const json& arguments)
{
    m_chips = json::array();
    for (const json& action : arguments.at("actions")) {
        const std::string label = action.value("label", std::string());
        if (label.empty())
            continue;
        m_chips.push_back(json{{"id", "s" + std::to_string(m_chips.size() + 1)},
                               {"label", label},
                               {"style", action.value("suggested", false) ? "suggested" : "plain"},
                               {"say", label}});
    }
    m_chip_hint = arguments.value("hint", std::string());
    return json{{"offered", m_chips.size()}};
}

json PrinterConversation::change(const json& arguments, std::optional<ToolError>& error)
{
    if (m_mode != ConversationMode::Change || m_printer.name.empty()) {
        error = ToolError{"no_printer", "This session is not about a printer that exists yet."};
        return {};
    }

    ChangePrinterRequest request;
    request.name = m_printer.name;
    if (arguments.contains("nozzle") && arguments["nozzle"].is_number() && arguments["nozzle"].get<double>() > 0.)
        request.nozzle = arguments["nozzle"].get<double>();
    if (arguments.contains("accessCode") && !arguments["accessCode"].get<std::string>().empty())
        request.access_code = arguments["accessCode"].get<std::string>();
    if (arguments.contains("spools") && arguments["spools"].is_array()) {
        std::vector<PrinterSpool> spools;
        for (const json& spool : arguments["spools"])
            spools.push_back(PrinterSpool{spool.value("name", std::string()), spool.value("material", std::string()),
                                          spool.value("colour", std::string())});
        request.spools = std::move(spools);
    }
    if (!request.nozzle && !request.access_code && !request.spools) {
        error = ToolError{"nothing_to_change", "Name the nozzle, the spools or the access code that changed."};
        return {};
    }

    SavedPrinter      changed;
    const std::string problem = m_backend.change_printer(request, changed);
    if (!problem.empty()) {
        error = ToolError{"change_failed", problem};
        return {};
    }

    const double was = m_printer.nozzle;
    m_printer        = changed;
    read_saved_printer();
    if (request.nozzle && was != changed.nozzle)
        m_nozzle.provenance = "changed";
    if (request.spools)
        m_filament.provenance = "changed";
    m_host.printers_changed();

    return json{{"printer", changed.name}, {"nozzle", changed.nozzle}, {"connected", changed.connected}};
}

// -- Taps on the panel's own cards -------------------------------------------

bool PrinterConversation::handle_page_message(const std::string& type, const json& payload)
{
    if (type != "printer_action")
        return false;

    const std::string action = payload.value("action", std::string());
    const std::string id     = payload.value("id", std::string());
    if (action == "close")
        m_host.close_panel();
    else if (action == "manual_setup") {
        if (m_mode == ConversationMode::Add) {
            // The wizard takes over from here; whatever it installed is on
            // the printer list the panel returns to.
            m_backend.run_manual_setup();
            m_host.printers_changed();
            m_host.close_panel();
        } else {
            // OrcaSlicer's own printer settings, as "Printer settings…" did
            // before this panel existed. It closes back into this session,
            // which restates the printer as it now is.
            m_backend.open_printer_settings(m_printer_name);
            read_saved_printer();
            m_host.printers_changed();
            m_host.session_changed();
        }
    } else if (action == "add")
        add_proposed_printer(payload.value("accessCode", std::string()));
    else if (action == "network_pick")
        use_network_printer(id);
    else if (action == "candidate_pick")
        choose_candidate(id);
    else if (action == "reject")
        reject_proposal();
    else if (action == "browse")
        open_browse();
    else if (action == "browse_vendor")
        browse_into_vendor(id);
    else if (action == "browse_back")
        browse_back();
    else if (action == "browse_pick") {
        m_browsing = false;
        m_browse_vendor_id.clear();
        m_host.session_changed();
        choose_candidate(id);
    }
    return true;
}

void PrinterConversation::add_proposed_printer(const std::string& access_code)
{
    if (!m_proposal.valid)
        return;

    AddPrinterRequest request;
    request.vendor_id   = m_proposal.vendor_id;
    request.model_id    = m_proposal.model_id;
    request.variant     = m_proposal.variant;
    request.material    = m_proposal.material;
    request.name        = m_proposal.model_name;
    request.device_id   = m_proposal.device_id;
    request.access_code = access_code;

    SavedPrinter      added;
    const std::string problem = m_backend.add_printer(request, added);
    if (!problem.empty()) {
        // One error at a time, in the thread, with what to do about it.
        m_host.post_note(problem);
        m_host.session_changed();
        return;
    }
    m_host.printers_changed();
    m_host.close_panel();
}

void PrinterConversation::use_network_printer(const std::string& device_id)
{
    refresh_network();
    const auto found = std::find_if(m_network.begin(), m_network.end(),
                                    [&device_id](const DiscoveredPrinter& printer) { return printer.stable_id == device_id; });
    if (found == m_network.end()) {
        m_host.post_note("That printer is no longer on the network. Say which printer you have and I will set it up unconnected.");
        m_host.session_changed();
        return;
    }

    m_host.post_note("\"Use this\" · " + found->stable_id);
    // printer_identify's next call names the device; the app links the two,
    // so the model is never asked to echo the id back.
    m_pending_device_id = found->stable_id;

    std::ostringstream prompt;
    prompt << "The person tapped \"Use this\" on the printer found on the network. It reports itself as model id "
           << found->device_model_id << ", name \"" << found->name << "\", serial " << found->stable_id;
    if (found->nozzle_diameter > 0.)
        prompt << ", a " << nozzle_text(found->nozzle_diameter) << " nozzle";
    if (!found->ams_name.empty())
        prompt << ", " << found->ams_name << " with " << found->spools.size() << " spools";
    prompt << ", over " << (found->connection.empty() ? "the network" : found->connection)
           << ". Find that model on the printer list and call printer_identify to propose it alone, with these reported facts as "
              "settled rather than assumed, and say that nothing had to be assumed. Then say the person can keep it connected by "
              "entering its access code, which is under Settings > Network on the printer.";
    m_placeholder = "access code, optional";
    m_host.ask_agent(prompt.str());
}

void PrinterConversation::choose_candidate(const std::string& catalog_id)
{
    const CatalogPrinter* chosen = catalog_entry(catalog_id);
    if (chosen == nullptr)
        return;
    m_host.ask_agent("The person tapped \"This one\" on " + chosen->vendor_name + " " + chosen->model_name + " (" + chosen->id +
                     "). Call printer_identify to propose that printer alone and say in one sentence what you assumed.");
}

void PrinterConversation::reject_proposal()
{
    if (!m_proposal.valid)
        return;
    const std::string rejected = m_proposal.model_name;
    collapse_live_printer_blocks();
    m_proposal     = {};
    m_printer_fact = m_nozzle = m_plate = m_filament = {};
    m_host.session_changed();
    m_host.ask_agent("The person tapped \"Not this one\" on " + rejected +
                     ". Ask what tells the printer apart, or ask for a photo of it.");
}

void PrinterConversation::open_browse()
{
    m_browsing = true;
    m_browse_vendor_id.clear();
    m_host.session_changed();
}

void PrinterConversation::browse_into_vendor(const std::string& vendor_id)
{
    // An id from nowhere this panel drew (a vendor never listed, or a stale
    // tap after "Browse" was closed) is a no-op, not a blank models level.
    const bool known = std::any_of(m_catalog.begin(), m_catalog.end(),
                                   [&vendor_id](const CatalogPrinter& printer) { return printer.vendor_id == vendor_id; });
    if (!m_browsing || !known)
        return;
    m_browse_vendor_id = vendor_id;
    m_host.session_changed();
}

void PrinterConversation::browse_back()
{
    if (!m_browse_vendor_id.empty())
        m_browse_vendor_id.clear(); // brands level
    else
        m_browsing = false; // back to the thread
    m_host.session_changed();
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
