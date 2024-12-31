#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DISABLE_FS_H_WARNING
#include <SdFat.h>

// TODO: make these use general stream classes instead of FsFile so it is more portable?

/**
 * Start a new WAV file with the given file.
 * Writes the minimal header to the file.
 */
bool startWAVFile(FsFile& file);

/**
 * Append the given data to the WAV file. Updates the file and data sizes in the header.
 */
bool appendWAVData(FsFile& file, uint8_t* data, uint32_t length);

/**
 * Read the WAV header from the given file.
 * Returns true if the header is valid. In this case the file position is at the start of the data
 * and the size of the wav data is returned. The start of the data can be obtained by calling
 * file.position() immediately after this function returns true.
 * If false is returned, the file position is undefined.
 */
bool readWavHeader(FsFile& file, uint32_t& dataSize);
