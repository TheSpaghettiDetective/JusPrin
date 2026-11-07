#include "PrinterConversation.hpp"

#include "slic3r/GUI/JusPrin/Agent/AgentProtocol.hpp"
#include "slic3r/GUI/JusPrin/Support/Base64.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
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

// How often a connection attempt is looked at while it settles.
constexpr auto kConnectionLook = std::chrono::seconds(1);

// How long a printer_connection_status call listens for the network search,
// and how often the search is looked at after that. On Kenneth's P1S the first
// announcement came 8 to 40 seconds in, six times out of seven, and once after
// more than two minutes.
constexpr auto kSearchWait     = std::chrono::seconds(30);
constexpr auto kSearchLateLook = std::chrono::seconds(5);

const char* const kPortTaken = "The network search cannot hear printers: another app on this computer holds UDP port 2021, where they "
                              "announce themselves, so none can be found, whatever mode they are in. It may be another slicer. "
                              "Searching needs that app closed and then this app restarted.";
const char* const kNotListening = "The network search cannot hear printers: this app is not listening on UDP port 2021, where they "
                                 "announce themselves, so none can be found, whatever mode they are in. It takes that port only as "
                                 "it starts, so searching needs this app restarted.";

const char* const kPersonsChoice = "Connecting it is the person's choice, and they can prepare prints for it without a connection.";

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


std::string with_address(const ConnectionCandidate& candidate)
{
    return candidate.address.empty() ? candidate.name : candidate.name + " at " + candidate.address;
}

bool found_on_network(const PrinterConnectionInfo& info)
{
    return std::any_of(info.candidates.begin(), info.candidates.end(),
                       [](const ConnectionCandidate& candidate) { return !candidate.account_only; });
}

// A report holds neither an access code nor an API key. The form's credential
// never reaches the chat; this takes out what a person may type into the chat
// anyway, and a printer's serial, which a report has no need of: a key's own
// shape, and any run of six or more letters and digits with a digit in it. A
// code of letters alone reads as a word, which is why the person is shown the
// report and told to look.
std::string without_secrets(const std::string& text)
{
    static const std::regex key("sk-[A-Za-z0-9_-]{6,}");
    static const std::regex token("[A-Za-z0-9]{6,}");
    const std::string       no_keys = std::regex_replace(text, key, "[hidden]");
    std::string             out;
    std::size_t             from = 0;
    for (auto it = std::sregex_iterator(no_keys.begin(), no_keys.end(), token); it != std::sregex_iterator(); ++it) {
        const std::string word = it->str();
        out += no_keys.substr(from, it->position() - from);
        out += std::any_of(word.begin(), word.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) ? "[hidden]" : word;
        from = it->position() + word.size();
    }
    return out + no_keys.substr(from);
}

// What an issue on GitHub, which anyone can read, holds of the person's
// printer: neither the serial and address the app knows it by, nor anything
// that reads as an IP address, nor what without_secrets takes out.
std::string for_public(std::string text, const std::vector<std::string>& known)
{
    static const std::regex address("\\b(?:[0-9]{1,3}\\.){3}[0-9]{1,3}\\b");
    for (const std::string& value : known)
        for (std::size_t at = value.empty() ? std::string::npos : text.find(value); at != std::string::npos;
             at = text.find(value, at + 8))
            text.replace(at, value.size(), "[hidden]");
    return without_secrets(std::regex_replace(text, address, "[hidden]"));
}

std::string percent_encoded(const std::string& text)
{
    static const char* const digits = "0123456789ABCDEF";
    std::string              out;
    for (const unsigned char c : text)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~')
            out += static_cast<char>(c);
        else
            out += {'%', digits[c >> 4], digits[c & 15]};
    return out;
}

json without_secrets(const json& value)
{
    if (value.is_string())
        return without_secrets(value.get<std::string>());
    json out = value;
    if (value.is_structured())
        for (auto it = out.begin(); it != out.end(); ++it)
            *it = without_secrets(json(*it));
    return out;
}

// A printer the app offers to connect: in LAN mode, and not held back for a
// reason of the backend's.
bool offered(const ConnectionCandidate& candidate) { return candidate.lan_mode && candidate.reason.empty(); }

// A printer in cloud mode on this network: the one kind the person can
// change by turning LAN Only on.
bool in_cloud_mode(const ConnectionCandidate& candidate)
{
    return !candidate.lan_mode && !candidate.account_only && candidate.reason.empty();
}

// Why a printer is in `seen` rather than offered.
std::string seen_reason(const ConnectionCandidate& candidate)
{
    if (!candidate.reason.empty())
        return candidate.reason;
    return candidate.account_only ? "account_only" : "cloud_mode";
}

// A printer the app knows of and does not offer, and what it knows about it.
std::string not_offered_text(const ConnectionCandidate& candidate)
{
    const std::string reason = seen_reason(candidate);
    if (reason == "account_only")
        return candidate.name + " is on the person's Bambu account and has not been seen on the network.";
    if (reason == "cloud_mode")
        return with_address(candidate) + " was seen on the network with LAN Only off.";
    if (reason == "other_model")
        return with_address(candidate) + " is " +
               (candidate.detail.empty() ? std::string("a printer of another model") : "a " + candidate.detail) +
               ", not this printer's model.";
    if (reason == "linked_elsewhere")
        return with_address(candidate) + " is already the printer \"" + candidate.detail +
               "\" in this app, and a device connects to one saved printer.";
    if (reason == "other_device")
        return with_address(candidate) + " is a printer of this model, but not the one this printer was "
               "set up from (" + candidate.detail + ").";
    if (reason == "occupied")
        return with_address(candidate) + " is in LAN mode, but its bind state is not free (occupied), so it is not offered.";
    throw std::logic_error("A connection candidate has a reason this conversation does not know: " + reason);
}

// Only LAN mode can be connected. Every other printer the app heard of is
// reported apart, with the reason, so the model knows it was seen and is
// never told "found no printer" about one the app chose not to offer.
json connection_json(const PrinterConnectionInfo& info, json search)
{
    json candidates = json::array();
    json seen       = json::array();
    for (const ConnectionCandidate& candidate : info.candidates)
        if (offered(candidate))
            candidates.push_back(json{{"deviceId", candidate.id}, {"name", candidate.name}, {"address", candidate.address}});
        else {
            json entry{{"name", candidate.name}, {"address", candidate.address}, {"reason", seen_reason(candidate)}};
            if (!candidate.detail.empty())
                entry["detail"] = candidate.detail;
            seen.push_back(std::move(entry));
        }
    return json{{"provider", info.provider},         {"state", info.state},         {"message", info.message},
                {"candidates", std::move(candidates)}, {"seen", std::move(seen)},   {"search", std::move(search)},
                {"address", info.address},           {"hostType", info.host_type},  {"nozzleMismatch", info.nozzle_mismatch}};
}

} // namespace

PrinterConversation::PrinterConversation(IPrinterBackend& backend, IConversationHost& host)
    : m_backend(backend), m_host(host)
{}

PrinterConversation::~PrinterConversation() { abandon_connection("The person closed the printer chat before connecting was finished."); }

std::vector<std::string> PrinterConversation::settings_tools()
{
    return {"settings_apply_patch", "settings_get", "settings_preview_patch", "settings_search"};
}

std::vector<std::string> PrinterConversation::session_tools()
{
    return {"printer_add", "printer_bambu_connect", "printer_change", "printer_connection_status", "printer_host_connect",
            "printer_identify"};
}

void PrinterConversation::start(ConversationMode mode, const std::string& printer_name)
{
    // An attempt the last session left waiting -- its panel closed meanwhile
    // -- is dropped before this session can read, and save, its outcome.
    abandon_connection("The printer chat started over before connecting was finished.");
    m_mode         = mode;
    m_printer_name = mode == ConversationMode::Add ? std::string() : printer_name;
    m_blocks       = json::array();
    m_next_block   = 1;
    m_opened       = false;
    // A new session's page writes its instructions again.
    m_instructions.clear();
    m_network.clear();
    m_credential_action.clear();
    m_credential_arguments = json::object();
    m_prepared.clear();
    m_search.reset();
    m_status_call.reset();
    m_added.clear();
    m_needs_plugin = false;
    m_host_type.clear();
    m_failed_connections = 0;
    m_form.reset();

    // The session is the one it was opened for. Its opener names a printer
    // it has just read -- a saved one, or for a change the stock profile the
    // project has selected -- so a name that finds neither is its bug.
    if ((m_mode == ConversationMode::Change && about().name.empty()) ||
        (m_mode == ConversationMode::Connect && saved(m_printer_name).name.empty()))
        throw std::logic_error("A printer conversation was opened for a printer that is not there: " + m_printer_name);

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
    // Connecting a Bambu Lab printer that nothing can reach yet: the opening
    // says so, and the notice under it offers the plug-in.
    if (m_mode == ConversationMode::Connect && saved(m_printer_name).vendor_id == "BBL" &&
        m_backend.connection(m_printer_name).needs_network_plugin)
        show_plugin_needed({});
    m_host.session_changed();
}

void PrinterConversation::show_plugin_needed(const std::string& message_id)
{
    m_needs_plugin = true;
    const bool shown = std::any_of(m_blocks.begin(), m_blocks.end(), [](const json& block) {
        return block.value("kind", std::string()) == "plugin" && !block.value("installed", false);
    });
    if (shown)
        return;
    m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", message_id}, {"kind", "plugin"}});
    m_host.session_changed();
}

void PrinterConversation::install_network_plugin()
{
    if (!m_needs_plugin || m_printer_name.empty())
        return;
    m_backend.install_network_plugin();
    // Closing the installer, or a download that failed, leaves the notice
    // and its button as they were; Orca's dialog has said what happened.
    if (m_backend.connection(m_printer_name).needs_network_plugin)
        return;
    m_needs_plugin = false;
    for (json& block : m_blocks)
        if (block.value("kind", std::string()) == "plugin")
            block["installed"] = true;
    // Discovery could not start without the plug-in; the next status check
    // starts it.
    m_prepared.clear();
    m_search.reset();
    m_host.post_note("Bambu's network plug-in is installed now, so " + m_printer_name + " can be connected.");
    m_host.session_changed();
    m_host.start_turn();
}

SavedPrinter PrinterConversation::saved(const std::string& name) const
{
    for (const SavedPrinter& printer : m_backend.saved_printers())
        if (!name.empty() && printer.name == name)
            return printer;
    return {};
}

SavedPrinter PrinterConversation::about() const
{
    const SavedPrinter printer = saved(m_printer_name);
    return !printer.name.empty() || m_printer_name.empty() ? printer : m_backend.stock_printer(m_printer_name);
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
    if (const SavedPrinter printer = about(); !printer.name.empty()) {
        context["printer"]             = printer_json(printer);
        context["printer"]["provider"] = printer.vendor_id == "BBL" ? "bambu" : "host";
        if (printer.stock) {
            context["printer"]["stock"]    = true;
            context["printer"]["copyName"] = printer.copy_name;
        }
        if (m_needs_plugin)
            context["printer"]["needsNetworkPlugin"] = true;
    }
    return context;
}

Agent::AgentSessionProfile PrinterConversation::profile() const
{
    Agent::AgentSessionProfile profile;
    // The tools are the app's to decide; the words are the page's
    // (printerInstructions.ts), sent with printer_instructions.
    profile.tool_names = session_tools();
    const std::vector<std::string> settings = settings_tools();
    profile.tool_names.insert(profile.tool_names.end(), settings.begin(), settings.end());
    profile.instructions = m_instructions;
    // This conversation is about a machine, not about the open project, and
    // what the app does on its own reaches the model as notes.
    profile.include_workspace          = false;
    return profile;
}

json PrinterConversation::state_json() const
{
    json state{{"mode", m_mode == ConversationMode::Add ? "add" : m_mode == ConversationMode::Connect ? "connect" : "change"},
               {"printerName", m_printer_name}, {"blocks", m_blocks}, {"context", context_json()}};
    if (m_form)
        state["form"] = form_json();
    if (!m_credential_action.empty())
        state["credentialRequest"] = json{{"actionId", m_credential_action},
                                           {"target", m_credential_arguments.at("target")},
                                           {"provider", m_credential_arguments.at("provider")}};
    return state;
}

// -- The report ------------------------------------------------------------

json connection_chat_record(const std::vector<Agent::ConversationMessage>& messages, const std::vector<ToolActivity>& activities)
{
    json turns = json::array();
    for (const Agent::ConversationMessage& message : messages)
        if (!message.text.empty()) // a turn that only called a tool
            turns.push_back({{"role", message.role == Agent::MessageRole::User ? "person" :
                                      message.role == Agent::MessageRole::Note ? "app" :
                                                                                 "model"},
                             {"text", message.text}});
    json tools = json::array();
    for (const ToolActivity& activity : activities) {
        if (activity.tool != "printer_host_connect" && activity.tool != "printer_bambu_connect" &&
            activity.tool != "printer_connection_status")
            continue;
        tools.push_back({{"tool", activity.tool},
                         {"arguments", json::parse(activity.arguments_json, nullptr, false)},
                         // A call still open has no outcome yet.
                         {"output", activity.error              ? json(activity.error->code + ": " + activity.error->message) :
                                    activity.result_json.empty() ? json() :
                                                                   json::parse(activity.result_json, nullptr, false)}});
    }
    return json{{"turns", std::move(turns)}, {"tools", std::move(tools)}};
}

// -- The session's tools ----------------------------------------------------

std::optional<Agent::SessionSkill> PrinterConversation::connection_skill() const
{
    // In force from the moment there is a saved printer to connect, before the
    // person asks: what they say first about connecting is read with it. A
    // chat that never connects carries the text (Kenneth, 2026-11-04). The
    // project's stock printer has no connection to make.
    const SavedPrinter printer = saved(m_printer_name);
    if (printer.name.empty())
        return std::nullopt;
    // Either comes with issue_report_open: Kenneth, 2026-11-06, offered it for
    // all printer connections.
    if (printer.vendor_id == "BBL")
        return Agent::SessionSkill{Agent::Protocol::SkillTrigger::kConnectBambu, {"issue_report_open"}};
    return Agent::SessionSkill{Agent::Protocol::SkillTrigger::kConnectPrintHost, {"issue_report_open"}};
}

std::optional<ToolError> PrinterConversation::preflight_tool(ToolHandler handler, ToolActivity& activity)
{
    // Every session is offered every printer tool, so the tool list stays the
    // same for the prompt cache; what a call may do depends on the session.
    // Finding and adding a printer belongs to an Add session: a Change or
    // Connect session is about a printer the person already has, and an add
    // there would save another printer and move the session onto it.
    if (m_mode != ConversationMode::Add && (handler == ToolHandler::PrinterIdentify || handler == ToolHandler::PrinterAdd))
        return ToolError{"not_in_this_conversation",
                         "This conversation is about " + m_printer_name + ", which is already set up; printers are not "
                         "found or added here. Tell the person that another printer is added with + Add printer on Home."};
    // The settings tools reach the project too; here they reach this
    // printer's own settings, and nothing else.
    if (handler == ToolHandler::SettingsSearch || handler == ToolHandler::SettingsGet ||
        handler == ToolHandler::SettingsPreviewPatch || handler == ToolHandler::SettingsApplyPatch) {
        if (m_printer_name.empty())
            return ToolError{"no_printer", "No printer is set up in this conversation yet: add one first."};
        const json arguments = json::parse(activity.arguments_json);
        if (arguments.value("scope", std::string()) != "printer")
            return ToolError{"not_in_this_conversation", "Only this printer's settings are changed here: pass scope printer and "
                                                         "target.preset \"" + m_printer_name + "\"."};
        if (arguments.at("target").value("preset", std::string()) != m_printer_name)
            return ToolError{"other_printer", "This conversation can change only \"" + m_printer_name + "\". Another saved printer is "
                                              "changed from Printer settings… in its menu on Home."};
        return std::nullopt;
    }
    // A stock profile holds nothing of the person's own: a nozzle size or a
    // connection is saved on a printer they have added.
    if (about().stock && (handler == ToolHandler::PrinterChange || handler == ToolHandler::PrinterHostConnect ||
                          handler == ToolHandler::PrinterBambuConnect || handler == ToolHandler::PrinterConnectionStatus))
        return ToolError{"stock_profile", "\"" + m_printer_name + "\" is the settings JusPrin comes with for this model, not a "
                                          "printer the person has added, so its nozzle and connection are not saved. Its other "
                                          "settings are changed by saving a copy, which is then theirs (settings_apply_patch with "
                                          "persistAs). To keep a nozzle or a connection, they add the printer with + Add printer "
                                          "on Home."};
    // A change is to the printer this session is about, and only once there
    // is one. The model sometimes names a printer on the network instead.
    if (handler == ToolHandler::PrinterChange) {
        if (saved(m_printer_name).name.empty())
            return ToolError{"no_printer", "No printer is set up in this conversation yet: add one first."};
        const std::string name = json::parse(activity.arguments_json).value("printerName", std::string());
        if (name != m_printer_name)
            return ToolError{saved(name).name.empty() ? "unknown_printer" : "other_printer",
                             "This conversation can change only \"" + m_printer_name + "\". Another saved printer is "
                             "changed from Printer settings… in its menu on Home."};
        return std::nullopt;
    }
    if (handler != ToolHandler::PrinterHostConnect && handler != ToolHandler::PrinterBambuConnect)
        return std::nullopt;
    json              arguments = json::parse(activity.arguments_json);
    const std::string name      = m_printer_name;
    if (saved(name).name.empty())
        return ToolError{"no_printer", "There is no saved printer for this conversation: add one first."};
    const PrinterConnectionInfo info = m_backend.connection(name);
    // No printer can be listed without the plug-in, so the reason is that,
    // not an unknown device.
    if (info.needs_network_plugin)
        return ToolError{"connection_unavailable", info.message};
    if (info.provider == "bambu") {
        // A call written for a print host names an address instead of an id,
        // and may be naming a printer the app has seen.
        if (handler == ToolHandler::PrinterHostConnect)
            return ToolError{"unknown_device", "This is a Bambu Lab printer, connected with printer_bambu_connect. " +
                                                   unknown_device_text(info, arguments.value("address", std::string()))};
        // The person names the printer as its list shows it, so its name
        // finds it as surely as its id.
        const std::string device = arguments.value("deviceId", std::string());
        const auto found = std::find_if(info.candidates.begin(), info.candidates.end(), [&](const ConnectionCandidate& candidate) {
            return offered(candidate) && (candidate.id == device || candidate.name == device);
        });
        if (found == info.candidates.end())
            return ToolError{"unknown_device", unknown_device_text(info, device)};
        activity.title = "Waiting for the access code for " + found->name;
        arguments["deviceId"] = found->id;
        arguments["target"]   = found->name;
    } else if (info.provider == "host") {
        if (handler == ToolHandler::PrinterBambuConnect)
            return ToolError{"address_needed", "This printer connects through Moonraker or OctoPrint, not as a Bambu Lab device: "
                                               "call printer_host_connect with hostType and the print host's address."};
        m_host_type = arguments["hostType"].get<std::string>();
        activity.title      = "Checking " + arguments["address"].get<std::string>();
        arguments["target"] = arguments["address"];
    } else
        return ToolError{"connection_unavailable", info.message};
    // Which field the local form asks for, and the printer it named.
    arguments["provider"]    = info.provider;
    arguments["printerName"] = name;
    activity.arguments_json = arguments.dump();
    return std::nullopt;
}

std::string PrinterConversation::unknown_device_text(const PrinterConnectionInfo& info, const std::string& asked) const
{
    // What it was asked for may be a printer the app has seen and does not
    // offer. An empty id or address is not a match for one with none known.
    const auto wanted = std::find_if(info.candidates.begin(), info.candidates.end(), [&asked](const ConnectionCandidate& candidate) {
        return !offered(candidate) && !asked.empty() && (candidate.id == asked || candidate.name == asked || candidate.address == asked);
    });
    if (wanted != info.candidates.end())
        return not_offered_text(*wanted) + " It cannot be connected as it is." +
               (in_cloud_mode(*wanted) ? std::string(" ") + kPersonsChoice : std::string());

    std::string listed;
    std::string others;
    bool        cloud = false;
    for (const ConnectionCandidate& candidate : info.candidates)
        if (offered(candidate))
            listed += (listed.empty() ? "" : ", ") + candidate.id + " (" + candidate.name + ")";
        else {
            others += " " + not_offered_text(candidate);
            cloud = cloud || in_cloud_mode(candidate);
        }
    return (listed.empty() ? search_text() : "Pass the deviceId of one of these printers in LAN mode: " + listed + ".") + others +
           (cloud ? std::string(" ") + kPersonsChoice : std::string());
}

std::string PrinterConversation::search_text() const
{
    if (!m_search)
        return "No network search has run yet in this conversation; printer_connection_status starts it.";
    const std::string seconds =
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_search->started).count());
    if (m_search->port != DiscoveryPort::Ours)
        return m_search->port == DiscoveryPort::Taken ? kPortTaken : kNotListening;
    return "The network search has listened for " + seconds + " seconds and has heard no printer of this model in LAN mode." +
           (m_search->found ? "" : " It goes on, and the app posts a note if one is heard.");
}

json PrinterConversation::search_json(const PrinterConnectionInfo& info) const
{
    if (info.provider != "bambu")
        return json{{"state", "not_applicable"}};
    if (!m_search || info.state == "unavailable")
        return json{{"state", "not_started"}};
    json search{{"state", m_search->port == DiscoveryPort::Taken ? "port_taken" :
                          m_search->port == DiscoveryPort::Free  ? "not_listening" :
                          m_search->said_none || m_search->found ? "listening" :
                                                                   "started"},
                {"secondsAgo", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_search->started).count()}};
    // Nothing heard is not "not there": a first announcement has come more
    // than two minutes in.
    if (m_search->said_none && !m_search->found && m_search->port == DiscoveryPort::Ours)
        search["message"] = "Nothing heard in " + std::to_string(search["secondsAgo"].get<long long>()) +
                            " seconds. The search is still listening, and the app posts a note if a printer of this model is heard.";
    return search;
}

Result PrinterConversation::execute_tool(ToolHandler handler, const ToolActivity& activity)
{
    const json arguments = json::parse(activity.arguments_json);
    switch (handler) {
    case ToolHandler::PrinterIdentify: return identify(arguments, activity.correlation_id);
    case ToolHandler::PrinterAdd: return add(arguments, activity.correlation_id);
    case ToolHandler::PrinterChange: return change(arguments);
    case ToolHandler::PrinterConnectionStatus: return connection_status(m_printer_name, activity);
    case ToolHandler::PrinterHostConnect: return host_connect(arguments, activity);
    case ToolHandler::PrinterBambuConnect: return request_credential(arguments, activity);
    case ToolHandler::IssueReportOpen: return open_issue(arguments);
    default: return {};
    }
}

std::optional<json> PrinterConversation::tool_output(const ToolActivity& activity) const
{
    const std::vector<std::string> tools = session_tools();
    if (std::find(tools.begin(), tools.end(), activity.tool) == tools.end() && activity.tool != "issue_report_open")
        return std::nullopt;
    if (activity.state == Agent::ToolState::Succeeded)
        return json::parse(activity.result_json);
    if (activity.state == Agent::ToolState::Failed && activity.error)
        return json{{"error", json{{"code", activity.error->code}, {"message", activity.error->message}}}};
    return std::nullopt;
}

void PrinterConversation::tool_settled(const ToolActivity& activity)
{
    // A waiting call the person stopped stops what it waited for: an
    // answer after that is the outcome of nothing.
    if (activity.state == Agent::ToolState::Cancelled) {
        if (!m_connect_call.empty() && activity.action_id == m_connect_call) {
            m_connect_call.clear();
            abandon_connection({});
            m_host.session_changed();
        }
        if (m_status_call && activity.action_id == m_status_call->action_id)
            m_status_call.reset();
        return;
    }
    if (activity.tool != "settings_apply_patch" || activity.state != Agent::ToolState::Succeeded)
        return;
    const std::string saved_as = json::parse(activity.result_json).value("savedAs", std::string());
    if (saved_as.empty() || saved_as == m_printer_name)
        return;
    // Saved as a copy, which Orca selected in the preset's place: the
    // printer the person now has, and Home lists it.
    m_printer_name = saved_as;
    m_host.printers_changed(saved_as);
    m_host.session_changed();
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

Result PrinterConversation::add(const json& arguments, const std::string& message_id)
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
    // The receipt is the app's, drawn from what was saved: the model's reply
    // can word the moment any way, but whether it happened is not its to say.
    m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", message_id},
                            {"kind", "added"}, {"printer", json{{"name", now.name}, {"nozzle", now.nozzle}}}});
    m_host.printers_changed(now.name);
    m_host.session_changed();
    return ok(json{{"printer", printer_json(now)}});
}

Result PrinterConversation::change(const json& arguments)
{
    // preflight_tool has checked that this is the session's own printer.
    const std::string  name = arguments.at("printerName");
    const SavedPrinter now  = saved(name);
    // What is loaded is the printer's to report, not the conversation's to
    // record: the nozzle is the one physical change this tool saves.
    if (!said_nozzle(arguments))
        return refuse("nothing_to_change", "Name the nozzle size that changed.");
    if (!now.nozzles.empty() && !ships(now.nozzles, arguments["nozzle"].get<double>()))
        return unknown_nozzle(now.nozzles, now.model.empty() ? now.name : now.model, arguments["nozzle"].get<double>());

    ChangePrinterRequest request;
    request.name = name;
    if (arguments["nozzle"].get<double>() != now.nozzle)
        request.nozzle = arguments["nozzle"].get<double>();
    if (!request.nozzle)
        return refuse("nothing_to_change", "That is already how " + name + " is set up.");

    SavedPrinter             changed;
    std::vector<std::string> discarded;
    if (const std::string problem = m_backend.change_printer(request, changed, discarded); !problem.empty())
        return refuse("change_failed", problem);
    const SavedPrinter after = saved(name);
    json               what  = json::array();
    what.push_back(json{{"field", "nozzle"}, {"before", now.nozzle}, {"after", after.nozzle}});
    m_host.printers_changed();
    m_host.session_changed();
    return ok(json{{"changed", std::move(what)}, {"discarded", discarded}, {"printer", printer_json(after)}});
}

Result PrinterConversation::connection_status(const std::string& name, const ToolActivity& activity)
{
    const std::string& message_id = activity.correlation_id;
    if (saved(name).name.empty())
        return refuse("no_printer", "There is no saved printer for this conversation: add one first.");
    // For a Bambu Lab printer this starts looking for it on the network, once;
    // see m_prepared.
    if (m_prepared != name) {
        m_backend.prepare_connection(name);
        m_prepared = name;
        m_search.reset();
    }
    const PrinterConnectionInfo info = m_backend.connection(name);
    // A host already saved says which kind it is; until one is, the first
    // connection attempt does.
    if (info.provider == "host" && !info.address.empty() && m_host_type.empty())
        m_host_type = info.host_type;
    if (info.needs_network_plugin)
        show_plugin_needed(message_id);
    if (!m_search && info.provider == "bambu" && info.state != "unavailable")
        m_search = NetworkSearch{std::chrono::steady_clock::now()};
    // What this read lists, the model now knows: no note need say it again.
    // A printer already reached needs no search to report on, however it is
    // reached.
    if (m_search && (found_on_network(info) || info.state == "verified"))
        m_search->found = true;
    // Nothing heard yet: the call listens, and watch_search ends it.
    if (m_search && !m_search->found && m_search->port == DiscoveryPort::Ours && !info.needs_network_plugin && m_connecting.empty()) {
        // One call listens at a time: one already listening, made beside this
        // one, ends with what is known now.
        answer_status_call(info);
        m_status_call = StatusCall{activity.action_id, std::chrono::steady_clock::now() + kSearchWait};
        m_search->last_look = {};
        Result waiting;
        waiting.handled = true;
        waiting.pending = true;
        return waiting;
    }
    return ok(connection_json(info, search_json(info)));
}

Result PrinterConversation::request_credential(const json& arguments, const ToolActivity& activity)
{
    abandon_connection("A later printer_bambu_connect call opened the form again, so this one was dropped.");
    open_connect_call(activity);
    ask_credential(arguments);
    return pending();
}

Result PrinterConversation::host_connect(const json& arguments, const ToolActivity& activity)
{
    const std::string name      = arguments.at("printerName");
    const std::string action_id = activity.action_id;
    // One test at a time: the worker runs one, and the form answers one.
    abandon_connection("A later printer_host_connect call started another test, so this one was dropped.");
    // A key the app holds for that address is sent; none otherwise.
    const std::string problem = m_backend.connect_host(name, arguments.at("hostType"), arguments.at("address"), {},
                                                       [this, action_id, arguments](const HostTestOutcome& outcome) {
                                                           host_tested(action_id, arguments, outcome);
                                                       });
    // preflight_tool has checked the printer; what is left is the address.
    if (!problem.empty()) {
        const std::string report = connection_failed(activity.correlation_id, problem);
        return refuse("invalid_arguments", report.empty() ? problem : problem + " " + report);
    }
    open_connect_call(activity);
    m_host_testing = name;
    return pending();
}

void PrinterConversation::host_tested(const std::string& action_id, const json& arguments, const HostTestOutcome& outcome)
{
    m_host_testing.clear();
    if (outcome.state != "needs_credential") {
        end_connect_call(outcome.state, outcome.message);
        return;
    }
    // The host wants a key: the call waits while the person types one in
    // the form, never in chat, and its result says what the host said.
    m_connect_said = outcome.message;
    m_host.update_tool(action_id, std::string("Waiting for ") + (outcome.key == "saved" ? "a different" : "an") + " API key for " +
                                      arguments.at("address").get<std::string>(),
                       false);
    ask_credential(arguments);
}

void PrinterConversation::form_tested(const HostTestOutcome& outcome)
{
    m_host_testing.clear();
    // A key the host refused is the person's failed attempt.
    end_connect_call(outcome.state == "verified" ? "verified" : "failed",
                     m_connect_said + " The person typed a key in the form, and the app tested again: " + outcome.message);
}

void PrinterConversation::connect(const json& arguments, const std::string& credential)
{
    const std::string name   = arguments.at("printerName");
    const std::string target = arguments.at("target");
    const bool        host   = arguments.at("provider") == "host";
    // From here the app's own work runs, and its dialogs belong to the call.
    m_host.update_tool(m_connect_call, "Checking " + target, true);
    const std::string problem =
        host ? m_backend.connect_host(name, arguments.at("hostType"), arguments.at("address"), credential,
                                      [this](const HostTestOutcome& outcome) { form_tested(outcome); }) :
               m_backend.connect_printer(name, arguments.at("deviceId"), credential);
    if (!problem.empty()) {
        end_connect_call("failed", "The app could not start connecting to " + target + ": " + problem);
        return;
    }
    if (host)
        m_host_testing = name;
    else {
        m_connecting = name;
        // An empty form reconnects with the code the app already holds.
        m_code_sent  = credential.empty() ? "The form was left empty, so the app used the access code it already holds for this printer." :
                                            "The app sent the access code typed in the form.";
    }
    m_last_look = {};
    m_host.session_changed();
}

void PrinterConversation::close_credential(bool help)
{
    const bool        bambu  = m_credential_arguments.at("provider") == "bambu";
    const std::string target = m_credential_arguments.at("target");
    const std::string asked  = bambu ? "the printer's access code" : "the API key for " + target;
    const std::string what   = help ? std::string("The person tapped \"Help me find the ") + (bambu ? "code" : "key") +
                                          "\" in the form: they need help finding " + asked + ". Help them find it." :
                                      "The person closed the form for " + asked + " without connecting.";
    end_connect_call(help ? "help_requested" : "dismissed",
                     (m_connect_said.empty() ? std::string() : m_connect_said + " ") + what + " Nothing was sent.");
}

void PrinterConversation::open_connect_call(const ToolActivity& activity)
{
    m_connect_call    = activity.action_id;
    m_connect_message = activity.correlation_id;
    m_connect_printer = m_printer_name;
    m_connect_said.clear();
}

void PrinterConversation::ask_credential(const json& arguments)
{
    m_credential_action    = m_connect_call;
    m_credential_arguments = arguments;
    m_host.session_changed();
}

void PrinterConversation::end_connect_call(const std::string& state, std::string message)
{
    if (state == "failed")
        if (const std::string report = connection_failed(m_connect_message, message); !report.empty())
            message += " " + report;
    const std::string call = std::exchange(m_connect_call, {});
    m_connect_message.clear();
    m_connect_printer.clear();
    m_connect_said.clear();
    m_credential_action.clear();
    m_credential_arguments = json::object();
    m_host.printers_changed();
    m_host.session_changed();
    m_host.finish_tool(call, ok(json{{"state", state}, {"message", message}}));
}

PrinterConversation::Result PrinterConversation::pending()
{
    Result waiting;
    waiting.handled = true;
    waiting.pending = true;
    return waiting;
}

std::string PrinterConversation::connection_failed(const std::string& message_id, const std::string& failure)
{
    // The rule is the app's, not the model's: a model that is stuck is the one
    // least likely to say so. Only where there is a skill a report can improve.
    if (++m_failed_connections != 2 || !connection_skill())
        return {};
    const json report{{"report", "connection"},
                      {"trigger", "failed_twice"},
                      {"connectionType", m_host_type.empty() ? "bambu" : m_host_type},
                      {"model", saved(m_printer_name).model},
                      {"failure", without_secrets(failure)},
                      {"chat", without_secrets(m_host.chat_record())}};
    m_blocks.push_back(json{{"id", next_block_id()}, {"seq", m_blocks.size() + 1}, {"afterMessageId", message_id},
                            {"kind", "report"}, {"report", report.dump(2)}});
    return "The app is asking the person, on a card of its own, whether to send a report of this chat to the JusPrin "
           "developers. Nothing is sent unless they choose to.";
}

void PrinterConversation::answer_report(const std::string& block_id, bool send)
{
    for (json& block : m_blocks) {
        if (block.value("id", std::string()) != block_id || block.value("kind", std::string()) != "report" || block.contains("answer"))
            continue;
        if (send) {
            const std::string kept = m_backend.keep_connection_report(block.at("report").get<std::string>());
            block["answer"] = kept.empty() ? "failed" : "kept";
            block["path"]   = kept;
        } else
            block["answer"] = "declined";
        m_host.post_note(!send                     ? "The person chose not to send the report." :
                         block["answer"] == "kept" ? "The person chose to send the report. Sending is not built yet, so it is saved on "
                                                     "this computer and has not reached the developers." :
                                                     "The person chose to send the report, and it could not be saved.");
        m_host.session_changed();
        return;
    }
}

// -- An issue for the developers --------------------------------------------

IssueLink issue_link(const std::string& title, const std::string& body, const std::string& footer)
{
    static const std::string page   = "https://github.com/TheSpaghettiDetective/JusPrin/issues/new";
    static const std::string marker = percent_encoded("\n\n(Cut short to fit the link.)");
    const std::string        start  = page + "?title=" + percent_encoded(title) + "&body=";
    const std::string        end    = percent_encoded(footer);
    const std::string        whole  = percent_encoded(body);
    if (start.size() + whole.size() + end.size() <= kIssueLinkLimit)
        return {start + whole + end, false};
    if (start.size() + marker.size() + end.size() > kIssueLinkLimit)
        throw std::logic_error("An issue's title and footer leave no room for its body");
    // Whole characters, so the cut never splits one.
    const std::size_t room = kIssueLinkLimit - start.size() - marker.size() - end.size();
    std::string       kept;
    for (std::size_t from = 0; from < body.size();) {
        std::size_t next = from + 1;
        while (next < body.size() && (static_cast<unsigned char>(body[next]) & 0xC0) == 0x80)
            ++next;
        const std::string character = percent_encoded(body.substr(from, next - from));
        if (kept.size() + character.size() > room)
            break;
        kept += character;
        from = next;
    }
    return {start + kept + marker + end, true};
}

Result PrinterConversation::open_issue(const json& arguments)
{
    // The tool is offered only with a connection skill.
    const std::optional<Agent::SessionSkill> skill = connection_skill();
    if (!skill)
        throw std::logic_error("issue_report_open ran in a printer chat without a connection skill in force");
    const bool bambu = skill->trigger == Agent::Protocol::SkillTrigger::kConnectBambu;
    const SavedPrinter          printer = saved(m_printer_name);
    const PrinterConnectionInfo info    = m_backend.connection(m_printer_name);
    std::vector<std::string>    known{printer.device_id, info.device_id, info.address};
    for (const ConnectionCandidate& candidate : info.candidates) {
        known.push_back(candidate.id);
        known.push_back(candidate.address);
    }
    // The app's own facts, which the model may have wrong or left out. A
    // print host is named once the chat or its saved connection says which
    // program it is.
    const std::string& type = m_host_type.empty() ? info.host_type : m_host_type;
    const std::string  host = type == "moonraker" ? "Moonraker" : type == "octoprint" ? "OctoPrint" : "print host";
    const std::string footer = "\n\n---\nApp version: " + m_host.app_version() +
                               "\nConnection: " + (bambu ? "Bambu Lab, LAN mode" : host) + "\nPrinter: " + printer.model;
    const IssueLink link = issue_link(for_public(arguments.at("title").get<std::string>(), known),
                                    for_public(arguments.at("body").get<std::string>(), known), footer);
    if (!m_host.open_in_browser(link.url))
        return refuse("failed", "The app could not open the person's browser, so no issue page is open.");
    std::string message = "The new issue page is open in the person's browser, filled in. Nothing has been filed: it is filed "
                          "only when they press Submit on it, signed in to GitHub.";
    if (link.shortened)
        message += " The body was too long for the link, so its end was cut; they can add the rest on the page.";
    return ok(json{{"state", "opened"}, {"shortened", link.shortened}, {"message", message}});
}

void PrinterConversation::abandon_connection(const std::string& why)
{
    if (!m_connecting.empty()) {
        m_backend.cancel_connection(m_connecting);
        m_connecting.clear();
    }
    if (!m_host_testing.empty()) {
        m_backend.cancel_connection(m_host_testing);
        m_host_testing.clear();
    }
    m_credential_action.clear();
    m_credential_arguments = json::object();
    m_connect_message.clear();
    m_connect_printer.clear();
    m_connect_said.clear();
    if (const std::string call = std::exchange(m_connect_call, {}); !call.empty())
        m_host.finish_tool(call, refuse("cancelled", why));
}

void PrinterConversation::tick(std::chrono::steady_clock::time_point now)
{
    if (m_form && m_form->reading.valid() && m_form->reading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_form->options = m_form->reading.get();
        m_host.session_changed();
    }
    watch_search(now);
    if (m_connecting.empty() || now - m_last_look < kConnectionLook)
        return;
    m_last_look = now;
    const PrinterConnectionInfo info = m_backend.connection(m_connecting);
    if (info.state == "connecting")
        return;
    const std::string name = std::exchange(m_connecting, {});
    end_connect_call(info.state == "verified" ? "verified" : "failed",
                     (info.state == "verified" ? "The app connected to " + name + "." : info.message) + " " + m_code_sent);
}

void PrinterConversation::watch_search(std::chrono::steady_clock::time_point now)
{
    // An attempt under way is read by tick alone: reading a connection can
    // settle it.
    if (!m_search || m_search->found || !m_connecting.empty() ||
        now - m_search->last_look < ((m_search->said_none && !m_status_call) || m_search->port != DiscoveryPort::Ours ?
                                         std::chrono::seconds(kSearchLateLook) :
                                         std::chrono::seconds(kConnectionLook)))
        return;
    m_search->last_look = now;
    const PrinterConnectionInfo info = m_backend.connection(m_prepared);
    // The printer was renamed or removed, or nothing can reach the network now.
    if (info.provider != "bambu" || info.state == "unavailable") {
        answer_status_call(info);
        m_search.reset();
        return;
    }
    if (info.state == "verified") {
        m_search->found = true;
        answer_status_call(info);
        return;
    }
    std::string note;
    if (found_on_network(info)) {
        m_search->found = true;
        // A call listening for it reads it in its result.
        if (m_status_call) {
            answer_status_call(info);
            return;
        }
        bool cloud = false;
        for (const ConnectionCandidate& candidate : info.candidates) {
            if (candidate.account_only)
                continue;
            cloud = cloud || in_cloud_mode(candidate);
            if (offered(candidate))
                note += (note.empty() ? "The network search found " : "; ") + with_address(candidate) + " in LAN mode";
            else if (in_cloud_mode(candidate))
                note += (note.empty() ? "The network search found " : "; ") + with_address(candidate) +
                        " with LAN Only off, which cannot be connected as it is";
            else
                // Heard, and not this printer: said as such, so "found no
                // printer of this model" is never the note about a printer
                // the app saw.
                note += (note.empty() ? "The network search heard a printer it does not offer: " : "; ") + not_offered_text(candidate);
        }
        note += ".";
        if (cloud)
            note += std::string(" ") + kPersonsChoice;
    } else if (m_status_call ? now < m_status_call->until : now - m_search->started < kSearchWait)
        return;
    else {
        // Checked only once discovery has had time to bind: the check itself
        // binds the port for a moment, which could cost discovery its own bind.
        const DiscoveryPort port    = m_backend.discovery_port();
        const bool          changed = port != m_search->port;
        m_search->port              = port;
        if (port == DiscoveryPort::Ours)
            m_search->said_none = true;
        if (m_status_call) {
            answer_status_call(info);
            return;
        }
        // Nothing heard is in the result of the call that listened; only a
        // port that changed since is news.
        if (!changed || port == DiscoveryPort::Ours)
            return;
        note = port == DiscoveryPort::Taken ? kPortTaken : kNotListening;
    }
    m_host.post_note(note);
    m_host.session_changed();
    m_host.start_turn();
}

void PrinterConversation::answer_status_call(const PrinterConnectionInfo& info)
{
    if (!m_status_call)
        return;
    const std::string action_id = m_status_call->action_id;
    m_status_call.reset();
    m_host.finish_tool(action_id, ok(connection_json(info, search_json(info))));
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
    else if (action == "manual_setup")
        open_form();
    else if (action == "form_options")
        ask_form_options(payload);
    else if (action == "form_finish")
        finish_form(payload);
    else if (action == "form_close") {
        m_form.reset();
        m_host.session_changed();
    } else if (action == "orca_printer_list") {
        // OrcaSlicer's own printer list, for a panel whose page could not
        // load and so cannot show the form; what it added is news the model
        // answers.
        const ManualPrinterResult result = m_backend.run_manual_setup();
        if (!result.added.empty()) {
            m_printer_name = result.added.front().name;
            m_host.post_note("The person added " + m_printer_name + " from the printer list.");
            m_host.printers_changed(m_printer_name);
            m_host.session_changed();
            m_host.start_turn();
        }
    } else if (action == "connect" && !m_credential_action.empty() && payload.value("actionId", std::string()) == m_credential_action &&
               payload.contains("credential") && payload["credential"].is_string()) {
        const json arguments = std::exchange(m_credential_arguments, json::object());
        m_credential_action.clear();
        connect(arguments, payload["credential"].get<std::string>());
    } else if ((action == "credential_help" || action == "credential_dismissed") && !m_credential_action.empty() &&
               payload.value("actionId", std::string()) == m_credential_action)
        close_credential(action == "credential_help"); else if (action == "open_printer_settings" && !m_printer_name.empty()) {
        m_backend.open_printer_settings(m_printer_name);
        m_host.printers_changed();
        m_host.session_changed();
    } else if (action == "undo_add")
        undo_add(payload.value("blockId", std::string()));
    else if (action == "install_network_plugin")
        install_network_plugin();
    else if (action == "report_send" || action == "report_decline")
        answer_report(payload.value("blockId", std::string()), action == "report_send");
    return true;
}

void PrinterConversation::undo_add(const std::string& block_id)
{
    json* receipt = nullptr;
    for (json& block : m_blocks)
        if (block.value("id", std::string()) == block_id && block.value("kind", std::string()) == "added")
            receipt = &block;
    // A second tap that arrived before the page redrew the first has nothing
    // left to undo.
    if (receipt == nullptr || receipt->value("removed", false))
        return;

    // Orca's own delete, as Home's "Remove printer…" runs it: the printer the
    // project was set up for gives way to the profile it was based on.
    const std::string name = receipt->at("printer").at("name");
    if (const std::string problem = m_backend.remove_printer(name); !problem.empty()) {
        m_host.post_note("Undo did not remove " + name + ": " + problem);
        m_host.session_changed();
        return;
    }
    // printer_add saves a printer based on a shipped profile, which Orca
    // deletes without asking; only a printer of the person's own design can
    // be kept by declining there.
    if (!saved(name).name.empty())
        throw std::logic_error("Removing an added printer left it saved");

    if (m_connect_printer == name)
        abandon_connection("The person removed " + name + " with Undo before connecting was finished, so it was dropped.");
    (*receipt)["removed"] = true;
    // The same printer can now be added again, and nothing here is about it.
    for (auto added = m_added.begin(); added != m_added.end();)
        added = added->second == name ? m_added.erase(added) : std::next(added);
    if (m_printer_name == name)
        m_printer_name.clear();
    if (m_prepared == name) {
        m_prepared.clear();
        m_search.reset();
        if (m_status_call)
            m_host.finish_tool(m_status_call->action_id,
                               refuse("cancelled", "The person removed " + name + " with Undo, so the search stopped listening for it."));
        m_status_call.reset();
    }
    m_host.post_note("The person tapped Undo on the receipt: " + name + " is removed and is no longer one of their printers.");
    m_host.printers_changed();
    m_host.session_changed();
    m_host.start_turn();
}

// -- The printer form --------------------------------------------------------

void PrinterConversation::open_form()
{
    // A printer is added in an Add session only, as printer_add is.
    if (m_mode != ConversationMode::Add)
        return;
    // An earlier form's read is waited for first: the backend's copy of the
    // person's own presets is what that read is using.
    m_form.reset();
    m_backend.prepare_setup_options();
    PrinterForm form;
    // A machine that already has a printer is that printer, and the form
    // adds new ones; a model the catalogue lacks cannot be set up from it.
    const std::vector<SavedPrinter> printers = m_backend.saved_printers();
    for (DiscoveredPrinter& found : m_backend.nearby_printers()) {
        if (std::any_of(printers.begin(), printers.end(),
                        [&found](const SavedPrinter& printer) { return printer.device_id == found.stable_id; }))
            continue;
        const auto& catalog = m_backend.catalog();
        const auto  model   = std::find_if(catalog.begin(), catalog.end(), [&found](const CatalogPrinter& printer) {
            return !printer.device_model_id.empty() && printer.device_model_id == found.device_model_id;
        });
        if (model != catalog.end())
            form.nearby.emplace_back(std::move(found), model->id);
    }
    m_form = std::move(form);
    m_host.session_changed();
}

void PrinterConversation::ask_form_options(const json& payload)
{
    if (!m_form)
        return;
    const std::string     id      = payload.value("catalogId", std::string());
    const double          nozzle  = payload.value("nozzle", 0.);
    const CatalogPrinter* printer = catalog_entry(id);
    if (printer == nullptr || !ships(printer->nozzles, nozzle))
        return;
    if (m_form->asked_id == id && m_form->asked_nozzle == nozzle)
        return;
    m_form->asked_id     = id;
    m_form->asked_nozzle = nozzle;
    m_form->options.reset();
    m_form->error.clear();
    // A read still running for the last choice is waited for here; its
    // answer is no longer wanted.
    m_form->reading = std::async(std::launch::async, [&backend = m_backend, id, nozzle] { return backend.setup_options(id, nozzle); });
    m_host.session_changed();
}

void PrinterConversation::finish_form(const json& payload)
{
    if (!m_form)
        return;
    const std::string     id      = payload.value("catalogId", std::string());
    const double          nozzle  = payload.value("nozzle", 0.);
    const CatalogPrinter* printer = catalog_entry(id);
    // The page finishes from the options it was sent; anything else is a
    // page out of step with the app, and nothing is saved from it.
    const auto refuse_form = [this](const std::string& why) {
        m_form->error = why;
        m_host.session_changed();
    };
    const std::optional<SetupOptions>& options = m_form->options;
    if (printer == nullptr || !options || options->catalog_id != id || options->nozzle != nozzle || !options->error.empty())
        return refuse_form(options && !options->error.empty() ? options->error : "The form was out of date. Choose the printer again.");

    std::optional<int> plate;
    std::string        plate_label;
    if (!options->plates.empty()) {
        const int  type  = payload.value("plate", 0);
        const auto found = std::find_if(options->plates.begin(), options->plates.end(),
                                        [type](const PlateOption& option) { return option.type == type; });
        if (found == options->plates.end())
            return refuse_form("Choose the plate on this printer.");
        plate       = type;
        plate_label = found->label;
    }

    // The filament to start with: one that fits, which is installed with
    // the printer; one of the person's own; one made for other printers,
    // which is copied for this one; or a preset the person makes in
    // OrcaSlicer's Create filament window once the printer is added.
    const bool        create   = payload.value("create", false);
    const std::string filament = create ? std::string() : payload.value("filament", std::string());
    const auto        in       = [&filament](const std::vector<FilamentOption>& list) -> const FilamentOption* {
        const auto found = std::find_if(list.begin(), list.end(), [&filament](const FilamentOption& option) { return option.name == filament; });
        return found == list.end() ? nullptr : &*found;
    };
    const FilamentOption* fitting = filament.empty() ? nullptr : in(options->filaments);
    const FilamentOption* other   = filament.empty() ? nullptr : in(options->others);
    const FilamentOption* own     = filament.empty() ? nullptr : in(options->own);
    if (!filament.empty() && fitting == nullptr && other == nullptr && own == nullptr)
        return refuse_form("That filament is no longer on the list. Choose another.");

    const std::string device = payload.value("deviceId", std::string());
    const auto        found  = std::find_if(m_form->nearby.begin(), m_form->nearby.end(),
                                            [&device](const auto& entry) { return entry.first.stable_id == device; });
    if (!device.empty() && (found == m_form->nearby.end() || found->second != id))
        return refuse_form("That printer is no longer on the list. Choose it again.");

    // The printer is installed with a filament that fits: the chosen one, or
    // the profile's default until the chosen one is in place.
    AddPrinterRequest request{printer->vendor_id, printer->model_id, number_text(nozzle),
                              fitting != nullptr ? fitting->name : options->default_filament, printer->display_name(), device, plate};
    SavedPrinter      added;
    if (const std::string problem = m_backend.add_printer(request, added); !problem.empty())
        return refuse_form(problem);
    const SavedPrinter now = saved(added.name);
    if (now.name.empty())
        throw std::logic_error("A successful add did not save a printer");

    const auto label_of = [&options](const std::string& name) {
        for (const auto* list : {&options->filaments, &options->others, &options->own})
            for (const FilamentOption& option : *list)
                if (option.name == name)
                    return option.label;
        return name;
    };
    std::string starting = fitting != nullptr ? fitting->label : label_of(options->default_filament);
    std::string filament_note;
    if (own != nullptr || other != nullptr) {
        const FilamentOption& chosen = own != nullptr ? *own : *other;
        const bool            copy   = !chosen.made_for.empty();
        std::string           used;
        if (const std::string problem = m_backend.start_with_filament(now.name, chosen.name, copy, used); !problem.empty())
            filament_note = " " + chosen.label + " could not be used: " + problem + " It starts with " + starting + ".";
        else if (copy)
            starting = "a copy of " + chosen.label + " saved for this printer as " + used;
        else
            starting = "their own preset " + used;
    }
    if (create) {
        const std::string made = m_backend.create_filament_preset(now.name);
        filament_note = made.empty() ? " The person opened the Create filament window and no new preset for this printer is in slot 1, so it starts with " +
                                           starting + "."
                                     : " The person made the filament preset " + made + " in the Create filament window; it is in slot 1.";
    }

    std::string note = "The person added " + now.name + " themselves with the printer form: " + printer->display_name() + ", " +
                       number_text(nozzle) + " mm nozzle";
    if (!plate_label.empty())
        note += ", " + plate_label;
    if (!create && filament_note.empty() && !starting.empty())
        note += ", starting with " + starting;
    note += "." + filament_note;
    if (!device.empty())
        note += " It is linked to the printer " + found->first.name + ", which is not connected yet.";

    m_form.reset();
    m_printer_name = now.name;
    m_added[id]    = now.name;
    m_host.post_note(note);
    m_host.printers_changed(now.name);
    m_host.session_changed();
    m_host.start_turn();
}

json PrinterConversation::form_json() const
{
    json catalog = json::array();
    for (const CatalogPrinter& printer : m_backend.catalog())
        catalog.push_back(json{{"id", printer.id},
                               {"vendorId", printer.vendor_id},
                               {"brand", printer.vendor_name},
                               {"model", printer.model_name},
                               {"nozzles", printer.nozzles}});
    json nearby = json::array();
    for (const auto& [found, model] : m_form->nearby)
        nearby.push_back(json{{"deviceId", found.stable_id}, {"name", found.name}, {"catalogId", model}});
    json form{{"catalog", std::move(catalog)}, {"nearby", std::move(nearby)}, {"error", m_form->error},
              {"loading", m_form->reading.valid()}};
    if (const auto& options = m_form->options) {
        json plates = json::array();
        for (const PlateOption& plate : options->plates)
            plates.push_back(json{{"type", plate.type}, {"label", plate.label}});
        const auto list = [](const std::vector<FilamentOption>& filaments) {
            json entries = json::array();
            for (const FilamentOption& filament : filaments) {
                json entry{{"name", filament.name}, {"label", filament.label}, {"vendor", filament.vendor}, {"material", filament.material}};
                if (!filament.made_for.empty())
                    entry["madeFor"] = filament.made_for;
                if (!filament.base.empty())
                    entry["base"] = filament.base;
                entries.push_back(std::move(entry));
            }
            return entries;
        };
        form["options"] = json{{"catalogId", options->catalog_id},
                               {"nozzle", options->nozzle},
                               {"plates", std::move(plates)},
                               {"defaultPlate", options->default_plate},
                               {"filaments", list(options->filaments)},
                               {"others", list(options->others)},
                               {"own", list(options->own)},
                               {"suggested", options->suggested},
                               {"defaultFilament", options->default_filament},
                               {"error", options->error}};
    }
    return form;
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
