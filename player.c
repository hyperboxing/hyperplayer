#include "player.h"
#include "ui.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <math.h>

#define PLAYER_SAMPLE_RATE 44100
#define PLAYER_BUFFER_FRAMES 1024
#define PLAYER_POOL_SIZE 32
#define SAMPLE_PREVIEW_WIDTH 555
#define AUDIO_HISTORY_SIZE 131072
#define SCOPE_ADVANCE_SCALE 16.0

typedef struct openmpt_module openmpt_module;

typedef struct openmpt_module_initial_ctl {
    const char *ctl;
    const char *value;
} openmpt_module_initial_ctl;

typedef openmpt_module *(*PFN_openmpt_module_create_from_memory2)(
    const void *filedata,
    size_t filesize,
    void *logfunc,
    void *loguser,
    void *errfunc,
    void *erruser,
    int *error,
    const char **error_message,
    const openmpt_module_initial_ctl *ctls
);
typedef void (*PFN_openmpt_module_destroy)(openmpt_module *mod);
typedef int32_t (*PFN_openmpt_module_get_num_channels)(openmpt_module *mod);
typedef int32_t (*PFN_openmpt_module_get_num_orders)(openmpt_module *mod);
typedef int32_t (*PFN_openmpt_module_get_order_pattern)(openmpt_module *mod, int32_t order);
typedef int32_t (*PFN_openmpt_module_get_pattern_num_rows)(openmpt_module *mod, int32_t pattern);
typedef const char *(*PFN_openmpt_module_get_metadata)(openmpt_module *mod, const char *key);
typedef const char *(*PFN_openmpt_module_format_pattern_row_channel_command)(openmpt_module *mod, int32_t pattern, int32_t row, int32_t channel, int command);
typedef double (*PFN_openmpt_module_get_duration_seconds)(openmpt_module *mod);
typedef double (*PFN_openmpt_module_set_position_seconds)(openmpt_module *mod, double seconds);
typedef double (*PFN_openmpt_module_get_time_at_position)(openmpt_module *mod, int32_t order, int32_t row);
typedef int32_t (*PFN_openmpt_module_get_current_order)(openmpt_module *mod);
typedef int32_t (*PFN_openmpt_module_get_current_pattern)(openmpt_module *mod);
typedef int32_t (*PFN_openmpt_module_get_current_row)(openmpt_module *mod);
typedef double (*PFN_openmpt_module_get_current_estimated_bpm)(openmpt_module *mod);
typedef int32_t (*PFN_openmpt_module_get_current_speed)(openmpt_module *mod);
typedef int (*PFN_openmpt_module_set_repeat_count)(openmpt_module *mod, int32_t repeat_count);
typedef size_t (*PFN_openmpt_module_read_interleaved_stereo)(openmpt_module *mod, int32_t samplerate, size_t count, int16_t *interleaved_stereo);
typedef int (*PFN_openmpt_module_set_render_param)(openmpt_module *mod, int param, int32_t value);
typedef double (*PFN_openmpt_module_get_current_channel_vu_mono)(openmpt_module *mod, int32_t channel);

typedef struct OpenMPTApi {
    HMODULE dll;
    HMODULE dependencyDlls[4];
    wchar_t loadedPath[MAX_PATH];

    PFN_openmpt_module_create_from_memory2 create_from_memory2;
    PFN_openmpt_module_destroy destroy;
    PFN_openmpt_module_get_num_channels get_num_channels;
    PFN_openmpt_module_get_num_orders get_num_orders;
    PFN_openmpt_module_get_order_pattern get_order_pattern;
    PFN_openmpt_module_get_pattern_num_rows get_pattern_num_rows;
    PFN_openmpt_module_get_metadata get_metadata;
    PFN_openmpt_module_format_pattern_row_channel_command format_pattern_row_channel_command;
    PFN_openmpt_module_get_duration_seconds get_duration_seconds;
    PFN_openmpt_module_set_position_seconds set_position_seconds;
    PFN_openmpt_module_get_time_at_position get_time_at_position;
    PFN_openmpt_module_get_current_order get_current_order;
    PFN_openmpt_module_get_current_pattern get_current_pattern;
    PFN_openmpt_module_get_current_row get_current_row;
    PFN_openmpt_module_get_current_estimated_bpm get_current_estimated_bpm;
    PFN_openmpt_module_get_current_speed get_current_speed;
    PFN_openmpt_module_set_repeat_count set_repeat_count;
    PFN_openmpt_module_read_interleaved_stereo read_interleaved_stereo;
    PFN_openmpt_module_set_render_param set_render_param;
    PFN_openmpt_module_get_current_channel_vu_mono get_current_channel_vu_mono;
} OpenMPTApi;

typedef struct AudioBuffer {
    WAVEHDR header;
    int16_t *samples;
    bool prepared;
    bool submitted;
} AudioBuffer;

typedef struct ChannelScopeState {
    int sampleIndex;
    char note[4];
    double frequency;
    double phase;
    float vu;
    char lastCell[16];
    double samplePos;
    int scopeStride;
    float sampleVolume;
    bool active;
    char wrapMode[8];
    double scopeHold;
} ChannelScopeState;

struct PlayerState {
    OpenMPTApi api;

    openmpt_module *renderModule;
    openmpt_module *uiModule;

    unsigned char *moduleBytes;
    size_t moduleSize;

    wchar_t currentFile[MAX_PATH];
    wchar_t currentName[260];
    wchar_t title[260];

    wchar_t sampleNames[31][64];
    int sampleVolumes[31];
    int sampleSizes[31];
    int sampleLoopStarts[31];
    int sampleLoopLengths[31];
    float *sampleValues[31];
    int sampleValueCounts[31];
    SamplePreviewPoint samplePreviews[31][SAMPLE_PREVIEW_WIDTH];
    int samplePreviewCounts[31];
    int nonEmptySampleIndices[31];
    int nonEmptySampleCount;

    float recentOutputLevel;
    float historyMono[AUDIO_HISTORY_SIZE];
    float historyLeft[AUDIO_HISTORY_SIZE];
    float historyRight[AUDIO_HISTORY_SIZE];
    int historyWriteIndex;
    uint64_t totalHistorySamplesWritten;

    bool moduleLoaded;
    bool isPaused;
    bool isStopped;
    bool endOfFile;
    bool loopEnabled;
    bool audioReady;
    bool hasPlayedSinceLoad;

    double duration;
    double playbackSeconds;
    double playbackBaseSeconds;

    int numOrders;
    int currentOrder;
    int currentPattern;
    int currentRow;
    int lastRow;
    int lastPattern;
    int bpm;
    int speed;

    ChannelScopeState channelStates[4];

    HWAVEOUT waveOut;
    int sampleRate;
    int bufferFrames;
    int poolSize;
    AudioBuffer buffers[PLAYER_POOL_SIZE];
};

static const COLORREF COLOR_INFO = RGB(0xBB, 0xBB, 0xBB);

enum {
    COMMAND_NOTE = 0,
    COMMAND_INSTRUMENT = 1,
    COMMAND_EFFECT = 3,
    COMMAND_PARAMETER = 5
};

static const char *g_notes[12] = {
    "C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"
};

enum {
    OPENMPT_MODULE_RENDER_STEREOSEPARATION_PERCENT = 2
};

static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

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


static void unload_dependency_dlls(OpenMPTApi *api)
{
    if (!api) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (api->dependencyDlls[i]) {
            FreeLibrary(api->dependencyDlls[i]);
            api->dependencyDlls[i] = NULL;
        }
    }
}

static bool load_dependency_dlls_from_dir(OpenMPTApi *api, const wchar_t *dir)
{
    static const wchar_t *dependencyNames[4] = {
        L"openmpt-mpg123.dll",
        L"openmpt-ogg.dll",
        L"openmpt-vorbis.dll",
        L"openmpt-zlib.dll"
    };

    if (!api || !dir || dir[0] == L'\0') {
        return false;
    }

    unload_dependency_dlls(api);

    for (int i = 0; i < 4; ++i) {
        wchar_t dllPath[MAX_PATH];

        join_path(dir, dependencyNames[i], dllPath, sizeof(dllPath) / sizeof(dllPath[0]));
        api->dependencyDlls[i] = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

        if (!api->dependencyDlls[i]) {
            unload_dependency_dlls(api);
            return false;
        }
    }

    return true;
}

static void utf8_to_wide(const char *src, wchar_t *dst, size_t dstCount)
{
    int result;

    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = L'\0';

    if (!src || src[0] == '\0') {
        return;
    }

    result = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dstCount);
    if (result == 0) {
        result = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dstCount);
    }

    if (result == 0) {
        dst[0] = L'\0';
    } else {
        dst[dstCount - 1] = L'\0';
    }
}

static void *get_proc(HMODULE dll, const char *name)
{
    return (void *)GetProcAddress(dll, name);
}

static void compact_ascii(const char *src, char *dst, size_t dstCount)
{
    size_t di = 0;

    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = '\0';

    if (!src) {
        return;
    }

    while (*src && di + 1 < dstCount) {
        unsigned char ch = (unsigned char)*src;
        if (!isspace(ch)) {
            dst[di++] = (char)ch;
        }
        src++;
    }

    dst[di] = '\0';
}

static void normalize_note_ascii(const char *src, char *dst, size_t dstCount)
{
    char temp[32];

    compact_ascii(src, temp, sizeof(temp));

    if (temp[0] == '\0' || strcmp(temp, "...") == 0) {
        snprintf(dst, dstCount, "---");
        return;
    }

    snprintf(dst, dstCount, "%.3s", temp);
}

static void normalize_instr_ascii(const char *src, char *dst, size_t dstCount)
{
    char temp[32];
    size_t len;

    compact_ascii(src, temp, sizeof(temp));
    for (size_t i = 0; temp[i]; ++i) {
        if (temp[i] == '.') {
            temp[i] = '0';
        }
    }

    len = strlen(temp);
    if (len == 0) {
        snprintf(dst, dstCount, "00");
    } else if (len == 1) {
        snprintf(dst, dstCount, "0%c", temp[0]);
    } else {
        snprintf(dst, dstCount, "%.2s", temp);
    }
}

static void normalize_effect_ascii(const char *src, char *dst, size_t dstCount)
{
    char temp[32];

    compact_ascii(src, temp, sizeof(temp));
    for (size_t i = 0; temp[i]; ++i) {
        if (temp[i] == '.') {
            temp[i] = '0';
        }
    }

    if (temp[0] == '\0') {
        snprintf(dst, dstCount, "0");
    } else {
        snprintf(dst, dstCount, "%.1s", temp);
    }
}

static void normalize_param_ascii(const char *src, char *dst, size_t dstCount)
{
    char temp[32];
    size_t len;

    compact_ascii(src, temp, sizeof(temp));
    for (size_t i = 0; temp[i]; ++i) {
        if (temp[i] == '.') {
            temp[i] = '0';
        }
    }

    len = strlen(temp);
    if (len == 0) {
        snprintf(dst, dstCount, "00");
    } else if (len == 1) {
        snprintf(dst, dstCount, "0%c", temp[0]);
    } else {
        snprintf(dst, dstCount, "%.2s", temp);
    }
}

static void ascii_to_wide(const char *src, wchar_t *dst, size_t dstCount)
{
    int result;

    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = L'\0';

    if (!src) {
        return;
    }

    result = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dstCount);
    if (result == 0) {
        result = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dstCount);
    }

    if (result == 0) {
        size_t i = 0;
        while (src[i] && i + 1 < dstCount) {
            dst[i] = (wchar_t)(unsigned char)src[i];
            i++;
        }
        dst[i] = L'\0';
    } else {
        dst[dstCount - 1] = L'\0';
    }
}

static unsigned short u16be(const unsigned char *bytes, size_t index, size_t size)
{
    if (!bytes || index + 1 >= size) {
        return 0;
    }

    return (unsigned short)(((unsigned short)bytes[index] << 8) | (unsigned short)bytes[index + 1]);
}

static float signed8_to_float(unsigned char byteValue)
{
    if (byteValue > 127) {
        return ((float)((int)byteValue - 256)) / 128.0f;
    }

    return ((float)byteValue) / 128.0f;
}

static bool has_known_31_sample_signature(const unsigned char *bytes, size_t size)
{
    if (!bytes || size < 1084) {
        return false;
    }

    if (memcmp(bytes + 1080, "M.K.", 4) == 0) return true;
    if (memcmp(bytes + 1080, "M!K!", 4) == 0) return true;
    if (memcmp(bytes + 1080, "FLT4", 4) == 0) return true;
    if (memcmp(bytes + 1080, "4CHN", 4) == 0) return true;
    if (memcmp(bytes + 1080, "N.T.", 4) == 0) return true;

    return false;
}

static void trim_sample_name_bytes(const unsigned char *src, size_t len, wchar_t *dst, size_t dstCount)
{
    char temp[128];
    size_t ti = 0;

    if (!dst || dstCount == 0) {
        return;
    }

    dst[0] = L'\0';

    if (!src) {
        return;
    }

    for (size_t i = 0; i < len && ti + 1 < sizeof(temp); ++i) {
        unsigned char ch = src[i];

        if (ch == 0) {
            continue;
        }

        if (ch < 32 || ch == 127) {
            temp[ti++] = ' ';
        } else {
            temp[ti++] = (char)ch;
        }
    }

    while (ti > 0 && temp[ti - 1] == ' ') {
        ti--;
    }

    temp[ti] = '\0';
    ascii_to_wide(temp, dst, dstCount);
}

static void reset_audio_history(PlayerState *p)
{
    if (!p) {
        return;
    }

    for (int i = 0; i < AUDIO_HISTORY_SIZE; ++i) {
        p->historyMono[i] = 0.0f;
        p->historyLeft[i] = 0.0f;
        p->historyRight[i] = 0.0f;
    }

    p->historyWriteIndex = 0;
    p->totalHistorySamplesWritten = 0;
}

static void clear_sample_data(PlayerState *p)
{
    if (!p) {
        return;
    }

    for (int i = 0; i < 31; ++i) {
        if (p->sampleValues[i]) {
            free(p->sampleValues[i]);
            p->sampleValues[i] = NULL;
        }

        p->sampleNames[i][0] = L'\0';
        p->sampleVolumes[i] = 0;
        p->sampleSizes[i] = 0;
        p->sampleLoopStarts[i] = 0;
        p->sampleLoopLengths[i] = 0;
        p->sampleValueCounts[i] = 0;
        p->samplePreviewCounts[i] = 0;

        for (int j = 0; j < SAMPLE_PREVIEW_WIDTH; ++j) {
            p->samplePreviews[i][j].minValue = 0.0f;
            p->samplePreviews[i][j].maxValue = 0.0f;
        }
    }

    p->nonEmptySampleCount = 0;
}

static void reset_channel_states(PlayerState *p)
{
    if (!p) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        p->channelStates[i].sampleIndex = 0;
        strcpy(p->channelStates[i].note, "---");
        p->channelStates[i].frequency = 0.0;
        p->channelStates[i].phase = 1.0;
        p->channelStates[i].vu = 0.0f;
        strcpy(p->channelStates[i].lastCell, "--- 00000");
        p->channelStates[i].samplePos = 1.0;
        p->channelStates[i].scopeStride = 1;
        p->channelStates[i].sampleVolume = 0.0f;
        p->channelStates[i].active = false;
        strcpy(p->channelStates[i].wrapMode, "none");
        p->channelStates[i].scopeHold = 0.0;
    }
}

static void build_sample_preview(
    const float *sampleValues,
    int sampleLength,
    SamplePreviewPoint *outPreview,
    int *outCount
)
{
    int width;

    if (!outCount) {
        return;
    }

    *outCount = 0;

    if (!sampleValues || sampleLength <= 0 || !outPreview) {
        return;
    }

    width = SAMPLE_PREVIEW_WIDTH;

    for (int i = 0; i < width; ++i) {
        int startIndex = (i * sampleLength) / width;
        int endIndex = (((i + 1) * sampleLength) / width) - 1;
        float minv = 1.0f;
        float maxv = -1.0f;

        if (endIndex < startIndex) {
            endIndex = startIndex;
        }
        if (endIndex >= sampleLength) {
            endIndex = sampleLength - 1;
        }
        if (startIndex >= sampleLength) {
            startIndex = sampleLength - 1;
        }
        if (startIndex < 0) {
            startIndex = 0;
        }

        for (int j = startIndex; j <= endIndex; ++j) {
            float v = sampleValues[j];

            if (v < minv) minv = v;
            if (v > maxv) maxv = v;
        }

        outPreview[i].minValue = minv;
        outPreview[i].maxValue = maxv;
    }

    *outCount = width;
}

static double note_to_hz(const char *note)
{
    char name[3];
    int octave = 0;
    int idx = -1;
    int midi;

    if (!note || strcmp(note, "---") == 0) {
        return 0.0;
    }

    name[0] = note[0];
    name[1] = note[1];
    name[2] = '\0';

    if (!isdigit((unsigned char)note[2])) {
        return 0.0;
    }

    octave = note[2] - '0';

    for (int i = 0; i < 12; ++i) {
        if (strcmp(g_notes[i], name) == 0) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        return 0.0;
    }

    midi = (octave + 1) * 12 + idx;
    return 440.0 * pow(2.0, ((double)midi - 69.0) / 12.0);
}

static void build_cell_text_internal(
    PlayerState *p,
    int pattern,
    int row,
    int channel,
    char *outCell,
    size_t outCount
)
{
    char note[16];
    char instr[16];
    char effect[16];
    char param[16];

    if (!outCell || outCount == 0) {
        return;
    }

    snprintf(outCell, outCount, "--- 00000");

    if (!p || !p->uiModule) {
        return;
    }

    if (channel < 0 || channel >= p->api.get_num_channels(p->uiModule)) {
        return;
    }

    normalize_note_ascii(
        p->api.format_pattern_row_channel_command(p->uiModule, pattern, row, channel, COMMAND_NOTE),
        note,
        sizeof(note)
    );
    normalize_instr_ascii(
        p->api.format_pattern_row_channel_command(p->uiModule, pattern, row, channel, COMMAND_INSTRUMENT),
        instr,
        sizeof(instr)
    );
    normalize_effect_ascii(
        p->api.format_pattern_row_channel_command(p->uiModule, pattern, row, channel, COMMAND_EFFECT),
        effect,
        sizeof(effect)
    );
    normalize_param_ascii(
        p->api.format_pattern_row_channel_command(p->uiModule, pattern, row, channel, COMMAND_PARAMETER),
        param,
        sizeof(param)
    );

    snprintf(outCell, outCount, "%.3s %.2s%.1s%.2s", note, instr, effect, param);
}

static void update_channel_state_from_row(PlayerState *p)
{
    int currentRow;
    int currentPattern;

    if (!p || !p->uiModule || !p->moduleLoaded) {
        return;
    }

    currentRow = p->currentRow;
    currentPattern = p->currentPattern;

    if (currentPattern < 0 || currentRow < 0) {
        return;
    }

    for (int channel = 0; channel < 4; ++channel) {
        ChannelScopeState *state = &p->channelStates[channel];
        char cell[16];
        char note[4];
        char sampleHex[3];
        int sampleIndex = 0;
        bool sampleChanged = false;

        build_cell_text_internal(p, currentPattern, currentRow, channel, cell, sizeof(cell));
        snprintf(state->lastCell, sizeof(state->lastCell), "%s", cell);

        note[0] = cell[0];
        note[1] = cell[1];
        note[2] = cell[2];
        note[3] = '\0';

        sampleHex[0] = cell[4];
        sampleHex[1] = cell[5];
        sampleHex[2] = '\0';

        sampleIndex = (int)strtol(sampleHex, NULL, 16);

        if (sampleIndex > 0) {
            if (sampleIndex != state->sampleIndex) {
                sampleChanged = true;
            }
            state->sampleIndex = sampleIndex;
        }

        if (state->sampleIndex > 0 && state->sampleIndex <= 31 && p->sampleValueCounts[state->sampleIndex - 1] > 1) {
            int idx = state->sampleIndex - 1;
            int sampleCount = p->sampleValueCounts[idx];
            int loopStart = p->sampleLoopStarts[idx] + 1;
            int loopLength = p->sampleLoopLengths[idx];

            state->sampleVolume = (float)p->sampleVolumes[idx] / 64.0f;

            if (loopLength > 2 && loopStart <= sampleCount) {
                strcpy(state->wrapMode, "loop");
            } else {
                strcpy(state->wrapMode, "none");
            }

            if (sampleChanged) {
                state->samplePos = 1.0;
                state->active = true;
                state->scopeHold = 0.20;
            }
        } else {
            state->sampleVolume = 0.0f;
            strcpy(state->wrapMode, "none");
        }

        if (strcmp(note, "---") != 0) {
            strcpy(state->note, note);
            state->frequency = note_to_hz(note);
            state->phase = 1.0;
            state->samplePos = 1.0;
            state->active = true;
            state->scopeHold = 0.20;
        } else if (sampleIndex > 0) {
            if (state->frequency <= 0.0) {
                state->frequency = 220.0;
            }
            state->samplePos = 1.0;
            state->active = true;
            state->scopeHold = 0.20;
        }
    }
}

static uint64_t get_output_played_samples_since_reset(PlayerState *p)
{
    MMTIME mmt;
    MMRESULT mm;

    if (!p || !p->waveOut) {
        return 0;
    }

    memset(&mmt, 0, sizeof(mmt));
    mmt.wType = TIME_SAMPLES;
    mm = waveOutGetPosition(p->waveOut, &mmt, sizeof(mmt));

    if (mm != MMSYSERR_NOERROR) {
        return 0;
    }

    if (mmt.wType == TIME_SAMPLES) {
        return (uint64_t)mmt.u.sample;
    } else if (mmt.wType == TIME_BYTES) {
        return (uint64_t)(mmt.u.cb / 4);
    } else if (mmt.wType == TIME_MS) {
        return (uint64_t)(((uint64_t)mmt.u.ms * (uint64_t)p->sampleRate) / 1000ULL);
    }

    return 0;
}

static bool fill_recent_mono_window_from_playhead(PlayerState *p, float *outSamples, int count)
{
    uint64_t played;
    int available;
    int missing;
    uint64_t startAbs;

    if (!p || !outSamples || count <= 0) {
        return false;
    }

    for (int i = 0; i < count; ++i) {
        outSamples[i] = 0.0f;
    }

    if (!p->moduleLoaded) {
        return false;
    }

    played = get_output_played_samples_since_reset(p);
    if (played > p->totalHistorySamplesWritten) {
        played = p->totalHistorySamplesWritten;
    }

    if (played > (uint64_t)count) {
        available = count;
    } else {
        available = (int)played;
    }

    if (available < 0) {
        available = 0;
    }

    missing = count - available;
    startAbs = played - (uint64_t)available;

    for (int i = 0; i < available; ++i) {
        uint64_t absIndex = startAbs + (uint64_t)i;
        outSamples[missing + i] = p->historyMono[(size_t)(absIndex % AUDIO_HISTORY_SIZE)];
    }

    return true;
}

static float get_playhead_aligned_recent_level(PlayerState *p)
{
    uint64_t played;
    int count;
    uint64_t startAbs;
    double sum = 0.0;

    if (!p || !p->moduleLoaded || p->isPaused || p->isStopped) {
        return 0.0f;
    }

    played = get_output_played_samples_since_reset(p);
    if (played > p->totalHistorySamplesWritten) {
        played = p->totalHistorySamplesWritten;
    }

    if (played > 512ULL) {
        count = 512;
    } else {
        count = (int)played;
    }

    if (count <= 0) {
        return 0.0f;
    }

    startAbs = played - (uint64_t)count;

    for (int i = 0; i < count; ++i) {
        float v = p->historyMono[(size_t)((startAbs + (uint64_t)i) % AUDIO_HISTORY_SIZE)];
        if (v < 0.0f) {
            v = -v;
        }
        sum += v;
    }

    return (float)(sum / (double)count);
}

static void parse_mod_samples(PlayerState *p)
{
    int sampleCount;
    int channels;
    size_t ordersOffset;
    size_t patternsOffset;
    int highestPattern = 0;
    int patternCount;
    size_t sampleDataOffset;
    size_t cursor;

    if (!p) {
        return;
    }

    clear_sample_data(p);

    if (!p->moduleBytes || p->moduleSize < 600) {
        return;
    }

    if (has_known_31_sample_signature(p->moduleBytes, p->moduleSize)) {
        sampleCount = 31;
        channels = 4;
        ordersOffset = 952;
        patternsOffset = 1084;
    } else {
        sampleCount = 15;
        channels = 4;
        ordersOffset = 472;
        patternsOffset = 600;
    }

    for (int i = 0; i < 128; ++i) {
        int orderValue;
        size_t offset = ordersOffset + (size_t)i;

        if (offset >= p->moduleSize) {
            break;
        }

        orderValue = (int)p->moduleBytes[offset];
        if (orderValue > highestPattern) {
            highestPattern = orderValue;
        }
    }

    patternCount = highestPattern + 1;
    sampleDataOffset = patternsOffset + (size_t)patternCount * 64u * (size_t)channels * 4u;
    cursor = sampleDataOffset;

    for (int i = 0; i < sampleCount; ++i) {
        size_t base = 20 + (size_t)i * 30;
        int sampleLength;

        if (base + 29 >= p->moduleSize) {
            break;
        }

        trim_sample_name_bytes(p->moduleBytes + base, 22, p->sampleNames[i], sizeof(p->sampleNames[i]) / sizeof(p->sampleNames[i][0]));
        p->sampleSizes[i] = (int)u16be(p->moduleBytes, base + 22, p->moduleSize) * 2;
        p->sampleVolumes[i] = (int)p->moduleBytes[base + 25];
        p->sampleLoopStarts[i] = (int)u16be(p->moduleBytes, base + 26, p->moduleSize) * 2;
        p->sampleLoopLengths[i] = (int)u16be(p->moduleBytes, base + 28, p->moduleSize) * 2;

        sampleLength = p->sampleSizes[i];
        if (sampleLength > 0 && cursor + (size_t)sampleLength <= p->moduleSize) {
            p->sampleValues[i] = (float *)malloc((size_t)sampleLength * sizeof(float));

            if (p->sampleValues[i]) {
                for (int j = 0; j < sampleLength; ++j) {
                    p->sampleValues[i][j] = signed8_to_float(p->moduleBytes[cursor + (size_t)j]);
                }

                p->sampleValueCounts[i] = sampleLength;

                build_sample_preview(
                    p->sampleValues[i],
                    sampleLength,
                    p->samplePreviews[i],
                    &p->samplePreviewCounts[i]
                );

                if (sampleLength > 1) {
                    p->nonEmptySampleIndices[p->nonEmptySampleCount] = i + 1;
                    p->nonEmptySampleCount++;
                }
            }

            cursor += (size_t)sampleLength;
        } else {
            p->samplePreviewCounts[i] = 0;
            p->sampleValueCounts[i] = 0;
        }
    }
}

static bool load_openmpt_api(AppState *app, PlayerState *p)
{
    wchar_t candidate[MAX_PATH];
    HMODULE dll = NULL;

    if (!app || !p) {
        return false;
    }

    if (app->runtimeDllsReady && app->runtimeDir[0] != L'\0') {
        if (load_dependency_dlls_from_dir(&p->api, app->runtimeDir)) {
            join_path(app->runtimeDir, L"libopenmpt.dll", candidate, sizeof(candidate) / sizeof(candidate[0]));
            dll = LoadLibraryExW(candidate, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
    }

    if (!dll) {
        unload_dependency_dlls(&p->api);

        join_path(app->exeDir, L"libopenmpt.dll", candidate, sizeof(candidate) / sizeof(candidate[0]));
        dll = LoadLibraryW(candidate);

        if (!dll) {
            join_path(app->exeDir, L"openmpt.dll", candidate, sizeof(candidate) / sizeof(candidate[0]));
            dll = LoadLibraryW(candidate);
        }

        if (!dll) {
            dll = LoadLibraryW(L"libopenmpt.dll");
            if (dll) {
                copy_wstr(candidate, sizeof(candidate) / sizeof(candidate[0]), L"libopenmpt.dll");
            }
        }

        if (!dll) {
            dll = LoadLibraryW(L"openmpt.dll");
            if (dll) {
                copy_wstr(candidate, sizeof(candidate) / sizeof(candidate[0]), L"openmpt.dll");
            }
        }
    }

    if (!dll) {
        return false;
    }

    p->api.dll = dll;
    copy_wstr(p->api.loadedPath, sizeof(p->api.loadedPath) / sizeof(p->api.loadedPath[0]), candidate);

    p->api.create_from_memory2 = (PFN_openmpt_module_create_from_memory2)get_proc(dll, "openmpt_module_create_from_memory2");
    p->api.destroy = (PFN_openmpt_module_destroy)get_proc(dll, "openmpt_module_destroy");
    p->api.get_num_channels = (PFN_openmpt_module_get_num_channels)get_proc(dll, "openmpt_module_get_num_channels");
    p->api.get_num_orders = (PFN_openmpt_module_get_num_orders)get_proc(dll, "openmpt_module_get_num_orders");
    p->api.get_order_pattern = (PFN_openmpt_module_get_order_pattern)get_proc(dll, "openmpt_module_get_order_pattern");
    p->api.get_pattern_num_rows = (PFN_openmpt_module_get_pattern_num_rows)get_proc(dll, "openmpt_module_get_pattern_num_rows");
    p->api.get_metadata = (PFN_openmpt_module_get_metadata)get_proc(dll, "openmpt_module_get_metadata");
    p->api.format_pattern_row_channel_command = (PFN_openmpt_module_format_pattern_row_channel_command)get_proc(dll, "openmpt_module_format_pattern_row_channel_command");
    p->api.get_duration_seconds = (PFN_openmpt_module_get_duration_seconds)get_proc(dll, "openmpt_module_get_duration_seconds");
    p->api.set_position_seconds = (PFN_openmpt_module_set_position_seconds)get_proc(dll, "openmpt_module_set_position_seconds");
    p->api.get_time_at_position = (PFN_openmpt_module_get_time_at_position)get_proc(dll, "openmpt_module_get_time_at_position");
    p->api.get_current_order = (PFN_openmpt_module_get_current_order)get_proc(dll, "openmpt_module_get_current_order");
    p->api.get_current_pattern = (PFN_openmpt_module_get_current_pattern)get_proc(dll, "openmpt_module_get_current_pattern");
    p->api.get_current_row = (PFN_openmpt_module_get_current_row)get_proc(dll, "openmpt_module_get_current_row");
    p->api.get_current_estimated_bpm = (PFN_openmpt_module_get_current_estimated_bpm)get_proc(dll, "openmpt_module_get_current_estimated_bpm");
    p->api.get_current_speed = (PFN_openmpt_module_get_current_speed)get_proc(dll, "openmpt_module_get_current_speed");
    p->api.set_repeat_count = (PFN_openmpt_module_set_repeat_count)get_proc(dll, "openmpt_module_set_repeat_count");
    p->api.read_interleaved_stereo = (PFN_openmpt_module_read_interleaved_stereo)get_proc(dll, "openmpt_module_read_interleaved_stereo");
    p->api.set_render_param = (PFN_openmpt_module_set_render_param)get_proc(dll, "openmpt_module_set_render_param");
    p->api.get_current_channel_vu_mono = (PFN_openmpt_module_get_current_channel_vu_mono)get_proc(dll, "openmpt_module_get_current_channel_vu_mono");

    if (!p->api.create_from_memory2 ||
        !p->api.destroy ||
        !p->api.get_num_channels ||
        !p->api.get_num_orders ||
        !p->api.get_order_pattern ||
        !p->api.get_pattern_num_rows ||
        !p->api.get_metadata ||
        !p->api.format_pattern_row_channel_command ||
        !p->api.get_duration_seconds ||
        !p->api.set_position_seconds ||
        !p->api.get_time_at_position ||
        !p->api.get_current_order ||
        !p->api.get_current_pattern ||
        !p->api.get_current_row ||
        !p->api.get_current_estimated_bpm ||
        !p->api.get_current_speed ||
        !p->api.set_repeat_count ||
        !p->api.read_interleaved_stereo) {
        FreeLibrary(dll);
        p->api.dll = NULL;
        unload_dependency_dlls(&p->api);
        memset(&p->api, 0, sizeof(p->api));
        return false;
    }

    return true;
}

static void apply_stereo_separation(AppState *app, PlayerState *p)
{
    int value = 33;

    if (!p || !p->api.set_render_param) {
        return;
    }

    if (app) {
        value = app->config.stereoSeparation;
    }

    if (value < 0) {
        value = 0;
    }
    if (value > 200) {
        value = 200;
    }

    if (p->renderModule) {
        p->api.set_render_param(p->renderModule, OPENMPT_MODULE_RENDER_STEREOSEPARATION_PERCENT, value);
    }

    if (p->uiModule) {
        p->api.set_render_param(p->uiModule, OPENMPT_MODULE_RENDER_STEREOSEPARATION_PERCENT, value);
    }
}

static bool read_file_bytes(const wchar_t *path, unsigned char **outBytes, size_t *outSize)
{
    FILE *f;
    long size;
    unsigned char *bytes;
    size_t readCount;

    if (!path || !outBytes || !outSize) {
        return false;
    }

    *outBytes = NULL;
    *outSize = 0;

    f = _wfopen(path, L"rb");
    if (!f) {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    bytes = (unsigned char *)malloc((size_t)size);
    if (!bytes) {
        fclose(f);
        return false;
    }

    readCount = fread(bytes, 1, (size_t)size, f);
    fclose(f);

    if (readCount != (size_t)size) {
        free(bytes);
        return false;
    }

    *outBytes = bytes;
    *outSize = (size_t)size;
    return true;
}

static void destroy_modules(PlayerState *p)
{
    if (!p) {
        return;
    }

    if (p->renderModule) {
        p->api.destroy(p->renderModule);
        p->renderModule = NULL;
    }

    if (p->uiModule) {
        p->api.destroy(p->uiModule);
        p->uiModule = NULL;
    }

    if (p->moduleBytes) {
        free(p->moduleBytes);
        p->moduleBytes = NULL;
    }

    p->moduleSize = 0;
    p->moduleLoaded = false;
    p->currentFile[0] = L'\0';
    p->currentName[0] = L'\0';
    p->title[0] = L'\0';
    p->duration = 0.0;
    p->playbackSeconds = 0.0;
    p->playbackBaseSeconds = 0.0;
    p->numOrders = 0;
    p->currentOrder = 0;
    p->currentPattern = 0;
    p->currentRow = 0;
    p->lastRow = -1;
    p->lastPattern = -1;
    p->bpm = 0;
    p->speed = 0;
    p->recentOutputLevel = 0.0f;
    p->hasPlayedSinceLoad = false;

    clear_sample_data(p);
    reset_audio_history(p);
    reset_channel_states(p);
}

static void update_playback_status(AppState *app)
{
    PlayerState *p;

    if (!app || !app->player) {
        return;
    }

    p = app->player;

    if (!p->moduleLoaded) {
        return;
    }

    if (p->isPaused) {
        app_set_status(app, L"Paused.");
        return;
    }

    if (p->isStopped) {
        if (p->hasPlayedSinceLoad) {
            app_set_status(app, L"Stopped playing.");
        } else {
            app_set_status(app, L"Loaded: %ls", p->currentName);
        }
        return;
    }

    app_set_status(app, L"Playing %ls", p->currentName);
}

static bool create_audio_output(PlayerState *p)
{
    WAVEFORMATEX fmt;
    MMRESULT mm;

    if (!p) {
        return false;
    }

    p->sampleRate = PLAYER_SAMPLE_RATE;
    p->bufferFrames = PLAYER_BUFFER_FRAMES;
    p->poolSize = PLAYER_POOL_SIZE;

    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = (DWORD)p->sampleRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * (fmt.wBitsPerSample / 8));
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    mm = waveOutOpen(&p->waveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
    if (mm != MMSYSERR_NOERROR) {
        p->waveOut = NULL;
        return false;
    }

    for (int i = 0; i < p->poolSize; ++i) {
        AudioBuffer *b = &p->buffers[i];
        memset(b, 0, sizeof(*b));

        b->samples = (int16_t *)malloc((size_t)p->bufferFrames * 2 * sizeof(int16_t));
        if (!b->samples) {
            return false;
        }

        b->header.lpData = (LPSTR)b->samples;
        b->header.dwBufferLength = (DWORD)((size_t)p->bufferFrames * 2 * sizeof(int16_t));

        mm = waveOutPrepareHeader(p->waveOut, &b->header, sizeof(b->header));
        if (mm != MMSYSERR_NOERROR) {
            return false;
        }

        b->prepared = true;
        b->submitted = false;
    }

    p->audioReady = true;
    return true;
}

static void close_audio_output(PlayerState *p)
{
    if (!p) {
        return;
    }

    if (p->waveOut) {
        waveOutReset(p->waveOut);
    }

    for (int i = 0; i < PLAYER_POOL_SIZE; ++i) {
        AudioBuffer *b = &p->buffers[i];

        if (p->waveOut && b->prepared) {
            waveOutUnprepareHeader(p->waveOut, &b->header, sizeof(b->header));
        }

        if (b->samples) {
            free(b->samples);
            b->samples = NULL;
        }

        memset(&b->header, 0, sizeof(b->header));
        b->prepared = false;
        b->submitted = false;
    }

    if (p->waveOut) {
        waveOutClose(p->waveOut);
        p->waveOut = NULL;
    }

    p->audioReady = false;
}

static void reset_audio_queue(PlayerState *p)
{
    if (!p || !p->waveOut) {
        return;
    }

    waveOutReset(p->waveOut);

    for (int i = 0; i < p->poolSize; ++i) {
        p->buffers[i].submitted = false;
    }
}

static bool any_buffer_busy(const PlayerState *p)
{
    if (!p) {
        return false;
    }

    for (int i = 0; i < p->poolSize; ++i) {
        const AudioBuffer *b = &p->buffers[i];
        if (b->submitted && ((b->header.dwFlags & WHDR_DONE) == 0)) {
            return true;
        }
    }

    return false;
}

static double get_output_playback_seconds(PlayerState *p)
{
    MMTIME mmt;
    MMRESULT mm;
    double playedSeconds = 0.0;

    if (!p || !p->waveOut) {
        return p ? p->playbackBaseSeconds : 0.0;
    }

    memset(&mmt, 0, sizeof(mmt));
    mmt.wType = TIME_SAMPLES;
    mm = waveOutGetPosition(p->waveOut, &mmt, sizeof(mmt));

    if (mm == MMSYSERR_NOERROR) {
        if (mmt.wType == TIME_SAMPLES) {
            playedSeconds = (double)mmt.u.sample / (double)p->sampleRate;
        } else if (mmt.wType == TIME_BYTES) {
            playedSeconds = (double)mmt.u.cb / (double)(p->sampleRate * 4);
        } else if (mmt.wType == TIME_MS) {
            playedSeconds = (double)mmt.u.ms / 1000.0;
        }
    }

    return p->playbackBaseSeconds + playedSeconds;
}

static bool queue_buffer(PlayerState *p, int index)
{
    AudioBuffer *b;
    size_t frames;
    MMRESULT mm;
    double accum = 0.0;

    if (!p || !p->audioReady || !p->renderModule) {
        return false;
    }

    b = &p->buffers[index];

    if (b->submitted && ((b->header.dwFlags & WHDR_DONE) == 0)) {
        return false;
    }

    b->submitted = false;

    if (p->endOfFile) {
        return false;
    }

    frames = p->api.read_interleaved_stereo(
        p->renderModule,
        p->sampleRate,
        (size_t)p->bufferFrames,
        b->samples
    );

    if (frames == 0) {
        p->endOfFile = true;
        return false;
    }

    for (size_t i = 0; i < frames; ++i) {
        float l = (float)b->samples[i * 2] / 32768.0f;
        float r = (float)b->samples[(i * 2) + 1] / 32768.0f;
        float mono = (l + r) * 0.5f;

        p->historyLeft[p->historyWriteIndex] = l;
        p->historyRight[p->historyWriteIndex] = r;
        p->historyMono[p->historyWriteIndex] = mono;

        p->historyWriteIndex++;
        if (p->historyWriteIndex >= AUDIO_HISTORY_SIZE) {
            p->historyWriteIndex = 0;
        }

        p->totalHistorySamplesWritten++;

        if (mono < 0.0f) {
            mono = -mono;
        }

        accum += mono;
    }

    p->recentOutputLevel = (frames > 0) ? (float)(accum / (double)frames) : 0.0f;

    if (frames < (size_t)p->bufferFrames) {
        memset(
            b->samples + (frames * 2),
            0,
            ((size_t)p->bufferFrames - frames) * 2 * sizeof(int16_t)
        );
    }

    b->header.dwBufferLength = (DWORD)((size_t)p->bufferFrames * 2 * sizeof(int16_t));

    mm = waveOutWrite(p->waveOut, &b->header, sizeof(b->header));
    if (mm != MMSYSERR_NOERROR) {
        p->endOfFile = true;
        return false;
    }

    b->submitted = true;
    return true;
}

static void prime_queue(PlayerState *p)
{
    bool progress;

    if (!p || !p->audioReady || !p->renderModule) {
        return;
    }

    do {
        progress = false;

        for (int i = 0; i < p->poolSize; ++i) {
            if (queue_buffer(p, i)) {
                progress = true;
            }
        }
    } while (progress);
}

static void update_ui_position(PlayerState *p)
{
    if (!p || !p->uiModule) {
        return;
    }

    p->api.set_position_seconds(p->uiModule, p->playbackSeconds);
    p->currentOrder = p->api.get_current_order(p->uiModule);
    p->currentPattern = p->api.get_current_pattern(p->uiModule);
    p->currentRow = p->api.get_current_row(p->uiModule);
    p->bpm = (int)(p->api.get_current_estimated_bpm(p->uiModule) + 0.5);
    p->speed = p->api.get_current_speed(p->uiModule);

    if (p->lastRow != p->currentRow || p->lastPattern != p->currentPattern) {
        update_channel_state_from_row(p);
        p->lastRow = p->currentRow;
        p->lastPattern = p->currentPattern;
    }
}

bool player_init(AppState *app)
{
    PlayerState *p;

    if (!app) {
        return false;
    }

    p = (PlayerState *)calloc(1, sizeof(PlayerState));
    if (!p) {
        return false;
    }

    p->loopEnabled = true;
    p->isStopped = true;
    p->sampleRate = PLAYER_SAMPLE_RATE;
    p->bufferFrames = PLAYER_BUFFER_FRAMES;
    p->poolSize = PLAYER_POOL_SIZE;
    p->recentOutputLevel = 0.0f;
    p->playbackBaseSeconds = 0.0;
    p->lastRow = -1;
    p->lastPattern = -1;
    p->hasPlayedSinceLoad = false;

    clear_sample_data(p);
    reset_audio_history(p);
    reset_channel_states(p);

    app->player = p;

    load_openmpt_api(app, p);
    create_audio_output(p);

    return true;
}

void player_shutdown(AppState *app)
{
    PlayerState *p;

    if (!app || !app->player) {
        return;
    }

    p = app->player;

    destroy_modules(p);
    close_audio_output(p);

    if (p->api.dll) {
        FreeLibrary(p->api.dll);
        p->api.dll = NULL;
    }

    unload_dependency_dlls(&p->api);

    free(p);
    app->player = NULL;
}

bool player_is_available(const AppState *app)
{
    return (app && app->player && app->player->api.dll != NULL);
}

bool player_is_loaded(const AppState *app)
{
    return (app && app->player && app->player->moduleLoaded);
}

bool player_is_paused(const AppState *app)
{
    return (app && app->player && app->player->isPaused);
}

bool player_is_stopped(const AppState *app)
{
    return (app && app->player && app->player->isStopped);
}

const wchar_t *player_get_current_name(const AppState *app)
{
    if (!app || !app->player || !app->player->moduleLoaded) {
        return L"";
    }

    return app->player->currentName;
}

const wchar_t *player_get_current_file_path(const AppState *app)
{
    if (!app || !app->player || !app->player->moduleLoaded) {
        return L"";
    }

    return app->player->currentFile;
}

int player_get_num_orders(const AppState *app)
{
    if (!app || !app->player || !app->player->moduleLoaded) {
        return 0;
    }

    return app->player->numOrders;
}

int player_get_current_order(const AppState *app)
{
    if (!app || !app->player || !app->player->moduleLoaded) {
        return 0;
    }

    return app->player->currentOrder;
}

int player_get_current_pattern(const AppState *app)
{
    if (!app || !app->player || !app->player->moduleLoaded) {
        return 0;
    }

    return app->player->currentPattern;
}

int player_get_current_row(const AppState *app)
{
    if (!app || !app->player || !app->player->moduleLoaded) {
        return 0;
    }

    return app->player->currentRow;
}

bool player_get_loop_enabled(const AppState *app)
{
    if (!app || !app->player) {
        return false;
    }

    return app->player->loopEnabled;
}

int player_get_order_pattern(const AppState *app, int order)
{
    if (!app || !app->player || !app->player->moduleLoaded || !app->player->uiModule) {
        return -1;
    }

    return app->player->api.get_order_pattern(app->player->uiModule, order);
}

int player_get_pattern_num_rows(const AppState *app, int pattern)
{
    if (!app || !app->player || !app->player->moduleLoaded || !app->player->uiModule) {
        return 0;
    }

    return app->player->api.get_pattern_num_rows(app->player->uiModule, pattern);
}

int player_get_num_channels(const AppState *app)
{
    if (!app || !app->player || !app->player->moduleLoaded || !app->player->uiModule) {
        return 0;
    }

    return app->player->api.get_num_channels(app->player->uiModule);
}

bool player_format_pattern_cell(const AppState *app, int pattern, int row, int channel, wchar_t *outText, size_t outCount)
{
    const PlayerState *p;
    char cell[32];

    if (!outText || outCount == 0) {
        return false;
    }

    copy_wstr(outText, outCount, L"--- 00000");

    if (!app || !app->player) {
        return false;
    }

    p = app->player;
    if (!p->moduleLoaded || !p->uiModule) {
        return false;
    }

    if (channel < 0 || channel >= 4 || channel >= p->api.get_num_channels(p->uiModule)) {
        return false;
    }

    build_cell_text_internal((PlayerState *)p, pattern, row, channel, cell, sizeof(cell));
    ascii_to_wide(cell, outText, outCount);
    return true;
}

bool player_get_sample_info(const AppState *app, int sampleIndex1Based, wchar_t *outName, size_t outNameCount, int *outVolume, int *outSize)
{
    const PlayerState *p;
    int index;

    if (outName && outNameCount > 0) {
        outName[0] = L'\0';
    }
    if (outVolume) {
        *outVolume = 0;
    }
    if (outSize) {
        *outSize = 0;
    }

    if (!app || !app->player) {
        return false;
    }

    p = app->player;
    index = sampleIndex1Based - 1;

    if (index < 0 || index >= 31) {
        return false;
    }

    if (outName && outNameCount > 0) {
        copy_wstr(outName, outNameCount, p->sampleNames[index]);
    }
    if (outVolume) {
        *outVolume = p->sampleVolumes[index];
    }
    if (outSize) {
        *outSize = p->sampleSizes[index];
    }

    return true;
}

bool player_get_sample_preview(const AppState *app, int sampleIndex1Based, const SamplePreviewPoint **outPreview, int *outCount)
{
    const PlayerState *p;
    int index;

    if (outPreview) {
        *outPreview = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }

    if (!app || !app->player) {
        return false;
    }

    p = app->player;
    index = sampleIndex1Based - 1;

    if (index < 0 || index >= 31) {
        return false;
    }

    if (outPreview) {
        *outPreview = p->samplePreviews[index];
    }
    if (outCount) {
        *outCount = p->samplePreviewCounts[index];
    }

    return true;
}

int player_get_nonempty_sample_count(const AppState *app)
{
    if (!app || !app->player) {
        return 0;
    }

    return app->player->nonEmptySampleCount;
}

int player_get_nonempty_sample_index(const AppState *app, int slotIndex)
{
    if (!app || !app->player) {
        return 0;
    }

    if (slotIndex < 0 || slotIndex >= app->player->nonEmptySampleCount) {
        return 0;
    }

    return app->player->nonEmptySampleIndices[slotIndex];
}

float player_get_recent_output_level(const AppState *app)
{
    if (!app || !app->player) {
        return 0.0f;
    }

    return app->player->recentOutputLevel;
}

bool player_get_recent_mono_window(const AppState *app, float *outSamples, int count)
{
    if (!app || !app->player) {
        return false;
    }

    return fill_recent_mono_window_from_playhead(app->player, outSamples, count);
}

bool player_get_quadrascope_state(const AppState *app, int channel1Based, QuadrascopeState *outState)
{
    const PlayerState *p;
    const ChannelScopeState *state;
    int index;

    if (!outState) {
        return false;
    }

    memset(outState, 0, sizeof(*outState));

    if (!app || !app->player) {
        return false;
    }

    p = app->player;
    index = channel1Based - 1;

    if (index < 0 || index >= 4) {
        return false;
    }

    state = &p->channelStates[index];
    outState->sampleIndex = state->sampleIndex;
    outState->samplePos = state->samplePos;
    outState->scopeStride = state->scopeStride;
    outState->sampleVolume = state->sampleVolume;
    outState->vu = state->vu;
    outState->active = state->active;
    outState->scopeHold = state->scopeHold;

    return true;
}

bool player_get_sample_values(const AppState *app, int sampleIndex1Based, const float **outValues, int *outCount, int *outLoopStart, int *outLoopLength)
{
    const PlayerState *p;
    int index;

    if (outValues) {
        *outValues = NULL;
    }
    if (outCount) {
        *outCount = 0;
    }
    if (outLoopStart) {
        *outLoopStart = 0;
    }
    if (outLoopLength) {
        *outLoopLength = 0;
    }

    if (!app || !app->player) {
        return false;
    }

    p = app->player;
    index = sampleIndex1Based - 1;

    if (index < 0 || index >= 31) {
        return false;
    }

    if (outValues) {
        *outValues = p->sampleValues[index];
    }
    if (outCount) {
        *outCount = p->sampleValueCounts[index];
    }
    if (outLoopStart) {
        *outLoopStart = p->sampleLoopStarts[index];
    }
    if (outLoopLength) {
        *outLoopLength = p->sampleLoopLengths[index];
    }

    return true;
}

bool player_load_module(AppState *app, const wchar_t *absolutePath, const wchar_t *displayName)
{
    PlayerState *p;
    unsigned char *bytes = NULL;
    size_t byteCount = 0;
    int errorCode = 0;
    const char *errorMessage = NULL;
    wchar_t titleWide[260];
    wchar_t errWide[512];

    if (!app || !app->player || !absolutePath || !displayName) {
        return false;
    }

    p = app->player;

    if (!p->api.dll) {
        app_set_status(app, L"OpenMPT runtime not available.");
        return false;
    }

    if (!read_file_bytes(absolutePath, &bytes, &byteCount)) {
        app_set_status(app, L"Could not open file: %ls", displayName);
        return false;
    }

    reset_audio_queue(p);
    destroy_modules(p);

    p->moduleBytes = bytes;
    p->moduleSize = byteCount;

    p->renderModule = p->api.create_from_memory2(
        p->moduleBytes,
        p->moduleSize,
        NULL,
        NULL,
        NULL,
        NULL,
        &errorCode,
        &errorMessage,
        NULL
    );

    if (!p->renderModule) {
        utf8_to_wide(errorMessage, errWide, sizeof(errWide) / sizeof(errWide[0]));
        app_set_status(app, L"libopenmpt render load failed: %ls", errWide[0] ? errWide : L"(no error text)");
        destroy_modules(p);
        return false;
    }

    errorCode = 0;
    errorMessage = NULL;

    p->uiModule = p->api.create_from_memory2(
        p->moduleBytes,
        p->moduleSize,
        NULL,
        NULL,
        NULL,
        NULL,
        &errorCode,
        &errorMessage,
        NULL
    );

    if (!p->uiModule) {
        utf8_to_wide(errorMessage, errWide, sizeof(errWide) / sizeof(errWide[0]));
        app_set_status(app, L"libopenmpt UI load failed: %ls", errWide[0] ? errWide : L"(no error text)");
        destroy_modules(p);
        return false;
    }

    p->api.set_repeat_count(p->renderModule, p->loopEnabled ? -1 : 0);
    p->api.set_repeat_count(p->uiModule, p->loopEnabled ? -1 : 0);
    apply_stereo_separation(app, p);

    copy_wstr(p->currentFile, sizeof(p->currentFile) / sizeof(p->currentFile[0]), absolutePath);
    copy_wstr(p->currentName, sizeof(p->currentName) / sizeof(p->currentName[0]), displayName);

    utf8_to_wide(p->api.get_metadata(p->uiModule, "title"), titleWide, sizeof(titleWide) / sizeof(titleWide[0]));
    if (titleWide[0] != L'\0') {
        copy_wstr(p->title, sizeof(p->title) / sizeof(p->title[0]), titleWide);
    } else {
        copy_wstr(p->title, sizeof(p->title) / sizeof(p->title[0]), displayName);
    }

    parse_mod_samples(p);
    reset_audio_history(p);

    p->duration = p->api.get_duration_seconds(p->uiModule);
    p->numOrders = p->api.get_num_orders(p->uiModule);
    p->playbackSeconds = 0.0;
    p->playbackBaseSeconds = 0.0;
    p->currentOrder = 0;
    p->currentPattern = 0;
    p->currentRow = 0;
    p->lastRow = -1;
    p->lastPattern = -1;
    p->bpm = 0;
    p->speed = 0;
    p->isPaused = false;
    p->isStopped = true;
    p->endOfFile = false;
    p->moduleLoaded = true;
    p->recentOutputLevel = 0.0f;
    p->hasPlayedSinceLoad = false;

    reset_channel_states(p);

    p->api.set_position_seconds(p->renderModule, 0.0);
    p->api.set_position_seconds(p->uiModule, 0.0);
    update_ui_position(p);

    for (int i = 0; i < 4; ++i) {
        p->channelStates[i].active = false;
        p->channelStates[i].scopeHold = 0.0;
        p->channelStates[i].samplePos = 1.0;
        p->channelStates[i].vu = 0.0f;
    }

    app->selectedSampleIndex = 0;
    app->sampleDisplayCurrentSlot = 0;
    app->sampleDisplayTimer = 0.0;

    update_playback_status(app);
    return true;
}

void player_play(AppState *app)
{
    PlayerState *p;

    if (!app || !app->player) {
        return;
    }

    p = app->player;

    if (!p->moduleLoaded) {
        app_set_status(app, L"Select a MOD first.");
        return;
    }

    if (!p->audioReady) {
        app_set_status(app, L"Audio output init failed.");
        return;
    }

    if (p->isPaused) {
        if (any_buffer_busy(p)) {
            waveOutRestart(p->waveOut);
        } else {
            reset_audio_queue(p);
            p->api.set_position_seconds(p->renderModule, p->playbackSeconds);
            p->api.set_position_seconds(p->uiModule, p->playbackSeconds);
            p->playbackBaseSeconds = p->playbackSeconds;
            p->endOfFile = false;
            prime_queue(p);
        }

        p->isPaused = false;
        p->isStopped = false;
        p->hasPlayedSinceLoad = true;
        update_playback_status(app);
        return;
    }

    if (p->isStopped) {
        reset_audio_queue(p);
        reset_audio_history(p);
        reset_channel_states(p);

        p->api.set_position_seconds(p->renderModule, p->playbackSeconds);
        p->api.set_position_seconds(p->uiModule, p->playbackSeconds);
        p->playbackBaseSeconds = p->playbackSeconds;
        p->endOfFile = false;

        p->lastRow = -1;
        p->lastPattern = -1;
        update_ui_position(p);

        prime_queue(p);

        p->isPaused = false;
        p->isStopped = false;
        p->hasPlayedSinceLoad = true;
        update_playback_status(app);
    }
}

void player_pause(AppState *app)
{
    PlayerState *p;

    if (!app || !app->player) {
        return;
    }

    p = app->player;

    if (!p->moduleLoaded || p->isStopped || !p->audioReady) {
        return;
    }

    p->playbackSeconds = get_output_playback_seconds(p);
    if (p->duration > 0.0) {
        if (p->loopEnabled) {
            while (p->playbackSeconds >= p->duration) {
                p->playbackSeconds -= p->duration;
            }
        } else if (p->playbackSeconds > p->duration) {
            p->playbackSeconds = p->duration;
        }
    }

    waveOutPause(p->waveOut);
    p->isPaused = true;

    for (int i = 0; i < 4; ++i) {
        p->channelStates[i].active = false;
        p->channelStates[i].scopeHold = 0.0;
        p->channelStates[i].samplePos = 1.0;
        p->channelStates[i].vu = 0.0f;
    }

    update_playback_status(app);
}

void player_stop(AppState *app)
{
    PlayerState *p;

    if (!app || !app->player) {
        return;
    }

    p = app->player;

    if (!p->moduleLoaded || !p->audioReady) {
        return;
    }

    reset_audio_queue(p);
    reset_audio_history(p);
    reset_channel_states(p);

    p->playbackSeconds = 0.0;
    p->playbackBaseSeconds = 0.0;
    p->endOfFile = false;
    p->isPaused = false;
    p->isStopped = true;
    p->lastRow = -1;
    p->lastPattern = -1;

    p->api.set_position_seconds(p->renderModule, 0.0);
    p->api.set_position_seconds(p->uiModule, 0.0);
    update_ui_position(p);

    update_playback_status(app);
}

bool player_jump_to_order(AppState *app, int step)
{
    PlayerState *p;
    int targetOrder;
    int targetPattern;
    double targetSeconds;

    if (!app || !app->player) {
        return false;
    }

    p = app->player;

    if (!p->moduleLoaded || !p->renderModule || !p->uiModule || p->numOrders <= 0) {
        return false;
    }

    targetOrder = p->currentOrder + step;
    if (targetOrder < 0) {
        targetOrder = 0;
    }
    if (targetOrder >= p->numOrders) {
        targetOrder = p->numOrders - 1;
    }

    if (targetOrder == p->currentOrder) {
        return false;
    }

    targetPattern = p->api.get_order_pattern(p->uiModule, targetOrder);
    if (targetPattern < 0) {
        return false;
    }

    targetSeconds = p->api.get_time_at_position(p->uiModule, targetOrder, 0);
    if (targetSeconds < 0.0) {
        return false;
    }

    reset_audio_queue(p);
    reset_audio_history(p);
    reset_channel_states(p);

    p->playbackSeconds = targetSeconds;
    p->playbackBaseSeconds = targetSeconds;
    p->endOfFile = false;
    p->lastRow = -1;
    p->lastPattern = -1;

    p->api.set_position_seconds(p->renderModule, targetSeconds);
    p->api.set_position_seconds(p->uiModule, targetSeconds);
    update_ui_position(p);

    if (!p->isStopped && !p->isPaused) {
        prime_queue(p);
    }

    return true;
}

bool player_restart_current_order(AppState *app)
{
    PlayerState *p;
    double targetSeconds;

    if (!app || !app->player) {
        return false;
    }

    p = app->player;

    if (!p->moduleLoaded || !p->renderModule || !p->uiModule || p->numOrders <= 0) {
        return false;
    }

    targetSeconds = p->api.get_time_at_position(p->uiModule, p->currentOrder, 0);
    if (targetSeconds < 0.0) {
        return false;
    }

    reset_audio_queue(p);
    reset_audio_history(p);
    reset_channel_states(p);

    p->playbackSeconds = targetSeconds;
    p->playbackBaseSeconds = targetSeconds;
    p->endOfFile = false;
    p->isPaused = false;
    p->isStopped = true;
    p->lastRow = -1;
    p->lastPattern = -1;

    p->api.set_position_seconds(p->renderModule, targetSeconds);
    p->api.set_position_seconds(p->uiModule, targetSeconds);
    update_ui_position(p);

    update_playback_status(app);
    return true;
}

void player_update(AppState *app, double dt)
{
    PlayerState *p;
    bool needsPrime = false;

    (void)dt;

    if (!app || !app->player) {
        return;
    }

    p = app->player;

    if (!p->moduleLoaded || !p->audioReady) {
        return;
    }

    if (!p->isPaused && !p->isStopped) {
        p->playbackSeconds = get_output_playback_seconds(p);

        if (p->duration > 0.0) {
            if (p->loopEnabled) {
                while (p->playbackSeconds >= p->duration) {
                    p->playbackSeconds -= p->duration;
                    p->playbackBaseSeconds -= p->duration;
                    if (p->playbackBaseSeconds < 0.0) {
                        p->playbackBaseSeconds = 0.0;
                    }
                }
            } else if (p->playbackSeconds >= p->duration && !any_buffer_busy(p) && p->endOfFile) {
                p->playbackSeconds = p->duration;
                p->isStopped = true;
            }
        }

        for (int i = 0; i < p->poolSize; ++i) {
            AudioBuffer *b = &p->buffers[i];
            if (!b->submitted || (b->header.dwFlags & WHDR_DONE) != 0) {
                needsPrime = true;
            }
        }

        if (needsPrime && !p->endOfFile) {
            prime_queue(p);
        }

        if (p->endOfFile && !any_buffer_busy(p) && !p->loopEnabled) {
            p->isStopped = true;
        }
    }

    update_ui_position(p);

    if (p->isStopped || p->isPaused) {
        p->recentOutputLevel = 0.0f;
        for (int i = 0; i < 4; ++i) {
            p->channelStates[i].vu = 0.0f;
        }
        update_playback_status(app);
        return;
    }

    p->recentOutputLevel = get_playhead_aligned_recent_level(p);

    for (int i = 0; i < 4; ++i) {
        double posAdvance;
        ChannelScopeState *state = &p->channelStates[i];
        state->vu = 0.0f;

        if (p->api.get_current_channel_vu_mono) {
            state->vu = (float)clampf_local((float)p->api.get_current_channel_vu_mono(p->uiModule, i), 0.0f, 1.0f);
        }

        if (state->scopeHold > 0.0) {
            state->scopeHold -= dt;
            if (state->scopeHold < 0.0) {
                state->scopeHold = 0.0;
            }
        }

        if (!state->active || state->sampleIndex <= 0 || state->sampleIndex > 31) {
            continue;
        }

        if (state->sampleVolume <= 0.0001f && state->scopeHold <= 0.0) {
            state->active = false;
            state->samplePos = 1.0;
            continue;
        }

        if (state->frequency <= 0.0) {
            continue;
        }

        posAdvance = state->frequency * dt * SCOPE_ADVANCE_SCALE;
        if (posAdvance <= 0.0) {
            continue;
        }

        state->samplePos += posAdvance;
    }

    update_playback_status(app);
}

void player_draw_songinfo(AppState *app, HDC hdc)
{
    PlayerState *p;
    wchar_t posText[32];
    wchar_t posSuffix[32];
    wchar_t patternText[32];
    wchar_t lengthText[32];
    wchar_t bpmText[32];
    wchar_t speedText[32];
    SIZE currentPosSize;
    const wchar_t *title;

    if (!app || !app->player || !hdc) {
        return;
    }

    p = app->player;
    if (!p->moduleLoaded) {
        return;
    }

    title = (p->title[0] != L'\0') ? p->title : p->currentName;

    swprintf(posText, sizeof(posText) / sizeof(posText[0]), L"%02d", p->currentOrder);
    swprintf(posSuffix, sizeof(posSuffix) / sizeof(posSuffix[0]), L"/%02d", p->numOrders);
    swprintf(patternText, sizeof(patternText) / sizeof(patternText[0]), L"%02d", p->currentPattern);
    swprintf(lengthText, sizeof(lengthText) / sizeof(lengthText[0]), L"%02d", p->numOrders);
    swprintf(bpmText, sizeof(bpmText) / sizeof(bpmText[0]), L"%d", p->bpm);
    swprintf(speedText, sizeof(speedText) / sizeof(speedText[0]), L"%d", p->speed);

    ui_draw_shadowed_text(hdc, app->fonts.info, title, 929, 3, RGB(0xFF, 0xFF, 0xFF), RGB(0x59, 0x59, 0x59), 3, 3, NULL, 0);

    SelectObject(hdc, app->fonts.info2);
    GetTextExtentPoint32W(hdc, posText, (int)wcslen(posText), &currentPosSize);

    ui_draw_shadowed_text(hdc, app->fonts.info2, posText, 931, 44, RGB(0xFF, 0xFF, 0xFF), RGB(0x59, 0x59, 0x59), 3, 3, NULL, 0);
    ui_draw_shadowed_text(hdc, app->fonts.info2, posSuffix, 931 + currentPosSize.cx, 44, COLOR_INFO, RGB(0x59, 0x59, 0x59), 3, 3, NULL, 0);

    ui_draw_shadowed_text(hdc, app->fonts.info2, patternText, 931, 74, RGB(0xFF, 0xFF, 0xFF), RGB(0x59, 0x59, 0x59), 3, 3, NULL, 0);
    ui_draw_shadowed_text(hdc, app->fonts.info2, lengthText, 931, 104, RGB(0xFF, 0xFF, 0xFF), RGB(0x59, 0x59, 0x59), 3, 3, NULL, 0);
    ui_draw_shadowed_text(hdc, app->fonts.info2, bpmText, 931, 134, RGB(0xFF, 0xFF, 0xFF), RGB(0x59, 0x59, 0x59), 3, 3, NULL, 0);
    ui_draw_shadowed_text(hdc, app->fonts.info2, speedText, 931, 164, RGB(0xFF, 0xFF, 0xFF), RGB(0x59, 0x59, 0x59), 3, 3, NULL, 0);
}
