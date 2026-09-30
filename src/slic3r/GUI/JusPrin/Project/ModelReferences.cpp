#include "ModelReferences.hpp"

#include <algorithm>
#include <cstring>

namespace Slic3r::GUI::JusPrin::Project {

namespace {

std::string xml_unescape(std::string text)
{
    static const std::pair<const char*, char> entities[] = {
        {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&amp;", '&'}};
    for (const auto& [entity, character] : entities) {
        std::size_t at = 0;
        while ((at = text.find(entity, at)) != std::string::npos)
            text.replace(at, std::strlen(entity), 1, character);
    }
    return text;
}

// The value of `name="…"` inside one tag's text, or empty.
std::string attribute(const std::string& tag, const char* name)
{
    const std::string key = std::string(name) + "=";
    std::size_t       at  = 0;
    while ((at = tag.find(key, at)) != std::string::npos) {
        const bool at_boundary = at == 0 || tag[at - 1] == ' ' || tag[at - 1] == '\t' || tag[at - 1] == '\n';
        std::size_t p          = at + key.size();
        if (at_boundary && p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
            const char        quote = tag[p];
            const std::size_t end   = tag.find(quote, p + 1);
            if (end != std::string::npos)
                return tag.substr(p + 1, end - p - 1);
        }
        at += key.size();
    }
    return {};
}

} // namespace

std::vector<MeshReference> mesh_references(const std::string& main_model_text)
{
    std::vector<MeshReference> references;
    static constexpr const char* tag = "<component";
    std::size_t                  at  = 0;
    while ((at = main_model_text.find(tag, at)) != std::string::npos) {
        const std::size_t close = main_model_text.find('>', at);
        if (close == std::string::npos)
            break;
        const std::string element = main_model_text.substr(at, close - at);
        at                        = close + 1;
        std::string path          = xml_unescape(attribute(element, "p:path"));
        const std::string id_text = attribute(element, "objectid");
        if (path.empty() || id_text.empty())
            continue;
        if (path.front() == '/')
            path.erase(0, 1);
        const std::uint64_t id = std::strtoull(id_text.c_str(), nullptr, 10);
        auto reference = std::find_if(references.begin(), references.end(),
                                      [&](const MeshReference& r) { return r.path == path; });
        if (reference == references.end())
            reference = references.insert(references.end(), MeshReference{path, {}});
        if (std::find(reference->ids.begin(), reference->ids.end(), id) == reference->ids.end())
            reference->ids.push_back(id);
    }
    return references;
}

} // namespace Slic3r::GUI::JusPrin::Project
