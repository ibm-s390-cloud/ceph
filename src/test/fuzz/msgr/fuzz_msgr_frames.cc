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

#include "include/buffer.h"
#include "include/encoding.h"

using namespace ceph;

// Fuzzer for Ceph messenger protocol frame parsing
// This is a simplified fuzzer that tests buffer handling and basic frame structure
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 16) {
    return 0;
  }

  try {
    bufferlist bl;
    bl.append((const char*)data, size);
    
    // Try to parse as different frame types
    auto it = bl.cbegin();
    
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
    
    // Test buffer operations with frame-like data
    {
      bufferlist frame_bl;
      frame_bl.append((const char*)data, std::min(size, (size_t)1024));
      
      // Test CRC calculation on frame
      uint32_t crc = frame_bl.crc32c(0);
      
      // Test frame splitting
      if (frame_bl.length() > 8) {
        bufferlist header, payload;
        header.substr_of(frame_bl, 0, 8);
        payload.substr_of(frame_bl, 8, frame_bl.length() - 8);
      }
    }
    
  } catch (const buffer::error& e) {
    return 0;
  } catch (const std::exception& e) {
    return -1;
  }

  return 0;
}
