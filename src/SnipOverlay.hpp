#pragma once
#include <windows.h>

class SnipOverlay {
public:
    // Shows dimmed fullscreen overlay, lets user select region with mouse.
    // Returns HBITMAP of the selected area (caller must DeleteObject), or nullptr on cancel.
    // selected receives the screen coordinates of the selection.
    static HBITMAP CaptureRegion(HINSTANCE hInstance, RECT& selected);
};
