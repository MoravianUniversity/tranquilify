#include "data.h"
#include "audio.h"
#include "settings.h"

#define DISABLE_FS_H_WARNING
#include <SPI.h>
#include <SdFat.h>
#include <sdios.h>

#include <freertos/queue.h>
#include <freertos/semphr.h>

// SD Card Pins
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5     // Thing Plus C

#define SPI_SPEED SD_SCK_MHZ(25)  // TODO: best speed is 50 but nothing above 25 works
#define SD_CONFIG SdSpiConfig(SD_CS, SHARED_SPI /*DEDICATED_SPI*/, SPI_SPEED) // TODO: use DEDICATED_SPI for better performance (but fails with multiple tasks... see https://github.com/greiman/SdFat/issues/349)
// One solution would be to use a task for just the SD cards and a queue of things to write and to which file

SdFs sd;

FsFile audioFile;

FsFile timestampFile;
unsigned long startTimestamp = 0;
QueueHandle_t timestampQueue = NULL;

// We use a mutex to protect the SD card and files from being accessed by multiple tasks at once.
// Every use of the variables sd, audioFile, and timestampFile MUST be protected by this mutex.
// The functions setupSD(), nextFiles(), ensureSDCardAndFiles(), and startWAVFile() must be protected by this mutex.
SemaphoreHandle_t sdMutex;


bool startWAVFile();
void timestampRecordingTask(void *pvParameters);


///// General SD Card /////

bool setupSD() {
    // Initialize SD card
    if (!sd.begin(SD_CONFIG)) {
        if (sd.card()->errorCode()) { Serial.printf("!! SD card initialization failed: 0x%02x 0x%08x\n", int(sd.card()->errorCode()), int(sd.card()->errorData())); }
        else if (sd.vol()->fatType() == 0) { Serial.println("!! SD card not formatted with FAT16/FAT32/exFAT"); }
        else { Serial.println("!! SD card initialization failed"); }
        return false;
    }

    // Print SD card info
    uint64_t size = sd.card()->sectorCount() * 512ull; // 512 bytes per sector
    if (size == 0) { Serial.println("!! Can't determine the SD card size.\n"); return false; }
    double sizeGB = size / (1024.0 * 1024.0 * 1024.0);

    uint32_t kbPerCluster = sd.vol()->sectorsPerCluster() / 2;
    int32_t freeClusterCount = sd.vol()->freeClusterCount();
    uint32_t freeKB = freeClusterCount < 0 ? 0 : freeClusterCount * kbPerCluster;
    double usedGB = freeClusterCount < 0 ? -1 : sizeGB - (freeKB / (1024.0 * 1024.0 * 1024.0)); // TODO: wrong?

    uint8_t fatType = sd.vol()->fatType();
    char fatTypeStr[8] = { 0 };
    if (fatType <= 32) { sprintf(fatTypeStr, "FAT%d", fatType); } else { strcpy(fatTypeStr, "exFAT"); }

    Serial.printf("SD Card: %s, used %.2f / %.2f GB, cluster size: %d KB\n", fatTypeStr, usedGB, sizeGB, kbPerCluster);

    if ((sizeGB > 1 && kbPerCluster < 32) || (sizeGB < 2 && fatType == 32)) {
        Serial.println("This SD card should be reformatted for best performance.");
        Serial.println("Use a cluster size of 32 KB for cards larger than 1 GB.");
        Serial.println("Only cards larger than 2 GB should be formatted FAT32.");
    }

    return true;
}

bool setupData() {
    timestampQueue = xQueueCreate(8, sizeof(unsigned long));
    if (!timestampQueue) { Serial.println("!! Failed to create timestamp queue"); return false; }

    sdMutex = xSemaphoreCreateMutex();
    if (!sdMutex) { Serial.println("!! Failed to create SD mutex"); return false; }

    if (xTaskCreate(timestampRecordingTask, "TimestampRecording", 4096*2, NULL, 1, NULL) != pdPASS) {
        Serial.println("!! Failed to create timestamp recording task");
        return false;
    }

    xSemaphoreTake(sdMutex, portMAX_DELAY);
    bool retval = setupSD();
    xSemaphoreGive(sdMutex);
    return retval;
}

bool nextFiles() {
    // Make sure the current files have all pending data written and are closed
    if (audioFile) {
        audioFile.close();
    }
    if (timestampFile) {
        // Make sure all timestamps are written before closing the file
        if (uxQueueMessagesWaiting(timestampQueue) > 0) {
            xSemaphoreGive(sdMutex);  // temporarily release the mutex
            while (uxQueueMessagesWaiting(timestampQueue) > 0) { yield(); }
            xSemaphoreTake(sdMutex, portMAX_DELAY); // re-acquire the mutex
        }
        // Technically, we could lose the last few microsecond of timestamps here, but it's not a big deal
        timestampFile.close();
    }

    int counter = incrementCounter();
    char filepath[32];

    sprintf(filepath, "/audio_%06d.wav", counter);
    audioFile = sd.open(filepath, O_WRONLY | O_CREAT | O_EXCL);
    if (!audioFile) { Serial.printf("!! Failed to create file '%s'\n", filepath); return false; }
    if (!startWAVFile()) { return false; }
    
    sprintf(filepath, "/timestamps_%06d.txt", counter);
    timestampFile = sd.open(filepath, O_WRONLY | O_CREAT | O_EXCL);
    if (!timestampFile) { Serial.printf("!! Failed to create file '%s'\n", filepath); audioFile.close(); return false; }
    xQueueReset(timestampQueue);
    startTimestamp = millis(); // the time that the timestamps are relative to

    return true;
}

bool ensureSDCardAndFiles() {
    // If the SD card isn't initialized, set it up
    if (sd.card()->errorCode() != 0 || sd.vol()->fatType() == 0) {
        if (!setupSD()) { return false; }
    }

    // Make sure files are availale
    if (!audioFile || !timestampFile) {
        // if (!nextFiles()) { return false; }  // don't want to do this yet (while testing); once we do, remove all of the code below
        audioFile = sd.open("/audio.wav", O_WRONLY | O_CREAT | O_TRUNC);
        if (!audioFile) { Serial.println("!! Failed to create audio file"); return false; }
        if (!startWAVFile()) { audioFile.close(); return false; }
        timestampFile = sd.open("/timestamps.txt", O_WRONLY | O_CREAT | O_TRUNC);
        if (!timestampFile) { Serial.println("!! Failed to create timestamp file"); audioFile.close(); return false; }
    }

    return true;
}


///// Timestamps /////

void timestampRecordingTask(void *pvParameters) {
    unsigned long timestamp;
    char buffer[32];
    while (true) {
        if (xQueueReceive(timestampQueue, &timestamp, portMAX_DELAY)) {
            sprintf(buffer, "%lu\n", timestamp);
            Serial.print(buffer);
            xSemaphoreTake(sdMutex, portMAX_DELAY);
            if (ensureSDCardAndFiles()) {
                if (timestampFile.print(buffer) != 0) { timestampFile.flush(); }
                else { Serial.printf("!! Failed to write timestamp %lu to file\n", timestamp); timestampFile.close(); }
            }
            xSemaphoreGive(sdMutex);
        }
    }
    vTaskDelete(NULL);
}

bool recordTimestampFromISR() {
    unsigned long timestamp = millis() - startTimestamp;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    BaseType_t retval = timestampQueue ? xQueueSendFromISR(timestampQueue, &timestamp, &higherPriorityTaskWoken) : pdFALSE;
    if (higherPriorityTaskWoken) { portYIELD_FROM_ISR(); }
    return retval == pdTRUE;
}


///// Audio /////

struct WavHeader {
    uint8_t FileTypeBlocID[4];
    uint32_t FileSize;
    uint8_t FileFormatID[4];
    uint8_t FormatBlocID[4];
    uint32_t BlocSize;
    uint16_t AudioFormat;
    uint16_t NbrChannels;
    uint32_t Frequence;
    uint32_t BytePerSec;
    uint16_t BytePerBloc;
    uint16_t BitsPerSample;
    uint8_t DataBlocID[4];
    uint32_t DataSize;
};

bool startWAVFile() {
    WavHeader header = {
        .FileTypeBlocID = { 'R', 'I', 'F', 'F' },
        .FileSize = sizeof(WavHeader) - 8, // updated as data is written
        .FileFormatID = { 'W', 'A', 'V', 'E' },
        .FormatBlocID = { 'f', 'm', 't', ' ' },
        .BlocSize = 16,
        .AudioFormat = 1, // PCM
        .NbrChannels = CHANNELS,
        .Frequence = SAMPLE_RATE,
        .BytePerSec = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE,
        .BytePerBloc = CHANNELS * BYTES_PER_SAMPLE,
        .BitsPerSample = BITS_PER_SAMPLE,
        .DataBlocID = { 'd', 'a', 't', 'a' },
        .DataSize = 0, // updated as data is written
    };
    // TODO: audioFile.preAllocate(...) // pre-allocate space for the file for an hour of recording (or max left on card minus a few KB): ONE_HOUR_OF_DATA + sizeof(WavHeader)
    if (audioFile.write((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        Serial.println("!! Failed to write complete WAV header");
        return false;
    }
    audioFile.flush();
    return true;
}

bool recordWAVData(uint8_t* data, uint32_t length) {
    // TODO: can this whole thing be done asynchronously? we would need a double buffer for the audio data, but we could then not hold up the main loop
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    if (!ensureSDCardAndFiles()) { xSemaphoreGive(sdMutex); return false; }

    // Append the data
    size_t cur_size = audioFile.size();
    if (!audioFile.seek(cur_size)) { Serial.println("!! Failed to seek to end of WAV data"); audioFile.close(); xSemaphoreGive(sdMutex); return false; }
    size_t written = audioFile.write(data, length);
    if (written == 0) { Serial.println("!! Failed to write any WAV data"); audioFile.close(); xSemaphoreGive(sdMutex); return false; }
    if (written != length) { Serial.printf("!! Warning: only wrote %llu bytes of WAV data instead of %llu. SD card is probably full\n", written, length); }

    // Update sizes in headers
    size_t new_size = cur_size + written;
    if (!audioFile.seek(4)) { Serial.println("!! Failed to seek to file size in WAV header"); audioFile.close(); xSemaphoreGive(sdMutex); return false; }
    size_t file_size = new_size - 8;
    if (audioFile.write((uint8_t*)&file_size, 4) != 4) { Serial.println("!! Failed to write file size in WAV header"); audioFile.close(); xSemaphoreGive(sdMutex); return false; }
    if (!audioFile.seek(40)) { Serial.println("!! Failed to seek to data size in WAV header"); audioFile.close(); xSemaphoreGive(sdMutex); return false; }
    size_t data_size = new_size - sizeof(WavHeader);
    if (audioFile.write((uint8_t*)&data_size, 4) != 4) { Serial.println("!! Failed to write data size in WAV header"); audioFile.close(); xSemaphoreGive(sdMutex); return false; }

    // Flush/sync the file
    audioFile.flush();

    // Every hour, start a new file (~600 MB, max WAV file is 4GB - same as FAT32 limit so we could do ~6.7 hours)
    if (data_size > ONE_HOUR_OF_DATA) { nextFiles(); }

    xSemaphoreGive(sdMutex);
    return true;
}
