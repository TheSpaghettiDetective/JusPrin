#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/JusPrin/Shell/DroppedFileUrl.hpp"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/filesys.h>

using Slic3r::GUI::JusPrin::dropped_file;

namespace {

wxString url_of(const wxString& path) { return wxFileSystem::FileNameToURL(wxFileName(path)); }

wxString in_temp(const wxString& name) { return wxFileName(wxFileName::GetTempDir(), name).GetFullPath(); }

} // namespace

TEST_CASE("a navigation to a local file other than the page is a dropped file", "[DroppedFileUrl]")
{
    const wxString page  = in_temp("page/index.html");
    const wxString photo = in_temp("stringy benchy?.png");

    const auto file = dropped_file(url_of(photo), page);
    REQUIRE(file.has_value());
    CHECK(wxFileName(*file).SameAs(wxFileName(photo)));
}

TEST_CASE("the page's own address is not a dropped file", "[DroppedFileUrl]")
{
    const wxString page = in_temp("page/index.html");

    CHECK_FALSE(dropped_file(url_of(page), page).has_value());
    CHECK_FALSE(dropped_file(url_of(page) + "?panel=printer", page).has_value());
}

TEST_CASE("a page served over http and its frames are not dropped files", "[DroppedFileUrl]")
{
    const wxString page = in_temp("page/index.html");

    CHECK_FALSE(dropped_file("http://localhost:5173/", page).has_value());
    CHECK_FALSE(dropped_file("about:srcdoc", page).has_value());
}
