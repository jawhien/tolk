// Probe each real vendor driver separately on MTA, as used by Tolk's worker.
#include <windows.h>
#include <cstdio>
#include <thread>
#include "../src/ScreenReaderDriverJAWS.h"
#include "../src/ScreenReaderDriverWE.h"
#include "../src/ScreenReaderDriverNVDA.h"
#include "../src/ScreenReaderDriverSNova.h"
#include "../src/ScreenReaderDriverSA.h"
#include "../src/ScreenReaderDriverZT.h"
#include "../src/ScreenReaderDriverSAPI.h"

template<class Driver> void Probe() {
  std::thread([] {
    const HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
      std::printf("FAIL: COM initialization 0x%08lX\n", hr);
      return;
    }
    {
      Driver driver;
      const bool active = driver.IsActive();
      std::wprintf(L"%ls: %ls\n", driver.GetName(),
                   active ? L"ACTIVE" : L"UNAVAILABLE (output not tested)");
    }
    CoUninitialize();
  }).join();
}

int main() {
  Probe<ScreenReaderDriverJAWS>();
  Probe<ScreenReaderDriverWE>();
  Probe<ScreenReaderDriverNVDA>();
#ifndef _WIN64
  Probe<ScreenReaderDriverSNova>();
#endif
  Probe<ScreenReaderDriverSA>();
  Probe<ScreenReaderDriverZT>();
  Probe<ScreenReaderDriverSAPI>();
  return 0;
}
