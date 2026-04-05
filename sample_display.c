#include "sample_display.h"
#include "player.h"
#include "ui.h"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <wchar.h>

static const int AREA_X = 1363;
static const int AREA_Y = 467;
static const int AREA_W = 555;
static const int AREA_H = 151;

static const COLORREF DEFAULT_COLOR_WAVEFORM = RGB(0xFF, 0xDD, 0x00);
static const COLORREF COLOR_INFO = RGB(0xBB, 0xBB, 0xBB);
static const COLORREF COLOR_SHADOW = RGB(0x59, 0x59, 0x59);

static COLORREF g_colorWaveform = RGB(0xFF, 0xDD, 0x00);
static bool g_configLoaded = false;

static void get_ini_path(wchar_t *path, size_t pathCount)
{
    DWORD len;
    wchar_t *slash;

    if (!path || pathCount == 0) {
        return;
    }

    path[0] = L'\0';

    len = GetModuleFileNameW(NULL, path, (DWORD)pathCount);
    if (len == 0 || len >= pathCount) {
        swprintf(path, pathCount, L"hyperplayer.ini");
        return;
    }

    slash = wcsrchr(path, L'\\');
    if (slash) {
        slash[1] = L'\0';
        wcsncat(path, L"hyperplayer.ini", pathCount - wcslen(path) - 1);
    } else {
        swprintf(path, pathCount, L"hyperplayer.ini");
    }
}

static COLORREF parse_ini_hex_color(const wchar_t *text, COLORREF fallback)
{
    unsigned int value = 0;

    if (!text || !*text) {
        return fallback;
    }

    while (*text == L' ' || *text == L'\t') {
        text++;
    }

    if (swscanf(text, L"%x", &value) != 1) {
        return fallback;
    }

    return RGB(
        (value >> 16) & 0xFF,
        (value >> 8) & 0xFF,
        value & 0xFF
    );
}

static void load_sample_display_config(void)
{
    wchar_t iniPath[MAX_PATH];
    wchar_t colorText[64];

    get_ini_path(iniPath, sizeof(iniPath) / sizeof(iniPath[0]));

    GetPrivateProfileStringW(
        L"SAMPLEVIEW",
        L"SAMPLECOLOR",
        L"FFDD00",
        colorText,
        (DWORD)(sizeof(colorText) / sizeof(colorText[0])),
        iniPath
    );

    g_colorWaveform = parse_ini_hex_color(colorText, DEFAULT_COLOR_WAVEFORM);
    g_configLoaded = true;
}

static int get_displayed_sample_index(AppState *app)
{
    int nonEmptyCount;

    if (!app) {
        return 0;
    }

    if (app->selectedSampleIndex > 0) {
        return app->selectedSampleIndex;
    }

    nonEmptyCount = player_get_nonempty_sample_count(app);
    if (nonEmptyCount <= 0) {
        return 0;
    }

    if (app->sampleDisplayCurrentSlot < 0 || app->sampleDisplayCurrentSlot >= nonEmptyCount) {
        app->sampleDisplayCurrentSlot = 0;
    }

    return player_get_nonempty_sample_index(app, app->sampleDisplayCurrentSlot);
}

void sample_display_update(AppState *app, double dt)
{
    int nonEmptyCount;

    if (!app) {
        return;
    }

    if (!g_configLoaded) {
        load_sample_display_config();
    }

    if (app->selectedSampleIndex > 0) {
        return;
    }

    nonEmptyCount = player_get_nonempty_sample_count(app);
    if (nonEmptyCount <= 0) {
        return;
    }

    app->sampleDisplayTimer += dt;
    if (app->sampleDisplayTimer >= 2.0) {
        app->sampleDisplayTimer -= 2.0;
        app->sampleDisplayCurrentSlot++;

        if (app->sampleDisplayCurrentSlot >= nonEmptyCount) {
            app->sampleDisplayCurrentSlot = 0;
        }
    }
}

void sample_display_draw(AppState *app, HDC hdc)
{
    const SamplePreviewPoint *preview = NULL;
    int previewCount = 0;
    int sampleIndex;
    HFONT oldFont;
    HPEN pen;
    HPEN oldPen;
    int savedDc;

    if (!app || !hdc) {
        return;
    }

    if (!g_configLoaded) {
        load_sample_display_config();
    }

    sampleIndex = get_displayed_sample_index(app);
    if (sampleIndex <= 0) {
        return;
    }

    if (!player_get_sample_preview(app, sampleIndex, &preview, &previewCount) || !preview || previewCount <= 0) {
        return;
    }

    savedDc = SaveDC(hdc);
    pen = CreatePen(PS_SOLID, 1, g_colorWaveform);
    oldPen = (HPEN)SelectObject(hdc, pen);

    for (int i = 0; i < previewCount && i < AREA_W; ++i) {
        int px = AREA_X + i;
        float minv = preview[i].minValue;
        float maxv = preview[i].maxValue;
        int midY = AREA_Y + (AREA_H / 2);
        int y1 = (int)(midY - (maxv * (AREA_H * 0.45f)));
        int y2 = (int)(midY - (minv * (AREA_H * 0.45f)));

        MoveToEx(hdc, px, y1, NULL);
        LineTo(hdc, px, y2);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    oldFont = (HFONT)SelectObject(hdc, app->fonts.waveform);
    SetBkMode(hdc, TRANSPARENT);

    {
        wchar_t numText[8];
        RECT numRect;

        swprintf(numText, sizeof(numText) / sizeof(numText[0]), L"%02X", sampleIndex);

        numRect.left = 1831;
        numRect.top = 454;
        numRect.right = 1831 + 80;
        numRect.bottom = 454 + 40;

        ui_draw_shadowed_text(
            hdc,
            app->fonts.waveform,
            numText,
            1831,
            454,
            COLOR_INFO,
            COLOR_SHADOW,
            2,
            2,
            &numRect,
            DT_RIGHT | DT_NOPREFIX | DT_SINGLELINE
        );
    }

    SelectObject(hdc, oldFont);
    RestoreDC(hdc, savedDc);
}