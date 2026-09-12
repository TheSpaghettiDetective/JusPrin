/* Minimal WGL smoke test: loads the bundled Mesa the same way OrcaSlicer's
   launcher does, makes a context, and draws frames. It answers one question --
   does the gallium driver work at all on this machine, independent of
   OrcaSlicer? -- so a canvas failure can be attributed to the driver or to us.

   Deliberately not part of the CMake build; it is a diagnostic run by hand.
   Build and run it from a directory holding a provisioned Mesa pair (see
   provision-mesa-windows.ps1):

     cl /nologo wgl-smoke-test.c /Fe:wgl-smoke-test.exe gdi32.lib user32.lib
     wgl-smoke-test.exe          # legacy context
     wgl-smoke-test.exe core     # the core 4.2 context the canvas requests

   Both should print ALL FRAMES OK and exit 0, under GALLIUM_DRIVER=llvmpipe and
   with it unset. Background: agent-docs/jusprin/headless-gl-handoff.md */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef HGLRC (WINAPI *PFN_wglCreateContext)(HDC);
typedef BOOL  (WINAPI *PFN_wglMakeCurrent)(HDC, HGLRC);
typedef PROC  (WINAPI *PFN_wglGetProcAddress)(LPCSTR);
typedef const unsigned char* (WINAPI *PFN_glGetString)(unsigned int);
typedef void  (WINAPI *PFN_glClearColor)(float, float, float, float);
typedef void  (WINAPI *PFN_glClear)(unsigned int);
typedef void  (WINAPI *PFN_glFinish)(void);
typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);

#define GL_VERSION_  0x1F02
#define GL_RENDERER_ 0x1F01
#define GL_COLOR_BUFFER_BIT_ 0x00004000

#define BACKSLASH 0x5C

int main(int argc, char** argv)
{
    int want_core = (argc > 1 && strcmp(argv[1], "core") == 0);

    wchar_t path[MAX_PATH + 1];
    wchar_t* slash;
    memset(path, 0, sizeof(path));
    GetModuleFileNameW(NULL, path, MAX_PATH);
    slash = wcsrchr(path, (wchar_t)BACKSLASH);
    if (slash) slash[1] = 0;
    /* LoadLibrary accepts forward slashes, so no escaping needed here. */
    wcscat(path, L"mesa/opengl32.dll");

    HMODULE gl = LoadLibraryExW(path, NULL, 0);
    wprintf(L"loading %s -> %hs\n", path, gl ? "ok" : "FAILED");
    fflush(stdout);
    if (!gl) return 2;

    PFN_wglCreateContext  p_wglCreateContext  = (PFN_wglCreateContext) GetProcAddress(gl, "wglCreateContext");
    PFN_wglMakeCurrent    p_wglMakeCurrent    = (PFN_wglMakeCurrent)   GetProcAddress(gl, "wglMakeCurrent");
    PFN_wglGetProcAddress p_wglGetProcAddress = (PFN_wglGetProcAddress)GetProcAddress(gl, "wglGetProcAddress");
    PFN_glGetString       p_glGetString       = (PFN_glGetString)      GetProcAddress(gl, "glGetString");
    PFN_glClearColor      p_glClearColor      = (PFN_glClearColor)     GetProcAddress(gl, "glClearColor");
    PFN_glClear           p_glClear           = (PFN_glClear)          GetProcAddress(gl, "glClear");
    PFN_glFinish          p_glFinish          = (PFN_glFinish)         GetProcAddress(gl, "glFinish");

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"gltest";
    wc.style = CS_OWNDC;
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"gltest", L"gltest", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              0, 0, 800, 600, NULL, NULL, wc.hInstance, NULL);
    HDC dc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;
    pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);

    HGLRC rc = p_wglCreateContext(dc);
    printf("legacy context: %s\n", rc ? "ok" : "NULL");
    if (!rc) return 3;
    p_wglMakeCurrent(dc, rc);
    printf("GL_VERSION : %s\n", (const char*)p_glGetString(GL_VERSION_));
    printf("GL_RENDERER: %s\n", (const char*)p_glGetString(GL_RENDERER_));
    fflush(stdout);

    if (want_core) {
        /* The same shape of request OrcaSlicer's canvas makes: a 4.2 core profile. */
        PFN_wglCreateContextAttribsARB p_attribs =
            (PFN_wglCreateContextAttribsARB)p_wglGetProcAddress("wglCreateContextAttribsARB");
        printf("wglCreateContextAttribsARB: %s\n", p_attribs ? "present" : "MISSING");
        fflush(stdout);
        if (p_attribs) {
            int attrs[7];
            attrs[0] = 0x2091; attrs[1] = 4;          /* CONTEXT_MAJOR_VERSION */
            attrs[2] = 0x2092; attrs[3] = 2;          /* CONTEXT_MINOR_VERSION */
            attrs[4] = 0x9126; attrs[5] = 0x00000001; /* PROFILE_MASK = CORE   */
            attrs[6] = 0;
            p_wglMakeCurrent(NULL, NULL);
            HGLRC core = p_attribs(dc, NULL, attrs);
            printf("core 4.2 context: %s\n", core ? "ok" : "NULL");
            fflush(stdout);
            if (!core) return 4;
            p_wglMakeCurrent(dc, core);
            printf("GL_VERSION (core): %s\n", (const char*)p_glGetString(GL_VERSION_));
            fflush(stdout);
        }
    }

    int i;
    for (i = 0; i < 30; ++i) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        p_glClearColor((i % 2) ? 0.1f : 0.6f, 0.2f, 0.4f, 1.0f);
        p_glClear(GL_COLOR_BUFFER_BIT_);
        p_glFinish();
        SwapBuffers(dc);
        printf("frame %d ok\n", i); fflush(stdout);
        Sleep(30);
    }
    printf("ALL FRAMES OK\n");
    return 0;
}
