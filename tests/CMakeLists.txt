add_executable(smoke_tests
    smoke_test.cpp
)

target_link_libraries(smoke_tests PRIVATE
    GTest::gtest_main
    shared
)

add_executable(network_tests
    network_tests.cpp
)

target_link_libraries(network_tests PRIVATE
    GTest::gtest_main
    server_lib
)

include(GoogleTest)
gtest_discover_tests(smoke_tests)
gtest_discover_tests(network_tests)
