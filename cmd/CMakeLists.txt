add_executable(vcs)
add_library(vcs-local)

if("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    message(STATUS "Enabling LTO...")
    set_property(TARGET vcs PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_link_libraries(vcs-local
    contrib-lmdb
    libvcs3
)

target_sources(vcs-local PRIVATE
    "local/bare.cpp"
    "local/db.cpp"
    "local/fetch.cpp"
    "local/index.cpp"
    "local/workspace.cpp"
    "local/worktree.cpp"
)

target_link_libraries(vcs
    contrib-cxxopts
    contrib-subprocess
    vcs-fs
    vcs-local
)

target_sources(vcs PRIVATE
    "ui/color.cpp"
    "ui/pager.cpp"
    "ui/printer.cpp"
)

target_sources(vcs PRIVATE
    "main.cpp"
    "actions.cpp"
    "actions/branch.cpp"
    "actions/commit.cpp"
    "actions/config.cpp"
    "actions/diff.cpp"
    "actions/dump.cpp"
    "actions/fetch.cpp"
    "actions/git.cpp"
    "actions/init.cpp"
    "actions/log.cpp"
    "actions/mount.cpp"
    "actions/remote.cpp"
    "actions/reset.cpp"
    "actions/restore.cpp"
    "actions/show.cpp"
    "actions/status.cpp"
    "actions/switch.cpp"
)
