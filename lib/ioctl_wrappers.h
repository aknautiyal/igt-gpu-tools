/*
 * Copyright © 2007,2014 Intel Corporation
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


#ifndef IOCTL_WRAPPERS_H
#define IOCTL_WRAPPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <i915_drm.h>

#include "i915/gem_context.h"
#include "i915/gem_scheduler.h"
#include "i915/intel_memory_region.h"

/**
 * igt_ioctl:
 * @fd: file descriptor
 * @request: IOCTL request number
 * @arg: argument pointer
 *
 * This is a wrapper around drmIoctl(), which can be augmented with special code
 * blocks like #igt_while_interruptible.
 */
extern __thread int (*igt_ioctl)(int fd, unsigned long request, void *arg);

/* ioctl_wrappers.c:
 *
 * ioctl wrappers and similar stuff for bare metal testing */
bool gem_get_tiling(int fd, uint32_t handle, uint32_t *tiling, uint32_t *swizzle);
void gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride);
int __gem_set_tiling(int fd, uint32_t handle, uint32_t tiling, uint32_t stride);

int __gem_set_caching(int fd, uint32_t handle, uint32_t caching);
void gem_set_caching(int fd, uint32_t handle, uint32_t caching);
uint32_t gem_get_caching(int fd, uint32_t handle);
uint32_t gem_flink(int fd, uint32_t handle);
uint32_t gem_open(int fd, uint32_t name);
void gem_close(int fd, uint32_t handle);
int __gem_write(int fd, uint32_t handle, uint64_t offset, const void *buf, uint64_t length);
void gem_write(int fd, uint32_t handle, uint64_t offset,  const void *buf, uint64_t length);
int __gem_read(int fd, uint32_t handle, uint64_t offset, void *buf, uint64_t length);
void gem_read(int fd, uint32_t handle, uint64_t offset, void *buf, uint64_t length);
bool gem_has_pwrite(int fd);
bool gem_has_pread(int fd);
void gem_require_pread_pwrite(int fd);
int __gem_set_domain(int fd, uint32_t handle, uint32_t read, uint32_t write);
void gem_set_domain(int fd, uint32_t handle, uint32_t read, uint32_t write);
int gem_wait(int fd, uint32_t handle, int64_t *timeout_ns);
void gem_sync(int fd, uint32_t handle);
uint32_t gem_buffer_create_fb_obj(int fd, uint64_t size);
void gem_execbuf_wr(int fd, struct drm_i915_gem_execbuffer2 *execbuf);
int __gem_execbuf_wr(int fd, struct drm_i915_gem_execbuffer2 *execbuf);
void gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf);
int __gem_execbuf(int fd, struct drm_i915_gem_execbuffer2 *execbuf);

#ifndef I915_GEM_DOMAIN_WC
#define I915_GEM_DOMAIN_WC 0x80
#endif

int gem_madvise(int fd, uint32_t handle, int state);

void gem_userptr(int fd, void *ptr, uint64_t size, int read_only, uint32_t flags, uint32_t *handle);
int __gem_userptr(int fd, void *ptr, uint64_t size, int read_only, uint32_t flags, uint32_t *handle);

void gem_sw_finish(int fd, uint32_t handle);

bool gem_bo_busy(int fd, uint32_t handle);

/* feature test helpers */
bool gem_has_llc(int fd);
bool gem_has_bsd(int fd);
bool gem_has_blt(int fd);
bool gem_has_vebox(int fd);
bool gem_has_bsd2(int fd);
bool gem_uses_ppgtt(int fd);
bool gem_uses_full_ppgtt(int fd);
int gem_gpu_reset_type(int fd);
bool gem_gpu_reset_enabled(int fd);
bool gem_engine_reset_enabled(int fd);
uint64_t gem_total_stolen_size(int fd);
bool gem_has_softpin(int fd);
bool gem_has_exec_fence(int fd);

/* check functions which auto-skip tests by calling igt_skip() */
void gem_require_caching(int fd);
void gem_require_ring(int fd, unsigned ring);
bool gem_has_mocs_registers(int fd);
void gem_require_mocs_registers(int fd);

#define gem_has_ring(f, r) gem_context_has_engine(f, 0, r)

/* prime */
struct local_dma_buf_sync {
	uint64_t flags;
};

#define LOCAL_DMA_BUF_SYNC_READ      (1 << 0)
#define LOCAL_DMA_BUF_SYNC_WRITE     (2 << 0)
#define LOCAL_DMA_BUF_SYNC_RW        (LOCAL_DMA_BUF_SYNC_READ | LOCAL_DMA_BUF_SYNC_WRITE)
#define LOCAL_DMA_BUF_SYNC_START     (0 << 2)
#define LOCAL_DMA_BUF_SYNC_END       (1 << 2)
#define LOCAL_DMA_BUF_SYNC_VALID_FLAGS_MASK \
		(LOCAL_DMA_BUF_SYNC_RW | LOCAL_DMA_BUF_SYNC_END)

#define LOCAL_DMA_BUF_BASE 'b'
#define LOCAL_DMA_BUF_IOCTL_SYNC _IOW(LOCAL_DMA_BUF_BASE, 0, struct local_dma_buf_sync)

int prime_handle_to_fd(int fd, uint32_t handle);
#ifndef DRM_RDWR
#define DRM_RDWR O_RDWR
#endif
int prime_handle_to_fd_for_mmap(int fd, uint32_t handle);
uint32_t prime_fd_to_handle(int fd, int dma_buf_fd);
off_t prime_get_size(int dma_buf_fd);
void prime_sync_start(int dma_buf_fd, bool write);
void prime_sync_end(int dma_buf_fd, bool write);

bool igt_has_fb_modifiers(int fd);
void igt_require_fb_modifiers(int fd);
int igt_has_drm_cap(int fd, uint64_t capability);
bool igt_has_set_caching(uint32_t devid);

/**
 * __kms_addfb:
 *
 * Creates a framebuffer object.
 */
int __kms_addfb(int fd, uint32_t handle,
		uint32_t width, uint32_t height,
		uint32_t pixel_format, uint64_t modifier,
		uint32_t strides[4], uint32_t offsets[4],
		int num_planes, uint32_t flags, uint32_t *buf_id);

/**
 * to_user_pointer:
 *
 * Makes sure that pointer on 32 and 64-bit systems
 * are casted properly for being sent through an ioctl.
 */
static inline uint64_t to_user_pointer(const void *ptr)
{
	return (uintptr_t)ptr;
}

/**
 * from_user_pointer:
 *
 * Casts a 64bit value from an ioctl into a pointer.
 */
static inline void *from_user_pointer(uint64_t u64p)
{
	return (void *)(uintptr_t)u64p;
}

/**
 * lower_32_bits:
 *
 * return bits 0-31 of a number.
 */
static inline uint32_t lower_32_bits(uint64_t x)
{
	return x & 0xffffffff;
}

/**
 * upper_32_bits:
 *
 * return bits 32-63 of a number.
 */
static inline uint32_t upper_32_bits(uint64_t x)
{
	return x >> 32;
}

#endif /* IOCTL_WRAPPERS_H */
