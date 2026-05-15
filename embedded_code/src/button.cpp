#include "button.h"
#include "config.h"
#include "sd.h"
#include "data.h"

#include <stdbool.h>

#include <driver/gpio.h>

// The button pin
// This code does some debouncing, it assumes there is hardware debouncing (e.g. a capacitor)
// The button will be set up to be pulled low
#define BUTTON_PIN 4 // GPIO4

// Optimize the GPIO read based on the pin number
// No shift is performed at the end
#if BUTTON_PIN < 32
#define READ_BUTTON() ((*(volatile uint32_t *)(0x3ff4403c)) & (1 << BUTTON_PIN))
#else
#define READ_BUTTON() ((*(volatile uint32_t *)(0x3ff44040)) & (1 << (BUTTON_PIN-32)))
#endif

// The current button state (used in the interrupt handlers)
unsigned long IRAM_DATA_ATTR WORD_ALIGNED_ATTR lastPress = 0;  // when 0 not currently pressed
unsigned long IRAM_DATA_ATTR WORD_ALIGNED_ATTR lastRelease = 0;


/** Record button press into the global variables (with some debouncing). */
void IRAM_ATTR onPress() {
    // if already pressed, ignore it (unless it's been 10 secs which means we missed the release)
    unsigned long now = (unsigned long)(esp_timer_get_time() / 1000ULL); // milliseconds
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
    if (lastPress == 0) { return; }
    lastRelease = (unsigned long)(esp_timer_get_time() / 1000ULL); // milliseconds

    // Write the button press and release times to the file (using a task)
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
void IRAM_ATTR onChange(void*) {
    if (READ_BUTTON()) { onPress(); }
    else { onRelease(); }

#ifdef DEBUG
    gpio_set_level((gpio_num_t)DEBUG_LED_PIN, lastPress ? 1 : 0);
#endif
}


/**
 * Set up the button for recording timestamps of button presses and releases.
 * This sets up an interrupt on the button pin along with a queue to store the timestamps and a
 * task to write them to the SD card.
 */
void setupButton() {
    // TODO: due to issues with edge-triggered interrupts on ESP32, some extra logic may be needed here and in onChange.
    // See https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32/03-errata-description/esp32/gpio-edge-interrupts.html

    // Set up the button pin to input-pulldown
    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&button_conf));

    // Add the ISR handler for the button pin
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { ESP_ERROR_CHECK(err); } // invalid state means already installed
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)BUTTON_PIN, onChange, NULL));
}
