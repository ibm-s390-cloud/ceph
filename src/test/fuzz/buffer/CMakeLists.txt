# Buffer fuzzing tests

add_fuzz_test(fuzz_bufferlist
  fuzz_bufferlist.cc
)

target_link_libraries(fuzz_bufferlist
  PRIVATE
  common
  global
)

# Create corpus directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

# Add initial corpus files
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/corpus/input_001 "test")
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/corpus/input_002 "a")
