add_library(vcs-util STATIC)

target_sources(vcs-util PRIVATE
    "file.cpp"
    "iterator.cpp"
    "split.cpp"
    "tty.cpp"
    "wildmatch.cpp"
)
