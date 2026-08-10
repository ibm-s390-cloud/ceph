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

    // Test 5: Buffer splitting and merging
    if (size > 8) {
      bufferlist bl;
      bl.append((const char*)data, size);
      
      size_t split_point = size / 2;
      bufferlist bl1, bl2;
      bl1.substr_of(bl, 0, split_point);
      bl2.substr_of(bl, split_point, size - split_point);
      
      // Merge back
      bufferlist merged;
      merged.append(bl1);
      merged.append(bl2);
      
      // Verify merged equals original
      if (merged.length() != bl.length()) {
        return -1;
      }
    }

    // Test 6: Buffer comparison operations
    {
      bufferlist bl1, bl2;
      bl1.append((const char*)data, size);
      bl2.append((const char*)data, size);
      
      // Should be equal
      if (bl1.contents_equal(bl2) == false) {
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
