// The app's own check of a tool call's arguments, for the prompt test runner
// (tests/project_prompt/run_prompt_tests.py). Each stdin line is
// {"tool": name, "arguments": "<the call's arguments as JSON text>"}; each
// stdout line is {"valid": true} or {"valid": false, "code": ..., "message": ...}
// with ToolRegistry::validate_call's own code and message, so the runner
// refuses exactly the calls ProviderAgent::next_call refuses, in its words.
// A tool the registry does not know is {"valid": false, "code": "unknown_tool"};
// whether a known tool was offered on a request is the runner's to decide.

#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using nlohmann::json;
using Slic3r::GUI::JusPrin::Agent::ToolRegistry;

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const json call = json::parse(line);
        const auto* definition = ToolRegistry::instance().find(call.at("tool").get<std::string>());
        json verdict{{"valid", false}, {"code", "unknown_tool"}};
        if (definition != nullptr) {
            const auto validation = ToolRegistry::instance().validate_call(*definition, call.at("arguments").get<std::string>());
            verdict = validation.valid() ? json{{"valid", true}} :
                                           json{{"valid", false}, {"code", validation.error->code}, {"message", validation.error->message}};
        }
        std::cout << verdict.dump() << std::endl;
    }
    return 0;
}
