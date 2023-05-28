cmake_minimum_required(VERSION 3.20)

option(BUILD_TESTS "Build tests" ON)

project(vcs)

include_directories(.)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Third parties come with their own setup of
# C++ standard and compiler options.
add_subdirectory(contrib)

# Setup C++ standard for the project.
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wnon-virtual-dtor -Werror")

if("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    message(STATUS "Enabling LTO...")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_subdirectory(cmd)
add_subdirectory(vcs)
add_subdirectory(util)

# Tests.
if(BUILD_TESTS)
    add_subdirectory(contrib/gtest)
    add_subdirectory(contrib/benchmark)
    add_subdirectory(ut)

    # Local test projects.
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/junk")
        add_subdirectory(junk EXCLUDE_FROM_ALL)
    endif()
endif()
