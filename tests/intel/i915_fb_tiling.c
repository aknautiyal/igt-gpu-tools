/*
 * Copyright © 2018 Intel Corporation
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
 */

#include "igt.h"
/**
 * TEST: i915 fb tiling
 * Description: Object tiling must be fixed after framebuffer creation.
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: Memory management tests
 * Functionality: object tiling
 * Feature: mapping
 *
 * SUBTEST: basic-x-tiling
 */

IGT_TEST_DESCRIPTION("Object tiling must be fixed after framebuffer creation.");

igt_main
{
	int drm_fd;
	struct igt_fb fb;

	igt_fixture {
		drm_fd = drm_open_driver_master(DRIVER_INTEL);
		igt_require(gem_available_fences(drm_fd) > 0);
	}

	igt_subtest("basic-x-tiling") {
		int ret, ret2;

		igt_create_fb(drm_fd, 512, 512, DRM_FORMAT_XRGB8888,
			      I915_FORMAT_MOD_X_TILED, &fb);

		ret = __gem_set_tiling(drm_fd, fb.gem_handle, I915_TILING_X, fb.strides[0]);

		if (!ret)
			ret2 = __gem_set_tiling(drm_fd, fb.gem_handle, I915_TILING_NONE, fb.strides[0]);

		igt_remove_fb(drm_fd, &fb);

		igt_assert_eq(ret, 0);
		igt_assert_eq(ret2, -EBUSY);
	}

	igt_fixture
		drm_close_driver(drm_fd);
}
