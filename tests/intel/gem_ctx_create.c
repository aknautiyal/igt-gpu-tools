/*
 * Copyright © 2012 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "i915/gem.h"
#include "i915/gem_create.h"
#include "igt.h"
#include "igt_rand.h"
#include "sw_sync.h"
/**
 * TEST: gem ctx create
 * Description: Test the context create ioctls
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: CMD submission
 * Functionality: context
 * Feature: context
 * Test category: GEM_Legacy
 *
 * SUBTEST: active
 * Description: For each engine calculate the average performance of context
 *		creation execution and exercise context reclaim
 *
 * SUBTEST: active-all
 * Description: Calculate the average performance of context creation and it's
 *		execution using all engines
 *
 * SUBTEST: basic
 * Description: Test random context creation
 *
 * SUBTEST: basic-files
 * Description: Exercise implicit per-fd context creation
 *
 * SUBTEST: ext-param
 * Description: Verify valid and invalid context extensions
 *
 * SUBTEST: files
 * Description: Exercise implicit per-fd context creation on 1 CPU for long duration
 *
 * SUBTEST: forked-active
 * Description: For each engine calculate the average performance of context
 *		creation and execution on multiple parallel processes
 *
 * SUBTEST: forked-active-all
 * Description: Calculate the average performance of context creation and it's
 *		execution using all engines on multiple parallel processes
 *
 * SUBTEST: forked-files
 * Description: Exercise implicit per-fd context creation on all CPUs for long
 *		duration
 *
 * SUBTEST: hog
 * Description: For each engine calculate the average performance of context
 *		creation and execution while all other engines are hogging the
 *		resources
 *
 * SUBTEST: iris-pipeline
 * Description: Set, validate and execute particular context params
 *
 * SUBTEST: maximum-mem
 * Description: Create contexts up to available RAM size, calculate the average
 *		performance of their execution on multiple parallel processes
 *
 * SUBTEST: maximum-swap
 * Description: Create contexts up to available RAM+SWAP size, calculate the
 *		average performance of their execution on multiple parallel
 *		processes
 *
 */

IGT_TEST_DESCRIPTION("Test the context create ioctls");

#define ENGINE_FLAGS  (I915_EXEC_RING_MASK | I915_EXEC_BSD_MASK)

static unsigned all_engines[I915_EXEC_RING_MASK + 1];
static unsigned all_nengine;

static unsigned ppgtt_engines[I915_EXEC_RING_MASK + 1];
static unsigned ppgtt_nengine;

static int create_ioctl(int fd, struct drm_i915_gem_context_create *arg)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, arg)) {
		err = -errno;
		igt_assume(err);
	}

	errno = 0;
	return err;
}

static int create_ext_ioctl(int i915,
			    struct drm_i915_gem_context_create_ext *arg)
{
	int err;

	err = 0;
	if (igt_ioctl(i915, DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT, arg)) {
		err = -errno;
		igt_assume(err);
	}

	errno = 0;
	return err;
}

static double elapsed(const struct timespec *start,
		      const struct timespec *end)
{
	return (end->tv_sec - start->tv_sec) + 1e-9*(end->tv_nsec - start->tv_nsec);
}

static void files(int core, const intel_ctx_cfg_t *cfg,
		  int timeout, const int ncpus)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	uint32_t batch, name;

	batch = gem_create(core, 4096);
	gem_write(core, batch, 0, &bbe, sizeof(bbe));
	name = gem_flink(core, batch);

	memset(&obj, 0, sizeof(obj));
	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(&obj);
	execbuf.buffer_count = 1;

	igt_fork(child, ncpus) {
		struct timespec start, end;
		unsigned count = 0;
		const intel_ctx_t *ctx;
		int fd;

		clock_gettime(CLOCK_MONOTONIC, &start);
		do {
			fd = drm_reopen_driver(core);

			ctx = intel_ctx_create(fd, cfg);
			execbuf.rsvd1 = ctx->id;

			obj.handle = gem_open(fd, name);
			execbuf.flags &= ~ENGINE_FLAGS;
			execbuf.flags |= ppgtt_engines[count % ppgtt_nengine];
			gem_execbuf(fd, &execbuf);

			intel_ctx_destroy(fd, ctx);
			drm_close_driver(fd);
			count++;

			clock_gettime(CLOCK_MONOTONIC, &end);
		} while (elapsed(&start, &end) < timeout);

		gem_sync(core, batch);
		clock_gettime(CLOCK_MONOTONIC, &end);
		igt_info("[%d] File creation + execution: %.3f us\n",
			 child, elapsed(&start, &end) / count *1e6);
	}
	igt_waitchildren();

	gem_close(core, batch);
}

static void active(int fd, const intel_ctx_cfg_t *cfg,
		   const struct intel_execution_engine2 *e,
		   int timeout, int ncpus)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	unsigned int nengine, engines[ARRAY_SIZE(all_engines)];
	unsigned *shared;
	/* When e is NULL, test would run for all engines */
	if (!e) {
		igt_require(all_nengine);
		nengine = all_nengine;
		memcpy(engines, all_engines, sizeof(engines[0])*nengine);
	} else {
		nengine = 1;
		engines[0] = e->flags;
	}

	shared = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	igt_assert(shared != MAP_FAILED);

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(&obj);
	execbuf.buffer_count = 1;

	if (ncpus < 0) {
		igt_fork(child, ppgtt_nengine) {
			unsigned long count = 0;
			const intel_ctx_t *ctx;

			/*
			 * Ensure the gpu is idle by launching
			 * a nop execbuf and stalling for it
			 */
			gem_quiescent_gpu(fd);

			if (ppgtt_engines[child] == e->flags)
				continue;

			ctx = intel_ctx_create(fd, cfg);
			execbuf.rsvd1 = ctx->id;
			execbuf.flags = ppgtt_engines[child];

			while (!READ_ONCE(*shared)) {
				obj.handle = gem_create(fd, 4096 << 10);
				gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

				gem_execbuf(fd, &execbuf);
				gem_close(fd, obj.handle);
				count++;
			}

			igt_debug("hog[%d]: cycles=%lu\n", child, count);
			intel_ctx_destroy(fd, ctx);
		}
		ncpus = -ncpus;
	}

	igt_fork(child, ncpus) {
		struct timespec start, end;
		unsigned count = 0;

		/*
		 * Ensure the gpu is idle by launching
		 * a nop execbuf and stalling for it.
		 */
		gem_quiescent_gpu(fd);

		clock_gettime(CLOCK_MONOTONIC, &start);
		do {
			const intel_ctx_t *ctx = intel_ctx_create(fd, cfg);
			execbuf.rsvd1 = ctx->id;
			for (unsigned n = 0; n < nengine; n++) {
				execbuf.flags = engines[n];
				gem_execbuf(fd, &execbuf);
			}
			intel_ctx_destroy(fd, ctx);
			count++;

			clock_gettime(CLOCK_MONOTONIC, &end);
		} while (elapsed(&start, &end) < timeout);

		gem_sync(fd, obj.handle);
		clock_gettime(CLOCK_MONOTONIC, &end);
		igt_info("[%d] Context creation + execution: %.3f us\n",
			 child, elapsed(&start, &end) / count *1e6);

		shared[0] = 1;
	}
	igt_waitchildren();

	gem_close(fd, obj.handle);
	munmap(shared, 4096);
}

static void xchg_u32(void *array, unsigned i, unsigned j)
{
	uint32_t *a = array, tmp;

	tmp = a[i];
	a[i] = a[j];
	a[j] = tmp;
}

static void xchg_ptr(void *array, unsigned i, unsigned j)
{
	void **a = array, *tmp;

	tmp = a[i];
	a[i] = a[j];
	a[j] = tmp;
}

static unsigned __context_size(int fd)
{
	switch (intel_gen(intel_get_drm_devid(fd))) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7: return 17 << 12;
	case 8: return 20 << 12;
	case 9: return 22 << 12;
	default: return 32 << 12;
	}
}

static unsigned context_size(int fd)
{
	uint64_t size;

	size = __context_size(fd);
	if (ppgtt_nengine > 1) {
		size += 4 << 12; /* ringbuffer as well */
		size *= ppgtt_nengine;
	}

	return size;
}

static uint64_t total_avail_mem(unsigned mode)
{
	uint64_t total = igt_get_avail_ram_mb();
	if (mode & CHECK_SWAP)
		total += igt_get_total_swap_mb();
	return total << 20;
}

static void maximum(int fd, const intel_ctx_cfg_t *cfg,
		    int ncpus, unsigned mode)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	uint64_t avail_mem = total_avail_mem(mode);
	unsigned ctx_size = context_size(fd);
	const intel_ctx_t **contexts = NULL;
	unsigned long count = 0;
	const intel_ctx_t *ctx;

	do {
		int err;

		if ((count & -count) == count) {
			int sz = count ? 2*count : 1;
			contexts = realloc(contexts,
					   sz*sizeof(*contexts));
			igt_assert(contexts);
		}

		err = -ENOMEM;
		if (avail_mem > (count + 1) * ctx_size)
			err = __intel_ctx_create(fd, cfg, &ctx);
		if (err) {
			igt_info("Created %lu contexts, before failing with '%s' [%d]\n",
				 count, strerror(-err), -err);
			break;
		}

		contexts[count++] = ctx;
	} while (1);
	igt_require(count);

	memset(obj, 0, sizeof(obj));
	obj[1].handle = gem_create(fd, 4096);
	gem_write(fd, obj[1].handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(obj);
	execbuf.buffer_count = 2;

	igt_fork(child, ncpus) {
		struct timespec start, end;

		hars_petruska_f54_1_random_perturb(child);
		obj[0].handle = gem_create(fd, 4096);

		clock_gettime(CLOCK_MONOTONIC, &start);
		for (int repeat = 0; repeat < 3; repeat++) {
			igt_permute_array(contexts, count, xchg_ptr);
			igt_permute_array(all_engines, all_nengine, xchg_u32);

			for (unsigned long i = 0; i < count; i++) {
				execbuf.rsvd1 = contexts[i]->id;
				for (unsigned long j = 0; j < all_nengine; j++) {
					execbuf.flags = all_engines[j];
					gem_execbuf(fd, &execbuf);
				}
			}
		}
		gem_sync(fd, obj[0].handle);
		clock_gettime(CLOCK_MONOTONIC, &end);
		gem_close(fd, obj[0].handle);

		igt_info("[%d] Context execution: %.3f us\n", child,
			 elapsed(&start, &end) / (3 * count * all_nengine) * 1e6);
	}
	igt_waitchildren();

	gem_close(fd, obj[1].handle);

	for (unsigned long i = 0; i < count; i++)
		intel_ctx_destroy(fd, contexts[i]);
	free(contexts);
}

static void basic_ext_param(int i915)
{
	struct drm_i915_gem_context_create_ext_setparam ext = {
		{ .name = I915_CONTEXT_CREATE_EXT_SETPARAM },
	};
	struct drm_i915_gem_context_create_ext create = {
		.flags = I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS
	};
	struct drm_i915_gem_context_param get;

	igt_require(create_ext_ioctl(i915, &create) == 0);
	gem_context_destroy(i915, create.ctx_id);

	create.extensions = -1ull;
	igt_assert_eq(create_ext_ioctl(i915, &create), -EFAULT);

	create.extensions = to_user_pointer(&ext);
	igt_assert_eq(create_ext_ioctl(i915, &create), -EINVAL);

	ext.param.param = I915_CONTEXT_PARAM_PRIORITY;
	if (create_ext_ioctl(i915, &create) != -ENODEV) {
		gem_context_destroy(i915, create.ctx_id);

		ext.base.next_extension = -1ull;
		igt_assert_eq(create_ext_ioctl(i915, &create), -EFAULT);
		ext.base.next_extension = to_user_pointer(&ext);
		igt_assert_eq(create_ext_ioctl(i915, &create), -E2BIG);
		ext.base.next_extension = 0;

		ext.param.value = 32;
		igt_assert_eq(create_ext_ioctl(i915, &create), 0);

		memset(&get, 0, sizeof(get));
		get.ctx_id = create.ctx_id;
		get.param = I915_CONTEXT_PARAM_PRIORITY;
		gem_context_get_param(i915, &get);
		igt_assert_eq(get.value, ext.param.value);

		gem_context_destroy(i915, create.ctx_id);


		/* Having demonstrated a valid setup, check a few invalids */
		ext.param.ctx_id = 1;
		igt_assert_eq(create_ext_ioctl(i915, &create), -EINVAL);
		ext.param.ctx_id = create.ctx_id;
		igt_assert_eq(create_ext_ioctl(i915, &create), -EINVAL);
		ext.param.ctx_id = -1;
		igt_assert_eq(create_ext_ioctl(i915, &create), -EINVAL);
		ext.param.ctx_id = 0;
	}
}

static void check_single_timeline(int i915, uint32_t ctx, int num_engines)
{
#define RCS_TIMESTAMP (0x2000 + 0x358)
	const unsigned int gen = intel_gen(intel_get_drm_devid(i915));
	const int has_64bit_reloc = gen >= 8;
	struct drm_i915_gem_exec_object2 results = { .handle = gem_create(i915, 4096) };
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	int timeline = sw_sync_timeline_create();
	uint32_t last, *map;

	{
		struct drm_i915_gem_execbuffer2 execbuf = {
			.buffers_ptr = to_user_pointer(&results),
			.buffer_count = 1,
			.rsvd1 = ctx,
		};
		gem_write(i915, results.handle, 0, &bbe, sizeof(bbe));
		gem_execbuf(i915, &execbuf);
		results.flags = EXEC_OBJECT_PINNED;
	}

	for (int i = 0; i < num_engines; i++) {
		struct drm_i915_gem_exec_object2 obj[2] = {
			results, /* write hazard lies! */
			{ .handle = gem_create(i915, 4096) },
		};
		struct drm_i915_gem_execbuffer2 execbuf = {
			.buffers_ptr = to_user_pointer(obj),
			.buffer_count = 2,
			.rsvd1 = ctx,
			.rsvd2 = sw_sync_timeline_create_fence(timeline, num_engines - i),
			.flags = i | I915_EXEC_FENCE_IN,
		};
		uint64_t offset = results.offset + 4 * i;
		uint32_t *cs;
		int j = 0;

		cs = gem_mmap__cpu(i915, obj[1].handle, 0, 4096, PROT_WRITE);

		cs[j] = 0x24 << 23 | 1; /* SRM */
		if (has_64bit_reloc)
			cs[j]++;
		j++;
		cs[j++] = RCS_TIMESTAMP;
		cs[j++] = offset;
		if (has_64bit_reloc)
			cs[j++] = offset >> 32;
		cs[j++] = MI_BATCH_BUFFER_END;

		munmap(cs, 4096);

		gem_execbuf(i915, &execbuf);
		gem_close(i915, obj[1].handle);
		close(execbuf.rsvd2);
	}
	close(timeline);
	gem_sync(i915, results.handle);

	map = gem_mmap__cpu(i915, results.handle, 0, 4096, PROT_READ);
	gem_set_domain(i915, results.handle, I915_GEM_DOMAIN_CPU, 0);
	gem_close(i915, results.handle);

	last = map[0];
	for (int i = 1; i < num_engines; i++) {
		igt_assert_f((map[i] - last) > 0,
			     "Engine instance [%d] executed too early: this:%x, last:%x\n",
			     i, map[i], last);
		last = map[i];
	}
	munmap(map, 4096);
}

static void iris_pipeline(int i915)
{
#ifdef I915_DEFINE_CONTEXT_PARAM_ENGINES
#define RCS0 {0, 0}
	I915_DEFINE_CONTEXT_PARAM_ENGINES(engines, 2) = {
		.engines = { RCS0, RCS0 }
	};
	struct drm_i915_gem_context_create_ext_setparam p_engines = {
		.base = {
			.name = I915_CONTEXT_CREATE_EXT_SETPARAM,
			.next_extension = 0, /* end of chain */
		},
		.param = {
			.param = I915_CONTEXT_PARAM_ENGINES,
			.value = to_user_pointer(&engines),
			.size = sizeof(engines),
		},
	};
	struct drm_i915_gem_context_create_ext_setparam p_recover = {
		.base = {
			.name =I915_CONTEXT_CREATE_EXT_SETPARAM,
			.next_extension = to_user_pointer(&p_engines),
		},
		.param = {
			.param = I915_CONTEXT_PARAM_RECOVERABLE,
			.value = 0,
		},
	};
	struct drm_i915_gem_context_create_ext_setparam p_prio = {
		.base = {
			.name =I915_CONTEXT_CREATE_EXT_SETPARAM,
			.next_extension = to_user_pointer(&p_recover),
		},
		.param = {
			.param = I915_CONTEXT_PARAM_PRIORITY,
			.value = 768,
		},
	};
	struct drm_i915_gem_context_create_ext create = {
		.flags = (I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE |
			  I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS),
	};
	struct drm_i915_gem_context_param get;

	igt_require(create_ext_ioctl(i915, &create) == 0);

	create.extensions = to_user_pointer(&p_prio);
	igt_assert_eq(create_ext_ioctl(i915, &create), 0);

	memset(&get, 0, sizeof(get));
	get.ctx_id = create.ctx_id;
	get.param = I915_CONTEXT_PARAM_PRIORITY;
	gem_context_get_param(i915, &get);
	igt_assert_eq(get.value, p_prio.param.value);

	memset(&get, 0, sizeof(get));
	get.ctx_id = create.ctx_id;
	get.param = I915_CONTEXT_PARAM_RECOVERABLE;
	gem_context_get_param(i915, &get);
	igt_assert_eq(get.value, 0);

	check_single_timeline(i915, create.ctx_id, 2);

	gem_context_destroy(i915, create.ctx_id);
#endif /* I915_DEFINE_CONTEXT_PARAM_ENGINES */
}

igt_main
{
	const int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	struct drm_i915_gem_context_create create;
	const struct intel_execution_engine2 *e;
	intel_ctx_cfg_t cfg;
	int fd = -1;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		igt_require_gem(fd);
		gem_require_contexts(fd);

		cfg = intel_ctx_cfg_all_physical(fd);
		for_each_ctx_cfg_engine(fd, &cfg, e)
			all_engines[all_nengine++] = e->flags;
		igt_require(all_nengine);

		if (gem_uses_full_ppgtt(fd)) {
			ppgtt_nengine = all_nengine;
			memcpy(ppgtt_engines,
			       all_engines,
			       all_nengine * sizeof(all_engines[0]));
		} else
			ppgtt_engines[ppgtt_nengine++] = 0;

		igt_fork_hang_detector(fd);
	}

	igt_describe("Test random context creation");
	igt_subtest("basic") {
		memset(&create, 0, sizeof(create));
		create.ctx_id = rand();
		create.pad = 0;
		igt_assert_eq(create_ioctl(fd, &create), 0);
		igt_assert(create.ctx_id != 0);
		gem_context_destroy(fd, create.ctx_id);
	}

	igt_describe("Verify valid and invalid context extensions");
	igt_subtest("ext-param")
		basic_ext_param(fd);

	igt_describe("Set, validate and execute particular context params");
	igt_subtest("iris-pipeline")
		iris_pipeline(fd);

	igt_describe("Create contexts upto available RAM size, calculate the average "
		     "performance of their execution on multiple parallel processes");
	igt_subtest("maximum-mem")
		maximum(fd, &cfg, ncpus, CHECK_RAM);

	igt_describe("Create contexts upto available RAM+SWAP size, calculate the average "
		     "performance of their execution on multiple parallel processes");
	igt_subtest("maximum-swap")
		maximum(fd, &cfg, ncpus, CHECK_RAM | CHECK_SWAP);

	igt_describe("Exercise implicit per-fd context creation");
	igt_subtest("basic-files")
		files(fd, &cfg, 2, 1);

	igt_describe("Exercise implicit per-fd context creation on 1 CPU for long duration");
	igt_subtest("files")
		files(fd, &cfg, 20, 1);

	igt_describe("Exercise implicit per-fd context creation on all CPUs for long duration");
	igt_subtest("forked-files")
		files(fd, &cfg, 20, ncpus);

	/* NULL value means all engines */
	igt_describe("Calculate the average performance of context creation and "
		     "it's execution using all engines");
	igt_subtest("active-all")
		active(fd, &cfg, NULL, 20, 1);

	igt_describe("Calculate the average performance of context creation and it's execution "
		     "using all engines on multiple parallel processes");
	igt_subtest("forked-active-all")
		active(fd, &cfg, NULL, 20, ncpus);

	igt_describe("For each engine calculate the average performance of context creation "
		     "execution and exercise context reclaim");
	igt_subtest_with_dynamic("active") {
		for_each_ctx_cfg_engine(fd, &cfg, e) {
			igt_dynamic_f("%s", e->name)
				active(fd, &cfg, e, 20, 1);
		}
	}

	igt_describe("For each engine calculate the average performance of context creation "
		     "and execution on multiple parallel processes");
	igt_subtest_with_dynamic("forked-active") {
		for_each_ctx_cfg_engine(fd, &cfg, e) {
			igt_dynamic_f("%s", e->name)
				active(fd, &cfg, e, 20, ncpus);
		}
	}

	igt_describe("For each engine calculate the average performance of context creation "
		     "and execution while all other engines are hogging the resources");
	igt_subtest_with_dynamic("hog") {
		for_each_ctx_cfg_engine(fd, &cfg, e) {
			igt_dynamic_f("%s", e->name)
				active(fd, &cfg, e, 20, -1);
		}
	}

	igt_fixture {
		igt_stop_hang_detector();
		drm_close_driver(fd);
	}
}
