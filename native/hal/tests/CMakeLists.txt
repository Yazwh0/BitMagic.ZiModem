# Catch2 is fetched once in native/CMakeLists.txt (see the comment there for why).

add_executable(zimodem_hal_tests
    test_timing.cpp
    test_pins.cpp
    test_log.cpp
    test_string.cpp
    test_stream.cpp
    test_ipaddress.cpp
    test_fs.cpp
    test_net.cpp
    test_wifi.cpp
    test_serial.cpp
)
target_link_libraries(zimodem_hal_tests PRIVATE zimodem_hal Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(zimodem_hal_tests)
