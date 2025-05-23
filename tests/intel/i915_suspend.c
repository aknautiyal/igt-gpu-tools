/*
 * Copyright © 2013, 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *    David Weinehall <david.weinehall@intel.com>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <drm.h>

#include "i915/gem.h"
#include "i915/gem_create.h"
#include "igt.h"
#include "igt_kmod.h"
#include "igt_device.h"
#include "igt_device_scan.h"
/**
 * TEST: i915 suspend
 * Category: Core
 * Mega feature: Power management
 * Sub-category: Power management tests
 * Functionality: s2idle w/o i915
 * Test category: suspend
 * Feature: suspend
 *
 * SUBTEST: basic-s2idle-without-i915
 * Description: Validate suspend-to-idle without i915 module
 *
 * SUBTEST: basic-s3-without-i915
 * Description:
 *   Validate S3 without i915 module.
 *   Validate S3 state without i915 module
 *
 * SUBTEST: debugfs-reader
 * Description: Test debugfs behavior during suspend to idle
 *
 * SUBTEST: debugfs-reader-hibernate
 *
 * SUBTEST: fence-restore-tiled2untiled
 * Feature: gtt, suspend feature, synchronization
 *
 * SUBTEST: fence-restore-tiled2untiled-hibernate
 * Feature: gtt, suspend feature, synchronization
 *
 * SUBTEST: fence-restore-untiled
 * Feature: gtt, suspend feature, synchronization
 *
 * SUBTEST: fence-restore-untiled-hibernate
 * Feature: gtt, suspend feature, synchronization
 *
 * SUBTEST: forcewake
 * Description: Test to prevent GT from suspend by opening forcewake handle
 *
 * SUBTEST: forcewake-hibernate
 *
 * SUBTEST: shrink
 *
 * SUBTEST: sysfs-reader
 * Description: Test sysfs behavior during suspend to idle
 *
 * SUBTEST: sysfs-reader-hibernate
 */

#define OBJECT_SIZE (16*1024*1024)

static void
test_fence_restore(int fd, bool tiled2untiled, bool hibernate)
{
	uint32_t handle1, handle2, handle_tiled;
	uint32_t *ptr1, *ptr2, *ptr_tiled;
	int i;

	/* We wall the tiled object with untiled canary objects to make sure
	 * that we detect tile leaking in both directions. */
	handle1 = gem_create(fd, OBJECT_SIZE);
	handle2 = gem_create(fd, OBJECT_SIZE);
	handle_tiled = gem_create(fd, OBJECT_SIZE);

	/* Access the buffer objects in the order we want to have the laid out. */
	ptr1 = gem_mmap__gtt(fd, handle1, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	gem_set_domain(fd, handle1, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		ptr1[i] = i;

	ptr_tiled = gem_mmap__gtt(fd, handle_tiled, OBJECT_SIZE,
				  PROT_READ | PROT_WRITE);
	gem_set_domain(fd, handle_tiled,
		       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	if (tiled2untiled)
		gem_set_tiling(fd, handle_tiled, I915_TILING_X, 2048);
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		ptr_tiled[i] = i;

	ptr2 = gem_mmap__gtt(fd, handle2, OBJECT_SIZE, PROT_READ | PROT_WRITE);
	gem_set_domain(fd, handle2, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		ptr2[i] = i;

	if (tiled2untiled)
		gem_set_tiling(fd, handle_tiled, I915_TILING_NONE, 2048);
	else
		gem_set_tiling(fd, handle_tiled, I915_TILING_X, 2048);

	if (hibernate)
		igt_system_suspend_autoresume(SUSPEND_STATE_DISK,
					      SUSPEND_TEST_NONE);
	else
		igt_system_suspend_autoresume(SUSPEND_STATE_MEM,
					      SUSPEND_TEST_NONE);

	igt_info("checking the first canary object\n");
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		igt_assert(ptr1[i] == i);

	igt_info("checking the second canary object\n");
	for (i = 0; i < OBJECT_SIZE/sizeof(uint32_t); i++)
		igt_assert(ptr2[i] == i);

	gem_close(fd, handle1);
	gem_close(fd, handle2);
	gem_close(fd, handle_tiled);

	munmap(ptr1, OBJECT_SIZE);
	munmap(ptr2, OBJECT_SIZE);
	munmap(ptr_tiled, OBJECT_SIZE);
}

static void
test_debugfs_reader(int fd, bool hibernate)
{
	struct igt_helper_process reader = {};
	reader.use_SIGKILL = true;

	igt_fork_helper(&reader) {
		static const char dfs_base[] = "/sys/kernel/debug/dri";
		static char tmp[1024];

		snprintf(tmp, sizeof(tmp) - 1,
			 "while true; do find %s/%i/ -type f ! -path \"*/crc/*\" | xargs cat > /dev/null 2>&1; done",
			 dfs_base, igt_device_get_card_index(fd));
		igt_assert(execl("/bin/sh", "sh", "-c", tmp, (char *) NULL) != -1);
	}

	sleep(1);

	if (hibernate)
		igt_system_suspend_autoresume(SUSPEND_STATE_DISK,
					      SUSPEND_TEST_NONE);
	else
		igt_system_suspend_autoresume(SUSPEND_STATE_MEM,
					      SUSPEND_TEST_NONE);

	sleep(1);

	igt_stop_helper(&reader);
}

static void
test_sysfs_reader(int fd, bool hibernate)
{
	struct igt_helper_process reader = {};
	reader.use_SIGKILL = true;

	igt_fork_helper(&reader) {
		static const char dfs_base[] = "/sys/class/drm/card";
		static char tmp[1024];

		snprintf(tmp, sizeof(tmp) - 1,
			 "while true; do find %s%i*/ -type f | xargs cat > /dev/null 2>&1; done",
			 dfs_base, igt_device_get_card_index(fd));
		igt_assert(execl("/bin/sh", "sh", "-c", tmp, (char *) NULL) != -1);
	}

	sleep(1);

	if (hibernate)
		igt_system_suspend_autoresume(SUSPEND_STATE_DISK,
					      SUSPEND_TEST_NONE);
	else
		igt_system_suspend_autoresume(SUSPEND_STATE_MEM,
					      SUSPEND_TEST_NONE);

	sleep(1);

	igt_stop_helper(&reader);
}

static void
test_shrink(int fd, unsigned int mode)
{
	size_t size;
	void *mem;

	gem_quiescent_gpu(fd);

	igt_multi_fork(child, 1) {
		fd = drm_reopen_driver(fd);
		igt_purge_vm_caches(fd);

		mem = igt_get_total_pinnable_mem(&size);
		igt_assert(mem != MAP_FAILED);

		igt_purge_vm_caches(fd);
		igt_system_suspend_autoresume(mode, SUSPEND_TEST_NONE);

		munmap(mem, size);
		drm_close_driver(fd);
	}

	igt_waitchildren();
}

static void
test_forcewake(int fd, bool hibernate)
{
	int suspend = hibernate ? SUSPEND_STATE_DISK : SUSPEND_STATE_MEM;
	int fw_fd;

	/* Once before to verify we can suspend */
	igt_system_suspend_autoresume(suspend, SUSPEND_TEST_NONE);

	fw_fd = igt_open_forcewake_handle(fd);
	igt_assert_lte(0, fw_fd);

	igt_system_suspend_autoresume(suspend, SUSPEND_TEST_NONE);

	close (fw_fd);
}

static void
test_suspend_without_i915(int state)
{
	struct igt_device_card card;
	uint32_t d3cold_allowed;
	int fd;

	fd = __drm_open_driver(DRIVER_INTEL);
	igt_devices_scan();

	/*
	 * When module is unloaded and s2idle is triggered, PCI core leaves the endpoint
	 * in D0 and the bridge in D3 state causing PCIE spec violation and config space
	 * is read as 0xFF. Keep the bridge in D0 before module unload to prevent
	 * this issue
	 */
	if (state == SUSPEND_STATE_FREEZE &&
	    igt_device_find_first_i915_discrete_card(&card)) {
		igt_pm_get_d3cold_allowed(card.pci_slot_name, &d3cold_allowed);
		igt_pm_set_d3cold_allowed(card.pci_slot_name, 0);
	}

	if (fd >= 0)
		drm_close_driver(fd);

	igt_kmsg(KMSG_INFO "Unloading i915\n");
	igt_assert_eq(igt_i915_driver_unload(),0);

	igt_system_suspend_autoresume(state, SUSPEND_TEST_NONE);

	if (state == SUSPEND_STATE_FREEZE && strlen(card.card))
		igt_pm_set_d3cold_allowed(card.pci_slot_name, d3cold_allowed);

	igt_kmsg(KMSG_INFO "Re-loading i915 \n");
	igt_assert_eq(igt_i915_driver_load(NULL), 0);

	igt_devices_free();
}

int fd;

igt_main
{
	igt_describe("Validate suspend-to-idle without i915 module");
	igt_subtest("basic-s2idle-without-i915")
		test_suspend_without_i915(SUSPEND_STATE_FREEZE);

	igt_describe("Validate S3 without i915 module");
	igt_subtest("basic-s3-without-i915")
		test_suspend_without_i915(SUSPEND_STATE_S3);

	igt_fixture {
		/*
		 * Since above subtests may fail, leaving i915 module unloaded
		 * but device list populated, refresh the device list before
		 * reopening the i915 device if we've been called with a device
		 * filter specified, otherwise drm_open_driver() will fail
		 * instead of reloading the i915 module.
		 */
		if (igt_device_filter_count())
			igt_devices_scan();
		fd = drm_open_driver(DRIVER_INTEL);
	}

	igt_subtest("fence-restore-tiled2untiled") {
		gem_require_mappable_ggtt(fd);
		test_fence_restore(fd, true, false);
	}

	igt_subtest("fence-restore-untiled") {
		gem_require_mappable_ggtt(fd);
		test_fence_restore(fd, false, false);
	}

	igt_subtest("debugfs-reader")
		test_debugfs_reader(fd, false);

	igt_subtest("sysfs-reader")
		test_sysfs_reader(fd, false);

	igt_subtest("shrink")
		test_shrink(fd, SUSPEND_STATE_MEM);

	igt_subtest("forcewake")
		test_forcewake(fd, false);

	igt_subtest("fence-restore-tiled2untiled-hibernate") {
		gem_require_mappable_ggtt(fd);
		test_fence_restore(fd, true, true);
	}

	igt_subtest("fence-restore-untiled-hibernate") {
		gem_require_mappable_ggtt(fd);
		test_fence_restore(fd, false, true);
	}

	igt_subtest("debugfs-reader-hibernate")
		test_debugfs_reader(fd, true);

	igt_subtest("sysfs-reader-hibernate")
		test_sysfs_reader(fd, true);

	igt_subtest("forcewake-hibernate")
		test_forcewake(fd, true);

	igt_fixture
		drm_close_driver(fd);
}
