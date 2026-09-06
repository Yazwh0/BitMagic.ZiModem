cmake_minimum_required(VERSION 3.20)
project(ZiModemHost LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# zimodem_host (a SHARED library) links zimodem_core and zimodem_hal, which are built as
# static libraries. On Linux, object files going into a shared library must be position-
# independent, but CMake doesn't compile static libraries with -fPIC by default (it's an
# opt-in cost). Without this, linking zimodem_host fails with "recompile with -fPIC" --
# Windows has no such requirement for DLLs, which is why this only shows up there.
# Applying it globally (rather than per-target) is simplest and has no downside for the
# executables/shared libs that already need it anyway.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Statically link the MSVC runtime into every target here. zimodem_host.dll gets
# xcopy-deployed next to a .NET host (BitMagic.X16Emulator) whose process has none of
# msvcp140[d].dll / vcruntime140[d].dll / ucrtbase[d].dll on its DLL search path -- and a
# Debug build pulls the *non-redistributable* debug CRT, so the load fails with
# 0x8007007E "one of its dependencies could not be found". Static = nothing to find.
# The C ABI passes no C++ types and does no cross-boundary malloc/free (see
# zimodem_host.h), so a private CRT in the DLL is safe. Generator expression so Debug
# gets /MTd and Release /MT. Honoured because cmake_minimum_required(3.20) sets
# CMP0091=NEW.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# All executables and shared libraries land in one directory (rather than each target's
# own subdirectory under build/native) so a test .exe can find zimodem_host.dll next to
# it at runtime -- Windows searches the executable's own directory first, and zimodem_host
# lives in a different CMake subdirectory (native/wrapper) than the test binaries that
# link it (native/wrapper/tests).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

get_filename_component(ZIMODEM_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(ZIMODEM_BUILD_TESTS "Build the HAL/wrapper unit test suites" ON)

# --- Vendoring -----------------------------------------------------------
# Regenerates build/vendor/zimodem-src from external/zimodem + patches/zimodem, but only
# when one of those inputs actually changed. See docs/native-wrapper-spec.md section 6.
# Dependency-tracked via a stamp file rather than an unconditional ALL target: the DEPENDS
# list below makes Ninja re-run vendoring when a submodule file or patch is edited, and
# skip it (no dotnet startup, no git apply) when nothing changed -- which matters because
# zimodem_vendor sits ahead of zimodem_core in every single build (including ones where
# nothing native changed at all, e.g. building a C# project that merely references
# ZiModemHost.vcxproj). CONFIGURE_DEPENDS makes CMake re-glob these directories on every
# regenerate so added/removed files (a new patch, a submodule update) are picked up
# without a manual reconfigure; in-place edits to an existing file are caught by the
# DEPENDS list itself, no reconfigure needed.
file(GLOB_RECURSE ZIMODEM_VENDOR_INPUTS CONFIGURE_DEPENDS
    "${ZIMODEM_REPO_ROOT}/external/zimodem/*"
    "${ZIMODEM_REPO_ROOT}/patches/zimodem/*"
)

set(ZIMODEM_VENDOR_STAMP "${CMAKE_BINARY_DIR}/zimodem_vendor.stamp")

add_custom_command(
    OUTPUT "${ZIMODEM_VENDOR_STAMP}"
    COMMAND dotnet run --project "${ZIMODEM_REPO_ROOT}/tools/Vendoring" -- vendor
    COMMAND ${CMAKE_COMMAND} -E touch "${ZIMODEM_VENDOR_STAMP}"
    WORKING_DIRECTORY "${ZIMODEM_REPO_ROOT}"
    DEPENDS ${ZIMODEM_VENDOR_INPUTS}
    COMMENT "Vendoring external/zimodem (copy + apply patches/zimodem/SERIES)"
    VERBATIM
)

add_custom_target(zimodem_vendor ALL DEPENDS "${ZIMODEM_VENDOR_STAMP}")

set(ZIMODEM_VENDOR_DIR "${ZIMODEM_REPO_ROOT}/build/vendor/zimodem-src" CACHE PATH
    "Location of the vendored+patched zimodem source tree" FORCE)

# Must come before any add_subdirectory() containing tests -- CTest only picks up
# add_test()/catch_discover_tests() registrations made in a scope where enable_testing()
# has already run in that directory or an ancestor.
#
# Catch2 is fetched once here (not repeated per test directory): FetchContent_MakeAvailable
# only runs Catch2's own CMakeLists.txt -- which appends its extras/ dir to
# CMAKE_MODULE_PATH so `include(Catch)` can find catch_discover_tests() -- the first time
# it's called for a given name. Declaring/fetching it again from a sibling directory
# (hal/tests vs wrapper/tests) is a no-op for the module path, since CMAKE_MODULE_PATH
# set in one subdirectory scope doesn't propagate to its siblings, only to scopes added
# after it in an ancestor. Doing it once here means every subdirectory added below
# inherits the module path correctly.
if(ZIMODEM_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# --- mbedTLS (bundled) -------------------------------------------------------
# Backs zimodem_hal's TlsSocket / the Arduino WiFiClientSecure shim. The vendored sketch
# asks for a TLS client whenever it dials a secure host (FLAG_SECURE, https:// URLs);
# patches/zimodem/0011 routes the ZIMODEM_HOST build into the same createWiFiClient()
# path the ESP32 firmware uses. Fetched and static-linked (rather than depending on a
# system TLS library) so the Windows and Linux builds are byte-for-byte the same shape
# and zimodem_host.dll keeps the "no external runtime deps" property that
# CMAKE_MSVC_RUNTIME_LIBRARY above is also protecting.
#
# Pinned to the 2.28 LTS line on purpose: its CMake build needs no Python/Jinja code
# generation step (GEN_FILES defaults OFF, generated sources are committed) and has no
# `framework` git submodule, so a plain shallow FetchContent clone builds cleanly on a
# machine with neither Python nor submodule fetching. Trade-off: TLS 1.2 is the ceiling
# (no TLS 1.3) -- fine for dialling BBS/telnet-over-TLS and typical HTTPS. See
# docs/native-wrapper-spec.md "ESP32 parity".
include(FetchContent)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
# mbedTLS 2.28's top-level cmake_minimum_required() predates CMake 3.5, which CMake 4.x
# refuses outright (only warns on 3.x). This lets the bundled build configure under a
# 4.x CMake without editing vendored files; harmless on older CMake.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
    MbedTLS
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v2.28.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(MbedTLS)
unset(CMAKE_POLICY_VERSION_MINIMUM)

add_subdirectory(hal)

add_subdirectory(wrapper)

# zimodem_host (the public C ABI shared library) lands here once zimodem_core compiles
# cleanly -- see docs/native-wrapper-spec.md section 8.
