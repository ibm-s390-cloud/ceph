/*
 * Copyright IBM Corp. 2015
 */
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <stdint.h>

/* Portable implementations of CRC-32 (IEEE and Castagnoli), both
   big-endian and little-endian variants. */
unsigned int crc32_be(unsigned int, const unsigned char *, size_t);
unsigned int crc32_le(unsigned int, const unsigned char *, size_t);
unsigned int crc32c_be(uint32_t, unsigned char const*, unsigned);
unsigned int crc32c_le(uint32_t, unsigned char const*, unsigned);

/* Hardware-accelerated versions of the above. It is up to the caller
   to detect the availability of vector facility and kernel support. */
unsigned int crc32_be_vx(unsigned int, const unsigned char *, size_t);
unsigned int crc32_le_vx(unsigned int, const unsigned char *, size_t);
unsigned int crc32c_be_vx(uint32_t, unsigned char const*, unsigned);
unsigned int crc32c_le_vx(uint32_t, unsigned char const*, unsigned);

#ifdef __cplusplus
}
#endif

