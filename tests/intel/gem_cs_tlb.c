/*
 * Copyright © 2011,2012 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

/*
 * Testcase: Check whether we correctly invalidate the cs tlb
 *
 * Motivated by a strange bug on launchpad where *acth != ipehr, on snb notably
 * where everything should be coherent by default.
 *
 * https://bugs.launchpad.net/ubuntu/+source/xserver-xorg-video-intel/+bug/1063252
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <drm.h>

#include "i915/gem.h"
#include "i915/gem_create.h"
#include "igt.h"
/**
 * TEST: gem cs tlb
 * Description: Check whether we correctly invalidate the cs tlb.
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: Memory management tests
 * Functionality: tlb
 * Feature: mapping
 * Test category: GEM_Legacy
 *
 * SUBTEST: engines
 */

IGT_TEST_DESCRIPTION("Check whether we correctly invalidate the cs tlb.");

#define BATCH_SIZE (1024*1024)

static bool has_softpin(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 37; /* I915_PARAM_HAS_EXEC_SOFTPIN */
	gp.value = &val;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;

	errno = 0;
	return (val == 1);
}

static void *
mmap_coherent(int fd, uint32_t handle, int size)
{
	int domain;
	void *ptr;

	if (gem_has_llc(fd) || !gem_mmap__has_wc(fd)) {
		domain = I915_GEM_DOMAIN_CPU;
		ptr = gem_mmap__cpu(fd, handle, 0, size, PROT_WRITE);
	} else {
		domain = I915_GEM_DOMAIN_WC;
		ptr = gem_mmap__wc(fd, handle, 0, size, PROT_WRITE);
	}

	gem_set_domain(fd, handle, domain, domain);
	return ptr;
}

static void run_on_ring(int fd, const intel_ctx_t *ctx,
			unsigned ring_id, const char *ring_name)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 execobj;
	struct {
		uint32_t handle;
		uint32_t *batch;
	} obj[2];
	unsigned i;

	igt_require(has_softpin(fd));

	for (i = 0; i < 2; i++) {
		obj[i].handle = gem_create(fd, BATCH_SIZE);
		obj[i].batch = mmap_coherent(fd, obj[i].handle, BATCH_SIZE);
		memset(obj[i].batch, 0xff, BATCH_SIZE);
	}

	memset(&execobj, 0, sizeof(execobj));
	execobj.handle = obj[0].handle;
	obj[0].batch[0] = MI_BATCH_BUFFER_END;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(&execobj);
	execbuf.buffer_count = 1;
	execbuf.rsvd1 = ctx->id;
	execbuf.flags = ring_id;

	/* Execute once to allocate a gtt-offset */
	gem_execbuf(fd, &execbuf);
	execobj.flags = EXEC_OBJECT_PINNED;

	i = 0;
	igt_until_timeout(2) {
		execobj.handle = obj[i&1].handle;
		obj[i&1].batch[i*64/4] = MI_BATCH_BUFFER_END;
		execbuf.batch_start_offset = i*64;

		gem_execbuf(fd, &execbuf);
		if (++i == BATCH_SIZE / 64)
			break;
	}
	igt_info("Completed %d cycles\n", i);

	for (i = 0; i < 2; i++) {
		gem_close(fd, obj[i].handle);
		munmap(obj[i].batch, BATCH_SIZE);
	}
}

igt_main
{
	const struct intel_execution_engine2 *e;
	const intel_ctx_t *ctx;
	int fd = -1;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		igt_require_gem(fd);
		ctx = intel_ctx_create_all_physical(fd);
	}

	igt_subtest_with_dynamic("engines") {
		for_each_ctx_engine(fd, ctx, e) {
			igt_dynamic_f("%s", e->name)
				run_on_ring(fd, ctx, e->flags, e->name);
		}
	}

	igt_fixture
		drm_close_driver(fd);
}
