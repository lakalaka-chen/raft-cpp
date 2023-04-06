
cmake_minimum_required(VERSION 3.10)

include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES "${PROJECT_SOURCE_DIR}/tests/*test.cpp")
#
foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_filename ${test_source} NAME)
    string(REPLACE ".cpp" "" test_name ${test_filename})
    add_executable(${test_name} EXCLUDE_FROM_ALL ${test_source})
    target_link_libraries(${test_name} PUBLIC gtest gmock_main rpc tcp timer ${PROJECT_NAME} )
endforeach()
