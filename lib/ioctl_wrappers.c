/*
 * Copyright © 2007, 2011, 2013, 2014 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <pciaccess.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <termios.h>
#include <errno.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "i915/gem_create.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "igt_debugfs.h"
#include "igt_sysfs.h"
#include "igt_x86.h"
#include "config.h"
#include "i915/gem_mman.h"

#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#include <valgrind/memcheck.h>

#define VG(x) x
#else
#define VG(x) do {} while (0)
#endif

#include "ioctl_wrappers.h"

/**
 * SECTION:ioctl_wrappers
 * @short_description: ioctl wrappers and related functions
 * @title: ioctl wrappers
 * @include: igt.h
 *
 * This helper library contains simple functions to wrap the raw drm/i915 kernel
 * ioctls. The normal versions never pass any error codes to the caller and use
 * igt_assert() to check for error conditions instead. For some ioctls raw
 * wrappers which do pass on error codes are available. These raw wrappers have
 * a __ prefix.
 *
 * For wrappers which check for feature bits there can also be two versions: The
 * normal one simply returns a boolean to the caller. But when skipping the
 * testcase entirely is the right action then it's better to use igt_skip()
 * directly in the wrapper. Such functions have _require_ in their name to
 * distinguish them.
 */

__thread int (*igt_ioctl)(int fd, unsigned long request, void *arg) = drmIoctl;


static int
__gem_get_tiling(int fd, struct drm_i915_gem_get_tiling *arg)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_GET_TILING, arg))
		err = -errno;
	errno = 0;

	return err;
}

/**
 * gem_get_tiling:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @tiling: (out) tiling mode of the gem buffer
 * @swizzle: (out) bit 6 swizzle mode
 *
 * This wraps the GET_TILING ioctl.
 *
 * Returns whether the actual physical tiling matches the reported tiling.
 */
bool
gem_get_tiling(int fd, uint32_t handle, uint32_t *tiling, uint32_t *swizzle)
{
	struct drm_i915_gem_get_tiling get_tiling;

	memset(&get_tiling, 0, sizeof(get_tiling));
	get_tiling.handle = handle;

	igt_assert_eq(__gem_get_tiling(fd, &get_tiling), 0);

	*tiling = get_tiling.tiling_mode;
	*swizzle = get_tiling.swizzle_mode;

	return get_tiling.phys_swizzle_mode == get_tiling.swizzle_mode;
}

int __gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
{
	struct drm_i915_gem_set_tiling st;
	int ret;

	/* The kernel doesn't know about these tiling modes, expects NONE */
	if (tiling == I915_TILING_Yf || tiling == I915_TILING_Ys)
		tiling = I915_TILING_NONE;

	memset(&st, 0, sizeof(st));
	do {
		st.handle = handle;
		st.tiling_mode = tiling;
		st.stride = tiling ? stride : 0;

		ret = ioctl(fd, DRM_IOCTL_I915_GEM_SET_TILING, &st);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));
	if (ret != 0)
		return -errno;

	errno = 0;
	igt_assert(st.tiling_mode == tiling);
	return 0;
}

/**
 * gem_set_tiling:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @tiling: tiling mode bits
 * @stride: stride of the buffer when using a tiled mode, otherwise must be 0
 *
 * This wraps the SET_TILING ioctl.
 */
void gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride)
{
	igt_assert(__gem_set_tiling(fd, handle, tiling, stride) == 0);
}

int __gem_set_caching(int fd, uint32_t handle, uint32_t caching)
{
	struct drm_i915_gem_caching arg;
	int err;

	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;
	arg.caching = caching;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_SET_CACHING, &arg))
		err = -errno;

	errno = 0;
	return err;
}

/**
 * gem_set_caching:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @caching: caching mode bits
 *
 * This wraps the SET_CACHING ioctl. Note that this function internally calls
 * igt_require() when SET_CACHING isn't available, hence automatically skips the
 * test. Therefore always extract test logic which uses this into its own
 * subtest.
 */
void gem_set_caching(int fd, uint32_t handle, uint32_t caching)
{
	igt_require(__gem_set_caching(fd, handle, caching) == 0);
}

/**
 * gem_get_caching:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This wraps the GET_CACHING ioctl.
 *
 * Returns: The current caching mode bits.
 */
uint32_t gem_get_caching(int fd, uint32_t handle)
{
	struct drm_i915_gem_caching arg;
	int ret;

	memset(&arg, 0, sizeof(arg));
	arg.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_I915_GEM_GET_CACHING, &arg);
	igt_assert(ret == 0);
	errno = 0;

	return arg.caching;
}

/**
 * gem_open:
 * @fd: open i915 drm file descriptor
 * @name: flink buffer name
 *
 * This wraps the GEM_OPEN ioctl, which is used to import an flink name.
 *
 * Returns: gem file-private buffer handle of the open object.
 */
uint32_t gem_open(int fd, uint32_t name)
{
	struct drm_gem_open open_struct;
	int ret;

	memset(&open_struct, 0, sizeof(open_struct));
	open_struct.name = name;
	ret = ioctl(fd, DRM_IOCTL_GEM_OPEN, &open_struct);
	igt_assert(ret == 0);
	igt_assert(open_struct.handle != 0);
	errno = 0;

	return open_struct.handle;
}

/**
 * gem_flink:
 * @fd: open i915 drm file descriptor
 * @handle: file-private gem buffer object handle
 *
 * This wraps the GEM_FLINK ioctl, which is used to export a gem buffer object
 * into the device-global flink namespace. See gem_open() for opening such a
 * buffer name on a different i915 drm file descriptor.
 *
 * Returns: The created flink buffer name.
 */
uint32_t gem_flink(int fd, uint32_t handle)
{
	struct drm_gem_flink flink;
	int ret;

	memset(&flink, 0, sizeof(flink));
	flink.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_GEM_FLINK, &flink);
	igt_assert(ret == 0);
	errno = 0;

	return flink.name;
}

/**
 * gem_close:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This wraps the GEM_CLOSE ioctl, which to release a file-private gem buffer
 * handle.
 */
void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close_bo;

	igt_assert_neq(handle, 0);

	memset(&close_bo, 0, sizeof(close_bo));
	close_bo.handle = handle;
	do_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo);
}

static bool is_cache_coherent(int fd, uint32_t handle)
{
	return gem_get_caching(fd, handle) != I915_CACHING_NONE;
}

static void mmap_write(int fd, uint32_t handle, uint64_t offset,
		       const void *buf, uint64_t length)
{
	void *map = NULL;

	if (!length)
		return;

	if (gem_has_lmem(fd)) {
		/*
		 * set/get_caching and set_domain are no longer supported on
		 * discrete, also the only mmap mode supportd is FIXED.
		 */
		map = gem_mmap_offset__fixed(fd, handle, 0,
					     offset + length,
					     PROT_READ | PROT_WRITE);
		igt_assert_eq(gem_wait(fd, handle, 0), 0);
	}

	if (!map && is_cache_coherent(fd, handle)) {
		/* offset arg for mmap functions must be 0 */
		map = __gem_mmap__cpu_coherent(fd, handle, 0, offset + length,
					       PROT_READ | PROT_WRITE);
		if (map)
			gem_set_domain(fd, handle,
				       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
	}

	if (!map) {
		map = __gem_mmap_offset__wc(fd, handle, 0, offset + length,
					    PROT_READ | PROT_WRITE);
		if (!map)
			map = gem_mmap__wc(fd, handle, 0, offset + length,
					   PROT_READ | PROT_WRITE);
		gem_set_domain(fd, handle,
			       I915_GEM_DOMAIN_WC, I915_GEM_DOMAIN_WC);
	}

	memcpy(map + offset, buf, length);
	munmap(map, offset + length);
}

static void mmap_read(int fd, uint32_t handle, uint64_t offset, void *buf, uint64_t length)
{
	void *map = NULL;

	if (!length)
		return;

	if (gem_has_lmem(fd)) {
		/*
		 * set/get_caching and set_domain are no longer supported on
		 * discrete, also the only supported mmap mode is FIXED.
		 */
		map = gem_mmap_offset__fixed(fd, handle, 0,
					     offset + length, PROT_READ);
		igt_assert_eq(gem_wait(fd, handle, 0), 0);
	}

	if (!map && (gem_has_llc(fd) || is_cache_coherent(fd, handle))) {
		/* offset arg for mmap functions must be 0 */
		map = __gem_mmap__cpu_coherent(fd, handle, 0,
					       offset + length, PROT_READ);
		if (map)
			gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, 0);
	}

	if (!map) {
		map = __gem_mmap_offset__wc(fd, handle, 0, offset + length,
					    PROT_READ);
		if (!map)
			map = gem_mmap__wc(fd, handle, 0, offset + length,
					   PROT_READ);
		gem_set_domain(fd, handle, I915_GEM_DOMAIN_WC, 0);
	}

	igt_memcpy_from_wc(buf, map + offset, length);
	munmap(map, offset + length);
}

int __gem_write(int fd, uint32_t handle, uint64_t offset, const void *buf, uint64_t length)
{
	struct drm_i915_gem_pwrite gem_pwrite;
	int err;

	memset(&gem_pwrite, 0, sizeof(gem_pwrite));
	gem_pwrite.handle = handle;
	gem_pwrite.offset = offset;
	gem_pwrite.size = length;
	gem_pwrite.data_ptr = to_user_pointer(buf);

	err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &gem_pwrite))
		err = -errno;
	return err;
}

/**
 * gem_write:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset within the buffer of the subrange
 * @buf: pointer to the data to write into the buffer
 * @length: size of the subrange
 *
 * Method to write to a gem object. Uses the PWRITE ioctl when it is
 * available, else it uses mmap + memcpy to upload linear data to a
 * subrange of a gem buffer object.
 */
void gem_write(int fd, uint32_t handle, uint64_t offset, const void *buf, uint64_t length)
{
	int ret = __gem_write(fd, handle, offset, buf, length);

	igt_assert(ret == 0 || ret == -EOPNOTSUPP);

	if (ret == -EOPNOTSUPP)
		mmap_write(fd, handle, offset, buf, length);
}

int __gem_read(int fd, uint32_t handle, uint64_t offset, void *buf, uint64_t length)
{
	struct drm_i915_gem_pread gem_pread;
	int err;

	memset(&gem_pread, 0, sizeof(gem_pread));
	gem_pread.handle = handle;
	gem_pread.offset = offset;
	gem_pread.size = length;
	gem_pread.data_ptr = to_user_pointer(buf);

	err = 0;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_PREAD, &gem_pread))
		err = -errno;
	return err;
}
/**
 * gem_read:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @offset: offset within the buffer of the subrange
 * @buf: pointer to the data to read into
 * @length: size of the subrange
 *
 * Method to read from a gem object. Uses the PREAD ioctl when it is
 * available, else it uses mmap + memcpy to download linear data from a
 * subrange of a gem buffer object.
 */
void gem_read(int fd, uint32_t handle, uint64_t offset, void *buf, uint64_t length)
{
	int ret = __gem_read(fd, handle, offset, buf, length);

	igt_assert(ret == 0 || ret == -EOPNOTSUPP);

	if (ret == -EOPNOTSUPP)
		mmap_read(fd, handle, offset, buf, length);
}

/**
 * gem_has_pwrite
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether pwrite ioctl is supported
 */
bool gem_has_pwrite(int fd)
{
	uint32_t handle = gem_create(fd, 4096);
	int buf, ret;

	ret = __gem_write(fd, handle, 0, &buf, sizeof(buf));
	gem_close(fd, handle);

	return ret != -EOPNOTSUPP;
}

/**
 * gem_has_pread
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether pread ioctl is supported
 */
bool gem_has_pread(int fd)
{
	uint32_t handle = gem_create(fd, 4096);
	int buf, ret;

	ret = __gem_read(fd, handle, 0, &buf, sizeof(buf));
	gem_close(fd, handle);

	return ret != -EOPNOTSUPP;
}

/**
 * gem_require_pread_pwrite
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether pread/pwrite ioctls are supported
 * and skip if they are not
 */
void gem_require_pread_pwrite(int fd)
{
	igt_require(gem_has_pread(fd) && gem_has_pwrite(fd));
}

int __gem_set_domain(int fd, uint32_t handle, uint32_t read, uint32_t write)
{
	struct drm_i915_gem_set_domain set_domain;
	int err;

	memset(&set_domain, 0, sizeof(set_domain));
	set_domain.handle = handle;
	set_domain.read_domains = read;
	set_domain.write_domain = write;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain))
		err = -errno;

	return err;
}

/**
 * gem_set_domain:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @read: gem domain bits for read access
 * @write: gem domain bit for write access
 *
 * This wraps the SET_DOMAIN ioctl, which is used to control the coherency of
 * the gem buffer object between the cpu and gtt mappings. It is also use to
 * synchronize with outstanding rendering in general, but for that use-case
 * please have a look at gem_sync().
 */
void gem_set_domain(int fd, uint32_t handle, uint32_t read, uint32_t write)
{
	int ret = __gem_set_domain(fd, handle, read, write);

	if (ret == -ENODEV && gem_has_lmem(fd))
		igt_assert_eq(gem_wait(fd, handle, 0), 0);
	else
		igt_assert_eq(ret, 0);
}

/**
 * __gem_wait:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @timeout_ns: [in] time to wait, [out] remaining time (in nanoseconds)
 *
 * This functions waits for outstanding rendering to complete, upto
 * the timeout_ns. If no timeout_ns is provided, the wait is indefinite and
 * only returns upon an error or when the rendering is complete.
 */
int gem_wait(int fd, uint32_t handle, int64_t *timeout_ns)
{
	struct drm_i915_gem_wait wait;
	int ret;

	memset(&wait, 0, sizeof(wait));
	wait.bo_handle = handle;
	wait.timeout_ns = timeout_ns ? *timeout_ns : -1;
	wait.flags = 0;

	ret = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_WAIT, &wait))
		ret = -errno;
	errno = 0;

	if (timeout_ns)
		*timeout_ns = wait.timeout_ns;

	return ret;
}

/**
 * gem_sync:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This functions waits for outstanding rendering to complete.
 */
void gem_sync(int fd, uint32_t handle)
{
	if (gem_wait(fd, handle, NULL))
		gem_set_domain(fd, handle,
			       I915_GEM_DOMAIN_GTT,
			       I915_GEM_DOMAIN_GTT);
	errno = 0;
}

/**
 * gem_buffer_create_fb_obj:
 * @fd: open i915 drm file descriptor
 * @size: desired size of the buffer
 *
 * This wraps the GEM_CREATE ioctl, which allocates a new gem buffer object of
 * @size from file descriptor specific region
 *
 * Returns: The file-private handle of the created buffer object
 */
uint32_t gem_buffer_create_fb_obj(int fd, uint64_t size)
{
	uint32_t handle;

	if (gem_has_lmem(fd))
		handle = gem_create_in_memory_regions(fd, size, REGION_LMEM(0));
	else
		handle = gem_create(fd, size);

	return handle;
}

/**
 * __gem_execbuf:
 * @fd: open i915 drm file descriptor
 * @execbuf: execbuffer data structure
 *
 * This wraps the EXECBUFFER2 ioctl, which submits a batchbuffer for the gpu to
 * run. This is allowed to fail, with -errno returned.
 */
int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf)) {
		err = -errno;
		igt_assume(err != 0);
	}
	errno = 0;
	return err;
}

/**
 * gem_execbuf:
 * @fd: open i915 drm file descriptor
 * @execbuf: execbuffer data structure
 *
 * This wraps the EXECBUFFER2 ioctl, which submits a batchbuffer for the gpu to
 * run.
 */
void gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	igt_assert_eq(__gem_execbuf(fd, execbuf), 0);
}

/**
 * __gem_execbuf_wr:
 * @fd: open i915 drm file descriptor
 * @execbuf: execbuffer data structure
 *
 * This wraps the EXECBUFFER2_WR ioctl, which submits a batchbuffer for the gpu to
 * run. This is allowed to fail, with -errno returned.
 */
int __gem_execbuf_wr(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2_WR, execbuf)) {
		err = -errno;
		igt_assume(err != 0);
	}
	errno = 0;
	return err;
}

/**
 * gem_execbuf_wr:
 * @fd: open i915 drm file descriptor
 * @execbuf: execbuffer data structure
 *
 * This wraps the EXECBUFFER2_WR ioctl, which submits a batchbuffer for the gpu to
 * run.
 */
void gem_execbuf_wr(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	igt_assert_eq(__gem_execbuf_wr(fd, execbuf), 0);
}

/**
 * gem_madvise:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 * @state: desired madvise state
 *
 * This wraps the MADVISE ioctl, which is used in libdrm to implement
 * opportunistic buffer object caching. Objects in the cache are set to DONTNEED
 * (internally in the kernel tracked as purgeable objects). When such a cached
 * object is in need again it must be set back to WILLNEED before first use.
 *
 * Returns: When setting the madvise state to WILLNEED this returns whether the
 * backing storage was still available or not.
 */
int gem_madvise(int fd, uint32_t handle, int state)
{
	struct drm_i915_gem_madvise madv;

	memset(&madv, 0, sizeof(madv));
	madv.handle = handle;
	madv.madv = state;
	madv.retained = 1;
	do_ioctl(fd, DRM_IOCTL_I915_GEM_MADVISE, &madv);

	return madv.retained;
}

int __gem_userptr(int fd, void *ptr, uint64_t size, int read_only, uint32_t flags, uint32_t *handle)
{
	struct drm_i915_gem_userptr userptr;

	memset(&userptr, 0, sizeof(userptr));
	userptr.user_ptr = to_user_pointer(ptr);
	userptr.user_size = size;
	userptr.flags = flags;
	if (read_only)
		userptr.flags |= I915_USERPTR_READ_ONLY;

	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_USERPTR, &userptr))
		return -errno;

	*handle = userptr.handle;
	return 0;
}

/**
 * gem_userptr:
 * @fd: open i915 drm file descriptor
 * @ptr: userptr pointer to be passed
 * @size: desired size of the buffer
 * @read_only: specify whether userptr is opened read only
 * @flags: other userptr flags
 * @handle: returned handle for the object
 *
 * Returns userptr handle for the GEM object.
 */
void gem_userptr(int fd, void *ptr, uint64_t size, int read_only, uint32_t flags, uint32_t *handle)
{
	igt_assert_eq(__gem_userptr(fd, ptr, size, read_only, flags, handle), 0);
}

/**
 * gem_sw_finish:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This wraps the SW_FINISH ioctl, which is used to flush out frontbuffer
 * rendering done through the direct cpu memory mappings. Shipping userspace
 * does _not_ call this after frontbuffer rendering through gtt memory mappings.
 */
void gem_sw_finish(int fd, uint32_t handle)
{
	struct drm_i915_gem_sw_finish finish;

	memset(&finish, 0, sizeof(finish));
	finish.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_SW_FINISH, &finish);
}

/**
 * gem_bo_busy:
 * @fd: open i915 drm file descriptor
 * @handle: gem buffer object handle
 *
 * This wraps the BUSY ioctl, which tells whether a buffer object is still
 * actively used by the gpu in a execbuffer.
 *
 * Returns: The busy state of the buffer object.
 */
bool gem_bo_busy(int fd, uint32_t handle)
{
	struct drm_i915_gem_busy busy;

	memset(&busy, 0, sizeof(busy));
	busy.handle = handle;

	do_ioctl(fd, DRM_IOCTL_I915_GEM_BUSY, &busy);

	return !!busy.busy;
}


/* feature test helpers */

/**
 * gem_gtt_type:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check what type of gtt is being used by the kernel:
 * 0 - global gtt
 * 1 - aliasing ppgtt
 * 2 - full ppgtt
 *
 * Returns: Type of gtt being used.
 */
static int gem_gtt_type(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_HAS_ALIASING_PPGTT;
	gp.value = &val;

	if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp)))
		return 0;

	errno = 0;
	return val;
}

/**
 * gem_uses_ppgtt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check whether the kernel internally uses ppgtt to
 * execute batches. Note that this is also true when we're using full ppgtt.
 *
 * Returns: Whether batches are run through ppgtt.
 */
bool gem_uses_ppgtt(int fd)
{
	return gem_gtt_type(fd) > 0;
}

/**
 * gem_uses_full_ppgtt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check whether the kernel internally uses full
 * per-process gtt to execute batches. Note that this is also true when we're
 * using full 64b ppgtt.
 *
 * Returns: Whether batches are run through full ppgtt.
 */
bool gem_uses_full_ppgtt(int fd)
{
	return gem_gtt_type(fd) > 1;
}

/**
 * gem_gpu_reset_type:
 * @fd: open i915 drm file descriptor
 *
 * Query whether reset-engine (2), global-reset (1) or reset-disable (0)
 * is available.
 *
 * Returns: GPU reset type available
 */
int gem_gpu_reset_type(int fd)
{
	struct drm_i915_getparam gp;
	int gpu_reset_type = -1;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_HAS_GPU_RESET;
	gp.value = &gpu_reset_type;
	drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);

	return gpu_reset_type;
}

/**
 * gem_gpu_reset_enabled:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check whether the kernel internally uses hangchecks
 * and can reset the GPU upon hang detection. Note that this is also true when
 * reset-engine (the lightweight, single engine reset) is available.
 *
 * Returns: Whether the driver will detect hangs and perform a reset.
 */
bool gem_gpu_reset_enabled(int fd)
{
	return gem_gpu_reset_type(fd) > 0;
}

/**
 * gem_engine_reset_enabled:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to check whether the kernel internally uses hangchecks
 * and can reset individual engines upon hang detection.
 *
 * Returns: Whether the driver will detect hangs and perform an engine reset.
 */
bool gem_engine_reset_enabled(int fd)
{
	return gem_gpu_reset_type(fd) > 1;
}

bool gem_has_llc(int fd)
{
	int has_llc;
	struct drm_i915_getparam gp;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_HAS_LLC;
	gp.value = &has_llc;

	has_llc = 0;
	ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
	errno = 0;

	return has_llc;
}

static bool has_param(int fd, int param)
{
	drm_i915_getparam_t gp;
	int tmp = 0;

	memset(&gp, 0, sizeof(gp));
	gp.value = &tmp;
	gp.param = param;

	if (igt_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return false;

	errno = 0;
	return tmp > 0;
}

/**
 * gem_has_bsd:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the BSD ring is available.
 *
 * Note that recent Bspec calls this the VCS ring for Video Command Submission.
 *
 * Returns: Whether the BSD ring is available or not.
 */
bool gem_has_bsd(int fd)
{
	return has_param(fd, I915_PARAM_HAS_BSD);
}

/**
 * gem_has_blt:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the blitter ring is available.
 *
 * Note that recent Bspec calls this the BCS ring for Blitter Command Submission.
 *
 * Returns: Whether the blitter ring is available or not.
 */
bool gem_has_blt(int fd)
{
	return has_param(fd, I915_PARAM_HAS_BLT);
}

/**
 * gem_has_vebox:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the vebox ring is available.
 *
 * Note that recent Bspec calls this the VECS ring for Video Enhancement Command
 * Submission.
 *
 * Returns: Whether the vebox ring is available or not.
 */
bool gem_has_vebox(int fd)
{
	return has_param(fd, I915_PARAM_HAS_VEBOX);
}

#define I915_PARAM_HAS_BSD2 31
/**
 * gem_has_bsd2:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the BSD2 ring is available.
 *
 * Note that recent Bspec calls this the VCS ring for Video Command Submission.
 *
 * Returns: Whether the BSD ring is avaible or not.
 */
bool gem_has_bsd2(int fd)
{
	return has_param(fd, I915_PARAM_HAS_BSD2);
}

/**
 * gem_has_softpin:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the softpinning functionality is
 * supported.
 *
 * Returns: Whether softpin support is available
 */
bool gem_has_softpin(int fd)
{
	int has_softpin;
	struct drm_i915_getparam gp;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_HAS_EXEC_SOFTPIN;
	gp.value = &has_softpin;

	has_softpin = 0;
	ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
	errno = 0;

	return has_softpin;
}

/**
 * gem_has_exec_fence:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether in/out fence support in execbuffer is
 * available.
 *
 * Returns: Whether fence support is available
 */
bool gem_has_exec_fence(int fd)
{
	int has_exec_fence;
	struct drm_i915_getparam gp;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_HAS_EXEC_FENCE;
	gp.value = &has_exec_fence;

	has_exec_fence = 0;
	ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp, sizeof(gp));
	errno = 0;

	return has_exec_fence;
}

/**
 * gem_require_caching:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether buffer object caching control is
 * available. Automatically skips through igt_require() if not.
 */
void gem_require_caching(int fd)
{
	uint32_t handle;

	handle = gem_create(fd, 4096);
	gem_set_caching(fd, handle, 0);
	gem_close(fd, handle);

	errno = 0;
}

/**
 * gem_require_ring:
 * @fd: open i915 drm file descriptor
 * @ring: ring flag bit as used in gem_execbuf()
 *
 * Feature test macro to query whether a specific ring is available.
 * This automagically skips if the ring isn't available by
 * calling igt_require().
 */
void gem_require_ring(int fd, unsigned ring)
{
	igt_require(gem_has_ring(fd, ring));
}

/**
 * gem_has_mocs_registers:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the device has MOCS registers.
 * These exist gen 9+.
 */
bool gem_has_mocs_registers(int fd)
{
	return intel_gen(intel_get_drm_devid(fd)) >= 9;
}

/**
 * gem_require_mocs_registers:
 * @fd: open i915 drm file descriptor
 *
 * Feature test macro to query whether the device has MOCS registers.
 * These exist gen 9+.
 */
void gem_require_mocs_registers(int fd)
{
	igt_require(gem_has_mocs_registers(fd));
}

/* prime */

/**
 * prime_handle_to_fd:
 * @fd: open i915 drm file descriptor
 * @handle: file-private gem buffer object handle
 *
 * This wraps the PRIME_HANDLE_TO_FD ioctl, which is used to export a gem buffer
 * object into a global (i.e. potentially cross-device) dma-buf file-descriptor
 * handle.
 *
 * Returns: The created dma-buf fd handle.
 */
int prime_handle_to_fd(int fd, uint32_t handle)
{
	struct drm_prime_handle args;

	memset(&args, 0, sizeof(args));
	args.handle = handle;
	args.flags = DRM_CLOEXEC;
	args.fd = -1;

	do_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);

	return args.fd;
}

/**
 * prime_handle_to_fd_for_mmap:
 * @fd: open i915 drm file descriptor
 * @handle: file-private gem buffer object handle
 *
 * Same as prime_handle_to_fd above but with DRM_RDWR capabilities, which can
 * be useful for writing into the mmap'ed dma-buf file-descriptor.
 *
 * Returns: The created dma-buf fd handle or -1 if the ioctl fails.
 */
int prime_handle_to_fd_for_mmap(int fd, uint32_t handle)
{
	struct drm_prime_handle args;

	memset(&args, 0, sizeof(args));
	args.handle = handle;
	args.flags = DRM_CLOEXEC | DRM_RDWR;
	args.fd = -1;

	if (igt_ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args) != 0)
		return -1;

	return args.fd;
}

/**
 * prime_fd_to_handle:
 * @fd: open i915 drm file descriptor
 * @dma_buf_fd: dma-buf fd handle
 *
 * This wraps the PRIME_FD_TO_HANDLE ioctl, which is used to import a dma-buf
 * file-descriptor into a gem buffer object.
 *
 * Returns: The created gem buffer object handle.
 */
uint32_t prime_fd_to_handle(int fd, int dma_buf_fd)
{
	struct drm_prime_handle args;

	memset(&args, 0, sizeof(args));
	args.fd = dma_buf_fd;
	args.flags = 0;
	args.handle = 0;

	do_ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &args);

	return args.handle;
}

/**
 * prime_get_size:
 * @dma_buf_fd: dma-buf fd handle
 *
 * This wraps the lseek() protocol used to query the invariant size of a
 * dma-buf.  Not all kernels support this, which is check with igt_require() and
 * so will result in automagic test skipping.
 *
 * Returns: The lifetime-invariant size of the dma-buf object.
 */
off_t prime_get_size(int dma_buf_fd)
{
	off_t ret;

	ret = lseek(dma_buf_fd, 0, SEEK_END);
	igt_assert(ret >= 0 || errno == ESPIPE);
	igt_require(ret >= 0);
	errno = 0;

	return ret;
}

/**
 * prime_sync_start
 * @dma_buf_fd: dma-buf fd handle
 * @write: read/write or read-only access
 *
 * Must be called before starting CPU mmap access to a dma-buf.
 */
void prime_sync_start(int dma_buf_fd, bool write)
{
	struct local_dma_buf_sync sync_start;

	memset(&sync_start, 0, sizeof(sync_start));
	sync_start.flags = LOCAL_DMA_BUF_SYNC_START;
	sync_start.flags |= LOCAL_DMA_BUF_SYNC_READ;
	if (write)
		sync_start.flags |= LOCAL_DMA_BUF_SYNC_WRITE;
	do_ioctl(dma_buf_fd, LOCAL_DMA_BUF_IOCTL_SYNC, &sync_start);
}

/**
 * prime_sync_end
 * @dma_buf_fd: dma-buf fd handle
 * @write: read/write or read-only access
 *
 * Must be called after finishing CPU mmap access to a dma-buf.
 */
void prime_sync_end(int dma_buf_fd, bool write)
{
	struct local_dma_buf_sync sync_end;

	memset(&sync_end, 0, sizeof(sync_end));
	sync_end.flags = LOCAL_DMA_BUF_SYNC_END;
	sync_end.flags |= LOCAL_DMA_BUF_SYNC_READ;
	if (write)
		sync_end.flags |= LOCAL_DMA_BUF_SYNC_WRITE;
	do_ioctl(dma_buf_fd, LOCAL_DMA_BUF_IOCTL_SYNC, &sync_end);
}

bool igt_has_fb_modifiers(int fd)
{
	bool has_modifiers;
	uint64_t cap_modifiers;
	int ret;

	ret = drmGetCap(fd, DRM_CAP_ADDFB2_MODIFIERS, &cap_modifiers);
	igt_assert(ret == 0 || errno == EINVAL || errno == EOPNOTSUPP);
	has_modifiers = ret == 0 && cap_modifiers == 1;

	return has_modifiers;
}

/**
 * igt_require_fb_modifiers:
 * @fd: Open DRM file descriptor.
 *
 * Requires presence of DRM_CAP_ADDFB2_MODIFIERS.
 */
void igt_require_fb_modifiers(int fd)
{
	igt_require(igt_has_fb_modifiers(fd));
}

int __kms_addfb(int fd, uint32_t handle,
		uint32_t width, uint32_t height,
		uint32_t pixel_format, uint64_t modifier,
		uint32_t strides[4], uint32_t offsets[4],
		int num_planes, uint32_t flags, uint32_t *buf_id)
{
	struct drm_mode_fb_cmd2 f;
	int ret, i;

	if (flags & DRM_MODE_FB_MODIFIERS)
		igt_require_fb_modifiers(fd);

	memset(&f, 0, sizeof(f));

	f.width  = width;
	f.height = height;
	f.pixel_format = pixel_format;
	f.flags = flags;

	for (i = 0; i < num_planes; i++) {
		f.handles[i] = handle;
		f.modifier[i] = modifier;
		f.pitches[i] = strides[i];
		f.offsets[i] = offsets[i];
	}

	ret = igt_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);

	*buf_id = f.fb_id;

	return ret < 0 ? -errno : ret;
}

/**
 * igt_has_drm_cap:
 * @fd: Open DRM file descriptor.
 * @capability: DRM capability
 *
 * This helper verifies if the passed capability is supported by the kernel.
 * This function asserts in case of a bad file descriptor.
 *
 * Returns: negative value if error (e.g. cap does not exist), 0 if cap is
 * not supported, 1 if cap is supported.
 */
int igt_has_drm_cap(int fd, uint64_t capability)
{
	uint64_t value = 0;
	int ret;

	ret = drmGetCap(fd, capability, &value);
	if (ret) {
		igt_assert_neq(errno, EBADF);
		return -errno;
	}

	return value ? 1 : 0;
}

/**
 * igt_has_set_caching:
 * @devid: platform id.
 *
 * This helper verifies if the passed platform id
 * has support for setting cache.
 *
 * Returns: Whether the cache setting  is supported or not.
 */
bool igt_has_set_caching(uint32_t devid)
{
	return IS_METEORLAKE(devid) ? false : true;
}
