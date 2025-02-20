/* SPDX-License-Identifier: MIT
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#ifndef AMD_IP_BLOCKS_H
#define AMD_IP_BLOCKS_H

#include "amd_registers.h"

#define MAX_CARDS_SUPPORTED 4

enum amd_ip_block_type {
	AMD_IP_GFX = 0,
	AMD_IP_COMPUTE,
	AMD_IP_DMA,
	AMD_IP_UVD,
	AMD_IP_VCE,
	AMD_IP_UVD_ENC,
	AMD_IP_VCN_DEC,
	AMD_IP_VCN_ENC,
	AMD_IP_VCN_UNIFIED = AMD_IP_VCN_ENC,
	AMD_IP_VCN_JPEG,
	AMD_IP_VPE,
	AMD_IP_MAX,
};

/* aux struct to hold misc parameters for convenience to maintain */
struct amdgpu_ring_context {

	int ring_id; /* ring_id from amdgpu_query_hw_ip_info */
	int res_cnt; /* num of bo in amdgpu_bo_handle resources[2] */

	uint32_t write_length;  /* length of data */
	uint32_t write_length2; /* length of data for second packet */
	uint32_t *pm4;		/* data of the packet */
	uint32_t pm4_size;	/* max allocated packet size */
	bool secure;		/* secure or not */

	uint64_t bo_mc;		/* GPU address of first buffer */
	uint64_t bo_mc2;	/* GPU address for p4 packet */
	uint64_t bo_mc3;	/* GPU address of second buffer */
	uint64_t bo_mc4;	/* GPU address of second p4 packet */

	uint32_t pm4_dw;	/* actual size of pm4 */
	uint32_t pm4_dw2;	/* actual size of second pm4 */

	volatile uint32_t *bo_cpu;	/* cpu adddress of mapped GPU buf */
	volatile uint32_t *bo2_cpu;	/* cpu adddress of mapped pm4 */
	volatile uint32_t *bo3_cpu;	/* cpu adddress of mapped GPU second buf */
	volatile uint32_t *bo4_cpu;	/* cpu adddress of mapped second pm4 */

	uint32_t bo_cpu_origin;

	amdgpu_bo_handle bo;
	amdgpu_bo_handle bo2;
	amdgpu_bo_handle bo3;
	amdgpu_bo_handle bo4;

	amdgpu_bo_handle boa_vram[2];
	amdgpu_bo_handle boa_gtt[2];

	amdgpu_context_handle context_handle;
	struct drm_amdgpu_info_hw_ip hw_ip_info;  /* result of amdgpu_query_hw_ip_info */

	amdgpu_bo_handle resources[4]; /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle;    /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle2;   /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle3;   /* amdgpu_bo_alloc_and_map */
	amdgpu_va_handle va_handle4;   /* amdgpu_bo_alloc_and_map */

	struct amdgpu_cs_ib_info ib_info;     /* amdgpu_bo_list_create */
	struct amdgpu_cs_request ibs_request; /* amdgpu_cs_query_fence_status */
};


struct amdgpu_ip_funcs {
	uint32_t	family_id;
	uint32_t	align_mask;
	uint32_t	nop;
	uint32_t	deadbeaf;
	uint32_t	pattern;
	/* functions */
	int (*write_linear)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*write_linear_atomic)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*const_fill)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*copy_linear)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);
	int (*compare)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, int div);
	int (*compare_pattern)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, int div);
	int (*get_reg_offset)(enum general_reg reg);
	int (*wait_reg_mem)(const struct amdgpu_ip_funcs *func, const struct amdgpu_ring_context *context, uint32_t *pm4_dw);

};

extern const struct amdgpu_ip_block_version gfx_v6_0_ip_block;

struct amdgpu_ip_block_version {
	const enum amd_ip_block_type type;
	const int major;
	const int minor;
	const int rev;
	struct amdgpu_ip_funcs *funcs;
};

/* global holder for the array of in use ip blocks */

struct amdgpu_ip_blocks_device {
	struct amdgpu_ip_block_version *ip_blocks[AMD_IP_MAX];
	int			num_ip_blocks;
};

extern  struct amdgpu_ip_blocks_device amdgpu_ips;

int
setup_amdgpu_ip_blocks(uint32_t major, uint32_t minor, struct amdgpu_gpu_info *amdinfo,
		       amdgpu_device_handle device);

const struct amdgpu_ip_block_version *
get_ip_block(amdgpu_device_handle device, enum amd_ip_block_type type);

struct amdgpu_cmd_base {
	uint32_t cdw;  /* Number of used dwords. */
	uint32_t max_dw; /* Maximum number of dwords. */
	uint32_t *buf; /* The base pointer of the chunk. */
	bool is_assigned_buf;

	/* functions */
	int (*allocate_buf)(struct amdgpu_cmd_base  *base, uint32_t size);
	int (*attach_buf)(struct amdgpu_cmd_base  *base, void *ptr, uint32_t size_bytes);
	void (*emit)(struct amdgpu_cmd_base  *base, uint32_t value);
	void (*emit_aligned)(struct amdgpu_cmd_base  *base, uint32_t mask, uint32_t value);
	void (*emit_repeat)(struct amdgpu_cmd_base  *base, uint32_t value, uint32_t number_of_times);
	void (*emit_at_offset)(struct amdgpu_cmd_base  *base, uint32_t value, uint32_t offset_dwords);
	void (*emit_buf)(struct amdgpu_cmd_base  *base, const void *ptr, uint32_t offset_bytes, uint32_t size_bytes);
};

struct amdgpu_cmd_base *get_cmd_base(void);

void free_cmd_base(struct amdgpu_cmd_base *base);

int
amdgpu_open_devices(bool open_render_node, int max_cards_supported, int drm_amdgpu_fds[]);

void
asic_rings_readness(amdgpu_device_handle device_handle, uint32_t mask, bool arr[AMD_IP_MAX]);

#endif
