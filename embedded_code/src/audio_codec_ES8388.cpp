/**
 * Support for ES8388 audio codec. This is available on several ESP32 boards
 * and breakout boards.
 * 
 * This code generates a clock signal on a GPIO pin to provide the MCLK signal
 * for the ES8388. You must configure the CLOCK_PIN define to the GPIO pin
 * you want to use for the clock signal.
 * 
 * You should also configure if OUT1 and/or OUT2 are used for audio output and
 * if IN1, IN2, or their difference are used for audio input.
 */

#include "audio_codec.hpp"
#include "audio.h"

#if AUDIO_CODEC == AUDIO_CODEC_ES8388

#include "clock_signal.hpp"
#include <Wire.h>


// Configuration
#define LINE_OUTPUT 2           // output to OUT1, OUT2, or both (3) - on the breakout board, OUT1 is headphone jack, OUT2 is pins
#define LINE_INPUT 1            // input from IN1, IN2, L-R on IN1 (3), or L-R on IN2 (4) - on the breakout board, IN1 is mic jack with extra components
#define RECORDING_VOLUME 0xbb   // 30dB for left and right; value from 0-8 for each nibble, maps to 05dB to +24dB with 3dB steps, assuming 0xb is 33dB although undocumented
#define CLOCK_PIN 18            // GPIO pin for clock signal (may have resrictions, but at least 16 and 18 are available on the ESP32)
#define ES8388_ADDR 0x20        // if CE pin is held high then the address is 0x22


static_assert(REC_BITS_PER_SAMPLE == 16 || REC_BITS_PER_SAMPLE == 18 || REC_BITS_PER_SAMPLE == 20 || REC_BITS_PER_SAMPLE == 24 || REC_BITS_PER_SAMPLE == 32, "ES8388: REC_BITS_PER_SAMPLE must be 16, 18, 20, 24, or 32");
static_assert(REC_CHANNELS == 2, "ES8388: CHANNELS must be 2 (with work 1, 3, or 4 channels could be supported as well)");

static_assert(PLAY_BITS_PER_SAMPLE == 16 || PLAY_BITS_PER_SAMPLE == 18 || PLAY_BITS_PER_SAMPLE == 20 || PLAY_BITS_PER_SAMPLE == 24 || PLAY_BITS_PER_SAMPLE == 32, "ES8388: PLAY_BITS_PER_SAMPLE must be 16, 18, 20, 24, or 32");
static_assert(PLAY_CHANNELS == 2, "ES8388: CHANNELS must be 2 (with work 1, 3, or 4 channels could be supported as well)");

static_assert(REC_SAMPLE_RATE == 8000 || REC_SAMPLE_RATE == 8018 || REC_SAMPLE_RATE == 11025 || REC_SAMPLE_RATE == 12000 || REC_SAMPLE_RATE == 16000 || REC_SAMPLE_RATE == 22050 ||
    REC_SAMPLE_RATE == 24000 || REC_SAMPLE_RATE == 32000 || REC_SAMPLE_RATE == 44100 || REC_SAMPLE_RATE == 48000 || REC_SAMPLE_RATE == 88200 || REC_SAMPLE_RATE == 96000,
    "ES8388: REC_SAMPLE_RATE must be one of the supported rates (8000, 8018, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, or 96000)");
static_assert(REC_SAMPLE_RATE == PLAY_SAMPLE_RATE, "ES8388: REC_SAMPLE_RATE must be equal to PLAY_SAMPLE_RATE (with work they could be different)");


// Registers
#define REG_CONTROL1         0x00
#define C1_VMIDDiv_5k        0b11
#define C1_VMIDDiv_500k      0b10
#define C1_VMIDDiv_50k       0b01
#define C1_VMIDDiv_DIS       0b00

#define REG_CONTROL2         0x01
#define REG_CHIPPOWER        0x02
#define REG_ADCPOWER         0x03

#define REG_DACPOWER         0x04
#define LDAC_POWER_UP        (0 << 7)
#define LDAC_POWER_DOWN      (1 << 7)
#define RDAC_POWER_UP        (0 << 6)
#define RDAC_POWER_DOWN      (1 << 6)
#define LOUT1_EN             (1 << 5)
#define LOUT1_DIS            (0 << 5)
#define LOUT2_EN             (1 << 4)
#define LOUT2_DIS            (0 << 4)
#define ROUT1_EN             (1 << 3)
#define ROUT1_DIS            (0 << 3)
#define ROUT2_EN             (1 << 2)
#define ROUT2_DIS            (0 << 2)

#define REG_CHIPLOPOW1       0x05
#define REG_CHIPLOPOW2       0x06
#define REG_ANAVOLMANAG      0x07

#define REG_MASTERMODE       0x08
#define MM_I2S_SLAVE         (0 << 7)
#define MM_I2S_MASTER        (1 << 7)  // default
#define MM_MCLK_NO_DIV       (0 << 6)  // MCLK is not divided
#define MM_MCLK_DIV_2        (1 << 6)  // MCLK is divided by 2
#define MM_BCLK_NORMAL       (0 << 5)  // BCLK is normal
#define MM_BCLK_INV          (1 << 5)  // BCLK is inverted
#define MM_BCLKDIV_AUTO      0b00000   // BCLK is automatically determined
#define MM_BCLKDIV_MCLK_1    0b00001   // BCLK is MCLK/1
#define MM_BCLKDIV_MCLK_2    0b00010   // BCLK is MCLK/2
#define MM_BCLKDIV_MCLK_3    0b00011   // BCLK is MCLK/3
#define MM_BCLKDIV_MCLK_4    0b00100   // BCLK is MCLK/4
#define MM_BCLKDIV_MCLK_6    0b00101   // BCLK is MCLK/6
#define MM_BCLKDIV_MCLK_8    0b00110   // BCLK is MCLK/8
#define MM_BCLKDIV_MCLK_9    0b00111   // BCLK is MCLK/9
#define MM_BCLKDIV_MCLK_11   0b01000   // BCLK is MCLK/11
#define MM_BCLKDIV_MCLK_12   0b01001   // BCLK is MCLK/12
#define MM_BCLKDIV_MCLK_16   0b01010   // BCLK is MCLK/16
#define MM_BCLKDIV_MCLK_18   0b01011   // BCLK is MCLK/18
#define MM_BCLKDIV_MCLK_22   0b01100   // BCLK is MCLK/22
#define MM_BCLKDIV_MCLK_24   0b01101   // BCLK is MCLK/24
#define MM_BCLKDIV_MCLK_33   0b01110   // BCLK is MCLK/33
#define MM_BCLKDIV_MCLK_36   0b01111   // BCLK is MCLK/36
#define MM_BCLKDIV_MCLK_44   0b10000   // BCLK is MCLK/44
#define MM_BCLKDIV_MCLK_48   0b10001   // BCLK is MCLK/48
#define MM_BCLKDIV_MCLK_66   0b10010   // BCLK is MCLK/66
#define MM_BCLKDIV_MCLK_72   0b10011   // BCLK is MCLK/72
#define MM_BCLKDIV_MCLK_5    0b10100   // BCLK is MCLK/5
#define MM_BCLKDIV_MCLK_10   0b10101   // BCLK is MCLK/10
#define MM_BCLKDIV_MCLK_15   0b10110   // BCLK is MCLK/15
#define MM_BCLKDIV_MCLK_17   0b10111   // BCLK is MCLK/17
#define MM_BCLKDIV_MCLK_20   0b11000   // BCLK is MCLK/20
#define MM_BCLKDIV_MCLK_25   0b11001   // BCLK is MCLK/25
#define MM_BCLKDIV_MCLK_30   0b11010   // BCLK is MCLK/30
#define MM_BCLKDIV_MCLK_32   0b11011   // BCLK is MCLK/32
#define MM_BCLKDIV_MCLK_34   0b11100   // BCLK is MCLK/34

// ADC
#define REG_ADCCONTROL1      0x09  // left and right channel PGA gain
#define PGA_GAIN_DB(N)       (N / 3)  // allow from 0dB to 24dB in 3dB steps, default 0
#define MIC_AMP_L_SFT        4
#define MIC_AMP_R_SFT        0

#define REG_ADCCONTROL2      0x0a
#define INPUT1               0b00
#define INPUT2               0b01
#define INPUT_DIFF           0b11
#define INPUT_SEL_L_SFT      6
#define INPUT_SEL_R_SFT      4
#define DSSEL_SEPARATE       (1 << 3)
#define DSSEL_MATCH          (0 << 3)  // default
#define DSR_LINPUT2_RINPUT2  (1 << 2)
#define DSR_LINPUT1_RINPUT1  (0 << 2)  // default

#define REG_ADCCONTROL3      0x0b
#define DS_LINPUT2_RINPUT2   (1 << 7)
#define DS_LINPUT1_RINPUT1   (0 << 7)  // default
#define MONOMIX_STEREO       (0b00 << 3)  // default
#define MONOMIX_MONO_L_ADC   (0b01 << 3)
#define MONOMIX_MONO_R_ADC   (0b10 << 3)
#define ASDOUT_IS_ADC        (0 << 2)  // default
#define ASDOUT_IS_TRISTATE   (1 << 2)

#define REG_ADCCONTROL4      0x0c  // WL and FMT (and left/right channel selection and polarity)
#define REG_ADCCONTROL5      0x0d  // FsMode and FsRatio
#define REG_ADCCONTROL6      0x0e  // polarity inversion, high pass filters
#define REG_ADCCONTROL7      0x0f  // mute, soft ramp, LR gains match
#define REG_ADCCONTROL8      0x10  // left ADC (input) volume
#define REG_ADCCONTROL9      0x11  // right ADC (input) volume

#define REG_ADCCONTROL10     0x12  // ALC enable and min/max gain
#define ALC_OFF              0b00 << 6   // default
#define ALC_RIGHT            0b01 << 6   
#define ALC_LEFT             0b10 << 6
#define ALC_STEREO           0b11 << 6
#define MAXGAIN_n6_5dB       (0b000 << 3)
#define MAXGAIN_n0_5dB       (0b001 << 3)
#define MAXGAIN_5_5dB        (0b010 << 3)
#define MAXGAIN_11_5dB       (0b011 << 3)
#define MAXGAIN_17_5dB       (0b100 << 3)
#define MAXGAIN_23_5dB       (0b101 << 3)
#define MAXGAIN_29_5dB       (0b110 << 3)
#define MAXGAIN_35_5dB       (0b111 << 3)  // default
#define MINGAIN_n12dB        (0b000 << 0)  // default
#define MINGAIN_n6dB         (0b001 << 0)
#define MINGAIN_0dB          (0b010 << 0)
#define MINGAIN_6dB          (0b011 << 0)
#define MINGAIN_12dB         (0b100 << 0)
#define MINGAIN_18dB         (0b101 << 0)
#define MINGAIN_24dB         (0b110 << 0)
#define MINGAIN_30dB         (0b111 << 0)

#define REG_ADCCONTROL11     0x13  // ALC target level and hold time
#define ALC_TARGET_n16_5dB   (0b0000 << 4)
#define ALC_TARGET_n15dB     (0b0001 << 4)
#define ALC_TARGET_n13_5dB   (0b0010 << 4)
#define ALC_TARGET_n12dB     (0b0011 << 4)
#define ALC_TARGET_n10_5dB   (0b0100 << 4)
#define ALC_TARGET_n9dB      (0b0101 << 4)
#define ALC_TARGET_n7_5dB    (0b0110 << 4)
#define ALC_TARGET_n6dB      (0b0111 << 4)
#define ALC_TARGET_n4_5dB    (0b1000 << 4)
#define ALC_TARGET_n3dB      (0b1001 << 4)
#define ALC_TARGET_n1_5dB    (0b1010 << 4)  // default (actually 1011 but everything up to 1111 maps to -1.5dB)
#define ALC_HOLD_0ms         (0b0000 << 0)  // default
#define ALC_HOLD_2_67ms      (0b0001 << 0)
#define ALC_HOLD_5_33ms      (0b0010 << 0)
#define ALC_HOLD_10_67ms     (0b0011 << 0)
#define ALC_HOLD_21_33ms     (0b0100 << 0)
#define ALC_HOLD_42_67ms     (0b0101 << 0)
#define ALC_HOLD_85_33ms     (0b0110 << 0)
#define ALC_HOLD_170_67ms    (0b0111 << 0)
#define ALC_HOLD_341_33ms    (0b1000 << 0)
#define ALC_HOLD_0_68s       (0b1001 << 0)
#define ALC_HOLD_1_36s       (0b1010 << 0)  // everything up to 1111 maps to 1.36s

#define REG_ADCCONTROL12     0x14  // ALC decay (gain ramp up) and attack (gain ramp down)

#define REG_ADCCONTROL13     0x15  // ALC mode, zero cross, and window size
#define ALC_MODE_NORMAL      0 << 7  // default
#define ALC_MODE_LIMITER     1 << 7
#define ALC_ZERO_CROSS_EN    1 << 6
#define ALC_ZERO_CROSS_DIS   0 << 6  // default and recommended
#define ALC_ZC_TIME_OUT_EN   1 << 5
#define ALC_ZC_TIME_OUT_DIS  0 << 5  // default
#define ALC_WIN_SIZE(N)   ((N) >> 4)  // N / 16

#define REG_ADCCONTROL14     0x16  // noise gate control
#define NOISE_GATE_ADC_MUTE  0b01 << 1
#define NOISE_GATE_PGA_CONST 0b00 << 1  // default
#define NOISE_GATE_EN        1
#define NOISE_GATE_DIS       0  // default

// DAC
#define REG_DACCONTROL1      0x17  // WL and FMT (and left/right channel selection and polarity)
#define REG_DACCONTROL2      0x18  // FsMode and FsRatio
#define REG_DACCONTROL3      0x19  // mute, soft ramp, LR gains match
#define REG_DACCONTROL4      0x1a  // left DAC (output) volume
#define REG_DACCONTROL5      0x1b  // right DAC (output) volume
#define REG_DACCONTROL6      0x1c  // deep emphasis, phase inversion, click free power up/down

#define REG_DACCONTROL7      0x1d  // output all 0s for left and right channel, make mono, SE strength, Vpp scale
#define VPP_3_5              0b00  // default
#define VPP_4_0              0b01
#define VPP_3_0              0b10
#define VPP_2_5              0b11

#define REG_DACCONTROL16     0x26
#define LMIXSEL_LIN1   (0b000 << 3) // default
#define LMIXSEL_LIN2   (0b001 << 3)
#define LMIXSEL_LADC_P (0b011 << 3)
#define LMIXSEL_LADC_N (0b100 << 3)
#define RMIXSEL_RIN1   (0b000 << 0) // default
#define RMIXSEL_RIN2   (0b001 << 0)
#define RMIXSEL_RADC_P (0b011 << 0)
#define RMIXSEL_RADC_N (0b100 << 0)

#define REG_DACCONTROL17     0x27  // left mixer gain
#define REG_DACCONTROL20     0x2a  // right mixer gain
#define DAC2MIX_EN           (1 << 7)
#define DAC2MIX_DIS          (0 << 7)  // default
#define IN2MIX_EN            (1 << 6)
#define IN2MIX_DIS           (0 << 6)  // default
#define IN_GAIN_DB(N)        ((N) / -3 + 2)  // allow from -15dB to +6dB in 3dB steps

#define REG_DACCONTROL21     0x2b  // DAC and ADC same clock, master clock, offset, mclk, ADC DLL power, DAC DLL power
#define REG_DACCONTROL22     0x2c  // DC offset
#define REG_DACCONTROL23     0x2d  // VREF to analog output resistance
#define REG_DACCONTROL24     0x2e  // LOUT1 volume
#define REG_DACCONTROL25     0x2f  // ROUT1 volume
#define REG_DACCONTROL26     0x30  // LOUT2 volume
#define REG_DACCONTROL27     0x31  // ROUT2 volume

#define REG_DACCONTROL8      0x1e  // shelving filter
#define REG_DACCONTROL9      0x1f  // shelving filter
#define REG_DACCONTROL10     0x20  // shelving filter
#define REG_DACCONTROL11     0x21  // shelving filter
#define REG_DACCONTROL12     0x22  // shelving filter
#define REG_DACCONTROL13     0x23  // shelving filter
#define REG_DACCONTROL14     0x24  // shelving filter
#define REG_DACCONTROL15     0x25  // shelving filter
#define REG_DACCONTROL18     0x28  // not configurable
#define REG_DACCONTROL19     0x29  // not configurable
#define REG_DACCONTROL28     0x32  // not configurable
#define REG_DACCONTROL29     0x33  // reserved
#define REG_DACCONTROL30     0x34  // reserved


// For REG_ADCCONTROL7 and REG_DACCONTROL3
#define MUTE                (1 << 2)
#define UNMUTE              (0 << 2)
#define LR_GAINS_MATCH      (1 << 3)
#define NO_LR_GAINS_MATCH   (0 << 3)
#define SOFT_RAMP           (1 << 5)
#define NO_SOFT_RAMP        (0 << 5)
#define SOFT_RAMP_4LRCK     (0b00 << 6)
#define SOFT_RAMP_8LRCK     (0b01 << 6)
#define SOFT_RAMP_16LRCK    (0b10 << 6)
#define SOFT_RAMP_32LRCK    (0b11 << 6)

// For REG_ADCCONTROL4 and REG_DACCONTROL1
#define FMT_I2S             0b00
#define FMT_LEFT_JUSTIFIED  0b01
#define FMT_RIGHT_JUSTIFIED 0b10
#define FMT_DSP_PSM         0b11
#define WL_16BIT            (0b011 << 2)
#define WL_18BIT            (0b010 << 2)
#define WL_20BIT            (0b001 << 2)
#define WL_24BIT            (0b000 << 2)
#define WL_32BIT            (0b100 << 2)
constexpr uint8_t bitsToWL(uint8_t bits) {
    switch (bits) {
        case 16: return WL_16BIT;
        case 18: return WL_18BIT;
        case 20: return WL_20BIT;
        case 24: return WL_24BIT;
        case 32: return WL_32BIT;
        default: return WL_16BIT; // default to 16 bit
    }
}

// For REG_ADCCONTROL5 and REG_DACCONTROL2
#define FsMode_SingleSpeed  (0 << 5)
#define FsMode_DoubleSpeed  (1 << 5)
// base frequency is 11.2896 MHz, 12.288 MHz, 16.9344 MHz, 18.432 MHz
#define FsRatio_MCLK_128    0b00000
#define FsRatio_MCLK_192    0b00001
#define FsRatio_MCLK_256    0b00010
#define FsRatio_MCLK_384    0b00011
#define FsRatio_MCLK_512    0b00100
#define FsRatio_MCLK_576    0b00101
#define FsRatio_MCLK_768    0b00110
#define FsRatio_MCLK_1024   0b00111
#define FsRatio_MCLK_1152   0b01000
#define FsRatio_MCLK_1408   0b01001
#define FsRatio_MCLK_1536   0b01010
#define FsRatio_MCLK_2112   0b01011
#define FsRatio_MCLK_2304   0b01100
// "USB" mode - base frequency is 12 MHz
#define FsRatio_MCLK_125    0b10000
#define FsRatio_MCLK_136    0b10001
#define FsRatio_MCLK_250    0b10010
#define FsRatio_MCLK_272    0b10011
#define FsRatio_MCLK_375    0b10100
#define FsRatio_MCLK_500    0b10101
#define FsRatio_MCLK_544    0b10110
#define FsRatio_MCLK_750    0b10111
#define FsRatio_MCLK_1000   0b11000
#define FsRatio_MCLK_1088   0b11001
#define FsRatio_MCLK_1496   0b11010
#define FsRatio_MCLK_1500   0b11011


class AudioCodec_ES8388 : public AudioCodec {
    ClockSignal* clock_signal;
    TwoWire *i2c;
    int freq;
    uint8_t clock_div;

    inline bool write_reg(uint8_t reg, uint8_t value) {
        i2c->beginTransmission((uint8_t)ES8388_ADDR);
        return i2c->write(reg) && i2c->write(value) && i2c->endTransmission();
    }

    inline int clamp(int x, int min, int max) {
        if (x < min) return min;
        if (x > max) return max;
        return x;
    }

    /**
     * Convert volume level for DAC and ADC registers.
     * Volume is in dB, from -96 to 0 (0 is max volume) in 0.5 dB steps.
     */
    inline int volume_dac_adc(int volume, bool plusHalf = false) {
        return (-clamp(volume, -96, 0) << 1) + (plusHalf ? 1 : 0);
    }

public:
    AudioCodec_ES8388() : i2c(&Wire) {
        if (REC_SAMPLE_RATE == 8000) { freq = 12288000; clock_div = FsRatio_MCLK_1536; } // or 12000000, FsRatio_MCLK_1500; 18432000, FsRatio_MCLK_72304
        else if (REC_SAMPLE_RATE == 12000) { freq = 12288000; clock_div = FsRatio_MCLK_1024; } // or 12000000, FsRatio_MCLK_1000; 18432000, FsRatio_MCLK_1536
        else if (REC_SAMPLE_RATE == 16000) { freq = 12288000; clock_div = FsRatio_MCLK_768; } // or 12000000, FsRatio_MCLK_750; 18432000, FsRatio_MCLK_1152
        else if (REC_SAMPLE_RATE == 24000) { freq = 12288000; clock_div = FsRatio_MCLK_512; } // or 12000000, FsRatio_MCLK_500; 18432000, FsRatio_MCLK_768
        else if (REC_SAMPLE_RATE == 32000) { freq = 12288000; clock_div = FsRatio_MCLK_384; } // or 12000000, FsRatio_MCLK_375; 18432000, FsRatio_MCLK_576
        else if (REC_SAMPLE_RATE == 48000) { freq = 12288000; clock_div = FsRatio_MCLK_256; } // or 12000000, FsRatio_MCLK_250; 18432000, FsRatio_MCLK_384
        else if (REC_SAMPLE_RATE == 96000) { freq = 12288000; clock_div = FsRatio_MCLK_128; } // or 12000000, FsRatio_MCLK_125; 18432000, FsRatio_MCLK_192
        else if (REC_SAMPLE_RATE == 8018) { freq = 11289600; clock_div = FsRatio_MCLK_1408; } // or 16934400, FsRatio_MCLK_2112
        else if (REC_SAMPLE_RATE == 11025) { freq = 11289600; clock_div = FsRatio_MCLK_1024; } // or 16934400, FsRatio_MCLK_1536
        else if (REC_SAMPLE_RATE == 22050) { freq = 11289600; clock_div = FsRatio_MCLK_512; } // or 16934400, FsRatio_MCLK_768
        else if (REC_SAMPLE_RATE == 44100) { freq = 11289600; clock_div = FsRatio_MCLK_256; } // or 16934400, FsRatio_MCLK_384
        else if (REC_SAMPLE_RATE == 88200) { freq = 11289600; clock_div = FsRatio_MCLK_128; } // or 16934400, FsRatio_MCLK_192
        else { freq = 12288000; clock_div = FsRatio_MCLK_256; } // default: 48 kHz
    }
    ~AudioCodec_ES8388() { delete clock_signal; }
    bool setup() override {
        clock_signal = new ClockSignal(freq, CLOCK_PIN);
        if (clock_signal->resume() != ESP_OK) {
            delete clock_signal;
            clock_signal = nullptr;
            return false;
        }

        return
            write_reg(REG_DACCONTROL3, MUTE) &&         // mute DAC during setup

            // Chip Control and Power Management
            write_reg(REG_CONTROL2, 0b01010000) &&      // LPVrefBuf low power, everything else normal
            write_reg(REG_CHIPPOWER, 0) &&              // normal/power up all
            write_reg(REG_ADCPOWER, 0b11111111) &&      // power down or low power all of ADC while setting up
            write_reg(REG_DACPOWER, LDAC_POWER_DOWN | RDAC_POWER_DOWN) &&   // power down DACs and disable LOUT/ROUT/1/2 while setting up
            //write_reg(REG_CHIPLOPOW1, 0) &&             // all normal (default)
            //write_reg(REG_CHIPLOPOW2, 0) &&             // all normal (default)
            //write_reg(REG_ANAVOLMANAG, 0b01111100) &&   // normal (default)
            write_reg(REG_CONTROL1, 0b00010000 | C1_VMIDDiv_500k) &&        // ADCMCLIK is master clock source, ADC Fs == DAC Fs, internal power up/down disabled, disable ref

            // Disable the internal DLL to improve 8K sample rate (undocumented)
            //write_reg(0x35, 0xA0) && write_reg(0x37, 0xD0) && write_reg(0x39, 0xD0) &&

            write_reg(REG_MASTERMODE, MM_I2S_SLAVE) &&

            // DAC
            write_reg(REG_DACCONTROL1, bitsToWL(REC_BITS_PER_SAMPLE) | FMT_I2S) &&
            write_reg(REG_DACCONTROL2, FsMode_SingleSpeed | clock_div) && // DAC clock
            //write_reg(REG_DACCONTROL6, 0b00001000) &&  // no deemphasis, no phase inv, click free power up/down (defaults)
            //write_reg(REG_DACCONTROL7, 0b00000000) &&  // normal output, stereo, SE strength 0, Vpp scale 3.5V (defaults)
            write_reg(REG_DACCONTROL16, LMIXSEL_LIN1 | RMIXSEL_RIN1) && // mixer select
            write_reg(REG_DACCONTROL17, DAC2MIX_EN | IN2MIX_DIS | IN_GAIN_DB(0)) &&   // left mixer
            write_reg(REG_DACCONTROL20, DAC2MIX_EN | IN2MIX_DIS | IN_GAIN_DB(0)) &&   // right mixer
            write_reg(REG_DACCONTROL21, 0b10000000) &&      // ADC and DAC use the same LRCK clock (using ADC LRCK), offset disable, normal mode
            //write_reg(REG_DACCONTROL22, 0) &&               // DC offset 0 (default)
            //write_reg(REG_DACCONTROL23, 0b00000000) &&      // 1.5k VREF to analog output resistance (default)
            #if LINE_OUTPUT != 2
            write_reg(REG_DACCONTROL24, 0x1E) && write_reg(REG_DACCONTROL25, 0x1E) &&   // LOUT1/ROUT1 volume to 0dB
            #else
            write_reg(REG_DACCONTROL24, 0) && write_reg(REG_DACCONTROL25, 0) &&         // LOUT1/ROUT1 volume to -45db
            #endif
            #if LINE_OUTPUT != 1
            write_reg(REG_DACCONTROL26, 0x1E) && write_reg(REG_DACCONTROL27, 0x1E) &&   // LOUT2/ROUT2 volume to 0dB
            #else
            write_reg(REG_DACCONTROL26, 0) && write_reg(REG_DACCONTROL27, 0) &&         // LOUT2/ROUT2 volume to -45dB
            #endif
            write_reg(REG_DACCONTROL4, 0) && write_reg(REG_DACCONTROL5, 0) &&           // DAC volumes to 0 dB


            // ADC
            write_reg(REG_ADCCONTROL1, RECORDING_VOLUME) &&  // MIC Left and Right channel PGA gain
            #if LINE_INPUT == 1
            write_reg(REG_ADCCONTROL2, (INPUT1 << INPUT_SEL_L_SFT) | (INPUT1 << INPUT_SEL_R_SFT)) &&
            write_reg(REG_ADCCONTROL3, 0b10) &&  // 0b10 is undocumeted but default
            #elif LINE_INPUT == 2
            write_reg(REG_ADCCONTROL2, (INPUT2 << INPUT_SEL_L_SFT) | (INPUT2 << INPUT_SEL_R_SFT)) &&
            write_reg(REG_ADCCONTROL3, 0b10) &&  // 0b10 is undocumeted but default
            #elif LINE_INPUT == 3
            write_reg(REG_ADCCONTROL2, (INPUT_DIFF << INPUT_SEL_L_SFT) | (INPUT_DIFF << INPUT_SEL_R_SFT) | DSSEL_MATCH | DSR_LINPUT1_RINPUT1) &&
            write_reg(REG_ADCCONTROL3, DS_LINPUT1_RINPUT1 | 0b10) &&  // 0b10 is undocumeted but default
            #else // if LINE_INPUT == 4
            write_reg(REG_ADCCONTROL2, (INPUT_DIFF << INPUT_SEL_L_SFT) | (INPUT_DIFF << INPUT_SEL_R_SFT) | DSSEL_MATCH | DSR_LINPUT2_RINPUT2) &&
            write_reg(REG_ADCCONTROL3, DS_LINPUT2_RINPUT2 | 0b10) &&  // 0b10 is undocumeted but default
            #endif
            write_reg(REG_ADCCONTROL4, bitsToWL(PLAY_BITS_PER_SAMPLE) | FMT_I2S) &&
            write_reg(REG_ADCCONTROL5, FsMode_SingleSpeed | clock_div) &&
            //write_reg(REG_ADCCONTROL6, 0b00110000) &&  // no polarity inversion, enable high pass filters (defaults)
            //write_reg(REG_ADCCONTROL7, 0b00100000) &&  // 4 LRCK soft ramp, no gain control, no mute (defaults)
            write_reg(REG_ADCCONTROL8, 0) && write_reg(REG_ADCCONTROL9, 0) &&  // ADC L/R Volume=0db
            //write_reg(REG_ADCCONTROL10, ALC_OFF | MAXGAIN_35_5dB | MINGAIN_n12dB) &&  // defaults  // TODO: this may be good to enable but limit the min and max
            //write_reg(REG_ADCCONTROL11, ALC_TARGET_n1_5dB | ALC_HOLD_0ms) &&          // defaults
            //write_reg(REG_ADCCONTROL12, ...) &&   // ALC decay and attack
            //write_reg(REG_ADCCONTROL13, ALC_MODE_NORMAL | ALC_ZERO_CROSS_DIS | ALC_ZC_TIME_OUT_DIS | ALC_WIN_SIZE(96)) &&  // defaults
            //write_reg(REG_ADCCONTROL14, NOISE_GATE_DIS) &&  // default

            // Power up the ADC and DAC
            #if LINE_OUTPUT == 1
            write_reg(REG_DACPOWER, LOUT1_EN | ROUT1_EN) &&
            #elif LINE_OUTPUT == 2
            write_reg(REG_DACPOWER, LOUT2_EN | ROUT2_EN) &&
            #else
            write_reg(REG_DACPOWER, LOUT1_EN | LOUT2_EN | ROUT1_EN | ROUT2_EN) &&
            #endif
            write_reg(REG_ADCPOWER, 0b00000001) &&      // power on ADC (inc mic bias) but int1 stays in low power mode
            write_reg(REG_DACCONTROL3, UNMUTE | NO_SOFT_RAMP | NO_LR_GAINS_MATCH);


            // TODO: reset internal state machine?
            // write_reg(REG_CHIPPOWER, 0xF0) &&  // reset filters / state machines
            // // write_reg(REG_CONTROL1, 0x14 | C1_VMIDDiv_500k) &&  // ADC Fs == DAC Fs, internal power up/down enabled
            // // write_reg(REG_CONTROL2, 0x50) &&  // LPVrefBuf low power, everything else normal
            // write_reg(REG_CHIPPOWER, 0x00) &&  // normal/power up all

        // TODO: have a function that returns the volume config (min, max, granularity, etc)
        // codec_dac_volume_config_t vol_cfg = REG_DAC_VOL_CFG_DEFAULT();
        // dac_vol_handle = audio_codec_volume_init(&vol_cfg);
    }

    void setVolume(int8_t volume) override {
        int8_t vol = AudioCodec_ES8388::volume_dac_adc(volume, false);
        write_reg(REG_DACCONTROL5, vol);
        write_reg(REG_DACCONTROL4, vol);
    }
};

AudioCodec* create_audio_codec() { return new AudioCodec_ES8388(); }

#endif
