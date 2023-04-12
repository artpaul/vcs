add_library(libvcs3 STATIC)

target_sources(libvcs3 PRIVATE
    "object/data.cpp"
    "object/hashid.cpp"
)

target_link_libraries(libvcs3 PUBLIC
    contrib-fmt
    contrib-sha
)
