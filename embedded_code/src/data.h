#pragma once
#include <stdint.h>

bool setupData();

bool recordTimestampFromISR();

bool recordWAVData(uint8_t* data, uint32_t length);
