#include "button.h"
#include "data.h"

#include <stdbool.h>

#include <Arduino.h> // for Serial, millis, pinMode, attachInterrupt, digitalPinToInterrupt, ...

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// The button pin
// While this code does do some debouncing, it is assumed there is a hardware debouncing circuit (e.g. a capacitor)
// The button will be set up to be pulled low
#define BUTTON_PIN 4 // GPIO4

// Each ButtonEvent represents a button press and release
struct ButtonEvent {
    unsigned long pressTime;
    unsigned long releaseTime;
};

// Queue of ButtonEvents, statically allocated (see https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation#creating-an-rtos-object-using-statically-allocated-ram)
// Used to send button press and release events from the ISR to the button task
static QueueHandle_t buttonQueue;
static StaticQueue_t _buttonQueue_buf;
#define QUEUE_LENGTH 3
uint8_t _buttonQueue_storage[QUEUE_LENGTH * sizeof(ButtonEvent)];

// Optimize the GPIO read based on the pin number
#if BUTTON_PIN < 32
#define BUTTON_REG GPIO_IN_REG
#else
#define BUTTON_REG GPIO_IN1_REG
#endif
#define READ_BUTTON() (REG_READ(BUTTON_REG) & (1 << BUTTON_PIN)) // read the button pin (without shift)

// The current button state (used in the interrupt handlers)
unsigned long lastPress = 0;
unsigned long lastRelease = 0;
bool buttonPressed = false;


/** Record button press into the global variables (with some debouncing). */
void IRAM_ATTR onPress() {
    //Serial.printf("%d Button pressed (%s)\n", millis(), READ_BUTTON ? "HIGH" : "LOW");

    // if already pressed, ignore it (unless it's been 10 seconds which means we missed the release...)
    unsigned long now = millis();
    if (buttonPressed && now - lastPress < 10000) { return; }
    // if too short of a press, ignore it (debounce)
    if (now - lastRelease < 10) { return; }

    buttonPressed = true;
    lastPress = now;
}

/**
 * Record button release into the global variables.
 * Add the button press and release times to the button queue.
 */
void IRAM_ATTR onRelease() {
    //Serial.printf("%d Button released (%s)\n", millis(), READ_BUTTON ? "HIGH" : "LOW");

    if (!buttonPressed) { return; }
    buttonPressed = false;
    lastRelease = millis();

    //Serial.printf("Button pressed for %lu ms (from %lu to %lu)\n", lastRelease - lastPress, lastPress, lastRelease);
    ButtonEvent be = {.pressTime = lastPress, .releaseTime = lastRelease};
    BaseType_t higher_priority_task_woken = pdFALSE;
    xQueueSendToBackFromISR(buttonQueue, &be, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) { portYIELD_FROM_ISR(); }
}

/**
 * Interrupt handler for the button pin.
 * Defers to onPress() or onRelease() based on the current button state.
 */
void IRAM_ATTR onChange() {
    if (READ_BUTTON()) { onPress(); }
    else { onRelease(); }

#ifdef _DEBUG
    digitalWrite(13 /*LED_PIN*/, buttonPressed ? HIGH : LOW);
#endif
}

/**
 * Task that writes button press and release timestamps to the SD card.
 */
void buttonTask(void *pvParameters) {
    ButtonEvent be;
    char buffer[32];
    while (true) {
        if (xQueueReceive(buttonQueue, &be, portMAX_DELAY)) {
            sprintf(buffer, "%lu %lu\n", be.pressTime, be.releaseTime);
            // TODO: write to the SD card
        }
    }
    vTaskDelete(NULL);
}

/**
 * Set up the button for recording timestamps of button presses and releases.
 * This sets up an interrupt on the button pin along with a queue to store the timestamps and a
 * task to write them to the SD card.
 */
bool setupButton() {
    // Create the button queue
    buttonQueue = xQueueCreateStatic(QUEUE_LENGTH, sizeof(ButtonEvent), _buttonQueue_storage, &_buttonQueue_buf);
    if (buttonQueue == NULL) { Serial.println("!! Failed to create the button queue"); return false; }

    // Create the button task
    // TODO: can the stack size be smaller?
    if (xTaskCreate(buttonTask, "Button", 1024, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) { Serial.println("!! Failed to create the button task"); return false; }

    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onChange, CHANGE);
    return true;
}
