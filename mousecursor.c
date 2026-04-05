#include "mousecursor.h"
#include "resource.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static HCURSOR g_mouseCursor = NULL;

static bool load_png_bgra_from_memory(const void *data, DWORD dataSize, unsigned char **outPixels, UINT *outWidth, UINT *outHeight)
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

    if (!data || dataSize == 0 || !outPixels || !outWidth || !outHeight) {
        return false;
    }

    *outPixels = NULL;
    *outWidth = 0;
    *outHeight = 0;

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
        &GUID_WICPixelFormat32bppBGRA,
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

    *outPixels = pixels;
    *outWidth = width;
    *outHeight = height;
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

static HCURSOR create_cursor_from_bgra(const unsigned char *pixels, UINT width, UINT height, DWORD hotspotX, DWORD hotspotY)
{
    BITMAPINFO bmi;
    HDC screenDC;
    void *dibBits = NULL;
    HBITMAP colorBitmap = NULL;
    HBITMAP maskBitmap = NULL;
    ICONINFO ii;
    HCURSOR cursor = NULL;
    size_t colorBytes;
    size_t maskStride;
    size_t maskBytes;
    unsigned char *maskBits = NULL;

    if (!pixels || width == 0 || height == 0) {
        return NULL;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)width;
    bmi.bmiHeader.biHeight = -(LONG)height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screenDC = GetDC(NULL);
    if (!screenDC) {
        return NULL;
    }

    colorBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &dibBits, NULL, 0);
    ReleaseDC(NULL, screenDC);

    if (!colorBitmap || !dibBits) {
        if (colorBitmap) {
            DeleteObject(colorBitmap);
        }
        return NULL;
    }

    colorBytes = (size_t)width * (size_t)height * 4;
    memcpy(dibBits, pixels, colorBytes);

    maskStride = (((size_t)width + 15u) / 16u) * 2u;
    maskBytes = maskStride * (size_t)height;
    maskBits = (unsigned char *)calloc(maskBytes, 1);
    if (!maskBits) {
        DeleteObject(colorBitmap);
        return NULL;
    }

    maskBitmap = CreateBitmap((int)width, (int)height, 1, 1, maskBits);
    free(maskBits);
    maskBits = NULL;

    if (!maskBitmap) {
        DeleteObject(colorBitmap);
        return NULL;
    }

    memset(&ii, 0, sizeof(ii));
    ii.fIcon = FALSE;
    ii.xHotspot = hotspotX;
    ii.yHotspot = hotspotY;
    ii.hbmMask = maskBitmap;
    ii.hbmColor = colorBitmap;

    cursor = CreateIconIndirect(&ii);

    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);

    return cursor;
}

bool mousecursor_load(AppState *app)
{
    const void *cursorData = NULL;
    DWORD cursorSize = 0;
    unsigned char *pixels = NULL;
    UINT width = 0;
    UINT height = 0;
    HCURSOR newCursor = NULL;

    if (!app) {
        return false;
    }

    if (!app_load_resource_bytes(IDR_MOUSECURSOR_PNG, &cursorData, &cursorSize)) {
        return false;
    }

    if (!load_png_bgra_from_memory(cursorData, cursorSize, &pixels, &width, &height)) {
        return false;
    }

    newCursor = create_cursor_from_bgra(pixels, width, height, 0, 0);
    free(pixels);

    if (!newCursor) {
        return false;
    }

    if (g_mouseCursor) {
        DestroyCursor(g_mouseCursor);
        g_mouseCursor = NULL;
    }

    g_mouseCursor = newCursor;
    return true;
}

void mousecursor_unload(void)
{
    if (g_mouseCursor) {
        DestroyCursor(g_mouseCursor);
        g_mouseCursor = NULL;
    }
}

HCURSOR mousecursor_get(void)
{
    return g_mouseCursor;
}

void mousecursor_apply(HWND hwnd)
{
    if (!g_mouseCursor) {
        return;
    }

    SetCursor(g_mouseCursor);

    if (hwnd) {
        SetClassLongPtrW(hwnd, GCLP_HCURSOR, (LONG_PTR)g_mouseCursor);
    }
}
