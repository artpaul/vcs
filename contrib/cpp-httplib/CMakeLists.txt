add_library(contrib-httplib STATIC
    "httplib.cpp"
)

target_compile_definitions(contrib-httplib PUBLIC
    CPPHTTPLIB_OPENSSL_SUPPORT
    CPPHTTPLIB_ZLIB_SUPPORT
)

target_link_libraries(contrib-httplib PUBLIC
    crypto
    ssl
    zlibstatic
)
