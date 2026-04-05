#include "quadrascope.h"
#include "player.h"

#include <windows.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct ScopeBox {
    int x;
    int y;
    int w;
    int h;
} ScopeBox;

static const ScopeBox g_boxes[4] = {
    {1382, 958, 113, 89},
    {1517, 958, 113, 89},
    {1652, 958, 113, 89},
    {1787, 958, 113, 89}
};

static bool quadrascope_build_ini_path(wchar_t *path, size_t pathCount)
{
    DWORD len;
    wchar_t *slash;

    if (!path || pathCount == 0) {
        return false;
    }

    len = GetModuleFileNameW(NULL, path, (DWORD)pathCount);
    if (len == 0 || len >= (DWORD)pathCount) {
        return false;
    }

    slash = wcsrchr(path, L'\\');
    if (!slash) {
        slash = wcsrchr(path, L'/');
    }
    if (!slash) {
        return false;
    }

    slash[1] = L'\0';

    if (wcslen(path) + wcslen(L"hyperplayer.ini") + 1 > pathCount) {
        return false;
    }

    wcscat(path, L"hyperplayer.ini");
    return true;
}

static bool quadrascope_parse_hex_color(const wchar_t *text, COLORREF *outColor)
{
    const wchar_t *p;
    wchar_t *endPtr;
    unsigned long value;
    unsigned int r;
    unsigned int g;
    unsigned int b;

    if (!text || !outColor) {
        return false;
    }

    p = text;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') {
        ++p;
    }

    if (*p == L'#') {
        ++p;
    } else if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) {
        p += 2;
    }

    value = wcstoul(p, &endPtr, 16);
    if (endPtr == p || value > 0xFFFFFFUL) {
        return false;
    }

    while (*endPtr == L' ' || *endPtr == L'\t' || *endPtr == L'\r' || *endPtr == L'\n') {
        ++endPtr;
    }

    if (*endPtr != L'\0') {
        return false;
    }

    r = (unsigned int)((value >> 16) & 0xFF);
    g = (unsigned int)((value >> 8) & 0xFF);
    b = (unsigned int)(value & 0xFF);

    *outColor = RGB(r, g, b);
    return true;
}

static COLORREF quadrascope_get_waveform_color(void)
{
    static bool loaded = false;
    static COLORREF color = RGB(0xFF, 0xDD, 0x00);

    if (!loaded) {
        wchar_t iniPath[MAX_PATH];
        wchar_t value[64];

        if (quadrascope_build_ini_path(iniPath, sizeof(iniPath) / sizeof(iniPath[0]))) {
            GetPrivateProfileStringW(
                L"QUADRASCOPE",
                L"QUADRACOLOR",
                L"FFDD00",
                value,
                (DWORD)(sizeof(value) / sizeof(value[0])),
                iniPath
            );

            quadrascope_parse_hex_color(value, &color);
        }

        loaded = true;
    }

    return color;
}

static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool sample_has_loop(int sampleCount, int loopStart, int loopLength)
{
    int loopStart1 = loopStart + 1;
    return (loopLength > 2 && loopStart1 <= sampleCount);
}

static float get_sample_value(const float *values, int sampleCount, double pos, bool useLoopMode, int loopStart, int loopLength)
{
    int loopStart1 = loopStart + 1;

    if (!values || sampleCount < 1) {
        return 0.0f;
    }

    if (useLoopMode && loopLength > 2 && loopStart1 <= sampleCount) {
        int loopEnd = loopStart1 + loopLength - 1;
        int loopSpan;

        if (loopEnd > sampleCount) {
            loopEnd = sampleCount;
        }

        if (pos < 1.0) {
            pos = 1.0;
        }

        if (pos > (double)loopEnd) {
            loopSpan = loopEnd - loopStart1 + 1;
            if (loopSpan < 1) {
                loopSpan = 1;
            }
            pos = loopStart1 + fmod((pos - loopStart1), (double)loopSpan);
        }
    } else {
        if (pos < 1.0) {
            pos = 1.0;
        } else if (pos > (double)sampleCount) {
            pos = (double)sampleCount;
        }
    }

    {
        int index = (int)floor(pos);

        if (index < 1) {
            index = 1;
        } else if (index > sampleCount) {
            index = sampleCount;
        }

        return values[index - 1];
    }
}

static double compute_window_start(int sampleCount, double currentPos, int width, int stride, bool useLoopMode)
{
    double startPos = currentPos - floor((double)(width * stride) * 0.5);
    int visibleSpan = (width - 1) * stride;
    int maxStart;

    if (useLoopMode) {
        return startPos;
    }

    maxStart = sampleCount - visibleSpan;
    if (maxStart < 1) {
        maxStart = 1;
    }

    if (startPos < 1.0) {
        startPos = 1.0;
    } else if (startPos > (double)maxStart) {
        startPos = (double)maxStart;
    }

    return startPos;
}

static void draw_scope_from_state(
    HDC hdc,
    const float *values,
    int sampleCount,
    int loopStart,
    int loopLength,
    const QuadrascopeState *state,
    const ScopeBox *box
)
{
    bool useLoopMode;
    int stride;
    double startPos;
    float ampScale;
    float amp;
    double midY;
    POINT points[113];
    float peak = 0.0f;
    HPEN pen;
    HPEN oldPen;
    int savedDc;

    if (!hdc || !values || sampleCount < 2 || !state || !box) {
        return;
    }

    if (state->sampleIndex <= 0) {
        return;
    }

    if ((!state->active) && state->vu <= 0.0f && state->scopeHold <= 0.0) {
        return;
    }

    useLoopMode = sample_has_loop(sampleCount, loopStart, loopLength);
    stride = state->scopeStride;
    if (stride < 1) {
        stride = 1;
    }

    startPos = compute_window_start(sampleCount, state->samplePos, box->w, stride, useLoopMode);
    ampScale = clampf_local((state->sampleVolume * 0.65f) + (state->vu * 0.85f), 0.12f, 1.0f);
    amp = (float)(box->h * 0.44f) * ampScale;
    midY = box->y + (box->h * 0.5);

    for (int i = 0; i < box->w; ++i) {
        double pos = startPos + (double)(i * stride);
        float v = get_sample_value(values, sampleCount, pos, useLoopMode, loopStart, loopLength);
        float a = (v < 0.0f) ? -v : v;

        if (a > peak) {
            peak = a;
        }

        points[i].x = box->x + i;
        points[i].y = (LONG)lround(midY - (v * amp));
    }

    if (peak <= 0.0001f) {
        return;
    }

    savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, box->x, box->y, box->x + box->w, box->y + box->h);

    pen = CreatePen(PS_SOLID, 1, quadrascope_get_waveform_color());
    oldPen = (HPEN)SelectObject(hdc, pen);

    Polyline(hdc, points, box->w);

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    RestoreDC(hdc, savedDc);
}

void quadrascope_draw(AppState *app, HDC hdc)
{
    if (!app || !hdc || !player_is_loaded(app)) {
        return;
    }

    for (int channel = 1; channel <= 4; ++channel) {
        QuadrascopeState state;
        const float *values = NULL;
        int sampleCount = 0;
        int loopStart = 0;
        int loopLength = 0;

        if (!player_get_quadrascope_state(app, channel, &state)) {
            continue;
        }

        if (!player_get_sample_values(app, state.sampleIndex, &values, &sampleCount, &loopStart, &loopLength)) {
            continue;
        }

        draw_scope_from_state(hdc, values, sampleCount, loopStart, loopLength, &state, &g_boxes[channel - 1]);
    }
}