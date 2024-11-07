#include "button.h"
#include "data.h"

#include <Arduino.h>

#define BUTTON_PIN 34

bool isOn = false;

void IRAM_ATTR onPress() {
    // TODO: debounce the button

    // TODO: remove this debug code
    isOn = !isOn;
    digitalWrite(LED_PIN, isOn);

    // TODO: we should not write to a file from an interrupt - causes lots of issues
    // General idea in an ISR is to do as little as possible
    // Maybe add the value to a queue and then write it in the main loop
    //recordTimestamp();
}

void setupButton() {
    pinMode(BUTTON_PIN, INPUT); // TODO: INPUT_PULLUP or INPUT_PULLDOWN? This may allow removing a resistor from our circuit
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, RISING);
}
