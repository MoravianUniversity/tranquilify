#include <Arduino.h>

#include <Wire.h> // I2C communication

#include "config.h"
#include "settings.h"
#include "audio.h"
#include "volume.h"
#include "sd.h"
#include "button.h"

void setup() {
    pinMode(DEBUG_LED_PIN, OUTPUT);
    digitalWrite(DEBUG_LED_PIN, LOW);

    Serial.begin(115200);
    while (!Serial) { yield(); } delay(1000); // Give time to open serial monitor (for debugging)
    Serial.println("Tranquilify");

    if (!setupSettings()) { while (true); }
    if (!setupButton()) { while (true); }

    // To turn on the power for the Qwiic connector (takes a lot of power)
    // pinMode(0, OUTPUT);
    // digitalWrite(0, HIGH);

    // Start I2C communication (must be before setupAudio())
    if (!Wire.begin()) { Serial.println("!! I2C communication failed"); while (true); }

    if (!setupSD()) { while (true); }
    if (!setupAudio()) { while (true); }
    if (!setupVolumeMonitor()) { while (true); }
}

//uint16_t audioBuffer[4096];
//uint32_t audioOffset = 0;

void loop() {
    // audioOffset = generateSineWave(440, 32767, audioOffset, audioBuffer, 4096);
    // sendAudioToI2S((uint8_t*)audioBuffer, 4096 * sizeof(uint16_t));
    yield();
}
