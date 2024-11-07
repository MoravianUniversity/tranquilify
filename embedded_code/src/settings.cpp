#include "settings.h"

#include <SPIFFS.h>
#include <FS.h>

void setupSettings() {
    if (!SPIFFS.begin(true)) { Serial.println("!! SPIFFS Mount failed!"); }
}


///// Counter /////

int _counter = -1; // cache the counter value, -1 means it hasn't been read yet

int getCounter() {
    if (_counter < 0) {
        _counter = 0;
        File file = SPIFFS.open("/counter");
        if (file) { file.read((uint8_t*)&_counter, sizeof(_counter)); file.close(); }
    }
    return _counter;
}

int setCounter(int counter) {
    File file = SPIFFS.open("/counter", FILE_WRITE);
    if (file) { file.write((uint8_t*)&counter, sizeof(counter)); file.close(); }
    _counter = counter;
    return counter;
}

int incrementCounter() { return setCounter(getCounter() + 1); }
