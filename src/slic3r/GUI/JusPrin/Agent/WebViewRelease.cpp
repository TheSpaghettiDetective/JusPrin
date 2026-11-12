#include "WebViewRelease.hpp"

#include "slic3r/GUI/GUI_App.hpp"

#include <functional>
#include <utility>

namespace Slic3r::GUI::JusPrin {

namespace {

// How long to wait before looking again whether the install is over.
constexpr int kRetryMs = 50;

} // namespace

WebViewRelease::WebViewRelease(std::function<void()> release) : m_release(std::move(release)) {}

bool WebViewRelease::request()
{
    if (wxGetApp().is_adding_script_handler()) {
        StartOnce(kRetryMs);
        return false;
    }
    Stop();
    m_release();
    return true;
}

} // namespace Slic3r::GUI::JusPrin
