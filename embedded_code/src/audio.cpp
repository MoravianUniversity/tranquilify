#include "audio.h"
#include "data.h"

#include <Arduino.h> // for Serial and WM8960 library

// WM8960 audio codec board
#include <SparkFun_WM8960_Arduino_Library.h>
WM8960 audio_codec;

// For the background task
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// I2S driver and event queue
#include <driver/i2s.h>
#include <freertos/queue.h>

// ESP32 Thing Plus C I2S pins/port
#define I2S_WS       33 // DACLRC/ADCLRC/LRC/"word select"/"left-right-channel", toggles for left or right channel data
#define I2S_ADC_DATA 27 // ADC_DATA/SD/"serial data in", carries the I2S audio data from codec's ADC to ESP32 I2S bus
#define I2S_DAC_DATA 14 // DAC_DATA/SDO/"serial data out", carries the I2S audio data from ESP32 to codec DAC
#define I2S_BCLK     32 // BCLK/SCK/"bit clock", this is the clock for I2S audio, can be controlled via controller or peripheral
#define I2S_PORT I2S_NUM_0

// Audio Recording Buffers
#define DMA_BUFFER_SAMPLE_LEN 1024 // from ~64 to 1024 - lower reduces latency but increases overhead
#define DMA_BUFFER_BYTE_LEN (DMA_BUFFER_SAMPLE_LEN * BYTES_PER_SAMPLE * CHANNELS) // IMPORTANT: this cannot be > 4096
#define WAV_BUFFER_LEN (100*BYTES_PER_SAMPLE*CHANNELS*SAMPLE_RATE/1000)  // 100 ms of audio buffered before writing to SD card
uint8_t audioBuffer[WAV_BUFFER_LEN + DMA_BUFFER_BYTE_LEN]; // this will store the data over a larger period and write it all at once
uint16_t audioBufferOffset = 0; // the current offset in the buffer


#define DIRECT_OUTPUT // comment this out to suppress automatic output to headphones from microphone


QueueHandle_t queue = NULL;

/**
 * Read I2S data into data buffer then write it to the SD card every so often.
 */
void readAudioData() { 
    // TODO: several questions about i2s_read:
    //    giving a smaller buffer size (e.g. 64) reduces latency but increases overhead
    //    giving a small timeout (instead of ininite) could also reduce latency
    
    if (queue != NULL) {
        // TODO: this only seems to work for a little while before it stops working (a total of like 6-8 messages displayed), don't know why
        i2s_event_t evt;
        while (xQueueReceive(queue, &evt, 0)) {
            if (evt.type == I2S_EVENT_DMA_ERROR) {
                Serial.println("!! I2S DMA error");
            } else if (evt.type == I2S_EVENT_RX_Q_OVF) {
                Serial.println("!! I2S Overflow receive buffer");
            }
        }
    }

    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_PORT, &audioBuffer[audioBufferOffset], DMA_BUFFER_BYTE_LEN, &bytesRead, portMAX_DELAY);
    if (result != ESP_OK) { Serial.println("!! I2S read error"); return; }
    audioBufferOffset += bytesRead;
    if (audioBufferOffset >= WAV_BUFFER_LEN) {
        // Write the buffer to the SD card every so often
        recordWAVData(audioBuffer, audioBufferOffset);
        audioBufferOffset = 0;
    }
}


/**
 * Permanent task that continually reads audio data from the I2S bus and write it to the SD card
 * while also writing audio data back to the I2S bus for playback.
 */
void audioRecordingTask(void *pvParameters) {
    while (true) {
        readAudioData();
    }
    vTaskDelete(NULL);
}


/**
 * Set up the audio codec for recording and playing back audio.
 * See the AUDIO_OUTPUT define for playback options.
 */
bool audio_codec_setup() {
    return // this is a chain of boolean ANDs, so if any fail, the whole thing fails

        // General setup needed
        audio_codec.enableVREF() && 
        audio_codec.enableVMID() &&

        // Enable mic bias voltage
        audio_codec.enableMicBias() &&
        audio_codec.setMicBiasVoltage(WM8960_MIC_BIAS_VOLTAGE_0_9_AVDD) &&

        // Setup signal flow to the ADC
        audio_codec.enableLMIC() &&
        audio_codec.enableRMIC() &&

        // Connect from INPUT1 to "n" (aka inverting) inputs of PGAs.
        audio_codec.connectLMN1() &&
        audio_codec.connectRMN1() &&

        // Disable mutes on PGA inputs (aka INTPUT1)
        audio_codec.disableLINMUTE() &&
        audio_codec.disableRINMUTE() &&

        // Set pga volumes
        audio_codec.setLINVOLDB(24.00) && // Valid options are -17.25dB to +30.00dB
        audio_codec.setRINVOLDB(24.00) && // Valid options are -17.25dB to +30.00dB

        // Set input boosts to get inputs 1 to the boost mixers
        audio_codec.setLMICBOOST(WM8960_MIC_BOOST_GAIN_0DB) &&
        audio_codec.setRMICBOOST(WM8960_MIC_BOOST_GAIN_0DB) &&

        // For MIC+ signal of differential mic signal
        // WM8960_PGAL_VMID for single ended input
        // WM8960_PGAL_LINPUT2/WM8960_PGAL_RINPUT2 for pseudo-differential input
        audio_codec.pgaLeftNonInvSignalSelect(WM8960_PGAL_LINPUT2) &&
        audio_codec.pgaRightNonInvSignalSelect(WM8960_PGAR_RINPUT2) &&

        // Connect from MIC inputs (aka pga output) to boost mixers
        audio_codec.connectLMIC2B() &&
        audio_codec.connectRMIC2B() &&

        // Enable boost mixers
        audio_codec.enableAINL() &&
        audio_codec.enableAINR() &&

        //audio_codec.enablePgaZeroCross() && // TODO: what does this do?

#ifdef DIRECT_OUTPUT
        // Connect LB2LO (booster to output mixer (analog bypass))
        audio_codec.enableLB2LO() &&
        audio_codec.enableRB2RO() &&

        // Connect from DAC outputs to output mixer
        audio_codec.enableLD2LO() &&
        audio_codec.enableRD2RO() &&

        // Set gainstage between booster mixer and output mixer
        audio_codec.setLB2LOVOL(WM8960_OUTPUT_MIXER_GAIN_0DB) &&
        audio_codec.setRB2ROVOL(WM8960_OUTPUT_MIXER_GAIN_0DB) &&

        // Enable output mixers
        audio_codec.enableLOMIX() &&
        audio_codec.enableROMIX() &&

        // enable bypass connection from Left INPUT3 to Left output mixer, note, the
        // default gain on this input (LI2LOVOL) is -15dB
        //audio_codec.enableLI2LO() &&
        //audio_codec.enableRI2RO() &&

        // sets volume control between "input" to "output mixer"
        //audio_codec.setLI2LOVOL(WM8960_OUTPUT_MIXER_GAIN_0DB) &&
        //audio_codec.setRI2ROVOL(WM8960_OUTPUT_MIXER_GAIN_0DB) &&

        audio_codec.enableOUT3MIX() && // Provides VMID as buffer for headphone/speaker ground

        audio_codec.enableHeadphones() &&
        audio_codec.setHeadphoneVolumeDB(0.00) &&

        //audio_codec.enableSpeakers() &&
        //audio_codec.enableSpeakerZeroCross() &&
        //audio_codec.setSpeakerVolume(120) &&
#else
        // Disconnect LB2LO (booster to output mixer (analog bypass))
        audio_codec.disableLB2LO() &&
        audio_codec.disableRB2RO() &&

        // Disconnect from DAC outputs to output mixer
        audio_codec.disableLD2LO() && // TODO: also above?
        audio_codec.disableRD2RO() &&
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
        audio_codec.setWL(WM8960_WL_16BIT) &&

        audio_codec.enablePeripheralMode() &&
        //audio_codec.enableMasterMode() &&

        // Set LR clock to be the same for ADC & DAC internally
        // Note: should not be changed while ADC is enabled
        audio_codec.setALRCGPIO() &&

        // enable ADCs and DACs
        audio_codec.enableAdcLeft() &&
        audio_codec.enableAdcRight() &&
        //audio_codec.enableDacLeft() &&
        //audio_codec.enableDacRight() &&
        audio_codec.disableDacLeft() &&
        audio_codec.disableDacRight() &&
        audio_codec.disableDacMute() && // default is "soft mute" on, so we must disable mute to make channels active

        //audio_codec.enableLoopBack(); // Loopback sends ADC data directly into DAC
        audio_codec.disableLoopBack();
}


/**
 * Set up the I2S driver. This makes the ESP32 the master and operate in both RX and TX modes.
 */
bool i2s_install() {
    // 16-bit stereo 44.1 kHz
    const i2s_driver_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), // | I2S_MODE_TX would allow for output
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // TODO: does a higher level make sense? (or 0 for default)
        .dma_buf_count = 8, // TODO: no idea what this should be
        .dma_buf_len = DMA_BUFFER_SAMPLE_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,  // for cleaner outputs when there are delays
        .fixed_mclk = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,  // TODO: 512?
        .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
    };
    if (i2s_driver_install(I2S_PORT, &i2s_config, 4, &queue) != ESP_OK) { Serial.println("!! i2s_driver_install()"); return false; }
    return true;

    //i2s_set_clk(I2S_PORT, SAMPLE_RATE, BITS_PER_SAMPLE, I2S_CHANNEL_STEREO);
    //i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
}


/**
 * Set up the I2S pins for the ESP32 Thing Plus C.
 */
bool i2s_setpin() {
    const i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_DAC_DATA,
        .data_in_num = I2S_ADC_DATA
    };
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) { Serial.println("!! i2s_set_pin()"); return false; }
    return true;
}


/**
 * Set up the audio codec and I2S for recording audio.
 * Before this is called, the Serial and Wire interfaces must be set up.
 * If there is a problem with the audio setup, the program will freeze.
 */
void setupAudio() {
    if (!audio_codec.begin()) { Serial.println("!! WM8960 audio codec did not respond. Please check wiring."); while (1); }
    if (!audio_codec_setup()) { Serial.println("!! WM8960 audio codec setup failed."); while (1); }
    vTaskDelay(10 / portTICK_PERIOD_MS); // Give time for codec to settle after setup
    if (!i2s_install() || !i2s_setpin()) { while (1); }

    if (xTaskCreate(audioRecordingTask, "AudioRecording", 4096, NULL, 1, NULL) != pdPASS) {
        Serial.println("!! Failed to create audio recording task");
        while (1);
    }
}
