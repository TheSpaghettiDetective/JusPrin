#include "FileLoads.hpp"

#include <wx/debug.h>
#include <wx/thread.h>

#include <cstddef>

namespace Slic3r::GUI {

namespace {
FileLoadObserver* g_observer = nullptr;
std::size_t       g_depth    = 0;
std::size_t       g_open_depth = 0;
} // namespace

void set_file_load_observer(FileLoadObserver* observer)
{
    wxASSERT(wxIsMainThread());
    wxASSERT(observer == nullptr || g_observer == nullptr);
    g_observer = observer;
}

bool report_file_load_message(const std::string& text, const char* source)
{
    if (!wxIsMainThread())
        return false;
    return g_observer != nullptr && (g_depth != 0 || g_open_depth != 0) &&
           g_observer->file_load_message(text, source);
}

FileLoadScope::FileLoadScope(const std::vector<boost::filesystem::path>& files, LoadStrategy strategy)
{
    wxASSERT(wxIsMainThread());
    if (g_depth++ == 0 && g_observer != nullptr) {
        m_observer = g_observer;
        m_observer->file_load_started(files, strategy);
    }
}

FileLoadScope::~FileLoadScope()
{
    --g_depth;
    // The observer may have gone during the load; only the one told of the
    // start is told of the end.
    if (m_observer != nullptr && m_observer == g_observer)
        m_observer->file_load_finished();
}

void FileLoadScope::settings_applied() const
{
    if (m_observer != nullptr && m_observer == g_observer)
        m_observer->file_settings_applied();
}

FileOpenScope::FileOpenScope(bool enabled) : m_enabled(enabled)
{
    wxASSERT(wxIsMainThread());
    if (!m_enabled)
        return;
    if (g_open_depth++ == 0 && g_observer != nullptr) {
        m_observer = g_observer;
        m_observer->file_open_started();
    }
}

FileOpenScope::~FileOpenScope()
{
    if (!m_enabled)
        return;
    --g_open_depth;
    if (m_observer != nullptr && m_observer == g_observer)
        m_observer->file_open_finished();
}

bool FileOpenScope::can_replace_project() const
{
    return !m_enabled || g_observer == nullptr || g_observer->file_open_can_replace_project();
}

} // namespace Slic3r::GUI
