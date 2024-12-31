#include <Arduino.h>

#include <Wire.h> // I2C communication

#include "settings.h"
#include "audio.h"
#include "volume.h"
#include "data.h"
#include "button.h"

#define LED_PIN 13

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);
    while (!Serial) { yield(); } delay(1000); // Give time to open serial monitor (for debugging)
    Serial.println("Tranquilify");

    setupSettings();
    setupButton();

    // To turn on the power for the Qwiic connector (takes a lot of power)
    // pinMode(0, OUTPUT);
    // digitalWrite(0, HIGH);

    // Start I2C communication (must be before setupAudio())
    Wire.begin();

    setupData();
    setupAudio();
    setupVolumeMonitor();
}

void loop() { yield(); }
