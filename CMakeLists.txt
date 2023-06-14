cmake_minimum_required(VERSION 3.20)

option(BUILD_TESTS "Build tests" ON)

project(vcs C CXX)

include_directories(.)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Grpc flags.
set(protobuf_INSTALL OFF)
set(protobuf_BUILD_TESTS OFF)
set(BUILD_SHARED_LIBS OFF)
set(gRPC_BUILD_CSHARP_EXT OFF)
set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF)
set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF)
set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF)
set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF)
set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF)
set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF)
set(gRPC_BUILD_TESTS OFF)
set(gRPC_INSTALL OFF)

# Third parties come with their own setup of
# C++ standard and compiler options.
add_subdirectory(contrib EXCLUDE_FROM_ALL)

# Grpc
set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_GRPC_GRPCPP grpc++)
get_target_property(Protobuf_INCLUDE_DIRS protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)

# Setup C++ standard for the project.
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wnon-virtual-dtor -Werror")

# Disable annoying gcc warnings.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-field-initializers -Wno-implicit-fallthrough")

    if("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
    endif()
endif()

add_subdirectory(cmd)
add_subdirectory(vcs)
add_subdirectory(util)

# Tests.
if(BUILD_TESTS)
    add_subdirectory(ut)

    # Local test projects.
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/junk")
        add_subdirectory(junk EXCLUDE_FROM_ALL)
    endif()
endif()
