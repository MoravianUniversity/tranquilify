#include "audio.h"
#include "sd.h"
#include "data.h"
#include "audio_codec.hpp"

#include <math.h> // for sin

// Background task
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// I2S driver
#include <driver/i2s.h>

// I2S pins/port
#define I2S_WS        33 // DACLRC/ADCLRC/LRC/"word select"/"left-right-channel", toggles for left or right channel data
#define I2S_ADC_DATA  14 // ADC_DATA/SD/"serial data in", carries the I2S audio data from codec's ADC to ESP32 I2S bus
#define I2S_DAC_DATA  32 // DAC_DATA/SDO/"serial data out", carries the I2S audio data from ESP32 to codec DAC
#define I2S_BCLK      27 // BCLK/SCK/"bit clock", this is the clock for I2S audio, can be controlled via controller or peripheral
#define I2S_PORT I2S_NUM_0 // Define which I2S peripheral to use

// Audio Recording Buffers (double buffering)
#define DMA_BUFFER_SAMPLE_LEN 1024 // from ~64 to 1024 - lower reduces latency but increases overhead (1024 is about 23.2ms of audio)
#define DMA_BUFFER_BYTE_LEN (DMA_BUFFER_SAMPLE_LEN * REC_BYTES_PER_SAMPLE * REC_CHANNELS) // IMPORTANT: this cannot be > 4096
static_assert(DMA_BUFFER_BYTE_LEN <= 4096, "DMA buffer size must be <= 4096 bytes");
#define BUFFER_READ_LEN (DMA_BUFFER_BYTE_LEN) // amount to try to read at once
#define WAV_BUFFER_LEN (100*REC_BYTES_PER_SAMPLE*REC_CHANNELS*REC_SAMPLE_RATE/1000)  // ~100 ms of audio buffered before writing to SD card
uint8_t readBuffer1[WAV_BUFFER_LEN + BUFFER_READ_LEN];
uint8_t readBuffer2[WAV_BUFFER_LEN + BUFFER_READ_LEN];
uint8_t writeBuffer[WAV_BUFFER_LEN + BUFFER_READ_LEN];


AudioCodec* audio_codec; // the audio codec object


#define CHECK_I2S_EVENTS // check for I2S events (errors) in the audioRecordingTask, undefine to disable
#ifdef CHECK_I2S_EVENTS
#include <freertos/queue.h>
QueueHandle_t i2sQueue = NULL;
#endif


void lowPassFilterGivenChannel(int16_t *samples, size_t numSamples, int channel, float alpha) {
    static float prev[REC_CHANNELS] = { 0.0f, 0.0f }; // filter values for each channel
    for (size_t i = channel; i < numSamples; i += REC_CHANNELS) {
        prev[channel] = alpha * samples[i] + (1 - alpha) * prev[channel];
        samples[i] = (int16_t)prev[channel];
    }
}

void lowPassFilter(uint8_t *data, size_t dataSize, float alpha) {
    int16_t *samples = (int16_t *)data;
    size_t numSamples = dataSize / sizeof(int16_t);
    for (int ch = 0; ch < REC_CHANNELS; ++ch) { lowPassFilterGivenChannel(samples, numSamples, ch, alpha); }
}

/**
 * Permanent task that continually reads audio data from the I2S bus and write it to the SD card
 * while also writing audio data back to the I2S bus for playback.
 */
void audioRecordingTask(void *pvParameters) {
    uint8_t* buffer = readBuffer1; // the current buffer being read into (switches between readBuffer1 and readBuffer2 for double buffering)
    uint16_t offset = 0; // the current offset in the buffer
    // We only allow one write at a time, so we can use a single instance of the params
    WriteWAVParams params = { .writing = false };

    while (true) {
#ifdef CHECK_I2S_EVENTS
        // Check for I2S events (errors)
        if (i2sQueue) {
            i2s_event_t evt;
            while (xQueueReceive(i2sQueue, &evt, 0)) {
                if (evt.type == I2S_EVENT_DMA_ERROR) {
                    printf("!! I2S DMA error\n");
                } else if (evt.type == I2S_EVENT_RX_Q_OVF) {
                    printf("!! I2S Overflow receive buffer\n");
                } else if (evt.type == I2S_EVENT_TX_Q_OVF) {
                    printf("!! I2S Overflow transmit buffer\n");
                // } else if (evt.type == I2S_EVENT_RX_DONE) {
                //     printf("-- I2S Received\n");
                // } else if (evt.type == I2S_EVENT_TX_DONE) {
                //     printf("-- I2S Transmitted\n");
                }
            }
        }
#endif

        // Read audio data from the I2S bus
        // TODO: several questions about i2s_read:
        //    giving a smaller buffer size (e.g. 64) reduces latency but increases overhead
        //    giving a small timeout (instead of infinite) could also reduce latency
        size_t bytesRead = 0;
        esp_err_t result = i2s_read(I2S_PORT, &buffer[offset], BUFFER_READ_LEN, &bytesRead, portMAX_DELAY);
        if (result != ESP_OK) { printf("!! I2S read error\n"); continue; }
        //offset += bytesRead;
    
        int16_t min = 0, max = 0;
        for (int i = 0; i < bytesRead / REC_BYTES_PER_SAMPLE; i++) {
            int16_t sample = ((int16_t*)buffer)[i + offset / REC_BYTES_PER_SAMPLE];
            if (sample < min) min = sample;
            if (sample > max) max = sample;
        }
        printf("Audio sample read: %d bytes, offset: %d\n", bytesRead, offset);
        printf("Audio sample range: %d to %d\n", min, max);

        memcpy(writeBuffer, &buffer[offset], bytesRead);
        lowPassFilter(writeBuffer, bytesRead, 0.05);

        //vTaskDelay(40 / portTICK_PERIOD_MS); // delay for 10 ms to allow other tasks to run
        sendAudioToI2S(writeBuffer, bytesRead); // send the audio data to the I2S bus for playback

        // Write the buffer to the SD card every so often
        if (offset >= WAV_BUFFER_LEN) {
            // if (params.writing) { printf("!! Audio writing is too slow\n"); }

            // // Write the filled buffer to the SD card
            // params.buffer = buffer;
            // params.length = offset;
            // params.writing = true;
            // submitSDTask((SDCallback)writeWAVData, &params);

            // // Swap the buffers
            // buffer = (buffer == readBuffer1) ? readBuffer2 : readBuffer1;
            offset = 0;

            // TODO: remove this debug print
            // printf("Audio Recording High watermark: %u\n", uxTaskGetStackHighWaterMark(NULL));
        }

        yield();
    }

    vTaskDelete(NULL);
}


/**
 * Generate a dual-channel sine wave.
 * The frequency should be in the range of human hearing (20 Hz to 20 kHz).
 * Two common frequencies are 440 Hz (A4) and 523.25 Hz (C5).
 * The amplitude should be in the range of 0 to 32767.
 * The offset is the starting point of the wave.
 * This returns the new offset after generating the wave.
 * The buffer length is in elements (not bytes).
 */
uint32_t generateSineWave(float frequency, int16_t amplitude, uint32_t offset, uint16_t* buffer, uint32_t length) {
    const float angularFreq = 2.0 * PI * frequency / REC_SAMPLE_RATE;
    for (int i = 0; i < length/2; i++) {
        buffer[2*i] = buffer[2*i+1] = amplitude * sin(angularFreq * (i + offset));
    }
    return (offset + length/2); // TODO: add in % so we don't overflow
}


/**
 * Mix two audio samples together.
 */
void mixAudio(uint16_t* sample1, uint16_t* sample2, uint32_t length, float ratio, uint16_t* output) {
    for (int i = 0; i < length; i++) {
        output[i] = sample1[i] * ratio + sample2[i] * (1 - ratio);
    }
}

/**
 * Add two audio samples together. Does not check for overflow.
 */
void addAudio(uint16_t* sample1, uint16_t* sample2, uint32_t length, uint16_t* output) {
    for (int i = 0; i < length; i++) {
        output[i] = sample1[i] + sample2[i];
    }
}


/**
 * Send audio data to the I2S bus for playback.
 */
void sendAudioToI2S(uint8_t* data, uint32_t length) {
    // TODO: is the loop necessary? can we just write the whole buffer at once? or maybe limit the size of each send or use a timeout and then use yield?
    size_t bytesWritten = 0;
    size_t offset = 0;
    while (offset < length) {
        i2s_write(I2S_PORT, &data[offset], length - offset, &bytesWritten, portMAX_DELAY);
        offset += bytesWritten;
    }
}


/**
 * Set the volume of the audio codec.
 * The volume must be in the range -48 to 79 where:
 *   <0 is muted (should not be less than -48)
 *   0 to 79 is -73dB to +6dB in 1dB steps
 */
void setVolume(int8_t volume) {
    if (audio_codec) {
        audio_codec->setVolume(volume);
    } else {
        printf("!! Audio codec not set up\n");
    }
}


/**
 * Set up the I2S driver. This makes the ESP32 the master and operate in both RX and TX modes.
 */
bool i2s_install() {
    static_assert(REC_BITS_PER_SAMPLE == 8 || REC_BITS_PER_SAMPLE == 16 || REC_BITS_PER_SAMPLE == 24 || REC_BITS_PER_SAMPLE == 32, "Bits per sample must be 8, 16, 24, or 32 - only supported by the I2S driver");
    static_assert(REC_CHANNELS == 1 || REC_CHANNELS == 2, "Channels must be 1 (mono) or 2 (stereo) - only supported by the I2S driver (unless TDM is supported)");
    const i2s_driver_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
        .sample_rate = REC_SAMPLE_RATE,
        .bits_per_sample = (i2s_bits_per_sample_t)REC_BITS_PER_SAMPLE,  // also supports 8, 24, and 32 bit
        .channel_format = (REC_CHANNELS == 2) ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = audio_codec->i2s_comm_format(),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,  // TODO: does a higher level make sense? (or 0 for default)
        
        // For more info see https://www.reddit.com/r/esp32/comments/muj6wo/esp32_i2s_dma_settings_dma_buf_len_and_dma_buf/
        // Basically:
        //  * dma_buf_len is the number of samples in each buffer, whenever we read from I2S we
        //    read in multiples of this size
        //  * increasing dma_buf_len reduces CPU overhead but reduces granularity and increases
        //    latency
        //  * dma_buf_count is the number of buffers, increasing this increases memory usage but
        //    allows for more buffers to be queued up in the driver before a call to i2s_read
        // If we are keeping up with the audio data, we can use a small dma_buf_count (like 2). To
        // reduce latency, we can use a small dma_buf_len (384 would be 8 ms at 48 kHz, 128 would
        // be 8 ms at 16 kHz).
        .dma_buf_count = 8,  // TODO: lower this
        .dma_buf_len = DMA_BUFFER_SAMPLE_LEN,

        .use_apll = false,
        .tx_desc_auto_clear = false,  // for cleaner outputs when there are delays
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,  // TODO: 512?
        .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,  // default means to use the same as bits_per_sample
    };
#ifdef CHECK_I2S_EVENTS
#define QUEUE_ARG 4, &i2sQueue
#else
#define QUEUE_ARG 0, NULL
#endif
    if (i2s_driver_install(I2S_PORT, &i2s_config, QUEUE_ARG) != ESP_OK) { printf("!! i2s_driver_install()\n"); return false; }
    return true;

    //i2s_set_clk(I2S_PORT, REC_SAMPLE_RATE, REC_BITS_PER_SAMPLE, I2S_CHANNEL_STEREO);
    //i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
}


/**
 * Set up the I2S pins for the ESP32 Thing Plus C.
 */
bool i2s_setpin() {
    const i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_DAC_DATA,
        .data_in_num = I2S_ADC_DATA
    };
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) { printf("!! i2s_set_pin()\n"); return false; }
    return true;
}


/**
 * Set up the audio codec and I2S for recording audio.
 * Before this is called, the Serial and Wire interfaces must be set up.
 * If there is a problem with the audio setup, the program will freeze.
 */
bool setupAudio() {
    audio_codec = create_audio_codec();
    if (!audio_codec->setup()) { printf("!! Audio codec setup failed.\n"); return false; }
    vTaskDelay(10 / portTICK_PERIOD_MS); // Give time for codec to settle after setup
    if (!i2s_install() || !i2s_setpin()) { return false; }

    // Start the audio recording task
    // TODO: can the stack size be smaller?
    if (xTaskCreate(audioRecordingTask, "AudioRecording", 4096, NULL, 1, NULL) != pdPASS) {
        printf("!! Failed to create audio recording task\n");
        return false;
    }
    return true;
}
