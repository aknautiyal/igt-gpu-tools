// SPDX-License-Identifier: MIT
/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2022 Advanced Micro Devices, Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include <amdgpu.h>
#include "lib/amdgpu/amd_memory.h"
#include "lib/amdgpu/amd_sdma.h"
#include "lib/amdgpu/amd_PM4.h"
#include "lib/amdgpu/amd_command_submission.h"
#include "ioctl_wrappers.h"


/*
 *
 * Caller need create/release:
 * pm4_src, resources, ib_info, and ibs_request
 * submit command stream described in ibs_request and wait for this IB accomplished
 */

int amdgpu_test_exec_cs_helper(amdgpu_device_handle device, unsigned int ip_type,
				struct amdgpu_ring_context *ring_context, int expect_failure)
{
	int r;
	uint32_t expired;
	uint32_t *ring_ptr;
	amdgpu_bo_handle ib_result_handle;
	void *ib_result_cpu;
	uint64_t ib_result_mc_address;
	struct amdgpu_cs_fence fence_status = {0};
	amdgpu_va_handle va_handle;
	bool user_queue = ring_context->user_queue;
	const struct amdgpu_ip_block_version *ip_block = NULL;
	amdgpu_bo_handle *all_res;

	ip_block = get_ip_block(device, ip_type);
	all_res = alloca(sizeof(ring_context->resources[0]) * (ring_context->res_cnt + 1));

	if (expect_failure) {
		/* allocate IB */
		r = amdgpu_bo_alloc_and_map_sync(device, ring_context->write_length, 4096,
						 AMDGPU_GEM_DOMAIN_GTT, 0, AMDGPU_VM_MTYPE_UC,
						 &ib_result_handle, &ib_result_cpu,
						 &ib_result_mc_address, &va_handle,
						 ring_context->timeline_syncobj_handle,
						 ++ring_context->point, user_queue);
	} else {
		/* prepare CS */
		igt_assert(ring_context->pm4_dw <= 1024);
		/* allocate IB */
		r = amdgpu_bo_alloc_and_map_sync(device, 4096, 4096,
						 AMDGPU_GEM_DOMAIN_GTT, 0, AMDGPU_VM_MTYPE_UC,
						 &ib_result_handle, &ib_result_cpu,
						 &ib_result_mc_address, &va_handle,
						 ring_context->timeline_syncobj_handle,
						 ++ring_context->point, user_queue);
	}
	igt_assert_eq(r, 0);

	if (user_queue) {
		r = amdgpu_timeline_syncobj_wait(device, ring_context->timeline_syncobj_handle,
						 ring_context->point);
		igt_assert_eq(r, 0);
	}

	/* copy PM4 packet to ring from caller */
	ring_ptr = ib_result_cpu;
	memcpy(ring_ptr, ring_context->pm4, ring_context->pm4_dw * sizeof(*ring_context->pm4));

	if (user_queue)
		ip_block->funcs->userq_submit(device, ring_context, ip_type, ib_result_mc_address);
	else {
		ring_context->ib_info.ib_mc_address = ib_result_mc_address;
		ring_context->ib_info.size = ring_context->pm4_dw;
		if (ring_context->secure)
			ring_context->ib_info.flags |= AMDGPU_IB_FLAGS_SECURE;

		ring_context->ibs_request.ip_type = ip_type;
		ring_context->ibs_request.ring = ring_context->ring_id;
		ring_context->ibs_request.number_of_ibs = 1;
		ring_context->ibs_request.ibs = &ring_context->ib_info;
		ring_context->ibs_request.fence_info.handle = NULL;

		memcpy(all_res, ring_context->resources,
		       sizeof(ring_context->resources[0]) * ring_context->res_cnt);

		all_res[ring_context->res_cnt] = ib_result_handle;

		r = amdgpu_bo_list_create(device, ring_context->res_cnt + 1, all_res,
					  NULL, &ring_context->ibs_request.resources);
		igt_assert_eq(r, 0);

		/* submit CS */
		r = amdgpu_cs_submit(ring_context->context_handle, 0,
				     &ring_context->ibs_request, 1);

		ring_context->err_codes.err_code_cs_submit = r;
		if (expect_failure)
			igt_info("amdgpu_cs_submit %d PID %d\n", r, getpid());
		else {
			/* we allow ECANCELED, ENODATA or -EHWPOISON for good jobs temporally */
			if (r != -ECANCELED && r != -ENODATA && r != -EHWPOISON)
				igt_assert_eq(r, 0);
		}

		r = amdgpu_bo_list_destroy(ring_context->ibs_request.resources);
		igt_assert_eq(r, 0);

		fence_status.ip_type = ip_type;
		fence_status.ip_instance = 0;
		fence_status.ring = ring_context->ibs_request.ring;
		fence_status.context = ring_context->context_handle;
		fence_status.fence = ring_context->ibs_request.seq_no;

		/* wait for IB accomplished */
		r = amdgpu_cs_query_fence_status(&fence_status,
						 AMDGPU_TIMEOUT_INFINITE,
						 0, &expired);
		ring_context->err_codes.err_code_wait_for_fence = r;
		if (expect_failure) {
			igt_info("EXPECT FAILURE amdgpu_cs_query_fence_status%d"
				 "expired %d PID %d\n", r, expired, getpid());
		} else {
			/* we allow ECANCELED or ENODATA for good jobs temporally */
			if (r != -ECANCELED && r != -ENODATA)
				igt_assert_eq(r, 0);
		}
	}
	amdgpu_bo_unmap_and_free(ib_result_handle, va_handle,
				 ib_result_mc_address, 4096);
	return r;
}

void amdgpu_command_submission_write_linear_helper(amdgpu_device_handle device,
						   const struct amdgpu_ip_block_version *ip_block,
						   bool secure, bool user_queue)

{
	const int sdma_write_length = 128;
	const int pm4_dw = 256;

	struct amdgpu_ring_context *ring_context;
	int i, r, loop, ring_id;

	uint64_t gtt_flags[2] = {0, AMDGPU_GEM_CREATE_CPU_GTT_USWC};

	ring_context = calloc(1, sizeof(*ring_context));
	igt_assert(ring_context);
	/* setup parameters */
	ring_context->write_length =  sdma_write_length;
	ring_context->pm4 = calloc(pm4_dw, sizeof(*ring_context->pm4));
	ring_context->secure = secure;
	ring_context->pm4_size = pm4_dw;
	ring_context->res_cnt = 1;
	ring_context->user_queue = user_queue;
	ring_context->time_out = 0;
	igt_assert(ring_context->pm4);

	r = amdgpu_query_hw_ip_info(device, ip_block->type, 0, &ring_context->hw_ip_info);
	igt_assert_eq(r, 0);

	for (i = 0; secure && (i < 2); i++)
		gtt_flags[i] |= AMDGPU_GEM_CREATE_ENCRYPTED;

	if (user_queue) {
		ip_block->funcs->userq_create(device, ring_context, ip_block->type);
	} else {
		r = amdgpu_cs_ctx_create(device, &ring_context->context_handle);
		igt_assert_eq(r, 0);
	}



/* Dont need but check with vitaly if for KMS also we need ring id or not */
	for (ring_id = 0; (1 << ring_id) & ring_context->hw_ip_info.available_rings; ring_id++) {
		loop = 0;
		ring_context->ring_id = ring_id;
		while (loop < 2) {
			/* allocate UC bo for sDMA use */
			r = amdgpu_bo_alloc_and_map_sync(device,
							 ring_context->write_length *
							 sizeof(uint32_t),
							 4096, AMDGPU_GEM_DOMAIN_GTT,
							 gtt_flags[loop],
							 AMDGPU_VM_MTYPE_UC,
							 &ring_context->bo,
							 (void **)&ring_context->bo_cpu,
							 &ring_context->bo_mc,
							 &ring_context->va_handle,
							 ring_context->timeline_syncobj_handle,
							 ++ring_context->point, user_queue);

			igt_assert_eq(r, 0);

			if (user_queue) {
				r = amdgpu_timeline_syncobj_wait(device,
					ring_context->timeline_syncobj_handle,
					ring_context->point);
				igt_assert_eq(r, 0);
			}

			/* clear bo */
			memset((void *)ring_context->bo_cpu, 0,
			       ring_context->write_length * sizeof(uint32_t));

			ring_context->resources[0] = ring_context->bo;

			ip_block->funcs->write_linear(ip_block->funcs, ring_context,
						      &ring_context->pm4_dw);

			ring_context->ring_id = ring_id;

			 amdgpu_test_exec_cs_helper(device, ip_block->type, ring_context, 0);

			/* verify if SDMA test result meets with expected */
			i = 0;
			if (!secure) {
				r = ip_block->funcs->compare(ip_block->funcs, ring_context, 1);
				igt_assert_eq(r, 0);
			} else if (ip_block->type == AMDGPU_HW_IP_GFX) {
				ip_block->funcs->write_linear_atomic(ip_block->funcs, ring_context, &ring_context->pm4_dw);
				amdgpu_test_exec_cs_helper(device, ip_block->type, ring_context, 0);
			} else if (ip_block->type == AMDGPU_HW_IP_DMA) {
				/* restore the bo_cpu to compare */
				ring_context->bo_cpu_origin = ring_context->bo_cpu[0];
				ip_block->funcs->write_linear_atomic(ip_block->funcs, ring_context, &ring_context->pm4_dw);

				amdgpu_test_exec_cs_helper(device, ip_block->type, ring_context, 0);

				igt_assert_neq(ring_context->bo_cpu[0], ring_context->bo_cpu_origin);
				/* restore again, here dest_data should be */
				ring_context->bo_cpu_origin = ring_context->bo_cpu[0];
				ip_block->funcs->write_linear_atomic(ip_block->funcs, ring_context, &ring_context->pm4_dw);

				amdgpu_test_exec_cs_helper(device, ip_block->type, ring_context, 0);
				/* here bo_cpu[0] should be unchanged, still is 0x12345678, otherwise failed*/
				igt_assert_eq(ring_context->bo_cpu[0], ring_context->bo_cpu_origin);
			}

			amdgpu_bo_unmap_and_free(ring_context->bo, ring_context->va_handle, ring_context->bo_mc,
						 ring_context->write_length * sizeof(uint32_t));
			loop++;
		}
	}
	/* clean resources */
	free(ring_context->pm4);

	if (user_queue) {
		ip_block->funcs->userq_destroy(device, ring_context, ip_block->type);
	} else {
		r = amdgpu_cs_ctx_free(ring_context->context_handle);
		igt_assert_eq(r, 0);
	}

	free(ring_context);
}


/**
 *
 * @param device
 * @param ip_type
 * @param user_queue
 */
void amdgpu_command_submission_const_fill_helper(amdgpu_device_handle device,
						 const struct amdgpu_ip_block_version *ip_block,
						 bool user_queue)
{
	const int sdma_write_length = 1024 * 1024;
	const int pm4_dw = 256;

	struct amdgpu_ring_context *ring_context;
	int r, loop, ring_id;

	uint64_t gtt_flags[2] = {0, AMDGPU_GEM_CREATE_CPU_GTT_USWC};

	ring_context = calloc(1, sizeof(*ring_context));
	ring_context->write_length =  sdma_write_length;
	ring_context->pm4 = calloc(pm4_dw, sizeof(*ring_context->pm4));
	ring_context->secure = false;
	ring_context->pm4_size = pm4_dw;
	ring_context->res_cnt = 1;
	ring_context->user_queue = user_queue;
	ring_context->time_out = 0;
	igt_assert(ring_context->pm4);
	r = amdgpu_query_hw_ip_info(device, ip_block->type, 0, &ring_context->hw_ip_info);
	igt_assert_eq(r, 0);

	if (user_queue) {
		ip_block->funcs->userq_create(device, ring_context, ip_block->type);
	} else {
		r = amdgpu_cs_ctx_create(device, &ring_context->context_handle);
		igt_assert_eq(r, 0);
	}

	for (ring_id = 0; (1 << ring_id) & ring_context->hw_ip_info.available_rings; ring_id++) {
		/* prepare resource */
		loop = 0;
		ring_context->ring_id = ring_id;
		while (loop < 2) {
			/* allocate UC bo for sDMA use */
			r = amdgpu_bo_alloc_and_map_sync(device, ring_context->write_length,
							 4096, AMDGPU_GEM_DOMAIN_GTT,
							 gtt_flags[loop],
							 AMDGPU_VM_MTYPE_UC,
							 &ring_context->bo,
							 (void **)&ring_context->bo_cpu,
							 &ring_context->bo_mc,
							 &ring_context->va_handle,
							 ring_context->timeline_syncobj_handle,
							 ++ring_context->point, user_queue);
			igt_assert_eq(r, 0);

			if (user_queue) {
				r = amdgpu_timeline_syncobj_wait(device,
					ring_context->timeline_syncobj_handle,
					ring_context->point);
				igt_assert_eq(r, 0);
			}

			/* clear bo */
			memset((void *)ring_context->bo_cpu, 0, ring_context->write_length);

			ring_context->resources[0] = ring_context->bo;

			/* fulfill PM4: test DMA const fill */
			ip_block->funcs->const_fill(ip_block->funcs, ring_context, &ring_context->pm4_dw);

			amdgpu_test_exec_cs_helper(device, ip_block->type, ring_context, 0);

			/* verify if SDMA test result meets with expected */
			r = ip_block->funcs->compare(ip_block->funcs, ring_context, 4);
			igt_assert_eq(r, 0);

			amdgpu_bo_unmap_and_free(ring_context->bo, ring_context->va_handle, ring_context->bo_mc,
					 ring_context->write_length);
			loop++;
		}
	}
	/* clean resources */
	free(ring_context->pm4);

	if (user_queue) {
		ip_block->funcs->userq_destroy(device, ring_context, ip_block->type);
	} else {
		r = amdgpu_cs_ctx_free(ring_context->context_handle);
		igt_assert_eq(r, 0);
	}

	free(ring_context);
}

/**
 *
 * @param device
 * @param ip_type
 * @param user_queue
 */
void amdgpu_command_submission_copy_linear_helper(amdgpu_device_handle device,
						  const struct amdgpu_ip_block_version *ip_block,
						  bool user_queue)
{
	const int sdma_write_length = 1024;
	const int pm4_dw = 256;

	struct amdgpu_ring_context *ring_context;
	int r, loop1, loop2, ring_id;

	uint64_t gtt_flags[2] = {0, AMDGPU_GEM_CREATE_CPU_GTT_USWC};


	ring_context = calloc(1, sizeof(*ring_context));
	ring_context->write_length =  sdma_write_length;
	ring_context->pm4 = calloc(pm4_dw, sizeof(*ring_context->pm4));
	ring_context->secure = false;
	ring_context->pm4_size = pm4_dw;
	ring_context->res_cnt = 2;
	ring_context->user_queue = user_queue;
	ring_context->time_out = 0;
	igt_assert(ring_context->pm4);
	r = amdgpu_query_hw_ip_info(device, ip_block->type, 0, &ring_context->hw_ip_info);
	igt_assert_eq(r, 0);


	if (user_queue) {
		ip_block->funcs->userq_create(device, ring_context, ip_block->type);
	} else {
		r = amdgpu_cs_ctx_create(device, &ring_context->context_handle);
		igt_assert_eq(r, 0);
	}

	for (ring_id = 0; (1 << ring_id) & ring_context->hw_ip_info.available_rings; ring_id++) {
		loop1 = loop2 = 0;
		ring_context->ring_id = ring_id;
	/* run 9 circle to test all mapping combination */
		while (loop1 < 2) {
			while (loop2 < 2) {
				/* allocate UC bo1for sDMA use */
				r = amdgpu_bo_alloc_and_map_sync(device, ring_context->write_length,
							4096, AMDGPU_GEM_DOMAIN_GTT,
							gtt_flags[loop1],
							AMDGPU_VM_MTYPE_UC,
							&ring_context->bo,
							(void **)&ring_context->bo_cpu,
							&ring_context->bo_mc,
							&ring_context->va_handle,
							ring_context->timeline_syncobj_handle,
							++ring_context->point, user_queue);
				igt_assert_eq(r, 0);

				if (user_queue) {
					r = amdgpu_timeline_syncobj_wait(device,
						ring_context->timeline_syncobj_handle,
						ring_context->point);
					igt_assert_eq(r, 0);
				}

				/* set bo_cpu */
				memset((void *)ring_context->bo_cpu, ip_block->funcs->pattern, ring_context->write_length);

				/* allocate UC bo2 for sDMA use */
				r = amdgpu_bo_alloc_and_map_sync(device,
							ring_context->write_length,
							4096, AMDGPU_GEM_DOMAIN_GTT,
							gtt_flags[loop2],
							AMDGPU_VM_MTYPE_UC,
							&ring_context->bo2,
							(void **)&ring_context->bo2_cpu,
							&ring_context->bo_mc2,
							&ring_context->va_handle2,
							ring_context->timeline_syncobj_handle,
							++ring_context->point, user_queue);
				igt_assert_eq(r, 0);

				if (user_queue) {
					r = amdgpu_timeline_syncobj_wait(device,
						ring_context->timeline_syncobj_handle,
						ring_context->point);
					igt_assert_eq(r, 0);
				}

				/* clear bo2_cpu */
				memset((void *)ring_context->bo2_cpu, 0, ring_context->write_length);

				ring_context->resources[0] = ring_context->bo;
				ring_context->resources[1] = ring_context->bo2;

				ip_block->funcs->copy_linear(ip_block->funcs, ring_context, &ring_context->pm4_dw);

				amdgpu_test_exec_cs_helper(device, ip_block->type, ring_context, 0);

				/* verify if SDMA test result meets with expected */
				r = ip_block->funcs->compare_pattern(ip_block->funcs, ring_context, 4);
				igt_assert_eq(r, 0);

				amdgpu_bo_unmap_and_free(ring_context->bo, ring_context->va_handle, ring_context->bo_mc,
						 ring_context->write_length);
				amdgpu_bo_unmap_and_free(ring_context->bo2, ring_context->va_handle2, ring_context->bo_mc2,
						 ring_context->write_length);
				loop2++;
			}
			loop1++;
		}
	}

	/* clean resources */
	free(ring_context->pm4);

	if (user_queue) {
		ip_block->funcs->userq_destroy(device, ring_context, ip_block->type);
	} else {
		r = amdgpu_cs_ctx_free(ring_context->context_handle);
		igt_assert_eq(r, 0);
	}

	free(ring_context);
}
