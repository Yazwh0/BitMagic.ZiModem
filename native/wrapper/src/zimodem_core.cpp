// Compiles the vendored, patched zimodem sketch as a single translation unit -- matching
// how the Arduino builder itself treats a multi-.ino sketch (see
// docs/native-wrapper-spec.md section 3). Arduino.h is included first because a real
// Arduino toolchain would make its contents implicitly available to every .ino file
// without an explicit #include; zimodem.ino relies on that exact assumption.
//
// The Arduino builder auto-concatenates *every* .ino file in the sketch folder into one
// translation unit -- not just the ones zimodem.ino itself `#include`s the .h side of.
// zimodem.ino only #includes each module's .h (for declarations); the matching .ino
// (the function bodies) is compiled in purely because it sits in the same sketch
// directory. We have to include all of them explicitly here for the same effect.
//
// The Arduino builder also auto-generates forward declarations for every function
// defined in the sketch (a ctags-based prototype pass) and inserts them before any
// function body, so define-after-use across files (or even within one file) just works.
// We don't have that pass, so include order matters here in a way it doesn't for a real
// Arduino build -- any remaining "identifier not found" points at a genuine ordering gap,
// resolved with a forward declaration below rather than by reordering (reordering just
// moves the problem: something else may rely on the original relative order).
#include "zimodem_hal/arduino/Arduino.h"

// Forward declarations for functions zimodem.ino calls before its own textual
// definition of them (setup()/loop()/checkBaudChange() all appear above the functions
// they call). A real Arduino build's auto-prototype pass would generate these; see the
// note above for why we do it by hand instead of reordering the #includes below.
static void changeBaudRate(int baudRate);
static void flushSerial();
static void initSDShell();
char lc(char c);
static void rawLogPrint(const char* str);
static void rawLogPrintln(const char* str);
static void rawLogPrintf(const char* format, ...);

#include "zimodem.ino"
#include "pet2asc.ino"
#include "rt_clock.ino"
#include "filelog.ino"
#include "serout.ino"
#include "connSettings.ino"
#include "wificlientnode.ino"
#include "phonebook.ino"
#include "wifiservernode.ino"
#include "zstream.ino"
#include "proto_http.ino"
#include "proto_ftp.ino"
#include "zconfigmode.ino"
#include "zcommand.ino"
#include "zprint.ino"
#include "zircmode.ino"

// zbrowser.ino provides the real initSDShell() under `#if INCLUDE_SD_SHELL` and a
// no-op fallback definition of the same function under `#else` -- since our host build
// disables SD_SHELL (patches/zimodem/0001), only that tiny fallback actually compiles
// in from this file.
#include "zbrowser.ino"
