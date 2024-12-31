#include "settings.h"

#include <SPIFFS.h>
#include <FS.h>

/** Set up the settings for the program. Settings are persistently stored in SPIFFS. */
void setupSettings() {
    if (!SPIFFS.begin(true)) { Serial.println("!! SPIFFS Mount failed!"); }
}


///// Counter /////

int _counter = -1; // cache the counter value, -1 means it hasn't been read yet

/** Get the current, persistently increasing, counter value. */
int getCounter() {
    if (_counter < 0) {
        _counter = 0;
        File file = SPIFFS.open("/counter");
        if (file) { file.read((uint8_t*)&_counter, sizeof(_counter)); file.close(); }
    }
    return _counter;
}

/** Set the current, persistently increasing, counter value. Return the new value. */
int setCounter(int counter) {
    File file = SPIFFS.open("/counter", FILE_WRITE);
    if (file) { file.write((uint8_t*)&counter, sizeof(counter)); file.close(); }
    _counter = counter;
    return counter;
}

/** Increment the current, persistently increasing, counter value. Return the new value. */
int incrementCounter() { return setCounter(getCounter() + 1); }
