#include "data.h"
#include "sd.h"
#include "settings.h"
#include "audio.h"
#include "wav.h"

#define DISABLE_FS_H_WARNING
#include <SdFat.h>


char audioFileName[32] = { 0 };
char timestampFileName[32] = { 0 };
unsigned long startTimestamp;


/** Get the next available file names */
bool nextFiles(SdFs* sd) {
    // Make sure the current files are closed
    //int counter = incrementCounter(); // don't want to do this yet (while testing); once we do, remove the line below
    int counter = 0;

    sprintf(audioFileName, "/audio_%06d.wav", counter);
    FsFile audioFile = sd->open(audioFileName, O_WRONLY | O_CREAT);  // TODO: O_EXCL
    if (!audioFile) { Serial.printf("!! Failed to create file '%s'\n", audioFileName); return false; }
    if (!startWAVFile(audioFile)) { return false; }
    audioFile.close();
    
    sprintf(timestampFileName, "/timestamps_%06d.txt", counter);
    FsFile timestampFile = sd->open(timestampFileName, O_WRONLY | O_CREAT);  // TODO: O_EXCL
    if (!timestampFile) { Serial.printf("!! Failed to create file '%s'\n", timestampFileName); return false; }
    startTimestamp = millis(); // the time that the timestamps are relative to
    timestampFile.close();

    return true;
}

/** Ensure that the audio and timestamp files are available to write to */
bool ensureFiles(SdFs* sd) {
    // If the SD card is not available, clear the file names
    if (!sd) {
        audioFileName[0] = 0;
        timestampFileName[0] = 0;
        return false;
    }

    // Make sure files are availale
    return (audioFileName[0] && timestampFileName[0]) || nextFiles(sd);
}

/**
 * Write the given audio data to the WAV file on the SD card.
 * The writing flag is set to false when the writing is done.
 */
bool writeWAVData(SdFs* sd, WriteWAVParams *params) {
    if (!ensureFiles(sd)) { return false; }

    FsFile file = sd->open(audioFileName, O_WRONLY);
    if (!file) { Serial.printf("!! Failed to open file '%s'\n", audioFileName); return false; }
    size_t data_size = appendWAVData(file, params->buffer, params->length);
    file.close();
    params->writing = false;
    if (data_size > ONE_HOUR_OF_DATA) { nextFiles(sd); }
    return data_size > 0;
}

/** Write the button press and release times to the file. */
bool writeButtonData(SdFs* sd, ButtonEvent* event) {
    if (!ensureFiles(sd)) { return false; }

    // Note: technically this could end up writing negative numbers, but then they apply to the previous file
    // TODO: worse than that, we need the timestamp to be relative to the start of the recording, not the start of the files being open (which could be 100ms off!)
    FsFile file = sd->open(timestampFileName, O_WRONLY | O_APPEND);
    if (!file) { Serial.printf("!! Failed to open file '%s'\n", timestampFileName); return false; }
    bool error = file.printf("%ld %ld\n", event->pressTime - startTimestamp, event->releaseTime - startTimestamp) == 0;
    if (error) {
        Serial.printf("!! Error writing button data to file (%ld %ld)\n", file.getWriteError());
    }
    file.close();
    return !error;
}
