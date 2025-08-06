#include "clock_signal.hpp"
#include "driver/ledc.h"
#include <stdint.h>  // uint32_t, uint8_t
#include <string.h>  // memset

uint8_t timers_in_use = 0;  // bitmask for timers 0-3, 1 means in use
uint8_t channels_in_use = 0; // bitmask for channels 0-7, 1 means in use

ClockSignal::ClockSignal(uint32_t frequency, int gpio) {
    // Configure the LEDC timer
    memset(&timer, 0, sizeof(timer)); // clear the timer config
    timer.speed_mode = LEDC_HIGH_SPEED_MODE;
    timer.duty_resolution = frequency > 20000000 ? LEDC_TIMER_1_BIT : LEDC_TIMER_2_BIT;
    timer.timer_num = LEDC_TIMER_0; // start with timer 0
    while (timers_in_use & (1 << timer.timer_num)) { // find an unused timer
        timer.timer_num = (ledc_timer_t)((timer.timer_num + 1) % LEDC_TIMER_MAX);
    }
    timers_in_use |= (1 << timer.timer_num); // mark this timer as in use
    timer.freq_hz = frequency;

    // Configure the LEDC channel
    memset(&channel, 0, sizeof(channel)); // clear the channel config
    channel.gpio_num = gpio;                 // GPIO pin for output
    channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0; // start with channel 0
    while (channels_in_use & (1 << channel.channel)) { // find an unused channel
        channel.channel = (ledc_channel_t)((channel.channel + 1) % LEDC_CHANNEL_MAX);
    }
    channels_in_use |= (1 << channel.channel); // mark this channel as in use
    channel.timer_sel = timer.timer_num;
    channel.duty = frequency > 20000000 ? 1 : 2; // from 0 to 2^(duty_resolution), so 0 to 4 when res is 2 bits

    ledc_timer_config(&timer);
    ledc_channel_config(&channel);
}

ClockSignal::~ClockSignal() {
    ledc_timer_pause(timer.speed_mode, timer.timer_num);
    ledc_stop(channel.speed_mode, channel.channel, 0);
    timers_in_use &= ~(1 << timer.timer_num); // mark this timer as not in use
    channels_in_use &= ~(1 << channel.channel); // mark this channel as not in use
}

esp_err_t ClockSignal::pause() { return ledc_timer_pause(timer.speed_mode, timer.timer_num); }
esp_err_t ClockSignal::resume() { return ledc_timer_resume(timer.speed_mode, timer.timer_num); }
