/*
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pciaccess.h>
#include <unistd.h>

#include "intel_io.h"
#include "intel_chipset.h"

#define KB(x) ((x) * 1024)
#define MB(x) ((x) * 1024 * 1024)
unsigned char *gtt;
uint32_t devid;

typedef uint32_t gen6_gtt_pte_t;
typedef uint64_t gen8_gtt_pte_t;

static gen6_gtt_pte_t gen6_gtt_pte(const unsigned i)
{
	return *((volatile gen6_gtt_pte_t *)(gtt) + i);
}

static gen8_gtt_pte_t gen8_gtt_pte(const unsigned i)
{
	return *((volatile gen8_gtt_pte_t *)(gtt) + i);
}

static uint64_t ingtt(const unsigned offset)
{
	if (intel_gen(devid) < 8)
		return gen6_gtt_pte(offset/KB(4));

	return gen8_gtt_pte(offset/KB(4));
}

static uint64_t get_phys(uint32_t pt_offset)
{
	uint64_t pae = 0;
	uint64_t phys = ingtt(pt_offset);

	if (intel_gen(devid) < 4 && !IS_G33(devid))
		return phys & ~0xfff;

	switch (intel_gen(devid)) {
		case 3:
		case 4:
		case 5:
			pae = (phys & 0xf0) << 28;
			break;
		case 6:
		case 7:
			if (IS_HASWELL(devid))
				pae = (phys & 0x7f0) << 28;
			else
				pae = (phys & 0xff0) << 28;
			break;
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 20:
			if (intel_graphics_ver(devid) >= IP_VER(12, 70))
				phys = phys & 0x3ffffffff000;
			else
				phys = phys & 0x7ffffff000;
			break;
		default:
			fprintf(stderr, "Unsupported platform\n");
			exit(-1);
	}

	return (phys | pae) & ~0xfff;
}

static int get_pte_size(void)
{
	return intel_gen(devid) < 8 ? 4 : 8;
}

static void pte_dump(int size, uint32_t offset) {
	int pte_size;
	int entries;
	unsigned int i;

	/* Want to print 4 ptes at a time (4b PTE assumed). */
	if (size % 16)
		size = (size + 16) & ~0xffff;

	pte_size = get_pte_size();
	entries = size / pte_size;

	printf("GTT offset   |                 %d PTEs (%d MB)\n", entries,
	       entries * 4096 / 1024 / 1024);
	printf("----------------------------------------------------------\n");

	for (i = 0; i < entries; i += 4) {
		if (intel_gen(devid) < 8) {
			printf("  0x%08x | 0x%08x 0x%08x 0x%08x 0x%08x\n",
			       KB(4 * i),
			       gen6_gtt_pte(i + 0),
			       gen6_gtt_pte(i + 1),
			       gen6_gtt_pte(i + 2),
			       gen6_gtt_pte(i + 3) );
		} else {
			printf("  0x%08x | 0x%016" PRIx64 " 0x%016" PRIx64
			       " 0x%016" PRIx64 " 0x%016" PRIx64 " \n",
			       KB(4 * i),
			       gen8_gtt_pte(i + 0),
			       gen8_gtt_pte(i + 1),
			       gen8_gtt_pte(i + 2),
			       gen8_gtt_pte(i + 3) );
		}
	}
}

int main(int argc, char **argv)
{
	struct pci_device *pci_dev;
	unsigned gtt_size;
	uint64_t start, gtt_max_addr;
	int flag[] = {
		PCI_DEV_MAP_FLAG_WRITE_COMBINE,
		PCI_DEV_MAP_FLAG_WRITABLE,
		0
	}, f;

	pci_dev = intel_get_pci_device();
	devid = pci_dev->device_id;

	if (IS_GEN2(devid)) {
		printf("Unsupported chipset for gtt dumper\n");
		exit(1);
	}

	for (f = 0; flag[f] != 0; f++) {
		if (IS_GEN3(devid)) {
			/* 915/945 chips has GTT range in bar 3 */
			if (pci_device_map_range(pci_dev,
						 pci_dev->regions[3].base_addr,
						 pci_dev->regions[3].size,
						 flag[f],
						 (void **)&gtt) == 0)
				break;
		} else {
			unsigned offset;

			offset = pci_dev->regions[0].size / 2;

			if (IS_GEN4(devid))
				offset = KB(512);

			if (pci_device_map_range(pci_dev,
						 pci_dev->regions[0].base_addr + offset,
						 offset,
						 flag[f],
						 (void **)&gtt) == 0)
				break;
		}
	}
	if (flag[f] == 0) {
		printf("Failed to map gtt\n");
		exit(1);
	}

	gtt_size = pci_dev->regions[0].size / 2;
	if (argc > 1 && !strncmp("-d", argv[1], 2)) {
		pte_dump(gtt_size, 0);
		return 0;
	}

	gtt_max_addr = (gtt_size / get_pte_size()) * KB(4ull);

	for (start = 0; start < gtt_max_addr; start += KB(4)) {
		uint64_t end, start_phys = get_phys(start);
		int constant_length = 0;
		int linear_length = 0;

		/* Check if it's a linear sequence */
		for (end = start + KB(4); end < gtt_max_addr; end += KB(4)) {
			uint64_t end_phys = get_phys(end);
			if (end_phys == start_phys + (end - start))
				linear_length++;
			else
				break;
		}
		if (linear_length > 0) {
			printf("0x%08" PRIx64 "- 0x%08" PRIx64 ": linear from "
			       "0x%" PRIx64 " to 0x%" PRIx64 "\n",
			       start, end - KB(4),
			       start_phys, start_phys + (end - start) - KB(4));
			start = end - KB(4);
			continue;
		}

		/* Check if it's a constant sequence */
		for (end = start + KB(4); end < gtt_max_addr; end += KB(4)) {
			uint64_t end_phys = get_phys(end);
			if (end_phys == start_phys)
				constant_length++;
			else
				break;
		}
		if (constant_length > 0) {
			printf("0x%08" PRIx64 " - 0x%08" PRIx64
				": constant 0x%" PRIx64 "\n",
			       start, end - KB(4), start_phys);
			start = end - KB(4);
			continue;
		}

		printf("0x%08" PRIx64 ": 0x%" PRIx64 "\n", start, start_phys);
	}

	return 0;
}
