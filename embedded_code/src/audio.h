#pragma once

#include <stdint.h>

// Audio Format Parameters
// These cannot just be changed here without also changing the audio codec and I2S setup
#define SAMPLE_RATE 44100
#define BITS_PER_SAMPLE 16
#define CHANNELS 2
#define BYTES_PER_SAMPLE (BITS_PER_SAMPLE / 8)
#define ONE_HOUR_OF_DATA (SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE * 3600)

/**
 * Set up the audio codec and I2S for recording audio.
 * Before this is called, the Serial and Wire interfaces must be set up.
 * If there is a problem with the audio setup, the program will freeze.
 */
void setupAudio();

/**
 * Set the volume of the audio codec.
 * The volume must be in the range -48 to 79 where:
 *   <0 is muted (should not be less than -48)
 *   0 to 79 is -73dB to +6dB in 1dB steps
 */
void setVolume(int8_t volume);
