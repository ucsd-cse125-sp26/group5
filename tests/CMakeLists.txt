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

add_executable(maze_generation_tests
    maze_generation_tests.cpp
)

target_link_libraries(maze_generation_tests PRIVATE
    GTest::gtest_main
    server_lib
)

add_executable(maze_trigger_tests
    maze_trigger_tests.cpp
)

target_link_libraries(maze_trigger_tests PRIVATE
    GTest::gtest_main
    server_lib
)

add_executable(maze_camera_tests
    maze_camera_tests.cpp
)

add_executable(maze_spirit_control_tests
    maze_spirit_control_tests.cpp
)

target_link_libraries(maze_camera_tests PRIVATE
    GTest::gtest_main
    server_lib
)

target_link_libraries(maze_spirit_control_tests PRIVATE
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

add_executable(overworld_maze_puzzle_tests
    overworld_maze_puzzle_tests.cpp
)

add_executable(tangram_layout_tests
    tangram_layout_tests.cpp
)

target_link_libraries(tangram_layout_tests PRIVATE
    GTest::gtest_main
    shared
)

target_link_libraries(overworld_logic_tests PRIVATE
    GTest::gtest_main
    server_lib
)

target_link_libraries(overworld_maze_puzzle_tests PRIVATE
    GTest::gtest_main
    server_lib
)

include(GoogleTest)
gtest_discover_tests(smoke_tests)
gtest_discover_tests(network_tests)
gtest_discover_tests(maze_generation_tests)
gtest_discover_tests(maze_trigger_tests)
gtest_discover_tests(maze_camera_tests)
gtest_discover_tests(maze_spirit_control_tests)
gtest_discover_tests(maze_logic_tests)
gtest_discover_tests(overworld_logic_tests)
gtest_discover_tests(overworld_maze_puzzle_tests)
gtest_discover_tests(tangram_layout_tests)
