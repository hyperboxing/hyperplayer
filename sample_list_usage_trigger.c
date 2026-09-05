#include "sample_list_usage_trigger.h"
#include "player.h"

#include <windows.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

typedef struct HighlightBox {
    int x;
    int y;
    int w;
    int h;
} HighlightBox;

typedef struct SampleListConfig {
    float bgTransparency;
    float bgFade;
    ULONGLONG lastLoadTick;
    bool loaded;
} SampleListConfig;

static const HighlightBox g_boxes[31] = {
    {800, 245, 335, 23}, {800, 272, 335, 23}, {800, 299, 335, 23}, {800, 326, 335, 23},
    {800, 353, 335, 23}, {800, 380, 335, 23}, {800, 407, 335, 23}, {800, 434, 335, 23},
    {800, 461, 335, 23}, {800, 488, 335, 23}, {800, 515, 335, 23}, {800, 542, 335, 23},
    {800, 569, 335, 23}, {800, 596, 335, 23}, {800, 623, 335, 23}, {800, 650, 335, 23},
    {800, 677, 335, 23}, {800, 704, 335, 23}, {800, 731, 335, 23}, {800, 758, 335, 23},
    {800, 785, 335, 23}, {800, 812, 335, 23}, {800, 839, 335, 23}, {800, 866, 335, 23},
    {800, 893, 335, 23}, {800, 920, 335, 23}, {800, 947, 335, 23}, {800, 974, 335, 23},
    {800, 1001, 335, 23}, {800, 1028, 335, 23}, {800, 1055, 335, 23}
};

static wchar_t g_lastFile[MAX_PATH];
static SampleListConfig g_config = { 0 };
static unsigned int g_lastTriggerSerial[4];

static void reset_highlights(AppState *app)
{
    if (!app) {
        return;
    }

    for (int i = 0; i < 31; ++i) {
        app->sampleHighlightAlpha[i] = 0.0f;
    }
    memset(g_lastTriggerSerial, 0, sizeof(g_lastTriggerSerial));
}

static void copy_wstr_local(wchar_t *dst, size_t dstCount, const wchar_t *src)
{
    if (!dst || dstCount == 0) {
        return;
    }

    if (!src) {
        dst[0] = L'\0';
        return;
    }

    wcsncpy(dst, src, dstCount - 1);
    dst[dstCount - 1] = L'\0';
}

static void build_ini_path(wchar_t *path, size_t pathCount)
{
    DWORD len;
    const wchar_t *iniName = L"hyperplayer.ini";
    size_t iniNameLen;

    if (!path || pathCount == 0) {
        return;
    }

    len = GetModuleFileNameW(NULL, path, (DWORD)pathCount);
    if (len == 0 || len >= pathCount) {
        copy_wstr_local(path, pathCount, iniName);
        return;
    }

    while (len > 0) {
        wchar_t ch = path[len - 1];
        if (ch == L'\\' || ch == L'/') {
            break;
        }
        --len;
    }

    if (len == 0) {
        copy_wstr_local(path, pathCount, iniName);
        return;
    }

    path[len] = L'\0';

    iniNameLen = wcslen(iniName);
    if ((size_t)len + iniNameLen + 1 > pathCount) {
        copy_wstr_local(path, pathCount, iniName);
        return;
    }

    wcsncpy(path + len, iniName, pathCount - len - 1);
    path[pathCount - 1] = L'\0';
}

static void sample_list_usage_trigger_load_config(void)
{
    wchar_t iniPath[MAX_PATH];
    wchar_t value[64];
    wchar_t *endPtr;
    unsigned int transparencyPercent;
    double fadeValue;
    ULONGLONG now = GetTickCount64();

    if (g_config.loaded && (now - g_config.lastLoadTick) < 250) {
        return;
    }

    g_config.bgTransparency = 0.54f;
    g_config.bgFade = 0.8333333f;

    build_ini_path(iniPath, sizeof(iniPath) / sizeof(iniPath[0]));

    transparencyPercent = GetPrivateProfileIntW(
        L"SAMPLELIST",
        L"BGTRANSPARENCY",
        54,
        iniPath
    );

    if (transparencyPercent > 100) {
        transparencyPercent = 100;
    }

    g_config.bgTransparency = (float)transparencyPercent / 100.0f;

    GetPrivateProfileStringW(
        L"SAMPLELIST",
        L"BGFADE",
        L"0.8333333",
        value,
        (DWORD)(sizeof(value) / sizeof(value[0])),
        iniPath
    );

    endPtr = NULL;
    fadeValue = wcstod(value, &endPtr);
    if (endPtr != value) {
        if (fadeValue < 0.0) {
            fadeValue = 0.0;
        }
        g_config.bgFade = (float)fadeValue;
    }

    g_config.lastLoadTick = now;
    g_config.loaded = true;
}

void sample_list_usage_trigger_update(AppState *app, double dt)
{
    bool activeSamples[31] = { false };

    if (!app) {
        return;
    }

    sample_list_usage_trigger_load_config();

    if (!player_is_loaded(app)) {
        if (g_lastFile[0] != L'\0') {
            g_lastFile[0] = L'\0';
            reset_highlights(app);
        }
        return;
    }

    {
        const wchar_t *currentFile = player_get_current_file_path(app);

        if (wcscmp(g_lastFile, currentFile) != 0) {
            copy_wstr_local(g_lastFile, sizeof(g_lastFile) / sizeof(g_lastFile[0]), currentFile);
            reset_highlights(app);
        }
    }

    for (int channel = 1; channel <= 4; ++channel) {
        QuadrascopeState state;

        if (player_get_quadrascope_state(app, channel, &state)) {
            if (state.sampleIndex > 0 && state.sampleIndex <= 31) {
                if (state.triggerSerial != 0 && state.triggerSerial != g_lastTriggerSerial[channel - 1]) {
                    activeSamples[state.sampleIndex - 1] = true;
                }
                if (state.vu > 0.01f || state.active || state.scopeHold > 0.0) {
                    activeSamples[state.sampleIndex - 1] = true;
                }
            }
            g_lastTriggerSerial[channel - 1] = state.triggerSerial;
        }
    }

    for (int i = 0; i < 31; ++i) {
        if (activeSamples[i]) {
            app->sampleHighlightAlpha[i] = g_config.bgTransparency;
        } else {
            float nextAlpha = app->sampleHighlightAlpha[i] - (float)(g_config.bgFade * dt);
            if (nextAlpha < 0.0f) {
                nextAlpha = 0.0f;
            }
            app->sampleHighlightAlpha[i] = nextAlpha;
        }
    }
}

void sample_list_usage_trigger_draw(AppState *app, HDC hdc)
{
    HDC memDC;
    HBITMAP bmp;
    HBITMAP oldBmp;
    RECT r;
    BLENDFUNCTION blend;
    unsigned int *pixel;

    if (!app || !hdc) {
        return;
    }

    memDC = CreateCompatibleDC(hdc);
    if (!memDC) {
        return;
    }

    {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 1;
        bi.bmiHeader.biHeight = -1;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&pixel, NULL, 0);
    }

    if (!bmp) {
        DeleteDC(memDC);
        return;
    }

    oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    if (pixel) {
        *pixel = 0x00FFFFFFu;
    } else {
        r.left = 0;
        r.top = 0;
        r.right = 1;
        r.bottom = 1;
        FillRect(memDC, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }

    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.AlphaFormat = 0;

    for (int i = 0; i < 31; ++i) {
        float alpha = app->sampleHighlightAlpha[i];

        if (alpha > 0.0f) {
            int alphaByte = (int)(alpha * 255.0f + 0.5f);
            const HighlightBox *box = &g_boxes[i];

            if (alphaByte < 0) {
                alphaByte = 0;
            } else if (alphaByte > 255) {
                alphaByte = 255;
            }

            blend.SourceConstantAlpha = (BYTE)alphaByte;

            AlphaBlend(
                hdc,
                box->x,
                box->y,
                box->w,
                box->h,
                memDC,
                0,
                0,
                1,
                1,
                blend
            );
        }
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
}
