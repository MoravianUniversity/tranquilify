#include "volume.h"
#include "audio.h"

#include <stdbool.h>
#include <stdint.h>

#include <Arduino.h> // for analogRead() and Serial

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


// Number of volume readings to average over, higher will be smoother but slower to respond
#define NUM_OF_VOL_READINGS 20

// Delay between volume readings in milliseconds, lower will be faster to respond but more overhead
#define VOL_READING_DELAY 10

// The pin the volume is connected to
#define VOLUME_PIN A0

// Possible range of the volumes in dB (defined by the audio codec)
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
    int volumeReadingPosition = 0;
    bool firstTime = true;  // if we haven't filled the array yet
    uint8_t prevVolume = 0;  // the previous set volume level

    while (true) {
        // Read the volume level from the ADC into the rolling average array
        volumeReadings[volumeReadingPosition] = analogRead(VOLUME_PIN);
        if (++volumeReadingPosition > NUM_OF_VOL_READINGS) { volumeReadingPosition = 0; firstTime = false; }

        // Calculate the average of the last 20 readings
        int32_t total = 0;
        int count = firstTime ? volumeReadingPosition : NUM_OF_VOL_READINGS;
        for (int i = 0; i < count; i++) { total += volumeReadings[i]; }
        int32_t average = total / count;

        // Map it from 0-4096 to a volume level (0-79) [<0 is muted, 0 is -73dB, 79 is +6dB (1dB steps)]
        int8_t volume = (ABSOLUTE_MAX_VOLUME - MIN_VOLUME) - (average * (MAX_VOLUME - MIN_VOLUME + 1)) / 4096;

        // If the volume has changed, update the audio codec
        if (volume != prevVolume) {
            setVolume(volume);
            //Serial.printf("Volume:  %03x  %02x  %d dB\n", average, volume, (int)volume - 73);
        }
        prevVolume = volume;

        vTaskDelay(VOL_READING_DELAY / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}


/**
 * Set up the volume monitor task. This task reads the volume level from the ADC and adjusts the
 * audio codec's volume.
 * The return value indicates if the task was successfully created.
 */
bool setupVolumeMonitor() {
    // TODO: can the stack size be smaller?
    if (xTaskCreate(adjustVolumeTask, "AdjustVolume", 1024, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) {
        Serial.println("!! Failed to create the adjust volume task");
        return false;
    }
    return true;
}
