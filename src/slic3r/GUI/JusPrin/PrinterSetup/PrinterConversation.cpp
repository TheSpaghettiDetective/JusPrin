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

// The Add instructions carry every printer, about 28 KB; well above that is
// not a prompt the page meant to send.
constexpr std::size_t kInstructionsLimit = 256 * 1024;

// A note or the opening is a sentence or two; a page that sends more has a
// bug, and none of it reaches the thread.
constexpr std::size_t kNoteLimit = 2 * 1024;

// The note the page wrote for a tap, or empty when it sent none it may.
std::string page_note(const json& payload)
{
    const auto note = payload.find("note");
    if (note == payload.end() || !note->is_string())
        return {};
    const std::string text = note->get<std::string>();
    return text.size() <= kNoteLimit ? text : std::string();
}

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

std::vector<PrinterSpool> reported_spools(const DiscoveredPrinter& device)
{
    std::vector<PrinterSpool> spools;
    for (const DiscoveredPrinter::Spool& spool : device.spools)
        spools.push_back(PrinterSpool{spool.name, spool.material, spool.colour});
    return spools;
}

// A nozzle the printer reported is settled only when its model ships it.
bool reports_nozzle(const DiscoveredPrinter& device, const CatalogPrinter& printer)
{
    return device.nozzle_diameter > 0. && ships(printer.nozzles, device.nozzle_diameter);
}

} // namespace

PrinterConversation::PrinterConversation(IPrinterBackend& backend, IConversationHost& host)
    : m_backend(backend), m_host(host)
{}

std::vector<std::string> PrinterConversation::session_tools(ConversationMode mode)
{
    if (mode == ConversationMode::Connect)
        return {};
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
    // A new session's page writes its instructions again.
    m_instructions.clear();
    m_network.clear();
    m_opened = false;
    m_added.clear();
    m_manual_applied = false;
    m_connection_name.clear();
    m_connection_view = nullptr;

    m_facts = {};
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

    if (m_mode == ConversationMode::Connect) {
        m_connection_name = printer_name;
        m_backend.prepare_connection(printer_name);
        refresh_connection();
    } else if (m_mode == ConversationMode::Change)
        read_saved_printer();
    else {
        refresh_network();
        // A photo is the fastest way in, and the printers already on the
        // network are the fastest of all. Both belong under the opening,
        // where the person is deciding what to say.
        m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", ""}, {"kind", "tip"}});
        if (!m_network.empty()) {
            json found = json::array();
            for (const DiscoveredPrinter& printer : m_network) {
                json entry{{"deviceId", printer.stable_id},
                           {"name", printer.name},
                           {"serial", printer.stable_id},
                           {"online", printer.connected}};
                // What "Use this" would put on the card, so the page can say
                // so to the model when it is tapped.
                if (const CatalogPrinter* model = model_of(printer)) {
                    const bool reported = reports_nozzle(printer, *model);
                    entry["match"] = json{{"name", model->display_name()},
                                          {"nozzle", reported ? printer.nozzle_diameter : assumed_nozzle(*model)},
                                          {"reported", reported}};
                }
                found.push_back(std::move(entry));
            }
            m_blocks.push_back(json{{"id", next_block_id()},
                                    {"seq", m_blocks.size() + 1},
                                    {"afterMessageId", ""},
                                    {"kind", "network"},
                                    {"printers", std::move(found)}});
        }
    }
    m_host.session_changed();
}

void PrinterConversation::post_opening(const std::string& text)
{
    // The first line is the page's to write and the app's to post, once: a
    // reloaded page asks again, and the thread already has it.
    if (m_opened || text.empty() || text.size() > kNoteLimit)
        return;
    m_opened                     = true;
    const std::string message_id = m_host.post_opening(text);
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

    m_facts         = {};
    m_facts.printer = m_printer.name.empty() ? m_printer_name : m_printer.name;
    m_facts.nozzle  = m_printer.nozzle;
    // The plate a printer is set up with is a starting point rather than a
    // reading: it is the plate its profile ships with until a project says
    // otherwise.
    m_facts.plate            = m_printer.plate;
    m_facts.plate_provenance = "assumed";
    m_facts.ams              = m_printer.ams;
    m_facts.spools           = m_printer.spools;
}

void PrinterConversation::refresh_network() { m_network = m_backend.network_printers(); }

json PrinterConversation::context_json() const
{
    json context = json::object();
    if (m_mode == ConversationMode::Add) {
        json printers = json::array();
        for (const CatalogPrinter& printer : m_backend.catalog())
            printers.push_back(json::array({printer.id, printer.display_name(), printer.build_volume}));
        json network = json::array();
        for (const DiscoveredPrinter& found : m_network)
            network.push_back(json{{"name", found.name}, {"serial", found.stable_id}});
        context["printers"] = std::move(printers);
        context["network"]  = std::move(network);
    } else {
        json nozzles = json::array();
        for (double size : m_printer.nozzles)
            nozzles.push_back(size);
        context["printer"] = json{{"name", m_printer.name},
                                  {"model", m_printer.model},
                                  {"nozzle", m_printer.nozzle},
                                  {"nozzles", std::move(nozzles)},
                                  {"spools", spools_json(m_printer.spools)},
                                  {"connected", m_printer.connected}};
    }
    return context;
}

Agent::AgentSessionProfile PrinterConversation::profile() const
{
    Agent::AgentSessionProfile profile;
    // The tools are the app's to decide, one per mode; the words are the
    // page's (printerInstructions.ts), sent with printer_instructions.
    profile.tool_names   = m_added.empty() && m_connection_name.empty() ? session_tools(m_mode) : std::vector<std::string>();
    profile.instructions = m_instructions;
    // This conversation is about a machine, not about the open project, and
    // what the person taps on its cards reaches the model as the app's notes.
    profile.include_workspace = false;
    profile.notes_in_context  = true;
    return profile;
}

json PrinterConversation::state_json() const
{
    json added = json::array();
    for (const auto& printer : m_added)
        added.push_back(json{{"name", printer.name}, {"model", printer.model}, {"connected", printer.connected}});
    const json facts{{"printer", json{{"name", m_facts.printer}, {"provenance", m_facts.printer_provenance}}},
                     {"nozzle", json{{"size", m_facts.nozzle}, {"provenance", m_facts.nozzle_provenance}}},
                     {"plate", json{{"name", m_facts.plate}, {"provenance", m_facts.plate_provenance}}},
                     {"filament", json{{"preset", m_facts.filament_preset},
                                       {"ams", m_facts.ams},
                                       {"spools", spools_json(m_facts.spools)},
                                       {"provenance", m_facts.filament_provenance}}}};
    return json{{"mode", m_mode == ConversationMode::Add ? "add" : m_mode == ConversationMode::Connect ? "connect" : "change"},
                {"facts", facts},
                {"blocks", m_blocks},
                // A printer is on its card: Add saves it, and it can be refused.
                {"canAdd", m_proposal.valid},
                // A printer found on the network can be kept connected; its
                // code goes from this field to the app, never into the chat.
                {"accessCode", false},
                {"added", std::move(added)},
                {"manualApplied", m_manual_applied},
                {"connection", m_connection_view},
                // The facts the page's instructions for the model state.
                {"context", context_json()}};
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
    m_proposal = {};
    m_facts    = {};
}

json PrinterConversation::card_json(const CatalogPrinter& printer, double nozzle, const std::string& action,
                                    const DiscoveredPrinter* device) const
{
    json card{{"catalogId", printer.id},
              {"deviceId", device != nullptr ? device->stable_id : std::string()},
              {"name", printer.display_name()},
              {"buildVolume", printer.build_volume},
              {"picture", picture_data_url(printer.picture)},
              {"action", action},
              {"assumed", json{{"nozzle", nozzle}, {"plate", printer.default_plate}, {"filament", printer.filament_for(nozzle)}}}};
    if (device != nullptr)
        card["device"] = json{{"nozzle", nozzle},
                              {"ams", device->ams_name},
                              {"spools", spools_json(reported_spools(*device))},
                              {"reported", reports_nozzle(*device, printer)}};
    return card;
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

    m_facts                   = {};
    m_facts.printer           = printer.display_name();
    m_facts.nozzle            = nozzle;
    m_facts.nozzle_provenance = nozzle_stated ? "settled" : "assumed";
    m_facts.plate             = printer.default_plate;
    m_facts.plate_provenance  = "assumed";
    // What the printer reported it has loaded is settled; otherwise the card
    // assumes the profile's filament.
    const std::vector<PrinterSpool> loaded = device != nullptr ? reported_spools(*device) : std::vector<PrinterSpool>();
    m_facts.filament_preset = filament;
    if (!loaded.empty()) {
        m_facts.ams    = device->ams_name;
        m_facts.spools = loaded;
    } else
        m_facts.filament_provenance = "assumed";
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
            return ToolError{"unknown_nozzle", "JusPrin supports " + sizes_text(now->nozzles) + " nozzles for " + model + ", but not " +
                                                   nozzle_text(nozzle) + ". Ask the person to check the nozzle marking or packaging; do not substitute a size."};
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
    if (!m_added.empty() || !m_connection_name.empty()) {
        result.error = ToolError{"setup_finished", "The printer is saved. Connection is handled by the person's connection controls."};
        return result;
    }
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
                error = ToolError{"unknown_nozzle", "JusPrin supports " + sizes_text(printer->nozzles) +
                                                        " nozzles for " + printer->display_name() + ", but not " + nozzle_text(wanted) +
                                                        ". Ask the person to check the nozzle marking or packaging; do not substitute a size."};
                return {};
            }

    // The newest answer is the one to act on; earlier cards fold away.
    collapse_cards();
    const std::string id    = next_block_id();
    json              cards = json::array();
    json              found = json::array();
    for (const CatalogPrinter* printer : printers) {
        const double nozzle = stated ? wanted : assumed_nozzle(*printer);
        cards.push_back(card_json(*printer, nozzle, printers.size() == 1 ? "add" : "choose", nullptr));
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
    json       what          = json::array();
    json       changed_facts = json::object();
    UndoRecord undo;
    if (request.nozzle && before.nozzle != m_printer.nozzle) {
        what.push_back(json{{"field", "nozzle"}, {"before", before.nozzle}, {"after", m_printer.nozzle}});
        changed_facts["nozzle"]   = json{{"before", before.nozzle}, {"after", m_printer.nozzle}};
        m_facts.nozzle_provenance = "changed";
        undo.nozzle               = before.nozzle;
    }
    if (request.spools && !same_spools(before.spools, m_printer.spools)) {
        what.push_back(json{{"field", "spools"}, {"before", spools_json(before.spools)}, {"after", spools_json(m_printer.spools)}});
        changed_facts["spools"] = json{{"before", spools_json(before.spools)}, {"after", spools_json(m_printer.spools)}};
        m_facts.filament_provenance = "changed";
        undo.spools                 = before.spools;
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
                                {"kind", "undo"}, {"changed", std::move(changed_facts)}});
        m_undo = std::move(undo);
    }
    m_host.printers_changed();
    return json{{"state", "applied"}, {"changed", std::move(what)}, {"printer", printer_json()}};
}

// -- Taps on the panel's own cards -------------------------------------------

bool PrinterConversation::handle_page_message(const std::string& type, const json& payload)
{
    if (type != "printer_action" && type != "printer_instructions" && type != "printer_opening")
        return false;

    if (type == "printer_instructions") {
        // Bounded: the page's words become every request's system prompt.
        const std::string text = payload.value("text", std::string());
        if (text.size() <= kInstructionsLimit && text != m_instructions) {
            m_instructions = text;
            m_host.profile_changed();
        }
        return true;
    }
    if (type == "printer_opening") {
        post_opening(payload.value("text", std::string()));
        return true;
    }
    const std::string action = payload.value("action", std::string());
    const std::string id     = payload.value("id", std::string());
    const std::string note   = page_note(payload);
    if (action == "close")
        m_host.close_panel(m_added.size() == 1 ? &m_facts : nullptr);
    else if (action == "connection_refresh") {
        refresh_connection();
        m_host.session_changed();
    } else if (action == "connect") {
        const bool allowed = (m_mode != ConversationMode::Add && id == m_printer_name) ||
            std::any_of(m_added.begin(), m_added.end(), [&](const SavedPrinter& p) { return p.name == id; });
        if (!allowed)
            return true;
        m_connection_name = id;
        m_backend.prepare_connection(id);
        refresh_connection();
        m_host.session_changed();
    } else if (action == "connection_sign_in" && !m_connection_name.empty()) {
        const auto info = m_backend.connection(m_connection_name);
        if (info.provider == "bambu" && info.state != "unavailable" && !info.signed_in)
            m_backend.sign_in_to_bambu();
        refresh_connection();
        m_host.session_changed();
    } else if (action == "connection_start" && !m_connection_name.empty()) {
        const auto problem = m_connection_view.value("provider", std::string()) == "host" ?
            m_backend.connect_host(m_connection_name, payload.value("hostType", std::string()),
                                   payload.value("address", std::string()), payload.value("accessCode", std::string())) :
            m_backend.connect_printer(m_connection_name, payload.value("deviceId", std::string()),
                                      payload.value("accessCode", std::string()));
        refresh_connection();
        if (!problem.empty()) {
            m_connection_view["state"] = "failed";
            m_connection_view["message"] = problem;
        }
        m_host.printers_changed();
        m_host.session_changed();
    }
    else if ((!m_added.empty() || !m_connection_name.empty()) && action != "manual_setup")
        return true;
    else if (action == "manual_setup") {
        if (!m_connection_name.empty()) {
            m_backend.open_printer_settings(m_connection_name);
            refresh_connection();
            m_host.session_changed();
        } else if (m_mode == ConversationMode::Add) {
            if (!m_added.empty())
                return true;
            // The wizard takes over from here; whatever it installed is on
            // the printer list the panel returns to.
            const auto result = m_backend.run_manual_setup();
            m_added = result.added;
            m_manual_applied = result.applied;
            if (m_added.size() == 1) {
                m_facts = {};
                m_facts.printer = m_added.front().name;
                m_facts.nozzle = m_added.front().nozzle;
            }
            if (!m_added.empty())
                m_proposal = {};
            m_host.printers_changed();
            m_host.session_changed();
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
        add_proposed_printer();
    else if (action == "reject")
        reject_proposal(note);
    else if (action == "network_pick")
        use_network_printer(id, note);
    else if (action == "candidate_pick")
        choose_candidate(id, payload.value("blockId", std::string()), note);
    else if (action == "undo")
        undo_change(id, note);
    return true;
}

void PrinterConversation::add_proposed_printer()
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
    // Adding never authenticates. Credentials belong only to the subsequent
    // connection gesture, so a failed connection cannot repeat this save.

    SavedPrinter      added;
    const std::string problem = m_backend.add_printer(request, added);
    if (!problem.empty()) {
        // One error at a time, in the thread, with what to do about it.
        m_host.post_note(problem);
        m_host.session_changed();
        return;
    }
    // Keep the receipt open; optional connection targets this saved identity.
    if (added.name.empty())
        throw std::logic_error("A successful add did not identify its saved printer");
    m_added = {added};
    m_facts.printer = added.name;
    m_proposal = {};
    m_host.printers_changed();
    m_host.session_changed();
}

void PrinterConversation::refresh_connection()
{
    if (m_connection_name.empty())
        return;
    const auto info = m_backend.connection(m_connection_name);
    json candidates = json::array();
    for (const auto& candidate : info.candidates)
        candidates.push_back(json{{"id", candidate.id}, {"name", candidate.name}, {"address", candidate.address}});
    m_connection_view = json{{"name", m_connection_name}, {"provider", info.provider}, {"state", info.state},
                            {"message", info.message}, {"deviceId", info.device_id}, {"candidates", candidates},
                            {"address", info.address}, {"hostType", info.host_type}, {"signedIn", info.signed_in}};
}

void PrinterConversation::use_network_printer(const std::string& device_id, const std::string& note)
{
    refresh_network();
    const auto found = std::find_if(m_network.begin(), m_network.end(),
                                    [&device_id](const DiscoveredPrinter& printer) { return printer.stable_id == device_id; });
    // The page wrote its note from the printer as the list showed it; these
    // two are what the app finds out only now.
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
    const bool        reported = reports_nozzle(*found, *printer);
    const double      nozzle   = reported ? found->nozzle_diameter : assumed_nozzle(*printer);
    const std::string anchor   = note.empty() ? std::string() : m_host.post_note(note);

    collapse_cards();
    const std::string id = next_block_id();
    m_blocks.push_back(json{{"id", id},
                            {"seq", m_blocks.size() + 1},
                            {"afterMessageId", anchor},
                            {"kind", "printers"},
                            {"printers", json::array({card_json(*printer, nozzle, "add", &*found)})}});
    show_proposal(*printer, nozzle, reported, id, &*found);
    m_host.session_changed();
    // Selection needs conversational guidance now that no summary is pinned.
    m_host.start_turn();
}

void PrinterConversation::choose_candidate(const std::string& catalog_id, const std::string& block_id, const std::string& note)
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
    // The page wrote it from what this card assumes, which is what Add now
    // saves.
    if (!note.empty())
        m_host.post_note(note);
    m_host.session_changed();
    m_host.start_turn();
}

void PrinterConversation::reject_proposal(const std::string& note)
{
    if (!m_proposal.valid)
        return;
    if (json* card = block(m_proposal.block_id))
        (*card)["collapsed"] = true;
    clear_proposal();
    if (!note.empty())
        m_host.post_note(note);
    m_host.session_changed();
    // Only the model can decide what to ask next.
    m_host.start_turn();
}

void PrinterConversation::undo_change(const std::string& block_id, const std::string& note)
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
    // The page wrote it from the undo row, which states exactly what is put
    // back.
    if (!note.empty())
        m_host.post_note(note);
    m_host.printers_changed();
    m_host.session_changed();
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
