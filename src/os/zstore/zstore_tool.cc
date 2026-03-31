// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

/*
 * ZStore Tool - Management and debugging utility for ZStore
 *
 * Copyright (C) 2026
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <iostream>
#include <string>
#include <vector>

#include "ZStore.h"
#include "common/ceph_argparse.h"
#include "common/config.h"
#include "global/global_init.h"
#include "include/stringify.h"

using namespace std;

void usage() {
  cout << "usage: ceph-zstore-tool <data-path> <command> [options]\n"
       << "\n"
       << "Commands:\n"
       << "  fsck [--deep]              Check filesystem consistency\n"
       << "  show-label                 Show device label\n"
       << "  set-label <label>          Set device label\n"
       << "  list-colls                 List all collections\n"
       << "  list <coll>                List objects in collection\n"
       << "  stat <coll> <obj>          Show object statistics\n"
       << "  dump-super                 Dump superblock\n"
       << "  allocator-stats            Show allocator statistics\n"
       << "  help                       Show this help\n"
       << "\n"
       << "Options:\n"
       << "  --deep                     Perform deep fsck\n"
       << "  --debug                    Enable debug output\n"
       << endl;
}

int main(int argc, const char **argv) {
  vector<const char*> args;
  auto cct = global_init(nullptr, args, CEPH_ENTITY_TYPE_OSD,
                         CODE_ENVIRONMENT_UTILITY,
                         CINIT_FLAG_NO_DEFAULT_CONFIG_FILE);
  
  common_init_finish(g_ceph_context);
  
  if (argc < 3) {
    usage();
    return 1;
  }
  
  string data_path = argv[1];
  string command = argv[2];
  
  // Parse options
  bool deep = false;
  bool debug = false;
  
  for (int i = 3; i < argc; i++) {
    string arg = argv[i];
    if (arg == "--deep") {
      deep = true;
    } else if (arg == "--debug") {
      debug = true;
    }
  }
  
  if (command == "help" || command == "--help" || command == "-h") {
    usage();
    return 0;
  }
  
  // Create ZStore instance
  ZStore store(g_ceph_context, data_path);
  
  int r = 0;
  
  if (command == "fsck") {
    cout << "Running fsck on " << data_path;
    if (deep) {
      cout << " (deep)";
    }
    cout << "..." << endl;
    
    r = store.fsck(deep);
    if (r < 0) {
      cerr << "fsck failed: " << cpp_strerror(r) << endl;
      return 1;
    }
    cout << "fsck completed successfully" << endl;
    
  } else if (command == "show-label") {
    string label;
    r = store.read_meta("label", &label);
    if (r < 0) {
      cerr << "Failed to read label: " << cpp_strerror(r) << endl;
      return 1;
    }
    cout << "Label: " << label << endl;
    
  } else if (command == "set-label") {
    if (argc < 4) {
      cerr << "Error: label argument required" << endl;
      usage();
      return 1;
    }
    string label = argv[3];
    r = store.write_meta("label", label);
    if (r < 0) {
      cerr << "Failed to set label: " << cpp_strerror(r) << endl;
      return 1;
    }
    cout << "Label set to: " << label << endl;
    
  } else if (command == "dump-super") {
    cout << "ZStore Superblock:" << endl;
    cout << "  Path: " << data_path << endl;
    
    string type, version;
    store.read_meta("type", &type);
    store.read_meta("zstore_version", &version);
    
    cout << "  Type: " << type << endl;
    cout << "  Version: " << version << endl;
    
    // Mount to get more info
    r = store.mount();
    if (r == 0) {
      struct store_statfs_t statfs;
      store.statfs(&statfs, nullptr);
      
      cout << "  Total: " << statfs.total << " bytes" << endl;
      cout << "  Available: " << statfs.available << " bytes" << endl;
      cout << "  Allocated: " << statfs.allocated << " bytes" << endl;
      
      store.umount();
    }
    
  } else if (command == "allocator-stats") {
    r = store.mount();
    if (r < 0) {
      cerr << "Failed to mount: " << cpp_strerror(r) << endl;
      return 1;
    }
    
    struct store_statfs_t statfs;
    store.statfs(&statfs, nullptr);
    
    cout << "Allocator Statistics:" << endl;
    cout << "  Total space: " << statfs.total << " bytes" << endl;
    cout << "  Free space: " << statfs.available << " bytes" << endl;
    cout << "  Allocated: " << statfs.allocated << " bytes" << endl;
    cout << "  Utilization: " 
         << (100.0 * statfs.allocated / statfs.total) << "%" << endl;
    
    store.umount();
    
  } else if (command == "list-colls") {
    r = store.mount();
    if (r < 0) {
      cerr << "Failed to mount: " << cpp_strerror(r) << endl;
      return 1;
    }
    
    vector<coll_t> colls;
    r = store.list_collections(colls);
    if (r < 0) {
      cerr << "Failed to list collections: " << cpp_strerror(r) << endl;
      store.umount();
      return 1;
    }
    
    cout << "Collections (" << colls.size() << "):" << endl;
    for (auto& c : colls) {
      cout << "  " << c << endl;
    }
    
    store.umount();
    
  } else {
    cerr << "Unknown command: " << command << endl;
    usage();
    return 1;
  }
  
  return 0;
}
