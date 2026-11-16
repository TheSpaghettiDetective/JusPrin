#pragma once

#include <cmath>
#include <string>

namespace Slic3r::GUI::JusPrin {

// A number as text a person reads: at least `min_decimals` and at most
// `max_decimals` decimals, the trailing zeros between them dropped, and
// `separator` before the decimals. Built from digits, so the C library's
// locale, which differs between threads in the app, plays no part.
inline std::string decimal_text(double value, int min_decimals, int max_decimals, const std::string& separator)
{
    long long scale = 1;
    for (int i = 0; i < max_decimals; ++i)
        scale *= 10;
    const long long scaled = std::llround(std::fabs(value) * static_cast<double>(scale));
    std::string fraction = max_decimals > 0 ? std::to_string(scaled % scale) : std::string();
    fraction.insert(0, static_cast<size_t>(max_decimals) - fraction.size(), '0');
    while (static_cast<int>(fraction.size()) > min_decimals && fraction.back() == '0')
        fraction.pop_back();
    std::string text = (value < 0 && scaled != 0 ? "-" : "") + std::to_string(scaled / scale);
    return fraction.empty() ? text : text + separator + fraction;
}

// The same, with the decimal separator of the app's language: "0.4" in
// English, "0,4" in German, as OrcaSlicer's settings fields write it. Only
// for text a person reads; the model's words keep a decimal point. UTF-8.
std::string app_decimal_text(double value, int min_decimals, int max_decimals);

// A whole number with the thousands separator of the app's language:
// "1,980" in English, "1.980" in German. UTF-8.
std::string app_whole_text(long long value);

} // namespace Slic3r::GUI::JusPrin
