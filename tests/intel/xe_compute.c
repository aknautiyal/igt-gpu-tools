// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/**
 * TEST: Check compute-related functionality
 * Category: Core
 * Mega feature: Compute
 * Sub-category: Compute tests
 * Test category: functionality test
 */

#include <string.h>

#include "igt.h"
#include "igt_sysfs.h"
#include "intel_compute.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

static int gt_sysfs_open(int gt)
{
	int fd, gt_fd;

	fd = drm_open_driver(DRIVER_XE);
	gt_fd = xe_sysfs_gt_open(fd, gt);
	drm_close_driver(fd);

	return gt_fd;
}

static bool get_num_cslices(u32 gt, u32 *num_slices)
{
	int gt_fd, ret;

	gt_fd = gt_sysfs_open(gt);
	ret = igt_sysfs_scanf(gt_fd, "num_cslices", "%u", num_slices);
	close(gt_fd);

	return ret > 0;
}

/* Grab GT mask in places where we don't have or want to maintain an open fd */
static uint64_t get_gt_mask(void)
{
	int fd = drm_open_driver(DRIVER_XE);
	uint64_t mask;

	mask = xe_device_get(fd)->gt_mask;
	drm_close_driver(fd);

	return mask;
}

#define for_each_bit(__mask, __bit) \
	for ( ; __bit = ffsll(__mask) - 1, __mask != 0; __mask &= ~(1ull << __bit))

/**
 * SUBTEST: ccs-mode-basic
 * GPU requirement: PVC
 * Description: Validate 'ccs_mode' sysfs uapi
 * Functionality: ccs mode
 */
static void
test_ccs_mode(void)
{
	struct drm_xe_engine_class_instance *hwe;
	u32 gt, m, ccs_mode, vm, q, num_slices;
	int fd, gt_fd, num_gt_with_ccs_mode = 0;
	uint64_t gt_mask = get_gt_mask();

	/*
	 * The loop body needs to run without any open file descriptors so we
	 * can't use xe_for_each_gt() which uses an open fd.
	 */
	for_each_bit(gt_mask, gt) {
		if (!get_num_cslices(gt, &num_slices))
			continue;

		num_gt_with_ccs_mode++;
		gt_fd = gt_sysfs_open(gt);
		igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", 0) < 0);
		for (m = 1; m <= num_slices; m++) {
			/* compute slices are to be equally distributed among enabled engines */
			if (num_slices % m) {
				igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", m) < 0);
				continue;
			}

			/* Validate allowed ccs modes by setting them and reading back */
			igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", m) > 0);
			igt_assert(igt_sysfs_scanf(gt_fd, "ccs_mode", "%u", &ccs_mode) > 0);
			igt_assert(m == ccs_mode);

			/* Validate exec queues creation with enabled ccs engines */
			fd = drm_open_driver(DRIVER_XE);
			vm = xe_vm_create(fd, 0, 0);
			xe_for_each_engine(fd, hwe) {
				if (hwe->gt_id != gt ||
				    hwe->engine_class != DRM_XE_ENGINE_CLASS_COMPUTE)
					continue;

				q = xe_exec_queue_create(fd, vm, hwe, 0);
				xe_exec_queue_destroy(fd, q);
			}

			/* Ensure exec queue creation fails for disabled ccs engines */
			hwe->gt_id = gt;
			hwe->engine_class = DRM_XE_ENGINE_CLASS_COMPUTE;
			hwe->engine_instance = m;
			igt_assert_neq(__xe_exec_queue_create(fd, vm, 1, 1, hwe, 0, &q), 0);

			xe_vm_destroy(fd, vm);
			drm_close_driver(fd);
		}

		/* Ensure invalid ccs mode setting is rejected */
		igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", m) < 0);

		/* Can't change ccs mode with an open drm clients */
		fd = drm_open_driver(DRIVER_XE);
		igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", 1) < 0);
		drm_close_driver(fd);

		/* Set ccs mode back to default value */
		igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", 1) > 0);

		close(gt_fd);
	}

	igt_require(num_gt_with_ccs_mode > 0);
}

/**
 * SUBTEST: ccs-mode-compute-kernel
 * GPU requirement: PVC
 * Description: Validate 'ccs_mode' by running compute kernel
 * Functionality: ccs mode
 */
static void
test_compute_kernel_with_ccs_mode(void)
{
	struct drm_xe_engine_class_instance *hwe;
	u32 gt, m, num_slices;
	int fd, gt_fd, num_gt_with_ccs_mode = 0;
	uint64_t gt_mask = get_gt_mask();

	/*
	 * The loop body needs to run without any open file descriptors so we
	 * can't use xe_for_each_gt() which uses an open fd.
	 */
	for_each_bit(gt_mask, gt) {
		if (!get_num_cslices(gt, &num_slices))
			continue;

		num_gt_with_ccs_mode++;
		gt_fd = gt_sysfs_open(gt);
		for (m = 1; m <= num_slices; m++) {
			if (num_slices % m)
				continue;

			igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", m) > 0);

			/* Run compute kernel on enabled ccs engines */
			fd = drm_open_driver(DRIVER_XE);
			xe_for_each_engine(fd, hwe) {
				if (hwe->gt_id != gt ||
				    hwe->engine_class != DRM_XE_ENGINE_CLASS_COMPUTE)
					continue;

				igt_info("GT-%d: Running compute kernel with ccs_mode %d on ccs engine %d\n",
					 gt, m, hwe->engine_instance);
				igt_assert_f(xe_run_intel_compute_kernel_on_engine(fd, hwe, NULL, EXECENV_PREF_SYSTEM),
					     "Unable to run compute kernel successfully\n");
			}
			drm_close_driver(fd);
		}

		/* Set ccs mode back to default value */
		igt_assert(igt_sysfs_printf(gt_fd, "ccs_mode", "%u", 1) > 0);

		close(gt_fd);
	}

	igt_require(num_gt_with_ccs_mode > 0);
}

/**
 * SUBTEST: compute-square
 * Mega feature: WMTP
 * Sub-category: wmtp tests
 * Functionality: OpenCL kernel
 * GPU requirement: TGL, PVC, LNL, PTL
 * Description:
 *	Run an openCL Kernel that returns output[i] = input[i] * input[i],
 *	for an input dataset..
 */
static void
test_compute_square(int fd)
{
	igt_require_f(run_intel_compute_kernel(fd, NULL, EXECENV_PREF_SYSTEM),
		      "GPU not supported\n");
}

igt_main
{
	int xe;

	igt_fixture {
		xe = drm_open_driver(DRIVER_XE);
	}

	igt_subtest("compute-square")
		test_compute_square(xe);

	igt_fixture
		drm_close_driver(xe);

	/* ccs mode tests should be run without open gpu file handles */
	igt_subtest("ccs-mode-basic")
		test_ccs_mode();

	igt_subtest("ccs-mode-compute-kernel")
		test_compute_kernel_with_ccs_mode();
}
