/*
 * CRC-32 algorithms implemented with the z/Architecture
 * Vector Extension Facility.
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 *
 */
#include <sys/types.h>
#include <endian.h>
#include "crc32c_s390x.h"
#include "slicing-consts.h"

#define VX_MIN_LEN		64
#define VX_ALIGNMENT		16L
#define VX_ALIGN_MASK		(VX_ALIGNMENT - 1)

/* Prototypes for functions in assembly files */
unsigned int crc32c_le_vgfm_16(uint32_t crc, unsigned char const*buf, unsigned size);

/* Pure C implementations of CRC, one byte at a time */
unsigned int crc32c_le(uint32_t crc, unsigned char const *buf, unsigned len){
	crc = htole32(crc);
	if(buf != 0)
		while (len--)
			crc = crc32ctable_le[0][((crc >> 24) ^ *buf++) & 0xFF] ^ (crc << 8);
	else
		while (len--)
			crc = crc32ctable_le[0][((crc >> 24)) & 0xFF] ^ (crc << 8);
	crc = le32toh(crc);
	return crc;
}

unsigned int crc32c_le_vx(uint32_t crc, unsigned char const *data, unsigned datalen)
{
	unsigned long prealign, aligned, remaining;

	if(data == 0)
		return crc32c_le(crc, data, datalen);

	if(datalen < VX_MIN_LEN + VX_ALIGN_MASK)
		return crc32c_le(crc, data, datalen);

	if ((unsigned long)data & VX_ALIGN_MASK) {
		prealign = VX_ALIGNMENT - ((unsigned long)data & VX_ALIGN_MASK);
		datalen -= prealign;
		crc = crc32c_le(crc, data, prealign);
		data = data + prealign;
	}

	if (datalen < VX_MIN_LEN)
		return crc32c_le(crc, data, datalen);

	aligned = datalen & ~VX_ALIGN_MASK;
	remaining = datalen & VX_ALIGN_MASK;

	crc = crc32c_le_vgfm_16(crc, data, aligned);
	data = data + aligned;

	if (remaining)
		crc = crc32c_le(crc, data, remaining);

	return crc;
}
