#ifndef PT2PLAY_H
#define PT2PLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PT2PLAY_CHANNELS 4
#define PT2PLAY_SAMPLES 31
#define PT2PLAY_ROWS 64

extern const char pt2play_attribution[];

typedef struct PT2PlayCell
{
	uint16_t period;
	uint8_t sample, command, param;
} PT2PlayCell;

typedef struct PT2PlaySample
{
	const char *name;
	const int8_t *data;
	int32_t length, loopStart, loopLength;
	uint8_t volume, fineTune;
} PT2PlaySample;

typedef struct PT2PlayChannel
{
	bool active;
	uint8_t sample;
	uint16_t period;
	float volume;
	double samplePosition;
	uint32_t triggerSerial;
} PT2PlayChannel;

typedef struct PT2PlaySnapshot
{
	int order, pattern, row, tick, speed, bpm;
	PT2PlayChannel channels[PT2PLAY_CHANNELS];
} PT2PlaySnapshot;

bool pt2play_init(uint32_t outputRate, uint8_t stereoSeparation);
void pt2play_shutdown(void);
bool pt2play_load(const uint8_t *data, size_t size, char *errorText, size_t errorTextSize);
void pt2play_unload(void);
bool pt2play_is_loaded(void);
const char *pt2play_get_title(void);
int pt2play_get_num_orders(void);
int pt2play_get_order_pattern(int order);
const PT2PlayCell *pt2play_get_cell(int pattern, int row, int channel);
bool pt2play_get_sample(int sample1Based, PT2PlaySample *sample);
void pt2play_start(int order, int row);
void pt2play_stop(void);
bool pt2play_set_order(int order);
void pt2play_render(int16_t *stereoOutput, int32_t frames);
void pt2play_get_snapshot(PT2PlaySnapshot *snapshot);
void pt2play_set_stereo_separation(uint8_t percentage);

#endif
