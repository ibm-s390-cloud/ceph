# CRC32C fuzzing tests

add_fuzz_test(fuzz_crc32c
  fuzz_crc32c.cc
)

target_link_libraries(fuzz_crc32c
  PRIVATE
  common
  global
)

# Create corpus directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

# Add initial corpus files with various sizes
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/corpus/input_001 "test")
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/corpus/input_002 "")
