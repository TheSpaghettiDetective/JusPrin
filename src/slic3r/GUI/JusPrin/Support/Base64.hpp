#pragma once

// Base64 for the fork's own boundaries: data URLs for the page, image parts
// for the provider. Header-only, dependency-free, and deliberately the only
// copy -- it was written three times before this file existed.

#include <array>
#include <cstdint>
#include <string>

namespace Slic3r::GUI::JusPrin {

inline std::string base64_encode(const std::string& input)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = -6;
    for (unsigned char byte : input) {
        value = (value << 8) + byte;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(value >> bits) & 0x3f]);
            bits -= 6;
        }
    }
    if (bits > -6)
        out.push_back(table[((value << 8) >> (bits + 8)) & 0x3f]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

inline std::string strip_data_url_prefix(const std::string& data)
{
    // Accept both a bare base64 payload and a full "data:...;base64,<payload>".
    const auto marker = data.find(";base64,");
    if (marker != std::string::npos)
        return data.substr(marker + 8);
    return data;
}

inline bool base64_decode(const std::string& in, std::string& out)
{
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const auto table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        for (int i = 0; i < 64; ++i)
            t[static_cast<unsigned char>(chars[i])] = i;
        return t;
    }();
    int val = 0, valb = -8;
    out.clear();
    for (unsigned char c : in) {
        if (c == '=')
            break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;
        const int decoded = table[c];
        if (decoded == -1)
            return false;
        val = (val << 6) + decoded;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return true;
}

} // namespace Slic3r::GUI::JusPrin
