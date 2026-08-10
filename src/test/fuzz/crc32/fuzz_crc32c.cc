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

  // Test with multiple splits
  if (size > 12) {
    size_t third = size / 3;
    uint32_t crc_full = ceph_crc32c(0, data, size);
    uint32_t crc1 = ceph_crc32c(0, data, third);
    uint32_t crc2 = ceph_crc32c(crc1, data + third, third);
    uint32_t crc3 = ceph_crc32c(crc2, data + 2*third, size - 2*third);
    
    if (crc_full != crc3) {
      return -1;
    }
  }

  // Test determinism - same input should always produce same output
  {
    uint32_t crc_first = ceph_crc32c(0, data, size);
    uint32_t crc_second = ceph_crc32c(0, data, size);
    
    if (crc_first != crc_second) {
      // CRC calculation must be deterministic
      return -1;
    }
  }

  return 0;
}
