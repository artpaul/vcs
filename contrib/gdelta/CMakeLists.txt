add_library(contrib-gdelta STATIC)

if(WIN32)
    target_sources(contrib-gdelta PRIVATE
        "gdelta.cpp"
        "msvc.c"
    )
else()
    target_sources(contrib-gdelta PRIVATE
        "gdelta.cpp"
    )
endif(WIN32)
