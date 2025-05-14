#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sd.h"


typedef struct _WriteWAVParams {
    uint8_t* buffer;
    uint32_t length;
    bool writing; // whether a buffer is currently being written to the SD card
} WriteWAVParams;

/**
 * Write the given audio data to the WAV file on the SD card.
 * The writing flag is set to false when the writing is done.
 */
bool writeWAVData(SdFs* sd, WriteWAVParams *params);


// Each ButtonEvent represents a button press and release
typedef struct _ButtonEvent {
    unsigned long pressTime;
    unsigned long releaseTime;
} ButtonEvent;

/**
 * Write the button press and release times to the timestamp file.
 */
bool writeButtonData(SdFs* sd, ButtonEvent* event);
