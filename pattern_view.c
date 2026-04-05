#include "pattern_view.h"
#include "player.h"

#include <windows.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>

#define PATTERN_VISIBLE_ROWS 64

typedef struct PatternRow {
    wchar_t rowText[8];
    wchar_t cells[4][16];
} PatternRow;

typedef struct PatternCache {
    bool valid;
    PatternRow rows[PATTERN_VISIBLE_ROWS];
    int highlightRow;

    wchar_t keyFile[MAX_PATH];
    int keyOrder;
    int keyPattern;
    int keyRow;
    bool keyStopped;
    bool keyLoopEnabled;
    bool keyLocked;
} PatternCache;

typedef struct ScrollState {
    bool locked;
    wchar_t lastFile[MAX_PATH];
    bool lastStopped;
} ScrollState;

static const int g_lineXs[4] = { 73, 245, 417, 589 };
static PatternCache g_cache = { 0 };
static ScrollState g_scroll = { false, L"", true };

static const wchar_t DEFAULT_TEXTCOLOR1[] = L"3648FF";
static const wchar_t DEFAULT_TEXTCOLOR2[] = L"7196FF";
static const COLORREF COLOR_WHITE = RGB(0xFF, 0xFF, 0xFF);

static void copy_wstr(wchar_t *dst, size_t dstCount, const wchar_t *src)
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

static int hex_nibble(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9') {
        return (int)(ch - L'0');
    }
    if (ch >= L'a' && ch <= L'f') {
        return 10 + (int)(ch - L'a');
    }
    if (ch >= L'A' && ch <= L'F') {
        return 10 + (int)(ch - L'A');
    }
    return -1;
}

static void trim_wstr_in_place(wchar_t *text)
{
    size_t len;
    size_t start = 0;
    size_t end;

    if (!text) {
        return;
    }

    len = wcslen(text);
    while (start < len && (text[start] == L' ' || text[start] == L'\t' || text[start] == L'\r' || text[start] == L'\n')) {
        start++;
    }

    end = len;
    while (end > start && (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r' || text[end - 1] == L'\n')) {
        end--;
    }

    if (start > 0) {
        memmove(text, text + start, (end - start) * sizeof(wchar_t));
    }

    text[end - start] = L'\0';
}

static COLORREF color_from_web_hex(const wchar_t *hexText, COLORREF fallback)
{
    int r1, r2, g1, g2, b1, b2;
    int r, g, b;
    wchar_t cleaned[16];

    if (!hexText) {
        return fallback;
    }

    copy_wstr(cleaned, sizeof(cleaned) / sizeof(cleaned[0]), hexText);
    trim_wstr_in_place(cleaned);

    if (wcslen(cleaned) != 6) {
        return fallback;
    }

    r1 = hex_nibble(cleaned[0]);
    r2 = hex_nibble(cleaned[1]);
    g1 = hex_nibble(cleaned[2]);
    g2 = hex_nibble(cleaned[3]);
    b1 = hex_nibble(cleaned[4]);
    b2 = hex_nibble(cleaned[5]);

    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
        return fallback;
    }

    r = (r1 << 4) | r2;
    g = (g1 << 4) | g2;
    b = (b1 << 4) | b2;

    return RGB(r, g, b);
}

static void get_ini_path(wchar_t *outPath, size_t outPathCount)
{
    wchar_t modulePath[MAX_PATH];
    wchar_t *lastSlash;

    if (!outPath || outPathCount == 0) {
        return;
    }

    outPath[0] = L'\0';

    if (GetModuleFileNameW(NULL, modulePath, (DWORD)(sizeof(modulePath) / sizeof(modulePath[0]))) == 0) {
        copy_wstr(outPath, outPathCount, L"hyperplayer.ini");
        return;
    }

    lastSlash = wcsrchr(modulePath, L'\\');
    if (!lastSlash) {
        copy_wstr(outPath, outPathCount, L"hyperplayer.ini");
        return;
    }

    *(lastSlash + 1) = L'\0';
    copy_wstr(outPath, outPathCount, modulePath);

    if (wcslen(outPath) + wcslen(L"hyperplayer.ini") < outPathCount) {
        wcscat(outPath, L"hyperplayer.ini");
    }
}

static void load_pattern_colors(COLORREF *outTextColor1, COLORREF *outTextColor2)
{
    wchar_t iniPath[MAX_PATH];
    wchar_t textColor1[64];
    wchar_t textColor2[64];

    if (!outTextColor1 || !outTextColor2) {
        return;
    }

    get_ini_path(iniPath, sizeof(iniPath) / sizeof(iniPath[0]));

    GetPrivateProfileStringW(
        L"PATTERN",
        L"TEXTCOLOR1",
        DEFAULT_TEXTCOLOR1,
        textColor1,
        (DWORD)(sizeof(textColor1) / sizeof(textColor1[0])),
        iniPath
    );

    GetPrivateProfileStringW(
        L"PATTERN",
        L"TEXTCOLOR2",
        DEFAULT_TEXTCOLOR2,
        textColor2,
        (DWORD)(sizeof(textColor2) / sizeof(textColor2[0])),
        iniPath
    );

    *outTextColor1 = color_from_web_hex(textColor1, RGB(0x36, 0x48, 0xFF));
    *outTextColor2 = color_from_web_hex(textColor2, RGB(0x71, 0x96, 0xFF));
}

static void build_empty_rows(void)
{
    for (int i = 0; i < PATTERN_VISIBLE_ROWS; ++i) {
        swprintf(g_cache.rows[i].rowText, sizeof(g_cache.rows[i].rowText) / sizeof(g_cache.rows[i].rowText[0]), L"%02d", i);
        for (int ch = 0; ch < 4; ++ch) {
            copy_wstr(g_cache.rows[i].cells[ch], sizeof(g_cache.rows[i].cells[ch]) / sizeof(g_cache.rows[i].cells[ch][0]), L"--- 00000");
        }
    }
}

static void reset_cache_to_empty(void)
{
    g_cache.valid = true;
    g_cache.highlightRow = 1;
    g_cache.keyFile[0] = L'\0';
    g_cache.keyOrder = 0;
    g_cache.keyPattern = 0;
    g_cache.keyRow = 0;
    g_cache.keyStopped = true;
    g_cache.keyLoopEnabled = false;
    g_cache.keyLocked = false;

    g_scroll.locked = false;
    g_scroll.lastFile[0] = L'\0';
    g_scroll.lastStopped = true;

    build_empty_rows();
}

static void update_scroll_lock(const AppState *app)
{
    const wchar_t *currentFile;
    bool isStopped;

    currentFile = player_get_current_file_path(app);
    isStopped = player_is_stopped(app);

    if (wcscmp(g_scroll.lastFile, currentFile) != 0) {
        g_scroll.locked = false;
        copy_wstr(g_scroll.lastFile, sizeof(g_scroll.lastFile) / sizeof(g_scroll.lastFile[0]), currentFile);
    }

    if (isStopped && !g_scroll.lastStopped) {
        g_scroll.locked = false;
    }

    if (!isStopped && player_get_current_row(app) >= 32) {
        g_scroll.locked = true;
    }

    g_scroll.lastStopped = isStopped;
}

static int get_highlight_row(const AppState *app)
{
    int row;

    update_scroll_lock(app);

    row = player_get_current_row(app);

    if (player_is_stopped(app)) {
        if (row + 1 < 33) {
            return row + 1;
        }
        return 33;
    }

    if (g_scroll.locked) {
        return 33;
    }

    if (row + 1 < 33) {
        return row + 1;
    }
    return 33;
}

static bool resolve_song_position(const AppState *app, int rowOffset, int *outOrder, int *outPattern, int *outRow)
{
    int songLength;
    int order;
    int row;

    if (!app || !outOrder || !outPattern || !outRow || !player_is_loaded(app)) {
        return false;
    }

    songLength = player_get_num_orders(app);
    if (songLength <= 0) {
        return false;
    }

    order = player_get_current_order(app);
    row = player_get_current_row(app);

    while (rowOffset > 0) {
        int pattern = player_get_order_pattern(app, order);
        int rowCount;

        if (pattern < 0) {
            return false;
        }

        rowCount = player_get_pattern_num_rows(app, pattern);
        row++;

        if (row >= rowCount) {
            order++;

            if (order >= songLength) {
                if (player_get_loop_enabled(app)) {
                    order = 0;
                } else {
                    return false;
                }
            }

            row = 0;
        }

        rowOffset--;
    }

    while (rowOffset < 0) {
        row--;

        if (row < 0) {
            int pattern;
            int rowCount;

            order--;

            if (order < 0) {
                if (player_get_loop_enabled(app)) {
                    order = songLength - 1;
                } else {
                    return false;
                }
            }

            pattern = player_get_order_pattern(app, order);
            if (pattern < 0) {
                return false;
            }

            rowCount = player_get_pattern_num_rows(app, pattern);
            row = (rowCount > 0) ? (rowCount - 1) : 0;
        }

        rowOffset++;
    }

    *outPattern = player_get_order_pattern(app, order);
    if (*outPattern < 0) {
        return false;
    }

    *outOrder = order;
    *outRow = row;
    return true;
}

static void rebuild_cache(const AppState *app)
{
    int highlightRow;

    build_empty_rows();

    highlightRow = get_highlight_row(app);
    g_cache.highlightRow = highlightRow;

    for (int visibleRow = 1; visibleRow <= PATTERN_VISIBLE_ROWS; ++visibleRow) {
        int rowOffset = visibleRow - highlightRow;
        int order;
        int pattern;
        int row;

        if (resolve_song_position(app, rowOffset, &order, &pattern, &row)) {
            swprintf(g_cache.rows[visibleRow - 1].rowText,
                     sizeof(g_cache.rows[visibleRow - 1].rowText) / sizeof(g_cache.rows[visibleRow - 1].rowText[0]),
                     L"%02d",
                     row);

            for (int channel = 0; channel < 4; ++channel) {
                player_format_pattern_cell(
                    app,
                    pattern,
                    row,
                    channel,
                    g_cache.rows[visibleRow - 1].cells[channel],
                    sizeof(g_cache.rows[visibleRow - 1].cells[channel]) / sizeof(g_cache.rows[visibleRow - 1].cells[channel][0])
                );
            }
        }
    }

    copy_wstr(g_cache.keyFile, sizeof(g_cache.keyFile) / sizeof(g_cache.keyFile[0]), player_get_current_file_path(app));
    g_cache.keyOrder = player_get_current_order(app);
    g_cache.keyPattern = player_get_current_pattern(app);
    g_cache.keyRow = player_get_current_row(app);
    g_cache.keyStopped = player_is_stopped(app);
    g_cache.keyLoopEnabled = player_get_loop_enabled(app);
    g_cache.keyLocked = g_scroll.locked;
    g_cache.valid = true;
}

static void ensure_cache(const AppState *app)
{
    const wchar_t *currentFile;
    bool isStopped;
    bool loopEnabled;
    int order;
    int pattern;
    int row;

    if (!player_is_loaded(app)) {
        if (!g_cache.valid) {
            reset_cache_to_empty();
        }
        return;
    }

    currentFile = player_get_current_file_path(app);
    isStopped = player_is_stopped(app);
    loopEnabled = player_get_loop_enabled(app);
    order = player_get_current_order(app);
    pattern = player_get_current_pattern(app);
    row = player_get_current_row(app);

    update_scroll_lock(app);

    if (!g_cache.valid ||
        wcscmp(g_cache.keyFile, currentFile) != 0 ||
        g_cache.keyOrder != order ||
        g_cache.keyPattern != pattern ||
        g_cache.keyRow != row ||
        g_cache.keyStopped != isStopped ||
        g_cache.keyLoopEnabled != loopEnabled ||
        g_cache.keyLocked != g_scroll.locked) {
        rebuild_cache(app);
    }
}

void pattern_view_draw(AppState *app, HDC hdc)
{
    HFONT oldFont;
    int savedDc;
    bool canHighlight;
    COLORREF textColor1;
    COLORREF textColor2;

    if (!app || !hdc || !app->fonts.pattern) {
        return;
    }

    ensure_cache(app);
    load_pattern_colors(&textColor1, &textColor2);

    savedDc = SaveDC(hdc);
    oldFont = (HFONT)SelectObject(hdc, app->fonts.pattern);
    SetBkMode(hdc, TRANSPARENT);

    canHighlight = player_is_loaded(app) && !player_is_stopped(app);

    for (int visibleRow = 1; visibleRow <= PATTERN_VISIBLE_ROWS; ++visibleRow) {
        int rowY = (int)(1.0 + ((visibleRow - 1) * 16.75) + 0.5);
        bool highlight = canHighlight && (visibleRow == g_cache.highlightRow);
        const PatternRow *rowData = &g_cache.rows[visibleRow - 1];
        COLORREF color = textColor1;

        if (wcscmp(rowData->rowText, L"00") == 0) {
            color = textColor2;
        }

        if (highlight) {
            color = COLOR_WHITE;
        }

        SetTextColor(hdc, color);
        TextOutW(hdc, 16, rowY, rowData->rowText, (int)wcslen(rowData->rowText));

        for (int channel = 0; channel < 4; ++channel) {
            TextOutW(
                hdc,
                g_lineXs[channel],
                rowY,
                rowData->cells[channel],
                (int)wcslen(rowData->cells[channel])
            );
        }
    }

    SelectObject(hdc, oldFont);
    RestoreDC(hdc, savedDc);
}