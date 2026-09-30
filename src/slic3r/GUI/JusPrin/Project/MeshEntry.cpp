#include "MeshEntry.hpp"

#include <openssl/evp.h>

#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

namespace Slic3r::GUI::JusPrin::Project {

namespace {

struct IdSpan
{
    std::size_t begin; // first digit
    std::size_t end;   // one past the last digit
};

// Walks the <object …> start tags and hands over the span of each one's id
// attribute value. The mesh body (<vertex>, <triangle>) carries no "id"
// attribute, so a linear scan for the tag is cheap even on a 2,000,000
// triangle entry.
void for_each_object_id(const std::string& text, const std::function<void(IdSpan)>& visit)
{
    static constexpr const char* tag = "<object";
    std::size_t                  at  = 0;
    while ((at = text.find(tag, at)) != std::string::npos) {
        const std::size_t after = at + std::strlen(tag);
        // "<objects" or "<object_x" would be a different element.
        if (after >= text.size() || !(text[after] == ' ' || text[after] == '\t' || text[after] == '\n' || text[after] == '\r')) {
            at = after;
            continue;
        }
        const std::size_t close = text.find('>', after);
        if (close == std::string::npos)
            return;
        // The id attribute: whitespace, "id", optional whitespace, '=',
        // optional whitespace, a quote. "puuid" and "objectid" end in "id"
        // too, so the character before must be whitespace.
        std::size_t pos = after;
        while (pos < close) {
            const std::size_t id = text.find("id", pos);
            if (id == std::string::npos || id >= close)
                break;
            const char before = text[id - 1];
            std::size_t p     = id + 2;
            while (p < close && (text[p] == ' ' || text[p] == '\t'))
                ++p;
            if ((before == ' ' || before == '\t' || before == '\n' || before == '\r') && p < close && text[p] == '=') {
                ++p;
                while (p < close && (text[p] == ' ' || text[p] == '\t'))
                    ++p;
                if (p < close && (text[p] == '"' || text[p] == '\'')) {
                    const char        quote = text[p];
                    const std::size_t begin = p + 1;
                    const std::size_t end   = text.find(quote, begin);
                    if (end != std::string::npos && end <= close) {
                        visit({begin, end});
                        break;
                    }
                }
            }
            pos = id + 2;
        }
        at = close + 1;
    }
}

std::string hex(const unsigned char* digest, std::size_t length)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string           out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(digits[digest[i] >> 4]);
        out.push_back(digits[digest[i] & 0x0f]);
    }
    return out;
}

} // namespace

std::vector<std::uint64_t> mesh_entry_ids(const std::string& text)
{
    std::vector<std::uint64_t> ids;
    for_each_object_id(text, [&](IdSpan span) {
        ids.push_back(std::strtoull(text.c_str() + span.begin, nullptr, 10));
    });
    return ids;
}

std::string mesh_entry_key(const std::string& text)
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr);
    std::size_t done = 0;
    for_each_object_id(text, [&](IdSpan span) {
        EVP_DigestUpdate(context.get(), text.data() + done, span.begin - done);
        done = span.end;
    });
    EVP_DigestUpdate(context.get(), text.data() + done, text.size() - done);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  length = 0;
    EVP_DigestFinal_ex(context.get(), digest, &length);
    return hex(digest, length);
}

std::string rewrite_mesh_entry_ids(const std::string& text, const std::vector<std::uint64_t>& ids)
{
    std::vector<IdSpan> spans;
    for_each_object_id(text, [&](IdSpan span) { spans.push_back(span); });
    if (spans.size() != ids.size())
        throw std::invalid_argument("The mesh entry holds " + std::to_string(spans.size()) + " objects, not " +
                                    std::to_string(ids.size()));
    std::string out;
    out.reserve(text.size() + ids.size() * 8);
    std::size_t done = 0;
    for (std::size_t k = 0; k < spans.size(); ++k) {
        out.append(text, done, spans[k].begin - done);
        out.append(std::to_string(ids[k]));
        done = spans[k].end;
    }
    out.append(text, done, std::string::npos);
    return out;
}

} // namespace Slic3r::GUI::JusPrin::Project
