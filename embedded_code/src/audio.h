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
 */
bool setupAudio();

/**
 * Set the volume of the audio codec.
 * The volume must be in the range -48 to 79 where:
 *   <0 is muted (should not be less than -48)
 *   0 to 79 is -73dB to +6dB in 1dB steps
 */
void setVolume(int8_t volume);

/**
 * Generate a dual-channel sine wave.
 * The frequency should be in the range of human hearing (20 Hz to 20 kHz).
 * Two common frequencies are 440 Hz (A4) and 523.25 Hz (C5).
 * The amplitude should be in the range of 0 to 32767.
 * The offset is the starting point of the wave.
 * This returns the new offset after generating the wave.
 * The buffer length is in elements (not bytes).
 */
uint32_t generateSineWave(float frequency, int16_t amplitude, uint32_t offset, uint16_t* buffer, uint32_t length);

/** Mix two audio samples together. */
void mixAudio(uint16_t* sample1, uint16_t* sample2, uint32_t length, float ratio, uint16_t* output);

/** Add two audio samples together. Does not check for overflow. */
void addAudio(uint16_t* sample1, uint16_t* sample2, uint32_t length, uint16_t* output);

/** Send audio data to the I2S bus for playback. The length is in bytes. */
void sendAudioToI2S(uint8_t* data, uint32_t length);
