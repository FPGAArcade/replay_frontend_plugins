// Replay OSystem Backend for ScummVM
//
// Custom OSystem implementation that integrates with the Replay Frontend.
// Uses libco coroutines to convert ScummVM's blocking main loop into
// frame-based execution compatible with run_frame() API.

#ifndef REPLAY_OSYSTEM_H
#define REPLAY_OSYSTEM_H

#include "backends/modular-backend.h"
#include "backends/events/default/default-events.h"
#include "common/events.h"
#include "common/queue.h"
#include "libco.h"

class ReplayGraphicsManager;
class ReplayMixerManager;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OSystem_Replay - Custom ScummVM backend for Replay Frontend

class OSystem_Replay : public ModularMixerBackend, public ModularGraphicsBackend, public Common::EventSource {
public:
    OSystem_Replay();
    virtual ~OSystem_Replay();

    // OSystem interface - initialization
    virtual void initBackend() override;

    // OSystem interface - events
    virtual bool pollEvent(Common::Event &event) override;

    // OSystem interface - threading/timing
    virtual Common::MutexInternal *createMutex() override;
    virtual uint32 getMillis(bool skipRecord = false) override;
    virtual void delayMillis(uint msecs) override;
    virtual void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

    // OSystem interface - misc
    virtual void quit() override;
    virtual void logMessage(LogMessageType::Type type, const char *message) override;
    virtual void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Replay Frontend integration

    // Run one frame of emulation (called from frontend via coroutine)
    void runFrame();

    // Get graphics manager (for frame buffer access)
    ReplayGraphicsManager* getReplayGraphicsManager() const { return _replayGraphicsManager; }

    // Get mixer manager (for audio access)
    ReplayMixerManager* getReplayMixerManager() const { return _replayMixerManager; }


    // Queue input events from frontend
    void queueEvent(const Common::Event& event);

    // Check if ScummVM has quit
    bool hasQuit() const { return _quit; }

    // Initialize coroutines (must be called before first runFrame)
    void initCoroutines();

    // Set sample rate for audio
    void setSampleRate(uint32_t rate) { _sampleRate = rate; }

    // Start ScummVM main loop in coroutine
    static void scummVMMainEntry();

    // Set game to launch (call before initCoroutines)
    void setGamePath(const char* path);
    void setGameId(const char* gameId);

private:
    // Coroutine handles
    cothread_t _mainThread;      // Frontend/host thread
    cothread_t _scummVMThread;   // ScummVM execution thread

    // Timing
    uint32 _startTime;           // System start time (real wall-clock ms)

    // Events
    Common::Queue<Common::Event> _eventQueue;

    // State
    bool _quit;
    bool _initialized;

    // Managers (we own these, ModularBackend stores pointers)
    ReplayGraphicsManager* _replayGraphicsManager;
    ReplayMixerManager* _replayMixerManager;

    // Audio
    uint32_t _sampleRate;
};

// Global instance (ScummVM uses g_system extensively)
extern OSystem_Replay* g_replay_system;

#endif // REPLAY_OSYSTEM_H
