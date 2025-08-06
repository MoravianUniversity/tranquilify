/**
 * Support for WM8960 audio codec breakout board from SparkFun:
 * https://www.sparkfun.com/sparkfun-audio-codec-breakout-wm8960-with-headers-qwiic.html
 * 
 * The WM8960 audio codec comes with an Arduino library that is used here.
 * 
 * The breakout board comes with a built-in 24 MHz crystal oscillator which is
 * used as the master clock. The code below only supports 44.1 kHz sample rate
 * but with some work could support other sample rates as well.
 * 
 * Note: The WM8960 chip has been deprecated by the manufacturer, so this code
 * may not be maintained in the future.
 */

#include "audio_codec.hpp"
#include "audio.h"

#if AUDIO_CODEC == AUDIO_CODEC_WM8960

#include <SparkFun_WM8960_Arduino_Library.h>

// Configuration
#define AUDIO_OUTPUT 2 // set to 0 for no audio output (just recording), 1 for loopback, 2 for manual output
#define RECORDING_VOLUME 55 // 24db; value from 0-63, maps to -17.25dB to +30.00dB with 0.75dB steps


static_assert(REC_SAMPLE_RATE == 44100, "WM8960: Sample rate must be 44.1kHz (can be changed in code)");
static_assert(REC_BITS_PER_SAMPLE == 16 || REC_BITS_PER_SAMPLE == 20 || REC_BITS_PER_SAMPLE == 24 || REC_BITS_PER_SAMPLE == 32, "WM8960: Bits per sample must be 16, 20, 24, or 32");
static_assert(REC_CHANNELS == 1 || REC_CHANNELS == 2, "WM8960: Channels must be 1 (mono) or 2 (stereo), with work 3 or 4 channels could be supported as well");

static_assert(PLAY_SAMPLE_RATE == 44100, "WM8960: Sample rate must be 44.1kHz (can be changed in code)");
static_assert(PLAY_BITS_PER_SAMPLE == 16 || PLAY_BITS_PER_SAMPLE == 20 || PLAY_BITS_PER_SAMPLE == 24 || PLAY_BITS_PER_SAMPLE == 32, "WM8960: Bits per sample must be 16, 20, 24, or 32");
static_assert(PLAY_CHANNELS == 1 || PLAY_CHANNELS == 2, "WM8960: Channels must be 1 (mono) or 2 (stereo), with work 3 or 4 channels could be supported as well");

static_assert(PLAY_BITS_PER_SAMPLE == REC_BITS_PER_SAMPLE, "WM8960: Playback bits per sample must match recording bits per sample");


class AudioCodec_WM8960 : public AudioCodec {
    WM8960 audio_codec;

public:
    bool setup() override {
        return // this is a chain of boolean ANDs, so if any fail, the whole thing fails
            audio_codec.begin() &&  // Initialize the codec

            // General setup needed
            audio_codec.enableVREF() && 
            audio_codec.enableVMID() &&

            // Enable mic bias voltage
            audio_codec.enableMicBias() &&
            audio_codec.setMicBiasVoltage(WM8960_MIC_BIAS_VOLTAGE_0_9_AVDD) &&

            // Setup signal flow to the ADC
            audio_codec.enableLMIC() &&
            #if REC_CHANNELS == 2
            audio_codec.enableRMIC() &&
            #endif

            // Connect from INPUT1 to "n" (aka inverting) inputs of PGAs (these are default to connected anyway)
            audio_codec.connectLMN1() &&
            #if REC_CHANNELS == 2
            audio_codec.connectRMN1() &&
            #endif

            // Disable mutes on PGA inputs (aka INTPUT1)
            audio_codec.disableLINMUTE() &&
            #if REC_CHANNELS == 2
            audio_codec.disableRINMUTE() &&
            #endif

            // Set PGA volumes (value from 0-63, maps to -17.25dB to +30.00dB with 0.75dB steps)
            audio_codec.setLINVOL(RECORDING_VOLUME) &&
            #if REC_CHANNELS == 2
            audio_codec.setRINVOL(RECORDING_VOLUME) &&
            #endif

            // Set input boosts to get inputs 1 to the boost mixers
            audio_codec.setLMICBOOST(WM8960_MIC_BOOST_GAIN_0DB) &&
            #if REC_CHANNELS == 2
            audio_codec.setRMICBOOST(WM8960_MIC_BOOST_GAIN_0DB) &&
            #endif

            // For MIC+ signal of differential mic signal
            // WM8960_PGAL_VMID for single ended input
            // WM8960_PGAL_LINPUT2/WM8960_PGAL_RINPUT2 for pseudo-differential input
            audio_codec.pgaLeftNonInvSignalSelect(WM8960_PGAL_LINPUT2) &&
            #if REC_CHANNELS == 2
            audio_codec.pgaRightNonInvSignalSelect(WM8960_PGAR_RINPUT2) &&
            #endif

            // Connect from MIC inputs (aka pga output) to boost mixers
            audio_codec.connectLMIC2B() &&
            #if REC_CHANNELS == 2
            audio_codec.connectRMIC2B() &&
            #endif

            // Enable boost mixers
            audio_codec.enableAINL() &&
            #if REC_CHANNELS == 2
            audio_codec.enableAINR() &&
            #endif

            //audio_codec.enablePgaZeroCross() && // TODO: what does this do?

#if AUDIO_OUTPUT == 0  // no audio output
            // Disconnect input boost mixer to output mixer (analog bypass) // this is default
            // audio_codec.disableLB2LO() &&
            // audio_codec.disableRB2RO() &&

            // Disconnect DAC outputs from output mixer // this is default
            // audio_codec.disableLD2LO() &&
            // audio_codec.disableRD2RO() &&

#else  // have audio output
    #if AUDIO_OUTPUT == 1  // loopback
            // Connect input boost mixer to output mixer (analog bypass)
            audio_codec.enableLB2LO() &&
        #if REC_CHANNELS == 2 && PLAY_CHANNELS == 2
            audio_codec.enableRB2RO() &&
        #endif

            // Set gainstage between booster mixer and output mixer
            audio_codec.setLB2LOVOL(WM8960_OUTPUT_MIXER_GAIN_0DB) &&
        #if REC_CHANNELS == 2 && PLAY_CHANNELS == 2
            audio_codec.setRB2ROVOL(WM8960_OUTPUT_MIXER_GAIN_0DB) &&
        #endif
    #else  // manual output
            // Connect from DAC outputs to output mixer
            audio_codec.enableLD2LO() &&
        #if PLAY_CHANNELS == 2
            audio_codec.enableRD2RO() &&
        #endif
    #endif

            // Enable output mixers
            audio_codec.enableLOMIX() &&
        #if PLAY_CHANNELS == 2
            audio_codec.enableROMIX() &&
        #endif

            // Provides VMID as buffer for headphone/speaker ground
            audio_codec.enableOUT3MIX() &&

            // Enable headphone/speaker output
            audio_codec.enableHeadphones() &&
            //audio_codec.setHeadphoneVolumeDB(0.00) &&  // happens in the volume monitor task
#endif

            // CLOCK STUFF, These settings will get you 44.1KHz sample rate, and class-d freq at 705.6kHz
            audio_codec.enablePLL() &&  // needed for class-d amp clock
            audio_codec.setPLLPRESCALE(WM8960_PLLPRESCALE_DIV_2) &&
            audio_codec.setSMD(WM8960_PLL_MODE_FRACTIONAL) &&
            audio_codec.setCLKSEL(WM8960_CLKSEL_PLL) &&
            audio_codec.setSYSCLKDIV(WM8960_SYSCLK_DIV_BY_2) &&
            audio_codec.setBCLKDIV(4) &&
            audio_codec.setDCLKDIV(WM8960_DCLKDIV_16) &&
            audio_codec.setPLLN(WM8960_DCLKDIV_16) &&
            audio_codec.setPLLK(0x86, 0xC2, 0x26) &&  // PLLK=86C226h
            //audio_codec.set_ADCDIV(0) && // default is 000 (what we need for 44.1KHz)
            //audio_codec.set_DACDIV(0) && // default is 000 (what we need for 44.1KHz)
            #if REC_BITS_PER_SAMPLE == 16
            audio_codec.setWL(WM8960_WL_16BIT) &&
            #elif REC_BITS_PER_SAMPLE == 20
            audio_codec.setWL(WM8960_WL_20BIT) &&
            #elif REC_BITS_PER_SAMPLE == 24
            audio_codec.setWL(WM8960_WL_24BIT) &&
            #elif REC_BITS_PER_SAMPLE == 32
            audio_codec.setWL(WM8960_WL_32BIT) &&
            #endif
            audio_codec.enablePeripheralMode() &&

            // Set LR clock to be the same for ADC & DAC internally
            // Note: should not be changed while ADC is enabled
            audio_codec.setALRCGPIO() &&

            // enable ADCs (allows for recording)
            audio_codec.enableAdcLeft() &&
            #if REC_CHANNELS == 2
            audio_codec.enableAdcRight() &&
            #endif

            // enable DACs (allows for manual output)
#if AUDIO_OUTPUT == 2  // manual output (using DAC)
            audio_codec.enableDacLeft() &&
            #if PLAY_CHANNELS == 2
            audio_codec.enableDacRight() &&
            #endif
            audio_codec.disableDacMute() && // default is "soft mute" on, so we must disable mute to make channels active
#else
            audio_codec.disableDacLeft() &&
            audio_codec.disableDacRight() &&
            audio_codec.enableDacMute() && 
#endif

            //audio_codec.enableLoopBack(); // Loopback sends ADC data directly into DAC
            audio_codec.disableLoopBack();
    }

    /**
     * The volume must be in the range -48 to 79 where:
     *   <0 is muted (should not be less than -48)
     *   0 to 79 is -73dB to +6dB in 1dB steps
     */
    void setVolume(int8_t volume) override {
        audio_codec.setHeadphoneVolume(volume + 48);
    }
};

AudioCodec* create_audio_codec() { return new AudioCodec_WM8960(); }

#endif
