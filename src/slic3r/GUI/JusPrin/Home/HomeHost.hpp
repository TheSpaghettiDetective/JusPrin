#pragma once

// The native side of the jusprin-home-bridge protocol: it answers the page's
// handshake, sends the snapshot the backend assembles, and turns the page's
// messages into backend actions. It holds no project or printer state of its
// own -- every hello is answered with a complete snapshot, so a page reload is
// always recoverable.
//
// No wx and no Orca types: the transport is injected and the application is
// reached through IHomeBackend, so the protocol can be exercised in a GUI-free
// test with a fake backend.

#include "HomeBackend.hpp"

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>

namespace Slic3r { namespace GUI { namespace JusPrin { namespace Home {

class HomeHost
{
public:
    // Receives one serialised envelope to hand to the page.
    using Send = std::function<void(const std::string&)>;

    HomeHost(IHomeBackend& backend, Send send);

    // A (re)load invalidates the handshake; nothing but hello is answered
    // until the new page introduces itself.
    void reset_page();

    void on_page_message(const std::string& text);

    // `added` names a printer the conversation just added: this one push
    // leads the printers list with it and asks the page to highlight it.
    void push_state(const std::string& added = {});
    // The live path, for a screen that stays up while its printers change
    // under it: re-reads only the printers, and sends the whole state only
    // when a card would look different from the last state sent. The printer
    // the last push_state named keeps its place and its highlight, so a live
    // push never reorders the column or cuts the highlight short. Projects
    // are re-read with any send; nothing that changes them alone happens
    // while Home is on screen. Returns whether it sent.
    bool refresh_if_changed();
    void push_appearance(bool dark);

    bool               connected() const { return m_connected; }
    unsigned long long messages_sent() const { return m_sent; }
    unsigned long long messages_received() const { return m_received; }

private:
    Snapshot collect() const;
    // The backend's printers, led by `m_added` when it is listed.
    std::vector<PrinterEntry> ordered_printers() const;
    void     send_state(Snapshot snapshot);
    void     send(const std::string& type, const nlohmann::json& payload, const std::string& correlation = {});

    IHomeBackend&      m_backend;
    Send               m_send;
    bool               m_connected{false};
    unsigned long long m_sent{0};
    unsigned long long m_received{0};
    unsigned long long m_next_id{1};
    // What the last push_state named, and the printers of the last state
    // sent, serialised as the page receives them: what refresh_if_changed
    // compares against.
    std::string        m_added;
    std::string        m_sent_printers;
};

}}}} // namespace Slic3r::GUI::JusPrin::Home
