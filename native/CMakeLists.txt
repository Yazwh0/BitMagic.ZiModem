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
# Regenerates build/vendor/zimodem-src from external/zimodem + patches/zimodem on every
# build. See docs/native-wrapper-spec.md section 6. This is deliberately ALL (always
# runs), not a one-time configure step, so editing a patch and rebuilding picks it up.
add_custom_target(zimodem_vendor ALL
    COMMAND dotnet run --project "${ZIMODEM_REPO_ROOT}/tools/Vendoring" -- vendor
    WORKING_DIRECTORY "${ZIMODEM_REPO_ROOT}"
    COMMENT "Vendoring external/zimodem (copy + apply patches/zimodem/SERIES)"
    VERBATIM
)

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

add_subdirectory(hal)

add_subdirectory(wrapper)

# zimodem_host (the public C ABI shared library) lands here once zimodem_core compiles
# cleanly -- see docs/native-wrapper-spec.md section 8.
