// Integration checks for the real DLL. Run next to Tolk.dll and its vendor DLLs.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <thread>
#include <vector>
#include "../src/Tolk.h"

void Check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

HRESULT Apartment(APTTYPE &type) {
  APTTYPEQUALIFIER qualifier;
  return CoGetApartmentType(&type, &qualifier);
}

void CheckCaller(DWORD mode, bool initialize) {
  if (initialize) Check(SUCCEEDED(CoInitializeEx(NULL, mode)), "caller COM initialization");
  APTTYPE before, after;
  HRESULT original = Apartment(before);
  Tolk_Unload();
  Tolk_Load();
  Check(Tolk_IsLoaded(), "load worker");
  Tolk_Load();
  Tolk_DetectScreenReader();
  Tolk_HasSpeech();
  Tolk_HasBraille();
  Tolk_IsSpeaking();
  Check(!Tolk_Output(NULL), "null output");
  Check(!Tolk_Speak(NULL), "null speech");
  Check(!Tolk_Braille(NULL), "null braille");
  if (initialize) {
    Check(Apartment(after) == original && before == after,
          "caller COM unchanged while loaded");
  } else {
    // An uninitialized thread can be reported as implicit MTA while another
    // MTA thread exists. It must still be free to explicitly choose STA.
    Check(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED) == S_OK,
          "uninitialized caller can choose STA while worker is running");
    CoUninitialize();
  }
  Tolk_Unload();
  Tolk_Unload();
  Check(!Tolk_IsLoaded(), "worker stopped");
  Check(Apartment(after) == original && (FAILED(original) || before == after),
        "caller COM unchanged after unload");
  if (initialize) CoUninitialize();
}

void CheckOleCaller() {
  Check(SUCCEEDED(OleInitialize(NULL)), "caller OLE initialization");
  APTTYPE before, after;
  Check(SUCCEEDED(Apartment(before)), "OLE apartment before load");
  Tolk_Load();
  Tolk_Unload();
  Check(SUCCEEDED(Apartment(after)) && before == after, "OLE apartment preserved");
  Check(OleInitialize(NULL) == S_FALSE, "OLE remains initialized");
  OleUninitialize();
  OleUninitialize();
}

int main() {
  std::thread([] { CheckCaller(0, false); }).join();
  std::thread([] { CheckCaller(COINIT_APARTMENTTHREADED, true); }).join();
  std::thread([] { CheckCaller(COINIT_MULTITHREADED, true); }).join();
  std::thread(CheckOleCaller).join();
  std::puts("PASS: uninitialized, STA, MTA and OLE callers retain their COM state");

  // Create on one caller and finalize on another without moving driver objects.
  std::thread([] { Tolk_Load(); }).join();
  std::thread([] {
    Check(Tolk_IsLoaded(), "cross-thread loaded state");
    Tolk_DetectScreenReader();
    Tolk_Unload();
  }).join();
  std::puts("PASS: load and unload from different callers");

  Tolk_TrySAPI(false);
  Tolk_PreferSAPI(false);
  Tolk_Load();
  const wchar_t *name = Tolk_DetectScreenReader();
  std::wprintf(L"Default driver: %ls\n", name ? name : L"unavailable");
  if (name) {
    Check(Tolk_Output(L"Tolk worker thread test."), "screen reader output");
    Check(Tolk_Speak(L"Speech test."), "screen reader speech");
    Sleep(1500);
    Check(Tolk_Silence(), "screen reader silence");
    std::printf("Braille API result: %d (does not verify a physical display)\n",
                Tolk_Braille(L"Tolk worker thread test."));
  }
  Tolk_TrySAPI(true);
  Tolk_PreferSAPI(true);
  name = Tolk_DetectScreenReader();
  if (name && std::wcscmp(name, L"SAPI") == 0) {
    Check(Tolk_HasSpeech() && !Tolk_HasBraille(), "SAPI capabilities");
    Check(Tolk_Speak(L"Tolk SAPI worker thread test."), "SAPI speech");
    Sleep(100);
    std::printf("SAPI speaking status: %d\n", Tolk_IsSpeaking());
    Check(Tolk_Silence(), "SAPI silence");
    std::puts("PASS: real SAPI driver");
  } else {
    std::puts("SKIP: SAPI not available");
  }
  Tolk_TrySAPI(false);
  Tolk_PreferSAPI(false);
  Tolk_Unload();

  // No speech during stress: exercise lifetime and driver discovery together.
  std::vector<std::thread> callers;
  for (int n = 0; n < 4; ++n) callers.emplace_back([] {
    for (int i = 0; i < 50; ++i) {
      Tolk_Load();
      Tolk_IsLoaded();
      Tolk_DetectScreenReader();
      Tolk_HasSpeech();
      Tolk_Unload();
    }
  });
  for (size_t i = 0; i < callers.size(); ++i) callers[i].join();
  Check(!Tolk_IsLoaded(), "stress final state");
  std::puts("PASS: concurrent lifecycle stress, 200 load/unload cycles");
  return 0;
}
