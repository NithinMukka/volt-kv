# Test executable discovering all unit tests
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

add_executable(voltkv_tests ${TEST_SOURCES})
target_link_libraries(voltkv_tests
    PRIVATE
        voltkv_lib
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(voltkv_tests)