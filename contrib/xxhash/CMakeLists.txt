add_library(contrib-xxhash STATIC
    "xxhash.c"
)

target_compile_definitions(contrib-xxhash
    PUBLIC XXH_INLINE_ALL
)
