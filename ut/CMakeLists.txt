add_executable(libvcs3-ut)

target_sources(libvcs3-ut PRIVATE
    "main.cpp"
)

target_sources(libvcs3-ut PRIVATE
    "data_ut.cpp"
    "hashid_ut.cpp"
    "object_ut.cpp"
)

target_link_libraries(libvcs3-ut
    contrib-gtest
    libvcs3
)
