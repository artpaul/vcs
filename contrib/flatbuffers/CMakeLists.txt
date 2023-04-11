add_executable(flatc)
add_library(contrib-flatbuffers STATIC)

target_include_directories(contrib-flatbuffers PUBLIC
    "include"
)

target_sources(contrib-flatbuffers PRIVATE
    "src/code_generators.cpp"
    "src/idl_gen_fbs.cpp"
    "src/idl_gen_text.cpp"
    "src/idl_parser.cpp"
    "src/reflection.cpp"
    "src/util.cpp"
)

target_sources(flatc PRIVATE
    "src/annotated_binary_text_gen.cpp"
    "src/binary_annotator.cpp"
    "src/flatc.cpp"
    "src/flatc_main.cpp"
    "src/idl_gen_binary.cpp"
    "src/idl_gen_cpp.cpp"
)

target_link_libraries(flatc
    contrib-flatbuffers
)
