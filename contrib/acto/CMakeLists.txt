add_library(contrib-acto STATIC)

target_sources(contrib-acto PRIVATE
    "acto.cpp"
    "event.cpp"
    "runtime.cpp"
    "worker.cpp"
)
