#pragma once
#include <stdbool.h>

/**
 * Set up the volume monitor task. This task reads the volume level from the ADC and adjusts the
 * audio codec's volume.
 * This should be called after setupAudio().
 * The return value indicates if the task was successfully created.
 */
bool setupVolumeMonitor();
