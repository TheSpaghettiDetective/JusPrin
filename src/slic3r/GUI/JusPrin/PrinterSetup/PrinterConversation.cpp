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
using Result = Agent::ToolExecutionCoordinator::ExtensionResult;

namespace {

// At most three candidates: more is a list to read rather than an answer.
constexpr std::size_t kMaximumCandidates = 3;

// The Add instructions carry every printer, about 28 KB; well above that is
// not a prompt the page meant to send.
constexpr std::size_t kInstructionsLimit = 256 * 1024;

// The opening is a sentence or two; a page that sends more has a bug, and
// none of it reaches the thread.
constexpr std::size_t kNoteLimit = 2 * 1024;

// How often a Bambu Lab connection attempt is looked at while it settles.
constexpr auto kConnectionLook = std::chrono::seconds(1);

Result ok(const json& output)
{
    Result result;
    result.handled     = true;
    result.result_json = output.dump();
    return result;
}

// A precondition the model can read and act on.
Result refuse(std::string code, std::string message)
{
    Result result;
    result.handled = true;
    result.error   = ToolError{std::move(code), std::move(message)};
    return result;
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

// "0.2, 0.4, 0.6 and 0.8 mm"
std::string sizes_text(const std::vector<double>& nozzles)
{
    std::string text;
    for (std::size_t i = 0; i < nozzles.size(); ++i)
        text += (i == 0 ? "" : i + 1 == nozzles.size() ? " and " : ", ") + number_text(nozzles[i]);
    return text + " mm";
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

Result unknown_nozzle(const std::vector<double>& nozzles, const std::string& model, double nozzle)
{
    return refuse("unknown_nozzle", "JusPrin supports " + sizes_text(nozzles) + " nozzles for " + model + ", but not " +
                                        number_text(nozzle) + " mm. Ask the person to check the nozzle marking or packaging; "
                                        "do not substitute a size.");
}

double assumed_nozzle(const CatalogPrinter& printer)
{
    return printer.nozzles.empty() || ships(printer.nozzles, 0.4) ? 0.4 : printer.nozzles.front();
}

std::string picture_data_url(const std::string& path)
{
    if (path.empty())
        return {};
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    std::ostringstream bytes;
    bytes << file.rdbuf();
    const std::string data = bytes.str();
    return data.empty() ? std::string() : "data:image/png;base64," + base64_encode(data);
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
    return spools_json(lhs) == spools_json(rhs);
}

// Only LAN mode: a printer reached through a Bambu account is not offered.
json connection_json(const PrinterConnectionInfo& info)
{
    json candidates = json::array();
    for (const ConnectionCandidate& candidate : info.candidates)
        if (candidate.lan_mode)
            candidates.push_back(json{{"deviceId", candidate.id}, {"name", candidate.name}, {"address", candidate.address}});
    return json{{"provider", info.provider},         {"state", info.state},         {"message", info.message},
                {"candidates", std::move(candidates)}, {"address", info.address}, {"hostType", info.host_type},
                {"nozzleMismatch", info.nozzle_mismatch}};
}

} // namespace

PrinterConversation::PrinterConversation(IPrinterBackend& backend, IConversationHost& host)
    : m_backend(backend), m_host(host)
{}

std::vector<std::string> PrinterConversation::session_tools()
{
    return {"printer_add",      "printer_change",            "printer_connect",      "printer_connection_status",
            "printer_identify", "printer_manual_connection", "printer_manual_setup", "printer_setup_finish"};
}

void PrinterConversation::start(ConversationMode mode, const std::string& printer_name)
{
    m_mode         = mode;
    m_printer_name = mode == ConversationMode::Add ? std::string() : printer_name;
    m_blocks       = json::array();
    m_next_block   = 1;
    m_opened       = false;
    // A new session's page writes its instructions again.
    m_instructions.clear();
    m_network.clear();
    m_connecting.clear();
    m_prepared.clear();
    m_added.clear();

    // A printer this app has not saved cannot be changed, and the header's
    // menu can name one: a system profile is selected until the person adds
    // a printer of their own. Adding one is what there is to do.
    if (m_mode == ConversationMode::Change && saved(m_printer_name).name.empty()) {
        m_mode = ConversationMode::Add;
        m_printer_name.clear();
    }

    if (m_mode == ConversationMode::Add) {
        m_network = m_backend.network_printers();
        // A photo is the fastest way in, and the printers already on the
        // network are worth naming. Both sit under the opening, where the
        // person is deciding what to say.
        m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", ""}, {"kind", "tip"}});
        if (!m_network.empty()) {
            json found = json::array();
            for (const DiscoveredPrinter& printer : m_network)
                found.push_back(json{{"name", printer.name}, {"serial", printer.stable_id}, {"online", printer.connected}});
            m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", ""},
                                    {"kind", "network"}, {"printers", std::move(found)}});
        }
    }
    m_host.session_changed();
}

SavedPrinter PrinterConversation::saved(const std::string& name) const
{
    for (const SavedPrinter& printer : m_backend.saved_printers())
        if (!name.empty() && printer.name == name)
            return printer;
    return {};
}

// printer_change names the saved printer it changes, and the model sometimes
// gives a printer's name on the network instead.
std::string PrinterConversation::unknown_printer_message(const std::string& name) const
{
    return "No saved printer is named \"" + name + "\"." +
           (m_printer_name.empty() ? std::string() : " The saved printer this conversation is about is \"" + m_printer_name + "\".");
}

const CatalogPrinter* PrinterConversation::catalog_entry(const std::string& id) const
{
    const std::vector<CatalogPrinter>& catalog = m_backend.catalog();
    const auto entry = std::find_if(catalog.begin(), catalog.end(), [&id](const CatalogPrinter& printer) { return printer.id == id; });
    return entry == catalog.end() ? nullptr : &*entry;
}

json PrinterConversation::printer_json(const SavedPrinter& printer) const
{
    return json{{"name", printer.name},
                {"model", printer.model},
                {"nozzle", printer.nozzle},
                {"nozzles", printer.nozzles},
                {"spools", spools_json(printer.spools)},
                {"connected", printer.connected}};
}

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
    }
    if (const SavedPrinter printer = saved(m_printer_name); !printer.name.empty()) {
        context["printer"]             = printer_json(printer);
        context["printer"]["provider"] = printer.vendor_id == "BBL" ? "bambu" : "host";
    }
    return context;
}

Agent::AgentSessionProfile PrinterConversation::profile() const
{
    Agent::AgentSessionProfile profile;
    // The tools are the app's to decide; the words are the page's
    // (printerInstructions.ts), sent with printer_instructions.
    profile.tool_names   = session_tools();
    profile.instructions = m_instructions;
    // This conversation is about a machine, not about the open project, and
    // what the app does on its own reaches the model as notes.
    profile.include_workspace          = false;
    profile.notes_in_context           = true;
    profile.reply_cancels_pending_card = true;
    return profile;
}

json PrinterConversation::state_json() const
{
    return json{{"mode", m_mode == ConversationMode::Add ? "add" : m_mode == ConversationMode::Connect ? "connect" : "change"},
                {"printerName", m_printer_name},
                {"blocks", m_blocks},
                {"context", context_json()}};
}

// -- The session's tools ----------------------------------------------------

std::optional<ToolError> PrinterConversation::preflight_tool(ToolHandler handler, ToolActivity& activity) const
{
    if (handler != ToolHandler::PrinterConnect)
        return std::nullopt;
    json              arguments = json::parse(activity.arguments_json);
    const std::string name      = m_printer_name;
    if (saved(name).name.empty())
        return ToolError{"no_printer", "There is no saved printer to connect yet: add one first."};
    if (!m_connecting.empty())
        return ToolError{"connection_in_progress", "A connection to " + m_connecting + " is still being checked."};

    const PrinterConnectionInfo info = m_backend.connection(name);
    if (info.provider == "bambu") {
        // The person names the printer as its list shows it, so its name
        // finds it as surely as its id.
        const std::string device = arguments.value("deviceId", std::string());
        const auto found = std::find_if(info.candidates.begin(), info.candidates.end(), [&](const ConnectionCandidate& candidate) {
            return candidate.lan_mode && (candidate.id == device || candidate.name == device);
        });
        if (found == info.candidates.end()) {
            std::string listed;
            for (const ConnectionCandidate& candidate : info.candidates)
                if (candidate.lan_mode)
                    listed += (listed.empty() ? "" : ", ") + candidate.id + " (" + candidate.name + ")";
            return ToolError{"unknown_device", listed.empty() ? "No printer in LAN mode is on the network now; check again with printer_connection_status." :
                                                                "Pass the deviceId of one of these printers in LAN mode: " + listed + "."};
        }
        activity.title = "Connect to " + found->name;
        arguments["deviceId"] = found->id;
    } else if (info.provider == "host") {
        // The registry has already refused a hostType without an address; a
        // deviceId here names a Bambu Lab printer this one is not.
        if (!arguments.contains("hostType"))
            return ToolError{"address_needed", "Pass hostType and the address the person gave."};
        activity.title = "Connect to " + arguments["address"].get<std::string>();
    } else
        return ToolError{"connection_unavailable", info.message};
    // Which field the card asks for, and the printer it named: what runs
    // on Connect is what the card showed.
    arguments["provider"]    = info.provider;
    arguments["printerName"] = name;
    activity.arguments_json = arguments.dump();
    return std::nullopt;
}

Result PrinterConversation::execute_tool(ToolHandler handler, const ToolActivity& activity)
{
    const json arguments = json::parse(activity.arguments_json);
    switch (handler) {
    case ToolHandler::PrinterIdentify: return identify(arguments, activity.correlation_id);
    case ToolHandler::PrinterAdd: return add(arguments);
    case ToolHandler::PrinterChange: return change(arguments);
    case ToolHandler::PrinterConnectionStatus: return connection_status(m_printer_name);
    case ToolHandler::PrinterConnect: return connect(arguments, activity.action_id);
    case ToolHandler::PrinterManualSetup: return manual_setup();
    case ToolHandler::PrinterManualConnection: {
        const std::string name = m_printer_name;
        if (saved(name).name.empty())
            return refuse("no_printer", "There is no saved printer yet: add one first.");
        // A window beside the app, not a modal one: the person edits and
        // closes it in their own time.
        m_backend.open_printer_settings(name);
        m_host.printers_changed();
        m_host.session_changed();
        return ok(json{{"state", "opened"}});
    }
    case ToolHandler::PrinterSetupFinish:
        m_host.close_panel();
        return ok(json{{"state", "closed"}});
    default: return {};
    }
}

std::optional<json> PrinterConversation::tool_output(const ToolActivity& activity) const
{
    const std::vector<std::string> tools = session_tools();
    if (std::find(tools.begin(), tools.end(), activity.tool) == tools.end())
        return std::nullopt;
    if (activity.state == Agent::ToolState::Succeeded)
        return json::parse(activity.result_json);
    if (activity.state == Agent::ToolState::Failed && activity.error)
        return json{{"error", json{{"code", activity.error->code}, {"message", activity.error->message}}}};
    // The person tapped Cancel on the card, or wrote something instead.
    if (activity.state == Agent::ToolState::Rejected)
        return json{{"state", "cancelled"}};
    return std::nullopt;
}

Result PrinterConversation::identify(const json& arguments, const std::string& message_id)
{
    std::vector<const CatalogPrinter*> printers;
    for (const json& id : arguments.at("catalogIds")) {
        const CatalogPrinter* printer = catalog_entry(id);
        if (printer == nullptr)
            return refuse("unknown_printer", "\"" + id.get<std::string>() + "\" is not on the printer list; copy it exactly from the printer list.");
        if (std::find(printers.begin(), printers.end(), printer) == printers.end())
            printers.push_back(printer);
    }
    if (printers.size() > kMaximumCandidates)
        return refuse("too_many", "More than three fit. Ask one question that narrows it down instead.");
    const bool stated = said_nozzle(arguments);
    for (const CatalogPrinter* printer : printers)
        if (stated && !ships(printer->nozzles, arguments["nozzle"].get<double>()))
            return unknown_nozzle(printer->nozzles, printer->display_name(), arguments["nozzle"].get<double>());

    json cards = json::array();
    json found = json::array();
    const std::vector<SavedPrinter> mine = m_backend.saved_printers();
    for (const CatalogPrinter* printer : printers) {
        const double nozzle = stated ? arguments["nozzle"].get<double>() : assumed_nozzle(*printer);
        const bool   yours  = std::any_of(mine.begin(), mine.end(), [&](const SavedPrinter& p) { return p.model_id == printer->model_id; });
        cards.push_back(json{{"catalogId", printer->id}, {"name", printer->display_name()},
                             {"buildVolume", printer->build_volume}, {"picture", picture_data_url(printer->picture)}});
        found.push_back(json{{"catalogId", printer->id},
                             {"brand", printer->vendor_name},
                             {"model", printer->model_name},
                             {"buildVolume", printer->build_volume},
                             {"nozzles", printer->nozzles},
                             {"assumed", json{{"nozzle", nozzle},
                                              {"plate", nullable(printer->default_plate)},
                                              {"filament", nullable(printer->filament_for(nozzle))}}},
                             {"alreadyYours", yours}});
    }
    m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", message_id},
                            {"kind", "printers"}, {"printers", std::move(cards)}});
    m_host.session_changed();
    return ok(json{{"printers", std::move(found)}});
}

Result PrinterConversation::add(const json& arguments)
{
    const std::string     id      = arguments.at("catalogId");
    const CatalogPrinter* printer = catalog_entry(id);
    if (printer == nullptr)
        return refuse("unknown_printer", "\"" + id + "\" is not on the printer list; copy it exactly from the printer list.");
    const double nozzle = said_nozzle(arguments) ? arguments["nozzle"].get<double>() : assumed_nozzle(*printer);
    if (!ships(printer->nozzles, nozzle))
        return unknown_nozzle(printer->nozzles, printer->display_name(), nozzle);
    // A second yes to the same printer is the same printer, not another one.
    if (const auto added = m_added.find(id); added != m_added.end())
        return refuse("already_added", "This conversation already added " + printer->display_name() + " as \"" + added->second +
                                           "\". To change it, use printer_change. A second printer of the same model is added by "
                                           "opening Add printer again.");

    // Adding never authenticates: credentials belong to connecting, so a
    // failed connection cannot repeat this save.
    AddPrinterRequest request{printer->vendor_id,           printer->model_id,        number_text(nozzle),
                              printer->filament_for(nozzle), printer->display_name(), {}};
    SavedPrinter      added;
    if (const std::string problem = m_backend.add_printer(request, added); !problem.empty())
        return refuse("add_failed", problem);
    const SavedPrinter now = saved(added.name);
    if (now.name.empty())
        throw std::logic_error("A successful add did not save a printer");
    m_printer_name = now.name;
    m_added[id]    = now.name;
    m_host.printers_changed(now.name);
    m_host.session_changed();
    return ok(json{{"printer", printer_json(now)}});
}

Result PrinterConversation::change(const json& arguments)
{
    const std::string  name = arguments.at("printerName");
    const SavedPrinter now  = saved(name);
    if (now.name.empty())
        return refuse("unknown_printer", unknown_printer_message(name));
    const bool has_nozzle = said_nozzle(arguments);
    const bool has_spools = arguments.contains("spools");
    if (!has_nozzle && !has_spools)
        return refuse("nothing_to_change", "Name the nozzle or the spools that changed.");
    if (has_nozzle && !now.nozzles.empty() && !ships(now.nozzles, arguments["nozzle"].get<double>()))
        return unknown_nozzle(now.nozzles, now.model.empty() ? now.name : now.model, arguments["nozzle"].get<double>());

    ChangePrinterRequest request;
    request.name = name;
    if (has_nozzle && arguments["nozzle"].get<double>() != now.nozzle)
        request.nozzle = arguments["nozzle"].get<double>();
    if (has_spools && !same_spools(spools_of(arguments["spools"]), now.spools))
        request.spools = spools_of(arguments["spools"]);
    if (!request.nozzle && !request.spools)
        return refuse("nothing_to_change", "That is already how " + name + " is set up.");

    SavedPrinter changed;
    if (const std::string problem = m_backend.change_printer(request, changed); !problem.empty())
        return refuse("change_failed", problem);
    const SavedPrinter after = saved(name);
    json               what  = json::array();
    if (request.nozzle)
        what.push_back(json{{"field", "nozzle"}, {"before", now.nozzle}, {"after", after.nozzle}});
    if (request.spools)
        what.push_back(json{{"field", "spools"}, {"before", spools_json(now.spools)}, {"after", spools_json(after.spools)}});
    m_host.printers_changed();
    m_host.session_changed();
    return ok(json{{"changed", std::move(what)}, {"printer", printer_json(after)}});
}

Result PrinterConversation::connection_status(const std::string& name)
{
    if (saved(name).name.empty())
        return refuse("no_printer", "There is no saved printer to connect yet: add one first.");
    // For a Bambu Lab printer this starts looking for it on the network --
    // once: starting again switches the printer agent and restarts discovery,
    // so a model that checks again would never let it find anything.
    if (m_prepared != name) {
        m_backend.prepare_connection(name);
        m_prepared = name;
    }
    return ok(connection_json(m_backend.connection(name)));
}

Result PrinterConversation::connect(const json& arguments, const std::string& action_id)
{
    const std::string name = arguments.at("printerName");
    // Typed on the card, used here, and kept nowhere else.
    const std::string credential = m_host.take_credential(action_id).value_or(std::string());
    if (arguments.at("provider") == "bambu") {
        if (const std::string problem = m_backend.connect_printer(name, arguments.at("deviceId"), credential); !problem.empty())
            return ok(json{{"state", "failed"}, {"message", problem}});
        m_connecting = name;
        m_last_look  = {};
        return ok(json{{"state", "connecting"}, {"message", ""}});
    }
    const std::string problem = m_backend.connect_host(name, arguments.at("hostType"), arguments.at("address"), credential);
    m_host.printers_changed();
    if (!problem.empty())
        return ok(json{{"state", "failed"}, {"message", problem}});
    // connect_host tests the host and keeps the outcome for connection().
    const PrinterConnectionInfo info = m_backend.connection(name);
    return ok(json{{"state", info.state == "verified" ? "verified" : "failed"}, {"message", info.message}});
}

Result PrinterConversation::manual_setup()
{
    const ManualPrinterResult result = m_backend.run_manual_setup();
    json                      names  = json::array();
    for (const SavedPrinter& printer : result.added)
        names.push_back(printer.name);
    if (!result.added.empty()) {
        m_printer_name = result.added.front().name;
        m_host.printers_changed(m_printer_name);
        m_host.session_changed();
    }
    return ok(json{{"applied", result.applied}, {"added", std::move(names)}});
}

void PrinterConversation::tick(std::chrono::steady_clock::time_point now)
{
    if (m_connecting.empty() || now - m_last_look < kConnectionLook)
        return;
    m_last_look = now;
    const PrinterConnectionInfo info = m_backend.connection(m_connecting);
    if (info.state == "connecting")
        return;
    const std::string name = m_connecting;
    m_connecting.clear();
    m_host.post_note(info.state == "verified" ? "Connection to " + name + " verified." :
                                                "Connection to " + name + " failed: " + info.message);
    m_host.printers_changed();
    m_host.start_turn();
}

// -- The panel's own page --------------------------------------------------

bool PrinterConversation::handle_page_message(const std::string& type, const json& payload)
{
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
        // The first line is the page's to write and the app's to post, once:
        // a reloaded page asks again, and the thread already has it.
        const std::string text = payload.value("text", std::string());
        if (m_opened || text.empty() || text.size() > kNoteLimit)
            return true;
        m_opened                     = true;
        const std::string message_id = m_host.post_opening(text);
        for (json& entry : m_blocks)
            if (entry.value("afterMessageId", std::string()).empty())
                entry["afterMessageId"] = message_id;
        m_host.session_changed();
        return true;
    }
    if (type != "printer_action")
        return false;

    const std::string action = payload.value("action", std::string());
    if (action == "close")
        m_host.close_panel();
    else if (action == "manual_setup") {
        // The overflow menu's way to OrcaSlicer's own printer list; what it
        // added is news the model answers.
        const ManualPrinterResult result = m_backend.run_manual_setup();
        if (!result.added.empty()) {
            m_printer_name = result.added.front().name;
            m_host.post_note("The person added " + m_printer_name + " from OrcaSlicer's printer list.");
            m_host.printers_changed(m_printer_name);
            m_host.session_changed();
            m_host.start_turn();
        }
    } else if (action == "open_printer_settings" && !m_printer_name.empty()) {
        m_backend.open_printer_settings(m_printer_name);
        m_host.printers_changed();
        m_host.session_changed();
    }
    return true;
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
