// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:nil -*-
// vim: ts=8 sw=2 sts=2 expandtab

/*
 * ZStore Configuration Options
 *
 * Copyright (C) 2026
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_ZSTORE_CONFIG_H
#define CEPH_ZSTORE_CONFIG_H

// ZStore configuration options with defaults
// These can be overridden in ceph.conf under [osd] or [global] sections

// Block size for allocation (default: 4KB)
// zstore_block_size = 4096

// Cache size in bytes (default: 256MB)
// zstore_cache_size = 268435456

// Use direct I/O for better performance (default: true)
// zstore_direct_io = true

// Maximum object size (default: 100MB)
// zstore_max_object_size = 104857600

// Number of finisher threads (default: 1)
// zstore_finisher_threads = 1

#endif // CEPH_ZSTORE_CONFIG_H
