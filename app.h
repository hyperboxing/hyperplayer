#ifndef APP_H
#define APP_H

#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct AppConfig {
    wchar_t defaultDir[MAX_PATH];
    int stereoSeparation;
} AppConfig;

typedef struct ImageRGBA {
    UINT width;
    UINT height;
    unsigned char *pixels;
} ImageRGBA;

typedef struct FontSet {
    HFONT pattern;
    HFONT sampleList;
    HFONT info;
    HFONT info2;
    HFONT dir;
    HFONT driveButtons;
    HFONT waveform;
    HFONT title;
} FontSet;

typedef struct DirectoryEntry {
    wchar_t name[260];
    wchar_t fullPath[MAX_PATH];
    bool isDir;
    bool isParent;
    unsigned long long size;
} DirectoryEntry;

typedef struct DirectoryListing {
    wchar_t currentPath[MAX_PATH];
    DirectoryEntry *entries;
    int entryCount;
    int entryCapacity;
    int scroll;
    int selectedIndex;
} DirectoryListing;

typedef struct PlayerState PlayerState;

typedef struct AppState {
    HINSTANCE instance;
    HWND hwnd;

    wchar_t exeDir[MAX_PATH];
    wchar_t iniPath[MAX_PATH];
    wchar_t backgroundPath[MAX_PATH];
    wchar_t fontPath[MAX_PATH];
    wchar_t statusText[2048];

    wchar_t currentSelectedFile[MAX_PATH];
    wchar_t currentSelectedName[260];

    int mouseX;
    int mouseY;
    int selectedSampleIndex;
    int sampleDisplayCurrentSlot;
    double sampleDisplayTimer;

    float sampleHighlightAlpha[31];

    int pendingWheelDelta;

    ULONGLONG lastUpdateTick;

    bool comInitialized;
    bool backgroundLoaded;
    bool privateFontLoaded;
    bool showFileBrowser;

    HANDLE privateFontHandle;

    ImageRGBA background;
    FontSet fonts;
    DirectoryListing directory;
    PlayerState *player;
    AppConfig config;
} AppState;

void app_set_status(AppState *app, const wchar_t *fmt, ...);
bool app_init(AppState *app);
void app_shutdown(AppState *app);

const wchar_t *app_get_ini_path(const AppState *app);
bool app_ini_get_string(const AppState *app, const wchar_t *section, const wchar_t *key, const wchar_t *defaultValue, wchar_t *dst, size_t dstCount);
int app_ini_get_int(const AppState *app, const wchar_t *section, const wchar_t *key, int defaultValue);
double app_ini_get_double(const AppState *app, const wchar_t *section, const wchar_t *key, double defaultValue);
COLORREF app_ini_get_color(const AppState *app, const wchar_t *section, const wchar_t *key, COLORREF defaultValue);

bool app_load_resource_bytes(WORD resourceId, const void **outData, DWORD *outSize);
bool app_ensure_default_ini_exists(void);

#endif
