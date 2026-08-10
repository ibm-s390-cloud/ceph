#!/bin/bash
# Script to integrate fuzzing tests into Ceph build system
# Copyright (C) 2024 IBM Corporation

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_CMAKE="${SCRIPT_DIR}/../CMakeLists.txt"

echo "=========================================="
echo "Ceph Fuzzing Integration Script"
echo "=========================================="
echo ""

# Check if we're in the right directory
if [[ ! -f "$TEST_CMAKE" ]]; then
    echo "Error: Cannot find test CMakeLists.txt at: $TEST_CMAKE"
    exit 1
fi

echo "Found test CMakeLists.txt: $TEST_CMAKE"
echo ""

# Check if fuzzing is already integrated
if grep -q "add_subdirectory(fuzz)" "$TEST_CMAKE"; then
    echo "✓ Fuzzing tests are already integrated in CMakeLists.txt"
else
    echo "Adding fuzzing subdirectory to CMakeLists.txt..."
    
    # Backup original file
    cp "$TEST_CMAKE" "${TEST_CMAKE}.backup"
    echo "  Created backup: ${TEST_CMAKE}.backup"
    
    # Add fuzzing subdirectory after the last add_subdirectory
    # Find the last add_subdirectory line and add after it
    sed -i '/^add_subdirectory/a\
# Fuzzing tests with libFuzzer\
if(NOT WIN32)\
  add_subdirectory(fuzz)\
endif()' "$TEST_CMAKE"
    
    echo "✓ Added fuzzing subdirectory to CMakeLists.txt"
fi

echo ""
echo "=========================================="
echo "Fuzzing Structure Verification"
echo "=========================================="
echo ""

# Verify directory structure
DIRS=("buffer" "encoding" "crc32" "msgr" "rgw")
for dir in "${DIRS[@]}"; do
    if [[ -d "${SCRIPT_DIR}/${dir}" ]]; then
        echo "✓ Directory exists: fuzz/${dir}/"
        
        # Check for CMakeLists.txt
        if [[ -f "${SCRIPT_DIR}/${dir}/CMakeLists.txt" ]]; then
            echo "  ✓ CMakeLists.txt found"
        else
            echo "  ✗ CMakeLists.txt missing"
        fi
        
        # Check for fuzzer source
        FUZZER_FILES=$(find "${SCRIPT_DIR}/${dir}" -name "*.cc" -type f)
        if [[ -n "$FUZZER_FILES" ]]; then
            echo "  ✓ Fuzzer source files found"
        else
            echo "  ✗ No fuzzer source files"
        fi
        
        # Check for corpus directory
        if [[ -d "${SCRIPT_DIR}/${dir}/corpus" ]]; then
            echo "  ✓ Corpus directory exists"
        else
            echo "  ✗ Corpus directory missing"
        fi
    else
        echo "✗ Directory missing: fuzz/${dir}/"
    fi
    echo ""
done

echo "=========================================="
echo "Build Instructions"
echo "=========================================="
echo ""
echo "To build the fuzzing tests:"
echo ""
echo "1. Configure with fuzzing enabled:"
echo "   mkdir build && cd build"
echo "   cmake -DWITH_FUZZING=ON \\"
echo "         -DCMAKE_CXX_COMPILER=clang++ \\"
echo "         -DCMAKE_C_COMPILER=clang \\"
echo "         -DCMAKE_BUILD_TYPE=Debug \\"
echo "         .."
echo ""
echo "2. Build all fuzzers:"
echo "   make -j\$(nproc)"
echo ""
echo "3. Or build individual fuzzers:"
echo "   make fuzz_bufferlist"
echo "   make fuzz_encode_decode"
echo "   make fuzz_crc32c"
echo "   make fuzz_msgr_frames"
echo "   make fuzz_rgw_xml"
echo ""
echo "4. Run a fuzzer:"
echo "   ./src/test/fuzz/run_fuzzer.sh fuzz_bufferlist"
echo ""
echo "=========================================="
echo "Integration Complete!"
echo "=========================================="
