#include <stdint.h>
#include "driver/ledc.h"

/**
 * @brief Generate a clock signal using the LEDC driver.
 * 
 * Generates a clock signal on the specified GPIO pin at the given frequency.
 * The LEDC driver is used to create a PWM signal that acts as a clock. The
 * clock signal can be at most 40 MHz.
 * 
 * @param frequency The frequency of the clock signal in Hz.
 * @param gpio The GPIO pin number to output the clock signal.
 */
class ClockSignal {
    ledc_timer_config_t timer;
    ledc_channel_config_t channel;
public:
    ClockSignal(uint32_t frequency, int gpio);
    ~ClockSignal();
    esp_err_t pause();
    esp_err_t resume();
};
