#include "button.h"
#include "config.h"
#include "sd.h"
#include "data.h"

#include <stdbool.h>

#include <Arduino.h> // for millis, pinMode, attachInterrupt, digitalPinToInterrupt, ...

// The button pin
// While this code does do some debouncing, it is assumed there is a hardware debouncing circuit (e.g. a capacitor)
// The button will be set up to be pulled low
#define BUTTON_PIN 4 // GPIO4

// Optimize the GPIO read based on the pin number
#if BUTTON_PIN < 32
#define BUTTON_REG GPIO_IN_REG
#else
#define BUTTON_REG GPIO_IN1_REG
#endif
#define READ_BUTTON() (REG_READ(BUTTON_REG) & (1 << BUTTON_PIN)) // read the button pin (without shift)

// The current button state (used in the interrupt handlers)
unsigned long IRAM_DATA_ATTR WORD_ALIGNED_ATTR lastPress = 0;  // when 0 means not currently pressed
unsigned long IRAM_DATA_ATTR WORD_ALIGNED_ATTR lastRelease = 0;


/** Record button press into the global variables (with some debouncing). */
void IRAM_ATTR onPress() {
    //Serial.printf("%d Button pressed (%s)\n", millis(), READ_BUTTON ? "HIGH" : "LOW");

    // if already pressed, ignore it (unless it's been 10 seconds which means we missed the release...)
    unsigned long now = millis();
    if (lastPress > 0 && now - lastPress < 10000) { return; }
    // if too short of a press, ignore it (debounce)
    if (now - lastRelease < 10) { return; }

    lastPress = now;
}


/**
 * Record button release into the global variables.
 * Add the button press and release times to the button queue.
 */
void IRAM_ATTR onRelease() {
    //Serial.printf("%d Button released (%s)\n", millis(), READ_BUTTON ? "HIGH" : "LOW");

    if (lastPress == 0) { return; }
    lastRelease = millis();

#ifdef DEBUG
    Serial.printf("Button pressed for %lu ms (from %lu to %lu)\n", lastRelease - lastPress, lastPress, lastRelease);
#endif

    // Write the button press and release times to the file
    static ButtonEvent events[MAX_FILE_TASKS];
    static unsigned char index = 0;
    events[index].pressTime = lastPress;
    events[index].releaseTime = lastRelease;
    submitSDTaskFromISR((SDCallback)writeButtonData, &events[index]);
    index = (index + 1) % MAX_FILE_TASKS;
    
    lastPress = 0;
}

/**
 * Interrupt handler for the button pin.
 * Defers to onPress() or onRelease() based on the current button state.
 */
void IRAM_ATTR onChange() {
    if (READ_BUTTON()) { onPress(); }
    else { onRelease(); }

#ifdef DEBUG
    digitalWrite(DEBUG_LED_PIN, lastPress ? HIGH : LOW);
#endif
}


/**
 * Set up the button for recording timestamps of button presses and releases.
 * This sets up an interrupt on the button pin along with a queue to store the timestamps and a
 * task to write them to the SD card.
 */
bool setupButton() {
    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onChange, CHANGE);
    return true;
}
