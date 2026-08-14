#include "WorkspaceProbe.hpp"

#include "Workspace.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/frame.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace Slic3r::GUI::JusPrin::Workspace {

namespace {

std::string reasons_text(WorkspaceChangeReasons reasons)
{
    std::string result;
    const auto add = [&result](const char* value) {
        if (!result.empty())
            result += '|';
        result += value;
    };
    if (has_reason(reasons, WorkspaceChangeReasons::Selection))
        add("Selection");
    if (has_reason(reasons, WorkspaceChangeReasons::Contents))
        add("Contents");
    if (has_reason(reasons, WorkspaceChangeReasons::History))
        add("History");
    if (has_reason(reasons, WorkspaceChangeReasons::Transform))
        add("Transform");
    if (has_reason(reasons, WorkspaceChangeReasons::Plates))
        add("Plates");
    return result.empty() ? "None" : result;
}

std::string snapshot_text(const WorkspaceSnapshot& snapshot)
{
    std::ostringstream out;
    out << "revision=" << snapshot.revision << " plates=" << snapshot.plates.size()
        << " active=" << (snapshot.active_plate ? std::to_string(snapshot.active_plate->value()) : "none") << " selection=";
    switch (snapshot.selection_status) {
    case SelectionStatus::None: out << "none"; break;
    case SelectionStatus::Objects:
        for (std::size_t i = 0; i < snapshot.selected_objects.size(); ++i) {
            if (i > 0)
                out << ',';
            out << snapshot.selected_objects[i].value();
        }
        break;
    case SelectionStatus::Unsupported: out << "unsupported"; break;
    }
    out << " can_undo=" << snapshot.can_undo << " can_redo=" << snapshot.can_redo << " objects=[";
    bool first = true;
    std::set<ObjectId> emitted;
    for (const WorkspacePlate& plate : snapshot.plates) {
        for (const WorkspaceObject& object : plate.objects) {
            if (!emitted.insert(object.id).second)
                continue;
            if (!first)
                out << "; ";
            first = false;
            out << object.id.value() << ':' << object.name;
            if (!object.instances.empty()) {
                const auto& transform = object.instances.front();
                out << " pos=(" << std::fixed << std::setprecision(3) << transform.position[0] << ',' << transform.position[1] << ','
                    << transform.position[2] << ") rot=(" << transform.rotation[0] << ',' << transform.rotation[1] << ','
                    << transform.rotation[2] << ')';
            }
        }
    }
    out << ']';
    return out.str();
}

class WorkspaceProbeFrame final : public wxFrame
{
public:
    WorkspaceProbeFrame(wxWindow* parent, IWorkspace& workspace)
        : wxFrame(parent, wxID_ANY, "JusPrin Workspace Adapter Probe", wxDefaultPosition, wxSize(760, 480)), m_workspace(workspace)
    {
        if (const char* path = std::getenv("JUSPRIN_WORKSPACE_SPIKE_LOG"))
            m_log_file.open(path, std::ios::out | std::ios::trunc);

        auto* root     = new wxBoxSizer(wxVERTICAL);
        auto* controls = new wxBoxSizer(wxHORIZONTAL);
        controls->Add(new wxStaticText(this, wxID_ANY, "Object:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_objects = new wxChoice(this, wxID_ANY);
        m_objects->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
            // On macOS, dismissing the native choice popup can leave the
            // probe's opaque parent as the key window. Restore the probe on
            // the next event-loop turn so it stays usable above the shell.
            CallAfter([this] {
                Raise();
                m_objects->SetFocus();
            });
            event.Skip();
        });
        controls->Add(m_objects, 1, wxRIGHT, 8);
        add_button(controls, "Refresh", [this] { refresh("manual"); });
        add_button(controls, "Select", [this] { run("select", [this](ObjectId id) { return m_workspace.select_object(id); }); });
        add_button(controls, "Rename", [this] {
            const ObjectId id = selected_id();
            if (!id)
                return;
            const WorkspaceSnapshot current = m_workspace.snapshot();
            std::string name                = "Renamed object";
            for (const WorkspacePlate& plate : current.plates)
                for (const WorkspaceObject& object : plate.objects)
                    if (object.id == id)
                        name = object.name + " renamed";
            log_result("rename", m_workspace.rename_object(id, name));
        });
        add_button(controls, "Duplicate", [this] { run("duplicate", [this](ObjectId id) { return m_workspace.duplicate_object(id); }); });
        add_button(controls, "Remove", [this] { run("remove", [this](ObjectId id) { return m_workspace.remove_object(id); }); });
        add_button(controls, "Undo", [this] { log_result("undo", m_workspace.undo()); });
        add_button(controls, "Redo", [this] { log_result("redo", m_workspace.redo()); });
        root->Add(controls, 0, wxEXPAND | wxALL, 8);

        m_log = new wxTextCtrl(this, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
        root->Add(m_log, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        SetSizer(root);

        m_subscription = m_workspace.subscribe([this](const WorkspaceChanged& change) {
            append("EVENT revision=" + std::to_string(change.revision) + " reasons=" + reasons_text(change.reasons));
            refresh("callback");
        });
        refresh("initial");
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            append("PROBE consumer destroyed");
            event.Skip();
        });
    }

private:
    template<class Fn> void add_button(wxSizer* sizer, const wxString& label, Fn&& fn)
    {
        auto* button = new wxButton(this, wxID_ANY, label);
        button->Bind(wxEVT_BUTTON, [callback = std::forward<Fn>(fn)](wxCommandEvent&) { callback(); });
        sizer->Add(button, 0, wxRIGHT, 4);
    }

    template<class Fn> void run(const char* operation, Fn&& fn)
    {
        const ObjectId id = selected_id();
        if (!id) {
            append(std::string("COMMAND ") + operation + " rejected: no object selected in probe");
            return;
        }
        log_result(operation, fn(id));
    }

    ObjectId selected_id() const
    {
        const int selection = m_objects->GetSelection();
        return selection >= 0 && selection < static_cast<int>(m_object_ids.size()) ? m_object_ids[selection] : ObjectId();
    }

    void log_result(const char* operation, const CommandResult& result)
    {
        std::string line = std::string("COMMAND ") + operation + (result.succeeded() ? " success" : " error");
        if (result.object_id)
            line += " object_id=" + std::to_string(result.object_id->value());
        if (!result.message.empty())
            line += " message=" + result.message;
        append(line);

        // A duplicate may synchronously produce Orca selection events before
        // its returned ID reaches this consumer. Refresh once from the stable
        // post-command model so the next probe command targets the new object.
        if (result.object_id) {
            m_preferred_object = result.object_id;
            refresh("command");
        }
    }

    void refresh(const char* source)
    {
        const WorkspaceSnapshot current = m_workspace.snapshot();
        ObjectId previous               = selected_id();
        if (m_preferred_object)
            previous = *m_preferred_object;
        else if (current.selection_status == SelectionStatus::Objects && current.selected_objects.size() == 1)
            previous = current.selected_objects.front();
        m_preferred_object.reset();
        m_objects->Clear();
        m_object_ids.clear();
        std::set<ObjectId> emitted;
        int restored = wxNOT_FOUND;
        for (const WorkspacePlate& plate : current.plates) {
            for (const WorkspaceObject& object : plate.objects) {
                if (!emitted.insert(object.id).second)
                    continue;
                m_object_ids.emplace_back(object.id);
                m_objects->Append(wxString::Format("%llu — ", static_cast<unsigned long long>(object.id.value())) +
                                  wxString::FromUTF8(object.name));
                if (object.id == previous)
                    restored = static_cast<int>(m_object_ids.size() - 1);
            }
        }
        if (!m_object_ids.empty())
            m_objects->SetSelection(restored == wxNOT_FOUND ? 0 : restored);
        append(std::string("SNAPSHOT source=") + source + ' ' + snapshot_text(current));
    }

    void append(const std::string& line)
    {
        std::ostringstream ordered;
        ordered << "PROBE seq=" << std::setw(4) << std::setfill('0') << ++m_log_sequence << ' ' << line;
        const std::string ordered_line = ordered.str();
        m_log->AppendText(wxString::FromUTF8(ordered_line) + "\n");
        if (m_log_file) {
            m_log_file << ordered_line << '\n';
            m_log_file.flush();
        }
    }

    IWorkspace& m_workspace;
    wxChoice* m_objects{nullptr};
    wxTextCtrl* m_log{nullptr};
    std::vector<ObjectId> m_object_ids;
    std::optional<ObjectId> m_preferred_object;
    WorkspaceSubscription m_subscription;
    std::ofstream m_log_file;
    std::uint64_t m_log_sequence{0};
};

} // namespace

wxWindow* show_workspace_probe(wxWindow* parent, IWorkspace& workspace)
{
    auto* probe = new WorkspaceProbeFrame(parent, workspace);
    probe->Show();
    return probe;
}

} // namespace Slic3r::GUI::JusPrin::Workspace
