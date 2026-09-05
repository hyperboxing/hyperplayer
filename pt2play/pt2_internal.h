#ifndef PT2_INTERNAL_H
#define PT2_INTERNAL_H

 





#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "pt2_paula.h"

#define PI 3.14159265358979323846264338327950288
#define CIA_PAL_CLK (AMIGA_PAL_CCK_HZ / 5.0)
#define AMIGA_PAL_VBLANK_HZ (AMIGA_PAL_CCK_HZ / (double)(313*227))

#define MOD_ROWS 64
#define MOD_SAMPLES 31
#define MAX_PATTERNS 100
#define MIN_BPM 32
#define MAX_BPM 255

#define BPM_FRAC_BITS 52
#define BPM_FRAC_SCALE (1ULL << BPM_FRAC_BITS)
#define BPM_FRAC_MASK (BPM_FRAC_SCALE-1)

#ifndef ASSERT
#define ASSERT(x) assert(x)
#endif

#ifndef CLAMP
#define CLAMP(x, low, high) ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x)))
#endif

enum
{
	MODE_IDLE = 0,
	MODE_EDIT = 1,
	MODE_PLAY = 2,
	MODE_RECORD = 3,
	PLAY_MODE_NORMAL = 0,
	PLAY_MODE_PATTERN = 1,
	RECORD_PATT = 0,
	TEMPO_MODE_CIA = 0,
	TEMPO_MODE_VBLANK = 1,
	DONT_SET_ORDER = -1,
	DONT_SET_PATTERN = -1,
	DONT_SET_ROW = -1,
	UPDATE_VUMETER = 1,
	UPDATE_SPECTRUM_ANALYZER = 2,
	POINTER_MODE_IDLE = 0,
	POINTER_MODE_EDIT = 1,
	POINTER_MODE_PLAY = 2,
	DO_CARRY = 1
};

typedef struct note_t
{
	uint8_t param, sample, command;
	uint16_t period;
} note_t;

typedef struct moduleHeader_t
{
	char name[21];
	uint16_t patternTable[128], songLength;
	uint16_t initialTempo;
} moduleHeader_t;

typedef struct moduleSample_t
{
	volatile int8_t *volumeDisp;
	volatile int32_t *lengthDisp, *loopStartDisp, *loopLengthDisp;
	char text[23];
	int8_t volume;
	uint8_t fineTune;
	int32_t offset, length, loopStart, loopLength;
} moduleSample_t;

typedef struct moduleChannel_t
{
	int8_t *n_start, *n_wavestart, *n_loopstart, n_volume, n_dmabit;
	int8_t n_toneportdirec, n_pattpos, n_loopcount;
	uint8_t n_wavecontrol, n_glissfunk, n_sampleoffset, n_toneportspeed;
	uint8_t n_vibratocmd, n_tremolocmd, n_finetune, n_funkoffset, n_samplenum;
	uint8_t n_vibratopos, n_tremolopos;
	int16_t n_period, n_note, n_wantedperiod;
	uint16_t n_cmd, n_length, n_replen;
	uint32_t n_scopedelta, n_chanindex;
	uint8_t syncFlags;
	int8_t syncAnalyzerVolume, syncVuVolume;
	uint16_t syncAnalyzerPeriod;
} moduleChannel_t;

typedef struct module_t
{
	bool loaded, modified;
	int8_t *sampleData;
	volatile int32_t tick, speed;
	int8_t row;
	moduleHeader_t header;
	moduleSample_t samples[MOD_SAMPLES];
	moduleChannel_t channels[PAULA_VOICES];
	note_t *patterns[MAX_PATTERNS];
	int8_t currRow;
	int32_t currSpeed, currBPM;
	uint16_t currPos, currPattern;
	uint32_t rowsCounter, rowsInTotal;
} module_t;

typedef struct pt2_config_compat_t
{
	bool compoMode, enableE8xEffect, keepEditModeAfterStepPlay;
	int32_t maxSampleLength;
} pt2_config_compat_t;

typedef struct pt2_editor_compat_t
{
	bool blockMarkFlag, didQuantize, metroFlag, mod2WavOngoing, pat2SmpOngoing;
	bool sampleZero, songPlaying, stepPlayBackwards, stepPlayEnabled, swapChannelFlag, tuningToneFlag;
	bool muted[PAULA_VOICES], rowVisitTable[128 * MOD_ROWS];
	uint8_t playMode, currMode, recordMode, stepPlayLastMode, timingMode;
	uint8_t f6Pos, f7Pos, f8Pos, f9Pos, f10Pos, hiLowInstr, initialSpeed, initialTempo;
	uint8_t multiModeNext[4], spectrumVolumes[23], vuMeterVolumes[PAULA_VOICES];
	int8_t currSample, editMoveAdd, realVuMeterVolumes[PAULA_VOICES];
	uint16_t metroChannel, metroSpeed;
	int32_t samplePos;
	uint32_t playbackSeconds, playbackSecondsFrac;
	uint16_t *currEditPatternDisp, *currPatternDisp, *currPosDisp, *currPosEdPattDisp;
} pt2_editor_compat_t;

typedef struct pt2_ui_compat_t
{
	bool posEdScreenShown, updateCurrPattText, updatePatternData, updatePosEd;
	bool updateSongBPM, updateSongLength, updateSongName, updateSongPattern;
	bool updateSongPos, updateSongSize, updateStatusText;
} pt2_ui_compat_t;

typedef struct pt2_cursor_compat_t
{
	uint8_t channel, pos;
} pt2_cursor_compat_t;

typedef struct audio_t
{
	bool locked, ledFilterEnabled;
	int32_t tickSampleCounter;
	uint32_t samplesPerTickInt, samplesPerTickIntTab[(MAX_BPM-MIN_BPM)+1];
	uint64_t tickSampleCounterFrac, samplesPerTickFrac, samplesPerTickFracTab[(MAX_BPM-MIN_BPM)+1];
	uint32_t tickTimeIntTab[(MAX_BPM-MIN_BPM)+1];
	uint64_t tickTimeFracTab[(MAX_BPM-MIN_BPM)+1];
} audio_t;

extern module_t *song;
extern pt2_config_compat_t config;
extern pt2_editor_compat_t editor;
extern pt2_ui_compat_t ui;
extern pt2_cursor_compat_t cursor;
extern audio_t audio;

extern const uint8_t vibratoTable[32];
extern const int16_t periodTable[(37*16)+15];
extern const int8_t vuMeterHeights[65];
extern const uint32_t tickDuration31fp[(MAX_BPM-MIN_BPM)+2];

double ciaBpm2Hz(int32_t bpm);
void initializeModuleChannels(module_t *m);
module_t *createEmptyMod(void);
bool tickReplayer(void);
void modSetSpeed(int32_t speed);
void modSetTempo(int32_t bpm, bool doLockAudio);
void modSetPos(int16_t pos, int16_t row);
void modPlay(int16_t patt, int16_t pos, int8_t row);
void modStop(void);
void turnOffVoices(void);
void modFree(void);

void lockAudio(void);
void unlockAudio(void);
void resetAudioDither(void);
void setLEDFilter(bool state);
void setSyncTickTimeLen(uint32_t timeInt, uint64_t timeFrac);
void setVisualsDataPtr(uint32_t channel, const int8_t *data);
void setVisualsDMACON(uint16_t value);
void setVisualsLength(uint32_t channel, uint16_t length);
void setVisualsPeriod(uint32_t channel, uint16_t period);
void setVisualsVolume(uint32_t channel, uint16_t volume);
void pointerSetMode(int32_t mode, int32_t carry);
void pointerSetModeThreadSafe(int32_t mode, bool carry);
void updateCursorPos(void);
void posEdClearNames(void);
void renderMuteButtons(void);
void updateCurrSample(void);

#endif
