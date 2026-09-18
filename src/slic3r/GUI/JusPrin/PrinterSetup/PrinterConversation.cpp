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

std::string number_text(double value)
{
    std::ostringstream out;
    out.precision(2);
    out << std::fixed << value;
    std::string text = out.str();
    while (text.size() > 3 && text.back() == '0')
        text.pop_back();
    return text;
}

std::string nozzle_text(double nozzle) { return nozzle > 0. ? number_text(nozzle) + " mm" : std::string(); }

// The profile's own spelling of a nozzle size, as the variant is named:
// "0.4", "0.25", "1.0".
std::string variant_text(double nozzle) { return number_text(nozzle); }

// "a, b and c"
std::string listed(const std::vector<std::string>& items)
{
    std::string text;
    for (std::size_t i = 0; i < items.size(); ++i)
        text += (i == 0 ? "" : i + 1 == items.size() ? " and " : ", ") + items[i];
    return text;
}

// "0.2, 0.4, 0.6 and 0.8 mm"
std::string sizes_text(const std::vector<double>& nozzles)
{
    std::vector<std::string> sizes;
    for (double nozzle : nozzles)
        sizes.push_back(number_text(nozzle));
    return listed(sizes) + " mm";
}

// A nozzle of null or 0 is the model leaving it unsaid.
bool said_nozzle(const json& arguments)
{
    return arguments.contains("nozzle") && arguments["nozzle"].is_number() && arguments["nozzle"].get<double>() > 0.;
}

bool ships(const std::vector<double>& nozzles, double nozzle)
{
    return std::find(nozzles.begin(), nozzles.end(), nozzle) != nozzles.end();
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

json nullable(const std::string& text) { return text.empty() ? json(nullptr) : json(text); }

json spools_json(const std::vector<PrinterSpool>& spools)
{
    json list = json::array();
    for (const PrinterSpool& spool : spools) {
        json entry{{"name", spool.name}, {"material", spool.material}};
        if (!spool.colour.empty())
            entry["colour"] = spool.colour;
        list.push_back(std::move(entry));
    }
    return list;
}

std::vector<PrinterSpool> spools_of(const json& list)
{
    std::vector<PrinterSpool> spools;
    for (const json& spool : list)
        spools.push_back(PrinterSpool{spool.value("name", std::string()), spool.value("material", std::string()),
                                      spool.value("colour", std::string())});
    return spools;
}

bool same_spools(const std::vector<PrinterSpool>& lhs, const std::vector<PrinterSpool>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const PrinterSpool& a, const PrinterSpool& b) {
        return a.name == b.name && a.material == b.material && a.colour == b.colour;
    });
}

// "PLA Matte, PETG" or "none"
std::string spools_text(const std::vector<PrinterSpool>& spools)
{
    std::vector<std::string> names;
    for (const PrinterSpool& spool : spools)
        names.push_back(spool.name.empty() ? spool.material : spool.name);
    return names.empty() ? std::string("none") : listed(names);
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

std::vector<PrinterSpool> reported_spools(const DiscoveredPrinter& device)
{
    std::vector<PrinterSpool> spools;
    for (const DiscoveredPrinter::Spool& spool : device.spools)
        spools.push_back(PrinterSpool{spool.name, spool.material, spool.colour});
    return spools;
}

} // namespace

PrinterConversation::PrinterConversation(IPrinterBackend& backend, IConversationHost& host)
    : m_backend(backend), m_host(host)
{}

std::vector<std::string> PrinterConversation::session_tools(ConversationMode mode)
{
    // One tool per mode. A Change session that could identify a printer
    // could also clear the card of the printer it is about.
    return {mode == ConversationMode::Add ? "printer_identify" : "printer_change"};
}

void PrinterConversation::start(ConversationMode mode, const std::string& printer_name)
{
    m_mode         = mode;
    m_printer_name = printer_name;
    m_printer      = {};
    m_proposal     = {};
    m_blocks       = json::array();
    m_next_block   = 1;
    m_stated_nozzle.clear();
    m_undo.reset();
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
        m_placeholder = "e.g. \"I put a 0.6 nozzle on it\" or \"loaded black PETG\"";
    } else {
        refresh_network();
        m_placeholder = "e.g. \"bambu a1 mini\" or \"not sure, the small one\"";
        // A photo is the fastest way in, and the printers already on the
        // network are the fastest of all. Both belong under the opening,
        // where the person is deciding what to say.
        m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", ""}, {"kind", "tip"}});
        if (!m_network.empty()) {
            json found = json::array();
            for (const DiscoveredPrinter& printer : m_network)
                found.push_back(json{{"deviceId", printer.stable_id},
                                     {"name", printer.name},
                                     {"serial", printer.stable_id},
                                     {"online", printer.connected}});
            m_blocks.push_back(json{{"id", next_block_id()},
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
    for (json& entry : m_blocks)
        if (entry.value("afterMessageId", std::string()).empty())
            entry["afterMessageId"] = message_id;
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

void PrinterConversation::refresh_network() { m_network = m_backend.network_printers(); }

std::string PrinterConversation::printer_list() const
{
    std::ostringstream list;
    for (const CatalogPrinter& printer : m_backend.catalog())
        list << printer.id << " | " << printer.display_name() << " | "
             << (printer.build_volume.empty() ? std::string("?") : printer.build_volume) << "\n";
    return list.str();
}

Agent::AgentSessionProfile PrinterConversation::profile() const
{
    Agent::AgentSessionProfile profile;
    profile.tool_names = session_tools(m_mode);
    // This conversation is about a machine, not about the open project, and
    // what the person taps on its cards reaches the model as the app's notes.
    profile.include_workspace = false;
    profile.notes_in_context  = true;

    std::ostringstream instructions;
    instructions << "You are JusPrin's printer assistant, in the printer panel on the Home screen. Keep every reply to one or two "
                    "short sentences of plain language: no lists, no headings, and never read the pinned card back. "
                    "Messages from the app (developer role) state what the person did on the panel, such as tapping a card; "
                    "they are facts, not requests.\n";

    if (m_mode == ConversationMode::Add) {
        instructions
            << "The person is adding a printer to this app. You decide which printer they have, from their words or a photo, "
               "using the printer list below. printer_identify shows the printers you name as cards; the person adds one by "
               "tapping \"Add this printer\" on its card, so never say a printer has been added.\n"
               "Rules:\n"
               "- One printer fits: call printer_identify with it.\n"
               "- Two or three genuinely fit: call it with all of them, and ask in your reply what tells them apart.\n"
               "- More than three fit: do not call it. Ask one question that narrows it down and say where to look, or point "
               "to \"Set it up myself\" at the top of the panel, which lists every printer. Never show three of many.\n"
               "- A query that is the start of more than one model name is not a clear match, even when it exactly equals one "
               "of them. Before calling with one id, look for other names in the list that begin with what the person typed: "
               "\"prusa mk4\" begins Prusa MK4, MK4S and MK4S HF, so it is three printers, not one.\n"
               "- Nothing fits, or it isn't a filament printer: say so in one sentence; do not call it.\n"
               "- A photo: name a model only from a readable name or a printed size. Going by shape alone, ask for a photo of "
               "the label (a sticker on the back, a plate under the frame, the About page on the screen). Never quote a label "
               "as read unless asking the person to confirm it. Never judge size from how big it looks. A clone uses the "
               "profile of the model it copies.\n"
               "- Pass a nozzle only when the person or a photo said its size.\n"
               "- When the tool returns an error, fix the call or ask. After unknown_nozzle, ask which of the sizes it lists "
               "is on the printer.\n"
               "- Write every reply from the facts the tool returns, never from memory. A fact that is null has nothing to "
               "say about it.\n";
        if (!m_network.empty()) {
            instructions << "These printers are on the network right now, and the panel already lists them with their own "
                            "\"Use this\" buttons: ";
            for (const DiscoveredPrinter& found : m_network)
                instructions << found.name << " (" << found.stable_id << ") ";
            instructions << "\n";
        }
        instructions << "\nPrinter list (catalogId | brand and model | build volume):\n" << printer_list();
    } else {
        instructions
            << "The person already has this printer set up and tells you what changed on it, or asks about it.\n"
               "Rules:\n"
               "- Work out what physically changed from what the person says, and change only that with printer_change. It "
               "asks the person to confirm on a card before anything is saved.\n"
               "- The plate belongs to each project, not the printer: say it is chosen in the project's printer menu; do not "
               "call the tool.\n"
               "- Nozzle material, such as hardened steel, isn't tracked: say so; do not call the tool.\n"
               "- Connecting a printer isn't possible from this panel: say so in one sentence.\n"
               "- Report only what the tool's result says changed.\n"
               "\nThe printer as it is now:\n"
               "Name: "
            << m_printer.name << "\n";
        if (!m_printer.model.empty())
            instructions << "Brand and model: " << m_printer.model << "\n";
        instructions << "Nozzle: " << (m_printer.nozzle > 0. ? nozzle_text(m_printer.nozzle) : std::string("unknown"));
        if (!m_printer.nozzles.empty())
            instructions << " (this model ships " << sizes_text(m_printer.nozzles) << ")";
        instructions << "\nSpools loaded:";
        if (m_printer.spools.empty())
            instructions << " none recorded";
        for (const PrinterSpool& spool : m_printer.spools)
            instructions << "\n- " << (spool.name.empty() ? spool.material : spool.name) << " (" << spool.material
                         << (spool.colour.empty() ? std::string() : ", " + spool.colour) << ")";
        instructions << "\nConnected to this app: " << (m_printer.connected ? "yes" : "no") << "\n";
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
    json chips = json::array();
    // The person can always add the printer on the card; the chip is the
    // approval for it, so it leads the row.
    if (m_proposal.valid) {
        chips.push_back(json{{"id", "add"}, {"label", "Add this printer"}, {"style", "primary"}, {"action", "add"}});
        chips.push_back(json{{"id", "reject"}, {"label", "Not this one"}, {"style", "plain"}, {"action", "reject"}});
    }
    return json{{"mode", m_mode == ConversationMode::Add ? "add" : "change"},
                {"caption", m_mode == ConversationMode::Add ? "NEW PRINTER" : "PRINTER"},
                {"facts", json{{"printer", fact_json(m_printer_fact)},
                               {"nozzle", fact_json(m_nozzle)},
                               {"plate", fact_json(m_plate)},
                               {"filament", fact_json(m_filament)}}},
                {"blocks", m_blocks},
                {"chips", std::move(chips)},
                // A printer found on the network can be kept connected; its
                // code goes from this field to the app, never into the chat.
                {"accessCode", m_proposal.valid && !m_proposal.device_id.empty()},
                {"placeholder", m_placeholder}};
}

const CatalogPrinter* PrinterConversation::catalog_entry(const std::string& id) const
{
    const std::vector<CatalogPrinter>& catalog = m_backend.catalog();
    const auto entry = std::find_if(catalog.begin(), catalog.end(), [&id](const CatalogPrinter& printer) { return printer.id == id; });
    return entry == catalog.end() ? nullptr : &*entry;
}

const CatalogPrinter* PrinterConversation::model_of(const DiscoveredPrinter& device) const
{
    if (device.device_model_id.empty())
        return nullptr;
    const std::vector<CatalogPrinter>& catalog = m_backend.catalog();
    const auto entry = std::find_if(catalog.begin(), catalog.end(), [&device](const CatalogPrinter& printer) {
        return printer.device_model_id == device.device_model_id;
    });
    return entry == catalog.end() ? nullptr : &*entry;
}

json* PrinterConversation::block(const std::string& id)
{
    for (json& entry : m_blocks)
        if (entry.value("id", std::string()) == id)
            return &entry;
    return nullptr;
}

double PrinterConversation::assumed_nozzle(const CatalogPrinter& printer) const
{
    if (printer.nozzles.empty() || ships(printer.nozzles, 0.4))
        return 0.4;
    return printer.nozzles.front();
}

json PrinterConversation::identified_json(const CatalogPrinter& printer, double nozzle) const
{
    json nozzles = json::array();
    for (double size : printer.nozzles)
        nozzles.push_back(size);
    const std::vector<SavedPrinter> saved = m_backend.saved_printers();
    const bool yours = std::any_of(saved.begin(), saved.end(),
                                   [&printer](const SavedPrinter& mine) { return mine.model_id == printer.model_id; });
    return json{{"catalogId", printer.id},
                {"brand", printer.vendor_name},
                {"model", printer.model_name},
                {"buildVolume", printer.build_volume},
                {"nozzles", std::move(nozzles)},
                {"assumed", json{{"nozzle", nozzle},
                                 {"plate", nullable(printer.default_plate)},
                                 {"filament", nullable(printer.filament_for(nozzle))}}},
                {"alreadyYours", yours}};
}

json PrinterConversation::printer_json() const
{
    json nozzles = json::array();
    for (double size : m_printer.nozzles)
        nozzles.push_back(size);
    return json{{"name", m_printer.name},
                {"nozzle", m_printer.nozzle},
                {"nozzles", std::move(nozzles)},
                {"spools", spools_json(m_printer.spools)},
                {"connected", m_printer.connected}};
}

void PrinterConversation::collapse_cards()
{
    for (json& entry : m_blocks)
        if (entry.value("kind", std::string()) == "printers")
            entry["collapsed"] = true;
}

void PrinterConversation::clear_proposal()
{
    m_proposal     = {};
    m_printer_fact = m_nozzle = m_plate = m_filament = {};
}

void PrinterConversation::show_proposal(const CatalogPrinter& printer, double nozzle, bool nozzle_stated,
                                        const std::string& block_id, const DiscoveredPrinter* device)
{
    const std::string filament = printer.filament_for(nozzle);
    m_proposal = PrinterProposal{true,
                                 block_id,
                                 printer.vendor_id,
                                 printer.model_id,
                                 printer.display_name(),
                                 variant_text(nozzle),
                                 filament,
                                 device != nullptr ? device->stable_id : std::string()};

    m_printer_fact = {printer.display_name(), "settled", {}};
    m_nozzle       = {nozzle_text(nozzle), nozzle_stated ? "settled" : "assumed", {}};
    m_plate        = {printer.default_plate, "assumed", {}};
    const std::vector<PrinterSpool> loaded = device != nullptr ? reported_spools(*device) : std::vector<PrinterSpool>();
    if (!loaded.empty())
        m_filament = {spool_summary(device->ams_name, loaded), "settled", loaded.front().colour};
    else
        m_filament = {filament, "assumed", {}};
}

// -- The session's tools ----------------------------------------------------

std::optional<ToolError> PrinterConversation::preflight_tool(ToolHandler handler, ToolActivity& activity) const
{
    if (handler != ToolHandler::PrinterChange)
        return std::nullopt;
    if (m_mode != ConversationMode::Change)
        return ToolError{"no_printer", "This session is not about a printer the person owns."};

    json arguments = json::parse(activity.arguments_json, nullptr, false);
    if (!arguments.is_object())
        return ToolError{"invalid_arguments", "The printer tool arguments are invalid."};

    std::optional<SavedPrinter> now;
    for (const SavedPrinter& saved : m_backend.saved_printers())
        if (saved.name == m_printer_name)
            now = saved;
    if (!now)
        return ToolError{"printer_gone", "\"" + m_printer_name + "\" was renamed or removed; this panel can no longer change it."};

    const bool has_nozzle = said_nozzle(arguments);
    const bool has_spools = arguments.contains("spools");
    if (!has_nozzle && !has_spools)
        return ToolError{"nothing_to_change", "Name the nozzle or the spools that changed."};
    const std::string model = now->model.empty() ? now->name : now->model;
    if (has_nozzle) {
        const double nozzle = arguments["nozzle"].get<double>();
        if (!now->nozzles.empty() && !ships(now->nozzles, nozzle))
            return ToolError{"unknown_nozzle", "The " + model + " ships " + sizes_text(now->nozzles) + " nozzles, not " +
                                                   nozzle_text(nozzle) + "."};
    }
    const bool nozzle_moves = has_nozzle && arguments["nozzle"].get<double>() != now->nozzle;
    const bool spools_move  = has_spools && !same_spools(spools_of(arguments["spools"]), now->spools);
    if (!nozzle_moves && !spools_move)
        return ToolError{"nothing_to_change", "That is already how this printer is set up: a " + nozzle_text(now->nozzle) +
                                                  " nozzle, and spools: " + spools_text(now->spools) + "."};

    // What the card states: the change in words, against the printer as it
    // is while the card waits.
    activity.title = nozzle_moves && spools_move ? "Change nozzle and spools" : nozzle_moves ? "Change nozzle" : "Change spools";
    json before = json::object();
    if (nozzle_moves)
        before["nozzle"] = now->nozzle;
    else
        arguments.erase("nozzle");
    if (spools_move)
        before["spools"] = spools_json(now->spools);
    else
        arguments.erase("spools");
    arguments["confirm"]    = json{{"printer", model}, {"before", std::move(before)}};
    activity.arguments_json = arguments.dump();
    return std::nullopt;
}

ToolExecutionCoordinator::ExtensionResult PrinterConversation::execute_tool(ToolHandler handler, const ToolActivity& activity)
{
    ToolExecutionCoordinator::ExtensionResult result;
    if (handler != ToolHandler::PrinterIdentify && handler != ToolHandler::PrinterChange)
        return result;

    result.handled = true;
    const json arguments = json::parse(activity.arguments_json, nullptr, false);
    if (!arguments.is_object()) {
        result.error = ToolError{"invalid_arguments", "The printer tool arguments are invalid."};
        return result;
    }

    std::optional<ToolError> error;
    const json output = handler == ToolHandler::PrinterIdentify ? identify(arguments, activity.correlation_id, error) :
                                                                  change(arguments, activity.correlation_id, error);
    if (error) {
        result.error = std::move(error);
        return result;
    }
    m_host.session_changed();
    result.result_json = output.dump();
    return result;
}

std::optional<json> PrinterConversation::tool_output(const ToolActivity& activity) const
{
    if (activity.tool != "printer_identify" && activity.tool != "printer_change")
        return std::nullopt;
    if (activity.state == Agent::ToolState::Succeeded)
        return json::parse(activity.result_json, nullptr, false);
    if (activity.state == Agent::ToolState::Failed && activity.error)
        return json{{"error", json{{"code", activity.error->code}, {"message", activity.error->message}}}};
    if (activity.state == Agent::ToolState::Rejected && activity.tool == "printer_change")
        return json{{"state", "declined"}, {"changed", json::array()}, {"printer", printer_json()}};
    return std::nullopt;
}

json PrinterConversation::identify(const json& arguments, const std::string& message_id, std::optional<ToolError>& error)
{
    if (m_mode != ConversationMode::Add) {
        error = ToolError{"not_adding", "This session is about a printer the person already has."};
        return {};
    }

    std::vector<std::string> ids;
    for (const json& id : arguments.at("catalogIds"))
        if (std::find(ids.begin(), ids.end(), id.get<std::string>()) == ids.end())
            ids.push_back(id.get<std::string>());
    if (ids.size() > kMaximumCandidates) {
        error = ToolError{"too_many", "More than three fit. Ask one question that narrows it down instead."};
        return {};
    }

    std::vector<const CatalogPrinter*> printers;
    for (const std::string& id : ids) {
        const CatalogPrinter* printer = catalog_entry(id);
        if (printer == nullptr) {
            error = ToolError{"unknown_printer", "\"" + id + "\" is not on the printer list; copy it exactly from the printer list."};
            return {};
        }
        printers.push_back(printer);
    }

    const bool   stated = said_nozzle(arguments);
    const double wanted = stated ? arguments["nozzle"].get<double>() : 0.;
    if (stated)
        for (const CatalogPrinter* printer : printers)
            if (!ships(printer->nozzles, wanted)) {
                error = ToolError{"unknown_nozzle", "The " + printer->display_name() + " ships " + sizes_text(printer->nozzles) +
                                                        " nozzles, not " + nozzle_text(wanted) + "."};
                return {};
            }

    // The newest answer is the one to act on; earlier cards fold away.
    collapse_cards();
    const std::string id    = next_block_id();
    json              cards = json::array();
    json              found = json::array();
    for (const CatalogPrinter* printer : printers) {
        const double nozzle = stated ? wanted : assumed_nozzle(*printer);
        cards.push_back(json{{"catalogId", printer->id},
                             {"deviceId", ""},
                             {"name", printer->display_name()},
                             {"subline", printer->build_volume},
                             {"picture", picture_data_url(printer->picture)},
                             {"action", printers.size() == 1 ? "add" : "choose"}});
        found.push_back(identified_json(*printer, nozzle));
    }
    m_blocks.push_back(json{{"id", id}, {"seq", m_blocks.size() + 1}, {"afterMessageId", message_id}, {"kind", "printers"},
                            {"printers", std::move(cards)}});
    if (stated)
        m_stated_nozzle[id] = wanted;

    if (printers.size() == 1)
        show_proposal(*printers.front(), stated ? wanted : assumed_nozzle(*printers.front()), stated, id, nullptr);
    else
        // Nothing is settled while two printers are still on the table.
        clear_proposal();
    return json{{"printers", std::move(found)}};
}

json PrinterConversation::change(const json& arguments, const std::string& message_id, std::optional<ToolError>& error)
{
    if (m_mode != ConversationMode::Change || m_printer.name.empty()) {
        error = ToolError{"no_printer", "This session is not about a printer the person owns."};
        return {};
    }
    const SavedPrinter before = m_printer;

    ChangePrinterRequest request;
    request.name = m_printer.name;
    if (said_nozzle(arguments))
        request.nozzle = arguments["nozzle"].get<double>();
    if (arguments.contains("spools"))
        request.spools = spools_of(arguments["spools"]);
    if (!request.nozzle && !request.spools) {
        error = ToolError{"nothing_to_change", "Name the nozzle or the spools that changed."};
        return {};
    }

    SavedPrinter      changed;
    const std::string problem = m_backend.change_printer(request, changed);
    if (!problem.empty()) {
        const std::vector<SavedPrinter> saved = m_backend.saved_printers();
        const bool gone = std::none_of(saved.begin(), saved.end(), [this](const SavedPrinter& printer) { return printer.name == m_printer_name; });
        error = ToolError{gone ? "printer_gone" : "change_failed", problem};
        return {};
    }

    read_saved_printer();
    json         what = json::array();
    std::string  said;
    UndoRecord   undo;
    if (request.nozzle && before.nozzle != m_printer.nozzle) {
        what.push_back(json{{"field", "nozzle"}, {"before", before.nozzle}, {"after", m_printer.nozzle}});
        m_nozzle.provenance = "changed";
        undo.nozzle         = before.nozzle;
        said                = "Nozzle set to " + nozzle_text(m_printer.nozzle);
    }
    if (request.spools && !same_spools(before.spools, m_printer.spools)) {
        what.push_back(json{{"field", "spools"}, {"before", spools_json(before.spools)}, {"after", spools_json(m_printer.spools)}});
        m_filament.provenance = "changed";
        undo.spools           = before.spools;
        said                  = said.empty() ? std::string("Spools updated") : said + ", spools updated";
    }

    // Undo names the exact change, so it applies directly: the latest change
    // is the one it reverses, and an older one's row goes.
    if (m_undo)
        m_blocks.erase(std::remove_if(m_blocks.begin(), m_blocks.end(),
                                      [this](const json& entry) { return entry.value("id", std::string()) == m_undo->block_id; }),
                       m_blocks.end());
    m_undo.reset();
    if (!what.empty()) {
        undo.block_id = next_block_id();
        m_blocks.push_back(json{{"id", undo.block_id}, {"seq", m_blocks.size() + 1}, {"afterMessageId", message_id},
                                {"kind", "undo"}, {"text", said}});
        m_undo = std::move(undo);
    }
    m_host.printers_changed();
    return json{{"state", "applied"}, {"changed", std::move(what)}, {"printer", printer_json()}};
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
    else if (action == "reject")
        reject_proposal();
    else if (action == "network_pick")
        use_network_printer(id);
    else if (action == "candidate_pick")
        choose_candidate(id, payload.value("blockId", std::string()));
    else if (action == "undo")
        undo_change(id);
    return true;
}

void PrinterConversation::add_proposed_printer(const std::string& access_code)
{
    if (!m_proposal.valid)
        return;

    AddPrinterRequest request;
    request.vendor_id = m_proposal.vendor_id;
    request.model_id  = m_proposal.model_id;
    request.variant   = m_proposal.variant;
    request.material  = m_proposal.material;
    request.name      = m_proposal.model_name;
    request.device_id = m_proposal.device_id;
    // The code goes from the panel's own field to the app and nowhere else;
    // the thread records only that there was one.
    if (!access_code.empty() && !m_proposal.device_id.empty()) {
        request.access_code = access_code;
        m_host.post_note("An access code was entered.");
    }

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
        m_host.post_note("The person chose the network printer " + device_id + ", but it is no longer on the network.");
        m_host.session_changed();
        m_host.start_turn();
        return;
    }

    const CatalogPrinter* printer = model_of(*found);
    if (printer == nullptr) {
        // Only the model can ask what it is.
        m_host.post_note("The person chose the network printer " + found->stable_id + ", which reports the model id \"" +
                         found->device_model_id + "\"; nothing in the printer list has that model id.");
        m_host.session_changed();
        m_host.start_turn();
        return;
    }

    // What the printer reported is settled; only what it did not say is
    // assumed.
    const bool   reported = found->nozzle_diameter > 0. && ships(printer->nozzles, found->nozzle_diameter);
    const double nozzle   = reported ? found->nozzle_diameter : assumed_nozzle(*printer);
    std::string  event    = "The person chose the network printer " + found->stable_id + ", a " + printer->display_name();
    event += reported ? " that reports a " + nozzle_text(nozzle) + " nozzle." :
                        "; it did not report its nozzle, so its card assumes " + nozzle_text(nozzle) + ".";
    const std::string note = m_host.post_note(event);

    std::string subline = nozzle_text(nozzle) + " nozzle";
    const std::vector<PrinterSpool> loaded = reported_spools(*found);
    if (!loaded.empty())
        subline += " · " + spool_summary(found->ams_name, loaded);
    subline += reported ? " · read from the printer just now" : "";

    collapse_cards();
    const std::string id = next_block_id();
    m_blocks.push_back(json{{"id", id},
                            {"seq", m_blocks.size() + 1},
                            {"afterMessageId", note},
                            {"kind", "printers"},
                            {"printers", json::array({json{{"catalogId", printer->id},
                                                           {"deviceId", found->stable_id},
                                                           {"name", printer->display_name()},
                                                           {"subline", subline},
                                                           {"picture", picture_data_url(printer->picture)},
                                                           {"action", "add"}}})}});
    show_proposal(*printer, nozzle, reported, id, &*found);
    m_host.session_changed();
}

void PrinterConversation::choose_candidate(const std::string& catalog_id, const std::string& block_id)
{
    const CatalogPrinter* chosen = catalog_entry(catalog_id);
    if (chosen == nullptr)
        return;
    json* card = block(block_id);
    if (card == nullptr) {
        // An older page names no card: the newest one with this printer.
        for (json& entry : m_blocks)
            if (entry.value("kind", std::string()) == "printers")
                for (const json& printer : entry.value("printers", json::array()))
                    if (printer.value("catalogId", std::string()) == catalog_id)
                        card = &entry;
    }
    if (card == nullptr)
        return;

    const std::string card_id = card->value("id", std::string());
    const auto        stated  = m_stated_nozzle.find(card_id);
    const double      nozzle  = stated != m_stated_nozzle.end() ? stated->second : assumed_nozzle(*chosen);

    // The card keeps only the printer chosen, now with Add.
    for (json& printer : (*card)["printers"])
        if (printer.value("catalogId", std::string()) == catalog_id) {
            printer["action"] = "add";
            (*card)["printers"] = json::array({printer});
            break;
        }
    show_proposal(*chosen, nozzle, stated != m_stated_nozzle.end(), card_id, nullptr);

    std::vector<std::string> assumed{"a " + nozzle_text(nozzle) + " nozzle"};
    if (!chosen->default_plate.empty())
        assumed.push_back("the " + chosen->default_plate);
    if (!m_proposal.material.empty())
        assumed.push_back(m_proposal.material);
    std::string event = "The person chose " + chosen->display_name() + ". Its card offers Add, assuming " + listed(assumed);
    if (chosen->default_plate.empty())
        event += "; the profile names no plate";
    if (m_proposal.material.empty())
        event += "; the profile names no filament";
    m_host.post_note(event + ".");
    m_host.session_changed();
}

void PrinterConversation::reject_proposal()
{
    if (!m_proposal.valid)
        return;
    const std::string name = m_proposal.model_name;
    if (json* card = block(m_proposal.block_id))
        (*card)["collapsed"] = true;
    clear_proposal();
    m_host.post_note("The person said " + name + " is not their printer.");
    m_host.session_changed();
    // Only the model can decide what to ask next.
    m_host.start_turn();
}

void PrinterConversation::undo_change(const std::string& block_id)
{
    if (!m_undo || m_undo->block_id != block_id || m_mode != ConversationMode::Change)
        return;

    ChangePrinterRequest request;
    request.name   = m_printer.name;
    request.nozzle = m_undo->nozzle;
    request.spools = m_undo->spools;

    SavedPrinter      changed;
    const std::string problem = m_backend.change_printer(request, changed);
    if (!problem.empty()) {
        m_host.post_note("Undo did not go through: " + problem);
        m_host.session_changed();
        return;
    }

    m_blocks.erase(std::remove_if(m_blocks.begin(), m_blocks.end(),
                                  [&block_id](const json& entry) { return entry.value("id", std::string()) == block_id; }),
                   m_blocks.end());
    m_undo.reset();
    read_saved_printer();

    std::vector<std::string> back;
    if (request.nozzle)
        back.push_back("the nozzle is " + nozzle_text(m_printer.nozzle) + " again");
    if (request.spools)
        back.push_back("the spools are " + spools_text(m_printer.spools) + " again");
    m_host.post_note("The person undid the change: " + listed(back) + ".");
    m_host.printers_changed();
    m_host.session_changed();
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
