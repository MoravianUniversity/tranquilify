#pragma once

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
