#pragma once

// The native side of the jusprin-home-bridge protocol: it answers the page's
// handshake, assembles the Home snapshot from OrcaSlicer's own recent-project
// history and device layer, and turns the page's messages into the actions the
// shell already owns. It holds no project or printer state of its own -- every
// hello is answered with a complete snapshot, so a page reload is always
// recoverable.
//
// The transport is injected, so this class carries no wx window and can be
// driven from a test or from HomeWebView alike.

#include "HomeSnapshot.hpp"

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>

namespace Slic3r { namespace GUI {
class MainFrame;
}} // namespace Slic3r::GUI

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Workspace {
class SpoolStore;
}}}} // namespace Slic3r::GUI::JusPrin::Workspace

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class HomeHost
{
public:
    // Receives one serialised envelope to hand to the page.
    using Send = std::function<void(const std::string&)>;

    // `spools` may be null: the shell owns the one store, and Home renders
    // without swatches rather than opening a second writer to the same file.
    HomeHost(MainFrame& frame, Workspace::SpoolStore* spools, Send send);

    // A (re)load invalidates the handshake; nothing but hello is answered
    // until the new page introduces itself.
    void reset_page();

    void on_page_message(const std::string& text);

    void push_state();
    void push_appearance(bool dark);

    bool               connected() const { return m_connected; }
    unsigned long long messages_sent() const { return m_sent; }
    unsigned long long messages_received() const { return m_received; }

private:
    Snapshot collect() const;
    void     send(const std::string& type, const nlohmann::json& payload, const std::string& correlation = {});

    MainFrame&             m_frame;
    Workspace::SpoolStore* m_spools{nullptr};
    Send                   m_send;
    bool                   m_connected{false};
    unsigned long long     m_sent{0};
    unsigned long long     m_received{0};
    unsigned long long     m_next_id{1};
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
