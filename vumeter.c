#include "vumeter.h"
#include "player.h"

#include <windows.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct BoxRect {
    int x;
    int y;
    int w;
    int h;
} BoxRect;

typedef struct VuMeterConfig {
    bool loaded;
    COLORREF color1;
    COLORREF color2;
    COLORREF color3;
    BYTE alpha;
} VuMeterConfig;

static const BoxRect g_boxes[] = {
    {1379, 816, 17, 101}, {1402, 816, 17, 101}, {1425, 816, 17, 101}, {1448, 816, 17, 101},
    {1471, 816, 17, 101}, {1494, 816, 17, 101}, {1517, 816, 17, 101}, {1540, 816, 17, 101},
    {1563, 816, 17, 101}, {1586, 816, 17, 101}, {1609, 816, 17, 101}, {1632, 816, 17, 101},
    {1655, 816, 17, 101}, {1678, 816, 17, 101}, {1701, 816, 17, 101}, {1724, 816, 17, 101},
    {1747, 816, 17, 101}, {1770, 816, 17, 101}, {1793, 816, 17, 101}, {1816, 816, 17, 101},
    {1839, 816, 17, 101}, {1862, 816, 17, 101}, {1885, 816, 17, 101}
};

static float g_level = 0.0f;

static VuMeterConfig g_config = {
    false,
    RGB(0, 255, 0),
    RGB(255, 255, 0),
    RGB(255, 0, 0),
    84
};

static void build_ini_path(wchar_t *path, size_t pathCount)
{
    DWORD len;

    if (!path || pathCount == 0) {
        return;
    }

    len = GetModuleFileNameW(NULL, path, (DWORD)pathCount);
    if (len == 0 || len >= pathCount) {
        lstrcpynW(path, L"hyperplayer.ini", (int)pathCount);
        return;
    }

    while (len > 0) {
        wchar_t c = path[len - 1];
        if (c == L'\\' || c == L'/') {
            break;
        }
        --len;
    }

    if (len == 0) {
        lstrcpynW(path, L"hyperplayer.ini", (int)pathCount);
        return;
    }

    path[len] = L'\0';
    lstrcatW(path, L"hyperplayer.ini");
}

static COLORREF parse_hex_color(const wchar_t *text, COLORREF fallback)
{
    wchar_t *endPtr;
    unsigned long value;
    int r;
    int g;
    int b;

    if (!text || lstrlenW(text) != 6) {
        return fallback;
    }

    value = wcstoul(text, &endPtr, 16);
    if (endPtr == text || *endPtr != L'\0' || value > 0xFFFFFFUL) {
        return fallback;
    }

    r = (int)((value >> 16) & 0xFF);
    g = (int)((value >> 8) & 0xFF);
    b = (int)(value & 0xFF);

    return RGB(r, g, b);
}

static BYTE parse_alpha_value(const wchar_t *text, BYTE fallback)
{
    wchar_t *endPtr;
    unsigned long value;

    if (!text || *text == L'\0') {
        return fallback;
    }

    value = wcstoul(text, &endPtr, 10);
    if (endPtr == text || *endPtr != L'\0' || value > 255UL) {
        return fallback;
    }

    return (BYTE)value;
}

static void load_vumeter_config(void)
{
    wchar_t iniPath[MAX_PATH];
    wchar_t value[64];

    build_ini_path(iniPath, sizeof(iniPath) / sizeof(iniPath[0]));

    GetPrivateProfileStringW(L"VUMETER", L"VUCOLOR1", L"", value, sizeof(value) / sizeof(value[0]), iniPath);
    if (value[0] != L'\0') {
        g_config.color1 = parse_hex_color(value, g_config.color1);
    }

    GetPrivateProfileStringW(L"VUMETER", L"VUCOLOR2", L"", value, sizeof(value) / sizeof(value[0]), iniPath);
    if (value[0] != L'\0') {
        g_config.color2 = parse_hex_color(value, g_config.color2);
    }

    GetPrivateProfileStringW(L"VUMETER", L"VUCOLOR3", L"", value, sizeof(value) / sizeof(value[0]), iniPath);
    if (value[0] != L'\0') {
        g_config.color3 = parse_hex_color(value, g_config.color3);
    }

    GetPrivateProfileStringW(L"VUMETER", L"VUTRANSPARENCY", L"", value, sizeof(value) / sizeof(value[0]), iniPath);
    if (value[0] != L'\0') {
        g_config.alpha = parse_alpha_value(value, g_config.alpha);
    }

    g_config.loaded = true;
}

static COLORREF get_box_color(int index, int total)
{
    float t = (float)index / (float)(total - 1);

    if (!g_config.loaded) {
        load_vumeter_config();
    }

    if (t < 0.70f) {
        return g_config.color1;
    } else if (t < 0.90f) {
        return g_config.color2;
    } else {
        return g_config.color3;
    }
}

void vumeter_update(AppState *app, double dt)
{
    float target = 0.0f;

    (void)dt;

    if (app && player_is_loaded(app) && !player_is_paused(app) && !player_is_stopped(app)) {
        target = player_get_recent_output_level(app) * 5.0f;
        if (target > 1.0f) {
            target = 1.0f;
        }
    }

    if (target > g_level) {
        g_level = g_level + ((target - g_level) * 0.35f);
    } else {
        g_level = g_level + ((target - g_level) * 0.08f);
    }

    if (g_level < 0.0f) {
        g_level = 0.0f;
    }
    if (g_level > 1.0f) {
        g_level = 1.0f;
    }
}

void vumeter_draw(AppState *app, HDC hdc)
{
    int lit;
    int total;
    HPEN pen;
    HPEN oldPen;
    HBRUSH oldBrush;
    BLENDFUNCTION blend;

    (void)app;

    if (!hdc) {
        return;
    }

    if (!g_config.loaded) {
        load_vumeter_config();
    }

    total = (int)(sizeof(g_boxes) / sizeof(g_boxes[0]));
    lit = (int)(g_level * (float)total + 0.5f);

    pen = (HPEN)GetStockObject(NULL_PEN);
    oldPen = (HPEN)SelectObject(hdc, pen);
    oldBrush = NULL;

    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = g_config.alpha;
    blend.AlphaFormat = 0;

    for (int i = 0; i < lit && i < total; ++i) {
        const BoxRect *box = &g_boxes[i];
        COLORREF color = get_box_color(i, total);
        HDC memDC;
        HBITMAP bmp;
        HBITMAP oldBmp;
        HBRUSH brush;
        RECT r;

        memDC = CreateCompatibleDC(hdc);
        if (!memDC) {
            continue;
        }

        bmp = CreateCompatibleBitmap(hdc, box->w, box->h);
        if (!bmp) {
            DeleteDC(memDC);
            continue;
        }

        oldBmp = (HBITMAP)SelectObject(memDC, bmp);

        r.left = 0;
        r.top = 0;
        r.right = box->w;
        r.bottom = box->h;

        brush = CreateSolidBrush(color);
        FillRect(memDC, &r, brush);
        DeleteObject(brush);

        AlphaBlend(hdc, box->x, box->y, box->w, box->h, memDC, 0, 0, box->w, box->h, blend);

        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
    }

    if (oldBrush) {
        SelectObject(hdc, oldBrush);
    }
    SelectObject(hdc, oldPen);
}