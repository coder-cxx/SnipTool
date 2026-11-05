#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ImageEditor.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <vector>
#include <algorithm>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

constexpr int TOOLBAR_H = 40;
constexpr int BTN_W = 90;
constexpr int BTN_H = 28;

// Helper: get DIB bits for PNG / clipboard
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

    pixelsBGRA.resize(w * h * 4);
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

    // Copy source into our original
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
    ReleaseDC(nullptr, screenDC);

    // Create canvas
    HDC screenDC2 = GetDC(nullptr);
    hdcCanvas_ = CreateCompatibleDC(screenDC2);
    hCanvas_ = CreateCompatibleBitmap(screenDC2, imgW_, imgH_);
    ReleaseDC(nullptr, screenDC2);
    hOldCanvas_ = (HBITMAP)SelectObject(hdcCanvas_, hCanvas_);
    RebuildCanvas();
}

ImageEditor::~ImageEditor() {
    if (hdcCanvas_) {
        SelectObject(hdcCanvas_, hOldCanvas_);
        DeleteDC(hdcCanvas_);
    }
    if (hCanvas_) DeleteObject(hCanvas_);
    if (hOriginal_) DeleteObject(hOriginal_);
}

void ImageEditor::RebuildCanvas() {
    // Start from original
    HDC srcDC = CreateCompatibleDC(hdcCanvas_);
    HBITMAP old = (HBITMAP)SelectObject(srcDC, hOriginal_);
    BitBlt(hdcCanvas_, 0, 0, imgW_, imgH_, srcDC, 0, 0, SRCCOPY);
    SelectObject(srcDC, old);
    DeleteDC(srcDC);

    // Replay all strokes
    for (const auto& s : strokes_) {
        HPEN pen = CreatePen(PS_SOLID, s.thickness, s.color);
        HPEN oldPen = (HPEN)SelectObject(hdcCanvas_, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcCanvas_, GetStockObject(NULL_BRUSH));

        if (s.tool == Tool::Freehand && s.points.size() >= 2) {
            MoveToEx(hdcCanvas_, s.points[0].x, s.points[0].y, nullptr);
            for (size_t i = 1; i < s.points.size(); ++i) {
                LineTo(hdcCanvas_, s.points[i].x, s.points[i].y);
            }
        }
        else if (s.tool == Tool::Line && s.points.size() == 2) {
            MoveToEx(hdcCanvas_, s.points[0].x, s.points[0].y, nullptr);
            LineTo(hdcCanvas_, s.points[1].x, s.points[1].y);
        }
        else if (s.tool == Tool::Rectangle && s.points.size() == 2) {
            Rectangle(hdcCanvas_,
                s.points[0].x, s.points[0].y,
                s.points[1].x, s.points[1].y);
        }

        SelectObject(hdcCanvas_, oldBrush);
        SelectObject(hdcCanvas_, oldPen);
        DeleteObject(pen);
    }
}

void ImageEditor::CreateControls() {
    // Simple buttons in toolbar area
    auto makeBtn = [&](const wchar_t* text, int x, int id) -> HWND {
        return CreateWindowW(L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, 6, BTN_W, BTN_H,
            hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
    };

    btnFree_  = makeBtn(L"Pencil",   8, 1001);
    btnLine_  = makeBtn(L"Line",     8 + BTN_W + 6, 1002);
    btnRect_  = makeBtn(L"Rect",     8 + 2*(BTN_W + 6), 1003);
    btnColor_ = makeBtn(L"Color",    8 + 3*(BTN_W + 6), 1004);
    btnUndo_  = makeBtn(L"Undo",     8 + 4*(BTN_W + 6), 1005);
    btnCopy_  = makeBtn(L"Copy",     8 + 5*(BTN_W + 6), 1006);
    btnSave_  = makeBtn(L"Save PNG", 8 + 6*(BTN_W + 6), 1007);

    UpdateToolButtons();
}

void ImageEditor::UpdateToolButtons() {
    // Simple visual feedback via button text or we can use check state later
    // For now just leave as is; user knows current by last click
}

void ImageEditor::OnPaint(HDC hdc) {
    // Fill toolbar background
    RECT rcToolbar{ 0, 0, imgW_ + 16, TOOLBAR_H };
    FillRect(hdc, &rcToolbar, (HBRUSH)(COLOR_BTNFACE + 1));

    // Draw the canvas below toolbar
    BitBlt(hdc, 8, TOOLBAR_H + 8, imgW_, imgH_, hdcCanvas_, 0, 0, SRCCOPY);

    // Border around image
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    HPEN old = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 7, TOOLBAR_H + 7, 9 + imgW_, TOOLBAR_H + 9 + imgH_);
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
        // Draw immediately for freehand
        HPEN pen = CreatePen(PS_SOLID, thickness_, currentColor_);
        HPEN old = (HPEN)SelectObject(hdcCanvas_, pen);
        MoveToEx(hdcCanvas_, lastPt_.x, lastPt_.y, nullptr);
        LineTo(hdcCanvas_, pt.x, pt.y);
        SelectObject(hdcCanvas_, old);
        DeleteObject(pen);

        currentStroke_.points.push_back(pt);
        lastPt_ = pt;

        // Invalidate only image area
        RECT inv{ 8, TOOLBAR_H + 8, 8 + imgW_, TOOLBAR_H + 8 + imgH_ };
        InvalidateRect(hwnd_, &inv, FALSE);
    }
    else {
        // For line/rect we just update preview point and full redraw on paint
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
        RebuildCanvas(); // commit
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ImageEditor::CopyToClipboard() {
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();

    // Put CF_BITMAP
    // We need a clean copy of the canvas
    HDC screenDC = GetDC(nullptr);
    HDC tmpDC = CreateCompatibleDC(screenDC);
    HBITMAP hCopy = CreateCompatibleBitmap(screenDC, imgW_, imgH_);
    HBITMAP old = (HBITMAP)SelectObject(tmpDC, hCopy);
    BitBlt(tmpDC, 0, 0, imgW_, imgH_, hdcCanvas_, 0, 0, SRCCOPY);
    SelectObject(tmpDC, old);
    DeleteDC(tmpDC);
    ReleaseDC(nullptr, screenDC);

    SetClipboardData(CF_BITMAP, hCopy); // system owns it now
    CloseClipboard();

    MessageBoxW(hwnd_, L"Copied to clipboard", L"SnipTool", MB_OK | MB_ICONINFORMATION);
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

    // Convert BGRA → RGBA for stb
    std::vector<uint8_t> rgba(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2]; // R
        rgba[i * 4 + 1] = bgra[i * 4 + 1]; // G
        rgba[i * 4 + 2] = bgra[i * 4 + 0]; // B
        rgba[i * 4 + 3] = 255;              // A
    }

    // Convert wide path to UTF-8
    char pathUtf8[MAX_PATH * 4];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr);

    std::wstring ext = path;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    bool isBmp = ext.size() >= 4 && ext.substr(ext.size() - 4) == L".bmp";

    int ok = 0;
    if (isBmp) {
        // Simple BMP write via WinAPI would be longer; use stb for both
        // stb can write BMP too
        ok = stbi_write_bmp(pathUtf8, w, h, 4, rgba.data());
    } else {
        ok = stbi_write_png(pathUtf8, w, h, 4, rgba.data(), w * 4);
    }

    if (ok) {
        MessageBoxW(hwnd_, L"Saved successfully", L"SnipTool", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd_, L"Failed to save file", L"SnipTool", MB_ICONERROR);
    }
}

void ImageEditor::Undo() {
    if (strokes_.empty()) return;
    strokes_.pop_back();
    RebuildCanvas();
    InvalidateRect(hwnd_, nullptr, FALSE);
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
        case 1004: {
            CHOOSECOLORW cc{ sizeof(cc) };
            static COLORREF cust[16]{};
            cc.hwndOwner = hwnd_;
            cc.lpCustColors = cust;
            cc.rgbResult = currentColor_;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                currentColor_ = cc.rgbResult;
            }
            break;
        }
        case 1005: Undo(); break;
        case 1006: CopyToClipboard(); break;
        case 1007: SaveToFile(); break;
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam) - 8;
        int y = GET_Y_LPARAM(lParam) - (TOOLBAR_H + 8);
        if (x >= 0 && y >= 0 && x < imgW_ && y < imgH_) {
            StartStroke({ x, y });
            SetCapture(hwnd_);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (drawing_) {
            int x = GET_X_LPARAM(lParam) - 8;
            int y = GET_Y_LPARAM(lParam) - (TOOLBAR_H + 8);
            // clamp
            x = std::max(0, std::min(imgW_ - 1, x));
            y = std::max(0, std::min(imgH_ - 1, y));
            ContinueStroke({ x, y });
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (drawing_) {
            int x = GET_X_LPARAM(lParam) - 8;
            int y = GET_Y_LPARAM(lParam) - (TOOLBAR_H + 8);
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

        // Double-buffer the whole client
        RECT rc;
        GetClientRect(hwnd_, &rc);
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        // Background
        FillRect(memDC, &rc, (HBRUSH)(COLOR_WINDOW + 1));

        OnPaint(memDC);

        // Preview of current line/rect while drawing
        if (drawing_ && (currentTool_ == Tool::Line || currentTool_ == Tool::Rectangle)) {
            HPEN pen = CreatePen(PS_SOLID, thickness_, currentColor_);
            HPEN oldPen = (HPEN)SelectObject(memDC, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));

            int ox = 8, oy = TOOLBAR_H + 8;
            if (currentTool_ == Tool::Line) {
                MoveToEx(memDC, ox + startPt_.x, oy + startPt_.y, nullptr);
                LineTo(memDC, ox + lastPt_.x, oy + lastPt_.y);
            } else {
                Rectangle(memDC,
                    ox + startPt_.x, oy + startPt_.y,
                    ox + lastPt_.x, oy + lastPt_.y);
            }
            SelectObject(memDC, oldBrush);
            SelectObject(memDC, oldPen);
            DeleteObject(pen);
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

    int winW = imgW_ + 24;
    int winH = imgH_ + TOOLBAR_H + 48;

    // Center on the monitor where the selection was
    int x = screenRect_.left + (screenRect_.right - screenRect_.left - winW) / 2;
    int y = screenRect_.top  + (screenRect_.bottom - screenRect_.top - winH) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"SnipEditorClass", L"SnipTool Editor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
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