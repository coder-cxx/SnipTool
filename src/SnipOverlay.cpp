#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SnipOverlay.hpp"
#include <windowsx.h>
#include <algorithm>
#include <vector>
#include <string>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

struct OverlayState {
    HWND hwnd = nullptr;
    HINSTANCE hInst = nullptr;
    bool selecting = false;
    POINT start{ 0, 0 };
    POINT current{ 0, 0 };
    bool finished = false;
    bool cancelled = false;
    RECT result{ 0, 0, 0, 0 };
    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    HBITMAP oldBmp = nullptr;
    int screenW = 0;
    int screenH = 0;
    int screenLeft = 0;
    int screenTop = 0;
};

OverlayState g_state;

void CleanupMem() {
    if (g_state.memDC) {
        if (g_state.oldBmp) SelectObject(g_state.memDC, g_state.oldBmp);
        if (g_state.memBmp) DeleteObject(g_state.memBmp);
        DeleteDC(g_state.memDC);
        g_state.memDC = nullptr;
        g_state.memBmp = nullptr;
        g_state.oldBmp = nullptr;
    }
}

bool IsWatermarkWindow(HWND hwnd) {
    if (!hwnd) return true;
    
    wchar_t className[256] = {0};
    wchar_t windowName[256] = {0};
    GetClassNameW(hwnd, className, 256);
    GetWindowTextW(hwnd, windowName, 256);

    std::wstring cls(className);
    std::wstring title(windowName);

    const std::vector<std::wstring> blacklist = {
        L"Watermark", L"Overlay", L"ActivateWindows", L"GameBar", 
        L"Xbox", L"Toast", L"Notification", L"SnipOverlayClass"
    };

    for (const auto& banned : blacklist) {
        if (cls.find(banned) != std::wstring::npos || title.find(banned) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

struct WindowInfo {
    HWND hwnd;
    RECT rect;
};

BOOL CALLBACK CollectWindows(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsWatermarkWindow(hwnd)) return TRUE;
    if (hwnd == g_state.hwnd) return TRUE;
    if (hwnd == GetShellWindow()) return TRUE;
    
    wchar_t cls[256] = {0};
    GetClassNameW(hwnd, cls, 256);
    
    if (wcscmp(cls, L"Progman") == 0 || 
        wcscmp(cls, L"WorkerW") == 0 || 
        wcscmp(cls, L"Shell_TrayWnd") == 0 ||
        wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0) {
        return TRUE; 
    }

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return TRUE;
    
    if (rc.left >= g_state.screenW + g_state.screenLeft || 
        rc.top >= g_state.screenH + g_state.screenTop || 
        rc.right <= g_state.screenLeft || 
        rc.bottom <= g_state.screenTop || 
        IsIconic(hwnd) ||
        (rc.right - rc.left) <= 0 || 
        (rc.bottom - rc.top) <= 0) {
        return TRUE;
    }

    auto& windows = *reinterpret_cast<std::vector<WindowInfo>*>(lParam);
    windows.push_back({hwnd, rc});
    
    return TRUE;
}

// Проверяет, есть ли в битмапе непустые пиксели
bool HasContent(HBITMAP hBmp, HDC hdc, int width, int height) {
    BITMAPINFO bi{ sizeof(BITMAPINFOHEADER) };
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint32_t> pixels((size_t)width * height);
    GetDIBits(hdc, hBmp, 0, height, pixels.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    // Проверяем хотя бы 1% пикселей
    size_t sampleSize = pixels.size() / 100;
    if (sampleSize == 0) sampleSize = 1;
    
    for (size_t i = 0; i < sampleSize; ++i) {
        if (pixels[i] & 0x00FFFFFF) return true; // Есть ненулевой RGB
    }
    return false;
}

// Гибридный захват: сначала PrintWindow, если пусто - BitBlt
HBITMAP CaptureWindowContent(HWND hwnd, int width, int height) {
    HDC hdcScreen = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBmp);

    // Попытка 1: PrintWindow (чистый захват, без оверлеев)
    BOOL pwSuccess = PrintWindow(hwnd, memDC, PW_RENDERFULLCONTENT);
    
    bool hasContent = false;
    if (pwSuccess) {
        hasContent = HasContent(hBmp, memDC, width, height);
    }

    // Попытка 2: Если PrintWindow дал пустой результат - используем BitBlt (для GPU-приложений)
    if (!hasContent) {
        HDC hdcWindow = GetDC(hwnd);
        if (hdcWindow) {
            BitBlt(memDC, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY | CAPTUREBLT);
            ReleaseDC(hwnd, hdcWindow);
        }
    }

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, hdcScreen);

    return hBmp;
}

void DrawWindowToCanvas(const WindowInfo& winInfo) {
    HWND hwnd = winInfo.hwnd;
    const RECT& rc = winInfo.rect;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HBITMAP hBmpWindow = CaptureWindowContent(hwnd, width, height);
    if (!hBmpWindow) return;

    int drawX = rc.left - g_state.screenLeft;
    int drawY = rc.top - g_state.screenTop;

    if (drawX < 0 || drawY < 0 || drawX >= g_state.screenW || drawY >= g_state.screenH) {
        DeleteObject(hBmpWindow);
        return;
    }

    int drawW = (std::min)(width, g_state.screenW - drawX);
    int drawH = (std::min)(height, g_state.screenH - drawY);

    HDC hdcWindow = GetDC(hwnd);
    HDC memDCWindow = CreateCompatibleDC(hdcWindow);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDCWindow, hBmpWindow);

    // Проверяем, есть ли альфа-канал
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    bool isLayered = (exStyle & WS_EX_LAYERED) != 0;

    if (isLayered) {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(g_state.memDC, drawX, drawY, drawW, drawH, 
                   memDCWindow, 0, 0, drawW, drawH, bf);
    } else {
        BitBlt(g_state.memDC, drawX, drawY, drawW, drawH, memDCWindow, 0, 0, SRCCOPY);
    }

    SelectObject(memDCWindow, oldBmp);
    DeleteDC(memDCWindow);
    ReleaseDC(hwnd, hdcWindow);
    DeleteObject(hBmpWindow);
}

void CaptureDesktopBackground() {
    HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 30));
    RECT rc = {0, 0, g_state.screenW, g_state.screenH};
    FillRect(g_state.memDC, &rc, hBrush);
    DeleteObject(hBrush);

    HWND hDesktop = GetDesktopWindow();
    if (hDesktop) {
        PrintWindow(hDesktop, g_state.memDC, PW_RENDERFULLCONTENT);
    }
}

bool CaptureFullScreen() {
    CleanupMem();
    g_state.screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_state.screenTop  = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_state.screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_state.screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return false;
    
    g_state.memDC = CreateCompatibleDC(screenDC);
    g_state.memBmp = CreateCompatibleBitmap(screenDC, g_state.screenW, g_state.screenH);
    g_state.oldBmp = (HBITMAP)SelectObject(g_state.memDC, g_state.memBmp);

    CaptureDesktopBackground();

    std::vector<WindowInfo> windows;
    EnumWindows(CollectWindows, reinterpret_cast<LPARAM>(&windows));

    std::reverse(windows.begin(), windows.end());

    for (const auto& winInfo : windows) {
        DrawWindowToCanvas(winInfo);
    }

    ReleaseDC(nullptr, screenDC);
    return g_state.memBmp != nullptr;
}

HBITMAP CreateARGBSurface(int w, int h, void** bits) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    return bmp;
}

void UpdateLayeredContent(HWND hwnd) {
    void* bits = nullptr;
    HBITMAP argbBmp = CreateARGBSurface(g_state.screenW, g_state.screenH, &bits);
    if (!argbBmp || !bits) return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_state.screenW;
    bmi.bmiHeader.biHeight = -g_state.screenH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(nullptr);
    GetDIBits(hdcScreen, g_state.memBmp, 0, g_state.screenH, bits, &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdcScreen);

    uint8_t* p = static_cast<uint8_t*>(bits);
    const int total = g_state.screenW * g_state.screenH;

    for (int i = 0; i < total; ++i) {
        p[0] = static_cast<uint8_t>(p[0] * 0.30);
        p[1] = static_cast<uint8_t>(p[1] * 0.30);
        p[2] = static_cast<uint8_t>(p[2] * 0.30);
        p[3] = 255;
        p += 4;
    }

    if (g_state.selecting || g_state.finished) {
        int x1 = (std::min)(g_state.start.x, g_state.current.x);
        int y1 = (std::min)(g_state.start.y, g_state.current.y);
        int x2 = (std::max)(g_state.start.x, g_state.current.x);
        int y2 = (std::max)(g_state.start.y, g_state.current.y);

        x1 = (std::max)(0, x1); y1 = (std::max)(0, y1);
        x2 = (std::min)(g_state.screenW, x2); y2 = (std::min)(g_state.screenH, y2);

        std::vector<uint8_t> orig(g_state.screenW * g_state.screenH * 4);
        HDC hdc = GetDC(nullptr);
        GetDIBits(hdc, g_state.memBmp, 0, g_state.screenH, orig.data(), &bmi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);

        for (int y = y1; y < y2; ++y) {
            for (int x = x1; x < x2; ++x) {
                int idx = (y * g_state.screenW + x) * 4;
                uint8_t* dst = static_cast<uint8_t*>(bits) + idx;
                dst[0] = orig[idx + 0];
                dst[1] = orig[idx + 1];
                dst[2] = orig[idx + 2];
                dst[3] = 255;
            }
        }

        auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
            if (x < 0 || y < 0 || x >= g_state.screenW || y >= g_state.screenH) return;
            int idx = (y * g_state.screenW + x) * 4;
            uint8_t* d = static_cast<uint8_t*>(bits) + idx;
            d[0] = b; d[1] = g; d[2] = r; d[3] = 255;
        };

        for (int t = 0; t < 2; ++t) {
            for (int x = x1; x < x2; ++x) {
                setPixel(x, y1 + t, 0, 120, 215);
                setPixel(x, y2 - 1 - t, 0, 120, 215);
            }
            for (int y = y1; y < y2; ++y) {
                setPixel(x1 + t, y, 0, 120, 215);
                setPixel(x2 - 1 - t, y, 0, 120, 215);
            }
        }
    }

    HDC hdcScreen2 = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen2);
    HBITMAP old = (HBITMAP)SelectObject(hdcMem, argbBmp);

    POINT ptSrc{ 0, 0 };
    POINT ptDst{ g_state.screenLeft, g_state.screenTop };
    SIZE size{ g_state.screenW, g_state.screenH };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(hwnd, hdcScreen2, &ptDst, &size, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, old);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen2);
    DeleteObject(argbBmp);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        return 0;
    case WM_LBUTTONDOWN:
        g_state.selecting = true;
        g_state.start.x = GET_X_LPARAM(lParam);
        g_state.start.y = GET_Y_LPARAM(lParam);
        g_state.current = g_state.start;
        SetCapture(hwnd);
        UpdateLayeredContent(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (g_state.selecting) {
            g_state.current.x = GET_X_LPARAM(lParam);
            g_state.current.y = GET_Y_LPARAM(lParam);
            UpdateLayeredContent(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_state.selecting) {
            g_state.selecting = false;
            ReleaseCapture();
            g_state.current.x = GET_X_LPARAM(lParam);
            g_state.current.y = GET_Y_LPARAM(lParam);

            int w = abs(g_state.current.x - g_state.start.x);
            int h = abs(g_state.current.y - g_state.start.y);
            if (w > 5 && h > 5) {
                g_state.result.left   = (std::min)(g_state.start.x, g_state.current.x);
                g_state.result.top    = (std::min)(g_state.start.y, g_state.current.y);
                g_state.result.right  = (std::max)(g_state.start.x, g_state.current.x);
                g_state.result.bottom = (std::max)(g_state.start.y, g_state.current.y);
                g_state.finished = true;
            }
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            g_state.cancelled = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

HBITMAP SnipOverlay::CaptureRegion(HINSTANCE hInstance, RECT& selected) {
    g_state = {};
    g_state.hInst = hInstance;

    if (!CaptureFullScreen()) {
        MessageBoxW(nullptr, L"Failed to capture screen", L"SnipTool", MB_ICONERROR);
        return nullptr;
    }

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = L"SnipOverlayClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"SnipOverlayClass", L"",
        WS_POPUP,
        g_state.screenLeft, g_state.screenTop, g_state.screenW, g_state.screenH,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        CleanupMem();
        return nullptr;
    }

    g_state.hwnd = hwnd;
    UpdateLayeredContent(hwnd);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(L"SnipOverlayClass", hInstance);

    HBITMAP resultBmp = nullptr;

    if (g_state.finished && !g_state.cancelled) {
        selected = g_state.result;
        int w = selected.right - selected.left;
        int h = selected.bottom - selected.top;

        HDC screenDC = GetDC(nullptr);
        HDC selDC = CreateCompatibleDC(screenDC);
        resultBmp = CreateCompatibleBitmap(screenDC, w, h);
        HBITMAP old = (HBITMAP)SelectObject(selDC, resultBmp);

        BitBlt(selDC, 0, 0, w, h, g_state.memDC,
               selected.left - g_state.screenLeft, selected.top - g_state.screenTop, SRCCOPY);

        SelectObject(selDC, old);
        DeleteDC(selDC);
        ReleaseDC(nullptr, screenDC);
    }

    CleanupMem();
    return resultBmp;
}