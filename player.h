#ifndef PLAYER_H
#define PLAYER_H

#include "app.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct SamplePreviewPoint {
    float minValue;
    float maxValue;
} SamplePreviewPoint;

typedef struct QuadrascopeState {
    int sampleIndex;
    double samplePos;
    int scopeStride;
    float sampleVolume;
    float vu;
    bool active;
    double scopeHold;
    unsigned int triggerSerial;
} QuadrascopeState;

bool player_init(AppState *app);
void player_shutdown(AppState *app);

bool player_is_available(const AppState *app);
bool player_is_loaded(const AppState *app);
bool player_is_paused(const AppState *app);
bool player_is_stopped(const AppState *app);

const wchar_t *player_get_current_name(const AppState *app);
const wchar_t *player_get_current_file_path(const AppState *app);

int player_get_num_orders(const AppState *app);
int player_get_current_order(const AppState *app);
int player_get_current_pattern(const AppState *app);
int player_get_current_row(const AppState *app);
bool player_get_loop_enabled(const AppState *app);

int player_get_order_pattern(const AppState *app, int order);
int player_get_pattern_num_rows(const AppState *app, int pattern);
int player_get_num_channels(const AppState *app);

bool player_format_pattern_cell(const AppState *app, int pattern, int row, int channel, wchar_t *outText, size_t outCount);

bool player_get_sample_info(const AppState *app, int sampleIndex1Based, wchar_t *outName, size_t outNameCount, int *outVolume, int *outSize);
bool player_get_sample_preview(const AppState *app, int sampleIndex1Based, const SamplePreviewPoint **outPreview, int *outCount);
int player_get_nonempty_sample_count(const AppState *app);
int player_get_nonempty_sample_index(const AppState *app, int slotIndex);
float player_get_recent_output_level(const AppState *app);
bool player_get_recent_mono_window(const AppState *app, float *outSamples, int count);

bool player_get_quadrascope_state(const AppState *app, int channel1Based, QuadrascopeState *outState);
bool player_get_sample_values(const AppState *app, int sampleIndex1Based, const float **outValues, int *outCount, int *outLoopStart, int *outLoopLength);

bool player_load_module(AppState *app, const wchar_t *absolutePath, const wchar_t *displayName);

void player_draw_songinfo(AppState *app, HDC hdc);

void player_play(AppState *app);
void player_pause(AppState *app);
void player_stop(AppState *app);

bool player_jump_to_order(AppState *app, int step);
bool player_restart_current_order(AppState *app);
void player_update(AppState *app, double dt);

#endif
