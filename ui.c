#include "ui.h"
#include "directory_listing_win32.h"
#include "player.h"
#include "pattern_view.h"
#include "sample_list.h"
#include "sample_list_usage_trigger.h"
#include "sample_display.h"
#include "spectrumanalyzer.h"
#include "vumeter.h"
#include "quadrascope.h"
#include "tunnelvisualizer.h"
#include "resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const COLORREF COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);
static const COLORREF COLOR_INFO = RGB(0xBB, 0xBB, 0xBB);
static const COLORREF COLOR_SHADOW = RGB(0x59, 0x59, 0x59);

static const wchar_t *FONT_FACE = L"protracker-fix";

static void ui_delete_font(HFONT *font)
{
    if (font && *font) {
        DeleteObject(*font);
        *font = NULL;
    }
}

static HFONT ui_make_font(const wchar_t *faceName, int pixelHeight, int weight)
{
    HFONT font = CreateFontW(
        -pixelHeight,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY,
        FF_DONTCARE,
        faceName
    );

    if (!font) {
        font = CreateFontW(
            -pixelHeight,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            FF_DONTCARE,
            L"Consolas"
        );
    }

    return font;
}

static bool ui_load_png_wic_from_memory(const void *data, DWORD dataSize, ImageRGBA *outImage)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    IWICStream *stream = NULL;
    HRESULT hr;
    bool ok = false;
    UINT width = 0;
    UINT height = 0;
    size_t stride = 0;
    size_t pixelBytes = 0;
    unsigned char *pixels = NULL;

    if (!data || dataSize == 0 || !outImage) {
        return false;
    }

    hr = CoCreateInstance(
        &CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory,
        (void **)&factory
    );
    if (FAILED(hr)) {
        goto done;
    }

    hr = IWICImagingFactory_CreateStream(factory, &stream);
    if (FAILED(hr)) {
        goto done;
    }

    hr = IWICStream_InitializeFromMemory(stream, (BYTE *)data, dataSize);
    if (FAILED(hr)) {
        goto done;
    }

    hr = IWICImagingFactory_CreateDecoderFromStream(
        factory,
        (IStream *)stream,
        NULL,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );
    if (FAILED(hr)) {
        goto done;
    }

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) {
        goto done;
    }

    hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (FAILED(hr)) {
        goto done;
    }

    hr = IWICFormatConverter_Initialize(
        converter,
        (IWICBitmapSource *)frame,
        &GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        NULL,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) {
        goto done;
    }

    hr = IWICBitmapSource_GetSize((IWICBitmapSource *)converter, &width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        goto done;
    }

    stride = (size_t)width * 4;
    pixelBytes = stride * (size_t)height;
    pixels = (unsigned char *)malloc(pixelBytes);
    if (!pixels) {
        goto done;
    }

    hr = IWICBitmapSource_CopyPixels(
        (IWICBitmapSource *)converter,
        NULL,
        (UINT)stride,
        (UINT)pixelBytes,
        pixels
    );
    if (FAILED(hr)) {
        free(pixels);
        pixels = NULL;
        goto done;
    }

    outImage->width = width;
    outImage->height = height;
    outImage->pixels = pixels;
    ok = true;

done:
    if (stream) {
        IWICStream_Release(stream);
    }
    if (converter) {
        IWICFormatConverter_Release(converter);
    }
    if (frame) {
        IWICBitmapFrameDecode_Release(frame);
    }
    if (decoder) {
        IWICBitmapDecoder_Release(decoder);
    }
    if (factory) {
        IWICImagingFactory_Release(factory);
    }

    return ok;
}

static void ui_free_image(ImageRGBA *image)
{
    if (!image) {
        return;
    }

    if (image->pixels) {
        free(image->pixels);
        image->pixels = NULL;
    }

    image->width = 0;
    image->height = 0;
}

static void ui_draw_image(HDC hdc, const RECT *clientRect, const ImageRGBA *image)
{
    BITMAPINFO bmi;

    if (!hdc || !clientRect || !image || !image->pixels || image->width == 0 || image->height == 0) {
        return;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)image->width;
    bmi.bmiHeader.biHeight = -(LONG)image->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(
        hdc,
        0,
        0,
        clientRect->right - clientRect->left,
        clientRect->bottom - clientRect->top,
        0,
        0,
        (int)image->width,
        (int)image->height,
        image->pixels,
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

void ui_draw_shadowed_text(
    HDC hdc,
    HFONT font,
    const wchar_t *text,
    int x,
    int y,
    COLORREF color,
    COLORREF shadowColor,
    int shadowDx,
    int shadowDy,
    const RECT *clipRect,
    UINT format
)
{
    HFONT oldFont;
    int savedDc;

    if (!hdc || !font || !text) {
        return;
    }

    savedDc = SaveDC(hdc);
    oldFont = (HFONT)SelectObject(hdc, font);

    SetBkMode(hdc, TRANSPARENT);

    if (clipRect) {
        IntersectClipRect(hdc, clipRect->left, clipRect->top, clipRect->right, clipRect->bottom);
    }

    SetTextColor(hdc, shadowColor);
    if (clipRect) {
        RECT shadowRect = *clipRect;
        shadowRect.left = x + shadowDx;
        shadowRect.top = y + shadowDy;
        DrawTextW(hdc, text, -1, &shadowRect, format);
    } else {
        TextOutW(hdc, x + shadowDx, y + shadowDy, text, (int)wcslen(text));
    }

    SetTextColor(hdc, color);
    if (clipRect) {
        RECT textRect = *clipRect;
        textRect.left = x;
        textRect.top = y;
        DrawTextW(hdc, text, -1, &textRect, format);
    } else {
        TextOutW(hdc, x, y, text, (int)wcslen(text));
    }

    SelectObject(hdc, oldFont);
    RestoreDC(hdc, savedDc);
}

static void ui_draw_fallback_shell(AppState *app, HDC hdc, const RECT *clientRect)
{
    HBRUSH bg = CreateSolidBrush(RGB(0xA0, 0xA0, 0xA0));
    HBRUSH dark = CreateSolidBrush(RGB(0x7A, 0x7A, 0x7A));
    RECT r;

    FillRect(hdc, clientRect, bg);

    r.left = 1363;
    r.top = 134;
    r.right = 1918;
    r.bottom = 459;
    FillRect(hdc, &r, dark);

    r.left = 1363;
    r.top = 467;
    r.right = 1918;
    r.bottom = 618;
    FillRect(hdc, &r, dark);

    r.left = 1363;
    r.top = 658;
    r.right = 1918;
    r.bottom = 768;
    FillRect(hdc, &r, dark);

    r.left = 1363;
    r.top = 958;
    r.right = 1918;
    r.bottom = 1047;
    FillRect(hdc, &r, dark);

    ui_draw_shadowed_text(hdc, app->fonts.title, L"HYPERPLAYER", 1375, 12, COLOR_WHITE, COLOR_SHADOW, 3, 3, NULL, 0);
    ui_draw_shadowed_text(hdc, app->fonts.dir, L"Stage 3 fallback shell: embedded background resource not loaded", 24, 24, COLOR_INFO, COLOR_SHADOW, 2, 2, NULL, 0);

    DeleteObject(dark);
    DeleteObject(bg);
}

bool ui_load_assets(AppState *app)
{
    const void *fontData = NULL;
    DWORD fontSize = 0;
    DWORD fontCount = 0;
    const void *backgroundData = NULL;
    DWORD backgroundSize = 0;

    if (!app) {
        return false;
    }

    app->privateFontHandle = NULL;
    app->privateFontLoaded = false;
    app->backgroundLoaded = false;

    if (app_load_resource_bytes(IDR_PROTRACKER_TTF, &fontData, &fontSize)) {
        app->privateFontHandle = AddFontMemResourceEx((PVOID)fontData, fontSize, 0, &fontCount);
        app->privateFontLoaded = (app->privateFontHandle != NULL && fontCount > 0);
    }

    app->fonts.pattern = ui_make_font(FONT_FACE, 18, FW_NORMAL);
    app->fonts.sampleList = ui_make_font(FONT_FACE, 17, FW_NORMAL);
    app->fonts.info = ui_make_font(FONT_FACE, 25, FW_NORMAL);
    app->fonts.info2 = ui_make_font(FONT_FACE, 24, FW_NORMAL);
    app->fonts.dir = ui_make_font(FONT_FACE, 16, FW_NORMAL);
    app->fonts.driveButtons = ui_make_font(FONT_FACE, 16, FW_NORMAL);
    app->fonts.waveform = ui_make_font(FONT_FACE, 32, FW_NORMAL);
    app->fonts.title = ui_make_font(FONT_FACE, 42, FW_BOLD);

    if (app_load_resource_bytes(IDR_BACKGROUND_PNG, &backgroundData, &backgroundSize)) {
        app->backgroundLoaded = ui_load_png_wic_from_memory(backgroundData, backgroundSize, &app->background);
    }

    return true;
}

void ui_release_assets(AppState *app)
{
    if (!app) {
        return;
    }

    ui_delete_font(&app->fonts.pattern);
    ui_delete_font(&app->fonts.sampleList);
    ui_delete_font(&app->fonts.info);
    ui_delete_font(&app->fonts.info2);
    ui_delete_font(&app->fonts.dir);
    ui_delete_font(&app->fonts.driveButtons);
    ui_delete_font(&app->fonts.waveform);
    ui_delete_font(&app->fonts.title);

    if (app->privateFontHandle) {
        RemoveFontMemResourceEx(app->privateFontHandle);
        app->privateFontHandle = NULL;
        app->privateFontLoaded = false;
    }

    ui_free_image(&app->background);
    app->backgroundLoaded = false;
}

void ui_draw(AppState *app, HDC hdc, const RECT *clientRect)
{
    RECT statusClip;

    if (!app || !hdc || !clientRect) {
        return;
    }

    if (app->backgroundLoaded) {
        ui_draw_image(hdc, clientRect, &app->background);
    } else {
        ui_draw_fallback_shell(app, hdc, clientRect);
    }

    ui_draw_shadowed_text(
        hdc,
        app->fonts.dir,
        L"v1.0",
        1368,
        68,
        COLOR_INFO,
        COLOR_SHADOW,
        2,
        2,
        NULL,
        0
    );

    pattern_view_draw(app, hdc);
    sample_list_usage_trigger_draw(app, hdc);
    sample_list_draw(app, hdc);
    player_draw_songinfo(app, hdc);
    if (app->showFileBrowser) {
        directory_listing_draw(app, hdc);
    } else {
        tunnelvisualizer_draw(app, hdc);
    }
    sample_display_draw(app, hdc);
    spectrumanalyzer_draw(app, hdc);
    vumeter_draw(app, hdc);
    quadrascope_draw(app, hdc);

    statusClip.left = 1362;
    statusClip.top = 1053;
    statusClip.right = 1362 + 556;
    statusClip.bottom = 1053 + 25;

    ui_draw_shadowed_text(
        hdc,
        app->fonts.dir,
        app->statusText,
        1366,
        1043,
        COLOR_INFO,
        COLOR_SHADOW,
        2,
        2,
        &statusClip,
        DT_LEFT | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
    );
}