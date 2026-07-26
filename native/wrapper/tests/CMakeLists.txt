# Catch2 is fetched once in native/CMakeLists.txt (see the comment there for why).

# Drives zimodem_core directly (setup()/loop() + zimodem_hal), proving the vendored/
# patched sketch behaves correctly -- see docs/native-wrapper-spec.md section 11.2.
add_executable(zimodem_core_tests
    test_smoke.cpp
    test_protocol.cpp
)
target_link_libraries(zimodem_core_tests PRIVATE zimodem_core zimodem_hal Catch2::Catch2WithMain)

# Drives the public C ABI (zimodem_host) the same way a real consumer (e.g. the C#
# binding) would -- only the exported zimodem_host_* functions and plain C callbacks,
# never zimodem_hal's instance-internal state directly. zimodem_hal is linked only for
# this test's own self-contained loopback socket (see test_c_abi.cpp's header comment).
add_executable(zimodem_host_tests
    test_c_abi.cpp
)
target_link_libraries(zimodem_host_tests PRIVATE zimodem_host zimodem_hal Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(zimodem_core_tests)
catch_discover_tests(zimodem_host_tests)
