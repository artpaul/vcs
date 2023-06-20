add_library(contrib-tlsh)

target_include_directories(contrib-tlsh PUBLIC
    "include"
)

target_sources(contrib-tlsh PRIVATE
    "src/tlsh.cpp"
    "src/tlsh_impl.cpp"
    "src/tlsh_util.cpp"
)
