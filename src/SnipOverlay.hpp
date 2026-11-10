#pragma once
#include <windows.h>

class SnipOverlay {
public:
    // Shows dimmed overlay, user selects region. Returns HBITMAP of selection (caller DeleteObject) or nullptr.
    static HBITMAP CaptureRegion(HINSTANCE hInstance, RECT& selected);

    // Captures entire virtual screen without overlay/editor. Caller must DeleteObject.
    static HBITMAP CaptureFullScreenBitmap();
};
