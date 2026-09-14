// main.cpp — Win32 host window + WebView2 lifecycle for OGFN Launcher.
#include "bridge.h"
#include "accounts.h"
#include "config.h"
#include "log.h"
#include "util.h"

#include <windows.h>
#include <dwmapi.h>

#include <wrl/event.h>
#include <wrl/module.h>
#include <wrl/wrappers/corewrappers.h>
#include <wrl/client.h>
#include <ShlObj.h>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include <filesystem>
#include <string>

using namespace Microsoft::WRL;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static const wchar_t kWindowClass[] = L"OGFNLauncherWindow";
static const wchar_t kWindowTitle[] = L"OGFN Launcher";

static ComPtr<ICoreWebView2Environment> g_env;
static ComPtr<ICoreWebView2Controller>  g_controller;
static ComPtr<ICoreWebView2>            g_webview;
static bool g_ready = false;

// ---------------------------------------------------------------- helpers

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

static bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// index.html next to the exe (CMake copies src/web there after every build).
static std::wstring WebIndexPath() {
    return ExeDir() + L"\\web\\index.html";
}

static void ResizeWebView(HWND hwnd) {
    if (!g_controller) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    g_controller->put_Bounds(rc);
}

static void EnableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                          sizeof(dark));
}

static void SetDarkBackground() {
    // Avoid a white flash before the HTML paints (matches --bg base color).
    ComPtr<ICoreWebView2Controller2> c2;
    if (g_controller && SUCCEEDED(g_controller.As(&c2))) {
        COREWEBVIEW2_COLOR color{0, 11, 14, 20}; // a, r, g, b — opaque
        c2->put_DefaultBackgroundColor(color);
    }
}

// ---------------------------------------------------------------- bridge

static void HandleWebMessage(const wchar_t* jsonUtf16) {
    if (!g_webview) return;
    std::string utf8 = util::ToUtf8(jsonUtf16 ? jsonUtf16 : L"");

    nlohmann::json request = nlohmann::json::object();
    try {
        request = nlohmann::json::parse(utf8);
    } catch (const std::exception& e) {
        ogfnlog::Warn(std::string("webmessage: bad json: ") + e.what());
        return;
    }

    nlohmann::json response = bridge::Handle(request);

    std::wstring out = util::ToUtf16(response.dump());
    g_webview->PostWebMessageAsJson(out.c_str());
}

// ---------------------------------------------------------------- webview

static bool RegisterWebView2Host(HWND hwnd) {
    // User data folder inside our %APPDATA% (writable even under Program Files).
    std::wstring userData = util::AppDataDir() + L"\\WebView2";

    auto options = Make<CoreWebView2EnvironmentOptions>();
    std::wstring lang = L"en-US";
    options->put_Language(lang.c_str());

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    ogfnlog::Error("WebView2 environment creation failed.");
                    MessageBoxW(hwnd,
                        L"Could not create the WebView2 environment.\n\n"
                        L"Install the WebView2 Runtime (evergreen) from:\n"
                        L"https://developer.microsoft.com/microsoft-edge/webview2/",
                        L"OGFN Launcher", MB_OK | MB_ICONERROR);
                    PostQuitMessage(1);
                    return E_FAIL;
                }
                g_env = env;

                return env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT result2, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(result2) || !ctrl) {
                                ogfnlog::Error("WebView2 controller creation failed.");
                                PostQuitMessage(1);
                                return E_FAIL;
                            }
                            g_controller = ctrl;
                            ctrl->get_CoreWebView2(&g_webview);

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(g_webview->get_Settings(&settings))) {
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                            }

                            // JS -> native pipe.
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2* sender,
                                       ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (SUCCEEDED(args->get_WebMessageAsJson(&raw)) && raw) {
                                            HandleWebMessage(raw);
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }).Get(),
                                nullptr);

                            SetDarkBackground();
                            ResizeWebView(hwnd);

                            std::wstring index = WebIndexPath();
                            if (FileExists(index)) {
                                g_webview->Navigate(
                                    (L"file:///" + index).c_str());
                            } else {
                                ogfnlog::Error("web/index.html missing next to exe");
                                g_webview->Navigate(
                                    L"data:text/html;charset=utf-8,<body style='"
                                    L"font-family:monospace;background:%230b0e14;"
                                    L"color:%23e6e9f0;padding:40px'>"
                                    L"OGFN Launcher: web assets not found "
                                    L"(web/index.html missing next to the exe).</body>");
                            }

                            g_ready = true;
                            ogfnlog::Info("WebView2 host ready");
                            return S_OK;
                        }).Get());
            }).Get());

    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------- window

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            ResizeWebView(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize = {960, 620};
            return 0;
        }
        case WM_DESTROY:
            if (g_webview)   g_webview->Stop();
            if (g_controller) g_controller->Close();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static int Run() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = CreateSolidBrush(RGB(11, 14, 20));
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;

    // Centered default window.
    const LONG W = 1200, H = 780;
    LONG x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT work{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        x = work.left + ((work.right - work.left) - W) / 2;
        y = work.top + ((work.bottom - work.top) - H) / 2;
    }

    HWND hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
                                WS_OVERLAPPEDWINDOW, x, y, W, H,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 1;
    EnableDarkTitleBar(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    if (!RegisterWebView2Host(hwnd)) {
        MessageBoxW(hwnd,
            L"Failed to start WebView2. Install the WebView2 Runtime from:\n"
            L"https://developer.microsoft.com/microsoft-edge/webview2/",
            L"OGFN Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Per-monitor v2 DPI (falls back gracefully on old Win10).
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setCtx = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setCtx) setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;

    util::EnsureDir(util::AppDataDir());
    ogfnlog::Init();
    config::Load();
    accounts::Init();

    int rc = Run();

    CoUninitialize();
    return rc;
}
