#include "app.h"
#include "ui.h"
#include "directory_listing_win32.h"
#include "action_buttons.h"
#include "player.h"
#include "sample_list.h"
#include "sample_list_usage_trigger.h"
#include "sample_display.h"
#include "spectrumanalyzer.h"
#include "vumeter.h"
#include "quadrascope.h"
#include "tunnelvisualizer.h"
#include "mousecursor.h"
#include "resource.h"
#include "urls.h"

#include <string.h>
#include <shellapi.h>
#include <windowsx.h>

static const wchar_t WINDOW_CLASS_NAME[] = L"HyperplayerStage3WindowClass";

typedef struct BackBuffer {
    HDC dc;
    HBITMAP bitmap;
    HBITMAP oldBitmap;
    int width;
    int height;
} BackBuffer;

static BackBuffer g_backbuffer = { 0 };

static AppState *app_from_hwnd(HWND hwnd)
{
    return (AppState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static double consume_frame_dt(AppState *app)
{
    ULONGLONG now;
    double dt;

    if (!app) {
        return 0.016;
    }

    now = GetTickCount64();

    if (app->lastUpdateTick == 0) {
        app->lastUpdateTick = now;
        return 0.016;
    }

    dt = (double)(now - app->lastUpdateTick) / 1000.0;
    app->lastUpdateTick = now;

    if (dt < 0.0) {
        dt = 0.0;
    }
    if (dt > 0.25) {
        dt = 0.25;
    }

    return dt;
}

static void process_pending_wheel(AppState *app)
{
    int delta;

    if (!app) {
        return;
    }

    delta = app->pendingWheelDelta;
    if (delta == 0) {
        return;
    }

    app->pendingWheelDelta = 0;

    while (delta >= WHEEL_DELTA) {
        directory_listing_mouse_wheel(app, WHEEL_DELTA);
        delta -= WHEEL_DELTA;
    }

    while (delta <= -WHEEL_DELTA) {
        directory_listing_mouse_wheel(app, -WHEEL_DELTA);
        delta += WHEEL_DELTA;
    }

    app->pendingWheelDelta = delta;
}

static bool ensure_selected_loaded(AppState *app)
{
    if (player_is_loaded(app)) {
        return true;
    }

    if (app->currentSelectedFile[0] == L'\0' || app->currentSelectedName[0] == L'\0') {
        app_set_status(app, L"Select a MOD first.");
        return false;
    }

    return player_load_module(app, app->currentSelectedFile, app->currentSelectedName);
}

static void open_module_from_command_line(AppState *app)
{
    LPWSTR *arguments;
    int argumentCount = 0;

    if (!app) {
        return;
    }

    arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments) {
        return;
    }

    if (argumentCount > 1 && arguments[1][0] != L'\0') {
        if (directory_listing_open_module(app, arguments[1])) {
            player_play(app);
        }
    }

    LocalFree(arguments);
}

static void destroy_backbuffer(void)
{
    if (g_backbuffer.dc) {
        if (g_backbuffer.oldBitmap) {
            SelectObject(g_backbuffer.dc, g_backbuffer.oldBitmap);
            g_backbuffer.oldBitmap = NULL;
        }

        if (g_backbuffer.bitmap) {
            DeleteObject(g_backbuffer.bitmap);
            g_backbuffer.bitmap = NULL;
        }

        DeleteDC(g_backbuffer.dc);
        g_backbuffer.dc = NULL;
    }

    g_backbuffer.width = 0;
    g_backbuffer.height = 0;
}

static bool ensure_backbuffer(HDC referenceDC, int width, int height)
{
    HBITMAP bitmap;
    HDC dc;

    if (!referenceDC || width <= 0 || height <= 0) {
        return false;
    }

    if (g_backbuffer.dc &&
        g_backbuffer.bitmap &&
        g_backbuffer.width == width &&
        g_backbuffer.height == height) {
        return true;
    }

    destroy_backbuffer();

    dc = CreateCompatibleDC(referenceDC);
    if (!dc) {
        return false;
    }

    bitmap = CreateCompatibleBitmap(referenceDC, width, height);
    if (!bitmap) {
        DeleteDC(dc);
        return false;
    }

    g_backbuffer.dc = dc;
    g_backbuffer.bitmap = bitmap;
    g_backbuffer.oldBitmap = (HBITMAP)SelectObject(g_backbuffer.dc, g_backbuffer.bitmap);
    g_backbuffer.width = width;
    g_backbuffer.height = height;

    return true;
}

static void paint_double_buffered(AppState *app, HDC windowDC, const RECT *clientRect)
{
    RECT backRect;
    int width;
    int height;

    if (!app || !windowDC || !clientRect) {
        return;
    }

    width = clientRect->right - clientRect->left;
    height = clientRect->bottom - clientRect->top;

    if (width <= 0 || height <= 0) {
        return;
    }

    if (!ensure_backbuffer(windowDC, width, height)) {
        ui_draw(app, windowDC, clientRect);
        return;
    }

    backRect.left = 0;
    backRect.top = 0;
    backRect.right = width;
    backRect.bottom = height;

    ui_draw(app, g_backbuffer.dc, &backRect);
    BitBlt(windowDC, 0, 0, width, height, g_backbuffer.dc, 0, 0, SRCCOPY);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppState *app = app_from_hwnd(hwnd);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }

        case WM_CREATE: {
            app = app_from_hwnd(hwnd);
            if (app) {
                app->hwnd = hwnd;
                app_init(app);
                mousecursor_load(app);
                mousecursor_apply(hwnd);
                app->lastUpdateTick = GetTickCount64();
            }
            SetTimer(hwnd, 1, 16, NULL);
            return 0;
        }

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                HDC hdc = GetDC(hwnd);
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);

                if (hdc) {
                    ensure_backbuffer(hdc, width, height);
                    ReleaseDC(hwnd, hdc);
                }
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && mousecursor_get()) {
                mousecursor_apply(hwnd);
                return TRUE;
            }
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_TIMER:
            if (app) {
                double dt = consume_frame_dt(app);
                process_pending_wheel(app);
                player_update(app, dt);
                sample_list_usage_trigger_update(app, dt);
                sample_display_update(app, dt);
                spectrumanalyzer_update(app, dt);
                vumeter_update(app, dt);
                tunnelvisualizer_update(app, dt);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_MOUSEMOVE:
            if (app) {
                app->mouseX = GET_X_LPARAM(lParam);
                app->mouseY = GET_Y_LPARAM(lParam);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (app) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);

                if (action_buttons_mouse_down(app, x, y)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                if (urls_mouse_down(app, x, y)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                if (sample_list_mouse_down(app, x, y)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                if (tunnelvisualizer_mouse_down(app, x, y)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                if (directory_listing_mouse_down(app, x, y)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }

                
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (app) {
                app->pendingWheelDelta += GET_WHEEL_DELTA_WPARAM(wParam);
            }
            return 0;

        case WM_KEYDOWN:
            if (app) {
                switch ((int)wParam) {
                    case VK_ESCAPE:
                        DestroyWindow(hwnd);
                        return 0;

                    case VK_SPACE:
                        if (player_is_paused(app) || player_is_stopped(app)) {
                            if (ensure_selected_loaded(app)) {
                                app->showFileBrowser = false;
                                player_play(app);
                            }
                        } else {
                            player_pause(app);
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;

                    case VK_CONTROL:
                    case VK_RCONTROL:
                        if ((lParam & 0x01000000) != 0) {
                            if (ensure_selected_loaded(app)) {
                                player_restart_current_order(app);
                                app->showFileBrowser = false;
                                player_play(app);
                            }
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;
                        }
                        break;

                    case 'S':
                        player_stop(app);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;

                    case VK_LEFT:
                        player_jump_to_order(app, -1);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;

                    case VK_RIGHT:
                        player_jump_to_order(app, 1);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;

                    case VK_UP:
                        if (!directory_listing_move_to_neighbor(app, -1)) {
                            app_set_status(app, L"No previous MOD file in this folder");
                        } else {
                            app->showFileBrowser = false;
                            player_play(app);
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;

                    case VK_DOWN:
                        if (!directory_listing_move_to_neighbor(app, 1)) {
                            app_set_status(app, L"No next MOD file in this folder");
                        } else {
                            app->showFileBrowser = false;
                            player_play(app);
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                }
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            RECT clientRect;
            HDC hdc = BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &clientRect);

            if (app) {
                paint_double_buffered(app, hdc, &clientRect);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            destroy_backbuffer();
            mousecursor_unload();
            if (app) {
                app_shutdown(app);
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prevInstance, PWSTR cmdLine, int cmdShow)
{
    AppState app;
    WNDCLASSW wc;
    DWORD style;
    DWORD exStyle;
    int clientWidth;
    int clientHeight;
    int windowWidth;
    int windowHeight;
    int screenWidth;
    int screenHeight;
    int screenX;
    int screenY;
    bool borderlessMode;
    RECT windowRect;
    HWND hwnd;
    MSG msg;

    (void)prevInstance;
    (void)cmdLine;

    memset(&app, 0, sizeof(app));
    app.instance = instance;

    if (!app_ensure_default_ini_exists()) {
        MessageBoxW(
            NULL,
            L"Failed to create default hyperplayer.ini next to the executable.",
            L"Hyperplayer",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;

    if (!RegisterClassW(&wc)) {
        return 1;
    }

    clientWidth = 1920;
    clientHeight = 1080;

    screenWidth = GetSystemMetrics(SM_CXSCREEN);
    screenHeight = GetSystemMetrics(SM_CYSCREEN);

    borderlessMode = (screenWidth == 1920 && screenHeight == 1080);

    if (borderlessMode) {
        style = WS_POPUP;
        exStyle = 0;
        windowWidth = clientWidth;
        windowHeight = clientHeight;
        screenX = 0;
        screenY = 0;
    } else {
        style = WS_OVERLAPPEDWINDOW;
        exStyle = 0;

        windowRect.left = 0;
        windowRect.top = 0;
        windowRect.right = clientWidth;
        windowRect.bottom = clientHeight;
        AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

        windowWidth = windowRect.right - windowRect.left;
        windowHeight = windowRect.bottom - windowRect.top;

        screenX = (screenWidth - windowWidth) / 2;
        screenY = (screenHeight - windowHeight) / 2;
    }

    hwnd = CreateWindowExW(
        exStyle,
        WINDOW_CLASS_NAME,
        L"Hyperplayer v1.1",
        style,
        screenX,
        screenY,
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        instance,
        &app
    );
    if (!hwnd) {
        return 1;
    }

    open_module_from_command_line(&app);

    {
        HICON hIconBig = (HICON)LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            32,
            32,
            LR_DEFAULTCOLOR
        );
    
        HICON hIconSmall = (HICON)LoadImageW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            16,
            16,
            LR_DEFAULTCOLOR
        );
    
        if (hIconBig) {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        }
    
        if (hIconSmall) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        }
    }

    ShowWindow(hwnd, cmdShow);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
