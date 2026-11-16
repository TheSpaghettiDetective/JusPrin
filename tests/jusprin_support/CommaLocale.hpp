#pragma once

#include <clocale>
#include <string>

// Switches the C library's number format to one that writes "0,4", as
// wxLocale does at startup when the app runs in Portuguese, German, French
// and most other languages. Restores the previous format when it goes.
class CommaLocale
{
public:
    CommaLocale() : m_previous(std::setlocale(LC_NUMERIC, nullptr))
    {
        // POSIX names first, then the names Windows takes.
        for (const char* name : {"pt_BR.UTF-8", "de_DE.UTF-8", "pt_BR", "de_DE", "pt-BR", "de-DE"})
            if (std::setlocale(LC_NUMERIC, name) != nullptr) {
                m_active = true;
                break;
            }
    }
    ~CommaLocale() { std::setlocale(LC_NUMERIC, m_previous.c_str()); }

    CommaLocale(const CommaLocale&)            = delete;
    CommaLocale& operator=(const CommaLocale&) = delete;

    // False where the machine has none of these locales installed.
    bool active() const { return m_active; }

private:
    std::string m_previous;
    bool        m_active{false};
};
