

#pragma once

#include <stdint.h>

 














#define BLEP_ZC 16
#define BLEP_OS 16
#define BLEP_SP 16
#define BLEP_NS (BLEP_ZC * BLEP_OS / BLEP_SP)
#define BLEP_RNS 31 

typedef struct blep_t
{
	int32_t index, samplesLeft;
	float fBuffer[BLEP_RNS+1], fLastValue;
} blep_t;

void blepAdd(blep_t *b, const float fOffset, const float fAmplitude);
float blepRun(blep_t *b, const float fInput);

