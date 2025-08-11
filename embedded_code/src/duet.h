#pragma once

#include <stdint.h>
#include <complex.h>
typedef float _Complex cfloat; // complex float type for the DUET algorithm


///////////////////////////////////////////////
////////// DUET Algorithm Parameters //////////
///////////////////////////////////////////////

// TODO: maybe try 16 kHz sample rate (third of 48000 Hz) and a window size of 512 (radix-4 compatible)
// That gives:
//  - 256 frequency bins
//  - 7 time slices for 96 ms of audio (1536 samples) [may want to increase the total time to increase the number of time slices]
//  - 1792 time-frequency bins
//  - 12.5 KB of global memory  (6 KB for FFT tables, 6.5 KB for DUET globals)
//  - 124+ KB of stack memory
//       12 KB for 96 ms of audio (2 channels, 16 kHz, float32)
//          not actually needed to be saved, could minimize it 3 time slices (down to <4 KB)
//       TODO: haven't updated the rest of these
//       0.5 KB for FFT computation
//       32 KB for spectrogram
//       24 KB for alpha, delta, weights
//       26+ KB for mean shift prepare          (move to heap and reduce in the vast majority of cases)
//       10 KB for mean shift core              (plan to remove 8 KB of this)
//       20 KB for demixed (~10 samples)
//       at least 1KB for other things
//  Overall that is 27% of the SRAM!
//  Audio buffers take 18.4 KB (2 channels, 48 kHz, uint16, 96 ms of audio)
//  Still need to fit the AI model

// ESP32: 512 KB of SRAM (direct) at 960MB / second (~1000 bytes/us)
// ESP32-S3:
//   - 512 KB of SRAM (direct) at 960MB / second (~1000 bytes/us)
//   - 16 MB of PSRAM (indirect) at 40MB / second (~42 bytes/us) (half that for writes)
// ESP32-P4:
//   - 768 KB of SRAM (direct)
//   - 32 MB of PSRAM (indirect)

// Frequency of the audio signal being processed
// This is the sample rate of the audio signal being processed, not the original audio signal
// Increasing this will:
//   - increase the time resolution
//   - decrease the frequency resolution (same number of frequency bins, more frequencies)
//   - increase the memory usage and processing time
// It will be best if this is an even divisor of the recording sample rate.
#ifndef DUET_SAMPLE_RATE
#define DUET_SAMPLE_RATE 16000 // 16 kHz
#endif

// Size of the STFT Window
// Must be a power of 2, max of 8192
// Preferred to be twice a power of 4 (8, 32, 128, 512, 2048, 8192) which will be relatively faster
// Increasing this will increase the frequency resolution but:
//   - decrease the time resolution
//   - increase the latency
//   - increase the memory usage and processing time
#ifndef DUET_WINDOW_SIZE
#define DUET_WINDOW_SIZE 256
#endif
#define DUET_WINDOW_SIZE_HALF (DUET_WINDOW_SIZE / 2) // Half the window size
#define DUET_N_FREQ (DUET_WINDOW_SIZE_HALF) // Number of frequency bins in the STFT (half of the window size)

// Number of audio samples we will save to do identification
// Must be a multiple of WINDOW_SIZE/2
// 100 ms * SAMPLE_RATE (16000) => 1600 samples => 1536 samples (96 ms) when rounded to the nearest multiple of WINDOW_SIZE/2
#ifndef DUET_N_SAMPLES
#define DUET_N_SAMPLES (DUET_SAMPLE_RATE / 10 / DUET_WINDOW_SIZE_HALF * DUET_WINDOW_SIZE_HALF)
#endif
#define DUET_N_TIME (DUET_N_SAMPLES / DUET_WINDOW_SIZE_HALF + 1) // Number of time slices in the STFT

// The symmetric attenuation estimator value weights
// See the paper for more details. The value of 1 reduces the math needed to compute the weights.
#ifndef DUET_P
#define DUET_P 1.0f
#endif

// DUET algorithm: the delay estimator value weights
// See the paper for more details. The value of 0 reduces the math needed to compute the weights.
//#define DUET_Q 0.0f  // define this to use a non-zero Q value

// Threshold to filter the points in the spectrogram. The higher this value,
// the faster it will run, but it may also start moving the cluster centers
// around. A recommendation is 0.05.
#ifndef DUET_POINT_THRESHOLD
#define DUET_POINT_THRESHOLD 0.5f
#endif

// Min and max bounds for processing attenuation (alpha) values
#ifndef ATTENUATION_MAX
#define ATTENUATION_MAX 3.6f
#endif

// Min and max bounds for processing delay (delta) values
#ifndef DELAY_MAX
#define DELAY_MAX 3.6f  // TODO: or 0.7f?
#endif


/**
 * Initializes the Duet audio processing library.
 * This function must be called before using any other functions in this library.
 * It initializes the FFT library and sets up the necessary parameters for the
 * DUET algorithm along with allocating memory for the audio buffer and other
 * arrays.
 * 
 * Returns ESP_OK on success, or an error code on failure.
 */
esp_err_t duet_init();

/**
 * Deinitializes the Duet audio processing library.
 * This function frees all allocated memory and resets the state of the library.
 */
void duet_deinit();


// TODO: remove this and only support the overall function which calls these in the right order

void prep_data(const int16_t * const in, const int n, float* out);
void decimate(const float* const input, int n, float* output, int offset);
void compute_spectrogram(
    float* x,           // in, shape of (N_CHANNELS, N_SAMPLES)
    int new_times,
    cfloat* out         // out, shape of (N_CHANNELS, N_FREQ, N_TIME)
);
void compute_atten_and_delay(
    const cfloat * const spectrogram, // in, shape (N_CHANNELS, N_FREQ, N_TIME)
    const int new_times,
    float* alpha,                     // out, shape (N_CHANNELS-1, N_FREQ, N_TIME)
    float* delta                      // out, shape (N_CHANNELS-1, N_FREQ, N_TIME)
);
void compute_weights(
    const cfloat * const spectrogram, // in, shape (N_CHANNELS, N_FREQ, N_TIME)
    const int new_times,
    float* tf_weights                 // out, shape (N_CHANNELS-1, N_FREQ, N_TIME)
);
#include <vector>
void find_peaks(
    const float * const tf_weights, // in, shape (N_CHANNELS-1, N_FREQ, N_TIME)
    const float * const alpha,      // in, shape (N_CHANNELS-1, N_FREQ, N_TIME)
    const float * const delta,      // in, shape (N_CHANNELS-1, N_FREQ, N_TIME)
    std::vector<float>& alpha_peaks, // out, shape (n_sources, N_CHANNELS-1)
    std::vector<float>& delta_peaks  // out, shape (n_sources, N_CHANNELS-1)
);
void convert_sym_to_atn(std::vector<float>& atn);
void full_demix(
    const cfloat * const spectrogram, // in, shape (2, N_FREQ, N_TIME)
    const std::vector<float> &alpha,  // in, shape (n_sources, N_CHANNELS-1)
    const std::vector<float> &delta,  // in, shape (n_sources, N_CHANNELS-1)
    std::vector<cfloat> &demixed,     // out, shape (n_sources, N_FREQ, N_TIME)
    uint8_t* best                     // out, shape (N_FREQ, N_TIME)
);
