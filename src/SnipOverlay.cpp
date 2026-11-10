#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SnipOverlay.hpp"

#include <windowsx.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>
#include <dwmapi.h>

#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#endif

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

bool IsOverlayOrWatermarkWindow(HWND hwnd) {
    if (!hwnd) return true;

    wchar_t className[256] = {};
    wchar_t windowName[256] = {};
    GetClassNameW(hwnd, className, 256);
    GetWindowTextW(hwnd, windowName, 256);

    auto toLower = [](std::wstring s) {
        for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
        return s;
    };

    std::wstring cls = toLower(className);
    std::wstring title = toLower(windowName);

    static const std::vector<std::wstring> blacklist = {
        L"watermark", L"overlay", L"activatewindows", L"gamebar",
        L"xbox", L"toast", L"notification", L"snipoverlayclass",
        L"windows.ui.core.corewindow",
        L"applicationframewindow"
    };

    for (const auto& banned : blacklist) {
        if (cls.find(banned) != std::wstring::npos || title.find(banned) != std::wstring::npos)
            return true;
    }
    return false;
}

struct WindowInfo {
    HWND hwnd;
    RECT fullRect;    // GetWindowRect — с тенью
    RECT visibleRect; // DWMWA_EXTENDED_FRAME_BOUNDS — без тени
};

BOOL CALLBACK CollectWindows(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;
    if (hwnd == g_state.hwnd) return TRUE;
    if (hwnd == GetShellWindow()) return TRUE;
    if (IsOverlayOrWatermarkWindow(hwnd)) return TRUE;

    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);

    if (wcscmp(cls, L"Progman") == 0 ||
        wcscmp(cls, L"WorkerW") == 0 ||
        wcscmp(cls, L"Shell_TrayWnd") == 0 ||
        wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
        return TRUE;
    }

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;

    RECT full{};
    if (!GetWindowRect(hwnd, &full)) return TRUE;
    if ((full.right - full.left) <= 1 || (full.bottom - full.top) <= 1) return TRUE;

    if (full.right <= g_state.screenLeft || full.bottom <= g_state.screenTop ||
        full.left >= g_state.screenLeft + g_state.screenW ||
        full.top  >= g_state.screenTop  + g_state.screenH)
        return TRUE;

    RECT visible = full;
    DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible));

    // Sanity: visible must be inside full (or equal)
    if (visible.left   < full.left)   visible.left   = full.left;
    if (visible.top    < full.top)    visible.top    = full.top;
    if (visible.right  > full.right)  visible.right  = full.right;
    if (visible.bottom > full.bottom) visible.bottom = full.bottom;

    auto& windows = *reinterpret_cast<std::vector<WindowInfo>*>(lParam);
    windows.push_back({ hwnd, full, visible });
    return TRUE;
}

bool HasMeaningfulContent(HBITMAP hBmp, int width, int height) {
    if (!hBmp || width <= 0 || height <= 0) return false;

    BITMAPINFO bi{ sizeof(BITMAPINFOHEADER) };
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    const size_t count = static_cast<size_t>(width) * height;
    std::vector<uint32_t> pixels(count);

    HDC hdc = GetDC(nullptr);
    int got = GetDIBits(hdc, hBmp, 0, height, pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    if (got == 0) return false;

    size_t step = (std::max)(size_t(1), count / 40);
    int nonBlack = 0;
    for (size_t i = 0; i < count; i += step) {
        if ((pixels[i] & 0x00FFFFFF) != 0) ++nonBlack;
    }

    auto check = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < width && y < height)
            if ((pixels[static_cast<size_t>(y) * width + x] & 0x00FFFFFF) != 0) ++nonBlack;
    };
    check(0, 0); check(width - 1, 0); check(0, height - 1); check(width - 1, height - 1);
    check(width / 2, height / 2);

    return nonBlack >= 3;
}

// Capture ONLY the visible part of the window (no shadow mask)
HBITMAP CaptureWindowContent(HWND hwnd, int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;

    HDC hdcScreen = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBmp);

    RECT rc{ 0, 0, width, height };
    FillRect(memDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    BOOL ok = PrintWindow(hwnd, memDC, PW_RENDERFULLCONTENT);
    bool good = ok && HasMeaningfulContent(hBmp, width, height);

    if (!good) {
        ok = PrintWindow(hwnd, memDC, 0);
        good = ok && HasMeaningfulContent(hBmp, width, height);
    }

    if (!good) {
        HDC hdcWin = GetWindowDC(hwnd);
        if (hdcWin) {
            // GetWindowDC is relative to full window including shadow margin.
            // We need content corresponding to visible area — caller passes visible size,
            // so offset by shadow margin if needed. For simplicity BitBlt from 0,0;
            // when used with visible size after DWM bounds it usually matches.
            BitBlt(memDC, 0, 0, width, height, hdcWin, 0, 0, SRCCOPY | CAPTUREBLT);
            ReleaseDC(hwnd, hdcWin);
            good = HasMeaningfulContent(hBmp, width, height);
        }
    }

    if (!good) {
        HDC hdcClient = GetDC(hwnd);
        if (hdcClient) {
            BitBlt(memDC, 0, 0, width, height, hdcClient, 0, 0, SRCCOPY | CAPTUREBLT);
            ReleaseDC(hwnd, hdcClient);
        }
    }

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, hdcScreen);
    return hBmp;
}

// Approach B from the guide: distance-based soft shadow in the margin only.
// Drawn onto canvas BEFORE the window content.
void DrawShadowForWindow(const RECT& full, const RECT& visible) {
    const int fullW = full.right - full.left;
    const int fullH = full.bottom - full.top;
    if (fullW <= 0 || fullH <= 0) return;

    const int marginL = visible.left   - full.left;
    const int marginT = visible.top    - full.top;
    const int marginR = full.right     - visible.right;
    const int marginB = full.bottom    - visible.bottom;

    // No shadow margin → nothing to do
    if (marginL <= 0 && marginT <= 0 && marginR <= 0 && marginB <= 0)
        return;

    // Visible rect size
    const int visW = visible.right - visible.left;
    const int visH = visible.bottom - visible.top;
    if (visW <= 0 || visH <= 0) return;

    // Destination of full rect on our virtual-screen canvas
    const int dstX = full.left - g_state.screenLeft;
    const int dstY = full.top  - g_state.screenTop;

    // Build a small ARGB buffer for the full rect (only margin will be non-zero)
    std::vector<uint8_t> shadow(static_cast<size_t>(fullW) * fullH * 4, 0);

    // Visible region relative to full rect origin
    const int vx0 = marginL;
    const int vy0 = marginT;
    const int vx1 = marginL + visW;
    const int vy1 = marginT + visH;

    // Max margin for falloff
    const float maxMargin = static_cast<float>(
        (std::max)({ marginL, marginT, marginR, marginB, 1 }));

    const float sigma = maxMargin * 0.55f; // softness
    const float peakAlpha = 120.0f;        // max shadow strength

    for (int y = 0; y < fullH; ++y) {
        for (int x = 0; x < fullW; ++x) {
            // Inside visible window → no shadow
            if (x >= vx0 && x < vx1 && y >= vy0 && y < vy1)
                continue;

            // Distance to visible rectangle (0 at edge, positive outside)
            int dx = 0, dy = 0;
            if (x < vx0)      dx = vx0 - x;
            else if (x >= vx1) dx = x - (vx1 - 1);
            if (y < vy0)      dy = vy0 - y;
            else if (y >= vy1) dy = y - (vy1 - 1);

            const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            // Gaussian-like falloff
            float a = std::exp(-(dist * dist) / (2.0f * sigma * sigma));
            // Slightly stronger bottom/right like DWM
            if (y >= vy1) a *= 1.12f;
            if (x >= vx1) a *= 1.08f;
            a = std::clamp(a, 0.0f, 1.0f);

            const uint8_t alpha = static_cast<uint8_t>(a * peakAlpha);
            const size_t idx = (static_cast<size_t>(y) * fullW + x) * 4;
            // Straight alpha (not premultiplied) — AlphaBlend with AC_SRC_ALPHA expects
            // premultiplied for best results; we use simple dark RGB + alpha.
            shadow[idx + 0] = 0;
            shadow[idx + 1] = 0;
            shadow[idx + 2] = 0;
            shadow[idx + 3] = alpha;
        }
    }

    // Premultiply for correct AlphaBlend
    for (size_t i = 0; i < shadow.size(); i += 4) {
        const uint8_t a = shadow[i + 3];
        // RGB already 0
        (void)a;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = fullW;
    bmi.bmiHeader.biHeight = -fullH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(nullptr);
    HDC shadowDC = CreateCompatibleDC(hdcScreen);
    void* bits = nullptr;
    HBITMAP hShadow = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hShadow && bits) {
        memcpy(bits, shadow.data(), shadow.size());
        HBITMAP old = (HBITMAP)SelectObject(shadowDC, hShadow);

        // Clip draw to canvas
        int drawX = dstX, drawY = dstY, drawW = fullW, drawH = fullH;
        int srcX = 0, srcY = 0;
        if (drawX < 0) { srcX = -drawX; drawW += drawX; drawX = 0; }
        if (drawY < 0) { srcY = -drawY; drawH += drawY; drawY = 0; }
        if (drawX + drawW > g_state.screenW) drawW = g_state.screenW - drawX;
        if (drawY + drawH > g_state.screenH) drawH = g_state.screenH - drawY;

        if (drawW > 0 && drawH > 0) {
            BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(g_state.memDC, drawX, drawY, drawW, drawH,
                       shadowDC, srcX, srcY, drawW, drawH, bf);
        }

        SelectObject(shadowDC, old);
        DeleteObject(hShadow);
    }
    DeleteDC(shadowDC);
    ReleaseDC(nullptr, hdcScreen);
}

void DrawWindowToCanvas(const WindowInfo& info) {
    const RECT& full = info.fullRect;
    const RECT& visible = info.visibleRect;

    const int fullW = full.right - full.left;
    const int fullH = full.bottom - full.top;
    const int visW = visible.right - visible.left;
    const int visH = visible.bottom - visible.top;
    if (fullW <= 0 || fullH <= 0 || visW <= 0 || visH <= 0) return;

    // 1) Shadow first (margin between full and visible)
    DrawShadowForWindow(full, visible);

    // 2) Capture full window (same path that already worked), then blit only visible part
    HBITMAP hBmp = CaptureWindowContent(info.hwnd, fullW, fullH);
    if (!hBmp) return;

    const int marginL = visible.left - full.left;
    const int marginT = visible.top  - full.top;

    int dstX = visible.left - g_state.screenLeft;
    int dstY = visible.top  - g_state.screenTop;

    int srcX = marginL;
    int srcY = marginT;
    int drawW = visW;
    int drawH = visH;

    if (dstX < 0) { srcX -= dstX; drawW += dstX; dstX = 0; }
    if (dstY < 0) { srcY -= dstY; drawH += dstY; dstY = 0; }
    if (dstX + drawW > g_state.screenW) drawW = g_state.screenW - dstX;
    if (dstY + drawH > g_state.screenH) drawH = g_state.screenH - dstY;

    if (drawW <= 0 || drawH <= 0) {
        DeleteObject(hBmp);
        return;
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC srcDC = CreateCompatibleDC(hdcScreen);
    HBITMAP old = (HBITMAP)SelectObject(srcDC, hBmp);

    LONG exStyle = GetWindowLongW(info.hwnd, GWL_EXSTYLE);
    bool layered = (exStyle & WS_EX_LAYERED) != 0;

    if (layered) {
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(g_state.memDC, dstX, dstY, drawW, drawH,
                   srcDC, srcX, srcY, drawW, drawH, bf);
    } else {
        BitBlt(g_state.memDC, dstX, dstY, drawW, drawH, srcDC, srcX, srcY, SRCCOPY);
    }

    SelectObject(srcDC, old);
    DeleteDC(srcDC);
    ReleaseDC(nullptr, hdcScreen);
    DeleteObject(hBmp);
}

void CaptureDesktopBackground() {
    HBRUSH br = CreateSolidBrush(RGB(32, 32, 32));
    RECT full{ 0, 0, g_state.screenW, g_state.screenH };
    FillRect(g_state.memDC, &full, br);
    DeleteObject(br);

    HWND hDesktop = GetDesktopWindow();
    if (hDesktop) {
        if (!PrintWindow(hDesktop, g_state.memDC, PW_RENDERFULLCONTENT)) {
            PrintWindow(hDesktop, g_state.memDC, 0);
        }
    }

    if (!HasMeaningfulContent(g_state.memBmp, g_state.screenW, g_state.screenH)) {
        HDC screenDC = GetDC(nullptr);
        if (screenDC) {
            BitBlt(g_state.memDC, 0, 0, g_state.screenW, g_state.screenH,
                   screenDC, g_state.screenLeft, g_state.screenTop, SRCCOPY | CAPTUREBLT);
            ReleaseDC(nullptr, screenDC);
        }
    }
}

bool CaptureFullScreen() {
    CleanupMem();

    g_state.screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_state.screenTop  = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_state.screenW    = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_state.screenH    = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return false;

    g_state.memDC  = CreateCompatibleDC(screenDC);
    g_state.memBmp = CreateCompatibleBitmap(screenDC, g_state.screenW, g_state.screenH);
    g_state.oldBmp = (HBITMAP)SelectObject(g_state.memDC, g_state.memBmp);
    ReleaseDC(nullptr, screenDC);

    CaptureDesktopBackground();

    std::vector<WindowInfo> windows;
    EnumWindows(CollectWindows, reinterpret_cast<LPARAM>(&windows));
    std::reverse(windows.begin(), windows.end()); // bottom → top

    for (const auto& w : windows) {
        DrawWindowToCanvas(w);
    }

    return g_state.memBmp != nullptr;
}

// ---------- selection overlay ----------

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
        p[0] = static_cast<uint8_t>(p[0] * 0.28f);
        p[1] = static_cast<uint8_t>(p[1] * 0.28f);
        p[2] = static_cast<uint8_t>(p[2] * 0.28f);
        p[3] = 255;
        p += 4;
    }

    if (g_state.selecting || g_state.finished) {
        int x1 = (std::min)(g_state.start.x, g_state.current.x);
        int y1 = (std::min)(g_state.start.y, g_state.current.y);
        int x2 = (std::max)(g_state.start.x, g_state.current.x);
        int y2 = (std::max)(g_state.start.y, g_state.current.y);

        x1 = (std::max)(0, x1); y1 = (std::max)(0, y1);
        x2 = (std::min)(g_state.screenW, x2);
        y2 = (std::min)(g_state.screenH, y2);

        std::vector<uint8_t> orig(static_cast<size_t>(g_state.screenW) * g_state.screenH * 4);
        HDC hdc = GetDC(nullptr);
        GetDIBits(hdc, g_state.memBmp, 0, g_state.screenH, orig.data(), &bmi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);

        for (int y = y1; y < y2; ++y) {
            for (int x = x1; x < x2; ++x) {
                size_t idx = (static_cast<size_t>(y) * g_state.screenW + x) * 4;
                uint8_t* dst = static_cast<uint8_t*>(bits) + idx;
                dst[0] = orig[idx + 0];
                dst[1] = orig[idx + 1];
                dst[2] = orig[idx + 2];
                dst[3] = 255;
            }
        }

        auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
            if (x < 0 || y < 0 || x >= g_state.screenW || y >= g_state.screenH) return;
            size_t idx = (static_cast<size_t>(y) * g_state.screenW + x) * 4;
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

        BitBlt(selDC, 0, 0, w, h, g_state.memDC, selected.left, selected.top, SRCCOPY);

        SelectObject(selDC, old);
        DeleteDC(selDC);
        ReleaseDC(nullptr, screenDC);
    }

    CleanupMem();
    return resultBmp;
}

HBITMAP SnipOverlay::CaptureFullScreenBitmap() {
    g_state = {};
    if (!CaptureFullScreen()) return nullptr;

    HBITMAP result = g_state.memBmp;
    SelectObject(g_state.memDC, g_state.oldBmp);
    DeleteDC(g_state.memDC);
    g_state.memDC = nullptr;
    g_state.memBmp = nullptr;
    g_state.oldBmp = nullptr;
    return result;
}
