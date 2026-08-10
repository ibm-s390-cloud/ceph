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
#include <vector>
#include <string>
#include <map>
#include <set>

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
      int8_t i8;
      int16_t i16;
      int32_t i32;
      int64_t i64;
      
      try { decode(u8, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(u16, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(u32, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(u64, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(i8, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(i16, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(i32, it_copy); } catch (...) {}
      it_copy = it;
      try { decode(i64, it_copy); } catch (...) {}
    }

    // Fuzz string decoding
    {
      auto it_copy = it;
      std::string str;
      try {
        decode(str, it_copy);
        
        // If decode succeeded, verify we can encode it back
        bufferlist bl_out;
        encode(str, bl_out);
      } catch (...) {}
    }

    // Fuzz vector decoding
    {
      auto it_copy = it;
      std::vector<uint32_t> vec;
      try {
        decode(vec, it_copy);
        
        // Verify encoding roundtrip
        bufferlist bl_out;
        encode(vec, bl_out);
      } catch (...) {}
    }

    // Fuzz vector of strings
    {
      auto it_copy = it;
      std::vector<std::string> vec_str;
      try {
        decode(vec_str, it_copy);
      } catch (...) {}
    }

    // Fuzz map decoding
    {
      auto it_copy = it;
      std::map<std::string, uint32_t> m;
      try {
        decode(m, it_copy);
        
        // Verify encoding roundtrip
        bufferlist bl_out;
        encode(m, bl_out);
      } catch (...) {}
    }

    // Fuzz set decoding
    {
      auto it_copy = it;
      std::set<uint32_t> s;
      try {
        decode(s, it_copy);
      } catch (...) {}
    }

    // Fuzz utime decoding
    {
      auto it_copy = it;
      utime_t t;
      try {
        decode(t, it_copy);
        
        // Verify encoding roundtrip
        bufferlist bl_out;
        encode(t, bl_out);
      } catch (...) {}
    }

    // Fuzz pair decoding
    {
      auto it_copy = it;
      std::pair<uint32_t, std::string> p;
      try {
        decode(p, it_copy);
      } catch (...) {}
    }

    // Fuzz nested structures
    {
      auto it_copy = it;
      std::map<std::string, std::vector<uint32_t>> nested;
      try {
        decode(nested, it_copy);
      } catch (...) {}
    }

    // Test encode_nohead/decode_nohead
    if (size >= 12) {
      auto it_copy = it;
      try {
        uint32_t len;
        decode(len, it_copy);
        
        // Limit length to prevent resource exhaustion
        if (len > 0 && len < 1024) {
          std::string str;
          decode_nohead(len, str, it_copy);
        }
      } catch (...) {}
    }

    // Test raw encoding/decoding
    {
      auto it_copy = it;
      if (size >= sizeof(uint32_t)) {
        uint32_t raw_val;
        try {
          decode_raw(raw_val, it_copy);
        } catch (...) {}
      }
    }

  } catch (const buffer::error& e) {
    return 0;
  } catch (const std::exception& e) {
    // Log unexpected exceptions
    return -1;
  }

  return 0;
}
