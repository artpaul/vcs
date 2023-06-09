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
    absl::flat_hash_map
    absl::synchronization

    contrib-flatbuffers
    contrib-fmt
    contrib-gdelta
    contrib-libgit2
    contrib-lz4
    contrib-sha
    contrib-tlsh
    contrib-xxhash

    vcs-util
)

target_sources(libvcs3 PRIVATE
    "changes/changelist.cpp"
    "changes/path.cpp"
    "changes/revwalk.cpp"
    "changes/stage.cpp"
    "changes/validate.cpp"
    "common/config.cpp"
    "common/ignore.cpp"
    "common/revparse.cpp"
    "git/converter.cpp"
    "object/change.cpp"
    "object/commit.cpp"
    "object/data.cpp"
    "object/hashid.cpp"
    "object/object.cpp"
    "object/serialize.cpp"
    "object/store.cpp"
    "store/collect.cpp"
    "store/loose.cpp"
    "store/memory.cpp"
    "store/pack.cpp"
)

target_sources(libvcs3 PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/commit.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/index.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/renames.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/tag.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/tree.fb.h"
    "${CMAKE_CURRENT_BINARY_DIR}/api/fbs/types.fb.h"
)
