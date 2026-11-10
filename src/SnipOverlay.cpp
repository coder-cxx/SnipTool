#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SnipOverlay.hpp"
#include <windowsx.h>
#include <algorithm>
#include <vector>

// Подключаем DXGI и Direct3D 11 для "чистого" захвата
#include <d3d11.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

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

// === СПОСОБ 2: DXGI Desktop Duplication (Чистый захват с GPU) ===
HBITMAP CaptureScreenDXGI(int& outW, int& outH) {
    HRESULT hr;
    HBITMAP hResultBmp = nullptr;

    // 1. Инициализация COM
    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return nullptr;

    // 2. Создание D3D11 устройства
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                           D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) {
        CoUninitialize();
        return nullptr;
    }

    // 3. Получение DXGI адаптера и вывода (монитора)
    IDXGIDevice* dxgiDevice = nullptr;
    hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter* adapter = nullptr;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) {
            IDXGIOutput* output = nullptr;
            hr = adapter->EnumOutputs(0, &output); // 0 = основной монитор
            if (SUCCEEDED(hr)) {
                IDXGIOutput1* output1 = nullptr;
                hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
                if (SUCCEEDED(hr)) {
                    // 4. Создание дубликата вывода
                    IDXGIOutputDuplication* duplication = nullptr;
                    hr = output1->DuplicateOutput(device, &duplication);
                    if (SUCCEEDED(hr)) {
                        // 5. Захват кадра
                        DXGI_OUTDUPL_FRAME_INFO frameInfo;
                        IDXGIResource* resource = nullptr;
                        hr = duplication->AcquireNextFrame(1000, &frameInfo, &resource);
                        
                        if (SUCCEEDED(hr) && resource) {
                            // 6. Получение текстуры
                            ID3D11Texture2D* tex = nullptr;
                            hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
                            if (SUCCEEDED(hr)) {
                                D3D11_TEXTURE2D_DESC desc;
                                tex->GetDesc(&desc);
                                outW = desc.Width;
                                outH = desc.Height;

                                // 7. Создание staging-текстуры для чтения CPU
                                desc.Usage = D3D11_USAGE_STAGING;
                                desc.BindFlags = 0;
                                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                                desc.MiscFlags = 0;
                                
                                ID3D11Texture2D* stagingTex = nullptr;
                                hr = device->CreateTexture2D(&desc, nullptr, &stagingTex);
                                if (SUCCEEDED(hr)) {
                                    // 8. Копирование и маппинг
                                    context->CopyResource(stagingTex, tex);
                                    D3D11_MAPPED_SUBRESOURCE mapped;
                                    hr = context->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
                                    
                                    if (SUCCEEDED(hr)) {
                                        // 9. Конвертация в HBITMAP
                                        BITMAPINFO bmi{};
                                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                        bmi.bmiHeader.biWidth = outW;
                                        bmi.bmiHeader.biHeight = -outH; // top-down
                                        bmi.bmiHeader.biPlanes = 1;
                                        bmi.bmiHeader.biBitCount = 32;
                                        bmi.bmiHeader.biCompression = BI_RGB;

                                        HDC hdc = GetDC(nullptr);
                                        void* bits = nullptr;
                                        hResultBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
                                        
                                        if (hResultBmp && bits) {
                                            // Копируем пиксели (BGRA)
                                            uint8_t* src = static_cast<uint8_t*>(mapped.pData);
                                            uint8_t* dst = static_cast<uint8_t*>(bits);
                                            size_t rowBytes = outW * 4;
                                            
                                            for (int y = 0; y < outH; ++y) {
                                                memcpy(dst + y * rowBytes, src + y * mapped.RowPitch, rowBytes);
                                            }
                                        }
                                        ReleaseDC(nullptr, hdc);
                                        context->Unmap(stagingTex, 0);
                                    }
                                    if (stagingTex) stagingTex->Release();
                                }
                                tex->Release();
                            }
                            duplication->ReleaseFrame();
                            duplication->Release();
                        }
                        if (resource) resource->Release();
                    }
                    output1->Release();
                }
                output->Release();
            }
            adapter->Release();
        }
        dxgiDevice->Release();
    }

    // 10. Очистка
    if (context) context->Release();
    if (device) device->Release();
    CoUninitialize();

    return hResultBmp;
}

// === FALLBACK: Старый метод, если DXGI не сработал ===
bool CaptureFullScreenFallback() {
    CleanupMem();
    g_state.screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_state.screenTop  = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_state.screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_state.screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC screenDC = GetDC(nullptr);
    g_state.memDC = CreateCompatibleDC(screenDC);
    g_state.memBmp = CreateCompatibleBitmap(screenDC, g_state.screenW, g_state.screenH);
    g_state.oldBmp = (HBITMAP)SelectObject(g_state.memDC, g_state.memBmp);

    // Пытаемся скрыть известные оверлеи перед захватом (дополнительная защита)
    HWND hwndOverlay = FindWindowW(L"DiscordOverlay", nullptr);
    bool wasVisible = hwndOverlay && IsWindowVisible(hwndOverlay);
    if (wasVisible) ShowWindow(hwndOverlay, SW_HIDE);

    BitBlt(g_state.memDC, 0, 0, g_state.screenW, g_state.screenH,
           screenDC, g_state.screenLeft, g_state.screenTop, SRCCOPY | CAPTUREBLT);

    if (wasVisible) ShowWindow(hwndOverlay, SW_SHOW); // Восстанавливаем

    ReleaseDC(nullptr, screenDC);
    return g_state.memBmp != nullptr;
}

bool CaptureFullScreen() {
    CleanupMem();
    
    // Пробуем современный "чистый" метод
    HBITMAP dxgiBmp = CaptureScreenDXGI(g_state.screenW, g_state.screenH);
    
    if (dxgiBmp) {
        // DXGI успешен. Подготавливаем state так, как это делал старый код
        g_state.screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        g_state.screenTop  = GetSystemMetrics(SM_YVIRTUALSCREEN);
        
        HDC screenDC = GetDC(nullptr);
        g_state.memDC = CreateCompatibleDC(screenDC);
        g_state.oldBmp = (HBITMAP)SelectObject(g_state.memDC, dxgiBmp);
        g_state.memBmp = dxgiBmp; // Теперь memBmp указывает на DXGI-битмап
        ReleaseDC(nullptr, screenDC);
        return true;
    }

    // Fallback, если DXGI не поддерживается (редко, но бывает на VM или старых драйверах)
    return CaptureFullScreenFallback();
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
        p[0] = static_cast<uint8_t>(p[0] * 0.30); // B
        p[1] = static_cast<uint8_t>(p[1] * 0.30); // G
        p[2] = static_cast<uint8_t>(p[2] * 0.30); // R
        p[3] = 255; // A
        p += 4;
    }

    // Restore original brightness inside selection rectangle
    if (g_state.selecting || g_state.finished) {
        int x1 = (std::min)(g_state.start.x, g_state.current.x);
        int y1 = (std::min)(g_state.start.y, g_state.current.y);
        int x2 = (std::max)(g_state.start.x, g_state.current.x);
        int y2 = (std::max)(g_state.start.y, g_state.current.y);

        x1 = (std::max)(0, x1); y1 = (std::max)(0, y1);
        x2 = (std::min)(g_state.screenW, x2); y2 = (std::min)(g_state.screenH, y2);

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

        // Draw selection border
        auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
            if (x < 0 || y < 0 || x >= g_state.screenW || y >= g_state.screenH) return;
            int idx = (y * g_state.screenW + x) * 4;
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

        BitBlt(selDC, 0, 0, w, h, g_state.memDC,
               selected.left - g_state.screenLeft, selected.top - g_state.screenTop, SRCCOPY);

        SelectObject(selDC, old);
        DeleteDC(selDC);
        ReleaseDC(nullptr, screenDC);
    }

    CleanupMem();
    return resultBmp;
}