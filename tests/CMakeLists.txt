add_executable(smoke_tests
    smoke_test.cpp
)

target_link_libraries(smoke_tests PRIVATE
    GTest::gtest_main
    shared
)

include(GoogleTest)
gtest_discover_tests(smoke_tests)
