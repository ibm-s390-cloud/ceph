/* Copyright (C) 2017 International Business Machines Corp.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "arch/s390x.h"
#include "arch/probe.h"

/* flags we export */
int ceph_arch_s390x_crc32 = 0;

#include <stdio.h>

int ceph_arch_s390x_probe(void)
{
  ceph_arch_s390x_crc32 = 1;

  return 0;
}
