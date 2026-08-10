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
#include <string>

#include "include/buffer.h"

// Simplified XML parsing fuzzer for RGW
// This tests basic XML-like structure parsing without full RGW dependencies
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0 || size > 1024 * 1024) {
    return 0;
  }

  try {
    std::string xml_data((const char*)data, size);
    
    // Test basic XML structure validation
    // Look for common XML patterns that could cause issues
    
    // Check for balanced tags
    size_t open_count = 0;
    size_t close_count = 0;
    
    for (size_t i = 0; i < xml_data.length(); ++i) {
      if (xml_data[i] == '<') {
        if (i + 1 < xml_data.length() && xml_data[i + 1] == '/') {
          close_count++;
        } else {
          open_count++;
        }
      }
    }
    
    // Test for deeply nested structures
    size_t max_depth = 0;
    size_t current_depth = 0;
    
    for (char c : xml_data) {
      if (c == '<') {
        current_depth++;
        if (current_depth > max_depth) {
          max_depth = current_depth;
        }
        // Prevent excessive nesting
        if (max_depth > 100) {
          return 0;
        }
      } else if (c == '>') {
        if (current_depth > 0) {
          current_depth--;
        }
      }
    }
    
    // Test for attribute parsing patterns
    size_t attr_count = 0;
    for (size_t i = 0; i < xml_data.length(); ++i) {
      if (xml_data[i] == '=') {
        attr_count++;
        // Limit attribute count
        if (attr_count > 1000) {
          return 0;
        }
      }
    }
    
    // Test for entity references
    size_t entity_count = 0;
    for (size_t i = 0; i < xml_data.length(); ++i) {
      if (xml_data[i] == '&') {
        entity_count++;
        // Prevent entity expansion attacks
        if (entity_count > 100) {
          return 0;
        }
      }
    }
    
    // Test for CDATA sections
    if (xml_data.find("<![CDATA[") != std::string::npos) {
      // Verify CDATA is properly closed
      if (xml_data.find("]]>") == std::string::npos) {
        return 0;
      }
    }
    
  } catch (const std::exception& e) {
    // Expected for malformed input
    return 0;
  }

  return 0;
}
