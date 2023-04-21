add_executable(libvcs3-ut)

target_sources(libvcs3-ut PRIVATE
    "main.cpp"
)

target_sources(libvcs3-ut PRIVATE
    "ut_changelist.cpp"
    "ut_data.cpp"
    "ut_hashid.cpp"
    "ut_object.cpp"
    "ut_stage.cpp"
    "ut_store.cpp"
    "ut_utils.cpp"
)

target_link_libraries(libvcs3-ut
    contrib-gtest
    libvcs3
)
