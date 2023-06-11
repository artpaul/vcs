add_executable(vcs)
add_library(vcs-local)

target_link_libraries(vcs-local
    contrib-lmdb
    libvcs3
)

target_sources(vcs-local PRIVATE
    "local/bare.cpp"
    "local/config.cpp"
    "local/db.cpp"
    "local/revparse.cpp"
    "local/workspace.cpp"
    "local/worktree.cpp"
    "ui/color.cpp"
    "ui/printer.cpp"
)

target_link_libraries(vcs
    contrib-cxxopts
    libvcs3
    vcs-local
)

target_sources(vcs PRIVATE
    "main.cpp"
    "actions.cpp"
    "actions/branch.cpp"
    "actions/commit.cpp"
    "actions/config.cpp"
    "actions/diff.cpp"
    "actions/dump.cpp"
    "actions/git.cpp"
    "actions/init.cpp"
    "actions/log.cpp"
    "actions/reset.cpp"
    "actions/restore.cpp"
    "actions/show.cpp"
    "actions/status.cpp"
    "actions/switch.cpp"
)
