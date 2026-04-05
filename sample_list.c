#include "sample_list.h"
#include "player.h"
#include "ui.h"

#include <stdio.h>
#include <wchar.h>

static const int NAME_X = 809;
static const int NUMBER_X = 758;
static const int VOLUME_X = 1144;
static const int SIZE_X = 1269;
static const int START_Y = 242;
static const int ROW_STEP = 27;
static const int ROW_HEIGHT = 27;

static const COLORREF COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);
static const COLORREF COLOR_SOFT_WHITE = RGB(0xD5, 0xD5, 0xD5);
static const COLORREF COLOR_SHADOW = RGB(0x59, 0x59, 0x59);

void sample_list_draw(AppState *app, HDC hdc)
{
    if (!app || !hdc) {
        return;
    }

    for (int i = 1; i <= 31; ++i) {
        wchar_t indexText[8];
        wchar_t nameText[64];
        wchar_t volText[16];
        wchar_t sizeText[32];
        RECT nameRect;
        RECT volRect;
        RECT sizeRect;
        int rowY;
        int volume = 0;
        int size = 0;

        player_get_sample_info(app, i, nameText, sizeof(nameText) / sizeof(nameText[0]), &volume, &size);

        swprintf(indexText, sizeof(indexText) / sizeof(indexText[0]), L"%02X", i);
        swprintf(volText, sizeof(volText) / sizeof(volText[0]), L"%02d", volume);
        swprintf(sizeText, sizeof(sizeText) / sizeof(sizeText[0]), L"%d", size);

        rowY = START_Y + (i - 1) * ROW_STEP;

        nameRect.left = NAME_X;
        nameRect.top = rowY;
        nameRect.right = 1138;
        nameRect.bottom = rowY + ROW_HEIGHT;

        volRect.left = VOLUME_X;
        volRect.top = rowY;
        volRect.right = VOLUME_X + 56;
        volRect.bottom = rowY + ROW_HEIGHT;

        sizeRect.left = SIZE_X;
        sizeRect.top = rowY;
        sizeRect.right = SIZE_X + 80;
        sizeRect.bottom = rowY + ROW_HEIGHT;

        ui_draw_shadowed_text(hdc, app->fonts.sampleList, indexText, NUMBER_X, rowY, COLOR_SOFT_WHITE, COLOR_SHADOW, 1, 1, NULL, 0);
        ui_draw_shadowed_text(hdc, app->fonts.sampleList, nameText, NAME_X, rowY, COLOR_WHITE, COLOR_SHADOW, 1, 1, &nameRect, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE | DT_END_ELLIPSIS);
        ui_draw_shadowed_text(hdc, app->fonts.sampleList, volText, VOLUME_X, rowY, COLOR_SOFT_WHITE, COLOR_SHADOW, 1, 1, &volRect, DT_RIGHT | DT_NOPREFIX | DT_SINGLELINE);
        ui_draw_shadowed_text(hdc, app->fonts.sampleList, sizeText, SIZE_X, rowY, COLOR_SOFT_WHITE, COLOR_SHADOW, 1, 1, &sizeRect, DT_RIGHT | DT_NOPREFIX | DT_SINGLELINE);
    }
}

bool sample_list_mouse_down(AppState *app, int x, int y)
{
    int listX1;
    int listX2;
    int listY1;
    int listY2;
    int rowIndex;
    int volume = 0;
    int size = 0;
    wchar_t sampleName[64];
    wchar_t hexText[8];

    if (!app || !player_is_loaded(app)) {
        return false;
    }

    listX1 = 758;
    listX2 = 1349;
    listY1 = START_Y;
    listY2 = START_Y + ((31 - 1) * ROW_STEP) + ROW_HEIGHT;

    if (x < listX1 || x > listX2 || y < listY1 || y > listY2) {
        return false;
    }

    rowIndex = ((y - START_Y) / ROW_STEP) + 1;
    if (rowIndex < 1 || rowIndex > 31) {
        return false;
    }

    app->selectedSampleIndex = rowIndex;

    player_get_sample_info(app, rowIndex, sampleName, sizeof(sampleName) / sizeof(sampleName[0]), &volume, &size);
    swprintf(hexText, sizeof(hexText) / sizeof(hexText[0]), L"%02X", rowIndex);

    if (size <= 1) {
        app_set_status(app, L"Displaying sample # %ls (empty)", hexText);
    } else {
        app_set_status(app, L"Displaying sample # %ls", hexText);
    }

    return true;
}