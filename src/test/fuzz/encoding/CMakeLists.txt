# Encoding/Decoding fuzzing tests

add_fuzz_test(fuzz_encode_decode
  fuzz_encode_decode.cc
)

target_link_libraries(fuzz_encode_decode
  PRIVATE
  common
  global
)

# Create corpus directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

# Create dictionary file for encoding patterns
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/encoding.dict
"# Encoding dictionary for libFuzzer
# Common encoding markers and values

# ENCODE_START markers
\"\\x00\\x00\\x00\\x01\"
\"\\x00\\x00\\x00\\x02\"
\"\\x00\\x00\\x00\\x03\"

# Common lengths
\"\\x00\\x00\\x00\\x00\"
\"\\x00\\x00\\x00\\x01\"
\"\\x00\\x00\\x00\\x10\"
\"\\x00\\x00\\x00\\xFF\"

# String markers
\"ceph\"
\"rados\"
\"rbd\"
\"rgw\"

# Type tags
\"\\x01\"
\"\\x02\"
\"\\x03\"
\"\\xFF\"
")
