# zimodem_core compiles the vendored+patched sketch (build/vendor/zimodem-src) against
# zimodem_hal's Arduino/ESP-IDF compat surface. See docs/native-wrapper-spec.md section 7.

add_library(zimodem_core STATIC
    src/zimodem_core.cpp
)

target_include_directories(zimodem_core PRIVATE
    ${ZIMODEM_VENDOR_DIR}/zimodem
)

target_compile_definitions(zimodem_core PRIVATE ZIMODEM_HOST_BUILD=1)

target_link_libraries(zimodem_core PUBLIC zimodem_hal)

add_dependencies(zimodem_core zimodem_vendor)

# zimodem_host: the public C ABI shared library. See docs/native-wrapper-spec.md section 8.
add_library(zimodem_host SHARED
    src/zimodem_host.cpp
)

target_include_directories(zimodem_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(zimodem_host PRIVATE zimodem_core zimodem_hal)

# PRIVATE, not PUBLIC: only zimodem_host.cpp's own translation unit switches convention.
# Consumers linking this target (the tests) are meant to keep the platform default -- and
# the top-level CMakeLists forbids ZIMODEM_HOST_MSABI + ZIMODEM_BUILD_TESTS together.
if(ZIMODEM_HOST_MSABI)
    target_compile_definitions(zimodem_host PRIVATE ZIMODEM_HOST_MSABI=1)
endif()

if(ZIMODEM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
