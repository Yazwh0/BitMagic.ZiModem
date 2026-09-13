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
// Must come before Arduino.h below: <winsock2.h> (pulled in transitively by
// wifisshclient.h -> libssh2.h once INCLUDE_SSH is compiled in) drags in the full
// <windows.h>, including <winuser.h> -- which this SDK includes unconditionally
// (WIN32_LEAN_AND_MEAN only trims a later, smaller set of headers, not this one).
// winuser.h declares `typedef struct tagINPUT {...} INPUT, *LPINPUT;`; Arduino.h's
// `#define INPUT 0`/`#define OUTPUT 1` (the pinMode direction constants) would corrupt
// that declaration if seen first. Getting Winsock's own headers fully parsed here, while
// the real Windows INPUT/OUTPUT identifiers are still just themselves, means Arduino.h's
// macros only ever shadow the plain-value meaning for the rest of this file -- nothing
// here ever needs the Windows INPUT struct type itself.
#ifdef _WIN32
#include <winsock2.h>
#else
// Same reasoning as the _WIN32 branch above, different collision: rt_clock.ino
// unconditionally `#define`s its own function-like `htonl(x)` fallback macro (no guard --
// upstream just always provides one rather than trusting the platform to have a real one).
// That's harmless as long as nothing later needs the *real* glibc htonl/htons/ntohl/ntohs
// function declarations -- but wifisshclient.ino (INCLUDE_SSH) does, via its own raw
// socket code needing <netinet/in.h>. Included there directly, `#define htonl(x) (...)`
// (already in effect by then, rt_clock.ino comes first in the include list below) would
// corrupt glibc's `extern uint32_t htonl(uint32_t __hostlong)` prototype the exact same
// way Arduino.h's INPUT/OUTPUT corrupts winuser.h on Windows. Getting the real POSIX
// declarations parsed here, before rt_clock.ino's macro exists, avoids it entirely --
// callers afterward just get the macro's byte-swap expansion instead of a real call,
// which is what upstream intended anyway.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

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
// wifisshclient.ino provides WiFiSSHClient's implementation, entirely under its own
// `#if INCLUDE_SSH` guard -- safe to include unconditionally regardless of the flag.
// wificlientnode.ino (above) is its only consumer (constructNode() picks it when a dial
// has FLAG_SECURE + a username).
#include "wifisshclient.ino"
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
