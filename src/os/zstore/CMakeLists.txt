# ZStore - Simple and Fast Storage Backend

set(zstore_srcs
  ZStore.cc
)

add_library(zstore STATIC ${zstore_srcs})

target_link_libraries(zstore
  PUBLIC
    os
    common
    ceph-common
    blk
)

target_include_directories(zstore
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
