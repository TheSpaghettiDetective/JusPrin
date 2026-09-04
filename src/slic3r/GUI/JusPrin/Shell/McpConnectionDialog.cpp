#include "McpConnectionDialog.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpConnections.hpp"
#include "slic3r/GUI/JusPrin/Mcp/McpConfigFile.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <filesystem>
#include <chrono>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/collpane.h>
#include <wx/dataobj.h>
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>
#include <wx/process.h>
#include <wx/stream.h>
#include <wx/timer.h>

namespace Slic3r::GUI::JusPrin {
namespace {
std::string utf8(const wxString& value) { return value.ToUTF8().data(); }

std::string command_path(const std::string& command)
{
    wxString path;
    wxGetEnv("PATH", &path);
#ifdef _WIN32
    wxStringTokenizer directories(path, ";");
    const auto filename = wxString::FromUTF8(command + ".exe");
#else
    wxStringTokenizer directories(path, ":");
    const auto filename = wxString::FromUTF8(command);
#endif
    while (directories.HasMoreTokens()) {
        const auto directory = directories.GetNextToken();
        const auto candidate = wxFileName(directory, filename).GetFullPath();
        if (!directory.empty() && wxFileName::IsFileExecutable(candidate)) return utf8(candidate);
    }
    // GUI launches on macOS often inherit a shorter PATH than a login shell.
#ifndef _WIN32
    for (const auto& directory : {wxGetHomeDir() + "/.local/bin", wxString("/opt/homebrew/bin"), wxString("/usr/local/bin")}) {
        const auto candidate = wxFileName(directory, filename).GetFullPath();
        if (wxFileName::IsFileExecutable(candidate)) return utf8(candidate);
    }
#endif
    return {};
}

bool confirm_setup(wxWindow* parent, const std::string& preview)
{
    wxDialog dialog(parent, wxID_ANY, "Confirm AI tool connection", wxDefaultPosition, parent->FromDIP(wxSize(740, 520)),
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(new wxStaticText(&dialog, wxID_ANY,
        "Allow this AI tool to read the open project and propose changes?\nChanges still require approval inside JusPrin."),
        0, wxEXPAND | wxALL, dialog.FromDIP(12));
    layout->Add(new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(preview), wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY), 1, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(12));
    auto* buttons = new wxStdDialogButtonSizer;
    auto* confirm = new wxButton(&dialog, wxID_OK, "Connect");
    auto* cancel = new wxButton(&dialog, wxID_CANCEL, "Cancel");
    buttons->AddButton(confirm); buttons->AddButton(cancel); buttons->Realize();
    cancel->SetDefault();
    layout->Add(buttons, 0, wxALIGN_RIGHT | wxALL, dialog.FromDIP(12));
    dialog.SetSizer(layout);
    return dialog.ShowModal() == wxID_OK;
}
}

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

void show_mcp_connection_dialog(wxWindow* parent, const ShellTheme& theme, const std::string& discovery_path,
                                const std::string& live_url, const std::string& startup_error)
{
#ifdef _WIN32
    constexpr bool windows = true;
    const std::string helper_name = "jusprin-mcp.exe";
#else
    constexpr bool windows = false;
    const std::string helper_name = "jusprin-mcp";
#endif
    const auto executable = std::filesystem::u8path(utf8(wxStandardPaths::Get().GetExecutablePath()));
    const auto bridge = executable.parent_path() / helper_name;
    auto launcher = bridge;
    std::vector<std::string> launch_arguments;
#if defined(__linux__)
    wxString appimage;
    if (wxGetEnv("APPIMAGE", &appimage) && !appimage.empty()) {
        launcher = std::filesystem::u8path(utf8(appimage));
        launch_arguments.emplace_back("--mcp-bridge");
    }
#endif
    const auto entries = Mcp::connection_entries(launcher.u8string(), discovery_path, windows, launch_arguments);
    const auto home = std::filesystem::u8path(utf8(wxGetHomeDir()));
#ifdef __APPLE__
    const auto config_home = home / "Library" / "Application Support";
#elif defined(_WIN32)
    wxString appdata;
    wxGetEnv("APPDATA", &appdata);
    const auto config_home = std::filesystem::u8path(utf8(appdata));
#else
    wxString xdg;
    wxGetEnv("XDG_CONFIG_HOME", &xdg);
    const auto config_home = xdg.empty() ? home / ".config" : std::filesystem::u8path(utf8(xdg));
#endif
    const std::vector<std::filesystem::path> configs{
        home / ".claude.json", home / ".codex" / "config.toml",
        config_home / "Claude" / "claude_desktop_config.json",
        config_home / "Claude" / "claude_desktop_config.json",
        home / ".cursor" / "mcp.json", config_home / "Code" / "User" / "mcp.json"
    };
    wxDialog dialog(parent, wxID_ANY, "Connect AI tools", wxDefaultPosition, parent->FromDIP(wxSize(760, 550)),
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    const auto& colors = theme.palette(GUI_App::dark_mode());
    dialog.SetBackgroundColour(colors.surface_raised);
    dialog.SetForegroundColour(colors.text_primary);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    const int gap = dialog.FromDIP(12);
    auto* intro = new wxStaticText(&dialog, wxID_ANY,
        "Connect a local AI tool to the open JusPrin project. Changes still require approval in the Agent panel.\n"
        "Copy the entry, or choose Connect and review the proposed change before saving it.");
    intro->Wrap(dialog.FromDIP(710));
    layout->Add(intro, 0, wxEXPAND | wxALL, gap);
    auto* clients = new wxChoice(&dialog, wxID_ANY);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::error_code error;
        const bool detected = std::filesystem::exists(configs[i], error) || !command_path(entries[i].id).empty();
        clients->Append(wxString::FromUTF8((detected ? "Detected: " : "Other: ") + entries[i].name));
    }
    clients->SetSelection(0);
    clients->SetName("MCP client");
    layout->Add(clients, 0, wxEXPAND | wxLEFT | wxRIGHT, gap);
    auto* instructions = new wxStaticText(&dialog, wxID_ANY, "");
    layout->Add(instructions, 0, wxEXPAND | wxALL, gap);
    auto* entry = new wxTextCtrl(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                 wxTE_MULTILINE | wxTE_READONLY);
    entry->SetName("MCP connection entry");
    layout->Add(entry, 1, wxEXPAND | wxLEFT | wxRIGHT, gap);
    auto* status = new wxStaticText(&dialog, wxID_ANY, "");
    layout->Add(status, 0, wxEXPAND | wxALL, gap);
    auto refresh = [&] {
        const auto index = std::size_t(clients->GetSelection());
        const auto& selected = entries[index];
        std::string guidance = selected.cli ? (windows ? "Run in PowerShell." : "Run in your terminal.") :
            "Merge this entry into " + configs[index].u8string() + ". Keep existing servers and settings.";
        if (selected.id == "cowork") guidance += " Requires a local Cowork session; cloud sessions cannot reach this helper.";
        if (selected.id == "codex") guidance += " For approvals longer than 60 seconds, raise tool_timeout_sec in the JusPrin MCP config.";
        instructions->SetLabel(wxString::FromUTF8(guidance));
        instructions->Wrap(dialog.FromDIP(710));
        entry->SetValue(wxString::FromUTF8(selected.text));
        status->SetLabel("");
        dialog.Layout();
    };
    clients->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { refresh(); });
    auto* disclosure = new wxCollapsiblePane(&dialog, wxID_ANY, "Direct HTTP (developer use)");
    auto* details = new wxBoxSizer(wxVERTICAL);
    const auto diagnostic = live_url.empty() ? "MCP could not start: " + startup_error :
        live_url + "\nThis URL can change after restarting JusPrin. Local requests need no authentication.";
    details->Add(new wxTextCtrl(disclosure->GetPane(), wxID_ANY, wxString::FromUTF8(diagnostic), wxDefaultPosition,
                              dialog.FromDIP(wxSize(-1, 70)), wxTE_MULTILINE | wxTE_READONLY), 1, wxEXPAND);
    disclosure->GetPane()->SetSizer(details);
    disclosure->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [&](wxCollapsiblePaneEvent&) { dialog.Layout(); });
    layout->Add(disclosure, 0, wxEXPAND | wxLEFT | wxRIGHT, gap);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* copy = new wxButton(&dialog, wxID_ANY, "Copy");
    copy->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        if (!wxTheClipboard->Open()) { status->SetLabel("Clipboard is busy. Try Copy again."); return; }
        const bool copied = wxTheClipboard->SetData(new wxTextDataObject(entry->GetValue()));
        wxTheClipboard->Close();
        status->SetLabel(copied ? "Copied. No client settings were changed." : "Could not copy the entry.");
    });
    buttons->Add(copy);
    auto* connect = new wxButton(&dialog, wxID_ANY, "Connect...");
    connect->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        const auto index = std::size_t(clients->GetSelection());
        const auto& selected = entries[index];
        try {
            if (!std::filesystem::is_regular_file(bridge))
                throw std::runtime_error("The bundled MCP helper is missing. Reinstall or rebuild JusPrin.");
            if (!std::filesystem::is_regular_file(launcher))
                throw std::runtime_error("The AppImage launcher is missing. Reopen the installed AppImage.");
            if (selected.cli) {
                const auto cli = command_path(selected.id);
                if (cli.empty()) throw std::runtime_error("Client CLI was not found. Install it or use Copy in your terminal.");
                std::vector<std::string> arguments{cli};
                arguments.insert(arguments.end(), selected.arguments.begin(), selected.arguments.end());
                std::string command;
                for (const auto& argument : arguments) command += (command.empty() ? "" : " ") + Mcp::quote_argument(argument, windows);
                if (!confirm_setup(&dialog, "Run this command directly (no shell):\n\n" + command +
                    "\n\nThe client CLI manages its configuration. It may replace an existing JusPrin entry. Other servers are not removed.")) return;
                const auto result = run_mcp_setup_command(&dialog, arguments);
                status->SetLabel(wxString::FromUTF8((result.success ? "Configuration saved. Restart the client session to load JusPrin.\n" : "Setup failed.\n") + result.diagnostic));
            } else {
                const std::string root = selected.id == "code" ? "servers" : "mcpServers";
                const auto value = nlohmann::json::parse(selected.text);
                const auto edit = Mcp::prepare_json_connection(configs[index], root, value[root]["jusprin"]);
                if (!confirm_setup(&dialog, edit.preview)) return;
                const auto backup = Mcp::apply_config_edit(edit);
                status->SetLabel(wxString::FromUTF8("Configuration saved. Restart the client to load JusPrin." +
                    (backup.empty() ? "" : "\nBackup: " + backup.u8string())));
            }
        } catch (const std::exception& error) {
            // Client setup is an independent user operation. Show its failure;
            // keep the slicer usable and never report an unsuccessful write as connected.
            status->SetLabel(wxString::FromUTF8(std::string("Could not configure client: ") + error.what()));
        }
        status->Wrap(dialog.FromDIP(710));
        dialog.Layout();
    });
    buttons->Add(connect, 0, wxLEFT, gap);
    buttons->AddStretchSpacer();
    buttons->Add(new wxButton(&dialog, wxID_OK, "Close"));
    layout->Add(buttons, 0, wxEXPAND | wxALL, gap);
    dialog.SetSizer(layout);
    refresh();
    if (!std::filesystem::is_regular_file(bridge)) status->SetLabel("The bundled MCP helper is missing. Reinstall or rebuild JusPrin.");
    dialog.SetMinSize(dialog.FromDIP(wxSize(580, 440)));
    dialog.ShowModal();
}
} // namespace Slic3r::GUI::JusPrin
