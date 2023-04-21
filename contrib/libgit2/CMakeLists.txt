add_library(contrib-libgit2 STATIC)

add_subdirectory(deps/pcre)
add_subdirectory(deps/http-parser)
add_subdirectory(deps/zlib)

target_include_directories(contrib-libgit2
    PUBLIC
        "include"
    PRIVATE
        "deps/pcre"
        "deps/http-parser"
        "deps/zlib"
        "src/libgit2"
        "src/util"
)

target_link_libraries(contrib-libgit2
    contrib-http-parser
    contrib-pcre
    contrib-zlib
)

file(GLOB LIBGIT2_SOURCES
    src/libgit2/*.c
    src/libgit2/*.h
    src/libgit2/streams/*.c
    src/libgit2/streams/*.h
    src/libgit2/transports/auth.c
    src/libgit2/transports/credential.c
    src/libgit2/transports/http.c
    src/libgit2/transports/httpclient.c
    src/libgit2/transports/local.c
    src/libgit2/transports/smart.c
    src/libgit2/transports/smart_pkt.c
    src/libgit2/transports/smart_protocol.c
    src/libgit2/transports/ssh.c
    src/libgit2/transports/git.c
    src/libgit2/xdiff/*.c
    src/libgit2/xdiff/*.h
    src/util/*.h
    src/util/*.c
    src/util/allocators/*.h
    src/util/allocators/*.c
    src/util/unix/*.h
    src/util/unix/*.c
    src/util/hash/sha.h
    src/util/hash/builtin.c
    src/util/hash/collisiondetect.c
    src/util/hash/rfc6234/sha.h
    src/util/hash/rfc6234/sha224-256.c
    src/util/hash/sha1dc/sha1.h
    src/util/hash/sha1dc/sha1.c
    src/util/hash/sha1dc/ubc_check.c
)

set_source_files_properties(${LIBGIT2_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-implicit-function-declaration")

target_sources(contrib-libgit2 PRIVATE
    ${LIBGIT2_SOURCES}
)
