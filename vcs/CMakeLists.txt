add_library(libvcs3 STATIC)

add_dependencies(libvcs3
    flatc
)

function(generate_fbs name)
    add_custom_command(
        OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/${name}.fb.h"
        COMMAND
            "${CMAKE_BINARY_DIR}/contrib/flatbuffers/flatc"
        ARGS
            -o ${CMAKE_CURRENT_BINARY_DIR}/api/fbs
            --cpp
            --cpp-ptr-type std::unique_ptr
            --cpp-std c++11
            --filename-suffix .fb
            --scoped-enums
            ${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/${name}.fbs
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/${name}.fbs
        WORKING_DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}/api/fbs
    )
endfunction(generate_fbs name)

generate_fbs(commit)
generate_fbs(index)
generate_fbs(renames)
generate_fbs(tag)
generate_fbs(tree)
generate_fbs(types)

target_include_directories(libvcs3 SYSTEM PUBLIC
    # Flatbuffers includes.
    "${CMAKE_BINARY_DIR}"
)

target_link_libraries(libvcs3 PUBLIC
    contrib-flatbuffers
    contrib-fmt
    contrib-sha
)

target_sources(libvcs3 PRIVATE
    "object/data.cpp"
    "object/hashid.cpp"
    "object/object.cpp"
    "object/serialize.cpp"
    "object/store.cpp"
    "store/memory.cpp"
)

target_sources(libvcs3 PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/commit.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/index.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/renames.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/tag.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/tree.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/types.fb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/commit.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/index.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/renames.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/tag.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/tree.fbs"
    "${CMAKE_CURRENT_SOURCE_DIR}/api/fbs/types.fbs"
)
