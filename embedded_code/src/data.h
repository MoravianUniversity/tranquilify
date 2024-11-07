#pragma once
#include <stdint.h>

bool setupSD();
bool recordTimestamp();
bool recordWAVData(uint8_t* data, uint32_t length);
