# add_executable(
#   hello_test
#   tests/hello_test.cc
# )
# target_link_libraries(
#   hello_test
#   gtest_main
# )

# gtest_discover_tests(hello_test)
enable_testing()

cmake_minimum_required(VERSION 3.0.0)

set(TEST_PROJECT_NAME
    NCKDTests
)
set(LIBRARY_TESTS_SOURCE
    hello_test.cc
    pg_test.cc
)

project(${TEST_PROJECT_NAME})

enable_testing()

if (NOT (TARGET gtest AND TARGET gtest_main))
    message(FATAL_ERROR "gtest and gtest_main targets NOT found")
endif()

add_executable(${TEST_PROJECT_NAME} ${LIBRARY_TESTS_SOURCE})
target_link_libraries(
    ${TEST_PROJECT_NAME}
    gtest
    gtest_main
    ${NCKD_LIBRARIES} nlohmann_json::nlohmann_json Boost::program_options PostgreSQL::PostgreSQL
)
add_test(${TEST_PROJECT_NAME} ${TEST_PROJECT_NAME} NckdPGTest NckdPGTest)
