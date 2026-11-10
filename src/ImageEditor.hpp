#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum class Tool {
    Freehand,
    Line,
    Rectangle,
    Arrow
};

struct DrawStroke {
    Tool tool;
    COLORREF color;
    int thickness;
    std::vector<POINT> points; // freehand: many; line/rect/arrow: 2
};

class ImageEditor {
public:
    ImageEditor(HINSTANCE hInstance, HBITMAP hSourceBmp, const RECT& screenRect);
    ~ImageEditor();

    void Run(); // modal message loop

private:
    HINSTANCE hInst_;
    HWND hwnd_ = nullptr;
    HBITMAP hOriginal_ = nullptr;
    HBITMAP hCanvas_ = nullptr;
    HDC hdcCanvas_ = nullptr;
    HBITMAP hOldCanvas_ = nullptr;

    int imgW_ = 0;
    int imgH_ = 0;
    RECT screenRect_;

    Tool currentTool_ = Tool::Freehand;
    COLORREF currentColor_ = RGB(255, 0, 0);
    int thickness_ = 3;

    bool drawing_ = false;
    POINT startPt_{};
    POINT lastPt_{};
    std::vector<DrawStroke> strokes_;
    DrawStroke currentStroke_;

    // UI controls
    HWND btnFree_ = nullptr;
    HWND btnLine_ = nullptr;
    HWND btnRect_ = nullptr;
    HWND btnArrow_ = nullptr;
    HWND btnUndo_ = nullptr;
    HWND btnSave_ = nullptr;
    // palette buttons
    static constexpr int PALETTE_COUNT = 8;
    HWND btnPalette_[PALETTE_COUNT]{};
    COLORREF paletteColors_[PALETTE_COUNT] = {
        RGB(255, 0, 0),     // red
        RGB(0, 180, 0),     // green
        RGB(0, 100, 255),   // blue
        RGB(255, 200, 0),   // yellow
        RGB(255, 100, 0),   // orange
        RGB(180, 0, 255),   // purple
        RGB(0, 0, 0),       // black
        RGB(255, 255, 255)  // white
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void RebuildCanvas();
    void OnPaint(HDC hdc);
    void StartStroke(POINT pt);
    void ContinueStroke(POINT pt);
    void EndStroke(POINT pt);
    void CopyToClipboard();
    void SaveToFile();
    void Undo();
    void DrawArrow(HDC hdc, POINT from, POINT to, int thickness, COLORREF color);
};
