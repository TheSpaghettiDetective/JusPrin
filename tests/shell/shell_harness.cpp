// Runs the complete native application and verifies the production JusPrin
// shell through Phase 6: installation, Prepare/Slice/Print, the typed
// Agent bridge and tools, persistence, manufacturing history, resize
// and project replacement, fallback, and restoration of stock presentation.
//
// Modes:
//   --dark-ui / --light-ui  process-only appearance for visual checks. macOS
//              sets the Cocoa appearance; Windows writes dark_color_mode into
//              the run's own config, the only input GUI_App::dark_mode reads
//              there (the registry read in check_dark_mode is compiled out).
//   (default)  automated shell checks, exits with the result
//   --stock    automated stock-mode checks (shell disabled via app config)
//   --manual   installs nothing extra and leaves the app open for a human
//   --manual-live-agent
//              leaves the isolated JusPrin shell open with the OpenAI
//              provider enabled for hands-on testing
//   --manual-unconfigured
//              leaves the isolated JusPrin shell open with no Agent service
//              configured at all -- the dock's not-set-up empty state
//   --file-corpus <manifest.json> <output.json> [--file-corpus-config <config.json>]
//              loads real files through the native UI path and records each
//              file-load report beside the selected setup before and after
//   --printer-menu
//              checks the saved-printer rows and selection in the header menu
//   --printer-menu-capture <output-directory>
//              runs the same checks and captures the open menu
//   --task-chat
//              exercises printer and filament help from Prepare, Back, each
//              manual settings escape, and saved-chat isolation
//   --live-agent
//              uses OPENAI_API_KEY and verifies live context/attachment use,
//              reload recovery, native mutation, and the model follow-ups
//   --mcp     real TCP discovery/read and mutation using the shared runtime
//   --mcp-bridge  same native scenario through a persistent stdio subprocess
//   --mcp-setup   native setup-command lifetime, output and argument checks;
//                 uses this harness as a fixture, never edits client config
//   --manual-mcp <fixture-directory>
//                 leaves a two-plate native fixture open for real MCP clients;
//                 exposes Orca's sidebar for native-edit/stale-revision checks;
//                 reuse the directory to test restarts without reconfiguration
//   --header-visual <fixture-directory>
//                 same fixture without the expert sidebar, with the header menu open
//   --right-pane-visual <fixture-directory>
//                 production-like right-pane fixture: expert sidebar hidden,
//                 Agent open, no project-header menu covering the layout
//   --live-agent-unavailable
//              selects OpenAI with consent withheld and verifies that the
//              application stays usable without silently selecting the mock
//   --slice-all-cold
//              guards Slice all on a multi-plate project before the Preview
//              canvas has ever rendered — the conditions of upstream crash
//              #15116 (null all-plates stats item; fixed upstream by
//              83723e2a7b). Observed 2026-08-31: it PASSES on this tree
//              because the startup color-mode sync also runs
//              _init_select_plate_toolbar on the Preview canvas before any
//              render. Kept as a guard in case that incidental init path
//              changes; run it alongside the default and --stock modes.
//              The synchronous Prepare-only slice command made this reachable
//              on 2026-09-04. The no-auto-preview policy now also leaves the
//              unopened Preview's plate selection unchanged.
//   --recomputing-capture <output-directory>
//              slices the fixture, scales the cube to 120 mm and re-slices,
//              asserts the setup card says the estimates are recomputing,
//              and shows no earlier figure, while the slice runs, and writes
//              recomputing-agent-pane.png and
//              recomputing-shell.png to the directory (handoff item 6)
//   --setup-differences-capture <output-directory>
//              edits process, filament and per-object settings the way a
//              person does, asserts the setup card and the setup page name
//              every one of them, writes setup-differences-card.png,
//              setup-differences-page.png and setup-differences-page-end.png
//              to the directory, then puts the values back and asserts the
//              card reports no changes. It then records a purpose and applies
//              a patch through the agent's own tools, slices, and writes the
//              card just after the change (setup-differences-agent-updated),
//              the card a minute later (setup-differences-agent-card) and its
//              page (setup-differences-agent-page): the state the design's
//              first frame draws. That minute makes the run about two long.
//   --timeline-capture <output-directory>
//              records a build, mirrors the object three times and edits a
//              print setting by hand, asserts the thread's change rows, and writes
//              timeline-agent-pane-<light|dark>.png (revision-timeline B10)
//   --figma-timeline-capture <output-directory>
//              shows the entire First print story from the two Figma timeline
//              frames in the real Agent WebView, then captures it with the
//              revert confirmation closed and open. Uses deterministic records
//              and an isolated project; no OpenAI key or printer is needed.
//   --figma-timeline-manual
//              leaves that isolated timeline open for hands-on inspection.
//   --external-project-history [--external-project-history-capture <output-directory>]
//              checks the repeated external 3MF choice and explicit,
//              confirmed version restore; optionally captures a history row
//   --chat-restoration
//              checks historical viewing and explicit chat/model/planning
//              restoration in an isolated managed project
//   --printer-live
//              needs OPENAI_API_KEY: the printer-agent-tools handoff's worked
//              examples through the real printer panel and the live model --
//              words and taps sent as the page sends them, every exchange
//              printed as HARNESS LIVE lines, and the mechanical outcomes
//              checked (cards drawn or not, the settings change, the saved
//              nozzle, Undo). The key is written to this run's throwaway
//              app config, where the panel reads it.
//   --printer-settings-live
//              needs OPENAI_API_KEY: the header's Printer settings… on the
//              printer OrcaSlicer ships, as the fixture selects it, through
//              the live model: a retraction change is saved as
//              "<preset> - Copy"; the copy holds it, is
//              selected, and is what the conversation is about
//   --filament-settings-live
//              needs OPENAI_API_KEY: the header's Filament settings… for slot
//              1, which holds a filament OrcaSlicer ships, through the live
//              model: a nozzle temperature change is saved as
//              "<preset> - Copy"; the copy holds it and
//              takes the slot
//   --printer-live-no-plugin
//              needs OPENAI_API_KEY and a machine without the Bambu network
//              plug-in: adds a Bambu Lab printer through the live model and
//              asks to connect it, from the add flow and from Home's
//              Connect…, with the fake printer off. Checks what the app
//              shows: the opening names the plug-in, not LAN mode; its
//              notice is drawn once; no credential form opens; a connect the
//              model tries anyway is refused for the plug-in; and Done ends
//              it. The model's own words are printed for reading
//   --printer-connect-capture <output-directory>
//              needs OPENAI_API_KEY: connects a Klipper printer
//              through the real printer panel and the live model, against
//              print hosts on loopback ports, and writes a PNG of the panel
//              with the local credential form open and of Home after.
//              The pictures come from the embedded browser's own snapshot.
//   --manual-tool-strip
//              leaves the shell open on the two-plate fixture with an object
//              selected, for hands-on testing of the canvas tool strip
//   --print-issues
//              the print issues on the Prepare canvas, through their whole
//              life in the real app: OrcaSlicer's validation, placement,
//              slicing warnings and failures read back as issues; the bubble
//              following camera, zoom, resize and object moves and staying
//              off its object; both buttons through to the chat; stale and
//              cleared issues; deletion, undo, plate switch, project
//              replacement; OrcaSlicer's own notices not drawn, in Prepare
//              or in Check print
//   --print-issues-capture <output-directory>
//              the same run, saving the canvas's own pixels at each stage
//   --manual-print-issues / --manual-print-issues-no-agent
//              leaves two overlapping cubes open for hands-on use, with a
//              stand-in assistant that answers "Noted.", or with none
//   --tool-strip-capture <output-directory>
//              clicks the Prepare canvas's tool strip with real pointer
//              events: toggles Move and Scale, follows the Rotate shortcut,
//              duplicates and undoes, opens More (Windows), orbits and
//              narrows the window; asserts the open tool, selection and
//              instance count at each step, and writes tool-strip-*.png
//   --home-live
//              saves and connects the in-process fake Bambu printer, then
//              drives it Online, Printing and Offline while Home stays on
//              screen, reading the printer card from the Home page itself:
//              every change must arrive without leaving Home, a card menu
//              opened first must stay open, and nothing is sent while the
//              printer is unchanged or Home is hidden. About a minute; the
//              Offline step waits out the 30-second freshness window
//   --printer-setup
//              drives the Add a printer modal from the printer menu with the
//              deterministic recognizer: dismissal and scrim lifetime, the
//              choice/Not this one/Start over/correction transitions, adding a
//              shipped but disabled profile, the network state, Set it up
//              myself, and install rollback; then named printers: a second
//              printer of one model, Home's list, rename, remove, and a
//              nozzle change that keeps the printer's own settings

// First: on Windows asio needs winsock2.h ahead of any windows.h.
#include <boost/asio.hpp>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/client.h>
#endif

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Init.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentConfiguration.hpp"
#include "slic3r/GUI/JusPrin/Agent/AgentWebView.hpp"
#include "slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolRegistry.hpp"
#include "slic3r/GUI/JusPrin/Agent/ToolResults.hpp"
#include "../jusprin_support/DeterministicMockAgent.hpp"
#include "print_issues_agent.hpp"
#include <glad/gl.h>
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/JusPrin/Brand/BrandPalette.hpp"
#include "slic3r/GUI/JusPrin/Canvas/ViewportToolStrip.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellTheme.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
// FindWindowByName and ImGuiWindow: the tool panel is an ImGui window, and its
// rectangle is what the anchor checks read.
#include <imgui/imgui_internal.h>
#include "slic3r/GUI/JusPrin/Mcp/McpRuntime.hpp"
#include "../agent/mcp_test_client.hpp"
#include "mcp_stdio_client.hpp"
#include "figma_timeline_fixture.hpp"
#include "slic3r/GUI/JusPrin/Shell/AgentPane.hpp"
#include "slic3r/GUI/JusPrin/Shell/LeftPane.hpp"
#include "slic3r/GUI/JusPrin/Workspace/SettingsSupport.hpp"
#include "slic3r/GUI/JusPrin/Shell/McpSetupCommand.hpp"
#include "slic3r/GUI/JusPrin/Shell/ShellController.hpp"
#include "slic3r/GUI/JusPrin/Shell/PrinterFilamentChip.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/JusPrin/Shell/StatusRow.hpp"
#include "slic3r/GUI/JusPrin/Shell/HeaderControls.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/OrcaPrinterBackend.hpp"
#include "slic3r/GUI/JusPrin/Testing/FakeBambuAgent.hpp"
#include "slic3r/Utils/NetworkAgentFactory.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/JusPrin/Agent/ProjectPersistence.hpp"

#include <deque>
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.hpp"
#include "slic3r/GUI/JusPrin/PrinterSetup/PrinterPanel.hpp"
#include "slic3r/GUI/JusPrin/Printers/NamedPrinters.hpp"
#include "slic3r/GUI/JusPrin/Home/HomeWebView.hpp"
#include "slic3r/GUI/JusPrin/Home/OrcaHomeBackend.hpp"
#include "slic3r/GUI/JusPrin/Home/PrinterWindow.hpp"
#include "slic3r/GUI/JusPrin/Shell/SetupCommands.hpp"
#include "slic3r/GUI/WebGuideDialog.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_Preview.hpp"
#include "slic3r/GUI/Auxiliary.hpp"
#include "slic3r/GUI/Project.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"
#include "slic3r/GUI/SyncAmsInfoDialog.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <wx/app.h>
#include <wx/base64.h>
#include <wx/dcmemory.h>
#include <wx/dcscreen.h>
#include <wx/glcanvas.h>
#include <wx/dialog.h>
#include <wx/dnd.h>
#include <wx/hyperlink.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/webview.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace fs = boost::filesystem;

#ifdef __APPLE__
void set_harness_appearance(bool dark);
bool snapshot_web_view(void* native_web_view, const char* path);
#endif

#ifdef _WIN32
// WebView2 can snapshot its page while the desktop is disconnected. The
// screen blit below remains for native controls, but task chats are web views.
bool snapshot_web_view(void* native_web_view, const char* path)
{
    auto* view = static_cast<ICoreWebView2*>(native_web_view);
    if (view == nullptr)
        return false;
    Microsoft::WRL::ComPtr<IStream> stream;
    if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())))
        return false;
    struct Result { bool done{false}; HRESULT status{E_FAIL}; };
    auto result = std::make_shared<Result>();
    auto handler = Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>(
        [result](HRESULT status) -> HRESULT {
            result->status = status;
            result->done = true;
            return S_OK;
        });
    if (FAILED(view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(), handler.Get())))
        return false;
    for (int attempt = 0; attempt < 100 && !result->done; ++attempt) {
        wxYield();
        wxMilliSleep(20);
    }
    if (!result->done || FAILED(result->status))
        return false;
    STATSTG stat{};
    LARGE_INTEGER beginning{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0 ||
        FAILED(stream->Seek(beginning, STREAM_SEEK_SET, nullptr)))
        return false;
    std::ofstream out(path, std::ios::binary);
    std::vector<char> buffer(64 * 1024);
    auto remaining = stat.cbSize.QuadPart;
    while (out && remaining > 0) {
        const ULONG requested = static_cast<ULONG>(std::min<ULONGLONG>(remaining, buffer.size()));
        ULONG read = 0;
        if (FAILED(stream->Read(buffer.data(), requested, &read)) || read == 0)
            return false;
        out.write(buffer.data(), read);
        remaining -= read;
    }
    return out.good() && remaining == 0;
}
#endif

namespace Slic3r::GUI::JusPrin {
namespace {

bool preview_has_subject(const std::string& url)
{
    constexpr const char* prefix = "data:image/png;base64,";
    if (url.rfind(prefix, 0) != 0)
        return false;
    const auto bytes = wxBase64Decode(url.c_str() + std::strlen(prefix));
    if (bytes.IsEmpty())
        return false;
    wxMemoryInputStream stream(bytes.GetData(), bytes.GetDataLen());
    wxImage image;
    if (!image.LoadFile(stream, wxBITMAP_TYPE_PNG) || !image.IsOk())
        return false;
    const auto* alpha = image.GetAlpha();
    if (alpha == nullptr)
        return true;
    return std::any_of(alpha, alpha + image.GetWidth() * image.GetHeight(), [](unsigned char value) {
        return value != 0;
    });
}

// Labels reach the widgets through _L, which decodes its narrow literal as
// UTF-8 explicitly (I18N.hpp). wxString's own narrow constructor instead
// decodes through the locale's encoding: UTF-8 on macOS, but the ANSI code
// page on Windows, where the three bytes of "…" become three separate
// characters. A lookup written as a bare literal therefore matches on macOS
// and silently misses on Windows -- silently because a missing row reads as
// "the menu does not contain this", which is what several of these checks
// assert. Every name below that carries a non-ASCII character goes through
// here so both sides decode the same way.
wxString ui_name(const char* utf8) { return wxString::FromUTF8(utf8); }

// The same hazard for a name read back out of a widget: menu_row_names hands
// back UTF-8, and letting that std::string convert itself to wxString would
// re-decode it through the locale and undo the round trip on Windows.
wxString ui_name(const std::string& utf8) { return wxString::FromUTF8(utf8.c_str()); }

// A print host on a loopback port, for connection tests without hardware.
// Each request is held for `hold` (until the stand-in goes, when it is
// negative), then either answered as Moonraker's /server/info or hung up on.
class StandInHost
{
public:
    StandInHost(std::chrono::milliseconds hold, bool answer)
        : m_acceptor(m_io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0))
        , m_hold(hold)
        , m_answer(answer)
    {
        m_thread = std::thread([this] { serve(); });
    }
    ~StandInHost()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
            // A client can connect and never send a request -- a web view
            // opening spare connections does -- which would hold the serving
            // thread in its read for good.
            if (m_serving != nullptr) {
                boost::system::error_code ignored;
                m_serving->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
            }
        }
        m_released.notify_all();
        // Wakes a blocking accept.
        boost::system::error_code ignored;
        boost::asio::ip::tcp::socket wake(m_io);
        wake.connect(m_acceptor.local_endpoint(), ignored);
        m_thread.join();
    }
    std::string address() const { return "http://127.0.0.1:" + std::to_string(m_acceptor.local_endpoint().port()); }
    int requests() const { return m_requests; }

private:
    void serve()
    {
        for (;;) {
            boost::system::error_code error;
            boost::asio::ip::tcp::socket socket(m_io);
            m_acceptor.accept(socket, error);
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_stopping || error)
                return;
            ++m_requests;
            boost::asio::streambuf request;
            m_serving = &socket;
            lock.unlock();
            boost::asio::read_until(socket, request, "\r\n\r\n", error);
            lock.lock();
            m_serving = nullptr;
            if (m_hold.count() < 0)
                m_released.wait(lock, [this] { return m_stopping; });
            else
                m_released.wait_for(lock, m_hold, [this] { return m_stopping; });
            if (m_answer && !m_stopping) {
                const std::string body = R"({"result":{"klippy_state":"ready"}})";
                const std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                boost::asio::write(socket, boost::asio::buffer(reply), error);
            }
            socket.close(error);
        }
    }

    boost::asio::io_context        m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::chrono::milliseconds      m_hold;
    bool                           m_answer;
    std::atomic<int>               m_requests{0};
    std::mutex                     m_mutex;
    std::condition_variable        m_released;
    bool                           m_stopping{false};
    boost::asio::ip::tcp::socket*  m_serving{nullptr}; // the connection being read, under m_mutex
    std::thread                    m_thread;
};

// A Moonraker host with a multi-filament unit, on a loopback port: answers
// /server/info as a connection test expects, and the two lane sources from
// whatever the test last set -- a body, or nothing (a 404) for lane_data.
class StandInMoonraker
{
public:
    StandInMoonraker() : m_acceptor(m_io, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0))
    {
        m_thread = std::thread([this] { serve(); });
    }
    ~StandInMoonraker()
    {
        m_stopping = true;
        boost::system::error_code    ignored;
        boost::asio::ip::tcp::socket wake(m_io);
        wake.connect(m_acceptor.local_endpoint(), ignored);
        m_thread.join();
    }
    std::string address() const { return "http://127.0.0.1:" + std::to_string(m_acceptor.local_endpoint().port()); }
    // Empty: the namespace was never written, which Moonraker answers with 404.
    void set_lane_data(std::string body)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lane_data = std::move(body);
    }
    void set_mmu(std::string status)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mmu = std::move(status);
    }
    int requests(const std::string& path_prefix) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int count = 0;
        for (const std::string& path : m_paths)
            count += path.rfind(path_prefix, 0) == 0 ? 1 : 0;
        return count;
    }

private:
    void serve()
    {
        for (;;) {
            boost::system::error_code    error;
            boost::asio::ip::tcp::socket socket(m_io);
            m_acceptor.accept(socket, error);
            if (m_stopping || error)
                return;
            boost::asio::streambuf request;
            boost::asio::read_until(socket, request, "\r\n\r\n", error);
            std::string line;
            std::istream stream(&request);
            std::getline(stream, line); // "GET /path HTTP/1.1"
            const size_t      start = line.find(' ') + 1;
            const std::string path  = line.substr(start, line.find(' ', start) - start);
            unsigned          status = 200;
            std::string       body;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_paths.push_back(path);
                if (path.rfind("/server/info", 0) == 0) {
                    body = R"({"result":{"klippy_state":"ready","moonraker_version":"harness"}})";
                } else if (path.rfind("/server/database/item?namespace=lane_data", 0) == 0) {
                    if (m_lane_data.empty()) {
                        status = 404;
                        body   = R"({"error":{"code":404,"message":"Namespace 'lane_data' not found"}})";
                    } else {
                        body = m_lane_data;
                    }
                } else if (path.rfind("/printer/objects/query?mmu", 0) == 0) {
                    body = R"({"result":{"eventtime":1.0,"status":{)" +
                           (m_mmu.empty() ? std::string() : R"("mmu":)" + m_mmu) + "}}}";
                } else {
                    status = 404;
                    body   = R"({"error":{"code":404,"message":"Not Found"}})";
                }
            }
            const std::string reply = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : " Not Found") +
                                      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                                      "\r\nConnection: close\r\n\r\n" + body;
            boost::asio::write(socket, boost::asio::buffer(reply), error);
            socket.close(error);
        }
    }

    boost::asio::io_context        m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::atomic<bool>              m_stopping{false};
    mutable std::mutex             m_mutex;
    std::string                    m_lane_data;
    std::string                    m_mmu;
    std::vector<std::string>       m_paths;
    std::thread                    m_thread;
};

// A model that answers the first message by calling `tool` with `arguments`,
// and anything after that with a line of text.
class ToolCallingAgent final : public Agent::IAgentService
{
public:
    ToolCallingAgent(std::string tool, nlohmann::json arguments) : m_tool(std::move(tool)), m_arguments(std::move(arguments)) {}

    bool ready() const override { return true; }
    bool busy() const override { return m_active; }
    bool start(const Agent::AgentRequest& request) override
    {
        m_active = true;
        if (request.purpose != Agent::AgentRequest::Purpose::ConversationTitle && !m_called) {
            m_called = true;
            m_events.push_back(Agent::AgentEvent::tool_call({"call-1", Agent::ToolRequest{m_tool, m_arguments.dump()}, true}));
        } else {
            m_events.push_back(Agent::AgentEvent::delta("Checking."));
            m_events.push_back(Agent::AgentEvent::completed());
        }
        return true;
    }
    bool continue_after_tool(const Agent::AgentToolResult&) override
    {
        m_events.push_back(Agent::AgentEvent::delta("Checking."));
        m_events.push_back(Agent::AgentEvent::completed());
        return true;
    }
    void cancel() override
    {
        m_active = false;
        m_events.clear();
    }
    std::optional<Agent::AgentEvent> poll() override
    {
        if (m_events.empty())
            return std::nullopt;
        Agent::AgentEvent event = std::move(m_events.front());
        m_events.pop_front();
        if (event.kind == Agent::AgentEventKind::Completed || event.kind == Agent::AgentEventKind::Failed)
            m_active = false;
        return event;
    }

private:
    std::string                   m_tool;
    nlohmann::json                m_arguments;
    std::deque<Agent::AgentEvent> m_events;
    bool                          m_active{false};
    bool                          m_called{false};
};

// The recording agent the project chat was given in the print-issues modes,
// for the scenario to read.
RecordingAgent* g_issue_agent = nullptr;
// --manual-print-issues-no-agent: the same fixture with no Agent service, to
// see what a press of either button does before the assistant is set up.
bool g_issue_no_agent = false;

// wxString::ToStdString() narrows through that same locale converter and
// returns an empty string when a character will not fit the code page. Names
// compared as std::string are read as UTF-8 for the same reason.
std::string ui_text(const wxString& name) { return name.ToUTF8().data(); }

struct HarnessState
{
    enum class Mode {
        Shell,
        Stock,
        Manual,
        ManualLiveAgent,
        ManualUnconfigured,
        SliceAllCold,
        LiveAgent,
        Mcp,
        McpSetup,
        ManualMcp,
        LiveAgentUnavailable,
        RecomputingCapture,
        SetupDifferencesCapture,
        TimelineCapture,
        FigmaTimelineCapture,
        ToolStripCapture,
        ManualToolStrip,
        PrintIssues,
        ManualPrintIssues,
        PrinterMenu,
        ClassicSwitch,
        TaskChat,
        PrinterSetup,
        PrinterLive,
        HomeLive,
        AutosaveSeed,
        AutosaveReopen,
        PresetClose,
        ExternalProjectHistory,
        ChatRestoration,
        FileCorpus,
        LeftPane,
        LeftPaneEditor,
        LeftPaneSlicedCapture
    };

    std::atomic<int>  result{-1};
    std::atomic<bool> stop{false};
    std::shared_ptr<void> runner;
    // Phase 4 added real project saves and reopen on top of two full
    // slices; 300 s was regularly exhausted mid-flow. The
    // warm Slice-all scenario adds up to two more plate slices.
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::now() + std::chrono::seconds(900)};
    Mode mode{Mode::Shell};
    bool mcp_bridge{false};
    bool header_visual{false};
    bool right_pane_visual{false};
    bool task_chat_configured{false};
    bool figma_timeline_manual{false};
    std::optional<bool> dark_appearance;
    fs::path capture_dir;
    fs::path file_corpus_manifest;
    fs::path file_corpus_output;
    fs::path file_corpus_config;
    // --printer-connect-capture: the connect flow only, pictured.
    bool connect_capture{false};
    // --printer-live-no-plugin: connecting a Bambu Lab printer without the
    // network plug-in, and without the fake printer standing in for it.
    bool no_plugin{false};
    // --printer-settings-live, --filament-settings-live: a settings change to
    // the printer, or slot 1's filament, OrcaSlicer ships, from the header.
    bool settings_live{false};
    bool filament_settings_live{false};
    bool keep_data{false};
    std::shared_ptr<JusPrinTest::StdioClient> bridge;
};

Notebook* find_notebook(wxWindow* root, Plater* plater)
{
    if (auto* notebook = dynamic_cast<Notebook*>(root))
        if (notebook->FindPage(plater) == MainFrame::tp3DEditor) return notebook;
    for (wxWindow* child : root->GetChildren())
        if (Notebook* notebook = find_notebook(child, plater))
            return notebook;
    return nullptr;
}

class Scenario final : public std::enable_shared_from_this<Scenario>
{
public:
    Scenario(GUI_App& app, std::shared_ptr<HarnessState> state) : m_app(app), m_state(std::move(state))
    {
        m_poll_handler.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll_wait(); });
        m_exit_handler.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            std::cerr << "HARNESS ERROR main loop still running 30 s after the frame was closed; forcing ExitMainLoop\n";
            m_app.ExitMainLoop();
        });
    }

    void start()
    {
        try {
            m_plater = m_app.plater();
            m_frame  = m_app.mainframe;
            check(m_plater != nullptr && m_frame != nullptr, "application_ready");
            m_notebook = find_notebook(m_frame, m_plater);
            check(m_notebook != nullptr, "notebook_found");
            if (m_plater == nullptr || m_frame == nullptr || m_notebook == nullptr) {
                fail("application widgets missing");
                return;
            }
            // GUI_App::post_init runs from the first idle event after the main
            // loop starts and ends with select_tab(0) (GUI_App.cpp, "if
            // (is_editor()) mainframe->select_tab(size_t(0))"). This scenario
            // starts from a CallAfter, which the loop drains before it idles,
            // so every mode used to drive the shell ahead of post_init, and
            // post_init later undid whatever tab the scenario had chosen --
            // which is how the three visual modes handed over a Home screen
            // while reporting failures=0. On a software renderer the first
            // idle arrived seconds after the fixture had finished slicing.
            wait_until_settled("startup_post_init_complete",
                               [self = shared_from_this()] { self->run_mode(); });
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

    void run_mode()
    {
        try {
            if (m_state->mode == HarnessState::Mode::Stock) {
                wait_until([this] { return m_plater->canvas3D()->is_initialized() &&
                    m_notebook->GetSelection() == MainFrame::tpHome; }, "stock_startup_ready",
                           [self = shared_from_this()] { self->verify_stock_slice(); });
                return;
            }
            verify_shell_installed();
            if (m_state->mode == HarnessState::Mode::ClassicSwitch) {
                verify_uninstall_restores_stock([self = shared_from_this()] { self->finish(); });
                return;
            }
            verify_agent_pane_follows_page([self = shared_from_this()] { self->run_shell_mode(); });
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

    void run_shell_mode()
    {
        try {
            if (m_state->mode == HarnessState::Mode::AutosaveSeed) {
                verify_autosave_seed();
                return;
            }
            if (m_state->mode == HarnessState::Mode::AutosaveReopen) {
                verify_autosave_reopen();
                return;
            }
            if (m_state->mode == HarnessState::Mode::PresetClose) {
                verify_preset_close();
                return;
            }
            if (m_state->mode == HarnessState::Mode::ExternalProjectHistory) {
                verify_external_project_history();
                return;
            }
            if (m_state->mode == HarnessState::Mode::ChatRestoration) {
                verify_chat_restoration();
                return;
            }
            if (m_state->mode == HarnessState::Mode::FileCorpus) {
                capture_file_corpus();
                finish();
                return;
            }
            if (m_state->mode == HarnessState::Mode::PrinterMenu) {
                check(selected_printer() == kSetupFixturePrinter, "printer_menu_fixture_starts_on_system_profile");
                verify_other_printers_menu();
                finish();
                return;
            }
            if (m_state->mode == HarnessState::Mode::PrinterSetup) {
                verify_printer_setup();
                return;
            }
            if (m_state->mode == HarnessState::Mode::PrinterLive) {
                begin_printer_live();
                return;
            }
            if (m_state->mode == HarnessState::Mode::HomeLive) {
                begin_home_live();
                return;
            }
            if (m_state->mode == HarnessState::Mode::McpSetup) {
                verify_mcp_setup();
                finish();
                return;
            }
            if (m_state->mode == HarnessState::Mode::FigmaTimelineCapture) {
                m_frame->select_tab(size_t(MainFrame::tp3DEditor));
                wait_until([this] {
                    return m_notebook->GetSelection() == MainFrame::tp3DEditor &&
                           m_plater->canvas3D()->is_initialized();
                }, "figma_timeline_canvas_ready", [self = shared_from_this()] {
                    self->load_multi_plate_fixture();
                    self->verify_canvas_interaction();
                    self->wait_for_agent_page("figma_timeline", [self] { self->begin_figma_timeline_capture(); });
                });
                return;
            }
            load_multi_plate_fixture();
            if (m_state->mode == HarnessState::Mode::LeftPaneEditor) {
                verify_left_pane_editor();
                return;
            }
            if (m_state->mode == HarnessState::Mode::LeftPaneSlicedCapture) {
                capture_sliced_left_pane();
                return;
            }
            if (m_state->mode == HarnessState::Mode::LeftPane) {
                verify_left_pane();
                return;
            }
            if (m_state->mode == HarnessState::Mode::TaskChat) {
                m_frame->select_tab(size_t(MainFrame::tp3DEditor));
                wait_until([this] {
                    return m_notebook->GetSelection() == MainFrame::tp3DEditor && m_notebook->IsShownOnScreen();
                }, "task_chat_starts_on_prepare", [self = shared_from_this()] {
                    self->verify_header_setup(Preset::TYPE_PRINTER);
                });
                return;
            }
            if (m_state->mode == HarnessState::Mode::ManualMcp) {
                verify_canvas_interaction();
                prepare_mcp_slice([self = shared_from_this()] {
                    // Real-client tests need a native setting edit while the
                    // MCP fixture remains live.
                    if (self->m_state->header_visual) {
                        installed_shell()->status_row()->show_action_menu();
                    } else if (!self->m_state->right_pane_visual) {
                        self->m_plater->set_sidebar_available(true);
                        self->m_plater->collapse_sidebar(false);
                        self->check(self->m_plater->sidebar().IsShown(), "mcp_fixture_native_sidebar_visible");
                    } else {
                        self->check(!self->m_plater->sidebar().IsShown(),
                                    "right_pane_fixture_native_sidebar_hidden");
                    }
                    // The checks above fire at the instant of navigation.
                    // Anything deferred -- a queued page change, a CallAfter
                    // from a load handler, a late post_init -- lands after
                    // them, so the tab is asserted once more when the loop
                    // has actually been idle, and the READY line carries
                    // that result.
                    self->wait_until_settled("mcp_fixture_settled", [self] {
                        self->check(self->m_notebook->GetSelection() == MainFrame::tp3DEditor,
                                    "mcp_fixture_tab_survives_settle");
                        self->check(!self->m_plater->is_preview_shown(), "mcp_fixture_view_survives_settle");
                        // The hand-over screen is what gets photographed, so
                        // the page's own error pane is re-checked last.
                        self->check(!installed_shell()->agent_pane()->web_view().bridge_error_shown(),
                                    "mcp_fixture_agent_page_healthy_at_handover");
                        std::cerr << "HARNESS MCP FIXTURE READY failures=" << self->m_failures
                                  << " discovery=" << data_dir() << "/jusprin/mcp.json\n";
                    });
                });
                return;
            }
            if (m_state->mode == HarnessState::Mode::Mcp) {
                verify_canvas_interaction();
                prepare_mcp_slice([self = shared_from_this()] { self->begin_mcp(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::LiveAgent) {
                verify_canvas_interaction();
                verify_live_agent();
                return;
            }
            if (m_state->mode == HarnessState::Mode::LiveAgentUnavailable) {
                verify_canvas_interaction();
                verify_unavailable_agent();
                return;
            }
            if (m_state->mode == HarnessState::Mode::RecomputingCapture) {
                verify_canvas_interaction();
                wait_for_agent_page("recomputing", [self = shared_from_this()] { self->begin_recomputing_capture(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::SetupDifferencesCapture) {
                verify_canvas_interaction();
                wait_for_agent_page("setup_differences", [self = shared_from_this()] { self->begin_setup_differences_capture(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::TimelineCapture) {
                verify_canvas_interaction();
                wait_for_agent_page("timeline", [self = shared_from_this()] { self->begin_timeline_capture(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::PrintIssues) {
                m_issue_agent = g_issue_agent;
                wait_for_agent_page("print_issues", [self = shared_from_this()] { self->begin_print_issues(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::ManualPrintIssues) {
                // Two overlapping cubes, left open for hands-on use.
                load_multi_plate_fixture();
                PartPlateList& plates = m_plater->get_partplate_list();
                plates.get_plate(0)->add_instance(1, 0, true);
                m_plater->canvas3D()->reload_scene(true, true);
                move_object(1, Vec3d(10, 0, 0));
                wait_until_settled("manual_print_issues_ready", [self = shared_from_this()] {
                    std::cerr << "HARNESS MANUAL READY print-issues failures=" << self->m_failures << '\n';
                });
                return;
            }
            if (m_state->mode == HarnessState::Mode::ManualToolStrip) {
                verify_canvas_interaction();
                wait_until_settled("manual_tool_strip_ready", [self = shared_from_this()] {
                    std::cerr << "HARNESS MANUAL READY tool-strip failures=" << self->m_failures << '\n';
                });
                return;
            }
            if (m_state->mode == HarnessState::Mode::ToolStripCapture) {
                verify_canvas_interaction();
                wait_until_settled("tool_strip_selection_settled", [self = shared_from_this()] { self->tool_strip_idle(); });
                return;
            }
            if (m_state->mode == HarnessState::Mode::SliceAllCold) {
                wait_for_agent_page("cold", [self = shared_from_this()] { self->begin_slice_all_cold(); });
                return;
            }
            verify_canvas_interaction();
            wait_for_agent_page("shell", [self = shared_from_this()] { self->begin_slice_check(); });
        } catch (const std::exception& error) {
            fail(std::string("exception: ") + error.what());
        } catch (...) {
            fail("unknown exception");
        }
    }

private:
    void verify_left_pane_editor()
    {
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        wait_until([this] {
            return m_notebook->GetSelection() == MainFrame::tp3DEditor && m_plater->canvas3D()->is_initialized();
        }, "editor_prepare_ready", [self = shared_from_this()] {
            self->wait_until_settled("editor_prepare_settled", [self] {
                if (installed_shell()->is_left_pane_collapsed()) {
                    installed_shell()->toggle_left_pane();
                    wxYield();
                }
                LeftPane* pane = installed_shell()->left_pane();
                self->check(pane != nullptr && pane->IsShownOnScreen(), "editor_pane_visible");
                if (pane == nullptr) {
                    self->finish();
                    return;
                }
                pane->show_project();
                pane->snapshot();
                const wxRect edit = pane->action_rect(_L("Edit Project Info"));
                self->check(!edit.IsEmpty(), "editor_action_visible");
                if (edit.IsEmpty()) {
                    self->finish();
                    return;
                }
                self->click_pane(pane, edit);
                self->wait_until([self] { return self->m_notebook->GetCurrentPage() == self->m_frame->m_project; },
                                 "editor_project_page_open", [self] {
                                     self->wait_until_settled("editor_page_settled", [self] {
                                         self->check(self->m_frame->m_project->IsShownOnScreen(),
                                                     "editor_project_page_visible");
                                         self->check(std::any_of(self->m_frame->m_project->GetChildren().begin(),
                                                                 self->m_frame->m_project->GetChildren().end(),
                                                                 [](wxWindow* child) {
                                                                     return dynamic_cast<AuxiliaryPanel*>(child) != nullptr &&
                                                                            child->IsShownOnScreen();
                                                                 }), "editor_form_visible");
                                         self->finish();
                                     });
                                 });
            });
        });
    }

    void capture_sliced_left_pane()
    {
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        wait_until([this] {
            return m_notebook->GetSelection() == MainFrame::tp3DEditor && m_plater->canvas3D()->is_initialized();
        }, "sliced_pane_prepare_ready", [self = shared_from_this()] {
            self->wait_until_settled("sliced_pane_settled", [self] {
                if (installed_shell()->is_left_pane_collapsed()) {
                    installed_shell()->toggle_left_pane();
                    wxYield();
                }
                self->m_plater->select_plate(0);
                installed_shell()->status_row()->request_slice();
                self->wait_until([self] {
                    PartPlate* first = self->m_plater->get_partplate_list().get_plate(0);
                    return first != nullptr && first->is_slice_result_valid() &&
                           !self->m_plater->is_background_process_slicing();
                }, "sliced_pane_plate_one_complete", [self] {
                    LeftPane* pane = installed_shell()->left_pane();
                    self->check(pane != nullptr, "sliced_pane_exists");
                    if (pane != nullptr) {
                        pane->refresh_from_workspace();
                        self->check(pane->rows().size() == 3 && pane->rows()[0].sliced && !pane->rows()[2].sliced,
                                    "sliced_pane_reads_real_plate_states");
                        self->save_pane("figma-sliced-and-unsliced-plates", pane->snapshot());
                        self->window_shot("figma-sliced-and-unsliced-plates");
                    }
                    self->finish();
                });
            });
        });
    }

    // The Plates / Project pane in the real shell: what Prepare shows, what a
    // click on each drawn control does to Orca, and what the pointer must not
    // disturb. Clicks go through the pane's own mouse handler at the place the
    // control was painted, so a hit region that drifted from its drawing fails.
    void verify_left_pane()
    {
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        wait_until([this] {
            return m_notebook->GetSelection() == MainFrame::tp3DEditor && m_plater->canvas3D()->is_initialized();
        }, "left_pane_prepare_ready", [self = shared_from_this()] {
            self->wait_until_settled("left_pane_settled", [self] { self->run_left_pane_checks(); });
        });
    }

    void click_pane(LeftPane* pane, const wxRect& rect, wxEventType type = wxEVT_LEFT_DOWN)
    {
        wxMouseEvent event(type);
        event.SetEventObject(pane);
        event.SetPosition(wxPoint(rect.x + rect.width / 2, rect.y + rect.height / 2));
        pane->GetEventHandler()->ProcessEvent(event);
    }

    void run_left_pane_checks()
    {
        LeftPane* pane = installed_shell()->left_pane();
        check(pane != nullptr, "left_pane_exists");
        if (pane == nullptr) {
            finish();
            return;
        }
        check(installed_shell()->is_left_pane_collapsed() && !pane->IsShown(),
              "left_pane_fixture_starts_at_the_closed_default");
        installed_shell()->toggle_left_pane();
        wxYield();
        check(pane->IsShownOnScreen(), "left_pane_shown_on_prepare");
        check(pane->GetSize().x == pane->FromDIP(200), "left_pane_is_200_dip_wide");
        auto* open_toggle = wxWindow::FindWindowByName("Plates and Project panel", pane);
        check(open_toggle != nullptr && open_toggle->IsShown(), "left_pane_open_toggle_has_its_own_header_row");
        auto* workspace = installed_shell()->workspace();
        const auto plate_count = [&] { return workspace->outline().plates.size(); };

        pane->refresh_from_workspace();
        wxBitmap first = pane->snapshot();
        check(pane->tab_rect(PaneTab::Plates).y >= pane->FromDIP(56),
              "left_pane_tabs_sit_below_the_toggle_row");
        check(pane->rows().size() == 3 && pane->rows()[0].kind == PaneRow::Kind::Plate && pane->rows()[0].expanded &&
                  pane->rows()[1].kind == PaneRow::Kind::Object && pane->rows()[2].kind == PaneRow::Kind::Plate &&
                  !pane->rows()[2].expanded,
              "left_pane_lists_two_plates_with_the_active_one_open");
        check(pane->rows()[2].summary.objects == 1, "left_pane_folded_plate_summarises_its_object");
        save_pane("left-pane-two-plates", first);
        window_shot("two-plates");

        // Clicking a folded plate's summary makes that Orca plate the active one.
        const auto second_plate = pane->rows()[2].plate;
        const wxRect folded_plate = pane->rect_of(2, LeftPane::Part::Row);
        const int title_height = pane->FromDIP(brand_theme()->metrics().left_pane.plate_row_height);
        check(folded_plate.height > title_height, "left_pane_folded_summary_is_in_hit_region");
        click_pane(pane, wxRect(folded_plate.x, folded_plate.y + title_height,
                                folded_plate.width, folded_plate.height - title_height));
        check(workspace->snapshot().active_plate == second_plate, "left_pane_click_activates_the_plate");
        check(m_plater->get_partplate_list().get_curr_plate_index() == 1, "left_pane_click_reached_orca");
        pane->snapshot();
        check(pane->rows().size() == 3 && pane->rows()[0].kind == PaneRow::Kind::Plate && !pane->rows()[0].expanded &&
                  pane->rows()[1].kind == PaneRow::Kind::Plate && pane->rows()[1].expanded,
              "left_pane_follows_the_plate_change");
        save_pane("left-pane-second-plate-active", pane->snapshot());
        window_shot("second-plate-active");

        // Canvas selection comes back into the pane without delay.
        // The second object sits on the plate that is now active.
        check(m_plater->select_object(1), "left_pane_canvas_selection_command");
        pane->snapshot();
        check(std::any_of(pane->rows().begin(), pane->rows().end(),
                          [](const PaneRow& row) { return row.kind == PaneRow::Kind::Object && row.selected; }),
              "left_pane_reflects_canvas_selection");
        save_pane("left-pane-object-selected", pane->snapshot());
        window_shot("object-selected");

        // Overflow menus: the plate's and the object's list what Orca offers,
        // and the commands behind them reach Orca and its undo history.
        const auto pane_menu = [pane]() -> HeaderMenu* {
            for (auto* child : pane->GetChildren())
                if (auto* menu = dynamic_cast<HeaderMenu*>(child); menu && menu->IsShown()) return menu;
            return nullptr;
        };
        const auto menu_has = [](HeaderMenu* menu, const wxString& label) {
            return menu != nullptr && wxWindow::FindWindowByName(label, menu) != nullptr;
        };
        pane->snapshot();
        const PaneRow plate_row = pane->rows()[1]; // the active plate: the second, now
        check(plate_row.kind == PaneRow::Kind::Plate && plate_row.expanded, "left_pane_menu_fixture_active_plate");
        pane->open_row_menu(plate_row);
        HeaderMenu* menu = pane_menu();
        check(menu_has(menu, _L("Rename")) && menu_has(menu, _L("Arrange")) && menu_has(menu, _L("Auto-orient")) &&
                  menu_has(menu, _L("Lock")) && menu_has(menu, _L("Plate settings…")) && menu_has(menu, _L("Delete plate")) &&
                  menu_has(menu, _L("Move to front")),
              "left_pane_plate_menu_lists_the_plate_actions");
        save_pane("left-pane-plate-menu", pane->snapshot());
        menu_shot("plate-menu");
        if (menu != nullptr)
            menu->close();
        pane->snapshot();
        pane->open_row_menu(pane->rows()[0]); // the first plate has no front to move to
        menu = pane_menu();
        check(menu != nullptr && !menu_has(menu, _L("Move to front")), "left_pane_first_plate_has_no_move_to_front");
        menu_shot("figma-26d1-plate-menu");
        if (menu != nullptr)
            menu->close();
        check(workspace->run_plate_action(plate_row.plate, Workspace::PlateAction::ToggleLock).succeeded() &&
                  workspace->outline().plates[1].locked,
              "left_pane_lock_reaches_orca");
        check(m_plater->get_partplate_list().is_locked(1), "left_pane_lock_is_orcas_plate_lock");
        check(workspace->run_plate_action(plate_row.plate, Workspace::PlateAction::ToggleLock).succeeded() &&
                  !workspace->outline().plates[1].locked,
              "left_pane_unlock_reaches_orca");

        pane->snapshot();
        const auto object_row = std::find_if(pane->rows().begin(), pane->rows().end(),
                                             [](const PaneRow& row) { return row.kind == PaneRow::Kind::Object; });
        check(object_row != pane->rows().end(), "left_pane_menu_fixture_object");
        if (object_row != pane->rows().end()) {
            const PaneRow object = *object_row;
            // The pane rebuilds its rows on every change, so only the index outlives them.
            const std::size_t object_index = static_cast<std::size_t>(object_row - pane->rows().begin());
            pane->open_row_menu(object);
            menu = pane_menu();
            check(menu_has(menu, _L("Rename")) && menu_has(menu, _L("Set number of instances…")) && menu_has(menu, _L("Printable")) &&
                      menu_has(menu, _L("Delete")),
                  "left_pane_object_menu_lists_the_object_actions");
            save_pane("left-pane-object-menu", pane->snapshot());
            menu_shot("object-menu");
            if (menu != nullptr)
                menu->close();
            const auto copies_print = [&] { return workspace->outline().objects[1].copies[0].printable; };
            check(workspace->run_object_action(object.object, Workspace::ObjectAction::TogglePrintable).succeeded() && !copies_print(),
                  "left_pane_printable_switch_reaches_orca");
            pane->snapshot();
            check(pane->rows()[2].all_wont_print(), "left_pane_shows_a_disabled_object_as_not_printing");
            check(workspace->run_object_action(object.object, Workspace::ObjectAction::TogglePrintable).succeeded() && copies_print(),
                  "left_pane_printable_switch_toggles_back");
            // A per-object setting makes the object customized. Its mark lists
            // the override count, while the unavailable editor remains disabled.
            m_plater->model().objects[1]->config.set_key_value("wall_loops", new ConfigOptionInt(5));
            pane->refresh_from_workspace();
            pane->snapshot();
            check(pane->rows()[object_index].customized && !pane->rows()[object_index].customization.has_tool(),
                  "left_pane_setting_override_marks_the_object_customized");
            check(!pane->rect_of(object_index, LeftPane::Part::Mark).IsEmpty(), "left_pane_settings_only_mark_opens_summary");
            click_pane(pane, pane->rect_of(object_index, LeftPane::Part::Mark));
            menu = pane_menu();
            check(menu_has(menu, _L("Per-object settings · 1")), "left_pane_settings_override_count_is_listed");
            if (menu != nullptr)
                menu->close();
            save_pane("left-pane-customized-object", pane->snapshot());
            window_shot("customized-object");
            m_plater->model().objects[1]->config.erase("wall_loops");
            pane->refresh_from_workspace();
            pane->snapshot();
            check(!pane->rows()[object_index].customized, "left_pane_mark_goes_when_the_customization_does");
            check(workspace->run_object_action(object.object, Workspace::ObjectAction::Filament, 1).error ==
                      Workspace::WorkspaceError::UnavailableOperation,
                  "left_pane_filament_action_is_absent_in_a_single_material_job");
        }

        // The tabs switch the pane's content and never the project.
        const auto active_before = workspace->snapshot().active_plate;
        const bool had_selection = !m_plater->canvas3D()->get_selection().is_empty();
        click_pane(pane, pane->tab_rect(PaneTab::Project));
        check(pane->navigation().tab() == PaneTab::Project, "left_pane_project_tab_opens");
        check(workspace->snapshot().active_plate == active_before &&
                  !m_plater->canvas3D()->get_selection().is_empty() == had_selection,
              "left_pane_tab_switch_keeps_plate_and_selection");
        save_pane("left-pane-project", pane->snapshot());
        window_shot("project");
        check(!pane->handle_escape(), "left_pane_escape_at_project_root_is_not_taken");
        check(pane->navigation().tab() == PaneTab::Project, "left_pane_escape_does_not_leave_project");
        // A project without 3MF metadata still offers the Details view.
        const auto has_label = [pane](const wxString& label) {
            const auto labels = pane->action_labels();
            return std::find(labels.begin(), labels.end(), label) != labels.end();
        };
        pane->snapshot();
        check(has_label(_L("Details")) && has_label(_L("Print history")) && !has_label(_L("Printed once")),
              "left_pane_root_offers_print_history_without_prints");
        check(pane->press_action(_L("Print history")) && pane->navigation().view() == ProjectView::PrintHistory,
              "left_pane_empty_print_history_opens");
        check(pane->handle_escape() && pane->navigation().view() == ProjectView::Root,
              "left_pane_empty_print_history_returns");
        pane->snapshot();
        check(has_label(_L("Version history")) && has_label(_L("Export project 3MF…")) && has_label(_L("Edit Project Info")),
              "left_pane_root_offers_version_history_export_and_edit");

        // Model metadata is the file's own words: the title shown is the model's,
        // never the saved project's name, and Details appears once there is
        // something to show.
        {
            Model& model = m_plater->model();
            model.model_info = std::make_shared<ModelInfo>();
            model.model_info->model_name = "Pane test model";
            model.design_info = std::make_shared<ModelDesignInfo>();
            model.design_info->Designer = "Ada";
            // A cover picture among the project's packed files.
            const std::filesystem::path pictures = std::filesystem::path(workspace->auxiliary_data_dir()) / "Model Pictures";
            std::filesystem::create_directories(pictures);
            {
                wxImage cover(60, 45);
                cover.SetRGB(wxRect(0, 0, 60, 45), 120, 80, 160);
                check(cover.SaveFile(wxString::FromUTF8((pictures / "cover.png").string()), wxBITMAP_TYPE_PNG),
                      "left_pane_cover_fixture_written");
            }
            pane->refresh_from_workspace();
            pane->snapshot();
            check(has_label(_L("Details")), "left_pane_details_appears_with_metadata");
            check(!has_label(_L("Printed once")), "left_pane_details_do_not_invent_a_print_count");
            check(workspace->project_details().title == "Pane test model" && workspace->project_details().designer == "Ada",
                  "left_pane_reads_the_models_own_title_and_designer");
            check(workspace->project_details().attachments.size() == 1 &&
                      workspace->project_details().attachments.front().folder == "Model Pictures",
                  "left_pane_cover_is_a_packed_model_picture");
            save_pane("left-pane-root-with-metadata", pane->snapshot());
            window_shot("root-with-metadata");
            check(pane->press_action(_L("Details")) && pane->navigation().view() == ProjectView::Details,
                  "left_pane_details_open");
            save_pane("left-pane-details", pane->snapshot());
            window_shot("details");
            check(pane->handle_escape(), "left_pane_details_escape_returns");
            model.model_info.reset();
            model.design_info.reset();
            pane->refresh_from_workspace();
            pane->snapshot();
            check(has_label(_L("Details")) && workspace->project_details().title.empty() &&
                      !workspace->project_details().attachments.empty(),
                  "left_pane_cover_only_metadata_has_details_without_invented_title");
            std::filesystem::remove_all(pictures);
            pane->refresh_from_workspace();
            pane->snapshot();
            check(has_label(_L("Details")), "left_pane_details_remains_without_metadata");
        }

        // A sparse legacy ledger entry exercises the count-only Figma state.
        // The live print path now records timestamps; this fixture deliberately
        // omits them to cover an older imported record.
        persistence().document().add_physical_print(Agent::PhysicalPrintRecord{}, "");
        persistence().notify_ledger_changed();
        pane->snapshot();
        check(has_label(_L("Printed once")), "left_pane_count_only_record_is_counted");
        check(pane->press_action(_L("Printed once")) && pane->navigation().view() == ProjectView::PrintHistory,
              "left_pane_count_only_history_opens");
        save_pane("left-pane-count-only-history", pane->snapshot());
        window_shot("count-only-history");
        check(pane->handle_escape(), "left_pane_count_only_history_returns");

        // A recorded physical print adds its row, and the pane reads the ledger
        // through its own subscription (the status row keeps its own).
        Agent::PhysicalPrintRecord record;
        record.id         = "print-1";
        record.outcome    = "completed";
        record.started_at = "2026-09-12T10:00:00Z";
        record.ended_at   = "2026-09-12T11:44:00Z";
        record.plate_name = "Plate 1";
        record.printer    = "A1 mini";
        record.statistics.material_grams = 38.0;
        record.statistics.material_cost = 0.95;
        persistence().document().add_physical_print(record, persistence().timestamp());
        persistence().notify_ledger_changed();
        pane->snapshot();
        check(has_label(_L("Printed 2 times")), "left_pane_root_shows_the_print_count");
        check(pane->press_action(_L("Printed 2 times")) && pane->navigation().view() == ProjectView::PrintHistory,
              "left_pane_print_history_opens");
        save_pane("left-pane-print-history", pane->snapshot());
        window_shot("print-history");
        check(pane->handle_escape() && pane->navigation().view() == ProjectView::Root, "left_pane_escape_goes_up_one_level");
        Agent::PhysicalPrintRecord stopped_record;
        stopped_record.id = "print-2";
        stopped_record.outcome = "cancelled";
        stopped_record.started_at = "2026-09-12T13:00:00Z";
        stopped_record.ended_at = "2026-09-12T13:12:00Z";
        stopped_record.plate_name = "Plate 2";
        stopped_record.printer = "A1 mini";
        stopped_record.statistics.material_grams = 9.0;
        stopped_record.statistics.material_cost = 0.23;
        persistence().document().add_physical_print(stopped_record, persistence().timestamp());
        persistence().notify_ledger_changed();
        pane->snapshot();
        check(pane->press_action(_L("Printed 3 times")), "figma_26e3_three_prints_open");
        save_pane("figma-26e3-mixed-history", pane->snapshot());
        window_shot("figma-26e3-mixed-history");
        check(pane->handle_escape(), "figma_26e3_history_returns");
        {
            Model& model = m_plater->model();
            model.model_info = std::make_shared<ModelInfo>();
            model.model_info->model_name = "Modular wall shelf brackets";
            model.model_info->license = "CC BY 4.0";
            model.model_info->description = "A set of modular brackets for a wall shelf.";
            model.design_info = std::make_shared<ModelDesignInfo>();
            model.design_info->Designer = "Anna K.";
            model.profile_info = std::make_shared<ModelProfileInfo>();
            model.profile_info->ProfileTile = "Strong PLA · by Anna K.";
            model.profile_info->ProfileDescription = "Strength focused PLA profile.";
            const std::filesystem::path auxiliary(workspace->auxiliary_data_dir());
            const std::filesystem::path pictures = auxiliary / "Model Pictures";
            std::filesystem::create_directories(pictures);
            for (int index = 0; index < 3; ++index) {
                wxImage picture(60, 45);
                picture.SetRGB(wxRect(0, 0, 60, 45), 105 + index * 35, 75 + index * 20, 160 - index * 30);
                check(picture.SaveFile(wxString::FromUTF8((pictures / ("view-" + std::to_string(index) + ".png")).string()),
                                       wxBITMAP_TYPE_PNG), "figma_metadata_picture_saved");
            }
            for (const char* folder : {"Bill of Materials", "Assembly Guide", "Others"})
                std::filesystem::create_directories(auxiliary / folder);
            std::ofstream((auxiliary / "Bill of Materials" / "parts.txt").string()) << "Bracket x2";
            std::ofstream((auxiliary / "Assembly Guide" / "instructions.txt").string()) << "Attach to wall";
            std::ofstream((auxiliary / "Others" / "notes.txt").string()) << "Fixture";
            std::ofstream((auxiliary / "Others" / "license.txt").string()) << "CC BY 4.0";
            pane->refresh_from_workspace();
            save_pane("figma-26e1-project-metadata", pane->snapshot());
            window_shot("figma-26e1-project-metadata");
            check(pane->press_action(_L("Details")), "figma_26e2_details_open");
            save_pane("figma-26e2-project-details", pane->snapshot());
            window_shot("figma-26e2-project-details");
            check(pane->handle_escape(), "figma_26e2_details_returns");
            model.model_info.reset();
            model.design_info.reset();
            model.profile_info.reset();
            for (const char* folder : {"Model Pictures", "Bill of Materials", "Assembly Guide", "Others"})
                std::filesystem::remove_all(auxiliary / folder);
            pane->refresh_from_workspace();
            save_pane("figma-26f-no-metadata", pane->snapshot());
            window_shot("figma-26f-no-metadata");
        }
        pane->snapshot();
        const auto captures_before_history = installed_shell()->autosave()->capture_attempts();
        check(pane->press_action(_L("Version history")) && pane->navigation().view() == ProjectView::Versions,
              "left_pane_version_history_opens");
        save_pane("left-pane-versions", pane->snapshot());
        check(installed_shell()->autosave()->capture_attempts() == captures_before_history,
              "left_pane_reading_version_history_does_not_save");
        window_shot("versions");
        pane->snapshot();
        check(pane->press_action(_L("Back to Project")) && pane->navigation().view() == ProjectView::Root, "left_pane_back_returns_to_root");
        pane->snapshot();
        check(pane->press_action(_L("Version history")), "left_pane_version_history_opens_again");
        click_pane(pane, pane->tab_rect(PaneTab::Plates));
        check(pane->navigation().tab() == PaneTab::Plates && pane->navigation().view() == ProjectView::Root,
              "left_pane_plates_tab_leaves_a_project_subview");
        click_pane(pane, pane->tab_rect(PaneTab::Plates));
        check(pane->navigation().tab() == PaneTab::Plates, "left_pane_plates_tab_returns");

        // Add plate is the toolbar's command, and undo takes it back.
        pane->snapshot();
        const std::size_t plates_before = plate_count();
        click_pane(pane, pane->add_plate_rect());
        check(plate_count() == plates_before + 1, "left_pane_add_plate_adds_a_plate");
        check(m_plater->get_partplate_list().get_plate_count() == int(plates_before) + 1, "left_pane_add_plate_reached_orca");
        check(workspace->undo().succeeded() && plate_count() == plates_before, "left_pane_add_plate_undoes");
        check(workspace->redo().succeeded() && plate_count() == plates_before + 1, "left_pane_add_plate_redoes");
        check(workspace->undo().succeeded() && plate_count() == plates_before, "left_pane_add_plate_undone_again");

        // Check print is not touched: the pane is not there, and it returns with Prepare.
        m_frame->select_tab(size_t(MainFrame::tpPreview));
        wxYield();
        auto* left_divider = wxWindow::FindWindowByName("Resize Plates and Project panel", m_frame);
        check(m_notebook->GetSelection() == MainFrame::tpPreview && !pane->IsShown(), "left_pane_is_absent_on_check_print");
        check(left_divider != nullptr && !left_divider->IsShown(), "left_resize_handle_is_absent_on_check_print");
        window_shot("check-print");
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        wxYield();
        check(pane->IsShown() && left_divider->IsShown(), "left_pane_and_resize_handle_return_with_prepare");
        window_shot("back-on-prepare");

        capture_figma_plate_states(pane);
        finish();
    }

    void capture_figma_plate_states(LeftPane* pane)
    {
        // These use Orca's real Model and PartPlateList. Separate captures let
        // the simple and folded mockups be compared without a synthetic pane.
        auto* workspace = installed_shell()->workspace();
        check(m_plater->new_project(true, true) != wxID_CANCEL, "figma_fixture_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const auto loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
            false);
        check(loaded.size() == 1, "figma_fixture_bracket_loaded");
        if (loaded.size() != 1)
            return;
        PartPlateList& plates = m_plater->get_partplate_list();
        Model& model = m_plater->model();
        model.objects[0]->name = "Bracket";
        pane->refresh_from_workspace();
        pane->snapshot();
        check(pane->rows().size() == 2 && pane->rows()[1].name == "Bracket", "figma_26c1_single_object");
        save_pane("figma-26c1-one-plate", pane->snapshot());
        window_shot("figma-26c1-one-plate");

        check(m_plater->duplicate_object(0) == 1, "figma_fixture_knob");
        model.objects[1]->name = "Knob";
        model.objects[0]->add_instance(*model.objects[0]->instances.front());
        check(plates.get_plate(0)->add_instance(0, 1, true) == 0, "figma_fixture_bracket_second_copy");
        check(plates.create_plate(true) == 1, "figma_fixture_plate_two");
        check(plates.add_to_plate(1, 0, 1) == 0, "figma_fixture_knob_on_plate_two");
        m_plater->canvas3D()->reload_scene(true, true);
        pane->refresh_from_workspace();
        pane->snapshot();
        check(pane->rows().size() == 3 && pane->rows()[1].copies == 2 && pane->rows()[2].summary.single_object == "Knob",
              "figma_26a_two_plate_summary");
        save_pane("figma-26a-two-plates", pane->snapshot());
        window_shot("figma-26a-two-plates");

        // Three more folded plates: copies, one disabled copy, and three objects.
        const auto add_duplicate = [&](const std::string& name, int plate_index, bool second_copy, bool disable_first) {
            const int index = m_plater->duplicate_object(1);
            check(index == int(model.objects.size()) - 1, "figma_fixture_object_duplicated");
            model.objects[index]->name = name;
            if (second_copy)
                model.objects[index]->add_instance(*model.objects[index]->instances.front());
            if (disable_first)
                model.objects[index]->instances.front()->printable = false;
            check(plates.add_to_plate(index, 0, plate_index) == 0, "figma_fixture_object_on_folded_plate");
            if (second_copy)
                check(plates.get_plate(plate_index)->add_instance(index, 1, true) == 0, "figma_fixture_copy_placed");
        };
        for (int plate_index = 2; plate_index < 5; ++plate_index)
            check(plates.create_plate(true) == plate_index, "figma_fixture_folded_plate_created");
        add_duplicate("Bracket", 2, true, false);
        add_duplicate("Bracket", 3, true, true);
        add_duplicate("Case", 4, false, false);
        add_duplicate("Spacer", 4, false, false);
        add_duplicate("Hinge pin", 4, false, false);
        plates.get_plate(4)->set_bed_type(BedType::btPEI);
        m_plater->canvas3D()->reload_scene(true, true);
        pane->refresh_from_workspace();
        pane->snapshot();
        check(pane->rows().size() == 6 && pane->rows()[4].summary.wont_print == 1 && pane->rows()[5].summary.objects == 3,
              "figma_26c2_folded_summaries");
        check(pane->rows()[5].custom_settings, "figma_26c2_plate_settings_mark");
        save_pane("figma-26c2-folded-summaries", pane->snapshot());
        window_shot("figma-26c2-folded-summaries");

        check(m_plater->new_project(true, true) != wxID_CANCEL, "figma_complex_new_project");
        const auto complex_loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
            false);
        check(complex_loaded.size() == 1, "figma_complex_case_loaded");
        if (complex_loaded.size() != 1)
            return;
        PartPlateList& complex_plates = m_plater->get_partplate_list();
        Model& complex_model = m_plater->model();
        for (int index = 1; index < 7; ++index)
            check(m_plater->duplicate_object(0) == index, "figma_complex_object_duplicated");
        const char* complex_names[] = {"Case", "Bracket", "Hinge pin", "Spacer", "Bracket", "Knob", "Pin"};
        for (int index = 0; index < 7; ++index)
            complex_model.objects[index]->name = complex_names[index];
        // The fixture printer can accept three material slots. These are
        // project setup facts read by the same outline path as a user setup.
        auto* presets = wxGetApp().preset_bundle;
        check(presets != nullptr && !presets->filament_presets.empty(), "figma_complex_material_setup_available");
        if (presets != nullptr && !presets->filament_presets.empty()) {
            presets->filament_presets.resize(3, presets->filament_presets.front());
            presets->project_config.set_key_value("filament_colour", new ConfigOptionStrings({"#00A58A", "#CE9B48", "#4B81C7"}));
        }
        complex_model.objects[0]->config.set_key_value("extruder", new ConfigOptionInt(1));
        complex_model.objects[1]->config.set_key_value("extruder", new ConfigOptionInt(3));
        complex_model.objects[2]->config.set_key_value("extruder", new ConfigOptionInt(3));
        check(complex_plates.create_plate(true) == 1, "figma_complex_plate_two");
        for (int index = 4; index < 7; ++index)
            check(complex_plates.add_to_plate(index, 0, 1) == 0, "figma_complex_plate_two_object");
        check(complex_plates.get_plate(0)->remove_instance(3, 0) == 0, "figma_complex_spacer_off_plate");
        for (int copy = 1; copy < 4; ++copy) {
            complex_model.objects[1]->add_instance(*complex_model.objects[1]->instances.front());
            check(complex_plates.get_plate(0)->add_instance(1, copy, true) == 0, "figma_complex_bracket_copy");
        }
        complex_model.objects[1]->instances[3]->printable = false;
        indexed_triangle_set open_mesh = complex_model.objects[2]->volumes.front()->mesh().its;
        open_mesh.indices.resize(1);
        complex_model.objects[2]->volumes.front()->set_mesh(std::move(open_mesh));
        complex_model.objects[2]->invalidate_bounding_box();
        ModelObject* case_object = complex_model.objects[0];
        ModelVolume* original_part = case_object->volumes.front();
        original_part->name = "Case part";
        ModelVolume* logo_part = case_object->add_volume(*original_part, ModelVolumeType::MODEL_PART);
        logo_part->name = "Logo part";
        case_object->add_volume(*original_part, ModelVolumeType::PARAMETER_MODIFIER)->name = "Dense infill";
        case_object->add_volume(*original_part, ModelVolumeType::NEGATIVE_VOLUME)->name = "Cable hole";
        TriangleSelector painted_facets(original_part->mesh());
        painted_facets.set_facet(0, EnforcerBlockerType::ENFORCER);
        check(original_part->supported_facets.set(painted_facets), "figma_complex_support_painting_saved");
        case_object->config.set_key_value("wall_loops", new ConfigOptionInt(5));
        case_object->layer_height_profile.set(std::vector<coordf_t>{0.0, 0.20, 20.0, 0.28});
        m_plater->canvas3D()->reload_scene(true, true);
        logo_part->config.set_key_value("extruder", new ConfigOptionInt(2));
        pane->refresh_from_workspace();
        const Workspace::ProjectOutline complex_outline = workspace->outline();
        check(std::any_of(complex_outline.objects[0].volumes.begin(), complex_outline.objects[0].volumes.end(),
                          [](const Workspace::OutlineVolume& volume) { return volume.name == "Logo part" && volume.extruder == 2; }),
              "figma_complex_logo_filament_two");
        pane->navigation().set_expanded(workspace->outline().objects[0].id, true);
        pane->refresh_from_workspace();
        pane->snapshot();
        check(std::any_of(pane->rows().begin(), pane->rows().end(), [](const PaneRow& row) {
                  return row.kind == PaneRow::Kind::OffPlateHeader;
              }), "figma_26b_off_plate_group");
        check(std::count_if(pane->rows().begin(), pane->rows().end(), [](const PaneRow& row) {
                  return row.kind == PaneRow::Kind::Volume && row.name == "Cable hole";
              }) == 1, "figma_26b_negative_part_visible");
        check(std::any_of(pane->rows().begin(), pane->rows().end(), [](const PaneRow& row) {
                  return row.kind == PaneRow::Kind::Object && row.name == "Hinge pin" && row.mesh.open_edges == 3;
              }), "figma_26b_three_mesh_errors");
        check(pane->rows()[1].customization.support_painting && pane->rows()[1].customization.variable_layer_height &&
                  pane->rows()[1].filament == 1, "figma_26h_painting_layer_height_and_filament_facts");
        save_pane("figma-26b-complex-plate", pane->snapshot());
        window_shot("figma-26b-complex-plate");
        const auto marked = std::find_if(pane->rows().begin(), pane->rows().end(), [](const PaneRow& row) {
            return row.kind == PaneRow::Kind::Object && row.name == "Case";
        });
        check(marked != pane->rows().end() && marked->customized, "figma_26h_customization_mark");
        if (marked != pane->rows().end()) {
            const std::size_t index = std::size_t(marked - pane->rows().begin());
            save_pane("figma-26h-customization-mark", pane->snapshot());
            window_shot("figma-26h-customization-mark");
            pane->open_row_menu(*marked);
            menu_shot("figma-26d2-object-split-menu");
            for (auto* child : pane->GetChildren())
                if (auto* menu = dynamic_cast<HeaderMenu*>(child); menu && menu->IsShown())
                    menu->close();
            click_pane(pane, pane->rect_of(index, LeftPane::Part::Mark));
            menu_shot("figma-26h-customization-mark-menu");
            for (auto* child : pane->GetChildren())
                if (auto* menu = dynamic_cast<HeaderMenu*>(child); menu && menu->IsShown())
                    menu->close();
        }
        const auto damaged = std::find_if(pane->rows().begin(), pane->rows().end(), [](const PaneRow& row) {
            return row.kind == PaneRow::Kind::Object && row.name == "Hinge pin";
        });
        if (damaged != pane->rows().end()) {
            pane->open_row_menu(*damaged);
            menu_shot("figma-26d2-object-repair-menu");
            for (auto* child : pane->GetChildren())
                if (auto* menu = dynamic_cast<HeaderMenu*>(child); menu && menu->IsShown())
                    menu->close();
        }

        check(m_plater->new_project(true, true) != wxID_CANCEL, "figma_count_only_new_project");
        for (int index = 0; index < 3; ++index)
            persistence().document().add_physical_print(Agent::PhysicalPrintRecord{}, "");
        persistence().notify_ledger_changed();
        pane->refresh_from_workspace();
        click_pane(pane, pane->tab_rect(PaneTab::Project));
        pane->snapshot();
        check(pane->press_action(_L("Printed 3 times")), "figma_26e4_count_only_opens");
        save_pane("figma-26e4-count-only-history", pane->snapshot());
        window_shot("figma-26e4-count-only-history");
    }

    // The whole window as the person sees it, from the screen. A popup menu is
    // captured without yielding, which would close it.
    void window_shot(const std::string& name)
    {
        if (!m_state->capture_dir.empty())
            write_screen_capture(m_frame->GetScreenRect(), "window-" + name);
    }
    void menu_shot(const std::string& name)
    {
        if (m_state->capture_dir.empty())
            return;
        // Give the popup time to be drawn; nothing here clicks, so it stays open.
        for (int settle = 0; settle < 10; ++settle) {
            wxYield();
            wxMilliSleep(50);
        }
        blit_screen(m_frame->GetScreenRect(), "window-" + name);
    }

    void save_pane(const std::string& name, const wxBitmap& bitmap)
    {
        check(bitmap.IsOk(), "captured_" + name);
        if (m_state->capture_dir.empty() || !bitmap.IsOk())
            return;
        fs::create_directories(m_state->capture_dir);
        const std::string file = (m_state->capture_dir / (name + ".png")).string();
        bitmap.ConvertToImage().SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
        std::cout << "HARNESS ARTIFACT " << name << " " << file << std::endl;
    }

    void capture_chat_pane(const std::string& name)
    {
        if (m_state->capture_dir.empty())
            return;
        // Let the bridge update and the web view paint before taking its
        // native snapshot. This keeps captures tied to visible UI states.
        for (int settle = 0; settle < 8; ++settle) {
            wxYield();
            wxMilliSleep(50);
        }
        capture_web_view(installed_shell()->agent_pane()->web_view().webview(), name);
    }

    void verify_chat_restoration()
    {
        const fs::path source = fs::path(data_dir()) / "chat-restoration-cube.stl";
        fs::copy_file(fs::path(JUSPRIN_SOURCE_DIR) / "tests/data/test_stl/ASCII/20mmbox-LF.stl",
                      source, fs::copy_option::overwrite_if_exists);
        const bool loaded = m_plater->load_files(std::vector<std::string>{source.string()},
                   LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
                   false).size() == 1;
        check(loaded, "chat_restore_model_loaded");
        if (!loaded) {
            finish();
            return;
        }
        auto& document = persistence().document();
        auto& host = installed_shell()->agent_pane()->web_view().host();
        const std::string first = document.active_conversation_id();
        check(document.rename_conversation(first, "Quick print"), "chat_restore_quick_chat_named");
        DynamicPrintConfig quick;
        quick.set_deserialize_strict("sparse_infill_density", "5%");
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(quick);
        document.set_setup_intent(first, "Quick print");
        Agent::PlanRecord quick_plan;
        quick_plan.headline = "Quick plan";
        document.set_plan(quick_plan, persistence().timestamp());
        const bool quick_saved = host.checkpoint_active_chat();
        check(quick_saved, "chat_restore_quick_checkpoint");
        if (!quick_saved) {
            finish();
            return;
        }
        const std::string quick_version = document.chat_checkpoint(first)->at("versionId").get<std::string>();
        auto& view = installed_shell()->agent_pane()->web_view();
        WebView::RunScript(view.webview(), "window.__jusprinTest.createConversation()");
        wait_until([this, first] { return persistence().document().active_conversation_id() != first; },
                   "chat_restore_second_chat_created", [self = shared_from_this(), first, quick_version] {
            auto& document = self->persistence().document();
            const std::string second = document.active_conversation_id();
            self->check(document.rename_conversation(second, "Strong print"), "chat_restore_strong_chat_named");
            DynamicPrintConfig strong;
            strong.set_deserialize_strict("sparse_infill_density", "80%");
            wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(strong);
            document.set_setup_intent(second, "Strong print");
            Agent::PlanRecord strong_plan;
            strong_plan.headline = "Strong plan";
            document.set_plan(strong_plan, self->persistence().timestamp());
            const bool strong_saved = installed_shell()->agent_pane()->web_view().host().checkpoint_active_chat();
            self->check(strong_saved, "chat_restore_strong_checkpoint");
            if (!strong_saved) {
                self->finish();
                return;
            }
            self->check(document.chat_checkpoint(second)->at("versionId") != quick_version,
                        "chat_restore_settings_have_distinct_versions");
            auto& view = installed_shell()->agent_pane()->web_view();
            WebView::RunScript(view.webview(), wxString::FromUTF8(
                "window.__jusprinTest.switchConversation('" + first + "')"));
            self->wait_until([self, first] { return self->persistence().document().viewed_conversation_id() == first; },
                             "chat_restore_history_viewed", [self, first, second, quick_version] {
                auto& document = self->persistence().document();
                self->check(document.active_conversation_id() == second,
                            "chat_restore_browsing_preserves_active_identity");
                self->check(wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_serialize(
                                "sparse_infill_density") == "80%", "chat_restore_browsing_preserves_live_settings");
                const auto checkpoint = document.chat_checkpoint(first);
                self->check(checkpoint && (*checkpoint)["summary"]["setupIntent"] == "Quick print",
                            "chat_restore_saved_card_keeps_quick_intent");
                self->capture_chat_pane("chat-history-quick-with-strong-project");
                const std::size_t old_messages = document.messages(first).size();
                auto& view = installed_shell()->agent_pane()->web_view();
                const auto received_before = view.host().messages_received();
                WebView::RunScript(view.webview(), "window.__jusprinTest.send('forged inactive turn')");
                self->wait_until([&view, old_messages, first, received_before] {
                    return view.host().persistence().document().messages(first).size() == old_messages &&
                           view.host().messages_received() > received_before;
                }, "chat_restore_inactive_send_refused", [self, first, second, old_messages, quick_version] {
                    auto& document = self->persistence().document();
                    self->check(document.messages(first).size() == old_messages &&
                                document.active_conversation_id() == second,
                                "chat_restore_forged_send_cannot_activate_history");
                    auto& view = installed_shell()->agent_pane()->web_view();
                    WebView::RunScript(view.webview(),
                        "document.querySelector('.project-updates-actions button.primary')?.click()");
                    self->capture_chat_pane("chat-restore-confirmation");
                    WebView::RunScript(view.webview(),
                        "(function(){ const timer=setInterval(function(){"
                        "const button=document.querySelector('.chat-dialog button.primary');"
                        "if(button){clearInterval(timer);button.click();}},50);"
                        "setTimeout(function(){clearInterval(timer);},5000); })()");
                    self->wait_until([self, first] {
                            return self->persistence().document().active_conversation_id() == first;
                        }, "chat_restore_confirmed_transition", [self, first, second, quick_version] {
                            auto& document = self->persistence().document();
                            self->check(document.viewed_conversation_id() == first &&
                                        document.plan().headline == "Quick plan",
                                        "chat_restore_matching_planning_and_authority");
                            self->check(wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_serialize(
                                            "sparse_infill_density") == "5%",
                                        "chat_restore_quick_settings_restored");
                            // The restore replays the saved settings under the
                            // person who asked for it, and logs itself last. The
                            // setup card reads that order to tell the restore's
                            // work from a hand edit, so nothing may follow it.
                            self->check(!document.changes().empty() && document.changes().back().kind == "restore",
                                        "chat_restore_is_the_newest_logged_change");
                            self->check(document.chat_checkpoint(second).has_value() &&
                                        document.chat_checkpoint(second)->at("versionId") != quick_version,
                                        "chat_restore_outgoing_version_remains_recoverable");
                            self->check(installed_shell()->autosave()->save_now(),
                                        "chat_restore_transition_durable");
                            self->capture_chat_pane("chat-restored-quick-setup");
                            self->finish();
                        });
                });
            });
        });
    }

    void verify_autosave_seed()
    {
        check(!installed_shell()->autosave()->pin_current().empty(),
              "restart_blank_agent_before_state_pinned");
        const fs::path source = fs::path(data_dir()) / "restart-source.stl";
        fs::copy_file(fs::path(JUSPRIN_SOURCE_DIR) / "tests/data/test_stl/ASCII/20mmbox-LF.stl",
                      source, fs::copy_option::overwrite_if_exists);
        check(m_plater->load_files(std::vector<std::string>{source.string()},
              LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
              "restart_seed_model_loaded");
        auto& document = persistence().document();
        document.create_conversation("Restart proof", persistence().timestamp());
        persistence().set_draft("unfinished restart draft");
        persistence().commit();
        check(installed_shell()->autosave()->save_now(), "restart_seed_committed");
        Slic3r::set_backup_interval(1);
        Slic3r::backup_soon();
        const auto home_projects = installed_shell()->home_view()->backend().recent_projects();
        check(std::any_of(home_projects.begin(), home_projects.end(), [id = document.project_id()](const auto& project) {
            return project.id == id;
        }), "home_lists_saved_local_project");
        const auto home_entry = std::find_if(home_projects.begin(), home_projects.end(),
            [id = document.project_id()](const auto& project) { return project.id == id; });
        check(home_entry != home_projects.end() && home_entry->name == "restart-source" &&
                  home_entry->status_text == "Saved" &&
                  home_entry->path.find("/jusprin/projects/") != std::string::npos,
              "home_describes_managed_project");
        check(home_entry != home_projects.end() &&
                  preview_has_subject(home_entry->thumbnail_url),
              "home_saved_project_has_visible_preview");
        const auto preview_file = std::filesystem::path(data_dir()) / "jusprin" / "projects" /
                                  document.project_id() / "preview.json";
        const auto preview_time = std::filesystem::last_write_time(preview_file);
        installed_shell()->home_view()->backend().recent_projects();
        check(std::filesystem::last_write_time(preview_file) == preview_time,
              "home_preview_cache_avoids_repeat_writes");
        Agent::ConversationMessage reply;
        reply.id = document.allocate_message_id();
        reply.role = Agent::MessageRole::Assistant;
        reply.state = Agent::MessageState::Streaming;
        const std::string conversation = document.active_conversation_id();
        document.append_message(conversation, reply, persistence().timestamp());
        persistence().commit();
        const auto exports_before_reply = installed_shell()->autosave()->capture_attempts();
        for (int word = 0; word < 4; ++word) {
            reply.text += " word";
            check(document.update_message(conversation, reply), "stream_reply_message_updated");
            persistence().commit();
            installed_shell()->autosave()->tick();
            wxMilliSleep(900);
        }
        reply.state = Agent::MessageState::Complete;
        check(document.update_message(conversation, reply), "stream_reply_completed");
        persistence().commit();
        check(installed_shell()->autosave()->save_now(), "stream_reply_durable");
        check(installed_shell()->autosave()->capture_attempts() == exports_before_reply,
              "stream_reply_does_not_export_model_per_word");
        const auto before_plan = installed_shell()->autosave()->current_version();
        const auto exports_before_plan = installed_shell()->autosave()->capture_attempts();
        Agent::PlanRecord plan;
        plan.headline = "Check the first layer";
        document.set_plan(plan, persistence().timestamp());
        persistence().commit();
        check(installed_shell()->autosave()->save_now(), "document_plan_durable");
        check(installed_shell()->autosave()->current_version() == before_plan &&
                  installed_shell()->autosave()->capture_attempts() == exports_before_plan,
              "document_plan_does_not_create_model_checkpoint");
        const auto before_rename = installed_shell()->autosave()->current_version();
        m_plater->get_partplate_list().get_plate(0)->set_plate_name("Quiet plate rename");
        wait_until([before_rename] {
            return installed_shell()->autosave()->current_version() != before_rename;
        }, "restart_plate_rename_saved_without_workspace_event", [self = shared_from_this(), source] {
            const auto& document = self->persistence().document();
            std::ofstream expected((fs::path(data_dir()) / "restart-expected.json").string());
            expected << nlohmann::json{{"projectId", document.project_id()},
                                       {"versionId", installed_shell()->autosave()->current_version()},
                                       {"backupPath", self->m_plater->model().get_backup_path()}}.dump();
            expected.close();
            fs::remove(source);
            const auto attempts = installed_shell()->autosave()->capture_attempts();
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            self->wait_until([until] { return std::chrono::steady_clock::now() >= until; },
                             "restart_idle_observation_window", [self, attempts] {
                self->check(installed_shell()->autosave()->capture_attempts() == attempts,
                            "restart_idle_does_not_export_metadata_again");
                const fs::path backup = self->m_plater->model().get_backup_path();
                self->check(!fs::exists(backup / ".3mf"), "managed_project_skips_orca_backup_archive");
                bool object_cache = false;
                const fs::path objects = backup / "3D" / "Objects";
                if (fs::is_directory(objects))
                    for (fs::directory_iterator it(objects), end; it != end; ++it)
                        object_cache |= it->path().extension() == ".model";
                self->check(!object_cache, "managed_project_skips_orca_backup_mesh_cache");
                const std::string saved_id = self->persistence().document().project_id();
                self->check(self->m_plater->new_project(true, true) != wxID_CANCEL,
                            "home_local_open_starts_from_another_project");
                self->wait_until([self, saved_id] {
                    return self->persistence().document().project_id() != saved_id;
                }, "home_new_project_identity_settled", [self, saved_id] {
                    installed_shell()->home_view()->backend().open_project(saved_id);
                    self->check(self->persistence().document().project_id() == saved_id &&
                                    self->m_plater->model().objects.size() == 1 &&
                                    self->m_plater->get_partplate_list().get_plate(0)->get_plate_name() == "Quiet plate rename",
                                "home_opens_saved_local_project");
                    self->finish();
                });
            });
        });
    }

    void verify_autosave_reopen()
    {
        std::ifstream expected((fs::path(data_dir()) / "restart-expected.json").string());
        nlohmann::json saved;
        expected >> saved;
        const std::string project_id = saved.at("projectId").get<std::string>();
        const std::string version_id = saved.at("versionId").get<std::string>();
        wait_until([this, project_id] {
            return persistence().document().project_id() == project_id && m_plater->model().objects.size() == 1;
        }, "restart_project_resumed", [self = shared_from_this(), project_id, version_id] {
            self->check(installed_shell()->autosave()->current_version() == version_id,
                        "restart_head_preserved");
            self->check(installed_shell()->status_row()->project_summary().BeforeFirst('\n') ==
                            wxString::FromUTF8("restart-source \xE2\x80\x94 ") + wxGetTranslation("Saved"),
                        "restart_title_uses_local_project_name_and_save_state");
            const auto home_projects = installed_shell()->home_view()->backend().recent_projects();
            self->check(std::any_of(home_projects.begin(), home_projects.end(),
                [&project_id](const auto& project) {
                    return project.id == project_id && project.name == "restart-source" &&
                           preview_has_subject(project.thumbnail_url);
                }), "restart_home_lists_local_project_with_preview");
            self->check(self->persistence().draft() == "unfinished restart draft" &&
                        self->persistence().document().conversations().size() == 2,
                        "restart_semantic_state_preserved");
            const auto replies = self->persistence().document().messages(
                self->persistence().document().active_conversation_id());
            self->check(std::any_of(replies.begin(), replies.end(), [](const auto& message) {
                return message.role == Agent::MessageRole::Assistant &&
                       message.state == Agent::MessageState::Complete &&
                       message.text == " word word word word";
            }), "restart_streamed_reply_preserved");
            self->check(self->persistence().document().plan().headline == "Check the first layer",
                        "restart_document_plan_preserved");
            self->check(self->m_plater->get_partplate_list().get_plate(0)->get_plate_name() == "Quiet plate rename",
                        "restart_quiet_plate_rename_restored");
            self->finish();
        });
    }

    void verify_preset_close()
    {
        int saves = 0;
        int warnings = 0;
        m_plater->set_before_project_release([&saves] {
            ++saves;
            return true;
        });
        const int closed = m_plater->close_with_confirm([&warnings](bool) {
            ++warnings;
            return true;
        });
        check(closed == wxID_NO && saves == 1, "preset_close_saves_project");
        check(warnings == 0, "preset_close_skips_reusable_preset_warning");

        saves = 0;
        warnings = 0;
        m_plater->set_before_project_release([&saves] {
            ++saves;
            return false;
        });
        const int refused = m_plater->close_with_confirm([&warnings](bool) {
            ++warnings;
            return true;
        });
        check(refused == wxID_CANCEL && saves == 1, "preset_close_stops_when_project_save_fails");
        check(warnings == 0, "failed_preset_close_skips_reusable_preset_warning");
        m_plater->set_before_project_release({});

        warnings = 0;
        const int stock_close = m_plater->close_with_confirm([&warnings](bool) {
            ++warnings;
            return true;
        });
        check(stock_close == wxID_NO && warnings == 1, "preset_close_keeps_stock_warning_check");
        finish();
    }

    void capture_file_corpus()
    {
        using nlohmann::json;
        std::ifstream input(m_state->file_corpus_manifest.string());
        json manifest;
        input >> manifest;
        auto* workspace = installed_shell()->workspace();
        json captures = json::array();
        const auto setup_json = [](const Workspace::WorkspaceSnapshot& snapshot) {
            json plates = json::array();
            for (const auto& plate : snapshot.plates) {
                json objects = json::array();
                for (const auto& object : plate.objects)
                    objects.push_back({{"id", std::to_string(object.id.value())},
                                       {"name", object.name}, {"instances", object.instances.size()}});
                plates.push_back({{"id", std::to_string(plate.id.value())}, {"name", plate.name},
                                  {"active", plate.active}, {"sliced", plate.sliced},
                                  {"objects", std::move(objects)}});
            }
            json selected = json::array();
            for (const auto id : snapshot.selected_objects)
                selected.push_back(std::to_string(id.value()));
            return json{{"sessionId", std::to_string(snapshot.session.value())},
                        {"revision", snapshot.revision},
                        {"projectName", snapshot.setup.project_name},
                        {"printerPreset", snapshot.setup.printer_preset},
                        {"filamentPreset", snapshot.setup.filament_preset},
                        {"processPreset", snapshot.setup.process_preset},
                        {"presetsDirty", snapshot.setup.presets_dirty},
                        {"selectedObjectIds", std::move(selected)},
                        {"plates", std::move(plates)}};
        };
        const auto installed_bundles = [] {
            json names = json::array();
            const fs::path directory = fs::path(data_dir()) / PRESET_SYSTEM_DIR;
            for (const auto& entry : fs::directory_iterator(directory))
                if (entry.path().extension() == ".json") names.push_back(entry.path().stem().string());
            return names;
        };
        std::optional<Workspace::LoadReport> heard;
        workspace->set_load_report_listener([&heard](const Workspace::LoadReport& report) { heard = report; });
        for (const json& item : manifest) {
            const std::string path = item.at("path").get<std::string>();
            const std::string method = item.at("method").get<std::string>();
            check(m_plater->new_project(true, true) != wxID_CANCEL, "file_corpus_starts_clean");
            heard.reset();
            const json before = setup_json(workspace->snapshot());
            const json bundles_before = installed_bundles();
            if (method == "project")
                m_plater->load_project(from_u8(path), "-");
            else if (method == "model")
                m_plater->add_model(false, path);
            else
                throw std::runtime_error("Unknown file corpus method: " + method);
            wxTheApp->ProcessPendingEvents();
            const json after = setup_json(workspace->snapshot());
            check(heard.has_value(), "file_corpus_report_arrived");
            captures.push_back({{"id", item.at("id")}, {"path", path}, {"method", method},
                                {"installedBundlesBefore", bundles_before},
                                {"installedBundlesAfter", installed_bundles()},
                                {"workspaceBefore", before}, {"workspaceAfter", after},
                                {"report", heard ? Agent::load_report_result(*heard) : json()}});
            std::ofstream output(m_state->file_corpus_output.string());
            output << captures.dump(2) << '\n';
            check(output.good(), "file_corpus_written");
        }
    }

    void verify_stock_slice()
    {
        verify_stock_mode();
        check(m_plater->auto_preview_after_slice(), "stock_keeps_auto_preview_policy");
        load_multi_plate_fixture();
        m_plater->update(true, true);
        wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
        m_frame->select_tab(MainFrame::tpPreview);
        wait_until([this] { return m_plater->is_preview_shown() && !m_plater->is_background_process_slicing() &&
            m_plater->get_partplate_list().get_curr_plate()->is_slice_result_valid(); }, "stock_slice_opens_preview",
            [self = shared_from_this()] {
                self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "stock_slice_teardown");
                self->finish();
            });
    }

    void verify_mcp_setup()
    {
        const std::string executable = wxStandardPaths::Get().GetExecutablePath().ToUTF8().data();
        const std::string literal = "Kenny's 打印 path; $NOT_EXPANDED";
        const auto success = run_mcp_setup_command(m_frame, {executable, "--setup-child", "success", literal});
        check(success.success && success.diagnostic.find(literal) != std::string::npos, "mcp_setup_literal_arguments");
        const auto failure = run_mcp_setup_command(m_frame, {executable, "--setup-child", "failure", literal});
        check(!failure.success && failure.diagnostic.find("code 7") != std::string::npos &&
              failure.diagnostic.find("fixture stderr") != std::string::npos, "mcp_setup_cli_failure_visible");
        const auto large = run_mcp_setup_command(m_frame, {executable, "--setup-child", "large", literal});
        check(large.success && large.diagnostic.size() <= 65600, "mcp_setup_output_bounded_without_deadlock");
        wxEvtHandler events;
        wxTimer cancel(&events);
        bool attempted_cancel = false;
        events.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            for (auto* window : wxTopLevelWindows) {
                auto* dialog = dynamic_cast<wxDialog*>(window);
                if (dialog && dialog->GetTitle() == "Connecting AI tool") {
                    attempted_cancel = true;
                    wxCommandEvent event(wxEVT_BUTTON, wxID_CANCEL);
                    dialog->GetEventHandler()->ProcessEvent(event);
                }
            }
        });
        cancel.StartOnce(100);
        const auto started = std::chrono::steady_clock::now();
        const auto delayed = run_mcp_setup_command(m_frame, {executable, "--setup-child", "delayed", literal});
        check(attempted_cancel, "mcp_setup_cancel_event_exercised");
        check(delayed.success && std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(800),
              "mcp_setup_monitor_outlives_child_after_cancel_event");

        std::vector<std::string> timeout_modes{"timeout"};
#ifndef _WIN32
        // Windows terminates a process directly; it has no POSIX TERM/KILL escalation.
        timeout_modes.push_back("ignore-term");
#endif
        for (const auto& mode : timeout_modes) {
            wxEvtHandler heartbeat_events;
            wxTimer heartbeat(&heartbeat_events);
            int ticks = 0;
            heartbeat_events.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { ++ticks; });
            heartbeat.Start(50);
            const auto timeout_started = std::chrono::steady_clock::now();
            const auto result = run_mcp_setup_command(m_frame, {executable, "--setup-child", mode, literal});
            heartbeat.Stop();
            const auto elapsed = std::chrono::steady_clock::now() - timeout_started;
            check(!result.success && result.diagnostic.find("Setup timed out") != std::string::npos &&
                  result.diagnostic.find("config may have changed") != std::string::npos,
                  "mcp_setup_" + mode + "_reports_uncertain_config");
            check(elapsed >= std::chrono::seconds(mode == "timeout" ? 30 : 32) &&
                  elapsed < std::chrono::seconds(45), "mcp_setup_" + mode + "_bounded_termination");
            check(ticks > 100, "mcp_setup_" + mode + "_event_loop_responsive");
            const auto marker = result.diagnostic.find("fixture pid ");
            check(marker != std::string::npos, "mcp_setup_" + mode + "_child_started");
            if (marker != std::string::npos) {
                const auto child_pid = std::stol(result.diagnostic.substr(marker + 12));
                check(!wxProcess::Exists(child_pid), "mcp_setup_" + mode + "_child_reaped");
            }
        }
        const auto recovered = run_mcp_setup_command(m_frame, {executable, "--setup-child", "success", literal});
        check(recovered.success, "mcp_setup_succeeds_after_timeouts");
    }

    // --- The printer conversation (--printer-setup) -----------------------
    //
    // The panel takes Home's place, so the checks below drive the shell the
    // way Home's own bridge does and read the session the panel would draw.
    // The conversation itself needs a live agent (--printer-live); what is
    // checked here is the panel around it: that it opens on both paths,
    // states the right printer, and gives Home back when it closes.

    static constexpr const char* kSetupFixturePrinter = "Bambu Lab X1 Carbon 0.4 nozzle";
    // Adding the Neo saves its system profile as a printer named after the
    // model, and that printer is what stays selected afterwards.
    static constexpr const char* kAddedPrinter        = "Creality Ender-3 V2 Neo";
    static constexpr const char* kAddedPrinterProfile = "Creality Ender-3 V2 Neo 0.4 nozzle";

    static std::string selected_printer() { return wxGetApp().preset_bundle->printers.get_selected_preset_name(); }

    // One half of the header's printer chip, by the name that half carries.
    static wxString chip_label(wxWindow* row, const char* half)
    {
        wxWindow* control = wxWindow::FindWindowByName(half, row);
        return control ? control->GetLabel() : wxString();
    }

    void verify_printer_setup()
    {
        check(selected_printer() == kSetupFixturePrinter, "setup_fixture_printer_selected");

        ShellController*            shell = installed_shell();
        Home::HomeWebView*          home  = shell == nullptr ? nullptr : shell->home_view();
        PrinterSetup::PrinterPanel* panel = shell == nullptr ? nullptr : shell->printer_panel();
        if (home == nullptr || panel == nullptr) {
            fail("the shell has no Home view or printer panel");
            return;
        }
        check(!panel->IsShown(), "panel_closed_before_it_is_asked_for");

        // "+ Add printer" on Home, as the page sends it.
        home->backend().add_printer();
        check(panel->IsShown(), "panel_opens_from_add_printer");
        check(!home->IsShownOnScreen(), "panel_covers_home");
        const nlohmann::json add = panel->session_json();
        check(add.value("mode", "") == "add" && add.value("printerName", "x").empty(), "panel_adds_with_no_printer_yet");
        check(add["context"].contains("printers") && !add["context"].contains("printer"), "panel_sends_the_printer_list");
        // The page writes every word; the app sends none.
        check(!add.contains("caption") && !add.contains("placeholder") && !add.contains("chips"),
              "panel_sends_facts_not_words");
        const bool tip = std::any_of(add["blocks"].begin(), add["blocks"].end(),
                                     [](const nlohmann::json& block) { return block.value("kind", "") == "tip"; });
        check(tip, "panel_offers_the_photo_path_in_words");

        // A printer to change: the fixture's own profile saved under a name.
        // Back restores Home before its next printer action can be chosen.
        panel->close();
        wxYield();
        check(!panel->IsShown() && home->IsShownOnScreen(), "panel_add_returns_to_home_before_change");
        const std::string named = Printers::add_named_printer(*m_plater, "Lab Printer", {});
        home->backend().open_printer_settings("named:" + named);
        check(panel->IsShown(), "panel_opens_from_printer_settings");
        const nlohmann::json change = panel->session_json();
        check(change.value("mode", "") == "change", "panel_changes_the_printer_it_was_opened_for");
        check(change.value("printerName", "") == named && change["context"]["printer"].value("name", "") == named,
              "panel_states_the_printer_by_name");
        check(change["context"]["printer"].value("nozzle", 0.) == 0.4, "panel_states_the_nozzle_it_is_set_up_with");
        verify_nozzle_change_leaves_the_project(named);

        // Back gives Home back.
        panel->close();
        wxYield();
        check(!panel->IsShown(), "panel_closes_back_to_the_printer_list");
        check(Printers::remove_named_printer(*m_plater, named).empty(), "panel_cleanup_removes_the_printer");
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        verify_setup_install_commands();
    }

    void verify_other_printers_menu()
    {
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        const std::string selected_name = Printers::add_named_printer(*m_plater, "Lab Printer", {});
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        const std::string other_name = Printers::add_named_printer(*m_plater, "Other Lab Printer", {});
        check(SetupCommands::select_named_printer(*m_plater, selected_name),
              "printer_menu_fixture_selects_first_saved_printer");
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        wxYield();

        auto* header = installed_shell()->status_row();
        header->refresh();
        header->open_printer_menu();
        HeaderMenu* menu = visible_header_menu();
        check(menu != nullptr, "printer_menu_opens_with_two_saved_printers");
        if (menu != nullptr) {
            const auto names = menu_row_names(menu);
            check(std::find(names.begin(), names.end(), other_name) != names.end(),
                  "printer_menu_lists_the_other_saved_printer");
            auto* other = dynamic_cast<HeaderButton*>(wxWindow::FindWindowByName(wxString::FromUTF8(other_name), menu));
            check(other != nullptr && other->decoration().status_word == _L("Not connected"),
                  "printer_menu_shows_the_other_printers_unverified_state");
            if (!m_state->capture_dir.empty()) {
                wxYield();
                check(visible_header_menu() == menu, "printer_menu_stays_open_for_capture");
                blit_screen(m_frame->GetScreenRect(), "printer-menu-other-printers");
            }
            if (other != nullptr) {
                click_row(menu, other);
                wxYield();
                check(selected_printer() == other_name, "printer_menu_switches_the_projects_printer");
                check(chip_label(header, "Printer") == wxString::FromUTF8(other_name),
                      "printer_menu_switch_refreshes_the_chip");
                check(visible_header_menu() == nullptr, "printer_menu_closes_after_switching");
            } else {
                menu->close();
            }
        }

        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        check(Printers::remove_named_printer(*m_plater, other_name).empty(),
              "printer_menu_fixture_removes_other_printer");
        check(Printers::remove_named_printer(*m_plater, selected_name).empty(),
              "printer_menu_fixture_removes_first_printer");
        m_frame->select_tab(size_t(MainFrame::tpHome));
        wxYield();
    }

    // A nozzle changed from Home is a change to that printer, not to the
    // project that happens to be open: the project keeps the printer it has
    // selected and its modified state, and nothing asks the person anything
    // (a dialog would stop this harness where it stands).
    void verify_nozzle_change_leaves_the_project(const std::string& named)
    {
        PresetCollection&              printers = wxGetApp().preset_bundle->printers;
        PrinterSetup::OrcaPrinterBackend backend(*m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()));
        const auto change = [&](double nozzle) {
            PrinterSetup::ChangePrinterRequest request;
            request.name   = named;
            request.nozzle = nozzle;
            PrinterSetup::SavedPrinter changed;
            return backend.change_printer(request, changed);
        };
        const auto parent = [&] {
            const Preset* profile = printer_profile(named);
            return profile == nullptr ? std::string() : profile->inherits();
        };
        const auto edited_nozzle = [&] {
            const auto* nozzle = printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
            return nozzle == nullptr || nozzle->values.empty() ? 0. : nozzle->values.front();
        };

        // Another printer is the project's.
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        const bool dirty = m_plater->is_project_dirty();
        check(change(0.6).empty() && parent() == "Bambu Lab X1 Carbon 0.6 nozzle", "nozzle_change_moves_the_printer_to_its_size");
        check(selected_printer() == kSetupFixturePrinter, "nozzle_change_keeps_the_projects_printer");
        check(m_plater->is_project_dirty() == dirty, "nozzle_change_keeps_the_projects_modified_state");
        check(edited_nozzle() == 0.4, "nozzle_change_leaves_the_projects_printer_settings");
        // Undo is the same operation back.
        check(change(0.4).empty() && parent() == kSetupFixturePrinter, "nozzle_undo_moves_it_back");

        // The printer is the project's, with nothing unsaved: the project's
        // copy follows, still selected, still clean.
        SetupCommands::select_printer_preset(*m_plater, named);
        const bool dirty_on_it = m_plater->is_project_dirty();
        PresetBundle&     bundle         = *wxGetApp().preset_bundle;
        const std::string process_before = bundle.prints.get_selected_preset_name();
        const std::string filament_before = bundle.filament_presets.front();
        check(change(0.6).empty() && selected_printer() == named, "nozzle_change_of_the_projects_printer_keeps_it_selected");
        check(edited_nozzle() == 0.6 && !printers.current_is_dirty(), "nozzle_change_of_the_projects_printer_reaches_it");
        // A process or filament made for the old nozzle is replaced by one
        // that fits, as Orca's own printer switch does. Replacing it is a real
        // change to the project; nothing replaced leaves it as it was.
        const std::string process_after  = bundle.prints.get_selected_preset_name();
        const std::string filament_after = bundle.filament_presets.front();
        const bool        repicked       = process_after != process_before || filament_after != filament_before;
        std::cerr << "HARNESS NOTE nozzle_repick process " << process_before << " -> " << process_after << " filament "
                  << filament_before << " -> " << filament_after << '\n';
        const Preset* process_preset  = bundle.prints.find_preset(process_after);
        const Preset* filament_preset = bundle.filaments.find_preset(filament_after);
        check(process_preset && process_preset->is_compatible && filament_preset && filament_preset->is_compatible,
              "nozzle_change_leaves_a_process_and_filament_that_fit");
        check(m_plater->is_project_dirty() == (dirty_on_it || repicked),
              "nozzle_change_of_the_projects_printer_changes_the_project_only_by_what_it_repicked");

        // Unsaved edits to it are the person's: refused in words, with no
        // prompt, and nothing moved.
        printers.get_edited_preset().config.set_key_value("printer_notes", new ConfigOptionString("unsaved"));
        check(printers.current_is_dirty(), "nozzle_fixture_has_unsaved_printer_edits");
        check(!change(0.4).empty() && parent() == "Bambu Lab X1 Carbon 0.6 nozzle",
              "nozzle_change_refuses_over_unsaved_edits_without_a_prompt");
        printers.discard_current_changes();
        check(change(0.4).empty() && parent() == kSetupFixturePrinter, "nozzle_cleanup_moves_it_back");
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
    }

    // --- The worked examples, live (--printer-live) ------------------------
    //
    // The handoff's worked examples through the real panel: its own session,
    // the live model, the Orca backend. Words and taps go in as the page would
    // send them; each exchange is printed for reading, and what can be checked
    // mechanically is.

    struct LiveStep
    {
        std::string            label;
        std::function<void()>  act;   // opens a session, or sends words or a tap
        std::function<bool()>  ready; // when the exchange has settled
        std::function<void()>  check; // then
    };
    std::deque<LiveStep>     m_live_steps;
    std::size_t              m_live_printed{0};
    std::string              m_live_printer;
    std::vector<std::string> m_live_added;
    bool                     m_live_wizard_seen{false};
    std::string              m_live_host_printer;
    std::string              m_live_step; // the running step's label

    // The Orca backend over the harness's own plater, for what the panel's
    // conversation reads through it.
    PrinterSetup::OrcaPrinterBackend& live_backend()
    {
        static PrinterSetup::OrcaPrinterBackend backend(*m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()));
        return backend;
    }

    PrinterSetup::PrinterPanel* live_panel() const
    {
        ShellController* shell = installed_shell();
        return shell == nullptr ? nullptr : shell->printer_panel();
    }

    // Opens the panel the way Home and the header do: through the shell, which
    // gives it the whole window.
    void live_show(PrinterSetup::ConversationMode mode, const std::string& printer)
    {
        installed_shell()->open_printer_conversation(mode == PrinterSetup::ConversationMode::Add ? std::string() : printer,
                                                     mode == PrinterSetup::ConversationMode::Connect);
    }

    void live_send(const std::string& type, const nlohmann::json& payload)
    {
        static int next = 0;
        // The person can close the panel before the script is done with it.
        // The step then fails by name, and the next open starts a fresh session.
        Agent::AgentHost* host = live_panel()->host();
        if (host == nullptr) {
            std::string step;
            for (const char c : m_live_step)
                step += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
            check(false, "live_panel_open_for_" + step);
            return;
        }
        host->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                             {"version", Agent::Protocol::kVersion},
                                             {"id", "live-" + std::to_string(++next)},
                                             {"type", type},
                                             {"payload", payload}}
                                  .dump());
    }

    // Whether the app posted a note after this message.
    bool live_note_after(const std::string& message_id) const
    {
        const auto messages = live_messages();
        bool after = false;
        for (const auto& message : messages) {
            if (after && message.role == Agent::MessageRole::Note)
                return true;
            after = after || message.id == message_id;
        }
        return false;
    }

    // The page's opening is the thread's first line, as the agent's.
    void check_live_opening(const std::string& start, const std::string& name)
    {
        const auto messages = live_messages();
        check(!messages.empty() && messages.front().role == Agent::MessageRole::Assistant &&
                  messages.front().text.rfind(start, 0) == 0,
              name);
    }

    // --- Pictures of the live run (PRINTER_LIVE_CAPTURE_DIR) ----------------
    //
    // With the variable set, each capture point writes <name>.png of the
    // panel, through WebKit's own snapshot as --printer-connect-capture does.
    void live_capture(const std::string& name)
    {
        const char* dir = std::getenv("PRINTER_LIVE_CAPTURE_DIR");
        if (dir == nullptr)
            return;
        m_state->capture_dir = dir;
        capture_panel(name);
    }

    void say(const std::string& text)
    {
        static int next = 0;
        live_send("user_message", {{"clientMessageId", "live-c-" + std::to_string(++next)}, {"text", text}});
    }

    std::vector<Agent::ConversationMessage> live_messages() const
    {
        if (live_panel()->host() == nullptr)
            return {};
        const Agent::ProjectPersistence* persistence = live_panel()->persistence();
        if (persistence == nullptr)
            return {};
        return persistence->document().messages(persistence->document().active_conversation_id());
    }

    const std::vector<Agent::ToolActivity>& live_activities() const
    {
        static const std::vector<Agent::ToolActivity> none;
        return live_panel()->host() == nullptr ? none : live_panel()->host()->tools().activities();
    }

    std::vector<const Agent::ToolActivity*> live_calls(const std::string& tool) const
    {
        std::vector<const Agent::ToolActivity*> calls;
        for (const Agent::ToolActivity& activity : live_activities())
            if (activity.tool == tool)
                calls.push_back(&activity);
        return calls;
    }

    // Settled: nothing streaming, no tool mid-run, and the model has had its
    // last word.
    bool live_settled() const
    {
        Agent::AgentHost* host = live_panel()->host();
        // A panel that closed has nothing more to say; the step's own
        // checks see what closed it.
        if (host == nullptr)
            return true;
        if (host->stream_active())
            return false;
        const auto& activities = live_activities();
        if (!activities.empty()) {
            const Agent::ToolState state = activities.back().state;
            if (state == Agent::ToolState::Running)
                return false;
        }
        // A finished reply with no words is settled too; live_print_new
        // says so, rather than the run waiting on it forever.
        const auto messages = live_messages();
        return !messages.empty() && messages.back().role == Agent::MessageRole::Assistant &&
               messages.back().state == Agent::MessageState::Complete;
    }

    static const char* tool_state_label(Agent::ToolState state)
    {
        switch (state) {
        case Agent::ToolState::Running: return "running";
        case Agent::ToolState::Succeeded: return "succeeded";
        case Agent::ToolState::Failed: return "failed";
        case Agent::ToolState::Cancelled: return "cancelled";
        }
        return "?";
    }

    void live_print_new()
    {
        if (live_panel()->host() == nullptr)
            std::cerr << "HARNESS LIVE   (the panel closed)\n";
        const auto messages = live_messages();
        for (std::size_t i = m_live_printed; i < messages.size(); ++i) {
            const auto& message = messages[i];
            const char* who = message.role == Agent::MessageRole::User ? "person" :
                              message.role == Agent::MessageRole::Note ? "app" :
                                                                         "model";
            if (!message.text.empty())
                std::cerr << "HARNESS LIVE   " << who << ": " << message.text << '\n';
            for (const nlohmann::json& receipt : live_receipts())
                if (receipt.value("afterMessageId", "") == message.id)
                    std::cerr << "HARNESS LIVE   receipt: Printer " << (receipt.value("removed", false) ? "removed" : "added") << ", "
                              << receipt["printer"].value("name", "") << ", "
                              << receipt["printer"].value("nozzle", 0.) << " mm nozzle\n";
        }
        if (m_live_printed < messages.size() && messages.back().role == Agent::MessageRole::Assistant && messages.back().text.empty())
            std::cerr << "HARNESS LIVE   model: (a finished reply with no words)\n";
        m_live_printed = messages.size();
    }

    // The "Printer added" cards the page draws in this session's thread.
    std::vector<nlohmann::json> live_receipts() const
    {
        std::vector<nlohmann::json> receipts;
        if (live_panel()->host() == nullptr)
            return receipts;
        for (const nlohmann::json& block : live_panel()->session_json().value("blocks", nlohmann::json::array()))
            if (block.value("kind", "") == "added")
                receipts.push_back(block);
        return receipts;
    }

    // One receipt per printer added, under a message the thread has.
    bool live_receipt_for(const std::string& printer) const
    {
        const auto receipts = live_receipts();
        if (receipts.size() != 1 || receipts.front()["printer"].value("name", "") != printer)
            return false;
        const auto messages = live_messages();
        return std::any_of(messages.begin(), messages.end(), [&](const Agent::ConversationMessage& message) {
            return message.id == receipts.front().value("afterMessageId", "");
        });
    }

    void run_live_steps()
    {
        if (m_live_steps.empty()) {
            finish_printer_live();
            return;
        }
        LiveStep step = std::move(m_live_steps.front());
        m_live_steps.pop_front();
        std::cerr << "HARNESS LIVE -- " << step.label << '\n';
        m_live_step = step.label;
        step.act();
        auto ready = step.ready ? step.ready : [this] { return live_settled(); };
        wait_until(
            ready, "live_" + step.label,
            [self = shared_from_this(), check = step.check] {
                self->live_print_new();
                if (check)
                    check();
                self->run_live_steps();
            },
            [this, ticks = std::make_shared<int>(0)] {
                if (++*ticks % 300 != 0 || live_panel()->host() == nullptr)
                    return;
                const auto messages = live_messages();
                std::cerr << "HARNESS LIVE   waiting: stream=" << live_panel()->host()->stream_active()
                          << " messages=" << messages.size() << " tools=" << live_activities().size();
                if (!messages.empty())
                    std::cerr << " last role=" << static_cast<int>(messages.back().role)
                              << " state=" << static_cast<int>(messages.back().state) << " text=" << messages.back().text.size()
                              << (messages.back().error ? " error=" + messages.back().error->code + " " + messages.back().error->message : "");
                std::cerr << '\n';
            });
    }

    void live_open(PrinterSetup::ConversationMode mode, const std::string& printer = {})
    {
        m_live_steps.push_back({mode == PrinterSetup::ConversationMode::Add ? "open: add" : "open: " + printer,
                                [this, mode, printer] {
                                    live_show(mode, printer);
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                {}});
    }

    void live_say(const std::string& words, std::function<void()> check)
    {
        m_live_steps.push_back({"say: " + words, [this, words] { say(words); }, {}, std::move(check)});
    }

    // Once nothing is left to decide, the assistant says the person can close
    // the chat. The person then uses the panel's close action.
    void live_close(const std::string& after)
    {
        m_live_steps.push_back({"check: safe to close", [] {}, [] { return true; }, [this, after] {
                                    const auto messages = live_messages();
                                    std::string reply = messages.empty() ? "" : messages.back().text;
                                    std::transform(reply.begin(), reply.end(), reply.begin(),
                                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                    check(!messages.empty() && messages.back().role == Agent::MessageRole::Assistant &&
                                              (reply.find("can close") != std::string::npos ||
                                               reply.find("safe to close") != std::string::npos) &&
                                              reply.find("chat") != std::string::npos &&
                                              reply.find("choices:") == std::string::npos,
                                          "live_" + after + "_says_the_chat_can_close");
                                }});
        m_live_steps.push_back({"person closes the chat",
                                [this] { live_send("printer_action", {{"action", "close"}}); },
                                [this] { return !live_panel()->IsShown(); },
                                [this, after] { check(!live_panel()->IsShown(), "live_person_closes_after_" + after); }});
    }

    bool live_identified(const std::string& id) const
    {
        for (const Agent::ToolActivity* call : live_calls("printer_identify"))
            if (call->state == Agent::ToolState::Succeeded && call->arguments_json.find("\"" + id + "\"") != std::string::npos)
                return true;
        return false;
    }

    bool live_drew_a_card() const
    {
        for (const Agent::ToolActivity* call : live_calls("printer_identify"))
            if (call->state == Agent::ToolState::Succeeded)
                return true;
        return false;
    }

    void live_print_calls()
    {
        for (const Agent::ToolActivity& activity : live_activities())
            std::cerr << "HARNESS LIVE   tool " << activity.tool << ' ' << activity.arguments_json << " -> "
                      << (activity.error ? activity.error->code : std::string(tool_state_label(activity.state))) << '\n';
    }

    void begin_printer_live()
    {
        if (live_panel() == nullptr || std::getenv("OPENAI_API_KEY") == nullptr) {
            fail("the printer panel or the OpenAI key is missing");
            return;
        }
        // The panel builds its own Agent from the app configuration, as the
        // shipped app does, so the key goes there: this run's throwaway data
        // directory, never the person's.
        // As a std::string: a char* would pick AppConfig::set's bool overload.
        wxGetApp().app_config->set("jusprin_agent", "openai_api_key", std::string(std::getenv("OPENAI_API_KEY")));
        if (m_state->connect_capture) {
            queue_connect_capture();
            run_live_steps();
            return;
        }
        if (m_state->no_plugin) {
            queue_no_plugin_live();
            run_live_steps();
            return;
        }
        if (m_state->settings_live) {
            queue_settings_live();
            run_live_steps();
            return;
        }
        if (m_state->filament_settings_live) {
            queue_filament_settings_live();
            run_live_steps();
            return;
        }
        using Mode = PrinterSetup::ConversationMode;

        // Adding: nothing is saved until the person says yes.
        live_open(Mode::Add);
        live_say("the small bambu one", [this] {
            check_live_opening("Which printer do you have?", "live_add_opens_with_the_pages_line");
            live_print_calls();
            check(live_calls("printer_add").empty(), "live_nothing_added_before_the_person_says_which");
        });
        // Naming the model plainly adds it, with the nozzle it ships with.
        const auto added_printers = [this] {
            std::vector<std::string> names;
            for (const Agent::ToolActivity* call : live_calls("printer_add"))
                if (call->state == Agent::ToolState::Succeeded)
                    names.push_back(nlohmann::json::parse(call->result_json)["printer"].value("name", ""));
            return names;
        };
        live_say("the A1 mini", [this, added_printers] {
            live_print_calls();
            m_live_added = added_printers();
            check(m_live_added.size() == 1 && printer_profile(m_live_added.front()) != nullptr, "live_naming_the_model_adds_it");
            check(!m_live_added.empty() && live_receipt_for(m_live_added.front()), "live_an_added_printer_draws_its_receipt");
            live_capture("add-a1-mini");
        });
        live_say("yes, add it", [this, added_printers] {
            live_print_calls();
            check(added_printers().size() == 1, "live_a_second_yes_adds_no_second_printer");
            check(!m_live_added.empty() && live_receipt_for(m_live_added.front()), "live_a_second_yes_draws_no_second_receipt");
            live_capture("add-a1-mini-second-yes");
        });
        live_say("Not now", [this] { live_print_calls(); });
        live_close("not_now");

        live_open(Mode::Add);
        live_say("prusa mk4", [this] {
            live_print_calls();
            // A name that begins several models is a question, not one of them.
            bool only_mk4s = true;
            std::size_t shown = 0;
            for (const Agent::ToolActivity* call : live_calls("printer_identify"))
                if (call->state == Agent::ToolState::Succeeded)
                    for (const auto& id : nlohmann::json::parse(call->arguments_json).value("catalogIds", nlohmann::json::array())) {
                        only_mk4s = only_mk4s && id.get<std::string>().rfind("Prusa/Prusa MK4", 0) == 0;
                        ++shown;
                    }
            check(only_mk4s && shown != 1, "live_prusa_mk4_asks_which");
            live_capture("add-prusa-mk4-asks-which");
        });
        // Answering with one of the three, as a tap on its choice sends it.
        live_say("Prusa MK4S", [this] {
            live_print_calls();
            std::string added;
            for (const Agent::ToolActivity* call : live_calls("printer_add"))
                if (call->state == Agent::ToolState::Succeeded)
                    added = nlohmann::json::parse(call->result_json)["printer"].value("name", "");
            if (!added.empty())
                m_live_added.push_back(added);
            check(!added.empty() && live_receipt_for(added), "live_a_chosen_printer_draws_its_receipt");
            live_capture("add-prusa-mk4s-chosen");
        });
        // Undo on its receipt deletes it as Orca does -- the profile it was
        // based on is selected -- and the model answers the app's note.
        auto undone = std::make_shared<std::pair<std::string, std::string>>(); // the printer, its parent
        m_live_steps.push_back({"tap Undo on the receipt",
                                [this, undone] {
                                    const auto receipts = live_receipts();
                                    if (receipts.empty())
                                        return;
                                    undone->first = receipts.back()["printer"].value("name", "");
                                    if (const Preset* printer = printer_profile(undone->first))
                                        undone->second = printer->inherits();
                                    live_send("printer_action", {{"action", "undo_add"}, {"blockId", receipts.back().value("id", "")}});
                                },
                                [this] {
                                    const auto receipts = live_receipts();
                                    return (receipts.empty() || receipts.back().value("removed", false)) && live_settled();
                                },
                                [this, undone] {
                                    live_print_calls();
                                    const auto receipts = live_receipts();
                                    check(!undone->first.empty() && printer_profile(undone->first) == nullptr, "live_undo_removes_the_added_printer");
                                    check(!receipts.empty() && receipts.back().value("removed", false), "live_undo_marks_the_receipt");
                                    check(!undone->second.empty() && selected_printer() == undone->second,
                                          "live_undo_selects_the_profile_the_printer_was_based_on");
                                    const auto messages = live_messages();
                                    const auto note     = std::find_if(messages.rbegin(), messages.rend(), [](const Agent::ConversationMessage& message) {
                                        return message.role == Agent::MessageRole::Note;
                                    });
                                    check(note != messages.rbegin() && note != messages.rend() &&
                                              note->text.find("tapped Undo") != std::string::npos &&
                                              messages.back().role == Agent::MessageRole::Assistant &&
                                              messages.back().text.find('?') != std::string::npos,
                                          "live_undo_is_answered_with_a_question");
                                    const auto adds = live_calls("printer_add");
                                    check(std::count_if(adds.begin(), adds.end(), [](const Agent::ToolActivity* call) {
                                              return call->state == Agent::ToolState::Succeeded;
                                          }) == 1,
                                          "live_undo_adds_nothing_back");
                                    m_live_added.erase(std::remove(m_live_added.begin(), m_live_added.end(), undone->first), m_live_added.end());
                                    live_capture("add-prusa-mk4s-undone");
                                }});

        live_open(Mode::Add);
        live_say("my elegoo mars", [this] {
            live_print_calls();
            check(live_calls("printer_identify").empty() && live_calls("printer_add").empty(), "live_elegoo_mars_draws_no_card");
            const auto messages = live_messages();
            std::string reply = messages.empty() ? "" : messages.back().text;
            std::transform(reply.begin(), reply.end(), reply.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            check(!messages.empty() && messages.back().role == Agent::MessageRole::Assistant &&
                      reply.find("manual") != std::string::npos,
                  "live_elegoo_mars_suggests_manual_setup");
        });

        // The person's menu action opens OrcaSlicer's own window. The model
        // has no tool to open it. Close it here as a person would.
        live_open(Mode::Add);
        m_live_steps.push_back({"menu: browse the full printer list",
                                [this] {
                                    m_live_wizard_seen = false;
                                    wxGetApp().CallAfter([self = shared_from_this()] {
                                        self->live_send("printer_action", {{"action", "manual_setup"}});
                                    });
                                },
                                [this] {
                                    // Closed only once its page has loaded: the window installs
                                    // its page's script handler from a CallAfter that waits on the
                                    // page, and closing it first leaves that wait hanging on a
                                    // hidden view (see the old setup_manual check).
                                    for (wxWindow* window : wxTopLevelWindows)
                                        if (auto* guide = dynamic_cast<GuideFrame*>(window); guide != nullptr && guide->IsModal()) {
                                            if (wxGetApp().is_adding_script_handler())
                                                return false;
                                            std::function<wxWebView*(wxWindow*)> find = [&](wxWindow* parent) -> wxWebView* {
                                                if (auto* view = dynamic_cast<wxWebView*>(parent))
                                                    return view;
                                                for (wxWindow* child : parent->GetChildren())
                                                    if (wxWebView* view = find(child))
                                                        return view;
                                                return nullptr;
                                            };
                                            wxWebView* page = find(guide);
                                            if (page == nullptr || page->IsBusy() || page->GetCurrentURL().empty())
                                                return false;
                                            m_live_wizard_seen = true;
                                            guide->EndModal(wxID_CANCEL);
                                            return false;
                                        }
                                    return live_settled();
                                },
                                [this] {
                                    live_print_calls();
                                    check(m_live_wizard_seen, "live_full_list_opens_orcas_window");
                                    check(live_activities().empty(), "live_full_list_needs_no_model_tool");
                                }});

        // Changing: a printer to change, the fixture's own profile under a
        // name, while the project keeps the fixture selected.
        m_live_steps.push_back({"make: Lab Printer",
                                [this] {
                                    // A named printer is saved from the selected system profile,
                                    // and the Add above left its own printer selected.
                                    SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                    m_live_printer = Printers::add_named_printer(*m_plater, "Lab Printer", {});
                                    SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                },
                                [] { return true; },
                                {}});
        m_live_steps.push_back({"open: change",
                                [this] {
                                    live_show(Mode::Change, m_live_printer);
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                {}});
        const auto parent_is = [this](const std::string& profile) {
            const Preset* printer = printer_profile(m_live_printer);
            return printer != nullptr && printer->inherits() == profile;
        };
        // A change the person states plainly is made at once; one the
        // assistant would have to guess is asked about first.
        const auto saved_changes = [this] {
            std::size_t saved = 0;
            for (const Agent::ToolActivity* call : live_calls("printer_change"))
                saved += call->state == Agent::ToolState::Succeeded ? 1 : 0;
            return saved;
        };
        live_say("I think I swapped the nozzle but I'm not sure which size", [this, parent_is, saved_changes] {
            check_live_opening("This is the " + m_live_printer + ". ", "live_change_opens_naming_the_printer");
            live_print_calls();
            check(saved_changes() == 0 && parent_is(kSetupFixturePrinter), "live_a_guess_is_asked_about_first");
        });
        live_say("i put a 0.6 nozzle on it", [this, parent_is] {
            live_print_calls();
            check(parent_is("Bambu Lab X1 Carbon 0.6 nozzle"), "live_a_plain_statement_changes_the_nozzle");
            check(selected_printer() == kSetupFixturePrinter, "live_nozzle_change_keeps_the_projects_printer");
        });
        live_say("actually put it back to 0.4", [this, parent_is] {
            live_print_calls();
            check(parent_is(kSetupFixturePrinter), "live_putting_it_back_is_the_same_tool");
        });
        live_say("swapped to a hardened steel 0.4", [this, parent_is, saved_changes] {
            check(saved_changes() == 2 && parent_is(kSetupFixturePrinter), "live_hardened_steel_changes_nothing");
        });
        live_say("the plate is smooth PEI now", [this, parent_is, saved_changes] {
            check(saved_changes() == 2 && parent_is(kSetupFixturePrinter), "live_plate_is_the_projects");
        });
        live_say("i put a 0.3 on it", [this, parent_is] {
            live_print_calls();
            check(parent_is(kSetupFixturePrinter), "live_0_3_is_never_saved");
        });
        // A setting no tool reads, about a printer the person already has: the
        // reply once offered to add it first (2026-09-28). Its words are
        // printed; what is checked is that nothing is added or looked up.
        live_say("what's the current start g-code?", [this] {
            check(live_calls("printer_add").empty() && live_calls("printer_identify").empty(),
                  "live_a_question_about_a_saved_printer_adds_nothing");
        });

        // Connecting the printer added above, to the in-process fake Bambu
        // Lab printer: the code goes in on the card, never in the thread.
        static constexpr const char* kCode = "13572468";
        const auto card = [this]() -> const Agent::ToolActivity* {
            const auto calls = live_calls("printer_connect");
            return calls.empty() ? nullptr : calls.back();
        };
        const auto credential = [this]() {
            return live_panel()->session_json().value("credentialRequest", nlohmann::json::object());
        };
        // The fake is found the way a printer on the network is: discovery
        // starts, and the device list fills. Waited for here, so what the
        // model does next is about the conversation, not the network.
        m_live_steps.push_back({"fake printer on",
                                [this] {
                                    wxGetApp().app_config->set("jusprin", "fake_printer", "true");
                                    live_backend().prepare_connection(m_live_added.empty() ? std::string() : m_live_added.front());
                                },
                                [this] {
                                    return !m_live_added.empty() &&
                                           !live_backend().connection(m_live_added.front()).candidates.empty();
                                },
                                [this] {
                                    for (const auto& candidate : live_backend().connection(m_live_added.front()).candidates)
                                        std::cerr << "HARNESS LIVE   found " << candidate.id << ' ' << candidate.name
                                                  << (candidate.lan_mode ? " (LAN)" : " (account)") << '\n';
                                }});
        // The printer is the one the Add above saved, known only once it ran.
        m_live_steps.push_back({"open: connect the added printer",
                                [this] {
                                    live_show(Mode::Connect, m_live_added.empty() ? std::string() : m_live_added.front());
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                [this] { check(!m_live_added.empty(), "live_connect_has_the_added_printer"); }});
        // The model may request the credential at once, or first ask whether
        // this is the printer: the script answers only if it asked.
        live_say("Yes, it is", [this] { live_print_calls(); });
        m_live_steps.push_back({"say, if still asked: Yes, connect it",
                                [this, credential] {
                                    if (credential().empty())
                                        say("Yes, connect it");
                                },
                                {},
                                [this, card, credential] {
                                    live_print_new();
                                    live_print_calls();
                                    check(card() != nullptr && credential().value("actionId", "") == card()->action_id,
                                          "live_connect_asks_for_the_code_locally");
                                }});
        // Types the code in the local form and taps Connect, then waits for the
        // app's note about how it went.
        const auto connect = [this, card, credential](const std::string& label, const char* verified_check) {
            m_live_steps.push_back({label,
                                    [this, credential] {
                                        const auto request = credential();
                                        if (!request.empty())
                                            live_send("printer_action", {{"action", "connect"}, {"actionId", request["actionId"]},
                                                                          {"credential", kCode}});
                                    },
                                    [this, card] {
                                        const auto calls = live_calls("printer_connect");
                                        return !calls.empty() && live_note_after(calls.back()->correlation_id) && live_settled();
                                    },
                                    [this, verified_check] {
                                        live_print_calls();
                                        const auto messages = live_messages();
                                        check(std::any_of(messages.begin(), messages.end(), [](const auto& message) {
                                                  return message.role == Agent::MessageRole::Note && message.text.find("verified") != std::string::npos;
                                              }),
                                              verified_check);
                                    }});
        };
        connect("type the code, tap Connect", "live_connection_is_verified_and_reported");
        // A Moonraker printer, against the stand-in server the run was given
        // (PRINTER_LIVE_MOONRAKER): the API key goes in on the card and
        // must reach the printer's own request header.
        if (const char* host = std::getenv("PRINTER_LIVE_MOONRAKER")) {
            static constexpr const char* kKey = "moonkey123";
            const std::string address = host;
            m_live_steps.push_back({"make: a Klipper printer",
                                    [this] {
                                        const auto before = wxGetApp().app_config->vendors();
                                        std::string error;
                                        SetupCommands::install_and_select_printer(*m_plater, "Custom", "Generic Klipper Printer", "0.4", {}, error);
                                        Printers::name_installed_printers(*m_plater, before);
                                        m_live_host_printer = "Generic Klipper Printer";
                                        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                    },
                                    [] { return true; },
                                    [this] { check(printer_profile(m_live_host_printer) != nullptr, "live_klipper_printer_saved"); }});
            m_live_steps.push_back({"open: connect the Klipper printer",
                                    [this] {
                                        live_show(Mode::Connect, m_live_host_printer);
                                        m_live_printed = 0;
                                    },
                                    [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                    [this] { check_live_opening("Let’s connect " + m_live_host_printer + ".", "live_host_opening_asks_for_the_address"); }});
            live_say(address, [this] { live_print_calls(); });
            // It may ask which kind of server this is.
            m_live_steps.push_back({"say, if still asked: Moonraker",
                                    [this, credential] {
                                        if (credential().empty())
                                            say("Moonraker");
                                    },
                                    {},
                                    [this, card, credential] {
                                        live_print_new();
                                        live_print_calls();
                                        check(card() != nullptr && credential().value("actionId", "") == card()->action_id &&
                                                  credential().value("provider", "") == "host",
                                              "live_host_asks_for_the_key_locally");
                                    }});
            // And asks while the printer is being checked: the question is
            // answered, and the app's note about the outcome comes after it.
            m_live_steps.push_back({"type the API key, tap Connect, ask how long it takes",
                                    [this, credential] {
                                        const auto request = credential();
                                        if (!request.empty())
                                            live_send("printer_action", {{"action", "connect"}, {"actionId", request["actionId"]},
                                                                          {"credential", kKey}});
                                        say("how long does this take?");
                                    },
                                    [this, card] { return card() != nullptr && live_note_after(card()->correlation_id) && live_settled(); },
                                    [this] {
                                        live_print_new();
                                        live_print_calls();
                                        const auto messages = live_messages();
                                        check(std::any_of(messages.begin(), messages.end(), [](const auto& message) {
                                                  return message.role == Agent::MessageRole::Note &&
                                                         message.text.find("verified") != std::string::npos;
                                              }),
                                              "live_host_connection_is_verified");
                                        const char* log = std::getenv("PRINTER_LIVE_MOONRAKER_LOG");
                                        std::ifstream seen(log ? log : "");
                                        const std::string requests((std::istreambuf_iterator<char>(seen)), std::istreambuf_iterator<char>());
                                        check(requests.find("key=right") != std::string::npos, "live_the_typed_key_reaches_the_printer");
                                        bool leaked = false;
                                        for (const auto& message : live_messages())
                                            leaked = leaked || message.text.find(kKey) != std::string::npos;
                                        for (const auto& activity : live_activities())
                                            leaked = leaked || activity.arguments_json.find(kKey) != std::string::npos ||
                                                     activity.result_json.find(kKey) != std::string::npos;
                                        check(!leaked, "live_the_key_is_nowhere_in_the_conversation");
                                    }});
        }
        m_live_steps.push_back({"the code stays out of the conversation", [] {}, [] { return true; }, [this] {
                                    bool leaked = live_panel()->session_json().dump().find(kCode) != std::string::npos;
                                    for (const auto& message : live_messages())
                                        leaked = leaked || message.text.find(kCode) != std::string::npos;
                                    for (const auto& activity : live_activities())
                                        leaked = leaked || activity.arguments_json.find(kCode) != std::string::npos ||
                                                 activity.result_json.find(kCode) != std::string::npos;
                                    check(!leaked, "live_the_code_is_nowhere_in_the_conversation");
                                }});
        live_close("verified_connection");
        run_live_steps();
    }

    // --printer-connect-capture: the local credential form and the connected
    // printer on Home.

    const Agent::ToolActivity* last_connect() const
    {
        const auto calls = live_calls("printer_connect");
        return calls.empty() ? nullptr : calls.back();
    }

    void capture_web_view(wxWebView* view, const std::string& name)
    {
#ifdef __APPLE__
        fs::create_directories(m_state->capture_dir);
        const std::string file = (m_state->capture_dir / (name + ".png")).string();
        const bool ok = view != nullptr && snapshot_web_view(view->GetNativeBackend(), file.c_str());
        check(ok, "captured_" + name);
        if (ok)
            std::cout << "HARNESS ARTIFACT " << name << " " << file << std::endl;
#elif defined(_WIN32)
        fs::create_directories(m_state->capture_dir);
        const std::string file = (m_state->capture_dir / (name + ".png")).string();
        const bool ok = view != nullptr && snapshot_web_view(view->GetNativeBackend(), file.c_str());
        check(ok, "captured_" + name);
        if (ok)
            std::cout << "HARNESS ARTIFACT " << name << " " << file << std::endl;
#else
        fail("--printer-connect-capture pictures web views on macOS and Windows only");
#endif
    }

    void capture_panel(const std::string& name) { capture_web_view(live_panel()->web_view()->webview(), name); }

    // Gives `address` and submits the local credential form,
    // naming the server if the model asks which kind it is.
    void connect_to(const std::string& words, const std::string& address, const std::string& picture)
    {
        m_live_steps.push_back({"say: " + words, [this, words] { say(words); }, {}, {}});
        m_live_steps.push_back({"say, if asked: Moonraker",
                                [this, address] {
                                    const auto* card = last_connect();
                                    const auto request = live_panel()->session_json().value("credentialRequest", nlohmann::json::object());
                                    if (card == nullptr || request.value("actionId", "") != card->action_id ||
                                        card->arguments_json.find(address) == std::string::npos)
                                        say("Moonraker");
                                },
                                {},
                                [this, address, picture] {
                                    live_print_calls();
                                    const auto* card = last_connect();
                                    const auto request = live_panel()->session_json().value("credentialRequest", nlohmann::json::object());
                                    check(card != nullptr && request.value("actionId", "") == card->action_id &&
                                              card->arguments_json.find(address) != std::string::npos,
                                          "capture_credential_form_for_" + picture);
                                    if (!picture.empty())
                                        capture_panel(picture);
                                }});
        m_live_steps.push_back({"type the API key, tap Connect",
                                [this] {
                                    const auto request = live_panel()->session_json().value("credentialRequest", nlohmann::json::object());
                                    if (!request.empty())
                                        live_send("printer_action", {{"action", "connect"}, {"actionId", request["actionId"]},
                                                                      {"credential", "moonkey123"}});
                                },
                                [this] {
                                    const auto* card = last_connect();
                                    return card != nullptr && live_settled();
                                },
                                {}});
    }

    // The header's Printer settings… on a project that uses the printer
    // OrcaSlicer ships (reported 2026-09-28: it opened a conversation about
    // adding a printer). A change is saved as a copy, which the project then
    // uses and the conversation is then about.
    void queue_settings_live()
    {
        using Mode = PrinterSetup::ConversationMode;
        const Preset& selected = wxGetApp().preset_bundle->printers.get_selected_preset();
        const std::string stock = selected.name;
        const std::string copy  = stock + " - Copy";
        check(selected.is_system, "live_settings_fixture_printer_is_stock");
        const auto card = [this]() -> const Agent::ToolActivity* {
            const auto calls = live_calls("settings_apply_patch");
            return calls.empty() ? nullptr : calls.back();
        };
        live_open(Mode::Change, stock);
        live_say("set the retraction length to 1 mm", [this, card, copy] {
            live_print_calls();
            check(live_panel()->session_json().value("mode", "") == "change", "live_settings_conversation_is_a_change");
            check(card() != nullptr && card()->state == Agent::ToolState::Succeeded &&
                      nlohmann::json::parse(card()->arguments_json).value("persistAs", "") == copy,
                  "live_settings_change_saves_as_a_copy");
        });
        m_live_steps.push_back({"verify the settings change",
                                [] {},
                                [this, card] { return card() != nullptr && Agent::tool_state_terminal(card()->state) && live_settled(); },
                                [this, card, copy, stock] {
                                    live_print_calls();
                                    const PresetCollection& printers = wxGetApp().preset_bundle->printers;
                                    const Preset*           saved    = printers.find_preset(copy, false);
                                    check(card() != nullptr && card()->state == Agent::ToolState::Succeeded, "live_settings_activity_succeeds");
                                    check(saved != nullptr && !saved->is_system && printers.get_selected_preset_name() == copy &&
                                              !printers.current_is_dirty(),
                                          "live_settings_copy_is_saved_and_selected");
                                    const auto* length = saved != nullptr ? saved->config.option<ConfigOptionFloats>("retraction_length") : nullptr;
                                    check(length != nullptr && std::all_of(length->values.begin(), length->values.end(),
                                                                           [](double value) { return std::abs(value - 1.0) < 1e-6; }),
                                          "live_settings_copy_holds_the_change");
                                    const auto* shipped = printers.find_preset(stock, false)->config.option<ConfigOptionFloats>("retraction_length");
                                    check(shipped != nullptr && std::abs(shipped->get_at(0) - 1.0) > 1e-6, "live_settings_shipped_printer_unchanged");
                                    check(live_panel()->session_json().value("printerName", "") == copy,
                                          "live_settings_conversation_follows_the_copy");
                                    live_capture("settings-saved-as-copy");
                                }});
    }

    // The header's Filament settings… for slot 1, which holds a filament
    // OrcaSlicer ships: the change is saved as a copy, which takes the slot.
    void queue_filament_settings_live()
    {
        const auto card = [this]() -> const Agent::ToolActivity* {
            const auto calls = live_calls("settings_apply_patch");
            return calls.empty() ? nullptr : calls.back();
        };
        auto before = std::make_shared<std::pair<std::string, std::string>>(); // the preset, its temperatures
        m_live_steps.push_back({"open: filament settings for slot 1",
                                [this, before] {
                                    m_frame->CallAfter([this, before] {
                                        const auto& bundle = *wxGetApp().preset_bundle;
                                        before->first      = bundle.filament_presets.front();
                                        before->second     = bundle.filaments.find_preset(before->first, false)->config.opt_serialize("nozzle_temperature");
                                        installed_shell()->open_filament_help(0, before->first, SetupCommands::current_filament().alias.ToStdString());
                                        m_live_printed = 0;
                                    });
                                },
                                [this] {
                                    return live_panel()->IsShown() && live_panel()->host() != nullptr &&
                                           live_panel()->host()->handshake_complete() && live_panel()->instructions_ready();
                                },
                                [this, before] {
                                    check(wxGetApp().preset_bundle->filaments.find_preset(before->first, false)->is_system,
                                          "live_filament_fixture_is_stock");
                                }});
        live_say("make the nozzle 5 degrees hotter", [this, card, before] {
            live_print_calls();
            const auto arguments = card() != nullptr ? nlohmann::json::parse(card()->arguments_json) : nlohmann::json::object();
            check(card() != nullptr && card()->state == Agent::ToolState::Succeeded && arguments.value("scope", "") == "filament" &&
                      arguments["target"].value("preset", "") == before->first &&
                      arguments.value("persistAs", "") == before->first + " - Copy",
                  "live_filament_change_saves_as_a_copy");
        });
        m_live_steps.push_back({"verify the filament change",
                                [] {},
                                [this, card] { return (card() == nullptr || Agent::tool_state_terminal(card()->state)) && live_settled(); },
                                [this, card, before] {
                                    live_print_calls();
                                    const auto&   bundle = *wxGetApp().preset_bundle;
                                    const std::string copy = before->first + " - Copy";
                                    const Preset* saved  = bundle.filaments.find_preset(copy, false);
                                    check(card() != nullptr && card()->state == Agent::ToolState::Succeeded, "live_filament_card_applies");
                                    check(saved != nullptr && !saved->is_system && bundle.filament_presets.front() == copy &&
                                              !bundle.filaments.current_is_dirty(),
                                          "live_filament_copy_takes_the_slot");
                                    std::string hotter;
                                    std::istringstream temperatures(before->second);
                                    for (std::string value; std::getline(temperatures, value, ',');)
                                        hotter += (hotter.empty() ? "" : ",") + std::to_string(std::stoi(value) + 5);
                                    check(saved != nullptr && saved->config.opt_serialize("nozzle_temperature") == hotter,
                                          "live_filament_copy_holds_the_change");
                                    check(bundle.filaments.find_preset(before->first, false)->config.opt_serialize("nozzle_temperature") ==
                                              before->second,
                                          "live_filament_shipped_preset_unchanged");
                                    live_capture("filament-saved-as-copy");
                                }});
    }

    void queue_connect_capture()
    {
        using Mode = PrinterSetup::ConversationMode;
        const auto answering = std::make_shared<StandInHost>(3000ms, /*answer=*/true);

        m_live_steps.push_back({"make: a Klipper printer",
                                [this] {
                                    const auto before = wxGetApp().app_config->vendors();
                                    std::string error;
                                    SetupCommands::install_and_select_printer(*m_plater, "Custom", "Generic Klipper Printer", "0.4", {}, error);
                                    Printers::name_installed_printers(*m_plater, before);
                                    m_live_host_printer = "Generic Klipper Printer";
                                    SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
                                },
                                [] { return true; },
                                [this] { check(printer_profile(m_live_host_printer) != nullptr, "capture_klipper_printer_saved"); }});
        m_live_steps.push_back({"open: connect the Klipper printer",
                                [this] {
                                    live_show(Mode::Connect, m_live_host_printer);
                                    m_live_printed = 0;
                                },
                                [this] { return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() && live_panel()->instructions_ready(); },
                                {}});

        connect_to(answering->address(), answering->address(), "1-credential");
        m_live_steps.push_back({"wait: the attempt is verified", [] {},
                                [this] {
                                    const auto* card = last_connect();
                                    return card != nullptr && live_note_after(card->correlation_id) && live_settled();
                                },
                                [this, answering] {
                                    check(printer_profile(m_live_host_printer)->config.opt_string("print_host") ==
                                              answering->address(),
                                          "capture_verified_address_saved");
                                }});
        // Home, with the printer the panel connected.
        m_live_steps.push_back({"back to Home", [this] { live_panel()->close(); },
                                [this] { return !live_panel()->IsShown() && installed_shell()->home_view()->IsShown(); },
                                [this] {
                                    for (int settle = 0; settle < 20; ++settle) {
                                        wxYield();
                                        wxMilliSleep(50);
                                    }
                                    capture_web_view(installed_shell()->home_view()->webview(), "2-home");
                                }});
    }

    // --- A Bambu Lab printer without the network plug-in, live -------------
    //
    // What someone hears when they ask to connect a Bambu Lab printer on a
    // machine where the plug-in is not installed: from the add flow, and
    // from Home's Connect…. The fake printer stays off, so nothing stands in
    // for the plug-in.
    static bool mentions(std::string text, const std::vector<std::string>& words)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return std::any_of(words.begin(), words.end(), [&](const std::string& word) { return text.find(word) != std::string::npos; });
    }

    static bool mentions_plugin(const std::string& text) { return mentions(text, {"plug-in", "plugin", "plug in"}); }

    // Whether printer_connection_status has said the plug-in is missing, and
    // whether a credential form opened for a code it could never use.
    bool live_status_said_unavailable() const
    {
        for (const Agent::ToolActivity* call : live_calls("printer_connection_status"))
            if (call->state == Agent::ToolState::Succeeded &&
                nlohmann::json::parse(call->result_json).value("state", "") == "unavailable")
                return true;
        return false;
    }

    bool live_credential_requested() const
    {
        return live_panel()->session_json().contains("credentialRequest");
    }

    // A picture of the panel (PRINTER_LIVE_CAPTURE_DIR), named for the
    // appearance the run was given.
    void live_capture_themed(const std::string& name)
    {
        std::string file = name;
        std::replace(file.begin(), file.end(), '_', '-');
        live_capture(file + (m_state->dark_appearance.value_or(false) ? "-dark" : "-light"));
    }

    // The app's notice that the plug-in is missing, as the page draws it.
    std::size_t live_plugin_notices() const
    {
        if (live_panel()->host() == nullptr)
            return 0;
        std::size_t notices = 0;
        for (const nlohmann::json& block : live_panel()->session_json().value("blocks", nlohmann::json::array()))
            notices += block.value("kind", "") == "plugin" ? 1 : 0;
        return notices;
    }

    // A printer_connect the model tried anyway was refused with the plug-in
    // as the reason, not as an unknown printer.
    bool live_connect_refusals_name_the_plugin() const
    {
        for (const Agent::ToolActivity* call : live_calls("printer_connect"))
            if (call->error && call->error->code != "connection_unavailable")
                return false;
        return true;
    }

    void queue_no_plugin_live()
    {
        using Mode = PrinterSetup::ConversationMode;
        m_live_steps.push_back({"check: no plug-in, no fake", [] {}, [] { return true; }, [this] {
                                    const bool loaded = NetworkAgent::is_network_module_loaded();
                                    const std::string fake = fake_bambu_printer_agent_id(wxGetApp().app_config);
                                    std::cerr << "HARNESS LIVE   network plug-in loaded: " << (loaded ? "yes" : "no")
                                              << ", fake printer: " << (fake.empty() ? "off" : fake) << '\n';
                                    check(!loaded && fake.empty(), "no_plugin_run_has_neither_the_plugin_nor_the_fake");
                                }});

        // From the add flow: the model offers to connect, the person says yes.
        live_open(Mode::Add);
        live_say("I have a Bambu Lab A1 mini", [this] {
            live_print_calls();
            for (const Agent::ToolActivity* call : live_calls("printer_add"))
                if (call->state == Agent::ToolState::Succeeded)
                    m_live_added.push_back(nlohmann::json::parse(call->result_json)["printer"].value("name", ""));
            check(m_live_added.size() == 1, "no_plugin_the_a1_mini_is_added");
        });
        // What must reach the person is the app's to show, not the model's to
        // remember: the notice, once, and no card asking for a code nothing
        // can use. The model's words are printed for reading.
        const auto app_says_the_plugin_is_needed = [this](const std::string& name) {
            live_print_calls();
            check(live_plugin_notices() == 1, name + "_draws_one_plugin_notice");
            check(!live_credential_requested(), name + "_opens_no_credential_form");
            check(live_connect_refusals_name_the_plugin(), name + "_a_connect_attempt_is_refused_for_the_plugin");
            live_capture_themed(name);
        };
        live_say("Connect it", [this, app_says_the_plugin_is_needed] {
            check(live_status_said_unavailable(), "no_plugin_add_flow_status_reports_unavailable");
            app_says_the_plugin_is_needed("no_plugin_add_flow");
        });
        live_say("where do I get it?", [app_says_the_plugin_is_needed] { app_says_the_plugin_is_needed("no_plugin_where_to_get_it"); });
        live_say("I'd rather not install anything right now. Leave it.", [this] { live_print_calls(); });
        live_close("no_plugin_add_flow");

        // From Home's Connect…: the conversation opens on the saved printer.
        m_live_steps.push_back({"open: Connect… on the added printer",
                                [this] {
                                    live_show(Mode::Connect, m_live_added.empty() ? std::string() : m_live_added.front());
                                    m_live_printed = 0;
                                },
                                // Until the page's opening is posted, too.
                                [this] {
                                    return live_panel()->host() != nullptr && live_panel()->host()->handshake_complete() &&
                                           live_panel()->instructions_ready() && !live_messages().empty();
                                },
                                [this] {
                                    check(!m_live_added.empty(), "no_plugin_connect_has_the_added_printer");
                                    // Before the model says anything: the page's opening and the
                                    // app's notice under it.
                                    live_print_new();
                                    const auto messages = live_messages();
                                    check(!messages.empty() && messages.front().text.rfind("To connect ", 0) == 0 &&
                                              mentions_plugin(messages.front().text) && !mentions(messages.front().text, {"lan mode"}),
                                          "no_plugin_connect_opens_on_the_plugin_not_lan_mode");
                                    check(live_plugin_notices() == 1, "no_plugin_connect_opens_with_the_notice");
                                    live_capture_themed("no_plugin_connect_opening");
                                }});
        live_say("Connect it", [app_says_the_plugin_is_needed] { app_says_the_plugin_is_needed("no_plugin_connect_flow"); });
        // Someone who is sure the printer's side is right.
        live_say("LAN mode is definitely on, I just checked on the printer",
                 [app_says_the_plugin_is_needed] { app_says_the_plugin_is_needed("no_plugin_lan_mode_on"); });
        // A serial number the app cannot see without the plug-in.
        live_say("Its serial number is 0309DA123456789. Just connect to that one.",
                 [app_says_the_plugin_is_needed] { app_says_the_plugin_is_needed("no_plugin_a_serial"); });
        live_say("Leave it for now", [] {});
        live_close("no_plugin_connect_flow");
    }

    void finish_printer_live()
    {
        live_panel()->close();
        wxYield();
        if (!m_live_printer.empty())
            Printers::remove_named_printer(*m_plater, m_live_printer);
        for (const std::string& added : m_live_added)
            Printers::remove_named_printer(*m_plater, added);
        if (!m_live_host_printer.empty())
            Printers::remove_named_printer(*m_plater, m_live_host_printer);
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        finish();
    }

    void verify_setup_install_commands()
    {
        const std::string before = selected_printer();
        const auto vendors_before = wxGetApp().app_config->vendors();
        std::string error;
        const bool missing_variant = SetupCommands::install_and_select_printer(
            *m_plater, "Creality", "Creality Ender-3 V2", "9.9", {}, error);
        check(!missing_variant && !error.empty(), "setup_install_failure_reported");
        check(selected_printer() == before, "setup_install_failure_keeps_previous_printer");
        check(wxGetApp().app_config->vendors() == vendors_before, "setup_install_failure_restores_enabled_printers");

        error.clear();
        const bool enabled = SetupCommands::install_and_select_printer(
            *m_plater, "BBL", "Bambu Lab X1 Carbon", "0.4", {}, error);
        check(enabled && error.empty() && selected_printer() == kSetupFixturePrinter,
              "setup_install_selects_an_already_enabled_profile");
        verify_named_printers();
    }

    static Preset* printer_profile(const std::string& name)
    {
        return wxGetApp().preset_bundle->printers.find_preset(name, false, true);
    }

    struct HostOutcome
    {
        std::string               state;
        int                       ticks{0};
        std::chrono::milliseconds took{0};
    };

    // Connects `name` to a Moonraker host and waits up to `limit` for the
    // outcome, counting a 100 ms timer's ticks meanwhile: a test run on the
    // main thread stops the app, and the timer with it.
    static HostOutcome connect_host_and_wait(PrinterSetup::OrcaPrinterBackend& backend, const std::string& name,
                                             const std::string& address, std::chrono::seconds limit)
    {
        HostOutcome outcome;
        wxTimer     timer;
        timer.Bind(wxEVT_TIMER, [&outcome](wxTimerEvent&) { ++outcome.ticks; });
        timer.Start(100);
        const auto        start   = std::chrono::steady_clock::now();
        const std::string problem = backend.connect_host(name, "moonraker", address, "");
        outcome.state             = problem.empty() ? backend.connection(name).state : "refused: " + problem;
        while (outcome.state == "connecting" && std::chrono::steady_clock::now() - start < limit) {
            wxYield();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            outcome.state = backend.connection(name).state;
        }
        timer.Stop();
        outcome.took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        std::cout << "HARNESS host connect to " << address << ": " << outcome.state << " after " << outcome.took.count()
                  << " ms, " << outcome.ticks << " timer ticks" << std::endl;
        return outcome;
    }

    // Waits, yielding to the app, until `done` or `limit` passes.
    static bool wait_for(const std::function<bool()>& done, std::chrono::milliseconds limit)
    {
        const auto until = std::chrono::steady_clock::now() + limit;
        while (!done()) {
            if (std::chrono::steady_clock::now() > until)
                return false;
            wxYield();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    // What Home's printer rail shows for the named printer `name`, read through
    // the real backend.
    std::optional<Home::PrinterEntry> home_card(const std::string& name) const
    {
        Home::OrcaHomeBackend home(*m_frame);
        for (Home::PrinterEntry& card : home.printers())
            if (card.id == "named:" + name)
                return card;
        return std::nullopt;
    }

    // The printer windows open now: Home's own printer windows are the only
    // top-level frames that hold a PrinterWebView.
    static std::vector<Home::PrinterWindow*> printer_windows()
    {
        std::vector<Home::PrinterWindow*> windows;
        for (wxWindow* window : wxTopLevelWindows)
            if (auto* printer = dynamic_cast<Home::PrinterWindow*>(window); printer && !printer->IsBeingDeleted())
                windows.push_back(printer);
        return windows;
    }

    // "Open printer window" on a print host's card: a window of its own,
    // loaded from that printer's saved address, while the project's selected
    // printer and the page the person is on stay as they were.
    void verify_open_printer_window(const StandInHost& host)
    {
        using namespace std::chrono_literals;
        const std::string selected_before = selected_printer();
        const int         tab_before      = m_notebook->GetSelection();
        check(selected_before != kAddedPrinter && printer_windows().empty(), "printer_window_fixture_starts_on_another_printer");
        {
            Home::OrcaHomeBackend home(*m_frame);
            home.launch_monitor(std::string("named:") + kAddedPrinter);
            const auto opened = printer_windows();
            check(opened.size() == 1 && opened.front()->GetTitle() == wxString::FromUTF8(kAddedPrinter) &&
                      opened.front()->IsShown(),
                  "printer_window_opens_for_the_printer");
            check(selected_printer() == selected_before && m_notebook->GetSelection() == tab_before,
                  "printer_window_leaves_the_project_printer_and_page");
            // Its browser is asked for the saved address.
            wxWebView* browser = nullptr;
            if (opened.size() == 1)
                for (wxWindow* child : opened.front()->view()->GetChildren())
                    if (auto* found = dynamic_cast<wxWebView*>(child))
                        browser = found;
            const std::string address = host.address();
            check(browser != nullptr && wait_for([browser, &address] {
                      return browser->GetCurrentURL().ToStdString().rfind(address, 0) == 0;
                  }, 20s),
                  "printer_window_loads_the_saved_address");
            home.launch_monitor(std::string("named:") + kAddedPrinter);
            check(printer_windows() == opened, "printer_window_second_click_reuses_the_window");
        }
        // Home's backend closes its windows when it goes, as the app's main
        // window taking Home with it does.
        check(wait_for([] { return printer_windows().empty(); }, 5s), "printer_window_closes_with_home");

        // Closed from its title bar before its page has installed its script
        // handler, as a quick click can: WebKit must not answer a view that is
        // gone (on macOS that crashed the app once the answer came), and the
        // next click opens a new window rather than raising the closing one.
        {
            Home::OrcaHomeBackend home(*m_frame);
            home.launch_monitor(std::string("named:") + kAddedPrinter);
            const auto first = printer_windows();
            if (first.size() == 1)
                first.front()->Close();
            home.launch_monitor(std::string("named:") + kAddedPrinter);
            // A closed window waits, hidden, while any web view is installing
            // its script handler; the person sees only the new one.
            std::vector<Home::PrinterWindow*> open;
            for (Home::PrinterWindow* window : printer_windows())
                if (!window->closing())
                    open.push_back(window);
            check(first.size() == 1 && first.front()->closing() && open.size() == 1 && open.front() != first.front() &&
                      open.front()->IsShown(),
                  "printer_window_reopens_after_a_quick_close");
        }
        check(wait_for([] { return printer_windows().empty(); }, 5s), "printer_window_quick_close_closes");
        wait_for([] { return false; }, 3000ms);
        check(true, "printer_window_quick_close_survives_the_late_answer");
    }

    // Testing a print host takes as long as the host takes to answer, and
    // the app goes on meanwhile: a host that holds the request for three
    // seconds leaves a 100 ms timer ticking the whole time. The address is
    // saved only once the host has answered, and an answer nobody waits for
    // any more is dropped.
    void verify_host_connection_keeps_the_app_responsive(PrinterSetup::OrcaPrinterBackend& backend)
    {
        using namespace std::chrono_literals;
        const auto saved_address = [] { return printer_profile(kAddedPrinter)->config.opt_string("print_host"); };
        const std::string address_before = saved_address();
        {
            StandInHost       slow(3000ms, /*answer=*/false);
            const HostOutcome outcome = connect_host_and_wait(backend, kAddedPrinter, slow.address(), 20s);
            check(outcome.state == "failed" && outcome.took >= 2500ms, "host_slow_failure_is_reported");
            check(outcome.ticks >= 10, "host_test_leaves_the_app_responsive");
            check(saved_address() == address_before, "host_failure_leaves_the_profile_as_it_was");
            // Finding 04 of the setup walkthrough: Home said "Sends files over
            // Wi-Fi" right after this. The card now says only what is verified.
            const auto card = home_card(kAddedPrinter);
            check(address_before.empty() && card && card->connection_state == Home::ConnectionState::None &&
                      card->connection_text == "Not connected" && card->address.empty() && !card->can_launch_monitor,
                  "home_says_not_connected_after_a_failed_host_test");
            check(card && card->model_text == "Ender-3 V2 Neo \xC2\xB7 0.4 mm", "home_model_row_names_model_and_nozzle");
            // The project has a filament selected, and adding the printer
            // selected it: neither is what the machine holds. A printer that
            // reports nothing gets no Loaded row.
            check(card && card->spools.empty(), "home_claims_nothing_loaded_on_a_printer_that_reports_nothing");
        }
        std::string answered;
        {
            StandInHost       host(0ms, /*answer=*/true);
            const HostOutcome outcome = connect_host_and_wait(backend, kAddedPrinter, host.address(), 20s);
            answered = host.address();
            check(outcome.state == "verified" && host.requests() == 1, "host_that_answers_is_verified");
            check(saved_address() == answered &&
                      printer_profile(kAddedPrinter)->config.opt_enum<PrintHostType>("host_type") == htMoonraker,
                  "host_success_saves_the_address");
            const std::string address = answered.substr(answered.find("://") + 3);
            const auto        card    = home_card(kAddedPrinter);
            check(card && card->connection_state == Home::ConnectionState::Connected && card->connection_kind == "host" &&
                      card->address == address && card->connection_text == "Connected \xC2\xB7 " + address &&
                      card->can_launch_monitor && card->connection_action.empty(),
                  "home_says_connected_with_the_saved_address");
            verify_open_printer_window(host);
        }
        {
            // Would answer in two seconds; cancelled first.
            StandInHost late(2000ms, /*answer=*/true);
            check(backend.connect_host(kAddedPrinter, "moonraker", late.address(), "").empty() &&
                      backend.connection(kAddedPrinter).state == "connecting",
                  "host_attempt_starts_waiting");
            backend.cancel_connection(kAddedPrinter);
            wait_for([] { return false; }, 3000ms);
            check(backend.connection(kAddedPrinter).state != "verified" && saved_address() == answered,
                  "host_cancel_drops_a_late_answer");
        }
        {
            // Replaced by a second attempt, to a closed port, before it answers.
            StandInHost late(2000ms, /*answer=*/true);
            check(backend.connect_host(kAddedPrinter, "moonraker", late.address(), "").empty(), "host_first_attempt_starts");
            const HostOutcome second = connect_host_and_wait(backend, kAddedPrinter, "http://127.0.0.1:1", 20s);
            wait_for([] { return false; }, 3000ms);
            check(second.state == "failed" && backend.connection(kAddedPrinter).state == "failed" && saved_address() == answered,
                  "host_replaced_attempt_answer_is_dropped");
        }
        {
            // Accepts and never answers: failed at the wait, not never.
            StandInHost       silent(-1ms, /*answer=*/false);
            const HostOutcome outcome = connect_host_and_wait(backend, kAddedPrinter, silent.address(), 45s);
            check(outcome.state == "failed" && outcome.took >= PrinterSetup::kConnectionWait &&
                      outcome.took < PrinterSetup::kConnectionWait + 5s,
                  "host_that_never_answers_fails_at_the_wait");
            check(backend.connection(kAddedPrinter).message.find("did not respond") != std::string::npos,
                  "host_silence_is_reported_as_no_response");
            check(saved_address() == answered, "host_silence_leaves_the_profile");
        }
        verify_panel_close_drops_a_waiting_connection(answered);
    }

    // "PLA #5F7D4F, PETG" for a card's Loaded row, to compare and to print.
    static std::string loaded_text(const std::vector<Home::SpoolEntry>& spools)
    {
        std::string text;
        for (const Home::SpoolEntry& spool : spools)
            text += (text.empty() ? "" : ", ") + spool.material + (spool.colour.empty() ? "" : " " + spool.colour);
        return text;
    }

    // A Klipper printer's multi-filament unit, as its Moonraker host reports
    // it, reaches Home's card and the printer conversation; a printer without
    // one shows nothing loaded; a host that stops answering goes Offline.
    // Read through the real backends against a stand-in Moonraker.
    void verify_host_lanes(PrinterSetup::OrcaPrinterBackend& backend)
    {
        using namespace std::chrono_literals;
        const auto card = [&]() -> std::optional<Home::PrinterEntry> {
            Home::OrcaHomeBackend home(*m_frame);
            for (Home::PrinterEntry& entry : home.printers())
                if (entry.id == std::string("named:") + kAddedPrinter)
                    return entry;
            return std::nullopt;
        };
        const auto loaded_now = [&] {
            const auto read = card();
            return read ? loaded_text(read->spools) : std::string("(no card)");
        };
        const auto saved_now = [&]() -> PrinterSetup::SavedPrinter {
            for (const PrinterSetup::SavedPrinter& saved : backend.saved_printers())
                if (saved.name == kAddedPrinter)
                    return saved;
            return {};
        };
        // A poll is due every ten seconds, so a change shows within one.
        const auto shows = [&](const std::string& expected, const std::string& name) {
            const bool shown = wait_for([&] { return loaded_now() == expected; }, 25s);
            std::cout << "HARNESS host lanes " << name << ": \"" << loaded_now() << "\"" << std::endl;
            check(shown, name);
        };

        std::string address;
        {
            StandInMoonraker host;
            host.set_lane_data(R"({"result":{"namespace":"lane_data","value":{
                "lane1":{"color":"#5F7D4F","material":"PLA","lane":"0","spool_id":null},
                "lane2":{"color":"","material":"PETG","lane":"1","spool_id":null},
                "lane3":{"color":"","material":"","lane":"2","spool_id":null}}}})");
            address = host.address();
            check(connect_host_and_wait(backend, kAddedPrinter, address, 20s).state == "verified", "host_lanes_fixture_connects");
            const auto first = card();
            check(first && first->connection_state == Home::ConnectionState::Connected && first->can_launch_monitor,
                  "host_lanes_connection_test_counts_as_an_answer");

            shows("PLA #5F7D4F, PETG", "host_lanes_home_shows_the_loaded_lanes");
            const PrinterSetup::SavedPrinter saved = saved_now();
            check(saved.connected && saved.spools.size() == 2 && saved.spools[0].material == "PLA" &&
                      saved.spools[0].colour == "#5F7D4F" && saved.spools[1].material == "PETG" && saved.spools[1].colour.empty(),
                  "host_lanes_conversation_reads_the_loaded_lanes");

            host.set_lane_data(R"({"result":{"namespace":"lane_data","value":{
                "lane1":{"color":"#5F7D4F","material":"PLA","lane":"0"},
                "lane2":{"color":"","material":"","lane":"1"},
                "lane3":{"color":"0xAA3322","material":"ASA","lane":"2"}}}})");
            shows("PLA #5F7D4F, ASA #AA3322", "host_lanes_home_follows_a_spool_change");

            // Happy Hare without the lane_data namespace.
            host.set_lane_data("");
            host.set_mmu(R"({"num_gates":4,"gate_status":[1,0,2,-1],"gate_material":["PETG","PLA","TPU","ABS"],
                             "gate_color":["ff8800","","112233",""],"gate_temperature":[235,210,220,240]})");
            shows("PETG #FF8800, TPU #112233", "host_lanes_home_reads_happy_hare");

            // No unit at all: the printer says nothing is loaded, and it is
            // still connected.
            host.set_mmu("");
            shows("", "host_lanes_printer_without_a_unit_shows_nothing");
            const auto plain = card();
            check(plain && plain->connection_state == Home::ConnectionState::Connected, "host_lanes_printer_without_a_unit_is_connected");
            check(saved_now().connected && saved_now().spools.empty(), "host_lanes_conversation_reads_no_unit_as_nothing");
            check(host.requests("/server/database/item") >= 4 && host.requests("/printer/objects/query") >= 2,
                  "host_lanes_both_sources_are_read");
        }

        // The host stops answering: Offline once its last answer is stale,
        // with Reconnect, and no longer said to hold anything.
        const bool offline = wait_for([&] {
            const auto read = card();
            return read && read->connection_state == Home::ConnectionState::Offline;
        }, PrinterSetup::kConnectionWait + 20s);
        const auto gone = card();
        std::cout << "HARNESS host lanes silent: " << (gone ? gone->connection_text : "(no card)") << ", \""
                  << (gone ? loaded_text(gone->spools) : "") << "\"" << std::endl;
        check(offline && gone->connection_text == "Offline" && gone->connection_action == "reconnect" &&
                  !gone->can_launch_monitor && gone->address == address.substr(address.find("://") + 3),
              "host_lanes_silent_host_goes_offline");
        check(gone && gone->spools.empty(), "host_lanes_silent_host_claims_nothing_loaded");
        check(!saved_now().connected && saved_now().spools.empty(), "host_lanes_conversation_sees_the_silent_host_disconnected");
    }

    // Opens the panel on `printer` with a model that answers the first
    // message by calling `tool`, sends that message, and returns the call.
    const Agent::ToolActivity* call_in_panel(const std::string& printer, const std::string& tool, const nlohmann::json& arguments,
                                             const std::string& name)
    {
        using namespace std::chrono_literals;
        PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
        panel->open(PrinterSetup::ConversationMode::Connect, printer);
        const bool loaded = wait_for([panel] {
            return panel->host() != nullptr && panel->host()->handshake_complete() && panel->instructions_ready();
        }, 60s);
        check(loaded, name + "_page_loaded");
        if (!loaded)
            return nullptr;
        Agent::AgentHost* host = panel->host();
        host->set_agent(std::make_unique<ToolCallingAgent>(tool, arguments), Agent::AgentAvailability::Ready);
        static int next = 0;
        host->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                             {"version", Agent::Protocol::kVersion},
                                             {"id", "call-" + std::to_string(++next)},
                                             {"type", "user_message"},
                                             {"payload", {{"clientMessageId", "call-c-" + std::to_string(next)}, {"text", "go on"}}}}
                                  .dump());
        const auto call = [host, tool]() -> const Agent::ToolActivity* {
            for (const auto& activity : host->tools().activities())
                if (activity.tool == tool)
                    return &activity;
            return nullptr;
        };
        const bool made = wait_for([&] {
            const auto* activity = call();
            return activity != nullptr && activity->state != Agent::ToolState::Running;
        }, 10s);
        check(made, name + "_tool_called");
        return made ? call() : nullptr;
    }

    // The panel closed while its printer was still being checked, and the
    // host answers afterwards: nothing is saved, then or when the panel next
    // looks at the connection.
    void verify_panel_close_drops_a_waiting_connection(const std::string& saved)
    {
        using namespace std::chrono_literals;
        PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
        StandInHost                 late(2000ms, /*answer=*/true);
        const Agent::ToolActivity*  card = call_in_panel(kAddedPrinter, "printer_connect",
                                                         nlohmann::json{{"hostType", "moonraker"}, {"address", late.address()}},
                                                         "panel_close_fixture");
        const auto request = panel->session_json().value("credentialRequest", nlohmann::json::object());
        check(card != nullptr && request.value("actionId", "") == card->action_id,
              "panel_close_fixture_credential_requested");
        if (card == nullptr)
            return;
        const std::string action = card->action_id;
        panel->host()->on_page_message(nlohmann::json{{"protocol", Agent::Protocol::kName},
                                                      {"version", Agent::Protocol::kVersion},
                                                      {"id", "close-decision"},
                                                      {"type", "printer_action"},
                                                      {"payload", {{"action", "connect"}, {"actionId", action}, {"credential", ""}}}}
                                           .dump());
        check(wait_for([&] { return late.requests() == 1; }, 10s), "panel_close_fixture_is_connecting");
        panel->close();
        wait_for([] { return false; }, 3000ms);
        check(!panel->IsShown() && late.requests() == 1, "panel_close_mid_test_closes");

        // Opened again: the status the model reads does not bring the old
        // answer back.
        const Agent::ToolActivity* status = call_in_panel(kAddedPrinter, "printer_connection_status", nlohmann::json::object(),
                                                          "panel_reopen");
        check(status != nullptr && status->state == Agent::ToolState::Succeeded &&
                  nlohmann::json::parse(status->result_json).value("state", "") != "verified",
              "panel_reopen_does_not_read_the_old_answer");
        check(printer_profile(kAddedPrinter)->config.opt_string("print_host") == saved, "panel_close_mid_test_saves_nothing");
        panel->close();
        wait_for([panel] { return !panel->IsShown(); }, 5s);
    }

    // A printer is a named user profile: a second one of a model is a second
    // printer, Home lists each, and renaming or removing one leaves the rest.
    void verify_named_printers()
    {
        const auto catalog = PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir());
        const auto neo = std::find_if(catalog.candidates().begin(), catalog.candidates().end(), [](const auto& candidate) {
            return candidate.model_id == "Creality Ender-3 V2 Neo" && candidate.variant == "0.4";
        });
        check(neo != catalog.candidates().end(), "named_catalog_has_the_neo");
        if (neo == catalog.candidates().end()) {
            fail("the catalogue has no Ender-3 V2 Neo 0.4");
            return;
        }

        // What "Add this printer" does, through the one route the panel uses.
        PrinterSetup::OrcaPrinterBackend backend(*m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()));
        PrinterSetup::AddPrinterRequest request;
        request.vendor_id = neo->vendor_id;
        request.model_id  = neo->model_id;
        request.variant   = neo->variant;
        request.material  = neo->default_material;
        request.name      = neo->model_name;
        PrinterSetup::SavedPrinter saved;
        std::string error = backend.add_printer(request, saved);

        const Preset* first = printer_profile(kAddedPrinter);
        check(error.empty() && first != nullptr && first->is_user() && first->inherits() == kAddedPrinterProfile,
              "named_add_saved_a_user_profile_on_the_system_one");

        // A second printer of the same model is a second printer.
        error = backend.add_printer(request, saved);
        const bool added = error.empty();
        const std::string second = std::string(kAddedPrinter) + " (2)";
        check(added && error.empty() && selected_printer() == second, "named_second_of_a_model_is_a_second_printer");
        check(printer_profile(kAddedPrinter) != nullptr, "named_second_add_keeps_the_first");

        Home::OrcaHomeBackend home(*m_frame);
        const auto cards = home.printers();
        const auto card = [&](const std::string& name) -> const Home::PrinterEntry* {
            const auto found = std::find_if(cards.begin(), cards.end(),
                                            [&](const Home::PrinterEntry& entry) { return entry.id == "named:" + name; });
            return found == cards.end() ? nullptr : &*found;
        };
        const auto* listed = card(second);
        check(card(kAddedPrinter) != nullptr && listed != nullptr && listed->name == second,
              "named_home_lists_both_printers_by_name");
        check(listed != nullptr && listed->kind == Home::PrinterKind::Named && listed->can_open_settings &&
                  listed->can_rename && listed->can_remove,
              "named_home_offers_the_printer_menu");

        verify_host_connection_keeps_the_app_responsive(backend);
        verify_host_lanes(backend);

        // Configure an unselected saved printer through the real adapter. A
        // closed loopback port exercises an actual provider failure without
        // contacting hardware or sending any file.
        const bool        dirty_before_connection = m_plater->is_project_dirty();
        const std::string address_before          = printer_profile(kAddedPrinter)->config.opt_string("print_host");
        check(connect_host_and_wait(backend, kAddedPrinter, "http://127.0.0.1:1", std::chrono::seconds(20)).state == "failed",
              "named_host_test_failure_is_a_connection_outcome");
        check(printer_profile(kAddedPrinter) != nullptr && selected_printer() == second &&
                  m_plater->is_project_dirty() == dirty_before_connection,
              "named_connect_preserves_saved_printer_and_unrelated_project");
        check(printer_profile(kAddedPrinter)->config.opt_string("print_host") == address_before,
              "named_host_failure_keeps_the_saved_address");
        auto& printer_presets = wxGetApp().preset_bundle->printers;
        const bool dirty_on_selected = m_plater->is_project_dirty();
        check(Printers::configure_named_printer_host(second, htOctoPrint, "http://127.0.0.1:1", "").empty() &&
                  printer_presets.get_edited_preset().config.opt_string("print_host") == "http://127.0.0.1:1" &&
                  !printer_presets.current_is_dirty() && m_plater->is_project_dirty() == dirty_on_selected,
              "named_host_settings_update_selected_printer_without_changing_project_edits");
        printer_presets.get_edited_preset().config.set_key_value("printer_notes", new ConfigOptionString("unsaved"));
        check(!Printers::configure_named_printer_host(second, htMoonraker, "http://127.0.0.1:2", "").empty() &&
                  printer_presets.get_edited_preset().config.opt_string("printer_notes") == "unsaved" &&
                  printer_profile(second)->config.opt_string("print_host") == "http://127.0.0.1:1",
              "named_connect_refuses_to_overwrite_unsaved_printer_edits");
        printer_presets.discard_current_changes();
        check(Printers::link_named_printer(kAddedPrinter, "harness-device").empty() &&
                  !Printers::link_named_printer(second, "harness-device").empty(),
              "named_device_link_refuses_a_second_owner");

        check(!Printers::rename_named_printer(*m_plater, second, kAddedPrinter).empty() &&
                  selected_printer() == second,
              "named_rename_refuses_a_taken_name");
        check(!Printers::rename_named_printer(*m_plater, second, "Shed/Neo").empty(),
              "named_rename_refuses_illegal_characters");
        check(Printers::rename_named_printer(*m_plater, second, "Shed Neo").empty() &&
                  selected_printer() == "Shed Neo" && printer_profile(second) == nullptr,
              "named_rename_of_the_selected_printer");
        check(Printers::rename_named_printer(*m_plater, kAddedPrinter, "Garage Neo").empty() &&
                  selected_printer() == "Shed Neo" && printer_profile(kAddedPrinter) == nullptr,
              "named_rename_of_another_printer_keeps_the_selection");
        const Preset* garage = printer_profile("Garage Neo");
        check(garage != nullptr && garage->inherits() == kAddedPrinterProfile, "named_rename_keeps_the_parent");
        const auto renamed_printers = Printers::named_printers();
        check(std::any_of(renamed_printers.begin(), renamed_printers.end(), [](const auto& printer) {
                  return printer.name == "Garage Neo" && printer.device_id == "harness-device";
              }), "named_rename_keeps_the_device_association");
        check(Printers::remove_named_printer(*m_plater, "Garage Neo").empty() &&
                  printer_profile("Garage Neo") == nullptr && selected_printer() == "Shed Neo",
              "named_remove_of_another_printer_keeps_the_selection");
        check(Printers::remove_named_printer(*m_plater, "Shed Neo").empty() &&
                  printer_profile("Shed Neo") == nullptr && selected_printer() == kAddedPrinterProfile,
              "named_remove_of_the_selected_printer_selects_its_parent");
        verify_named_header();
    }

    // The header names the selected printer by its own name.
    void verify_named_header()
    {
        const std::string name = Printers::add_named_printer(*m_plater, "Lab Printer", {});
        check(SetupCommands::current_printer().nickname == name, "named_header_shows_the_printers_name");
        check(Printers::remove_named_printer(*m_plater, name).empty(), "named_header_cleanup_removes_the_printer");
        verify_named_wizard_install();
    }

    // "Set it up myself" runs Orca's wizard, which only enables models. What
    // it newly enabled becomes a printer, as a recognized model does; the
    // wizard itself is a web page this harness cannot drive, so the enabling
    // is done the way the wizard's apply does it.
    void verify_named_wizard_install()
    {
        const auto before  = wxGetApp().app_config->vendors();
        const auto custom  = before.find("Custom");
        check(custom == before.end() || custom->second.count("Generic Klipper Printer") == 0 ||
                  custom->second.at("Generic Klipper Printer").empty(),
              "named_wizard_klipper_not_enabled_before");
        std::string error;
        check(SetupCommands::install_and_select_printer(*m_plater, "Custom", "Generic Klipper Printer", "0.4", {}, error),
              "named_wizard_enables_klipper");
        Printers::name_installed_printers(*m_plater, before);
        const Preset* klipper = printer_profile("Generic Klipper Printer");
        check(selected_printer() == "Generic Klipper Printer" && klipper != nullptr && klipper->is_user() &&
                  klipper->inherits() == "MyKlipper 0.4 nozzle",
              "named_wizard_install_becomes_a_printer");
        Printers::name_installed_printers(*m_plater, wxGetApp().app_config->vendors());
        check(printer_profile("Generic Klipper Printer (2)") == nullptr, "named_wizard_names_only_new_models");
        check(Printers::remove_named_printer(*m_plater, "Generic Klipper Printer").empty(),
              "named_wizard_cleanup_removes_the_printer");
        SetupCommands::select_printer_preset(*m_plater, kSetupFixturePrinter);
        check(selected_printer() == kSetupFixturePrinter, "named_cleanup_restores_the_fixture_printer");
        verify_saved_bambu_connection();
    }

    // Home's card for a connected Bambu printer: Online on its transport, and
    // what its trays hold, with a swatch only where a tray reports a colour.
    void verify_home_bambu_card(const std::string& name, std::function<void()> then)
    {
        const auto card = home_card(name);
        check(card && card->connection_state == Home::ConnectionState::Online && card->connection_kind == "lan" &&
                  card->connection_text == "Online \xC2\xB7 LAN" && card->can_launch_monitor &&
                  card->connection_action.empty(),
              "home_says_online_for_a_connected_bambu_printer");
        check(card && card->model_text.rfind("A1 mini \xC2\xB7 ", 0) == 0, "home_model_row_names_the_bambu_model");
        const auto fake = std::dynamic_pointer_cast<FakeBambuAgent>(wxGetApp().getAgent()->get_printer_agent());
        check(fake != nullptr, "home_trays_fixture_drives_the_fake");
        if (fake == nullptr) {
            then();
            return;
        }
        const std::string control = fake->control_file_path();
        boost::filesystem::create_directories(boost::filesystem::path(control).parent_path());
        {
            boost::nowide::ofstream out(control);
            out << R"({"spools":[{"subBrands":"PLA Matte","trayType":"PLA","colour":"5F7D4FFF"},)"
                   R"({"subBrands":"PETG HF","trayType":"PETG"}]})";
        }
        wait_until(
            [self = shared_from_this(), name] {
                const auto read = self->home_card(name);
                return read && read->spools.size() == 2;
            },
            "home_reads_the_connected_printers_trays",
            [self = shared_from_this(), name, control, then] {
                const auto read = self->home_card(name);
                if (read)
                    for (const Home::SpoolEntry& spool : read->spools)
                        std::cerr << "HARNESS NOTE home_tray material=" << spool.material << " colour=" << spool.colour << '\n';
                self->check(read && read->spools.size() == 2 && read->spools[0].material == "PLA" &&
                                read->spools[0].colour == "#5F7D4F",
                            "home_names_a_trays_material_and_colour");
                self->check(read && read->spools.size() == 2 && read->spools[1].material == "PETG" &&
                                read->spools[1].colour.empty(),
                            "home_draws_no_colour_a_tray_did_not_report");
                boost::filesystem::remove(control);
                then();
            });
    }

    void verify_saved_bambu_connection()
    {
        // Exercise the actual adapter and parsed device observations with the
        // in-process fake. This proves state ownership, not network authentication.
        wxGetApp().app_config->set("jusprin", "fake_printer", "true");
        auto backend = std::make_shared<PrinterSetup::OrcaPrinterBackend>(
            *m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()));
        PrinterSetup::AddPrinterRequest request;
        request.vendor_id = "BBL";
        request.model_id = "Bambu Lab A1 mini";
        request.variant = "0.6";
        request.name = "Connection fixture";
        PrinterSetup::SavedPrinter saved;
        check(backend->add_printer(request, saved).empty(), "connect_fixture_saved_without_device");
        SetupCommands::select_printer_preset(*m_plater, kAddedPrinterProfile);
        check(selected_printer() == kAddedPrinterProfile && !wxGetApp().preset_bundle->is_bbl_vendor(),
            "connection_test_uses_a_non_bambu_slicing_profile");
        const bool dirty = m_plater->is_project_dirty();
        backend->prepare_connection(saved.name);
        wait_until([backend, name = saved.name] { return !backend->connection(name).candidates.empty(); },
            "connect_fake_discovered_by_real_adapter", [self = shared_from_this(), backend, name = saved.name, dirty] {
                const auto info = backend->connection(name);
                const std::string device = info.candidates.front().id;
                self->check(info.state == "not_configured", "unlinked_online_device_is_not_a_connected_saved_printer");
                wxGetApp().getDeviceManager()->get_my_machine(device)->reset(); // Ignore the fake's unsolicited heartbeat.
                self->check(Printers::link_named_printer(name, device).empty(), "discovered_device_linked_before_authentication");
                self->check(backend->connection(name).state == "not_configured", "identified_device_has_neutral_first_connection_state");
                const auto linked = self->home_card(name);
                self->check(linked && linked->connection_state == Home::ConnectionState::None &&
                    linked->connection_text == "Not connected" && !linked->can_launch_monitor && linked->connection_action.empty(),
                    "home_says_not_connected_for_a_linked_but_unverified_device");
                self->check(backend->connect_printer(name, device, "").empty(), "connect_existing_saved_bambu");
                self->check(backend->connection(name).state == "connecting", "connect_waits_for_fresh_device_data");
                self->check(backend->connect_printer(name, device, "").empty(), "duplicate_connect_is_idempotent");
                // The person picks another printer later, not in the same event
                // turn: a connect re-selects an already-selected printer on the next.
                wxYield();
                wxGetApp().getDeviceManager()->set_selected_machine("");
                const auto interrupted = backend->connection(name);
                self->check(interrupted.state == "failed" && interrupted.message.find("The selected printer changed") != std::string::npos &&
                    interrupted.message.find("access code") == std::string::npos, "selection_interruption_does_not_blame_credentials");
                // Separate gestures by a GUI event turn, allowing upstream disconnect
                // notifications to finish before the next user-requested connection.
                wxGetApp().CallAfter([self, backend, name, device, dirty] {
                self->check(backend->connect_printer(name, device, "").empty(), "interrupted_connection_can_retry");
                self->wait_until([backend, name] { return backend->connection(name).state == "verified"; },
                    "connect_verified_from_parsed_fake_data", [self, backend, name, device, dirty] {
                        self->verify_home_bambu_card(name, [self, backend, name, device, dirty] {
                        self->check(backend->connection(name).nozzle_mismatch, "reported_nozzle_mismatch_does_not_block_connection");
                        self->check(wxGetApp().app_config->get("jusprin_verified_connections", device) == "true",
                            "verified_connection_history_is_recorded_for_this_device");
                        const auto printers = backend->saved_printers();
                        self->check(std::count_if(printers.begin(), printers.end(), [&](const auto& p) {
                            return p.name == name && p.device_id == device;
                        }) == 1, "connect_keeps_one_saved_identity");
                        self->check(self->selected_printer() == kAddedPrinterProfile &&
                            self->m_plater->is_project_dirty() == dirty, "connect_preserves_unrelated_project_selection_and_edits");
                        // Profile changes go through Orca's own preset selection, which
                        // re-evaluates the printer agent. The printer's own Bambu profile
                        // keeps the connection; a non-Bambu profile hands the agent back
                        // to Orca's profile-driven choice, as stock Orca does.
                        const auto agent_id = [] { return wxGetApp().getAgent()->get_printer_agent()->get_agent_info().id; };
                        const std::string fake = fake_bambu_printer_agent_id(wxGetApp().app_config);
                        self->check(SetupCommands::select_printer_preset(*self->m_plater, name) && agent_id() == fake &&
                            backend->connection(name).state == "verified", "selecting_the_printers_own_profile_keeps_the_connection");
                        self->check(SetupCommands::select_printer_preset(*self->m_plater, kAddedPrinterProfile) && agent_id() != fake,
                            "selecting_a_non_bambu_profile_replaces_connection_provider");
                        const auto replaced = std::chrono::steady_clock::now();
                        std::cerr << "HARNESS NOTE state_after_provider_replaced " << backend->connection(name).state << '\n';
                        self->wait_until([backend, name] { return backend->connection(name).state == "unknown"; },
                            "home_reports_unknown_after_provider_replaced", [self, backend, name, device, replaced, agent_id, fake] {
                        std::cerr << "HARNESS NOTE seconds_until_unknown " << std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - replaced).count() << '\n';
                        backend->prepare_connection(name);
                        self->check(agent_id() == fake, "connecting_again_reinstalls_provider");
                        wxGetApp().CallAfter([self, backend, name, device] {
                        self->check(backend->connect_printer(name, device, "").empty(), "reconnect_after_profile_reevaluation");
                        self->wait_until([backend, name] { return backend->connection(name).state == "verified"; },
                            "connection_receives_fresh_data_after_reconnect", [self, backend, name, device] {
                                wxGetApp().getDeviceManager()->set_selected_machine("");
                                wxGetApp().CallAfter([self, backend, name, device] {
                                // Simulate a transport that authenticates but never supplies status.
                                // The isolated fake otherwise pushes even while disconnected.
                                auto fake = wxGetApp().getAgent()->get_printer_agent();
                                fake->set_on_local_message_fn({});
                                fake->set_on_message_fn({});
                                self->check(backend->connect_printer(name, device, "").empty(), "timeout_fixture_starts_attempt");
                                self->wait_until([backend, name] { return backend->connection(name).state == "failed"; },
                                    "connection_times_out_without_messages", [self, backend, name] {
                                        const auto timeout = backend->connection(name);
                                        self->check(timeout.message.find("did not respond") != std::string::npos &&
                                            timeout.message.find("access code") == std::string::npos,
                                            "timeout_explains_no_response_without_inventing_auth_failure");
                                        PrinterSetup::OrcaPrinterBackend reopened(*self->m_plater,
                                            PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()));
                                        self->check(reopened.connection(name).state == "unknown",
                                            "previously_verified_connection_remains_distinct_from_never_connected");
                                        // Verified before and silent now: Offline, with Reconnect.
                                        const auto silent = self->home_card(name);
                                        self->check(silent && silent->connection_state == Home::ConnectionState::Offline &&
                                            silent->connection_text == "Offline" && silent->connection_action == "reconnect" &&
                                            !silent->can_launch_monitor, "home_says_offline_for_a_silent_verified_printer");
                                        self->verify_other_printers_menu();
                                        self->finish();
                                    });
                                });
                            });
                        });
                        });
                        });
                    });
                });
            });
    }

    // --- Home's printer cards, live while Home stays up (--home-live) -----
    // A fake Bambu printer, saved and connected through the real backend,
    // goes Online, Printing, then Offline while Home is on screen. The
    // harness never leaves Home and never asks it to refresh after the first
    // look, and it reads the cards from the page itself -- what the person
    // sees -- rather than from the backend. A card menu opened at the start
    // must still be open at the end.
    static constexpr const char* kHomeLivePrinter = "Live A1 mini";

    std::shared_ptr<PrinterSetup::OrcaPrinterBackend> m_home_live_backend;
    std::string                                       m_home_live_control;
    // The page's last answer to read_home_page(): {cards:[...]}.
    nlohmann::json m_home_page;
    bool           m_home_page_bound{false};
    unsigned       m_home_page_ticks{0};

    // Asks the Home page what its printer cards show. The answer arrives as a
    // script message; HomeHost ignores it (another protocol) and the handler
    // below keeps it.
    void read_home_page()
    {
        wxWebView* view = installed_shell()->home_view()->webview();
        if (view == nullptr)
            return;
        if (!m_home_page_bound) {
            view->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, [weak = std::weak_ptr<Scenario>(shared_from_this())](wxWebViewEvent& event) {
                event.Skip();
                const auto self = weak.lock();
                if (!self)
                    return;
                const nlohmann::json message = nlohmann::json::parse(event.GetString().ToUTF8().data(), nullptr, false);
                if (message.is_object() && message.value("harness", "") == "home-cards")
                    self->m_home_page = message;
            });
            m_home_page_bound = true;
        }
        WebView::RunScript(view,
            "(function () {"
            "  var cards = Array.prototype.map.call(document.querySelectorAll('.printer-card'), function (card) {"
            "    var name = card.querySelector('.printer-name');"
            "    var bar = card.querySelector('[role=progressbar]');"
            "    return { name: name ? name.textContent : '',"
            "             printing: !!card.querySelector('.printer-job'),"
            "             progress: bar ? Number(bar.getAttribute('aria-valuenow')) : -1,"
            "             details: Array.prototype.map.call(card.querySelectorAll('.printer-fact dd'), function (d) { "
            "return d.textContent; }),"
                                 "             buttons: "
                                 "Array.prototype.map.call(card.querySelectorAll('button:not(.printer-menu-button):not([role=menuitem])'), "
                                 "function (b) { return b.textContent; }),"
            "             menuOpen: !!card.querySelector('.printer-menu') };"
            "  });"
            "  window.wx.postMessage(JSON.stringify({ harness: 'home-cards', cards: cards }));"
            "})()");
    }

    // The live printer's card as the page last drew it, or null.
    nlohmann::json home_page_card() const
    {
        if (m_home_page.contains("cards"))
            for (const nlohmann::json& card : m_home_page["cards"])
                if (card.value("name", "") == kHomeLivePrinter)
                    return card;
        return nullptr;
    }

    static bool has_button(const nlohmann::json& card, const std::string& label)
    {
        for (const nlohmann::json& button : card.value("buttons", nlohmann::json::array()))
            if (button.get<std::string>().find(label) != std::string::npos)
                return true;
        return false;
    }

    static bool card_says(const nlohmann::json& card, const std::string& text)
    {
        for (const nlohmann::json& detail : card.value("details", nlohmann::json::array()))
            if (detail.get<std::string>().find(text) != std::string::npos)
                return true;
        return false;
    }

    // Waits until the page draws the live card so that `drawn` holds, asking
    // the page again every quarter second.
    void wait_for_home_page(std::function<bool(const nlohmann::json&)> drawn, const std::string& name, std::function<void()> then)
    {
        m_home_page = nullptr;
        wait_until([this, drawn] { const nlohmann::json card = home_page_card(); return !card.is_null() && drawn(card); },
                   name, std::move(then), [this] {
                       if (m_home_page_ticks % 500 == 499)
                           std::cerr << "HARNESS NOTE home_page_card " << home_page_card().dump() << '\n';
                       if (m_home_page_ticks++ % 25 == 0)
                           read_home_page();
                   });
    }

    void write_home_live_control(const std::string& json)
    {
        boost::filesystem::create_directories(boost::filesystem::path(m_home_live_control).parent_path());
        boost::nowide::ofstream out(m_home_live_control);
        out << json;
    }

    bool still_on_home() const
    {
        return m_notebook->GetSelection() == MainFrame::tpHome && installed_shell()->home_view()->IsShown() &&
               installed_shell()->home_view()->webview()->IsShownOnScreen();
    }

    void begin_home_live()
    {
        wxGetApp().app_config->set("jusprin", "fake_printer", "true");
        m_home_live_backend = std::make_shared<PrinterSetup::OrcaPrinterBackend>(
            *m_plater, PrinterSetup::PrinterCatalog::load(Slic3r::resources_dir()));
        PrinterSetup::AddPrinterRequest request;
        request.vendor_id = "BBL";
        request.model_id  = "Bambu Lab A1 mini";
        request.variant   = "0.4";
        request.name      = kHomeLivePrinter;
        PrinterSetup::SavedPrinter saved;
        check(m_home_live_backend->add_printer(request, saved).empty() && saved.name == kHomeLivePrinter, "home_live_printer_saved");
        m_home_live_backend->prepare_connection(kHomeLivePrinter);
        wait_until([self = shared_from_this()] { return !self->m_home_live_backend->connection(kHomeLivePrinter).candidates.empty(); },
                   "home_live_fake_discovered", [self = shared_from_this()] {
                       const std::string device = self->m_home_live_backend->connection(kHomeLivePrinter).candidates.front().id;
                       const auto fake = std::dynamic_pointer_cast<FakeBambuAgent>(wxGetApp().getAgent()->get_printer_agent());
                       self->check(fake != nullptr, "home_live_fake_agent_installed");
                       if (fake == nullptr) {
                           self->finish();
                           return;
                       }
                       // The path the fake rereads (Testing/README.md).
                       self->m_home_live_control = (fs::path(Slic3r::data_dir()) / "jusprin" / "fake_printer.json").string();
                       self->write_home_live_control(R"({"state":"idle"})");
                       self->check(Printers::link_named_printer(kHomeLivePrinter, device).empty() &&
                                       self->m_home_live_backend->connect_printer(kHomeLivePrinter, device, "").empty(),
                                   "home_live_connect_starts");
                       self->wait_until([self] { return self->m_home_live_backend->connection(kHomeLivePrinter).state == "verified"; },
                                        "home_live_printer_connected", [self] { self->home_live_online(); });
                   });
    }

    // The one look on the way in. Everything after it has to arrive live.
    void home_live_online()
    {
        if (m_notebook->GetSelection() != MainFrame::tpHome)
            m_frame->select_tab(size_t(MainFrame::tpHome));
        installed_shell()->home_view()->refresh();
        wait_for_home_page(
            [](const nlohmann::json& card) { return !card.value("printing", true) && card_says(card, "Online") && has_button(card, "Launch monitor"); },
            "home_live_page_shows_online", [self = shared_from_this()] {
                self->check(self->still_on_home(), "home_live_online_on_home");
                // The card's menu stays open through every update below.
                WebView::RunScript(installed_shell()->home_view()->webview(),
                                   wxString::FromUTF8(std::string("document.querySelector('.printer-menu-button[aria-label=\"Actions for ") +
                                                      kHomeLivePrinter + "\"]').click()"));
                self->wait_for_home_page([](const nlohmann::json& card) { return card.value("menuOpen", false); },
                                         "home_live_card_menu_opens", [self] { self->home_live_printing(); });
            });
    }

    void home_live_printing()
    {
        write_home_live_control(R"({"state":"printing","progress":0.43})");
        wait_for_home_page(
            [](const nlohmann::json& card) { return card.value("printing", false) && card.value("progress", -1) == 43; },
            "home_live_page_shows_printing", [self = shared_from_this()] {
                self->check(self->still_on_home(), "home_live_printing_without_leaving_home");
                self->check(self->home_page_card().value("menuOpen", false), "home_live_printing_keeps_the_card_menu_open");
                self->home_live_quiet();
            });
    }

    // The fake keeps pushing the same status every second; none of it may
    // reach the page. Then the cost of the check that decided so.
    void home_live_quiet()
    {
        Home::HomeHost& host  = installed_shell()->home_view()->host();
        const auto      sent  = host.messages_sent();
        const auto      until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        wait_until([until] { return std::chrono::steady_clock::now() >= until; }, "home_live_quiet_window_elapsed",
                   [self = shared_from_this(), sent] {
                       Home::HomeHost& host = installed_shell()->home_view()->host();
                       std::cerr << "HARNESS NOTE home_live_sent_while_unchanged " << host.messages_sent() - sent << '\n';
                       self->check(host.messages_sent() == sent, "home_live_sends_nothing_while_unchanged");
                       constexpr int kRuns   = 500;
                       int           sends   = 0;
                       const auto    started = std::chrono::steady_clock::now();
                       for (int i = 0; i < kRuns; ++i)
                           sends += host.refresh_if_changed() ? 1 : 0;
                       const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
                       std::cerr << "HARNESS NOTE home_live_unchanged_tick_us " << static_cast<double>(micros) / kRuns << '\n';
                       self->check(sends == 0, "home_live_unchanged_tick_sends_nothing");
                       self->home_live_offline();
                   });
    }

    // Offline is time passing: the fake stops reporting, and the card
    // changes when the freshness window closes, with no message to say so.
    void home_live_offline()
    {
        write_home_live_control(R"({"state":"idle","offline":true})");
        const auto stopped = std::chrono::steady_clock::now();
        wait_for_home_page(
            [](const nlohmann::json& card) {
                return !card.value("printing", true) && card_says(card, "Offline") && !has_button(card, "Launch monitor") && has_button(card, "Reconnect");
            },
            "home_live_page_shows_offline", [self = shared_from_this(), stopped] {
                std::cerr << "HARNESS NOTE home_live_seconds_until_offline "
                          << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - stopped).count() << '\n';
                self->check(self->still_on_home(), "home_live_offline_without_leaving_home");
                self->check(self->home_page_card().value("menuOpen", false), "home_live_offline_keeps_the_card_menu_open");
                // Off Home, the rail stops being read.
                self->m_frame->select_tab(size_t(MainFrame::tp3DEditor));
                Home::HomeHost& host = installed_shell()->home_view()->host();
                const auto      sent = host.messages_sent();
                self->write_home_live_control(R"({"state":"idle"})");
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                self->wait_until([until] { return std::chrono::steady_clock::now() >= until; }, "home_live_hidden_window_elapsed",
                                 [self, sent] {
                                     self->check(installed_shell()->home_view()->host().messages_sent() == sent,
                                                 "home_live_sends_nothing_while_home_is_hidden");
                                     self->home_live_trays();
                                 });
            });
    }

    // The printer reports trays now, so the filament menu leads with "Use
    // what's loaded on the printer…", and the row opens OrcaSlicer's own sync
    // dialog with the sidebar hidden. The dialog is modal; a watcher queued
    // before the click finds it inside its own loop, notes what opened, and
    // cancels it, so the check never waits on a person. Then the printer goes
    // quiet, and the row stays where it was, greyed, with the reason.
    void home_live_trays()
    {
        write_home_live_control(
            R"({"state":"idle","spools":[{"subBrands":"PLA Matte","trayType":"PLA","colour":"5F7D4FFF","filamentId":"GFA01"},)"
            R"({"subBrands":"PETG HF","trayType":"PETG","filamentId":"GFG02"}]})");
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        wait_until([] { return SetupCommands::printer_trays() == SetupCommands::TrayState::Available; },
                   "home_live_printer_reports_trays", [self = shared_from_this()] {
                       installed_shell()->status_row()->open_filament_menu();
                       auto* menu = self->visible_header_menu();
                       self->check(menu != nullptr, "filament_menu_opens_on_a_printer_with_trays");
                       auto* tray = menu ? dynamic_cast<HeaderButton*>(wxWindow::FindWindowByName(
                                               ui_name("Use what's loaded on the printer…"), menu))
                                         : nullptr;
                       self->check(tray != nullptr && tray->IsEnabled(), "the_tray_row_leads_and_is_live");
                       if (tray == nullptr) {
                           if (menu) menu->close();
                           self->finish();
                           return;
                       }
                       self->m_sync_dialog_seen.clear();
                       self->m_sync_watch_done = false;
                       self->watch_for_sync_dialog(200);
                       self->click_row(menu, tray);
                       self->wait_until([self] { return self->m_sync_watch_done; }, "home_live_sync_run_finished",
                                        [self] {
                                            std::cerr << "HARNESS NOTE sync_row_opened " << self->m_sync_dialog_seen << '\n';
                                            // Orca's sync shows its mapping dialog only when it has a choice to
                                            // offer, and a message only when something could not be matched;
                                            // with known trays it copies them without a word. What it did is
                                            // the check, not which dialogs it showed.
                                            // What it did: the project's slots are now the printer's trays.
                                            const auto slots = SetupCommands::filament_slots();
                                            std::cerr << "HARNESS NOTE slots_after_sync " << slots.size();
                                            for (const auto& slot : slots)
                                                std::cerr << " [" << slot.filament.preset_name << " " << slot.colour.ToStdString() << "]";
                                            std::cerr << '\n';
                                            self->check(slots.size() == 2 && wxColour(slots[0].colour) == wxColour("#5F7D4F"),
                                                        "the_sync_copies_the_trays_into_the_slots");
                                            self->home_live_trays_offline();
                                        });
                   });
    }

    void home_live_trays_offline()
    {
        write_home_live_control(R"({"state":"idle","offline":true})");
        wait_until([] { return SetupCommands::printer_trays() == SetupCommands::TrayState::Offline; },
                   "home_live_trays_go_offline", [self = shared_from_this()] {
                       installed_shell()->status_row()->open_filament_menu();
                       auto* menu = self->visible_header_menu();
                       auto* tray = menu ? dynamic_cast<HeaderButton*>(wxWindow::FindWindowByName(
                                               ui_name("Use what's loaded on the printer…"), menu))
                                         : nullptr;
                       self->check(tray != nullptr && !tray->IsEnabled(), "an_offline_printer_keeps_the_tray_row_greyed");
                       self->check(tray != nullptr && tray->decoration().sub_label.Contains("is offline"),
                                   "the_greyed_tray_row_says_why");
                       if (menu) menu->close();
                       self->finish();
                   });
    }

    // Polls, from inside whatever modal loop is running, for the dialogs
    // Orca's sync puts up: its mapping dialog, which is confirmed as a person
    // syncing would, and the message that may follow, which is dismissed.
    // Done once a dialog has been seen and none has been open for a while,
    // so the sync has finished before its outcome is read.
    void watch_for_sync_dialog(int tries_left, int quiet_polls = 0)
    {
        for (wxWindow* window : wxTopLevelWindows)
            if (auto* dialog = dynamic_cast<wxDialog*>(window); dialog && dialog->IsModal()) {
                const bool mapping = dynamic_cast<SyncAmsInfoDialog*>(dialog) != nullptr;
                m_sync_dialog_seen += std::string(m_sync_dialog_seen.empty() ? "" : "; ") +
                                      (mapping ? "SyncAmsInfoDialog" : "other") + " title=" + ui_text(dialog->GetTitle());
                dialog->EndModal(mapping ? wxID_YES : wxID_OK);
                quiet_polls = -1; // restart the quiet count after this one
                break;
            }
        ++quiet_polls;
        if ((!m_sync_dialog_seen.empty() && quiet_polls >= 10) || tries_left <= 0) {
            if (m_sync_dialog_seen.empty())
                m_sync_dialog_seen = "none";
            m_sync_watch_done = true;
            return;
        }
        wxGetApp().CallAfter([self = shared_from_this(), tries_left, quiet_polls] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            self->watch_for_sync_dialog(tries_left - 1, quiet_polls);
        });
    }

    std::string m_sync_dialog_seen;
    bool        m_sync_watch_done{false};

    void check(bool condition, const std::string& name)
    {
        std::cerr << "HARNESS CHECK " << name << ' ' << (condition ? "PASS" : "FAIL") << '\n';
        if (!condition)
            ++m_failures;
    }

    // Every label rendered by the status row, joined. Read by walking the
    // widget tree so the row needs no test-only accessor.
    static wxString status_row_labels(wxWindow* window)
    {
        wxString text = window->GetLabel() + "\n";
        for (wxWindow* child : window->GetChildren())
            text += status_row_labels(child);
        return text;
    }

    void verify_stock_mode()
    {
        check(installed_shell() == nullptr, "stock_mode_installs_no_shell");
        check(m_notebook->GetBtnsListCtrl()->IsShown(), "stock_mode_keeps_tab_strip");
        check(m_plater->is_sidebar_available(), "stock_mode_keeps_sidebar_available");
        load_multi_plate_fixture();
        check(m_plater->canvas3D()->get_volumes_count() >= 2, "stock_mode_canvas_renders_fixture");
        // Exiting with loaded volumes trips an inherited teardown crash in
        // ~GLCanvas3D (Selection::clear() re-enters the destroyed Plater);
        // see the Phase 1 handback. End on an empty project like the shell
        // scenario does.
        check(m_plater->new_project(true, true) != wxID_CANCEL, "stock_mode_teardown_project");
    }

    void verify_shell_installed()
    {
        ShellController* shell = installed_shell();
        check(shell != nullptr && shell->is_installed(), "shell_installed");
        if (shell == nullptr)
            throw std::runtime_error("shell is not installed");
        // Startup lands on Home, which shows no header and collapses the Agent
        // pane on the person's behalf. verify_agent_pane_follows_page checks
        // both return on Prepare, and checks the header's layout there.
        check(m_notebook->GetSelection() == MainFrame::tpHome, "shell_starts_on_home");
        check(shell->status_row() != nullptr && !shell->status_row()->IsShown(), "status_row_hidden_on_home");
        check(shell->persistence()->document().physical_print_count() == 0 &&
                  shell->status_row()->project_summary().Find('\n') == wxNOT_FOUND,
              "overflow_summary_is_the_project_name_alone");
        verify_type_roles_render_at_token_size();
        check(shell->agent_pane() != nullptr && shell->is_agent_pane_collapsed() && !shell->agent_pane()->IsShown(),
              "agent_pane_collapsed_on_home");
        check(shell->agent_pane()->web_view().host().mcp() != nullptr, "shell_starts_mcp_automatically");
        check(!m_notebook->GetBtnsListCtrl()->IsShown(), "tab_strip_hidden");
        check(!m_plater->is_sidebar_available(), "sidebar_marked_unavailable");
        // The shell hides the entire legacy canvas-overlay layer on Prepare.
        check(m_plater->get_view3D_canvas3D()->legacy_overlays_hidden(), "prepare_legacy_overlays_hidden");

        // Later Orca paths call enable_sidebar(true); the persistent policy
        // must keep the sidebar down until the shell releases it.
        m_plater->enable_sidebar(true);
        check(!wxGetApp().sidebar().IsShown(), "sidebar_stays_hidden_after_reenable_attempt");

        // A second installation attempt must fail without disturbing layout.
        ShellController second;
        bool second_install_failed = false;
        try {
            second.install(*m_frame, *m_notebook, *frame_main_sizer());
        } catch (const std::exception&) {
            second_install_failed = true;
        }
        check(second_install_failed, "second_install_rejected");
        check(installed_shell()->is_installed() && installed_shell()->status_row()->GetContainingSizer() != nullptr,
              "shell_survives_rejected_install");
    }

    struct PaneStep
    {
        int                   tab;
        std::string           name;
        std::function<void()> on_arrival;
    };

    // Home shows no header and always collapses the Agent pane; leaving Home
    // shows the header and restores what the person last chose for the pane
    // on Prepare, in both directions. Ends back on Home, where every mode
    // started before this check existed.
    void verify_agent_pane_follows_page(std::function<void()> next)
    {
        ShellController* shell = installed_shell();
        auto collapsed = [shell] { return shell->is_agent_pane_collapsed() && !shell->agent_pane()->IsShown(); };
        auto expanded  = [shell] { return !shell->is_agent_pane_collapsed() && shell->agent_pane()->IsShown(); };
        auto steps = std::make_shared<std::vector<PaneStep>>(std::vector<PaneStep>{
            {MainFrame::tp3DEditor, "agent_pane_prepare_first_visit", [this, shell, collapsed, expanded] {
                 check(shell->status_row()->IsShown(), "status_row_shown_on_prepare");
                 verify_header_layout();
                 check(shell->is_left_pane_collapsed() && !shell->left_pane()->IsShown(),
                       "left_pane_defaults_closed_on_prepare");
                 check(expanded(), "agent_pane_shown_on_prepare");
                 shell->toggle_agent_pane();
                 check(collapsed(), "agent_pane_user_collapses_on_prepare");
             }},
            {MainFrame::tpHome, "agent_pane_home_after_user_collapse", [this, shell, collapsed] {
                 check(!shell->status_row()->IsShown(), "status_row_hidden_on_return_to_home");
                 check(collapsed(), "agent_pane_collapsed_on_home_after_user_collapse");
             }},
            {MainFrame::tp3DEditor, "agent_pane_prepare_after_user_collapse", [this, shell, collapsed, expanded] {
                 check(collapsed(), "agent_pane_user_collapse_restored_on_prepare");
                 shell->toggle_agent_pane();
                 check(expanded(), "agent_pane_user_expands_on_prepare");
             }},
            {MainFrame::tpHome, "agent_pane_home_after_user_expand",
             [this, collapsed] { check(collapsed(), "agent_pane_collapsed_on_home_after_user_expand"); }},
            {MainFrame::tp3DEditor, "agent_pane_prepare_after_user_expand",
             [this, expanded] { check(expanded(), "agent_pane_user_expand_restored_on_prepare"); }},
            {MainFrame::tpHome, "agent_pane_returns_home", [] {}},
        });
        run_pane_steps(steps, 0, std::move(next));
    }

    void run_pane_steps(std::shared_ptr<std::vector<PaneStep>> steps, size_t index, std::function<void()> next)
    {
        if (index == steps->size()) {
            next();
            return;
        }
        const PaneStep& step = (*steps)[index];
        // The header's Home button goes to Home; Home's own page returns to
        // Prepare through the same request its backend makes.
        StatusRow* row = installed_shell()->status_row();
        if (step.tab == MainFrame::tpHome)
            row->request_home();
        else
            row->request_prepare();
        const int tab = step.tab;
        wait_until([this, tab] { return m_notebook->GetSelection() == tab; }, step.name,
                   [self = shared_from_this(), steps, index, next] {
                       (*steps)[index].on_arrival();
                       self->run_pane_steps(steps, index + 1, next);
                   });
    }

    wxSizer* frame_main_sizer()
    {
        // MainFrame's outer sizer owns the platform top bar and the inner
        // application sizer. The project header now lives one level deeper,
        // so find the direct child that recursively contains it.
        wxSizer* root = m_frame->GetSizer();
        for (wxSizerItem* item : root->GetChildren())
            if (wxSizer* child = item->GetSizer(); child != nullptr &&
                child->GetItem(installed_shell()->status_row(), true) != nullptr)
                return child;
        return nullptr;
    }

    void load_multi_plate_fixture()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "fixture_new_project");
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> loaded = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
            false);
        check(loaded.size() == 1, "fixture_cube_loaded");
        check(m_plater->duplicate_object(0) == 1, "fixture_second_object");
        PartPlateList& plates = m_plater->get_partplate_list();
        check(plates.create_plate(true) == 1, "fixture_second_plate");
        check(plates.add_to_plate(1, 0, 1) == 0, "fixture_object_on_second_plate");
        // Slicing needs printable on-bed placement, so center each object on
        // its plate through the same path add_to_plate uses. The outside-set
        // recomputes asynchronously; slice_completes_in_preview is the
        // printability proof.
        check(plates.get_plate(0)->add_instance(0, 0, true) == 0, "fixture_object_centered_on_first_plate");
        m_plater->canvas3D()->reload_scene(true, true);
    }

    void verify_canvas_interaction()
    {
        check(m_plater->canvas3D()->get_volumes_count() >= 2, "canvas_renders_fixture");
        const wxSize canvas_size = m_plater->canvas3D()->get_wxglcanvas()->GetSize();
        check(canvas_size.GetWidth() > 200 && canvas_size.GetHeight() > 200, "canvas_has_working_area");

        check(m_plater->select_object(0), "canvas_selection_command");
        check(!m_plater->canvas3D()->get_selection().is_empty(), "canvas_selection_visible");

        installed_shell()->status_row()->refresh();
        check(!m_plater->is_preview_shown(), "starts_in_prepare");
    }

    void begin_slice_check()
    {
        auto* row = installed_shell()->status_row();
        row->request_slice();
        check(row->action_state().slicing, "header_enters_slicing");
        row->request_action(PrintAction::Cancel);
        wait_until([this] { return !m_plater->is_background_process_slicing(); }, "header_cancel_finishes",
                   [self = shared_from_this()] {
                       auto* row = installed_shell()->status_row();
                       self->check(!row->action_state().sliced, "cancel_leaves_no_valid_slice");
                       self->check(primary_print_action(row->action_state()).primary.action == PrintAction::Slice, "cancel_returns_to_slice");
                       self->begin_verified_slice();
                   });
    }

    void begin_verified_slice()
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return m_plater->is_background_process_slicing(); }, "header_slice_starts",
                   [self = shared_from_this()] {
                       self->wait_until(
                           [self] {
                               PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
                               return plate != nullptr && plate->is_slice_result_valid() && !self->m_plater->is_background_process_slicing();
                           },
                           "slice_completes", [self] { self->after_slice(); });
                   });
    }

    void after_slice()
    {
        auto* row = installed_shell()->status_row();
        check(!m_plater->is_preview_shown(), "slice_stays_in_prepare");
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Print, "slice_offers_print");
        auto* primary = wxWindow::FindWindowByName("Next print action", row);
        check(primary && primary->GetLabel().StartsWith("Print"), "slice_completion_updates_rendered_button");
        m_frame->CallAfter([self = shared_from_this()] {
            wxDialog* dialog = nullptr;
            for (wxWindow* window : wxTopLevelWindows)
                if (window->GetName() == _L("Check Print"))
                    dialog = dynamic_cast<wxDialog*>(window);
            self->check(dialog && dialog->IsModal(), "check_print_opens_modal_window");
            if (!dialog) return;
            Preview* preview = nullptr;
            for (wxWindow* child : dialog->GetChildren())
                if (auto* candidate = dynamic_cast<Preview*>(child))
                    preview = candidate;
            self->check(preview && preview->is_loaded() &&
                            preview->get_canvas3d() != self->m_plater->get_preview_canvas3D(),
                        "check_print_uses_current_orca_preview_result");
            self->check(self->m_notebook->GetSelection() == MainFrame::tp3DEditor && !self->m_plater->is_preview_shown(),
                        "check_print_keeps_prepare_under_window");
            dialog->EndModal(wxID_CANCEL);
        });
        row->request_action(PrintAction::CheckPrint);
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Print,
              "closing_check_print_keeps_print_primary");
        const int original_plate = m_plater->get_partplate_list().get_curr_plate_index();
        m_frame->CallAfter([self = shared_from_this(), original_plate] {
            self->m_plater->select_plate(original_plate == 0 ? 1 : 0);
        });
        row->request_action(PrintAction::CheckPrint);
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Slice, "other_unsliced_plate_offers_slice");
        m_plater->select_plate(original_plate);
        check(primary_print_action(row->action_state()).primary.action == PrintAction::Print, "return_to_sliced_plate_offers_print");
        row->request_slice();
        check(!m_plater->is_background_process_slicing(), "valid_slice_cannot_reslice");
        verify_header_menus();
    }

    void choose_header_item(const wxString& name, std::function<void()> then)
    {
        auto* menu = visible_header_menu();
        auto* item = menu ? wxWindow::FindWindowByName(name,menu) : nullptr;
        if (!item || !item->IsEnabled()) throw std::runtime_error("missing enabled header menu item: " + name.ToStdString());
        for (size_t i=0;i<menu->GetChildren().size() && menu->selected_item() != item;++i) {
            wxKeyEvent down(wxEVT_CHAR); down.m_keyCode = WXK_DOWN;
            menu->GetEventHandler()->ProcessEvent(down);
        }
        check(menu->selected_item() == item,"header_menu_keyboard_selects_enabled_row");
        wxKeyEvent enter(wxEVT_CHAR); enter.m_keyCode = WXK_RETURN;
        menu->GetEventHandler()->ProcessEvent(enter);
        then();
    }

    // A mouse-opened menu must start with nothing highlighted, matching native
    // menus; a keyboard-opened one must start on the first enabled row so a
    // keyboard user has somewhere to arrow from. Keyboard runs first so the
    // mouse case proves a click clears the flag rather than reading its default.
    void verify_header_menu_open_paths(HeaderButton* chevron)
    {
        if (!chevron) return;
        auto has_enabled_item = [](HeaderMenu* menu) {
            if (!menu) return false;
            for (auto* child : menu->GetChildren())
                if (auto* item = dynamic_cast<HeaderButton*>(child); item && item->IsEnabled()) return true;
            return false;
        };
        auto dismiss = [](HeaderMenu* menu) {
            if (!menu) return;
            wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode = WXK_ESCAPE;
            menu->GetEventHandler()->ProcessEvent(escape);
        };
        wxKeyEvent open_key(wxEVT_KEY_DOWN); open_key.m_keyCode = WXK_RETURN;
        chevron->ProcessWindowEvent(open_key);
        auto* key_menu = visible_header_menu();
        check(key_menu && bool(key_menu->selected_item()) == has_enabled_item(key_menu),
              "header_keyboard_menu_selects_an_enabled_row_when_available");
        dismiss(key_menu);

        wxMouseEvent press(wxEVT_LEFT_DOWN), release(wxEVT_LEFT_UP);
        chevron->ProcessWindowEvent(press);
        chevron->ProcessWindowEvent(release);
        auto* mouse_menu = visible_header_menu();
        check(mouse_menu && !mouse_menu->selected_item(),"header_mouse_menu_starts_unhighlighted");
        if (mouse_menu) {
            wxKeyEvent down(wxEVT_CHAR); down.m_keyCode = WXK_DOWN;
            mouse_menu->GetEventHandler()->ProcessEvent(down);
            check(bool(mouse_menu->selected_item()) == has_enabled_item(mouse_menu),
                  "header_mouse_menu_arrow_selects_an_enabled_row_when_available");
        }
        dismiss(mouse_menu);
    }

    void verify_header_menus()
    {
        auto* row = installed_shell()->status_row();
        row->show_action_menu();
        auto* menu = visible_header_menu();
        check(menu && menu->IsShown(),"header_action_menu_visible");
        check(menu && wxWindow::FindWindowByName(ui_name("Check Print"),menu) &&
              wxWindow::FindWindowByName(ui_name("Print all plates…"),menu) &&
              wxWindow::FindWindowByName(ui_name("Export sliced file…"),menu),"header_menu_has_contextual_actions");
        auto* arrow = wxWindow::FindWindowByName("Print actions",row);
        check(menu && menu->GetScreenRect().GetRight() == arrow->GetScreenRect().GetRight(),"header_menu_right_edge_matches_split_button");
        // The trigger must read as held down (and show an up chevron) for as
        // long as the menu stands, and must not stay stuck that way after.
        auto* chevron = dynamic_cast<HeaderButton*>(arrow);
        check(chevron && chevron->is_open(),"header_menu_trigger_marked_open");
        // A touching surface reads as an extension of the button whose corner
        // radius it cannot reconcile with, so the menu stands off by 4px.
        check(menu && menu->GetScreenRect().GetTop() == arrow->GetScreenRect().GetBottom()+1+row->FromDIP(4),
              "header_menu_stands_off_split_button");
        wxKeyEvent escape(wxEVT_CHAR_HOOK); escape.m_keyCode = WXK_ESCAPE;
        menu->GetEventHandler()->ProcessEvent(escape);
        check(!menu->IsShown(),"header_menu_escape_dismisses");
        check(chevron && !chevron->is_open(),"header_menu_trigger_open_state_cleared");
        verify_header_menu_open_paths(chevron);
        row->request_home();
        wait_until([this] { return m_notebook->GetSelection() == MainFrame::tpHome &&
            m_notebook->GetPage(MainFrame::tpHome)->IsShown(); },"header_home_opens_native_home",
            [self=shared_from_this()] {
                self->check(!installed_shell()->status_row()->IsShown(), "header_hidden_on_home");
                installed_shell()->status_row()->request_prepare();
                self->wait_until([self] { return self->m_notebook->GetSelection() == MainFrame::tp3DEditor && !self->m_plater->is_preview_shown(); },
                    "home_returns_to_prepare",[self] {
                        self->verify_chip_keyboard();
                        self->verify_menu_in_place_navigation();
                        self->verify_filament_menu();
                        self->capture_chip_appearance();
                        self->verify_filament_change();
                        self->verify_colour_keyboard();
#ifdef _WIN32
                        self->verify_one_line_across_a_dialog();
#endif
                        self->verify_filament_slots();
                        self->verify_header_setup(Preset::TYPE_PRINTER);
                    });
            });
    }

    // Clicks a menu row the way the platform's own pointer reaches it.
    //
    // Orca's PopupWindow installs a mouse re-dispatcher under __WXOSX__ only
    // (Widgets/PopupWindow.hpp): there the popup receives the event and
    // forwards it to the child under the pointer, so posting to the popup is
    // what a real click does. Windows delivers to the child window directly
    // and nothing forwards, so a click posted to the popup arrives nowhere and
    // the row is never invoked -- the menu simply stays as it was, which reads
    // as "the row did nothing" rather than as a broken click.
    //
    // The macOS position is measured from the popup through screen
    // coordinates, so a control nested in a panel (a colour cell) is hit where
    // it is; its own GetPosition() is relative to that panel, not the popup.
    void click_row(HeaderMenu* menu, wxWindow* row)
    {
#ifdef __WXOSX__
        wxWindow* target = menu;
        const wxPoint at = row->GetScreenPosition() - menu->GetScreenPosition()
            + wxPoint(row->GetSize().x / 2, row->GetSize().y / 2);
#else
        wxWindow* target = row;
        const wxPoint at(row->GetSize().x / 2, row->GetSize().y / 2);
#endif
        for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP}) {
            wxMouseEvent mouse(type);
            mouse.SetPosition(at);
            mouse.SetEventObject(target);
            target->GetEventHandler()->ProcessEvent(mouse);
        }
    }

    // Clicks a keeps_open row and lets the deferred rebuild run. Returns the
    // menu that replaced it.
    HeaderMenu* click_menu_row(HeaderMenu* menu, const wxString& name)
    {
        auto* row = dynamic_cast<HeaderButton*>(wxWindow::FindWindowByName(name, menu));
        if (row == nullptr || !row->IsEnabled()) return nullptr;
        click_row(menu, row);
        wxYield();
        return visible_header_menu();
    }

    // The selectable rows of an open menu, in order. Rows are named by their
    // full label, which the paint-time ellipsis never shortens.
    std::vector<std::string> menu_row_names(HeaderMenu* menu) const
    {
        std::vector<std::string> names;
        for (auto* child : menu->GetChildren())
            if (auto* button = dynamic_cast<HeaderButton*>(child)) names.push_back(ui_text(button->GetName()));
        return names;
    }

    // The one-slot filament menu, as the design lists it: a colour row, the
    // installed filaments that fit in brand-then-name order with the current
    // one ticked, then Other filament…, Filament settings… and Add a slot. The
    // fixture printer reports no trays, so the tray row is absent rather than
    // greyed.
    void verify_filament_menu()
    {
        installed_shell()->status_row()->open_filament_menu();
        auto* menu = visible_header_menu();
        check(menu != nullptr, "filament_menu_opens");
        if (menu == nullptr) return;

        std::vector<std::string> expected;
        for (const auto& filament : SetupCommands::compatible_filaments()) expected.push_back(ui_text(filament.alias));
        check(!expected.empty(), "the_printer_has_installed_filaments");
        for (const char* tail : {"Other filament…", "Filament settings…", "Add a slot"})
            expected.push_back(tail);
        const std::vector<std::string> names = menu_row_names(menu);
        check(names == expected, "filament_menu_lists_the_installed_filaments_then_the_quiet_rows");
        if (names != expected)
            for (const auto& name : names) std::cerr << "HARNESS NOTE filament_menu_row " << name << '\n';
        check(wxWindow::FindWindowByName(ui_name("Use what's loaded on the printer…"), menu) == nullptr,
              "no_tray_row_on_a_printer_that_reports_none");

        auto* current = dynamic_cast<HeaderButton*>(wxWindow::FindWindowByName(SetupCommands::current_filament().alias, menu));
        check(current != nullptr && current->decoration().check, "the_current_filament_is_ticked");
        // The colour row: the slot's own colour first, and the "+" for the
        // system picker. Named by value, since the app holds no colour words.
        check(wxWindow::FindWindowByName(wxColour(SetupCommands::current_colour()).GetAsString(wxC2S_HTML_SYNTAX), menu) != nullptr,
              "the_colour_row_shows_the_slot_colour");
        check(wxWindow::FindWindowByName("Other colour", menu) != nullptr, "the_colour_row_offers_the_system_picker");

        menu->close();
        wxYield();
        check(!chip_half_open(), "filament_menu_dismisses_cleanly");
    }

    void verify_header_setup(Preset::Type type)
    {
        // The printer half opens the printer menu; the filament half's menu
        // carries Filament settings….
        if (type == Preset::TYPE_PRINTER) {
            // First the fixture's own printer, which Orca ships: the chat is a
            // change about that preset, by its name, never one about adding a
            // printer (as it was when the header passed its display name).
            installed_shell()->status_row()->open_printer_menu();
            choose_header_item(ui_name("Printer settings…"),
                [self=shared_from_this()] { self->verify_header_stock_printer_settings(); });
            return;
        }
        const auto& document = installed_shell()->persistence()->document();
        m_header_project_chat_count = document.messages(document.active_conversation_id()).size();
        installed_shell()->status_row()->open_filament_menu();
        // Filament settings… is a plain row of the filament menu.
        m_frame->CallAfter([self=shared_from_this(),type] {
            self->choose_header_item(ui_name("Filament settings…"),
                [self,type] { self->verify_header_setup_open(type); });
        });
    }

    void verify_header_stock_printer_settings()
    {
        const Preset& selected = wxGetApp().preset_bundle->printers.get_selected_preset();
        const std::string preset = selected.name;
        const bool        stock  = selected.is_system;
        wait_until([this] {
                PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
                return panel != nullptr && panel->IsShownOnScreen() && panel->host() != nullptr && panel->host()->handshake_complete();
            }, "header_stock_printer_settings_opens",
            [self=shared_from_this(), preset, stock] {
                PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
                const nlohmann::json session = panel->session_json();
                self->check(session.value("mode", "") == "change" && session.value("printerName", "") == preset,
                            "header_printer_settings_is_about_the_selected_preset_by_name");
                self->check(session["context"]["printer"].value("stock", false) == stock, "header_printer_settings_says_it_is_stock");
                // Its settings are the printer tab as it stands.
                self->live_send("printer_action", {{"action", "open_printer_settings"}});
                self->wait_until([] { return wxGetApp().params_dialog()->IsShown(); }, "header_stock_printer_settings_window_opens",
                    [self, preset] {
                        self->check(wxGetApp().preset_bundle->printers.get_selected_preset_name() == preset,
                                    "header_stock_printer_settings_keeps_the_printer");
                        wxGetApp().params_dialog()->Close();
                        self->verify_header_named_printer_settings();
                    });
            });
    }

    // Then a printer saved under a name, which this check saves from the
    // fixture's own profile and selects for its length.
    void verify_header_named_printer_settings()
    {
        m_header_setup_restore_printer = wxGetApp().preset_bundle->printers.get_selected_preset_name();
        m_header_setup_printer         = Printers::add_named_printer(*m_plater, "Header Printer", {});
        check(SetupCommands::current_printer().nickname.ToStdString() == m_header_setup_printer,
              "header_setup_named_printer_selected");
        installed_shell()->status_row()->open_printer_menu();
        choose_header_item(ui_name("Printer settings…"),
            [self=shared_from_this()] { self->verify_header_printer_settings_open(); });
    }

    // Writes the chip as it actually renders, in both appearance modes and in
    // the long-name case, so appearance is evidence rather than a claim. The
    // application draws these itself, so they need no screen-capture
    // permission and do not depend on what else is on the tester's display.
    void capture_chip_appearance()
    {
        auto* row  = installed_shell()->status_row();
        auto* chip = dynamic_cast<PrinterFilamentChip*>(wxWindow::FindWindowByName("Printer and filament", row));
        check(chip != nullptr, "chip_is_in_the_header");
        if (chip == nullptr) return;

        // The automated run's data directory is temporary and removed at exit.
        // JUSPRIN_ARTIFACT_DIR keeps the images for a human to look at.
        const char* artifact_dir = std::getenv("JUSPRIN_ARTIFACT_DIR");
        const fs::path out = artifact_dir != nullptr ? fs::path(artifact_dir)
                                                     : fs::path(data_dir()) / "chip-appearance";
        fs::create_directories(out);
        const bool was_dark = wxGetApp().dark_mode();
        // At rest means no menu open AND no focus ring: either one makes these
        // images describe a state the reader does not see when simply looking
        // at the header. Earlier checks leave a half focused, and a popup that
        // is still dismissing hands focus back to its anchor after it goes --
        // so drain those pending events first, then move focus away
        // unconditionally rather than testing where it happens to be.
        // The assertion below reads HasFocus(), the same predicate the painter
        // uses to draw the ring, so it cannot disagree with the image. macOS
        // grants focus only inside the key window, so it can only fire on a run
        // where this app is frontmost -- which is the run that would otherwise
        // write a chip with a ring on it.
        wxYield();
        // Focus has to land on a window that can actually hold it, and that is
        // not the row: wxPanel::SetFocus() forwards to a child -- on Windows,
        // one of the very chip halves this is clearing -- and
        // SetFocusIgnoringChildren() on a panel that is not itself focusable
        // leaves focus exactly where it was. Either way the ring the next line
        // asserts against would be the one this line drew. The canvas takes
        // focus on every platform and is not part of the header being
        // photographed.
        m_plater->canvas3D()->get_wxglcanvas()->SetFocus();
        wxYield();
        check(!chip_half_open(), "chip_is_at_rest_before_capture");
        check(!chip->printer_half().HasFocus() && !chip->filament_half().HasFocus(),
              "no_focus_ring_when_the_chip_is_captured");

        // Deliberately does NOT refresh: a refresh re-reads Orca and would
        // overwrite any label the caller set for the shot.
        auto write = [&](const std::string& name) {
            chip->Layout();
            const wxBitmap bitmap = chip->snapshot();
            const std::string file = (out / (name + ".png")).string();
            const bool ok = bitmap.IsOk() && bitmap.GetWidth() > 0 &&
                            bitmap.ConvertToImage().SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
            check(ok, "chip_renders_" + name);
            if (ok) std::cout << "HARNESS ARTIFACT " << name << " " << file
                              << " " << bitmap.GetWidth() << "x" << bitmap.GetHeight() << std::endl;
            return bitmap.IsOk() ? bitmap.ConvertToImage() : wxImage();
        };
        auto same_pixels = [](const wxImage& a, const wxImage& b) {
            if (!a.IsOk() || !b.IsOk() || a.GetWidth() != b.GetWidth() || a.GetHeight() != b.GetHeight())
                return false;
            return std::memcmp(a.GetData(), b.GetData(), std::size_t(a.GetWidth()) * a.GetHeight() * 3) == 0;
        };

        row->refresh();
        installed_shell()->status_row()->apply_appearance(false);
        const wxImage light = write("light");
        installed_shell()->status_row()->apply_appearance(true);
        const wxImage dark = write("dark");
        // Dark mode is a real remapping, not the same pixels with a filter.
        check(!same_pixels(light, dark), "light_and_dark_render_differently");

        // A name far past the 240 DIP cap must ellipsize rather than widen the
        // chip. No refresh after this point: it would restore the real name.
        installed_shell()->status_row()->apply_appearance(false);
        const int natural = chip->GetBestSize().x;
        FilamentChipModel long_model;
        long_model.dots = {{0, "#101010", false}};
        chip->set_filaments(long_model, "Prusament Galaxy Black PLA Blend, third reel from the shelf by the window", {});
        chip->InvalidateBestSize();
        const int capped = chip->GetBestSize().x;
        const wxImage long_name = write("long-name");
        // The proof the long name was actually rendered: different pixels.
        check(!same_pixels(light, long_name), "long_name_actually_rendered");
        // 240 DIP label cap plus the half's own padding and chevron; a name
        // this long must not push the chip past that.
        check(capped <= chip->FromDIP(240) + natural, "long_filament_name_stays_within_the_cap");
        std::cout << "HARNESS MEASURE chip_natural_width=" << natural
                  << " chip_capped_width=" << capped << std::endl;

        // Leave the chip showing real state again.
        installed_shell()->status_row()->apply_appearance(was_dark);
        row->refresh();
    }

    // The nozzle, plate and slot steps all replace the popup's
    // own rows instead of opening a second popup. That mechanism is the one
    // thing a screenshot cannot check and a click test kept missing, so it is
    // asserted here: after activating the row, the SAME popup must still be
    // open and must now show the sub-step.
    void verify_menu_in_place_navigation()
    {
        auto* row = installed_shell()->status_row();

        row->open_printer_menu();
        auto* menu = visible_header_menu();
        check(menu != nullptr, "printer_menu_opens");
        if (menu == nullptr) return;
        auto* nozzle = wxWindow::FindWindowByName("Nozzle", menu);
        check(nozzle != nullptr, "printer_menu_has_nozzle_row");
        check(nozzle != nullptr && !nozzle->IsEnabled(), "nozzle_row_is_read_only");
        check(wxWindow::FindWindowByName("Plate", menu) != nullptr, "printer_menu_has_plate_row");

        auto* plate = static_cast<HeaderButton*>(wxWindow::FindWindowByName("Plate", menu));
        check(plate != nullptr && plate->IsEnabled(), "plate_row_is_live_with_the_sidebar_hidden");
        if (plate == nullptr || !plate->IsEnabled()) { menu->Dismiss(); return; }

        // Drive it the way a pointer does, not by synthesising the button
        // event: a real click was observed to close the menu where the button
        // event does not, so the defect lives somewhere in this path.
        click_row(menu, plate);

        // The rebuild is deferred past the click, so let the queue drain.
        wxYield();
        auto* after = visible_header_menu();
        check(after != nullptr, "plate_substep_keeps_the_menu_open_after_a_click");
        if (after == nullptr) return;
        // The sub-step replaced the rows: the root's Nozzle row is gone and a
        // back row named Plate heads the list.
        check(wxWindow::FindWindowByName("Nozzle", after) == nullptr, "plate_substep_replaced_the_rows");
        check(wxWindow::FindWindowByName(ui_name("Printer settings…"), after) == nullptr, "plate_substep_hides_root_rows");
        after->close();
        wxYield();
        // Dismissing must also clear the anchor's open state, or the chip keeps
        // painting its pressed fill with no menu on screen.
        check(!chip_half_open(), "dismissing_the_menu_clears_the_chip_open_state");
    }

    // True while either chip half still believes its menu is open.
    bool chip_half_open() const
    {
        auto* chip = dynamic_cast<PrinterFilamentChip*>(
            wxWindow::FindWindowByName("Printer and filament", installed_shell()->status_row()));
        return chip != nullptr && (chip->printer_half().is_open() || chip->filament_half().is_open());
    }

    // Left/right arrows move between the chip's halves, so the whole chip
    // behaves like one control to the keyboard while remaining two to the
    // pointer. Focus is asserted, not the key press.
    void verify_chip_keyboard()
    {
        auto* row  = installed_shell()->status_row();
        auto* chip = dynamic_cast<PrinterFilamentChip*>(wxWindow::FindWindowByName("Printer and filament", row));
        check(chip != nullptr, "chip_present_for_keyboard");
        if (chip == nullptr) return;

        // Focus only sticks while the top-level window is active, so make it
        // so before asserting; otherwise this measures window activation.
        m_frame->Raise();
        m_frame->SetFocus();
        wxYield();
        chip->printer_half().SetFocus();
        wxYield();
        if (!chip->printer_half().HasFocus()) {
            // The harness window is not key (common when another app is
            // frontmost). Focus assertions would measure the environment, not
            // the chip, so say that plainly instead of failing or pretending.
            std::cout << "HARNESS SKIP chip_keyboard_needs_an_active_window" << std::endl;
            return;
        }
        check(chip->printer_half().HasFocus(), "printer_half_takes_focus");

        auto arrow = [&](wxWindow& from, int key) {
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = key;
            event.SetEventObject(&from);
            from.GetEventHandler()->ProcessEvent(event);
            wxYield();
        };
        arrow(chip->printer_half(), WXK_RIGHT);
        check(chip->filament_half().HasFocus(), "right_arrow_moves_to_the_filament_half");
        arrow(chip->filament_half(), WXK_LEFT);
        check(chip->printer_half().HasFocus(), "left_arrow_moves_back_to_the_printer_half");
    }

    // The after-change line must reach the page as a note, rendered without
    // a bubble and led by the colour the change landed on. Checked in the DOM, because the host storing it proves nothing
    // about what the reader sees. RunScript cannot return a value on macOS, so
    // the probe reports through the composer draft, which the host owns.
    void verify_note_rendered(std::function<void()> then)
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        WebView::RunScript(web_view.webview(),
            "(function(){"
            "  var notes = document.querySelectorAll('.message.note');"
            "  var bubbles = document.querySelectorAll('.message.note.user, .message.note.assistant');"
            "  var swatches = document.querySelectorAll('.message.note .note-swatch');"
            "  var text = notes.length ? notes[0].textContent : '';"
            "  var probe = 'notes=' + notes.length + ';bubbles=' + bubbles.length + ';swatches=' + swatches.length + ';first=' + text;"
            "  window.__jusprinTest && window.__jusprinTest.setDraft(probe);"
            "})()");
        wait_until([this] { return persistence().draft().rfind("notes=", 0) == 0; },
            "note_probe_reported", [self = shared_from_this(), then] {
                const std::string probe = self->persistence().draft();
                self->check(probe.find("notes=0;") == std::string::npos, "page_renders_the_filament_note");
                self->check(probe.find(";bubbles=0;") != std::string::npos, "notes_render_without_a_bubble");
                self->check(probe.find(";swatches=0;") == std::string::npos, "the_note_is_led_by_its_colour");
                self->check(probe.find("Filament is now") != std::string::npos, "the_rendered_note_names_the_filament");
                std::cout << "HARNESS NOTE PROBE " << probe << std::endl;
                self->persistence().set_draft({});
                then();
            });
    }

    // A filament change through the menu, as a person makes one: the chip and
    // the project follow at once, the menu stays open so a colour can change
    // in the same visit, and the visit leaves one line in the thread when it
    // ends. Nothing is recorded on the side: the chip names the project.
    void verify_filament_change()
    {
        auto* row = installed_shell()->status_row();
        const auto before = SetupCommands::current_filament();
        check(chip_label(row, "Filament") == before.alias, "chip_names_the_project_filament");

        std::string other;
        wxString    other_alias;
        for (const auto& filament : SetupCommands::compatible_filaments())
            if (filament.preset_name != before.preset_name) {
                other       = filament.preset_name;
                other_alias = filament.alias;
                break;
            }
        check(!other.empty(), "a_second_compatible_filament_exists");
        if (other.empty()) return;
        // Orca's own rule for the colour: a preset that names a default colour
        // brings it; one that names none leaves the slot's colour alone.
        const Preset*  preset          = wxGetApp().preset_bundle->filaments.find_preset(other);
        const auto*    defaults        = preset ? preset->config.option<ConfigOptionStrings>("default_filament_colour") : nullptr;
        const wxString default_colour  = defaults && !defaults->values.empty() ? wxString::FromUTF8(defaults->values.front()) : wxString();
        const wxString colour_before   = wxColour(SetupCommands::current_colour()).GetAsString(wxC2S_HTML_SYNTAX);
        const wxString expected_colour = default_colour.empty() ? colour_before
                                                                : wxColour(default_colour).GetAsString(wxC2S_HTML_SYNTAX);
        std::cout << "HARNESS NOTE filament_change from=" << before.preset_name << " to=" << other
                  << " default_colour=" << default_colour.ToStdString() << std::endl;

        // A saved colour for the colour click below, in the picker's own list.
        const wxColour saved("#101010");
        SetupCommands::remember_colour(saved);
        const auto colours = SetupCommands::saved_colours();
        check(!colours.empty() && colours.front() == saved, "a_picked_colour_joins_the_saved_colours");

        Agent::AgentHost& host        = installed_shell()->agent_pane()->web_view().host();
        const auto        count_notes = [&host] {
            const auto messages = host.conversation();
            return std::count_if(messages.begin(), messages.end(),
                                 [](const auto& message) { return message.role == Agent::MessageRole::Note; });
        };
        const auto notes_before = count_notes();

        row->open_filament_menu();
        auto* menu = visible_header_menu();
        check(menu != nullptr, "filament_menu_opens_for_a_change");
        if (menu == nullptr) return;
        menu = click_menu_row(menu, other_alias);
        check(menu != nullptr, "the_menu_stays_open_after_a_filament_click");
        check(SetupCommands::current_filament().preset_name == other, "the_project_is_on_the_picked_filament");
        check(chip_label(row, "Filament") == other_alias, "the_chip_names_the_picked_filament");
        check(wxColour(SetupCommands::current_colour()).GetAsString(wxC2S_HTML_SYNTAX) == expected_colour,
              "a_filament_change_follows_orcas_colour_rule");
        if (menu == nullptr) return;
        auto* ticked = dynamic_cast<HeaderButton*>(wxWindow::FindWindowByName(other_alias, menu));
        check(ticked != nullptr && ticked->decoration().check, "the_tick_moved_to_the_picked_filament");
        // A thread line lands when the visit ends, not on each click.
        check(count_notes() == notes_before, "no_line_while_the_menu_is_open");

        // The same visit: a colour.
        auto* cell = wxWindow::FindWindowByName(saved.GetAsString(wxC2S_HTML_SYNTAX), menu);
        check(cell != nullptr, "the_colour_row_offers_the_saved_colour");
        if (cell != nullptr) {
            click_row(menu, cell);
            wxYield();
            menu = visible_header_menu();
        }
        check(wxColour(SetupCommands::current_colour()) == saved, "a_colour_click_sets_the_slot_colour");
        check(menu != nullptr, "the_menu_stays_open_after_a_colour_click");
        if (menu == nullptr) return;

        menu->close();
        wxYield();
        wxYield();
        check(count_notes() == notes_before + 1, "the_visit_leaves_one_line");
        const auto messages = host.conversation();
        if (!messages.empty() && messages.back().role == Agent::MessageRole::Note) {
            check(messages.back().text.find(ui_text(other_alias)) != std::string::npos, "the_line_names_the_new_filament");
            check(wxColour(wxString::FromUTF8(messages.back().swatch)) == saved, "the_line_is_led_by_the_colour");
        }

        // Put the fixture back as it was: later checks slice it, and the
        // filament picked here need not print on the fixture's plate.
        check(SetupCommands::select_filament_preset(*m_plater, 0, before.preset_name), "the_fixture_filament_is_restored");
        SetupCommands::set_filament_colour(*m_plater, 0, wxColour(colour_before));
        row->refresh();
        check(chip_label(row, "Filament") == before.alias, "the_chip_follows_the_restored_filament");
    }

    // The colour row by keyboard alone, through the menu's own key handler:
    // Down onto the first filament, Up into the colour row, Right to a saved
    // colour, Return to apply it. The menu stays open and the keyboard stays
    // on that colour after the rebuild; Down leaves the row again.
    void verify_colour_keyboard()
    {
        auto*          row           = installed_shell()->status_row();
        const wxColour saved("#101010"); // remembered by verify_filament_change
        const wxString saved_name    = saved.GetAsString(wxC2S_HTML_SYNTAX);
        const wxString colour_before = SetupCommands::current_colour();
        const auto     filaments     = SetupCommands::compatible_filaments();

        auto* chip = dynamic_cast<PrinterFilamentChip*>(wxWindow::FindWindowByName("Printer and filament", row));
        check(chip != nullptr, "filament_chip_present_for_keyboard_menu");
        if (chip == nullptr) return;
        // A real pointer activation resets the trigger's keyboard-open flag;
        // calling StatusRow directly leaves the previous visit's flag behind.
        auto& trigger = chip->filament_half();
        for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP}) {
            wxMouseEvent mouse(type);
            mouse.SetPosition({trigger.GetSize().x / 2, trigger.GetSize().y / 2});
            mouse.SetEventObject(&trigger);
            trigger.GetEventHandler()->ProcessEvent(mouse);
        }
        HeaderMenu* menu = visible_header_menu();
        check(menu != nullptr, "filament_menu_opens_for_the_keyboard");
        if (menu == nullptr || filaments.empty()) return;
        check(menu->selected_item() == nullptr, "mouse_opened_filament_menu_starts_unselected");
        const auto key = [&](int code) {
            wxKeyEvent event(wxEVT_CHAR_HOOK);
            event.m_keyCode = code;
            menu->GetEventHandler()->ProcessEvent(event);
            wxYield();
            menu = visible_header_menu();
        };
        const auto keyboard_on = [&]() -> wxString {
            HeaderMenuKeyView* keys = menu ? menu->key_view() : nullptr;
            return keys && keys->key_child() ? keys->key_child()->GetName() : wxString();
        };

        key(WXK_DOWN);
        check(menu && menu->selected_item() && menu->selected_item()->GetName() == filaments.front().alias,
              "down_selects_the_first_filament");
        key(WXK_UP);
        check(menu && menu->key_view_active() && menu->selected_item() == nullptr, "up_from_the_first_filament_enters_the_colour_row");
        check(keyboard_on() == wxColour(colour_before).GetAsString(wxC2S_HTML_SYNTAX), "the_keyboard_starts_on_the_slot_colour");
        for (int step = 0; step < 16 && keyboard_on() != saved_name; ++step)
            key(WXK_RIGHT);
        check(keyboard_on() == saved_name, "right_reaches_a_saved_colour");

        key(WXK_RETURN);
        wxYield();
        menu = visible_header_menu();
        check(wxColour(SetupCommands::current_colour()) == saved, "return_applies_the_colour");
        check(menu != nullptr, "the_menu_stays_open_after_a_keyboard_colour");
        check(menu && menu->key_view_active() && keyboard_on() == saved_name, "the_keyboard_stays_on_the_applied_colour");

        key(WXK_DOWN);
        check(menu && !menu->key_view_active() && menu->selected_item() != nullptr, "down_leaves_the_colour_row");
        if (menu) menu->close();
        wxYield();

        SetupCommands::set_filament_colour(*m_plater, 0, wxColour(colour_before));
        row->refresh();
    }

#ifdef _WIN32
    // One visit, one line, even when the menu steps out to a dialog and comes
    // back: pick a filament, open the colour picker from "+" and press OK,
    // pick another filament in the menu that returns, then leave. The system
    // picker is the native one on Windows, so a timer, which its own message
    // loop still serves, presses OK in it.
    void verify_one_line_across_a_dialog()
    {
        auto*      row    = installed_shell()->status_row();
        const auto before = SetupCommands::current_filament();
        const wxString colour_before = SetupCommands::current_colour();
        wxString first, second;
        for (const auto& filament : SetupCommands::compatible_filaments()) {
            if (filament.preset_name == before.preset_name) continue;
            if (first.empty()) first = filament.alias;
            else if (second.empty()) second = filament.alias;
        }
        check(!first.empty() && !second.empty(), "two_other_filaments_exist");
        if (first.empty() || second.empty()) return;

        Agent::AgentHost& host        = installed_shell()->agent_pane()->web_view().host();
        const auto        count_notes = [&host] {
            const auto messages = host.conversation();
            return std::count_if(messages.begin(), messages.end(),
                                 [](const auto& message) { return message.role == Agent::MessageRole::Note; });
        };
        const auto notes_before = count_notes();

        row->open_filament_menu();
        HeaderMenu* menu = visible_header_menu();
        if (menu != nullptr) menu = click_menu_row(menu, first);
        check(menu != nullptr && SetupCommands::current_filament().alias == first, "the_first_filament_is_picked");
        if (menu == nullptr) return;

        bool          pressed = false;
        wxEvtHandler  handler;
        wxTimer       timer(&handler);
        handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            struct Search { DWORD pid; HWND found; } search{GetCurrentProcessId(), nullptr};
            EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
                auto* s = reinterpret_cast<Search*>(data);
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                wchar_t name[16] = {};
                GetClassNameW(hwnd, name, 16);
                if (pid == s->pid && IsWindowVisible(hwnd) && std::wstring(name) == L"#32770") {
                    s->found = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search));
            if (search.found != nullptr) {
                PostMessageW(search.found, WM_COMMAND, IDOK, 0);
                pressed = true;
                timer.Stop();
            }
        });
        timer.Start(100);
        wxWindow* plus = wxWindow::FindWindowByName("Other colour", menu);
        check(plus != nullptr, "the_colour_row_offers_the_picker");
        if (plus != nullptr) {
            click_row(menu, plus);
            for (int i = 0; i < 10 && !pressed; ++i) wxYield();
            wxYield();
        }
        timer.Stop();
        check(pressed, "the_system_picker_opened_and_took_ok");

        menu = visible_header_menu();
        check(menu != nullptr, "the_menu_comes_back_after_the_picker");
        check(count_notes() == notes_before, "stepping_out_to_the_picker_leaves_no_line");
        if (menu != nullptr) menu = click_menu_row(menu, second);
        check(SetupCommands::current_filament().alias == second, "the_second_filament_is_picked");
        if (menu != nullptr) menu->close();
        wxYield();
        wxYield();

        check(count_notes() == notes_before + 1, "a_visit_through_a_dialog_leaves_one_line");
        const auto messages = host.conversation();
        if (!messages.empty() && messages.back().role == Agent::MessageRole::Note)
            check(messages.back().text.find(ui_text(second)) != std::string::npos, "the_line_names_the_final_filament");

        check(SetupCommands::select_filament_preset(*m_plater, 0, before.preset_name), "the_fixture_filament_is_restored_after_the_dialog");
        SetupCommands::set_filament_colour(*m_plater, 0, wxColour(colour_before));
        row->refresh();
    }
#endif

    // Several slots: "Add a slot" turns the one-slot menu into the slot list
    // with the new slot open, and the chip draws a dot per slot -- faded for a
    // slot the plate does not print with. Adding a slot changes no filament,
    // so the visit leaves no line. The slot is taken away again, as the
    // sidebar's own "-" does, so later checks see the one-slot fixture.
    void verify_filament_slots()
    {
        auto* row  = installed_shell()->status_row();
        auto* chip = dynamic_cast<PrinterFilamentChip*>(wxWindow::FindWindowByName("Printer and filament", row));
        check(chip != nullptr, "chip_present_for_slots");
        if (chip == nullptr) return;
        check(SetupCommands::filament_slots().size() == 1, "the_fixture_has_one_slot");
        check(chip->filament_half().decoration().slot_dots.size() == 1, "one_slot_draws_one_dot");

        row->open_filament_menu();
        auto* menu = visible_header_menu();
        check(menu != nullptr, "filament_menu_opens_for_a_slot");
        if (menu == nullptr) return;
        menu = click_menu_row(menu, ui_name("Add a slot"));
        check(menu != nullptr, "adding_a_slot_keeps_the_menu_open");
        const auto slots = SetupCommands::filament_slots();
        check(slots.size() == 2, "adding_a_slot_adds_one");
        if (menu == nullptr || slots.size() != 2) return;

        // The new slot's own view, with the way back to the list at the top.
        check(wxWindow::FindWindowByName(ui_name("Add a slot"), menu) == nullptr, "a_slot_view_does_not_offer_add_a_slot");
        std::string back_name;
        for (const auto& name : menu_row_names(menu))
            if (name.rfind(ui_text(ui_name("FILAMENT · SLOT 2")), 0) == 0) back_name = name;
        check(!back_name.empty(), "the_new_slot_opens_with_its_title");

        // Behind the menu the chip already draws both slots. A dot fades
        // exactly when Orca's plate says it does not print with that slot --
        // unless the plate prints with none, when every slot reads as in use.
        // At this point in the run the current plate is empty, so both are
        // full; the fading itself is covered by the chip model's own tests.
        const auto& dots          = chip->filament_half().decoration().slot_dots;
        const bool  plate_uses_any = slots[0].used || slots[1].used;
        check(dots.size() == 2, "two_slots_draw_two_dots");
        check(dots.size() == 2 && dots[0].faded == (plate_uses_any && !slots[0].used) &&
                  dots[1].faded == (plate_uses_any && !slots[1].used),
              "each_dot_fades_exactly_when_the_plate_does_not_use_its_slot");
        check(chip_label(row, "Filament") == slots[0].filament.alias, "the_label_names_the_filament_both_slots_share");

        if (!back_name.empty()) menu = click_menu_row(menu, ui_name(back_name));
        check(menu != nullptr, "back_reaches_the_slot_list");
        if (menu != nullptr) {
            const auto names = menu_row_names(menu);
            check(std::find(names.begin(), names.end(), "1  " + ui_text(slots[0].filament.alias)) != names.end() &&
                      std::find(names.begin(), names.end(), "2  " + ui_text(slots[1].filament.alias)) != names.end(),
                  "the_slot_list_names_each_slot");
            // The row as it renders: number, then dot, then name. Read from
            // the row's own drawing, left to right: the first column holding
            // text ink comes before the first holding the slot's colour, and
            // more text follows the dot.
            if (auto* first = dynamic_cast<HeaderButton*>(
                    wxWindow::FindWindowByName(ui_name("1  " + ui_text(slots[0].filament.alias)), menu))) {
                const wxImage image = first->snapshot().ConvertToImage();
                const fs::path out  = (std::getenv("JUSPRIN_ARTIFACT_DIR") ? fs::path(std::getenv("JUSPRIN_ARTIFACT_DIR"))
                                                                          : fs::path(data_dir()) / "chip-appearance") /
                                     "slot-list-row.png";
                fs::create_directories(out.parent_path());
                image.SaveFile(wxString::FromUTF8(out.string()), wxBITMAP_TYPE_PNG);
                std::cout << "HARNESS ARTIFACT slot-list-row " << out.string() << std::endl;
                // Ink is whatever stands well apart from the row's own background,
                // in either mode and whatever the font smoothing tints it; the
                // dot is found by the slot's colour and kept out of the ink.
                const wxColour dot(slots[0].colour);
                const int      mid_y = image.GetHeight() / 2;
                const auto     luminance = [&](int x, int y) {
                    return (image.GetRed(x, y) * 299 + image.GetGreen(x, y) * 587 + image.GetBlue(x, y) * 114) / 1000;
                };
                const auto close_to = [&](int x, int y, const wxColour& c, int within) {
                    const int dr = image.GetRed(x, y) - c.Red(), dg = image.GetGreen(x, y) - c.Green(), db = image.GetBlue(x, y) - c.Blue();
                    return dr * dr + dg * dg + db * db < within * within;
                };
                // Only the row's inside: the snapshot's rounded corners leave an
                // unpainted edge that must not read as ink.
                int       text_x = -1, dot_x = -1, text_after_dot = -1;
                const int edge       = first->FromDIP(4);
                const int background = luminance(edge, mid_y);
                const int dot_size   = first->FromDIP(8);
                for (int x = edge; x < image.GetWidth() - edge; ++x)
                    for (int y = image.GetHeight() / 4; y < image.GetHeight() * 3 / 4; ++y) {
                        if (dot.IsOk() && dot_x < 0 && close_to(x, y, dot, 40)) dot_x = x;
                        const bool on_dot = dot_x >= 0 && x >= dot_x - 1 && x <= dot_x + dot_size + 1;
                        if (!on_dot && std::abs(luminance(x, y) - background) > 110) {
                            if (text_x < 0) text_x = x;
                            if (dot_x >= 0 && x > dot_x + dot_size + 1 && text_after_dot < 0) text_after_dot = x;
                        }
                    }
                std::cout << "HARNESS NOTE slot_row text_x=" << text_x << " dot_x=" << dot_x
                          << " text_after_dot=" << text_after_dot << std::endl;
                check(text_x > edge && dot_x > text_x && text_after_dot > dot_x, "a_slot_row_reads_number_dot_name");
            } else {
                check(false, "a_slot_row_reads_number_dot_name");
            }
            menu->close();
            wxYield();
            wxYield();
        }

        // Put the fixture back as it was.
        m_plater->sidebar().delete_filament();
        row->refresh();
        check(SetupCommands::filament_slots().size() == 1, "the_added_slot_is_taken_away");
        check(chip->filament_half().decoration().slot_dots.size() == 1, "the_chip_follows_back_to_one_slot");
    }

    // The header's Printer settings… covers Prepare with a temporary chat.
    void verify_header_printer_settings_open()
    {
        wait_until([this] {
                PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
                return panel != nullptr && panel->IsShownOnScreen() && panel->host() != nullptr &&
                       panel->host()->handshake_complete() && m_notebook->GetSelection() == MainFrame::tp3DEditor;
            }, "header_printer_settings_covers_prepare",
            [self=shared_from_this()] {
                PrinterSetup::PrinterPanel* panel = installed_shell()->printer_panel();
                const nlohmann::json session = panel->session_json();
                self->check(session.value("mode", "") == "change" &&
                                session.value("printerName", "") == self->m_header_setup_printer,
                            "header_printer_settings_is_about_the_selected_printer");
                self->check(panel->GetParent() == self->m_frame && !self->m_notebook->IsShownOnScreen(),
                            "header_printer_chat_hides_prepare_without_switching_tabs");
                self->check(panel->GetSize().x == self->m_frame->GetClientSize().x,
                            "header_printer_chat_fills_the_workspace");
                self->check(!wxGetApp().params_dialog()->IsShown(), "header_printer_settings_leaves_settings_window_closed");
                self->live_capture_themed("prepare_printer_chat");
                panel->close();
                self->wait_until([self,panel] { return !panel->IsShown() && self->m_notebook->IsShownOnScreen(); },
                    "header_printer_conversation_restores_prepare",
                    [self] {
                        self->check(self->m_notebook->GetSelection() == MainFrame::tp3DEditor &&
                                        !self->m_plater->is_preview_shown(),
                                    "header_printer_settings_returns_to_prepare");
                        installed_shell()->open_printer_conversation(self->m_header_setup_printer);
                        self->wait_until([self] {
                                auto* panel = installed_shell()->printer_panel();
                                return panel->IsShownOnScreen() && panel->host() != nullptr && panel->host()->handshake_complete();
                            }, "header_printer_manual_chat_reopens", [self] {
                                self->live_send("printer_action", {{"action", "open_printer_settings"}});
                                self->wait_until([] { return wxGetApp().params_dialog()->IsShown(); },
                                    "header_printer_manual_settings_open", [self] {
                                        self->check(self->m_notebook->IsShownOnScreen() &&
                                                        self->m_notebook->GetSelection() == MainFrame::tp3DEditor,
                                                    "header_printer_manual_action_restores_prepare");
                                        wxGetApp().params_dialog()->Close();
                                        self->check(Printers::remove_named_printer(*self->m_plater,
                                                             self->m_header_setup_printer).empty(),
                                                    "header_setup_named_printer_removed");
                                        SetupCommands::select_printer_preset(*self->m_plater,
                                                                             self->m_header_setup_restore_printer);
                                        self->check(wxGetApp().preset_bundle->printers.get_selected_preset_name() ==
                                                        self->m_header_setup_restore_printer,
                                                    "header_setup_fixture_printer_restored");
                                        self->verify_header_setup(Preset::TYPE_FILAMENT);
                                    });
                            });
                    });
            });
    }

    // Filament settings… opens a fresh temporary chat. Its manual action then
    // restores Prepare and opens Orca's material settings window.
    void verify_header_setup_open(Preset::Type type)
    {
        const std::string kind = "filament";
        wait_until([this] {
                auto* panel = installed_shell()->printer_panel();
                return panel != nullptr && panel->IsShownOnScreen() && panel->host() != nullptr &&
                       panel->host()->handshake_complete() && m_notebook->GetSelection() == MainFrame::tp3DEditor;
            }, "header_setup_opens_temporary_" + kind + "_chat",
            [self=shared_from_this(),type,kind] {
                auto* panel = installed_shell()->printer_panel();
                self->check(panel->GetParent() == self->m_frame && !self->m_notebook->IsShownOnScreen() &&
                                panel->GetSize().x == self->m_frame->GetClientSize().x,
                            "header_filament_chat_hides_prepare_and_fills_workspace");
                const auto& document = installed_shell()->persistence()->document();
                self->check(document.messages(document.active_conversation_id()).size() == self->m_header_project_chat_count,
                            "header_filament_chat_does_not_change_saved_project_chat");
                const auto& task_document = panel->persistence()->document();
                const auto& task_messages = task_document.messages(task_document.active_conversation_id());
                self->check(task_messages.size() == 1 && task_messages.front().role == Agent::MessageRole::Assistant &&
                                task_messages.front().text.find("slot 1") != std::string::npos,
                            "header_filament_chat_opens_with_filament_context");
                self->live_capture_themed("prepare_filament_chat");
                self->live_send("shell_action", {{"action", "open_filament_settings"}});
                self->wait_until([] { return wxGetApp().params_dialog()->IsShown(); },
                    "header_setup_opens_" + kind + "_editor", [self,type,kind] {
                        self->check(self->m_notebook->IsShownOnScreen() &&
                                        self->m_notebook->GetSelection() == MainFrame::tp3DEditor,
                                    "header_filament_manual_action_restores_prepare");
                        self->check(wxGetApp().params_dialog()->panel()->get_current_tab() == wxGetApp().get_tab(type),
                                    "header_setup_selects_" + kind + "_tab");
                        wxGetApp().params_dialog()->Close();
                        if (self->m_state->mode == HarnessState::Mode::TaskChat)
                            self->verify_home_task_routes();
                        else
                            self->verify_header_overflow();
                });
            });
    }

    void verify_home_task_route(const std::string& name, const std::string& mode,
                                std::function<void()> open, std::function<void()> next)
    {
        open();
        wait_until([this] {
                auto* panel = installed_shell()->printer_panel();
                return panel->IsShownOnScreen() && panel->host() != nullptr &&
                       panel->host()->handshake_complete() && m_notebook->GetSelection() == MainFrame::tpHome;
            }, "home_" + name + "_opens_task_chat", [self = shared_from_this(),name,mode,next = std::move(next)] {
                auto* panel = installed_shell()->printer_panel();
                self->check(panel->session_json().value("mode", "") == mode &&
                                !installed_shell()->home_view()->IsShownOnScreen() &&
                                panel->GetSize().x == self->m_frame->GetClientSize().x,
                            "home_" + name + "_covers_home_with_correct_context");
                self->live_capture_themed("home_" + name + "_chat");
                panel->close();
                self->wait_until([panel] {
                        return !panel->IsShown() && installed_shell()->home_view()->IsShownOnScreen();
                    }, "home_" + name + "_restores_home", next);
            });
    }

    void verify_home_task_routes()
    {
        m_frame->select_tab(size_t(MainFrame::tpHome));
        wait_until([this] { return installed_shell()->home_view()->IsShownOnScreen(); },
            "home_task_routes_start_on_home", [self = shared_from_this()] {
                auto& backend = installed_shell()->home_view()->backend();
                self->verify_home_task_route("add", "add", [&backend] { backend.add_printer(); }, [self] {
                    const std::string name = Printers::add_named_printer(*self->m_plater, "Home Task Printer", {});
                    self->check(!name.empty(), "home_task_named_printer_saved");
                    const std::string id = "named:" + name;
                    auto& backend = installed_shell()->home_view()->backend();
                    self->verify_home_task_route("settings", "change", [&backend,id] {
                            backend.open_printer_settings(id);
                        }, [self,name,id] {
                            auto& backend = installed_shell()->home_view()->backend();
                            self->verify_home_task_route("connect", "connect", [&backend,id] {
                                    backend.connect_printer(id);
                                }, [self,name] {
                                    self->check(Printers::remove_named_printer(*self->m_plater, name).empty(),
                                                "home_task_named_printer_removed");
                                    self->finish();
                                });
                        });
                });
            });
    }

    void verify_header_overflow()
    {
        installed_shell()->status_row()->show_overflow_menu();
        HeaderMenu* menu = visible_header_menu();
        check(menu != nullptr && wxWindow::FindWindowByName(_L("Recent projects…"), menu) != nullptr &&
                  wxWindow::FindWindowByName(_L("Version history…"), menu) == nullptr &&
                  wxWindow::FindWindowByName(_L("Export project 3MF…"), menu) == nullptr &&
                  wxWindow::FindWindowByName(_L("Project details"), menu) == nullptr,
              "header_overflow_keeps_recent_projects_and_hands_project_entries_to_the_pane");
        if (menu != nullptr)
            menu->close();
        // Edit Project Info, on the Project tab, is how OrcaSlicer's own editor opens.
        LeftPane* pane = installed_shell()->left_pane();
        pane->show_project();
        pane->snapshot();
        check(pane->press_action(_L("Edit Project Info")), "left_pane_edit_project_info_pressed");
        verify_project_details_open();
    }

    void verify_project_details_open()
    {
        wait_until([this] { return m_notebook->GetCurrentPage() == m_frame->m_project; },
            "header_overflow_opens_project_details",[self=shared_from_this()] {
                installed_shell()->status_row()->request_prepare();
                self->wait_until([self] { return self->m_notebook->GetSelection() == MainFrame::tp3DEditor; },
                    "header_navigation_keeps_project",[self] {
                        self->check(self->m_plater->model().objects.size() >= 2,"header_navigation_preserves_objects");
                        self->verify_unused_slot_fades();
                        // Adding and removing a filament slot invalidates the
                        // earlier slice. Check Print after slicing again.
                        self->begin_slice_all_warm();
                    });
            });
    }

    // With the fixture's objects on the plate, a slot nothing prints with
    // keeps its dot, faded, and the label still names what the plate uses.
    // The slot is added and taken away with the sidebar's own "+" and "-".
    void verify_unused_slot_fades()
    {
        auto* row  = installed_shell()->status_row();
        auto* chip = dynamic_cast<PrinterFilamentChip*>(wxWindow::FindWindowByName("Printer and filament", row));
        if (chip == nullptr) return;
        // The chip reads the current plate; make it one with the fixture's
        // objects on it, and put the selection back afterwards.
        auto&     plates   = m_plater->get_partplate_list();
        const int previous = plates.get_curr_plate_index();
        int       loaded   = -1;
        for (int i = 0; i < plates.get_plate_count() && loaded < 0; ++i)
            if (plates.get_plate(i)->has_printable_instances()) loaded = i;
        check(loaded >= 0, "the_fixture_has_a_plate_with_objects");
        if (loaded < 0) return;
        m_plater->select_plate(loaded);
        const auto added = SetupCommands::add_filament_slot(*m_plater);
        check(added.has_value(), "a_slot_can_be_added_with_objects_on_the_plate");
        if (!added) return;
        row->refresh();
        const auto  slots = SetupCommands::filament_slots();
        const auto& dots  = chip->filament_half().decoration().slot_dots;
        check(slots.size() == 2 && slots[0].used && !slots[1].used, "the_plate_prints_with_slot_one_only");
        check(dots.size() == 2 && !dots[0].faded && dots[1].faded, "the_unused_slot_fades");
        check(slots.size() == 2 && chip_label(row, "Filament") == slots[0].filament.alias,
              "the_label_names_the_filament_the_plate_uses");
        m_plater->sidebar().delete_filament();
        m_plater->select_plate(previous);
        row->refresh();
        check(SetupCommands::filament_slots().size() == 1, "the_faded_slot_is_taken_away");
    }

    HeaderMenu* visible_header_menu() const
    {
        // Dismissed popups remain in the wx tree until delayed destruction.
        // Only the currently shown popup represents a user-visible menu.
        for (auto* child : installed_shell()->status_row()->GetChildren())
            if (auto* menu = dynamic_cast<HeaderMenu*>(child); menu && menu->IsShown()) return menu;
        return nullptr;
    }

    void verify_print_preflight(PrintAction action)
    {
        // Close only the expected native confirmation with Cancel. No printer
        // is selected and no Send button is ever invoked by this test.
        bool observed = false;
        wxEvtHandler handler;
        wxTimer timer(&handler);
        handler.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            for (auto* window : wxTopLevelWindows) {
                auto* dialog = dynamic_cast<wxDialog*>(window);
                if (dialog && dialog->IsModal() && dialog->GetTitle() == "Send print job") {
                    observed = true;
                    dialog->EndModal(wxID_CANCEL);
                    timer.Stop();
                    return;
                }
            }
        });
        timer.Start(50);
        installed_shell()->status_row()->request_action(action);
        timer.Stop();
        check(observed, action == PrintAction::Print ? "print_opens_cancellable_native_preflight" : "print_all_opens_cancellable_native_preflight");
    }

    bool all_nonempty_plates_sliced()
    {
        for (PartPlate* plate : m_plater->get_partplate_list().get_nonempty_plate_list())
            if (plate == nullptr || !plate->is_slice_result_valid())
                return false;
        return true;
    }

    // Slice all with the Preview canvas already initialized by the earlier
    // single-plate slice. Guards the interaction between the shell's hidden
    // plate toolbar and upstream's all-plates stats item (see 83723e2a7b):
    // the same menu dispatch must slice every nonempty plate without crashing.
    void begin_slice_all_warm()
    {
        auto& plates = m_plater->get_partplate_list();
        for (int i = 0; i < plates.get_plate_count(); ++i)
            if (!plates.get_plate(i)->is_slice_result_valid()) { m_plater->select_plate(i); break; }
        installed_shell()->status_row()->request_action(PrintAction::SliceAll);
        wait_until([this] { return all_nonempty_plates_sliced() && !m_plater->is_background_process_slicing(); }, "slice_all_completes_all_plates",
                   [self = shared_from_this()] {
                       self->check(!self->m_plater->is_preview_shown(), "slice_all_stays_in_prepare");
                       self->check(!self->m_plater->get_preview_canvas3D()->is_all_plates_selected(), "slice_all_does_not_select_hidden_preview");
                       self->verify_print_preflight(PrintAction::Print);
                       self->verify_print_preflight(PrintAction::PrintAll);
                       // Restore the deterministic first-plate state for later phases.
                       self->m_plater->select_plate(0);
                       self->verify_agent_bridge();
                   });
    }

    // Opt-in --slice-all-cold: same dispatch, but before the Preview canvas
    // has ever rendered. See the mode comment at the top of this file.
    void begin_slice_all_cold()
    {
        check(!m_plater->is_preview_shown(), "cold_starts_in_prepare");
        check(m_plater->get_partplate_list().get_nonempty_plate_list().size() > 1, "cold_fixture_has_multiple_plates");
        installed_shell()->status_row()->request_slice(true);
        wait_until([this] { return all_nonempty_plates_sliced(); }, "cold_slice_all_completes_all_plates",
                   [self = shared_from_this()] {
                       self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "cold_teardown_project");
                       self->finish();
                   });
    }

    // The Agent page's failure surface is its own Retry pane, and no fixture
    // mode looked at it: a WebView2 controller that failed to come up
    // (Windows ERROR_INVALID_STATE, delivered as wxWEBVIEW_NAV_ERR_OTHER) left
    // the pane on "could not connect" while the run reported failures=0.
    // AgentWebView shows that pane on a load error at once and on a silent
    // page after kHandshakeDeadlineMs (20 s), so "handshake or error pane" is
    // bounded by that deadline; the margin here is only for a page that
    // neither connects nor admits it. Automated modes stop at once instead of
    // spending the 900 s run deadline on a page that is not coming; fixture
    // modes still hand over, with the failure counted, so it can be looked at.
    void wait_for_agent_page(const std::string& prefix, std::function<void()> next)
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.webview() != nullptr, prefix + "_agent_webview_created");
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        wait_until([&web_view, give_up] {
            return web_view.host().handshake_complete() || web_view.bridge_error_shown() ||
                   std::chrono::steady_clock::now() >= give_up;
        }, prefix + "_agent_page_settled", [self = shared_from_this(), prefix, next = std::move(next)] {
            AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
            const bool up = web_view.host().handshake_complete();
            self->check(up, prefix + "_agent_page_handshake_complete");
            self->check(!web_view.bridge_error_shown(), prefix + "_agent_page_shows_no_bridge_error");
            if (!up && self->m_state->mode != HarnessState::Mode::ManualMcp) {
                self->fail(prefix + ": the Agent page did not complete its handshake");
                return;
            }
            next();
        });
    }

    // Handoff item 6. The setup card's "recomputing" state exists only while
    // a background slice runs on a plate that already has an estimate to
    // strike through (OrcaWorkspaceAdapter::snapshot, the remembered-estimate
    // branch), and a 20 mm cube slices faster than a person can photograph
    // it. So the second slice is made slow -- the same cube at 6x is 120 mm
    // on a side, 600 layers instead of 100 -- and the pictures are taken from
    // in here, at the moment request_slice() returns.
    void begin_recomputing_capture()
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return active_plate_sliced_and_idle(); }, "recomputing_first_slice_completes",
                   [self = shared_from_this()] { self->reslice_for_recomputing(); });
    }

    bool active_plate_sliced_and_idle() const
    {
        const PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
        return plate != nullptr && plate->is_slice_result_valid() && !m_plater->is_background_process_slicing();
    }

    std::optional<Workspace::WorkspacePlate> active_workspace_plate() const
    {
        const auto snapshot = installed_workspace_snapshot();
        for (const auto& plate : snapshot.plates)
            if (plate.active) return plate;
        return std::nullopt;
    }

    void reslice_for_recomputing()
    {
        // The estimate the card strikes through is remembered only when a
        // snapshot is read while the slice is valid and idle. The page reads
        // one on its own; reading one here makes that a check, not a race.
        const auto before = active_workspace_plate();
        check(before && before->estimate.has_value() &&
              before->estimate_status == Workspace::EstimateStatus::Current,
              "recomputing_plate_has_a_current_estimate");

        // The instance transform is what the scale gizmo changes, so Orca's
        // own apply() sees a model change and invalidates the plate through
        // the path a person's edit takes.
        ModelObject* object = m_plater->model().objects.front();
        object->instances.front()->set_scaling_factor(Vec3d(6.0, 6.0, 6.0));
        object->ensure_on_bed();
        m_plater->update(false, true);
        auto* row = installed_shell()->status_row();
        row->refresh();
        check(!row->action_state().sliced, "recomputing_scale_invalidates_the_slice");

        row->request_slice();
        check(m_plater->is_background_process_slicing(), "recomputing_reslice_starts");
        // Native truth first, so this cannot pass on a plate that never left
        // "stale".
        const auto during = active_workspace_plate();
        check(during && during->estimate_status == Workspace::EstimateStatus::Recomputing,
              "workspace_reports_recomputing_while_slicing");
        // Then the page, which learns of the slice over the bridge. The probe
        // is asked on every tick and the wait ends the moment the slice does,
        // so a slice that beats the page is a failure and not a hang.
        persistence().set_draft({});
        wait_until([this] {
            return persistence().draft().rfind("card=", 0) == 0 || !m_plater->is_background_process_slicing();
        }, "recomputing_card_probe_settled",
        [self = shared_from_this()] { self->capture_recomputing_card(); },
        [] { probe_setup_card(); });
    }

    // Reports only once the card carries the recomputing state
    // (SetupCard.tsx: .current-setup-heading is the estimate area's heading,
    // .current-setup-metric a time or material figure), so an unanswered
    // probe and a stale card look the same: no draft.
    static void probe_setup_card()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "(function(){"
            "  var card = document.querySelector('[data-testid=\"current-setup\"]');"
            "  var heading = card && card.querySelector('.current-setup-heading');"
            "  if (heading && /recomputing/.test(heading.textContent) && window.__jusprinTest)"
            "    window.__jusprinTest.setDraft('card=' + heading.textContent + '|state=' +"
            "      card.querySelector('.current-setup-state').textContent + '|figures=' +"
            "      card.querySelectorAll('.current-setup-metric').length);"
            "})()");
    }

    void capture_recomputing_card()
    {
        const std::string probe = persistence().draft();
        const bool observed = probe.rfind("card=", 0) == 0;
        check(observed, "setup_card_renders_recomputing_while_slicing");
        check(observed && probe.find("estimates recomputing") != std::string::npos, "setup_card_says_estimates_are_recomputing");
        // The settings are confirmed and the old figures are not: none of
        // them may stand as current while the plate is being sliced again.
        check(observed && probe.find("|state=Confirmed") != std::string::npos, "setup_card_keeps_settings_confirmed");
        check(observed && probe.find("|figures=0") != std::string::npos, "setup_card_shows_no_superseded_figure");
        std::cout << "HARNESS CARD PROBE " << probe << std::endl;
        persistence().set_draft({});
        // The pictures must show the state the probe saw.
        check(m_plater->is_background_process_slicing(), "recomputing_still_slicing_at_capture");
        wxYield();
        write_screen_capture(installed_shell()->agent_pane()->GetScreenRect(), "recomputing-agent-pane");
        write_screen_capture(m_frame->GetScreenRect(), "recomputing-shell");
        wait_until([this] { return active_plate_sliced_and_idle(); }, "recomputing_reslice_completes",
                   [self = shared_from_this()] {
                       const auto after = self->active_workspace_plate();
                       self->check(after && after->estimate_status == Workspace::EstimateStatus::Current,
                                   "recomputing_returns_to_current");
                       self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "recomputing_teardown_project");
                       self->finish();
                   });
    }

    // The setup card and its page against real presets. The values are
    // edited through the process tab, the filament tab and an object's own
    // settings, so every label, group and formatted value on the page is one
    // OrcaSlicer and the adapter produced, not a fixture's.
    void begin_setup_differences_capture()
    {
        m_plater->model().objects.front()->config.set_key_value("wall_loops", new ConfigOptionInt(5));
        DynamicPrintConfig process;
        process.set_deserialize_strict("brim_type", "outer_only");
        process.set_deserialize_strict("brim_width", "8");
        process.set_deserialize_strict("initial_layer_print_height", "0.28");
        process.set_deserialize_strict("initial_layer_speed", "25");
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(process);
        DynamicPrintConfig filament;
        filament.set_deserialize_strict("hot_plate_temp", "105");
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->load_config(filament);
        // One value per extruder, which Orca's index files under its first.
        DynamicPrintConfig printer;
        printer.set_deserialize_strict("retraction_length", "1.2");
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(printer);
        await_setup_text("card", "[data-testid=\"current-setup\"]", "scope.querySelector('.current-setup-row--change')",
                         [self = shared_from_this()] { self->capture_setup_differences_card(); });
    }

    // Waits until `ready`, a script expression over the element `scope`,
    // holds, then reports the scope's visible text through the draft, one
    // line per row.
    void await_setup_text(const std::string& name, const std::string& scope, const std::string& ready,
                          std::function<void()> next)
    {
        persistence().set_draft({});
        const std::string script =
            "(function(){"
            "  var scope = document.querySelector('" + scope + "');"
            "  if (scope && (" + ready + ") && window.__jusprinTest)"
            "    window.__jusprinTest.setDraft('" + name + "=' + scope.innerText.replace(/\\n+/g, ' | '));"
            "})()";
        wait_until([this, name] { return persistence().draft().rfind(name + "=", 0) == 0; },
                   "setup_differences_" + name + "_rendered", std::move(next),
                   [script] { WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(), script); });
    }

    void run_in_agent_page(const std::string& script)
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(), script);
    }

    void capture_setup_differences_card()
    {
        const std::string card = persistence().draft();
        std::cout << "HARNESS SETUP " << card << std::endl;
        // The change log under the card is written the way the card is.
        bool readable = false;
        for (const Agent::ChangeEntry& change : persistence().document().changes())
            if (change.key == "brim_type") readable = change.from == "Auto" && change.to == "Outer brim only";
        check(readable, "change_log_records_values_as_a_person_reads_them");
        // Two of the six, whichever were edited last.
        check(card.find(" \xE2\x86\x92 ") != std::string::npos, "setup_card_shows_a_changed_setting");
        check(card.find("more change") != std::string::npos, "setup_card_counts_the_changes_it_does_not_show");
        check(card.find("1 object with overrides") != std::string::npos, "setup_card_counts_the_object_override");
        capture_web_view(installed_shell()->agent_pane()->web_view().webview(), "setup-differences-card");
        run_in_agent_page("Array.prototype.find.call(document.querySelectorAll('.current-setup-link'),"
                          "function(link){return link.textContent==='View setup';}).click()");
        await_setup_text("page", "[data-testid=\"current-setup-page\"]", "scope.querySelector('.current-setup-row--change')",
                         [self = shared_from_this()] { self->capture_setup_differences_page(); });
    }

    void capture_setup_differences_page()
    {
        const std::string page = persistence().draft();
        std::cout << "HARNESS SETUP " << page << std::endl;
        for (const char* expected : {"Brim width", "Brim type", "First layer height", "Wall loops", "Bed temperature"})
            check(page.find(expected) != std::string::npos, std::string("setup_page_names_") + expected);
        // The page and group come from Orca's settings tabs, in every user
        // mode: "First layer" alone names four settings.
        for (const char* caption : {"Others \xC2\xB7 Brim", "Quality \xC2\xB7 Layer height", "Speed \xC2\xB7 First layer speed"})
            check(page.find(caption) != std::string::npos, std::string("setup_page_groups_under_") + caption);
        check(page.find("Bed temperature | Bed temperature") != std::string::npos, "setup_page_groups_a_filament_setting");
        check(page.find("Retraction | Retraction Length | ") != std::string::npos, "setup_page_groups_a_printer_setting");
        check(page.find("1.2 mm") != std::string::npos, "setup_page_shows_a_printer_setting_with_its_unit");
        // The thread's own record of the same edits reads the same way.
        check(page.find("outer_only") == std::string::npos, "setup_page_shows_no_config_token");
        check(page.find("Outer brim only") != std::string::npos, "setup_page_shows_a_choice_by_its_label");
        check(page.find("0.28 mm") != std::string::npos, "setup_page_shows_a_number_with_its_unit");
        check(page.find("Ask about this print") == std::string::npos, "setup_page_has_no_message_box");
        capture_web_view(installed_shell()->agent_pane()->web_view().webview(), "setup-differences-page");
        run_in_agent_page("document.querySelector('[data-testid=\"current-setup-page\"]').scrollTop = 100000");
        capture_web_view(installed_shell()->agent_pane()->web_view().webview(), "setup-differences-page-end");

        // Put every value back to the preset's and the card has nothing left
        // to report.
        run_in_agent_page("document.querySelector('.panel-back-link').click()");
        m_plater->model().objects.front()->config.erase("wall_loops");
        const auto restore = [](Preset::Type type, const PresetCollection& presets, std::initializer_list<const char*> keys) {
            DynamicPrintConfig saved;
            for (const char* key : keys)
                saved.set_key_value(key, presets.get_selected_preset().config.option(key)->clone());
            wxGetApp().get_tab(type)->load_config(saved);
        };
        const PresetBundle& bundle = *wxGetApp().preset_bundle;
        restore(Preset::TYPE_PRINTER, bundle.printers, {"retraction_length"});
        restore(Preset::TYPE_FILAMENT, bundle.filaments, {"hot_plate_temp"});
        restore(Preset::TYPE_PRINT, bundle.prints,
                {"brim_type", "brim_width", "initial_layer_print_height", "initial_layer_speed"});
        await_setup_text("restored", "[data-testid=\"current-setup\"]",
                         "!scope.querySelector('.current-setup-row--change')", [self = shared_from_this()] {
            const std::string card = self->persistence().draft();
            std::cout << "HARNESS SETUP " << card << std::endl;
            self->check(card.find("No changes from presets") != std::string::npos, "setup_card_reports_no_changes_after_restore");
            self->check(card.find("with overrides") == std::string::npos, "setup_card_drops_the_object_override_after_restore");
            self->begin_setup_differences_agent_turn();
        });
    }

    // The state the design's first frame draws: a purpose the agent recorded
    // for the chat, settings the agent changed, a hand edit to another preset
    // made before it, an object override, and a sliced plate. The purpose and
    // the patch go through the tools the model calls, so the title, the
    // attribution and the change log are the app's own.
    void begin_setup_differences_agent_turn()
    {
        DynamicPrintConfig filament;
        filament.set_deserialize_strict("hot_plate_temp", "105");
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->load_config(filament);
        m_plater->model().objects.front()->config.set_key_value("wall_loops", new ConfigOptionInt(5));

        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        run_in_agent_page("window.__jusprinTest.send('the corners keep lifting off the bed')");
        wait_until([&web_view] {
            for (const auto& message : web_view.host().conversation())
                if (message.role == Agent::MessageRole::Assistant && message.state == Agent::MessageState::Complete)
                    return true;
            return false;
        }, "setup_differences_agent_turn_complete", [self = shared_from_this()] { self->apply_setup_differences_as_agent(); });
    }

    void apply_setup_differences_as_agent()
    {
        Agent::AgentHost& host = installed_shell()->agent_pane()->web_view().host();
        std::string message_id;
        for (const auto& message : host.conversation())
            if (message.role == Agent::MessageRole::User) message_id = message.id;
        check(!message_id.empty(), "setup_differences_user_message_recorded");

        const auto run = [&host, &message_id, this](const char* tool, const nlohmann::json& arguments, const std::string& name) {
            const std::string action = host.tools().propose({tool, arguments.dump()}, message_id).action_id;
            for (int tick = 0; tick < 200 && !Agent::tool_state_terminal(host.tools().find(action)->state); ++tick)
                host.pump_tools();
            const Agent::ToolActivity* done = host.tools().find(action);
            if (done->state != Agent::ToolState::Succeeded)
                std::cout << "HARNESS SETUP tool " << tool << " ended " << int(done->state) << " " << done->result_json << std::endl;
            check(done->state == Agent::ToolState::Succeeded, name);
        };
        run("intent_update", {{"fields", nlohmann::json::array()}, {"setupTitle", "Stop the corners lifting"}},
            "setup_differences_agent_records_the_purpose");
        const auto snapshot = installed_workspace_snapshot();
        run("settings_apply_patch",
            {{"scope", "process"},
             {"changes", {{"brim_type", "outer_only"}, {"brim_width", "8"}, {"initial_layer_speed", "25"}}},
             {"expectedSessionId", std::to_string(snapshot.session.value())},
             {"expectedRevision", snapshot.revision}},
            "setup_differences_agent_applies_the_patch");

        installed_shell()->status_row()->request_slice();
        wait_until([this] { return active_plate_sliced_and_idle(); }, "setup_differences_plate_sliced", [self = shared_from_this()] {
            self->await_setup_text("updated", "[data-testid=\"current-setup\"]",
                                   "scope.querySelector('.current-setup-metric') && scope.querySelector('.current-setup-row--change')",
                                   [self] { self->capture_setup_differences_updated(); });
        });
    }

    // Straight after the agent's change the card says what just changed.
    void capture_setup_differences_updated()
    {
        const std::string card = persistence().draft();
        std::cout << "HARNESS SETUP " << card << std::endl;
        check(card.find("updated=Setup summary | Updated | Stop the corners lifting | ") == 0,
              "setup_card_is_titled_by_the_purpose_the_agent_recorded");
        // The rows already lead with what the agent changed, and the thread
        // lists it under the card: the card does not say it a third time.
        check(card.find("50 mm/s \xE2\x86\x92 25 mm/s") == std::string::npos, "setup_card_does_not_repeat_the_change_it_just_made");
        capture_web_view(installed_shell()->agent_pane()->web_view().webview(), "setup-differences-agent-updated");
        // A change stays "just changed" for a minute. After it the card is
        // the design's first frame.
        await_setup_text("current", "[data-testid=\"current-setup\"]",
                         "scope.querySelector('.current-setup-state').textContent === 'Current'",
                         [self = shared_from_this()] { self->capture_setup_differences_current(); });
    }

    void capture_setup_differences_current()
    {
        const std::string card = persistence().draft();
        std::cout << "HARNESS SETUP " << card << std::endl;
        const std::string process = short_preset(wxGetApp().preset_bundle->prints.get_edited_preset().name);
        const std::string filament = short_preset(wxGetApp().preset_bundle->filaments.get_edited_preset().name);
        const std::string lead = "current=Setup summary | Current | Stop the corners lifting | " + process + " \xC2\xB7 " + filament + " | ";
        check(card.find(lead) == 0, "setup_card_names_the_process_and_the_filament_under_the_purpose");
        // The two rows are the agent's, ahead of the bed temperature edited
        // by hand: the last two of the three it changed.
        const std::string rows = card.substr(std::min(card.size(), lead.size()), card.find("more change") - std::min(card.size(), lead.size()));
        check(rows.find("Bed temperature") == std::string::npos, "setup_card_leads_with_the_agents_changes");
        int agent_rows = 0;
        for (const char* label : {"Brim type", "Brim width", "First layer"})
            if (rows.find(label) != std::string::npos) ++agent_rows;
        check(agent_rows == 2, "setup_card_shows_two_of_the_agents_changes");
        check(card.find("2 more changes \xC2\xB7 1 object with overrides") != std::string::npos, "setup_card_counts_the_rest");
        check(card.find("~") != std::string::npos && card.find(" g | ") != std::string::npos, "setup_card_shows_the_slice_estimate");
        capture_web_view(installed_shell()->agent_pane()->web_view().webview(), "setup-differences-agent-card");
        run_in_agent_page("Array.prototype.find.call(document.querySelectorAll('.current-setup-link'),"
                          "function(link){return link.textContent==='View setup';}).click()");
        await_setup_text("agentpage", "[data-testid=\"current-setup-page\"]", "scope.querySelector('.current-setup-row--change')",
                         [self = shared_from_this()] { self->finish_setup_differences_capture(); });
    }

    static std::string short_preset(const std::string& name)
    {
        return name.substr(0, name.find(" @"));
    }

    void finish_setup_differences_capture()
    {
        const std::string page = persistence().draft();
        std::cout << "HARNESS SETUP " << page << std::endl;
        check(page.find("Changed from presets \xC2\xB7 4") != std::string::npos, "setup_page_counts_the_agents_and_the_hand_edit");
        check(page.find("Hide the Agent panel") == std::string::npos, "setup_page_text_is_the_page_alone");
        capture_web_view(installed_shell()->agent_pane()->web_view().webview(), "setup-differences-agent-page");
        run_in_agent_page("document.querySelector('.agent-pane-toggle').click()");
        wait_until([] { return !installed_shell()->agent_pane()->IsShown(); }, "setup_page_can_hide_the_agent_panel",
                   [self = shared_from_this()] {
            self->persistence().set_draft({});
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "setup_differences_teardown_project");
            self->finish();
        });
    }

    // A visual fixture for both full-timeline Figma frames. The conversation
    // and manufacturing facts are illustrative, but they pass through the
    // project's real document and the production WebView. Real model edits
    // back the saved versions; the named change entries reproduce the Figma
    // story for this visual fixture.
    void begin_figma_timeline_capture()
    {
        DynamicPrintConfig diff;
        diff.set_deserialize_strict("sparse_infill_density", "35%");
        diff.set_deserialize_strict("wall_loops", "5");
        diff.set_deserialize_strict("sparse_infill_pattern", "gyroid");
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff);
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return active_plate_sliced_and_idle(); }, "figma_timeline_fixture_sliced",
                   [self = shared_from_this()] { self->seed_figma_timeline(); });
    }

    void seed_figma_timeline()
    {
        auto& document = persistence().document();
        const auto fixture = JusPrinTest::seed_figma_timeline_start(document);
        check(m_plater->select_object(0), "figma_timeline_object_selected");
        check(document.set_active_conversation(fixture.background_conversation_id),
              "figma_timeline_background_selected_for_model_edit");
        m_plater->mirror(Slic3r::X);
        check(document.set_active_conversation(fixture.conversation_id),
              "figma_timeline_conversation_restored");
        for (int step = 0; step < 11; ++step) {
            Agent::ChangeEntry change;
            change.kind = "step";
            change.actor = "user";
            change.label = "Rotated bracket to 45°";
            change.location = "on the plate";
            document.add_change(std::move(change), "2026-10-01T15:02:00Z");
        }
        check(document.set_active_conversation(fixture.background_conversation_id),
              "figma_timeline_background_selected_for_setting_edit");
        DynamicPrintConfig diff;
        diff.set_deserialize_strict("sparse_infill_density", "45%");
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff);
        check(document.set_active_conversation(fixture.conversation_id),
              "figma_timeline_conversation_restored_after_setting");
        Agent::ChangeEntry infill;
        infill.kind = "setting";
        infill.actor = "user";
        infill.label = "Infill";
        infill.from = "35%";
        infill.to = "45%";
        infill.preset = "Strong";
        document.add_change(std::move(infill), "2026-10-01T15:03:00Z");
        check(installed_shell()->autosave()->save_now(), "figma_timeline_latest_edit_saved");
        // A subsequent model edit in another conversation makes the infill
        // checkpoint a meaningful restore target, as in the Figma frame.
        check(document.set_active_conversation(fixture.background_conversation_id),
              "figma_timeline_background_selected_for_later_edit");
        m_plater->mirror(Slic3r::Y);
        check(document.set_active_conversation(fixture.conversation_id),
              "figma_timeline_conversation_restored_after_later_edit");
        check(installed_shell()->autosave()->save_now(), "figma_timeline_later_edit_saved");
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return active_plate_sliced_and_idle(); }, "figma_timeline_revised_slice",
                   [self = shared_from_this(), fixture] { self->finish_figma_timeline_capture(fixture); });
    }

    void finish_figma_timeline_capture(const JusPrinTest::FigmaTimelineFixture& fixture)
    {
        auto& document = persistence().document();
        JusPrinTest::finish_figma_timeline(document, fixture);
        persistence().commit();
        installed_shell()->agent_pane()->web_view().host().refresh_page_state();
        m_frame->SetSize(m_frame->FromDIP(wxSize(1200, 1850)));
        auto* divider = wxWindow::FindWindowByName("Resize Agent panel", m_frame);
        check(divider != nullptr, "figma_timeline_resize_handle_found");
        if (divider) {
            const wxPoint start(divider->GetClientSize().x / 2, divider->GetClientSize().y / 2);
            const int delta = installed_shell()->agent_pane()->GetSize().x - divider->FromDIP(429);
            for (wxEventType type : {wxEVT_LEFT_DOWN, wxEVT_MOTION, wxEVT_LEFT_UP}) {
                wxMouseEvent event(type);
                event.SetPosition(start + (type == wxEVT_LEFT_DOWN ? wxPoint() : wxPoint(delta, 0)));
                event.SetEventObject(divider);
                divider->GetEventHandler()->ProcessEvent(event);
            }
            wxYield();
        }
        wait_until([this] { return persistence().draft().rfind("figma-timeline=", 0) == 0; },
                   "figma_timeline_rendered", [self = shared_from_this()] { self->capture_figma_timeline(); },
                   [] { probe_figma_timeline(); });
    }
    static void probe_figma_timeline()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "(function(){ var list = document.querySelector('.message-list');"
            "  if (!list || !window.__jusprinTest) return;"
            "  var found = [document.querySelectorAll('.message.user').length,"
            "    document.querySelectorAll('.message.assistant').length,"
            "    document.querySelectorAll('.history-card').length,"
            "    document.querySelectorAll('.change-row').length,"
            "    document.querySelectorAll('.change-revert-button').length,"
            "    document.querySelectorAll('.settings-changes .settings-change-row').length,"
            "    Number(Array.from(document.querySelectorAll('.settings-changes-heading')).some(function(heading) {"
            "      return (heading.textContent || '').toLowerCase().includes('4 of 12 settings'); })),"
            "    Number((document.querySelector('.agent-change-summary')?.textContent || '').includes('5 settings'))];"
            "  var state = window.__jusprinTest.state();"
            "  window.__jusprinTest.setDraft('figma-timeline=' + found.join(',') + ';' + list.clientHeight + '/' + "
                           "list.scrollHeight + ';points=' + JSON.stringify(state.restorePoints));"
            "})()");
    }

    void capture_figma_timeline()
    {
        const std::string probe = persistence().draft();
        std::cout << "HARNESS FIGMA TIMELINE PROBE " << probe << std::endl;
        check(probe.find("figma-timeline=3,3,3,6,1,4,1,1;") == 0, "figma_timeline_has_messages_history_and_revert");
        persistence().set_draft({});
        installed_shell()->agent_pane()->web_view().host().refresh_page_state();
        if (m_state->figma_timeline_manual) {
            m_frame->SetSize(m_frame->FromDIP(wxSize(1200, 850)));
            m_frame->CentreOnScreen();
            WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
                               "document.querySelector('.message-list').scrollTop = 0");
            m_poll_timer.Stop();
            std::cerr << "HARNESS FIGMA TIMELINE MANUAL READY failures=" << m_failures << '\n';
            return;
        }
        auto* view = installed_shell()->agent_pane()->web_view().webview();
        WebView::RunScript(view, "(function(){ var input = document.querySelector('.composer textarea');"
                                 "if (input) { var setter = Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value').set;"
                                 "setter.call(input, ''); input.dispatchEvent(new Event('input', {bubbles:true})); } })()");
        auto ticks = std::make_shared<int>(0);
        wait_until([ticks] { return ++*ticks > 10; }, "figma_timeline_top_settled", [self = shared_from_this()] {
            auto* view = installed_shell()->agent_pane()->web_view().webview();
            WebView::RunScript(view, "(function(){ var list = document.querySelector('.message-list');"
                                     "list.scrollTop = 0; list.dispatchEvent(new Event('scroll', {bubbles:true})); })()");
            auto top_ticks = std::make_shared<int>(0);
            self->wait_until([top_ticks] { return ++*top_ticks > 10; }, "figma_timeline_top_scrolled", [self] {
                auto* view = installed_shell()->agent_pane()->web_view().webview();
                self->capture_web_view(view, "figma-timeline-top");
                WebView::RunScript(view, "document.querySelector('.message-list').scrollTop = "
                                                                     "document.querySelector('.message-list').scrollHeight");
                auto bottom_ticks = std::make_shared<int>(0);
                self->wait_until([bottom_ticks] { return ++*bottom_ticks > 10; }, "figma_timeline_bottom_settled", [self] {
                    auto* view = installed_shell()->agent_pane()->web_view().webview();
                    self->capture_web_view(view, "figma-timeline-bottom");
                    WebView::RunScript(view, "if (document.querySelector('.change-revert-button')) "
                                                                             "document.querySelector('.change-revert-button').click()");
                    self->wait_until([self] { return self->persistence().draft() == "revert-popover=open"; },
                        "figma_timeline_revert_open", [self] {
                            self->capture_web_view(installed_shell()->agent_pane()->web_view().webview(),
                                                   "figma-timeline-revert-open");
                            self->finish();
                        }, [] { probe_revert_popover(); });
                });
            });
        });
    }

    // Revision-timeline handoff B10: the thread the Figma frame
    // "print-timeline-panel-full · no revert" draws, built from real edits so
    // the rows come through the adapter, persistence and the bridge exactly
    // as a person's would. A recorded build gives a history card, three
    // mirrors give one merged row, and a Tab edit gives a setting row.
    void begin_timeline_capture()
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] { return active_plate_sliced_and_idle(); }, "timeline_fixture_sliced",
                   [self = shared_from_this()] { self->timeline_record_build(); });
    }

    void timeline_record_build()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/build')");
        wait_until([&web_view, activities_before] {
            const auto& activities = web_view.host().tools().activities();
            return activities.size() > activities_before && activities.back().tool == "record_build" &&
                   activities.back().state == Agent::ToolState::Succeeded;
        }, "timeline_build_completed", [self = shared_from_this()] {
            AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
            self->wait_until([self, &web_view] {
                return self->persistence().document().builds().size() == 1 && !web_view.host().stream_active();
            }, "timeline_build_recorded", [self] { self->timeline_ask(); });
        });
    }

    void timeline_ask()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t before = web_view.host().conversation().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest.send('Will it need supports?')");
        wait_until([&web_view, before] {
            const auto messages = web_view.host().conversation();
            return messages.size() >= before + 2 && messages.back().role == Agent::MessageRole::Assistant &&
                   messages.back().state == Agent::MessageState::Complete;
        }, "timeline_reply_completes", [self = shared_from_this()] { self->timeline_edit_by_hand(); });
    }

    void timeline_edit_by_hand()
    {
        const std::size_t changes_before = persistence().document().changes().size();
        check(m_plater->select_object(0), "timeline_object_selected");
        auto* autosave = installed_shell()->autosave();
        for (int mirror = 0; mirror < 3; ++mirror) {
            m_plater->mirror(Slic3r::X);
            if (mirror == 0) {
                check(autosave->save_now(), "timeline_first_mirror_saved_as_checkpoint");
                const auto saved = autosave->history();
                const auto logged = persistence().document().changes();
                check(!saved.empty() && !logged.empty() && saved.back().change_seq == logged.back().seq,
                      "timeline_checkpoint_links_first_mirror");
            }
        }
        const std::string density =
            wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_serialize("sparse_infill_density") == "35%" ?
                "45%" : "35%";
        DynamicPrintConfig diff;
        diff.set_deserialize_strict("sparse_infill_density", density);
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(diff);

        const auto changes = persistence().document().changes();
        const auto added = [&](const std::string& kind) {
            return std::count_if(changes.begin() + static_cast<std::ptrdiff_t>(changes_before), changes.end(),
                                 [&kind](const Agent::ChangeEntry& change) { return change.kind == kind; });
        };
        check(added("step") == 3, "timeline_three_mirrors_are_three_steps");
        check(added("setting") >= 1, "timeline_tab_edit_is_a_setting_change");
        check(std::all_of(changes.begin() + static_cast<std::ptrdiff_t>(changes_before), changes.end(),
                          [](const Agent::ChangeEntry& change) { return change.actor == "person"; }),
              "timeline_hand_edits_are_the_persons");
        check(autosave->save_now(), "timeline_hand_edits_saved_as_version");
        const auto versions = autosave->history();
        check(!versions.empty() && versions.back().change_seq == changes.back().seq,
              "timeline_version_links_to_last_captured_change");
        installed_shell()->agent_pane()->web_view().host().refresh_page_state();

        persistence().set_draft({});
        wait_until([this] { return persistence().draft().rfind("timeline=", 0) == 0; }, "timeline_rows_rendered",
                   [self = shared_from_this()] { self->capture_timeline(); }, [] { probe_timeline(); });
    }

    // Reports the rendered rows once the merged mirror row is on the page, so
    // an unanswered probe and a page still catching up look the same: no draft.
    static void probe_timeline()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "(function(){"
            "  var rows = Array.prototype.map.call(document.querySelectorAll('.change-row'),"
            "    function (row) { return row.textContent; });"
            "  var revert = document.querySelectorAll('.change-revert-button').length;"
            "  var visibleRevert = Array.prototype.filter.call(document.querySelectorAll('.change-revert-button'),"
            "    function (button) { return getComputedStyle(button).opacity === '1'; }).length;"
            "  var changeRows = document.querySelectorAll('.change-row');"
            "  var first = changeRows.length && changeRows[0].querySelector('.change-revert-button');"
            "  var merged = Array.prototype.filter.call(changeRows, function (row) {"
            "    return row.textContent.indexOf('3 steps merged') >= 0; })[0];"
            "  var last = changeRows.length && changeRows[changeRows.length - 1].querySelector('.change-revert-button');"
            "  if (window.__jusprinTest && merged && first)"
            "    window.__jusprinTest.setDraft('timeline=' + rows.join('|') + '|revert-controls=' + revert + '|visible-revert-controls=' + visibleRevert + '|first-revert=' + !!first + '|merged-revert=' + !!merged.querySelector('.change-revert-button') + '|latest-revert=' + !!last);"
            "})()");
    }

    void capture_timeline()
    {
        const std::string probe = persistence().draft();
        std::cout << "HARNESS TIMELINE PROBE " << probe << std::endl;
        check(probe.rfind("timeline=", 0) == 0, "timeline_rows_are_present");
        check(probe.find("3 steps merged") != std::string::npos, "timeline_mirrors_merge_into_one_row");
        check(probe.find("\xE2\x86\x92") != std::string::npos, "timeline_setting_row_reads_from_to");
        check(probe.find("revert-controls=1") != std::string::npos, "timeline_current_saved_group_has_no_revert_control");
        check(probe.find("visible-revert-controls=1") != std::string::npos, "timeline_previous_revert_control_visible_without_hover");
        check(probe.find("first-revert=true") != std::string::npos, "timeline_previous_checkpoint_has_revert_control");
        check(probe.find("merged-revert=false") != std::string::npos, "timeline_merged_run_skips_inner_checkpoint");
        check(probe.find("latest-revert=false") != std::string::npos, "timeline_latest_row_has_no_revert_control");
        persistence().set_draft({});
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "document.querySelector('.change-revert-button').click()");
        wait_until([this] { return persistence().draft() == "revert-popover=open"; },
                   "timeline_revert_popover_opened", [self = shared_from_this()] {
            self->check(self->persistence().draft() == "revert-popover=open", "timeline_revert_explains_restore_before_action");
            WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
                "document.querySelector('.change-revert-popover button').click()");
            self->persistence().set_draft({});
            self->wait_until([self] { return self->persistence().draft() == "revert-popover=closed"; },
                "timeline_revert_cancelled", [self] { self->begin_timeline_spacing(); }, [] { probe_revert_popover(); });
        }, [] { probe_revert_popover(); });
    }

    static void probe_revert_popover()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "if (window.__jusprinTest) window.__jusprinTest.setDraft('revert-popover=' + "
            "(document.querySelector('.change-revert-popover') ? 'open' : 'closed'));");
    }

    void begin_timeline_spacing()
    {
        check(persistence().draft() == "revert-popover=closed", "timeline_revert_cancel_keeps_timeline");
        persistence().set_draft({});
        wait_until([this] { return persistence().draft().rfind("spacing=", 0) == 0; }, "timeline_spacing_measured",
                   [self = shared_from_this()] { self->check_timeline_spacing(); }, [] { probe_spacing(); });
    }

    // The thread's spacing as laid out, against the corrected Figma frame:
    // items 12 apart; padding 4 top, 16 sides and bottom; 8 inside a turn;
    // and 12 from the turn to the change run after it.
    static void probe_spacing()
    {
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "(function(){"
            "  var list = document.querySelector('.message-list');"
            "  if (!list || !window.__jusprinTest) return;"
            "  var group = Array.prototype.filter.call(document.querySelectorAll('.message-group'), function (g) {"
            "    var n = g.nextElementSibling; return n && n.className === 'change-rows'; })[0];"
            "  if (!group) return;"
            "  var next = group.nextElementSibling;"
            "  var s = getComputedStyle(list);"
            "  window.__jusprinTest.setDraft('spacing=' + [s.rowGap, s.paddingTop, s.paddingRight, s.paddingBottom,"
            "    s.paddingLeft, getComputedStyle(group).rowGap,"
            "    Math.round(next.getBoundingClientRect().top - group.getBoundingClientRect().bottom)].join(','));"
            "})()");
    }

    void check_timeline_spacing()
    {
        const std::string spacing = persistence().draft();
        std::cout << "HARNESS TIMELINE SPACING " << spacing << std::endl;
        check(spacing == "spacing=12px,4px,16px,16px,16px,8px,12", "timeline_spacing_matches_figma");
        persistence().set_draft({});
        // The thread follows new content, but a late reflow can leave the last
        // row just below the fold; the picture must show the newest rows.
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
                           "(function(){ var list = document.querySelector('.message-list');"
                           "  if (list) list.scrollTop = list.scrollHeight; })()");
        auto ticks = std::make_shared<int>(0);
        wait_until([ticks] { return ++*ticks > 10; }, "timeline_thread_scrolled_to_end", [self = shared_from_this()] {
            wxYield();
            const bool dark = self->m_state->dark_appearance.value_or(false);
            self->write_screen_capture(installed_shell()->agent_pane()->GetScreenRect(),
                                       std::string("timeline-agent-pane-") + (dark ? "dark" : "light"));
            self->timeline_revert_previous_group();
        });
    }

    void timeline_revert_previous_group()
    {
        const auto versions = installed_shell()->autosave()->history();
        const auto first = std::find_if(versions.begin(), versions.end(), [](const auto& version) {
            return version.change_seq != 0;
        });
        check(first != versions.end(), "timeline_previous_saved_group_exists");
        if (first == versions.end()) {
            timeline_new_project_starts_fresh();
            return;
        }
        const std::string selected_id = first->id;
        const std::size_t changes_before = persistence().document().changes().size();
        const std::size_t conversations_before = persistence().document().conversations().size();
        persistence().set_draft({});
        WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
            "document.querySelectorAll('.change-revert-button')[0].click()");
        wait_until([this] { return persistence().draft() == "revert-popover=open"; },
                   "timeline_previous_revert_explained", [self = shared_from_this(), selected_id, changes_before, conversations_before] {
            WebView::RunScript(installed_shell()->agent_pane()->web_view().webview(),
                "document.querySelector('.change-revert-buttons .confirm').click()");
            self->wait_until([self, changes_before] {
                const auto changes = self->persistence().document().changes();
                return changes.size() > changes_before && changes.back().kind == "restore";
            }, "timeline_revert_confirmed", [self, selected_id, conversations_before] {
                const auto changes = self->persistence().document().changes();
                self->check(changes.back().to == selected_id, "timeline_revert_selected_exact_saved_version");
                self->check(self->persistence().document().conversations().size() == conversations_before,
                            "timeline_revert_keeps_conversation");
                self->timeline_new_project_starts_fresh();
            });
        }, [] { probe_revert_popover(); });
    }

    // OrcaSlicer's undo timestamps restart with a new project, so the adapter
    // must reset what it has seen: the new project's log starts empty, and
    // its first edit is its first entry.
    void timeline_new_project_starts_fresh()
    {
        const std::string before = persistence().document().project_id();
        check(m_plater->new_project(true, true) != wxID_CANCEL, "timeline_teardown_project");
        wait_until([this, before] {
            return persistence().document().has_identity() && persistence().document().project_id() != before;
        }, "timeline_new_project_adopted", [self = shared_from_this()] {
            self->check(self->persistence().document().changes().empty(), "timeline_new_project_starts_an_empty_change_log");
            const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
            self->m_plater->load_files(std::vector<std::string>{cube},
                                       LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
                                       false);
            const std::size_t after_load = self->persistence().document().changes().size();
            for (const Agent::ChangeEntry& change : self->persistence().document().changes())
                std::cout << "HARNESS TIMELINE AFTER LOAD " << change.kind << " '" << change.label << "'" << std::endl;
            // A real undo step in the new project. Its timestamp restarts
            // from zero with the new history; had the adapter kept the old
            // project's baseline, this step would go unreported.
            self->check(self->m_plater->select_object(0), "timeline_new_project_object_selected");
            self->m_plater->mirror(Slic3r::X);
            const auto changes = self->persistence().document().changes();
            self->check(changes.size() == after_load + 1 && changes.back().kind == "step",
                        "timeline_first_edit_after_new_project_is_logged");
            self->finish();
        });
    }

    // What is on the screen, not what a widget would draw: the card is
    // WebView2 content, which no snapshot() can paint offscreen. Needs the
    // frame on a visible desktop; at 100% scaling GetScreenRect() and the
    // screen DC share pixels (see handoff item 4 before trusting 150%/200%).
    // The Prepare canvas's tool strip. Pointer events go to the canvas at a
    // button's centre and pass through GLCanvas3D::on_mouse and ImGui's hit
    // test as a user's click does; every result is read back from the gizmo
    // manager, the selection and the model, never from the strip.
    GLCanvas3D& prepare_canvas() { return *m_plater->canvas3D(); }

    GLGizmosManager::EType open_tool() { return prepare_canvas().get_gizmos_manager().get_current_type(); }

    StripLayout tool_strip_layout()
    {
        const Size size = prepare_canvas().get_canvas_size();
        return layout_tool_strip(tool_strip_geometry(brand_theme()->metrics()), wxGetApp().imgui()->get_style_scaling(),
                                 float(size.get_width()), float(size.get_height()));
    }

    void send_canvas_mouse(wxEventType type, const wxPoint& at, bool left_down)
    {
        wxGLCanvas* target = prepare_canvas().get_wxglcanvas();
        wxMouseEvent event(type);
        event.SetEventObject(target);
        event.SetPosition(at);
        event.m_leftDown = left_down;
        target->GetEventHandler()->ProcessEvent(event);
    }

    // A click the way a pointer makes one on a strip already on screen: a
    // frame shows the strip (ImGui hit-tests against the previous frame's
    // windows), the pointer arrives and a frame is drawn so ImGui knows what
    // is under it, then the button goes down and up.
    void press_tool_strip(StripTool tool)
    {
        prepare_canvas().render();
        const StripRect& button = tool_strip_layout().buttons[size_t(tool)];
        const double scale = prepare_canvas().get_scale();
        const wxPoint at(int((button.x + button.w / 2) / scale), int((button.y + button.h / 2) / scale));
        send_canvas_mouse(wxEVT_MOTION, at, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, at, true);
        send_canvas_mouse(wxEVT_LEFT_UP, at, false);
    }

    // The strip runs its action after the frame, so callers wait for the loop.
    void click_tool_strip(StripTool tool, const std::string& settled, std::function<void()> then)
    {
        press_tool_strip(tool);
        wait_until_settled(settled, std::move(then));
    }

    // The whole window, not the canvas: the canvas's own screen rectangle
    // comes back window-relative here, which photographs the wrong part of
    // the screen, while the frame's is right. The window also shows the strip
    // against the rest of the shell, which is what these pictures are for.
    void capture_tool_strip(const std::string& name)
    {
        prepare_canvas().render();
        const bool dark = m_state->dark_appearance.value_or(false);
        write_screen_capture(m_frame->GetScreenRect(), "tool-strip-" + name + (dark ? "-dark" : "-light"));
    }

    size_t first_object_instances() { return m_plater->model().objects.front()->instances.size(); }

    // The open tool's value card is an ImGui window of ours, so its rectangle
    // is the anchor contract: it hangs a panel padding below the strip with
    // its left edge under the button that opened it.
    void check_tool_panel_anchored(StripTool tool, const std::string& name)
    {
        prepare_canvas().render();
        const ImGuiWindow* window = ImGui::FindWindowByName("##jusprin_tool_values");
        check(window != nullptr && window->Active, name + "_panel_is_open");
        if (window == nullptr || !window->Active)
            return;
        const StripLayout layout = tool_strip_layout();
        const StripRect& button = layout.buttons[size_t(tool)];
        const float padding = std::round(brand_theme()->metrics().tool_panel.padding *
                                         wxGetApp().imgui()->get_style_scaling());
        check(std::abs(window->Pos.x - button.x) <= 1.f, name + "_panel_left_edge_under_the_button");
        check(std::abs(window->Pos.y - (layout.strip.y + layout.strip.h + padding)) <= 1.f,
              name + "_panel_hangs_below_the_strip");
        check(window->Size.x > 0.f && window->Size.y > 0.f, name + "_panel_has_content");
    }

    void tool_strip_idle()
    {
        check(open_tool() == GLGizmosManager::Undefined, "tool_strip_starts_with_no_tool_open");
        m_tool_strip_selection = prepare_canvas().get_selection().get_volume_idxs();
        capture_tool_strip("selected");
        click_tool_strip(StripTool::Move, "tool_strip_move_click_settled", [self = shared_from_this()] {
            self->check(self->open_tool() == GLGizmosManager::Move, "tool_strip_opens_move");
            self->check(self->prepare_canvas().get_selection().get_volume_idxs() == self->m_tool_strip_selection,
                        "tool_strip_click_keeps_the_selection");
            self->check_tool_panel_anchored(StripTool::Move, "move");
            self->capture_tool_strip("move");
            self->click_tool_strip(StripTool::Move, "tool_strip_second_move_click_settled", [self] {
                self->check(self->open_tool() == GLGizmosManager::Undefined, "tool_strip_second_click_closes_move");
                self->tool_strip_shortcut();
            });
        });
    }

    // A tool opened from the keyboard must light its button: the strip reads
    // the open tool every frame instead of remembering its own clicks.
    void tool_strip_shortcut()
    {
        wxGLCanvas* target = prepare_canvas().get_wxglcanvas();
        wxKeyEvent key(wxEVT_CHAR);
        key.SetEventObject(target);
        key.m_keyCode = 'r';
        target->GetEventHandler()->ProcessEvent(key);
        wait_until_settled("tool_strip_shortcut_settled", [self = shared_from_this()] {
            self->check(self->open_tool() == GLGizmosManager::Rotate, "r_key_opens_rotate");
            self->check_tool_panel_anchored(StripTool::Rotate, "rotate");
            self->capture_tool_strip("rotate-shortcut");
            self->tool_strip_typed_rotation();
        });
    }

    // Typing into a field, the way a person does it: click the field, type
    // the digits, press Enter. The result is read from the model, not from
    // the card, so a value that displays but never applies still fails.
    void type_into_field(const char* field, const std::string& digits)
    {
        prepare_canvas().render();
        const ViewportToolStrip* strip = installed_shell()->prepare_canvas_presentation().tool_strip();
        check(strip != nullptr, "tool_strip_is_installed");
        if (strip == nullptr)
            return;
        const auto& rects = strip->values().field_rects();
        const auto found = rects.find(field);
        check(found != rects.end(), std::string("field_") + field + "_was_drawn");
        if (found == rects.end())
            return;
        const double scale = prepare_canvas().get_scale();
        const wxPoint at(int((found->second.x + found->second.w / 2) / scale),
                         int((found->second.y + found->second.h / 2) / scale));
        send_canvas_mouse(wxEVT_MOTION, at, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, at, true);
        send_canvas_mouse(wxEVT_LEFT_UP, at, false);
        prepare_canvas().render();

        wxGLCanvas* target = prepare_canvas().get_wxglcanvas();
        const auto send_char = [target](int code) {
            wxKeyEvent event(wxEVT_CHAR);
            event.SetEventObject(target);
            event.m_keyCode = code;
            event.m_uniChar = code;
            target->GetEventHandler()->ProcessEvent(event);
        };
        // A key, not a character: ImGui reads Enter from the key state, which
        // only wxEVT_KEY_DOWN and KEY_UP carry, so a char alone leaves the
        // field open and the value uncommitted.
        const auto send_key = [target](int code) {
            for (const wxEventType type : {wxEVT_KEY_DOWN, wxEVT_KEY_UP}) {
                wxKeyEvent event(type);
                event.SetEventObject(target);
                event.m_keyCode = code;
                target->GetEventHandler()->ProcessEvent(event);
            }
        };
        for (const char digit : digits)
            send_char(digit);
        prepare_canvas().render();
        // While the caret is in the field: the focus ring and the axis hint.
        if (m_capture_typing) {
            capture_tool_strip("typing");
            m_capture_typing = false;
        }
        send_key(WXK_RETURN);
        prepare_canvas().render();
        prepare_canvas().render();
    }

    double first_instance_rotation_z() const
    {
        return m_plater->model().objects.front()->instances.front()->get_rotation().z();
    }

    void report_rotation(const char* what)
    {
        const GizmoObjectManipulation& manip = prepare_canvas().get_gizmos_manager().get_object_manipulation();
        const Vec3d model = m_plater->model().objects.front()->instances.front()->get_rotation();
        std::cerr << "HARNESS ANGLES " << what << " model=(" << Geometry::rad2deg(model.x()) << ", "
                  << Geometry::rad2deg(model.y()) << ", " << Geometry::rad2deg(model.z()) << ")"
                  << " relative=(" << manip.m_new_rotation.x() << ", " << manip.m_new_rotation.y() << ", "
                  << manip.m_new_rotation.z() << ")"
                  << " absolute=(" << manip.m_new_absolute_rotation.x() << ", " << manip.m_new_absolute_rotation.y()
                  << ", " << manip.m_new_absolute_rotation.z() << ")\n";
    }

    // A quarter turn about Y reads back as (180, 90, 180) rather than
    // (0, 90, 0): at ninety degrees on the middle axis, pulling Euler angles
    // back out of a transform is degenerate, and OrcaSlicer's extraction picks
    // that equivalent form. It is upstream's arithmetic -- driving the same
    // change straight through GizmoObjectManipulation gives the same triple --
    // and the fork matches it deliberately. What must stay true is the
    // orientation itself, which is what this checks; the numbers beside it are
    // only a different spelling of the same turn.
    void tool_strip_ninety_orientation()
    {
        type_into_field("##rotation_y", "90");
        wait_until_settled("ninety_typed_settled", [self = shared_from_this()] {
            self->report_rotation("after typing relative Y=90");
            const Transform3d actual =
                self->m_plater->model().objects.front()->instances.front()->get_transformation().get_rotation_matrix();
            const Transform3d expected = Transform3d(Eigen::AngleAxisd(M_PI / 2, Vec3d::UnitY()));
            const double difference = (actual.matrix() - expected.matrix()).cwiseAbs().maxCoeff();
            self->check(difference < 1e-9, "ninety_leaves_the_object_in_the_right_orientation");
            self->m_plater->undo();
            self->wait_until_settled("ninety_undo_settled", [self] { self->tool_strip_scale_step(); });
        });
    }

    // A typed rotation has to reach the model, and it has to reach it once:
    // the relative field says "turn it by this much", so 45 typed into Z is
    // 45 degrees, not 90, and the field goes back to zero afterwards.
    void tool_strip_typed_rotation()
    {
        const double before = first_instance_rotation_z();
        type_into_field("##rotation_z", "45");
        wait_until_settled("typed_rotation_settled", [self = shared_from_this(), before] {
            const double after = self->first_instance_rotation_z();
            const double turned = Geometry::rad2deg(after - before);
            std::cerr << "HARNESS ROTATION before=" << Geometry::rad2deg(before) << " after=" << Geometry::rad2deg(after)
                      << " turned=" << turned << '\n';
            self->check(std::abs(turned - 45.0) < 0.5, "typed_relative_rotation_turns_by_what_was_typed");
            self->capture_tool_strip("rotated");
            self->check(self->open_tool() == GLGizmosManager::Rotate, "typing_keeps_the_rotate_tool_open");
            self->tool_strip_absolute_rotation();
        });
    }

    // The absolute row says "put it at this angle", so 10 typed into Z with
    // the object already at 45 leaves it at 10, not 55. Then the reset beside
    // the relative row puts it back where the tool found it.
    void tool_strip_absolute_rotation()
    {
        type_into_field("##absolute_rotation_z", "10");
        wait_until_settled("typed_absolute_rotation_settled", [self = shared_from_this()] {
            const double absolute = Geometry::rad2deg(self->first_instance_rotation_z());
            std::cerr << "HARNESS ROTATION absolute=" << absolute << '\n';
            self->check(std::abs(absolute - 10.0) < 0.5, "typed_absolute_rotation_sets_the_angle");
            self->tool_strip_rotation_reset();
        });
    }

    void tool_strip_rotation_reset()
    {
        prepare_canvas().render();
        const ViewportToolStrip* strip = installed_shell()->prepare_canvas_presentation().tool_strip();
        const auto& rects = strip->values().field_rects();
        const bool has_reset = rects.count("##reset_rotate-ccw") == 1;
        check(has_reset, "rotation_reset_button_is_shown_once_rotated");
        if (!has_reset) {
            tool_strip_scale_step();
            return;
        }
        const StripRect& button = rects.at("##reset_rotate-ccw");
        const double scale = prepare_canvas().get_scale();
        const wxPoint at(int((button.x + button.w / 2) / scale), int((button.y + button.h / 2) / scale));
        send_canvas_mouse(wxEVT_MOTION, at, false);
        prepare_canvas().render();
        send_canvas_mouse(wxEVT_LEFT_DOWN, at, true);
        send_canvas_mouse(wxEVT_LEFT_UP, at, false);
        wait_until_settled("rotation_reset_settled", [self = shared_from_this()] {
            const double after = Geometry::rad2deg(self->first_instance_rotation_z());
            std::cerr << "HARNESS ROTATION after reset=" << after << '\n';
            self->check(std::abs(after) < 0.5, "reset_returns_the_rotation_to_where_the_tool_opened");
            self->tool_strip_ninety_orientation();
        });
    }

    void tool_strip_scale_step()
    {
        click_tool_strip(StripTool::Scale, "tool_strip_scale_click_settled", [self = shared_from_this()] {
            self->check(self->open_tool() == GLGizmosManager::Scale, "tool_strip_switches_rotate_to_scale");
            self->check_tool_panel_anchored(StripTool::Scale, "scale");
            self->capture_tool_strip("scale");
            self->click_tool_strip(StripTool::Scale, "tool_strip_second_scale_click_settled", [self] {
                self->check(self->open_tool() == GLGizmosManager::Undefined, "tool_strip_second_click_closes_scale");
                self->tool_strip_duplicate();
            });
        });
    }

    void tool_strip_duplicate()
    {
        const size_t before = first_object_instances();
        click_tool_strip(StripTool::Duplicate, "tool_strip_duplicate_click_settled", [self = shared_from_this(), before] {
            self->check(self->first_object_instances() == before + 1, "duplicate_adds_exactly_one_instance");
            self->capture_tool_strip("duplicated");
            self->m_plater->undo();
            self->wait_until_settled("tool_strip_duplicate_undo_settled", [self, before] {
                self->check(self->first_object_instances() == before, "one_undo_removes_the_duplicate");
                self->check(self->m_plater->select_object(0), "tool_strip_reselects_after_undo");
                self->tool_strip_more();
            });
        });
    }

    // More opens the canvas's own object menu. PopupMenu runs a modal loop,
    // so the check, the capture and the dismissal happen from a timer inside
    // it. Dismissing a native popup from inside needs EndMenu, so the step
    // runs on Windows only.
    void tool_strip_more()
    {
#ifdef _WIN32
        // Plater::PopupMenu shows its menus through the main frame, and a menu
        // names the window that popped it up for exactly as long as it is shown.
        // Taken once: MenuFactory::object_menu() appends items on every call.
        wxMenu* object_menu = m_plater->object_menu();
        auto dismissed      = std::make_shared<bool>(false);
        auto ticks          = std::make_shared<int>(0);
        auto shown_ticks    = std::make_shared<int>(0);
        auto* timer         = new wxTimer();
        timer->Bind(wxEVT_TIMER, [self = shared_from_this(), object_menu, timer, dismissed, ticks, shown_ticks](wxTimerEvent&) {
            const bool shown = object_menu->GetInvokingWindow() == self->m_frame;
            // Not up yet, or up but not yet painted: the timer runs every
            // 100 ms for up to five seconds, and waits three ticks once the
            // menu is up.
            if (shown ? ++*shown_ticks < 3 : ++*ticks < 50)
                return;
            self->check(shown, "more_opens_the_object_menu");
            if (shown) {
                const bool dark = self->m_state->dark_appearance.value_or(false);
                self->blit_screen(self->m_frame->GetScreenRect(), std::string("tool-strip-more-") + (dark ? "dark" : "light"));
                ::EndMenu();
            }
            *dismissed = true;
            timer->Stop();
            wxTheApp->CallAfter([timer] { delete timer; });
        });
        timer->Start(100);
        press_tool_strip(StripTool::More);
        wait_until([dismissed] { return *dismissed; }, "tool_strip_more_menu_dismissed",
                   [self = shared_from_this()] { self->tool_strip_orbit(); });
#else
        tool_strip_orbit();
#endif
    }

    void tool_strip_orbit()
    {
        prepare_canvas().select_view("left");
        wait_until_settled("tool_strip_orbit_settled", [self = shared_from_this()] {
            self->capture_tool_strip("orbited");
            self->tool_strip_narrow();
        });
    }

    void tool_strip_narrow()
    {
        m_frame->Maximize(false);
        m_frame->SetSize(m_frame->FromDIP(wxSize(1000, 700)));
        wait_until_settled("tool_strip_narrow_settled", [self = shared_from_this()] {
            const wxRect canvas = self->prepare_canvas().get_wxglcanvas()->GetScreenRect();
            self->check(canvas.GetRight() < installed_shell()->agent_pane()->GetScreenRect().GetLeft(),
                        "narrow_canvas_ends_before_the_agent_pane");
            const StripLayout narrow = self->tool_strip_layout();
            self->check(narrow.strip.x >= 0.f &&
                            narrow.strip.x + narrow.strip.w <= float(self->prepare_canvas().get_canvas_size().get_width()),
                        "strip_row_fits_the_narrow_canvas");
            self->prepare_canvas().render();
            const bool dark = self->m_state->dark_appearance.value_or(false);
            self->write_screen_capture(self->m_frame->GetScreenRect(), std::string("tool-strip-narrow-") + (dark ? "dark" : "light"));
            self->finish();
        });
    }

#include "print_issues_scenario.inc"

    void write_screen_capture(const wxRect& rect, const std::string& name)
    {
        fs::create_directories(m_state->capture_dir);
        // A screen blit photographs whatever covers the frame, and another
        // application's window on the same desktop is enough to ruin it. Hold
        // the frame on top for the capture, and give WebView2 time to repaint
        // the area it gets back.
        const long style = m_frame->GetWindowStyleFlag();
        m_frame->SetWindowStyleFlag(style | wxSTAY_ON_TOP);
        m_frame->Raise();
        for (int settle = 0; settle < 10; ++settle) {
            wxYield();
            wxMilliSleep(50);
        }
        struct RestoreStyle { wxFrame* frame; long style; ~RestoreStyle() { frame->SetWindowStyleFlag(style); } }
            restore{m_frame, style};
        blit_screen(rect, name);
    }

    // The capture itself, with no yield: safe inside a popup menu's modal loop.
    // The desktop scaling this process does not see.
    //
    // The application is DPI-unaware, so its own coordinates are the
    // virtualized desktop's -- 1728x1084 on a 3456x2168 screen at 200%. The
    // screen DC blits real pixels, so a rectangle has to be scaled up before
    // it names the same place. The ratio is only discoverable through the
    // device itself: HORZRES is what this process is told, DESKTOPHORZRES is
    // what is really there. It is 1 whenever the session runs unscaled, which
    // is why captures were right until the session reconnected at 200%.
    double desktop_scale() const
    {
#ifdef _WIN32
        HDC hdc = ::GetDC(nullptr);
        const int physical = ::GetDeviceCaps(hdc, DESKTOPHORZRES);
        const int logical  = ::GetDeviceCaps(hdc, HORZRES);
        ::ReleaseDC(nullptr, hdc);
        if (logical > 0 && physical > 0)
            return double(physical) / double(logical);
#endif
        return 1.0;
    }

    void blit_screen(const wxRect& logical, const std::string& name)
    {
        fs::create_directories(m_state->capture_dir);
        const double scale = desktop_scale();
        const wxRect rect(int(logical.x * scale), int(logical.y * scale), int(logical.width * scale),
                          int(logical.height * scale));
        wxScreenDC screen;
        wxBitmap   bitmap(rect.GetWidth(), rect.GetHeight());
        {
            wxMemoryDC memory(bitmap);
            memory.Blit(0, 0, rect.GetWidth(), rect.GetHeight(), &screen, rect.GetLeft(), rect.GetTop());
        }
        const std::string file = (m_state->capture_dir / (name + ".png")).string();
        const bool ok = bitmap.IsOk() && bitmap.ConvertToImage().SaveFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
        check(ok, "captured_" + name);
        if (ok) std::cout << "HARNESS ARTIFACT " << name << " " << file
                          << " " << rect.GetWidth() << "x" << rect.GetHeight() << std::endl;
    }

    // Phase 2: the packaged React page in the real WKWebView completes the
    // versioned handshake, a scripted user message round-trips through the
    // real script-message channel and streams to completion, native selection
    // changes push context over the live bridge, and a reload reconstructs
    // the page from native state.
    void verify_agent_bridge()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.webview() != nullptr, "agent_webview_created");
        wait_until([&web_view] { return web_view.host().handshake_complete(); }, "agent_bridge_handshake",
                   [self = shared_from_this()] { self->agent_send_scripted_message(); });
    }

    void verify_live_agent()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.webview() != nullptr, "live_agent_webview_created");
        wait_until([&web_view] { return web_view.host().handshake_complete(); }, "live_agent_bridge_handshake",
                   [self = shared_from_this()] { self->live_agent_context_and_attachment(); });
    }

    void verify_unavailable_agent()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        wait_until([&web_view] { return web_view.host().handshake_complete(); },
                   "unconfigured_agent_bridge_handshake",
                   [self = shared_from_this()] { self->verify_not_set_up_empty_state(); });
    }

    // What the packaged page actually renders in the dock before anything is
    // delegated: the offer replaces the conversation chrome, its one action
    // opens setup, and the ask box stays put and disabled.
    // The page reports the answer back over the bridge's draft message, which
    // the host stores where this harness can read it.
    void verify_not_set_up_empty_state()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.host().availability() == Agent::AgentAvailability::Unavailable,
              "missing_cloud_consent_is_unavailable");
        WebView::RunScript(
            web_view.webview(),
            "(function () {"
            "  var q = function (s) { return document.querySelector(s); };"
            "  var setup = q('[data-testid=\"agent-not-configured\"] button.primary');"
            "  var ask = q('.composer textarea');"
            "  window.__jusprinTest && window.__jusprinTest.setDraft(["
            "    q('[data-testid=\"agent-not-configured\"]') ? 'offer' : 'no-offer',"
            "    q('[data-testid=\"agent-not-configured-header\"]') ? 'header' : 'no-header',"
            "    q('[data-testid=\"context-summary\"]') ? 'chrome' : 'no-chrome',"
            "    setup && setup.disabled ? 'setup-inert' : 'setup-live',"
            "    ask && ask.disabled ? 'ask-inert' : 'ask-live'"
            "  ].join('|'));"
            "})()");
        wait_until([self = shared_from_this()] { return !self->persistence().draft().empty(); },
                   "not_set_up_state_reported", [self = shared_from_this()] {
                       self->check(self->persistence().draft() ==
                                       "offer|header|no-chrome|setup-live|ask-inert",
                                   "not_set_up_dock_is_one_live_offer");
                       if (self->persistence().draft() != "offer|header|no-chrome|setup-live|ask-inert")
                           std::cerr << "HARNESS DETAIL dock state was " << self->persistence().draft() << '\n';
                       self->verify_setup_opens_the_chooser();
                   });
    }

    // The setup flow in the real WKWebView. Each step clicks, then reports
    // from a timeout so React has re-rendered before the page is asked what
    // it now shows; chaining clicks inside one script would read the previous
    // screen.
    void verify_setup_opens_the_chooser()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        persistence().set_draft({});
        WebView::RunScript(web_view.webview(),
                           "(function () {"
                           "  document.querySelector('[data-testid=\"agent-not-configured\"] button.primary').click();"
                           "  setTimeout(function () {"
                           "    window.__jusprinTest.setDraft("
                           "      document.querySelector('[data-testid=\"setup-chooser\"]') ? 'chooser' : 'no-chooser');"
                           "  }, 0);"
                           "})()");
        wait_until([self = shared_from_this()] { return !self->persistence().draft().empty(); },
                   "setup_chooser_reported", [self = shared_from_this()] {
                       self->check(self->persistence().draft() == "chooser", "offer_action_opens_setup");
                       self->verify_setup_reaches_the_key_screen();
                   });
    }

    void verify_setup_reaches_the_key_screen()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        persistence().set_draft({});
        WebView::RunScript(
            web_view.webview(),
            "(function () {"
            "  document.querySelector('[data-testid=\"setup-row-api-key\"]').click();"
            "  setTimeout(function () {"
            "    var screen = document.querySelector('[data-testid=\"setup-api-key\"]');"
            "    var live = [];"
            "    document.querySelectorAll('[data-testid=\"setup-api-key\"] .setup-tab').forEach("
            "      function (tab) { if (!tab.disabled) live.push(tab.textContent); });"
            "    window.__jusprinTest.setDraft("
            "      (screen ? 'key-screen' : 'no-key-screen') + '|' + live.join(','));"
            "  }, 0);"
            "})()");
        wait_until([self = shared_from_this()] { return !self->persistence().draft().empty(); },
                   "setup_key_screen_reported", [self = shared_from_this()] {
                       self->check(self->persistence().draft() == "key-screen|OpenAI",
                                   "key_screen_offers_only_verifiable_providers");
                       if (self->persistence().draft() != "key-screen|OpenAI")
                           std::cerr << "HARNESS DETAIL key screen was " << self->persistence().draft() << '\n';
                       self->verify_setup_key_check_round_trips();
                   });
    }

    // A key check driven from the page, over the real script-message channel,
    // through the real host and HTTP transport. JUSPRIN_OPENAI_ENDPOINT points
    // at a closed local port for this run, so the whole path is exercised and
    // the failure the user sees is a real one, with no external service
    // involved.
    void verify_setup_key_check_round_trips()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        persistence().set_draft({});
        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest.checkKey('openai', 'sk-harness-not-a-real-key')");
        wait_until([self = shared_from_this()] { return self->persistence().draft() == "setup-error"; },
                   "setup_key_check_reports_failure",
                   [self = shared_from_this()] {
                       // An unreachable provider must leave nothing configured.
                       self->check(installed_shell()->agent_pane()->web_view().host().availability() ==
                                       Agent::AgentAvailability::Unavailable,
                                   "unreachable_provider_does_not_configure_the_agent");
                       self->unconfigured_agent_refuses_to_answer();
                   },
                   [self = shared_from_this()] { self->poll_setup_error(); });
    }

    void poll_setup_error()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        WebView::RunScript(web_view.webview(),
                           "(function () {"
                           "  if (document.querySelector('[data-testid=\"setup-error\"]'))"
                           "    window.__jusprinTest.setDraft('setup-error');"
                           "})()");
    }

    void unconfigured_agent_refuses_to_answer()
    {
        auto           self         = shared_from_this();
        AgentWebView&  web_view     = installed_shell()->agent_pane()->web_view();
        const std::size_t objects_before = m_plater->model().objects.size();
        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest && window.__jusprinTest.send('do not use the mock')");
        wait_until(
            [&web_view] {
                const auto conversation = web_view.host().conversation();
                return conversation.size() >= 2 && conversation.back().state == Agent::MessageState::Failed;
            },
            "unconfigured_agent_request_fails_visibly", [self, objects_before] {
                const auto conversation = installed_shell()->agent_pane()->web_view().host().conversation();
                self->check(conversation.back().error && conversation.back().error->code == "agent_unavailable",
                            "unconfigured_agent_does_not_fall_back_to_mock");
                self->check(self->m_plater->model().objects.size() == objects_before &&
                                self->m_plater->select_object(1),
                            "unconfigured_agent_keeps_orca_usable");
                self->check(self->m_plater->new_project(true, true) != wxID_CANCEL,
                            "unconfigured_agent_teardown_project");
                self->finish();
            });
    }

    void live_agent_context_and_attachment()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(web_view.host().availability() == Agent::AgentAvailability::Ready, "live_agent_service_ready");
        check(m_plater->select_object(0), "live_agent_target_selected");
        m_objects_before_tool = m_plater->model().objects.size();
        m_live_copies_before  = copy_count();
        const std::size_t attachments_before = persistence().document().attachments().size();
        WebView::RunScript(
            web_view.webview(),
            "window.__jusprinTest && window.__jusprinTest.attach('live-context.txt', "
            "'SnVzUHJpbiBsaXZlIHZlcmlmaWNhdGlvbiBwaHJhc2U6IGNvYmFsdCBuYXJ3aGFsLg==', 'text/plain')");
        wait_until(
            [this, attachments_before] { return persistence().document().attachments().size() > attachments_before; },
            "live_agent_attachment_staged", [self = shared_from_this()] { self->live_agent_send_context_question(); });
    }

    void live_agent_send_context_question()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t messages_before = web_view.host().conversation().size();
        WebView::RunScript(
            web_view.webview(),
            "window.__jusprinTest && window.__jusprinTest.send('Read the attached note and the authoritative workspace. "
            "Reply exactly: COBALT NARWHAL | 2 plates | selected cube-a. Do not call a tool.')");
        wait_until(
            [&web_view, messages_before] { return web_view.host().conversation().size() >= messages_before + 2; },
            "live_agent_context_request_started", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                web_view.reload();
                self->wait_until([&web_view] { return web_view.host().handshake_complete(); },
                                 "live_agent_reload_during_stream", [self] { self->live_agent_verify_context(); });
            });
    }

    void live_agent_verify_context()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        wait_until(
            [&web_view] {
                const auto conversation = web_view.host().conversation();
                return !conversation.empty() && conversation.back().role == Agent::MessageRole::Assistant &&
                       (conversation.back().state == Agent::MessageState::Complete ||
                        conversation.back().state == Agent::MessageState::Failed);
            },
            "live_agent_context_reply_terminal", [self = shared_from_this()] {
                const auto conversation = installed_shell()->agent_pane()->web_view().host().conversation();
                if (conversation.back().state != Agent::MessageState::Complete) {
                    const std::string code = conversation.back().error ? conversation.back().error->code : "unknown";
                    self->fail("live Agent context reply failed: " + code);
                    return;
                }
                std::string reply = conversation.back().text;
                std::transform(reply.begin(), reply.end(), reply.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                self->check(reply.find("cobalt narwhal") != std::string::npos,
                            "live_agent_used_decoded_attachment");
                self->check(reply.find("2 plates") != std::string::npos && reply.find("cube-a") != std::string::npos,
                            "live_agent_used_native_workspace_context");
                self->live_agent_send_mutation();
            });
    }

    void live_agent_send_mutation()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(
            web_view.webview(),
            "window.__jusprinTest && window.__jusprinTest.send('Make one more copy of the currently selected object now. Use the "
            "plate_layout tool with the exact sessionId and objectId from the authoritative workspace context and quantity 2.');");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && Agent::tool_state_terminal(activities.back().state);
            },
            "live_agent_tool_completed", [self = shared_from_this()] { self->live_agent_decide(); });
    }

    void live_agent_decide()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const auto& activity = web_view.host().tools().activities().back();
        check(activity.tool == "plate_layout", "live_agent_called_typed_duplicate");
        m_live_action_id = activity.action_id;
        wait_until(
            [&web_view, this] {
                const Agent::ToolActivity* current = web_view.host().tools().find(m_live_action_id);
                const auto conversation = web_view.host().conversation();
                return current != nullptr && current->state == Agent::ToolState::Succeeded &&
                       !conversation.empty() && conversation.back().role == Agent::MessageRole::Assistant &&
                       (conversation.back().state == Agent::MessageState::Complete ||
                        conversation.back().state == Agent::MessageState::Failed);
            },
            "live_agent_native_result_and_followup_terminal", [self = shared_from_this()] {
                const auto conversation = installed_shell()->agent_pane()->web_view().host().conversation();
                if (conversation.back().state != Agent::MessageState::Complete) {
                    const std::string code = conversation.back().error ? conversation.back().error->code : "unknown";
                    self->fail("live Agent follow-up failed: " + code);
                    return;
                }
                self->live_agent_verify();
            });
    }

    void live_agent_verify()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(copy_count() == m_live_copies_before + 1, "live_agent_mutated_real_orca_model_once");
        check(m_plater->can_undo_project(), "live_agent_mutation_is_in_orca_history");
        const auto conversation = web_view.host().conversation();
        check(conversation.size() >= 3 && !conversation.back().text.empty(), "live_agent_explains_structured_native_result");
        std::size_t matching_actions = 0;
        for (const auto& action : web_view.host().tools().activities())
            if (action.action_id == m_live_action_id)
                ++matching_actions;
        check(matching_actions == 1, "live_agent_action_id_is_idempotent");
        const auto selected = installed_shell()->workspace()->snapshot().plates[0].objects[0].id;
        const auto selection = installed_shell()->workspace()->select_object(selected);
        check(selection.succeeded() || selection.error == Workspace::WorkspaceError::NoChange, "live_agent_native_selection");
        const auto inspect_id = web_view.host().tools().propose({"workspace_inspect", "{}"}, "live-selection-proof").action_id;
        web_view.host().pump_tools();
        const auto* inspected = web_view.host().tools().find(inspect_id);
        check(inspected && inspected->state == Agent::ToolState::Succeeded &&
              nlohmann::json::parse(inspected->result_json)["selection"]["items"].size() == 1, "live_agent_shared_selection_result");
        check(installed_shell()->workspace()->undo().succeeded(), "live_agent_native_undo_executes");
        check(copy_count() == m_live_copies_before, "live_agent_native_undo_restores_object_count");
        check(m_plater->new_project(true, true) != wxID_CANCEL, "live_agent_teardown_project");
        finish();
    }

    void mcp_wait(std::function<void()> next)
    {
        wait_until([this] { return m_mcp_client->done(); }, "mcp_response_complete", std::move(next));
    }

    void mcp_request(nlohmann::json request)
    {
        if (!m_mcp_client) m_mcp_client = std::make_unique<JusPrinTest::NativeMcpClient>(m_state->bridge);
        m_mcp_client->request(installed_shell()->agent_pane()->web_view().host().mcp()->server(), std::move(request));
    }

    nlohmann::json mcp_result() const
    {
        const auto messages = m_mcp_client->messages();
        if (messages.empty() || !messages.back().contains("result")) throw std::runtime_error("No MCP tool result");
        return messages.back()["result"];
    }

    // Both MCP modes hand the Agent pane to a real client or a person, so
    // the page has to be up before the fixture is worth preparing.
    void prepare_mcp_slice(std::function<void()> next)
    {
        wait_for_agent_page("mcp_fixture", [self = shared_from_this(), next = std::move(next)]() mutable {
            self->slice_mcp_fixture(std::move(next));
        });
    }

    void slice_mcp_fixture(std::function<void()> next)
    {
        installed_shell()->status_row()->request_slice();
        wait_until([this] {
            const auto* plate = m_plater->get_partplate_list().get_curr_plate();
            return plate && plate->is_slice_result_valid() && !m_plater->is_background_process_slicing();
        }, "mcp_fixture_has_real_slice", [self = shared_from_this(), next] {
            installed_shell()->status_row()->request_prepare();
            // The tab has to be named, not merely "not Preview": request_prepare
            // asks the notebook to select Prepare, and Notebook::SetSelection
            // abandons a vetoed change without saying so. Home satisfies "not
            // Preview" too, so a check written that way passes whether the
            // navigation happened or never ran -- and Home is where the fixture
            // was in fact being left, with the whole workspace status row (plate
            // label, review panel, return button) hidden because that row only
            // shows on Prepare or Preview.
            self->wait_until([self] {
                return self->m_notebook->GetSelection() == MainFrame::tp3DEditor && !self->m_plater->is_preview_shown();
            }, "mcp_fixture_prepare", next);
        });
    }

    void begin_mcp()
    {
        auto& view = installed_shell()->agent_pane()->web_view();
        check(view.host().mcp() != nullptr, "mcp_started_without_launch_option");
        const auto discovery = Mcp::read_discovery(view.host().mcp()->discovery_path());
        check(discovery.has_value(), "mcp_discovery_file_published");
        check(discovery && discovery->url == view.host().mcp()->server().url(), "mcp_discovery_file_matches_listener");
        check(discovery && discovery->app_version == Mcp::mcp_build_version(), "mcp_discovery_file_build_version");
        if (m_state->mcp_bridge)
            m_state->bridge = std::make_shared<JusPrinTest::StdioClient>(JUSPRIN_MCP_BRIDGE_PATH,
                                                                       view.host().mcp()->discovery_path().u8string());
        m_objects_before_tool = m_plater->model().objects.size();
        mcp_request(JusPrinTest::request("server/discover"));
        mcp_wait([self = shared_from_this()] {
            self->check(self->mcp_result()["supportedVersions"] == nlohmann::json::array({Mcp::kProtocolVersion}), "mcp_real_discovery");
            self->check(self->mcp_result()["ttlMs"] == 0 && self->mcp_result()["cacheScope"] == "private",
                        "mcp_real_discovery_cache_policy");
            self->mcp_request(JusPrinTest::request("tools/list"));
            self->mcp_wait([self] {
                const auto result = self->mcp_result();
                if (self->m_state->mcp_bridge)
                    self->check(!result.contains("ttlMs") && !result.contains("cacheScope") && !result.contains("resultType"),
                                "mcp_legacy_catalog_omits_modern_cache_fields");
                else
                    self->check(result["ttlMs"] == 0 && result["cacheScope"] == "private", "mcp_real_catalog_cache_policy");
                const auto tools = result["tools"];
                // The live catalog is the registry's MCP-exposed list, in its
                // deterministic order, and nothing else: a tool that forgets
                // its exposure shows up here as a name that moved. A page
                // holds 25; the rest follows the cursor.
                const auto exposed = Agent::ToolRegistry::instance().exposed(Agent::ToolExposure::Mcp);
                bool same = tools.size() == std::min<std::size_t>(25, exposed.size()) &&
                            result.contains("nextCursor") == (exposed.size() > 25);
                for (std::size_t index = 0; same && index < tools.size(); ++index)
                    same = tools[index]["name"] == exposed[index].get().name;
                self->check(same, "mcp_real_registry_catalog");
                self->mcp_request(JusPrinTest::request("tools/call", {{"name", "workspace_inspect"}}));
                self->mcp_wait([self] {
                    const auto result = self->mcp_result()["structuredContent"];
                    self->check(result["plateCount"] == 2 && result["objectCount"] == self->m_objects_before_tool, "mcp_real_workspace_snapshot");
                    self->mcp_settings_reads();
                });
            });
        });
    }

    void mcp_settings_reads()
    {
        mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_search"}, {"arguments", {{"scope", "process"}, {"query", "infill"}}}}));
        mcp_wait([self = shared_from_this()] {
            self->check(!self->mcp_result()["structuredContent"]["items"].empty(), "mcp_settings_search_results");
            self->mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_get"},
                {"arguments", {{"scope", "process"}, {"keys", {"layer_height", "sparse_infill_density"}}}}}));
            self->mcp_wait([self] {
                const auto result = self->mcp_result()["structuredContent"];
                self->m_settings_original = nlohmann::json::object();
                for (const auto& item : result["items"])
                    self->m_settings_original[item["key"].get<std::string>()] = item["value"];
                self->check(self->m_settings_original.size() == 2, "mcp_settings_get_two_values");
                self->m_settings_patch = {{"layer_height", "0.16"}, {"sparse_infill_density", "25%"}};
                self->mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_preview_patch"},
                    {"arguments", {{"scope", "process"}, {"changes", self->m_settings_patch}}}}));
                self->mcp_wait([self] {
                    const auto preview = self->mcp_result()["structuredContent"];
                    self->check(preview["valid"] == true && preview["changes"].size() == 2, "mcp_settings_preview_two_changes");
                    self->mcp_mutation(0);
                });
            });
        });
    }

    void mcp_mutation(int stage)
    {
        const auto snapshot = installed_shell()->workspace()->snapshot();
        mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_apply_patch"},
            {"arguments", {{"scope", "process"}, {"expectedSessionId", std::to_string(snapshot.session.value())}, {"expectedRevision", snapshot.revision},
                           {"changes", stage == 0 ? m_settings_patch : m_settings_original}}}}));
        mcp_wait([self = shared_from_this(), stage] {
            const auto result = self->mcp_result();
            const auto content = result["structuredContent"];
                self->check(result["isError"] == false && content["applied"] == true && content["changes"].size() == 2,
                            "mcp_settings_batch_succeeded");
                self->check(content["projectUndo"] == false, "mcp_settings_explain_project_undo");
            self->check(self->m_plater->model().objects.size() == self->m_objects_before_tool, "mcp_settings_preserve_objects");
            const auto expected = stage == 0 ? self->m_settings_patch : self->m_settings_original;
            for (const auto& item : installed_shell()->workspace()->read_settings({"layer_height", "sparse_infill_density"}).items)
                self->check(item.value == expected[item.key], "mcp_settings_native_value_" + item.key);
            if (stage == 0) {
                self->wait_until([self] { return !self->m_plater->get_partplate_list().get_curr_plate()->is_slice_result_valid(); },
                        "mcp_settings_invalidate_real_slice", [self] {
                            self->check(primary_print_action(installed_shell()->status_row()->action_state()).primary.action == PrintAction::Slice,
                                        "settings_change_returns_header_to_slice");
                            self->mcp_mutation(1);
                        });
            } else {
                self->mcp_teardown();
            }
        });
    }

    void mcp_teardown()
    {
        check(m_plater->model().objects.size() == m_objects_before_tool, "mcp_disconnect_changes_nothing");
        // A read remains available across a page reset with no handshake.
        auto& view = installed_shell()->agent_pane()->web_view();
        view.host().reset_page();
        mcp_request(JusPrinTest::request("tools/call", {{"name", "settings_get"}, {"arguments", {{"scope", "process"}, {"keys", {"wall_loops"}}}}}));
        mcp_wait([self = shared_from_this()] {
            self->check(self->mcp_result()["isError"] == false, "mcp_read_during_missing_handshake");
            self->m_mcp_client.reset();
            self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "mcp_teardown_project");
            self->finish();
        });
    }

    void agent_send_scripted_message()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(!web_view.bridge_error_shown(), "agent_no_bridge_error_after_handshake");
        check(web_view.host().availability() == Agent::AgentAvailability::Ready, "agent_service_ready");

        // Drive the send through the page itself so the user path (page ->
        // script message -> host) is what gets exercised.
        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest && window.__jusprinTest.send('what is on the plate?')");
        wait_until(
            [&web_view] {
                // Wait for the assistant's own message to finish. Counting
                // entries is not enough: the thread can also hold host notes,
                // which are complete the instant they are posted.
                for (const auto& message : web_view.host().conversation())
                    if (message.role == Agent::MessageRole::Assistant && message.state == Agent::MessageState::Complete)
                        return true;
                return false;
            },
            "agent_reply_streams_to_completion", [self = shared_from_this()] { self->agent_verify_reply_and_context(); });
    }

    void agent_verify_reply_and_context()
    {
        AgentWebView&     web_view = installed_shell()->agent_pane()->web_view();
        Agent::AgentHost& host     = web_view.host();

        // The thread holds turns and host notes. These checks are about the
        // exchange, so they read the turns; a separate check below covers the
        // note the earlier filament change posted.
        std::vector<Agent::ConversationMessage> turns, notes;
        for (const auto& message : host.conversation())
            (message.role == Agent::MessageRole::Note ? notes : turns).push_back(message);

        check(turns.size() == 2, "agent_conversation_has_exchange");
        check(!turns.empty() && turns.front().role == Agent::MessageRole::User, "agent_user_message_recorded");
        check(!turns.empty() && turns.back().role == Agent::MessageRole::Assistant, "agent_reply_recorded");
        if (turns.empty()) return;
        // Each menu visit earlier in this run that changed a filament posted
        // one note -- on Windows the second visit also stepped out to the
        // colour picker and came back -- and the colour-only visits and the
        // slot added and taken away posted none, because the plan did not
        // change.
#ifdef _WIN32
        const std::size_t visits = 2;
#else
        const std::size_t visits = 1;
#endif
        check(notes.size() == visits, "one_note_per_menu_visit_that_changed_a_filament");
        for (const auto& note : notes) {
            check(note.text.find("Filament is now") != std::string::npos, "the_note_names_the_new_filament");
            check(!note.swatch.empty(), "the_note_carries_its_colour");
        }
        // A note is a statement, not a turn: nothing replies to it, and it
        // never carries the streaming or failure states a turn can.
        for (const auto& note : notes) {
            check(note.state == Agent::MessageState::Complete, "filament_note_is_complete");
            check(note.in_reply_to.empty(), "filament_note_starts_no_exchange");
        }
        // The reply must describe the authoritative fixture, not canned text:
        // the two-plate fixture and its active plate contents appear in it.
        check(turns.back().text.find("2 plates") != std::string::npos, "agent_reply_describes_fixture_plates");
        check(turns.back().text.find("is active with") != std::string::npos, "agent_reply_describes_active_plate");

        // A native selection change must push fresh context over the bridge.
        const std::uint64_t sent_before = host.messages_sent();
        check(m_plater->select_object(1), "agent_native_selection_change");
        wait_until([&host, sent_before] { return host.messages_sent() > sent_before; },
                   "agent_context_pushed_on_native_selection",
                   [self = shared_from_this()] {
                       self->verify_note_rendered([self] { self->agent_verify_reload(); });
                   });
    }

    void agent_verify_reload()
    {
        AgentWebView& web_view          = installed_shell()->agent_pane()->web_view();
        const std::size_t conversation_size = web_view.host().conversation().size();

        web_view.reload();
        wait_until([&web_view] { return web_view.host().handshake_complete(); }, "agent_reload_handshake",
                   [self = shared_from_this(), conversation_size] {
                       AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                       self->check(web_view.host().conversation().size() == conversation_size,
                                   "agent_reload_preserves_native_conversation");
                       self->check(!web_view.bridge_error_shown(), "agent_reload_clears_bridge_error");
                       self->agent_tool_propose();
                   });
    }

    // Phase 3: the mock Agent duplicates the selected object through Orca's
    // own command, then undo and redo go through Orca's history.
    // Every printed copy in the real model: tool flows add instances.
    std::size_t copy_count() const
    {
        std::size_t count = 0;
        for (const ModelObject* object : m_plater->model().objects)
            count += object->instances.size();
        return count;
    }

    void agent_tool_propose()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(m_plater->select_object(0), "tool_target_selected");
        m_objects_before_tool = copy_count();
        const std::size_t activities_before = web_view.host().tools().activities().size();

        WebView::RunScript(web_view.webview(),
                           "window.__jusprinTest && window.__jusprinTest.send('please duplicate the selected object')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before &&
                       activities.back().state == Agent::ToolState::Succeeded;
            },
            "tool_run_succeeded", [self = shared_from_this()] { self->agent_tool_verify_undo();
            });
    }

    void agent_tool_verify_undo()
    {
        // The duplicate is authoritative Orca state and one Orca
        // history step.
        check(copy_count() == m_objects_before_tool + 1, "tool_duplicate_visible_in_model");
        check(m_plater->canvas3D()->get_volumes_count() >= 3, "tool_duplicate_visible_on_canvas");
        check(m_plater->can_undo_project(), "tool_change_is_undoable");
        check(m_plater->undo_project(), "tool_undo_through_orca");
        check(copy_count() == m_objects_before_tool, "tool_undo_removes_duplicate");
        check(m_plater->redo_project(), "tool_redo_through_orca");
        check(copy_count() == m_objects_before_tool + 1, "tool_redo_restores_duplicate");
        agent_conversations();
    }

    // Phase 4: conversations, project-owned persistence, save/reopen,
    // import identity, and the clean-sharing copy — all against the real
    // application and real 3MF archives.
    Agent::ProjectPersistence& persistence() { return *installed_shell()->persistence(); }

    void agent_conversations()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        check(persistence().document().has_identity(), "project_document_has_identity");
        check(persistence().document().conversations().size() == 1, "starts_with_one_conversation");
        const std::size_t conversations_before = persistence().document().conversations().size();

        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.createConversation()");
        wait_until(
            [this, conversations_before] {
                return persistence().document().conversations().size() == conversations_before + 1;
            },
            "conversation_created_via_page", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::size_t messages_before = web_view.host().conversation().size();
                self->check(messages_before == 0, "new_conversation_starts_empty");
                WebView::RunScript(web_view.webview(),
                                   "window.__jusprinTest && window.__jusprinTest.send('what changed so far?')");
                self->wait_until(
                    [&web_view] {
                        // Same reason as the first exchange: wait for the
                        // assistant's message, not for a count that a host
                        // note could also satisfy.
                        for (const auto& message : web_view.host().conversation())
                            if (message.role == Agent::MessageRole::Assistant &&
                                message.state == Agent::MessageState::Complete)
                                return true;
                        return false;
                    },
                    "second_conversation_reply_completes", [self] { self->agent_manage_chats(); });
            });
    }

    void agent_manage_chats()
    {
        const auto id = persistence().document().active_conversation_id();
        wait_until([this, id] { return !persistence().document().needs_conversation_title(id); },
                   "chat_title_generated_after_exchange", [self = shared_from_this(), id] {
            auto& view = installed_shell()->agent_pane()->web_view();
            WebView::RunScript(view.webview(), wxString::FromUTF8(
                "window.__jusprinTest.renameConversation('" + id + "', 'Backpack frame test')"));
            self->wait_until([self] { return self->persistence().document().conversations().front().title == "Backpack frame test"; },
                             "chat_renamed_via_page", [self, id] {
                const auto revision = installed_shell()->workspace()->snapshot().revision;
                auto& view = installed_shell()->agent_pane()->web_view();
                WebView::RunScript(view.webview(), "window.__jusprinTest.createConversation()");
                self->wait_until([self] { return self->persistence().document().conversations().size() == 3; },
                                 "disposable_chat_created", [self, id, revision] {
                    const auto disposable = self->persistence().document().active_conversation_id();
                    auto& view = installed_shell()->agent_pane()->web_view();
                    WebView::RunScript(view.webview(), wxString::FromUTF8(
                        "window.__jusprinTest.deleteConversation('" + disposable + "')"));
                    self->wait_until([self, disposable] {
                        return self->persistence().document().conversations().size() == 3 &&
                               self->persistence().document().active_conversation_id() != disposable;
                    },
                                     "chat_deleted_via_page", [self, id, revision] {
                        self->check(self->persistence().document().active_conversation_id() != id,
                                    "deletion_creates_fresh_active_chat");
                        self->check(installed_shell()->workspace()->snapshot().revision == revision, "chat_management_preserves_model");
                        self->agent_save_reopen();
                    });
                });
            });
        });
    }

    void agent_save_reopen()
    {
        m_saved_project_id  = persistence().document().project_id();
        m_saved_project_file = (fs::temp_directory_path() / fs::unique_path("jusprin-phase4-%%%%.3mf")).string();
        // An explicit archive export carries Orca model/settings. Managed
        // conversation history remains in the local version store.
        const auto save_started = std::chrono::steady_clock::now();
        check(m_plater->export_3mf(boost::filesystem::path(m_saved_project_file),
                                   SaveStrategy::SplitModel | SaveStrategy::ShareMesh | SaveStrategy::Silence) >= 0,
              "project_exported");
        m_save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - save_started).count();
        std::ifstream saved(m_saved_project_file, std::ios::binary);
        const std::string saved_bytes((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        m_saved_project_original_bytes = saved_bytes;
        m_saved_project_bytes = saved_bytes.size();
        check(saved_bytes.find("JusPrin/state.json") == std::string::npos, "exported_archive_excludes_managed_state");
        // JusPrin keeps no project copies of its own inside the project.
        check(saved_bytes.find(".snapshot") == std::string::npos, "saved_archive_has_no_project_copies");

        check(m_plater->new_project(true, true) != wxID_CANCEL, "phase4_new_project");
        wait_until(
            [this] {
                return persistence().document().has_identity() && persistence().document().project_id() != m_saved_project_id;
            },
            "new_project_starts_new_identity", [self = shared_from_this()] {
                self->verify_project_open_answers_dialogs();
                self->verify_step_import();
                self->verify_support_settings_patch();
                self->verify_regions();
                self->verify_reshape();
                self->verify_slice_checks([self] {
                    self->check(installed_shell()->autosave()->open_managed_project(self->m_saved_project_id),
                                "prior_local_project_reopened");
                    self->wait_until(
                        [self] { return self->persistence().document().project_id() == self->m_saved_project_id; },
                        "saved_state_adopted_on_reopen", [self] {
                            self->check(self->persistence().document().conversations().size() == 3,
                                        "saved_conversations_survive_reopen");
                            const auto chats = self->persistence().document().conversations();
                            const auto earlier = std::find_if(chats.begin(), chats.end(), [](const auto& chat) {
                                return chat.title == "Backpack frame test";
                            });
                            self->check(earlier != chats.end(),
                                        "renamed_chat_survives_project_reopen");
                            self->check(earlier != chats.end() &&
                                        !self->persistence().document().messages(earlier->id).empty(),
                                        "saved_messages_survive_reopen");
                            self->agent_phase6_history();
                        });
                });
            });
    }

    void verify_external_project_history()
    {
        auto* autosave = installed_shell()->autosave();
        const fs::path source = fs::path(data_dir()) / "previously-imported.3mf";
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        check(m_plater->load_files(std::vector<std::string>{cube},
              LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
              "external_3mf_fixture_loaded");
        check(m_plater->export_3mf(source, SaveStrategy::SplitModel | SaveStrategy::ShareMesh | SaveStrategy::Silence) >= 0,
              "external_3mf_fixture_exported");
        check(m_plater->new_project(true, true) != wxID_CANCEL, "external_3mf_first_draft_created");
        m_plater->load_project(wxString::FromUTF8(source.string()), "<loadall>");
        check(autosave->has_local_project_for_source(std::filesystem::path(source.string())),
              "external_3mf_live_source_found_before_save");
        check(autosave->save_now() && m_plater->model().objects.size() == 1,
              "external_3mf_first_import_saved");
        const std::string original_id = persistence().document().project_id();
        check(autosave->has_local_project_for_source(std::filesystem::path(source.string())),
              "external_3mf_source_index_found");
        check(m_plater->new_project(true, true) != wxID_CANCEL, "external_3mf_second_draft_created");
        const std::string draft_id = persistence().document().project_id();
        wxGetApp().app_config->set(SETTING_PROJECT_LOAD_BEHAVIOUR, OPTION_PROJECT_LOAD_BEHAVIOUR_LOAD_ALL);
        int prompt_count = 0;
        auto decision = ShellController::ReimportDecision::Cancel;
        installed_shell()->set_reimport_confirmation([&] {
            ++prompt_count;
            return decision;
        });
        const bool cancelled = m_plater->open_3mf_file(source);
        check(cancelled && prompt_count == 1 && persistence().document().project_id() == draft_id,
              "external_3mf_reopen_cancel_keeps_draft");
        decision = ShellController::ReimportDecision::OpenExisting;
        const bool deferred = m_plater->open_3mf_file(source);
        check(deferred && prompt_count == 2 && persistence().document().project_id() == original_id &&
                  m_plater->model().objects.size() == 1 && m_notebook->GetSelection() == MainFrame::tp3DEditor,
              "external_3mf_reopen_opens_existing_project_directly");
        check(m_plater->new_project(true, true) != wxID_CANCEL, "external_3mf_new_draft_after_reopen");
        const std::string next_draft_id = persistence().document().project_id();
        decision = ShellController::ReimportDecision::CreateNew;
        const bool opened = m_plater->open_3mf_file(source);
        check(opened && prompt_count == 3 && persistence().document().project_id() != original_id &&
                  persistence().document().project_id() != draft_id &&
                  persistence().document().project_id() != next_draft_id,
              "external_3mf_new_project_requires_confirmation");

        check(autosave->save_now(), "history_fixture_baseline_saved");
        const std::string newest_id = persistence().document().project_id();
        check(m_plater->new_project(true, true) != wxID_CANCEL, "external_3mf_third_draft_created");
        decision = ShellController::ReimportDecision::OpenExisting;
        const bool reopened = m_plater->open_3mf_file(source);
        installed_shell()->set_reimport_confirmation({});
        check(reopened && prompt_count == 4 && persistence().document().project_id() == newest_id,
              "external_3mf_multiple_matches_open_most_recent_project");
        check(m_plater->duplicate_object(0) >= 0 && autosave->save_now(),
              "history_fixture_second_model_version_saved");
        const auto versions = autosave->history();
        check(versions.size() >= 2, "history_fixture_has_older_version");
        if (!versions.empty() && !persistence().document().changes().empty())
            check(versions.back().change_seq == persistence().document().changes().back().seq,
                  "history_saved_version_links_to_last_captured_change");
        if (versions.size() >= 2) {
            const std::string latest = autosave->current_version();
            const std::size_t objects = m_plater->model().objects.size();
            // Version history lives on the Project tab. Newest first, so the
            // first Restore button is the next-older version's.
            LeftPane* pane = installed_shell()->left_pane();
            pane->show_project();
            pane->open_project_view(ProjectView::Versions);
            pane->snapshot();
            const auto find_restore_label = [pane]() -> std::optional<wxString> {
                for (const wxString& label : pane->action_labels())
                    if (label.StartsWith(_L("Restore") + " "))
                        return label;
                return std::nullopt;
            };
            std::optional<wxString> restore_label = find_restore_label();
            check(restore_label.has_value(), "history_older_timestamp_visible");
            if (restore_label) {
                save_pane("version-history-row", pane->snapshot());
                // A click on the time, left of the button, restores nothing.
                const wxRect button = pane->action_rect(*restore_label);
                click_pane(pane, wxRect(button.x - 60, button.y, 2, button.height));
                wxYield();
                check(autosave->current_version() == latest && m_plater->model().objects.size() == objects,
                      "history_timestamp_click_does_not_restore");
                int restore_prompt_count = 0;
                bool restore_answer = false;
                pane->set_restore_confirmation([&] {
                    ++restore_prompt_count;
                    return restore_answer;
                });
                check(pane->press_action(*restore_label), "history_older_version_has_restore_button");
                wxYield();
                check(restore_prompt_count == 1 && autosave->current_version() == latest &&
                          m_plater->model().objects.size() == objects,
                      "history_restore_cancel_keeps_model");
                pane->snapshot();
                restore_label = find_restore_label();
                const bool restore_button_found = restore_label.has_value();
                if (restore_button_found) {
                    restore_answer = true;
                    pane->press_action(*restore_label);
                    wxYield();
                }
                pane->set_restore_confirmation({});
                check(restore_button_found && restore_prompt_count == 2 &&
                          m_plater->model().objects.size() == versions[versions.size() - 2].objects.size(),
                      "history_restore_confirm_reloads_older_model");
            }
        }
        const wxString expected_title = wxString::FromUTF8("previously-imported \xE2\x80\x94 ") +
                                        wxGetTranslation("Saved");
        wait_until([this, expected_title] { return m_frame->GetTitle() == expected_title; },
                   "external_3mf_window_title_shows_local_save_state",
                   [self = shared_from_this()] { self->finish(); });
    }

    // Counts every dialog that becomes visible, other than the load progress
    // window, while it is installed.
    struct DialogCounter : wxEventFilter
    {
        int  shown = 0;
        std::vector<std::string> titles;
        DialogCounter() { wxEvtHandler::AddFilter(this); }
        ~DialogCounter() override { wxEvtHandler::RemoveFilter(this); }
        int FilterEvent(wxEvent& event) override
        {
            if (event.GetEventType() == wxEVT_SHOW) {
                auto* dialog = dynamic_cast<wxDialog*>(event.GetEventObject());
                // load_files' progress window asks nothing; it is titled, not typed.
                if (dialog != nullptr && static_cast<wxShowEvent&>(event).IsShown() &&
                    !dialog->GetTitle().StartsWith(wxGetTranslation("Loading"))) {
                    ++shown;
                    titles.push_back(dialog->GetTitle().ToUTF8().data());
                }
            }
            return Event_Skip;
        }
    };

    // project_open through the real adapter: a model saved in metres makes
    // Orca ask "Object too small", which the file listener answers No and
    // records, and a dirty project is dropped without Orca's save prompt.
    // The same file brought in the person's own way (Import) reaches the load
    // report listener instead, with the same message. No dialog may reach
    // the screen.
    void verify_project_open_answers_dialogs()
    {
        const fs::path tiny = fs::temp_directory_path() / fs::unique_path("jusprin-metres-%%%%.stl");
        {
            // A 20 mm cube written in metres.
            std::ofstream out(tiny.string());
            out << "solid tiny" << std::endl;
            const double s = 0.02;
            const double v[8][3] = {{0,0,0},{s,0,0},{s,s,0},{0,s,0},{0,0,s},{s,0,s},{s,s,s},{0,s,s}};
            const int f[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
            for (const auto& t : f) {
                out << "facet normal 0 0 0" << std::endl << "outer loop" << std::endl;
                for (int i : t) out << "vertex " << v[i][0] << " " << v[i][1] << " " << v[i][2] << std::endl;
                out << "endloop" << std::endl << "endfacet" << std::endl;
            }
            out << "endsolid tiny" << std::endl;
        }
        // Orca's too-small question is the only one here that offers Yes and
        // No; it is recognised by its buttons, never by its words.
        const auto asked_yes_no_answered_no = [](const Workspace::LoadReport& report) {
            for (const auto& load : report.loads)
                for (const auto& message : load.messages) {
                    const auto offers = [&message](const char* button) {
                        return std::find(message.buttons.begin(), message.buttons.end(), button) != message.buttons.end();
                    };
                    if (offers("yes") && offers("no") && message.answer == "no")
                        return true;
                }
            return false;
        };
        const auto print_messages = [](const char* where, const Workspace::LoadReport& report) {
            for (const auto& load : report.loads)
                for (const auto& message : load.messages)
                    std::cout << where << " message: " << message.title << " | " << message.text << " -> " << message.answer << std::endl;
        };

        auto* workspace = installed_shell()->workspace();
        Workspace::ProjectOpenRequest request;
        request.path            = tiny.string();
        request.discard_unsaved = true;
        Workspace::LoadReport report;
        {
            DialogCounter counter;
            check(workspace->open_project(request, report).succeeded(), "project_open_model_file");
            for (const auto& title : counter.titles) std::cout << "project_open dialog shown: " << title << std::endl;
            check(counter.shown == 0, "project_open_model_shows_no_dialog");
        }
        print_messages("project_open", report);
        check(asked_yes_no_answered_no(report), "project_open_records_object_too_small");
        check(report.started_by_agent && report.project_opened, "project_open_report_belongs_to_the_tool");
        const BoundingBoxf3 box = m_plater->model().objects.empty() ? BoundingBoxf3() : m_plater->model().objects.front()->bounding_box_exact();
        check(m_plater->model().objects.size() == 1 && box.size().x() < 1., "project_open_leaves_metres_model_as_it_was");

        // The person's own way in: the same file through Import. Its report
        // arrives a turn of the event loop after the load.
        std::optional<Workspace::LoadReport> heard;
        workspace->set_load_report_listener([&heard](const Workspace::LoadReport& delivered) { heard = delivered; });
        {
            DialogCounter counter;
            m_plater->add_model(false, tiny.string());
            for (const auto& title : counter.titles) std::cout << "import dialog shown: " << title << std::endl;
            check(counter.shown == 0, "user_import_shows_no_dialog");
        }
        wxTheApp->ProcessPendingEvents();
        check(heard.has_value(), "user_import_reported");
        if (heard) {
            print_messages("user import", *heard);
            check(!heard->started_by_agent && !heard->project_opened, "user_import_report_is_the_persons");
            check(asked_yes_no_answered_no(*heard), "user_import_records_object_too_small");
            check(heard->objects.size() == 1, "user_import_reports_what_arrived");
        }

        // This refusal happens before Orca enters the inner model loader.
        // The outer operation must still collect it and deliver a report.
        heard.reset();
        wxArrayString mixed;
        mixed.Add(from_u8(tiny.string()));
        mixed.Add(from_u8((fs::temp_directory_path() / "sample.gcode").string()));
        {
            DialogCounter counter;
            check(!m_plater->load_files(mixed), "mixed_files_refused");
            check(counter.shown == 0, "preload_refusal_shows_no_dialog");
        }
        wxTheApp->ProcessPendingEvents();
        check(heard.has_value() && heard->has_messages(), "preload_refusal_reported");
        if (heard)
            check(heard->objects.empty(), "preload_refusal_added_nothing");

        // Orca's project-versus-geometry choice is also before the inner
        // loader. Its automatic Cancel must leave the workspace untouched.
        const std::string previous_load_behaviour = wxGetApp().app_config->get(SETTING_PROJECT_LOAD_BEHAVIOUR);
        wxGetApp().app_config->set(SETTING_PROJECT_LOAD_BEHAVIOUR, OPTION_PROJECT_LOAD_BEHAVIOUR_ALWAYS_ASK);
        heard.reset();
        const std::size_t objects_before_choice = m_plater->model().objects.size();
        {
            DialogCounter counter;
            check(!m_plater->open_3mf_file(fs::path(std::string(JUSPRIN_SOURCE_DIR) +
                                                 "/tests/data/jusprin/prusaslicer_cube.3mf")),
                  "preload_project_choice_cancelled");
            check(counter.shown == 0, "preload_project_choice_shows_no_dialog");
        }
        wxGetApp().app_config->set(SETTING_PROJECT_LOAD_BEHAVIOUR, previous_load_behaviour);
        wxTheApp->ProcessPendingEvents();
        check(heard.has_value() && heard->has_messages(), "preload_project_choice_reported");
        check(m_plater->model().objects.size() == objects_before_choice, "preload_project_choice_kept_workspace");

        // The case people see most: a PrusaSlicer 3MF (a Printables download)
        // opened the person's way, through the same load_project Home's
        // recent-project open calls. Orca's "not supported, loading geometry
        // data only" box is answered OK and reaches the report, not the screen.
        check(m_plater->new_project(true, true) != wxID_CANCEL, "prusa_open_starts_clean");
        heard.reset();
        {
            DialogCounter counter;
            m_plater->load_project(from_u8(std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/prusaslicer_cube.3mf"), "-");
            for (const auto& title : counter.titles) std::cout << "prusa open dialog shown: " << title << std::endl;
            check(counter.shown == 0, "prusa_open_shows_no_dialog");
        }
        wxTheApp->ProcessPendingEvents();
        check(heard.has_value() && heard->project_opened && !heard->started_by_agent, "prusa_open_reported_as_the_persons");
        if (heard) {
            print_messages("prusa open", *heard);
            bool notice = false;
            for (const auto& load : heard->loads)
                for (const auto& message : load.messages)
                    notice = notice || (message.buttons == std::vector<std::string>{"ok"} && message.answer == "ok" &&
                                        message.recognized && !message.text.empty());
            check(notice, "prusa_open_records_the_settings_notice");
            check(!heard->loads.empty() && heard->loads.front().with_settings, "prusa_open_was_asked_for_settings");
            check(heard->objects.size() == 1, "prusa_open_reports_what_arrived");
        }
        // Back to the shell's own wiring.
        workspace->set_load_report_listener([pane = wxWeakRef<AgentPane>(installed_shell()->agent_pane())](const Workspace::LoadReport& delivered) {
            if (pane) pane->web_view().host().on_file_loaded(delivered);
        });

        // Make the project dirty, then reopen the saved project over it.
        check(m_plater->duplicate_object(0) >= 0, "project_open_dirty_before_reopen");
        const auto objects_before_refusal = m_plater->model().objects.size();
        heard.reset();
        workspace->set_load_report_listener([&heard](const Workspace::LoadReport& delivered) { heard = delivered; });
        {
            DialogCounter counter;
            m_plater->load_project(from_u8(m_saved_project_file), "-");
            check(counter.shown == 0, "dirty_user_open_shows_no_dialog");
        }
        wxTheApp->ProcessPendingEvents();
        check(heard.has_value() && heard->has_messages(), "dirty_user_open_reported");
        check(m_plater->model().objects.size() == objects_before_refusal, "dirty_user_open_kept_existing_model");
        workspace->set_load_report_listener([pane = wxWeakRef<AgentPane>(installed_shell()->agent_pane())](const Workspace::LoadReport& delivered) {
            if (pane) pane->web_view().host().on_file_loaded(delivered);
        });
        request      = {};
        request.path = m_saved_project_file;
        report       = {};
        check(!workspace->open_project(request, report).succeeded(), "project_open_refuses_unsaved_work");
        request.discard_unsaved = true;
        {
            DialogCounter counter;
            const auto opened = workspace->open_project(request, report);
            if (!opened.succeeded()) std::cout << "project_open failed: " << opened.message << std::endl;
            check(opened.succeeded(), "project_open_project_file");
            for (const auto& title : counter.titles) std::cout << "project_open dialog shown: " << title << std::endl;
            check(counter.shown == 0, "project_open_project_shows_no_dialog");
        }
        print_messages("reopen", report);
        // The unsaved work was dropped before the load, so Orca never asked
        // to save it (its prompt offers Yes, No and Cancel).
        bool asked_to_save = false;
        for (const auto& load : report.loads)
            for (const auto& message : load.messages)
                asked_to_save = asked_to_save || message.buttons == std::vector<std::string>{"yes", "no", "cancel"};
        check(!asked_to_save, "project_open_drops_unsaved_work_without_asking");
        fs::remove(tiny);
    }

    // M4's settings through the real adapter: a tree-support patch on the
    // build plate with a brim reaches the support page's fields and asks
    // nothing; a support style that does not fit is refused; an object
    // override lands in the object's ModelConfig as one undo step.
    void verify_support_settings_patch()
    {
        auto* workspace = installed_shell()->workspace();
        auto* tab       = wxGetApp().get_tab(Preset::TYPE_PRINT);
        auto& prints    = wxGetApp().preset_bundle->prints;
        const DynamicPrintConfig original = prints.get_edited_preset().config;
        tab->activate_option("enable_support", "Support");
        const Workspace::SettingsPatch supports{{{"enable_support", "1"}, {"support_type", "tree(auto)"},
                                                 {"support_style", "organic"}, {"support_on_build_plate_only", "1"},
                                                 {"brim_type", "outer_only"}, {"brim_width", "6"}}};
        {
            DialogCounter counter;
            const auto preview = workspace->preview_settings(supports);
            for (const auto& issue : preview.issues) std::cout << "support patch issue: " << issue.key << " " << issue.message << std::endl;
            check(preview.valid, "settings_support_patch_valid");
            Workspace::SettingsPreview applied;
            check(workspace->apply_settings(supports, Workspace::previewed_changes(preview), applied).succeeded(),
                  "settings_support_patch_applies");
            for (const auto& title : counter.titles) std::cout << "settings dialog shown: " << title << std::endl;
            check(counter.shown == 0, "settings_support_patch_shows_no_dialog");
        }
        const auto& config = prints.get_edited_preset().config;
        for (const auto& [key, value] : supports.changes)
            check(config.option(key)->serialize() == value, "settings_support_value_" + key);
        for (const char* key : {"enable_support", "support_on_build_plate_only"}) {
            Field* shown = tab->get_field(key);
            check(shown != nullptr && boost::any_cast<bool>(shown->get_value()), std::string("settings_support_field_shows_") + key);
        }
        const auto mismatch = workspace->preview_settings({{{"support_style", "grid"}}});
        check(!mismatch.valid && mismatch.issues.front().key == "support_style" &&
                  std::count(mismatch.issues.front().allowed.begin(), mismatch.issues.front().allowed.end(), "organic") == 1,
              "settings_support_style_mismatch_refused");
        check(workspace->preview_settings({{{"support_style", "tree_strong"}}}).valid, "settings_support_style_fitting_accepted");
        check(workspace->preview_settings({{{"support_on_build_plate_only", "0"}}}).valid, "settings_boolean_has_no_bounds");
        Workspace::SettingsQuery changed;
        changed.limit        = 25;
        changed.changed_only = true;
        const auto unsaved   = workspace->search_settings(changed);
        check(std::any_of(unsaved.items.begin(), unsaved.items.end(), [](const auto& item) { return item.key == "brim_width"; }) &&
                  std::none_of(unsaved.items.begin(), unsaved.items.end(), [](const auto& item) { return item.key == "layer_height"; }),
              "settings_search_changed_only");

        // One object's override.
        const auto snapshot = workspace->snapshot();
        check(!snapshot.plates.empty() && !snapshot.plates[0].objects.empty(), "settings_object_present");
        if (snapshot.plates.empty() || snapshot.plates[0].objects.empty()) return;
        Workspace::SettingsTarget target{Workspace::SettingsScope::Object, snapshot.plates[0].objects[0].id};
        const ModelObject* object = m_plater->model().objects.front();
        const Workspace::SettingsPatch walls{{{"wall_loops", "5"}}, target};
        {
            DialogCounter counter;
            const auto preview = workspace->preview_settings(walls);
            check(preview.valid, "settings_object_patch_valid");
            Workspace::SettingsPreview applied;
            check(workspace->apply_settings(walls, Workspace::previewed_changes(preview), applied).succeeded(),
                  "settings_object_patch_applies");
            check(counter.shown == 0, "settings_object_patch_shows_no_dialog");
        }
        check(object->config.has("wall_loops") && object->config.opt_int("wall_loops") == 5, "settings_object_override_in_model");
        check(config.opt_int("wall_loops") == original.opt_int("wall_loops"), "settings_object_leaves_process_alone");
        const auto read = workspace->read_settings({"wall_loops"}, target);
        check(read.items.size() == 1 && read.items[0].value == "5" && read.items[0].overridden == true, "settings_object_read_back");
        check(workspace->snapshot().can_undo, "settings_object_is_undoable");
        check(workspace->preview_settings({{{"skirt_loops", "2"}}, target}).issues.front().code == "unsupported_scope",
              "settings_object_refuses_print_scope");
        m_plater->undo();
        check(!object->config.has("wall_loops"), "settings_object_undo_removes_override");
        tab->load_config(original);
    }

    // A STEP file through the real adapter: Orca asks how finely to mesh it,
    // and the request takes Orca's own defaults without showing the dialog.
    void verify_step_import()
    {
        auto* workspace = installed_shell()->workspace();
        const std::size_t before = m_plater->model().objects.size();
        Workspace::ImportRequest import;
        import.path = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/box_20x15x10.step";
        Workspace::LoadReport                 report;
        std::vector<Workspace::ObjectId>     added;
        {
            DialogCounter counter;
            const auto imported = workspace->import_objects(import, report, added);
            if (!imported.succeeded()) std::cout << "step import failed: " << imported.message << std::endl;
            check(imported.succeeded() && added.size() == 1, "step_import_succeeds");
            for (const auto& title : counter.titles) std::cout << "step import dialog shown: " << title << std::endl;
            check(counter.shown == 0, "step_import_shows_no_dialog");
        }
        for (const auto& load : report.loads)
            for (const auto& message : load.messages) std::cout << "step import message: " << message.title << " -> " << message.answer << std::endl;
        check(m_plater->model().objects.size() == before + 1, "step_import_adds_one_object");
        if (m_plater->model().objects.size() == before + 1) {
            const Vec3d size = m_plater->model().objects.back()->bounding_box_exact().size();
            check(std::abs(size.x() - 20.) < 0.01 && std::abs(size.y() - 15.) < 0.01 && std::abs(size.z() - 10.) < 0.01,
                  "step_import_keeps_the_size");
            check(workspace->undo().succeeded() && m_plater->model().objects.size() == before, "step_import_undone");
        }
    }

    // Regions through the real adapter, on a T bar with a 10 mm hole through
    // it: a precision hole becomes a support blocker filling the hole, a face
    // becomes seam paint; Undo strands the record, Redo restores it, a turn
    // keeps it bound, and removal takes the artifacts away.
    void verify_regions()
    {
        auto* workspace = installed_shell()->workspace();
        Workspace::LoadReport                 report;
        std::vector<Workspace::ObjectId>     added;
        Workspace::ImportRequest             import;
        import.path = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/tee_with_hole.stl";
        check(workspace->import_objects(import, report, added).succeeded() && added.size() == 1, "regions_fixture_imported");
        if (added.size() != 1) return;
        const Workspace::ObjectId tee = added.front();
        ModelObject* object = nullptr;
        for (ModelObject* candidate : m_plater->model().objects)
            if (candidate->id().id == tee.value()) object = candidate;

        Workspace::AnalysisRequest wanted;
        wanted.features = true;
        Workspace::ObjectAnalysis analysis;
        check(workspace->analyze_object(tee, wanted, analysis).succeeded() && analysis.features, "regions_features_read");
        if (!analysis.features) return;
        const auto hole = std::find_if(analysis.features->holes.begin(), analysis.features->holes.end(),
                                       [](const Workspace::HoleFeature& h) { return std::abs(h.diameter - 10) < 0.2; });
        const auto top = std::find_if(analysis.features->faces.begin(), analysis.features->faces.end(),
                                      [](const Workspace::FaceFeature& f) { return f.normal[2] > 0.99 && f.area > 900; });
        check(hole != analysis.features->holes.end(), "regions_hole_found");
        check(top != analysis.features->faces.end(), "regions_top_face_found");
        if (hole == analysis.features->holes.end() || top == analysis.features->faces.end()) return;

        std::vector<Workspace::RegionRequest> requests(2);
        requests[0].object   = tee;
        requests[0].kind     = "precision_hole";
        requests[0].geometry = Workspace::RegionGeometry{"hole", hole->handle};
        requests[1].object   = tee;
        requests[1].kind     = "seam_preferred";
        requests[1].geometry = Workspace::RegionGeometry{"face", top->handle};
        std::vector<Workspace::RegionRecord> planned, applied;
        const auto plan = workspace->plan_regions(requests, {}, planned);
        if (!plan.succeeded()) std::cout << "regions plan: " << plan.message << std::endl;
        check(plan.succeeded() && planned.size() == 2, "regions_planned");
        if (planned.size() != 2) return;
        std::cout << "regions: " << planned[0].label << " | " << planned[1].label << std::endl;
        check(std::abs(planned[0].geometry.length - 20) < 0.05, "regions_hole_depth_is_through");
        check(planned[1].artifacts.size() == 1 && !planned[1].artifacts[0].facets.empty(), "regions_face_facets_resolved");
        const std::size_t volumes_before = object->volumes.size();
        {
            DialogCounter counter;
            check(workspace->apply_regions(planned, {}, applied).succeeded(), "regions_applied");
            check(counter.shown == 0, "regions_show_no_dialog");
        }
        check(object->volumes.size() == volumes_before + 1 && object->volumes.back()->is_support_blocker() &&
                  object->volumes.back()->name == "JusPrin r1 support blocker",
              "regions_blocker_added");
        // The blocker fills the hole and a millimetre around it: 12 mm across,
        // 22 mm along Y, centred on the hole's axis in the object's frame.
        const BoundingBoxf3 blocker = object->volumes.back()->mesh().transformed_bounding_box(object->volumes.back()->get_matrix());
        const BoundingBoxf3 part    = object->volumes.front()->mesh().transformed_bounding_box(object->volumes.front()->get_matrix());
        std::cout << "blocker size " << blocker.size().transpose() << " centre " << blocker.center().transpose()
                  << " part centre " << part.center().transpose() << std::endl;
        check(std::abs(blocker.size().y() - 22) < 0.1 && std::abs(blocker.size().x() - 12) < 0.2 &&
                  std::abs(blocker.size().z() - 12) < 0.2,
              "regions_blocker_fills_the_hole");
        check(std::abs(blocker.center().x() - part.center().x()) < 0.1 && std::abs(blocker.center().y() - part.center().y()) < 0.1 &&
                  std::abs(blocker.center().z() - (part.min.z() + 28)) < 0.1,
              "regions_blocker_on_the_hole_axis");
        check(object->volumes.front()->seam_facets.has_facets(*object->volumes.front(), EnforcerBlockerType::ENFORCER),
              "regions_seam_painted");

        auto status = [&] { return workspace->region_status(applied); };
        auto all = [](const std::vector<Workspace::RegionStatus>& statuses, bool Workspace::RegionStatus::*field, bool value) {
            return std::all_of(statuses.begin(), statuses.end(), [&](const auto& s) { return s.object && s.*field == value; });
        };
        check(all(status(), &Workspace::RegionStatus::binding_lost, false) && all(status(), &Workspace::RegionStatus::artifacts_missing, false),
              "regions_status_bound_and_present");
        m_plater->undo();
        check(all(status(), &Workspace::RegionStatus::artifacts_missing, true) && all(status(), &Workspace::RegionStatus::binding_lost, false),
              "regions_undo_strands_the_record");
        m_plater->redo();
        check(all(status(), &Workspace::RegionStatus::artifacts_missing, false), "regions_redo_restores_artifacts");

        // Turning the object is not a mesh change.
        Workspace::PlacementRequest turn;
        turn.rotate = Workspace::Vec3{0, 0, 90};
        Workspace::PlacementResult turned;
        check(workspace->place_object(tee, turn, "regions-turn", turned).succeeded(), "regions_object_turned");
        check(all(status(), &Workspace::RegionStatus::binding_lost, false) && all(status(), &Workspace::RegionStatus::artifacts_missing, false),
              "regions_survive_a_turn");

        check(workspace->remove_regions(applied).succeeded(), "regions_removed");
        check(object->volumes.size() == volumes_before &&
                  !object->volumes.front()->seam_facets.has_facets(*object->volumes.front(), EnforcerBlockerType::ENFORCER),
              "regions_removal_takes_the_artifacts");
        check(all(status(), &Workspace::RegionStatus::artifacts_missing, true), "regions_status_after_removal");
        check(workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, tee}}).succeeded(), "regions_fixture_removed");
    }

    // Reshaping through the real adapter: a preview that changes nothing, a
    // plane cut, a merge of the pieces, a split back into shells, and a
    // repair of a mesh with a hole in it, none of which may ask anything.
    void verify_reshape()
    {
        auto* workspace = installed_shell()->workspace();
        auto import_file = [&](const std::string& path) -> std::optional<Workspace::ObjectId> {
            Workspace::LoadReport                 report;
            std::vector<Workspace::ObjectId>     added;
            Workspace::ImportRequest             import;
            import.path = path;
            if (!workspace->import_objects(import, report, added).succeeded() || added.size() != 1)
                return std::nullopt;
            return added.front();
        };
        const auto tee = import_file(std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/tee_with_hole.stl");
        check(tee.has_value(), "reshape_fixture_imported");
        if (!tee) return;
        const auto count = [this] { return m_plater->model().objects.size(); };
        const std::size_t objects_before = count();

        // The bar is 36 mm tall and centred; cut it through the stem.
        const BoundingBoxf3 box = m_plater->model().objects.back()->instance_bounding_box(0);
        Workspace::DivideRequest cut;
        cut.point  = {box.center().x(), box.center().y(), box.min.z() + 10};
        cut.normal = {0, 0, 1};
        Workspace::DivideResult preview;
        const auto revision = workspace->snapshot().revision;
        const auto steps    = m_plater->undo_redo_stack_main().snapshots().size();
        const bool dirty    = m_plater->is_project_dirty();
        {
            DialogCounter counter;
            check(workspace->preview_divide(*tee, cut, preview).succeeded() && preview.pieces.size() == 2, "reshape_preview_two_pieces");
            check(counter.shown == 0, "reshape_preview_shows_no_dialog");
        }
        for (const auto& piece : preview.pieces)
            std::cout << "preview piece " << piece.name << " size " << piece.size[0] << " " << piece.size[1] << " " << piece.size[2]
                      << " overhang " << piece.overhang_area << std::endl;
        std::cout << "overhang before " << preview.overhang_area_before << std::endl;
        check(count() == objects_before && workspace->snapshot().revision == revision &&
                  m_plater->undo_redo_stack_main().snapshots().size() == steps && m_plater->is_project_dirty() == dirty,
              "reshape_preview_changes_nothing");
        check(preview.overhang_area_before > 500 && preview.pieces.size() == 2 &&
                  std::abs(preview.pieces[0].size[2] + preview.pieces[1].size[2] - 36) < 0.2,
              "reshape_preview_heights_add_up");

        Workspace::DivideResult divided;
        {
            DialogCounter counter;
            check(workspace->divide_object(*tee, cut, divided).succeeded(), "reshape_cut");
            check(counter.shown == 0, "reshape_cut_shows_no_dialog");
        }
        check(count() == objects_before + 1 && divided.pieces.size() == 2 && divided.pieces[0].object && divided.pieces[1].object,
              "reshape_cut_makes_two_objects");
        if (divided.pieces.size() != 2 || !divided.pieces[0].object || !divided.pieces[1].object) return;

        Workspace::ObjectId merged;
        {
            DialogCounter counter;
            check(workspace->merge_objects({*divided.pieces[0].object, *divided.pieces[1].object}, merged).succeeded(), "reshape_merge");
            check(counter.shown == 0, "reshape_merge_shows_no_dialog");
        }
        check(count() == objects_before && m_plater->model().objects.back()->volumes.size() == 2, "reshape_merge_makes_one_object_of_two_parts");

        Workspace::DivideRequest shells;
        shells.mode = Workspace::DivideRequest::Mode::Shells;
        Workspace::DivideResult split;
        check(workspace->divide_object(merged, shells, split).succeeded() && split.pieces.size() == 2 && count() == objects_before + 1,
              "reshape_split_to_objects");
        for (const auto& piece : split.pieces)
            if (piece.object)
                workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, *piece.object}});

        // A 20 mm cube with one facet missing.
        const fs::path open = fs::temp_directory_path() / fs::unique_path("jusprin-open-%%%%.stl");
        {
            std::ofstream out(open.string());
            out << "solid open" << std::endl;
            const double s = 20;
            const double v[8][3] = {{0,0,0},{s,0,0},{s,s,0},{0,s,0},{0,0,s},{s,0,s},{s,s,s},{0,s,s}};
            const int f[11][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4}};
            for (const auto& tri : f) {
                out << "facet normal 0 0 0" << std::endl << "outer loop" << std::endl;
                for (int i : tri) out << "vertex " << v[i][0] << " " << v[i][1] << " " << v[i][2] << std::endl;
                out << "endloop" << std::endl << "endfacet" << std::endl;
            }
            out << "endsolid open" << std::endl;
        }
        const auto broken = import_file(open.string());
        check(broken.has_value(), "reshape_open_mesh_imported");
        if (broken) {
            Workspace::RepairResult repaired;
            {
                DialogCounter counter;
                check(workspace->repair_object(*broken, repaired).succeeded(), "reshape_repair");
                check(counter.shown == 0, "reshape_repair_shows_no_dialog");
            }
            std::cout << "repair open edges " << repaired.open_edges_before << " -> " << repaired.open_edges_after << ", facets "
                      << repaired.facets_before << " -> " << repaired.facets_after << std::endl;
            check(repaired.changed && repaired.open_edges_before > 0 && repaired.open_edges_after == 0, "reshape_repair_closes_the_mesh");
            Workspace::RepairResult again;
            check(workspace->repair_object(*broken, again).succeeded() && !again.changed, "reshape_repair_of_a_closed_mesh_changes_nothing");
            workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, *broken}});
        }
        fs::remove(open);
    }

    // The slice checks on a real slice: the T bar with supports on prints
    // support inside its hole, which the report finds without any region;
    // once the hole is a precision hole and the back face is hidden, a new
    // slice keeps support out of the hole and puts seams on that face.
    void verify_slice_checks(std::function<void()> then)
    {
        auto* workspace = installed_shell()->workspace();
        auto& prints    = wxGetApp().preset_bundle->prints;
        m_slice_check_settings = prints.get_edited_preset().config;
        Workspace::LoadReport                 load_report;
        std::vector<Workspace::ObjectId>     added;
        Workspace::ImportRequest             import;
        import.path = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/tee_with_hole.stl";
        const bool imported = workspace->import_objects(import, load_report, added).succeeded() && added.size() == 1;
        check(imported, "slice_checks_fixture_imported");
        const Workspace::SettingsPatch supports{{{"enable_support", "1"}, {"support_type", "normal(auto)"},
                                                 {"support_on_build_plate_only", "0"}}};
        Workspace::SettingsPreview applied;
        check(workspace->apply_settings(supports, Workspace::previewed_changes(workspace->preview_settings(supports)), applied).succeeded(),
              "slice_checks_supports_on");
        const auto plate = workspace->snapshot().active_plate;
        if (!imported || !plate) {
            then();
            return;
        }
        m_slice_check_object = added.front();
        slice_then("slice_checks_first_slice", *plate, [self = shared_from_this(), plate, then] {
            auto* workspace = installed_shell()->workspace();
            Workspace::SliceReportRequest request;
            request.supports = request.seams = request.first_layer = request.islands = true;
            const auto report = workspace->slice_report(*plate, request);
            self->check(report.valid && report.supports && report.supports->generated, "slice_checks_supports_generated");
            const auto hole_contact = [self](const Workspace::SliceReport& r) {
                return r.supports && std::any_of(r.supports->contacts.begin(), r.supports->contacts.end(), [&](const auto& c) {
                           return c.object && *c.object == self->m_slice_check_object;
                       });
            };
            if (report.supports)
                for (const auto& contact : report.supports->contacts)
                    std::cout << "support contact " << contact.object_name << " " << contact.target << " " << contact.area
                              << " mm2 over " << contact.layers << " layers" << std::endl;
            self->check(hole_contact(report), "slice_checks_support_enters_the_hole");
            self->check(report.first_layer && std::any_of(report.first_layer->objects.begin(), report.first_layer->objects.end(),
                                                          [&](const auto& o) { return o.object && *o.object == self->m_slice_check_object &&
                                                                                      std::abs(o.contact_area - 320) < 20; }),
                        "slice_checks_first_layer_is_the_stem");
            self->check(report.islands && report.seams && report.seams->count > 0, "slice_checks_islands_and_seams_read");

            // Annotate the hole and the back face.
            Workspace::AnalysisRequest wanted;
            wanted.features = true;
            Workspace::ObjectAnalysis analysis;
            workspace->analyze_object(self->m_slice_check_object, wanted, analysis);
            const auto& features = *analysis.features;
            const auto hole = std::find_if(features.holes.begin(), features.holes.end(),
                                           [](const auto& h) { return std::abs(h.diameter - 10) < 0.2; });
            const auto back = std::find_if(features.faces.begin(), features.faces.end(),
                                           [](const auto& f) { return f.normal[1] > 0.99; });
            if (hole == features.holes.end() || back == features.faces.end()) {
                self->check(false, "slice_checks_features_found");
                self->finish_slice_checks(then);
                return;
            }
            std::vector<Workspace::RegionRequest> requests(2);
            requests[0].object   = self->m_slice_check_object;
            requests[0].kind     = "precision_hole";
            requests[0].geometry = Workspace::RegionGeometry{"hole", hole->handle};
            requests[1].object   = self->m_slice_check_object;
            requests[1].kind     = "hidden";
            requests[1].geometry = Workspace::RegionGeometry{"face", back->handle};
            std::vector<Workspace::RegionRecord> planned;
            self->check(workspace->plan_regions(requests, {}, planned).succeeded() &&
                            workspace->apply_regions(planned, {}, self->m_slice_check_regions).succeeded(),
                        "slice_checks_regions_applied");
            self->slice_then("slice_checks_second_slice", *plate, [self, plate, then, hole_contact] {
                auto* workspace = installed_shell()->workspace();
                Workspace::SliceReportRequest request;
                request.supports = request.seams = true;
                request.regions  = self->m_slice_check_regions;
                const auto report = workspace->slice_report(*plate, request);
                if (report.supports)
                    for (const auto& contact : report.supports->contacts)
                        std::cout << "support contact after " << contact.object_name << " " << contact.target << " " << contact.area << std::endl;
                self->check(report.valid && !hole_contact(report), "slice_checks_precision_hole_keeps_support_out");
                const bool seams_on_back = report.seams && std::any_of(report.seams->regions.begin(), report.seams->regions.end(),
                                                                       [](const auto& r) { return r.region_id == "r2" && r.seams > 0; });
                if (report.seams)
                    for (const auto& placement : report.seams->regions)
                        std::cout << "seams on " << placement.region_id << " " << placement.kind << ": " << placement.seams << " of "
                                  << report.seams->count << std::endl;
                self->check(seams_on_back, "slice_checks_seams_on_the_hidden_face");
                self->verify_outputs(*plate);
                self->verify_cancel(*plate, [self, then] { self->finish_slice_checks(then); });
            });
        });
    }

    // M6 through the real adapter, on the sliced plate: a rendered picture,
    // a packed picture read back, the slice's layers and G-code, every export
    // kind, and a cancelled slice; none may ask anything.
    void verify_outputs(Workspace::PlateId plate)
    {
        auto* workspace = installed_shell()->workspace();
        DialogCounter counter;

        Workspace::RenderRequest render;
        render.plate  = plate;
        render.view   = "front";
        render.width  = 640;
        render.height = 480;
        Workspace::RenderedImage picture;
        const auto rendered = workspace->render_view(render, picture);
        if (!rendered.succeeded()) std::cout << "render: " << rendered.message << std::endl;
        check(rendered.succeeded() && picture.width == 640 && picture.height == 480 && picture.png.size() > 100 &&
                  picture.png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0,
              "outputs_render_is_a_png");
        // A plain background would compress to almost nothing; the plate's
        // objects make a picture of some size.
        std::cout << "rendered front view: " << picture.png.size() << " bytes" << std::endl;
        check(picture.png.size() > 2000, "outputs_render_shows_something");

        // A packed picture, larger than the cap, comes back scaled.
        const fs::path pictures = fs::path(workspace->auxiliary_data_dir()) / "Model Pictures";
        fs::create_directories(pictures);
        {
            wxImage large(2000, 1000);
            large.SetRGB(wxRect(0, 0, 2000, 1000), 200, 60, 30);
            large.SaveFile(wxString::FromUTF8((pictures / "cover.png").string()), wxBITMAP_TYPE_PNG);
            std::ofstream((pictures / "notes.txt").string()) << "Print the bracket in PETG.";
        }
        Workspace::AttachmentContent cover, notes, escape;
        check(workspace->read_attachment("Model Pictures/cover.png", cover).succeeded() && cover.kind == "image" &&
                  cover.width == 1280 && cover.height == 640 && cover.truncated,
              "outputs_attachment_picture_scaled");
        check(workspace->read_attachment("Model Pictures/notes.txt", notes).succeeded() && notes.kind == "text" &&
                  notes.data == "Print the bracket in PETG.",
              "outputs_attachment_text");
        check(!workspace->read_attachment("../Metadata/model_settings.config", escape).succeeded() &&
                  !workspace->read_attachment("JusPrin/state.json", escape).succeeded(),
              "outputs_attachment_stays_in_the_folder");
        fs::remove_all(pictures);

        Workspace::SliceInspectRequest inspect;
        inspect.plate = plate;
        inspect.count = 5;
        const auto layers = workspace->inspect_slice(inspect);
        check(layers.valid && layers.layer_count > 100 && layers.layers.size() == 5 && layers.next == 5 &&
                  std::abs(layers.layers[0].z - 0.2) < 0.01 && layers.layers[1].seconds > 0 && !layers.layers[1].roles.empty(),
              "outputs_inspect_layers");
        if (layers.layers.size() > 1)
            std::cout << "layer 1: z " << layers.layers[1].z << " time " << layers.layers[1].seconds << " s, speed "
                      << layers.layers[1].speed_min << "-" << layers.layers[1].speed_max << ", roles " << layers.layers[1].roles.size()
                      << std::endl;
        inspect.gcode = true;
        const auto gcode = workspace->inspect_slice(inspect);
        check(gcode.valid && !gcode.gcode.empty() && gcode.gcode.size() <= 64 * 1024 && gcode.next.has_value(), "outputs_inspect_gcode");

        const fs::path folder = fs::temp_directory_path() / fs::unique_path("jusprin-exports-%%%%");
        fs::create_directories(folder);
        const auto exported = [&](const std::string& kind, const fs::path& path, const char* name) {
            Workspace::ExportRequest request;
            request.kind  = kind;
            request.path  = path.string();
            request.plate = kind == "project_3mf" || kind == "presets" ? std::nullopt : std::optional<Workspace::PlateId>(plate);
            Workspace::ExportResult result;
            const auto done = workspace->export_file(request, result);
            if (!done.succeeded()) std::cout << name << ": " << done.message << std::endl;
            check(done.succeeded() && !result.files.empty() && result.bytes > 0, name);
            return result;
        };
        exported("gcode", folder / "plate.gcode", "outputs_export_gcode");
        std::string first_line;
        {
            std::ifstream written((folder / "plate.gcode").string());
            std::getline(written, first_line);
        }
        check(!first_line.empty() && first_line[0] == ';', "outputs_export_gcode_is_gcode");
        exported("sliced_3mf", folder / "plate.gcode.3mf", "outputs_export_sliced_3mf");
        exported("project_3mf", folder / "project.3mf", "outputs_export_project_3mf");
        exported("stl", folder / "plate.stl", "outputs_export_stl");
        const auto presets = exported("presets", folder, "outputs_export_presets");
        std::cout << "exported presets: " << presets.files.size() << " files" << std::endl;
        Workspace::ExportRequest again;
        again.kind = "gcode";
        again.path = (folder / "plate.gcode").string();
        check(!workspace->check_export(again).succeeded(), "outputs_export_refuses_to_replace_unasked");
        again.path = "relative.gcode";
        check(!workspace->check_export(again).succeeded(), "outputs_export_refuses_a_relative_path");
        again.path = (fs::path(data_dir()) / "stolen.gcode").string();
        again.overwrite = true;
        check(!workspace->check_export(again).succeeded(), "outputs_export_refuses_the_data_folder");
        boost::system::error_code removed;
        fs::remove_all(folder, removed);
        for (const auto& title : counter.titles) std::cout << "outputs dialog shown: " << title << std::endl;
        check(counter.shown == 0, "outputs_show_no_dialog");
    }

    // A slice stopped while it runs, through the slicing notification's own
    // Cancel.
    void verify_cancel(Workspace::PlateId plate, std::function<void()> then)
    {
        auto* workspace = installed_shell()->workspace();
        const Workspace::SettingsPatch finer{{{"layer_height", "0.12"}}};
        Workspace::SettingsPreview applied;
        workspace->apply_settings(finer, Workspace::previewed_changes(workspace->preview_settings(finer)), applied);
        check(workspace->start_slice(plate, false).succeeded(), "outputs_cancel_slice_started");
        wait_until([] { return installed_shell()->workspace()->snapshot().slicing.running; }, "outputs_cancel_slice_running",
                   [self = shared_from_this(), then] {
                       bool stopped = false;
                       auto* workspace = installed_shell()->workspace();
                       const bool done = workspace->cancel_slice(stopped).succeeded();
                       std::cout << "cancel: succeeded " << done << " stopped " << stopped << " running after "
                                 << workspace->snapshot().slicing.running << std::endl;
                       self->check(done && stopped, "outputs_cancel_stops_the_slice");
                       self->wait_until([] { return !installed_shell()->workspace()->snapshot().slicing.running; },
                                        "outputs_cancel_slice_reported_stopped", then);
                   });
    }

    void slice_then(const char* name, Workspace::PlateId plate, std::function<void()> then)
    {
        auto* workspace = installed_shell()->workspace();
        check(workspace->start_slice(plate, false).succeeded(), std::string(name) + "_started");
        const auto sliced = [plate] {
            const auto snapshot = installed_shell()->workspace()->snapshot();
            return !snapshot.slicing.running &&
                   std::any_of(snapshot.plates.begin(), snapshot.plates.end(), [&](const auto& p) { return p.id == plate && p.sliced; });
        };
        // A result from before the change still reads as sliced until the run
        // starts; wait for the run first, then for its result.
        wait_until([sliced] { return !sliced(); }, std::string(name) + "_running",
                   [self = shared_from_this(), sliced, name, then] { self->wait_until(sliced, name, then); });
    }

    void finish_slice_checks(const std::function<void()>& then)
    {
        auto* workspace = installed_shell()->workspace();
        if (!m_slice_check_regions.empty())
            workspace->remove_regions(m_slice_check_regions);
        workspace->delete_items({Workspace::DeleteItem{Workspace::DeleteItem::Kind::Object, m_slice_check_object}});
        wxGetApp().get_tab(Preset::TYPE_PRINT)->load_config(m_slice_check_settings);
        then();
    }

    Workspace::ObjectId                  m_slice_check_object;
    std::vector<Workspace::RegionRecord> m_slice_check_regions;
    DynamicPrintConfig                   m_slice_check_settings;

    // Phase 6: record one real sliced plate as a deterministic build, then an
    // exported copy and completed physical print through the same page ->
    // Agent -> coordinator path, and confirm that changing the
    // manufacturing input makes the build's derived staleness visible.
    void agent_phase6_history()
    {
        const std::size_t objects_before = m_plater->model().objects.size();
        check(m_plater->duplicate_object(0) >= 0, "phase6_manufacturing_change_before_build");
        wait_until(
            [this, objects_before] { return m_plater->model().objects.size() > objects_before; },
            "phase6_manufacturing_change_applied", [self = shared_from_this()] {
                installed_shell()->status_row()->request_slice();
                self->wait_until(
                    [self] {
                        PartPlate* plate = self->m_plater->get_partplate_list().get_curr_plate();
                        return plate != nullptr && plate->is_slice_result_valid();
                    },
                    "phase6_active_plate_sliced", [self] { self->phase6_propose_build(); });
            });
    }

    void phase6_propose_build()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/build')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && activities.back().tool == "record_build" &&
                       activities.back().state == Agent::ToolState::Succeeded;
            },
            "phase6_build_completed", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::string action_id = web_view.host().tools().activities().back().action_id;
                self->wait_until(
                    [self, &web_view, action_id] {
                        const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                        return activity != nullptr && activity->state == Agent::ToolState::Succeeded &&
                               self->persistence().document().builds().size() == 1;
                    },
                    "phase6_build_recorded", [self] { self->phase6_propose_export(); });
            });
    }

    void phase6_propose_export()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/export')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && activities.back().tool == "record_export_copy" &&
                       activities.back().state == Agent::ToolState::Succeeded;
            },
            "phase6_export_completed", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::string action_id = web_view.host().tools().activities().back().action_id;
                self->wait_until(
                    [self, &web_view, action_id] {
                        const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                        return activity != nullptr && activity->state == Agent::ToolState::Succeeded &&
                               self->persistence().document().exported_copies().size() == 1;
                    },
                    "phase6_export_recorded", [self] { self->phase6_propose_print(); });
            });
    }

    void phase6_propose_print()
    {
        AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
        const std::size_t activities_before = web_view.host().tools().activities().size();
        WebView::RunScript(web_view.webview(), "window.__jusprinTest && window.__jusprinTest.send('/print')");
        wait_until(
            [&web_view, activities_before] {
                const auto& activities = web_view.host().tools().activities();
                return activities.size() > activities_before && activities.back().tool == "record_physical_print" &&
                       activities.back().state == Agent::ToolState::Succeeded;
            },
            "phase6_print_completed", [self = shared_from_this()] {
                AgentWebView& web_view = installed_shell()->agent_pane()->web_view();
                const std::string action_id = web_view.host().tools().activities().back().action_id;
                self->wait_until(
                    [self, &web_view, action_id] {
                        const Agent::ToolActivity* activity = web_view.host().tools().find(action_id);
                        return activity != nullptr && activity->state == Agent::ToolState::Succeeded &&
                               self->persistence().document().physical_prints().size() == 1;
                    },
                    "phase6_physical_print_recorded", [self] { self->phase6_verify_records(); });
            });
    }

    void phase6_verify_records()
    {
        // The count moved from the header's overflow menu to the Project tab.
        {
            LeftPane* pane = installed_shell()->left_pane();
            pane->show_project();
            pane->snapshot();
            const auto labels = pane->action_labels();
            check(std::find(labels.begin(), labels.end(), _L("Printed once")) != labels.end(),
                  "project_tab_print_count_follows_the_ledger");
            pane->show_plates();
        }
        const Agent::BuildRecord build = persistence().document().builds().front();
        const Agent::ExportedCopyRecord copy = persistence().document().exported_copies().front();
        const Agent::PhysicalPrintRecord print = persistence().document().physical_prints().front();
        check(build.manufacturing_input_hash.size() == 64 && build.output_hash.size() == 64,
              "phase6_build_has_sha256_provenance");
        check(build.plate_name == print.plate_name, "phase6_print_keeps_plate");
        check(!print.printer.empty() && !print.material.empty() && !print.started_at.empty() && !print.ended_at.empty(),
              "phase6_print_keeps_setup_and_times");
        check(print.outcome == "completed" && print.gcode_hash == build.output_hash,
              "phase6_print_keeps_outcome_and_gcode_hash");
        check(print.statistics.layer_count == 124 && copy.expected_output_hash == build.output_hash &&
                  copy.observed_output_hash == build.output_hash,
              "phase6_stats_and_export_checksum_survive");

        check(m_plater->duplicate_object(0) >= 0, "phase6_change_after_build");
        wait_until(
            [self = shared_from_this(), input_hash = build.manufacturing_input_hash, plate_index = build.plate_index] {
                const auto current = Agent::manufacturing_input_hash(self->installed_workspace_snapshot(), plate_index);
                return current && *current != input_hash;
            },
            "phase6_old_build_becomes_stale", [self = shared_from_this()] { self->agent_import_and_clean_share(); });
    }

    Workspace::WorkspaceSnapshot installed_workspace_snapshot() const
    {
        return installed_shell()->workspace()->snapshot();
    }

    void agent_import_and_clean_share()
    {
        const std::string identity_before = persistence().document().project_id();
        const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
        const std::vector<size_t> imported = m_plater->load_files(
            std::vector<std::string>{cube}, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence,
            false);
        check(imported.size() == 1, "import_adds_content");
        check(persistence().document().project_id() == identity_before, "import_keeps_project_identity");

        const std::string clean_path = (fs::temp_directory_path() / fs::unique_path("jusprin-clean-%%%%.3mf")).string();
        check(persistence().export_clean_copy(clean_path).succeeded(), "clean_copy_written");
        std::ifstream clean(clean_path, std::ios::binary);
        const std::string clean_bytes((std::istreambuf_iterator<char>(clean)), std::istreambuf_iterator<char>());
        check(clean_bytes.find("3D/3dmodel.model") != std::string::npos, "clean_copy_is_a_project_archive");
        check(clean_bytes.find("JusPrin/state.json") == std::string::npos, "clean_copy_has_no_conversation_state");

        check(m_plater->rename_object(0, "Unsaved failure fixture"), "failed_autosave_edit_applied");
        const std::string project_id = persistence().document().project_id();
        const std::filesystem::path pending = std::filesystem::path(data_dir()) / "jusprin" / "projects" /
                                               project_id / "pending";
        const auto original_permissions = std::filesystem::status(pending).permissions();
        std::filesystem::permissions(pending, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::replace);
        const bool refused = !installed_shell()->autosave()->save_now();
        check(refused && installed_shell()->autosave()->state() == Workspace::ProjectAutosave::State::Failed,
              "failed_autosave_is_visible");
        check(installed_shell()->status_row()->project_summary().BeforeFirst('\n').EndsWith(wxGetTranslation("Couldn't save")),
              "failed_autosave_has_plain_status_text");
        check(m_plater->new_project(true, true) == wxID_CANCEL && persistence().document().project_id() == project_id,
              "failed_autosave_blocks_project_release");
        std::filesystem::permissions(pending, original_permissions, std::filesystem::perm_options::replace);
        check(installed_shell()->autosave()->save_now() &&
              installed_shell()->autosave()->state() == Workspace::ProjectAutosave::State::Saved,
              "failed_autosave_retries_after_storage_recovers");

        auto* autosave = installed_shell()->autosave();
        Agent::PlanRecord retained_plan;
        retained_plan.headline = "Keep this plan after model restore";
        persistence().document().set_plan(retained_plan, persistence().timestamp());
        persistence().commit();
        check(autosave->save_now(), "restore_fixture_document_saved");
        const std::string before_restore = autosave->current_version();
        const std::size_t conversations_before = persistence().document().conversations().size();
        const auto history = autosave->history();
        const auto earlier = std::find_if(history.begin(), history.end(), [&before_restore](const auto& version) {
            return version.id != before_restore && !version.objects.empty();
        });
        check(earlier != history.end(), "earlier_model_version_available");
        if (earlier != history.end()) {
            check(autosave->restore(earlier->id), "earlier_model_version_restored");
            check(m_plater->model().objects.size() == earlier->objects.size(),
                  "restore_reloads_selected_model");
            check(persistence().document().conversations().size() == conversations_before &&
                  persistence().document().plan().headline == retained_plan.headline,
                  "restore_preserves_current_document");
            const bool reopened = autosave->restore(before_restore);
            if (!reopened)
                std::cerr << "HARNESS RESTORE ERROR " << autosave->error() << '\n';
            check(reopened, "pre_restore_version_can_be_reopened");
        }

        std::cerr << "HARNESS BENCH saved_project_bytes=" << m_saved_project_bytes << " save_ms=" << m_save_ms << '\n';
        boost::system::error_code ec;
        if (!m_saved_project_file.empty()) {
            std::ifstream source(m_saved_project_file, std::ios::binary);
            const std::string current((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
            check(current == m_saved_project_original_bytes, "autosave_does_not_overwrite_imported_3mf");
        }
        fs::remove(m_saved_project_file, ec);
        fs::remove(clean_path, ec);
        after_agent();
    }

    void after_agent()
    {
        verify_resize();
        verify_project_replacement();
        verify_uninstall_restores_stock([self = shared_from_this()] { self->finish(); });
    }

    void verify_resize()
    {
        if (installed_shell()->is_left_pane_collapsed()) {
            installed_shell()->toggle_left_pane();
            wxYield();
        }
        auto* left_divider = wxWindow::FindWindowByName("Resize Plates and Project panel", m_frame);
        auto* left_pane = installed_shell()->left_pane();
        check(left_divider != nullptr && left_divider->IsShown(), "left_resize_handle_shown");
        check(left_divider != nullptr && left_divider->GetSize().x == left_divider->FromDIP(8),
              "left_resize_handle_matches_agent_treatment");
        if (left_divider != nullptr && left_pane != nullptr) {
            auto drag_left = [left_divider](int delta_x) {
                const wxPoint start(left_divider->GetClientSize().x / 2, left_divider->GetClientSize().y / 2);
                for (wxEventType type : {wxEVT_LEFT_DOWN, wxEVT_MOTION, wxEVT_LEFT_UP}) {
                    wxMouseEvent event(type);
                    event.SetPosition(start + (type == wxEVT_LEFT_DOWN ? wxPoint() : wxPoint(delta_x, 0)));
                    event.SetEventObject(left_divider);
                    left_divider->GetEventHandler()->ProcessEvent(event);
                }
                wxYield();
            };

            const int original_width = left_pane->GetSize().x;
            drag_left(left_divider->FromDIP(120));
            check(left_pane->GetSize().x > original_width, "left_panel_mouse_drag_grows_width");

            drag_left(-left_divider->FromDIP(2000));
            check(left_pane->GetSize().x == left_pane->FromDIP(160), "left_panel_drag_stops_at_minimum_width");
            check(left_pane->IsShown() && left_divider->IsShown(), "left_panel_drag_never_closes_the_pane");

            drag_left(left_divider->FromDIP(2000));
            auto* agent_divider = wxWindow::FindWindowByName("Resize Agent panel", m_frame);
            const int agent_width = installed_shell()->agent_pane()->GetSize().x;
            const int expected_max = std::max(left_pane->FromDIP(160),
                m_frame->GetClientSize().x - left_pane->FromDIP(320) - left_divider->GetSize().x -
                agent_width - (agent_divider == nullptr ? 0 : agent_divider->GetSize().x));
            check(left_pane->GetSize().x == expected_max, "left_panel_drag_uses_available_window_width");
            check(m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth() > 200,
                  "left_panel_drag_preserves_workspace_width");

            drag_left(original_width - left_pane->GetSize().x);
            check(left_pane->GetSize().x == original_width, "left_panel_width_can_be_restored");

            auto* open_toggle = dynamic_cast<HeaderButton*>(
                wxWindow::FindWindowByName("Plates and Project panel", left_pane));
            auto* project_toggle = dynamic_cast<HeaderButton*>(
                wxWindow::FindWindowByName("Plates and Project panel", installed_shell()->status_row()));
            auto press = [](HeaderButton* button) {
                if (button == nullptr) return;
                wxMouseEvent down(wxEVT_LEFT_DOWN), up(wxEVT_LEFT_UP);
                button->ProcessWindowEvent(down);
                button->ProcessWindowEvent(up);
                wxYield();
            };
            auto double_click_left = [left_divider] {
                const wxPoint start(left_divider->GetClientSize().x / 2, left_divider->GetClientSize().y / 2);
                for (wxEventType type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_LEFT_UP}) {
                    wxMouseEvent event(type);
                    event.SetPosition(start);
                    event.SetEventObject(left_divider);
                    left_divider->GetEventHandler()->ProcessEvent(event);
                }
                wxYield();
            };
            check(open_toggle != nullptr && open_toggle->IsShown() &&
                  project_toggle != nullptr && !project_toggle->IsShown(),
                  "left_panel_open_toggle_stays_in_its_own_row");
            const int open_toggle_x = open_toggle == nullptr ? -1 : open_toggle->GetScreenPosition().x;
            press(open_toggle);
            check(installed_shell()->is_left_pane_collapsed(), "left_panel_open_toggle_collapses");
            check(!left_pane->IsShown() && !left_divider->IsShown(),
                  "left_panel_collapse_hides_pane_and_divider");
            check(project_toggle != nullptr && project_toggle->IsShown(),
                  "left_panel_closed_toggle_moves_to_project_header");
            check(project_toggle != nullptr && project_toggle->GetScreenPosition().x == open_toggle_x,
                  "left_panel_toggle_keeps_the_same_screen_position");
            press(project_toggle);
            check(!installed_shell()->is_left_pane_collapsed() && left_pane->IsShown() && left_divider->IsShown(),
                  "left_panel_project_toggle_reopens_the_pane");
            check(left_pane->GetSize().x == original_width, "left_panel_reopens_at_its_previous_width");

            double_click_left();
            check(installed_shell()->is_left_pane_collapsed(), "left_panel_divider_double_click_collapses");
            press(project_toggle);
            check(!installed_shell()->is_left_pane_collapsed() && left_pane->IsShown() && left_divider->IsShown(),
                  "left_panel_reopens_after_divider_collapse");
            verify_agent_pane_tiling("after_left_panel_drag");
        }

        auto* divider = wxWindow::FindWindowByName("Resize Agent panel", m_frame);
        auto* pane = installed_shell()->agent_pane();
        check(divider != nullptr && divider->IsShown(), "agent_resize_handle_shown");
        check(divider != nullptr && divider->GetSize().x == divider->FromDIP(8),
              "agent_resize_handle_matches_token_width");
        if (divider != nullptr) {
            auto drag = [divider](int delta_x) {
                const wxPoint start(divider->GetClientSize().x / 2, divider->GetClientSize().y / 2);
                wxMouseEvent down(wxEVT_LEFT_DOWN);
                down.SetPosition(start);
                down.SetEventObject(divider);
                divider->GetEventHandler()->ProcessEvent(down);
                wxMouseEvent motion(wxEVT_MOTION);
                motion.SetPosition(start + wxPoint(delta_x, 0));
                motion.SetEventObject(divider);
                divider->GetEventHandler()->ProcessEvent(motion);
                wxMouseEvent up(wxEVT_LEFT_UP);
                up.SetPosition(start + wxPoint(delta_x, 0));
                up.SetEventObject(divider);
                divider->GetEventHandler()->ProcessEvent(up);
                wxYield();
            };

            auto double_click = [divider] {
                const wxPoint start(divider->GetClientSize().x / 2, divider->GetClientSize().y / 2);
                for (wxEventType type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_LEFT_UP}) {
                    wxMouseEvent event(type);
                    event.SetPosition(start);
                    event.SetEventObject(divider);
                    divider->GetEventHandler()->ProcessEvent(event);
                }
                wxYield();
            };
            auto* project_toggle = dynamic_cast<HeaderButton*>(
                wxWindow::FindWindowByName("Agent panel", installed_shell()->status_row()));
            auto press_toggle = [project_toggle, pane] {
                if (!installed_shell()->is_agent_pane_collapsed()) {
                    static int next = 0;
                    pane->web_view().host().on_page_message(
                        nlohmann::json{{"protocol", Agent::Protocol::kName},
                                       {"version", Agent::Protocol::kVersion},
                                       {"id", "pane-toggle-" + std::to_string(++next)},
                                       {"type", Agent::Protocol::kShellAction},
                                       {"payload", {{"action", "collapse_agent_pane"}}}}
                            .dump());
                    wxYield();
                    return;
                }
                if (project_toggle == nullptr) return;
                wxMouseEvent press(wxEVT_LEFT_DOWN), release(wxEVT_LEFT_UP);
                project_toggle->ProcessWindowEvent(press);
                project_toggle->ProcessWindowEvent(release);
                wxYield();
            };
            auto workspace_width = [this] {
                return m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth();
            };

            const int original_pane_width = pane->GetSize().x;
            drag(-divider->FromDIP(120));
            check(pane->GetSize().x > original_pane_width, "agent_panel_mouse_drag_grows_width");

            // However far a drag is pushed, it only sizes the pane: it stops
            // at the minimum and never closes it, so aiming for a narrow pane
            // cannot take the conversation off the screen by accident.
            drag(divider->FromDIP(2000));
            check(pane->GetSize().x == pane->FromDIP(320), "agent_panel_drag_stops_at_minimum_width");
            check(!installed_shell()->is_agent_pane_collapsed() && pane->IsShown() && divider->IsShown(),
                  "agent_panel_drag_never_closes_the_pane");

            drag(-divider->FromDIP(2000));
            const LeftPane* current_left_pane = installed_shell()->left_pane();
            const int left_width = current_left_pane != nullptr && current_left_pane->IsShown() ? current_left_pane->GetSize().x : 0;
            auto* current_left_divider = wxWindow::FindWindowByName("Resize Plates and Project panel", m_frame);
            const int left_handle_width = current_left_divider != nullptr && current_left_divider->IsShown()
                                              ? current_left_divider->GetSize().x : 0;
            const int expected_max = std::max(pane->FromDIP(320),
                m_frame->GetClientSize().x - pane->FromDIP(320) - divider->GetSize().x - left_width - left_handle_width);
            check(pane->GetSize().x == expected_max, "agent_panel_drag_uses_available_window_width");
            check(workspace_width() > 200, "agent_panel_drag_preserves_workspace_width");

            drag(pane->GetSize().x - original_pane_width);
            check(pane->GetSize().x == original_pane_width, "agent_panel_width_can_be_restored");

            check(project_toggle != nullptr && !project_toggle->IsShown(),
                  "agent_panel_open_toggle_leaves_project_header");
            const int open_workspace_width = workspace_width();

            double_click();
            check(installed_shell()->is_agent_pane_collapsed(), "agent_panel_divider_double_click_collapses");
            check(!pane->IsShown() && !divider->IsShown(), "agent_panel_collapse_hides_pane_and_divider");
            check(project_toggle != nullptr && project_toggle->IsShown(),
                  "agent_panel_closed_toggle_moves_to_project_header");
            check(workspace_width() > open_workspace_width, "agent_panel_collapse_widens_workspace");
            check(pane->web_view().host().mcp() != nullptr, "agent_panel_collapse_keeps_the_runtime");

            press_toggle();
            check(!installed_shell()->is_agent_pane_collapsed() && pane->IsShown() && divider->IsShown(),
                  "agent_panel_toggle_reopens_the_pane");
            check(pane->GetSize().x == pane->FromDIP(320), "agent_panel_reopens_at_a_usable_width");

            press_toggle();
            check(installed_shell()->is_agent_pane_collapsed(), "agent_panel_toggle_closes_the_pane");
            press_toggle();
            check(!installed_shell()->is_agent_pane_collapsed(), "agent_panel_toggle_reopens_again");

            drag(-divider->FromDIP(120));
            check(pane->GetSize().x > original_pane_width, "agent_panel_still_resizable_after_collapse");
            drag(pane->GetSize().x - original_pane_width);
            check(pane->GetSize().x == original_pane_width, "agent_panel_width_restored_after_collapse");

            // Both checks below need a pane that is not sitting at its own
            // minimum: only a pane wide enough to have width taken from it
            // exercises the width policy at all.
            const wxSize whole = m_frame->GetSize();
            m_frame->SetSize(m_frame->FromDIP(760), whole.y);
            m_frame->Layout();
            wxYield();

            // A drag that runs past the edge of a narrow window asks for a
            // width the pane cannot have. That width must not be banked: the
            // pane would take it, at the workspace's expense, as soon as the
            // window grew enough to allow it.
            drag(-divider->FromDIP(3000));
            const int narrow_max = pane->GetSize().x;
            m_frame->SetSize(m_frame->FromDIP(1400), whole.y);
            m_frame->Layout();
            wxYield();
            check(pane->GetSize().x == narrow_max,
                  "agent_panel_over_drag_is_not_banked_for_a_larger_window");

            // Shrinking the window while the pane is wider than the room left
            // for it re-runs the width policy on every step. No Layout() call
            // in the loop, on purpose: the point is what the frame's own size
            // handlers leave behind, and an extra layout against the settled
            // client size would repair a stale one before it could be seen.
            drag(-divider->FromDIP(3000));
            for (int width : {1200, 1000, 860, 760}) {
                m_frame->SetSize(m_frame->FromDIP(width), whole.y);
                wxYield();
                verify_agent_pane_tiling("shrinking_to_" + std::to_string(width) + "_dip");
            }
            for (int width : {860, 1100, 1400}) {
                m_frame->SetSize(m_frame->FromDIP(width), whole.y);
                wxYield();
                verify_agent_pane_tiling("growing_to_" + std::to_string(width) + "_dip");
            }
            m_frame->SetSize(whole);
            m_frame->Layout();
            wxYield();
            drag(pane->GetSize().x - original_pane_width);
        }

        const wxSize original = m_frame->GetSize();
        verify_agent_pane_tiling("before_resize");
        m_frame->SetSize(original + wxSize(120, 80));
        m_frame->Layout();
        StatusRow* row = installed_shell()->status_row();
        check(row->IsShown() && row->GetSize().GetWidth() > 0, "status_row_survives_resize");
        check(m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth() > 200, "canvas_survives_resize");
        verify_agent_pane_tiling("after_grow");
        verify_header_layout();
        m_frame->SetSize(m_frame->FromDIP(900),original.y);
        m_frame->Layout();
        // The single setup chip became a two-half printer/filament chip; both
        // halves must exist, since each anchors its own menu.
        auto* setup = wxWindow::FindWindowByName("Printer",row) && wxWindow::FindWindowByName("Filament",row)
                          ? wxWindow::FindWindowByName("Printer and filament",row) : nullptr;
        setup->SetLabel(wxString('W',180));
        row->SendSizeEvent();
        verify_header_layout();
        row->refresh();
        m_frame->SetSize(original);
        m_frame->Layout();
    }

    // Workspace, divider and pane must tile the frame's client width exactly.
    // Each one on its own can look right while the three together do not: a
    // layout run against a stale width sizes some of them for the previous
    // width, so they overlap and the pane covers part of the workspace.
    void verify_agent_pane_tiling(const std::string& name)
    {
        auto* pane = installed_shell()->agent_pane();
        auto* divider = wxWindow::FindWindowByName("Resize Agent panel", m_frame);
        if (pane == nullptr || divider == nullptr || installed_shell()->is_agent_pane_collapsed())
            return;
        const int client = m_frame->GetClientSize().x;
        const wxRect workspace = m_notebook->GetRect();
        const wxRect bar = divider->GetRect();
        const wxRect agent = pane->GetRect();
        auto* row = installed_shell()->status_row();
        auto* webview = pane->web_view().webview();
        // The Plates / Project pane, when shown, is the first column.
        const LeftPane* left_pane = installed_shell()->left_pane();
        const bool left_shown = left_pane != nullptr && left_pane->IsShown();
        auto* left_divider = wxWindow::FindWindowByName("Resize Plates and Project panel", m_frame);
        const bool left_divider_shown = left_divider != nullptr && left_divider->IsShown();
        const int project_origin = left_shown && left_divider_shown ? left_divider->GetRect().GetRight() + 1 : 0;
        const bool tiled = (!left_shown || (left_pane->GetRect().x == 0 && left_divider_shown &&
                                            left_pane->GetRect().GetRight() + 1 == left_divider->GetRect().x)) &&
                           workspace.x == project_origin && workspace.GetWidth() > 0 &&
                           workspace.GetRight() + 1 == bar.x &&
                           bar.GetRight() + 1 == agent.x &&
                           agent.GetRight() + 1 == client;
        check(tiled, "agent_panel_columns_tile_the_client_" + name);
        const bool header_split = row != nullptr && webview != nullptr &&
                                  row->GetRect().x == project_origin && row->GetRect().GetRight() + 1 == bar.x &&
                                  row->GetRect().y == bar.y && agent.y == bar.y &&
                                  webview->GetRect().x == 0 && webview->GetRect().y == 0 &&
                                  webview->GetRect().GetWidth() == agent.GetWidth();
        check(header_split, "agent_panel_web_header_splits_at_divider_" + name);
        if (!tiled)
            std::cerr << "HARNESS DETAIL client=" << client << " workspace=" << workspace.x << "+"
                      << workspace.GetWidth() << " left-divider="
                      << (left_divider == nullptr ? -1 : left_divider->GetRect().x) << "+"
                      << (left_divider == nullptr ? 0 : left_divider->GetRect().GetWidth())
                      << " divider=" << bar.x << "+" << bar.GetWidth()
                      << " pane=" << agent.x << "+" << agent.GetWidth() << '\n';
    }

    void verify_header_layout()
    {
        auto* row = installed_shell()->status_row();
        auto* home = wxWindow::FindWindowByName("Home navigation",row);
        // The single setup chip became a two-half printer/filament chip; both
        // halves must exist, since each anchors its own menu.
        auto* setup = wxWindow::FindWindowByName("Printer",row) && wxWindow::FindWindowByName("Filament",row)
                          ? wxWindow::FindWindowByName("Printer and filament",row) : nullptr;
        auto* action = wxWindow::FindWindowByName("Next print action",row);
        auto* arrow = wxWindow::FindWindowByName("Print actions",row);
        auto* more = wxWindow::FindWindowByName("Project actions",row);
        auto* toggle = wxWindow::FindWindowByName("Agent panel",row);
        auto* left_toggle = wxWindow::FindWindowByName("Plates and Project panel",row);
        check(home && setup && action && arrow && more,"header_has_all_five_controls");
        if (!home || !setup || !action || !arrow || !more) return;
        check(row->GetSize().y == row->FromDIP(56),"header_matches_56_dip_design");
        const int expected_home_x = row->FromDIP(16) +
            (left_toggle != nullptr && left_toggle->IsShown() ? left_toggle->GetSize().x + row->FromDIP(8) : 0);
        check(home->GetPosition().x == expected_home_x,"header_home_follows_optional_left_pane_toggle");
        check(home->GetRect().GetRight() < setup->GetPosition().x && setup->GetRect().GetRight() < action->GetPosition().x,
              "header_home_setup_actions_order_without_overlap");
        // The reducer returns no menu items for a single unsliced plate, and the
        // chevron half is hidden in that state, so the joined-halves geometry
        // only applies while both halves are laid out.
        if (arrow->IsShown()) {
            check(action->GetPosition().x+action->GetSize().x == arrow->GetPosition().x,"header_split_halves_joined");
            check(arrow->GetSize().y == action->GetSize().y,"header_split_halves_equal_height");
        } else {
            check(action->GetRect().GetRight() < more->GetPosition().x,"header_lone_action_precedes_overflow");
        }
        check(action->GetSize().y == row->FromDIP(34),"header_action_height");
        if (toggle != nullptr && toggle->IsShown()) {
            check(toggle->GetPosition().x + toggle->GetSize().x == row->GetSize().x - row->FromDIP(16),
                  "header_closed_agent_toggle_right_aligned");
            check(more->GetRect().GetRight() < toggle->GetPosition().x,
                  "header_overflow_precedes_closed_agent_toggle");
        } else {
            check(more->GetPosition().x+more->GetSize().x == row->GetSize().x-row->FromDIP(16),
                  "header_overflow_right_aligned");
        }
        check(!setup->GetLabel().Contains("Plate ") && !setup->GetLabel().Contains("@"),"header_setup_compact_no_plate_or_raw_suffix");
        check(!status_row_labels(row).Contains(wxString::FromUTF8("Prints \xC2\xB7")),"header_has_no_standalone_print_count");
    }

    // The em of a font in pixels. wx has no portable accessor for it: on MSW
    // GetPixelSize() reads LOGFONT::lfHeight, the em at the screen DPI the
    // font was built for; on macOS and GTK it measures a rendered "g", which
    // is the line height, but there a point is a DIP
    // (wxHAS_DPI_INDEPENDENT_PIXELS), so the em is the point size: as is on
    // macOS, and at Pango's 96 dpi logical resolution on GTK.
    static int font_em_pixels(const wxFont& font)
    {
#if defined(__WXMSW__)
        return font.GetPixelSize().y;
#elif defined(__APPLE__)
        return wxRound(font.GetFractionalPointSize());
#else
        return wxRound(font.GetFractionalPointSize() * 96.0 / 72.0);
#endif
    }

    // The type roles are DIP sizes and every role font must render its token
    // as its em. Windows once drew the body role at 15 px because
    // Label::sysFont truncates 14 * 4 / 5 to 11 pt, which is why filament
    // names truncated sooner in the spool picker there than on macOS. The
    // shell resolves its theme through brand_theme(), so that is checked.
    void verify_type_roles_render_at_token_size()
    {
        const ShellTheme* theme = brand_theme();
        check(theme != nullptr, "shell_theme_loaded");
        if (theme == nullptr) return;
        const std::pair<TextRole, const char*> roles[] = {
            {TextRole::Body, "body"}, {TextRole::Label, "label"}, {TextRole::Metadata, "metadata"}};
        for (const auto& [role, name] : roles) {
            const int         token = theme->type_style(role).size;
            const std::string suffix = std::string("_em_is_") + std::to_string(token) + "_dip";
            check(font_em_pixels(theme->font(role)) == m_frame->FromDIP(token), std::string("font_") + name + suffix);
        }
    }

    void verify_project_replacement()
    {
        check(m_plater->new_project(true, true) != wxID_CANCEL, "replacement_project");
        check(installed_shell() != nullptr && installed_shell()->is_installed(), "shell_survives_project_replacement");
        check(!m_notebook->GetBtnsListCtrl()->IsShown(), "tab_strip_still_hidden_after_replacement");
        check(!wxGetApp().sidebar().IsShown(), "sidebar_still_hidden_after_replacement");
    }

    void verify_uninstall_restores_stock(std::function<void()> next)
    {
        // A printer connection can leave an agent the slicing profile would not
        // pick. Detaching the shell hands the choice back to the profile.
        NetworkAgent* agent = wxGetApp().getAgent();
        const std::string profile_agent = agent->get_printer_agent()->get_agent_info().id;
        agent->set_printer_agent(NetworkAgentFactory::create_printer_agent_by_id(
            JUSPRIN_FAKE_BAMBU_AGENT_ID, agent->get_cloud_agent(BBL_CLOUD_PROVIDER), Slic3r::data_dir()));
        check(profile_agent != JUSPRIN_FAKE_BAMBU_AGENT_ID &&
                  agent->get_printer_agent()->get_agent_info().id == JUSPRIN_FAKE_BAMBU_AGENT_ID,
              "detach_fixture_installs_an_agent_the_profile_does_not_pick");
        m_frame->select_tab(size_t(MainFrame::tp3DEditor));
        installed_shell()->status_row()->show_overflow_menu();
        choose_header_item(_L("Switch to classic view…"), [self = shared_from_this(), agent, profile_agent, next = std::move(next)] {
            self->wait_until([] { return installed_shell() == nullptr; }, "menu_switches_to_classic_view",
                [self, agent, profile_agent, next] {
                    self->check(agent->get_printer_agent()->get_agent_info().id == profile_agent,
                                "detach_restores_the_profile_driven_agent");
                    self->check(self->m_notebook->IsShown() &&
                                    self->m_notebook->GetSelection() == MainFrame::tp3DEditor,
                                "classic_prepare_window_shown");
                    self->check(self->m_notebook->GetBtnsListCtrl()->IsShown(), "tab_strip_restored");
                    self->check(self->m_plater->is_sidebar_available(), "sidebar_available_restored");
                    self->check(!self->m_plater->get_view3D_canvas3D()->legacy_overlays_hidden(),
                                "prepare_legacy_overlays_restored");
                    self->m_frame->Layout();
                    self->check(self->m_plater->canvas3D()->get_wxglcanvas()->GetSize().GetWidth() > 200,
                                "stock_canvas_usable_after_restore");
                    const std::string cube = std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/test_stl/ASCII/20mmbox-LF.stl";
                    self->check(self->m_plater->load_files(std::vector<std::string>{cube},
                                    LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances | LoadStrategy::Silence, false).size() == 1,
                                "stock_backup_fixture_loaded");
                    const fs::path backup = self->m_plater->model().get_backup_path();
                    Slic3r::set_backup_interval(1);
                    Slic3r::backup_soon();
                    self->check(wait_for([backup] { return fs::exists(backup / ".3mf"); }, std::chrono::seconds(10)),
                                "stock_backup_resumes_after_shell_detach");
                    self->check(self->m_plater->new_project(true, true) != wxID_CANCEL, "stock_backup_fixture_cleared");
                    next();
                });
        });
    }

    // Polls through a one-shot wxTimer rather than a self-reposting
    // CallAfter: a pending-event spin keeps wxApp's pending queue non-empty,
    // which starves any nested YieldFor on the stack (wx's WKWebView
    // AddScriptMessageHandler runs script through one) and deadlocks the
    // WebView setup this harness is waiting on.
    // each_tick runs before every re-test of the condition. It exists for
    // states that only the page can report: the tick asks the page, the page
    // answers over the bridge, and the condition sees the answer.
    void wait_until(std::function<bool()> condition,
                    std::string           name,
                    std::function<void()> then,
                    std::function<void()> each_tick = {})
    {
        m_wait_condition = std::move(condition);
        m_wait_name      = std::move(name);
        m_wait_then      = std::move(then);
        m_wait_each_tick = std::move(each_tick);
        poll_wait();
    }

    // A settle condition rather than a delay: post_init has been entered, the
    // frame is out of post_init's Freeze/Thaw, and the loop has delivered a
    // further wxEVT_IDLE since -- which it only does once nothing is pending:
    // no queued page change, no CallAfter, no timer already due. The watcher
    // is bound on the app after GUI_App's own idle handler, so wx runs it
    // first in the same event; arming only after post_initialized() is seen
    // is what keeps the idle that ran post_init from counting.
    void wait_until_settled(std::string name, std::function<void()> then)
    {
        if (!m_idle_watch_bound) {
            m_app.Bind(wxEVT_IDLE, [weak = std::weak_ptr<Scenario>(shared_from_this())](wxIdleEvent& event) {
                if (auto self = weak.lock(); self && self->m_idle_armed)
                    self->m_idle_seen = true;
                event.Skip();
            });
            m_idle_watch_bound = true;
        }
        m_idle_armed = false;
        m_idle_seen  = false;
        wait_until([this] {
            if (!m_app.post_initialized() || m_frame->IsFrozen())
                return false;
            if (!m_idle_armed) {
                m_idle_armed = true;
                return false;
            }
            return m_idle_seen;
        }, std::move(name), std::move(then));
    }

    void poll_wait()
    {
        if (m_state->stop || !m_wait_condition)
            return;
        if (m_wait_condition()) {
            check(true, m_wait_name);
            auto then        = std::move(m_wait_then);
            m_wait_condition = nullptr;
            m_wait_then      = nullptr;
            m_wait_each_tick = nullptr;
            then();
            return;
        }
        if (m_wait_each_tick)
            m_wait_each_tick();
        if (++m_wait_ticks % 500 == 0) {
            PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
            std::cerr << "HARNESS WAIT " << m_wait_name << " preview=" << m_plater->is_preview_shown()
                      << " plate=" << m_plater->get_partplate_list().get_curr_plate_index()
                      << " slice_valid=" << (plate != nullptr && plate->is_slice_result_valid()) << '\n';
        }
        if (std::chrono::steady_clock::now() >= m_state->deadline) {
            check(false, m_wait_name);
            fail(m_wait_name + " did not become true before the deadline");
            return;
        }
        m_poll_timer.StartOnce(10);
    }

    void fail(const std::string& message)
    {
        std::cerr << "HARNESS ERROR " << message << '\n';
        ++m_failures;
        finish();
    }

    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        const int result = m_failures == 0 ? 0 : 1;
        std::cerr << "HARNESS RESULT " << (result == 0 ? "PASS" : "FAIL") << " failures=" << m_failures << '\n';
        m_state->result = result;
        m_state->stop = true;
        m_poll_timer.Stop();
        if (m_frame == nullptr) {
            m_app.ExitMainLoop();
            return;
        }
        // Leave the way the application leaves. A forced close runs
        // MainFrame::shutdown() -- backup callback cleared, canvas handlers
        // unbound and volumes released, background threads stopped,
        // GUI_App::set_closing, the window hidden -- and then Destroy()s the
        // frame inside the main loop, so wx leaves the loop on its own once
        // the last top-level window is gone and GUI_App::OnExit() runs with
        // nothing left to tear down.
        //
        // ExitMainLoop() skipped all of that: OnExit() deleted the device
        // manager and agent first, and wxAppBase::CleanUp() then deleted the
        // still-shown MainFrame from inside ~wxInitializer, where any
        // exception is std::terminate -- a 0xC0000409 exit on Windows with
        // nothing printed. Deferred so nothing on the current stack keeps
        // using a frame that has already been shut down.
        m_app.CallAfter([frame = m_frame] { frame->Close(true); });
        // Safety net: a stray top-level window would keep the loop alive.
        m_exit_watchdog.StartOnce(30000);
    }

    GUI_App&                      m_app;
    std::shared_ptr<HarnessState> m_state;
    Plater*                       m_plater{nullptr};
    MainFrame*                    m_frame{nullptr};
    Notebook*                     m_notebook{nullptr};
    int                           m_failures{0};
    bool                          m_finished{false};
    std::uint64_t                 m_wait_ticks{0};
    nlohmann::json                m_settings_original, m_settings_patch;
    std::size_t                   m_objects_before_tool{0};
    std::size_t                   m_live_copies_before{0};
    std::string                   m_saved_project_id;
    std::string                   m_saved_project_file;
    std::string                   m_live_action_id;
    std::unique_ptr<JusPrinTest::NativeMcpClient> m_mcp_client;
    std::size_t                   m_saved_project_bytes{0};
    std::string                   m_saved_project_original_bytes;
    double                        m_save_ms{0.0};
    std::string                   m_header_setup_printer;
    std::string                   m_header_setup_restore_printer;
    std::size_t                   m_header_project_chat_count{0};
    Selection::IndicesList        m_tool_strip_selection;
    bool                          m_capture_typing{true};

    wxEvtHandler          m_poll_handler;
    wxTimer               m_poll_timer{&m_poll_handler};
    wxEvtHandler          m_exit_handler;
    wxTimer               m_exit_watchdog{&m_exit_handler};
    std::function<bool()> m_wait_condition;
    std::string           m_wait_name;
    std::function<void()> m_wait_then;
    std::function<void()> m_wait_each_tick;
    bool                  m_idle_watch_bound{false};
    bool                  m_idle_armed{false};
    bool                  m_idle_seen{false};
};

// The app ships no stand-in Agent, so each mode's Agent is installed here
// through the host's own set_agent, the call setup uses when a key verifies.
void install_harness_agent(HarnessState::Mode mode)
{
    ShellController* shell = installed_shell();
    if (shell == nullptr)
        return; // stock mode: the shell is disabled, so there is no Agent panel
    Agent::AgentHost& host = shell->agent_pane()->web_view().host();
    switch (mode) {
    case HarnessState::Mode::LiveAgentUnavailable:
    case HarnessState::Mode::ManualUnconfigured:
        return;
    case HarnessState::Mode::LiveAgent:
    case HarnessState::Mode::ManualLiveAgent: {
        // Without a key the live checks report the missing service themselves.
        const char* key = std::getenv("OPENAI_API_KEY");
        if (key == nullptr || *key == '\0')
            return;
        // The printer panel builds its own Agent from the app configuration,
        // so hands-on printer setup needs the key there too: this run's
        // throwaway data directory, as --printer-live does. A std::string,
        // or AppConfig::set picks its bool overload.
        if (mode == HarnessState::Mode::ManualLiveAgent)
            wxGetApp().app_config->set("jusprin_agent", "openai_api_key", std::string(key));
        Agent::OpenAIResponsesConfig config;
        config.api_key = key;
        config.usage_listener = [](const Agent::AgentUsage& usage) {
            // cached_input_tokens is the evidence the tool-loading decision
            // rests on; report it on every request, not only when it is
            // non-zero, so a chat that stops caching is visible in the log.
            std::cerr << "JUSPRIN LIVE USAGE provider=openai input_tokens=" << usage.input
                      << " cached_input_tokens=" << usage.cached_input << " output_tokens=" << usage.output
                      << " total_tokens=" << usage.total << '\n';
        };
        host.set_agent(std::make_unique<Agent::OpenAIResponsesAgent>(std::move(config), Agent::make_openai_http_transport()),
                       Agent::AgentAvailability::Ready);
        return;
    }
    case HarnessState::Mode::PrintIssues:
    case HarnessState::Mode::ManualPrintIssues: {
        if (g_issue_no_agent)
            return;
        auto agent = std::make_unique<RecordingAgent>();
        g_issue_agent = agent.get();
        host.set_agent(std::move(agent), Agent::AgentAvailability::Ready);
        return;
    }
    default:
        host.set_agent(std::make_unique<Agent::DeterministicMockAgent>(), Agent::AgentAvailability::Ready);
        return;
    }
}

void start_when_ready(GUI_App& app, const std::shared_ptr<HarnessState>& state)
{
    if (state->stop)
        return;
    if (std::chrono::steady_clock::now() >= state->deadline) {
        std::cerr << "HARNESS ERROR application did not become ready before timeout\n";
        state->result = 1;
        state->stop = true;
        app.ExitMainLoop();
        return;
    }
    if (app.mainframe != nullptr && app.plater() != nullptr) {
#ifdef __APPLE__
        if (state->dark_appearance) set_harness_appearance(*state->dark_appearance);
#endif
        install_harness_agent(state->mode);
        if (state->mode == HarnessState::Mode::Manual || state->mode == HarnessState::Mode::ManualLiveAgent ||
            state->mode == HarnessState::Mode::ManualUnconfigured)
            return;
        auto scenario = std::make_shared<Scenario>(app, state);
        state->runner = scenario;
        scenario->start();
        return;
    }
    app.CallAfter([&app, state] { start_when_ready(app, state); });
}

} // namespace
} // namespace Slic3r::GUI::JusPrin

namespace {

// The argument the parent sent, as the bytes it sent.
//
// run_mcp_setup_command hands wxExecute a wide argv built with
// wxString::FromUTF8, so Windows receives the argument intact -- but the CRT
// then builds this process's narrow argv by converting that command line to
// the ANSI code page, where the fixture literal's CJK has no representation
// and is replaced before main is even entered. Nothing the parent does can
// prevent that. Windows keeps the real command line in UTF-16, so read the
// argument from there and re-encode it as the UTF-8 the parent will search
// the output for. Off Windows the narrow argv already carries those bytes.
std::string utf8_argument(int index, char** argv)
{
#ifdef _WIN32
    int count = 0;
    wchar_t** wide = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (wide == nullptr) return argv[index];
    std::string text;
    if (index < count) {
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, nullptr, 0, nullptr, nullptr);
        if (bytes > 1) {
            text.resize(std::size_t(bytes) - 1);
            ::WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, text.data(), bytes, nullptr, nullptr);
        }
    }
    ::LocalFree(wide);
    return text;
#else
    return argv[index];
#endif
}

} // namespace

int main(int argc, char** argv)
{
    // A real subprocess for setup tests, with no GUI or external configuration.
    if (argc == 4 && std::string(argv[1]) == "--setup-child") {
        const std::string mode(argv[2]);
        if (mode == "timeout" || mode == "ignore-term") {
            if (mode == "ignore-term") std::signal(SIGTERM, SIG_IGN);
            std::cout << "fixture pid " << wxGetProcessId() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(45));
            return 0; // The timeout test must terminate us before this fallback.
        }
        if (mode == "delayed") std::this_thread::sleep_for(std::chrono::milliseconds(800));
        if (mode == "large") std::cout << std::string(256 * 1024, 'x');
        std::cout << utf8_argument(3, argv) << '\n';
        std::cerr << "fixture stderr\n";
        return mode == "failure" ? 7 : 0;
    }
    // Registered before any function-local static is constructed, so those
    // statics' destructors run before this marker and namespace-scope
    // statics' destructors run after it.
    std::atexit([] {
        std::fputs("HARNESS ATEXIT REACHED\n", stderr);
        std::fflush(stderr);
    });
    using namespace Slic3r;
    using namespace Slic3r::GUI;
    using namespace Slic3r::GUI::JusPrin;

    const fs::path original_directory = fs::current_path();
    fs::path data_directory = fs::temp_directory_path() / fs::unique_path("jusprin-shell-%%%%-%%%%-%%%%");

    auto state = std::make_shared<HarnessState>();
    std::vector<char*> gui_arguments;
    gui_arguments.reserve(static_cast<std::size_t>(argc));
    gui_arguments.emplace_back(argv[0]);
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--dark-ui" || argument == "--light-ui") {
#if defined(__APPLE__) || defined(_WIN32)
            state->dark_appearance = argument == "--dark-ui";
#else
            std::cerr << "Appearance overrides are supported only by the macOS and Windows harnesses\n";
            return 2;
#endif
        }
        else if (argument == "--stock")
            state->mode = HarnessState::Mode::Stock;
        else if (argument == "--left-pane")
            state->mode = HarnessState::Mode::LeftPane;
        else if (argument == "--left-pane-editor")
            state->mode = HarnessState::Mode::LeftPaneEditor;
        else if (argument == "--left-pane-capture") {
            if (++index == argc) return 2;
            state->mode        = HarnessState::Mode::LeftPane;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--left-pane-sliced-capture") {
            if (++index == argc) return 2;
            state->mode        = HarnessState::Mode::LeftPaneSlicedCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--classic-switch")
            state->mode = HarnessState::Mode::ClassicSwitch;
        else if (argument == "--preset-close")
            state->mode = HarnessState::Mode::PresetClose;
        else if (argument == "--external-project-history")
            state->mode = HarnessState::Mode::ExternalProjectHistory;
        else if (argument == "--chat-restoration")
            state->mode = HarnessState::Mode::ChatRestoration;
        else if (argument == "--chat-restoration-capture") {
            if (++index == argc) return 2;
            state->mode = HarnessState::Mode::ChatRestoration;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--external-project-history-capture") {
            if (++index == argc) return 2;
            state->mode = HarnessState::Mode::ExternalProjectHistory;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--autosave-seed" || argument == "--autosave-reopen") {
            if (++index == argc) {
                std::cerr << argument << " requires a dedicated fixture directory\n";
                return 2;
            }
            state->mode = argument == "--autosave-seed" ? HarnessState::Mode::AutosaveSeed : HarnessState::Mode::AutosaveReopen;
            state->keep_data = true;
            data_directory = fs::absolute(argv[index]);
        }
        else if (argument == "--manual")
            state->mode = HarnessState::Mode::Manual;
        else if (argument == "--manual-live-agent")
            state->mode = HarnessState::Mode::ManualLiveAgent;
        else if (argument == "--manual-unconfigured")
            state->mode = HarnessState::Mode::ManualUnconfigured;
        else if (argument == "--manual-tool-strip")
            state->mode = HarnessState::Mode::ManualToolStrip;
        else if (argument == "--file-corpus") {
            if (index + 2 >= argc) {
                std::cerr << "--file-corpus needs a manifest and output path\n";
                return 2;
            }
            state->mode = HarnessState::Mode::FileCorpus;
            state->file_corpus_manifest = utf8_argument(++index, argv);
            state->file_corpus_output = utf8_argument(++index, argv);
        }
        else if (argument == "--file-corpus-config") {
            if (index + 1 >= argc) {
                std::cerr << "--file-corpus-config needs a config path\n";
                return 2;
            }
            state->file_corpus_config = utf8_argument(++index, argv);
        }
        else if (argument == "--slice-all-cold")
            state->mode = HarnessState::Mode::SliceAllCold;
        else if (argument == "--live-agent")
            state->mode = HarnessState::Mode::LiveAgent;
        else if (argument == "--mcp")
            state->mode = HarnessState::Mode::Mcp;
        else if (argument == "--mcp-setup")
            state->mode = HarnessState::Mode::McpSetup;
        else if (argument == "--manual-mcp" || argument == "--header-visual" ||
                 argument == "--right-pane-visual") {
            if (++index == argc) {
                std::cerr << "--manual-mcp requires a dedicated fixture directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::ManualMcp;
            state->header_visual = argument == "--header-visual";
            state->right_pane_visual = argument == "--right-pane-visual";
            data_directory = fs::absolute(argv[index]);
        }
        else if (argument == "--mcp-bridge") {
            state->mode = HarnessState::Mode::Mcp;
            state->mcp_bridge = true;
        }
        else if (argument == "--live-agent-unavailable")
            state->mode = HarnessState::Mode::LiveAgentUnavailable;
        else if (argument == "--printer-setup")
            state->mode = HarnessState::Mode::PrinterSetup;
        else if (argument == "--printer-menu")
            state->mode = HarnessState::Mode::PrinterMenu;
        else if (argument == "--task-chat")
            state->mode = HarnessState::Mode::TaskChat;
        else if (argument == "--task-chat-configured") {
            state->mode = HarnessState::Mode::TaskChat;
            state->task_chat_configured = true;
        }
        else if (argument == "--printer-menu-capture") {
            if (++index == argc) {
                std::cerr << "--printer-menu-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::PrinterMenu;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--home-live")
            state->mode = HarnessState::Mode::HomeLive;
        else if (argument == "--printer-live")
            state->mode = HarnessState::Mode::PrinterLive;
        else if (argument == "--printer-live-no-plugin") {
            state->mode      = HarnessState::Mode::PrinterLive;
            state->no_plugin = true;
        }
        else if (argument == "--printer-settings-live") {
            state->mode          = HarnessState::Mode::PrinterLive;
            state->settings_live = true;
        }
        else if (argument == "--filament-settings-live") {
            state->mode                   = HarnessState::Mode::PrinterLive;
            state->filament_settings_live = true;
        }
        else if (argument == "--printer-connect-capture") {
            if (++index == argc) {
                std::cerr << "--printer-connect-capture requires an output directory\n";
                return 2;
            }
            state->mode            = HarnessState::Mode::PrinterLive;
            state->connect_capture = true;
            state->capture_dir     = fs::absolute(argv[index]);
        }
        else if (argument == "--recomputing-capture") {
            if (++index == argc) {
                std::cerr << "--recomputing-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::RecomputingCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--setup-differences-capture") {
            if (++index == argc) {
                std::cerr << "--setup-differences-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::SetupDifferencesCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--timeline-capture") {
            if (++index == argc) {
                std::cerr << "--timeline-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::TimelineCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--figma-timeline-capture") {
            if (++index == argc) {
                std::cerr << "--figma-timeline-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::FigmaTimelineCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--figma-timeline-manual") {
            state->mode = HarnessState::Mode::FigmaTimelineCapture;
            state->figma_timeline_manual = true;
        }
        else if (argument == "--print-issues")
            state->mode = HarnessState::Mode::PrintIssues;
        else if (argument == "--manual-print-issues" || argument == "--manual-print-issues-no-agent") {
            state->mode = HarnessState::Mode::ManualPrintIssues;
            g_issue_no_agent = argument == "--manual-print-issues-no-agent";
        }
        else if (argument == "--print-issues-capture") {
            if (++index == argc) {
                std::cerr << "--print-issues-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::PrintIssues;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else if (argument == "--tool-strip-capture") {
            if (++index == argc) {
                std::cerr << "--tool-strip-capture requires an output directory\n";
                return 2;
            }
            state->mode = HarnessState::Mode::ToolStripCapture;
            state->capture_dir = fs::absolute(argv[index]);
        }
        else
            gui_arguments.emplace_back(argv[index]);
    }
#ifdef __APPLE__
    if (state->dark_appearance) set_harness_appearance(*state->dark_appearance);
#endif
    fs::create_directories(data_directory / "log");
    if (state->mode == HarnessState::Mode::LiveAgentUnavailable || state->task_chat_configured) {
        // The unavailable setup check exercises a connection failure. The
        // configured task-chat capture never sends a turn, but also uses the
        // closed port so its placeholder key cannot reach a provider.
        wxSetEnv("JUSPRIN_OPENAI_ENDPOINT", "http://127.0.0.1:1/v1/responses");
    }

    {
        const std::string config_path = state->file_corpus_config.empty()
            ? std::string(JUSPRIN_SOURCE_DIR) + "/tests/data/jusprin/harness.conf"
            : state->file_corpus_config.string();
        std::ifstream base(config_path);
        std::string   config((std::istreambuf_iterator<char>(base)), std::istreambuf_iterator<char>());
        if (state->mode == HarnessState::Mode::Stock) {
            const std::string anchor = "\"language\": \"en_US\",";
            config.replace(config.find(anchor), anchor.size(), anchor + "\n    \"jusprin_shell\": \"0\",");
        }
#ifdef _WIN32
        // On Windows GUI_App::dark_mode() honours dark_color_mode alone, and
        // AppConfig::set_defaults writes "0" when the key is absent, so the
        // system theme is never consulted. Seeding the key here reaches
        // Update_dark_mode_flag and NppDarkMode::InitDarkMode before any
        // window exists, the same path the Preferences toggle takes.
        if (state->dark_appearance) {
            const std::string anchor = "\"language\": \"en_US\",";
            config.replace(config.find(anchor), anchor.size(),
                           anchor + "\n    \"dark_color_mode\": \"" + (*state->dark_appearance ? "1" : "0") + "\",");
        }
#endif
        if (state->mode == HarnessState::Mode::LiveAgent ||
            state->mode == HarnessState::Mode::ManualLiveAgent || state->mode == HarnessState::Mode::PrinterLive ||
            state->mode == HarnessState::Mode::LiveAgentUnavailable || state->task_chat_configured) {
            // The base config has the Agent off, like a fresh install. Only
            // the configured task-chat visual run adds a placeholder key.
            const std::string from = "\"jusprin_agent\": {\n    \"enabled\": false\n  }";
            const bool live_enabled = state->mode == HarnessState::Mode::LiveAgent ||
                                      state->mode == HarnessState::Mode::ManualLiveAgent ||
                                      state->mode == HarnessState::Mode::PrinterLive || state->task_chat_configured;
            const std::string consent = live_enabled ? "true" : "false";
            const std::string placeholder = state->task_chat_configured ?
                ",\n    \"openai_api_key\": \"visual-test-placeholder\"" : "";
            const std::string to = "\"jusprin_agent\": {\n    \"cloud_consent\": " + consent +
                                   ",\n    \"enabled\": true,\n    \"model\": \"gpt-5.4-mini\"" + placeholder +
                                   ",\n    \"provider\": \"openai\"\n  }";
            const std::size_t pos = config.find(from);
            if (pos != std::string::npos)
                config.replace(pos, from.size(), to);
        }
        std::ofstream out((data_directory / (std::string(SLIC3R_APP_KEY) + ".conf")).string());
        out << config;
    }

    const fs::path resources = fs::path(JUSPRIN_SOURCE_DIR) / "resources";
    set_resources_dir(resources.string());
    set_var_dir((resources / "images").string());
    set_local_dir((resources / "i18n").string());
    set_sys_shapes_dir((resources / "shapes").string());
    set_custom_gcodes_dir((resources / "custom_gcodes").string());
    set_data_dir(data_directory.string());
    set_temporary_dir(data_directory.string());
    save_main_thread_id();

    std::thread installer([state] {
        while (!state->stop && std::chrono::steady_clock::now() < state->deadline) {
            if (auto* app = dynamic_cast<GUI_App*>(wxApp::GetInstance())) {
                app->CallAfter([app, state] { start_when_ready(*app, state); });
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        state->result = 1;
        state->stop = true;
    });

    GUI_InitParams params;
    params.argc = static_cast<int>(gui_arguments.size());
    params.argv = gui_arguments.data();
    const int gui_result = GUI_Run(params);
    std::cerr << "HARNESS GUI_RUN RETURNED " << gui_result << '\n';
    state->stop = true;
    installer.join();

    if (state->mode == HarnessState::Mode::Mcp) {
        const bool removed = !fs::exists(data_directory / "jusprin" / "mcp.json");
        std::cerr << "HARNESS CHECK mcp_discovery_removed_after_app_shutdown " << (removed ? "PASS" : "FAIL") << '\n';
        if (!removed) state->result = 1;
        if (state->bridge) {
            state->bridge->request(JusPrinTest::request("tools/call", {{"name", "workspace_inspect"}}));
            const bool responded = JusPrinTest::wait_for([&] { return state->bridge->done(); }, [] {});
            const auto messages = state->bridge->messages();
            const bool offline = responded && !messages.empty() && messages.back().contains("result") &&
                messages.back()["result"]["structuredContent"]["error"]["code"] == "workspace_unavailable";
            std::cerr << "HARNESS CHECK mcp_bridge_survives_app_shutdown_and_reports_offline " << (offline ? "PASS" : "FAIL") << '\n';
            const bool clean = state->bridge->shutdown();
            std::cerr << "HARNESS CHECK mcp_bridge_eof_exit_zero " << (clean ? "PASS" : "FAIL") << '\n';
            if (!offline || !clean) state->result = 1;
        }
    }

    fs::current_path(original_directory);
    if (state->result == 0 && !state->keep_data) {
        // The boost::log sink keeps log/debug_*.log.0 open until static
        // destruction, so on Windows this cannot delete everything, and an
        // uncaught filesystem_error here would end a PASS run with
        // 0xE06D7363. Report instead of throwing.
        boost::system::error_code error;
        fs::remove_all(data_directory, error);
        if (error)
            std::cerr << "HARNESS WARNING data dir not fully removed: " << error.message() << " ("
                      << data_directory.string() << ")\n";
    } else
        std::cerr << "HARNESS DATA DIR kept for inspection: " << data_directory.string() << '\n';
    int exit_code = state->result;
    if (state->mode == HarnessState::Mode::Manual || state->mode == HarnessState::Mode::ManualLiveAgent ||
        state->mode == HarnessState::Mode::ManualUnconfigured || state->mode == HarnessState::Mode::ManualMcp ||
        state->mode == HarnessState::Mode::ManualToolStrip || state->mode == HarnessState::Mode::ManualPrintIssues ||
        state->figma_timeline_manual)
        exit_code = gui_result;
    else if (state->result < 0)
        exit_code = gui_result == 0 ? 1 : gui_result;
    // Static destructors run after this line; a crash without a later
    // ATEXIT line is in a function-local static (the 3mf backup manager,
    // the shell slot, ...), one after it is in a namespace-scope static.
    std::cerr << "HARNESS MAIN RETURNING " << exit_code << '\n';
    return exit_code;
}
