#pragma once

// Marks one run of Plater::priv::load_files, the function every open and
// import of a file passes through (a project, Import, drag and drop, the
// command line, the Agent's tools), from its first line to its return, so a
// fork observer can listen to what Orca says in between. Fork-owned; Orca
// reaches it through the neutral forwarding header ProjectState.hpp.
//
// GUI thread only. A load inside a load belongs to the outer one.

#include <boost/filesystem/path.hpp>

#include <string>
#include <vector>

namespace Slic3r {

enum class LoadStrategy;

namespace GUI {

class FileLoadObserver
{
public:
    virtual ~FileLoadObserver() = default;
    virtual void file_load_started(const std::vector<boost::filesystem::path>& files, LoadStrategy strategy) = 0;
    virtual void file_settings_applied() = 0;
    virtual void file_load_finished() = 0;
    virtual void file_open_started() = 0;
    virtual void file_open_finished() = 0;
    virtual bool file_load_message(const std::string& text, const char* source) = 0;
    virtual bool file_open_can_replace_project() = 0;
};

// One observer at a time; nullptr removes it.
void set_file_load_observer(FileLoadObserver* observer);

// An observer handling a load-time notification or error owns its presentation.
// False leaves the normal Orca presentation in place.
bool report_file_load_message(const std::string& text, const char* source);

class FileLoadScope
{
public:
    FileLoadScope(const std::vector<boost::filesystem::path>& files, LoadStrategy strategy);
    ~FileLoadScope();
    void settings_applied() const;
    FileLoadScope(const FileLoadScope&)            = delete;
    FileLoadScope& operator=(const FileLoadScope&) = delete;

private:
    FileLoadObserver* m_observer{nullptr}; // the one told this load started
};

// Covers the outer GUI operation, including dialogs before load_files starts.
// Nested entry points belong to the same operation.
class FileOpenScope
{
public:
    explicit FileOpenScope(bool enabled = true);
    ~FileOpenScope();
    bool can_replace_project() const;
    FileOpenScope(const FileOpenScope&)            = delete;
    FileOpenScope& operator=(const FileOpenScope&) = delete;

private:
    FileLoadObserver* m_observer{nullptr};
    bool              m_enabled{false};
};

} // namespace GUI
} // namespace Slic3r
