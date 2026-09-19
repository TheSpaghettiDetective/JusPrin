#pragma once

// Base64 for the fork's own boundaries: data URLs for the page, image parts
// for the provider. Header-only, dependency-free, and deliberately the only
// copy -- it was written three times before this file existed.

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

} // namespace Slic3r::GUI::JusPrin
