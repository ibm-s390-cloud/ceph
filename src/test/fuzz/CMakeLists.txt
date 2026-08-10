# Fuzzing tests using LLVM libFuzzer
# Copyright (C) 2024 IBM Corporation

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  option(WITH_FUZZING "Build fuzzing tests with libFuzzer" OFF)
  
  if(WITH_FUZZING)
    message(STATUS "Building with libFuzzer support")
    
    # Fuzzing compiler flags
    set(FUZZING_FLAGS
      "-fsanitize=fuzzer,address,undefined"
      "-fno-omit-frame-pointer"
      "-g"
    )
    
    # Fuzzing linker flags
    set(FUZZING_LINK_FLAGS
      "-fsanitize=fuzzer,address,undefined"
    )
    
    # Helper function to add fuzzing targets
    function(add_fuzz_test name)
      add_executable(${name} ${ARGN})
      target_compile_options(${name} PRIVATE ${FUZZING_FLAGS})
      target_link_options(${name} PRIVATE ${FUZZING_LINK_FLAGS})
      
      # Install to fuzz directory
      install(TARGETS ${name}
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/ceph/fuzz)
    endfunction()
    
    # Add fuzzing subdirectories
    add_subdirectory(buffer)
    add_subdirectory(encoding)
    add_subdirectory(crc32)
    add_subdirectory(msgr)
    add_subdirectory(rgw)
    
    message(STATUS "Fuzzing targets will be installed to: ${CMAKE_INSTALL_LIBDIR}/ceph/fuzz")
  else()
    message(STATUS "Fuzzing disabled. Use -DWITH_FUZZING=ON to enable.")
  endif()
else()
  message(STATUS "Fuzzing requires Clang compiler. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()
