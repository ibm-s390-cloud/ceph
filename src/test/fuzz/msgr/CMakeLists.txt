# Messenger protocol fuzzing tests

add_fuzz_test(fuzz_msgr_frames
  fuzz_msgr_frames.cc
)

target_link_libraries(fuzz_msgr_frames
  PRIVATE
  common
  global
)

# Create corpus directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
