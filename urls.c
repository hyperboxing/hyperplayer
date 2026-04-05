#include "urls.h"

#include <shellapi.h>
#include <stdint.h>

typedef struct RectI {
    int x;
    int y;
    int w;
    int h;
} RectI;

static const RectI URL_HYPERUNKNOWN = { 1374, 48, 338, 17 };
static const RectI URL_GITHUB = { 1800, 48, 105, 17 };

static bool point_in_rect(int px, int py, const RectI *r)
{
    return (px >= r->x && px <= r->x + r->w && py >= r->y && py <= r->y + r->h);
}

static bool urls_open(AppState *app, const wchar_t *url)
{
    INT_PTR result;

    if (!url || url[0] == L'\0') {
        return false;
    }

    result = (INT_PTR)ShellExecuteW(
        app ? app->hwnd : NULL,
        L"open",
        url,
        NULL,
        NULL,
        SW_SHOWNORMAL
    );

    if (result <= 32) {
        if (app) {
            app_set_status(app, L"Failed to open URL: %ls", url);
        }
        return false;
    }

    return true;
}

bool urls_mouse_down(AppState *app, int x, int y)
{
    if (!app) {
        return false;
    }

    if (point_in_rect(x, y, &URL_HYPERUNKNOWN)) {
        return urls_open(app, L"http://www.hyperunknown.net");
    }

    if (point_in_rect(x, y, &URL_GITHUB)) {
        return urls_open(app, L"https://github.com/hyperboxing/hyperplayer");
    }

    return false;
}