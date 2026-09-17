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

// At most three candidates: more is a list to read rather than an answer.
constexpr std::size_t kMaximumCandidates = 3;

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
    return {"printer_catalog_search", "printer_propose", "printer_suggest", "printer_change"};
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
           "The panel above the thread pins four facts -- Printer, Nozzle, Plate, Filament -- and you fill them only by calling printer_propose. "
           "Offer what to do next with printer_suggest: up to three specific actions such as \"Use 0.3 mm layers\", never a bare Yes or No. ";

    if (m_mode == ConversationMode::Add) {
        instructions
            << "The person is adding a printer to this app.\n"
               "Finding it: search the packaged profiles with printer_catalog_search using the person's own words. A photo of the printer, "
               "its nameplate or its box is evidence -- read it and search for what you see.\n"
               "One clear match: call printer_propose with that printer alone, assuming a 0.4 mm nozzle, the plate the model ships with and "
               "PLA unless the person or the photo says otherwise, then say in one sentence what you assumed.\n"
               "Two or three that fit: call printer_propose with all of them and say what tells them apart, so the person can answer.\n"
               "Nothing fits: say so and ask for the one detail that would settle it.\n"
               "The person adds the printer by tapping \"Add this printer\" on the card you drew. Never say a printer has been added, and "
               "never claim to have added one yourself.";
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
    json chips = m_chips;
    // The person can always add the printer the agent has drawn; the chip is
    // the approval for it, so it leads the row whatever else is offered.
    if (m_proposal.valid) {
        json add{{"id", "add"}, {"label", "Add this printer"}, {"style", "primary"}, {"action", "add"}};
        chips.insert(chips.begin(), add);
        chips.push_back(json{{"id", "reject"}, {"label", "Not this one"}, {"style", "plain"}, {"say", "Not this one"}});
    }
    return json{{"mode", m_mode == ConversationMode::Add ? "add" : "change"},
                {"caption", m_mode == ConversationMode::Add ? "NEW PRINTER" : "PRINTER"},
                {"facts", json{{"printer", fact_json(m_printer_fact)},
                               {"nozzle", fact_json(m_nozzle)},
                               {"plate", fact_json(m_plate)},
                               {"filament", fact_json(m_filament)}}},
                {"blocks", m_blocks},
                {"chips", std::move(chips)},
                {"chipHint", m_chip_hint},
                {"placeholder", m_placeholder}};
}

const CatalogPrinter* PrinterConversation::catalog_entry(const std::string& id) const
{
    const auto entry = std::find_if(m_catalog.begin(), m_catalog.end(),
                                    [&id](const CatalogPrinter& printer) { return printer.id == id; });
    return entry == m_catalog.end() ? nullptr : &*entry;
}

// -- The session's tools ----------------------------------------------------

ToolExecutionCoordinator::ExtensionResult PrinterConversation::execute_tool(ToolHandler handler, const ToolActivity& activity)
{
    ToolExecutionCoordinator::ExtensionResult result;
    if (handler != ToolHandler::PrinterCatalogSearch && handler != ToolHandler::PrinterPropose &&
        handler != ToolHandler::PrinterSuggest && handler != ToolHandler::PrinterChange)
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
    case ToolHandler::PrinterCatalogSearch: output = search_catalog(arguments); break;
    case ToolHandler::PrinterPropose: output = propose(arguments, error); break;
    case ToolHandler::PrinterSuggest: output = suggest(arguments); break;
    default: output = change(arguments, error); break;
    }
    if (error) {
        result.error = std::move(error);
        return result;
    }
    // Everything the tools draw lands in the same place: the panel's own
    // state, pushed once per call.
    if (handler != ToolHandler::PrinterCatalogSearch) {
        if (handler == ToolHandler::PrinterPropose && !m_blocks.empty())
            m_blocks.back()["afterMessageId"] = activity.correlation_id;
        m_host.session_changed();
    }
    result.result_json = output.dump();
    return result;
}

json PrinterConversation::search_catalog(const json& arguments)
{
    const std::string query = arguments.value("query", std::string());
    const std::size_t limit = std::min<std::size_t>(arguments.value("limit", 5), 8);

    json items = json::array();
    for (const CatalogPrinter& printer : m_backend.search_catalog(query, limit)) {
        json nozzles = json::array();
        for (double nozzle : printer.nozzles)
            nozzles.push_back(nozzle);
        items.push_back(json{{"catalogId", printer.id},
                             {"vendor", printer.vendor_name},
                             {"model", printer.model_name},
                             {"buildVolume", printer.build_volume},
                             {"nozzles", std::move(nozzles)},
                             {"plate", printer.default_plate},
                             {"material", printer.default_material}});
        if (catalog_entry(printer.id) == nullptr)
            m_catalog.push_back(printer);
    }
    return json{{"items", std::move(items)}};
}

json PrinterConversation::propose(const json& arguments, std::optional<ToolError>& error)
{
    const json& printers = arguments.at("printers");
    if (printers.empty() || printers.size() > kMaximumCandidates) {
        error = ToolError{"invalid_arguments", "Propose one printer, or two or three when they are hard to tell apart."};
        return {};
    }

    json cards = json::array();
    std::vector<const CatalogPrinter*> proposed;
    for (const json& entry : printers) {
        const std::string catalog_id = entry.value("catalogId", std::string());
        const std::string device_id  = entry.value("deviceId", std::string());
        const CatalogPrinter* known  = catalog_entry(catalog_id);
        if (known == nullptr) {
            error = ToolError{"unknown_printer",
                              "\"" + catalog_id + "\" is not a printer from printer_catalog_search. Search first, then propose."};
            return {};
        }
        proposed.push_back(known);
        cards.push_back(json{{"catalogId", known->id},
                             {"deviceId", device_id},
                             {"name", known->vendor_name + " " + known->model_name},
                             {"subline", entry.value("subline", known->build_volume)},
                             {"picture", picture_data_url(known->picture)},
                             {"action", printers.size() == 1 ? "add" : "choose"}});
    }

    m_blocks.push_back(json{{"id", "b" + std::to_string(m_next_block++)},
                            {"seq", m_blocks.size() + 1},
                            {"afterMessageId", ""},
                            {"kind", "printers"},
                            {"printers", std::move(cards)}});

    // A nozzle of zero is "nothing was said about it", which for a printer
    // nobody has changed is the 0.4 mm it ships with.
    const double      stated   = arguments.value("nozzle", 0.);
    const double      nozzle   = stated > 0. ? stated : 0.4;
    const std::string plate    = arguments.value("plate", std::string());
    const std::string filament = arguments.value("filament", std::string());
    const std::string state    = arguments.value("provenance", std::string("assumed"));

    if (proposed.size() == 1) {
        const CatalogPrinter& printer = *proposed.front();
        const json&           entry   = printers.front();
        // A nozzle this model has no profile for cannot be saved, and the
        // person should hear that now rather than when they tap Add.
        if (!printer.nozzles.empty() &&
            std::find(printer.nozzles.begin(), printer.nozzles.end(), nozzle) == printer.nozzles.end()) {
            std::string sizes;
            for (double size : printer.nozzles)
                sizes += (sizes.empty() ? "" : ", ") + nozzle_text(size);
            m_blocks.erase(m_blocks.end() - 1);
            error = ToolError{"unknown_nozzle", "The " + printer.model_name + " ships " + sizes + " nozzles, not " +
                                                    nozzle_text(nozzle) + "."};
            return {};
        }
        m_proposal = PrinterProposal{true,
                                     printer.vendor_id,
                                     printer.model_id,
                                     printer.vendor_name + " " + printer.model_name,
                                     variant_text(nozzle),
                                     printer.default_material,
                                     entry.value("deviceId", std::string()),
                                     printer.picture,
                                     entry.value("subline", printer.build_volume)};
        m_printer_fact = {m_proposal.model_name, "settled", {}};
        m_nozzle       = {nozzle_text(nozzle), state, {}};
        m_plate        = {plate.empty() ? printer.default_plate : plate, state, {}};
        m_filament     = {filament.empty() ? std::string("PLA") : filament, state, {}};
    } else {
        // Nothing is settled while two printers are still on the table.
        m_proposal     = {};
        m_printer_fact = m_nozzle = m_plate = m_filament = {};
    }
    return json{{"proposed", proposed.size()}};
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

    std::ostringstream prompt;
    prompt << "The person tapped \"Use this\" on the printer found on the network. It reports itself as model id "
           << found->device_model_id << ", name \"" << found->name << "\", serial " << found->stable_id;
    if (found->nozzle_diameter > 0.)
        prompt << ", a " << nozzle_text(found->nozzle_diameter) << " nozzle";
    if (!found->ams_name.empty())
        prompt << ", " << found->ams_name << " with " << found->spools.size() << " spools";
    prompt << ", over " << (found->connection.empty() ? "the network" : found->connection)
           << ". Find that model with printer_catalog_search, propose it with these reported facts as settled rather than assumed, and "
              "say that nothing had to be assumed. Then say the person can keep it connected by entering its access code, which is under "
              "Settings > Network on the printer.";
    m_placeholder = "access code, optional";
    m_host.ask_agent(prompt.str());
}

void PrinterConversation::choose_candidate(const std::string& catalog_id)
{
    const CatalogPrinter* chosen = catalog_entry(catalog_id);
    if (chosen == nullptr)
        return;
    m_host.ask_agent("The person tapped \"This one\" on " + chosen->vendor_name + " " + chosen->model_name + " (" + chosen->id +
                     "). Propose that printer alone with printer_propose and say in one sentence what you assumed.");
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
