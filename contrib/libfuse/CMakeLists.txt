add_library(contrib-libfuse STATIC
    "lib/buffer.c"
    "lib/compat.c"
    "lib/cuse_lowlevel.c"
    "lib/fuse.c"
    "lib/fuse_log.c"
    "lib/fuse_loop.c"
    "lib/fuse_loop_mt.c"
    "lib/fuse_lowlevel.c"
    "lib/fuse_opt.c"
    "lib/fuse_signals.c"
    "lib/fuse_versionscript"
    "lib/helper.c"
    "lib/modules/subdir.c"
    "lib/mount.c"
    "lib/mount_util.c"
)

# FUSE setup
target_compile_definitions(contrib-libfuse PUBLIC
    -DHAVE_LIBFUSE_PRIVATE_CONFIG_H
    -DFUSE_USE_VERSION=350
    -DFUSERMOUNT_DIR="/usr/local/bin"
)

target_include_directories(contrib-libfuse PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# target_link_options(contrib-libfuse PUBLIC "-Wl,--version-script,lib/fuse_versionscript")
