file(GLOB SRC_HTTP "*.c" "*.h")
list(SORT SRC_HTTP)

add_library(contrib-http-parser STATIC ${SRC_HTTP})
