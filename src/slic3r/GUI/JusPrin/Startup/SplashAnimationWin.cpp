#include "SplashAnimation.hpp"

#include <boost/log/trivial.hpp>

#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/imagwebp.h>
#include <wx/window.h>

#include <wx/msw/wrapwin.h>

#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin {

namespace {

// What the animation thread needs, taken from the splash on the main thread.
// The thread touches no window of wxWidgets'; it only scales the frames, which
// are its own.
struct Playback
{
    HWND                              splash; // the splash's top-level window
    POINT                             at;     // the box's top-left, in screen pixels
    SIZE                              size;
    wxColour                          background;
    std::vector<wxWebPAnimationFrame> frames;
};

// frame scaled to the box and drawn over background, as an opaque 32-bit DIB.
HBITMAP dib(HDC screen, const wxImage& frame, SIZE size, const wxColour& background)
{
    const wxImage scaled = frame.Scale(size.cx, size.cy, wxIMAGE_QUALITY_HIGH);
    BITMAPINFO    info{};
    info.bmiHeader.biSize        = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth       = size.cx;
    info.bmiHeader.biHeight      = -size.cy; // rows top first, as wxImage has them
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void*         bits           = nullptr;
    const HBITMAP bitmap         = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr)
        return nullptr;
    const unsigned char* rgb   = scaled.GetData();
    const unsigned char* alpha = scaled.GetAlpha();
    auto*                out   = static_cast<std::uint32_t*>(bits);
    const auto over = [](std::uint32_t ink, std::uint32_t under, std::uint32_t a) { return (ink * a + under * (255 - a)) / 255; };
    for (long i = 0; i < long(size.cx) * size.cy; ++i) {
        const std::uint32_t a = alpha != nullptr ? alpha[i] : 255;
        out[i] = 0xFFu << 24 | over(rgb[3 * i], background.Red(), a) << 16 | over(rgb[3 * i + 1], background.Green(), a) << 8 |
                 over(rgb[3 * i + 2], background.Blue(), a);
    }
    return bitmap;
}

LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

// Runs on its own thread: a layered window over the splash's name, with its
// own message loop and a timer that steps through the frames, until the
// splash is hidden or gone. Each frame is scaled the first time it shows, so
// the first one is up within milliseconds of the splash.
void play(Playback p)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW       type{};
    type.lpfnWndProc   = overlay_proc;
    type.hInstance     = instance;
    type.lpszClassName = L"JusPrinSplashName";
    RegisterClassW(&type);
    // No owner: an owner on another thread would attach this thread's input
    // to the main thread's, which is blocked. Clicks pass through to the
    // splash, and the window never takes focus.
    const HWND overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                         type.lpszClassName, L"", WS_POPUP, p.at.x, p.at.y, p.size.cx, p.size.cy, nullptr,
                                         nullptr, instance, nullptr);
    if (overlay == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "Splash name animation: CreateWindowEx failed, error " << GetLastError();
        return;
    }

    const HDC            screen = GetDC(nullptr);
    const HDC            memory = CreateCompatibleDC(screen);
    std::vector<HBITMAP> bitmaps(p.frames.size(), nullptr);
    size_t               current = 0;
    // Returns whether the frame could be shown.
    const auto show = [&] {
        if (bitmaps[current] == nullptr) {
            bitmaps[current] = dib(screen, p.frames[current].image, p.size, p.background);
            if (bitmaps[current] == nullptr) {
                BOOST_LOG_TRIVIAL(error) << "Splash name animation: CreateDIBSection failed, error " << GetLastError();
                return false;
            }
            p.frames[current].image.Destroy();
        }
        const HGDIOBJ before = SelectObject(memory, bitmaps[current]);
        POINT         origin{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(overlay, screen, &p.at, &p.size, memory, &origin, 0, &blend, ULW_ALPHA);
        SelectObject(memory, before);
        // Just above the splash, wherever the splash is in the stack: clicking
        // it raises it, and another app's window may cover both.
        const HWND above = GetWindow(p.splash, GW_HWNDPREV);
        if (above != overlay)
            SetWindowPos(overlay, above != nullptr ? above : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return true;
    };
    if (show()) {
        ShowWindow(overlay, SW_SHOWNOACTIVATE);
        SetTimer(overlay, 1, p.frames[current].duration, nullptr);
    } else
        DestroyWindow(overlay);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && message.hwnd == overlay) {
            current = (current + 1) % p.frames.size();
            if (!IsWindow(p.splash) || !IsWindowVisible(p.splash) || !show())
                DestroyWindow(overlay);
            else
                SetTimer(overlay, 1, p.frames[current].duration, nullptr);
            continue;
        }
        DispatchMessageW(&message);
    }

    for (const HBITMAP bitmap : bitmaps)
        if (bitmap != nullptr)
            DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

} // namespace

void add_splash_animation(wxWindow& window, const wxRect& box, std::vector<wxWebPAnimationFrame> frames)
{
    const wxPoint at = window.ClientToScreen(box.GetTopLeft());
    // Detached: it ends by itself within a frame of the splash being hidden.
    std::thread(play, Playback{GetAncestor(static_cast<HWND>(window.GetHWND()), GA_ROOT), POINT{at.x, at.y},
                               SIZE{box.width, box.height}, window.GetBackgroundColour(), std::move(frames)})
        .detach();
}

} // namespace Slic3r::GUI::JusPrin
