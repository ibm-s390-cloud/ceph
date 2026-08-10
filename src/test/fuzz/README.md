# Ceph Fuzzing Tests with LLVM libFuzzer

This directory contains fuzzing tests for Ceph using LLVM's libFuzzer. These tests help discover bugs, security vulnerabilities, and edge cases through automated input generation.

## Prerequisites

- **Clang compiler** (version 10 or later)
- **LLVM libFuzzer** (included with Clang)
- **AddressSanitizer** and **UndefinedBehaviorSanitizer** support

## Building Fuzzing Tests

### Configure CMake with Fuzzing Enabled

```bash
mkdir build && cd build
cmake -DWITH_FUZZING=ON \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_BUILD_TYPE=Debug \
      ..
```

### Build All Fuzzers

```bash
make -j$(nproc)
```

### Build Individual Fuzzers

```bash
make fuzz_bufferlist
make fuzz_encode_decode
make fuzz_crc32c
make fuzz_msgr_frames
make fuzz_rgw_xml
```

## Running Fuzzers

### Basic Usage

```bash
# Run a fuzzer with its corpus
./bin/fuzz_bufferlist src/test/fuzz/buffer/corpus/

# Run for a specific duration (in seconds)
./bin/fuzz_bufferlist -max_total_time=3600 src/test/fuzz/buffer/corpus/

# Run with multiple workers
./bin/fuzz_bufferlist -jobs=8 -workers=8 src/test/fuzz/buffer/corpus/
```

### Common Options

- `-max_total_time=N` - Run for N seconds
- `-max_len=N` - Maximum input length
- `-timeout=N` - Timeout per input (seconds)
- `-rss_limit_mb=N` - Memory limit in MB
- `-jobs=N` - Number of parallel jobs
- `-workers=N` - Number of worker processes
- `-dict=file` - Use dictionary file
- `-print_final_stats=1` - Print statistics at end

### Using Dictionaries

```bash
./bin/fuzz_encode_decode \
    -dict=src/test/fuzz/encoding/encoding.dict \
    src/test/fuzz/encoding/corpus/
```

## Available Fuzzers

### 1. fuzz_bufferlist
Tests buffer operations including:
- Buffer append, copy, and substr operations
- Buffer iterator operations
- Encoding/decoding with bufferlist
- CRC calculations
- Buffer splitting and merging

**Target**: `src/include/buffer.h`

### 2. fuzz_encode_decode
Tests Ceph's encoding/decoding infrastructure:
- Primitive type encoding/decoding
- String, vector, map, set encoding
- Nested structure encoding
- encode_nohead/decode_nohead operations

**Target**: `src/include/encoding.h`

### 3. fuzz_crc32c
Tests CRC32C implementations for consistency:
- Multiple CRC32C implementations (Intel, ARM, RISC-V)
- Incremental CRC calculation
- Determinism verification

**Target**: `src/include/crc32c.h`

### 4. fuzz_msgr_frames
Tests messenger protocol frame parsing:
- Frame header parsing
- Segment parsing
- Buffer operations on frames

**Target**: Messenger protocol handling

### 5. fuzz_rgw_xml
Tests RGW XML parsing:
- XML structure validation
- Nested element handling
- Attribute parsing
- Entity reference handling

**Target**: RGW XML parsing

## Analyzing Crashes

### Reproduce a Crash

```bash
./bin/fuzz_bufferlist crash-file
```

### Get Detailed Output

```bash
ASAN_OPTIONS=symbolize=1:abort_on_error=1 \
./bin/fuzz_bufferlist crash-file
```

### Minimize Crash Input

```bash
./bin/fuzz_bufferlist \
    -minimize_crash=1 \
    -exact_artifact_path=minimized-crash \
    crash-file
```

## Corpus Management

### Merge Corpora

```bash
./bin/fuzz_bufferlist -merge=1 new_corpus/ old_corpus/
```

### Minimize Corpus

```bash
./bin/fuzz_bufferlist -merge=1 minimized_corpus/ corpus/
```

## Coverage Analysis

### Generate Coverage Report

```bash
# Run with coverage
LLVM_PROFILE_FILE="fuzzing.profraw" \
./bin/fuzz_bufferlist -runs=1000000 corpus/

# Merge profile data
llvm-profdata merge -sparse fuzzing.profraw -o fuzzing.profdata

# Generate HTML report
llvm-cov show ./bin/fuzz_bufferlist \
    -instr-profile=fuzzing.profdata \
    -format=html \
    -output-dir=coverage_report

# View report
open coverage_report/index.html
```

## Continuous Fuzzing

### GitHub Actions

See `.github/workflows/fuzzing.yml` for automated fuzzing in CI/CD.

### OSS-Fuzz Integration

Ceph can be integrated with Google's OSS-Fuzz for continuous fuzzing. See `oss-fuzz/build.sh` for details.

## Best Practices

1. **Start Small**: Begin with short fuzzing runs (1-2 hours) to verify setup
2. **Use Dictionaries**: Provide dictionaries to guide fuzzing toward interesting inputs
3. **Monitor Resources**: Set appropriate memory and timeout limits
4. **Minimize Corpus**: Regularly minimize corpus to remove redundant inputs
5. **Triage Crashes**: Investigate and fix crashes promptly
6. **Update Corpus**: Add interesting inputs from production to corpus

## Troubleshooting

### Fuzzer Doesn't Build

- Ensure you're using Clang compiler
- Verify `-DWITH_FUZZING=ON` is set
- Check that libFuzzer is available: `clang++ -fsanitize=fuzzer`

### Fuzzer Runs Slowly

- Reduce `-max_len` to limit input size
- Use `-jobs` and `-workers` for parallel execution
- Check for performance bottlenecks in code

### Out of Memory

- Set `-rss_limit_mb` to limit memory usage
- Reduce `-max_len` to limit input size
- Check for memory leaks in code

## Security Considerations

- All fuzzers run with AddressSanitizer and UndefinedBehaviorSanitizer
- Crashes should be treated as potential security issues
- Report security-sensitive crashes through proper channels
- Do not commit crash files to public repositories

## Contributing

When adding new fuzzers:

1. Create fuzzer in appropriate subdirectory
2. Implement `LLVMFuzzerTestOneInput()` function
3. Add to CMakeLists.txt
4. Create initial corpus
5. Test locally before submitting
6. Update this README

## Resources

- [LLVM libFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html)
- [Fuzzing Book](https://www.fuzzingbook.org/)
- [OSS-Fuzz](https://github.com/google/oss-fuzz)
- [Ceph Development Guide](https://docs.ceph.com/en/latest/dev/)

## License

Copyright (C) 2024 IBM Corporation

This is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License version 2.1, as published by the Free Software Foundation. See file COPYING.
