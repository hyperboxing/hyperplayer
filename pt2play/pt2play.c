 





#include "pt2play.h"
#include "pt2_internal.h"
#include "pt2_downsample2x.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const char pt2play_attribution[] =
	"Replay core derived from ProTracker 2.3D Clone by Olav \"8bitbubsy\" Sorensen; "
	"BLEP routines based on code by aciddose.";

#define PT2_MAX_SAMPLE_LENGTH 65534
#define PT2_RENDER_CHUNK 2048
#define INITIAL_DITHER_SEED 0x12345000U
#define STEREO_NORM_FACTOR 0.5f
#define AUDIO_GAIN 2.0
#define NORMALIZE_VALUE (float)(AUDIO_GAIN * ((INT16_MAX+1.0) / PAULA_VOICES))

enum
{
	FORMAT_UNKNOWN,
	FORMAT_PT,
	FORMAT_FLT,
	FORMAT_FT2,
	FORMAT_NT,
	FORMAT_HMNT
};

typedef struct visualVoice_t
{
	bool active;
	const int8_t *data;
	uint16_t length, period, volume;
	uint32_t triggerSerial;
} visualVoice_t;

module_t *song;
pt2_config_compat_t config;
pt2_editor_compat_t editor;
pt2_ui_compat_t ui;
pt2_cursor_compat_t cursor;
audio_t audio;

static visualVoice_t visuals[PAULA_VOICES];
static uint32_t outputRate;
static uint8_t stereoSeparation;
static float sideFactor;
static uint32_t randSeed;
static float prngStateL, prngStateR;
static float mixBufferL[PT2_RENDER_CHUNK * 2];
static float mixBufferR[PT2_RENDER_CHUNK * 2];

static void set_error(char *text, size_t textSize, const char *message)
{
	if (text == NULL || textSize == 0)
		return;

	if (message == NULL)
		message = "Invalid module";

	snprintf(text, textSize, "%s", message);
}

static uint16_t read_be16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static void free_module(module_t *m)
{
	if (m == NULL)
		return;

	for (int32_t i = 0; i < MAX_PATTERNS; i++)
		free(m->patterns[i]);

	free(m->sampleData);
	free(m);
}

static void fix_zeroes_in_string(char *text, uint32_t maxLength)
{
	int32_t i;
	for (i = (int32_t)maxLength - 1; i >= 0; i--)
	{
		if (text[i] != '\0')
			break;
	}

	if (i > 0)
	{
		for (int32_t j = 0; j < i; j++)
		{
			if (text[j] == '\0')
				text[j] = ' ';
		}
	}
}

static void sanitize_module(module_t *m)
{
	m->header.name[20] = '\0';
	for (int32_t i = 0; i < 20; i++)
	{
		unsigned char c = (unsigned char)m->header.name[i];
		if ((c < ' ' || c > '~') && c != '\0')
			m->header.name[i] = ' ';
	}
	fix_zeroes_in_string(m->header.name, 20);

	for (int32_t i = 0; i < MOD_SAMPLES; i++)
	{
		moduleSample_t *s = &m->samples[i];
		s->text[22] = '\0';
		for (int32_t j = 0; j < 22; j++)
		{
			unsigned char c = (unsigned char)s->text[j];
			if ((c < ' ' || c > '~') && c != '\0')
				s->text[j] = ' ';
		}
		fix_zeroes_in_string(s->text, 22);

		if (s->length > config.maxSampleLength)
			s->length = config.maxSampleLength;
		if ((uint8_t)s->volume > 64)
			s->volume = 64;
		if (s->loopLength < 2)
			s->loopLength = 2;

		if (s->loopStart < 0 || s->loopLength < 0 ||
			s->loopStart > config.maxSampleLength ||
			s->loopStart > config.maxSampleLength - s->loopLength)
		{
			s->loopStart = 0;
			s->loopLength = 2;
		}

		if (s->length > 0 && s->loopLength > 2 && s->loopStart + s->loopLength > s->length)
		{
			const int32_t overflow = (s->loopStart + s->loopLength) - s->length;
			if (overflow <= config.maxSampleLength - s->length)
				s->length += overflow;
			else
			{
				s->loopStart = 0;
				s->loopLength = 2;
			}
		}

		if (s->length >= 2 && s->loopStart + s->loopLength <= 2)
		{
			m->sampleData[s->offset + 0] = 0;
			m->sampleData[s->offset + 1] = 0;
		}
	}

	initializeModuleChannels(m);
}

static uint8_t get_mod31_type(const uint8_t *data, size_t size, uint8_t *numChannels)
{
	if (data == NULL || numChannels == NULL || size < 1084 + 1024)
		return FORMAT_UNKNOWN;

	const uint8_t *id = &data[1080];
	*numChannels = 4;
	if (!memcmp(id, "M.K.", 4) || !memcmp(id, "M!K!", 4) ||
		!memcmp(id, "NSMS", 4) || !memcmp(id, "LARD", 4) || !memcmp(id, "PATT", 4))
		return FORMAT_PT;
	if (!memcmp(id, "FLT4", 4))
		return FORMAT_FLT;
	if (!memcmp(id, "N.T.", 4))
		return FORMAT_NT;
	if (!memcmp(id, "M&K!", 4) || !memcmp(id, "FEST", 4))
		return FORMAT_HMNT;
	if (id[0] >= '0' && id[0] <= '9' && id[1] == 'C' && id[2] == 'H' && id[3] == 'N')
	{
		*numChannels = id[0] - '0';
		return FORMAT_FT2;
	}

	return FORMAT_UNKNOWN;
}

static bool validate_payload(size_t headerSize, int32_t numPatterns, int32_t channels,
	const int32_t *sampleLengths, int32_t sampleCount, size_t fileSize, size_t *sampleOffset)
{
	if (numPatterns < 1 || numPatterns > MAX_PATTERNS || channels != PAULA_VOICES)
		return false;

	const size_t patternBytes = (size_t)numPatterns * MOD_ROWS * channels * 4;
	if (headerSize > fileSize || patternBytes > fileSize - headerSize)
		return false;

	size_t cursor = headerSize + patternBytes;
	for (int32_t i = 0; i < sampleCount; i++)
	{
		if (sampleLengths[i] < 0 || (size_t)sampleLengths[i] > fileSize - cursor)
			return false;
		cursor += (size_t)sampleLengths[i];
	}

	*sampleOffset = headerSize + patternBytes;
	return true;
}

static module_t *load_mod31(const uint8_t *data, size_t size, char *errorText, size_t errorTextSize)
{
	uint8_t channels;
	const uint8_t format = get_mod31_type(data, size, &channels);
	if (format == FORMAT_UNKNOWN || channels != PAULA_VOICES)
	{
		set_error(errorText, errorTextSize, "Unsupported MOD signature or channel count");
		return NULL;
	}

	int32_t realLengths[MOD_SAMPLES];
	for (int32_t i = 0; i < MOD_SAMPLES; i++)
		realLengths[i] = (int32_t)read_be16(&data[20 + (i * 30) + 22]) * 2;

	uint16_t songLength = data[950];
	if (format == FORMAT_PT && songLength == 129)
		songLength = 127;
	if (songLength == 0 || songLength > 128)
	{
		set_error(errorText, errorTextSize, "Invalid MOD song length");
		return NULL;
	}

	int32_t numPatterns = 0;
	for (int32_t i = 0; i < 128; i++)
	{
		if (data[952 + i] > numPatterns)
			numPatterns = data[952 + i];
	}
	numPatterns++;

	size_t sampleDataOffset;
	if (!validate_payload(1084, numPatterns, channels, realLengths, MOD_SAMPLES, size, &sampleDataOffset))
	{
		set_error(errorText, errorTextSize, "Truncated or oversized MOD data");
		return NULL;
	}

	module_t *m = createEmptyMod();
	if (m == NULL)
	{
		set_error(errorText, errorTextSize, "Out of memory");
		return NULL;
	}

	memcpy(m->header.name, data, 20);
	for (int32_t i = 0; i < MOD_SAMPLES; i++)
	{
		const uint8_t *h = &data[20 + (i * 30)];
		moduleSample_t *s = &m->samples[i];
		memcpy(s->text, h, 22);
		s->length = realLengths[i] > config.maxSampleLength ? config.maxSampleLength : realLengths[i];
		s->fineTune = h[24] & 0x0F;
		s->volume = (int8_t)h[25];
		s->loopStart = (int32_t)read_be16(&h[26]) * 2;
		s->loopLength = (int32_t)read_be16(&h[28]) * 2;

		if (s->loopLength > 2 && s->loopStart + s->loopLength > s->length &&
			(s->loopStart / 2) + s->loopLength <= s->length)
		{
			s->loopStart /= 2;
		}
	}

	m->header.songLength = songLength;
	for (int32_t i = 0; i < 128; i++)
		m->header.patternTable[i] = data[952 + i];

	const uint8_t *p = &data[1084];
	for (int32_t pattern = 0; pattern < numPatterns; pattern++)
	{
		for (int32_t row = 0; row < MOD_ROWS; row++)
		{
			for (int32_t channel = 0; channel < PAULA_VOICES; channel++, p += 4)
			{
				note_t *note = &m->patterns[pattern][row * PAULA_VOICES + channel];
				note->period = (uint16_t)(((p[0] & 0x0F) << 8) | p[1]);
				note->sample = (uint8_t)(((p[0] & 0xF0) | (p[2] >> 4)) & 31);
				note->command = p[2] & 0x0F;
				note->param = p[3];
			}
		}
	}

	if (format != FORMAT_PT)
	{
		for (int32_t pattern = 0; pattern < numPatterns; pattern++)
		{
			for (int32_t i = 0; i < MOD_ROWS * PAULA_VOICES; i++)
			{
				note_t *note = &m->patterns[pattern][i];
				if ((format == FORMAT_NT || format == FORMAT_HMNT) && note->command == 0xD)
					note->param = 0;
				if ((format == FORMAT_NT || format == FORMAT_HMNT) && note->command == 0xF && note->param == 0)
					note->command = 0;
				if (format == FORMAT_FLT && note->command == 0xE)
					note->command = note->param = 0;
				if (format == FORMAT_FLT && note->command == 0xF && note->param > 0x1F)
					note->param = 0x1F;
				if (note->command == 0xE && ((note->param >> 4) == 0x8 || (note->param >> 4) == 0xF))
					note->command = note->param = 0;
			}
		}
	}

	size_t cursorPos = sampleDataOffset;
	for (int32_t i = 0; i < MOD_SAMPLES; i++)
	{
		moduleSample_t *s = &m->samples[i];
		if (s->length > 0)
			memcpy(&m->sampleData[s->offset], &data[cursorPos], (size_t)s->length);
		cursorPos += (size_t)realLengths[i];
	}

	m->header.initialTempo = 125;
	sanitize_module(m);
	return m;
}

static module_t *load_mod15(const uint8_t *data, size_t size, char *errorText, size_t errorTextSize)
{
	if (data == NULL || size < 600 + 1024)
	{
		set_error(errorText, errorTextSize, "File is too small to be a 15-sample MOD");
		return NULL;
	}

	int32_t realLengths[15];
	for (int32_t i = 0; i < 15; i++)
		realLengths[i] = (int32_t)read_be16(&data[20 + (i * 30) + 22]) * 2;

	const uint8_t songLength = data[470];
	uint8_t initTempo = data[471];
	if (songLength == 0 || songLength > 128 || initTempo > 220)
	{
		set_error(errorText, errorTextSize, "Invalid 15-sample MOD header");
		return NULL;
	}

	int32_t numPatterns = 0;
	for (int32_t i = 0; i < 128; i++)
	{
		if (data[472 + i] > numPatterns)
			numPatterns = data[472 + i];
	}
	numPatterns++;

	size_t sampleDataOffset;
	if (!validate_payload(600, numPatterns, PAULA_VOICES, realLengths, 15, size, &sampleDataOffset))
	{
		set_error(errorText, errorTextSize, "Truncated or oversized 15-sample MOD data");
		return NULL;
	}

	module_t *m = createEmptyMod();
	if (m == NULL)
	{
		set_error(errorText, errorTextSize, "Out of memory");
		return NULL;
	}

	bool veryLate = false, late = false;
	memcpy(m->header.name, data, 20);
	for (int32_t i = 0; i < 15; i++)
	{
		const uint8_t *h = &data[20 + (i * 30)];
		moduleSample_t *s = &m->samples[i];
		memcpy(s->text, h, 22);
		s->length = realLengths[i] > config.maxSampleLength ? config.maxSampleLength : realLengths[i];
		if (s->length > 9999)
			late = true;
		s->volume = (int8_t)h[25];
		s->loopStart = read_be16(&h[26]);
		s->loopLength = (int32_t)read_be16(&h[28]) * 2;
	}

	m->header.songLength = songLength;
	if (initTempo == 0)
		initTempo = 120;
	if (!memcmp(m->header.name, "jjk55", 5))
		initTempo = 120;
	m->header.initialTempo = 125;
	if (initTempo != 120)
	{
		const uint16_t ciaPeriod = (uint16_t)((240 - initTempo) * 122);
		const double hz = CIA_PAL_CLK / (ciaPeriod + 1.0);
		m->header.initialTempo = (uint16_t)((hz * (125.0 / 50.0)) + 0.5);
	}
	for (int32_t i = 0; i < 128; i++)
		m->header.patternTable[i] = data[472 + i];

	const uint8_t *p = &data[600];
	for (int32_t pattern = 0; pattern < numPatterns; pattern++)
	{
		for (int32_t i = 0; i < MOD_ROWS * PAULA_VOICES; i++, p += 4)
		{
			note_t *note = &m->patterns[pattern][i];
			note->period = (uint16_t)(((p[0] & 0x0F) << 8) | p[1]);
			note->sample = (uint8_t)((p[0] & 0xF0) | (p[2] >> 4));
			note->command = p[2] & 0x0F;
			note->param = p[3];
			if (note->sample > 31)
				note->sample = 0;
			if (note->command == 0xC || note->command == 0xD || note->command == 0xE)
				late = true;
			if (note->command == 0xF)
				late = veryLate = true;
		}
	}

	for (int32_t pattern = 0; pattern < numPatterns; pattern++)
	{
		for (int32_t i = 0; i < MOD_ROWS * PAULA_VOICES; i++)
		{
			note_t *note = &m->patterns[pattern][i];
			if (!late)
			{
				if (note->command == 1)
					note->command = 0;
				else if (note->command == 2 && (note->param & 0xF0))
				{
					note->command = 2;
					note->param >>= 4;
				}
				else if (note->command == 2 && (note->param & 0x0F))
					note->command = 1;
			}
			else if (note->command == 0xD)
			{
				if (veryLate)
					note->param = 0;
				else
					note->command = 0xA;
			}

			if (note->command == 0xF && note->param == 0)
				note->command = 0;
			if (note->command == 0xE && ((note->param >> 4) == 0x8 || (note->param >> 4) == 0xF))
				note->command = note->param = 0;
		}
	}

	size_t cursorPos = sampleDataOffset;
	for (int32_t i = 0; i < 15; i++)
	{
		moduleSample_t *s = &m->samples[i];
		int32_t sourceOffset = 0;
		if (s->loopStart > 0 && s->loopLength < s->length && s->loopStart < s->length)
		{
			s->length -= s->loopStart;
			sourceOffset = s->loopStart;
			s->loopStart = 0;
		}

		const int32_t loopEnd = s->loopStart + s->loopLength;
		if (loopEnd > 2 && s->length > loopEnd)
			s->length = loopEnd;

		if (s->length > 0)
			memcpy(&m->sampleData[s->offset], &data[cursorPos + (size_t)sourceOffset], (size_t)s->length);
		cursorPos += (size_t)realLengths[i];
	}

	sanitize_module(m);
	return m;
}

static inline int32_t random32(void)
{
	randSeed *= 134775813U;
	randSeed++;
	return (int32_t)randSeed;
}

static int16_t quantize_sample(float sample, float *previousNoise)
{
	const float noise = (float)random32() * (1.0f / ((float)UINT32_MAX + 1.0f));
	const float value = (sample + noise) - *previousNoise;
	*previousNoise = noise;
	const int32_t integer = (int32_t)value;
	return (int16_t)CLAMP(integer, INT16_MIN, INT16_MAX);
}

static void output_audio(int16_t *target, int32_t frames)
{
	paulaGenerateSamples(mixBufferL, mixBufferR, frames * 2);
	for (int32_t i = 0; i < frames; i++)
	{
		const uint32_t a = (uint32_t)i * 2;
		float left = downsample2x_L(mixBufferL[a], mixBufferL[a + 1]);
		float right = downsample2x_R(mixBufferR[a], mixBufferR[a + 1]);

		if (stereoSeparation != 100)
		{
			const float mid = (left + right) * STEREO_NORM_FACTOR;
			const float side = (left - right) * sideFactor;
			left = mid + side;
			right = mid - side;
		}

		left *= NORMALIZE_VALUE;
		right *= NORMALIZE_VALUE;
		target[i * 2] = quantize_sample(left, &prngStateL);
		target[i * 2 + 1] = quantize_sample(right, &prngStateR);
	}
}

static void generate_bpm_table(void)
{
	for (int32_t bpm = MIN_BPM; bpm <= MAX_BPM; bpm++)
	{
		const double hz = ciaBpm2Hz(bpm);
		const double samplesPerTick = outputRate / hz;
		double integerPart;
		const double fractionalPart = modf(samplesPerTick, &integerPart);
		const int32_t i = bpm - MIN_BPM;
		audio.samplesPerTickIntTab[i] = (uint32_t)integerPart;
		audio.samplesPerTickFracTab[i] = (uint64_t)(fractionalPart * BPM_FRAC_SCALE);
	}
}

bool pt2play_init(uint32_t rate, uint8_t separation)
{
	if (rate < 32000)
		return false;

	memset(&config, 0, sizeof(config));
	memset(&editor, 0, sizeof(editor));
	memset(&ui, 0, sizeof(ui));
	memset(&cursor, 0, sizeof(cursor));
	memset(&audio, 0, sizeof(audio));
	memset(visuals, 0, sizeof(visuals));
	config.maxSampleLength = PT2_MAX_SAMPLE_LENGTH;
	config.enableE8xEffect = false;
	editor.initialSpeed = 6;
	editor.initialTempo = 125;
	editor.timingMode = TEMPO_MODE_CIA;
	editor.playMode = PLAY_MODE_NORMAL;
	editor.currMode = MODE_IDLE;
	outputRate = rate;

	generate_bpm_table();
	paulaSetup(rate * 2.0, MODEL_A1200);
	clearDownsample2xStates();
	resetAudioDither();
	pt2play_set_stereo_separation(separation);
	setLEDFilter(false);

	audio.samplesPerTickInt = audio.samplesPerTickIntTab[125 - MIN_BPM];
	audio.samplesPerTickFrac = audio.samplesPerTickFracTab[125 - MIN_BPM];
	return true;
}

void pt2play_shutdown(void)
{
	pt2play_unload();
	memset(visuals, 0, sizeof(visuals));
}

bool pt2play_load(const uint8_t *data, size_t size, char *errorText, size_t errorTextSize)
{
	if (errorText != NULL && errorTextSize > 0)
		errorText[0] = '\0';
	if (data == NULL || size == 0)
	{
		set_error(errorText, errorTextSize, "Empty module data");
		return false;
	}

	uint8_t channels;
	module_t *newSong;
	if (get_mod31_type(data, size, &channels) != FORMAT_UNKNOWN)
		newSong = load_mod31(data, size, errorText, errorTextSize);
	else
		newSong = load_mod15(data, size, errorText, errorTextSize);

	if (newSong == NULL)
		return false;

	pt2play_unload();
	song = newSong;
	song->loaded = true;
	editor.playMode = PLAY_MODE_NORMAL;
	editor.currMode = MODE_IDLE;
	editor.songPlaying = false;
	editor.timingMode = TEMPO_MODE_CIA;
	editor.currPatternDisp = &song->header.patternTable[0];
	editor.currPosEdPattDisp = &song->header.patternTable[0];
	song->currPos = 0;
	song->currPattern = song->header.patternTable[0];
	song->currRow = song->row = 0;
	modSetSpeed(6);
	modSetTempo(song->header.initialTempo, false);
	memset(visuals, 0, sizeof(visuals));
	clearDownsample2xStates();
	resetAudioDither();
	return true;
}

void pt2play_unload(void)
{
	if (song != NULL)
	{
		modStop();
		free_module(song);
		song = NULL;
	}
	editor.songPlaying = false;
	memset(visuals, 0, sizeof(visuals));
}

bool pt2play_is_loaded(void)
{
	return song != NULL && song->loaded;
}

const char *pt2play_get_title(void)
{
	return pt2play_is_loaded() ? song->header.name : "";
}

int pt2play_get_num_orders(void)
{
	return pt2play_is_loaded() ? song->header.songLength : 0;
}

int pt2play_get_order_pattern(int order)
{
	if (!pt2play_is_loaded() || order < 0 || order >= song->header.songLength)
		return -1;
	return song->header.patternTable[order];
}

const PT2PlayCell *pt2play_get_cell(int pattern, int row, int channel)
{
	static PT2PlayCell cell;
	if (!pt2play_is_loaded() || pattern < 0 || pattern >= MAX_PATTERNS ||
		row < 0 || row >= MOD_ROWS || channel < 0 || channel >= PAULA_VOICES)
	{
		return NULL;
	}

	const note_t *note = &song->patterns[pattern][row * PAULA_VOICES + channel];
	cell.period = note->period;
	cell.sample = note->sample;
	cell.command = note->command;
	cell.param = note->param;
	return &cell;
}

bool pt2play_get_sample(int sample1Based, PT2PlaySample *sample)
{
	if (sample != NULL)
		memset(sample, 0, sizeof(*sample));
	if (!pt2play_is_loaded() || sample == NULL || sample1Based < 1 || sample1Based > MOD_SAMPLES)
		return false;

	const moduleSample_t *s = &song->samples[sample1Based - 1];
	sample->name = s->text;
	sample->data = &song->sampleData[s->offset];
	sample->length = s->length;
	sample->loopStart = s->loopStart;
	sample->loopLength = s->loopLength;
	sample->volume = (uint8_t)s->volume;
	sample->fineTune = s->fineTune;
	return true;
}

void pt2play_start(int order, int row)
{
	if (!pt2play_is_loaded())
		return;
	order = CLAMP(order, 0, song->header.songLength - 1);
	row = CLAMP(row, 0, MOD_ROWS - 1);
	editor.playMode = PLAY_MODE_NORMAL;
	editor.currMode = MODE_PLAY;
	modPlay(DONT_SET_PATTERN, (int16_t)order, (int8_t)row);
}

void pt2play_stop(void)
{
	if (!pt2play_is_loaded())
		return;
	modStop();
	editor.currMode = MODE_IDLE;
	memset(visuals, 0, sizeof(visuals));
	clearDownsample2xStates();
	resetAudioDither();
}

bool pt2play_set_order(int order)
{
	if (!pt2play_is_loaded() || order < 0 || order >= song->header.songLength)
		return false;
	pt2play_start(order, 0);
	return true;
}

void pt2play_render(int16_t *stereoOutput, int32_t frames)
{
	if (stereoOutput == NULL || frames <= 0)
		return;
	if (!pt2play_is_loaded())
	{
		memset(stereoOutput, 0, (size_t)frames * 2 * sizeof(int16_t));
		return;
	}

	int32_t framesLeft = frames;
	while (framesLeft > 0)
	{
		if (audio.tickSampleCounter <= 0)
		{
			if (editor.songPlaying)
				tickReplayer();

			audio.tickSampleCounter = (int32_t)audio.samplesPerTickInt;
			audio.tickSampleCounterFrac += audio.samplesPerTickFrac;
			if (audio.tickSampleCounterFrac >= BPM_FRAC_SCALE)
			{
				audio.tickSampleCounterFrac &= BPM_FRAC_MASK;
				audio.tickSampleCounter++;
			}
		}

		int32_t count = framesLeft;
		if (count > PT2_RENDER_CHUNK)
			count = PT2_RENDER_CHUNK;
		if (audio.tickSampleCounter > 0 && count > audio.tickSampleCounter)
			count = audio.tickSampleCounter;
		if (count <= 0)
			count = 1;

		output_audio(stereoOutput, count);
		stereoOutput += count * 2;
		framesLeft -= count;
		audio.tickSampleCounter -= count;
	}
}

void pt2play_get_snapshot(PT2PlaySnapshot *snapshot)
{
	if (snapshot == NULL)
		return;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!pt2play_is_loaded())
		return;

	snapshot->order = song->currPos;
	snapshot->pattern = song->currPattern;
	snapshot->row = song->currRow;
	snapshot->tick = song->tick;
	snapshot->speed = song->currSpeed;
	snapshot->bpm = song->currBPM;

	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		const moduleChannel_t *ch = &song->channels[i];
		PT2PlayChannel *out = &snapshot->channels[i];
		const int8_t *location = NULL;
		float volume = 0.0f;
		uint16_t period = 0;
		out->active = paulaGetVoiceState(i, &location, &volume, &period);
		out->period = period;
		out->volume = volume / 64.0f;
		out->triggerSerial = visuals[i].triggerSerial;
		if (ch->n_start != NULL && ch->n_samplenum < MOD_SAMPLES)
		{
			out->sample = ch->n_samplenum + 1;
			const moduleSample_t *s = &song->samples[ch->n_samplenum];
			const uintptr_t base = (uintptr_t)&song->sampleData[s->offset];
			const uintptr_t end = base + (uintptr_t)s->length;
			const uintptr_t pos = (uintptr_t)location;
			if (location != NULL && pos >= base && pos <= end)
				out->samplePosition = (double)(pos - base);
		}
	}
}

void pt2play_set_stereo_separation(uint8_t percentage)
{
	if (percentage > 100)
		percentage = 100;
	stereoSeparation = percentage;
	sideFactor = (percentage / 100.0f) * STEREO_NORM_FACTOR;
}

void lockAudio(void)
{
	audio.locked = true;
}

void unlockAudio(void)
{
	audio.locked = false;
}

void resetAudioDither(void)
{
	randSeed = INITIAL_DITHER_SEED;
	prngStateL = prngStateR = 0.0f;
}

void setLEDFilter(bool state)
{
	audio.ledFilterEnabled = state;
	paulaWriteByte(0xBFE001, (uint8_t)state << 1);
}

void setSyncTickTimeLen(uint32_t timeInt, uint64_t timeFrac)
{
	(void)timeInt;
	(void)timeFrac;
}

void setVisualsDataPtr(uint32_t channel, const int8_t *data)
{
	if (channel < PAULA_VOICES)
		visuals[channel].data = data;
}

void setVisualsDMACON(uint16_t value)
{
	const bool enable = (value & 0x8000) != 0;
	for (uint32_t i = 0; i < PAULA_VOICES; i++)
	{
		if (value & (1U << i))
		{
			visuals[i].active = enable;
			if (enable)
				visuals[i].triggerSerial++;
		}
	}
}

void setVisualsLength(uint32_t channel, uint16_t length)
{
	if (channel < PAULA_VOICES)
		visuals[channel].length = length;
}

void setVisualsPeriod(uint32_t channel, uint16_t period)
{
	if (channel < PAULA_VOICES)
		visuals[channel].period = period;
}

void setVisualsVolume(uint32_t channel, uint16_t volume)
{
	if (channel < PAULA_VOICES)
		visuals[channel].volume = volume;
}

void pointerSetMode(int32_t mode, int32_t carry) { (void)mode; (void)carry; }
void pointerSetModeThreadSafe(int32_t mode, bool carry) { (void)mode; (void)carry; }
void updateCursorPos(void) { }
void posEdClearNames(void) { }
void renderMuteButtons(void) { }
void updateCurrSample(void) { }
