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
    std::vector<POINT> points; // for freehand: many points; for line/rect: 2 points
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
    HBITMAP hCanvas_ = nullptr;   // working bitmap with drawings
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

    // UI
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
    void CopyToClipboard();
    void SaveToFile();
    void Undo();
    void UpdateToolButtons();
};