#pragma once

// Standard base64 with padding, for the pictures tool results carry.
// GUI-free.

#include <string>

namespace Slic3r::GUI::JusPrin::Agent {

inline std::string base64_bytes(const std::string& input)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);
    std::size_t index = 0;
    for (; index + 2 < input.size(); index += 3) {
        const unsigned value = (unsigned char)input[index] << 16 | (unsigned char)input[index + 1] << 8 | (unsigned char)input[index + 2];
        out += table[value >> 18 & 63];
        out += table[value >> 12 & 63];
        out += table[value >> 6 & 63];
        out += table[value & 63];
    }
    if (index + 1 == input.size()) {
        const unsigned value = (unsigned char)input[index] << 16;
        out += table[value >> 18 & 63];
        out += table[value >> 12 & 63];
        out += "==";
    } else if (index + 2 == input.size()) {
        const unsigned value = (unsigned char)input[index] << 16 | (unsigned char)input[index + 1] << 8;
        out += table[value >> 18 & 63];
        out += table[value >> 12 & 63];
        out += table[value >> 6 & 63];
        out += '=';
    }
    return out;
}

} // namespace Slic3r::GUI::JusPrin::Agent
