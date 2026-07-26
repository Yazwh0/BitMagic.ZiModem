# BitMagic.ZiModem

A native (C/C++) + managed (C#) wrapper around [zimodem](https://github.com/bozimmerman/zimodem)
— Bo Zimmerman's ESP32/ESP8266 firmware that makes a Wi-Fi-connected board behave like a
Hayes-compatible modem for retro computers — so the same firmware logic can run as a
library on Windows or Linux, driven from a C# application, with no physical ESP32
involved.

The vendored firmware itself is never modified in place; a copy-and-patch pipeline
compiles it against a host-side Hardware Abstraction Layer (real sockets, filesystem,
and clocks standing in for `WiFiClient`, `SPIFFS`, `millis()`, etc.). See
[`docs/native-wrapper-spec.md`](docs/native-wrapper-spec.md) for the full architecture,
design rationale, and current implementation status.

## Repository layout

```
external/zimodem/     git submodule, the vendored firmware, pinned to tag 4.0.2 (the
                       actual shipped version, not the tip of upstream's dev branch)
patches/zimodem/       mechanical portability patches applied on top of the submodule
tools/Vendoring/       .NET tool that applies the patches into build/vendor/zimodem-src
tools/ZiModem.Console/ interactive console app for manually testing the wrapper
native/hal/            the Hardware Abstraction Layer (host code, no firmware code)
native/wrapper/        compiles the vendored+patched firmware, and the public C ABI
managed/ZiModem.Net/   C# P/Invoke binding over the C ABI
docs/                  design spec and architecture notes
```

## Prerequisites

- **.NET 8 SDK** (or newer) — required on every platform; the vendoring tool and the
  managed side both depend on it.
- **CMake 3.20+**
- A C++17 compiler:
  - **Windows**: Visual Studio with the "Desktop development with C++" workload (CMake
    and Ninja ship bundled with it). Build commands below assume a Developer Command
    Prompt / `vcvarsall.bat` environment.
  - **Linux**: `gcc`/`g++` (11+) and `make`. Verified working under WSL/Ubuntu 22.04.
- Git, with submodules initialized (`git submodule update --init`).

## Building

The native build always regenerates `build/vendor/zimodem-src` from the submodule +
patches as part of every build (see the spec, §6) — you don't run the vendoring tool
by hand unless you're iterating on a patch.

**Windows** (from a Developer Command Prompt, or after running `vcvarsall.bat x64`):

```
cmake -S native -B build/native -G Ninja
cmake --build build/native -j
```

**Linux**:

```
cmake -S native -B build/native
cmake --build build/native -j$(nproc)
```

Both produce a `zimodem_host.dll` / `libzimodem_host.so` (the public C ABI) plus three
native test binaries, all under `build/native/bin/` (or `build/native-linux/bin/` if
you keep a separate build directory per OS on the same machine — useful under WSL,
since the two platforms' CMake caches can't share one directory).

### Running the native tests

```
ctest --test-dir build/native --output-on-failure
```

80 tests as of this writing, covering the HAL in isolation, the vendored firmware
driven directly, and the C ABI's full lifecycle (including a real TCP dial and
bidirectional data passthrough).

### Building the managed side

```
dotnet build managed/ZiModem.Net/ZiModem.Net.csproj
dotnet build tools/ZiModem.Console/ZiModem.Console.csproj
```

`ZiModem.Net.csproj` copies the native binary from the CMake build output directory
automatically (a dev-time convenience — see the spec §9 for the NuGet packaging that's
still open).

## Trying it out: the test console

```
dotnet run --project tools/ZiModem.Console
```

This is a real, raw terminal, not a line editor — every keystroke (including backspace)
forwards to the modem immediately, and there's no local echo of any kind: everything
you see is genuinely what the firmware sent back. Type AT commands directly (`AT`,
`ATDT127.0.0.1:<port>`, `AT+IRC`, ...). `Ctrl+Q` quits, `Ctrl+D` prints the data
directory. Useful flags:

- `--echo <port>` (repeatable) — starts a loopback TCP echo server at startup, so you
  can `ATDT` into it without a separate tool.
- `--datadir <path>` — pins a persistent config/phonebook location instead of a fresh
  temp directory every run.
- `--log` / `-v` / `--verbose` — also prints the firmware's own debug log lines (off by
  default; noisy).

## Updating the vendored firmware

```
git submodule update --remote external/zimodem
dotnet run --project tools/Vendoring -- vendor   # re-applies patches, fails loudly if any don't apply cleanly
ctest --test-dir build/native
```

See the spec's §6.4 for what to do if a patch needs rebasing.
