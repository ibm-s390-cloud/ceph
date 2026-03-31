# ZStore - Simple and Fast Storage Backend

## Overview

ZStore is a lightweight, high-performance storage backend for Ceph that provides direct block device access without the complexity of BlueStore. It's designed for scenarios where simplicity and predictable performance are more important than advanced features.

## Architecture

### Key Components

1. **Direct Block Device I/O**
   - Raw block device access using O_DIRECT
   - No filesystem layer overhead
   - Predictable I/O patterns

2. **Simple Block Allocator**
   - Bitmap-based allocation
   - First-fit allocation strategy
   - Fast allocation and deallocation

3. **In-Memory Metadata**
   - Object metadata stored in memory
   - Fast metadata lookups
   - Simple collection management

4. **Synchronous Operations**
   - No complex transaction journaling
   - Direct writes to block device
   - Simplified error handling

## Features

### Implemented
- ✅ Direct block device I/O
- ✅ Basic object CRUD operations
- ✅ Collection management
- ✅ Extended attributes (xattrs)
- ✅ OMAP support (in-memory)
- ✅ Performance counters
- ✅ Block allocation/deallocation
- ✅ Configurable block size
- ✅ Direct I/O support

### Not Implemented (Simplified)
- ❌ Compression
- ❌ Checksumming
- ❌ Deferred writes
- ❌ Write-ahead logging
- ❌ Tiered storage (WAL/DB devices)
- ❌ Advanced caching strategies

## Configuration

Add to `ceph.conf`:

```ini
[osd]
osd_objectstore = zstore

# Optional ZStore-specific settings
zstore_block_size = 4096          # 4KB blocks (default)
zstore_cache_size = 268435456     # 256MB cache (default)
zstore_direct_io = true           # Use O_DIRECT (default)
```

## Usage

### Creating a New OSD with ZStore

```bash
# Create the OSD data directory
mkdir -p /var/lib/ceph/osd/ceph-0

# Initialize ZStore
ceph-osd --mkfs -i 0 --osd-objectstore zstore

# Start the OSD
ceph-osd -i 0
```

### Converting from BlueStore

**Warning:** This is destructive and will erase all data!

```bash
# Stop the OSD
systemctl stop ceph-osd@0

# Backup data if needed
ceph-objectstore-tool --data-path /var/lib/ceph/osd/ceph-0 --op export --file backup.dat

# Reinitialize with ZStore
rm -rf /var/lib/ceph/osd/ceph-0/*
ceph-osd --mkfs -i 0 --osd-objectstore zstore

# Restore data (if applicable)
ceph-objectstore-tool --data-path /var/lib/ceph/osd/ceph-0 --op import --file backup.dat

# Start the OSD
systemctl start ceph-osd@0
```

## Performance Characteristics

### Strengths
- **Low Latency**: Direct I/O eliminates filesystem overhead
- **Predictable**: Simple allocation strategy, no background compaction
- **Fast Metadata**: In-memory metadata for quick lookups
- **Simple**: Easier to debug and understand

### Limitations
- **No Compression**: Objects stored uncompressed
- **No Checksums**: No data integrity verification
- **Memory Usage**: All metadata kept in memory
- **Single Device**: No tiered storage support

## Performance Tuning

### Block Size
```ini
zstore_block_size = 4096   # For small random I/O
zstore_block_size = 65536  # For large sequential I/O
```

### Cache Size
```ini
zstore_cache_size = 536870912  # 512MB for metadata-heavy workloads
zstore_cache_size = 134217728  # 128MB for memory-constrained systems
```

### Direct I/O
```ini
zstore_direct_io = true   # Best for SSDs/NVMe
zstore_direct_io = false  # May help with HDDs (uses page cache)
```

## Monitoring

### Performance Counters

ZStore exposes the following performance counters:

- `zstore.write_lat` - Write operation latency
- `zstore.read_lat` - Read operation latency
- `zstore.commit_lat` - Transaction commit latency
- `zstore.bytes_written` - Total bytes written
- `zstore.bytes_read` - Total bytes read
- `zstore.allocated_blocks` - Number of allocated blocks
- `zstore.free_blocks` - Number of free blocks

### Viewing Metrics

```bash
# Via admin socket
ceph daemon osd.0 perf dump | jq '.zstore'

# Via Ceph CLI
ceph osd perf
```

## Troubleshooting

### OSD Won't Start

1. Check logs: `journalctl -u ceph-osd@0 -f`
2. Verify block device: `ls -l /var/lib/ceph/osd/ceph-0/block`
3. Check permissions: `ls -la /var/lib/ceph/osd/ceph-0/`

### Performance Issues

1. Check if direct I/O is enabled
2. Verify block size matches workload
3. Monitor allocation fragmentation
4. Check device I/O stats: `iostat -x 1`

### Out of Space

```bash
# Check allocation
ceph daemon osd.0 perf dump | jq '.zstore.free_blocks'

# Check actual usage
df -h /var/lib/ceph/osd/ceph-0/
```

## Development

### Building

```bash
cd /path/to/ceph
mkdir build && cd build
cmake -DWITH_ZSTORE=ON ..
make -j$(nproc)
```

### Testing

```bash
# Unit tests
cd build
ctest -R zstore

# Integration test
./bin/ceph-osd --mkfs -i 0 --osd-objectstore zstore --osd-data /tmp/test-osd
```

### Code Structure

```
src/os/zstore/
├── ZStore.h              # Main header with class definitions
├── ZStore.cc             # Implementation
├── CMakeLists.txt        # Build configuration
├── zstore_config.h       # Configuration options
└── README.md             # This file
```

## Comparison with Other Backends

| Feature | ZStore | BlueStore | MemStore |
|---------|--------|-----------|----------|
| Persistence | Yes | Yes | No |
| Compression | No | Yes | No |
| Checksums | No | Yes | No |
| Complexity | Low | High | Low |
| Performance | High | High | Highest |
| Memory Usage | Medium | Medium | High |
| Production Ready | No | Yes | No |

## Use Cases

### Ideal For
- Development and testing
- Performance benchmarking
- Learning Ceph internals
- Scenarios requiring predictable latency
- Systems with limited memory for metadata

### Not Recommended For
- Production deployments (use BlueStore)
- Data requiring integrity checks
- Workloads needing compression
- Multi-tiered storage setups

## Future Enhancements

Potential improvements (not currently implemented):

1. **Persistent Metadata**: Store metadata on disk for crash recovery
2. **Basic Checksumming**: Optional CRC32 for data integrity
3. **Write Coalescing**: Batch small writes for efficiency
4. **Async I/O**: Use io_uring for better performance
5. **Wear Leveling**: Better block allocation for SSDs

## Contributing

To contribute to ZStore:

1. Follow Ceph coding standards
2. Add unit tests for new features
3. Update this README
4. Submit pull request to Ceph project

## License

ZStore is part of Ceph and is licensed under LGPL 2.1.

## Contact

For questions or issues:
- Ceph mailing list: ceph-devel@vger.kernel.org
- IRC: #ceph-devel on OFTC

## References

- [Ceph ObjectStore Documentation](https://docs.ceph.com/en/latest/dev/internals/)
- [BlueStore Design](https://docs.ceph.com/en/latest/rados/configuration/bluestore-config-ref/)
- [Ceph Development Guide](https://docs.ceph.com/en/latest/dev/)
