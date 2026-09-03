// Replay Mixer Manager for ScummVM
//
// Manages audio mixing and provides samples to the frontend

#ifndef REPLAY_MIXER_H
#define REPLAY_MIXER_H

#include "backends/mixer/mixer.h"

class ReplayMixerManager : public MixerManager {
public:
    ReplayMixerManager(uint32_t sampleRate = 44100);
    virtual ~ReplayMixerManager();

    // MixerManager interface
    virtual void init() override;
    virtual void suspendAudio() override;
    virtual int resumeAudio() override;
    virtual bool isNullDevice() const override { return false; }

    // Process audio for one frame (fills internal buffer)
    void update(uint32_t sampleCount);

    // Get audio buffer (16-bit stereo interleaved)
    const int16_t* getAudioBuffer() const { return _audioBuffer; }
    uint32_t getAudioSamples() const { return _audioSamples; }
    uint32_t getSampleRate() const { return _sampleRate; }

    // Convert to float buffer for frontend
    void convertToFloat(float* output, uint32_t frames) const;

private:
    uint32_t _sampleRate;
    uint32_t _audioSamples;  // Number of stereo samples in buffer

    // Audio buffer (16-bit stereo interleaved)
    int16_t* _audioBuffer;
    uint32_t _bufferSize;    // Buffer capacity in samples
};

#endif // REPLAY_MIXER_H
