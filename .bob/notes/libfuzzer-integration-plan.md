# LLVM libFuzzer Integration Plan for Ceph

## Executive Summary

This document provides a comprehensive analysis of Ceph's existing unit test infrastructure and outlines a strategy for integrating LLVM libFuzzer to enhance security testing through automated fuzzing. The plan identifies high-priority fuzzing targets, provides implementation examples, and includes recommendations for CI/CD integration.

**Status**: No existing libFuzzer integration found in the codebase.

---

## 1. Analysis of Existing Test Structure

### 1.1 Test Organization

Ceph's test suite is located in `src/test/` with the following structure:

```
src/test/
├── common/          # Core utility tests (80+ test files)
├── librados/        # RADOS client library tests
├── librbd/          # RBD (block device) tests
├── osd/             # Object Storage Daemon tests
├── rgw/             # RADOS Gateway tests
├── mon/             # Monitor daemon tests
├── mds/             # Metadata Server tests
├── encoding/        # Serialization tests
├── crush/           # CRUSH algorithm tests
└── [40+ other component directories]
```

### 1.2 Testing Framework

- **Primary Framework**: Google Test (GTest) and Google Mock (GMock)
- **Build System**: CMake with custom `AddCephTest` module
- **Test Execution**: Individual test binaries per component
- **Coverage**: Extensive unit tests (141 files contain parse/decode operations)

### 1.3 Key Test Patterns Observed

```cpp
// Typical GTest pattern in Ceph
TEST(ComponentName, TestCase) {
  // Setup
  Type object;
  
  // Execute
  encode(object, bufferlist);
  decode(object, bufferlist);
  
  // Verify
  ASSERT_EQ(expected, actual);
}
```

---

## 2. High-Priority Fuzzing Targets

Based on analysis of 835+ parse/decode operations across 141 test files, the following components are identified as critical fuzzing targets:

### 2.1 Tier 1: Critical Security Targets

#### A. Buffer Operations (`src/include/buffer.h`, `src/test/bufferlist.cc`)
- **Risk**: Memory corruption, buffer overflows
- **Lines of Test Code**: 3,144 lines
- **Fuzzing Value**: HIGH
- **Rationale**: Core data structure used throughout Ceph; handles untrusted input

**Key Functions to Fuzz**:
```cpp
bufferlist::append()
bufferlist::copy()
bufferlist::substr()
bufferptr::copy_in()
buffer::create_page_aligned()
```

#### B. Encoding/Decoding (`src/include/encoding.h`, `src/test/encoding.cc`)
- **Risk**: Deserialization vulnerabilities, type confusion
- **Lines of Test Code**: 546 lines
- **Fuzzing Value**: HIGH
- **Rationale**: Handles serialization of all Ceph objects; network protocol parsing

**Key Functions to Fuzz**:
```cpp
encode(T& obj, bufferlist& bl)
decode(T& obj, bufferlist::const_iterator& p)
encode_nohead()
decode_nohead()
ENCODE_START/ENCODE_FINISH macros
DECODE_START/DECODE_FINISH macros
```

#### C. CRC32 Calculations (`src/test/common/test_crc32c.cc`)
- **Risk**: Data integrity bypass, collision attacks
- **Lines of Test Code**: 378 lines
- **Fuzzing Value**: MEDIUM-HIGH
- **Rationale**: Critical for data integrity verification

**Key Functions to Fuzz**:
```cpp
ceph_crc32c()
ceph_crc32c_intel_baseline()
ceph_crc32c_aarch64()
```

### 2.2 Tier 2: Network Protocol Targets

#### D. Message Parsing (`src/test/msgr/`)
- **Risk**: Protocol confusion, message injection
- **Fuzzing Value**: HIGH
- **Components**:
  - `test_frames_v2.cc` - Protocol v2 frame parsing
  - `test_msgr.cc` - Message handling
  - `test_async_networkstack.cc` - Async network operations

#### E. RGW XML/JSON Parsing (`src/test/rgw/`)
- **Risk**: XML/JSON injection, parser exploits
- **Fuzzing Value**: HIGH
- **Components**:
  - `test_rgw_xml.cc` - XML parsing
  - `test_rgw_tag.cc` - Tag parsing
  - `test_rgw_arn.cc` - ARN parsing

### 2.3 Tier 3: Storage Backend Targets

#### F. Object Store Transactions (`src/test/objectstore/`)
- **Risk**: Transaction corruption, state inconsistency
- **Fuzzing Value**: MEDIUM
- **Components**:
  - `test_transaction.cc`
  - `test_bluestore_types.cc`

#### G. Compression (`src/test/compressor/`)
- **Risk**: Decompression bombs, buffer overflows
- **Fuzzing Value**: MEDIUM
- **Component**: `test_compression.cc`

---

## 3. libFuzzer Integration Strategy

### 3.1 Build System Integration

#### CMake Configuration

Create `src/test/fuzz/CMakeLists.txt`:

```cmake
# Fuzzing tests using LLVM libFuzzer
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
      target_link_libraries(${name} PRIVATE ${UNITTEST_LIBS})
      
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
  endif()
endif()
```

### 3.2 Directory Structure

```
src/test/fuzz/
├── CMakeLists.txt
├── README.md
├── buffer/
│   ├── CMakeLists.txt
│   ├── fuzz_bufferlist.cc
│   └── corpus/
├── encoding/
│   ├── CMakeLists.txt
│   ├── fuzz_encode_decode.cc
│   └── corpus/
├── crc32/
│   ├── CMakeLists.txt
│   ├── fuzz_crc32c.cc
│   └── corpus/
├── msgr/
│   ├── CMakeLists.txt
│   ├── fuzz_msgr_frames.cc
│   └── corpus/
└── rgw/
    ├── CMakeLists.txt
    ├── fuzz_rgw_xml.cc
    └── corpus/
```

---

## 4. Implementation Examples

### 4.1 Example 1: Buffer Operations Fuzzer

**File**: `src/test/fuzz/buffer/fuzz_bufferlist.cc`

```cpp
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2024 IBM Corporation
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "include/buffer.h"
#include "include/encoding.h"

using namespace ceph;

// Fuzzing entry point for libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // Minimum size check to avoid trivial inputs
  if (size < 4) {
    return 0;
  }

  try {
    // Test 1: bufferlist append operations
    {
      bufferlist bl;
      bl.append((const char*)data, size);
      
      // Verify length
      if (bl.length() != size) {
        return -1;
      }
      
      // Test copy operations
      bufferlist bl2;
      bl2 = bl;
      
      // Test substr
      if (size > 10) {
        bufferlist sub = bl.substr(0, 10);
      }
    }

    // Test 2: bufferptr operations
    {
      bufferptr ptr(buffer::create(size));
      memcpy(ptr.c_str(), data, size);
      
      bufferlist bl;
      bl.append(ptr);
      
      // Test iterator operations
      auto it = bl.cbegin();
      while (!it.end()) {
        ++it;
      }
    }

    // Test 3: Encoding/decoding with bufferlist
    {
      bufferlist bl;
      bl.append((const char*)data, size);
      
      // Try to decode various types
      auto it = bl.cbegin();
      
      // Attempt to decode as uint32_t
      if (size >= sizeof(uint32_t)) {
        uint32_t val;
        try {
          decode(val, it);
        } catch (...) {
          // Expected for malformed input
        }
      }
      
      // Attempt to decode as string
      it = bl.cbegin();
      if (size >= sizeof(uint32_t)) {
        std::string str;
        try {
          decode(str, it);
        } catch (...) {
          // Expected for malformed input
        }
      }
    }

    // Test 4: CRC operations
    {
      bufferlist bl;
      bl.append((const char*)data, size);
      uint32_t crc = bl.crc32c(0);
      
      // Verify CRC is deterministic
      uint32_t crc2 = bl.crc32c(0);
      if (crc != crc2) {
        return -1;
      }
    }

  } catch (const buffer::error& e) {
    // Expected exceptions for malformed input
    return 0;
  } catch (const std::exception& e) {
    // Unexpected exceptions should be investigated
    return -1;
  }

  return 0;
}
```

**CMakeLists.txt** for buffer fuzzer:

```cmake
add_fuzz_test(fuzz_bufferlist
  fuzz_bufferlist.cc
)

target_link_libraries(fuzz_bufferlist
  PRIVATE
  common
  global
)
```

### 4.2 Example 2: Encoding/Decoding Fuzzer

**File**: `src/test/fuzz/encoding/fuzz_encode_decode.cc`

```cpp
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>
#include <map>

#include "include/buffer.h"
#include "include/encoding.h"
#include "include/utime.h"

using namespace ceph;

// Fuzzer for Ceph's encoding/decoding infrastructure
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8) {
    return 0;
  }

  bufferlist bl;
  bl.append((const char*)data, size);
  auto it = bl.cbegin();

  try {
    // Fuzz primitive types
    {
      auto it_copy = it;
      uint8_t u8;
      uint16_t u16;
      uint32_t u32;
      uint64_t u64;
      
      try { decode(u8, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(u16, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(u32, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(u64, it_copy); } catch (...) {}
    }

    // Fuzz string decoding
    {
      auto it_copy = it;
      std::string str;
      try {
        decode(str, it_copy);
      } catch (...) {}
    }

    // Fuzz vector decoding
    {
      auto it_copy = it;
      std::vector<uint32_t> vec;
      try {
        decode(vec, it_copy);
      } catch (...) {}
    }

    // Fuzz map decoding
    {
      auto it_copy = it;
      std::map<std::string, uint32_t> m;
      try {
        decode(m, it_copy);
      } catch (...) {}
    }

    // Fuzz utime decoding
    {
      auto it_copy = it;
      utime_t t;
      try {
        decode(t, it_copy);
      } catch (...) {}
    }

  } catch (const buffer::error& e) {
    return 0;
  } catch (const std::exception& e) {
    // Log unexpected exceptions
    return -1;
  }

  return 0;
}
```

### 4.3 Example 3: CRC32 Fuzzer

**File**: `src/test/fuzz/crc32/fuzz_crc32c.cc`

```cpp
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "include/crc32c.h"
#include "common/crc32c_intel_baseline.h"

#ifdef __aarch64__
#include "common/crc32c_aarch64.h"
#endif

#ifdef __riscv
#include "common/crc32c_riscv.h"
#endif

// Fuzzer to test CRC32C implementations for consistency
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0) {
    return 0;
  }

  // Test with different initial values
  uint32_t init_values[] = {0, 0xFFFFFFFF, 0x12345678, 0xDEADBEEF};
  
  for (uint32_t init : init_values) {
    // Calculate CRC using main implementation
    uint32_t crc1 = ceph_crc32c(init, data, size);
    
    // Calculate using baseline implementation
    uint32_t crc2 = ceph_crc32c_intel_baseline(init, data, size);
    
    // Verify consistency
    if (crc1 != crc2) {
      // Implementations should produce same result
      return -1;
    }

#ifdef __aarch64__
    // Test ARM implementation if available
    uint32_t crc3 = ceph_crc32c_aarch64(init, data, size);
    if (crc1 != crc3) {
      return -1;
    }
#endif

#ifdef __riscv
    // Test RISC-V implementation if available
    uint32_t crc4 = ceph_crc32c_riscv(init, data, size);
    if (crc1 != crc4) {
      return -1;
    }
#endif
  }

  // Test incremental CRC calculation
  if (size > 4) {
    size_t split = size / 2;
    uint32_t crc_full = ceph_crc32c(0, data, size);
    uint32_t crc_part1 = ceph_crc32c(0, data, split);
    uint32_t crc_part2 = ceph_crc32c(crc_part1, data + split, size - split);
    
    if (crc_full != crc_part2) {
      // Incremental CRC should match full CRC
      return -1;
    }
  }

  return 0;
}
```

### 4.4 Example 4: Message Frame Fuzzer

**File**: `src/test/fuzz/msgr/fuzz_msgr_frames.cc`

```cpp
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

#include <stdint.h>
#include <stddef.h>

#include "include/buffer.h"
#include "msg/async/frames_v2.h"

using namespace ceph::msgr::v2;

// Fuzzer for Ceph messenger protocol v2 frame parsing
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 16) {
    return 0;
  }

  try {
    bufferlist bl;
    bl.append((const char*)data, size);
    
    // Try to parse as different frame types
    auto it = bl.cbegin();
    
    // Attempt to decode frame header
    // Note: Actual implementation depends on Ceph's frame structure
    // This is a simplified example
    
    // Test frame parsing robustness
    while (!it.end() && it.get_remaining() >= 16) {
      try {
        // Parse frame header (simplified)
        uint32_t tag;
        decode(tag, it);
        
        uint32_t segment_count;
        decode(segment_count, it);
        
        // Limit segment count to prevent resource exhaustion
        if (segment_count > 100) {
          break;
        }
        
        // Parse segments
        for (uint32_t i = 0; i < segment_count && !it.end(); ++i) {
          uint32_t segment_len;
          decode(segment_len, it);
          
          // Limit segment length
          if (segment_len > 1024 * 1024) {
            break;
          }
          
          // Skip segment data
          it.advance(std::min((size_t)segment_len, it.get_remaining()));
        }
        
      } catch (const buffer::error& e) {
        break;
      }
    }
    
  } catch (const std::exception& e) {
    return -1;
  }

  return 0;
}
```

### 4.5 Example 5: RGW XML Parser Fuzzer

**File**: `src/test/fuzz/rgw/fuzz_rgw_xml.cc`

```cpp
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

#include <stdint.h>
#include <stddef.h>
#include <string>

#include "rgw/rgw_xml.h"
#include "rgw/rgw_common.h"

// Fuzzer for RGW XML parsing
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0 || size > 1024 * 1024) {
    return 0;
  }

  try {
    std::string xml_data((const char*)data, size);
    
    // Test XML parsing with various RGW XML structures
    RGWXMLParser parser;
    
    if (!parser.init()) {
      return 0;
    }
    
    // Parse the XML
    if (!parser.parse(xml_data.c_str(), xml_data.length(), 1)) {
      // Parsing failed - expected for malformed XML
      return 0;
    }
    
    // If parsing succeeded, try to extract data
    XMLObj *obj = parser.find_first("root");
    if (obj) {
      // Try to get various attributes
      std::string attr;
      obj->get_data(attr);
    }
    
  } catch (const std::exception& e) {
    // Expected for malformed input
    return 0;
  }

  return 0;
}
```

---

## 5. Corpus Management

### 5.1 Initial Corpus Creation

For each fuzzer, create an initial corpus from existing test cases:

```bash
#!/bin/bash
# Script: create_initial_corpus.sh

# Buffer corpus from existing tests
mkdir -p src/test/fuzz/buffer/corpus
cd src/test/fuzz/buffer/corpus

# Extract test inputs from unit tests
echo "Creating buffer corpus..."
echo -n "test" > input_001
echo -n "a" > input_002
dd if=/dev/urandom of=input_003 bs=1024 count=1 2>/dev/null
dd if=/dev/urandom of=input_004 bs=4096 count=1 2>/dev/null

# Encoding corpus
mkdir -p ../../encoding/corpus
cd ../../encoding/corpus

echo "Creating encoding corpus..."
# Create various encoded structures
python3 << 'EOF'
import struct

# Encoded uint32
with open('input_001', 'wb') as f:
    f.write(struct.pack('<I', 42))

# Encoded string
with open('input_002', 'wb') as f:
    s = b"test string"
    f.write(struct.pack('<I', len(s)))
    f.write(s)

# Encoded vector
with open('input_003', 'wb') as f:
    f.write(struct.pack('<I', 3))  # count
    f.write(struct.pack('<I', 1))
    f.write(struct.pack('<I', 2))
    f.write(struct.pack('<I', 3))
EOF

# CRC32 corpus
mkdir -p ../../crc32/corpus
cd ../../crc32/corpus

echo "Creating CRC32 corpus..."
echo -n "test" > input_001
echo -n "" > input_002
dd if=/dev/urandom of=input_003 bs=1024 count=1 2>/dev/null
```

### 5.2 Corpus Minimization

```bash
#!/bin/bash
# Script: minimize_corpus.sh

FUZZER=$1
CORPUS_DIR=$2

if [ -z "$FUZZER" ] || [ -z "$CORPUS_DIR" ]; then
    echo "Usage: $0 <fuzzer_binary> <corpus_directory>"
    exit 1
fi

# Create minimized corpus directory
MINIMIZED_DIR="${CORPUS_DIR}_minimized"
mkdir -p "$MINIMIZED_DIR"

# Run corpus minimization
$FUZZER -merge=1 "$MINIMIZED_DIR" "$CORPUS_DIR"

echo "Minimized corpus saved to: $MINIMIZED_DIR"
```

---

## 6. Running Fuzzing Tests

### 6.1 Build Configuration

```bash
# Configure with fuzzing enabled
cmake -DWITH_FUZZING=ON \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_BUILD_TYPE=Debug \
      ..

# Build fuzzing targets
make fuzz_bufferlist fuzz_encode_decode fuzz_crc32c
```

### 6.2 Running Individual Fuzzers

```bash
# Run buffer fuzzer for 1 hour with 8 jobs
./bin/fuzz_bufferlist \
    -max_total_time=3600 \
    -jobs=8 \
    -workers=8 \
    src/test/fuzz/buffer/corpus/

# Run with specific options
./bin/fuzz_encode_decode \
    -max_len=65536 \
    -timeout=10 \
    -rss_limit_mb=2048 \
    -dict=encoding.dict \
    src/test/fuzz/encoding/corpus/
```

### 6.3 Dictionary Files

Create dictionary files to guide fuzzing:

**File**: `src/test/fuzz/encoding/encoding.dict`

```
# Encoding dictionary for libFuzzer
# Common encoding markers and values

# ENCODE_START markers
"\x00\x00\x00\x01"
"\x00\x00\x00\x02"
"\x00\x00\x00\x03"

# Common lengths
"\x00\x00\x00\x00"
"\x00\x00\x00\x01"
"\x00\x00\x00\x10"
"\x00\x00\x00\xFF"

# String markers
"ceph"
"rados"
"rbd"
"rgw"

# Type tags
"\x01"
"\x02"
"\x03"
"\xFF"
```

---

## 7. CI/CD Integration

### 7.1 GitHub Actions Workflow

**File**: `.github/workflows/fuzzing.yml`

```yaml
name: Fuzzing Tests

on:
  schedule:
    # Run nightly
    - cron: '0 2 * * *'
  workflow_dispatch:
    inputs:
      duration:
        description: 'Fuzzing duration in seconds'
        required: false
        default: '3600'

jobs:
  fuzz:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        fuzzer:
          - fuzz_bufferlist
          - fuzz_encode_decode
          - fuzz_crc32c
          - fuzz_msgr_frames
          - fuzz_rgw_xml
    
    steps:
      - name: Checkout code
        uses: actions/checkout@v4
        with:
          submodules: recursive
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            clang-15 \
            llvm-15 \
            cmake \
            ninja-build \
            libboost-all-dev
      
      - name: Configure with fuzzing
        run: |
          mkdir build
          cd build
          cmake -GNinja \
            -DWITH_FUZZING=ON \
            -DCMAKE_CXX_COMPILER=clang++-15 \
            -DCMAKE_C_COMPILER=clang-15 \
            -DCMAKE_BUILD_TYPE=Debug \
            ..
      
      - name: Build fuzzer
        run: |
          cd build
          ninja ${{ matrix.fuzzer }}
      
      - name: Download corpus
        run: |
          # Download existing corpus from artifact storage
          # or use initial corpus
          mkdir -p corpus
      
      - name: Run fuzzer
        run: |
          DURATION=${{ github.event.inputs.duration || '3600' }}
          ./build/bin/${{ matrix.fuzzer }} \
            -max_total_time=$DURATION \
            -print_final_stats=1 \
            -artifact_prefix=artifacts/ \
            corpus/
      
      - name: Upload artifacts
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: fuzzing-artifacts-${{ matrix.fuzzer }}
          path: artifacts/
      
      - name: Upload corpus
        uses: actions/upload-artifact@v4
        with:
          name: corpus-${{ matrix.fuzzer }}
          path: corpus/
```

### 7.2 OSS-Fuzz Integration

For continuous fuzzing with Google's OSS-Fuzz:

**File**: `oss-fuzz/build.sh`

```bash
#!/bin/bash -eu
# OSS-Fuzz build script for Ceph

# Build Ceph with fuzzing enabled
cd $SRC/ceph
mkdir build
cd build

cmake -GNinja \
  -DCMAKE_CXX_COMPILER=$CXX \
  -DCMAKE_C_COMPILER=$CC \
  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
  -DCMAKE_C_FLAGS="$CFLAGS" \
  -DWITH_FUZZING=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  ..

ninja fuzz_bufferlist fuzz_encode_decode fuzz_crc32c

# Copy fuzzers to output
cp bin/fuzz_* $OUT/

# Copy corpus
for fuzzer in fuzz_bufferlist fuzz_encode_decode fuzz_crc32c; do
  zip -r $OUT/${fuzzer}_seed_corpus.zip \
    $SRC/ceph/src/test/fuzz/*/corpus/
done

# Copy dictionaries
cp $SRC/ceph/src/test/fuzz/*/*.dict $OUT/ || true
```

### 7.3 Jenkins Pipeline

**File**: `Jenkinsfile.fuzzing`

```groovy
pipeline {
    agent {
        label 'fuzzing'
    }
    
    triggers {
        cron('H 2 * * *')  // Nightly
    }
    
    parameters {
        string(name: 'DURATION', defaultValue: '7200', 
               description: 'Fuzzing duration in seconds')
        choice(name: 'FUZZER', choices: ['all', 'fuzz_bufferlist', 
               'fuzz_encode_decode', 'fuzz_crc32c'], 
               description: 'Fuzzer to run')
    }
    
    stages {
        stage('Checkout') {
            steps {
                checkout scm
                sh 'git submodule update --init --recursive'
            }
        }
        
        stage('Build') {
            steps {
                sh '''
                    mkdir -p build
                    cd build
                    cmake -DWITH_FUZZING=ON \
                          -DCMAKE_CXX_COMPILER=clang++ \
                          -DCMAKE_C_COMPILER=clang \
                          -DCMAKE_BUILD_TYPE=Debug \
                          ..
                    make -j$(nproc)
                '''
            }
        }
        
        stage('Fuzz') {
            steps {
                script {
                    def fuzzers = params.FUZZER == 'all' ? 
                        ['fuzz_bufferlist', 'fuzz_encode_decode', 'fuzz_crc32c'] :
                        [params.FUZZER]
                    
                    parallel fuzzers.collectEntries { fuzzer ->
                        ["${fuzzer}": {
                            sh """
                                ./build/bin/${fuzzer} \
                                    -max_total_time=${params.DURATION} \
                                    -jobs=4 \
                                    -workers=4 \
                                    -artifact_prefix=artifacts/${fuzzer}/ \
                                    src/test/fuzz/*/corpus/
                            """
                        }]
                    }
                }
            }
        }
    }
    
    post {
        always {
            archiveArtifacts artifacts: 'artifacts/**/*', 
                           allowEmptyArchive: true
        }
        failure {
            emailext(
                subject: "Fuzzing failure: ${env.JOB_NAME}",
                body: "Fuzzing test failed. Check artifacts.",
                to: "security-team@example.com"
            )
        }
    }
}
```

---

## 8. Crash Analysis and Triage

### 8.1 Analyzing Crashes

When a fuzzer finds a crash:

```bash
# Reproduce the crash
./bin/fuzz_bufferlist crash-file

# Get detailed output with AddressSanitizer
ASAN_OPTIONS=symbolize=1:abort_on_error=1 \
./bin/fuzz_bufferlist crash-file

# Minimize the crash input
./bin/fuzz_bufferlist \
    -minimize_crash=1 \
    -exact_artifact_path=minimized-crash \
    crash-file
```

### 8.2 Crash Triage Script

**File**: `src/test/fuzz/triage_crash.sh`

```bash
#!/bin/bash
# Crash triage script

FUZZER=$1
CRASH_FILE=$2

if [ -z "$FUZZER" ] || [ -z "$CRASH_FILE" ]; then
    echo "Usage: $0 <fuzzer> <crash_file>"
    exit 1
fi

echo "=== Crash Triage Report ==="
echo "Fuzzer: $FUZZER"
echo "Crash file: $CRASH_FILE"
echo "Date: $(date)"
echo ""

# Get crash hash
CRASH_HASH=$(sha256sum "$CRASH_FILE" | cut -d' ' -f1)
echo "Crash hash: $CRASH_HASH"
echo ""

# Reproduce crash
echo "=== Reproducing crash ==="
ASAN_OPTIONS=symbolize=1:print_stacktrace=1 \
    ./$FUZZER "$CRASH_FILE" 2>&1 | tee crash_${CRASH_HASH}.log

# Minimize crash
echo ""
echo "=== Minimizing crash input ==="
./$FUZZER \
    -minimize_crash=1 \
    -exact_artifact_path=minimized_${CRASH_HASH} \
    "$CRASH_FILE"

echo ""
echo "Minimized crash saved to: minimized_${CRASH_HASH}"
echo "Full log saved to: crash_${CRASH_HASH}.log"
```

---

## 9. Performance Considerations

### 9.1 Resource Limits

```bash
# Limit memory usage
./bin/fuzz_bufferlist -rss_limit_mb=2048 corpus/

# Limit execution time per input
./bin/fuzz_bufferlist -timeout=10 corpus/

# Limit total fuzzing time
./bin/fuzz_bufferlist -max_total_time=3600 corpus/
```

### 9.2 Parallel Fuzzing

```bash
# Run with multiple workers
./bin/fuzz_bufferlist \
    -jobs=8 \
    -workers=8 \
    corpus/

# Distributed fuzzing across machines
# Machine 1:
./bin/fuzz_bufferlist -jobs=4 -workers=4 shared_corpus/

# Machine 2:
./bin/fuzz_bufferlist -jobs=4 -workers=4 shared_corpus/
```

---

## 10. Security Considerations

### 10.1 Sanitizer Configuration

Always enable multiple sanitizers:

```cmake
set(FUZZING_FLAGS
  "-fsanitize=fuzzer,address,undefined,integer"
  "-fno-sanitize-recover=all"
  "-fno-omit-frame-pointer"
  "-g"
)
```

### 10.2 Coverage-Guided Fuzzing

```bash
# Generate coverage report
LLVM_PROFILE_FILE="fuzzing.profraw" \
./bin/fuzz_bufferlist -runs=1000000 corpus/

llvm-profdata merge -sparse fuzzing.profraw -o fuzzing.profdata

llvm-cov show ./bin/fuzz_bufferlist \
    -instr-profile=fuzzing.profdata \
    -format=html \
    -output-dir=coverage_report
```

---

## 11. Maintenance and Updates

### 11.1 Regular Tasks

1. **Weekly**:
   - Review fuzzing results
   - Triage new crashes
   - Update corpus with interesting inputs

2. **Monthly**:
   - Minimize corpus
   - Update dictionaries
   - Review coverage reports
   - Add new fuzzing targets

3. **Quarterly**:
   - Evaluate fuzzing effectiveness
   - Update fuzzing infrastructure
   - Train team on new findings

### 11.2 Metrics to Track

- **Coverage**: Lines/branches covered by fuzzing
- **Crashes**: Number and severity of crashes found
- **Corpus size**: Growth of corpus over time
- **Execution speed**: Executions per second
- **Unique crashes**: Deduplicated crash count

---

## 12. Documentation and Training

### 12.1 Developer Guide

Create `docs/dev/fuzzing.md`:

```markdown
# Fuzzing Guide for Ceph Developers

## Adding a New Fuzzer

1. Create fuzzer file in `src/test/fuzz/<component>/`
2. Implement `LLVMFuzzerTestOneInput()` function
3. Add to CMakeLists.txt
4. Create initial corpus
5. Test locally
6. Submit PR with fuzzer

## Best Practices

- Keep fuzzers focused on single components
- Handle expected exceptions gracefully
- Add resource limits to prevent hangs
- Create meaningful initial corpus
- Document fuzzing targets
```

### 12.2 Security Team Guide

Document for security team:

```markdown
# Security Fuzzing Operations

## Responding to Crashes

1. Verify crash is reproducible
2. Minimize crash input
3. Determine severity (CVSS scoring)
4. Create security advisory if needed
5. Develop fix
6. Add regression test
7. Update corpus

## Severity Classification

- **Critical**: Remote code execution, privilege escalation
- **High**: Denial of service, information disclosure
- **Medium**: Local crashes, resource exhaustion
- **Low**: Edge cases, theoretical issues
```

---

## 13. Recommendations

### 13.1 Immediate Actions (Week 1-2)

1. ✅ Set up fuzzing build infrastructure
2. ✅ Implement buffer and encoding fuzzers (highest priority)
3. ✅ Create initial corpus from existing tests
4. ✅ Run local fuzzing campaigns (24-48 hours each)
5. ✅ Document any findings

### 13.2 Short-term Goals (Month 1-3)

1. ✅ Implement all Tier 1 fuzzers
2. ✅ Integrate with CI/CD pipeline
3. ✅ Set up continuous fuzzing infrastructure
4. ✅ Train development team on fuzzing
5. ✅ Establish crash triage process

### 13.3 Long-term Goals (Month 3-12)

1. ✅ Implement Tier 2 and Tier 3 fuzzers
2. ✅ Integrate with OSS-Fuzz
3. ✅ Achieve 70%+ code coverage in critical paths
4. ✅ Establish fuzzing as part of development workflow
5. ✅ Regular security audits based on fuzzing results

---

## 14. Expected Outcomes

### 14.1 Security Improvements

- **Proactive vulnerability discovery**: Find bugs before attackers
- **Improved code robustness**: Better handling of malformed input
- **Reduced attack surface**: Identify and fix parsing vulnerabilities
- **Compliance**: Meet security testing requirements

### 14.2 Quality Improvements

- **Better error handling**: Discover edge cases
- **Performance insights**: Identify slow code paths
- **Code coverage**: Increase test coverage
- **Documentation**: Better understanding of input validation

---

## 15. Conclusion

Integrating LLVM libFuzzer into Ceph's testing infrastructure will significantly enhance the project's security posture. The proposed implementation:

1. **Targets critical components**: Focus on high-risk parsing and encoding operations
2. **Leverages existing infrastructure**: Builds on CMake and GTest framework
3. **Provides concrete examples**: Ready-to-use fuzzer implementations
4. **Includes CI/CD integration**: Automated continuous fuzzing
5. **Establishes processes**: Crash triage and maintenance procedures

**Next Steps**:
1. Review and approve this plan
2. Allocate resources for implementation
3. Begin with Tier 1 fuzzers (buffer, encoding, CRC32)
4. Iterate based on findings and feedback

---

## Appendix A: References

- [LLVM libFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html)
- [OSS-Fuzz](https://github.com/google/oss-fuzz)
- [Fuzzing Book](https://www.fuzzingbook.org/)
- [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [Ceph Development Guide](https://docs.ceph.com/en/latest/dev/)

## Appendix B: Glossary

- **Corpus**: Collection of test inputs for fuzzing
- **Coverage**: Measure of code executed during fuzzing
- **Crash**: Program failure discovered by fuzzer
- **Dictionary**: Hints to guide fuzzer toward interesting inputs
- **Sanitizer**: Runtime error detection tool (ASan, UBSan, etc.)
- **Seed**: Initial input for fuzzing campaign

---

**Document Version**: 1.0  
**Last Updated**: 2026-08-10  
**Author**: Bob Shell AI Assistant  
**Status**: Ready for Review
