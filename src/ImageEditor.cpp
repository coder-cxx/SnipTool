#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ImageEditor.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <vector>
#include <algorithm>
#include <cmath>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

constexpr int TOOLBAR_H = 36;
constexpr int BTN_W = 64;
constexpr int BTN_H = 24;
constexpr int PALETTE_SIZE = 20;
constexpr int MARGIN = 8;

bool GetBitmapData(HBITMAP hBmp, int& w, int& h, std::vector<uint8_t>& pixelsBGRA) {
    BITMAP bm{};
    if (!GetObject(hBmp, sizeof(bm), &bm)) return false;
    w = bm.bmWidth;
    h = bm.bmHeight;

    BITMAPINFOHEADER bi{ sizeof(bi) };
    bi.biWidth = w;
    bi.biHeight = -h; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    pixelsBGRA.resize(static_cast<size_t>(w) * h * 4);
    HDC hdc = GetDC(nullptr);
    int res = GetDIBits(hdc, hBmp, 0, h, pixelsBGRA.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    return res != 0;
}

} // namespace

ImageEditor::ImageEditor(HINSTANCE hInstance, HBITMAP hSourceBmp, const RECT& screenRect)
    : hInst_(hInstance), screenRect_(screenRect)
{
    BITMAP bm{};
    GetObject(hSourceBmp, sizeof(bm), &bm);
    imgW_ = bm.bmWidth;
    imgH_ = bm.bmHeight;

    HDC screenDC = GetDC(nullptr);
    HDC srcDC = CreateCompatibleDC(screenDC);
    HBITMAP oldSrc = (HBITMAP)SelectObject(srcDC, hSourceBmp);

    hOriginal_ = CreateCompatibleBitmap(screenDC, imgW_, imgH_);
    HDC origDC = CreateCompatibleDC(screenDC);
    HBITMAP oldOrig = (HBITMAP)SelectObject(origDC, hOriginal_);
    BitBlt(origDC, 0, 0, imgW_, imgH_, srcDC, 0, 0, SRCCOPY);
    SelectObject(origDC, oldOrig);
    DeleteDC(origDC);

    SelectObject(srcDC, oldSrc);
    DeleteDC(srcDC);

    hdcCanvas_ = CreateCompatibleDC(screenDC);
    hCanvas_ = CreateCompatibleBitmap(screenDC, imgW_, imgH_);
    ReleaseDC(nullptr, screenDC);
    hOldCanvas_ = (HBITMAP)SelectObject(hdcCanvas_, hCanvas_);
    RebuildCanvas();
}

ImageEditor::~ImageEditor() {
    // Final safety copy (already updated after each stroke)
    CopyToClipboard();

    if (hdcCanvas_) {
        SelectObject(hdcCanvas_, hOldCanvas_);
        DeleteDC(hdcCanvas_);
    }
    if (hCanvas_) DeleteObject(hCanvas_);
    if (hOriginal_) DeleteObject(hOriginal_);
}

void ImageEditor::DrawArrow(HDC hdc, POINT from, POINT to, int thickness, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH brush = CreateSolidBrush(color);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);

    MoveToEx(hdc, from.x, from.y, nullptr);
    LineTo(hdc, to.x, to.y);

    // Arrow head
    double dx = static_cast<double>(to.x - from.x);
    double dy = static_cast<double>(to.y - from.y);
    double len = std::sqrt(dx * dx + dy * dy);
    if (len > 1.0) {
        dx /= len; dy /= len;
        const double headLen = (std::max)(12.0, thickness * 4.0);
        const double headWidth = headLen * 0.5;

        POINT tip = to;
        POINT left{
            static_cast<LONG>(to.x - dx * headLen + dy * headWidth),
            static_cast<LONG>(to.y - dy * headLen - dx * headWidth)
        };
        POINT right{
            static_cast<LONG>(to.x - dx * headLen - dy * headWidth),
            static_cast<LONG>(to.y - dy * headLen + dx * headWidth)
        };

        POINT pts[3] = { tip, left, right };
        Polygon(hdc, pts, 3);
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void ImageEditor::RebuildCanvas() {
    HDC srcDC = CreateCompatibleDC(hdcCanvas_);
    HBITMAP old = (HBITMAP)SelectObject(srcDC, hOriginal_);
    BitBlt(hdcCanvas_, 0, 0, imgW_, imgH_, srcDC, 0, 0, SRCCOPY);
    SelectObject(srcDC, old);
    DeleteDC(srcDC);

    for (const auto& s : strokes_) {
        if (s.tool == Tool::Freehand && s.points.size() >= 2) {
            HPEN pen = CreatePen(PS_SOLID, s.thickness, s.color);
            HPEN oldPen = (HPEN)SelectObject(hdcCanvas_, pen);
            MoveToEx(hdcCanvas_, s.points[0].x, s.points[0].y, nullptr);
            for (size_t i = 1; i < s.points.size(); ++i) {
                LineTo(hdcCanvas_, s.points[i].x, s.points[i].y);
            }
            SelectObject(hdcCanvas_, oldPen);
            DeleteObject(pen);
        }
        else if (s.tool == Tool::Line && s.points.size() == 2) {
            HPEN pen = CreatePen(PS_SOLID, s.thickness, s.color);
            HPEN oldPen = (HPEN)SelectObject(hdcCanvas_, pen);
            MoveToEx(hdcCanvas_, s.points[0].x, s.points[0].y, nullptr);
            LineTo(hdcCanvas_, s.points[1].x, s.points[1].y);
            SelectObject(hdcCanvas_, oldPen);
            DeleteObject(pen);
        }
        else if (s.tool == Tool::Rectangle && s.points.size() == 2) {
            HPEN pen = CreatePen(PS_SOLID, s.thickness, s.color);
            HPEN oldPen = (HPEN)SelectObject(hdcCanvas_, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdcCanvas_, GetStockObject(NULL_BRUSH));
            Rectangle(hdcCanvas_, s.points[0].x, s.points[0].y, s.points[1].x, s.points[1].y);
            SelectObject(hdcCanvas_, oldBrush);
            SelectObject(hdcCanvas_, oldPen);
            DeleteObject(pen);
        }
        else if (s.tool == Tool::Arrow && s.points.size() == 2) {
            DrawArrow(hdcCanvas_, s.points[0], s.points[1], s.thickness, s.color);
        }
    }
}

void ImageEditor::CreateControls() {
    auto makeBtn = [&](const wchar_t* text, int x, int id) -> HWND {
        return CreateWindowW(L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, 6, BTN_W, BTN_H,
            hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
    };

    int x = MARGIN;
    btnFree_  = makeBtn(L"Pencil", x, 1001); x += BTN_W + 4;
    btnLine_  = makeBtn(L"Line",   x, 1002); x += BTN_W + 4;
    btnRect_  = makeBtn(L"Rect",   x, 1003); x += BTN_W + 4;
    btnArrow_ = makeBtn(L"Arrow",  x, 1004); x += BTN_W + 4;
    btnUndo_  = makeBtn(L"Undo",   x, 1005); x += BTN_W + 4;
    btnSave_  = makeBtn(L"Save",   x, 1006); x += BTN_W + 10;

    // Color palette
    for (int i = 0; i < PALETTE_COUNT; ++i) {
        btnPalette_[i] = CreateWindowW(L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            x, 8, PALETTE_SIZE, PALETTE_SIZE,
            hwnd_, (HMENU)(INT_PTR)(1100 + i), hInst_, nullptr);
        x += PALETTE_SIZE + 3;
    }
}

void ImageEditor::OnPaint(HDC hdc) {
    RECT rc;
    GetClientRect(hwnd_, &rc);

    // Toolbar background
    RECT rcToolbar{ 0, 0, rc.right, TOOLBAR_H };
    FillRect(hdc, &rcToolbar, (HBRUSH)(COLOR_BTNFACE + 1));

    // Image area background (in case image is smaller than window)
    RECT rcImgBg{ 0, TOOLBAR_H, rc.right, rc.bottom };
    FillRect(hdc, &rcImgBg, (HBRUSH)GetStockObject(WHITE_BRUSH));

    // Center the image if the window is larger
    int imgX = MARGIN;
    int imgY = TOOLBAR_H + MARGIN;
    if (rc.right > imgW_ + 2 * MARGIN) {
        imgX = (rc.right - imgW_) / 2;
    }
    if (rc.bottom - TOOLBAR_H > imgH_ + 2 * MARGIN) {
        imgY = TOOLBAR_H + (rc.bottom - TOOLBAR_H - imgH_) / 2;
    }

    BitBlt(hdc, imgX, imgY, imgW_, imgH_, hdcCanvas_, 0, 0, SRCCOPY);

    // Border
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    HPEN old = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, imgX - 1, imgY - 1, imgX + imgW_ + 1, imgY + imgH_ + 1);
    SelectObject(hdc, oldB);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void ImageEditor::StartStroke(POINT pt) {
    drawing_ = true;
    startPt_ = lastPt_ = pt;
    currentStroke_ = {};
    currentStroke_.tool = currentTool_;
    currentStroke_.color = currentColor_;
    currentStroke_.thickness = thickness_;
    currentStroke_.points.push_back(pt);
}

void ImageEditor::ContinueStroke(POINT pt) {
    if (!drawing_) return;

    if (currentTool_ == Tool::Freehand) {
        HPEN pen = CreatePen(PS_SOLID, thickness_, currentColor_);
        HPEN old = (HPEN)SelectObject(hdcCanvas_, pen);
        MoveToEx(hdcCanvas_, lastPt_.x, lastPt_.y, nullptr);
        LineTo(hdcCanvas_, pt.x, pt.y);
        SelectObject(hdcCanvas_, old);
        DeleteObject(pen);

        currentStroke_.points.push_back(pt);
        lastPt_ = pt;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    else {
        lastPt_ = pt;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ImageEditor::EndStroke(POINT pt) {
    if (!drawing_) return;
    drawing_ = false;

    if (currentTool_ == Tool::Freehand) {
        if (currentStroke_.points.size() >= 2) {
            strokes_.push_back(std::move(currentStroke_));
        }
    }
    else {
        currentStroke_.points.clear();
        currentStroke_.points.push_back(startPt_);
        currentStroke_.points.push_back(pt);
        strokes_.push_back(std::move(currentStroke_));
        RebuildCanvas();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);

    // Update clipboard after every completed drawing operation
    CopyToClipboard();
}

// Free helper: put any HBITMAP to clipboard with max compatibility
void PutBitmapToClipboard(HBITMAP hBmp) {
    if (!hBmp) return;
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    BITMAP bm{};
    if (!GetObject(hBmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        CloseClipboard();
        return;
    }

    // 1. CF_DIB
    {
        BITMAPINFOHEADER bi{};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bm.bmWidth;
        bi.biHeight = bm.bmHeight; // bottom-up
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        const size_t headerSize = sizeof(BITMAPINFOHEADER);
        const size_t imageSize = static_cast<size_t>(bm.bmWidth) * bm.bmHeight * 4;
        const size_t totalSize = headerSize + imageSize;

        HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, totalSize);
        if (hDib) {
            void* p = GlobalLock(hDib);
            if (p) {
                memcpy(p, &bi, headerSize);
                BITMAPINFO bmi{};
                bmi.bmiHeader = bi;
                HDC hdc = GetDC(nullptr);
                GetDIBits(hdc, hBmp, 0, bm.bmHeight,
                          static_cast<BYTE*>(p) + headerSize, &bmi, DIB_RGB_COLORS);
                ReleaseDC(nullptr, hdc);
                GlobalUnlock(hDib);
                SetClipboardData(CF_DIB, hDib);
            } else {
                GlobalFree(hDib);
            }
        }
    }

    // 2. PNG
    {
        UINT pngFmt = RegisterClipboardFormatA("PNG");
        if (pngFmt) {
            // Get top-down BGRA
            BITMAPINFOHEADER bi{};
            bi.biSize = sizeof(BITMAPINFOHEADER);
            bi.biWidth = bm.bmWidth;
            bi.biHeight = -bm.bmHeight; // top-down
            bi.biPlanes = 1;
            bi.biBitCount = 32;
            bi.biCompression = BI_RGB;

            std::vector<uint8_t> bgra(static_cast<size_t>(bm.bmWidth) * bm.bmHeight * 4);
            BITMAPINFO bmi{};
            bmi.bmiHeader = bi;
            HDC hdc = GetDC(nullptr);
            GetDIBits(hdc, hBmp, 0, bm.bmHeight, bgra.data(), &bmi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, hdc);

            std::vector<uint8_t> rgba(bgra.size());
            for (size_t i = 0; i < bgra.size() / 4; ++i) {
                rgba[i*4 + 0] = bgra[i*4 + 2];
                rgba[i*4 + 1] = bgra[i*4 + 1];
                rgba[i*4 + 2] = bgra[i*4 + 0];
                rgba[i*4 + 3] = 255;
            }

            int pngLen = 0;
            unsigned char* pngData = stbi_write_png_to_mem(rgba.data(), bm.bmWidth * 4, bm.bmWidth, bm.bmHeight, 4, &pngLen);
            if (pngData && pngLen > 0) {
                HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, pngLen);
                if (hPng) {
                    void* p = GlobalLock(hPng);
                    if (p) {
                        memcpy(p, pngData, pngLen);
                        GlobalUnlock(hPng);
                        SetClipboardData(pngFmt, hPng);
                    } else {
                        GlobalFree(hPng);
                    }
                }
                STBIW_FREE(pngData);
            }
        }
    }

    // 3. CF_BITMAP
    {
        HDC screenDC = GetDC(nullptr);
        HDC tmpDC = CreateCompatibleDC(screenDC);
        HBITMAP hCopy = CreateCompatibleBitmap(screenDC, bm.bmWidth, bm.bmHeight);
        HBITMAP oldBmp = (HBITMAP)SelectObject(tmpDC, hCopy);
        HDC srcDC = CreateCompatibleDC(screenDC);
        HBITMAP oldSrc = (HBITMAP)SelectObject(srcDC, hBmp);
        BitBlt(tmpDC, 0, 0, bm.bmWidth, bm.bmHeight, srcDC, 0, 0, SRCCOPY);
        SelectObject(srcDC, oldSrc);
        DeleteDC(srcDC);
        SelectObject(tmpDC, oldBmp);
        DeleteDC(tmpDC);
        ReleaseDC(nullptr, screenDC);
        SetClipboardData(CF_BITMAP, hCopy);
    }

    CloseClipboard();
}

void ImageEditor::CopyToClipboard() {
    PutBitmapToClipboard(hCanvas_);
}

void ImageEditor::SaveToFile() {
    wchar_t path[MAX_PATH] = L"screenshot.png";
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"PNG files\0*.png\0BMP files\0*.bmp\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"png";

    if (!GetSaveFileNameW(&ofn)) return;

    int w = 0, h = 0;
    std::vector<uint8_t> bgra;
    if (!GetBitmapData(hCanvas_, w, h, bgra)) {
        MessageBoxW(hwnd_, L"Failed to get bitmap data", L"SnipTool", MB_ICONERROR);
        return;
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
    }

    char pathUtf8[MAX_PATH * 4];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr);

    std::wstring ext = path;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    bool isBmp = ext.size() >= 4 && ext.substr(ext.size() - 4) == L".bmp";

    int ok = isBmp
        ? stbi_write_bmp(pathUtf8, w, h, 4, rgba.data())
        : stbi_write_png(pathUtf8, w, h, 4, rgba.data(), w * 4);

    if (!ok) {
        MessageBoxW(hwnd_, L"Failed to save file", L"SnipTool", MB_ICONERROR);
    }
}

void ImageEditor::Undo() {
    if (strokes_.empty()) return;
    strokes_.pop_back();
    RebuildCanvas();
    InvalidateRect(hwnd_, nullptr, FALSE);

    // Keep clipboard in sync with current state (including after undo)
    CopyToClipboard();
}

LRESULT ImageEditor::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case 1001: currentTool_ = Tool::Freehand; break;
        case 1002: currentTool_ = Tool::Line;     break;
        case 1003: currentTool_ = Tool::Rectangle; break;
        case 1004: currentTool_ = Tool::Arrow;    break;
        case 1005: Undo(); break;
        case 1006: SaveToFile(); break;
        default:
            if (id >= 1100 && id < 1100 + PALETTE_COUNT) {
                currentColor_ = paletteColors_[id - 1100];
                InvalidateRect(hwnd_, nullptr, FALSE); // refresh palette selection if needed
            }
            break;
        }
        return 0;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlID >= 1100 && dis->CtlID < 1100 + PALETTE_COUNT) {
            int idx = dis->CtlID - 1100;
            HBRUSH br = CreateSolidBrush(paletteColors_[idx]);
            FillRect(dis->hDC, &dis->rcItem, br);
            DeleteObject(br);

            // highlight current color
            if (paletteColors_[idx] == currentColor_) {
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN old = (HPEN)SelectObject(dis->hDC, pen);
                HBRUSH oldB = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                          dis->rcItem.right, dis->rcItem.bottom);
                SelectObject(dis->hDC, oldB);
                SelectObject(dis->hDC, old);
                DeleteObject(pen);
            }
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int imgX = MARGIN;
        int imgY = TOOLBAR_H + MARGIN;
        if (rc.right > imgW_ + 2 * MARGIN) imgX = (rc.right - imgW_) / 2;
        if (rc.bottom - TOOLBAR_H > imgH_ + 2 * MARGIN) imgY = TOOLBAR_H + (rc.bottom - TOOLBAR_H - imgH_) / 2;

        int x = GET_X_LPARAM(lParam) - imgX;
        int y = GET_Y_LPARAM(lParam) - imgY;
        if (x >= 0 && y >= 0 && x < imgW_ && y < imgH_) {
            StartStroke({ x, y });
            SetCapture(hwnd_);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (drawing_) {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            int imgX = MARGIN;
            int imgY = TOOLBAR_H + MARGIN;
            if (rc.right > imgW_ + 2 * MARGIN) imgX = (rc.right - imgW_) / 2;
            if (rc.bottom - TOOLBAR_H > imgH_ + 2 * MARGIN) imgY = TOOLBAR_H + (rc.bottom - TOOLBAR_H - imgH_) / 2;

            int x = GET_X_LPARAM(lParam) - imgX;
            int y = GET_Y_LPARAM(lParam) - imgY;
            x = std::max(0, std::min(imgW_ - 1, x));
            y = std::max(0, std::min(imgH_ - 1, y));
            ContinueStroke({ x, y });
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (drawing_) {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            int imgX = MARGIN;
            int imgY = TOOLBAR_H + MARGIN;
            if (rc.right > imgW_ + 2 * MARGIN) imgX = (rc.right - imgW_) / 2;
            if (rc.bottom - TOOLBAR_H > imgH_ + 2 * MARGIN) imgY = TOOLBAR_H + (rc.bottom - TOOLBAR_H - imgH_) / 2;

            int x = GET_X_LPARAM(lParam) - imgX;
            int y = GET_Y_LPARAM(lParam) - imgY;
            x = std::max(0, std::min(imgW_ - 1, x));
            y = std::max(0, std::min(imgH_ - 1, y));
            EndStroke({ x, y });
            ReleaseCapture();
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);

        RECT rc;
        GetClientRect(hwnd_, &rc);
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        FillRect(memDC, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        OnPaint(memDC);

        // Live preview for line / rect / arrow
        if (drawing_ && currentTool_ != Tool::Freehand) {
            int imgX = MARGIN;
            int imgY = TOOLBAR_H + MARGIN;
            if (rc.right > imgW_ + 2 * MARGIN) imgX = (rc.right - imgW_) / 2;
            if (rc.bottom - TOOLBAR_H > imgH_ + 2 * MARGIN) imgY = TOOLBAR_H + (rc.bottom - TOOLBAR_H - imgH_) / 2;

            POINT p1{ imgX + startPt_.x, imgY + startPt_.y };
            POINT p2{ imgX + lastPt_.x,  imgY + lastPt_.y };

            if (currentTool_ == Tool::Line) {
                HPEN pen = CreatePen(PS_SOLID, thickness_, currentColor_);
                HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                MoveToEx(memDC, p1.x, p1.y, nullptr);
                LineTo(memDC, p2.x, p2.y);
                SelectObject(memDC, oldPen);
                DeleteObject(pen);
            }
            else if (currentTool_ == Tool::Rectangle) {
                HPEN pen = CreatePen(PS_SOLID, thickness_, currentColor_);
                HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                Rectangle(memDC, p1.x, p1.y, p2.x, p2.y);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(pen);
            }
            else if (currentTool_ == Tool::Arrow) {
                // temporary DC offset is awkward, just draw in image coords then offset is hard;
                // simpler: draw arrow in canvas coords temporarily on memDC by translating
                // For preview we draw directly with offset.
                HPEN pen = CreatePen(PS_SOLID, thickness_, currentColor_);
                HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                HBRUSH brush = CreateSolidBrush(currentColor_);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);

                MoveToEx(memDC, p1.x, p1.y, nullptr);
                LineTo(memDC, p2.x, p2.y);

                double dx = static_cast<double>(p2.x - p1.x);
                double dy = static_cast<double>(p2.y - p1.y);
                double len = std::sqrt(dx * dx + dy * dy);
                if (len > 1.0) {
                    dx /= len; dy /= len;
                    const double headLen = (std::max)(12.0, thickness_ * 4.0);
                    const double headWidth = headLen * 0.5;
                    POINT tip = p2;
                    POINT left{
                        static_cast<LONG>(p2.x - dx * headLen + dy * headWidth),
                        static_cast<LONG>(p2.y - dy * headLen - dx * headWidth)
                    };
                    POINT right{
                        static_cast<LONG>(p2.x - dx * headLen - dy * headWidth),
                        static_cast<LONG>(p2.y - dy * headLen + dx * headWidth)
                    };
                    POINT pts[3] = { tip, left, right };
                    Polygon(memDC, pts, 3);
                }

                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(brush);
                DeleteObject(pen);
            }
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd_);
        }
        else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            Undo();
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

LRESULT CALLBACK ImageEditor::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImageEditor* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ImageEditor*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ImageEditor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ImageEditor::Run() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ImageEditor::WndProc;
    wc.hInstance = hInst_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"SnipEditorClass";
    RegisterClassExW(&wc);

    // Minimum width so all buttons + palette are visible
    const int minToolbarW = MARGIN + 6 * (BTN_W + 4) + 10 + PALETTE_COUNT * (PALETTE_SIZE + 3) + MARGIN + 20;
    int winW = (std::max)(imgW_ + 2 * MARGIN + 16, minToolbarW);
    int winH = imgH_ + TOOLBAR_H + 2 * MARGIN + 40; // extra for caption

    // Center near selection
    int x = screenRect_.left + (screenRect_.right - screenRect_.left - winW) / 2;
    int y = screenRect_.top  + (screenRect_.bottom - screenRect_.top - winH) / 2;

    // Keep on screen
    x = (std::max)(0, x);
    y = (std::max)(0, y);

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"SnipEditorClass", L"SnipTool Editor  (close = copy to clipboard)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
        x, y, winW, winH,
        nullptr, nullptr, hInst_, this);

    if (!hwnd_) return;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    SetForegroundWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(L"SnipEditorClass", hInst_);
}
