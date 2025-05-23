/*
 * Copyright © 2013 Intel Corporation
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
 *
 */

/**
 * TEST: kms addfb basic
 * Category: Display
 * Description: Sanity test for ioctls DRM_IOCTL_MODE_ADDFB2 & DRM_IOCTL_MODE_RMFB.
 * Driver requirement: i915, xe
 * Mega feature: General Display Features
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "drm.h"
#include "drm_fourcc.h"

#include "igt_rand.h"
#include "igt_device.h"
#include "i915/intel_memory_region.h"
#include "intel_common.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

/**
 * SUBTEST: unused-%s
 * Description: Test that addfb2 call fails correctly for unused %arg[1]
 *
 * SUBTEST: unused-modifier
 * Description: Test that addfb2 call fails correctly for unused modifier
 *
 * SUBTEST: clobberred-modifier
 * Description: Check if addfb2 call works for clobbered modifier
 * Driver requirement: i915
 *
 * SUBTEST: invalid-smem-bo-on-discrete
 * Description: Check if addfb2 with a system memory gem object fails correctly
 *              if device requires local memory framebuffers
 *
 * SUBTEST: legacy-format
 * Description: Check if addfb2 call works for legacy formats
 *
 * arg[1]:
 *
 * @handle:       Handle
 * @pitches:      Pitches
 * @offsets:      Offsets
 */

/**
 * SUBTEST: no-handle
 * Description: Test that addfb2 call fails correctly without handle
 *
 * SUBTEST: basic
 * Description: Check if addfb2 call works with given handle
 *
 * SUBTEST: bad-pitch-%i
 * Description: bad-pitch-%arg[1]: Test that addfb2 call fails correctly for bad-pitches
 *
 * arg[1].values: 0, 32, 63, 128, 256, 999, 1024, 65536
 */

/**
 * SUBTEST: basic-x-tiled-legacy
 * Description: Check if addfb2 and rmfb call works for basic x-tiling test
 *
 * SUBTEST: basic-y-tiled-legacy
 * Description: Check if addfb2 and rmfb call works for basic y-tiling test
 * Driver requirement: i915
 *
 * SUBTEST: framebuffer-vs-set-tiling
 * Description: Check if addfb2 call works for x and y tiling
 * Driver requirement: i915
 *
 * SUBTEST: tile-pitch-mismatch
 * Description: Test that addfb2 call fails correctly for pitches mismatch
 * Driver requirement: i915
 */

/**
 * SUBTEST: size-max
 * Description: Check if addfb2 call works with max size of buffer object
 *
 * SUBTEST: too-wide
 * Description: Test that addfb2 call fails correctly with increased width of fb
 *
 * SUBTEST: too-high
 * Description: Test that addfb2 call fails correctly with increased height of fb
 *
 * SUBTEST: bo-too-small
 * Description: Test that addfb2 call fails correctly with small size of buffer object
 *
 * SUBTEST: small-bo
 * Description: Check if addfb2 call works for given height
 *
 * SUBTEST: bo-too-small-due-to-tiling
 * Description: Test that addfb2 call fails correctly with small buffer object
 *              after changing tile
 */

/**
 * SUBTEST: addfb25-modifier-no-flag
 * Description: Test that addfb2 call fails correctly for x-tiling with given modifier
 *
 * SUBTEST: addfb25-bad-modifier
 * Description: Test that addfb2 call fails correctly for irrelevant modifier
 *
 * SUBTEST: addfb25-x-tiled-mismatch-legacy
 * Description: Test that addfb2 call fails correctly for irrelevant x-tiling
 * Driver requirement: i915
 *
 * SUBTEST: addfb25-x-tiled-legacy
 * Description: Check if addfb2 call works for x-tiling
 *
 * SUBTEST: addfb25-framebuffer-vs-set-tiling
 * Description: Check if addfb2 call works for relevant combination of tiling and fbs
 * Driver requirement: i915
 */

/**
 * SUBTEST: addfb25-y-tiled-%s
 * Description: Check if addfb2 call works for y-tiling %arg[1]
 *
 * SUBTEST: addfb25-yf-tiled-legacy
 * Description: Check if addfb2 call works for yf-tiling
 *
 * arg[1]:
 *
 * @legacy:               with default size
 * @small-legacy:         with given size & modifier
 */

/**
 * SUBTEST: addfb25-4-tiled
 * Description: Check if addfb2 call works for tiling-4
 *
 * SUBTEST: invalid-%s-%s
 * Description: Test that %arg[1] ioctl call fails correctly for %arg[2] object
 *
 * arg[1]:
 *
 * @get:           get-properties
 * @set:           set-properties
 *
 * arg[2]:
 *
 * @prop:          fb mode
 * @prop-any:      invalid
 */

/**
 * SUBTEST: master-rmfb
 * Description: Check that only master can rmfb
 */

static uint32_t gem_bo;
static uint32_t gem_bo_small;
static igt_display_t display;

static int legacy_addfb(int fd, struct drm_mode_fb_cmd *arg)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_MODE_ADDFB, arg))
		err = -errno;

	errno = 0;
	return err;
}

static int addfb_expected_ret(igt_display_t *disp, struct drm_mode_fb_cmd2 *f)
{
	return igt_display_has_format_mod(disp, f->pixel_format,
					  f->modifier[0]) ? 0 : -1;
}

static void invalid_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};

	f.width = 512;
	f.height = 512;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 512*4;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
		gem_bo_small = igt_create_bo_with_dimensions(fd, 1024, 1023,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo_small);

		f.handles[0] = gem_bo;

		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
	}

	f.flags = DRM_MODE_FB_MODIFIERS;

	igt_describe("Test that addfb2 call fails correctly for unused handle");
	igt_subtest("unused-handle") {
		igt_require_fb_modifiers(fd);

		f.handles[1] = gem_bo_small;
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		f.handles[1] = 0;
	}

	igt_describe("Test that addfb2 call fails correctly for unused pitches");
	igt_subtest("unused-pitches") {
		igt_require_fb_modifiers(fd);

		f.pitches[1] = 512;
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		f.pitches[1] = 0;
	}

	igt_describe("Test that addfb2 call fails correctly for unused offset");
	igt_subtest("unused-offsets") {
		igt_require_fb_modifiers(fd);

		f.offsets[1] = 512;
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		f.offsets[1] = 0;
	}

	igt_describe("Test that addfb2 call fails correctly for unused modifier");
	igt_subtest("unused-modifier") {
		igt_require_fb_modifiers(fd);

		f.modifier[1] =  I915_FORMAT_MOD_X_TILED;
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		f.modifier[1] = 0;
	}

	igt_describe("Check if addfb2 call works for clobbered modifier");
	igt_subtest("clobberred-modifier") {
		igt_require_i915(fd);
		igt_require(gem_available_fences(fd) > 0);
		f.flags = 0;
		f.modifier[0] = 0;
		gem_set_tiling(fd, gem_bo, I915_TILING_X, 512*4);
		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
		igt_assert_eq(f.modifier[0], 0);
	}

	igt_describe("Check if addfb2 with a system memory gem object "
		     "fails correctly if device requires local memory framebuffers");
	igt_subtest("invalid-smem-bo-on-discrete") {
		struct igt_fb fb;
		uint32_t handle;

		igt_require_intel(fd);
		igt_init_fb(&fb, fd, f.width, f.height,
			    DRM_FORMAT_XRGB8888, 0,
			    IGT_COLOR_YCBCR_BT709, IGT_COLOR_YCBCR_LIMITED_RANGE);
		igt_calc_fb_size(&fb);

		igt_require(is_intel_dgfx(fd));
		if (is_i915_device(fd))
			handle = gem_create_in_memory_regions(fd, fb.size, REGION_SMEM);
		else
			handle = xe_bo_create(fd, 0, fb.size, system_memory(fd), 0);

		f.handles[0] = handle;
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EREMOTE);
	}

	igt_describe("Check if addfb2 call works for legacy formats");
	igt_subtest("legacy-format") {
		struct {
			/* drm_mode_legacy_fb_format() */
			int bpp, depth;
			int expect;
		} known_formats[] = {
			{  8,  8 }, /* c8 (palette) */
			{ 16, 15 }, /* x1r5g5b5 */
			{ 16, 16 }, /* r5g6b5 or a1r5g5b5! */
			{ 24, 24 }, /* r8g8b8 */
			{ 32, 24 }, /* x8r8g8b8 */
			{ 32, 30 }, /* x2r10g10b10 */
			{ 32, 32 }, /* a8r8g8b8 or a2r10g10b10! */
		};
		struct drm_mode_fb_cmd arg = {
			.handle = f.handles[0],
			.width  = f.width,
			.height = f.height,
			.pitch  = f.pitches[0],
		};
		uint32_t prng = 0x12345678;
		unsigned long timeout = 1;
		unsigned long count = 0;

		/*
		 * First confirm the kernel recognises our known_formats;
		 * some may be invalid for different devices.
		 */
		for (int i = 0; i < ARRAY_SIZE(known_formats); i++) {
			arg.bpp = known_formats[i].bpp;
			arg.depth = known_formats[i].depth;
			known_formats[i].expect = legacy_addfb(fd, &arg);
			igt_debug("{bpp:%d, depth:%d} -> expect:%d\n",
				  arg.bpp, arg.depth, known_formats[i].expect);
			if (arg.fb_id) {
				do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &arg.fb_id);
				arg.fb_id = 0;
			}
		}

		igt_until_timeout(timeout) {
			int expect = -EINVAL;
			int err;

			arg.bpp = hars_petruska_f54_1_random(&prng);
			arg.depth = hars_petruska_f54_1_random(&prng);
			for (int start = 0, end = ARRAY_SIZE(known_formats);
			     start < end; ) {
				int mid = start + (end - start) / 2;
				typeof(*known_formats) *tbl = &known_formats[mid];

				if (arg.bpp < tbl->bpp) {
					end = mid;
				} else if (arg.bpp > tbl->bpp) {
					start = mid + 1;
				} else {
					if (arg.depth < tbl->depth) {
						end = mid;
					} else if (arg.depth > tbl->depth) {
						start = mid + 1;
					} else {
						expect = tbl->expect;
						break;
					}
				}
			}

			err = legacy_addfb(fd, &arg);
			igt_assert_f(err == expect,
				     "Expected %d with {bpp:%d, depth:%d}, got %d instead\n",
				     expect, arg.bpp, arg.depth, err);
			if (arg.fb_id) {
				do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &arg.fb_id);
				arg.fb_id = 0;
			}

			count++;

			/*
			 * to avoid exessive logging stop after 10k loops
			 */
			if (count >= 10000) {
				break;
			}
		}

		/* After all the abuse, confirm the known_formats */
		for (int i = 0; i < ARRAY_SIZE(known_formats); i++) {
			int err;

			arg.bpp = known_formats[i].bpp;
			arg.depth = known_formats[i].depth;

			err = legacy_addfb(fd, &arg);
			igt_assert_f(err == known_formats[i].expect,
				     "Expected %d with {bpp:%d, depth:%d}, got %d instead\n",
				     known_formats[i].expect,
				     arg.bpp, arg.depth,
				     err);
			if (arg.fb_id) {
				do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &arg.fb_id);
				arg.fb_id = 0;
			}
		}

		igt_info("Successfully fuzzed %lu {bpp, depth} variations\n",
			 count);
	}

	igt_fixture {
		gem_close(fd, gem_bo);
		gem_close(fd, gem_bo_small);
	}
}

static void pitch_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};
	int bad_pitches[] = { 0, 32, 63, 128, 256, 256*4, 999, 64*1024 };
	int i;

	f.width = 512;
	f.height = 512;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 1024*4;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
	}

	igt_describe("Test that addfb2 call fails correctly without handle");
	igt_subtest("no-handle") {
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
	}

	igt_describe("Check if addfb2 call works with given handle");
	f.handles[0] = gem_bo;
	igt_subtest("basic") {
		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
	}

	igt_describe("Test that addfb2 call fails correctly for bad-pitches");
	for (i = 0; i < ARRAY_SIZE(bad_pitches); i++) {
		igt_subtest_f("bad-pitch-%i", bad_pitches[i]) {
			f.pitches[0] = bad_pitches[i];
			igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f), -1);
			if (is_nouveau_device(fd) && bad_pitches[i] > 4 * 1024)
				igt_assert_eq(errno, ERANGE);
			else
				igt_assert_eq(errno, EINVAL);
			errno = 0;
		}
	}

	igt_fixture
		gem_close(fd, gem_bo);
}

static void tiling_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};
	uint32_t tiled_x_bo = 0;
	uint32_t tiled_y_bo = 0;

	f.width = 512;
	f.height = 512;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 1024*4;

	igt_subtest_group {
		igt_fixture {
			igt_require_intel(fd);
			tiled_x_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
				DRM_FORMAT_XRGB8888, I915_FORMAT_MOD_X_TILED,
				1024*4, NULL, NULL, NULL);
			igt_assert(tiled_x_bo);

			tiled_y_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
				DRM_FORMAT_XRGB8888, I915_FORMAT_MOD_Y_TILED,
				1024*4, NULL, NULL, NULL);
			igt_assert(tiled_y_bo);

			gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
				DRM_FORMAT_XRGB8888, 0, 0, NULL, NULL, NULL);
			igt_assert(gem_bo);
		}

		f.pitches[0] = 1024*4;
		igt_describe("Check if addfb2 and rmfb call works for basic x-tiling test");
		igt_subtest("basic-x-tiled-legacy") {
			if (is_i915_device(fd))
				igt_require(gem_available_fences(fd) > 0);
			f.handles[0] = tiled_x_bo;

			do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
			do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
			f.fb_id = 0;
		}

		igt_describe("Check if addfb2 call works for x and y tiling");
		igt_subtest("framebuffer-vs-set-tiling") {
			igt_require_i915(fd);
			igt_require(gem_available_fences(fd) > 0);
			f.handles[0] = gem_bo;

			gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4);
			do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
			igt_assert_eq(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 512*4), -EBUSY);
			igt_assert_eq(__gem_set_tiling(fd, gem_bo, I915_TILING_Y, 1024*4), -EBUSY);
			do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
			f.fb_id = 0;
		}

		igt_describe("Test that addfb2 call fails correctly for pitches mismatch");
			f.pitches[0] = 512*4;
		igt_subtest("tile-pitch-mismatch") {
			igt_require_i915(fd);
			igt_require(gem_available_fences(fd) > 0);

			f.handles[0] = tiled_x_bo;
			do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		}

		igt_describe("Test that addfb2 call fails correctly for basic y-tiling test");
		f.pitches[0] = 1024*4;
		igt_subtest("basic-y-tiled-legacy") {
			igt_require_i915(fd);
			igt_require(!gem_has_lmem(fd));
			igt_require(gem_available_fences(fd) > 0);

			f.handles[0] = tiled_y_bo;

			do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		}

		igt_fixture {
			gem_close(fd, tiled_x_bo);
			gem_close(fd, tiled_y_bo);
		}
	}
}

static void size_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};
	struct drm_mode_fb_cmd2 f_16 = {};
	struct drm_mode_fb_cmd2 f_8 = {};
	struct drm_mode_fb_cmd2 *framebuffers[] = {&f, &f_16, &f_8};
	int i;

	f.width = 1024;
	f.height = 1024;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 1024*4;

	f_16.width = 1024;
	f_16.height = 1024*2;
	f_16.pixel_format = DRM_FORMAT_RGB565;
	f_16.pitches[0] = 1024*2;

	f_8.width = 1024*2;
	f_8.height = 1024*2;
	f_8.pixel_format = DRM_FORMAT_C8;
	f_8.pitches[0] = 1024*2;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
		gem_bo_small = igt_create_bo_with_dimensions(fd, 1024, 1023,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo_small);
	}

	f.handles[0] = gem_bo;
	f_16.handles[0] = gem_bo;
	f_8.handles[0] = gem_bo;

	igt_describe("Check if addfb2 call works with max size of buffer object");
	igt_subtest("size-max") {
		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_16);
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f_16.fb_id);
		f_16.fb_id = 0;
		if (igt_display_has_format_mod(&display, DRM_FORMAT_C8, 0)) {
			do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f_8);
			do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f_8.fb_id);
			f_8.fb_id = 0;
		}
	}

	f.width++;
	f_16.width++;
	f_8.width++;
	igt_describe("Test that addfb2 call fails correctly with increased width of fb");
	igt_subtest("too-wide") {
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f_16, EINVAL);
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f_8, EINVAL);
	}
	f.width--;
	f_16.width--;
	f_8.width--;
	f.height++;
	f_16.height++;
	f_8.height++;
	igt_describe("Test that addfb2 call fails correctly with increased height of fb");
	igt_subtest("too-high") {
		for (i = 0; i < ARRAY_SIZE(framebuffers); i++) {
			igt_debug("Checking framebuffer %i\n", i);
			igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, framebuffers[i]), -1);
			if (is_nouveau_device(fd))
				igt_assert_eq(errno, ERANGE);
			else
				igt_assert_eq(errno, EINVAL);
			errno = 0;
		}
	}

	igt_describe("Test that addfb2 call fails correctly with small size of buffer object");
	f.handles[0] = gem_bo_small;
	igt_subtest("bo-too-small") {
		igt_assert_eq(igt_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f), -1);
		igt_assert(errno != 0);
		if (is_nouveau_device(fd))
			igt_assert_eq(errno, ERANGE);
		else
			igt_assert_eq(errno, EINVAL);
		errno = 0;
	}

	/* Just to check that the parameters would work. */
	igt_describe("Check if addfb2 call works for given height");
	f.height = 1020;
	igt_subtest("small-bo") {
		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
	}

	igt_describe("Test that addfb2 call fails correctly with small buffer object after changing tile");
	igt_subtest("bo-too-small-due-to-tiling") {
		igt_require_i915(fd);
		igt_require(gem_available_fences(fd) > 0);
		gem_set_tiling(fd, gem_bo_small, I915_TILING_X, 1024*4);
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
	}


	igt_fixture {
		gem_close(fd, gem_bo);
		gem_close(fd, gem_bo_small);
	}
}

static void addfb25_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);

		memset(&f, 0, sizeof(f));

		f.width = 1024;
		f.height = 1024;
		f.pixel_format = DRM_FORMAT_XRGB8888;
		f.pitches[0] = 1024*4;
		f.modifier[0] = DRM_FORMAT_MOD_LINEAR;

		f.handles[0] = gem_bo;
	}

	igt_describe("Test that addfb2 call fails correctly for x-tiling with given modifier");
	igt_subtest("addfb25-modifier-no-flag") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = I915_FORMAT_MOD_X_TILED;
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
	}

	igt_fixture
		f.flags = DRM_MODE_FB_MODIFIERS;

	igt_describe("Test that addfb2 call fails correctly for irrelevant modifier");
	igt_subtest("addfb25-bad-modifier") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = DRM_FORMAT_MOD_INVALID;
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
	}

	igt_subtest_group {
		igt_fixture {
			igt_require_intel(fd);
			if (is_i915_device(fd)) {
				igt_require(gem_available_fences(fd) > 0);
				gem_set_tiling(fd, gem_bo, I915_TILING_X, 1024*4);
			}
			igt_require_fb_modifiers(fd);
		}

		igt_describe("Test that addfb2 call fails correctly for irrelevant x-tiling");
		igt_subtest("addfb25-x-tiled-mismatch-legacy") {
			igt_require_i915(fd);
			f.modifier[0] = DRM_FORMAT_MOD_LINEAR;
			do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		}

		igt_describe("Check if addfb2 call works for x-tiling");
		igt_subtest("addfb25-x-tiled-legacy") {
			f.modifier[0] = I915_FORMAT_MOD_X_TILED;
			igt_assert_eq(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f),
				      addfb_expected_ret(&display, &f));
			if (!addfb_expected_ret(&display, &f))
				do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
			f.fb_id = 0;
		}

		igt_describe("Check if addfb2 call works for relevant combination of tiling and fbs");
		igt_subtest("addfb25-framebuffer-vs-set-tiling") {
			igt_require_i915(fd);
			f.modifier[0] = I915_FORMAT_MOD_X_TILED;
			do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
			igt_assert_eq(__gem_set_tiling(fd, gem_bo, I915_TILING_X, 512*4), -EBUSY);
			igt_assert_eq(__gem_set_tiling(fd, gem_bo, I915_TILING_Y, 1024*4), -EBUSY);
			do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
			f.fb_id = 0;
		}
	}

	igt_fixture
		gem_close(fd, gem_bo);
}

static void addfb25_ytile(int fd)
{
	struct drm_mode_fb_cmd2 f = {};

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);
		gem_bo_small = igt_create_bo_with_dimensions(fd, 1024, 1023,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo_small);

		memset(&f, 0, sizeof(f));

		f.width = 1024;
		f.height = 1024;
		f.pixel_format = DRM_FORMAT_XRGB8888;
		f.pitches[0] = 1024*4;
		f.flags = DRM_MODE_FB_MODIFIERS;
		f.modifier[0] = DRM_FORMAT_MOD_LINEAR;

		f.handles[0] = gem_bo;
	}

	igt_describe("Check if addfb2 call works for y-tiling");
	igt_subtest("addfb25-y-tiled-legacy") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = I915_FORMAT_MOD_Y_TILED;
		igt_assert_eq(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f),
			      addfb_expected_ret(&display, &f));
		if (!addfb_expected_ret(&display, &f))
			do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
	}

	igt_describe("Check if addfb2 call works for yf-tiling");
	igt_subtest("addfb25-yf-tiled-legacy") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = I915_FORMAT_MOD_Yf_TILED;
		igt_assert_eq(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f),
			      addfb_expected_ret(&display, &f));
		if (!addfb_expected_ret(&display, &f))
			do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
	}

	igt_describe("Test that addfb2 call fails correctly for y-tiling with given height and modifier");
	igt_subtest("addfb25-y-tiled-small-legacy") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = I915_FORMAT_MOD_Y_TILED;
		f.height = 1023;
		f.handles[0] = gem_bo_small;
		igt_require(addfb_expected_ret(&display, &f) == 0);
		do_ioctl_err(fd, DRM_IOCTL_MODE_ADDFB2, &f, EINVAL);
		f.fb_id = 0;
	}

	igt_fixture {
		gem_close(fd, gem_bo);
		gem_close(fd, gem_bo_small);
	}
}

static void addfb25_4tile(int fd)
{
	struct drm_mode_fb_cmd2 f = {};

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
				DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);

		memset(&f, 0, sizeof(f));

		f.width = 1024;
		f.height = 1024;
		f.pixel_format = DRM_FORMAT_XRGB8888;
		f.pitches[0] = 1024*4;
		f.flags = DRM_MODE_FB_MODIFIERS;
		f.modifier[0] = DRM_FORMAT_MOD_LINEAR;

		f.handles[0] = gem_bo;

	}

	igt_describe("Check if addfb2 call works for tiling-4");
	igt_subtest("addfb25-4-tiled") {
		igt_require_fb_modifiers(fd);

		f.modifier[0] = I915_FORMAT_MOD_4_TILED;
		igt_assert_eq(drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f),
			      addfb_expected_ret(&display, &f));
		if (!addfb_expected_ret(&display, &f))
			do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		f.fb_id = 0;
	}

	igt_fixture
		gem_close(fd, gem_bo);
}

static void prop_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};
	struct drm_mode_obj_get_properties get_props = {};
	struct drm_mode_obj_set_property set_prop = {};
	uint64_t prop, prop_val;

	f.width = 1024;
	f.height = 1024;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 1024*4;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);

		f.handles[0] = gem_bo;

		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
	}

	get_props.props_ptr = (uintptr_t) &prop;
	get_props.prop_values_ptr = (uintptr_t) &prop_val;
	get_props.count_props = 1;
	get_props.obj_id = f.fb_id;

	igt_describe("Test that get-properties ioctl call fails correctly for invalid object type");
	igt_subtest("invalid-get-prop-any") {
		get_props.obj_type = DRM_MODE_OBJECT_ANY;

		do_ioctl_err(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &get_props, EINVAL);
	}

	igt_describe("Test that get-properties ioctl call fails correctly for fb mode object");
	igt_subtest("invalid-get-prop") {
		get_props.obj_type = DRM_MODE_OBJECT_FB;

		do_ioctl_err(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &get_props, EINVAL);
	}

	set_prop.value = 0;
	set_prop.prop_id = 1;
	set_prop.obj_id = f.fb_id;

	igt_describe("Test that set-properties ioctl call fails correctly for invalid object type");
	igt_subtest("invalid-set-prop-any") {
		set_prop.obj_type = DRM_MODE_OBJECT_ANY;

		do_ioctl_err(fd, DRM_IOCTL_MODE_OBJ_SETPROPERTY, &set_prop, EINVAL);
	}

	igt_describe("Test that get-properties ioctl call fails correctly for fb mode object");
	igt_subtest("invalid-set-prop") {
		set_prop.obj_type = DRM_MODE_OBJECT_FB;

		do_ioctl_err(fd, DRM_IOCTL_MODE_OBJ_SETPROPERTY, &set_prop, EINVAL);
	}

	igt_fixture {
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		gem_close(fd, gem_bo);
	}
}

static void master_tests(int fd)
{
	struct drm_mode_fb_cmd2 f = {};

	f.width = 1024;
	f.height = 1024;
	f.pixel_format = DRM_FORMAT_XRGB8888;
	f.pitches[0] = 1024*4;

	igt_fixture {
		gem_bo = igt_create_bo_with_dimensions(fd, 1024, 1024,
			DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, 0, NULL, NULL, NULL);
		igt_assert(gem_bo);

		f.handles[0] = gem_bo;

		do_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
	}

	igt_describe("Check that only master can rmfb");
	igt_subtest("master-rmfb") {
		int master2_fd;

		igt_device_drop_master(fd);

		master2_fd = drm_open_driver_master(DRIVER_ANY);

		do_ioctl_err(master2_fd, DRM_IOCTL_MODE_RMFB, &f.fb_id, ENOENT);

		igt_device_drop_master(master2_fd);
		drm_close_driver(master2_fd);

		igt_device_set_master(fd);
	}

	igt_fixture {
		do_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id);
		gem_close(fd, gem_bo);
	}
}

static bool has_addfb2_iface(int fd)
{
	struct drm_mode_fb_cmd2 f = {};
	int err;

	err = 0;
	if (drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f))
		err = -errno;
	switch (err) {
	case -ENOTTY: /* ioctl unrecognised (kernel too old) */
	case -ENOTSUP: /* driver doesn't support KMS */
		return false;

		/*
		 * The only other valid response is -EINVAL, but we leave
		 * that for the actual tests themselves to discover for
		 * more accurate reporting.
		 */
	default:
		return true;
	}
}

int fd;

igt_main
{
	igt_fixture {
		fd = drm_open_driver_master(DRIVER_ANY);
		igt_require(has_addfb2_iface(fd));
	}

	igt_subtest_group
		invalid_tests(fd);

	igt_subtest_group
		pitch_tests(fd);

	igt_subtest_group
		prop_tests(fd);

	igt_subtest_group
		master_tests(fd);

	igt_subtest_group
		tiling_tests(fd);

	igt_subtest_group {
		igt_fixture
			igt_display_require(&display, fd);

		size_tests(fd);

		addfb25_tests(fd);

		igt_fixture
			igt_require_intel(fd);

		addfb25_ytile(fd);

		addfb25_4tile(fd);

		igt_fixture
			igt_display_fini(&display);
	}

	igt_fixture
		drm_close_driver(fd);
}
