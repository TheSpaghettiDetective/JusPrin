#include "RecordConversion.hpp"

#include "ProviderProfile.hpp"

#include <cctype>
#include <stdexcept>

using nlohmann::json;

namespace Slic3r::GUI::JusPrin::Agent {

std::string anthropic_tool_id(const std::string& id)
{
    std::string safe = id;
    for (char& c : safe)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            c = '_';
    return safe;
}

namespace {

json responses_from_anthropic(const json& output)
{
    json items = json::array();
    for (const json& block : output) {
        const std::string type = block.at("type").get<std::string>();
        if (type == "text")
            items.push_back(json{{"type", "message"}, {"role", "assistant"},
                                 {"content", json::array({json{{"type", "output_text"}, {"text", block.at("text")}}})}});
        else if (type == "tool_use")
            items.push_back(json{{"type", "function_call"}, {"call_id", block.at("id")}, {"name", block.at("name")},
                                 {"arguments", block.at("input").dump()}});
    }
    return items;
}

json anthropic_from_responses(const json& output)
{
    json blocks = json::array();
    for (const json& item : output) {
        const std::string type = item.at("type").get<std::string>();
        if (type == "message") {
            for (const json& part : item.at("content"))
                if (part.at("type") == "output_text")
                    blocks.push_back(json{{"type", "text"}, {"text", part.at("text")}});
        } else if (type == "function_call") {
            // A call the model wrote badly is still the call it made; the
            // record says so with an empty input rather than losing the pair.
            const json input = json::parse(item.at("arguments").get<std::string>(), nullptr, false);
            blocks.push_back(json{{"type", "tool_use"}, {"id", anthropic_tool_id(item.at("call_id").get<std::string>())},
                                  {"name", item.at("name")}, {"input", input.is_object() ? input : json::object()}});
        }
    }
    return blocks;
}

} // namespace

json assistant_output_as(const json& output, const std::string& from, const std::string& to)
{
    if (from == to)
        return output;
    if (from == kFormatAnthropic && to == kFormatResponses)
        return responses_from_anthropic(output);
    if (from == kFormatResponses && to == kFormatAnthropic)
        return anthropic_from_responses(output);
    throw std::logic_error("a record item in format " + from + " read as " + to);
}

} // namespace Slic3r::GUI::JusPrin::Agent
