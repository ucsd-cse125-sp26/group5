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

add_executable(maze_logic_tests
    maze_logic_tests.cpp
)

target_link_libraries(maze_logic_tests PRIVATE
    GTest::gtest_main
    server_lib
)

add_executable(overworld_logic_tests
    overworld_logic_tests.cpp
)

target_link_libraries(overworld_logic_tests PRIVATE
    GTest::gtest_main
    server_lib
)

include(GoogleTest)
gtest_discover_tests(smoke_tests)
gtest_discover_tests(network_tests)
gtest_discover_tests(maze_logic_tests)
gtest_discover_tests(overworld_logic_tests)
