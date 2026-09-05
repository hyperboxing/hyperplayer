#include "player.h"
#include "ui.h"
#include "pt2play/pt2play.h"

#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PLAYER_SAMPLE_RATE 44100
#define PLAYER_BUFFER_FRAMES 1024
#define PLAYER_POOL_SIZE 8
#define SAMPLE_PREVIEW_WIDTH 555
#define AUDIO_HISTORY_SIZE 131072
#define PAULA_PAL_CLOCK (28375160.0 / 8.0)

typedef struct AudioBuffer
{
	WAVEHDR header;
	int16_t *samples;
	bool prepared, submitted;
	uint64_t absoluteStart;
	PT2PlaySnapshot snapshot;
} AudioBuffer;

struct PlayerState
{
	wchar_t currentFile[MAX_PATH], currentName[260], title[260];
	wchar_t sampleNames[31][64];
	int sampleVolumes[31], sampleSizes[31], sampleLoopStarts[31], sampleLoopLengths[31];
	float *sampleValues[31];
	int sampleValueCounts[31];
	SamplePreviewPoint samplePreviews[31][SAMPLE_PREVIEW_WIDTH];
	int samplePreviewCounts[31], nonEmptySampleIndices[31], nonEmptySampleCount;

	float recentOutputLevel;
	float historyMono[AUDIO_HISTORY_SIZE];
	int historyWriteIndex;
	uint64_t totalHistorySamplesWritten;

	bool engineReady, moduleLoaded, isPaused, isStopped, loopEnabled, audioReady, hasPlayedSinceLoad;
	int numOrders, currentOrder, currentPattern, currentRow, bpm, speed;
	PT2PlaySnapshot audibleSnapshot;
	uint32_t lastTriggerSerial[4];
	double scopeHold[4];

	HWAVEOUT waveOut;
	int sampleRate, bufferFrames, poolSize;
	AudioBuffer buffers[PLAYER_POOL_SIZE];
};

static const COLORREF COLOR_INFO = RGB(0xBB, 0xBB, 0xBB);
static const char *NOTE_NAMES[12] =
{
	"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};
static const uint16_t BASE_PERIODS[36] =
{
	856,808,762,720,678,640,604,570,538,508,480,453,
	428,404,381,360,339,320,302,285,269,254,240,226,
	214,202,190,180,170,160,151,143,135,127,120,113
};

static void copy_wstr(wchar_t *dst, size_t dstCount, const wchar_t *src)
{
	if (dst == NULL || dstCount == 0)
		return;
	if (src == NULL)
		src = L"";
	wcsncpy(dst, src, dstCount - 1);
	dst[dstCount - 1] = L'\0';
}

static void ansi_to_wide(const char *src, wchar_t *dst, size_t dstCount)
{
	if (dst == NULL || dstCount == 0)
		return;
	dst[0] = L'\0';
	if (src == NULL || src[0] == '\0')
		return;

	int result = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dstCount);
	if (result == 0)
	{
		size_t i = 0;
		while (src[i] != '\0' && i + 1 < dstCount)
		{
			dst[i] = (wchar_t)(unsigned char)src[i];
			i++;
		}
		dst[i] = L'\0';
	}
	dst[dstCount - 1] = L'\0';
}

static void trim_wide_right(wchar_t *text)
{
	if (text == NULL)
		return;
	size_t length = wcslen(text);
	while (length > 0 && (text[length - 1] == L' ' || text[length - 1] == L'\t'))
		text[--length] = L'\0';
}

static bool read_file_bytes(const wchar_t *path, uint8_t **outBytes, size_t *outSize)
{
	*outBytes = NULL;
	*outSize = 0;
	FILE *file = _wfopen(path, L"rb");
	if (file == NULL)
		return false;
	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return false;
	}
	const long fileSize = ftell(file);
	if (fileSize <= 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return false;
	}
	uint8_t *bytes = (uint8_t *)malloc((size_t)fileSize);
	if (bytes == NULL)
	{
		fclose(file);
		return false;
	}
	const size_t readCount = fread(bytes, 1, (size_t)fileSize, file);
	fclose(file);
	if (readCount != (size_t)fileSize)
	{
		free(bytes);
		return false;
	}
	*outBytes = bytes;
	*outSize = readCount;
	return true;
}

static void reset_audio_history(PlayerState *player)
{
	memset(player->historyMono, 0, sizeof(player->historyMono));
	player->historyWriteIndex = 0;
	player->totalHistorySamplesWritten = 0;
	player->recentOutputLevel = 0.0f;
}

static void clear_sample_data(PlayerState *player)
{
	for (int i = 0; i < 31; i++)
	{
		free(player->sampleValues[i]);
		player->sampleValues[i] = NULL;
		player->sampleNames[i][0] = L'\0';
		player->sampleVolumes[i] = 0;
		player->sampleSizes[i] = 0;
		player->sampleLoopStarts[i] = 0;
		player->sampleLoopLengths[i] = 0;
		player->sampleValueCounts[i] = 0;
		player->samplePreviewCounts[i] = 0;
		memset(player->samplePreviews[i], 0, sizeof(player->samplePreviews[i]));
	}
	player->nonEmptySampleCount = 0;
}

static void build_sample_preview(const float *values, int length, SamplePreviewPoint *preview, int *count)
{
	*count = 0;
	if (values == NULL || length <= 0)
		return;

	for (int x = 0; x < SAMPLE_PREVIEW_WIDTH; x++)
	{
		int start = (x * length) / SAMPLE_PREVIEW_WIDTH;
		int end = (((x + 1) * length) / SAMPLE_PREVIEW_WIDTH) - 1;
		if (start >= length)
			start = length - 1;
		if (end < start)
			end = start;
		if (end >= length)
			end = length - 1;

		float minimum = 1.0f, maximum = -1.0f;
		for (int i = start; i <= end; i++)
		{
			if (values[i] < minimum) minimum = values[i];
			if (values[i] > maximum) maximum = values[i];
		}
		preview[x].minValue = minimum;
		preview[x].maxValue = maximum;
	}
	*count = SAMPLE_PREVIEW_WIDTH;
}

static bool cache_module_samples(PlayerState *player)
{
	clear_sample_data(player);
	for (int i = 0; i < 31; i++)
	{
		PT2PlaySample sample;
		if (!pt2play_get_sample(i + 1, &sample))
			return false;
		ansi_to_wide(sample.name, player->sampleNames[i], 64);
		trim_wide_right(player->sampleNames[i]);
		player->sampleVolumes[i] = sample.volume;
		player->sampleSizes[i] = sample.length;
		player->sampleLoopStarts[i] = sample.loopStart;
		player->sampleLoopLengths[i] = sample.loopLength;

		if (sample.length > 0)
		{
			player->sampleValues[i] = (float *)malloc((size_t)sample.length * sizeof(float));
			if (player->sampleValues[i] == NULL)
			{
				clear_sample_data(player);
				return false;
			}
			for (int j = 0; j < sample.length; j++)
				player->sampleValues[i][j] = sample.data[j] / 128.0f;
			player->sampleValueCounts[i] = sample.length;
			build_sample_preview(player->sampleValues[i], sample.length,
				player->samplePreviews[i], &player->samplePreviewCounts[i]);
			if (sample.length > 1)
				player->nonEmptySampleIndices[player->nonEmptySampleCount++] = i + 1;
		}
	}
	return true;
}

static bool create_audio_output(PlayerState *player)
{
	WAVEFORMATEX format;
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.nSamplesPerSec = PLAYER_SAMPLE_RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 4;
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

	if (waveOutOpen(&player->waveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
		return false;

	for (int i = 0; i < PLAYER_POOL_SIZE; i++)
	{
		AudioBuffer *buffer = &player->buffers[i];
		buffer->samples = (int16_t *)calloc((size_t)PLAYER_BUFFER_FRAMES * 2, sizeof(int16_t));
		if (buffer->samples == NULL)
			return false;
		buffer->header.lpData = (LPSTR)buffer->samples;
		buffer->header.dwBufferLength = PLAYER_BUFFER_FRAMES * 2 * sizeof(int16_t);
		if (waveOutPrepareHeader(player->waveOut, &buffer->header, sizeof(buffer->header)) != MMSYSERR_NOERROR)
			return false;
		buffer->prepared = true;
	}

	player->audioReady = true;
	return true;
}

static void close_audio_output(PlayerState *player)
{
	if (player->waveOut != NULL)
		waveOutReset(player->waveOut);
	for (int i = 0; i < PLAYER_POOL_SIZE; i++)
	{
		AudioBuffer *buffer = &player->buffers[i];
		if (player->waveOut != NULL && buffer->prepared)
			waveOutUnprepareHeader(player->waveOut, &buffer->header, sizeof(buffer->header));
		free(buffer->samples);
		memset(buffer, 0, sizeof(*buffer));
	}
	if (player->waveOut != NULL)
	{
		waveOutClose(player->waveOut);
		player->waveOut = NULL;
	}
	player->audioReady = false;
}

static void reset_audio_queue(PlayerState *player)
{
	if (player->waveOut != NULL)
		waveOutReset(player->waveOut);
	for (int i = 0; i < player->poolSize; i++)
	{
		player->buffers[i].submitted = false;
		player->buffers[i].absoluteStart = 0;
		memset(&player->buffers[i].snapshot, 0, sizeof(PT2PlaySnapshot));
	}
}

static uint64_t get_played_frames(PlayerState *player)
{
	if (player == NULL || player->waveOut == NULL)
		return 0;
	MMTIME time;
	memset(&time, 0, sizeof(time));
	time.wType = TIME_SAMPLES;
	if (waveOutGetPosition(player->waveOut, &time, sizeof(time)) != MMSYSERR_NOERROR)
		return 0;
	if (time.wType == TIME_SAMPLES) return time.u.sample;
	if (time.wType == TIME_BYTES) return time.u.cb / 4;
	if (time.wType == TIME_MS) return ((uint64_t)time.u.ms * player->sampleRate) / 1000;
	return 0;
}

static bool any_buffer_busy(const PlayerState *player)
{
	for (int i = 0; i < player->poolSize; i++)
	{
		if (player->buffers[i].submitted && (player->buffers[i].header.dwFlags & WHDR_DONE) == 0)
			return true;
	}
	return false;
}

static bool queue_buffer(PlayerState *player, int index)
{
	AudioBuffer *buffer = &player->buffers[index];
	if (buffer->submitted && (buffer->header.dwFlags & WHDR_DONE) == 0)
		return false;

	buffer->submitted = false;
	buffer->absoluteStart = player->totalHistorySamplesWritten;
	pt2play_get_snapshot(&buffer->snapshot);
	pt2play_render(buffer->samples, player->bufferFrames);

	double levelSum = 0.0;
	for (int i = 0; i < player->bufferFrames; i++)
	{
		const float left = buffer->samples[i * 2] / 32768.0f;
		const float right = buffer->samples[i * 2 + 1] / 32768.0f;
		const float mono = (left + right) * 0.5f;
		player->historyMono[player->historyWriteIndex++] = mono;
		if (player->historyWriteIndex >= AUDIO_HISTORY_SIZE)
			player->historyWriteIndex = 0;
		player->totalHistorySamplesWritten++;
		levelSum += fabsf(mono);
	}
	player->recentOutputLevel = (float)(levelSum / player->bufferFrames);

	buffer->header.dwBufferLength = player->bufferFrames * 2 * sizeof(int16_t);
	if (waveOutWrite(player->waveOut, &buffer->header, sizeof(buffer->header)) != MMSYSERR_NOERROR)
		return false;
	buffer->submitted = true;
	return true;
}

static void prime_queue(PlayerState *player)
{
	for (int i = 0; i < player->poolSize; i++)
		queue_buffer(player, i);
}

static const AudioBuffer *find_audible_buffer(const PlayerState *player, uint64_t played)
{
	const AudioBuffer *best = NULL;
	for (int i = 0; i < player->poolSize; i++)
	{
		const AudioBuffer *buffer = &player->buffers[i];
		if (!buffer->submitted)
			continue;
		if (played >= buffer->absoluteStart && played < buffer->absoluteStart + (uint64_t)player->bufferFrames)
			return buffer;
		if (buffer->absoluteStart <= played && (best == NULL || buffer->absoluteStart > best->absoluteStart))
			best = buffer;
	}
	return best;
}

static void advance_snapshot_scope_positions(PlayerState *player, PT2PlaySnapshot *snapshot, uint64_t frames)
{
	for (int i = 0; i < 4; i++)
	{
		PT2PlayChannel *channel = &snapshot->channels[i];
		if (!channel->active || channel->sample < 1 || channel->sample > 31 || channel->period == 0)
			continue;
		const int sampleIndex = channel->sample - 1;
		const int length = player->sampleValueCounts[sampleIndex];
		if (length <= 0)
			continue;
		channel->samplePosition += (PAULA_PAL_CLOCK / channel->period) * ((double)frames / player->sampleRate);
		const int loopStart = player->sampleLoopStarts[sampleIndex];
		const int loopLength = player->sampleLoopLengths[sampleIndex];
		if (loopLength > 2 && loopStart >= 0 && loopStart + loopLength <= length)
		{
			const double loopEnd = loopStart + loopLength;
			while (channel->samplePosition >= loopEnd)
				channel->samplePosition -= loopLength;
		}
		else if (channel->samplePosition >= length)
		{
			channel->samplePosition = length - 1;
			channel->active = false;
		}
	}
}

static void update_audible_snapshot(PlayerState *player, double dt)
{
	for (int i = 0; i < 4; i++)
	{
		player->scopeHold[i] -= dt;
		if (player->scopeHold[i] < 0.0)
			player->scopeHold[i] = 0.0;
	}

	if (player->isStopped || player->isPaused || !any_buffer_busy(player))
		return;
	const uint64_t played = get_played_frames(player);
	const AudioBuffer *buffer = find_audible_buffer(player, played);
	if (buffer == NULL)
		return;
	player->audibleSnapshot = buffer->snapshot;
	uint64_t offset = played > buffer->absoluteStart ? played - buffer->absoluteStart : 0;
	if (offset > (uint64_t)player->bufferFrames)
		offset = player->bufferFrames;
	advance_snapshot_scope_positions(player, &player->audibleSnapshot, offset);

	player->currentOrder = player->audibleSnapshot.order;
	player->currentPattern = player->audibleSnapshot.pattern;
	player->currentRow = player->audibleSnapshot.row;
	player->speed = player->audibleSnapshot.speed;
	player->bpm = player->audibleSnapshot.bpm;
	for (int i = 0; i < 4; i++)
	{
		const uint32_t serial = player->audibleSnapshot.channels[i].triggerSerial;
		if (serial != player->lastTriggerSerial[i])
		{
			player->lastTriggerSerial[i] = serial;
			player->scopeHold[i] = 0.20;
		}
	}
}

static void set_snapshot_from_engine(PlayerState *player)
{
	pt2play_get_snapshot(&player->audibleSnapshot);
	player->currentOrder = player->audibleSnapshot.order;
	player->currentPattern = player->audibleSnapshot.pattern;
	player->currentRow = player->audibleSnapshot.row;
	player->speed = player->audibleSnapshot.speed;
	player->bpm = player->audibleSnapshot.bpm;
}

static void update_status(AppState *app)
{
	PlayerState *player = app->player;
	if (!player->moduleLoaded)
		return;
	if (player->isPaused)
		app_set_status(app, L"Paused.");
	else if (player->isStopped)
		app_set_status(app, player->hasPlayedSinceLoad ? L"Stopped playing." : L"Loaded: %ls", player->currentName);
	else
		app_set_status(app, L"Playing %ls", player->currentName);
}

bool player_init(AppState *app)
{
	if (app == NULL)
		return false;
	PlayerState *player = (PlayerState *)calloc(1, sizeof(PlayerState));
	if (player == NULL)
		return false;
	player->loopEnabled = true;
	player->isStopped = true;
	player->sampleRate = PLAYER_SAMPLE_RATE;
	player->bufferFrames = PLAYER_BUFFER_FRAMES;
	player->poolSize = PLAYER_POOL_SIZE;
	int separation = app->config.stereoSeparation;
	if (separation < 0) separation = 0;
	if (separation > 100) separation = 100;
	player->engineReady = pt2play_init(PLAYER_SAMPLE_RATE, (uint8_t)separation);
	app->player = player;
	if (!create_audio_output(player))
		close_audio_output(player);
	return player->engineReady;
}

void player_shutdown(AppState *app)
{
	if (app == NULL || app->player == NULL)
		return;
	PlayerState *player = app->player;
	reset_audio_queue(player);
	close_audio_output(player);
	clear_sample_data(player);
	pt2play_shutdown();
	free(player);
	app->player = NULL;
}

bool player_is_available(const AppState *app) { return app != NULL && app->player != NULL && app->player->engineReady; }
bool player_is_loaded(const AppState *app) { return app != NULL && app->player != NULL && app->player->moduleLoaded; }
bool player_is_paused(const AppState *app) { return app != NULL && app->player != NULL && app->player->isPaused; }
bool player_is_stopped(const AppState *app) { return app != NULL && app->player != NULL && app->player->isStopped; }

const wchar_t *player_get_current_name(const AppState *app) { return player_is_loaded(app) ? app->player->currentName : L""; }
const wchar_t *player_get_current_file_path(const AppState *app) { return player_is_loaded(app) ? app->player->currentFile : L""; }
int player_get_num_orders(const AppState *app) { return player_is_loaded(app) ? app->player->numOrders : 0; }
int player_get_current_order(const AppState *app) { return player_is_loaded(app) ? app->player->currentOrder : 0; }
int player_get_current_pattern(const AppState *app) { return player_is_loaded(app) ? app->player->currentPattern : 0; }
int player_get_current_row(const AppState *app) { return player_is_loaded(app) ? app->player->currentRow : 0; }
bool player_get_loop_enabled(const AppState *app) { return app != NULL && app->player != NULL && app->player->loopEnabled; }

int player_get_order_pattern(const AppState *app, int order)
{
	return player_is_loaded(app) ? pt2play_get_order_pattern(order) : -1;
}

int player_get_pattern_num_rows(const AppState *app, int pattern)
{
	return player_is_loaded(app) && pattern >= 0 && pattern < 100 ? 64 : 0;
}

int player_get_num_channels(const AppState *app) { return player_is_loaded(app) ? 4 : 0; }

static void format_note(uint16_t period, char noteText[4])
{
	if (period == 0)
	{
		memcpy(noteText, "---", 4);
		return;
	}
	int best = 0, bestDistance = INT_MAX;
	for (int i = 0; i < 36; i++)
	{
		const int distance = abs((int)period - BASE_PERIODS[i]);
		if (distance < bestDistance)
		{
			best = i;
			bestDistance = distance;
		}
	}
	noteText[0] = NOTE_NAMES[best % 12][0];
	noteText[1] = NOTE_NAMES[best % 12][1];
	noteText[2] = (char)('1' + (best / 12));
	noteText[3] = '\0';
}

bool player_format_pattern_cell(const AppState *app, int pattern, int row, int channel, wchar_t *outText, size_t outCount)
{
	if (outText == NULL || outCount == 0)
		return false;
	copy_wstr(outText, outCount, L"--- 00000");
	if (!player_is_loaded(app))
		return false;
	const PT2PlayCell *cell = pt2play_get_cell(pattern, row, channel);
	if (cell == NULL)
		return false;
	char note[4], text[16];
	format_note(cell->period, note);
	snprintf(text, sizeof(text), "%s %02X%1X%02X", note, cell->sample, cell->command, cell->param);
	ansi_to_wide(text, outText, outCount);
	return true;
}

bool player_get_sample_info(const AppState *app, int index1, wchar_t *name, size_t nameCount, int *volume, int *size)
{
	if (name != NULL && nameCount > 0) name[0] = L'\0';
	if (volume != NULL) *volume = 0;
	if (size != NULL) *size = 0;
	if (app == NULL || app->player == NULL || index1 < 1 || index1 > 31)
		return false;
	const int index = index1 - 1;
	if (name != NULL) copy_wstr(name, nameCount, app->player->sampleNames[index]);
	if (volume != NULL) *volume = app->player->sampleVolumes[index];
	if (size != NULL) *size = app->player->sampleSizes[index];
	return true;
}

bool player_get_sample_preview(const AppState *app, int index1, const SamplePreviewPoint **preview, int *count)
{
	if (preview != NULL) *preview = NULL;
	if (count != NULL) *count = 0;
	if (app == NULL || app->player == NULL || index1 < 1 || index1 > 31)
		return false;
	const int index = index1 - 1;
	if (preview != NULL) *preview = app->player->samplePreviews[index];
	if (count != NULL) *count = app->player->samplePreviewCounts[index];
	return true;
}

int player_get_nonempty_sample_count(const AppState *app) { return app != NULL && app->player != NULL ? app->player->nonEmptySampleCount : 0; }
int player_get_nonempty_sample_index(const AppState *app, int slot)
{
	if (app == NULL || app->player == NULL || slot < 0 || slot >= app->player->nonEmptySampleCount)
		return 0;
	return app->player->nonEmptySampleIndices[slot];
}

float player_get_recent_output_level(const AppState *app)
{
	return app != NULL && app->player != NULL ? app->player->recentOutputLevel : 0.0f;
}

bool player_get_recent_mono_window(const AppState *app, float *samples, int count)
{
	if (samples == NULL || count <= 0)
		return false;
	memset(samples, 0, (size_t)count * sizeof(float));
	if (!player_is_loaded(app))
		return false;
	PlayerState *player = app->player;
	uint64_t played = get_played_frames(player);
	if (played > player->totalHistorySamplesWritten) played = player->totalHistorySamplesWritten;
	const int available = played > (uint64_t)count ? count : (int)played;
	const int missing = count - available;
	const uint64_t start = played - (uint64_t)available;
	for (int i = 0; i < available; i++)
		samples[missing + i] = player->historyMono[(start + (uint64_t)i) % AUDIO_HISTORY_SIZE];
	return true;
}

bool player_get_quadrascope_state(const AppState *app, int channel1, QuadrascopeState *state)
{
	if (state == NULL)
		return false;
	memset(state, 0, sizeof(*state));
	if (!player_is_loaded(app) || channel1 < 1 || channel1 > 4)
		return false;
	const int index = channel1 - 1;
	const PT2PlayChannel *source = &app->player->audibleSnapshot.channels[index];
	state->sampleIndex = source->sample;
	state->samplePos = source->samplePosition;
	state->scopeStride = 1;
	state->sampleVolume = source->volume;
	state->active = source->active && !app->player->isPaused && !app->player->isStopped;
	state->scopeHold = app->player->scopeHold[index];
	state->triggerSerial = source->triggerSerial;
	if (source->sample >= 1 && source->sample <= 31)
	{
		const int sampleIndex = source->sample - 1;
		const int count = app->player->sampleValueCounts[sampleIndex];
		if (count > 0)
		{
			int pos = (int)source->samplePosition;
			if (pos < 0) pos = 0;
			if (pos >= count) pos = count - 1;
			state->vu = fabsf(app->player->sampleValues[sampleIndex][pos]) * source->volume;
		}
	}
	return true;
}

bool player_get_sample_values(const AppState *app, int index1, const float **values, int *count, int *loopStart, int *loopLength)
{
	if (values != NULL) *values = NULL;
	if (count != NULL) *count = 0;
	if (loopStart != NULL) *loopStart = 0;
	if (loopLength != NULL) *loopLength = 0;
	if (app == NULL || app->player == NULL || index1 < 1 || index1 > 31)
		return false;
	const int index = index1 - 1;
	if (values != NULL) *values = app->player->sampleValues[index];
	if (count != NULL) *count = app->player->sampleValueCounts[index];
	if (loopStart != NULL) *loopStart = app->player->sampleLoopStarts[index];
	if (loopLength != NULL) *loopLength = app->player->sampleLoopLengths[index];
	return true;
}

bool player_load_module(AppState *app, const wchar_t *path, const wchar_t *displayName)
{
	if (app == NULL || app->player == NULL || path == NULL || displayName == NULL)
		return false;
	PlayerState *player = app->player;
	uint8_t *bytes;
	size_t size;
	if (!read_file_bytes(path, &bytes, &size))
	{
		app_set_status(app, L"Could not open file: %ls", displayName);
		return false;
	}

	reset_audio_queue(player);
	reset_audio_history(player);
	pt2play_unload();
	player->moduleLoaded = false;
	char error[256];
	const bool loaded = pt2play_load(bytes, size, error, sizeof(error));
	free(bytes);
	if (!loaded)
	{
		wchar_t wideError[256];
		ansi_to_wide(error, wideError, 256);
		app_set_status(app, L"ProTracker MOD load failed: %ls", wideError);
		player->moduleLoaded = false;
		clear_sample_data(player);
		return false;
	}

	if (!cache_module_samples(player))
	{
		pt2play_unload();
		app_set_status(app, L"Not enough memory for sample display data.");
		return false;
	}
	copy_wstr(player->currentFile, MAX_PATH, path);
	copy_wstr(player->currentName, 260, displayName);
	ansi_to_wide(pt2play_get_title(), player->title, 260);
	trim_wide_right(player->title);
	if (player->title[0] == L'\0') copy_wstr(player->title, 260, displayName);
	player->numOrders = pt2play_get_num_orders();
	player->moduleLoaded = true;
	player->isPaused = false;
	player->isStopped = true;
	player->hasPlayedSinceLoad = false;
	memset(player->lastTriggerSerial, 0, sizeof(player->lastTriggerSerial));
	memset(player->scopeHold, 0, sizeof(player->scopeHold));
	set_snapshot_from_engine(player);
	app->selectedSampleIndex = 0;
	app->sampleDisplayCurrentSlot = 0;
	app->sampleDisplayTimer = 0.0;
	update_status(app);
	return true;
}

void player_play(AppState *app)
{
	if (app == NULL || app->player == NULL)
		return;
	PlayerState *player = app->player;
	if (!player->moduleLoaded)
	{
		app_set_status(app, L"Select a MOD first.");
		return;
	}
	if (!player->audioReady)
	{
		app_set_status(app, L"Audio output init failed.");
		return;
	}

	if (player->isPaused && any_buffer_busy(player))
	{
		waveOutRestart(player->waveOut);
		player->isPaused = false;
		player->isStopped = false;
		player->hasPlayedSinceLoad = true;
		update_status(app);
		return;
	}

	if (player->isStopped || player->isPaused)
	{
		const int order = player->currentOrder;
		const int row = player->currentRow;
		reset_audio_queue(player);
		reset_audio_history(player);
		pt2play_start(order, row);
		set_snapshot_from_engine(player);
		prime_queue(player);
		player->isPaused = false;
		player->isStopped = false;
		player->hasPlayedSinceLoad = true;
		update_status(app);
	}
}

void player_pause(AppState *app)
{
	if (app == NULL || app->player == NULL)
		return;
	PlayerState *player = app->player;
	if (!player->moduleLoaded || player->isStopped || !player->audioReady)
		return;
	waveOutPause(player->waveOut);
	player->isPaused = true;
	player->recentOutputLevel = 0.0f;
	update_status(app);
}

void player_stop(AppState *app)
{
	if (app == NULL || app->player == NULL || !app->player->moduleLoaded)
		return;
	PlayerState *player = app->player;
	reset_audio_queue(player);
	reset_audio_history(player);
	pt2play_set_order(0);
	pt2play_stop();
	set_snapshot_from_engine(player);
	memset(player->scopeHold, 0, sizeof(player->scopeHold));
	player->isPaused = false;
	player->isStopped = true;
	update_status(app);
}

bool player_jump_to_order(AppState *app, int step)
{
	if (app == NULL || app->player == NULL || !app->player->moduleLoaded)
		return false;
	PlayerState *player = app->player;
	int target = player->currentOrder + step;
	if (target < 0) target = 0;
	if (target >= player->numOrders) target = player->numOrders - 1;
	if (target == player->currentOrder)
		return false;
	const bool wasPlaying = !player->isStopped && !player->isPaused;
	reset_audio_queue(player);
	reset_audio_history(player);
	if (!pt2play_set_order(target))
		return false;
	if (wasPlaying)
		prime_queue(player);
	else
		pt2play_stop();
	set_snapshot_from_engine(player);
	return true;
}

bool player_restart_current_order(AppState *app)
{
	if (app == NULL || app->player == NULL || !app->player->moduleLoaded)
		return false;
	PlayerState *player = app->player;
	const int order = player->currentOrder;
	reset_audio_queue(player);
	reset_audio_history(player);
	if (!pt2play_set_order(order))
		return false;
	pt2play_stop();
	set_snapshot_from_engine(player);
	player->isPaused = false;
	player->isStopped = true;
	memset(player->scopeHold, 0, sizeof(player->scopeHold));
	update_status(app);
	return true;
}

void player_update(AppState *app, double dt)
{
	if (app == NULL || app->player == NULL || !app->player->moduleLoaded)
		return;
	PlayerState *player = app->player;
	if (!player->isPaused && !player->isStopped && player->audioReady)
	{
		for (int i = 0; i < player->poolSize; i++)
		{
			AudioBuffer *buffer = &player->buffers[i];
			if (!buffer->submitted || (buffer->header.dwFlags & WHDR_DONE) != 0)
				queue_buffer(player, i);
		}
	}
	update_audible_snapshot(player, dt);
	if (player->isPaused || player->isStopped)
		player->recentOutputLevel = 0.0f;
	else
	{
		uint64_t played = get_played_frames(player);
		if (played > player->totalHistorySamplesWritten) played = player->totalHistorySamplesWritten;
		const int count = played > 512 ? 512 : (int)played;
		double sum = 0.0;
		for (int i = 0; i < count; i++)
			sum += fabsf(player->historyMono[(played - (uint64_t)count + (uint64_t)i) % AUDIO_HISTORY_SIZE]);
		player->recentOutputLevel = count > 0 ? (float)(sum / count) : 0.0f;
	}
	update_status(app);
}

void player_draw_songinfo(AppState *app, HDC hdc)
{
	if (!player_is_loaded(app) || hdc == NULL)
		return;
	PlayerState *player = app->player;
	wchar_t pos[32], suffix[32], pattern[32], length[32], bpm[32], speed[32];
	SIZE posSize;
	swprintf(pos, 32, L"%02d", player->currentOrder);
	swprintf(suffix, 32, L"/%02d", player->numOrders);
	swprintf(pattern, 32, L"%02d", player->currentPattern);
	swprintf(length, 32, L"%02d", player->numOrders);
	swprintf(bpm, 32, L"%d", player->bpm);
	swprintf(speed, 32, L"%d", player->speed);
	ui_draw_shadowed_text(hdc, app->fonts.info, player->title, 929, 3, RGB(255,255,255), RGB(0x59,0x59,0x59), 3, 3, NULL, 0);
	SelectObject(hdc, app->fonts.info2);
	GetTextExtentPoint32W(hdc, pos, (int)wcslen(pos), &posSize);
	ui_draw_shadowed_text(hdc, app->fonts.info2, pos, 931, 44, RGB(255,255,255), RGB(0x59,0x59,0x59), 3, 3, NULL, 0);
	ui_draw_shadowed_text(hdc, app->fonts.info2, suffix, 931 + posSize.cx, 44, COLOR_INFO, RGB(0x59,0x59,0x59), 3, 3, NULL, 0);
	ui_draw_shadowed_text(hdc, app->fonts.info2, pattern, 931, 74, RGB(255,255,255), RGB(0x59,0x59,0x59), 3, 3, NULL, 0);
	ui_draw_shadowed_text(hdc, app->fonts.info2, length, 931, 104, RGB(255,255,255), RGB(0x59,0x59,0x59), 3, 3, NULL, 0);
	ui_draw_shadowed_text(hdc, app->fonts.info2, bpm, 931, 134, RGB(255,255,255), RGB(0x59,0x59,0x59), 3, 3, NULL, 0);
	ui_draw_shadowed_text(hdc, app->fonts.info2, speed, 931, 164, RGB(255,255,255), RGB(0x59,0x59,0x59), 3, 3, NULL, 0);
}
