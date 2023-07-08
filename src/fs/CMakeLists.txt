add_library(vcs-fs STATIC)

target_link_libraries(vcs-fs PUBLIC
  libvcs3
)

if(UNIX)
  target_link_libraries(vcs-fs PUBLIC
    contrib-libfuse
    leveldb
  )

  if(LINUX)
    target_sources(vcs-fs PRIVATE
      "linux/db.cpp"
      "linux/fs.cpp"
      "linux/mount.cpp"
    )
  endif()
endif(UNIX)
