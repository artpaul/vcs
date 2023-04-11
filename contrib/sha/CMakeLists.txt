add_library(contrib-sha STATIC
    "sha.c"
    "sha1/sha1.c"
    "sha256/sha256.c"
)

set_target_properties(contrib-sha
    PROPERTIES C_STANDARD 90
)
