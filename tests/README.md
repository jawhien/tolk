# Worker thread integration checks

Build the core DLL with the repository's `src/Makefile` in an MSVC developer
prompt for the desired architecture. JNI is optional; omit `/D_WITH_JNI` from
`CFLAGS` if no JDK is installed. Put the matching `libs/x86` or `libs/x64` vendor
DLLs next to `Tolk.dll`.

From the DLL output directory, build and run the integration test:

```bat
cl /nologo /EHsc /W4 /DUNICODE /Fe:WorkerThread.exe PATH\TO\tests\WorkerThread.cpp Tolk.lib Ole32.lib
WorkerThread.exe
```

The test makes short audible requests through the detected screen reader and
SAPI. It checks caller COM isolation (including OLE), cross-thread lifecycle,
return values, SAPI switching, and 200 concurrent load/unload cycles. Missing
drivers are reported as unavailable; successful braille API calls do not verify
a physical display. Run under both x86 and x64.

`DriverProbe.cpp` checks each vendor driver's creation, detection and destruction
on an MTA thread independently of Tolk's selection order. Compile it with the
`ScreenReaderDriver*.cpp`, `fsapi.c`, `wineyes.c`, and `zt.c` source files, linking
`User32.lib Ole32.lib OleAut32.lib`. An unavailable driver is not an output test.
