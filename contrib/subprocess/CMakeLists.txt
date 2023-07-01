add_library(contrib-subprocess)

find_package(Threads REQUIRED)

target_include_directories(contrib-subprocess PUBLIC
    ./
)

target_sources(contrib-subprocess PRIVATE
    "subprocess/environ.cpp"
    "subprocess/pipe.cpp"
    "subprocess/process_builder.cpp"
    "subprocess/shell_utils.cpp"
    "subprocess/utf8_to_utf16.cpp"
)

if(UNIX)
    target_sources(contrib-subprocess PRIVATE
        "subprocess/process_builder_posix.cpp"
    )
elseif(WIN32)
    target_sources(contrib-subprocess PRIVATE
        "subprocess/process_builder_windows.cpp"
    )
endif()

if(MSVC)
    target_compile_options(contrib-subprocess PUBLIC -Zc:__cplusplus)
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_link_libraries(contrib-subprocess PUBLIC stdc++fs)

    if(NOT WIN32)
        target_link_libraries(contrib-subprocess PUBLIC dl)
    endif()
endif()

target_link_libraries(contrib-subprocess PUBLIC Threads::Threads)
