#pragma once

// The rule every file of the local project store follows for the format
// number it carries:
//  - A change that a build without it still reads correctly, such as a new
//    field or a new list, keeps the number. Such a build keeps what it does
//    not know when it edits the file in place.
//  - A change that such a build would misread raises the number, and adds a
//    step to the file's list that brings a file of the previous number to it.
//    So does storing part of the model somewhere new: a build that ignored it
//    would open an incomplete model.
//  - A build reads every number from the file's first to its own, upgrading
//    an older file step by step as it reads it, and refuses a higher one: a
//    newer JusPrin wrote it, and reading it could lose what it holds.

#include <nlohmann/json.hpp>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

// A stored file with a format number above this build's own.
class NewerFormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

struct StoredFormat
{
    enum class Check { Readable, Newer, Unreadable };

    std::string key;   // the member that holds the number
    int         first; // the oldest number this build reads
    // steps[i] brings a file of number first + i to first + i + 1.
    std::vector<std::function<void(nlohmann::json&)>> steps;

    int current() const { return first + int(steps.size()); }

    // Brings `value` to current() in place. Newer for a number above it;
    // Unreadable, with `value` untouched, for a missing number or one below first.
    Check upgrade(nlohmann::json& value) const
    {
        if (!value.is_object() || !value.contains(key) || !value[key].is_number_integer())
            return Check::Unreadable;
        const int number = value[key].get<int>();
        if (number > current())
            return Check::Newer;
        if (number < first)
            return Check::Unreadable;
        for (int at = number; at < current(); ++at) {
            steps[std::size_t(at - first)](value);
            value[key] = at + 1;
        }
        return Check::Readable;
    }
};

} // namespace Slic3r::GUI::JusPrin::Workspace
