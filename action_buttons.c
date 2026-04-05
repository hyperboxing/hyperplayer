#include "action_buttons.h"
#include "directory_listing_win32.h"
#include "player.h"

static void play_and_show_tunnel(AppState *app)
{
    if (!app) {
        return;
    }

    app->showFileBrowser = false;
    player_play(app);
}


typedef struct RectI {
    int x;
    int y;
    int w;
    int h;
} RectI;

static const RectI BUTTON_PREVPOS = { 1312, 55, 16, 23 };
static const RectI BUTTON_NEXTPOS = { 1335, 55, 16, 23 };

static const RectI BUTTON_PREV = { 1241, 177, 17, 19 };
static const RectI BUTTON_STOP = { 1265, 177, 19, 19 };
static const RectI BUTTON_PLAY = { 1291, 177, 15, 19 };
static const RectI BUTTON_PAUSE = { 1313, 177, 14, 19 };
static const RectI BUTTON_NEXT = { 1334, 177, 17, 19 };

static bool point_in_rect(int px, int py, const RectI *r)
{
    return (px >= r->x && px <= r->x + r->w && py >= r->y && py <= r->y + r->h);
}

static bool ensure_selected_loaded(AppState *app)
{
    if (player_is_loaded(app)) {
        return true;
    }

    if (app->currentSelectedFile[0] == L'\0' || app->currentSelectedName[0] == L'\0') {
        app_set_status(app, L"Select a MOD first.");
        return false;
    }

    return player_load_module(app, app->currentSelectedFile, app->currentSelectedName);
}

bool action_buttons_mouse_down(AppState *app, int x, int y)
{
    if (!app) {
        return false;
    }

    if (point_in_rect(x, y, &BUTTON_PREVPOS)) {
        if (ensure_selected_loaded(app)) {
            player_jump_to_order(app, -1);
        }
        return true;
    }

    if (point_in_rect(x, y, &BUTTON_NEXTPOS)) {
        if (ensure_selected_loaded(app)) {
            player_jump_to_order(app, 1);
        }
        return true;
    }

    if (point_in_rect(x, y, &BUTTON_STOP)) {
        player_stop(app);
        return true;
    }

    if (point_in_rect(x, y, &BUTTON_PLAY)) {
        if (ensure_selected_loaded(app)) {
            play_and_show_tunnel(app);
        }
        return true;
    }

    if (point_in_rect(x, y, &BUTTON_PAUSE)) {
        if (player_is_paused(app) || player_is_stopped(app)) {
            if (ensure_selected_loaded(app)) {
                play_and_show_tunnel(app);
            }
        } else {
            player_pause(app);
        }
        return true;
    }

    if (point_in_rect(x, y, &BUTTON_NEXT)) {
        if (!directory_listing_move_to_neighbor(app, 1)) {
            app_set_status(app, L"No next MOD file in this folder");
        } else {
            play_and_show_tunnel(app);
        }
        return true;
    }

    if (point_in_rect(x, y, &BUTTON_PREV)) {
        if (!directory_listing_move_to_neighbor(app, -1)) {
            app_set_status(app, L"No previous MOD file in this folder");
        } else {
            play_and_show_tunnel(app);
        }
        return true;
    }

    return false;
}