#include <Arduino.h>

#include <Wire.h> // I2C communication

#include "settings.h"
#include "audio.h"
#include "data.h"
#include "button.h"

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);
    while (!Serial) { yield(); } delay(1000); // Give time to open serial monitor (for debugging)
    Serial.println("SoundSound");

    setupSettings();
    setupButton();

    // Start I2C communication (must be before setupAudio())
    Wire.begin();

    setupSD();
    setupAudio();
}

void loop() {
    readAudioData();
}
