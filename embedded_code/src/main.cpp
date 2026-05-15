#include <Arduino.h>  // Serial
#include <Wire.h> // I2C communication

#include "config.h"
#include "settings.h"
#include "audio.h"
#include "volume.h"
#include "sd.h"
#include "button.h"

#include "duet.h" // DUET algorithm

#include <driver/gpio.h>

#include <esp32/clk.h>
#define CPU_FREQ 240000.0 // ESP32 CPU frequency in 1/ms

#include "fast_math.hpp"
#include "bootloader_random.h"

#include "clock_signal.hpp"

#include "test.h"
char buffer[256];

//static_assert(n_samples == DUET_N_SAMPLES, "n_samples must be equal to DUET_N_SAMPLES");
//float decimated[2 * DUET_N_SAMPLES / 2];

void dump(Print& p, const char* name, const float* data, int n, const char* shape) {
    p.printf("%s_dump = np.array([", name);
    for (int i = 0; i < n; i++) {
        p.printf("%.9g, ", data[i]);
    }
    p.printf("], np.float32).reshape(%s)\n", shape);
}

void dump(Print& p, const char* name, const std::vector<float>& data, const char* shape) {
    p.printf("%s_dump = np.array([", name);
    for (int i = 0; i < data.size(); i++) {
        p.printf("%.9g, ", data[i]);
    }
    p.printf("], np.float32).reshape(%s)\n", shape);
}

void dump(Print& p, const char* name, const uint8_t* data, int n, const char* shape) {
    p.printf("%s_dump = np.array([", name);
    for (int i = 0; i < n; i++) {
        p.printf("%u, ", data[i]);
    }
    p.printf("], np.uint8).reshape(%s)\n", shape);
}

void dump(const char* name, const float* data, int n, const char* shape) { dump(Serial, name, data, n, shape); }
void dump(const char* name, const std::vector<float>& data, const char* shape) { dump(Serial, name, data, shape); }
void dump(const char* name, const uint8_t* data, int n, const char* shape) { dump(Serial, name, data, n, shape); }

SemaphoreHandle_t sd_semaphore = NULL;
char dump_filename[128] = "/duet/dump.py";

void prepare_for_sd() {
    if (!setupSD()) { while (true); }
    sd_semaphore = xSemaphoreCreateBinary();
    submitSDTask([](SdFs* sd, void* params) {
        if (sd == nullptr) { printf("SD Task: sd is null\n"); return false; }
        if (!sd->exists("/duet")) { sd->mkdir("/duet"); }
        for (int i = 0; i < 10000; i++) {
            snprintf(dump_filename, sizeof(dump_filename), "/duet/dump_%05d.py", i);
            if (!sd->exists(dump_filename)) break;
        }
        printf("Dump filename: %s\n", dump_filename);
        xSemaphoreGive(sd_semaphore);
        return true;
    }, nullptr);
    xSemaphoreTake(sd_semaphore, portMAX_DELAY);
}

typedef struct {
    const char* name;
    const void* data;
    int n;
    const char* shape;
} SDDumpParams;
void dump_to_sd(const char* name, const float* data, int n, const char* shape) {
    SDDumpParams params = { name, data, n, shape };
    submitSDTask([](SdFs* sd, void* params) {
        SDDumpParams* p = (SDDumpParams*)params;
        if (sd == nullptr) { printf("SD Task: sd is null\n"); return false; }
        FsFile file = sd->open(dump_filename, O_WRONLY | O_CREAT | O_APPEND);
        if (!file) { printf("Failed to open file for writing\n"); return false; }
        dump(file, p->name, (const float*)p->data, p->n, p->shape);
        file.close();
        xSemaphoreGive(sd_semaphore);
        return true;
    }, &params);
    xSemaphoreTake(sd_semaphore, portMAX_DELAY);
}
void dump_to_sd(const char* name, const std::vector<float>& data, const char* shape) {
    dump_to_sd(name, data.data(), data.size(), shape);
}
void dump_to_sd(const char* name, const std::vector<cfloat>& data, const char* shape) {
    dump_to_sd(name, (float*)data.data(), data.size()*2, shape);
}
void dump_to_sd(const char* name, const uint8_t* data, int n, const char* shape) {
    SDDumpParams params = { name, data, n, shape };
    submitSDTask([](SdFs* sd, void* params) {
        SDDumpParams* p = (SDDumpParams*)params;
        if (sd == nullptr) { printf("SD Task: sd is null\n"); return false; }
        FsFile file = sd->open(dump_filename, O_WRONLY | O_CREAT | O_APPEND);
        if (!file) { printf("Failed to open file for writing\n"); return false; }
        dump(file, p->name, (const uint8_t*)p->data, p->n, p->shape);
        file.close();
        xSemaphoreGive(sd_semaphore);
        return true;
    }, &params);
    xSemaphoreTake(sd_semaphore, portMAX_DELAY);
}


void print_mem_info() {
    printf("Free RAM: %d / %d / %d bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT), esp_get_free_heap_size(), ESP.getHeapSize());
    printf("Stack HWM: %d bytes\n\n", uxTaskGetStackHighWaterMark(NULL) * 4);
}


static inline void* malloc0(size_t nbytes) {
    void* ptr = malloc(nbytes);
    if (ptr) { memset(ptr, 0, nbytes); }
    return ptr;
}

template <typename T> void roll(  // 3.06
    T* data,            // in/out, shape of (shape, data_len)
    int shape,          // product of all dimensions except the last
    int data_len,       // number of elements in the last dimension
    int new_len,        // number of elements to have room for in the last dimension (not inc overwritten data)
    int overwrite_len   // number of elements to overwrite in the last dimension
) {
    for (int i = 0; i < shape; i++) {
        T* a = &data[i*data_len];
        memmove(a, &a[new_len], (data_len-new_len-overwrite_len)*sizeof(T));  // TODO: use dsps_memcpy?
    }
}

template <typename T> static void roll2(  // 3.44
    T* data,            // in/out, shape of (shape, data_len)
    int total_size,     // total size of the data
    int new_len         // number of elements to have room for in the last dimension (not inc overwritten data)
) {
    memmove(data, data+new_len, (total_size-new_len)*sizeof(T));  // TODO: use dsps_memcpy?
}

template <typename T> static void roll2(
    T* data,            // in/out, shape of (shape, data_len)
    int shape,          // product of all dimensions except the last
    int data_len,       // number of elements in the last dimension
    int new_len,        // number of elements to have room for in the last dimension (not inc overwritten data)
    int overwrite_len   // number of elements to overwrite in the last dimension
) {
    roll2(data, shape * data_len, new_len);
}

void setup() {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << DEBUG_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)DEBUG_LED_PIN, 0));

    #ifdef DEBUG
    //delay(2000);  // Give time for the serial monitor to open on the computer
    #endif
    printf("Tranquilify\n");

    bootloader_random_enable();
    srand(esp_random());
    bootloader_random_disable();

    print_config();
    esp_cpu_ccount_t start, end, diff, total = 0;

    prepare_for_sd();

    printf("Amount of audio: %f ms\n", n_samples * 1000.0 / DUET_SAMPLE_RATE);
    printf("Update size: %f ms\n", DUET_WINDOW_SIZE * 1000.0 / DUET_SAMPLE_RATE);

    start = esp_cpu_get_ccount();
    if (duet_init() != ESP_OK) { printf("DUET init failed\n"); return; }
    end = esp_cpu_get_ccount();
    printf("DUET init took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
    print_mem_info();

    printf("--------------------------------\n");

    float* audio_temp = (float*)malloc(2 * n_samples * sizeof(float));
    if (!audio_temp) { printf("Failed to allocate memory for temporary audio buffer\n"); return; }
    float* audio = (float*)malloc0(2 * DUET_N_SAMPLES * sizeof(float));
    if (!audio) { printf("Failed to allocate memory for audio buffer\n"); return; }
    cfloat* spectrogram = (cfloat*)malloc0(2 * DUET_N_TIME * DUET_N_FREQ * sizeof(cfloat));
    if (!spectrogram) { printf("Failed to allocate memory for spectrogram buffer\n"); return; }
    float* alpha = (float*)malloc0(DUET_N_TIME * DUET_N_FREQ * sizeof(float));
    if (!alpha) { printf("Failed to allocate memory for alpha buffer\n"); return; }
    float* delta = (float*)malloc0(DUET_N_TIME * DUET_N_FREQ * sizeof(float));
    if (!delta) { printf("Failed to allocate memory for delta buffer\n"); return; }
    float* weights = (float*)malloc0(DUET_N_TIME * DUET_N_FREQ * sizeof(float));
    if (!weights) { printf("Failed to allocate memory for weights buffer\n"); return; }
    std::vector<float> alpha_peaks; alpha_peaks.reserve(8);
    std::vector<float> delta_peaks; delta_peaks.reserve(8);
    std::vector<cfloat> demixed; demixed.reserve(8*DUET_N_TIME*DUET_N_FREQ);
    uint8_t* best = (uint8_t*)malloc0(DUET_N_FREQ * DUET_N_TIME * sizeof(uint8_t));
    if (!best) { printf("Failed to allocate memory for best buffer\n"); return; }


    for (int i = 0; i < n_chunks; i++) {
        printf("***** Processing chunk %d / %d *****\n", i+1, n_chunks);
        const int16_t* chunk = audio_data[i];

        start = esp_cpu_get_ccount();
        prep_data(chunk, n_samples, audio_temp);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET prep took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        //dump_to_sd("audio", audio, 2 * n_samples, "(2, -1)");
        //print_mem_info();

        start = esp_cpu_get_ccount();
        roll(audio, REC_CHANNELS, DUET_N_SAMPLES, DUET_WINDOW_SIZE_HALF, 0);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET roll took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

        start = esp_cpu_get_ccount();
        decimate(audio_temp, n_samples, audio, DUET_N_SAMPLES - DUET_WINDOW_SIZE_HALF);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET decimate took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        //print_mem_info();

        start = esp_cpu_get_ccount();
        roll(spectrogram, REC_CHANNELS*DUET_N_FREQ, DUET_N_TIME, 1, 1);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET roll took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

        start = esp_cpu_get_ccount();
        compute_spectrogram(audio, 2, spectrogram);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET spectrogram took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        //dump_to_sd("spectrogram", (float*)spectrogram, 2 * DUET_N_TIME * DUET_N_FREQ * 2, "(2, 128, -1, 2)");
        //print_mem_info();

        start = esp_cpu_get_ccount();
        roll(alpha, DUET_N_FREQ, DUET_N_TIME, 1, 1);
        roll(delta, DUET_N_FREQ, DUET_N_TIME, 1, 1);
        roll(weights, DUET_N_FREQ, DUET_N_TIME, 1, 1);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET roll took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

        start = esp_cpu_get_ccount();
        compute_atten_and_delay(spectrogram, 2, alpha, delta);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET compute atten and delay took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        // dump_to_sd("alpha", alpha, DUET_N_TIME * DUET_N_FREQ, "(128, -1)");
        // dump_to_sd("delta", delta, DUET_N_TIME * DUET_N_FREQ, "(128, -1)");
        // print_mem_info();

        start = esp_cpu_get_ccount();
        compute_weights(spectrogram, 2, weights);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET compute weights took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        // dump_to_sd("weights", weights, DUET_N_TIME * DUET_N_FREQ, "(128, -1)");
        // print_mem_info();

        start = esp_cpu_get_ccount();
        find_peaks(weights, alpha, delta, alpha_peaks, delta_peaks);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET find_peaks took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        // dump("alpha_peaks", alpha_peaks, itoa(alpha_peaks.size(), buffer, 10));
        // dump_to_sd("alpha_peaks", alpha_peaks, itoa(alpha_peaks.size(), buffer, 10));
        // dump("delta_peaks", delta_peaks, itoa(delta_peaks.size(), buffer, 10));
        // dump_to_sd("delta_peaks", delta_peaks, itoa(delta_peaks.size(), buffer, 10));
        // print_mem_info();

        if (alpha_peaks.empty()) { printf("No peaks found, skipping demix\n"); continue; } // No peaks found, skip demix

        start = esp_cpu_get_ccount();
        convert_sym_to_atn(alpha_peaks);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET convert_sym_to_atn alpha took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        // dump("alpha_peaks_sym", alpha_peaks, itoa(alpha_peaks.size(), buffer, 10));
        // dump_to_sd("alpha_peaks_sym", alpha_peaks, itoa(alpha_peaks.size(), buffer, 10));
        // print_mem_info();

        start = esp_cpu_get_ccount();
        full_demix(spectrogram, alpha_peaks, delta_peaks, demixed, best);
        end = esp_cpu_get_ccount();
        total += end - start;
        printf("DUET full_demix took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);
        // dump_to_sd("demixed", demixed, "(-1, 128, 13, 2)");
        // dump_to_sd("best", best, DUET_N_FREQ * DUET_N_TIME, "(128, 13)");
        
        print_mem_info();
    }

    printf("--------------------------------\n");
    printf("Total DUET time: %d cycles / %0.3f ms\n", total, total / CPU_FREQ);
    printf("--------------------------------\n");

    total = 0;

    // warmup
    // prep_data(audio_data, DUET_WINDOW_SIZE_HALF*2, audio);

    // start = esp_cpu_get_ccount();
    // prep_data(audio_data, DUET_WINDOW_SIZE_HALF*2, audio);
    // end = esp_cpu_get_ccount();
    // total += end - start;
    // printf("DUET prep took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

    // prep_data(audio_data, n_samples, audio);

    // start = esp_cpu_get_ccount();
    // decimate(audio, DUET_WINDOW_SIZE*2, decimated, 0);
    // end = esp_cpu_get_ccount();
    // total += end - start;
    // printf("DUET decimate update took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

    // start = esp_cpu_get_ccount();
    // compute_spectrogram(audio, 2, spectrogram);
    // end = esp_cpu_get_ccount();
    // total += end - start;
    // printf("DUET update spectrogram took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

    // start = esp_cpu_get_ccount();
    // compute_atten_and_delay(spectrogram, 2, alpha, delta);
    // end = esp_cpu_get_ccount();
    // total += end - start;
    // printf("DUET update atten and delay took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

    // start = esp_cpu_get_ccount();
    // compute_weights(spectrogram, 2, weights);
    // end = esp_cpu_get_ccount();
    // total += end - start;
    // printf("DUET update weights took %d cycles / %0.3f ms\n", end - start, (end - start) / CPU_FREQ);

    // printf("Total DUET update time: %d cycles / %0.3f ms\n", total, total / CPU_FREQ);



    // setupSettings();
    // setupButton();

    // // To turn on the power for the Qwiic connector (takes a lot of power)
    // // gpio_config_t pwr_conf = {
    // //     .pin_bit_mask = (1ULL << 0),
    // //     .mode = GPIO_MODE_OUTPUT,
    // // };
    // // ESP_ERROR_CHECK(gpio_config(&pwr_conf));
    // // ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_0, 1));

    // // Start I2C communication (must be before setupAudio())
    // // Note: default I2C clock speed is 100kHz, but we can set it to 400kHz if needed
    // if (!Wire.begin()) { printf("!! I2C communication failed\n"); while (true); }

    // if (!setupSD()) { while (true); }
    // if (!setupAudio()) { while (true); }
    // setupVolumeMonitor();
}

// uint16_t audioBuffer[4096];
// uint32_t audioOffset = 0;

void loop() {
    // audioOffset = generateSineWave(440, 32767, audioOffset, audioBuffer, 4096);
    // sendAudioToI2S((uint8_t*)audioBuffer, 4096 * sizeof(uint16_t));
    yield();
}
