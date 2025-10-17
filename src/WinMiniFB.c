#if !PICO_ON_DEVICE

#include "MiniFB.h"

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static WNDCLASS s_wc;
static HWND s_wnd;
static int s_close = 0;
static int s_width;
static int s_height;
static int s_scale = 1;
static HDC s_hdc;
static void *s_buffer;
BITMAPINFO *s_bitmapInfo;
static char key_status[512] = {0};
RECT rect = {0};
HMENU hOptionsMenu;
POINT lastPos = {0, 0};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern void HandleInput(WPARAM wParam, BOOL isKeyDown);

extern void HandleMouse(int x, int y, int buttons);

extern BOOL HanldeMenu(int menu_id, BOOL checked);

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    LRESULT res = 0;
    POINT currentPos;

    switch (message) {
        case WM_COMMAND: {
            int menu_id = LOWORD(wParam);
            unsigned int checked = GetMenuState(hOptionsMenu, menu_id, MF_BYCOMMAND) & MF_CHECKED;
            // if (HanldeMenu(menu_id, checked)) {
                // CheckMenuItem(hOptionsMenu, menu_id, MF_BYCOMMAND | MF_CHECKED);
            // } else {
                // CheckMenuItem(hOptionsMenu, menu_id, MF_BYCOMMAND | MF_UNCHECKED);
            // }
            break;
        }
        case WM_PAINT: {
            if (s_buffer) {
                StretchDIBits(s_hdc,
                              0, 0, s_width * s_scale, s_height * s_scale,
                              0, 0, s_width, s_height,
                              s_buffer,
                              s_bitmapInfo, DIB_RGB_COLORS, SRCCOPY);

                ValidateRect(hWnd, NULL);
            }

            break;
        }

        case WM_MOUSEMOVE:
            if (GetCapture() != hWnd) {
                // SetCapture(hWnd);
                ShowCursor(FALSE);
                // SetCursorPos(GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2);
                // GetCursorPos(&lastPos);
            }
             if (GetFocus() != hWnd) {
                 SetFocus(hWnd);
             }
            GetCursorPos(&currentPos);
            HandleMouse(currentPos.x, currentPos.y, 0);
            break;
        case WM_LBUTTONDOWN:
            GetCursorPos(&currentPos);
            HandleMouse(currentPos.x, currentPos.y, 0b10);

            break;

        case WM_LBUTTONUP:
            GetCursorPos(&currentPos);
            HandleMouse(currentPos.x, currentPos.y, 0b00);

            break;
        case WM_RBUTTONDOWN:
            GetCursorPos(&currentPos);
            HandleMouse(currentPos.x, currentPos.y, 0b1);

            break;
        case WM_RBUTTONUP:
            GetCursorPos(&currentPos);
            HandleMouse(currentPos.x, currentPos.y, 0b0);

            break;

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            HandleInput(wParam, !(lParam >> 31 & 1));
            key_status[wParam] = !(lParam >> 31 & 1);

            if ((wParam & 0xFF) == 27 && GetCapture() == hWnd) {
                // ShowCursor(TRUE);
                // ReleaseCapture();
            }
            /*            if ((wParam & 0xFF) == 27)
                            s_close = 1;*/

            break;
        }

        case WM_CLOSE: {
            s_close = 1;
            break;
        }

        default: {
            res = DefWindowProc(hWnd, message, wParam, lParam);
        }
    }

    return res;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int mfb_open(const char *title, int width, int height, int scale) {
    s_wc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
    s_wc.lpfnWndProc = WndProc;
    s_wc.hCursor = LoadCursor(0, IDC_ARROW);
    s_wc.lpszClassName = title;

    RegisterClass(&s_wc);

    rect.right = width * scale;
    rect.bottom = (10 + height) * scale;

    AdjustWindowRect(&rect, WS_POPUP | WS_SYSMENU | WS_CAPTION, 0);

    rect.right -= rect.left;
    rect.bottom -= rect.top;

    s_width = width;
    s_height = height;
    s_scale = scale;

    if (GetSystemMetrics(SM_XVIRTUALSCREEN) < 0) {
        //
        s_wnd = CreateWindowEx(0,
                               title, title,
                               WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                               -1920 + (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2,
                               320 + (GetSystemMetrics(SM_CYSCREEN) - rect.bottom + rect.top) / 2,
                               rect.right, rect.bottom,
                               0, 0, 0, 0);
    } else {
        s_wnd = CreateWindowEx(0,
                           title, title,
                           WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                           (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2,
                           (GetSystemMetrics(SM_CYSCREEN) - rect.bottom + rect.top) / 2,
                           rect.right, rect.bottom,
                           0, 0, 0, 0);
    }

    if (!s_wnd)
        return 0;

// Create a menu with checkable items "Tandy Enabled" and "Switch to composite"
    HMENU hMenu = CreateMenu();
    hOptionsMenu = CreateMenu();

    AppendMenu(hOptionsMenu, MF_STRING | MF_UNCHECKED, 1, "Tandy Enabled");
    AppendMenu(hOptionsMenu, MF_STRING | MF_UNCHECKED, 2, "Switch to composite");

    AppendMenu(hMenu, MF_POPUP, (UINT_PTR) hOptionsMenu, "Options");

    SetMenu(s_wnd, hMenu);
    ShowWindow(s_wnd, SW_NORMAL);

    s_bitmapInfo = (BITMAPINFO *) malloc(sizeof(BITMAPINFO) + sizeof(RGBQUAD) * 256);
    s_bitmapInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s_bitmapInfo->bmiHeader.biBitCount = 32;
    s_bitmapInfo->bmiHeader.biPlanes = 1;
    s_bitmapInfo->bmiHeader.biWidth = width;
    s_bitmapInfo->bmiHeader.biHeight = -height;
    s_bitmapInfo->bmiHeader.biCompression = 0;
    s_bitmapInfo->bmiHeader.biSizeImage = 0;
    // s_bitmapInfo->bmiHeader.biXPelsPerMeter = 14173;
    // s_bitmapInfo->bmiHeader.biYPelsPerMeter = 14173;
    s_bitmapInfo->bmiHeader.biClrUsed = 0xff;
    s_bitmapInfo->bmiHeader.biClrImportant = 1;


    /*
        s_bitmapInfo->bmiHeader.biBitCount = 16;
        s_bitmapInfo->bmiHeader.biCompression = BI_BITFIELDS;

        ((DWORD *)s_bitmapInfo->bmiColors)[0] = 0xF800;
        ((DWORD *)s_bitmapInfo->bmiColors)[1] = 0x07E0;
        ((DWORD *)s_bitmapInfo->bmiColors)[2] = 0x001F;
    */
    // ((DWORD *)s_bitmapInfo->bmiColors)[0] = 0x001F;
    // ((DWORD *)s_bitmapInfo->bmiColors)[1] = 0x001F;
    // ((DWORD *)s_bitmapInfo->bmiColors)[2] = 0x001F;
    s_hdc = GetDC(s_wnd);

    return 1;
}

void mfb_set_pallete_array(const uint32_t *new_palette, uint8_t start, uint8_t count) {
    uint32_t *palette = (uint32_t *) &s_bitmapInfo->bmiColors[0];
    for (int i = start; i < start + count; i++) {
        palette[i] = new_palette[i - start];
    }
}

void mfb_set_pallete(const uint8_t color_index, const uint32_t color) {
    uint32_t *palette = (uint32_t *) &s_bitmapInfo->bmiColors[0];
    palette[color_index] = color;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int mfb_update(void *buffer, int fps_limit) {
    static DWORD previousFrameTime = 0;
    MSG msg;

    s_buffer = buffer;

    InvalidateRect(s_wnd, NULL, TRUE);
    SendMessage(s_wnd, WM_PAINT, 0, 0);

    while (PeekMessage(&msg, s_wnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (s_close == 1)
        return -1;

    if (fps_limit) {
        const DWORD targetFrameTime = 1000 / fps_limit;
        const DWORD elapsedFrameTime = GetTickCount() - previousFrameTime;

        if (elapsedFrameTime < targetFrameTime) {
            Sleep(targetFrameTime - elapsedFrameTime);
        }

        previousFrameTime = GetTickCount();
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mfb_close() {
    s_buffer = 0;
    free(s_bitmapInfo);
    ReleaseDC(s_wnd, s_hdc);
    DestroyWindow(s_wnd);
}

char *mfb_keystatus() {
    return key_status;
}

#endif
