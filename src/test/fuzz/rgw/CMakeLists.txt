# RGW XML parsing fuzzing tests

add_fuzz_test(fuzz_rgw_xml
  fuzz_rgw_xml.cc
)

target_link_libraries(fuzz_rgw_xml
  PRIVATE
  common
  global
)

# Create corpus directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

# Add initial XML corpus files
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/corpus/input_001.xml
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<root>
  <element>test</element>
</root>")

file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/corpus/input_002.xml
"<simple>value</simple>")

file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/corpus/input_003.xml
"<tag attr=\"value\">content</tag>")
