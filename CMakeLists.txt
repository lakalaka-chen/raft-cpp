cmake_minimum_required(VERSION 3.20)
project(raft VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE "Debug")

enable_testing()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

set(INCLUDE_DIR
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/third_party/googletest/googletest/include)

include_directories("~/CppSources/spdlog/include" "~/CppSources/tcp/include" "~/CppSources/rpc/include")
link_directories("~/CppSources/libs")

include_directories(${INCLUDE_DIR})
include_directories(BEFORE src)


file(GLOB_RECURSE INCLUDE_FILES "${PROJECT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE SOURCE_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp")


add_library(${PROJECT_NAME}
        ${INCLUDE_FILES}
        ${SOURCE_FILES})

add_subdirectory(tests)
add_subdirectory(third_party)