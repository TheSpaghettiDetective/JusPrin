#include "McpSetupCommand.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <wx/dialog.h>
#include <wx/process.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stream.h>
#include <wx/timer.h>
#include <wx/utils.h>

namespace Slic3r::GUI::JusPrin {

McpSetupResult run_mcp_setup_command(wxWindow* parent, const std::vector<std::string>& arguments)
{
    if (arguments.empty()) throw std::invalid_argument("Missing setup executable");
    wxDialog progress(parent, wxID_ANY, "Connecting AI tool", wxDefaultPosition, parent->FromDIP(wxSize(460, 140)));
    auto* label = new wxStaticText(&progress, wxID_ANY, "Saving the MCP entry using the client's own CLI...");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(label, 1, wxEXPAND | wxALL, progress.FromDIP(16));
    progress.SetSizer(layout);
    wxProcess process(&progress);
    process.Redirect();
    std::vector<wxString> values;
    for (const auto& value : arguments) values.push_back(wxString::FromUTF8(value));
    std::vector<const wchar_t*> argv;
    for (const auto& value : values) argv.push_back(value.wc_str());
    argv.push_back(nullptr);
    const auto pid = wxExecute(argv.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, &process);
    if (pid <= 0) return {false, "Could not start the client CLI. Use Copy or check its installation."};
    process.CloseOutput();
    std::string output;
    bool timed_out = false;
    int exit_code = -1;
    const auto started = std::chrono::steady_clock::now();
    auto drain = [&] {
        for (auto* stream : {process.GetInputStream(), process.GetErrorStream()}) {
            while (stream && stream->CanRead()) {
                char buffer[4096];
                stream->Read(buffer, sizeof buffer);
                const auto size = stream->LastRead();
                if (!size) break;
                if (output.size() < 65536) output.append(buffer, std::min(size, 65536 - output.size()));
            }
        }
    };
    wxTimer timer(&progress);
    progress.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
        drain();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (!timed_out && elapsed > std::chrono::seconds(30)) {
            timed_out = true;
            label->SetLabel("The setup command timed out. Stopping it...");
            wxProcess::Kill(pid, wxSIGTERM);
        } else if (timed_out && elapsed > std::chrono::seconds(32)) wxProcess::Kill(pid, wxSIGKILL);
    });
    progress.Bind(wxEVT_CLOSE_WINDOW, [&](wxCloseEvent& event) {
        if (event.CanVeto()) event.Veto(); // Keep the owned process alive only while its monitor exists.
    });
    // wxDialog handles Cancel separately from window-close events. Do not
    // destroy the process monitor while the CLI may still be saving settings.
    progress.Bind(wxEVT_BUTTON, [](wxCommandEvent&) {}, wxID_CANCEL);
    progress.Bind(wxEVT_END_PROCESS, [&](wxProcessEvent& event) {
        drain(); exit_code = event.GetExitCode(); timer.Stop(); progress.EndModal(wxID_OK);
    });
    timer.Start(30);
    progress.ShowModal();
    if (timed_out) return {false, "Setup timed out; the client config may have changed. Inspect it before trying again.\n" + output};
    return {exit_code == 0, "Client CLI exited with code " + std::to_string(exit_code) + ".\n" + output};
}

void start_mcp_setup_command(wxWindow* parent, const std::vector<std::string>& arguments,
                             std::function<void(McpSetupResult)> done)
{
    if (arguments.empty()) {
        done({false, "Missing setup executable"});
        return;
    }
    struct Watch : wxEvtHandler {
        wxProcess process;
        wxTimer timer;
        std::function<void(McpSetupResult)> done;
        std::string output;
        std::chrono::steady_clock::time_point started;
        long pid{0};
        bool timed_out{false};
        bool finished{false};
        Watch(wxWindow*, std::function<void(McpSetupResult)> complete)
            : process(this), timer(this), done(std::move(complete)), started(std::chrono::steady_clock::now())
        {}
        void drain()
        {
            for (auto* stream : {process.GetInputStream(), process.GetErrorStream()}) {
                while (stream && stream->CanRead()) {
                    char buffer[4096];
                    stream->Read(buffer, sizeof buffer);
                    const auto size = stream->LastRead();
                    if (!size) break;
                    if (output.size() < 65536) output.append(buffer, std::min(size, 65536 - output.size()));
                }
            }
        }
        void finish(McpSetupResult result)
        {
            if (finished) return;
            finished = true;
            timer.Stop();
            auto complete = std::move(done);
            complete(std::move(result));
            CallAfter([this] { delete this; });
        }
    };
    auto* watch = new Watch(parent, std::move(done));
    std::vector<wxString> values;
    for (const auto& value : arguments) values.push_back(wxString::FromUTF8(value));
    std::vector<const wchar_t*> argv;
    for (const auto& value : values) argv.push_back(value.wc_str());
    argv.push_back(nullptr);
    watch->pid = wxExecute(argv.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, &watch->process);
    if (watch->pid <= 0) {
        watch->finish({false, "Could not start the client CLI. Use Copy or check its installation."});
        return;
    }
    watch->process.CloseOutput();
    watch->Bind(wxEVT_TIMER, [watch](wxTimerEvent&) {
        watch->drain();
        const auto elapsed = std::chrono::steady_clock::now() - watch->started;
        if (!watch->timed_out && elapsed > std::chrono::seconds(30)) {
            watch->timed_out = true;
            wxProcess::Kill(watch->pid, wxSIGTERM);
        } else if (watch->timed_out && elapsed > std::chrono::seconds(32))
            wxProcess::Kill(watch->pid, wxSIGKILL);
    });
    watch->Bind(wxEVT_END_PROCESS, [watch](wxProcessEvent& event) {
        if (event.GetPid() != watch->pid) { event.Skip(); return; }
        watch->drain();
        if (watch->timed_out)
            watch->finish({false, "Setup timed out; the client config may have changed. Inspect it before trying again.\n" + watch->output});
        else
            watch->finish({event.GetExitCode() == 0,
                           "Client CLI exited with code " + std::to_string(event.GetExitCode()) + ".\n" + watch->output});
    });
    watch->timer.Start(30);
}
} // namespace Slic3r::GUI::JusPrin
