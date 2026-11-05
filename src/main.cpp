#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>

#include "SnipOverlay.hpp"
#include "ImageEditor.hpp"

// Hotkey ID
constexpr int HOTKEY_ID = 1;

// Global instance for message handling
HINSTANCE g_hInstance = nullptr;
HWND g_hwndMain = nullptr;

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            // Start snipping
            RECT selected{};
            HBITMAP hBmp = SnipOverlay::CaptureRegion(g_hInstance, selected);
            if (hBmp) {
                ImageEditor editor(g_hInstance, hBmp, selected);
                editor.Run();
                DeleteObject(hBmp);
            }
        }
        break;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInstance = hInstance;

    // Register a hidden message-only window to receive hotkeys
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SnipToolMainClass";
    RegisterClassExW(&wc);

    g_hwndMain = CreateWindowExW(
        0, L"SnipToolMainClass", L"SnipTool",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInstance, nullptr);

    if (!g_hwndMain) {
        MessageBoxW(nullptr, L"Failed to create main window", L"SnipTool", MB_ICONERROR);
        return 1;
    }

    // Register global hotkey: Ctrl + Shift + S
    if (!RegisterHotKey(g_hwndMain, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 'S')) {
        MessageBoxW(nullptr, L"Failed to register hotkey Ctrl+Shift+S.\nMaybe already in use.", L"SnipTool", MB_ICONWARNING);
    }

    // Optional: tray icon for quit
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwndMain;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_APP + 1;
    nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
    wcscpy_s(nid.szTip, L"SnipTool — Ctrl+Shift+S to capture");
    Shell_NotifyIconW(NIM_ADD, &nid);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_APP + 1) {
            if (msg.lParam == WM_RBUTTONUP || msg.lParam == WM_LBUTTONUP) {
                // Simple quit on tray click
                if (MessageBoxW(nullptr, L"Exit SnipTool?", L"SnipTool", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    DestroyWindow(g_hwndMain);
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    return static_cast<int>(msg.wParam);
}