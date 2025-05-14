#include "sd.h"
#include "config.h"

#include <stdint.h>
#include <stdbool.h>

#define DISABLE_FS_H_WARNING
#include <SPI.h>
#include <SdFat.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// SD Card Pins
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5     // Thing Plus C

// The Thing Plus doesn't connect the SD card DET pin, so we can't auto-detect removals/insertions
// We could solder a wire to it (small notch near "SHLD") and connect that to a GPIO pin

#define SPI_SPEED SD_SCK_MHZ(30)  // best speed is 50 but >30 doesn't work/make a difference
#define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_SPEED)

// Speed notes:
//   - We can write ~1350 bytes/ms to the SD card (at 25 MHz) using the dedicated SPI bus
//     - That is about 1.3 MB/s (the audio is 172 KB/s)
//     - 30 MHz: 1740 bytes/ms -> 1.7 MB/s  (same as 35 MHz)
//   - Opening a file takes ~100 us (there seems to be some caching, first open is slower)
//     - At 30 MHz it is ~70 us after the first few opens
// Should probably not leave files open and just reopen them for every read/write as opening is
// apparently fast and already partially cached in the SD card driver. If we leave them open, we
// could run into issues with the SD card driver if a card is switched or similar (the file doesn't
// get closed when the card is removed, so it could be in an invalid state).

// Just one SD card object and a queue for submitting tasks to the SD card
// This allows us to use the faster dedicated SPI bus for the SD card
SdFs sd;
QueueHandle_t IRAM_DATA_ATTR WORD_ALIGNED_ATTR queue = NULL;
struct SDTaskParams {
    SDCallback callback;
    void* params;
};
TaskHandle_t sdTaskHandle = NULL;

/**
 * Setup the SD card. Returns true if the SD card is set up and false if there was an error.
 * This may be called multiple times to reinitialize the SD card if it is removed and reinserted.
 * This must be called from the only task that uses the SD card.
 */
bool setupSDCard() {
#ifdef DEBUG
    if (xTaskGetCurrentTaskHandle() != sdTaskHandle) {
        Serial.println("!! setupSDCard() called from wrong task");
        return false;
    }
    Serial.println("Setting up SD card...");
#endif

    // Make sure the SD card is not already initialized
    sd.end();

    // Initialize SD card
    if (!sd.begin(SD_CONFIG)) {
        if (sd.card()->errorCode()) {
            Serial.printf("!! SD card initialization failed: 0x%02x 0x%08x\n",
                (int)sd.card()->errorCode(), (int)sd.card()->errorData());
        } else if (sd.vol()->fatType() == 0) {
            Serial.println("!! SD card not formatted with FAT16/FAT32/exFAT");
        } else { Serial.println("!! SD card initialization failed"); }
        return false;
    }

    uint32_t sectors = sd.card()->sectorCount();
    if (sectors == 0) { Serial.println("!! Can't determine the SD card size.\n"); return false; }

#ifdef DEBUG
    // Print SD card info
    uint64_t size = sectors * 512ull; // 512 bytes per sector
    double sizeGB = size / (1024.0 * 1024.0 * 1024.0);

    uint32_t kbPerCluster = sd.vol()->sectorsPerCluster() / 2;
    int32_t freeClusterCount = sd.vol()->freeClusterCount();
    uint32_t freeKB = freeClusterCount < 0 ? 0 : freeClusterCount * kbPerCluster;
    double usedGB = freeClusterCount < 0 ? -1 : sizeGB - (freeKB / (1024.0 * 1024.0));

    uint8_t fatType = sd.vol()->fatType();
    char fatTypeStr[8] = { 0 };
    if (fatType <= 32) { sprintf(fatTypeStr, "FAT%d", fatType); }
    else { strcpy(fatTypeStr, "exFAT"); }

    Serial.printf("SD Card: %s, used %.2f / %.2f GB, cluster size: %d KB\n",
        fatTypeStr, usedGB, sizeGB, kbPerCluster);

    if ((sizeGB > 1 && kbPerCluster < 32) || (sizeGB < 2 && fatType == 32)) {
        Serial.println("This SD card should be reformatted for best performance.");
        Serial.println("Use a cluster size of 32 KB for cards larger than 1 GB.");
        Serial.println("Only cards larger than 2 GB should be formatted FAT32.");
    }
#endif

    return true;
}

/**
 * Ensure that the SD card is initialized.
 * Returns true if the SD card is available and false if it is not available.
 * This can be called even if the SD card is already initialized.
 * This must be called from the only task that uses the SD card.
 */
bool ensureSDCard() {
#ifdef DEBUG
    if (xTaskGetCurrentTaskHandle() != sdTaskHandle) {
        Serial.println("!! ensureSDCard() called from wrong task");
        return false;
    }
#endif
    bool avail = sd.card()->errorCode() == 0 &&
                sd.card()->sectorCount() != 0 &&
                sd.vol()->fatType() != 0;
    return avail || setupSDCard();
}

/** Task that runs tasks utilizing the SD card. */
void sdTask(void *pvParameters) {
    int highWaterMark = 0;

    SDTaskParams params;
    setupSDCard(); // try once at the beginning to save time later
    while (true) {
        if (xQueueReceive(queue, &params, portMAX_DELAY)) {
            params.callback(ensureSDCard() ? &sd : NULL, params.params);

            if (uxTaskGetStackHighWaterMark(NULL) > highWaterMark) { // TODO: remove this debug code
                highWaterMark = uxTaskGetStackHighWaterMark(NULL);
                Serial.printf("SD Card Task SHWM: %u\n", highWaterMark);
            }
        }
    }
    vTaskDelete(NULL);
}

/**
 * Set up the SD card task for later use.
 * Returns true if the queue and task was created and false if it was not.
 */
bool setupSD() {
    queue = xQueueCreate(MAX_FILE_TASKS, sizeof(SDTaskParams));
    if (!queue) { Serial.println("!! Failed to create SD card queue"); return false; }

    // TODO: can the stack size be smaller?
    if (xTaskCreate(sdTask, "SDCard", 4096*4, NULL, tskIDLE_PRIORITY, &sdTaskHandle) != pdPASS) {
        vQueueDelete(queue);
        queue = NULL;
        Serial.println("!! Failed to create SD card task");
        return false;
    }

    return true;
}

/**
 * Submit a file task to the SD card task.
 * If there is no room in the queue, this will block until there is.
 */
void submitSDTask(SDCallback callback, void* params) {
    SDTaskParams sdtp = {
        .callback = callback,
        .params = params,
    };
    xQueueSendToBack(queue, &sdtp, portMAX_DELAY);
}

/**
 * Submit a file task to the SD card task.
 * This waits for a certain amount of time for the task to be submitted on to the queue.
 * If there is no room in the queue after the time has elapsed, this will return false.
 */
bool submitSDTask(SDCallback callback, void* params, TickType_t ticks_to_wait) {
    SDTaskParams sdtp = {
        .callback = callback,
        .params = params,
    };
    return xQueueSendToBack(queue, &sdtp, ticks_to_wait) == pdTRUE;
}

/**
 * Submit a file task to the SD card task from an ISR.
 * If there is no room in the queue, this will return false.
 */
bool IRAM_ATTR submitSDTaskFromISR(SDCallback callback, void* params) {
    SDTaskParams sdtp = {
        .callback = callback,
        .params = params,
    };
    BaseType_t higher_priority_task_woken = pdFALSE;
    bool retval = xQueueSendToBackFromISR(queue, &sdtp, &higher_priority_task_woken) == pdTRUE;
    if (higher_priority_task_woken == pdTRUE) { portYIELD_FROM_ISR(); }
    return retval;
}
