#include "wav.h"
#include "audio.h"

#include <stdbool.h>
#include <stdint.h>

#define DISABLE_FS_H_WARNING
#include <SdFat.h>

#define RIFF_BLOCK_ID { 'R', 'I', 'F', 'F' }
#define WAVE_FORMAT_ID { 'W', 'A', 'V', 'E' }
#define FMT_BLOCK_ID { 'f', 'm', 't', ' ' }
#define DATA_BLOCK_ID { 'd', 'a', 't', 'a' }

const static uint8_t RIFF_BLOCK_ID_CONST[] = RIFF_BLOCK_ID;
const static uint8_t WAVE_FORMAT_ID_CONST[] = WAVE_FORMAT_ID;
const static uint8_t FMT_BLOCK_ID_CONST[] = FMT_BLOCK_ID;
const static uint8_t DATA_BLOCK_ID_CONST[] = FMT_BLOCK_ID;

/** The master header of a RIFF (WAV) file. */
struct __attribute__((packed)) RiffHeader {
    uint8_t fileTypeBlockID[4]; // "RIFF"
    uint32_t fileSize; // size of file - 8 (skipping the header and this field)
    uint8_t fileFormatID[4]; // "WAVE"
};

/** A generic chunk in a RIFF file. */
struct __attribute__((packed)) RiffChunk {
    uint8_t blockID[4]; // ID of the block (e.g. "data")
    uint32_t blockSize; // number of bytes in the block (minus the ID and size)
    // uint8_t data[]; // the data itself
    // then padding to 2-byte boundary
};

/** The format chunk of a WAV file. */
struct __attribute__((packed)) FmtChunk {
    uint8_t blockID[4]; // "fmt "
    uint32_t blockSize; // always 16 (number of bytes in the format block)
    uint16_t audioFormat; // 1 for PCM
    uint16_t numChannels; // 1 for mono, 2 for stereo
    uint32_t sampleRate; // 44100 for CD quality
    uint32_t byteRate; // sampleRate * numChannels * bitsPerSample/8 
    uint16_t bytePerBlock; // numChannels * bitsPerSample/8
    uint16_t bitsPerSample; // 16 for CD quality
};

/**
 * The header of a WAV file.
 * This is a combination of the RIFF header, the FMT chunk, and the data chunk.
 * There can be other chunks in a WAV file, but this is the minimum required.
 */
struct __attribute__((packed)) WavHeader {
    RiffHeader riffHeader;
    FmtChunk fmtChunk;
    RiffChunk dataChunk;
};



///// Writing WAV Files /////

/**
 * Start a new WAV file with the given file.
 * Writes the minimal header to the file.
 */
bool startWAVFile(FsFile& file) {
    WavHeader header = {
        .riffHeader = {
            .fileTypeBlockID = RIFF_BLOCK_ID,
            .fileSize = sizeof(WavHeader) - 8, // updated as data is written
            .fileFormatID = WAVE_FORMAT_ID,
        },
        .fmtChunk = {
            .blockID = FMT_BLOCK_ID,
            .blockSize = 16,
            .audioFormat = 1, // PCM
            .numChannels = CHANNELS,
            .sampleRate = SAMPLE_RATE,
            .byteRate = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE,
            .bytePerBlock = CHANNELS * BYTES_PER_SAMPLE,
            .bitsPerSample = BITS_PER_SAMPLE,
        },
        .dataChunk = {
            .blockID = DATA_BLOCK_ID,
            .blockSize = 0, // updated as data is written
        },
    };
    // TODO: file.preAllocate(...) // pre-allocate space for the file for an hour of recording (or max left on card minus a few KB): ONE_HOUR_OF_DATA + sizeof(WavHeader)
    if (file.write((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        Serial.println("!! Failed to write complete WAV header");
        return false;
    }
    file.flush();
    return true;
}

/** Write the integer at a specific offset in the file. */
bool writeAt(FsFile& file, size_t offset, uint32_t data) {
    return file.seek(offset) && file.write(&data, sizeof(uint32_t)) == sizeof(uint32_t);
}

/**
 * Append the given data to the WAV file. Updates the file and data sizes in the header.
 */
size_t appendWAVData(FsFile& file, uint8_t* data, uint32_t length) {
    // Append the data
    size_t cur_size = file.size();
    if (!file.seek(cur_size)) { Serial.println("!! Failed to seek to end of WAV data"); return 0; }
    size_t written = file.write(data, length);
    if (written == 0) { Serial.println("!! Failed to write any WAV data"); return 0; }
    if (written != length) { Serial.printf("!! Warning: only wrote %llu bytes of WAV data instead of %llu. SD card is probably full\n", written, length); }

    // Update sizes in headers
    size_t new_size = cur_size + written;
    size_t data_size = new_size - sizeof(WavHeader);
    if (!writeAt(file, offsetof(WavHeader, riffHeader.fileSize), new_size-8)) { Serial.println("!! Failed to write file size in WAV header"); return 0; }
    if (!writeAt(file, offsetof(WavHeader, dataChunk.blockSize), data_size)) { Serial.println("!! Failed to write data size in WAV header"); return 0; }

    // Flush/sync the file
    file.flush();
    return data_size;
}


///// Reading WAV Files /////

bool findChunk(FsFile& file, const uint8_t* chunkID, RiffChunk* chunk) {
    // Look at chunks until we find the format chunk
    if (file.read((uint8_t*)chunk, sizeof(RiffChunk)) != sizeof(RiffChunk)) { return false; }
    while (memcmp(chunk->blockID, chunkID, 4) != 0) {
        if (!file.seek(file.position() + chunk->blockSize + chunk->blockSize % 2)) { return false; }
        if (file.read((uint8_t*)chunk, sizeof(RiffChunk)) != sizeof(RiffChunk)) { return false; }
    }
    return true;
}

/**
 * Read the WAV header from the given file.
 * Returns true if the header is valid. In this case the file position is at the start of the data
 * and the size of the wav data is returned. The start of the data can be obtained by calling
 * file.position() immediately after this function returns true.
 * If false is returned, the file position is undefined.
 */
bool readWavHeader(FsFile& file, uint32_t& dataSize) {
    // Read RIFF header
    RiffHeader riffHeader;
    if (file.read((uint8_t*)&riffHeader, sizeof(RiffHeader)) != sizeof(RiffHeader)) { return false; }
    if (memcmp(riffHeader.fileTypeBlockID, RIFF_BLOCK_ID_CONST, 4) != 0 || memcmp(riffHeader.fileFormatID, WAVE_FORMAT_ID_CONST, 4) != 0) { return false; }

    // Find the format chunk
    FmtChunk fmtChunk;
    if (!findChunk(file, FMT_BLOCK_ID_CONST, (RiffChunk*)&fmtChunk)) { return false; }
    if (fmtChunk.blockSize != sizeof(FmtChunk) - sizeof(RiffChunk)) { return false; }

    // Read and check the rest of the format chunk
    if (file.read(((uint8_t*)&fmtChunk) + sizeof(RiffChunk), sizeof(FmtChunk) - sizeof(RiffChunk)) != sizeof(FmtChunk) - sizeof(RiffChunk)) { return false; }
    if (fmtChunk.byteRate != fmtChunk.sampleRate * fmtChunk.numChannels * fmtChunk.bitsPerSample / 8 || fmtChunk.bytePerBlock != fmtChunk.numChannels * fmtChunk.bitsPerSample / 8) { return false; }
    if (fmtChunk.audioFormat != 1 || fmtChunk.numChannels != CHANNELS || fmtChunk.sampleRate != SAMPLE_RATE || fmtChunk.bitsPerSample != BITS_PER_SAMPLE) { return false; }

    // Find the data chunk
    RiffChunk dataChunk;
    if (!findChunk(file, DATA_BLOCK_ID_CONST, &dataChunk)) { return false; }

    // Save the data size
    dataSize = dataChunk.blockSize;
    return true;
}
