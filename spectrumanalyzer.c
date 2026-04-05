#include "spectrumanalyzer.h"
#include "player.h"

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define BOX_X 1363
#define BOX_Y 658
#define BOX_W 555
#define BOX_H 110

typedef struct SpectrumAnalyzerConfig {
    int fftSize;
    int bandCount;
    double minHz;
    double maxHz;
    int barW;
    int gapX;
    int slatH;
    int slatGap;
    COLORREF barColor;
} SpectrumAnalyzerConfig;

static double *g_window = NULL;
static double *g_real = NULL;
static double *g_imag = NULL;
static double *g_mags = NULL;
static float *g_mono = NULL;
static float *g_bars = NULL;
static int *g_bandEdges = NULL;
static double *g_bandEQ = NULL;
static bool g_initialized = false;
static bool g_configLoaded = false;
static int g_sampleRate = 44100;

static SpectrumAnalyzerConfig g_cfg = {
    1024,
    37,
    80.0,
    11000.0,
    14,
    1,
    6,
    2,
    RGB(0xBB, 0xBB, 0xBB)
};

static double clamp01(double x)
{
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

static bool is_wspace_simple(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

static bool is_wdigit_simple(wchar_t ch)
{
    return ch >= L'0' && ch <= L'9';
}

static bool is_hex_digit_simple(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') ||
           (ch >= L'a' && ch <= L'f') ||
           (ch >= L'A' && ch <= L'F');
}

static int hex_digit_value(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9') return (int)(ch - L'0');
    if (ch >= L'a' && ch <= L'f') return 10 + (int)(ch - L'a');
    if (ch >= L'A' && ch <= L'F') return 10 + (int)(ch - L'A');
    return 0;
}

static bool parse_ini_double_text(const wchar_t *text, double *outValue)
{
    const wchar_t *p = text;
    double value = 0.0;
    double fracScale = 0.1;
    int sign = 1;
    bool sawDigit = false;

    if (!text || !outValue) {
        return false;
    }

    while (is_wspace_simple(*p)) {
        ++p;
    }

    if (*p == L'+') {
        ++p;
    } else if (*p == L'-') {
        sign = -1;
        ++p;
    }

    while (is_wdigit_simple(*p)) {
        value = (value * 10.0) + (double)(*p - L'0');
        sawDigit = true;
        ++p;
    }

    if (*p == L'.' || *p == L',') {
        ++p;
        while (is_wdigit_simple(*p)) {
            value += ((double)(*p - L'0')) * fracScale;
            fracScale *= 0.1;
            sawDigit = true;
            ++p;
        }
    }

    while (is_wspace_simple(*p)) {
        ++p;
    }

    if (!sawDigit || *p != L'\0') {
        return false;
    }

    *outValue = value * (double)sign;
    return true;
}

static bool parse_ini_hex_color_text(const wchar_t *text, COLORREF *outColor)
{
    const wchar_t *p = text;
    unsigned int value = 0;
    int digits = 0;

    if (!text || !outColor) {
        return false;
    }

    while (is_wspace_simple(*p)) {
        ++p;
    }

    while (is_hex_digit_simple(*p) && digits < 6) {
        value = (value * 16U) + (unsigned int)hex_digit_value(*p);
        ++digits;
        ++p;
    }

    while (is_wspace_simple(*p)) {
        ++p;
    }

    if (digits != 6 || *p != L'\0') {
        return false;
    }

    *outColor = RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return true;
}

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

    for (size_t i = (size_t)len; i > 0; --i) {
        if (path[i - 1] == L'\\' || path[i - 1] == L'/') {
            lstrcpynW(path + i, L"hyperplayer.ini", (int)(pathCount - i));
            return;
        }
    }

    lstrcpynW(path, L"hyperplayer.ini", (int)pathCount);
}

static int normalize_fft_size(int value)
{
    int normalized = 64;

    if (value < 64) {
        return 64;
    }
    if (value > 8192) {
        value = 8192;
    }

    while (normalized < value && normalized < 8192) {
        normalized <<= 1;
    }

    return normalized;
}

static double read_ini_double(const wchar_t *iniPath, const wchar_t *section, const wchar_t *key, double defaultValue)
{
    wchar_t text[64];
    double value;

    GetPrivateProfileStringW(section, key, L"", text, (DWORD)(sizeof(text) / sizeof(text[0])), iniPath);
    if (text[0] == L'\0') {
        return defaultValue;
    }

    if (parse_ini_double_text(text, &value)) {
        return value;
    }

    return defaultValue;
}

static COLORREF read_ini_hex_color(const wchar_t *iniPath, const wchar_t *section, const wchar_t *key, COLORREF defaultValue)
{
    wchar_t text[64];
    COLORREF value;

    GetPrivateProfileStringW(section, key, L"", text, (DWORD)(sizeof(text) / sizeof(text[0])), iniPath);
    if (text[0] == L'\0') {
        return defaultValue;
    }

    if (parse_ini_hex_color_text(text, &value)) {
        return value;
    }

    return defaultValue;
}

static void load_config_from_ini(void)
{
    wchar_t iniPath[MAX_PATH];
    SpectrumAnalyzerConfig cfg = g_cfg;

    if (g_configLoaded) {
        return;
    }

    build_ini_path(iniPath, sizeof(iniPath) / sizeof(iniPath[0]));

    cfg.fftSize = GetPrivateProfileIntW(L"SPECTRUMANALYZER", L"FFT_SIZE", cfg.fftSize, iniPath);
    cfg.bandCount = GetPrivateProfileIntW(L"SPECTRUMANALYZER", L"BAND_COUNT", cfg.bandCount, iniPath);
    cfg.minHz = read_ini_double(iniPath, L"SPECTRUMANALYZER", L"MIN_HZ", cfg.minHz);
    cfg.maxHz = read_ini_double(iniPath, L"SPECTRUMANALYZER", L"MAX_HZ", cfg.maxHz);
    cfg.barW = GetPrivateProfileIntW(L"SPECTRUMANALYZER", L"BAR_W", cfg.barW, iniPath);
    cfg.gapX = GetPrivateProfileIntW(L"SPECTRUMANALYZER", L"GAP_X", cfg.gapX, iniPath);
    cfg.slatH = GetPrivateProfileIntW(L"SPECTRUMANALYZER", L"SLAT_H", cfg.slatH, iniPath);
    cfg.slatGap = GetPrivateProfileIntW(L"SPECTRUMANALYZER", L"SLAT_GAP", cfg.slatGap, iniPath);

    cfg.barColor = read_ini_hex_color(iniPath, L"SPECTRUMANALYZER", L"BAR_COLOR", cfg.barColor);

    cfg.fftSize = normalize_fft_size(cfg.fftSize);

    if (cfg.bandCount < 2) {
        cfg.bandCount = 2;
    }
    if (cfg.bandCount > 256) {
        cfg.bandCount = 256;
    }
    if (cfg.minHz < 1.0) {
        cfg.minHz = 1.0;
    }
    if (cfg.maxHz <= cfg.minHz) {
        cfg.minHz = 80.0;
        cfg.maxHz = 11000.0;
    }
    if (cfg.barW < 1) {
        cfg.barW = 1;
    }
    if (cfg.gapX < 0) {
        cfg.gapX = 0;
    }
    if (cfg.slatH < 1) {
        cfg.slatH = 1;
    }
    if (cfg.slatGap < 0) {
        cfg.slatGap = 0;
    }

    g_cfg = cfg;
    g_configLoaded = true;
}

static void release_tables(void)
{
    free(g_window);
    free(g_real);
    free(g_imag);
    free(g_mags);
    free(g_mono);
    free(g_bars);
    free(g_bandEdges);
    free(g_bandEQ);

    g_window = NULL;
    g_real = NULL;
    g_imag = NULL;
    g_mags = NULL;
    g_mono = NULL;
    g_bars = NULL;
    g_bandEdges = NULL;
    g_bandEQ = NULL;
}

static bool allocate_tables(void)
{
    size_t fftCount;
    size_t halfCount;
    size_t bandCount;

    fftCount = (size_t)g_cfg.fftSize;
    halfCount = (size_t)(g_cfg.fftSize / 2);
    bandCount = (size_t)g_cfg.bandCount;

    g_window = (double *)malloc(sizeof(double) * fftCount);
    g_real = (double *)malloc(sizeof(double) * fftCount);
    g_imag = (double *)malloc(sizeof(double) * fftCount);
    g_mags = (double *)malloc(sizeof(double) * halfCount);
    g_mono = (float *)malloc(sizeof(float) * fftCount);
    g_bars = (float *)calloc(bandCount, sizeof(float));
    g_bandEdges = (int *)malloc(sizeof(int) * (bandCount + 1));
    g_bandEQ = (double *)malloc(sizeof(double) * bandCount);

    if (!g_window || !g_real || !g_imag || !g_mags || !g_mono || !g_bars || !g_bandEdges || !g_bandEQ) {
        release_tables();
        return false;
    }

    return true;
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
    int bin = (int)floor(((hz / (double)g_sampleRate) * (double)g_cfg.fftSize) + 0.5);
    int half = g_cfg.fftSize / 2;

    if (bin < 1) {
        bin = 1;
    }
    if (bin > half) {
        bin = half;
    }

    return bin;
}

static void init_tables(void)
{
    double logMin;
    double logMax;

    if (g_initialized) {
        return;
    }

    load_config_from_ini();

    if (!allocate_tables()) {
        return;
    }

    for (int n = 0; n < g_cfg.fftSize; ++n) {
        g_window[n] = 0.5 * (1.0 - cos((2.0 * 3.14159265358979323846 * (double)n) / (double)(g_cfg.fftSize - 1)));
    }

    logMin = log(g_cfg.minHz);
    logMax = log(g_cfg.maxHz);

    for (int i = 0; i <= g_cfg.bandCount; ++i) {
        double t = (double)i / (double)g_cfg.bandCount;
        double hz = exp(logMin + ((logMax - logMin) * t));
        g_bandEdges[i] = hz_to_bin(hz);
    }

    for (int i = 0; i < g_cfg.bandCount; ++i) {
        double t = (double)i / (double)(g_cfg.bandCount - 1);
        g_bandEQ[i] = lerp(0.24, 0.50, t);
    }

    g_initialized = true;
}

void spectrumanalyzer_update(AppState *app, double dt)
{
    int half;
    int gotWindow;

    (void)dt;

    init_tables();
    if (!g_initialized || !g_bars || !g_window || !g_real || !g_imag || !g_mags || !g_mono || !g_bandEdges || !g_bandEQ) {
        return;
    }

    gotWindow = player_get_recent_mono_window(app, g_mono, g_cfg.fftSize);
    if (!gotWindow) {
        return;
    }

    for (int i = 0; i < g_cfg.fftSize; ++i) {
        g_real[i] = (double)g_mono[i] * g_window[i];
        g_imag[i] = 0.0;
    }

    fft_inplace(g_real, g_imag, g_cfg.fftSize);

    half = g_cfg.fftSize / 2;
    for (int k = 0; k < half; ++k) {
        double r = g_real[k];
        double im = g_imag[k];
        g_mags[k] = sqrt((r * r) + (im * im)) + 1e-12;
    }

    for (int i = 0; i < g_cfg.bandCount; ++i) {
        int a = g_bandEdges[i];
        int b = g_bandEdges[i + 1] - 1;
        double peak = 0.0;
        double sum = 0.0;
        int count = 0;
        double avg;
        double raw;
        float prev;

        if (b < a) {
            b = a;
        }
        if (b > half) {
            b = half;
        }

        for (int k = a; k <= b; ++k) {
            double m = g_mags[k - 1];
            sum += m;
            count++;
            if (m > peak) {
                peak = m;
            }
        }

        avg = (count > 0) ? (sum / (double)count) : 0.0;
        raw = (peak * 0.65) + (avg * 0.35);

        raw = raw * g_bandEQ[i];
        raw = log(1.0 + (raw * 3.0)) / log(4.0);
        raw = clamp01(raw * 0.38);

        prev = g_bars[i];
        if ((float)raw > prev) {
            g_bars[i] = (float)lerp(prev, raw, 0.68);
        } else {
            g_bars[i] = (float)lerp(prev, raw, 0.24);
        }
    }
}

void spectrumanalyzer_draw(AppState *app, HDC hdc)
{
    const int bottom = BOX_Y + BOX_H;
    const int maxH = BOX_H;
    const int barStep = g_cfg.barW + g_cfg.gapX;
    const int stepY = g_cfg.slatH + g_cfg.slatGap;
    HBRUSH brush;
    HBRUSH oldBrush;
    HPEN oldPen;
    HPEN nullPen;
    RECT r;

    (void)app;

    if (!hdc) {
        return;
    }

    init_tables();
    if (!g_initialized || !g_bars) {
        return;
    }

    brush = CreateSolidBrush(g_cfg.barColor);
    nullPen = (HPEN)GetStockObject(NULL_PEN);
    oldPen = (HPEN)SelectObject(hdc, nullPen);
    oldBrush = (HBRUSH)SelectObject(hdc, brush);

    for (int i = 0; i < g_cfg.bandCount; ++i) {
        int h = (int)floor((double)g_bars[i] * (double)maxH + 0.5);

        if (h > 0) {
            int x = BOX_X + (i * barStep);
            int drawn = 0;

            while (drawn < h) {
                int piece = g_cfg.slatH;
                int y;

                if (drawn + piece > h) {
                    piece = h - drawn;
                }

                y = bottom - drawn - piece;

                r.left = x;
                r.top = y;
                r.right = x + g_cfg.barW;
                r.bottom = y + piece;
                FillRect(hdc, &r, brush);

                drawn += stepY;
            }
        }
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
}