// Stub implementations of scripting classes without Lua dependency
// This provides empty implementations for the scripting API that Mesen2's
// Debugger class requires, allowing the core to build without Lua support.

// Suppress a warning Mesen2's headers trip. -Wunused-private-field is a clang warning and the
// pragma naming it is a clang pragma, so it is guarded: GCC builds this too, and an unrecognised
// pragma is an error there under -Werror.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif

#include "Debugger/Debugger.h"
#include "Debugger/ScriptHost.h"
#include "Debugger/ScriptingContext.h"
#include "Debugger/ScriptManager.h"
#include "pch.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ScriptingContext stubs

ScriptingContext* ScriptingContext::_context = nullptr;

ScriptingContext::ScriptingContext(Debugger* debugger) {
    _debugger = debugger;
}

ScriptingContext::~ScriptingContext() {}

bool ScriptingContext::LoadScript(string scriptName, string path, string scriptContent, Debugger* debugger) {
    return false;
}

void ScriptingContext::Log(string message) {}

string ScriptingContext::GetLog() {
    return "";
}

Debugger* ScriptingContext::GetDebugger() {
    return _debugger;
}

string ScriptingContext::GetScriptName() {
    return _scriptName;
}

int ScriptingContext::CallEventCallback(EventType type, CpuType cpuType) {
    return 0;
}

bool ScriptingContext::CheckInitDone() {
    return false;
}

bool ScriptingContext::IsSaveStateAllowed() {
    return true;
}

void ScriptingContext::RefreshMemoryCallbackFlags() {}

void ScriptingContext::RegisterMemoryCallback(CallbackType type, int startAddr, int endAddr, MemoryType memType,
                                              CpuType cpuType, int reference) {}

void ScriptingContext::UnregisterMemoryCallback(CallbackType type, int startAddr, int endAddr, MemoryType memType,
                                                CpuType cpuType, int reference) {}

void ScriptingContext::RegisterEventCallback(EventType type, int reference) {}

void ScriptingContext::UnregisterEventCallback(EventType type, int reference) {}

// Template instantiations for CallMemoryCallback
template <typename T>
void ScriptingContext::CallMemoryCallback(AddressInfo relAddr, T& value, CallbackType type, CpuType cpuType) {
    // No-op: no scripts to call
}

// Explicit template instantiations for the types used by Mesen2
template void ScriptingContext::CallMemoryCallback<uint8_t>(AddressInfo, uint8_t&, CallbackType, CpuType);
template void ScriptingContext::CallMemoryCallback<uint16_t>(AddressInfo, uint16_t&, CallbackType, CpuType);
template void ScriptingContext::CallMemoryCallback<uint32_t>(AddressInfo, uint32_t&, CallbackType, CpuType);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ScriptHost stubs

ScriptHost::ScriptHost(int scriptId) {
    _scriptId = scriptId;
}

int ScriptHost::GetScriptId() {
    return _scriptId;
}

string ScriptHost::GetLog() {
    return "";
}

bool ScriptHost::LoadScript(string scriptName, string path, string scriptContent, Debugger* debugger) {
    return false;
}

void ScriptHost::ProcessEvent(EventType eventType, CpuType cpuType) {}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ScriptManager stubs

ScriptManager::ScriptManager(Debugger* debugger) {
    _debugger = debugger;
    _hasScript = false;
}

ScriptManager::~ScriptManager() {}

int32_t ScriptManager::LoadScript(string name, string path, string content, int32_t scriptId) {
    // Lua scripting not supported in this build
    return -1;
}

void ScriptManager::RemoveScript(int32_t scriptId) {
    // No-op: no scripts to remove
}

string ScriptManager::GetScriptLog(int32_t scriptId) {
    return "";
}

void ScriptManager::ProcessEvent(EventType type, CpuType cpuType) {
    // No-op: no scripts to process events
}

void ScriptManager::RefreshMemoryCallbackFlags() {
    _isCpuMemoryCallbackEnabled = false;
    _isPpuMemoryCallbackEnabled = false;
}
