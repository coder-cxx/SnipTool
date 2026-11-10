#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SnipOverlay.hpp"
#include <windowsx.h>
#include <algorithm>
#include <vector>

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

bool CaptureFullScreen() {
    CleanupMem();

    g_state.screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_state.screenTop  = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_state.screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_state.screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC screenDC = GetDC(nullptr);
    g_state.memDC = CreateCompatibleDC(screenDC);
    g_state.memBmp = CreateCompatibleBitmap(screenDC, g_state.screenW, g_state.screenH);
    g_state.oldBmp = (HBITMAP)SelectObject(g_state.memDC, g_state.memBmp);

    BitBlt(g_state.memDC, 0, 0, g_state.screenW, g_state.screenH,
           screenDC, g_state.screenLeft, g_state.screenTop, SRCCOPY | CAPTUREBLT);

    ReleaseDC(nullptr, screenDC);
    return g_state.memBmp != nullptr;
}

// Create a 32-bit ARGB dibsection for layered window
HBITMAP CreateARGBSurface(int w, int h, void** bits) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
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

    // Copy original screenshot into ARGB (BGRX -> BGRA with full alpha)
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

    // Darken everything (aggressive ~70% black overlay)
    for (int i = 0; i < total; ++i) {
        // pre-multiplied-ish darken
        p[0] = static_cast<uint8_t>(p[0] * 0.30); // B
        p[1] = static_cast<uint8_t>(p[1] * 0.30); // G
        p[2] = static_cast<uint8_t>(p[2] * 0.30); // R
        p[3] = 255; // A fully opaque (we darken the content itself)
        p += 4;
    }

    // Restore original brightness inside selection rectangle
    if (g_state.selecting || g_state.finished) {
        int x1 = (std::min)(g_state.start.x, g_state.current.x);
        int y1 = (std::min)(g_state.start.y, g_state.current.y);
        int x2 = (std::max)(g_state.start.x, g_state.current.x);
        int y2 = (std::max)(g_state.start.y, g_state.current.y);

        // clamp
        x1 = (std::max)(0, x1); y1 = (std::max)(0, y1);
        x2 = (std::min)(g_state.screenW, x2); y2 = (std::min)(g_state.screenH, y2);

        // Get original bits again for the rect
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

        // Draw selection border (simple blue-ish)
        auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
            if (x < 0 || y < 0 || x >= g_state.screenW || y >= g_state.screenH) return;
            int idx = (y * g_state.screenW + x) * 4;
            uint8_t* d = static_cast<uint8_t*>(bits) + idx;
            d[0] = b; d[1] = g; d[2] = r; d[3] = 255;
        };

        // 2px border
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

    // Update the layered window
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

    // Initial full dim
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

        // Convert selection coords from client (relative to virtual screen origin) 
        // already correct because window is at virtual screen origin.

        int w = selected.right - selected.left;
        int h = selected.bottom - selected.top;

        HDC screenDC = GetDC(nullptr);
        HDC selDC = CreateCompatibleDC(screenDC);
        resultBmp = CreateCompatibleBitmap(screenDC, w, h);
        HBITMAP old = (HBITMAP)SelectObject(selDC, resultBmp);

        BitBlt(selDC, 0, 0, w, h, g_state.memDC,
               selected.left, selected.top, SRCCOPY);

        SelectObject(selDC, old);
        DeleteDC(selDC);
        ReleaseDC(nullptr, screenDC);
    }

    CleanupMem();
    return resultBmp;
}
