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

#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>

#include "i915/gem.h"
#include "i915/gem_create.h"
#include "igt.h"
#include "igt_vgem.h"
#include "intel_batchbuffer.h"	/* igt_blitter_copy() */
/**
 * TEST: prime vgem
 * Description: Basic check of polling for prime/vgem fences.
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: DRM
 * Functionality: mock device
 * Feature: prime
 * Test category: GEM_Legacy
 *
 * SUBTEST: basic-blt
 * Description: Examine blitter access path.
 *
 * SUBTEST: basic-fence-blt
 * Description: Examine blitter access path fencing.
 *
 * SUBTEST: basic-fence-flip
 * Description: Examine vgem bo front/back flip fencing.
 *
 * SUBTEST: basic-fence-mmap
 * Description: Examine GTT access path fencing.
 * Feature: gtt, prime
 *
 * SUBTEST: basic-fence-read
 * Description: Examine read access path fencing.
 * Feature: gtt, prime
 *
 * SUBTEST: basic-gtt
 * Description: Examine access path through GTT.
 * Feature: gtt, prime
 *
 * SUBTEST: basic-read
 * Description: Examine read access path.
 * Feature: gtt, prime
 *
 * SUBTEST: basic-write
 * Description: Examine write access path.
 * Feature: gtt, prime
 *
 * SUBTEST: busy
 * Description: Examine busy check of polling for vgem fence.
 *
 * SUBTEST: coherency-blt
 * Description: Examine blitter access path WC coherency.
 *
 * SUBTEST: coherency-gtt
 * Description: Examine concurrent access of vgem bo.
 * Feature: gtt, prime
 *
 * SUBTEST: fence-flip-hang
 * Description: Examine vgem bo front/back flip fencing with a pending gpu hang.
 * Feature: blacklist, prime, synchronization
 *
 * SUBTEST: fence-read-hang
 * Description: Examine read access path fencing with a pending gpu hang.
 * Feature: blacklist, prime, synchronization
 *
 * SUBTEST: fence-wait
 * Description: Examine basic dma-buf fence interop.
 *
 * SUBTEST: fence-write-hang
 * Description: Examine write access path fencing with a pending gpu hang.
 * Feature: blacklist, prime, synchronization
 *
 * SUBTEST: shrink
 * Description: Examine link establishment between shrinker and vgem bo.
 *
 * SUBTEST: sync
 * Description: Examine sync on vgem fence.
 *
 * SUBTEST: wait
 * Description: Examine wait on vgem fence.
 */

IGT_TEST_DESCRIPTION("Basic check of polling for prime/vgem fences.");

static void test_read(int vgem, int i915)
{
	struct vgem_bo scratch;
	uint32_t handle;
	uint32_t *ptr;
	int dmabuf, i;

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	handle = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	igt_skip_on_f(__gem_read(i915, handle, 0, &i, sizeof(i)),
		      "PREAD from dma-buf not supported on this hardware\n");

	ptr = vgem_mmap(vgem, &scratch, PROT_WRITE);
	for (i = 0; i < 1024; i++)
		ptr[1024*i] = i;
	munmap(ptr, scratch.size);
	gem_close(vgem, scratch.handle);

	for (i = 0; i < 1024; i++) {
		uint32_t tmp;
		gem_read(i915, handle, 4096*i, &tmp, sizeof(tmp));
		igt_assert_eq(tmp, i);
	}
	gem_close(i915, handle);
}

static void test_fence_read(int i915, int vgem)
{
	struct vgem_bo scratch;
	uint32_t handle;
	uint32_t *ptr;
	uint32_t fence;
	int dmabuf, i;
	int master[2], slave[2];

	igt_assert(pipe(master) == 0);
	igt_assert(pipe(slave) == 0);

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	handle = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	igt_skip_on_f(__gem_read(i915, handle, 0, &i, sizeof(i)),
		      "PREAD from dma-buf not supported on this hardware\n");

	igt_fork(child, 1) {
		close(master[0]);
		close(slave[1]);
		for (i = 0; i < 1024; i++) {
			uint32_t tmp;
			gem_read(i915, handle, 4096*i, &tmp, sizeof(tmp));
			igt_assert_eq(tmp, 0);
		}
		write(master[1], &child, sizeof(child));
		read(slave[0], &child, sizeof(child));
		for (i = 0; i < 1024; i++) {
			uint32_t tmp;
			gem_read(i915, handle, 4096*i, &tmp, sizeof(tmp));
			igt_assert_eq(tmp, i);
		}
		gem_close(i915, handle);
	}

	close(master[1]);
	close(slave[0]);
	read(master[0], &i, sizeof(i));
	fence = vgem_fence_attach(vgem, &scratch, VGEM_FENCE_WRITE);
	write(slave[1], &i, sizeof(i));

	ptr = vgem_mmap(vgem, &scratch, PROT_WRITE);
	for (i = 0; i < 1024; i++)
		ptr[1024*i] = i;
	munmap(ptr, scratch.size);
	vgem_fence_signal(vgem, fence);
	gem_close(vgem, scratch.handle);

	igt_waitchildren();
	close(master[0]);
	close(slave[1]);
}

static void test_fence_mmap(int i915, int vgem)
{
	struct vgem_bo scratch;
	uint32_t handle;
	uint32_t *ptr;
	uint32_t fence;
	int dmabuf, i;
	int master[2], slave[2];

	igt_assert(pipe(master) == 0);
	igt_assert(pipe(slave) == 0);

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	handle = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	igt_fork(child, 1) {
		close(master[0]);
		close(slave[1]);
		ptr = gem_mmap__gtt(i915, handle, 4096*1024, PROT_READ);

		gem_set_domain(i915, handle, I915_GEM_DOMAIN_GTT, 0);
		for (i = 0; i < 1024; i++)
			igt_assert_eq(ptr[1024*i], 0);

		write(master[1], &child, sizeof(child));
		read(slave[0], &child, sizeof(child));

		gem_set_domain(i915, handle, I915_GEM_DOMAIN_GTT, 0);
		for (i = 0; i < 1024; i++)
			igt_assert_eq(ptr[1024*i], i);

		gem_close(i915, handle);
	}

	close(master[1]);
	close(slave[0]);
	read(master[0], &i, sizeof(i));
	fence = vgem_fence_attach(vgem, &scratch, VGEM_FENCE_WRITE);
	write(slave[1], &i, sizeof(i));

	ptr = vgem_mmap(vgem, &scratch, PROT_WRITE);
	for (i = 0; i < 1024; i++)
		ptr[1024*i] = i;
	munmap(ptr, scratch.size);
	vgem_fence_signal(vgem, fence);
	gem_close(vgem, scratch.handle);

	igt_waitchildren();
	close(master[0]);
	close(slave[1]);
}

static void test_fence_blt(int i915, int vgem)
{
	struct vgem_bo scratch;
	uint32_t prime;
	uint32_t *ptr;
	uint32_t fence;
	int dmabuf, i;
	int master[2], slave[2];

	igt_assert(pipe(master) == 0);
	igt_assert(pipe(slave) == 0);

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	prime = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	igt_fork(child, 1) {
		uint32_t native;
		uint64_t ahnd;

		close(master[0]);
		close(slave[1]);

		intel_allocator_init();
		ahnd = get_reloc_ahnd(i915, 0);

		native = gem_create(i915, scratch.size);

		ptr = gem_mmap__device_coherent(i915, native, 0, scratch.size, PROT_READ);
		for (i = 0; i < scratch.height; i++)
			igt_assert_eq_u32(ptr[scratch.pitch * i / sizeof(*ptr)],
					  0);

		write(master[1], &child, sizeof(child));
		read(slave[0], &child, sizeof(child));

		igt_blitter_copy(i915, ahnd, 0, NULL, prime, 0, scratch.pitch,
				 I915_TILING_NONE, 0, 0, scratch.size,
				 scratch.width, scratch.height, scratch.bpp,
				 native, 0, scratch.pitch,
				 I915_TILING_NONE, 0, 0, scratch.size);
		gem_sync(i915, native);

		for (i = 0; i < scratch.height; i++)
			igt_assert_eq_u32(ptr[scratch.pitch * i / sizeof(*ptr)],
					  i);

		munmap(ptr, scratch.size);
		gem_close(i915, native);
		gem_close(i915, prime);
		put_ahnd(ahnd);
	}

	close(master[1]);
	close(slave[0]);
	read(master[0], &i, sizeof(i));
	fence = vgem_fence_attach(vgem, &scratch, VGEM_FENCE_WRITE);
	write(slave[1], &i, sizeof(i));

	/* Emphasize that the only thing stopping the blitter is the fence */
	usleep(50*1000);

	ptr = vgem_mmap(vgem, &scratch, PROT_WRITE);
	for (i = 0; i < scratch.height; i++)
		ptr[scratch.pitch * i / sizeof(*ptr)] = i;
	munmap(ptr, scratch.size);
	vgem_fence_signal(vgem, fence);
	gem_close(vgem, scratch.handle);

	igt_waitchildren();
	close(master[0]);
	close(slave[1]);
}

static void test_write(int vgem, int i915)
{
	struct vgem_bo scratch;
	uint32_t handle;
	uint32_t *ptr;
	int dmabuf, i;

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	handle = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	igt_skip_on_f(__gem_write(i915, handle, 0, &i, sizeof(i)),
		      "PWRITE to dma-buf not supported on this hardware\n");

	ptr = vgem_mmap(vgem, &scratch, PROT_READ);
	gem_close(vgem, scratch.handle);

	for (i = 0; i < 1024; i++)
		gem_write(i915, handle, 4096*i, &i, sizeof(i));
	gem_close(i915, handle);

	for (i = 0; i < 1024; i++)
		igt_assert_eq(ptr[1024*i], i);
	munmap(ptr, scratch.size);
}

static void test_gtt(int vgem, int i915)
{
	struct vgem_bo scratch;
	uint32_t handle;
	uint32_t *ptr;
	int dmabuf, i;

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	handle = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	ptr = gem_mmap__gtt(i915, handle, scratch.size, PROT_WRITE);
	for (i = 0; i < 1024; i++)
		ptr[1024*i] = i;
	munmap(ptr, scratch.size);

	ptr = vgem_mmap(vgem, &scratch, PROT_READ | PROT_WRITE);
	for (i = 0; i < 1024; i++) {
		igt_assert_eq(ptr[1024*i], i);
		ptr[1024*i] = ~i;
	}
	munmap(ptr, scratch.size);

	ptr = gem_mmap__gtt(i915, handle, scratch.size, PROT_READ);
	for (i = 0; i < 1024; i++)
		igt_assert_eq(ptr[1024*i], ~i);
	munmap(ptr, scratch.size);

	gem_close(i915, handle);
	gem_close(vgem, scratch.handle);
}

static void test_blt(int vgem, int i915)
{
	struct vgem_bo scratch;
	uint32_t prime, native;
	uint32_t *ptr;
	int dmabuf, i;
	uint64_t ahnd = get_reloc_ahnd(i915, 0);

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	prime = prime_fd_to_handle(i915, dmabuf);

	native = gem_create(i915, scratch.size);

	ptr = gem_mmap__device_coherent(i915, native, 0, scratch.size, PROT_WRITE);
	for (i = 0; i < scratch.height; i++)
		ptr[scratch.pitch * i / sizeof(*ptr)] = i;
	munmap(ptr, scratch.size);

	igt_blitter_copy(i915, ahnd, 0, NULL, native, 0, scratch.pitch,
			 I915_TILING_NONE, 0, 0, scratch.size,
			 scratch.width, scratch.height, scratch.bpp,
			 prime, 0, scratch.pitch, I915_TILING_NONE, 0, 0,
			 scratch.size);
	prime_sync_start(dmabuf, true);
	prime_sync_end(dmabuf, true);
	close(dmabuf);

	ptr = vgem_mmap(vgem, &scratch, PROT_READ | PROT_WRITE);
	for (i = 0; i < scratch.height; i++) {
		igt_assert_eq_u32(ptr[scratch.pitch * i / sizeof(*ptr)], i);
		ptr[scratch.pitch * i / sizeof(*ptr)] = ~i;
	}
	munmap(ptr, scratch.size);

	igt_blitter_copy(i915, ahnd, 0, NULL, prime, 0, scratch.pitch,
			 I915_TILING_NONE, 0, 0, scratch.size,
			 scratch.width, scratch.height, scratch.bpp,
			 native, 0, scratch.pitch, I915_TILING_NONE, 0, 0,
			 scratch.size);
	gem_sync(i915, native);

	ptr = gem_mmap__device_coherent(i915, native, 0, scratch.size, PROT_READ);
	for (i = 0; i < scratch.height; i++)
		igt_assert_eq_u32(ptr[scratch.pitch * i / sizeof(*ptr)], ~i);
	munmap(ptr, scratch.size);

	gem_close(i915, native);
	gem_close(i915, prime);
	gem_close(vgem, scratch.handle);
	put_ahnd(ahnd);
}

static void test_shrink(int vgem, int i915)
{
	struct vgem_bo scratch = {
		.width = 1024,
		.height = 1024,
		.bpp = 32
	};
	int dmabuf;

	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	gem_close(vgem, scratch.handle);

	scratch.handle = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	/* Populate the i915_bo->pages. */
	gem_set_domain(i915, scratch.handle, I915_GEM_DOMAIN_GTT, 0);

	/* Now evict them, establishing the link from i915:shrinker to vgem. */
	igt_drop_caches_set(i915, DROP_SHRINK_ALL);

	gem_close(i915, scratch.handle);
}

static bool is_coherent(int i915)
{
	int val = 1; /* by default, we assume GTT is coherent, hence the test */
	struct drm_i915_getparam gp = {
		gp.param = 52, /* GTT_COHERENT */
		gp.value = &val,
	};

	ioctl(i915, DRM_IOCTL_I915_GETPARAM, &gp);
	return val;
}

static void test_gtt_interleaved(int vgem, int i915)
{
	struct vgem_bo scratch;
	uint32_t handle;
	uint32_t *ptr, *gtt;
	int dmabuf, i;

	igt_require(is_coherent(i915));

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	handle = prime_fd_to_handle(i915, dmabuf);
	close(dmabuf);

	/* This assumes that GTT is perfectedly coherent. On certain machines,
	 * it is possible for a direct acces to bypass the GTT indirection.
	 *
	 * This test may fail. It tells us how far userspace can trust
	 * concurrent dmabuf/i915 access. In the future, we may have a kernel
	 * param to indicate whether or not this interleaving is possible.
	 * However, the mmaps may be passed around to third parties that do
	 * not know about the shortcommings...
	 */
	ptr = vgem_mmap(vgem, &scratch, PROT_WRITE);
	gtt = gem_mmap__gtt(i915, handle, scratch.size, PROT_WRITE);
	for (i = 0; i < 1024; i++) {
		gtt[1024*i] = i;
		/* The read from WC should act as a flush for the GTT wcb */
		igt_assert_eq(ptr[1024*i], i);

		ptr[1024*i] = ~i;
		/* The read from GTT should act as a flush for the WC wcb */
		igt_assert_eq(gtt[1024*i], ~i);
	}
	munmap(gtt, scratch.size);
	munmap(ptr, scratch.size);

	gem_close(i915, handle);
	gem_close(vgem, scratch.handle);
}

static void test_blt_interleaved(int vgem, int i915)
{
	struct vgem_bo scratch;
	uint32_t prime, native;
	uint32_t *foreign, *local;
	int dmabuf, i;
	uint64_t ahnd = get_reloc_ahnd(i915, 0);

	scratch.width = 1024;
	scratch.height = 1024;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);

	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	prime = prime_fd_to_handle(i915, dmabuf);

	native = gem_create(i915, scratch.size);

	foreign = vgem_mmap(vgem, &scratch, PROT_WRITE);
	local = gem_mmap__device_coherent(i915, native, 0, scratch.size, PROT_WRITE);

	for (i = 0; i < SLOW_QUICK(scratch.height, 64); i++) {
		local[scratch.pitch * i / sizeof(*local)] = i;
		igt_blitter_copy(i915, ahnd, 0, NULL, native, 0,
				 scratch.pitch, I915_TILING_NONE, 0, i,
				 scratch.size, scratch.width, 1,
				 scratch.bpp, prime, 0, scratch.pitch,
				 I915_TILING_NONE, 0, i, scratch.size);
		prime_sync_start(dmabuf, true);
		igt_assert_eq_u32(foreign[scratch.pitch * i / sizeof(*foreign)],
				  i);
		prime_sync_end(dmabuf, true);

		foreign[scratch.pitch * i / sizeof(*foreign)] = ~i;
		igt_blitter_copy(i915, ahnd, 0, NULL, prime, 0, scratch.pitch,
				 I915_TILING_NONE, 0, i, scratch.size,
				 scratch.width, 1,
				 scratch.bpp, native, 0, scratch.pitch,
				 I915_TILING_NONE, 0, i, scratch.size);
		gem_sync(i915, native);
		igt_assert_eq_u32(local[scratch.pitch * i / sizeof(*local)],
				  ~i);
	}
	close(dmabuf);

	munmap(local, scratch.size);
	munmap(foreign, scratch.size);

	gem_close(i915, native);
	gem_close(i915, prime);
	gem_close(vgem, scratch.handle);
	put_ahnd(ahnd);
}

static bool prime_busy(int fd, bool excl)
{
	struct pollfd pfd = { .fd = fd, .events = excl ? POLLOUT : POLLIN };
	return poll(&pfd, 1, 0) == 0;
}

static void work(int i915, uint64_t ahnd, uint64_t scratch_offset, int dmabuf,
		 const intel_ctx_t *ctx, unsigned ring)
{
	const int SCRATCH = 0;
	const int BATCH = 1;
	const int gen = intel_gen(intel_get_drm_devid(i915));
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry store[1024+1];
	struct drm_i915_gem_execbuffer2 execbuf;
	unsigned size = ALIGN(ARRAY_SIZE(store)*16 + 4, 4096);
	bool read_busy, write_busy;
	uint32_t *batch, *bbe;
	int i, count;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.flags = ring;
	if (gem_store_dword_needs_secure(i915))
		execbuf.flags |= I915_EXEC_SECURE;
	execbuf.rsvd1 = ctx->id;

	memset(obj, 0, sizeof(obj));
	obj[SCRATCH].handle = prime_fd_to_handle(i915, dmabuf);

	obj[BATCH].handle = gem_create(i915, size);
	obj[BATCH].offset = get_offset(ahnd, obj[BATCH].handle, size, 0);
	obj[BATCH].relocs_ptr = (uintptr_t)store;
	obj[BATCH].relocation_count = !ahnd ? ARRAY_SIZE(store) : 0;
	memset(store, 0, sizeof(store));

	if (ahnd) {
		obj[SCRATCH].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE;
		obj[SCRATCH].offset = scratch_offset;
		obj[BATCH].flags = EXEC_OBJECT_PINNED;
	}

	batch = gem_mmap__device_coherent(i915, obj[BATCH].handle, 0, size, PROT_WRITE);
	gem_set_domain(i915, obj[BATCH].handle,
		       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	i = 0;
	for (count = 0; count < 1024; count++) {
		store[count].target_handle = obj[SCRATCH].handle;
		store[count].presumed_offset = -1;
		store[count].offset = sizeof(uint32_t) * (i + 1);
		store[count].delta = sizeof(uint32_t) * count;
		store[count].read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		store[count].write_domain = I915_GEM_DOMAIN_INSTRUCTION;
		batch[i] = MI_STORE_DWORD_IMM_GEN4 | (gen < 6 ? 1 << 22 : 0);
		if (gen >= 8) {
			batch[++i] = scratch_offset + store[count].delta;
			batch[++i] = (scratch_offset + store[count].delta) >> 32;
		} else if (gen >= 4) {
			batch[++i] = 0;
			batch[++i] = 0;
			store[count].offset += sizeof(uint32_t);
		} else {
			batch[i]--;
			batch[++i] = 0;
		}
		batch[++i] = count;
		i++;
	}

	bbe = &batch[i];
	store[count].target_handle = obj[BATCH].handle; /* recurse */
	store[count].presumed_offset = 0;
	store[count].offset = sizeof(uint32_t) * (i + 1);
	store[count].delta = 0;
	store[count].read_domains = I915_GEM_DOMAIN_COMMAND;
	store[count].write_domain = 0;
	batch[i] = MI_BATCH_BUFFER_START;
	if (gen >= 8) {
		batch[i] |= 1 << 8 | 1;
		batch[++i] = obj[BATCH].offset;
		batch[++i] = obj[BATCH].offset >> 32;
	} else if (gen >= 6) {
		batch[i] |= 1 << 8;
		batch[++i] = 0;
	} else {
		batch[i] |= 2 << 6;
		batch[++i] = 0;
		if (gen < 4) {
			batch[i] |= 1;
			store[count].delta = 1;
		}
	}
	i++;
	igt_assert(i < size/sizeof(*batch));
	igt_require(__gem_execbuf(i915, &execbuf) == 0);
	gem_close(i915, obj[BATCH].handle);
	gem_close(i915, obj[SCRATCH].handle);

	write_busy = prime_busy(dmabuf, false);
	read_busy = prime_busy(dmabuf, true);

	*bbe = MI_BATCH_BUFFER_END;
	__sync_synchronize();
	munmap(batch, size);

	igt_assert(read_busy && write_busy);
}

static void test_busy(int i915, int vgem, const intel_ctx_t *ctx, unsigned ring)
{
	struct vgem_bo scratch;
	struct timespec tv;
	uint32_t *ptr;
	int dmabuf;
	int i;
	uint64_t ahnd = get_reloc_ahnd(i915, ctx->id), scratch_offset;

	scratch.width = 1024;
	scratch.height = 1;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);
	scratch_offset = get_offset(ahnd, scratch.handle, scratch.size, 0);
	dmabuf = prime_handle_to_fd(vgem, scratch.handle);

	work(i915, ahnd, scratch_offset, dmabuf, ctx, ring);

	put_ahnd(ahnd);

	/* Calling busy in a loop should be enough to flush the rendering */
	memset(&tv, 0, sizeof(tv));
	while (prime_busy(dmabuf, false))
		igt_assert(igt_seconds_elapsed(&tv) < 10);

	ptr = vgem_mmap(vgem, &scratch, PROT_READ);
	for (i = 0; i < 1024; i++)
		igt_assert_eq_u32(ptr[i], i);
	munmap(ptr, 4096);

	gem_close(vgem, scratch.handle);
	close(dmabuf);
}

static void test_wait(int i915, int vgem, const intel_ctx_t *ctx, unsigned ring)
{
	struct vgem_bo scratch;
	struct pollfd pfd;
	uint32_t *ptr;
	int i;
	uint64_t ahnd = get_reloc_ahnd(i915, ctx->id), scratch_offset;

	scratch.width = 1024;
	scratch.height = 1;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);
	scratch_offset = get_offset(ahnd, scratch.handle, scratch.size, 0);
	pfd.fd = prime_handle_to_fd(vgem, scratch.handle);

	work(i915, ahnd, scratch_offset, pfd.fd, ctx, ring);

	put_ahnd(ahnd);

	pfd.events = POLLIN;
	igt_assert_eq(poll(&pfd, 1, 10000), 1);

	ptr = vgem_mmap(vgem, &scratch, PROT_READ);
	for (i = 0; i < 1024; i++)
		igt_assert_eq_u32(ptr[i], i);
	munmap(ptr, 4096);

	gem_close(vgem, scratch.handle);
	close(pfd.fd);
}

static void test_sync(int i915, int vgem, const intel_ctx_t *ctx, unsigned ring)
{
	struct vgem_bo scratch;
	uint32_t *ptr;
	int dmabuf;
	int i;
	uint64_t ahnd = get_reloc_ahnd(i915, ctx->id), scratch_offset;

	scratch.width = 1024;
	scratch.height = 1;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);
	scratch_offset = get_offset(ahnd, scratch.handle, scratch.size, 0);
	dmabuf = prime_handle_to_fd(vgem, scratch.handle);

	ptr = mmap(NULL, scratch.size, PROT_READ, MAP_SHARED, dmabuf, 0);
	igt_assert(ptr != MAP_FAILED);
	gem_close(vgem, scratch.handle);

	work(i915, ahnd, scratch_offset, dmabuf, ctx, ring);

	put_ahnd(ahnd);

	prime_sync_start(dmabuf, false);
	for (i = 0; i < 1024; i++)
		igt_assert_eq_u32(ptr[i], i);
	prime_sync_end(dmabuf, false);
	close(dmabuf);

	munmap(ptr, scratch.size);
}

static void test_fence_wait(int i915, int vgem, const intel_ctx_t *ctx, unsigned ring)
{
	struct vgem_bo scratch;
	uint32_t fence;
	uint32_t *ptr;
	int dmabuf;
	uint64_t ahnd = get_reloc_ahnd(i915, ctx->id), scratch_offset;

	scratch.width = 1024;
	scratch.height = 1;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);
	scratch_offset = get_offset(ahnd, scratch.handle, scratch.size, 0);
	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	fence = vgem_fence_attach(vgem, &scratch, VGEM_FENCE_WRITE);
	igt_assert(prime_busy(dmabuf, false));
	gem_close(vgem, scratch.handle);

	ptr = mmap(NULL, scratch.size, PROT_READ, MAP_SHARED, dmabuf, 0);
	igt_assert(ptr != MAP_FAILED);

	igt_fork(child, 1) {
		ahnd = get_reloc_ahnd(i915, ctx->id);
		work(i915, ahnd, scratch_offset, dmabuf, ctx, ring);
		put_ahnd(ahnd);
	}

	sleep(1);
	put_ahnd(ahnd);

	/* Check for invalidly completing the task early */
	for (int i = 0; i < 1024; i++)
		igt_assert_eq_u32(ptr[i], 0);

	igt_assert(prime_busy(dmabuf, false));
	vgem_fence_signal(vgem, fence);
	igt_waitchildren();

	/* But after signaling and waiting, it should be done */
	prime_sync_start(dmabuf, false);
	for (int i = 0; i < 1024; i++)
		igt_assert_eq_u32(ptr[i], i);
	prime_sync_end(dmabuf, false);
	close(dmabuf);

	munmap(ptr, scratch.size);
}

static void test_fence_hang(int i915, int vgem, unsigned flags)
{
	struct vgem_bo scratch;
	uint32_t *ptr;
	int dmabuf;
	int i;
	uint64_t ahnd = get_reloc_ahnd(i915, 0), scratch_offset;

	scratch.width = 1024;
	scratch.height = 1;
	scratch.bpp = 32;
	vgem_create(vgem, &scratch);
	scratch_offset = get_offset(ahnd, scratch.handle, scratch.size, 0);
	dmabuf = prime_handle_to_fd(vgem, scratch.handle);
	vgem_fence_attach(vgem, &scratch, flags | WIP_VGEM_FENCE_NOTIMEOUT);

	ptr = mmap(NULL, scratch.size, PROT_READ, MAP_SHARED, dmabuf, 0);
	igt_assert(ptr != MAP_FAILED);
	gem_close(vgem, scratch.handle);

	work(i915, ahnd, scratch_offset, dmabuf, intel_ctx_0(i915), 0);

	put_ahnd(ahnd);

	/* The work should have been cancelled */

	prime_sync_start(dmabuf, false);
	for (i = 0; i < 1024; i++)
		igt_assert_eq_u32(ptr[i], 0);
	prime_sync_end(dmabuf, false);
	close(dmabuf);

	munmap(ptr, scratch.size);
}

static bool has_prime_export(int fd)
{
	uint64_t value;

	if (drmGetCap(fd, DRM_CAP_PRIME, &value))
		return false;

	return value & DRM_PRIME_CAP_EXPORT;
}

static bool has_prime_import(int fd)
{
	uint64_t value;

	if (drmGetCap(fd, DRM_CAP_PRIME, &value))
		return false;

	return value & DRM_PRIME_CAP_IMPORT;
}

static uint32_t set_fb_on_crtc(int fd, int pipe, struct vgem_bo *bo, uint32_t fb_id)
{
	drmModeRes *resources = drmModeGetResources(fd);
	struct drm_mode_modeinfo *modes = malloc(4096*sizeof(*modes));
	uint32_t encoders[32];

	for (int o = 0; o < resources->count_connectors; o++) {
		struct drm_mode_get_connector conn;
		struct drm_mode_crtc set;
		int e, m;

		memset(&conn, 0, sizeof(conn));
		conn.connector_id = resources->connectors[o];
		drmIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
		if (!conn.count_modes)
			continue;

		igt_assert(conn.count_modes <= 4096);
		igt_assert(conn.count_encoders <= 32);

		conn.modes_ptr = (uintptr_t)modes;
		conn.encoders_ptr = (uintptr_t)encoders;
		conn.count_props = 0;
		do_or_die(drmIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn));

		for (e = 0; e < conn.count_encoders; e++) {
			struct drm_mode_get_encoder enc;

			memset(&enc, 0, sizeof(enc));
			enc.encoder_id = encoders[e];
			drmIoctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc);
			if (enc.possible_crtcs & (1 << pipe))
				break;
		}
		if (e == conn.count_encoders)
			continue;

		for (m = 0; m < conn.count_modes; m++) {
			if (modes[m].hdisplay <= bo->width &&
			    modes[m].vdisplay <= bo->height)
				break;
		}
		if (m == conn.count_modes)
			continue;

		memset(&set, 0, sizeof(set));
		set.crtc_id = resources->crtcs[pipe];
		set.fb_id = fb_id;
		set.set_connectors_ptr = (uintptr_t)&conn.connector_id;
		set.count_connectors = 1;
		set.mode = modes[m];
		set.mode_valid = 1;
		if (drmIoctl(fd, DRM_IOCTL_MODE_SETCRTC, &set) == 0) {
			drmModeFreeResources(resources);
			return set.crtc_id;
		}
	}

	drmModeFreeResources(resources);
	return 0;
}

static inline uint32_t pipe_select(int pipe)
{
	if (pipe > 1)
		return pipe << DRM_VBLANK_HIGH_CRTC_SHIFT;
	else if (pipe > 0)
		return DRM_VBLANK_SECONDARY;
	else
		return 0;
}

static unsigned get_vblank(int fd, int pipe, unsigned flags)
{
	union drm_wait_vblank vbl;

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = DRM_VBLANK_RELATIVE | pipe_select(pipe) | flags;
	if (drmIoctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl))
		return 0;

	return vbl.reply.sequence;
}

static void flip_to_vgem(int i915, int vgem,
			 struct vgem_bo *bo,
			 uint32_t fb_id,
			 uint32_t crtc_id,
			 unsigned hang,
			 const char *name)
{
	struct pollfd pfd = { i915, POLLIN };
	struct drm_event_vblank vbl;
	uint32_t fence;

	fence = vgem_fence_attach(vgem, bo, VGEM_FENCE_WRITE | hang);

	igt_fork(child, 1) { /* Use a child in case we block uninterruptibly */
		/* Check we don't block nor flip before the fence is ready */
		do_or_die(drmModePageFlip(i915, crtc_id, fb_id,
					  DRM_MODE_PAGE_FLIP_EVENT, &fb_id));
		for (int n = 0; n < 5; n++) { /* 5 frames should be <100ms */
			igt_assert_f(poll(&pfd, 1, 0) == 0,
				     "flip to %s completed whilst busy\n",
				     name);
			get_vblank(i915, 0, DRM_VBLANK_NEXTONMISS);
		}
	}
	igt_waitchildren_timeout(2, "flip blocked by waiting for busy vgem fence");

	/* And then the flip is completed as soon as it is ready */
	if (!hang) {
		unsigned long miss;

		/* Signal fence at the start of the next vblank */
		get_vblank(i915, 0, DRM_VBLANK_NEXTONMISS);
		vgem_fence_signal(vgem, fence);

		miss = 0;
		igt_until_timeout(5) {
			get_vblank(i915, 0, DRM_VBLANK_NEXTONMISS);
			if (poll(&pfd, 1, 0))
				break;
			miss++;
		}
		if (miss > 1) {
			igt_warn("Missed %lu vblanks after signaling before flip was completed\n",
				 miss);
		}
		igt_assert_eq(poll(&pfd, 1, 0), 1);
	}

	/* Even if hung, the flip must complete *eventually* */
	igt_set_timeout(20, "flip blocked by hanging vgem fence"); /* XXX lower fail threshold? */
	igt_assert_eq(read(i915, &vbl, sizeof(vbl)), sizeof(vbl));
	igt_reset_timeout();
}

static void test_flip(int i915, int vgem, unsigned hang)
{
	drmModeModeInfo *mode = NULL;
	uint32_t fb_id[2], handle[2], crtc_id;
	igt_display_t display;
	igt_output_t *output;
	struct vgem_bo bo[2];
	enum pipe pipe;

	igt_display_require(&display, i915);
	igt_display_require_output(&display);

	for_each_pipe_with_valid_output(&display, pipe, output) {
		mode = igt_output_get_mode(output);
		break;
	}

	igt_assert(mode);

	for (int i = 0; i < 2; i++) {
		uint32_t strides[4] = {};
		uint32_t offsets[4] = {};
		int fd;

		bo[i].width = mode->hdisplay;
		bo[i].height = mode->vdisplay;
		bo[i].bpp = 32;
		vgem_create(vgem, &bo[i]);

		fd = prime_handle_to_fd(vgem, bo[i].handle);
		handle[i] = prime_fd_to_handle(i915, fd);
		igt_assert(handle[i]);
		close(fd);

		strides[0] = bo[i].pitch;

		/* May skip if i915 has no displays */
		igt_require(__kms_addfb(i915, handle[i],
					bo[i].width, bo[i].height,
					DRM_FORMAT_XRGB8888, I915_TILING_NONE,
					strides, offsets, 1,
					DRM_MODE_FB_MODIFIERS,
					&fb_id[i]) == 0);
		igt_assert(fb_id[i]);
	}

	igt_require((crtc_id = set_fb_on_crtc(i915, 0, &bo[0], fb_id[0])));

	/* Bind both fb for use by flipping */
	for (int i = 1; i >= 0; i--) {
		struct drm_event_vblank vbl;

		do_or_die(drmModePageFlip(i915, crtc_id, fb_id[i],
					  DRM_MODE_PAGE_FLIP_EVENT, &fb_id[i]));
		igt_assert_eq(read(i915, &vbl, sizeof(vbl)), sizeof(vbl));
	}

	/* Schedule a flip to wait upon the frontbuffer vgem being written */
	flip_to_vgem(i915, vgem, &bo[0], fb_id[0], crtc_id, hang, "front");

	/* Schedule a flip to wait upon the backbuffer vgem being written */
	flip_to_vgem(i915, vgem, &bo[1], fb_id[1], crtc_id, hang, "back");

	for (int i = 0; i < 2; i++) {
		do_or_die(drmModeRmFB(i915, fb_id[i]));
		gem_close(i915, handle[i]);
		gem_close(vgem, bo[i].handle);
	}
}

static void test_each_engine(const char *name, int vgem, int i915,
			     void (*fn)(int i915, int vgem,
					const intel_ctx_t *ctx,
					unsigned int flags))
{
	const struct intel_execution_engine2 *e;
	const intel_ctx_t *ctx;

	igt_fixture
		ctx = intel_ctx_create_all_physical(i915);

	igt_subtest_with_dynamic(name) {
		for_each_ctx_engine(i915, ctx, e) {
			if (!gem_class_can_store_dword(i915, e->class))
				continue;

			if (!gem_class_has_mutable_submission(i915, e->class))
				continue;

			igt_dynamic_f("%s", e->name) {
				gem_quiescent_gpu(i915);
				fn(i915, vgem, ctx, e->flags);
			}
		}
	}

	igt_fixture
		intel_ctx_destroy(i915, ctx);
}

igt_main
{
	int i915 = -1;
	int vgem = -1;

	igt_fixture {
		vgem = drm_open_driver(DRIVER_VGEM);
		igt_require(has_prime_export(vgem));

		i915 = drm_open_driver_master(DRIVER_INTEL);
		igt_require_gem(i915);
		igt_require(has_prime_import(i915));
		gem_require_mmap_device_coherent(i915);
	}

	igt_describe("Examine read access path.");
	igt_subtest("basic-read")
		test_read(vgem, i915);

	igt_describe("Examine write access path.");
	igt_subtest("basic-write")
		test_write(vgem, i915);

	igt_describe("Examine access path through GTT.");
	igt_subtest("basic-gtt") {
		gem_require_mappable_ggtt(i915);
		test_gtt(vgem, i915);
	}

	igt_describe("Examine blitter access path.");
	igt_subtest("basic-blt")
		test_blt(vgem, i915);

	igt_describe("Examine link establishment between shrinker and vgem bo.");
	igt_subtest("shrink")
		test_shrink(vgem, i915);

	igt_describe("Examine concurrent access of vgem bo.");
	igt_subtest("coherency-gtt") {
		gem_require_mappable_ggtt(i915);
		test_gtt_interleaved(vgem, i915);
	}

	igt_describe("Examine blitter access path WC coherency.");
	igt_subtest("coherency-blt")
		test_blt_interleaved(vgem, i915);

	{
		static const struct {
			const char *name;
			void (*fn)(int i915, int vgem, const intel_ctx_t *ctx,
				   unsigned int engine);
			const char *describe;
		} tests[] = {
			{ "sync", test_sync, "Examine sync on vgem fence." },
			{ "busy", test_busy, "Examine busy check of polling for vgem fence." },
			{ "wait", test_wait, "Examine wait on vgem fence." },
			{ }
		};

		for (const typeof(*tests) *t = tests; t->name; t++) {
			igt_describe(t->describe);
			test_each_engine(t->name, vgem, i915, t->fn);
		}
	}

	/* Fence testing */
	igt_subtest_group {
		igt_fixture {
			igt_require(vgem_has_fences(vgem));
		}

		igt_describe("Examine read access path fencing.");
		igt_subtest("basic-fence-read")
			test_fence_read(i915, vgem);

		igt_describe("Examine GTT access path fencing.");
		igt_subtest("basic-fence-mmap") {
			gem_require_mappable_ggtt(i915);
			test_fence_mmap(i915, vgem);
		}

		igt_describe("Examine blitter access path fencing.");
		igt_subtest("basic-fence-blt")
			test_fence_blt(i915, vgem);

		igt_describe("Examine vgem bo front/back flip fencing.");
		igt_subtest("basic-fence-flip")
			test_flip(i915, vgem, 0);

		igt_subtest_group {
			igt_fixture {
				igt_require(vgem_fence_has_flag(vgem, WIP_VGEM_FENCE_NOTIMEOUT));
			}

			igt_describe("Examine read access path fencing with a pending gpu hang.");
			igt_subtest("fence-read-hang")
				test_fence_hang(i915, vgem, 0);

			igt_describe("Examine write access path fencing with a pending gpu hang.");
			igt_subtest("fence-write-hang")
				test_fence_hang(i915, vgem, VGEM_FENCE_WRITE);

			igt_describe("Examine vgem bo front/back flip fencing with a pending gpu"
				     " hang.");
			igt_subtest("fence-flip-hang")
				test_flip(i915, vgem, WIP_VGEM_FENCE_NOTIMEOUT);
		}
	}

	/* Fence testing, requires multiprocess allocator */
	igt_subtest_group {
		igt_fixture {
			igt_require(vgem_has_fences(vgem));
			intel_allocator_multiprocess_start();
		}

		igt_describe("Examine basic dma-buf fence interop.");
		test_each_engine("fence-wait", vgem, i915, test_fence_wait);

		igt_fixture {
			intel_allocator_multiprocess_stop();
		}
	}


	igt_fixture {
		drm_close_driver(i915);
		drm_close_driver(vgem);
	}
}
