add_library(contrib-lz4 STATIC
    "lz4.c"
)

set_property(TARGET contrib-lz4 PROPERTY POSITION_INDEPENDENT_CODE ON)
