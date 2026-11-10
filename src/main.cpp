#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>

#include "SnipOverlay.hpp"
#include "ImageEditor.hpp"
#include "resource.h" 

// Hotkey ID
constexpr int HOTKEY_ID = 1;
constexpr UINT WM_TRAYICON = WM_APP + 1;

// Global instance for message handling
HINSTANCE g_hInstance = nullptr;
HWND g_hwndMain = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_hAppIcon = nullptr;

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, 1, L"Capture (Ctrl+Shift+S)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, L"Exit");

    // Required so the menu closes correctly when clicking outside
    SetForegroundWindow(hwnd);

    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(hMenu);

    // Required after TrackPopupMenu
    PostMessageW(hwnd, WM_NULL, 0, 0);

    if (cmd == 1) {
        // Trigger capture the same way as hotkey
        PostMessageW(hwnd, WM_HOTKEY, HOTKEY_ID, 0);
    }
    else if (cmd == 2) {
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            RECT selected{};
            HBITMAP hBmp = SnipOverlay::CaptureRegion(g_hInstance, selected);
            if (hBmp) {
                ImageEditor editor(g_hInstance, hBmp, selected);
                editor.Run();
                DeleteObject(hBmp);
            }
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        }
        else if (lParam == WM_LBUTTONDBLCLK) {
            // Double-click also starts capture
            PostMessageW(hwnd, WM_HOTKEY, HOTKEY_ID, 0);
        }
        break;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInstance = hInstance;

    g_hAppIcon = (HICON)LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        32, 32,
        LR_DEFAULTCOLOR);

    // Register a hidden message-only window to receive hotkeys
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = g_hAppIcon;
    wc.hIconSm = g_hAppIcon;
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

    // Tray icon
    g_nid = { sizeof(g_nid) };
    g_nid.hWnd = g_hwndMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    //g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
    g_nid.hIcon = g_hAppIcon;
    wcscpy_s(g_nid.szTip, L"SnipTool — Ctrl+Shift+S to capture");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}