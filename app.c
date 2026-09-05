#include "app.h"
#include "ui.h"
#include "directory_listing_win32.h"
#include "player.h"
#include "resource.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static void app_copy_wstr(wchar_t *dst, size_t dstCount, const wchar_t *src)
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

static void app_join_path(wchar_t *dst, size_t dstCount, const wchar_t *dir, const wchar_t *name)
{
    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = L'\0';

    if (!dir || !name) {
        return;
    }

    _snwprintf(dst, dstCount - 1, L"%ls\\%ls", dir, name);
    dst[dstCount - 1] = L'\0';
}

static void app_get_exe_dir(wchar_t *dst, size_t dstCount)
{
    wchar_t fullPath[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, fullPath, MAX_PATH);

    if (len == 0 || len >= MAX_PATH) {
        app_copy_wstr(dst, dstCount, L".");
        return;
    }

    fullPath[len] = L'\0';

    for (int i = (int)len - 1; i >= 0; --i) {
        if (fullPath[i] == L'\\' || fullPath[i] == L'/') {
            fullPath[i] = L'\0';
            break;
        }
    }

    app_copy_wstr(dst, dstCount, fullPath);
}

static void app_trim_wstr_in_place(wchar_t *text)
{
    size_t len;
    size_t start = 0;
    size_t end;

    if (!text) {
        return;
    }

    len = wcslen(text);
    while (start < len && iswspace(text[start])) {
        start++;
    }

    end = len;
    while (end > start && iswspace(text[end - 1])) {
        end--;
    }

    if (start > 0 && end > start) {
        memmove(text, text + start, (end - start) * sizeof(wchar_t));
    } else if (start > 0 && end == start) {
        text[0] = L'\0';
        return;
    }

    text[end - start] = L'\0';
}

static void app_get_active_ini_path(const AppState *app, wchar_t *dst, size_t dstCount)
{
    wchar_t exeDir[MAX_PATH];

    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = L'\0';

    if (app && app->iniPath[0] != L'\0') {
        app_copy_wstr(dst, dstCount, app->iniPath);
        return;
    }

    app_get_exe_dir(exeDir, sizeof(exeDir) / sizeof(exeDir[0]));
    app_join_path(dst, dstCount, exeDir, L"hyperplayer.ini");
}

static bool app_parse_hex_color_text(const wchar_t *text, COLORREF *outColor)
{
    wchar_t cleaned[32];
    size_t di = 0;
    unsigned long value;
    wchar_t *endPtr = NULL;

    if (!text || !outColor) {
        return false;
    }

    while (*text && iswspace(*text)) {
        text++;
    }

    if (*text == L'#') {
        text++;
    } else if (text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        text += 2;
    }

    while (*text && di + 1 < (sizeof(cleaned) / sizeof(cleaned[0]))) {
        if (*text == L';') {
            break;
        }

        if (!iswspace(*text)) {
            cleaned[di++] = *text;
        }
        text++;
    }

    cleaned[di] = L'\0';

    if (di != 6) {
        return false;
    }

    value = wcstoul(cleaned, &endPtr, 16);
    if (!endPtr || *endPtr != L'\0') {
        return false;
    }

    *outColor = RGB(
        (int)((value >> 16) & 0xFF),
        (int)((value >> 8) & 0xFF),
        (int)(value & 0xFF)
    );
    return true;
}

static void app_color_to_hex_text(COLORREF color, wchar_t *dst, size_t dstCount)
{
    if (!dst || dstCount == 0) {
        return;
    }

    _snwprintf(
        dst,
        dstCount - 1,
        L"%02X%02X%02X",
        (unsigned int)GetRValue(color),
        (unsigned int)GetGValue(color),
        (unsigned int)GetBValue(color)
    );
    dst[dstCount - 1] = L'\0';
}

static const char g_defaultIniFileContents[] =
    "; \r\n"
    "; Config file for Hyperplayer\r\n"
    "; Deleting this file will make the program re-create it with default settings on start.\r\n"
    "; \r\n"
    "\r\n"
    "[SYSTEM]\r\n"
    "; DEFAULTDIR = Default directory shown for modules when starting the program, if \".\" then use the same directory as the exe.\r\n"
    ";\r\n"
    "DEFAULTDIR=.\r\n"
    "\r\n"
    "[AUDIO]\r\n"
    "; STEREOSEPARATION = Controls how far apart the channels are placed in the stereo audio.\r\n"
    ";                    In ProTracker style panning, channels 1 and 4 are sent to the left speaker, and channels 2 and 3 to the right speaker.\r\n"
    ";                    Higher values make the left/right split wider, while lower values make the sound more centered. 0-100\r\n"
    ";\r\n"
    "STEREOSEPARATION=33\r\n"
    "\r\n"
    "[SAMPLELIST]\r\n"
    "; BGTRANSPARENCY = Controls how much transparency each bar will have when filled, when an instrument/sample is played. 0-100\r\n"
    "; BGFADE = Controls how fast the filled bar fades back to none. 0.01 to 1.00\r\n"
    "; \r\n"
    "BGTRANSPARENCY=45\r\n"
    "BGFADE=0.95\r\n"
    "\r\n"
    "[PATTERN]\r\n"
    "; TEXTCOLOR1 = Default pattern text color\r\n"
    "; TEXTCOLOR2 = Text color for the first line of each new pattern\r\n"
    "; \r\n"
    "TEXTCOLOR1=3648FF\r\n"
    "TEXTCOLOR2=7196FF\r\n"
    "\r\n"
    "[QUADRASCOPE]\r\n"
    "; QUADRACOLOR = Controls the color of the waveform in the quadrascope oscillators\r\n"
    ";\r\n"
    "QUADRACOLOR=FFDD00\r\n"
    "\r\n"
    "[VUMETER]\r\n"
    "; VUCOLOR1 to VUCOLOR3 = Controls the 3 colors, default is GREEN -> YELLOW -> RED\r\n"
    "; VUTRANSPARENCY = Controls how transparent the VU-Meter is against the gray background.\r\n"
    ";                  Higher values make the VU-Meter more visible and solid. Lower values make it more transparent and faded. 0-100\r\n"
    "; \r\n"
    "VUCOLOR1=00FF00\r\n"
    "VUCOLOR2=FFFF00\r\n"
    "VUCOLOR3=FF0000\r\n"
    "VUTRANSPARENCY=45\r\n"
    "\r\n"
    "[SAMPLEVIEW]\r\n"
    "; SAMPLECOLOR = Controls the color of the sample waveform view\r\n"
    "; \r\n"
    "SAMPLECOLOR=FFDD00\r\n"
    "\r\n"
    "[SPECTRUMANALYZER]\r\n"
    "; FFT_SIZE = How many audio samples are analyzed at once for the spectrum. Higher values give more precise frequency separation, but slower and less immediate visual response.\r\n"
    ";            Lower values react faster, but frequency accuracy becomes worse.\r\n"
    "; BAND_COUNT = How many vertical bars the spectrum analyzer has. More bands means more detail across the frequency range. Fewer bands means a simpler, chunkier display.\r\n"
    "; MIN_HZ = The lowest frequency the analyzer starts at. Frequencies below this are ignored for the bars. Raising it removes more deep bass from the display.\r\n"
    "; MAX_HZ = The highest frequency the analyzer ends at. Frequencies above this are ignored for the bars. Lowering it focuses the display more on the musically useful range.\r\n"
    "; BAR_W = The width of each spectrum bar in pixels.\r\n"
    "; GAP_X = The horizontal gap in pixels between each bar.\r\n"
    "; SLAT_H = The height of each small rectangle segment inside a bar.\r\n"
    "; SLAT_GAP = The vertical gap in pixels between slats. Bigger gap gives more of a \"window blinds\" look. Smaller gap makes the bar look more solid.\r\n"
    "; \r\n"
    "FFT_SIZE=1024\r\n"
    "BAND_COUNT=37\r\n"
    "MIN_HZ=80.0\r\n"
    "MAX_HZ=11000.0\r\n"
    "BAR_W=14\r\n"
    "GAP_X=1\r\n"
    "SLAT_H=6\r\n"
    "SLAT_GAP=2\r\n"
    "\r\n"
    "[VISUALIZER]\r\n"
    "; VIS_ROTATION_SPEED       = How fast the whole thing rotates\r\n"
    "; VIS_BAR_DECLINE_SPEED    = How fast each bar falls back down\r\n"
    "; VIS_BAR_LENGTH_SCALE     = Overall bar length multiplier\r\n"
    "; VIS_GLOBAL_GLOW_STRENGTH = Bloom strength on the glow\r\n"
    "; VIS_HIGH_HZ_RESPONSE     = Boosts upper frequencies progressively\r\n"
    "; VIS_HIGH_HZ_LENGTH_BONUS = Extra progressive bar length for high frequencies\r\n"
    "; VIS_STARFIELD_COUNT      = Number of background stars\r\n"
    "; VIS_STARFIELD_SPEED      = Fixed starfield speed\r\n"
    "; VIS_STARFIELD_BRIGHTNESS = Overall star brightness\r\n"
    "; VIS_BASS_ONLY_BAND_COUNT = First N radial bars are bass-only gated\r\n"
    "; VIS_BASS_ONLY_DOMINANCE  = Bass/non-bass ratio needed before low bars open\r\n"
    "; VIS_BASS_ONLY_GATE_SCALE = Lower = easier low-bar opening, higher = stricter\r\n"
    "; VIS_BASS_ONLY_ABS_LEVEL  = Absolute bass level needed to help open low bars\r\n"
    "; VIS_AGC_IGNORE_LOW_BANDS = First N bands ignored by global auto gain control\r\n"
    "; VIS_HIGH_PRESENCE_FLOOR  = Minimum preserved high-band presence after AGC\r\n"
    "; \r\n"
    "VIS_ROTATION_SPEED=2.00\r\n"
    "VIS_BAR_DECLINE_SPEED=3.60\r\n"
    "VIS_BAR_LENGTH_SCALE=1.30\r\n"
    "VIS_GLOBAL_GLOW_STRENGTH=0.90\r\n"
    "VIS_HIGH_HZ_RESPONSE=2.00\r\n"
    "VIS_HIGH_HZ_LENGTH_BONUS=1.63\r\n"
    "VIS_STARFIELD_COUNT=5000\r\n"
    "VIS_STARFIELD_SPEED=0.36\r\n"
    "VIS_STARFIELD_BRIGHTNESS=1.00\r\n"
    "VIS_BASS_ONLY_BAND_COUNT=20\r\n"
    "VIS_BASS_ONLY_DOMINANCE=1.10\r\n"
    "VIS_BASS_ONLY_GATE_SCALE=0.85\r\n"
    "VIS_BASS_ONLY_ABS_LEVEL=0.018\r\n"
    "VIS_AGC_IGNORE_LOW_BANDS=20\r\n"
    "VIS_HIGH_PRESENCE_FLOOR=0.90\r\n"
    "\r\n"
    "\r\n"
    "\r\n";

bool app_load_resource_bytes(WORD resourceId, const void **outData, DWORD *outSize)
{
    HRSRC resourceHandle;
    HGLOBAL loadedResource;
    const void *resourceData;
    DWORD resourceSize;

    if (!outData || !outSize) {
        return false;
    }

    *outData = NULL;
    *outSize = 0;

    resourceHandle = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resourceHandle) {
        return false;
    }

    resourceSize = SizeofResource(NULL, resourceHandle);
    if (resourceSize == 0) {
        return false;
    }

    loadedResource = LoadResource(NULL, resourceHandle);
    if (!loadedResource) {
        return false;
    }

    resourceData = LockResource(loadedResource);
    if (!resourceData) {
        return false;
    }

    *outData = resourceData;
    *outSize = resourceSize;
    return true;
}

static bool app_write_file_bytes(const wchar_t *path, const void *data, DWORD size)
{
    HANDLE fileHandle;
    DWORD written = 0;

    if (!path || !data || size == 0) {
        return false;
    }

    fileHandle = CreateFileW(
        path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (!WriteFile(fileHandle, data, size, &written, NULL) || written != size) {
        CloseHandle(fileHandle);
        DeleteFileW(path);
        return false;
    }

    CloseHandle(fileHandle);
    return true;
}

static bool app_file_exists(const wchar_t *path)
{
    DWORD attributes;

    if (!path || path[0] == L'\0') {
        return false;
    }

    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    return ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

bool app_ensure_default_ini_exists(void)
{
    wchar_t exeDir[MAX_PATH];
    wchar_t iniPath[MAX_PATH];

    app_get_exe_dir(exeDir, sizeof(exeDir) / sizeof(exeDir[0]));
    app_join_path(iniPath, sizeof(iniPath) / sizeof(iniPath[0]), exeDir, L"hyperplayer.ini");

    if (app_file_exists(iniPath)) {
        return true;
    }

    return app_write_file_bytes(
        iniPath,
        g_defaultIniFileContents,
        (DWORD)(sizeof(g_defaultIniFileContents) - 1)
    );
}

const wchar_t *app_get_ini_path(const AppState *app)
{
    if (!app) {
        return L"";
    }

    return app->iniPath;
}

bool app_ini_get_string(const AppState *app, const wchar_t *section, const wchar_t *key, const wchar_t *defaultValue, wchar_t *dst, size_t dstCount)
{
    wchar_t iniPath[MAX_PATH];
    DWORD readCount;

    if (!dst || dstCount == 0) {
        return false;
    }

    dst[0] = L'\0';

    if (!section || !key) {
        if (defaultValue) {
            app_copy_wstr(dst, dstCount, defaultValue);
            app_trim_wstr_in_place(dst);
        }
        return false;
    }

    app_get_active_ini_path(app, iniPath, sizeof(iniPath) / sizeof(iniPath[0]));
    if (iniPath[0] == L'\0') {
        if (defaultValue) {
            app_copy_wstr(dst, dstCount, defaultValue);
            app_trim_wstr_in_place(dst);
        }
        return false;
    }

    readCount = GetPrivateProfileStringW(
        section,
        key,
        defaultValue ? defaultValue : L"",
        dst,
        (DWORD)dstCount,
        iniPath
    );
    (void)readCount;

    dst[dstCount - 1] = L'\0';
    app_trim_wstr_in_place(dst);
    return (dst[0] != L'\0');
}

int app_ini_get_int(const AppState *app, const wchar_t *section, const wchar_t *key, int defaultValue)
{
    wchar_t text[64];
    wchar_t *endPtr = NULL;
    long value;

    _snwprintf(text, (sizeof(text) / sizeof(text[0])) - 1, L"%d", defaultValue);
    text[(sizeof(text) / sizeof(text[0])) - 1] = L'\0';

    app_ini_get_string(app, section, key, text, text, sizeof(text) / sizeof(text[0]));
    value = wcstol(text, &endPtr, 10);

    if (endPtr == text) {
        return defaultValue;
    }

    while (endPtr && *endPtr) {
        if (!iswspace(*endPtr)) {
            return defaultValue;
        }
        endPtr++;
    }

    return (int)value;
}

double app_ini_get_double(const AppState *app, const wchar_t *section, const wchar_t *key, double defaultValue)
{
    wchar_t text[64];
    wchar_t *endPtr = NULL;
    double value;

    _snwprintf(text, (sizeof(text) / sizeof(text[0])) - 1, L"%.15g", defaultValue);
    text[(sizeof(text) / sizeof(text[0])) - 1] = L'\0';

    app_ini_get_string(app, section, key, text, text, sizeof(text) / sizeof(text[0]));
    value = wcstod(text, &endPtr);

    if (endPtr == text) {
        return defaultValue;
    }

    while (endPtr && *endPtr) {
        if (!iswspace(*endPtr)) {
            return defaultValue;
        }
        endPtr++;
    }

    return value;
}

COLORREF app_ini_get_color(const AppState *app, const wchar_t *section, const wchar_t *key, COLORREF defaultValue)
{
    wchar_t text[64];
    COLORREF color;

    app_color_to_hex_text(defaultValue, text, sizeof(text) / sizeof(text[0]));
    app_ini_get_string(app, section, key, text, text, sizeof(text) / sizeof(text[0]));

    if (!app_parse_hex_color_text(text, &color)) {
        return defaultValue;
    }

    return color;
}

static void app_load_config(AppState *app)
{
    if (!app) {
        return;
    }

    app_ini_get_string(
        app,
        L"SYSTEM",
        L"DEFAULTDIR",
        L".",
        app->config.defaultDir,
        sizeof(app->config.defaultDir) / sizeof(app->config.defaultDir[0])
    );

    if (app->config.defaultDir[0] == L'\0') {
        app_copy_wstr(
            app->config.defaultDir,
            sizeof(app->config.defaultDir) / sizeof(app->config.defaultDir[0]),
            L"."
        );
    }

    app->config.stereoSeparation = app_ini_get_int(app, L"AUDIO", L"STEREOSEPARATION", 33);

    if (app->config.stereoSeparation < 0) {
        app->config.stereoSeparation = 0;
    }
    if (app->config.stereoSeparation > 100) {
        app->config.stereoSeparation = 100;
    }
}

void app_set_status(AppState *app, const wchar_t *fmt, ...)
{
    va_list args;

    if (!app || !fmt) {
        return;
    }

    va_start(args, fmt);
    vswprintf(app->statusText, sizeof(app->statusText) / sizeof(app->statusText[0]), fmt, args);
    va_end(args);

    app->statusText[(sizeof(app->statusText) / sizeof(app->statusText[0])) - 1] = L'\0';
}

static bool app_path_is_absolute(const wchar_t *path)
{
    if (!path || path[0] == L'\0') {
        return false;
    }

    if (path[0] == L'\\' && path[1] == L'\\') {
        return true;
    }

    if (path[0] == L'/' || path[0] == L'\\') {
        return true;
    }

    if (path[0] && path[1] == L':') {
        return true;
    }

    return false;
}

static void app_resolve_default_dir(const AppState *app, wchar_t *dst, size_t dstCount)
{
    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = L'\0';

    if (!app) {
        return;
    }

    if (app->config.defaultDir[0] == L'\0' ||
        (app->config.defaultDir[0] == L'.' && app->config.defaultDir[1] == L'\0')) {
        app_copy_wstr(dst, dstCount, app->exeDir);
        return;
    }

    if (app_path_is_absolute(app->config.defaultDir)) {
        app_copy_wstr(dst, dstCount, app->config.defaultDir);
        return;
    }

    app_join_path(dst, dstCount, app->exeDir, app->config.defaultDir);
}

bool app_init(AppState *app)
{
    HRESULT hr;
    bool assetsOk;
    bool dirOk;
    wchar_t defaultDir[MAX_PATH];

    if (!app) {
        return false;
    }

    app_get_exe_dir(app->exeDir, sizeof(app->exeDir) / sizeof(app->exeDir[0]));
    app_join_path(app->iniPath, sizeof(app->iniPath) / sizeof(app->iniPath[0]), app->exeDir, L"hyperplayer.ini");
    app_join_path(app->backgroundPath, sizeof(app->backgroundPath) / sizeof(app->backgroundPath[0]), app->exeDir, L"background.png");
    app_join_path(app->fontPath, sizeof(app->fontPath) / sizeof(app->fontPath[0]), app->exeDir, L"protracker.ttf");
    app->privateFontHandle = NULL;

    app->directory.selectedIndex = -1;
    app->currentSelectedFile[0] = L'\0';
    app->currentSelectedName[0] = L'\0';
    app->selectedSampleIndex = 0;
    app->sampleDisplayCurrentSlot = 0;
    app->sampleDisplayTimer = 0.0;
    app->pendingWheelDelta = 0;
    app->lastUpdateTick = GetTickCount64();
    app->showFileBrowser = true;

    for (int i = 0; i < 31; ++i) {
        app->sampleHighlightAlpha[i] = 0.0f;
    }

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        app->comInitialized = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        app->comInitialized = false;
    } else {
        app_set_status(app, L"COM init failed: 0x%08lX", (unsigned long)hr);
        return false;
    }

    app_load_config(app);

    assetsOk = ui_load_assets(app);
    player_init(app);
    app_resolve_default_dir(app, defaultDir, sizeof(defaultDir) / sizeof(defaultDir[0]));
    dirOk = directory_listing_init(app, defaultDir);

    if (!dirOk) {
        app_set_status(app, L"Stage 3 ready, but directory listing init failed for %ls", defaultDir);
        return true;
    }

    if (!player_is_available(app)) {
        app_set_status(app, L"ProTracker replay engine failed to initialize.");
        return true;
    }

    if (assetsOk && app->backgroundLoaded && app->privateFontLoaded) {
        app_set_status(app, L"Ready.");
    } else {
        app_set_status(app, L"Ready.");
    }

    return true;
}

void app_shutdown(AppState *app)
{
    if (!app) {
        return;
    }

    directory_listing_shutdown(app);
    player_shutdown(app);
    ui_release_assets(app);
    if (app->comInitialized) {
        CoUninitialize();
        app->comInitialized = false;
    }
}
