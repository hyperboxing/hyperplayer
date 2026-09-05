#include "directory_listing_win32.h"
#include "ui.h"
#include "player.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static const int AREA_X = 1363;
static const int AREA_Y = 134;
static const int AREA_W = 555;
static const int AREA_H = 325;
static const int ROW_H = 20;
static const int TEXT_X = 1367;
static const int TEXT_Y_OFFSET = -1;
static const int SIZE_RIGHT_X = 1910;

static const int DRIVE_BUTTONS_Y = 99;
static const int DRIVE_BUTTONS_RIGHT_MARGIN = 10;
static const int DRIVE_BUTTONS_GAP = 16;

static const COLORREF COLOR_INFO = RGB(0xBB, 0xBB, 0xBB);
static const COLORREF COLOR_SHADOW = RGB(0x59, 0x59, 0x59);
static const COLORREF COLOR_SELECTED = RGB(0xFF, 0xDD, 0x00);

typedef struct DriveButton {
    wchar_t path[8];
    RECT rect;
} DriveButton;

static DriveButton g_driveButtons[26];
static int g_driveButtonCount = 0;

static bool refresh_listing(AppState *app);
static void set_current_path(AppState *app, const wchar_t *path);

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

static void fit_text_to_width(HDC hdc, HFONT font, const wchar_t *src, wchar_t *dst, size_t dstCount, int maxWidth)
{
    SIZE sz;
    size_t len;

    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = L'\0';

    if (!hdc || !font || !src || maxWidth <= 0) {
        return;
    }

    len = wcslen(src);
    if (len >= dstCount) {
        len = dstCount - 1;
    }

    wcsncpy(dst, src, len);
    dst[len] = L'\0';

    SelectObject(hdc, font);
    GetTextExtentPoint32W(hdc, dst, (int)wcslen(dst), &sz);

    while (dst[0] != L'\0' && sz.cx > maxWidth) {
        dst[wcslen(dst) - 1] = L'\0';
        GetTextExtentPoint32W(hdc, dst, (int)wcslen(dst), &sz);
    }
}

static int visible_rows(void)
{
    return AREA_H / ROW_H;
}

static bool is_drive_root(const wchar_t *path)
{
    size_t len;

    if (!path) {
        return false;
    }

    len = wcslen(path);
    return (len == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'));
}

static void get_parent_path(const wchar_t *path, wchar_t *outPath, size_t outCount)
{
    wchar_t temp[MAX_PATH];
    size_t len;
    size_t i;

    if (!path || !outPath || outCount == 0) {
        return;
    }

    copy_wstr(temp, MAX_PATH, path);
    len = wcslen(temp);

    while (len > 3 && (temp[len - 1] == L'\\' || temp[len - 1] == L'/')) {
        temp[len - 1] = L'\0';
        len--;
    }

    if (is_drive_root(temp)) {
        copy_wstr(outPath, outCount, temp);
        return;
    }

    for (i = len; i > 0; --i) {
        if (temp[i - 1] == L'\\' || temp[i - 1] == L'/') {
            temp[i - 1] = L'\0';
            break;
        }
    }

    if (wcslen(temp) == 2 && temp[1] == L':') {
        temp[2] = L'\\';
        temp[3] = L'\0';
    }

    if (temp[0] == L'\0') {
        copy_wstr(outPath, outCount, path);
        return;
    }

    copy_wstr(outPath, outCount, temp);
}

static void join_path(const wchar_t *a, const wchar_t *b, wchar_t *outPath, size_t outCount)
{
    size_t len;

    if (!a || !b || !outPath || outCount == 0) {
        return;
    }

    len = wcslen(a);

    if (len > 0 && (a[len - 1] == L'\\' || a[len - 1] == L'/')) {
        _snwprintf(outPath, outCount - 1, L"%ls%ls", a, b);
    } else {
        _snwprintf(outPath, outCount - 1, L"%ls\\%ls", a, b);
    }

    outPath[outCount - 1] = L'\0';
}

static bool is_mod_filename(const wchar_t *name)
{
    size_t len;

    if (!name) {
        return false;
    }

    len = wcslen(name);

    if (len >= 4) {
        if (towlower(name[0]) == L'm' &&
            towlower(name[1]) == L'o' &&
            towlower(name[2]) == L'd' &&
            name[3] == L'.') {
            return true;
        }

        if (towlower(name[len - 4]) == L'.' &&
            towlower(name[len - 3]) == L'm' &&
            towlower(name[len - 2]) == L'o' &&
            towlower(name[len - 1]) == L'd') {
            return true;
        }
    }

    return false;
}

static void clear_entries(DirectoryListing *d)
{
    if (!d) {
        return;
    }

    if (d->entries) {
        free(d->entries);
        d->entries = NULL;
    }

    d->entryCount = 0;
    d->entryCapacity = 0;
}

static bool push_entry(DirectoryListing *d, const DirectoryEntry *entry)
{
    DirectoryEntry *newEntries;
    int newCapacity;

    if (!d || !entry) {
        return false;
    }

    if (d->entryCount >= d->entryCapacity) {
        newCapacity = (d->entryCapacity > 0) ? (d->entryCapacity * 2) : 64;
        newEntries = (DirectoryEntry *)realloc(d->entries, (size_t)newCapacity * sizeof(DirectoryEntry));
        if (!newEntries) {
            return false;
        }

        d->entries = newEntries;
        d->entryCapacity = newCapacity;
    }

    d->entries[d->entryCount] = *entry;
    d->entryCount++;
    return true;
}

static int compare_entries(const void *a, const void *b)
{
    const DirectoryEntry *ea = (const DirectoryEntry *)a;
    const DirectoryEntry *eb = (const DirectoryEntry *)b;

    if (ea->isParent && !eb->isParent) {
        return -1;
    }
    if (eb->isParent && !ea->isParent) {
        return 1;
    }
    if (ea->isDir != eb->isDir) {
        return ea->isDir ? -1 : 1;
    }

    return _wcsicmp(ea->name, eb->name);
}

static void format_bytes_w(unsigned long long value, wchar_t *outText, size_t outCount)
{
    wchar_t digits[64];
    wchar_t reversed[96];
    wchar_t grouped[96];
    int len;
    int outLen = 0;
    int groupedLen = 0;
    int group = 0;
    int i;

    if (!outText || outCount == 0) {
        return;
    }

    swprintf(digits, sizeof(digits) / sizeof(digits[0]), L"%llu", value);
    len = (int)wcslen(digits);

    for (i = len - 1; i >= 0 && outLen < (int)(sizeof(reversed) / sizeof(reversed[0])) - 1; --i) {
        reversed[outLen++] = digits[i];
        group++;

        if (i > 0 && group == 3 && outLen < (int)(sizeof(reversed) / sizeof(reversed[0])) - 1) {
            reversed[outLen++] = L'.';
            group = 0;
        }
    }

    for (i = outLen - 1; i >= 0 && groupedLen < (int)(sizeof(grouped) / sizeof(grouped[0])) - 1; --i) {
        grouped[groupedLen++] = reversed[i];
    }
    grouped[groupedLen] = L'\0';

    _snwprintf(outText, outCount - 1, L"%ls b", grouped);
    outText[outCount - 1] = L'\0';
}

static void clamp_scroll(DirectoryListing *d)
{
    int maxScroll;

    if (!d) {
        return;
    }

    maxScroll = d->entryCount - visible_rows();
    if (maxScroll < 0) {
        maxScroll = 0;
    }

    if (d->scroll < 0) {
        d->scroll = 0;
    }
    if (d->scroll > maxScroll) {
        d->scroll = maxScroll;
    }
}

static void update_selected_index_from_path(AppState *app)
{
    DirectoryListing *d;
    int i;

    if (!app) {
        return;
    }

    d = &app->directory;
    d->selectedIndex = -1;

    if (app->currentSelectedFile[0] == L'\0') {
        return;
    }

    for (i = 0; i < d->entryCount; ++i) {
        if (_wcsicmp(d->entries[i].fullPath, app->currentSelectedFile) == 0) {
            d->selectedIndex = i;
            return;
        }
    }
}

static bool current_path_matches_drive(const wchar_t *path, const wchar_t *drivePath)
{
    if (!path || !drivePath) {
        return false;
    }

    if (wcslen(path) < 2 || wcslen(drivePath) < 2) {
        return false;
    }

    return (towupper(path[0]) == towupper(drivePath[0]) &&
            path[1] == L':' &&
            drivePath[1] == L':');
}

static void update_drive_buttons_layout(AppState *app, HDC existingHdc)
{
    RECT clientRect;
    HDC hdc;
    HFONT font;
    int savedDc;
    DWORD driveMask;
    int rightX;
    int drive;
    bool releaseHdc = false;

    g_driveButtonCount = 0;

    if (!app || !app->hwnd) {
        return;
    }

    if (!GetClientRect(app->hwnd, &clientRect)) {
        clientRect.left = 0;
        clientRect.top = 0;
        clientRect.right = 1920;
        clientRect.bottom = 1080;
    }

    hdc = existingHdc;
    if (!hdc) {
        hdc = GetDC(app->hwnd);
        if (!hdc) {
            return;
        }
        releaseHdc = true;
    }

    font = app->fonts.driveButtons ? app->fonts.driveButtons : app->fonts.dir;
    driveMask = GetLogicalDrives();
    rightX = clientRect.right - DRIVE_BUTTONS_RIGHT_MARGIN;
    savedDc = SaveDC(hdc);

    SelectObject(hdc, font);

    for (drive = 25; drive >= 0; --drive) {
        DriveButton *button;
        SIZE textSize;

        if ((driveMask & (1UL << drive)) == 0) {
            continue;
        }

        if (g_driveButtonCount >= (int)(sizeof(g_driveButtons) / sizeof(g_driveButtons[0]))) {
            break;
        }

        button = &g_driveButtons[g_driveButtonCount];
        swprintf(button->path, sizeof(button->path) / sizeof(button->path[0]), L"%c:", L'A' + drive);

        GetTextExtentPoint32W(hdc, button->path, (int)wcslen(button->path), &textSize);

        button->rect.left = rightX - textSize.cx;
        button->rect.top = DRIVE_BUTTONS_Y;
        button->rect.right = rightX;
        button->rect.bottom = DRIVE_BUTTONS_Y + textSize.cy + 2;

        rightX = button->rect.left - DRIVE_BUTTONS_GAP;
        g_driveButtonCount++;
    }

    RestoreDC(hdc, savedDc);

    if (releaseHdc) {
        ReleaseDC(app->hwnd, hdc);
    }
}

static void draw_drive_buttons(AppState *app, HDC hdc)
{
    int i;
    HFONT font;

    if (!app || !hdc) {
        return;
    }

    update_drive_buttons_layout(app, hdc);
    font = app->fonts.driveButtons ? app->fonts.driveButtons : app->fonts.dir;

    for (i = 0; i < g_driveButtonCount; ++i) {
        COLORREF textColor = current_path_matches_drive(app->directory.currentPath, g_driveButtons[i].path)
            ? COLOR_SELECTED
            : COLOR_INFO;

        ui_draw_shadowed_text(
            hdc,
            font,
            g_driveButtons[i].path,
            g_driveButtons[i].rect.left,
            DRIVE_BUTTONS_Y,
            textColor,
            COLOR_SHADOW,
            2,
            2,
            NULL,
            0
        );
    }
}

static bool handle_drive_button_click(AppState *app, int x, int y)
{
    DirectoryListing *d;
    int i;

    if (!app) {
        return false;
    }

    update_drive_buttons_layout(app, NULL);
    d = &app->directory;

    for (i = 0; i < g_driveButtonCount; ++i) {
        const RECT *r = &g_driveButtons[i].rect;

        if (x >= r->left - 2 &&
            x <= r->right + 2 &&
            y >= r->top - 2 &&
            y <= r->bottom + 4) {
            {
               wchar_t driveRoot[4];
               swprintf(driveRoot, sizeof(driveRoot) / sizeof(driveRoot[0]), L"%c:\\", g_driveButtons[i].path[0]);
               set_current_path(app, driveRoot);
           }
            d->scroll = 0;
            refresh_listing(app);
            app_set_status(app, L"%ls", d->currentPath);
            return true;
        }
    }

    return false;
}

static bool refresh_listing(AppState *app)
{
    DirectoryListing *d;
    WIN32_FIND_DATAW findData;
    HANDLE hFind;
    wchar_t searchPath[MAX_PATH];
    DirectoryEntry entry;

    if (!app) {
        return false;
    }

    d = &app->directory;
    clear_entries(d);

    memset(&entry, 0, sizeof(entry));
    copy_wstr(entry.name, sizeof(entry.name) / sizeof(entry.name[0]), L"..");
    get_parent_path(d->currentPath, entry.fullPath, sizeof(entry.fullPath) / sizeof(entry.fullPath[0]));
    entry.isDir = true;
    entry.isParent = true;
    entry.size = 0;

    if (!push_entry(d, &entry)) {
        return false;
    }

    join_path(d->currentPath, L"*", searchPath, sizeof(searchPath) / sizeof(searchPath[0]));
    hFind = FindFirstFileW(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            const wchar_t *name = findData.cFileName;
            bool isDir;

            if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
                continue;
            }

            isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!isDir && !is_mod_filename(name)) {
                continue;
            }

            memset(&entry, 0, sizeof(entry));
            copy_wstr(entry.name, sizeof(entry.name) / sizeof(entry.name[0]), name);
            join_path(d->currentPath, name, entry.fullPath, sizeof(entry.fullPath) / sizeof(entry.fullPath[0]));
            entry.isDir = isDir;
            entry.isParent = false;
            entry.size = ((unsigned long long)findData.nFileSizeHigh << 32) | (unsigned long long)findData.nFileSizeLow;

            if (!push_entry(d, &entry)) {
                FindClose(hFind);
                return false;
            }
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
    }

    qsort(d->entries, (size_t)d->entryCount, sizeof(DirectoryEntry), compare_entries);
    update_selected_index_from_path(app);
    clamp_scroll(d);
    return true;
}

static void set_current_path(AppState *app, const wchar_t *path)
{
    if (!app || !path) {
        return;
    }

    copy_wstr(app->directory.currentPath, sizeof(app->directory.currentPath) / sizeof(app->directory.currentPath[0]), path);
}

static bool select_file(AppState *app, int actualIndex)
{
    DirectoryListing *d;
    DirectoryEntry *entry;

    if (!app) {
        return false;
    }

    d = &app->directory;
    if (actualIndex < 0 || actualIndex >= d->entryCount) {
        return false;
    }

    entry = &d->entries[actualIndex];
    if (entry->isDir || entry->isParent) {
        return false;
    }

    copy_wstr(app->currentSelectedFile, sizeof(app->currentSelectedFile) / sizeof(app->currentSelectedFile[0]), entry->fullPath);
    copy_wstr(app->currentSelectedName, sizeof(app->currentSelectedName) / sizeof(app->currentSelectedName[0]), entry->name);
    d->selectedIndex = actualIndex;

    if (!player_load_module(app, entry->fullPath, entry->name)) {
        return false;
    }

    app->showFileBrowser = false;
    return true;
}

bool directory_listing_init(AppState *app, const wchar_t *rootPath)
{
    if (!app || !rootPath) {
        return false;
    }

    memset(&app->directory, 0, sizeof(app->directory));
    app->directory.selectedIndex = -1;
    set_current_path(app, rootPath);

    return refresh_listing(app);
}

void directory_listing_shutdown(AppState *app)
{
    if (!app) {
        return;
    }

    clear_entries(&app->directory);
    app->directory.selectedIndex = -1;
}

bool directory_listing_open_module(AppState *app, const wchar_t *path)
{
    DirectoryListing *d;
    wchar_t fullPath[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t *fileName = NULL;
    DWORD pathLength;
    DWORD attributes;
    int i;

    if (!app || !path || path[0] == L'\0') {
        return false;
    }

    pathLength = GetFullPathNameW(
        path,
        (DWORD)(sizeof(fullPath) / sizeof(fullPath[0])),
        fullPath,
        &fileName
    );
    if (pathLength == 0 ||
        pathLength >= (DWORD)(sizeof(fullPath) / sizeof(fullPath[0])) ||
        !fileName || fileName[0] == L'\0') {
        app_set_status(app, L"Invalid or too-long module path.");
        return false;
    }

    attributes = GetFileAttributesW(fullPath);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        app_set_status(app, L"Could not open file: %ls", fileName);
        return false;
    }

    if (!is_mod_filename(fileName)) {
        app_set_status(app, L"Not a recognized MOD filename: %ls", fileName);
        return false;
    }

    get_parent_path(fullPath, directory, sizeof(directory) / sizeof(directory[0]));
    d = &app->directory;
    set_current_path(app, directory);
    d->scroll = 0;

    if (!refresh_listing(app)) {
        app_set_status(app, L"Could not list module folder: %ls", directory);
        return false;
    }

    for (i = 0; i < d->entryCount; ++i) {
        if (!d->entries[i].isDir &&
            !d->entries[i].isParent &&
            _wcsicmp(d->entries[i].fullPath, fullPath) == 0) {
            if (!select_file(app, i)) {
                return false;
            }

            if (i >= visible_rows()) {
                d->scroll = i - visible_rows() + 1;
                clamp_scroll(d);
            }
            return true;
        }
    }

    app_set_status(app, L"Could not find module in its folder: %ls", fileName);
    return false;
}

void directory_listing_draw(AppState *app, HDC hdc)
{
    DirectoryListing *d;
    int startIndex;
    int endIndex;
    int row;
    int i;
    RECT clipRect;

    if (!app || !hdc) {
        return;
    }

    if (!app->showFileBrowser) {
        return;
    }

    d = &app->directory;
    draw_drive_buttons(app, hdc);

    if (!d->entries || d->entryCount <= 0) {
        return;
    }

    clipRect.left = AREA_X;
    clipRect.top = AREA_Y;
    clipRect.right = AREA_X + AREA_W;
    clipRect.bottom = AREA_Y + AREA_H;

    startIndex = d->scroll;
    endIndex = d->scroll + visible_rows();
    if (endIndex > d->entryCount) {
        endIndex = d->entryCount;
    }

    for (i = startIndex; i < endIndex; ++i) {
        wchar_t sizeText[96];
        wchar_t fittedName[260];
        int y;
        COLORREF rowColor;
        RECT sizeRect;
        RECT nameRect;
        SIZE sizeTextSize;
        SIZE charSize;
        int maxNameWidth;

        row = i - startIndex;
        y = AREA_Y + (row * ROW_H) + TEXT_Y_OFFSET;
        rowColor = (i == d->selectedIndex) ? COLOR_SELECTED : COLOR_INFO;

        if (!d->entries[i].isDir) {
            format_bytes_w(d->entries[i].size, sizeText, sizeof(sizeText) / sizeof(sizeText[0]));

            SelectObject(hdc, app->fonts.dir);
            GetTextExtentPoint32W(hdc, sizeText, (int)wcslen(sizeText), &sizeTextSize);
            GetTextExtentPoint32W(hdc, L"W", 1, &charSize);

            maxNameWidth = (SIZE_RIGHT_X - sizeTextSize.cx - charSize.cx) - TEXT_X;
            if (maxNameWidth < 0) {
                maxNameWidth = 0;
            }

            fit_text_to_width(
                hdc,
                app->fonts.dir,
                d->entries[i].name,
                fittedName,
                sizeof(fittedName) / sizeof(fittedName[0]),
                maxNameWidth
            );

            nameRect = clipRect;
            nameRect.left = TEXT_X;
            nameRect.right = TEXT_X + maxNameWidth;

            ui_draw_shadowed_text(
                hdc,
                app->fonts.dir,
                fittedName,
                TEXT_X,
                y,
                rowColor,
                COLOR_SHADOW,
                2,
                2,
                &nameRect,
                DT_LEFT | DT_NOPREFIX | DT_SINGLELINE
            );

            sizeRect = clipRect;
            sizeRect.left = AREA_X;
            sizeRect.right = SIZE_RIGHT_X;

            ui_draw_shadowed_text(
                hdc,
                app->fonts.dir,
                sizeText,
                AREA_X,
                y,
                rowColor,
                COLOR_SHADOW,
                2,
                2,
                &sizeRect,
                DT_RIGHT | DT_NOPREFIX | DT_SINGLELINE
            );
        } else {
            ui_draw_shadowed_text(
                hdc,
                app->fonts.dir,
                d->entries[i].name,
                TEXT_X,
                y,
                rowColor,
                COLOR_SHADOW,
                2,
                2,
                &clipRect,
                DT_LEFT | DT_NOPREFIX | DT_SINGLELINE
            );
        }
    }
}

bool directory_listing_mouse_down(AppState *app, int x, int y)
{
    DirectoryListing *d;
    int row;
    int index;
    DirectoryEntry *entry;

    if (!app) {
        return false;
    }

    if (!app->showFileBrowser) {
        return false;
    }

    if (handle_drive_button_click(app, x, y)) {
        return true;
    }

    if (x < AREA_X || x > AREA_X + AREA_W || y < AREA_Y || y > AREA_Y + AREA_H) {
        return false;
    }

    d = &app->directory;
    row = (y - AREA_Y) / ROW_H;
    index = d->scroll + row;

    if (index < 0 || index >= d->entryCount) {
        return true;
    }

    entry = &d->entries[index];

    if (entry->isDir) {
        set_current_path(app, entry->fullPath);
        d->scroll = 0;
        refresh_listing(app);
        app_set_status(app, L"%ls", d->currentPath);
        return true;
    }

    select_file(app, index);
    return true;
}

void directory_listing_mouse_wheel(AppState *app, int wheelDelta)
{
    DirectoryListing *d;
    int steps;

    if (!app) {
        return;
    }

    if (!app->showFileBrowser) {
        return;
    }

    d = &app->directory;
    steps = (wheelDelta / WHEEL_DELTA) * 3;

    if (steps > 0) {
        d->scroll -= steps;
    } else if (steps < 0) {
        d->scroll += (-steps);
    }

    clamp_scroll(d);
}

bool directory_listing_move_to_neighbor(AppState *app, int step)
{
    DirectoryListing *d;
    int playableCount = 0;
    int currentPlayable = -1;
    int newPlayable;
    int i;

    if (!app) {
        return false;
    }

    d = &app->directory;
    if (d->entryCount <= 0) {
        return false;
    }

    for (i = 0; i < d->entryCount; ++i) {
        if (!d->entries[i].isDir && !d->entries[i].isParent) {
            if (app->currentSelectedFile[0] != L'\0' &&
                _wcsicmp(d->entries[i].fullPath, app->currentSelectedFile) == 0) {
                currentPlayable = playableCount;
            }
            playableCount++;
        }
    }

    if (playableCount <= 0) {
        app_set_status(app, L"No MOD files in this folder");
        return false;
    }

    if (currentPlayable < 0) {
        currentPlayable = (step > 0) ? -1 : playableCount;
    }

    newPlayable = currentPlayable + step;
    if (newPlayable < 0 || newPlayable >= playableCount) {
        return false;
    }

    playableCount = 0;
    for (i = 0; i < d->entryCount; ++i) {
        if (!d->entries[i].isDir && !d->entries[i].isParent) {
            if (playableCount == newPlayable) {
                if (!select_file(app, i)) {
                    return false;
                }

                if (i < d->scroll) {
                    d->scroll = i;
                } else if (i >= d->scroll + visible_rows()) {
                    d->scroll = i - visible_rows() + 1;
                }

                clamp_scroll(d);
                return true;
            }
            playableCount++;
        }
    }

    return false;
}
