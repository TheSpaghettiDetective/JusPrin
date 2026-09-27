#pragma once

// What the right half of the printer/filament chip says about the project's
// filament slots: one dot per slot, then a label.
//
// Kept apart from the widget so the rules -- which dots are full, which fade,
// when unused slots fold into "+n", when the label names a filament and when
// it counts them -- are plain data that tests can read without wx or Orca.

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin {

// One project slot, as the chip needs it.
struct ChipSlot
{
    std::string preset; // the filament preset; slots sharing one print the same filament
    std::string name;   // its short name
    std::string colour; // "#RRGGBB", or empty
    bool        used{false};
};

struct ChipDot
{
    std::size_t slot{0}; // 0-based
    std::string colour;
    bool        faded{false};
};

struct FilamentChipModel
{
    // In slot order.
    std::vector<ChipDot> dots;
    // Unused slots folded into a grey "+n" after the dots; zero draws nothing.
    std::size_t folded{0};
    // More slots in use than fit at full size: every one keeps its dot, and
    // the dots shrink.
    bool shrunk{false};
    // The short name when the slots in use share one filament; empty when the
    // label is a count instead.
    std::string name;
    // How many different filaments the slots in use print with.
    std::size_t filaments{0};
};

// Up to this many dots draw at full size.
constexpr std::size_t kFullSizeDots = 8;

// A plate with nothing on it uses no slot, and a chip of faded dots over a
// count would say nothing about the project. Every slot then reads as in use.
FilamentChipModel describe_filament_chip(const std::vector<ChipSlot>& slots);

} // namespace Slic3r::GUI::JusPrin
