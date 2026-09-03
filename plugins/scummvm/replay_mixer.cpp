// Replay Mixer Manager for ScummVM - Implementation

#include "replay_mixer.h"
#include "audio/mixer_intern.h"
#include <cassert>
#include <cstring>
#include <cstdlib>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor

ReplayMixerManager::ReplayMixerManager(uint32_t sampleRate)
    : MixerManager()
    , _sampleRate(sampleRate)
    , _audioSamples(0)
    , _audioBuffer(nullptr)
    , _bufferSize(0) {

    // Allocate buffer for ~2 frames worth of audio at 60fps
    // At 44100Hz, one frame at 60fps = 735 samples, so 2 frames = ~1470
    // We'll use 2048 to be safe
    _bufferSize = 4096;
    _audioBuffer = (int16_t*)calloc(_bufferSize * 2, sizeof(int16_t)); // *2 for stereo
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor

ReplayMixerManager::~ReplayMixerManager() {
    free(_audioBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize mixer

void ReplayMixerManager::init() {
    // Create the mixer with our sample rate
    // stereo = true, samples = buffer size for internal processing
    _mixer = new Audio::MixerImpl(_sampleRate, true, 1024);
    assert(_mixer);
    _mixer->setReady(true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Suspend/resume audio

void ReplayMixerManager::suspendAudio() {
    _audioSuspended = true;
}

int ReplayMixerManager::resumeAudio() {
    if (!_audioSuspended) {
        return -2;
    }
    _audioSuspended = false;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update - mix audio samples for one frame

void ReplayMixerManager::update(uint32_t sampleCount) {
    if (_audioSuspended || !_mixer) {
        _audioSamples = 0;
        return;
    }

    // Clamp to buffer size
    if (sampleCount > _bufferSize) {
        sampleCount = _bufferSize;
    }

    // Mix audio into our buffer
    // mixCallback expects buffer size in bytes (samples * 2 channels * 2 bytes per sample)
    _mixer->mixCallback((uint8_t*)_audioBuffer, sampleCount * 2 * sizeof(int16_t));
    _audioSamples = sampleCount;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert to float buffer for frontend

void ReplayMixerManager::convertToFloat(float* output, uint32_t frames) const {
    if (!output || !_audioBuffer) return;

    uint32_t samplesToConvert = frames;
    if (samplesToConvert > _audioSamples) {
        samplesToConvert = _audioSamples;
    }

    // Convert 16-bit stereo to float stereo
    const float scale = 1.0f / 32768.0f;
    for (uint32_t i = 0; i < samplesToConvert * 2; i++) { // *2 for stereo
        output[i] = (float)_audioBuffer[i] * scale;
    }

    // Zero any remaining samples if we didn't have enough
    if (frames > _audioSamples) {
        memset(output + _audioSamples * 2, 0, (frames - _audioSamples) * 2 * sizeof(float));
    }
}
