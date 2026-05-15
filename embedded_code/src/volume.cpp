#include "volume.h"
#include "audio.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h> // for memset

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/adc.h>


// Number of volume readings to average over, higher will be smoother but slower to respond
#define NUM_OF_VOL_READINGS 20

// Delay between volume readings in milliseconds, lower will be faster to respond but more overhead
#define VOL_READING_DELAY 10

// The pin the volume is connected to
#define VOLUME_PIN 34  // A2, on ADC1 (ADC2 has several restrictions, may as well avoid it)
#define VOLUME_CHANNEL ADC1_CHANNEL_6  // need to look at datasheet for converting pin to channel

// Possible range of the volumes in dB (defined by the audio codec)
// TODO: adjust these based on the audio codec
#define ABSOLUTE_MAX_VOLUME 6
#define ABSOLUTE_MIN_VOLUME -73 // mapped to the value 0

// Range of the volumes in dB
// TODO: calibrate these values
#define MAX_VOLUME 6  // at most ABSOLUTE_MAX_VOLUME
#define MIN_VOLUME -73  // at least ABSOLUTE_MIN_VOLUME

static_assert(MAX_VOLUME <= ABSOLUTE_MAX_VOLUME, "MAX_VOLUME must be less than or equal to ABSOLUTE_MAX_VOLUME");
static_assert(MIN_VOLUME >= ABSOLUTE_MIN_VOLUME, "MIN_VOLUME must be greater than or equal to ABSOLUTE_MIN_VOLUME");


/**
 * Constantly monitor the volume level from the ADC and adjust the audio codec's volume.
 * This task will run forever and reads the volume level from the ADC every VOL_READING_DELAY milliseconds.
 * The volume level is then mapped to a volume level and set on the audio codec.
 * The volume level is calculated as a rolling average of the last NUM_OF_VOL_READINGS readings.
 */
void adjustVolumeTask(void* pvParameters) {
    uint16_t volumeReadings[NUM_OF_VOL_READINGS] = {};  // used to make a rolling average of readings on ADC input
    memset(volumeReadings, 0, sizeof(volumeReadings));
    int32_t total = 0;  // the total of the last NUM_OF_VOL_READINGS readings
    uint8_t prevVolume = 0;  // the previous set volume level

    // Get the initial readings and total
    for (int i = 0; i < NUM_OF_VOL_READINGS; i++) {
        volumeReadings[i] = adc1_get_raw(VOLUME_CHANNEL);
        total += volumeReadings[i];
        vTaskDelay(VOL_READING_DELAY / portTICK_PERIOD_MS / 4); // shorter delay to get the initial readings faster
    }

    // Main loop to read the volume level and adjust the audio codec's volume
    int i = 0;
    while (true) {
        // Read the volume level from the ADC into the rolling average array
        // Also update the total to make the average calculation faster
        total -= volumeReadings[i];
        volumeReadings[i] = adc1_get_raw(VOLUME_CHANNEL);
        total += volumeReadings[i];
        if (++i >= NUM_OF_VOL_READINGS) { i = 0; }

        // Map it from 0-4096 to a volume level (0-79) [<0 is muted, 0 is -73dB, 79 is +6dB (1dB steps)]
        int8_t volume = (ABSOLUTE_MAX_VOLUME - MIN_VOLUME) - (total * (MAX_VOLUME - MIN_VOLUME + 1)) / (NUM_OF_VOL_READINGS * 4096);

        // If the volume has changed, update the audio codec
        if (volume != prevVolume) {
            setVolume(volume);
            //printf("Volume:  %03x  %02x  %d dB\n", total/NUM_OF_VOL_READINGS, volume, (int)volume - 73);
        }
        prevVolume = volume;

        vTaskDelay(VOL_READING_DELAY / portTICK_PERIOD_MS);

        // printf("Volume Monitor Task: %d\n", uxTaskGetStackHighWaterMark(NULL));
    }

    vTaskDelete(NULL);
}


/**
 * Set up the volume monitor task. This task reads the volume level from the ADC and adjusts the
 * audio codec's volume.
 * The return value indicates if the task was successfully created.
 */
void setupVolumeMonitor() {
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));  // 12-bit width gives values 0 to 4096
    ESP_ERROR_CHECK(adc1_config_channel_atten(VOLUME_CHANNEL, ADC_ATTEN_DB_11));  // widest range of reading values

#ifdef DEBUG
    gpio_num_t pin;
    ESP_ERROR_CHECK(adc1_pad_get_io_num(VOLUME_CHANNEL, &pin));
    assert(pin == VOLUME_PIN);
#endif

    // A stack size of 1024 was just barely too small (could do a printf() but not setVolume())
    // With +80, the high water mark is 40, indicating +40 should be sufficient, but reducing to +72 becomes too small
    if (xTaskCreate(adjustVolumeTask, "AdjustVolume", 1024+80+48, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) {
        printf("!! Failed to create the adjust volume task\n");
        abort();
    }
}
