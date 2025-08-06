/**
 * General interface for audio codecs.
 *
 * Actual audio codecs are implemented in individual files.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <driver/i2s.h>

#include "audio.h"

class AudioCodec {
public:
    /**
     * Set up the audio codec.
     * 
     * Before this is called, the Serial and Wire interfaces must be set up.
     *
     * This must be called before any other methods are called.
     *
     * Returns true if the setup was successful, false otherwise.
     */
    virtual bool setup() = 0;

    /**
     * Set the volume of the audio codec.
     * The volume must be in the range -48 to 79 where:
     *   <0 is muted (should not be less than -48)
     *   0 to 79 is -73dB to +6dB in 1dB steps
     */
    virtual void setVolume(int8_t volume) = 0;

    /**
     * Get the I2S communication format used by the codec.
     * The default is I2S_COMM_FORMAT_STAND_I2S.
     */
    virtual i2s_comm_format_t i2s_comm_format() { return I2S_COMM_FORMAT_STAND_I2S; }
};

/**
 * Create an audio codec object.
 * This will create an instance of an AudioCodec subclass depending on the
 * AUDIO_CODEC define in audio.h. The caller is responsible for deleting the
 * object when done.
 */
AudioCodec* create_audio_codec();
