#pragma once

#include <stdint.h>
#include <stdbool.h>

#define AMIGA_PAL_XTAL_HZ 28375160
#define AMIGA_PAL_CCK_HZ (AMIGA_PAL_XTAL_HZ/8.0)

enum
{
	MODEL_A1200 = 0,
	MODEL_A500  = 1
};

#define PAULA_VOICES 4
#define PAULA_PAL_CLK AMIGA_PAL_CCK_HZ
#define PAL_PAULA_MIN_PERIOD 113
#define PAL_PAULA_MIN_SAFE_PERIOD 124
#define PAL_PAULA_MAX_HZ (PAULA_PAL_CLK / (double)PAL_PAULA_MIN_PERIOD)
#define PAL_PAULA_MAX_SAFE_HZ (PAULA_PAL_CLK / (double)PAL_PAULA_MIN_SAFE_PERIOD)

void paulaSetup(double dOutputFreq, uint32_t amigaModel);
void paulaDisableFilters(void); 

int8_t *paulaGetNullSamplePtr(void);

void paulaWriteByte(uint32_t address, uint8_t data8);
void paulaWriteWord(uint32_t address, uint16_t data16);
void paulaWritePtr(uint32_t address, const int8_t *ptr);

void clearBlepState(void);

bool paulaGetVoiceState(uint32_t channel, const int8_t **location, float *volume, uint16_t *period);


void paulaGenerateSamples(float *fOutL, float *fOutR, int32_t numSamples);
