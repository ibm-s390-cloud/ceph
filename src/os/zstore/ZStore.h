// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

/*
 * ZStore - Simple and Fast Storage Backend
 *
 * Copyright (C) 2026
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_OSD_ZSTORE_H
#define CEPH_OSD_ZSTORE_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "include/buffer.h"
#include "os/ObjectStore.h"
#include "common/ceph_mutex.h"
#include "common/Finisher.h"
#include "common/PriorityCache.h"
#include "blk/BlockDevice.h"

class ZStore : public ObjectStore {
public:
  // Configuration
  struct Config {
    uint64_t block_size = 4096;           // 4KB blocks
    uint64_t device_size = 0;             // Auto-detected
    uint64_t cache_size = 256 * 1024 * 1024; // 256MB cache
    std::string data_path;
    bool direct_io = true;                // Use O_DIRECT for performance
  };

private:
  // Internal structures
  struct Collection;
  struct Onode;
  
  typedef std::shared_ptr<Collection> CollectionRef;
  typedef std::shared_ptr<Onode> OnodeRef;

  // Object metadata
  struct Onode {
    ghobject_t oid;
    uint64_t size = 0;
    uint64_t block_offset = 0;  // Physical block offset on device
    std::map<std::string, ceph::buffer::ptr> xattrs;
    std::map<std::string, ceph::buffer::list> omap;
    ceph::buffer::list omap_header;
    
    ceph::mutex lock = ceph::make_mutex("ZStore::Onode::lock");
    
    Onode(const ghobject_t& o) : oid(o) {}
  };

  // Collection metadata
  struct Collection : public CollectionImpl {
    ZStore* store;
    coll_t cid;
    std::map<ghobject_t, OnodeRef> object_map;
    ceph::mutex lock = ceph::make_mutex("ZStore::Collection::lock");
    
    Collection(CephContext* cct, ZStore* s, const coll_t& c)
      : CollectionImpl(cct, c), store(s), cid(c) {}
    
    void flush() override {}
    bool flush_commit(Context* c) override {
      if (c) {
        c->complete(0);
      }
      return true;
    }
  };

  // Transaction context
  struct TransContext {
    ZStore* store;
    CollectionRef coll;
    std::vector<OnodeRef> onodes;
    std::vector<std::function<void()>> ops;
    Context* on_commit = nullptr;
    
    TransContext(ZStore* s, CollectionRef c) : store(s), coll(c) {}
    ~TransContext() {
      if (on_commit) {
        on_commit->complete(0);
      }
    }
  };

  // Block allocator - simple bitmap-based
  class BlockAllocator {
    std::vector<uint64_t> bitmap;  // Each bit represents one block
    uint64_t total_blocks = 0;
    uint64_t free_blocks = 0;
    uint64_t next_free = 0;
    ceph::mutex lock = ceph::make_mutex("ZStore::BlockAllocator::lock");
    
  public:
    void init(uint64_t device_size, uint64_t block_size);
    int64_t allocate(uint64_t num_blocks);
    void release(uint64_t block_offset, uint64_t num_blocks);
    uint64_t get_free_blocks() const { return free_blocks; }
  };

  // Member variables
  Config config;
  CephContext* cct;
  std::string path;
  BlockDevice* bdev = nullptr;  // Block device for async I/O
  
  BlockAllocator allocator;
  std::map<coll_t, CollectionRef> coll_map;
  ceph::mutex coll_lock = ceph::make_mutex("ZStore::coll_lock");
  
  Finisher finisher;
  std::atomic<bool> mounted{false};
  
  PerfCounters* logger = nullptr;
  
  // Performance counters
  enum {
    l_zstore_first = 800000,
    l_zstore_write_lat,
    l_zstore_read_lat,
    l_zstore_commit_lat,
    l_zstore_bytes_written,
    l_zstore_bytes_read,
    l_zstore_allocated_blocks,
    l_zstore_free_blocks,
    l_zstore_last
  };

  // Internal methods
  int _open_device();
  void _close_device();
  static void _aio_callback(void* priv, void* priv2);
  int _aio_write(uint64_t offset, ceph::buffer::list& bl, IOContext* ioc);
  int _aio_read(uint64_t offset, uint64_t length, ceph::buffer::list& bl, IOContext* ioc);
  int _write_block(uint64_t block_offset, const ceph::buffer::list& bl);
  int _read_block(uint64_t block_offset, uint64_t length, ceph::buffer::list& bl);
  
  OnodeRef _get_onode(CollectionRef& c, const ghobject_t& oid);
  int _do_transaction(Transaction& t, CollectionRef& c, TransContext* txc);
  int _do_write(TransContext* txc, OnodeRef& o, uint64_t offset, 
                const ceph::buffer::list& bl);
  int _do_read(CollectionRef& c, OnodeRef& o, uint64_t offset, 
               size_t length, ceph::buffer::list& bl);
  int _do_remove(TransContext* txc, OnodeRef& o);
  
  void _setup_perf_counters();
  void _shutdown_perf_counters();

public:
  ZStore(CephContext* cct, const std::string& path);
  ~ZStore() override;

  // ObjectStore interface implementation
  std::string get_type() override { return "zstore"; }
  
  bool test_mount_in_use() override;
  int mount() override;
  int umount() override;
  int fsck(bool deep) override;
  int mkfs() override;
  int mkjournal() override { return 0; }
  
  bool needs_journal() override { return false; }
  bool wants_journal() override { return false; }
  bool allows_journal() override { return false; }
  
  uint64_t get_min_alloc_size() const override {
    return config.block_size;
  }
  
  int statfs(struct store_statfs_t* buf, osd_alert_list_t* alerts = nullptr) override;
  int pool_statfs(uint64_t pool_id, struct store_statfs_t* buf,
                  bool* per_pool_omap) override;
  
  bool exists(CollectionHandle& c, const ghobject_t& oid) override;
  int stat(CollectionHandle& c, const ghobject_t& oid, struct stat* st,
           bool allow_eio = false) override;
  
  int read(CollectionHandle& c, const ghobject_t& oid,
           uint64_t offset, size_t len, ceph::buffer::list& bl,
           uint32_t op_flags = 0) override;
  
  int fiemap(CollectionHandle& c, const ghobject_t& oid,
             uint64_t offset, size_t len, ceph::buffer::list& bl) override;
  int fiemap(CollectionHandle& c, const ghobject_t& oid,
             uint64_t offset, size_t len, 
             std::map<uint64_t, uint64_t>& destmap) override;
  
  int getattr(CollectionHandle& c, const ghobject_t& oid,
              const char* name, ceph::buffer::ptr& value) override;
  int getattrs(CollectionHandle& c, const ghobject_t& oid,
               std::map<std::string, ceph::buffer::ptr>& aset) override;
  
  int list_collections(std::vector<coll_t>& ls) override;
  bool collection_exists(const coll_t& c) override;
  int collection_empty(CollectionHandle& c, bool* empty) override;
  int collection_list(CollectionHandle& c, const ghobject_t& start,
                      const ghobject_t& end, int max,
                      std::vector<ghobject_t>* ls, ghobject_t* next) override;
  
  CollectionHandle open_collection(const coll_t& cid) override;
  CollectionHandle create_new_collection(const coll_t& cid) override;
  void set_collection_commit_queue(const coll_t& cid, 
                                   ContextQueue* commit_queue) override {}
  
  int queue_transactions(CollectionHandle& ch, std::vector<Transaction>& tls,
                         TrackedOpRef op = TrackedOpRef(),
                         ThreadPool::TPHandle* handle = nullptr) override;
  
  // OMAP operations
  int omap_get(CollectionHandle& c, const ghobject_t& oid,
               ceph::buffer::list* header,
               std::map<std::string, ceph::buffer::list>* out) override;
  int omap_get_header(CollectionHandle& c, const ghobject_t& oid,
                      ceph::buffer::list* header, bool allow_eio = false) override;
  int omap_get_keys(CollectionHandle& c, const ghobject_t& oid,
                    std::set<std::string>* keys) override;
  int omap_get_values(CollectionHandle& c, const ghobject_t& oid,
                      const std::set<std::string>& keys,
                      std::map<std::string, ceph::buffer::list>* out) override;
  int omap_check_keys(CollectionHandle& c, const ghobject_t& oid,
                      const std::set<std::string>& keys,
                      std::set<std::string>* out) override;
  
  ObjectMap::ObjectMapIterator get_omap_iterator(
    CollectionHandle& c, const ghobject_t& oid) override;
  
  // Performance and stats
  objectstore_perf_stat_t get_cur_stats() override;
  const PerfCounters* get_perf_counters() const override { return logger; }
  void refresh_perf_counters() override {}
  
  int validate_hobject_key(const hobject_t& obj) const override { return 0; }
  unsigned get_max_attr_name_length() override { return 256; }
  int set_collection_opts(CollectionHandle& c, const pool_opts_t& opts) override {
    return 0;
  }
};

#endif // CEPH_OSD_ZSTORE_H
