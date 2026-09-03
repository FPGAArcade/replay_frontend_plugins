// Replay OSystem Backend for ScummVM - Implementation

#include "replay_osystem.h"
#include "replay_graphics.h"
#include "replay_mixer.h"
#include "replay_fs.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "base/main.h"

#include <ctime>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <time.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cross-platform time and sleep functions

static uint32 getTimeMillis() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // Convert from 100-nanosecond intervals since 1601 to milliseconds
    return (uint32)(uli.QuadPart / 10000ULL);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

static void sleepMillis(uint32 msecs) {
#ifdef _WIN32
    Sleep((DWORD)msecs);
#else
    usleep((useconds_t)(msecs * 1000));
#endif
}

// Global instance for ScummVM's g_system
OSystem_Replay* g_replay_system = nullptr;

// ScummVM entry point arguments (allocated statically for simplicity)
static int s_argc = 0;
static char* s_argv_storage[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
static char** s_argv = s_argv_storage;
static char s_game_path[1024] = { 0 };
static char s_game_id[256] = { 0 };
static char s_path_arg[1100] = { 0 };  // "-p" + path

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor

OSystem_Replay::OSystem_Replay()
    : _mainThread(nullptr)
    , _scummVMThread(nullptr)
    , _startTime(0)
    , _quit(false)
    , _initialized(false)
    , _replayGraphicsManager(nullptr)
    , _replayMixerManager(nullptr)
    , _sampleRate(44100) {

    // Set global instance
    g_replay_system = this;

    // Create filesystem factory (uses Replay VFS integration)
    _fsFactory = new ReplayFilesystemFactory();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor

OSystem_Replay::~OSystem_Replay() {
    // Clean up coroutines
    if (_scummVMThread) {
        co_delete(_scummVMThread);
        _scummVMThread = nullptr;
    }

    g_replay_system = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize backend

void OSystem_Replay::initBackend() {
    // Guard against double initialization
    if (_initialized) {
        return;
    }

    _startTime = getTimeMillis();

    // Create timer manager
    _timerManager = new DefaultTimerManager();

    // Create event manager
    _eventManager = new DefaultEventManager(this);

    // Create save file manager
    _savefileManager = new DefaultSaveFileManager();

    // Create our custom graphics manager
    _replayGraphicsManager = new ReplayGraphicsManager();
    _graphicsManager = _replayGraphicsManager;

    // Create our custom mixer manager
    _replayMixerManager = new ReplayMixerManager(_sampleRate);
    _mixerManager = _replayMixerManager;
    _mixerManager->init();

    // Call base class
    BaseBackend::initBackend();

    _initialized = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set game path (directory containing game data)

void OSystem_Replay::setGamePath(const char* path) {
    strncpy(s_game_path, path, sizeof(s_game_path) - 1);
    s_game_path[sizeof(s_game_path) - 1] = '\0';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set game ID (ScummVM game identifier like "monkey", "tentacle", etc.)

void OSystem_Replay::setGameId(const char* gameId) {
    strncpy(s_game_id, gameId, sizeof(s_game_id) - 1);
    s_game_id[sizeof(s_game_id) - 1] = '\0';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize coroutines

void OSystem_Replay::initCoroutines() {
    // Build command line arguments to launch a game directly
    // argv[0] = program name
    // If game path is set, use --auto-detect to detect and run the game automatically
    // Otherwise, launch the launcher GUI (requires theme files)

    s_argv_storage[0] = (char*)"scummvm";

    if (s_game_path[0] != '\0') {
        // Use --auto-detect to detect and run the game from the specified path
        // ScummVM will scan the directory, identify the game, and start it
        snprintf(s_path_arg, sizeof(s_path_arg), "--path=%s", s_game_path);
        s_argv_storage[1] = (char*)"--auto-detect";
        s_argv_storage[2] = s_path_arg;
        s_argv_storage[3] = nullptr;
        s_argc = 3;
    } else {
        // No game specified - launch launcher GUI
        s_argc = 1;
    }

    // Debug: Print command line being passed to ScummVM
    fprintf(stderr, "[ScummVM DEBUG] Command line: ");
    for (int i = 0; i < s_argc; i++) {
        fprintf(stderr, "%s ", s_argv_storage[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);

    // Get handle to main thread (frontend thread)
    _mainThread = co_active();

    // Create ScummVM thread with 1MB stack
    _scummVMThread = co_create(1024 * 1024, scummVMMainEntry);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ScummVM main entry point (runs in coroutine)

void OSystem_Replay::scummVMMainEntry() {
    // Run ScummVM main loop
    scummvm_main(s_argc, s_argv);

    // Mark as quit when main returns and return to main thread
    if (g_replay_system) {
        g_replay_system->_quit = true;
        co_switch(g_replay_system->_mainThread);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Run one frame (called from frontend)

void OSystem_Replay::runFrame() {
    if (!_scummVMThread || _quit) {
        return;
    }

    // Switch to ScummVM thread - timing is handled by real wall-clock time
    co_switch(_scummVMThread);

    // When we return here, ScummVM has yielded (from delayMillis)
    // The graphics manager should have a new frame ready
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Poll events

bool OSystem_Replay::pollEvent(Common::Event &event) {
    // Process timers
    if (_timerManager) {
        ((DefaultTimerManager *)_timerManager)->checkTimers();
    }

    // Note: Audio is mixed in delayMillis() before each yield to frontend

    // Return queued events from frontend
    if (!_eventQueue.empty()) {
        event = _eventQueue.pop();
        return true;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queue event from frontend

void OSystem_Replay::queueEvent(const Common::Event& event) {
    _eventQueue.push(event);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create mutex

Common::MutexInternal *OSystem_Replay::createMutex() {
    return new NullMutexInternal();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get milliseconds - returns real wall-clock time since backend init

uint32 OSystem_Replay::getMillis(bool skipRecord) {
    (void)skipRecord;
    return getTimeMillis() - _startTime;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Delay milliseconds - THIS IS THE KEY YIELD POINT

void OSystem_Replay::delayMillis(uint msecs) {
    (void)msecs; // Debug logging removed

    uint32 start_time = getMillis();
    uint32 elapsed_time = 0;

    // Wait until enough real time has passed, yielding each iteration
    while (elapsed_time < msecs) {
        // Mix audio before yielding to frontend
        if (_replayMixerManager) {
            uint32_t samplesPerFrame = _sampleRate / 60;
            _replayMixerManager->update(samplesPerFrame);
        }

        // Yield to frontend to render a frame
        if (_mainThread) {
            co_switch(_mainThread);
        } else {
            // No coroutine - just sleep
            sleepMillis(1);
        }
        elapsed_time = getMillis() - start_time;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get time and date

void OSystem_Replay::getTimeAndDate(TimeDate &td, bool skipRecord) const {
    (void)skipRecord;

    time_t curTime = time(nullptr);
    struct tm t = *localtime(&curTime);

    td.tm_sec = t.tm_sec;
    td.tm_min = t.tm_min;
    td.tm_hour = t.tm_hour;
    td.tm_mday = t.tm_mday;
    td.tm_mon = t.tm_mon;
    td.tm_year = t.tm_year;
    td.tm_wday = t.tm_wday;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Quit

void OSystem_Replay::quit() {
    _quit = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Log message

void OSystem_Replay::logMessage(LogMessageType::Type type, const char *message) {
    // Output to stderr for now
    FILE *output = (type == LogMessageType::kInfo || type == LogMessageType::kDebug)
                   ? stdout : stderr;
    fputs(message, output);
    fflush(output);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add system archives to search set

void OSystem_Replay::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
    // Add paths where ScummVM looks for data files
    // These can be customized based on where the frontend stores game data
    (void)s;
    (void)priority;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory function to create OSystem instance

OSystem *OSystem_Replay_create() {
    return new OSystem_Replay();
}
