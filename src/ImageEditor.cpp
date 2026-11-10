#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ImageEditor.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <vector>
#include <algorithm>
#include <cstdint>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

constexpr int TOOLBAR_H = 40;
constexpr int BTN_W = 90;
constexpr int BTN_H = 28;
constexpr int MARGIN_X = 8;
constexpr int MARGIN_Y_TOP = 6;
constexpr int IMAGE_OFFSET_X = 8;
constexpr int IMAGE_OFFSET_Y = TOOLBAR_H + 8;

// Минимальные размеры окна, чтобы влезала вся панель инструментов
constexpr int MIN_WIN_W = 760;
constexpr int MIN_WIN_H = 200;

bool GetBitmapData(HBITMAP hBmp, int& w, int& h, std::vector<uint8_t>& pixelsBGRA) {
    BITMAP bm{};
    if (!GetObject(hBmp, sizeof(bm), &bm)) return false;
    w = bm.bmWidth;
    h = bm.bmHeight;

    BITMAPINFOHEADER bi{ sizeof(bi) };
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    pixelsBGRA.resize(w * h * 4);
    HDC hdc = GetDC(nullptr);
    int res = GetDIBits(hdc, hBmp, 0, h, pixelsBGRA.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    return res != 0;
}

POINT GetImagePoint(LPARAM lParam) {
    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    pt.x -= IMAGE_OFFSET_X;
    pt.y -= IMAGE_OFFSET_Y;
    return pt;
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
    ReleaseDC(nullptr, screenDC);

    hdcCanvas_ = CreateCompatibleDC(nullptr);
    hCanvas_ = CreateCompatibleBitmap(screenDC, imgW_, imgH_);
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
    HDC srcDC = CreateCompatibleDC(hdcCanvas_);
    HBITMAP old = (HBITMAP)SelectObject(srcDC, hOriginal_);
    BitBlt(hdcCanvas_, 0, 0, imgW_, imgH_, srcDC, 0, 0, SRCCOPY);
    SelectObject(srcDC, old);
    DeleteDC(srcDC);

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
            Rectangle(hdcCanvas_, s.points[0].x, s.points[0].y, s.points[1].x, s.points[1].y);
        }

        SelectObject(hdcCanvas_, oldBrush);
        SelectObject(hdcCanvas_, oldPen);
        DeleteObject(pen);
    }
}

void ImageEditor::CreateControls() {
    auto makeBtn = [&](const wchar_t* text, int x, int id) -> HWND {
        HWND hwnd = CreateWindowW(L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, MARGIN_Y_TOP, BTN_W, BTN_H,
            hwnd_, (HMENU)(INT_PTR)id, hInst_, nullptr);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        return hwnd;
    };

    btnFree_  = makeBtn(L"Pencil",   MARGIN_X, 1001);
    btnLine_  = makeBtn(L"Line",     MARGIN_X + 1 * (BTN_W + 6), 1002);
    btnRect_  = makeBtn(L"Rect",     MARGIN_X + 2 * (BTN_W + 6), 1003);
    btnColor_ = makeBtn(L"More...",  MARGIN_X + 3 * (BTN_W + 6), 1004);
    btnUndo_  = makeBtn(L"Undo",     MARGIN_X + 4 * (BTN_W + 6), 1005);
    btnCopy_  = makeBtn(L"Copy",     MARGIN_X + 5 * (BTN_W + 6), 1006);
    btnSave_  = makeBtn(L"Save PNG", MARGIN_X + 6 * (BTN_W + 6), 1007);

    UpdateToolButtons();
}

void ImageEditor::UpdateToolButtons() {
    SetWindowTextW(btnFree_,  (currentTool_ == Tool::Freehand)  ? L"[Pencil]" : L"Pencil");
    SetWindowTextW(btnLine_,  (currentTool_ == Tool::Line)      ? L"[Line]"   : L"Line");
    SetWindowTextW(btnRect_,  (currentTool_ == Tool::Rectangle) ? L"[Rect]"   : L"Rect");
}

void ImageEditor::OnPaint(HDC hdc) {
    RECT rcToolbar{ 0, 0, imgW_ + 16, TOOLBAR_H };
    FillRect(hdc, &rcToolbar, (HBRUSH)(COLOR_BTNFACE + 1));

    BitBlt(hdc, IMAGE_OFFSET_X, IMAGE_OFFSET_Y, imgW_, imgH_, hdcCanvas_, 0, 0, SRCCOPY);

    // Отрисовка палитры быстрых цветов
    int px = MARGIN_X + PALETTE_BTN_OFFSET * (BTN_W + 6);
    int py = MARGIN_Y_TOP;
    for (int i = 0; i < 6; ++i) {
        RECT rc{ px + i * 28, py, px + i * 28 + 24, py + 24 };

        HBRUSH hBrush = CreateSolidBrush(paletteColors_[i]);
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        if (currentColor_ == paletteColors_[i]) {
            HPEN selPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
            HPEN oldSel = (HPEN)SelectObject(hdc, selPen);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left - 2, rc.top - 2, rc.right + 2, rc.bottom + 2);
            SelectObject(hdc, oldSel);
            DeleteObject(selPen);
        }
    }

    // Рамка вокруг изображения
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    HPEN oldBp = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH oldBb = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, IMAGE_OFFSET_X - 1, IMAGE_OFFSET_Y - 1, IMAGE_OFFSET_X + imgW_ + 1, IMAGE_OFFSET_Y + imgH_ + 1);
    SelectObject(hdc, oldBb);
    SelectObject(hdc, oldBp);
    DeleteObject(borderPen);
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

        RECT inv{ IMAGE_OFFSET_X, IMAGE_OFFSET_Y, IMAGE_OFFSET_X + imgW_, IMAGE_OFFSET_Y + imgH_ };
        InvalidateRect(hwnd_, &inv, FALSE);
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
    }

    RebuildCanvas();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ImageEditor::CopyToClipboard(bool showNotification) {
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();

    HDC screenDC = GetDC(nullptr);
    HDC tmpDC = CreateCompatibleDC(screenDC);
    HBITMAP hCopy = CreateCompatibleBitmap(screenDC, imgW_, imgH_);
    HBITMAP old = (HBITMAP)SelectObject(tmpDC, hCopy);
    BitBlt(tmpDC, 0, 0, imgW_, imgH_, hdcCanvas_, 0, 0, SRCCOPY);
    SelectObject(tmpDC, old);
    DeleteDC(tmpDC);
    ReleaseDC(nullptr, screenDC);

    SetClipboardData(CF_BITMAP, hCopy);

    int w = 0, h = 0;
    std::vector<uint8_t> bgra;
    if (GetBitmapData(hCopy, w, h, bgra)) {
        size_t dibSize = sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4;
        HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, dibSize);
        if (hDIB) {
            auto* pDIB = reinterpret_cast<BITMAPV5HEADER*>(GlobalLock(hDIB));
            pDIB->bV5Size = sizeof(BITMAPV5HEADER);
            pDIB->bV5Width = w;
            pDIB->bV5Height = -h;
            pDIB->bV5Planes = 1;
            pDIB->bV5BitCount = 32;
            pDIB->bV5Compression = BI_RGB;
            pDIB->bV5SizeImage = static_cast<DWORD>(w) * h * 4;

            uint8_t* pixelsDest = reinterpret_cast<uint8_t*>(pDIB) + sizeof(BITMAPV5HEADER);
            memcpy(pixelsDest, bgra.data(), bgra.size());

            GlobalUnlock(hDIB);
            SetClipboardData(CF_DIB, hDIB);
        }
    }

    CloseClipboard();

    if (showNotification) {
        MessageBoxW(hwnd_, L"Copied to clipboard", L"SnipTool", MB_OK | MB_ICONINFORMATION);
    }
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

    std::vector<uint8_t> rgba(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
    }

    char pathUtf8[MAX_PATH * 4];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr);

    std::wstring wpath(path);
    std::transform(wpath.begin(), wpath.end(), wpath.begin(), ::towlower);
    bool isBmp = (wpath.length() >= 4) && (wpath.compare(wpath.length() - 4, 4, L".bmp") == 0);

    int ok = isBmp ? stbi_write_bmp(pathUtf8, w, h, 4, rgba.data())
                   : stbi_write_png(pathUtf8, w, h, 4, rgba.data(), w * 4);

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
        case 1006: CopyToClipboard(true); break;
        case 1007: SaveToFile(); break;
        }
        UpdateToolButtons();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        int px = MARGIN_X + PALETTE_BTN_OFFSET * (BTN_W + 6);
        int py = MARGIN_Y_TOP;

        if (my >= py && my <= py + 24) {
            for (int i = 0; i < 6; ++i) {
                if (mx >= px + i * 28 && mx <= px + i * 28 + 24) {
                    currentColor_ = paletteColors_[i];
                    RECT inv{ 0, 0, imgW_ + 16, TOOLBAR_H };
                    InvalidateRect(hwnd_, &inv, FALSE);
                    return 0;
                }
            }
        }

        POINT pt = GetImagePoint(lParam);
        if (pt.x >= 0 && pt.y >= 0 && pt.x < imgW_ && pt.y < imgH_) {
            StartStroke(pt);
            SetCapture(hwnd_);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (drawing_) {
            POINT pt = GetImagePoint(lParam);
            int px = std::max(0, std::min(imgW_ - 1, static_cast<int>(pt.x)));
            int py = std::max(0, std::min(imgH_ - 1, static_cast<int>(pt.y)));
            ContinueStroke({ px, py });
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (drawing_) {
            POINT pt = GetImagePoint(lParam);
            int px = std::max(0, std::min(imgW_ - 1, static_cast<int>(pt.x)));
            int py = std::max(0, std::min(imgH_ - 1, static_cast<int>(pt.y)));
            EndStroke({ px, py });
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

        if (drawing_ && (currentTool_ == Tool::Line || currentTool_ == Tool::Rectangle)) {
            HPEN pen = CreatePen(PS_SOLID, thickness_, currentColor_);
            HPEN oldPen = (HPEN)SelectObject(memDC, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));

            if (currentTool_ == Tool::Line) {
                MoveToEx(memDC, IMAGE_OFFSET_X + startPt_.x, IMAGE_OFFSET_Y + startPt_.y, nullptr);
                LineTo(memDC, IMAGE_OFFSET_X + lastPt_.x, IMAGE_OFFSET_Y + lastPt_.y);
            } else {
                Rectangle(memDC,
                    IMAGE_OFFSET_X + startPt_.x, IMAGE_OFFSET_Y + startPt_.y,
                    IMAGE_OFFSET_X + lastPt_.x, IMAGE_OFFSET_Y + lastPt_.y);
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

    int winW = std::max(MIN_WIN_W, imgW_ + 24);
    int winH = std::max(MIN_WIN_H, imgH_ + TOOLBAR_H + 48);

    int x = screenRect_.left + (screenRect_.right - screenRect_.left - winW) / 2;
    int y = screenRect_.top  + (screenRect_.bottom - screenRect_.top - winH) / 2;

    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int workLeft   = static_cast<int>(workArea.left);
    int workTop    = static_cast<int>(workArea.top);
    int workRight  = static_cast<int>(workArea.right);
    int workBottom = static_cast<int>(workArea.bottom);

    x = std::max(workLeft, std::min(x, workRight - winW));
    y = std::max(workTop, std::min(y, workBottom - winH));

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

    // Автоматическое копирование в буфер обмена при создании (без уведомления)
    CopyToClipboard(false);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(L"SnipEditorClass", hInst_);
}