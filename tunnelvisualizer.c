#include "tunnelvisualizer.h"
#include "player.h"

#include <windows.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#define TV_BOX_X 1363
#define TV_BOX_Y 132
#define TV_BOX_W 555
#define TV_BOX_H 329

#define TV_BROWSER_BUTTON_X1 1363
#define TV_BROWSER_BUTTON_X2 1576
#define TV_BROWSER_BUTTON_Y1 102
#define TV_BROWSER_BUTTON_Y2 124

#define FFT_SIZE 1024
#define BAND_COUNT 48
#define MIN_HZ 50.0
#define MAX_HZ 9000.0

typedef struct VisualizerConfig {
    float rotationSpeed;
    float barDeclineSpeed;
    float barLengthScale;
    float colorCycleSpeed;
    float globalGlowStrength;
    float highHzResponse;
    float highHzLengthBonus;
    int starfieldCount;
    float starfieldSpeed;
    float starfieldBrightness;
    int bassOnlyBandCount;
    float bassOnlyDominance;
    float bassOnlyGateScale;
    float bassOnlyAbsLevel;
    int agcIgnoreLowBands;
    float highPresenceFloor;
    COLORREF backgroundColor;
    COLORREF starColor;
} VisualizerConfig;

static const VisualizerConfig g_visualizerDefaults = {
    2.00f,
    3.60f,
    1.50f,
    2.00f,
    0.90f,
    2.00f,
    0.63f,
    7000,
    0.36f,
    1.00f,
    20,
    1.10f,
    0.85f,
    0.018f,
    20,
    0.90f,
    RGB(6, 6, 10),
    RGB(255, 255, 255)
};

static VisualizerConfig g_visualizer;
static bool g_visualizerLoaded = false;
static wchar_t g_visualizerIniPath[MAX_PATH];
static bool g_visualizerIniPathReady = false;

typedef struct RadialState {
    float mono[FFT_SIZE];
    float bands[BAND_COUNT];
    float energy;
    float bass;
    float treble;
    float beatFlash;
    float agcGain;
    float bassGatePrev;
    double time;
    double rotation;
    bool initialized;
} RadialState;

typedef struct BloomSurface {
    HDC dc;
    HBITMAP bitmap;
    HBITMAP oldBitmap;
    unsigned int *pixels;
    unsigned int *src;
    unsigned int *tmp;
    int width;
    int height;
} BloomSurface;

typedef struct StarfieldState {
    float *x;
    float *y;
    float *z;
    float *speed;
    float *brightness;
    float *size;
    int count;
    int width;
    int height;
    float fov;
    bool ready;
} StarfieldState;

static RadialState g_radial;
static BloomSurface g_bloom = { 0 };
static StarfieldState g_starfield = { 0 };
static unsigned int g_star_rng = 0xA341316Cu;

static double g_window[FFT_SIZE];
static double g_real[FFT_SIZE];
static double g_imag[FFT_SIZE];
static double g_mags[FFT_SIZE / 2];
static int g_bandEdges[BAND_COUNT + 1];
static bool g_fftInit = false;
static int g_sampleRate = 44100;

static void visualizer_copy_defaults(void)
{
    g_visualizer = g_visualizerDefaults;
}

static void visualizer_build_ini_path(void)
{
    DWORD len;
    wchar_t *slash;

    if (g_visualizerIniPathReady) {
        return;
    }

    g_visualizerIniPath[0] = L'\0';
    len = GetModuleFileNameW(NULL, g_visualizerIniPath, (DWORD)(sizeof(g_visualizerIniPath) / sizeof(g_visualizerIniPath[0])));

    if (len == 0 || len >= (DWORD)(sizeof(g_visualizerIniPath) / sizeof(g_visualizerIniPath[0]))) {
        lstrcpynW(g_visualizerIniPath, L"hyperplayer.ini", (int)(sizeof(g_visualizerIniPath) / sizeof(g_visualizerIniPath[0])));
        g_visualizerIniPathReady = true;
        return;
    }

    slash = wcsrchr(g_visualizerIniPath, L'\\');
    if (slash) {
        slash[1] = L'\0';
        lstrcatW(g_visualizerIniPath, L"hyperplayer.ini");
    } else {
        lstrcpynW(g_visualizerIniPath, L"hyperplayer.ini", (int)(sizeof(g_visualizerIniPath) / sizeof(g_visualizerIniPath[0])));
    }

    g_visualizerIniPathReady = true;
}

static bool visualizer_parse_float(const wchar_t *text, float *outValue)
{
    wchar_t *endPtr = NULL;
    double value;

    if (!text || !*text || !outValue) {
        return false;
    }

    value = wcstod(text, &endPtr);
    if (endPtr == text) {
        return false;
    }

    while (*endPtr == L' ' || *endPtr == L'\t' || *endPtr == L'\r' || *endPtr == L'\n') {
        ++endPtr;
    }

    if (*endPtr != L'\0') {
        return false;
    }

    *outValue = (float)value;
    return true;
}

static bool visualizer_parse_int(const wchar_t *text, int *outValue)
{
    wchar_t *endPtr = NULL;
    long value;

    if (!text || !*text || !outValue) {
        return false;
    }

    value = wcstol(text, &endPtr, 10);
    if (endPtr == text) {
        return false;
    }

    while (*endPtr == L' ' || *endPtr == L'\t' || *endPtr == L'\r' || *endPtr == L'\n') {
        ++endPtr;
    }

    if (*endPtr != L'\0') {
        return false;
    }

    *outValue = (int)value;
    return true;
}

static bool visualizer_parse_hex_color(const wchar_t *text, COLORREF *outColor)
{
    wchar_t cleaned[16];
    int j = 0;
    unsigned long value;
    wchar_t *endPtr = NULL;

    if (!text || !outColor) {
        return false;
    }

    for (int i = 0; text[i] != L'\0' && j < (int)(sizeof(cleaned) / sizeof(cleaned[0])) - 1; ++i) {
        wchar_t c = text[i];

        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            continue;
        }

        if (j == 0 && c == L'#') {
            continue;
        }

        cleaned[j++] = c;
    }

    cleaned[j] = L'\0';

    if (j == 0) {
        return false;
    }

    if ((cleaned[0] == L'0') && (cleaned[1] == L'x' || cleaned[1] == L'X')) {
        memmove(cleaned, cleaned + 2, (wcslen(cleaned + 2) + 1) * sizeof(wchar_t));
        j -= 2;
    }

    if (j != 6) {
        return false;
    }

    value = wcstoul(cleaned, &endPtr, 16);
    if (endPtr == cleaned || *endPtr != L'\0') {
        return false;
    }

    *outColor = RGB(
        (int)((value >> 16) & 0xFFu),
        (int)((value >> 8) & 0xFFu),
        (int)(value & 0xFFu)
    );

    return true;
}

static float visualizer_read_float(const wchar_t *section, const wchar_t *key, float fallback)
{
    wchar_t buffer[64];
    float value;

    visualizer_build_ini_path();
    buffer[0] = L'\0';
    GetPrivateProfileStringW(section, key, L"", buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])), g_visualizerIniPath);

    if (visualizer_parse_float(buffer, &value)) {
        return value;
    }

    return fallback;
}

static int visualizer_read_int(const wchar_t *section, const wchar_t *key, int fallback)
{
    wchar_t buffer[64];
    int value;

    visualizer_build_ini_path();
    buffer[0] = L'\0';
    GetPrivateProfileStringW(section, key, L"", buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])), g_visualizerIniPath);

    if (visualizer_parse_int(buffer, &value)) {
        return value;
    }

    return fallback;
}

static COLORREF visualizer_read_hex_color(const wchar_t *section, const wchar_t *key, COLORREF fallback)
{
    wchar_t buffer[64];
    COLORREF value;

    visualizer_build_ini_path();
    buffer[0] = L'\0';
    GetPrivateProfileStringW(section, key, L"", buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])), g_visualizerIniPath);

    if (visualizer_parse_hex_color(buffer, &value)) {
        return value;
    }

    return fallback;
}

static void visualizer_load_config(void)
{
    visualizer_copy_defaults();

    g_visualizer.rotationSpeed = visualizer_read_float(L"VISUALIZER", L"VIS_ROTATION_SPEED", g_visualizer.rotationSpeed);
    g_visualizer.barDeclineSpeed = visualizer_read_float(L"VISUALIZER", L"VIS_BAR_DECLINE_SPEED", g_visualizer.barDeclineSpeed);
    g_visualizer.barLengthScale = visualizer_read_float(L"VISUALIZER", L"VIS_BAR_LENGTH_SCALE", g_visualizer.barLengthScale);
    g_visualizer.colorCycleSpeed = visualizer_read_float(L"VISUALIZER", L"VIS_COLOR_CYCLE_SPEED", g_visualizer.colorCycleSpeed);
    g_visualizer.globalGlowStrength = visualizer_read_float(L"VISUALIZER", L"VIS_GLOBAL_GLOW_STRENGTH", g_visualizer.globalGlowStrength);
    g_visualizer.highHzResponse = visualizer_read_float(L"VISUALIZER", L"VIS_HIGH_HZ_RESPONSE", g_visualizer.highHzResponse);
    g_visualizer.highHzLengthBonus = visualizer_read_float(L"VISUALIZER", L"VIS_HIGH_HZ_LENGTH_BONUS", g_visualizer.highHzLengthBonus);
    g_visualizer.starfieldCount = visualizer_read_int(L"VISUALIZER", L"VIS_STARFIELD_COUNT", g_visualizer.starfieldCount);
    g_visualizer.starfieldSpeed = visualizer_read_float(L"VISUALIZER", L"VIS_STARFIELD_SPEED", g_visualizer.starfieldSpeed);
    g_visualizer.starfieldBrightness = visualizer_read_float(L"VISUALIZER", L"VIS_STARFIELD_BRIGHTNESS", g_visualizer.starfieldBrightness);
    g_visualizer.bassOnlyBandCount = visualizer_read_int(L"VISUALIZER", L"VIS_BASS_ONLY_BAND_COUNT", g_visualizer.bassOnlyBandCount);
    g_visualizer.bassOnlyDominance = visualizer_read_float(L"VISUALIZER", L"VIS_BASS_ONLY_DOMINANCE", g_visualizer.bassOnlyDominance);
    g_visualizer.bassOnlyGateScale = visualizer_read_float(L"VISUALIZER", L"VIS_BASS_ONLY_GATE_SCALE", g_visualizer.bassOnlyGateScale);
    g_visualizer.bassOnlyAbsLevel = visualizer_read_float(L"VISUALIZER", L"VIS_BASS_ONLY_ABS_LEVEL", g_visualizer.bassOnlyAbsLevel);
    g_visualizer.agcIgnoreLowBands = visualizer_read_int(L"VISUALIZER", L"VIS_AGC_IGNORE_LOW_BANDS", g_visualizer.agcIgnoreLowBands);
    g_visualizer.highPresenceFloor = visualizer_read_float(L"VISUALIZER", L"VIS_HIGH_PRESENCE_FLOOR", g_visualizer.highPresenceFloor);

    g_visualizer.backgroundColor = visualizer_read_hex_color(L"VISUALIZER", L"VIS_BACKGROUND_COLOR", g_visualizer.backgroundColor);
    g_visualizer.starColor = visualizer_read_hex_color(L"VISUALIZER", L"VIS_STAR_COLOR", g_visualizer.starColor);

    if (g_visualizer.rotationSpeed < 0.0f) {
        g_visualizer.rotationSpeed = 0.0f;
    }
    if (g_visualizer.barDeclineSpeed < 0.0f) {
        g_visualizer.barDeclineSpeed = 0.0f;
    }
    if (g_visualizer.barLengthScale < 0.0f) {
        g_visualizer.barLengthScale = 0.0f;
    }
    if (g_visualizer.colorCycleSpeed < 0.0f) {
        g_visualizer.colorCycleSpeed = 0.0f;
    }
    if (g_visualizer.globalGlowStrength < 0.0f) {
        g_visualizer.globalGlowStrength = 0.0f;
    }
    if (g_visualizer.highHzResponse < 0.0f) {
        g_visualizer.highHzResponse = 0.0f;
    }
    if (g_visualizer.highHzLengthBonus < 0.0f) {
        g_visualizer.highHzLengthBonus = 0.0f;
    }
    if (g_visualizer.starfieldCount < 1) {
        g_visualizer.starfieldCount = 1;
    }
    if (g_visualizer.starfieldSpeed < 0.0f) {
        g_visualizer.starfieldSpeed = 0.0f;
    }
    if (g_visualizer.starfieldBrightness < 0.0f) {
        g_visualizer.starfieldBrightness = 0.0f;
    }
    if (g_visualizer.bassOnlyBandCount < 0) {
        g_visualizer.bassOnlyBandCount = 0;
    }
    if (g_visualizer.bassOnlyBandCount > BAND_COUNT) {
        g_visualizer.bassOnlyBandCount = BAND_COUNT;
    }
    if (g_visualizer.bassOnlyGateScale < 0.0001f) {
        g_visualizer.bassOnlyGateScale = 0.0001f;
    }
    if (g_visualizer.bassOnlyAbsLevel < 0.0001f) {
        g_visualizer.bassOnlyAbsLevel = 0.0001f;
    }
    if (g_visualizer.agcIgnoreLowBands < 0) {
        g_visualizer.agcIgnoreLowBands = 0;
    }
    if (g_visualizer.agcIgnoreLowBands > BAND_COUNT) {
        g_visualizer.agcIgnoreLowBands = BAND_COUNT;
    }
    if (g_visualizer.highPresenceFloor < 0.0f) {
        g_visualizer.highPresenceFloor = 0.0f;
    }

    g_visualizerLoaded = true;
}

static void visualizer_ensure_loaded(void)
{
    if (!g_visualizerLoaded) {
        visualizer_load_config();
    }
}

static COLORREF color_scale_rgb(COLORREF color, float scale)
{
    int r;
    int g;
    int b;

    if (scale < 0.0f) {
        scale = 0.0f;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }

    r = (int)((float)GetRValue(color) * scale);
    g = (int)((float)GetGValue(color) * scale);
    b = (int)((float)GetBValue(color) * scale);

    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    return RGB(r, g, b);
}

static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float lerpf_local(float a, float b, float t)
{
    return a + ((b - a) * t);
}

static bool point_in_rect_xy(int x, int y, int x1, int y1, int x2, int y2)
{
    return (x >= x1 && x <= x2 && y >= y1 && y <= y2);
}

static void clear_samples(void)
{
    memset(g_radial.mono, 0, sizeof(g_radial.mono));
}

static int reverse_bits(int x, int bits)
{
    int y = 0;

    for (int i = 0; i < bits; ++i) {
        y = (y * 2) + (x & 1);
        x >>= 1;
    }

    return y;
}

static void fft_inplace(double *re, double *im, int n)
{
    int bits = 0;
    int temp = n;

    while (temp > 1) {
        bits++;
        temp >>= 1;
    }

    for (int i = 0; i < n; ++i) {
        int j = reverse_bits(i, bits);
        if (j > i) {
            double tr = re[i];
            double ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }

    for (int len = 2; len <= n; len *= 2) {
        double ang = -2.0 * 3.14159265358979323846 / (double)len;
        double wlenCos = cos(ang);
        double wlenSin = sin(ang);

        for (int i = 0; i < n; i += len) {
            double wCos = 1.0;
            double wSin = 0.0;
            int half = len / 2;

            for (int j = 0; j < half; ++j) {
                double uR = re[i + j];
                double uI = im[i + j];

                double vR = (re[i + j + half] * wCos) - (im[i + j + half] * wSin);
                double vI = (re[i + j + half] * wSin) + (im[i + j + half] * wCos);

                re[i + j] = uR + vR;
                im[i + j] = uI + vI;
                re[i + j + half] = uR - vR;
                im[i + j + half] = uI - vI;

                {
                    double nextCos = (wCos * wlenCos) - (wSin * wlenSin);
                    double nextSin = (wCos * wlenSin) + (wSin * wlenCos);
                    wCos = nextCos;
                    wSin = nextSin;
                }
            }
        }
    }
}

static int hz_to_bin(double hz)
{
    int bin = (int)floor(((hz / (double)g_sampleRate) * (double)FFT_SIZE) + 0.5);
    int half = FFT_SIZE / 2;

    if (bin < 1) {
        bin = 1;
    }
    if (bin > half) {
        bin = half;
    }

    return bin;
}

static void init_fft_tables(void)
{
    double logMin;
    double logMax;

    if (g_fftInit) {
        return;
    }

    for (int n = 0; n < FFT_SIZE; ++n) {
        double t = (double)n / (double)(FFT_SIZE - 1);
        double hann = 0.5 * (1.0 - cos((2.0 * 3.14159265358979323846 * (double)n) / (double)(FFT_SIZE - 1)));
        double tail = 0.20 + (0.80 * t * t * t);
        g_window[n] = hann * tail;
    }

    logMin = log(MIN_HZ);
    logMax = log(MAX_HZ);

    for (int i = 0; i <= BAND_COUNT; ++i) {
        double t = (double)i / (double)BAND_COUNT;
        double hz = exp(logMin + ((logMax - logMin) * t));
        g_bandEdges[i] = hz_to_bin(hz);
    }

    g_fftInit = true;
}

static COLORREF hsv_to_rgb(float h, float s, float v)
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float hh;
    float f;
    float p;
    float q;
    float t;
    int i;
    int ri;
    int gi;
    int bi;

    while (h < 0.0f) {
        h += 1.0f;
    }
    while (h >= 1.0f) {
        h -= 1.0f;
    }

    s = clampf_local(s, 0.0f, 1.0f);
    v = clampf_local(v, 0.0f, 1.0f);

    if (s <= 0.0f) {
        ri = (int)(v * 255.0f);
        gi = (int)(v * 255.0f);
        bi = (int)(v * 255.0f);
        return RGB(ri, gi, bi);
    }

    hh = h * 6.0f;
    i = (int)floorf(hh);
    f = hh - (float)i;
    p = v * (1.0f - s);
    q = v * (1.0f - (s * f));
    t = v * (1.0f - (s * (1.0f - f)));

    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }

    ri = (int)(r * 255.0f);
    gi = (int)(g * 255.0f);
    bi = (int)(b * 255.0f);

    if (ri < 0) ri = 0;
    if (ri > 255) ri = 255;
    if (gi < 0) gi = 0;
    if (gi > 255) gi = 255;
    if (bi < 0) bi = 0;
    if (bi > 255) bi = 255;

    return RGB(ri, gi, bi);
}

static unsigned int pack_bgra(unsigned int r, unsigned int g, unsigned int b, unsigned int a)
{
    return ((a & 255u) << 24) | ((r & 255u) << 16) | ((g & 255u) << 8) | (b & 255u);
}

static void bloom_release(void)
{
    if (g_bloom.dc) {
        if (g_bloom.oldBitmap) {
            SelectObject(g_bloom.dc, g_bloom.oldBitmap);
            g_bloom.oldBitmap = NULL;
        }

        if (g_bloom.bitmap) {
            DeleteObject(g_bloom.bitmap);
            g_bloom.bitmap = NULL;
        }

        DeleteDC(g_bloom.dc);
        g_bloom.dc = NULL;
    }

    if (g_bloom.src) {
        free(g_bloom.src);
        g_bloom.src = NULL;
    }

    if (g_bloom.tmp) {
        free(g_bloom.tmp);
        g_bloom.tmp = NULL;
    }

    g_bloom.pixels = NULL;
    g_bloom.width = 0;
    g_bloom.height = 0;
}

static bool bloom_ensure(int width, int height)
{
    BITMAPINFO bmi;
    void *bits = NULL;

    if (width <= 0 || height <= 0) {
        return false;
    }

    if (g_bloom.dc &&
        g_bloom.bitmap &&
        g_bloom.pixels &&
        g_bloom.src &&
        g_bloom.tmp &&
        g_bloom.width == width &&
        g_bloom.height == height) {
        return true;
    }

    bloom_release();

    g_bloom.dc = CreateCompatibleDC(NULL);
    if (!g_bloom.dc) {
        return false;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_bloom.bitmap = CreateDIBSection(g_bloom.dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!g_bloom.bitmap || !bits) {
        bloom_release();
        return false;
    }

    g_bloom.oldBitmap = (HBITMAP)SelectObject(g_bloom.dc, g_bloom.bitmap);
    g_bloom.pixels = (unsigned int *)bits;
    g_bloom.src = (unsigned int *)malloc((size_t)width * (size_t)height * sizeof(unsigned int));
    g_bloom.tmp = (unsigned int *)malloc((size_t)width * (size_t)height * sizeof(unsigned int));

    if (!g_bloom.src || !g_bloom.tmp) {
        bloom_release();
        return false;
    }

    g_bloom.width = width;
    g_bloom.height = height;
    return true;
}

static void buffer_add_color(unsigned int *buffer, int width, int height, int x, int y, COLORREF color, float amount)
{
    unsigned int p;
    int r;
    int g;
    int b;

    if (!buffer || x < 0 || y < 0 || x >= width || y >= height || amount <= 0.0f) {
        return;
    }

    p = buffer[(size_t)y * (size_t)width + (size_t)x];

    r = (int)((float)((p >> 16) & 255u) + ((float)GetRValue(color) * amount));
    g = (int)((float)((p >> 8) & 255u) + ((float)GetGValue(color) * amount));
    b = (int)((float)(p & 255u) + ((float)GetBValue(color) * amount));

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    buffer[(size_t)y * (size_t)width + (size_t)x] = pack_bgra((unsigned int)r, (unsigned int)g, (unsigned int)b, 0u);
}

static void buffer_add_soft_ellipse(
    unsigned int *buffer,
    int width,
    int height,
    float cx,
    float cy,
    float rx,
    float ry,
    COLORREF color,
    float strength
)
{
    int minX;
    int maxX;
    int minY;
    int maxY;

    if (!buffer || rx <= 0.0f || ry <= 0.0f || strength <= 0.0f) {
        return;
    }

    minX = (int)floorf(cx - rx - 1.0f);
    maxX = (int)ceilf(cx + rx + 1.0f);
    minY = (int)floorf(cy - ry - 1.0f);
    maxY = (int)ceilf(cy + ry + 1.0f);

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float dx = ((float)x + 0.5f - cx) / rx;
            float dy = ((float)y + 0.5f - cy) / ry;
            float d2 = (dx * dx) + (dy * dy);

            if (d2 < 1.0f) {
                float d = sqrtf(d2);
                float falloff = 1.0f - d;
                float glow = falloff * falloff * strength;
                buffer_add_color(buffer, width, height, x, y, color, glow);
            }
        }
    }
}

static void buffer_add_soft_line(
    unsigned int *buffer,
    int width,
    int height,
    float x1,
    float y1,
    float x2,
    float y2,
    float radius,
    COLORREF color,
    float strength
)
{
    float dx;
    float dy;
    float len;
    int steps;

    if (!buffer || radius <= 0.0f || strength <= 0.0f) {
        return;
    }

    dx = x2 - x1;
    dy = y2 - y1;
    len = sqrtf((dx * dx) + (dy * dy));
    steps = (int)(len * 1.25f) + 1;

    if (steps < 1) {
        steps = 1;
    }

    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float px = x1 + (dx * t);
        float py = y1 + (dy * t);
        buffer_add_soft_ellipse(buffer, width, height, px, py, radius, radius, color, strength);
    }
}

static void blur_horizontal(const unsigned int *src, unsigned int *dst, int width, int height, int radius)
{
    if (!src || !dst || width <= 0 || height <= 0) {
        return;
    }

    if (radius < 1) {
        memcpy(dst, src, (size_t)width * (size_t)height * sizeof(unsigned int));
        return;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            int count = 0;

            for (int k = -radius; k <= radius; ++k) {
                int xx = x + k;
                unsigned int p;

                if (xx < 0 || xx >= width) {
                    continue;
                }

                p = src[(size_t)y * (size_t)width + (size_t)xx];
                b += (int)(p & 255u);
                g += (int)((p >> 8) & 255u);
                r += (int)((p >> 16) & 255u);
                count++;
            }

            if (count < 1) {
                count = 1;
            }

            dst[(size_t)y * (size_t)width + (size_t)x] = pack_bgra(
                (unsigned int)(r / count),
                (unsigned int)(g / count),
                (unsigned int)(b / count),
                0u
            );
        }
    }
}

static void blur_vertical_to_premult(
    const unsigned int *src,
    unsigned int *dst,
    int width,
    int height,
    int radius,
    float strength
)
{
    if (!src || !dst || width <= 0 || height <= 0) {
        return;
    }

    if (radius < 1) {
        radius = 1;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int r = 0;
            int g = 0;
            int b = 0;
            int count = 0;
            int rr;
            int gg;
            int bb;
            int aa;

            for (int k = -radius; k <= radius; ++k) {
                int yy = y + k;
                unsigned int p;

                if (yy < 0 || yy >= height) {
                    continue;
                }

                p = src[(size_t)yy * (size_t)width + (size_t)x];
                b += (int)(p & 255u);
                g += (int)((p >> 8) & 255u);
                r += (int)((p >> 16) & 255u);
                count++;
            }

            if (count < 1) {
                count = 1;
            }

            rr = (int)(((float)r / (float)count) * strength);
            gg = (int)(((float)g / (float)count) * strength);
            bb = (int)(((float)b / (float)count) * strength);

            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;

            aa = rr;
            if (gg > aa) aa = gg;
            if (bb > aa) aa = bb;

            dst[(size_t)y * (size_t)width + (size_t)x] = pack_bgra(
                (unsigned int)rr,
                (unsigned int)gg,
                (unsigned int)bb,
                (unsigned int)aa
            );
        }
    }
}

static void compute_bass_gate_metrics(
    const float *samples,
    int count,
    float *outBassMetric,
    float *outRefMetric
)
{
    float bass1 = 0.0f;
    float refLp = 0.0f;
    float dt;
    float rcBass;
    float rcRef;
    float aBass;
    float aRef;
    float bassPeak = 0.0f;
    float refPeak = 0.0f;
    double bassSum = 0.0;
    double refSum = 0.0;
    double totalWeight = 0.0;

    if (outBassMetric) {
        *outBassMetric = 0.0f;
    }
    if (outRefMetric) {
        *outRefMetric = 0.0f;
    }

    if (!samples || count <= 0 || g_sampleRate <= 0) {
        return;
    }

    dt = 1.0f / (float)g_sampleRate;
    rcBass = 1.0f / (2.0f * 3.14159265358979323846f * 180.0f);
    rcRef = 1.0f / (2.0f * 3.14159265358979323846f * 220.0f);

    aBass = dt / (rcBass + dt);
    aRef = dt / (rcRef + dt);

    for (int i = 0; i < count; ++i) {
        float s = samples[i];
        float refHp;
        float bassAbs;
        float refAbs;
        float w;

        bass1 += aBass * (s - bass1);
        refLp += aRef * (s - refLp);
        refHp = s - refLp;

        bassAbs = fabsf(bass1);
        refAbs = fabsf(refHp);

        w = 0.15f + (0.85f * ((float)i / (float)(count - 1)));

        bassSum += (double)(bassAbs * bassAbs * w);
        refSum += (double)(refAbs * refAbs * w);
        totalWeight += (double)w;

        if (bassAbs > bassPeak) {
            bassPeak = bassAbs;
        }
        if (refAbs > refPeak) {
            refPeak = refAbs;
        }
    }

    if (totalWeight <= 0.0) {
        totalWeight = 1.0;
    }

    if (outBassMetric) {
        float bassRms = sqrtf((float)(bassSum / totalWeight));
        *outBassMetric = (bassRms * 0.55f) + (bassPeak * 0.95f);
    }

    if (outRefMetric) {
        float refRms = sqrtf((float)(refSum / totalWeight));
        *outRefMetric = (refRms * 0.95f) + (refPeak * 0.15f);
    }
}

static float star_rand01(void)
{
    g_star_rng = (g_star_rng * 1664525u) + 1013904223u;
    return (float)(g_star_rng & 0x00FFFFFFu) / 16777215.0f;
}

static float star_rand_range(float a, float b)
{
    return a + ((b - a) * star_rand01());
}

static void starfield_release(void)
{
    if (g_starfield.x) free(g_starfield.x);
    if (g_starfield.y) free(g_starfield.y);
    if (g_starfield.z) free(g_starfield.z);
    if (g_starfield.speed) free(g_starfield.speed);
    if (g_starfield.brightness) free(g_starfield.brightness);
    if (g_starfield.size) free(g_starfield.size);

    g_starfield.x = NULL;
    g_starfield.y = NULL;
    g_starfield.z = NULL;
    g_starfield.speed = NULL;
    g_starfield.brightness = NULL;
    g_starfield.size = NULL;
    g_starfield.count = 0;
    g_starfield.width = 0;
    g_starfield.height = 0;
    g_starfield.fov = 0.0f;
    g_starfield.ready = false;
}

static void starfield_reset_one(int i)
{
    if (!g_starfield.ready || i < 0 || i >= g_starfield.count) {
        return;
    }

    g_starfield.x[i] = star_rand_range(-(float)g_starfield.width, (float)g_starfield.width);
    g_starfield.y[i] = star_rand_range(-(float)g_starfield.height, (float)g_starfield.height);
    g_starfield.z[i] = star_rand_range(1.0f, (float)g_starfield.width);
    g_starfield.speed[i] = star_rand_range(20.0f, 150.0f);
    g_starfield.brightness[i] = star_rand_range(0.25f, 1.0f);
    g_starfield.size[i] = star_rand_range(1.0f, 3.0f);
}

static bool starfield_ensure(int count, int width, int height)
{
    if (count < 1 || width <= 0 || height <= 0) {
        return false;
    }

    if (g_starfield.ready &&
        g_starfield.count == count &&
        g_starfield.width == width &&
        g_starfield.height == height) {
        return true;
    }

    starfield_release();

    g_starfield.x = (float *)malloc((size_t)count * sizeof(float));
    g_starfield.y = (float *)malloc((size_t)count * sizeof(float));
    g_starfield.z = (float *)malloc((size_t)count * sizeof(float));
    g_starfield.speed = (float *)malloc((size_t)count * sizeof(float));
    g_starfield.brightness = (float *)malloc((size_t)count * sizeof(float));
    g_starfield.size = (float *)malloc((size_t)count * sizeof(float));

    if (!g_starfield.x || !g_starfield.y || !g_starfield.z ||
        !g_starfield.speed || !g_starfield.brightness || !g_starfield.size) {
        starfield_release();
        return false;
    }

    g_starfield.count = count;
    g_starfield.width = width;
    g_starfield.height = height;
    g_starfield.fov = (float)width * 0.5f;
    g_starfield.ready = true;

    for (int i = 0; i < count; ++i) {
        starfield_reset_one(i);
    }

    return true;
}

static void starfield_project(float x, float y, float z, float *sx, float *sy)
{
    float projection;

    if (sx) {
        *sx = 0.0f;
    }
    if (sy) {
        *sy = 0.0f;
    }

    if (!sx || !sy || z <= 0.0f) {
        return;
    }

    projection = g_starfield.fov / z;
    *sx = (x * projection) + ((float)g_starfield.width * 0.5f);
    *sy = (y * projection) + ((float)g_starfield.height * 0.5f);
}

static void starfield_draw_square(HDC hdc, int x, int y, int size, COLORREF color)
{
    if (size <= 1) {
        SetPixelV(hdc, x, y, color);
        return;
    }

    for (int yy = 0; yy < size; ++yy) {
        for (int xx = 0; xx < size; ++xx) {
            SetPixelV(hdc, x + xx, y + yy, color);
        }
    }
}

static void starfield_draw(HDC hdc)
{
    int i;

    if (!hdc || !g_starfield.ready) {
        return;
    }

    for (i = 0; i < g_starfield.count; ++i) {
        float sx = 0.0f;
        float sy = 0.0f;
        float brightness;
        int px;
        int py;
        int c;
        int size;
        COLORREF color;

        starfield_project(g_starfield.x[i], g_starfield.y[i], g_starfield.z[i], &sx, &sy);

        if (sx <= 0.0f || sx >= (float)g_starfield.width || sy <= 0.0f || sy >= (float)g_starfield.height) {
            continue;
        }

        brightness = 1.0f - (g_starfield.z[i] / (float)g_starfield.width);
        brightness = clampf_local(brightness, 0.0f, 1.0f);
        brightness *= g_starfield.brightness[i] * g_visualizer.starfieldBrightness;
        brightness = clampf_local(brightness, 0.0f, 1.0f);

        c = (int)(brightness * 255.0f);
        if (c < 12) {
            continue;
        }

        px = TV_BOX_X + (int)lroundf(sx);
        py = TV_BOX_Y + (int)lroundf(sy);

        size = 1;
        if (brightness > 0.40f) {
            size = 2;
        }
        if (brightness > 0.78f) {
            size = 3;
        }

        color = color_scale_rgb(g_visualizer.starColor, (float)c / 255.0f);
        starfield_draw_square(hdc, px, py, size, color);
    }
}

static void starfield_update(double dt)
{
    int i;

    if (!g_starfield.ready) {
        return;
    }

    for (i = 0; i < g_starfield.count; ++i) {
        g_starfield.z[i] -= g_starfield.speed[i] * g_visualizer.starfieldSpeed * (float)dt;

        if (g_starfield.z[i] < 1.0f) {
            starfield_reset_one(i);
            continue;
        }

        g_starfield.brightness[i] += (star_rand01() - 0.5f) * 0.05f;

        if (g_starfield.brightness[i] < 0.15f) {
            g_starfield.brightness[i] = 0.15f;
        }
        if (g_starfield.brightness[i] > 1.0f) {
            g_starfield.brightness[i] = 1.0f;
        }
    }
}

static HPEN make_round_pen(int width, COLORREF color)
{
    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    lb.lbHatch = 0;

    return ExtCreatePen(
        PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND,
        (DWORD)width,
        &lb,
        0,
        NULL
    );
}

static void draw_core_line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color, int width)
{
    HPEN pen;
    HPEN oldPen;

    pen = make_round_pen(width, color);
    oldPen = (HPEN)SelectObject(hdc, pen);

    MoveToEx(hdc, x1, y1, NULL);
    LineTo(hdc, x2, y2);

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void tunnelvisualizer_update(AppState *app, double dt)
{
    visualizer_ensure_loaded();
    float mono[FFT_SIZE];
    bool haveWindow = false;
    bool playing = false;
    float frameEnergy = 0.0f;
    float bass = 0.0f;
    float treble = 0.0f;
    float bassPrev;
    float bassMetric = 0.0f;
    float refMetric = 0.0f;
    float lowBandGate = 0.0f;
    float attackBoost = 0.0f;
    float meanRaw = 0.0f;
    float targetGain = 1.0f;
    int bassBands;
    int trebleBandsStart;

    init_fft_tables();
    starfield_ensure(g_visualizer.starfieldCount, TV_BOX_W, TV_BOX_H);

    if (!g_radial.initialized) {
        memset(&g_radial, 0, sizeof(g_radial));
        clear_samples();
        g_radial.agcGain = 1.0f;
        g_radial.bassGatePrev = 0.0f;
        g_radial.initialized = true;
    }

    if (dt < 0.0) {
        dt = 0.0f;
    }
    if (dt > 0.1) {
        dt = 0.1f;
    }

    if (app && player_is_loaded(app) && player_get_recent_mono_window(app, mono, FFT_SIZE)) {
        memcpy(g_radial.mono, mono, sizeof(mono));
        haveWindow = true;
    } else {
        clear_samples();
    }

    if (app && player_is_loaded(app) && !player_is_paused(app) && !player_is_stopped(app)) {
        playing = true;
    }

    if (haveWindow && playing) {
        compute_bass_gate_metrics(g_radial.mono, FFT_SIZE, &bassMetric, &refMetric);

        {
            float dominance = bassMetric / (refMetric + 0.0001f);
            float dominanceGate = clampf_local(
                (dominance - (g_visualizer.bassOnlyDominance * 0.55f)) /
                (g_visualizer.bassOnlyGateScale * 1.40f),
                0.0f,
                1.0f
            );
            float absPresence = clampf_local(
                bassMetric / (g_visualizer.bassOnlyAbsLevel * 0.70f),
                0.0f,
                1.0f
            );
            float bassRise = bassMetric - g_radial.bassGatePrev;

            attackBoost = clampf_local(
                bassRise / (g_visualizer.bassOnlyAbsLevel * 0.18f),
                0.0f,
                1.0f
            );

            lowBandGate = absPresence;
            if (attackBoost > lowBandGate) {
                lowBandGate = attackBoost;
            }
            if ((dominanceGate * 0.65f) > lowBandGate) {
                lowBandGate = dominanceGate * 0.65f;
            }

            lowBandGate = clampf_local(lowBandGate, 0.0f, 1.0f);
            g_radial.bassGatePrev = bassMetric;
        }

        for (int i = 0; i < FFT_SIZE; ++i) {
            g_real[i] = (double)g_radial.mono[i] * g_window[i];
            g_imag[i] = 0.0;
        }

        fft_inplace(g_real, g_imag, FFT_SIZE);

        for (int k = 0; k < (FFT_SIZE / 2); ++k) {
            double r = g_real[k];
            double im = g_imag[k];
            g_mags[k] = sqrt((r * r) + (im * im));
        }

        {
            int agcBandsUsed = 0;

            for (int i = 0; i < BAND_COUNT; ++i) {
                int a = g_bandEdges[i];
                int b = g_bandEdges[i + 1] - 1;
                double sum = 0.0;
                int count = 0;
                float raw;

                if (b < a) {
                    b = a;
                }
                if (b > (FFT_SIZE / 2)) {
                    b = FFT_SIZE / 2;
                }

                for (int k = a; k <= b; ++k) {
                    sum += g_mags[k - 1];
                    count++;
                }

                if (count <= 0) {
                    count = 1;
                }

                raw = (float)(sum / (double)count);
                raw = log10f(1.0f + (raw * 4.2f));

                if (i >= g_visualizer.agcIgnoreLowBands) {
                    meanRaw += raw;
                    agcBandsUsed++;
                }
            }

            if (agcBandsUsed > 0) {
                meanRaw /= (float)agcBandsUsed;
            } else {
                meanRaw = 0.0f;
            }
        }

        if (meanRaw > 0.0001f) {
            targetGain = 0.18f / meanRaw;
        } else {
            targetGain = 1.0f;
        }

        targetGain = clampf_local(targetGain, 0.16f, 3.2f);
        g_radial.agcGain = lerpf_local(g_radial.agcGain, targetGain, 0.45f);

        for (int i = 0; i < BAND_COUNT; ++i) {
            int a = g_bandEdges[i];
            int b = g_bandEdges[i + 1] - 1;
            double sum = 0.0;
            int count = 0;
            float raw;
            float shaped;
            float target;
            float smoothing;
            float hiRamp;
            float hiCurve;
            float hiBoost;

            if (b < a) {
                b = a;
            }
            if (b > (FFT_SIZE / 2)) {
                b = FFT_SIZE / 2;
            }

            for (int k = a; k <= b; ++k) {
                sum += g_mags[k - 1];
                count++;
            }

            if (count <= 0) {
                count = 1;
            }

            raw = (float)(sum / (double)count);
            raw = log10f(1.0f + (raw * 4.2f));

            hiRamp = (float)i / (float)(BAND_COUNT - 1);
            hiCurve = powf(hiRamp, 1.8f);
            hiBoost = 1.0f + (hiCurve * (g_visualizer.highHzResponse - 1.0f));

            shaped = raw;

            if (i < BAND_COUNT / 5) {
                shaped *= 1.12f;
            }

            shaped *= hiBoost;

            target = shaped * g_radial.agcGain;
            target = clampf_local(target, 0.0f, 1.0f);
            target = powf(target, 1.10f);

            if (i >= g_visualizer.bassOnlyBandCount) {
                float preserve = clampf_local(raw * g_visualizer.highPresenceFloor, 0.0f, 1.0f);
                if (target < preserve) {
                    target = preserve;
                }
            }

            if (i < g_visualizer.bassOnlyBandCount) {
                float lowBandLift;
                float lowBandCurve;
                float lowRawFloor;

                lowBandLift = clampf_local(
                    0.25f + (lowBandGate * 0.75f) + (attackBoost * 0.20f),
                    0.0f,
                    1.0f
                );

                target *= lowBandLift;

                lowBandCurve = 1.0f;
                if (g_visualizer.bassOnlyBandCount > 1) {
                    lowBandCurve = 1.0f - ((float)i / (float)(g_visualizer.bassOnlyBandCount - 1));
                }

                lowRawFloor = clampf_local(
                    raw * (0.95f - (0.30f * (1.0f - lowBandCurve))),
                    0.0f,
                    1.0f
                );

                if (target < (lowRawFloor * (0.35f + (lowBandGate * 0.65f)))) {
                    target = lowRawFloor * (0.35f + (lowBandGate * 0.65f));
                }
            }

            if (target < 0.002f) {
                target = 0.0f;
            }

            if (target > g_radial.bands[i]) {
                smoothing = 0.98f;
                if (i < g_visualizer.bassOnlyBandCount) {
                    smoothing = 1.00f;
                }
            } else {
                smoothing = 0.82f;
            }

            g_radial.bands[i] = lerpf_local(g_radial.bands[i], target, smoothing);
            g_radial.bands[i] -= (float)(dt * g_visualizer.barDeclineSpeed * 1.10f);

            if (g_radial.bands[i] < 0.0f) {
                g_radial.bands[i] = 0.0f;
            }

            frameEnergy += g_radial.bands[i];
        }

        frameEnergy /= (float)BAND_COUNT;
    } else {
        for (int i = 0; i < BAND_COUNT; ++i) {
            g_radial.bands[i] = lerpf_local(g_radial.bands[i], 0.0f, 0.70f);
            g_radial.bands[i] -= (float)(dt * g_visualizer.barDeclineSpeed * 1.10f);

            if (g_radial.bands[i] < 0.0f) {
                g_radial.bands[i] = 0.0f;
            }

            frameEnergy += g_radial.bands[i];
        }

        frameEnergy /= (float)BAND_COUNT;
        g_radial.bassGatePrev = 0.0f;
    }

    bassBands = BAND_COUNT / 6;
    if (bassBands < 1) {
        bassBands = 1;
    }

    trebleBandsStart = (BAND_COUNT * 3) / 4;
    if (trebleBandsStart < 0) {
        trebleBandsStart = 0;
    }
    if (trebleBandsStart >= BAND_COUNT) {
        trebleBandsStart = BAND_COUNT - 1;
    }

    for (int i = 0; i < bassBands; ++i) {
        bass += g_radial.bands[i];
    }
    bass /= (float)bassBands;

    for (int i = trebleBandsStart; i < BAND_COUNT; ++i) {
        treble += g_radial.bands[i];
    }
    treble /= (float)(BAND_COUNT - trebleBandsStart);

    bassPrev = g_radial.bass;

    if (playing) {
        g_radial.energy = lerpf_local(g_radial.energy, clampf_local(frameEnergy, 0.0f, 1.0f), 0.72f);
        g_radial.bass = lerpf_local(g_radial.bass, clampf_local(bass, 0.0f, 1.0f), 0.82f);
        g_radial.treble = lerpf_local(g_radial.treble, clampf_local(treble, 0.0f, 1.0f), 0.72f);

        if ((g_radial.bass - bassPrev) > 0.012f && g_radial.bass > 0.03f) {
            g_radial.beatFlash = 1.0f;
        }

        g_radial.rotation += dt * (0.10f + (g_radial.energy * 0.18f)) * g_visualizer.rotationSpeed;
    } else {
        g_radial.energy = lerpf_local(g_radial.energy, 0.0f, 0.55f);
        g_radial.bass = lerpf_local(g_radial.bass, 0.0f, 0.60f);
        g_radial.treble = lerpf_local(g_radial.treble, 0.0f, 0.55f);
        g_radial.rotation += dt * 0.03f * g_visualizer.rotationSpeed;
    }

    g_radial.beatFlash *= 0.72f;
    if (g_radial.beatFlash < 0.0f) {
        g_radial.beatFlash = 0.0f;
    }

    g_radial.time += dt;

    starfield_update(dt);
}

bool tunnelvisualizer_mouse_down(AppState *app, int x, int y)
{
    if (!app) {
        return false;
    }

    if (!player_is_loaded(app)) {
        return false;
    }

    if (!point_in_rect_xy(x, y, TV_BROWSER_BUTTON_X1, TV_BROWSER_BUTTON_Y1, TV_BROWSER_BUTTON_X2, TV_BROWSER_BUTTON_Y2)) {
        return false;
    }

    app->showFileBrowser = !app->showFileBrowser;

    if (app->showFileBrowser) {
        app_set_status(app, L"File browser shown.");
    } else {
        app_set_status(app, L"Radial spectrum visualizer shown.");
    }

    return true;
}

void tunnelvisualizer_draw(AppState *app, HDC hdc)
{
    visualizer_ensure_loaded();
    RECT clipRect;
    HBRUSH bgBrush;
    int savedDc;
    int cx;
    int cy;
    int innerRadius;
    int i;
    int bloomRadius;
    BLENDFUNCTION bf;
    float auraStrength;
    float hueBase;
    COLORREF auraColorA;
    COLORREF auraColorB;
    int auraRx;
    int auraRy;
    bool bloomReady;

    if (!app || !hdc) {
        return;
    }

    if (app->showFileBrowser) {
        return;
    }

    if (!player_is_loaded(app)) {
        return;
    }

    clipRect.left = TV_BOX_X;
    clipRect.top = TV_BOX_Y;
    clipRect.right = TV_BOX_X + TV_BOX_W;
    clipRect.bottom = TV_BOX_Y + TV_BOX_H;

    savedDc = SaveDC(hdc);
    IntersectClipRect(hdc, clipRect.left, clipRect.top, clipRect.right, clipRect.bottom);

    bgBrush = CreateSolidBrush(g_visualizer.backgroundColor);
    FillRect(hdc, &clipRect, bgBrush);
    DeleteObject(bgBrush);

    starfield_draw(hdc);

    cx = TV_BOX_W / 2;
    cy = TV_BOX_H / 2;

    if (TV_BOX_H < TV_BOX_W) {
        innerRadius = (TV_BOX_H / 2) - 56;
    } else {
        innerRadius = (TV_BOX_W / 2) - 56;
    }

    if (innerRadius < 40) {
        innerRadius = 40;
    }

    bloomReady = bloom_ensure(TV_BOX_W, TV_BOX_H);

    if (bloomReady) {
        memset(g_bloom.src, 0, (size_t)g_bloom.width * (size_t)g_bloom.height * sizeof(unsigned int));
        memset(g_bloom.tmp, 0, (size_t)g_bloom.width * (size_t)g_bloom.height * sizeof(unsigned int));
        memset(g_bloom.pixels, 0, (size_t)g_bloom.width * (size_t)g_bloom.height * sizeof(unsigned int));

        auraStrength = clampf_local(
            ((g_radial.energy * 0.85f) + (g_radial.bass * 0.95f) + (g_radial.beatFlash * 0.45f)) * g_visualizer.globalGlowStrength,
            0.0f,
            1.8f
        );

        hueBase = (float)(g_radial.rotation * (0.02f * g_visualizer.colorCycleSpeed));
        while (hueBase >= 1.0f) {
            hueBase -= 1.0f;
        }

        auraColorA = hsv_to_rgb(hueBase + 0.02f, 0.65f, 1.00f);
        auraColorB = hsv_to_rgb(hueBase + 0.18f, 0.70f, 1.00f);

        auraRx = (int)lroundf((double)innerRadius + 90.0);
        auraRy = (int)lroundf(((double)innerRadius * 0.72) + 58.0);

        buffer_add_soft_ellipse(g_bloom.src, g_bloom.width, g_bloom.height, (float)cx, (float)cy, (float)auraRx, (float)auraRy, auraColorA, auraStrength * 0.18f);
        buffer_add_soft_ellipse(g_bloom.src, g_bloom.width, g_bloom.height, (float)cx, (float)cy, (float)(auraRx * 0.70f), (float)(auraRy * 0.70f), auraColorB, auraStrength * 0.24f);

        for (i = 0; i < BAND_COUNT; ++i) {
            float t = (float)i / (float)BAND_COUNT;
            float angle = (float)(g_radial.rotation + (t * 6.28318530718) - 1.57079632679);
            float amp = clampf_local(g_radial.bands[i], 0.0f, 1.0f);
            float hiRamp = (float)i / (float)(BAND_COUNT - 1);
            float hiCurve = powf(hiRamp, 1.6f);
            float hiLenBoost = 1.0f + (hiCurve * g_visualizer.highHzLengthBonus);
            float barBase = (float)innerRadius;
            float len = 5.0f + ((((amp * 86.0f) + (g_radial.bass * 8.0f)) * 0.5f) * g_visualizer.barLengthScale * hiLenBoost);
            float sx = (float)cx + (cosf(angle) * barBase);
            float sy = (float)cy + (sinf(angle) * barBase);
            float ex = (float)cx + (cosf(angle) * (barBase + len));
            float ey = (float)cy + (sinf(angle) * (barBase + len));
            float hue = t + (float)(g_radial.rotation * (0.02f * g_visualizer.colorCycleSpeed));
            float bloomRadiusF;
            float bloomIntensity;
            COLORREF color;

            while (hue >= 1.0f) {
                hue -= 1.0f;
            }

            color = hsv_to_rgb(hue, 0.78f, 1.00f);

            bloomRadiusF = 2.8f + (amp * 4.8f) + (g_visualizer.globalGlowStrength * 2.5f);
            bloomIntensity = (0.10f + (amp * 0.22f) + (g_radial.beatFlash * 0.08f)) * g_visualizer.globalGlowStrength;

            buffer_add_soft_line(
                g_bloom.src,
                g_bloom.width,
                g_bloom.height,
                sx,
                sy,
                ex,
                ey,
                bloomRadiusF,
                color,
                bloomIntensity
            );
        }

        bloomRadius = 4 + (int)lroundf(g_visualizer.globalGlowStrength * 4.0f);
        if (bloomRadius < 2) {
            bloomRadius = 2;
        }
        if (bloomRadius > 14) {
            bloomRadius = 14;
        }

        blur_horizontal(g_bloom.src, g_bloom.tmp, g_bloom.width, g_bloom.height, bloomRadius);
        blur_vertical_to_premult(g_bloom.tmp, g_bloom.pixels, g_bloom.width, g_bloom.height, bloomRadius, 1.0f);

        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat = AC_SRC_ALPHA;

        AlphaBlend(hdc, TV_BOX_X, TV_BOX_Y, TV_BOX_W, TV_BOX_H, g_bloom.dc, 0, 0, g_bloom.width, g_bloom.height, bf);
    }

    for (i = 0; i < BAND_COUNT; ++i) {
        float t = (float)i / (float)BAND_COUNT;
        float angle = (float)(g_radial.rotation + (t * 6.28318530718) - 1.57079632679);
        float amp = clampf_local(g_radial.bands[i], 0.0f, 1.0f);
        float hiRamp = (float)i / (float)(BAND_COUNT - 1);
        float hiCurve = powf(hiRamp, 1.6f);
        float hiLenBoost = 1.0f + (hiCurve * g_visualizer.highHzLengthBonus);
        float barBase = (float)innerRadius;
        float len = 5.0f + ((((amp * 86.0f) + (g_radial.bass * 8.0f)) * 0.5f) * g_visualizer.barLengthScale * hiLenBoost);
        float sx = (float)TV_BOX_X + (float)cx + (cosf(angle) * barBase);
        float sy = (float)TV_BOX_Y + (float)cy + (sinf(angle) * barBase);
        float ex = (float)TV_BOX_X + (float)cx + (cosf(angle) * (barBase + len));
        float ey = (float)TV_BOX_Y + (float)cy + (sinf(angle) * (barBase + len));
        float hue = t + (float)(g_radial.rotation * (0.02f * g_visualizer.colorCycleSpeed));
        float val = 0.40f + (amp * 0.60f) + (g_radial.beatFlash * 0.04f);
        COLORREF color;
        int width;

        while (hue >= 1.0f) {
            hue -= 1.0f;
        }

        color = hsv_to_rgb(hue, 0.84f, val);

        width = 3;
        if (amp > 0.40f) {
            width = 4;
        }
        if (amp > 0.78f) {
            width = 5;
        }

        draw_core_line(
            hdc,
            (int)lroundf(sx),
            (int)lroundf(sy),
            (int)lroundf(ex),
            (int)lroundf(ey),
            color,
            width
        );
    }

    RestoreDC(hdc, savedDc);
}