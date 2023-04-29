cmake_minimum_required(VERSION 3.20)

option(BUILD_TESTS "Build tests" ON)

project(vcs)

include_directories(.)

find_package(Threads REQUIRED)

# Setup C++ standard.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wnon-virtual-dtor -Werror")

add_subdirectory(contrib)
add_subdirectory(vcs)
add_subdirectory(util)

# Tests.
if(BUILD_TESTS)
    add_subdirectory(contrib/gtest)
    add_subdirectory(contrib/benchmark)
    add_subdirectory(ut)

    # Local test projects.
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/junk")
        add_subdirectory(junk)
    endif()
endif()
