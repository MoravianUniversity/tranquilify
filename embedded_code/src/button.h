#pragma once

#include <stdbool.h>

/**
 * Set up the button for recording timestamps of button presses and releases.
 * This sets up an interrupt on the button pin along with a queue to store the timestamps and a
 * task to write them to the SD card.
 */
bool setupButton();
