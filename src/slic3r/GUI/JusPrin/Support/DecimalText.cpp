#include "DecimalText.hpp"

#include <wx/intl.h>

#include <string>
#include <wx/string.h>

namespace Slic3r::GUI::JusPrin {

std::string app_decimal_text(double value, int min_decimals, int max_decimals)
{
    // The app's wxLocale follows its interface language (GUI_App::load_language).
    const wxString separator = wxLocale::GetInfo(wxLOCALE_DECIMAL_POINT, wxLOCALE_CAT_NUMBER);
    return decimal_text(value, min_decimals, max_decimals, separator.ToStdString(wxConvUTF8));
}

std::string app_whole_text(long long value)
{
    const std::string separator = wxLocale::GetInfo(wxLOCALE_THOUSANDS_SEP, wxLOCALE_CAT_NUMBER).ToStdString(wxConvUTF8);
    std::string digits = std::to_string(value < 0 ? -value : value);
    for (int at = int(digits.length()) - 3; at > 0; at -= 3)
        digits.insert(size_t(at), separator);
    return (value < 0 ? "-" : "") + digits;
}

} // namespace Slic3r::GUI::JusPrin
