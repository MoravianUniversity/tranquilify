#pragma once
#include <stdbool.h>

#define DISABLE_FS_H_WARNING
#include <SdFat.h>

#define MAX_FILE_TASKS 8

/** Set up the SD card task for later use. */
bool setupSD();

/**
 * Callback for SD file tasks. If the SD card is not present, the first parameter will be NULL.
 */
typedef bool (*SDCallback)(SdFs* sd, void* params);

/**
 * Submit a file task to the SD card task.
 * If there is no room in the queue, this will block until there is.
 */
void submitSDTask(SDCallback callback, void* params);

/**
 * Submit a file task to the SD card task.
 * This waits for a certain amount of time for the task to be submitted on to the queue.
 * If there is no room in the queue after the time has elapsed, this will return false.
 */
// bool submitSDTask(SDCallback callback, void* params, TickType_t ticks_to_wait);

/**
 * Submit a file task to the SD card task from an ISR.
 * If there is no room in the queue, this will return false.
 */
bool IRAM_ATTR submitSDTaskFromISR(SDCallback callback, void* params);
