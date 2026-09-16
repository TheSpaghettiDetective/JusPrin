#include "PrinterNames.hpp"

namespace Slic3r::GUI::JusPrin::Printers {

NameProblem check_printer_name(const std::string& name, const NamePredicate& reserved, const NamePredicate& taken)
{
    if (name.empty())
        return NameProblem::Empty;
    if (name.front() == ' ' || name.back() == ' ')
        return NameProblem::EdgeSpace;
    if (name.find_first_of(kIllegalNameCharacters) != std::string::npos)
        return NameProblem::IllegalCharacter;
    if (reserved(name))
        return NameProblem::Reserved;
    if (taken(name))
        return NameProblem::Taken;
    return NameProblem::None;
}

std::string first_free_printer_name(const std::string& base, const NamePredicate& taken)
{
    if (!taken(base))
        return base;
    for (int n = 2;; ++n) {
        std::string candidate = base + " (" + std::to_string(n) + ")";
        if (!taken(candidate))
            return candidate;
    }
}

} // namespace Slic3r::GUI::JusPrin::Printers
