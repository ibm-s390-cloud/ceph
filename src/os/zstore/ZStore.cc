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

#include "ZStore.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "common/debug.h"
#include "common/errno.h"
#include "common/safe_io.h"
#include "common/blkdev.h"
#include "include/stringify.h"
#include "include/compat.h"
#include "blk/BlockDevice.h"

#define dout_context cct
#define dout_subsys ceph_subsys_filestore
#undef dout_prefix
#define dout_prefix *_dout << "zstore(" << path << ") "

using std::string;
using std::vector;
using std::map;
using std::set;
using ceph::bufferlist;
using ceph::bufferptr;

// ============================================================================
// BlockAllocator Implementation
// ============================================================================

void ZStore::BlockAllocator::init(uint64_t device_size, uint64_t block_size) {
  std::lock_guard l(lock);
  total_blocks = device_size / block_size;
  free_blocks = total_blocks;
  
  // Allocate bitmap (1 bit per block)
  size_t bitmap_size = (total_blocks + 63) / 64;
  bitmap.resize(bitmap_size, 0);
  
  next_free = 0;
}

int64_t ZStore::BlockAllocator::allocate(uint64_t num_blocks) {
  std::lock_guard l(lock);
  
  if (num_blocks > free_blocks) {
    return -ENOSPC;
  }
  
  // Simple first-fit allocation
  uint64_t start_block = next_free;
  uint64_t found = 0;
  uint64_t search_start = 0;
  
  for (uint64_t i = 0; i < total_blocks && found < num_blocks; ++i) {
    uint64_t block = (start_block + i) % total_blocks;
    uint64_t word_idx = block / 64;
    uint64_t bit_idx = block % 64;
    
    if (!(bitmap[word_idx] & (1ULL << bit_idx))) {
      if (found == 0) {
        search_start = block;
      }
      found++;
    } else {
      found = 0;
    }
  }
  
  if (found < num_blocks) {
    return -ENOSPC;
  }
  
  // Mark blocks as allocated
  for (uint64_t i = 0; i < num_blocks; ++i) {
    uint64_t block = search_start + i;
    uint64_t word_idx = block / 64;
    uint64_t bit_idx = block % 64;
    bitmap[word_idx] |= (1ULL << bit_idx);
  }
  
  free_blocks -= num_blocks;
  next_free = (search_start + num_blocks) % total_blocks;
  
  return search_start;
}

void ZStore::BlockAllocator::release(uint64_t block_offset, uint64_t num_blocks) {
  std::lock_guard l(lock);
  
  for (uint64_t i = 0; i < num_blocks; ++i) {
    uint64_t block = block_offset + i;
    if (block >= total_blocks) {
      break;
    }
    uint64_t word_idx = block / 64;
    uint64_t bit_idx = block % 64;
    bitmap[word_idx] &= ~(1ULL << bit_idx);
  }
  
  free_blocks += num_blocks;
  if (block_offset < next_free) {
    next_free = block_offset;
  }
}

// ============================================================================
// ZStore Implementation
// ============================================================================

ZStore::ZStore(CephContext* cct, const std::string& path_)
  : ObjectStore(cct, path_),
    cct(cct),
    path(path_),
    finisher(cct, "zstore_finisher", "zs_fin")
{
  config.data_path = path + "/block";
  config.block_size = cct->_conf.get_val<uint64_t>("zstore_block_size");
  config.cache_size = cct->_conf.get_val<uint64_t>("zstore_cache_size");
  config.direct_io = cct->_conf.get_val<bool>("zstore_direct_io");
}

ZStore::~ZStore() {
  _close_device();
  _shutdown_perf_counters();
}

// ============================================================================
// Device Management
// ============================================================================

void ZStore::_aio_callback(void* priv, void* priv2) {
  // AIO completion callback
  // priv is the ZStore instance, priv2 is the IOContext
}

int ZStore::_open_device() {
  dout(10) << __func__ << " opening " << config.data_path << dendl;
  
  // Create BlockDevice for async I/O
  bdev = BlockDevice::create(cct, config.data_path, _aio_callback, this, 
                             nullptr, nullptr);
  if (!bdev) {
    derr << __func__ << " failed to create block device" << dendl;
    return -EINVAL;
  }
  
  int r = bdev->open(config.data_path);
  if (r < 0) {
    derr << __func__ << " failed to open block device: " 
         << cpp_strerror(r) << dendl;
    delete bdev;
    bdev = nullptr;
    return r;
  }
  
  config.device_size = bdev->get_size();
  config.block_size = std::max(config.block_size, bdev->get_block_size());
  
  dout(10) << __func__ << " device size: " << config.device_size 
           << " block_size: " << config.block_size << dendl;
  
  return 0;
}

void ZStore::_close_device() {
  if (bdev) {
    dout(10) << __func__ << dendl;
    bdev->close();
    delete bdev;
    bdev = nullptr;
  }
}

int ZStore::_aio_write(uint64_t offset, bufferlist& bl, IOContext* ioc) {
  if (!bdev) {
    return -EINVAL;
  }
  
  dout(20) << __func__ << " offset=" << offset 
           << " len=" << bl.length() << dendl;
  
  int r = bdev->aio_write(offset, bl, ioc, false);
  if (r < 0) {
    derr << __func__ << " aio_write failed: " << cpp_strerror(r) << dendl;
    return r;
  }
  
  if (logger) {
    logger->inc(l_zstore_bytes_written, bl.length());
  }
  
  return 0;
}

int ZStore::_aio_read(uint64_t offset, uint64_t length, bufferlist& bl, IOContext* ioc) {
  if (!bdev) {
    return -EINVAL;
  }
  
  dout(20) << __func__ << " offset=" << offset 
           << " len=" << length << dendl;
  
  int r = bdev->aio_read(offset, length, &bl, ioc);
  if (r < 0) {
    derr << __func__ << " aio_read failed: " << cpp_strerror(r) << dendl;
    return r;
  }
  
  if (logger) {
    logger->inc(l_zstore_bytes_read, length);
  }
  
  return 0;
}

int ZStore::_write_block(uint64_t block_offset, const bufferlist& bl) {
  if (!bdev) {
    return -EINVAL;
  }
  
  uint64_t offset = block_offset * config.block_size;
  
  dout(20) << __func__ << " offset=" << offset 
           << " len=" << bl.length() << dendl;
  
  bufferlist write_bl = bl;
  int r = bdev->write(offset, write_bl, false);
  if (r < 0) {
    derr << __func__ << " write failed: " << cpp_strerror(r) << dendl;
    return r;
  }
  
  if (logger) {
    logger->inc(l_zstore_bytes_written, bl.length());
  }
  
  return 0;
}

int ZStore::_read_block(uint64_t block_offset, uint64_t length, bufferlist& bl) {
  if (!bdev) {
    return -EINVAL;
  }
  
  uint64_t offset = block_offset * config.block_size;
  
  dout(20) << __func__ << " offset=" << offset 
           << " len=" << length << dendl;
  
  bl.clear();
  IOContext ioc(cct, this);
  int r = bdev->read(offset, length, &bl, &ioc, false);
  if (r < 0) {
    derr << __func__ << " read failed: " << cpp_strerror(r) << dendl;
    return r;
  }
  
  if (logger) {
    logger->inc(l_zstore_bytes_read, r);
  }
  
  return r;
}

// ============================================================================
// Mount/Unmount
// ============================================================================

bool ZStore::test_mount_in_use() {
  string fn = path + "/lock";
  int fd = ::open(fn.c_str(), O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    return false;
  }
  
  struct flock l;
  memset(&l, 0, sizeof(l));
  l.l_type = F_WRLCK;
  l.l_whence = SEEK_SET;
  int r = ::fcntl(fd, F_SETLK, &l);
  ::close(fd);
  
  return r < 0;
}

int ZStore::mount() {
  dout(5) << __func__ << dendl;
  
  if (mounted) {
    return 0;
  }
  
  int r = _open_device();
  if (r < 0) {
    return r;
  }
  
  // Initialize allocator
  allocator.init(config.device_size, config.block_size);
  
  // Start finisher
  finisher.start();
  
  // Setup performance counters
  _setup_perf_counters();
  
  mounted = true;
  
  dout(5) << __func__ << " complete" << dendl;
  return 0;
}

int ZStore::umount() {
  dout(5) << __func__ << dendl;
  
  if (!mounted) {
    return 0;
  }
  
  // Stop finisher
  finisher.stop();
  
  // Close device
  _close_device();
  
  mounted = false;
  
  dout(5) << __func__ << " complete" << dendl;
  return 0;
}

int ZStore::mkfs() {
  dout(5) << __func__ << dendl;
  
  // Create data directory
  int r = ::mkdir(path.c_str(), 0755);
  if (r < 0 && errno != EEXIST) {
    r = -errno;
    derr << __func__ << " failed to create " << path 
         << ": " << cpp_strerror(r) << dendl;
    return r;
  }
  
  // Create block device file if it doesn't exist
  string block_path = path + "/block";
  int fd = ::open(block_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  if (fd < 0) {
    if (errno != EEXIST) {
      r = -errno;
      derr << __func__ << " failed to create " << block_path 
           << ": " << cpp_strerror(r) << dendl;
      return r;
    }
  } else {
    // Initialize with default size (10GB)
    uint64_t default_size = 10ULL * 1024 * 1024 * 1024;
    r = ::ftruncate(fd, default_size);
    ::close(fd);
    if (r < 0) {
      r = -errno;
      derr << __func__ << " failed to set size: " 
           << cpp_strerror(r) << dendl;
      return r;
    }
  }
  
  // Write metadata
  r = write_meta("type", "zstore");
  if (r < 0) {
    return r;
  }
  
  r = write_meta("zstore_version", "1");
  if (r < 0) {
    return r;
  }
  
  dout(5) << __func__ << " complete" << dendl;
  return 0;
}

int ZStore::fsck(bool deep) {
  dout(5) << __func__ << " deep=" << deep << dendl;
  // Simple implementation - just verify we can open the device
  int r = _open_device();
  if (r < 0) {
    return r;
  }
  _close_device();
  return 0;
}

// ============================================================================
// Statistics
// ============================================================================

int ZStore::statfs(struct store_statfs_t* buf, osd_alert_list_t* alerts) {
  buf->reset();
  buf->total = config.device_size;
  buf->available = allocator.get_free_blocks() * config.block_size;
  buf->allocated = buf->total - buf->available;
  buf->data_stored = buf->allocated;  // Simplified
  
  if (logger) {
    logger->set(l_zstore_allocated_blocks, 
                (buf->total - buf->available) / config.block_size);
    logger->set(l_zstore_free_blocks, allocator.get_free_blocks());
  }
  
  return 0;
}

int ZStore::pool_statfs(uint64_t pool_id, struct store_statfs_t* buf,
                        bool* per_pool_omap) {
  return statfs(buf, nullptr);
}

// ============================================================================
// Object Operations
// ============================================================================

ZStore::OnodeRef ZStore::_get_onode(CollectionRef& c, const ghobject_t& oid) {
  std::lock_guard l(c->lock);
  
  auto it = c->object_map.find(oid);
  if (it != c->object_map.end()) {
    return it->second;
  }
  
  // Create new onode
  OnodeRef o = std::make_shared<Onode>(oid);
  c->object_map[oid] = o;
  return o;
}

bool ZStore::exists(CollectionHandle& ch, const ghobject_t& oid) {
  auto c = static_cast<Collection*>(ch.get());
  std::lock_guard l(c->lock);
  return c->object_map.find(oid) != c->object_map.end();
}

int ZStore::stat(CollectionHandle& ch, const ghobject_t& oid,
                 struct stat* st, bool allow_eio) {
  auto c = static_cast<Collection*>(ch.get());
  std::lock_guard l(c->lock);
  
  auto it = c->object_map.find(oid);
  if (it == c->object_map.end()) {
    return -ENOENT;
  }
  
  memset(st, 0, sizeof(*st));
  st->st_size = it->second->size;
  st->st_blksize = config.block_size;
  st->st_blocks = (st->st_size + config.block_size - 1) / config.block_size;
  
  return 0;
}

int ZStore::read(CollectionHandle& ch, const ghobject_t& oid,
                 uint64_t offset, size_t len, bufferlist& bl,
                 uint32_t op_flags) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  return _do_read(c, o, offset, len, bl);
}

int ZStore::_do_read(CollectionRef& c, OnodeRef& o, uint64_t offset,
                     size_t length, bufferlist& bl) {
  std::lock_guard l(o->lock);
  
  if (offset >= o->size) {
    return 0;
  }
  
  uint64_t read_len = std::min(length, o->size - offset);
  uint64_t block_offset = o->block_offset + (offset / config.block_size);
  
  return _read_block(block_offset, read_len, bl);
}

int ZStore::fiemap(CollectionHandle& ch, const ghobject_t& oid,
                   uint64_t offset, size_t len, bufferlist& bl) {
  // Simple implementation - return single extent
  map<uint64_t, uint64_t> m;
  m[offset] = len;
  encode(m, bl);
  return 0;
}

int ZStore::fiemap(CollectionHandle& ch, const ghobject_t& oid,
                   uint64_t offset, size_t len,
                   map<uint64_t, uint64_t>& destmap) {
  destmap[offset] = len;
  return 0;
}

// ============================================================================
// Attribute Operations
// ============================================================================

int ZStore::getattr(CollectionHandle& ch, const ghobject_t& oid,
                    const char* name, bufferptr& value) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  std::lock_guard l(o->lock);
  auto it = o->xattrs.find(name);
  if (it == o->xattrs.end()) {
    return -ENODATA;
  }
  
  value = it->second;
  return 0;
}

int ZStore::getattrs(CollectionHandle& ch, const ghobject_t& oid,
                     map<string, bufferptr>& aset) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  std::lock_guard l(o->lock);
  aset = o->xattrs;
  return 0;
}

// ============================================================================
// Collection Operations
// ============================================================================

int ZStore::list_collections(vector<coll_t>& ls) {
  std::lock_guard l(coll_lock);
  for (auto& p : coll_map) {
    ls.push_back(p.first);
  }
  return 0;
}

bool ZStore::collection_exists(const coll_t& c) {
  std::lock_guard l(coll_lock);
  return coll_map.find(c) != coll_map.end();
}

int ZStore::collection_empty(CollectionHandle& ch, bool* empty) {
  auto c = static_cast<Collection*>(ch.get());
  std::lock_guard l(c->lock);
  *empty = c->object_map.empty();
  return 0;
}

int ZStore::collection_list(CollectionHandle& ch, const ghobject_t& start,
                            const ghobject_t& end, int max,
                            vector<ghobject_t>* ls, ghobject_t* next) {
  auto c = static_cast<Collection*>(ch.get());
  std::lock_guard l(c->lock);
  
  int count = 0;
  for (auto& p : c->object_map) {
    if (p.first >= start && p.first < end) {
      if (count < max) {
        ls->push_back(p.first);
        count++;
      } else {
        if (next) {
          *next = p.first;
        }
        break;
      }
    }
  }
  
  return 0;
}

CollectionHandle ZStore::open_collection(const coll_t& cid) {
  std::lock_guard l(coll_lock);
  
  auto it = coll_map.find(cid);
  if (it != coll_map.end()) {
    return it->second;
  }
  
  return CollectionHandle();
}

CollectionHandle ZStore::create_new_collection(const coll_t& cid) {
  std::lock_guard l(coll_lock);
  
  auto c = std::make_shared<Collection>(cct, this, cid);
  coll_map[cid] = c;
  return c;
}

// ============================================================================
// Transaction Processing
// ============================================================================

int ZStore::queue_transactions(CollectionHandle& ch, vector<Transaction>& tls,
                               TrackedOpRef op,
                               ThreadPool::TPHandle* handle) {
  auto c = static_cast<Collection*>(ch.get());
  
  for (auto& t : tls) {
    TransContext* txc = new TransContext(this, c);
    int r = _do_transaction(t, c, txc);
    if (r < 0) {
      delete txc;
      return r;
    }
    delete txc;
  }
  
  return 0;
}

int ZStore::_do_transaction(Transaction& t, CollectionRef& c, TransContext* txc) {
  dout(10) << __func__ << " " << t << dendl;
  
  Transaction::iterator i = t.begin();
  
  while (i.have_op()) {
    Transaction::Op* op = i.decode_op();
    int r = 0;
    
    switch (op->op) {
    case Transaction::OP_WRITE:
      {
        coll_t cid = i.get_cid(op->cid);
        ghobject_t oid = i.get_oid(op->oid);
        uint64_t off = op->off;
        uint64_t len = op->len;
        bufferlist bl;
        i.decode_bl(bl);
        
        OnodeRef o = _get_onode(c, oid);
        r = _do_write(txc, o, off, bl);
      }
      break;
      
    case Transaction::OP_REMOVE:
      {
        coll_t cid = i.get_cid(op->cid);
        ghobject_t oid = i.get_oid(op->oid);
        OnodeRef o = _get_onode(c, oid);
        r = _do_remove(txc, o);
      }
      break;
      
    case Transaction::OP_COLL_CREATE:
      {
        coll_t cid = i.get_cid(op->cid);
        std::lock_guard l(coll_lock);
        if (coll_map.find(cid) == coll_map.end()) {
          coll_map[cid] = std::make_shared<Collection>(cct, this, cid);
        }
      }
      break;
      
    default:
      derr << __func__ << " unknown op " << (int)op->op << dendl;
      r = -EINVAL;
      break;
    }
    
    if (r < 0) {
      return r;
    }
  }
  
  return 0;
}

int ZStore::_do_write(TransContext* txc, OnodeRef& o, uint64_t offset,
                      const bufferlist& bl) {
  std::lock_guard l(o->lock);
  
  // Allocate blocks if needed
  if (o->block_offset == 0) {
    uint64_t num_blocks = (bl.length() + config.block_size - 1) / config.block_size;
    int64_t block = allocator.allocate(num_blocks);
    if (block < 0) {
      return block;
    }
    o->block_offset = block;
  }
  
  // Use async I/O for better performance
  uint64_t device_offset = (o->block_offset + (offset / config.block_size)) * config.block_size;
  IOContext ioc(cct, this);
  bufferlist write_bl = bl;
  
  int r = _aio_write(device_offset, write_bl, &ioc);
  if (r < 0) {
    return r;
  }
  
  // Submit and wait for I/O completion
  if (ioc.has_pending_aios()) {
    bdev->aio_submit(&ioc);
    ioc.aio_wait();
    r = ioc.get_return_value();
    if (r < 0) {
      return r;
    }
  }
  
  o->size = std::max(o->size, offset + bl.length());
  return 0;
}

int ZStore::_do_remove(TransContext* txc, OnodeRef& o) {
  std::lock_guard l(o->lock);
  
  if (o->block_offset > 0) {
    uint64_t num_blocks = (o->size + config.block_size - 1) / config.block_size;
    allocator.release(o->block_offset, num_blocks);
    o->block_offset = 0;
  }
  
  o->size = 0;
  return 0;
}

// ============================================================================
// OMAP Operations (Simplified)
// ============================================================================

int ZStore::omap_get(CollectionHandle& ch, const ghobject_t& oid,
                     bufferlist* header,
                     map<string, bufferlist>* out) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  std::lock_guard l(o->lock);
  if (header) {
    *header = o->omap_header;
  }
  if (out) {
    *out = o->omap;
  }
  return 0;
}

int ZStore::omap_get_header(CollectionHandle& ch, const ghobject_t& oid,
                            bufferlist* header, bool allow_eio) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  std::lock_guard l(o->lock);
  *header = o->omap_header;
  return 0;
}

int ZStore::omap_get_keys(CollectionHandle& ch, const ghobject_t& oid,
                          set<string>* keys) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  std::lock_guard l(o->lock);
  for (auto& p : o->omap) {
    keys->insert(p.first);
  }
  return 0;
}

int ZStore::omap_get_values(CollectionHandle& ch, const ghobject_t& oid,
                            const set<string>& keys,
                            map<string, bufferlist>* out) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  std::lock_guard l(o->lock);
  for (auto& k : keys) {
    auto it = o->omap.find(k);
    if (it != o->omap.end()) {
      (*out)[k] = it->second;
    }
  }
  return 0;
}

int ZStore::omap_check_keys(CollectionHandle& ch, const ghobject_t& oid,
                            const set<string>& keys, set<string>* out) {
  auto c = static_cast<Collection*>(ch.get());
  OnodeRef o = _get_onode(c, oid);
  
  std::lock_guard l(o->lock);
  for (auto& k : keys) {
    if (o->omap.find(k) != o->omap.end()) {
      out->insert(k);
    }
  }
  return 0;
}

ObjectMap::ObjectMapIterator ZStore::get_omap_iterator(
  CollectionHandle& ch, const ghobject_t& oid) {
  // Return empty iterator for now
  return ObjectMap::ObjectMapIterator();
}

// ============================================================================
// Performance Counters
// ============================================================================

void ZStore::_setup_perf_counters() {
  PerfCountersBuilder b(cct, "zstore", l_zstore_first, l_zstore_last);
  
  b.add_time_avg(l_zstore_write_lat, "write_lat", "Write latency");
  b.add_time_avg(l_zstore_read_lat, "read_lat", "Read latency");
  b.add_time_avg(l_zstore_commit_lat, "commit_lat", "Commit latency");
  b.add_u64_counter(l_zstore_bytes_written, "bytes_written", "Bytes written");
  b.add_u64_counter(l_zstore_bytes_read, "bytes_read", "Bytes read");
  b.add_u64(l_zstore_allocated_blocks, "allocated_blocks", "Allocated blocks");
  b.add_u64(l_zstore_free_blocks, "free_blocks", "Free blocks");
  
  logger = b.create_perf_counters();
  cct->get_perfcounters_collection()->add(logger);
}

void ZStore::_shutdown_perf_counters() {
  if (logger) {
    cct->get_perfcounters_collection()->remove(logger);
    delete logger;
    logger = nullptr;
  }
}

objectstore_perf_stat_t ZStore::get_cur_stats() {
  objectstore_perf_stat_t ret;
  ret.os_commit_latency_ns = 0;
  ret.os_apply_latency_ns = 0;
  return ret;
}
