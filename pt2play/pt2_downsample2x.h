#pragma once

#include <stdint.h>


void clearDownsample2xStates(void);
float downsample2x_L(float sample1, float sample2);
float downsample2x_R(float sample1, float sample2);



void downsample2xFloat(float *buffer, uint32_t originalLength);
void downsample2xDouble(double *buffer, uint32_t originalLength);


bool downsample2x8Bit(int8_t *buffer, uint32_t originalLength);
bool downsample2x8BitU(uint8_t *buffer, uint32_t originalLength);
bool downsample2x16Bit(int16_t *buffer, uint32_t originalLength);
bool downsample2x32Bit(int32_t *buffer, uint32_t originalLength);

