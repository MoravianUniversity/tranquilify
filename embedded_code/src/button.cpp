#include "button.h"
#include "data.h"

#include <Arduino.h>

#define BUTTON_PIN 34
#define DEBOUNCE_DELAY 200 // in milliseconds

bool isOn = false;

unsigned long lastPress = 0;

void IRAM_ATTR onPress() {
    // Debounce the button
    if (millis() - lastPress < DEBOUNCE_DELAY) { return; }
    lastPress = millis();

    // TODO: remove this debug code
    isOn = !isOn;
    digitalWrite(LED_PIN, isOn);

    // Record the timestamp (actually adds it to a queue to be written to the file later)
    recordTimestampFromISR();
}

void setupButton() {
    pinMode(BUTTON_PIN, INPUT); // TODO: INPUT_PULLUP or INPUT_PULLDOWN? This may allow removing a resistor from our circuit
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, RISING);
}
