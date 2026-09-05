 






#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "pt2_internal.h" 
#include "pt2_paula.h"
#include "pt2_blep.h"
#include "pt2_rcfilters.h"

typedef struct voice_t
{
	volatile bool active;

	
	bool sampleJustStarted, nextSampleStage;
	int8_t AUD_DAT[2]; 
	const int8_t *location; 
	uint16_t lengthCounter; 
	int32_t sampleCounter; 
	float fSample; 
	float fDelta, fPhase;
	float fBlepDelta, fBlepPhase;

	
	const int8_t *storedLocation; 
	uint16_t storedLength;
	float fStoredVol, fStoredDelta;
} paulaVoice_t;

static bool useLEDFilter, useLowpassFilter, useHighpassFilter;
static int8_t nullSample[0xFFFF*2]; 
static float fPeriodToDeltaDiv;
static double dPaulaOutputFreq;
static blep_t blep[PAULA_VOICES];
static onePoleFilter_t filterLo, filterHi;
static twoPoleFilter_t filterLED;
static paulaVoice_t paula[PAULA_VOICES];

void paulaSetup(double dOutputFreq, uint32_t amigaModel)
{
	ASSERT(dOutputFreq != 0.0);
	dPaulaOutputFreq = dOutputFreq;
	fPeriodToDeltaDiv = (float)(PAULA_PAL_CLK / dPaulaOutputFreq);

	clearBlepState();

	useLowpassFilter = useHighpassFilter = true;
	clearOnePoleFilterState(&filterLo);
	clearOnePoleFilterState(&filterHi);
	clearTwoPoleFilterState(&filterLED);

	 












	double R, C, R1, R2, C1, C2, cutoff, qfactor;

	if (amigaModel == MODEL_A1200)
	{
		

		 




		useLowpassFilter = false;

		
		R = 1360.0; 
		C = 2.2e-5; 
		cutoff = 1.0 / ((2.0 * PI) * R * C); 
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterHi);
	}
	else
	{
		

		
		R = 360.0; 
		C = 1e-7;  
		cutoff = 1.0 / ((2.0 * PI) * R * C); 
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterLo);

		
		R = 1390.0;   
		C = 2.233e-5; 
		cutoff = 1.0 / ((2.0 * PI) * R * C); 
		setupOnePoleFilter(dPaulaOutputFreq, cutoff, &filterHi);
	}

	
	R1 = 10000.0; 
	R2 = 10000.0; 
	C1 = 6.8e-9;  
	C2 = 3.9e-9;  
	cutoff = 1.0 / ((2.0 * PI) * sqrt(R1 * R2 * C1 * C2)); 
	qfactor = sqrt(R1 * R2 * C1 * C2) / (C2 * (R1 + R2)); 
	setupTwoPoleFilter(dPaulaOutputFreq, cutoff, qfactor, &filterLED);
}

void paulaDisableFilters(void) 
{
	useHighpassFilter = false;
	useLowpassFilter = false;
}

int8_t *paulaGetNullSamplePtr(void)
{
	return nullSample;
}

static inline void refetchPeriod(paulaVoice_t *v) 
{
	v->fBlepPhase = v->fPhase;
	v->fBlepDelta = v->fDelta;

	
	v->fDelta = v->fStoredDelta;

	v->nextSampleStage = true;
}

static inline void nextSample(paulaVoice_t *v, blep_t *b) 
{
	if (v->sampleCounter == 0)
	{
		

		
		if (!v->sampleJustStarted)
		{
			if (--v->lengthCounter == 0)
			{
				v->lengthCounter = v->storedLength;
				v->location = v->storedLocation;
			}
		}

		v->sampleJustStarted = false;

		
		v->AUD_DAT[0] = *v->location++;
		v->AUD_DAT[1] = *v->location++;
		v->sampleCounter = 2;
	}

	 




	v->fSample = v->AUD_DAT[0] * v->fStoredVol; 

	
	if (v->fSample != b->fLastValue)
	{
		if (v->fBlepDelta > v->fBlepPhase) 
		{
			const float fBlepOffset = v->fBlepPhase / v->fBlepDelta;
			blepAdd(b, fBlepOffset, b->fLastValue - v->fSample);
		}

		b->fLastValue = v->fSample;
	}

	
	v->AUD_DAT[0] = v->AUD_DAT[1];
	v->sampleCounter--;
}

static void audxper(int32_t ch, uint16_t period)
{
	paulaVoice_t *v = &paula[ch];

	int32_t realPeriod = period;
	if (realPeriod == 0)
		realPeriod = 65536; 
	else if (realPeriod < 113)
		realPeriod = 113; 

	
	v->fStoredDelta = fPeriodToDeltaDiv / (float)realPeriod;

	
	if (v->fBlepDelta == 0.0f)
		v->fBlepDelta = v->fDelta;
}

static void audxvol(int32_t ch, uint16_t vol)
{
	int32_t realVol = vol & 127;
	if (realVol > 64)
		realVol = 64;

	
	paula[ch].fStoredVol = (float)realVol * (1.0f / (128.0f * 64.0f));
}

static void audxlen(int32_t ch, uint16_t len)
{
	paula[ch].storedLength = len;
}

static void audxdat(int32_t ch, const int8_t *src)
{
	if (src == NULL)
		src = nullSample;

	paula[ch].storedLocation = src;
}

static void startDMA(int32_t ch)
{
	paulaVoice_t *v = &paula[ch];

	if (v->storedLocation == NULL)
		v->storedLocation = nullSample;

	
	v->location = v->storedLocation;
	v->lengthCounter = v->storedLength;

	
	v->sampleCounter = 0;
	v->sampleJustStarted = true;
	refetchPeriod(v);

	
	v->fPhase = 0.0f;

	v->active = true;
}

static void stopDMA(int32_t ch)
{
	paula[ch].active = false;
}

void paulaWriteByte(uint32_t address, uint8_t data8)
{
	if (address == 0)
		return;

	switch (address)
	{
		
		case 0xBFE001:
		{
			const bool oldLedFilterState = useLEDFilter;

			useLEDFilter = !!(data8 & 2);
			if (useLEDFilter != oldLedFilterState)
				clearTwoPoleFilterState(&filterLED);
		}
		break;

		default:
			return;
	}
}

void paulaWriteWord(uint32_t address, uint16_t data16)
{
	if (address == 0)
		return;

	switch (address)
	{
		
		case 0xDFF096:
		{
			if (data16 & 0x8000)
			{
				
				if (data16 & 1) startDMA(0);
				if (data16 & 2) startDMA(1);
				if (data16 & 4) startDMA(2);
				if (data16 & 8) startDMA(3);
			}
			else
			{
				
				if (data16 & 1) stopDMA(0);
				if (data16 & 2) stopDMA(1);
				if (data16 & 4) stopDMA(2);
				if (data16 & 8) stopDMA(3);
			}
		}
		break;

		
		case 0xDFF0A4: audxlen(0, data16); break;
		case 0xDFF0B4: audxlen(1, data16); break;
		case 0xDFF0C4: audxlen(2, data16); break;
		case 0xDFF0D4: audxlen(3, data16); break;

		
		case 0xDFF0A6: audxper(0, data16); break;
		case 0xDFF0B6: audxper(1, data16); break;
		case 0xDFF0C6: audxper(2, data16); break;
		case 0xDFF0D6: audxper(3, data16); break;

		
		case 0xDFF0A8: audxvol(0, data16); break;
		case 0xDFF0B8: audxvol(1, data16); break;
		case 0xDFF0C8: audxvol(2, data16); break;
		case 0xDFF0D8: audxvol(3, data16); break;

		default:
			return;
	}
}

void paulaWritePtr(uint32_t address, const int8_t *ptr)
{
	if (address == 0)
		return;

	switch (address)
	{
		
		case 0xDFF0A0: audxdat(0, ptr); break;
		case 0xDFF0B0: audxdat(1, ptr); break;
		case 0xDFF0C0: audxdat(2, ptr); break;
		case 0xDFF0D0: audxdat(3, ptr); break;

		default:
			return;
	}
}

void clearBlepState(void)
{
	memset(blep, 0, sizeof (blep));
}

bool paulaGetVoiceState(uint32_t channel, const int8_t **location, float *volume, uint16_t *period)
{
	if (channel >= PAULA_VOICES)
		return false;

	const paulaVoice_t *v = &paula[channel];
	if (location != NULL)
		*location = v->location;
	if (volume != NULL)
		*volume = v->fStoredVol * (128.0f * 64.0f);
	if (period != NULL)
	{
		if (v->fStoredDelta > 0.0f)
			*period = (uint16_t)(fPeriodToDeltaDiv / v->fStoredDelta + 0.5f);
		else
			*period = 0;
	}

	return v->active;
}


void paulaGenerateSamples(float *fOutL, float *fOutR, int32_t numSamples)
{
	float *fMixBufSelect[PAULA_VOICES];

	if (numSamples <= 0)
		return;

	fMixBufSelect[0] = fOutL;
	fMixBufSelect[1] = fOutR;
	fMixBufSelect[2] = fOutR;
	fMixBufSelect[3] = fOutL;

	
	memset(fOutL, 0, numSamples * sizeof (float));
	memset(fOutR, 0, numSamples * sizeof (float));

	

	paulaVoice_t *v = paula;
	blep_t *b = blep;

	for (int32_t i = 0; i < PAULA_VOICES; i++, v++, b++)
	{
		if (!v->active || v->location == NULL || v->storedLocation == NULL)
			continue;

		float *fMixBuffer = fMixBufSelect[i]; 
		for (int32_t j = 0; j < numSamples; j++)
		{
			if (v->nextSampleStage)
			{
				v->nextSampleStage = false;
				nextSample(v, b);
			}

			float fSample = v->fSample; 
			if (b->samplesLeft > 0)
				fSample = blepRun(b, fSample);

			fMixBuffer[j] += fSample;

			v->fPhase += v->fDelta;
			if (v->fPhase >= 1.0f)
			{
				v->fPhase -= 1.0f;
				refetchPeriod(v);
			}
		}
	}

	
	for (int32_t i = 0; i < numSamples; i++)
	{
		float fOut[2];

		fOut[0] = fOutL[i];
		fOut[1] = fOutR[i];

		if (useLowpassFilter)
			onePoleLPFilterStereo(&filterLo, fOut, fOut);

		if (useLEDFilter)
			twoPoleLPFilterStereo(&filterLED, fOut, fOut);

		if (useHighpassFilter)
			onePoleHPFilterStereo(&filterHi, fOut, fOut);

		fOutL[i] = fOut[0];
		fOutR[i] = fOut[1];
	}
}
