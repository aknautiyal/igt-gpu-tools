/*
 * Copyright © 2016 Intel Corporation
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
 */

#include "igt.h"
#include "igt_kmod.h"
#include "igt_vgem.h"
#include "igt_debugfs.h"
#include "igt_sysfs.h"

#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
/**
 * TEST: vgem basic
 * Description: Basic sanity check of Virtual GEM module (vGEM).
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: DRM
 * Functionality: mock device
 * Test category: GEM_Legacy
 * Feature: vgem
 *
 * SUBTEST: bad-fence
 * Description: Make sure a non-existent fence cannot be signaled.
 *
 * SUBTEST: bad-flag
 * Description: Make sure a fence cannot be attached and signaled with invalid flags.
 *
 * SUBTEST: bad-handle
 * Description: Make sure a fence cannot be attached to a invalid handle.
 *
 * SUBTEST: bad-pad
 * Description: Make sure a non-zero pad is rejected.
 *
 * SUBTEST: busy-fence
 * Description: Make sure a conflicting fence cannot be attached.
 *
 * SUBTEST: create
 * Description: Check the basic working of vgem_create ioctl.
 *
 * SUBTEST: debugfs
 * Description: Check the basic access to debugfs and also try to read entries in the directory.
 *
 * SUBTEST: dmabuf-export
 * Description: Check whether it can export/import the vgem handle using prime.
 * Feature: prime, vgem
 *
 * SUBTEST: dmabuf-fence
 * Description: Check the working of dma-buf fence interop.
 * Feature: prime, vgem
 *
 * SUBTEST: dmabuf-fence-before
 * Description: Attach a fence before exporting a vgem handle and check the working of fence.
 * Feature: prime, vgem
 *
 * SUBTEST: dmabuf-mmap
 * Description: Export the vgem handle along with RDWR capabilities using prime and check if it can be mmaped.
 * Feature: prime, vgem
 *
 * SUBTEST: mmap
 * Description: Create a vgem handle and check if it can be mmaped.
 *
 * SUBTEST: second-client
 * Description: Check whether it can open multiple clients.
 *
 * SUBTEST: setversion
 * Description: Check the working of SET_VERSION ioctl.
 *
 * SUBTEST: sysfs
 * Description: Check the basic access to sysfs and also try to read entries in the directory.
 *
 * SUBTEST: unload
 * Description: Basic test for handling of module unload.
 */

IGT_TEST_DESCRIPTION("Basic sanity check of Virtual GEM module (vGEM).");

static int __gem_setversion(int fd, drm_set_version_t *sv)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_SET_VERSION, sv))
		err = -errno;
	errno = 0;

	return err;
}

static void test_setversion(int fd)
{
	drm_set_version_t sv;

	memset(&sv, 0, sizeof(sv));
	sv.drm_di_major = 1; /* must be equal to DRM_IF_MAJOR */
	sv.drm_di_minor = 4; /* must be less than DRM_IF_MINOR */
	sv.drm_dd_major = -1; /* don't care */
	sv.drm_dd_minor = -1; /* don't care */
	igt_assert_eq(__gem_setversion(fd, &sv), 0);

	igt_info("vgem DRM interface v%d.%d, device v%d.%d\n",
		 sv.drm_di_major, sv.drm_di_minor,
		 sv.drm_dd_major, sv.drm_dd_minor);
}

static void test_client(int fd)
{
	drm_close_driver(drm_open_driver(DRIVER_VGEM));
	drm_close_driver(drm_open_driver_render(DRIVER_VGEM));
}

static void test_create(int fd)
{
	struct vgem_bo bo;

	bo.width = 0;
	bo.height = 0;
	bo.bpp = 0;
	igt_assert_eq(__vgem_create(fd, &bo), -EINVAL);

	bo.width = 1;
	bo.height = 1;
	bo.bpp = 1;
	vgem_create(fd, &bo);
	igt_assert_eq(bo.size, 4096);
	gem_close(fd, bo.handle);

	bo.width = 1024;
	bo.height = 1024;
	bo.bpp = 8;
	vgem_create(fd, &bo);
	igt_assert_eq(bo.size, 1<<20);
	gem_close(fd, bo.handle);

	bo.width = 1<<15;
	bo.height = 1<<15;
	bo.bpp = 16;
	vgem_create(fd, &bo);
	igt_assert_eq(bo.size, 1<<31);
	gem_close(fd, bo.handle);
}

static void test_mmap(int fd)
{
	struct vgem_bo bo;
	uint32_t *ptr;

	bo.width = 1024;
	bo.height = 1024;
	bo.bpp = 32;
	vgem_create(fd, &bo);

	ptr = vgem_mmap(fd, &bo, PROT_WRITE);
	gem_close(fd, bo.handle);

	for (int page = 0; page < bo.size >> 12; page++)
		ptr[page] = 0;

	munmap(ptr, bo.size);
}

static bool has_prime_import(int fd)
{
	uint64_t value;

	if (drmGetCap(fd, DRM_CAP_PRIME, &value))
		return false;

	return value & DRM_PRIME_CAP_IMPORT;
}

static void test_dmabuf_export(int fd)
{
	struct vgem_bo bo;
	uint32_t handle;
	int other;
	int dmabuf;

	other = drm_open_driver(DRIVER_ANY);
	igt_require(has_prime_import(other));

	bo.width = 1024;
	bo.height = 1;
	bo.bpp = 32;

	vgem_create(fd, &bo);
	dmabuf = prime_handle_to_fd(fd, bo.handle);
	gem_close(fd, bo.handle);

	handle = prime_fd_to_handle(other, dmabuf);
	close(dmabuf);
	gem_close(other, handle);
	close(other);
}

static void test_busy_fence(int fd)
{
	struct drm_vgem_fence_attach arg = {};
	struct vgem_bo bo;

	bo.width = 1024;
	bo.height = 1;
	bo.bpp = 32;
	vgem_create(fd, &bo);

	/* Attach a fence for reading */
	vgem_fence_attach(fd, &bo, 0);

	/* Attach a fence for writing, so it should be an exclusive fence */
	arg.handle = bo.handle;
	arg.flags = VGEM_FENCE_WRITE;

	/* As the fence is not exclusive, return -EBUSY, indicating a conflicting fence */
	do_ioctl_err(fd, DRM_IOCTL_VGEM_FENCE_ATTACH, &arg, EBUSY);
}

static void test_dmabuf_mmap(int fd)
{
	struct vgem_bo bo;
	uint32_t *ptr;
	int export;

	bo.width = 1024;
	bo.height = 1024;
	bo.bpp = 32;
	vgem_create(fd, &bo);

	export = prime_handle_to_fd_for_mmap(fd, bo.handle);
	ptr = mmap(NULL, bo.size, PROT_WRITE, MAP_SHARED, export, 0);
	close(export);
	igt_assert(ptr != MAP_FAILED);

	for (int page = 0; page < bo.size >> 12; page++)
		ptr[page] = page;
	munmap(ptr, bo.size);

	ptr = vgem_mmap(fd, &bo, PROT_READ);
	gem_close(fd, bo.handle);

	for (int page = 0; page < bo.size >> 12; page++)
		igt_assert_eq(ptr[page], page);
	munmap(ptr, bo.size);
}

static bool prime_busy(int fd, bool excl)
{
	struct pollfd pfd = { .fd = fd, .events = excl ? POLLOUT : POLLIN };
	return poll(&pfd, 1, 0) == 0;
}

static void test_dmabuf_fence(int fd)
{
	struct vgem_bo bo;
	int dmabuf;
	uint32_t fence;

	bo.width = 1024;
	bo.height = 1;
	bo.bpp = 32;
	vgem_create(fd, &bo);

	/* export, then fence */

	dmabuf = prime_handle_to_fd(fd, bo.handle);

	fence = vgem_fence_attach(fd, &bo, 0);
	igt_assert(!prime_busy(dmabuf, false));
	igt_assert(prime_busy(dmabuf, true));

	vgem_fence_signal(fd, fence);
	igt_assert(!prime_busy(dmabuf, false));
	igt_assert(!prime_busy(dmabuf, true));

	fence = vgem_fence_attach(fd, &bo, VGEM_FENCE_WRITE);
	igt_assert(prime_busy(dmabuf, false));
	igt_assert(prime_busy(dmabuf, true));

	vgem_fence_signal(fd, fence);
	igt_assert(!prime_busy(dmabuf, false));
	igt_assert(!prime_busy(dmabuf, true));

	close(dmabuf);
	gem_close(fd, bo.handle);
}

static void test_dmabuf_fence_before(int fd)
{
	struct vgem_bo bo;
	int dmabuf;
	uint32_t fence;

	bo.width = 1024;
	bo.height = 1;
	bo.bpp = 32;
	vgem_create(fd, &bo);

	fence = vgem_fence_attach(fd, &bo, 0);
	dmabuf = prime_handle_to_fd(fd, bo.handle);

	igt_assert(!prime_busy(dmabuf, false));
	igt_assert(prime_busy(dmabuf, true));

	vgem_fence_signal(fd, fence);
	igt_assert(!prime_busy(dmabuf, false));
	igt_assert(!prime_busy(dmabuf, true));

	close(dmabuf);
	gem_close(fd, bo.handle);

	vgem_create(fd, &bo);

	fence = vgem_fence_attach(fd, &bo, VGEM_FENCE_WRITE);
	dmabuf = prime_handle_to_fd(fd, bo.handle);
	igt_assert(prime_busy(dmabuf, false));
	igt_assert(prime_busy(dmabuf, true));

	vgem_fence_signal(fd, fence);
	igt_assert(!prime_busy(dmabuf, false));
	igt_assert(!prime_busy(dmabuf, true));

	close(dmabuf);
	gem_close(fd, bo.handle);
}

static void test_sysfs_read(int fd)
{
	int dir = igt_sysfs_open(fd);
	DIR *dirp = fdopendir(dir);
	struct dirent *de;

	while ((de = readdir(dirp))) {
		struct stat st;

		if (*de->d_name == '.')
			continue;

		if (fstatat(dir, de->d_name, &st, 0))
			continue;

		if (S_ISDIR(st.st_mode))
			continue;

		igt_debug("Reading %s\n", de->d_name);
		igt_set_timeout(1, "vgem sysfs read stalled");
		free(igt_sysfs_get(dir, de->d_name));
		igt_reset_timeout();
	}

	closedir(dirp);
	close(dir);
}

static void test_debugfs_read(int fd)
{
	int dir = igt_debugfs_dir(fd);
	DIR *dirp = fdopendir(dir);
	struct dirent *de;

	igt_assert(dirp);
	while ((de = readdir(dirp))) {
		struct stat st;

		if (*de->d_name == '.')
			continue;

		if (fstatat(dir, de->d_name, &st, 0))
			continue;

		if (S_ISDIR(st.st_mode))
			continue;

		igt_debug("Reading %s\n", de->d_name);
		igt_set_timeout(1, "vgem debugfs read stalled");
		free(igt_sysfs_get(dir, de->d_name));
		igt_reset_timeout();
	}

	closedir(dirp);
	close(dir);
}

static int module_unload(void)
{
	return igt_kmod_unload("vgem");
}

static void test_unload(void)
{
	struct vgem_bo bo;
	int vgem, dmabuf;
	uint32_t *ptr;

	/* Load and unload vgem just to make sure it exists */
	vgem = __drm_open_driver(DRIVER_VGEM);
	igt_require(vgem != -1);
	close(vgem);
	igt_require(module_unload() == 0);

	vgem = __drm_open_driver(DRIVER_VGEM);
	igt_assert(vgem != -1);

	/* The driver should stop the module from unloading */
	igt_assert_f(module_unload() != 0,
		     "open(//dev/vgem) should keep the module alive\n");

	bo.width = 1024;
	bo.height = 1;
	bo.bpp = 32;
	vgem_create(vgem, &bo);
	close(vgem);

	/* Closing the driver should clear all normal references */
	igt_assert_f(module_unload() == 0,
		     "No open(/dev/vgem), should be able to unload\n");

	vgem = __drm_open_driver(DRIVER_VGEM);
	igt_assert(vgem != -1);
	bo.width = 1024;
	bo.height = 1;
	bo.bpp = 32;
	vgem_create(vgem, &bo);
	dmabuf = prime_handle_to_fd(vgem, bo.handle);
	close(vgem);

	/* A dmabuf should prevent module unload. */
	igt_assert_f(module_unload() != 0,
		     "A dmabuf should keep the module alive\n");

	close(dmabuf);
	igt_assert_f(module_unload() == 0,
		     "No open dmabuf, should be able to unload\n");

	vgem = __drm_open_driver(DRIVER_VGEM);
	igt_assert(vgem != -1);
	bo.width = 1024;
	bo.height = 1;
	bo.bpp = 32;
	vgem_create(vgem, &bo);
	dmabuf = prime_handle_to_fd_for_mmap(vgem, bo.handle);
	close(vgem);

	ptr = mmap(NULL, bo.size, PROT_WRITE, MAP_SHARED, dmabuf, 0);
	igt_assert(ptr != MAP_FAILED);
	close(dmabuf);

	/* Although closed, the mmap should keep the dmabuf/module alive */
	igt_assert_f(module_unload() != 0,
		     "A mmap should keep the module alive\n");

	for (int page = 0; page < bo.size >> 12; page++)
		ptr[1024*page + page%1024] = page;

	/* And finally we should have no more uses on the module. */
	munmap(ptr, bo.size);

	igt_assert_f(module_unload() == 0,
		     "No mmap anymore, should be able to unload\n");

}

static bool has_prime_export(int fd)
{
	uint64_t value;

	if (drmGetCap(fd, DRM_CAP_PRIME, &value))
		return false;

	return value & DRM_PRIME_CAP_EXPORT;
}

igt_main
{
	int fd = -1;

	igt_describe("Basic test for handling of module unload.");
	igt_subtest("unload")
		test_unload();

	igt_fixture {
		fd = drm_open_driver(DRIVER_VGEM);
	}

	igt_describe("Check the working of SET_VERSION ioctl.");
	igt_subtest_f("setversion")
		test_setversion(fd);

	igt_describe("Check whether it can open multiple clients.");
	igt_subtest_f("second-client")
		test_client(fd);

	igt_describe("Check the basic working of vgem_create ioctl.");
	igt_subtest_f("create")
		test_create(fd);

	igt_describe("Create a vgem handle and check if it can be mmaped.");
	igt_subtest_f("mmap")
		test_mmap(fd);

	igt_describe("Make sure a fence cannot be attached and signaled with invalid flags.");
	igt_subtest("bad-flag") {
		struct drm_vgem_fence_attach attach = {
			.flags = 0xff,
		};

		struct drm_vgem_fence_signal signal = {
			.flags = 0xff,
		};

		do_ioctl_err(fd, DRM_IOCTL_VGEM_FENCE_ATTACH, &attach, EINVAL);
		do_ioctl_err(fd, DRM_IOCTL_VGEM_FENCE_SIGNAL, &signal, EINVAL);
	}

	igt_describe("Make sure a non-zero pad is rejected.");
	igt_subtest("bad-pad") {
		struct drm_vgem_fence_attach arg = {
			.pad = 0x01,
		};
		do_ioctl_err(fd, DRM_IOCTL_VGEM_FENCE_ATTACH, &arg, EINVAL);
	}

	igt_describe("Make sure a fence cannot be attached to a invalid handle.");
	igt_subtest("bad-handle") {
		struct drm_vgem_fence_attach arg = {
			.handle = 0xff,
		};
		do_ioctl_err(fd, DRM_IOCTL_VGEM_FENCE_ATTACH, &arg, ENOENT);
	}

	igt_describe("Make sure a non-existent fence cannot be signaled.");
	igt_subtest("bad-fence") {
		struct drm_vgem_fence_signal arg = {
			.fence = 0xff,
		};
		do_ioctl_err(fd, DRM_IOCTL_VGEM_FENCE_SIGNAL, &arg, ENOENT);
	}

	igt_describe("Make sure a conflicting fence cannot be attached.");
	igt_subtest("busy-fence")
		test_busy_fence(fd);

	igt_subtest_group {
		igt_fixture {
			igt_require(has_prime_export(fd));
		}

		igt_describe("Check whether it can export/import the vgem handle"
			     " using prime.");
		igt_subtest("dmabuf-export")
			test_dmabuf_export(fd);

		igt_describe("Export the vgem handle along with RDWR capabilities"
			     " using prime and check if it can be mmaped.");
		igt_subtest("dmabuf-mmap")
			test_dmabuf_mmap(fd);

		igt_subtest_group {
			igt_fixture {
				igt_require(vgem_has_fences(fd));
			}

			igt_describe("Check the working of dma-buf fence interop.");
			igt_subtest("dmabuf-fence")
				test_dmabuf_fence(fd);
			igt_describe("Attach a fence before exporting a vgem handle"
				     " and check the working of fence.");
			igt_subtest("dmabuf-fence-before")
				test_dmabuf_fence_before(fd);
		}
	}

	igt_describe("Check the basic access to sysfs and also try to"
		     " read entries in the directory.");
	igt_subtest("sysfs")
		test_sysfs_read(fd);
	igt_describe("Check the basic access to debugfs and also try to"
		     " read entries in the directory.");
	igt_subtest("debugfs")
		test_debugfs_read(fd);

	igt_fixture {
		drm_close_driver(fd);
	}
}
