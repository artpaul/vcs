add_executable(vcs)
add_library(vcs-local)

target_link_libraries(vcs-local
    libvcs3
)

target_sources(vcs-local PRIVATE
    "local/bare.cpp"
    "local/workspace.cpp"
    "local/worktree.cpp"
)

target_link_libraries(vcs
    contrib-cxxopts
    libvcs3
    vcs-local
)

target_sources(vcs PRIVATE
    "main.cpp"
    "actions.cpp"
    "actions/git.cpp"
    "actions/init.cpp"
    "actions/status.cpp"
)