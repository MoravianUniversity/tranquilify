#include "settings.h"

#include <stdio.h>
#include <esp_spiffs.h>

// TODO: move to LittleFS which is faster and wastes less space
// See https://github.com/joltwallet/esp_littlefs

#define BASE_SETTINGS_PATH "/spiffs"
#define SETTINGS_PARTITION_LABEL "settings"

/** Set up the settings for the program. Settings are persistently stored in SPIFFS. */
void setupSettings() {
    esp_vfs_spiffs_conf_t conf = {  // -> esp_vfs_littlefs_conf_t
        .base_path = BASE_SETTINGS_PATH,
        .partition_label = SETTINGS_PARTITION_LABEL,
        .max_files = 5, // max simultaneously open files
        .format_if_mount_failed = true,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));  // -> esp_vfs_littlefs_register
}


///// Counter /////

int _counter = -1; // cache the counter value, -1 means it hasn't been read yet

/** Get the current, persistently increasing, counter value. */
int getCounter() {
    if (_counter < 0) {
        _counter = 0;
        FILE* file = fopen(BASE_SETTINGS_PATH "/counter", "r");
        if (file) { fread((uint8_t*)&_counter, sizeof(_counter), 1, file); fclose(file); }
    }
    return _counter;
}

/** Set the current, persistently increasing, counter value. Return the new value. */
int setCounter(int counter) {
    FILE* file = fopen(BASE_SETTINGS_PATH "/counter", "w");
    if (file) { fwrite((uint8_t*)&counter, sizeof(counter), 1, file); fclose(file); }
    _counter = counter;
    return counter;
}

/** Increment the current, persistently increasing, counter value. Return the new value. */
int incrementCounter() { return setCounter(getCounter() + 1); }
