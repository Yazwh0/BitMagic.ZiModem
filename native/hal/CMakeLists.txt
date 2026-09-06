add_library(zimodem_hal STATIC
    src/timing.cpp
    src/pins.cpp
    src/log.cpp
    src/fs_root.cpp
    src/arduino_globals.cpp
    src/arduino_fs.cpp
    src/net.cpp
    src/tls.cpp
    src/serial_port.cpp
)

# The second path lets `#include <FS.h>`, `#include <WiFi.h>`, etc. (angle-bracket,
# bare filename -- exactly how a real Arduino core's library directories are found)
# resolve directly to our compat headers, matching how the vendored sketch expects its
# environment to be set up. Our own code always includes these as "zimodem_hal/arduino/X.h".
target_include_directories(zimodem_hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/zimodem_hal/arduino
)

if(WIN32)
    target_link_libraries(zimodem_hal PUBLIC ws2_32)
endif()

# src/tls.cpp only -- mbedTLS headers are hidden behind TlsSocket's pimpl, so this stays
# PRIVATE and nothing downstream (zimodem_core / the sketch TU) sees an mbedTLS include.
# CMake still propagates it as a link-only dependency to the final executables/DLL
# because zimodem_hal is a static library.
target_link_libraries(zimodem_hal PRIVATE mbedtls)

if(ZIMODEM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
