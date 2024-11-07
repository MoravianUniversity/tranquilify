#pragma once

// Audio Format Parameters
// These cannot just be changed here without also changing the audio codec and I2S setup
#define SAMPLE_RATE 44100
#define BITS_PER_SAMPLE 16
#define CHANNELS 2
#define BYTES_PER_SAMPLE (BITS_PER_SAMPLE / 8)
#define ONE_HOUR_OF_DATA (SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE * 3600)

void setupAudio();
