include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(smoke_tests
    smoke_test.cpp
)

target_link_libraries(smoke_tests PRIVATE
    GTest::gtest_main
    shared
)

include(GoogleTest)
gtest_discover_tests(smoke_tests)
