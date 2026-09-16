#pragma once

// Facts about a physical printer that no sensor reports and only the person
// can state: which plate is installed, that the spool was dried, that glue is
// on the bed, that the bed is clear.
//
// A fact is true until it expires, because the world changes without telling
// the app: a plate gets swapped, a dried spool picks up moisture. So every
// fact carries the moment it was confirmed and the moment it stops being
// current, and a fact past its expiry is simply not reported.
//
// Machine facts are app-level, not project-level, the same reasoning as the
// spool store: a project opened on another machine describes the same print,
// not the same printer. GUI-free and Orca-free, so it tests against a file.

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

// One stated fact. `printer` is the identity it belongs to -- a device id when
// the printer is known to the device manager, otherwise the printer preset --
// and `fact` is the name the caller chose for it ("plate", "spool_dry").
struct PrinterFact
{
    std::string printer;
    std::string fact;
    std::string value;
    std::string confirmed_at; // ISO 8601 UTC
    std::string expires_at;   // ISO 8601 UTC

    bool operator==(const PrinterFact& rhs) const
    {
        return printer == rhs.printer && fact == rhs.fact && value == rhs.value &&
               confirmed_at == rhs.confirmed_at && expires_at == rhs.expires_at;
    }
};

// What a caller confirms: the fact, its value, and how long it stays true.
struct FactConfirmation
{
    std::string          fact;
    std::string          value;
    std::chrono::seconds lifetime{std::chrono::hours(24)};
};

class PrinterFactsStore
{
public:
    struct Config
    {
        // Absolute path of the JSON file. Required.
        std::string file_path;
        // Injectable for deterministic tests.
        std::function<std::chrono::system_clock::time_point()> now;
    };

    // A missing file is an empty store. A file that does not parse is moved
    // aside to "<file_path>.corrupt" and reported through corrupt(), the same
    // rule as the spool store: facts are cheap to state again, but a damaged
    // file is evidence and is not overwritten.
    explicit PrinterFactsStore(Config config);

    PrinterFactsStore(const PrinterFactsStore&)            = delete;
    PrinterFactsStore& operator=(const PrinterFactsStore&) = delete;

    bool               corrupt() const { return m_corrupt; }
    const std::string& corrupt_reason() const { return m_corrupt_reason; }

    // The unexpired facts for one printer, ordered by fact name.
    std::vector<PrinterFact> current(const std::string& printer) const;

    // Records each confirmation for the printer -- a fact already stated is
    // replaced, never duplicated -- drops every fact that has expired, writes,
    // and returns the printer's current facts. Throws when the write fails: a
    // fact the person just stated must not silently fail to persist.
    std::vector<PrinterFact> confirm(const std::string& printer, const std::vector<FactConfirmation>& confirmations);

private:
    void write();

    Config                   m_config;
    std::vector<PrinterFact> m_facts;
    bool                     m_corrupt{false};
    std::string              m_corrupt_reason;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
