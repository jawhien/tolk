/**
 *  Product:        Tolk
 *  File:           Tolk.cpp
 *  Description:    C-style DLL exports.
 *  Copyright:      (c) 2014-2016, Davy Kager <mail@davykager.nl>
 *  License:        LGPLv3
 */

#include <windows.h>
#include <process.h>
#include <exception>
#include <list>
#include "Tolk.h"
#include "ScreenReaderDriverJAWS.h"
#include "ScreenReaderDriverNVDA.h"
#include "ScreenReaderDriverSA.h"
#include "ScreenReaderDriverSAPI.h"
#include "ScreenReaderDriverSNova.h"
#include "ScreenReaderDriverWE.h"
#include "ScreenReaderDriverZT.h"

using namespace std;

namespace Driver {

bool Tolk_IsLoaded();
const wchar_t *Tolk_DetectScreenReader();

bool g_isLoaded = false;
list<ScreenReaderDriver*> g_screenReaderDrivers;
ScreenReaderDriverSAPI *g_sapi = NULL;
ScreenReaderDriver *g_currentScreenReaderDriver = NULL;
bool g_trySAPI = false;
bool g_preferSAPI = false;

void Tolk_Load() {
  if (CoInitializeEx(NULL, COINIT_MULTITHREADED) == S_FALSE) CoUninitialize();
  if (Tolk_IsLoaded()) return;
  g_screenReaderDrivers.push_back(new ScreenReaderDriverJAWS());
  g_screenReaderDrivers.push_back(new ScreenReaderDriverWE());
  g_screenReaderDrivers.push_back(new ScreenReaderDriverNVDA());
#ifndef _WIN64
  // This driver does not have 64-bit support.
  g_screenReaderDrivers.push_back(new ScreenReaderDriverSNova());
#endif
  g_screenReaderDrivers.push_back(new ScreenReaderDriverSA());
  g_screenReaderDrivers.push_back(new ScreenReaderDriverZT());
  if (g_trySAPI)
    g_sapi = new ScreenReaderDriverSAPI();
  g_isLoaded = true;
}

bool Tolk_IsLoaded() {
  return g_isLoaded;
}

void Tolk_Unload() {
  if (Tolk_IsLoaded()) {
    g_isLoaded = false;
    g_currentScreenReaderDriver = NULL;
    if (g_sapi) {
      delete g_sapi;
      g_sapi = NULL;
    }
    for (list<ScreenReaderDriver*>::reverse_iterator rit = g_screenReaderDrivers.rbegin(); rit != g_screenReaderDrivers.rend(); ++rit)
      delete *rit;
    g_screenReaderDrivers.clear();
  }
  CoUninitialize();
}

void Tolk_TrySAPI(bool trySAPI) {
  if (g_trySAPI == trySAPI) return;
  g_trySAPI = trySAPI;
  if (Tolk_IsLoaded()) {
    if (g_trySAPI && !g_sapi)
      g_sapi = new ScreenReaderDriverSAPI();
    else if (!g_trySAPI && g_sapi) {
      delete g_sapi;
      g_sapi = NULL;
    }
    g_currentScreenReaderDriver = NULL;
  }
}

void Tolk_PreferSAPI(bool preferSAPI) {
  if (g_preferSAPI == preferSAPI) return;
  g_preferSAPI = preferSAPI;
  if (Tolk_IsLoaded() && g_trySAPI && g_sapi)
    g_currentScreenReaderDriver = NULL;
}

const wchar_t * Tolk_DetectScreenReader() {
  if (!Tolk_IsLoaded()) return NULL;
  if (g_currentScreenReaderDriver && (g_preferSAPI || g_currentScreenReaderDriver != g_sapi) && g_currentScreenReaderDriver->IsActive())
    return g_currentScreenReaderDriver->GetName();
  if (g_trySAPI && g_preferSAPI && g_sapi && g_sapi->IsActive()) {
    g_currentScreenReaderDriver = g_sapi;
    return g_currentScreenReaderDriver->GetName();
  }
  for (list<ScreenReaderDriver*>::iterator it = g_screenReaderDrivers.begin(); it != g_screenReaderDrivers.end(); ++it) {
    ScreenReaderDriver *screenReaderDriver = *it;
    if (screenReaderDriver != g_currentScreenReaderDriver && screenReaderDriver->IsActive()) {
      g_currentScreenReaderDriver = screenReaderDriver;
      return g_currentScreenReaderDriver->GetName();
    }
  }
  if (g_trySAPI && !g_preferSAPI && g_sapi && g_sapi->IsActive()) {
    g_currentScreenReaderDriver = g_sapi;
    return g_currentScreenReaderDriver->GetName();
  }
  g_currentScreenReaderDriver = NULL;
  return NULL;
}

bool Tolk_HasSpeech() {
  if (Tolk_DetectScreenReader())
    return g_currentScreenReaderDriver->HasSpeech();
  return false;
}

bool Tolk_HasBraille() {
  if (Tolk_DetectScreenReader())
    return g_currentScreenReaderDriver->HasBraille();
  return false;
}

bool Tolk_Output(const wchar_t *str, bool interrupt) {
  if (str && Tolk_DetectScreenReader())
    return g_currentScreenReaderDriver->Output(str, interrupt);
  return false;
}

bool Tolk_Speak(const wchar_t *str, bool interrupt) {
  if (str && Tolk_DetectScreenReader())
    return g_currentScreenReaderDriver->Speak(str, interrupt);
  return false;
}

bool Tolk_Braille(const wchar_t *str) {
  if (str && Tolk_DetectScreenReader())
    return g_currentScreenReaderDriver->Braille(str);
  return false;
}

bool Tolk_IsSpeaking() {
  if (Tolk_DetectScreenReader())
    return g_currentScreenReaderDriver->IsSpeaking();
  return false;
}

bool Tolk_Silence() {
  if (Tolk_DetectScreenReader())
    return g_currentScreenReaderDriver->Silence();
  return false;
}

} // namespace Driver

namespace {

enum Operation {
  OpLoad, OpIsLoaded, OpUnload, OpTrySAPI, OpPreferSAPI, OpDetectScreenReader,
  OpHasSpeech, OpHasBraille, OpOutput, OpSpeak, OpBraille, OpIsSpeaking, OpSilence
};

struct Request {
  Operation operation;
  const wchar_t *text;
  bool flag;
  bool result;
  const wchar_t *name;
  exception_ptr error;

  Request(Operation op, const wchar_t *str, bool value) :
    operation(op), text(str), flag(value), result(false), name(NULL) {}
};

// Serialize the complete request, including startup and shutdown. A request
// stays on the caller's stack until the worker has returned the driver result.
SRWLOCK g_requestLock = SRWLOCK_INIT;
HANDLE g_worker = NULL;
HANDLE g_requestReady = NULL;
HANDLE g_requestDone = NULL;
Request *g_request = NULL;

void Execute(Request &request) {
  switch (request.operation) {
    case OpLoad: Driver::Tolk_Load(); break;
    case OpIsLoaded: request.result = Driver::Tolk_IsLoaded(); break;
    case OpUnload: Driver::Tolk_Unload(); break;
    case OpTrySAPI: Driver::Tolk_TrySAPI(request.flag); break;
    case OpPreferSAPI: Driver::Tolk_PreferSAPI(request.flag); break;
    case OpDetectScreenReader: request.name = Driver::Tolk_DetectScreenReader(); break;
    case OpHasSpeech: request.result = Driver::Tolk_HasSpeech(); break;
    case OpHasBraille: request.result = Driver::Tolk_HasBraille(); break;
    case OpOutput: request.result = Driver::Tolk_Output(request.text, request.flag); break;
    case OpSpeak: request.result = Driver::Tolk_Speak(request.text, request.flag); break;
    case OpBraille: request.result = Driver::Tolk_Braille(request.text); break;
    case OpIsSpeaking: request.result = Driver::Tolk_IsSpeaking(); break;
    case OpSilence: request.result = Driver::Tolk_Silence(); break;
  }
}

unsigned __stdcall WorkerMain(void *) {
  for (;;) {
    WaitForSingleObject(g_requestReady, INFINITE);
    const bool stop = (g_request->operation == OpUnload);
    try {
      Execute(*g_request);
    } catch (...) {
      // Always wake the caller, even if a driver throws an exception.
      g_request->error = current_exception();
    }
    SetEvent(g_requestDone);
    if (stop) return 0;
  }
}

void CloseWorkerHandles() {
  if (g_worker) CloseHandle(g_worker);
  if (g_requestReady) CloseHandle(g_requestReady);
  if (g_requestDone) CloseHandle(g_requestDone);
  g_worker = g_requestReady = g_requestDone = NULL;
}

void WaitForWorker(HANDLE handle) {
  // A driver may marshal COM calls back to the caller's STA. Keep COM call
  // dispatch alive while waiting, without initializing COM on that caller.
  DWORD index;
  if (FAILED(CoWaitForMultipleHandles(0, INFINITE, 1, &handle, &index)))
    WaitForSingleObject(handle, INFINITE);
}

Request Dispatch(Operation operation, const wchar_t *text = NULL, bool flag = false) {
  Request request(operation, text, flag);
  AcquireSRWLockExclusive(&g_requestLock);

  if (!g_worker && operation == OpLoad) {
    g_requestReady = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_requestDone = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (g_requestReady && g_requestDone)
      g_worker = (HANDLE)_beginthreadex(NULL, 0, WorkerMain, NULL, 0, NULL);
    if (!g_worker) {
      CloseWorkerHandles();
      ReleaseSRWLockExclusive(&g_requestLock);
      return request;
    }
  }

  if (g_worker) {
    g_request = &request;
    SetEvent(g_requestReady);
    WaitForWorker(g_requestDone);
    g_request = NULL;
    if (operation == OpUnload) {
      WaitForWorker(g_worker);
      CloseWorkerHandles();
    }
  } else if (operation != OpUnload) {
    // Before Load there are no drivers. Only configuration and empty-state
    // queries run here; in particular, Unload must not touch the caller's COM.
    Execute(request);
  }

  ReleaseSRWLockExclusive(&g_requestLock);
  if (request.error) rethrow_exception(request.error);
  return request;
}

} // namespace

extern "C" {

TOLK_DLL_DECLSPEC void TOLK_CALL Tolk_Load() { Dispatch(OpLoad); }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_IsLoaded() { return Dispatch(OpIsLoaded).result; }
TOLK_DLL_DECLSPEC void TOLK_CALL Tolk_Unload() { Dispatch(OpUnload); }
TOLK_DLL_DECLSPEC void TOLK_CALL Tolk_TrySAPI(bool value) { Dispatch(OpTrySAPI, NULL, value); }
TOLK_DLL_DECLSPEC void TOLK_CALL Tolk_PreferSAPI(bool value) { Dispatch(OpPreferSAPI, NULL, value); }
TOLK_DLL_DECLSPEC const wchar_t * TOLK_CALL Tolk_DetectScreenReader() { return Dispatch(OpDetectScreenReader).name; }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_HasSpeech() { return Dispatch(OpHasSpeech).result; }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_HasBraille() { return Dispatch(OpHasBraille).result; }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_Output(const wchar_t *str, bool interrupt) { return Dispatch(OpOutput, str, interrupt).result; }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_Speak(const wchar_t *str, bool interrupt) { return Dispatch(OpSpeak, str, interrupt).result; }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_Braille(const wchar_t *str) { return Dispatch(OpBraille, str).result; }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_IsSpeaking() { return Dispatch(OpIsSpeaking).result; }
TOLK_DLL_DECLSPEC bool TOLK_CALL Tolk_Silence() { return Dispatch(OpSilence).result; }

} // extern "C"
