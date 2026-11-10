#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>

#include "SnipOverlay.hpp"
#include "ImageEditor.hpp"

// from ImageEditor.cpp
void PutBitmapToClipboard(HBITMAP hBmp);
#include "resource.h"

constexpr int HOTKEY_ID_CTRLSHIFTS = 1;
constexpr int HOTKEY_ID_PRINTSCREEN = 2;
constexpr UINT WM_TRAYICON = WM_APP + 1;

HINSTANCE g_hInstance = nullptr;
HWND g_hwndMain = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_hTrayIcon = nullptr;

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, 1, L"Capture (PrintScreen / Ctrl+Shift+S)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, L"Exit");

    // Хак для корректного закрытия меню при клике вне его
    SetForegroundWindow(hwnd);

    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(hMenu);
    PostMessageW(hwnd, WM_NULL, 0, 0); // Позволяет меню корректно закрыться

    if (cmd == 1) {
        PostMessageW(hwnd, WM_HOTKEY, HOTKEY_ID_CTRLSHIFTS, 0);
    }
    else if (cmd == 2) {
        // ИСПРАВЛЕНО: Используем WM_CLOSE вместо прямого DestroyWindow.
        // Это безопаснее для порядка обработки сообщений.
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID_CTRLSHIFTS || wParam == HOTKEY_ID_PRINTSCREEN) {
            RECT selected{};
            HBITMAP hBmp = SnipOverlay::CaptureRegion(g_hInstance, selected);
            if (hBmp) {
                PutBitmapToClipboard(hBmp);

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
            PostMessageW(hwnd, WM_HOTKEY, HOTKEY_ID_CTRLSHIFTS, 0);
        }
        break;

    case WM_CLOSE:
        // ИСПРАВЛЕНО: Добавлен явный обработчик закрытия
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID_CTRLSHIFTS);
        UnregisterHotKey(hwnd, HOTKEY_ID_PRINTSCREEN);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_hTrayIcon) {
            DestroyIcon(g_hTrayIcon);
            g_hTrayIcon = nullptr;
        }
        
        // Стандартный сигнал к завершению цикла сообщений
        PostQuitMessage(0);
        
        // ГАРАНТИРОВАННОЕ ЗАВЕРШЕНИЕ:
        // Если сообщение WM_QUIT было "съедено" вложенным циклом GetMessage 
        // (например, от окна ImageEditor, которое не успело полностью закрыться), 
        // главный цикл wWinMain продолжит висеть в ожидании. 
        // ExitProcess(0) после очистки ресурсов гарантирует, что процесс 
        // не останется висеть в диспетчере задач как "зомби".
        // Для утилиты скриншотов это абсолютно безопасно, так как все данные 
        // уже сохранены в буфер обмена или файл.
        ExitProcess(0);

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInstance = hInstance;

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

    if (!RegisterHotKey(g_hwndMain, HOTKEY_ID_CTRLSHIFTS, MOD_CONTROL | MOD_SHIFT, 'S')) {
        MessageBoxW(nullptr, L"Failed to register hotkey Ctrl+Shift+S.\nMaybe already in use.", L"SnipTool", MB_ICONWARNING);
    }

    if (!RegisterHotKey(g_hwndMain, HOTKEY_ID_PRINTSCREEN, 0, VK_SNAPSHOT)) {
        MessageBoxW(nullptr, L"Failed to register hotkey PrintScreen.\nMaybe already in use by another app.", L"SnipTool", MB_ICONWARNING);
    }

    g_hTrayIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_TRAYICON));
    if (!g_hTrayIcon) {
        g_hTrayIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }

    g_nid = { sizeof(g_nid) };
    g_nid.hWnd = g_hwndMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_hTrayIcon;
    wcscpy_s(g_nid.szTip, L"SnipTool — PrintScreen or Ctrl+Shift+S to capture");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}