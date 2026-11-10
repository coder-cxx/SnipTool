#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum class Tool {
    Freehand,
    Line,
    Rectangle
};

struct DrawStroke {
    Tool tool;
    COLORREF color;
    int thickness;
    std::vector<POINT> points;
};

class ImageEditor {
public:
    ImageEditor(HINSTANCE hInstance, HBITMAP hSourceBmp, const RECT& screenRect);
    ~ImageEditor();

    void Run();

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

    HWND btnCopy_ = nullptr;
    HWND btnSave_ = nullptr;
    HWND btnUndo_ = nullptr;
    HWND btnFree_ = nullptr;
    HWND btnLine_ = nullptr;
    HWND btnRect_ = nullptr;
    HWND btnColor_ = nullptr;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void RebuildCanvas();
    void OnPaint(HDC hdc);
    void StartStroke(POINT pt);
    void ContinueStroke(POINT pt);
    void EndStroke(POINT pt);
    void CopyToClipboard(bool showNotification = true);
    void SaveToFile();
    void Undo();
    void UpdateToolButtons();

    // Палитра
    static constexpr int PALETTE_BTN_OFFSET = 7;
    inline static constexpr COLORREF paletteColors_[6] = {
        RGB(255, 0, 0),    // Red
        RGB(0, 255, 0),    // Green
        RGB(0, 0, 255),    // Blue
        RGB(255, 255, 0),  // Yellow
        RGB(0, 0, 0),      // Black
        RGB(255, 255, 255) // White
    };
};