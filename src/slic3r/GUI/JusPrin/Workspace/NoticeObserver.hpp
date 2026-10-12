#pragma once

// Tells one fork listener that OrcaSlicer's notification manager is about to
// age its notifications: the turn on which its list may have gained, changed
// or lost one. NotificationManager::update_notifications calls it first, on
// every idle turn of every canvas and whether or not the window is active, so
// the listener compares before it does any work. Fork-owned; OrcaSlicer
// reaches it through FileLoads.hpp, which NotificationManager.cpp already
// includes.
//
// GUI thread only.

#include <functional>

namespace Slic3r::GUI {

inline std::function<void()>& notifications_listener()
{
    static std::function<void()> listener;
    return listener;
}

// One listener at a time; an empty one removes it.
inline void set_notifications_listener(std::function<void()> listener) { notifications_listener() = std::move(listener); }

inline void report_notifications_updated()
{
    if (const std::function<void()>& listener = notifications_listener())
        listener();
}

} // namespace Slic3r::GUI
