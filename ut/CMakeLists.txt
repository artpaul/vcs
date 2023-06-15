add_executable(libvcs3-ut)

target_link_libraries(libvcs3-ut
    contrib-gtest
    libvcs3
)

target_sources(libvcs3-ut PRIVATE
    "main.cpp"
)

target_sources(libvcs3-ut PRIVATE
    "lib/graph.cpp"
)

# Unit tests.
target_sources(libvcs3-ut PRIVATE
    "ut_changelist.cpp"
    "ut_config.cpp"
    "ut_data.cpp"
    "ut_hashid.cpp"
    "ut_object.cpp"
    "ut_path.cpp"
    "ut_revparse.cpp"
    "ut_revwalk.cpp"
    "ut_stage.cpp"
    "ut_store.cpp"
    "ut_utils.cpp"
)
