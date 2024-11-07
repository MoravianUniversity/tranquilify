#include "data.h"
#include "audio.h"
#include "settings.h"

#include <SPI.h>
#include <SdFat.h>
#include <sdios.h>

// SD Card Pins
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5     // Thing Plus C

#define SPI_SPEED SD_SCK_MHZ(25)  // TODO: best speed is 50 but nothing above 25 works
#define SD_CONFIG SdSpiConfig(SD_CS, DEDICATED_SPI, SPI_SPEED)

SdFs sd;

bool setupSD() {
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


char timestampFilePath[24] = "/timestamps.txt";
char audioFilePath[24] = "/audio.wav";
unsigned long startTimestamp = 0;

void nextFiles() {
    int counter = incrementCounter();
    sprintf(timestampFilePath, "/timestamps_%06d.txt", counter);
    sprintf(audioFilePath, "/audio_%06d.wav", counter);
    startTimestamp = millis(); // the time that the timestamps are relative to
}

bool ensureSDCardAndFilePaths() {
    // If the SD card isn't initialized, set it up
    if (sd.card()->errorCode() != 0 || sd.vol()->fatType() == 0) {
        if (!setupSD()) { return false; }
    }

    // TODO: if the file path variables are not set up call nextFiles() - don't want to do this yet while testing though

    return true;
}

bool writeFile(const char * path, const char * content) {
    FsFile file = sd.open(path, FILE_WRITE);
    if (!file) { Serial.printf("!! Failed to open file %s for writing\n", path); return false; }
    if (!file.print(content)) { Serial.println("!! Write failed"); file.close(); return false; }
    file.close();
    return true;
}

bool recordTimestamp() {
    if (!ensureSDCardAndFilePaths()) { return false; }
    char buffer[32];
    sprintf(buffer, "%lu\n", millis() - startTimestamp);
    return writeFile(timestampFilePath, buffer);
}

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

FsFile startWAVFile() {
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
    FsFile file = sd.open(audioFilePath, O_WRONLY | O_CREAT | O_TRUNC); // write and create   // TODO: eventually use O_EXCL instead of O_TRUNC
    if (!file) { Serial.printf("!! Failed to create WAV file %s\n", audioFilePath); return file; }
    // TODO: file.preAllocate(...) // pre-allocate space for the file for an hour of recording (or max left on card minus a few KB)
    if (file.write((uint8_t*)&header, sizeof(header)) != sizeof(header)) { Serial.println("!! Failed to write complete WAV header"); file.close(); }
    return file;
}

bool recordWAVData(uint8_t* data, uint32_t length) {
    // TODO: can this whole thing be done asynchronously? we would need a double buffer for the audio data, but we could then not hold up the main loop
    if (!ensureSDCardAndFilePaths()) { return false; }

    // Open the file for writing, creating if necessary
    FsFile file = !sd.exists(audioFilePath) ? startWAVFile() : sd.open(audioFilePath, O_WRONLY | O_AT_END); // write but don't create, start at end
    if (!file) { Serial.printf("!! Failed to open WAV file %s\n", audioFilePath); return false; }

    // Append the data
    size_t written = file.write(data, length);
    if (written == 0) { Serial.println("!! Failed to write any WAV data"); file.close(); return false; }
    if (written != length) { Serial.printf("!! Warning: only wrote %llu bytes of WAV data instead of %llu. SD card is probably full\n", written, length); }

    // Update sizes in headers
    size_t cur_size = file.size();
    size_t new_size = cur_size + written;
    if (!file.seek(4)) { Serial.println("!! Failed to seek to file size in WAV header"); file.close(); return false; }
    size_t file_size = new_size - 8;
    if (file.write((uint8_t*)&file_size, 4) != 4) { Serial.println("!! Failed to write file size in WAV header"); file.close(); return false; }
    if (!file.seek(40)) { Serial.println("!! Failed to seek to data size in WAV header"); file.close(); return false; }
    size_t data_size = new_size - sizeof(WavHeader);
    if (file.write((uint8_t*)&data_size, 4) != 4) { Serial.println("!! Failed to write data size in WAV header"); file.close(); return false; }

    // Close the file
    file.close();

    // Every hour, start a new file (~600 MB, max WAV file is 4GB - same as FAT32 limit so we could do ~6.7 hours)
    if (data_size > ONE_HOUR_OF_DATA) { nextFiles(); }

    return true;
}
