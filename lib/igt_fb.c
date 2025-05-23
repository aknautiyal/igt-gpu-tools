/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright © 2013,2014 Intel Corporation
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
 * 	Daniel Vetter <daniel.vetter@ffwll.ch>
 * 	Damien Lespiau <damien.lespiau@intel.com>
 */

#include <stdio.h>
#include <math.h>
#include <wchar.h>
#include <inttypes.h>
#include <pixman.h>

#include "drmtest.h"
#include "i915/gem_create.h"
#include "i915/gem_mman.h"
#include "intel_blt.h"
#include "intel_mocs.h"
#include "intel_pat.h"
#include "igt_aux.h"
#include "igt_color_encoding.h"
#include "igt_fb.h"
#include "igt_halffloat.h"
#include "igt_kms.h"
#include "igt_matrix.h"
#include "igt_vc4.h"
#include "igt_amd.h"
#include "igt_x86.h"
#include "igt_nouveau.h"
#include "igt_syncobj.h"
#include "ioctl_wrappers.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "intel_bufops.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

/**
 * SECTION:igt_fb
 * @short_description: Framebuffer handling and drawing library
 * @title: Framebuffer
 * @include: igt.h
 *
 * This library contains helper functions for handling kms framebuffer objects
 * using #igt_fb structures to track all the metadata.  igt_create_fb() creates
 * a basic framebuffer and igt_remove_fb() cleans everything up again.
 *
 * It also supports drawing using the cairo library and provides some simplified
 * helper functions to easily draw test patterns. The main function to create a
 * cairo drawing context for a framebuffer object is igt_get_cairo_ctx().
 *
 * Finally it also pulls in the drm fourcc headers and provides some helper
 * functions to work with these pixel format codes.
 */

#define PIXMAN_invalid	0

#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 17, 2)
/*
 * We need cairo 1.17.2 to use HDR formats, but the only thing added is a value
 * to cairo_format_t.
 *
 * To prevent going outside the enum, make cairo_format_t an int and define
 * ourselves.
 */

#define CAIRO_FORMAT_RGB96F (6)
#define CAIRO_FORMAT_RGBA128F (7)
#define cairo_format_t int
#endif

#if PIXMAN_VERSION < PIXMAN_VERSION_ENCODE(0, 36, 0)
#define PIXMAN_FORMAT_BYTE(bpp,type,a,r,g,b) \
	(((bpp >> 3) << 24) |		     \
	(3 << 22) | ((type) << 16) |	     \
	((a >> 3) << 12) |		     \
	((r >> 3) << 8) |		     \
	((g >> 3) << 4) |		     \
	((b >> 3)))
#define PIXMAN_TYPE_RGBA_FLOAT 11
#define PIXMAN_rgba_float PIXMAN_FORMAT_BYTE(128, PIXMAN_TYPE_RGBA_FLOAT,32,32,32,32)
#endif

/* drm fourcc/cairo format maps */
static const struct format_desc_struct {
	const char *name;
	uint32_t drm_id;
	cairo_format_t cairo_id;
	pixman_format_code_t pixman_id;
	int depth;
	int num_planes;
	int plane_bpp[4];
	uint8_t hsub;
	uint8_t vsub;
	bool convert;
} format_desc[] = {
	{ .name = "ARGB1555", .depth = -1, .drm_id = DRM_FORMAT_ARGB1555,
	  .cairo_id = CAIRO_FORMAT_ARGB32, .convert = true,
	  .pixman_id = PIXMAN_a1r5g5b5,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "C8", .depth = -1, .drm_id = DRM_FORMAT_C8,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .pixman_id = PIXMAN_r3g3b2,
	  .num_planes = 1, .plane_bpp = { 8, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XRGB1555", .depth = -1, .drm_id = DRM_FORMAT_XRGB1555,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .pixman_id = PIXMAN_x1r5g5b5,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "RGB565", .depth = 16, .drm_id = DRM_FORMAT_RGB565,
	  .cairo_id = CAIRO_FORMAT_RGB16_565,
	  .pixman_id = PIXMAN_r5g6b5,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "BGR565", .depth = -1, .drm_id = DRM_FORMAT_BGR565,
	  .cairo_id = CAIRO_FORMAT_RGB16_565, .convert = true,
	  .pixman_id = PIXMAN_b5g6r5,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "BGR888", .depth = -1, .drm_id = DRM_FORMAT_BGR888,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .pixman_id = PIXMAN_b8g8r8,
	  .num_planes = 1, .plane_bpp = { 24, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "RGB888", .depth = -1, .drm_id = DRM_FORMAT_RGB888,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .pixman_id = PIXMAN_r8g8b8,
	  .num_planes = 1, .plane_bpp = { 24, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XYUV8888", .depth = -1, .drm_id = DRM_FORMAT_XYUV8888,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XRGB8888", .depth = 24, .drm_id = DRM_FORMAT_XRGB8888,
	  .cairo_id = CAIRO_FORMAT_RGB24,
	  .pixman_id = PIXMAN_x8r8g8b8,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XBGR8888", .depth = -1, .drm_id = DRM_FORMAT_XBGR8888,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .pixman_id = PIXMAN_x8b8g8r8,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XRGB2101010", .depth = 30, .drm_id = DRM_FORMAT_XRGB2101010,
	  .cairo_id = CAIRO_FORMAT_RGB30,
	  .pixman_id = PIXMAN_x2r10g10b10,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XBGR2101010", .depth = -1, .drm_id = DRM_FORMAT_XBGR2101010,
	  .cairo_id = CAIRO_FORMAT_RGB30, .convert = true,
	  .pixman_id = PIXMAN_x2b10g10r10,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "ARGB8888", .depth = 32, .drm_id = DRM_FORMAT_ARGB8888,
	  .cairo_id = CAIRO_FORMAT_ARGB32,
	  .pixman_id = PIXMAN_a8r8g8b8,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "ABGR8888", .depth = -1, .drm_id = DRM_FORMAT_ABGR8888,
	  .cairo_id = CAIRO_FORMAT_ARGB32, .convert = true,
	  .pixman_id = PIXMAN_a8b8g8r8,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "ARGB2101010", .depth = 30, .drm_id = DRM_FORMAT_ARGB2101010,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .pixman_id = PIXMAN_a2r10g10b10,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "ABGR2101010", .depth = -1, .drm_id = DRM_FORMAT_ABGR2101010,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .pixman_id = PIXMAN_a2b10g10r10,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XRGB16161616F", .depth = -1, .drm_id = DRM_FORMAT_XRGB16161616F,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "ARGB16161616F", .depth = -1, .drm_id = DRM_FORMAT_ARGB16161616F,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "XBGR16161616F", .depth = -1, .drm_id = DRM_FORMAT_XBGR16161616F,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "ABGR16161616F", .depth = -1, .drm_id = DRM_FORMAT_ABGR16161616F,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "XRGB16161616", .depth = -1, .drm_id = DRM_FORMAT_XRGB16161616,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "ARGB16161616", .depth = -1, .drm_id = DRM_FORMAT_ARGB16161616,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "XBGR16161616", .depth = -1, .drm_id = DRM_FORMAT_XBGR16161616,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "ABGR16161616", .depth = -1, .drm_id = DRM_FORMAT_ABGR16161616,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	},
	{ .name = "NV12", .depth = -1, .drm_id = DRM_FORMAT_NV12,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 2, .plane_bpp = { 8, 16, },
	  .hsub = 2, .vsub = 2,
	},
	{ .name = "NV16", .depth = -1, .drm_id = DRM_FORMAT_NV16,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 2, .plane_bpp = { 8, 16, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "NV21", .depth = -1, .drm_id = DRM_FORMAT_NV21,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 2, .plane_bpp = { 8, 16, },
	  .hsub = 2, .vsub = 2,
	},
	{ .name = "NV61", .depth = -1, .drm_id = DRM_FORMAT_NV61,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 2, .plane_bpp = { 8, 16, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "YUYV", .depth = -1, .drm_id = DRM_FORMAT_YUYV,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "YVYU", .depth = -1, .drm_id = DRM_FORMAT_YVYU,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "UYVY", .depth = -1, .drm_id = DRM_FORMAT_UYVY,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "VYUY", .depth = -1, .drm_id = DRM_FORMAT_VYUY,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 1, .plane_bpp = { 16, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "YU12", .depth = -1, .drm_id = DRM_FORMAT_YUV420,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 3, .plane_bpp = { 8, 8, 8, },
	  .hsub = 2, .vsub = 2,
	},
	{ .name = "YU16", .depth = -1, .drm_id = DRM_FORMAT_YUV422,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 3, .plane_bpp = { 8, 8, 8, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "YV12", .depth = -1, .drm_id = DRM_FORMAT_YVU420,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 3, .plane_bpp = { 8, 8, 8, },
	  .hsub = 2, .vsub = 2,
	},
	{ .name = "YV16", .depth = -1, .drm_id = DRM_FORMAT_YVU422,
	  .cairo_id = CAIRO_FORMAT_RGB24, .convert = true,
	  .num_planes = 3, .plane_bpp = { 8, 8, 8, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "Y410", .depth = -1, .drm_id = DRM_FORMAT_Y410,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "Y412", .depth = -1, .drm_id = DRM_FORMAT_Y412,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "Y416", .depth = -1, .drm_id = DRM_FORMAT_Y416,
	  .cairo_id = CAIRO_FORMAT_RGBA128F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XV30", .depth = -1, .drm_id = DRM_FORMAT_XVYU2101010,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XV36", .depth = -1, .drm_id = DRM_FORMAT_XVYU12_16161616,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "XV48", .depth = -1, .drm_id = DRM_FORMAT_XVYU16161616,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 64, },
	  .hsub = 1, .vsub = 1,
	},
	{ .name = "P010", .depth = -1, .drm_id = DRM_FORMAT_P010,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 2, .plane_bpp = { 16, 32 },
	  .vsub = 2, .hsub = 2,
	},
	{ .name = "P012", .depth = -1, .drm_id = DRM_FORMAT_P012,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 2, .plane_bpp = { 16, 32 },
	  .vsub = 2, .hsub = 2,
	},
	{ .name = "P016", .depth = -1, .drm_id = DRM_FORMAT_P016,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 2, .plane_bpp = { 16, 32 },
	  .vsub = 2, .hsub = 2,
	},
	{ .name = "Y210", .depth = -1, .drm_id = DRM_FORMAT_Y210,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "Y212", .depth = -1, .drm_id = DRM_FORMAT_Y212,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "Y216", .depth = -1, .drm_id = DRM_FORMAT_Y216,
	  .cairo_id = CAIRO_FORMAT_RGB96F, .convert = true,
	  .num_planes = 1, .plane_bpp = { 32, },
	  .hsub = 2, .vsub = 1,
	},
	{ .name = "IGT-FLOAT", .depth = -1, .drm_id = IGT_FORMAT_FLOAT,
	  .cairo_id = CAIRO_FORMAT_RGBA128F,
	  .pixman_id = PIXMAN_rgba_float,
	  .num_planes = 1, .plane_bpp = { 128 },
	},
};
#define for_each_format(f)	\
	for (f = format_desc; f - format_desc < ARRAY_SIZE(format_desc); f++)

static const struct format_desc_struct *lookup_drm_format(uint32_t drm_format)
{
	const struct format_desc_struct *format;

	for_each_format(format) {
		if (format->drm_id != drm_format)
			continue;

		return format;
	}

	return NULL;
}

static const struct format_desc_struct *lookup_drm_format_str(const char *name)
{
	const struct format_desc_struct *format;

	for_each_format(format) {
		if (!strcmp(format->name, name))
			return format;
	}

	return NULL;
}

/**
 * igt_format_is_yuv_semiplanar:
 * @format: drm fourcc pixel format code
 *
 * This function returns true if given format is yuv semiplanar.
 */
bool igt_format_is_yuv_semiplanar(uint32_t format)
{
	const struct format_desc_struct *f = lookup_drm_format(format);

	return igt_format_is_yuv(format) && f->num_planes == 2;
}

static bool is_yuv_semiplanar_plane(const struct igt_fb *fb, int color_plane)
{
	return igt_format_is_yuv_semiplanar(fb->drm_format) &&
	       color_plane == 1;
}

/**
 * igt_get_fb_tile_size:
 * @fd: the DRM file descriptor
 * @modifier: tiling layout of the framebuffer (as framebuffer modifier)
 * @fb_bpp: bits per pixel of the framebuffer
 * @width_ret: width of the tile in bytes
 * @height_ret: height of the tile in lines
 *
 * This function returns width and height of a tile based on the given tiling
 * format.
 */
void igt_get_fb_tile_size(int fd, uint64_t modifier, int fb_bpp,
			  unsigned *width_ret, unsigned *height_ret)
{
	uint32_t vc4_modifier_param = 0;

	if (is_vc4_device(fd)) {
		vc4_modifier_param = fourcc_mod_broadcom_param(modifier);
		modifier = fourcc_mod_broadcom_mod(modifier);
	}
	// For all non-linear modifiers, AMD uses 64 KiB tiles
	else if (IS_AMD_FMT_MOD(modifier)) {
		const int bytes_per_pixel = fb_bpp / 8;
		const int format_log2 = log2(bytes_per_pixel);
		const int pixel_log2 = log2(64 * 1024) - format_log2;
		const int width_log2 = (pixel_log2 + 1) / 2;
		const int height_log2 = pixel_log2 - width_log2;
		igt_require_amdgpu(fd);

		*width_ret = bytes_per_pixel << width_log2;
		*height_ret = 1 << height_log2;
		return;
	}

	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR :
		if (is_intel_device(fd))
			*width_ret = 64;
		else
			*width_ret = 1;

		*height_ret = 1;
		break;
	case I915_FORMAT_MOD_X_TILED:
		igt_require_intel(fd);
		if (intel_display_ver(intel_get_drm_devid(fd)) == 2) {
			*width_ret = 128;
			*height_ret = 16;
		} else {
			*width_ret = 512;
			*height_ret = 8;
		}
		break;
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_MTL_MC_CCS:
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
	case I915_FORMAT_MOD_4_TILED:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_BMG_CCS:
	case I915_FORMAT_MOD_4_TILED_LNL_CCS:
		igt_require_intel(fd);
		if (intel_display_ver(intel_get_drm_devid(fd)) == 2) {
			*width_ret = 128;
			*height_ret = 16;
		} else if (IS_915(intel_get_drm_devid(fd))) {
			*width_ret = 512;
			*height_ret = 8;
		} else {
			*width_ret = 128;
			*height_ret = 32;
		}
		break;
	case I915_FORMAT_MOD_Yf_TILED:
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		igt_require_intel(fd);
		switch (fb_bpp) {
		case 8:
			*width_ret = 64;
			*height_ret = 64;
			break;
		case 16:
		case 32:
			*width_ret = 128;
			*height_ret = 32;
			break;
		case 64:
		case 128:
			*width_ret = 256;
			*height_ret = 16;
			break;
		default:
			igt_assert(false);
		}
		break;
	case DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED:
		igt_require_vc4(fd);
		*width_ret = 128;
		*height_ret = 32;
		break;
	case DRM_FORMAT_MOD_BROADCOM_SAND32:
		igt_require_vc4(fd);
		*width_ret = 32;
		*height_ret = vc4_modifier_param;
		break;
	case DRM_FORMAT_MOD_BROADCOM_SAND64:
		igt_require_vc4(fd);
		*width_ret = 64;
		*height_ret = vc4_modifier_param;
		break;
	case DRM_FORMAT_MOD_BROADCOM_SAND128:
		igt_require_vc4(fd);
		*width_ret = 128;
		*height_ret = vc4_modifier_param;
		break;
	case DRM_FORMAT_MOD_BROADCOM_SAND256:
		igt_require_vc4(fd);
		*width_ret = 256;
		*height_ret = vc4_modifier_param;
		break;
	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0):
	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1):
	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2):
	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3):
	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4):
	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5):
		modifier = drm_fourcc_canonicalize_nvidia_format_mod(modifier);
		/* fallthrough */
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 0):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 1):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 2):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 3):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 4):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x7a, 5):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 0):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 1):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 2):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 3):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 4):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x78, 5):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 0):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 1):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 2):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 3):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 4):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 1, 0x70, 5):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 0):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 1):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 2):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 3):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 4):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 0, 0xfe, 5):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 0x06, 0):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 0x06, 1):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 0x06, 2):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 0x06, 3):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 0x06, 4):
	case DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(0, 1, 2, 0x06, 5):
		igt_require_nouveau(fd);
		*width_ret = 64;
		*height_ret = igt_nouveau_get_block_height(modifier);
		break;
	default:
		igt_assert(false);
	}
}

/**
 * igt_fb_is_gen12_mc_ccs_modifier:
 * @modifier: drm modifier
 *
 * This function returns true if @modifier supports media compression.
 */
bool igt_fb_is_gen12_mc_ccs_modifier(uint64_t modifier)
{
	return modifier == I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS ||
		modifier == I915_FORMAT_MOD_4_TILED_DG2_MC_CCS ||
		modifier == I915_FORMAT_MOD_4_TILED_MTL_MC_CCS;
}

/**
 * igt_fb_is_gen12_rc_ccs_cc_modifier:
 * @modifier: drm modifier
 *
 * This function returns true if @modifier supports clear color.
 */
bool igt_fb_is_gen12_rc_ccs_cc_modifier(uint64_t modifier)
{
	return modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC ||
		modifier == I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC ||
		modifier == I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC;
}

static bool is_gen12_ccs_modifier(uint64_t modifier)
{
	return igt_fb_is_gen12_mc_ccs_modifier(modifier) ||
		igt_fb_is_gen12_rc_ccs_cc_modifier(modifier) ||
		modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS ||
		modifier == I915_FORMAT_MOD_4_TILED_DG2_RC_CCS ||
		modifier == I915_FORMAT_MOD_4_TILED_MTL_RC_CCS;
}

/**
 * igt_fb_is_ccs_modifier:
 * @modifier: drm modifier
 *
 * This function returns true if @modifier supports compression.
 */
bool igt_fb_is_ccs_modifier(uint64_t modifier)
{
	return is_gen12_ccs_modifier(modifier) ||
		modifier == I915_FORMAT_MOD_Y_TILED_CCS ||
		modifier == I915_FORMAT_MOD_Yf_TILED_CCS;
}

static bool is_ccs_plane(const struct igt_fb *fb, int plane)
{
	if (!igt_fb_is_ccs_modifier(fb->modifier) ||
	    HAS_FLATCCS(intel_get_drm_devid(fb->fd)))
		return false;

	return plane >= fb->num_planes / 2;
}

bool igt_fb_is_ccs_plane(const struct igt_fb *fb, int plane)
{
	return is_ccs_plane(fb, plane);
}

static bool is_gen12_ccs_plane(const struct igt_fb *fb, int plane)
{
	return is_gen12_ccs_modifier(fb->modifier) && is_ccs_plane(fb, plane);
}

static bool is_gen12_ccs_cc_plane(const struct igt_fb *fb, int plane)
{
	if (plane == 2 &&
	    (fb->modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC ||
	     fb->modifier == I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC))
		return true;

	if (fb->modifier == I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC &&
	    plane == 1)
		return true;

	return false;
}

bool igt_fb_is_gen12_ccs_cc_plane(const struct igt_fb *fb, int plane)
{
	return is_gen12_ccs_cc_plane(fb, plane);
}

static int ccs_to_main_plane(const struct igt_fb *fb, int plane)
{
	if (is_gen12_ccs_cc_plane(fb, plane))
		return 0;

	return plane - fb->num_planes / 2;
}

int igt_fb_ccs_to_main_plane(const struct igt_fb *fb, int plane)
{
	return ccs_to_main_plane(fb, plane);
}

static unsigned fb_plane_width(const struct igt_fb *fb, int plane)
{
	const struct format_desc_struct *format = lookup_drm_format(fb->drm_format);

	if (is_gen12_ccs_cc_plane(fb, plane)) {
		return 64;
	} if (is_gen12_ccs_plane(fb, plane)) {
		int main_plane = ccs_to_main_plane(fb, plane);
		int width = fb->width;

		if (main_plane)
			width = DIV_ROUND_UP(width, format->hsub);

		return DIV_ROUND_UP(width,
				    512 / (fb->plane_bpp[main_plane] / 8)) * 64;
	} else if (is_ccs_plane(fb, plane)) {
		 return DIV_ROUND_UP(fb->width, 1024) * 128;
	}

	if (plane == 0)
		return fb->width;

	return DIV_ROUND_UP(fb->width, format->hsub);
}

static unsigned fb_plane_bpp(const struct igt_fb *fb, int plane)
{
	const struct format_desc_struct *format = lookup_drm_format(fb->drm_format);

	if (is_ccs_plane(fb, plane))
		return 8;
	else
		return format->plane_bpp[plane];
}

static unsigned fb_plane_height(const struct igt_fb *fb, int plane)
{
	const struct format_desc_struct *format = lookup_drm_format(fb->drm_format);

	if (is_gen12_ccs_cc_plane(fb, plane)) {
		return 1;
	} else if (is_gen12_ccs_plane(fb, plane)) {
		int height = fb->height;

		if (ccs_to_main_plane(fb, plane))
			height = DIV_ROUND_UP(height, format->vsub);

		return DIV_ROUND_UP(height, 32);
	} else if (is_ccs_plane(fb, plane)) {
		return DIV_ROUND_UP(fb->height, 512) * 32;
	}

	if (plane == 0)
		return fb->height;

	return DIV_ROUND_UP(fb->height, format->vsub);
}

static int fb_num_planes(const struct igt_fb *fb)
{
	int num_planes = lookup_drm_format(fb->drm_format)->num_planes;

	if (igt_fb_is_ccs_modifier(fb->modifier) &&
	    !HAS_FLATCCS(intel_get_drm_devid(fb->fd)))
		num_planes *= 2;

	if (igt_fb_is_gen12_rc_ccs_cc_modifier(fb->modifier))
		num_planes++;

	return num_planes;
}

void igt_init_fb(struct igt_fb *fb, int fd, int width, int height,
		 uint32_t drm_format, uint64_t modifier,
		 enum igt_color_encoding color_encoding,
		 enum igt_color_range color_range)
{
	const struct format_desc_struct *f = lookup_drm_format(drm_format);

	igt_assert_f(f, "DRM format %08x not found\n", drm_format);

	memset(fb, 0, sizeof(*fb));

	fb->width = width;
	fb->height = height;
	fb->modifier = modifier;
	fb->drm_format = drm_format;
	fb->fd = fd;
	fb->num_planes = fb_num_planes(fb);
	fb->color_encoding = color_encoding;
	fb->color_range = color_range;

	for (int i = 0; i < fb->num_planes; i++) {
		fb->plane_bpp[i] = fb_plane_bpp(fb, i);
		fb->plane_height[i] = fb_plane_height(fb, i);
		fb->plane_width[i] = fb_plane_width(fb, i);
	}
}

static uint32_t calc_plane_stride(struct igt_fb *fb, int plane)
{
	uint32_t min_stride = fb->plane_width[plane] *
		(fb->plane_bpp[plane] / 8);

	if (fb->modifier != DRM_FORMAT_MOD_LINEAR &&
	    is_intel_device(fb->fd) &&
	    intel_display_ver(intel_get_drm_devid(fb->fd)) <= 3) {
		uint32_t stride;

		/* Round the tiling up to the next power-of-two and the region
		 * up to the next pot fence size so that this works on all
		 * generations.
		 *
		 * This can still fail if the framebuffer is too large to be
		 * tiled. But then that failure is expected.
		 */

		stride = max(min_stride, 512u);
		stride = roundup_power_of_two(stride);

		return stride;
	} else if (igt_format_is_yuv(fb->drm_format) && is_amdgpu_device(fb->fd)) {
		/*
		 * Chroma address needs to be aligned to 256 bytes on AMDGPU
		 * so the easiest way is to align the luma stride to 256.
		 */
		return ALIGN(min_stride, 256);
	} else if (fb->modifier != DRM_FORMAT_MOD_LINEAR && is_amdgpu_device(fb->fd)) {
		/*
		 * For amdgpu device with tiling mode
		 */
		uint32_t tile_width, tile_height;

		igt_amd_fb_calculate_tile_dimension(fb->plane_bpp[plane],
				     &tile_width, &tile_height);
		tile_width *= (fb->plane_bpp[plane] / 8);

		return ALIGN(min_stride, tile_width);
	} else if (is_gen12_ccs_cc_plane(fb, plane)) {
		/* clear color always fixed to 64 bytes */
		return HAS_FLATCCS(intel_get_drm_devid(fb->fd)) ? 512 : 64;
	} else if (is_gen12_ccs_plane(fb, plane)) {
		/*
		 * The CCS surface stride is
		 *    ccs_stride = main_surface_stride_in_bytes / 512 * 64.
		 */
		return ALIGN(min_stride, 64);
	} else if (!fb->modifier && is_nouveau_device(fb->fd)) {
		int align;

		/* Volta supports 47-bit memory addresses, everything before only supports 40-bit */
		if (igt_nouveau_get_chipset(fb->fd) >= IGT_NOUVEAU_CHIPSET_GV100)
			align = 64;
		else
			align = 256;

		return ALIGN(min_stride, align);
	} else {
		unsigned int tile_width, tile_height;
		int tile_align = 1;

		igt_get_fb_tile_size(fb->fd, fb->modifier, fb->plane_bpp[plane],
				     &tile_width, &tile_height);

		if (is_gen12_ccs_modifier(fb->modifier))
			tile_align = 4;

		return ALIGN(min_stride, tile_width * tile_align);
	}
}

static uint64_t calc_plane_size(struct igt_fb *fb, int plane)
{
	if (fb->modifier != DRM_FORMAT_MOD_LINEAR &&
	    is_intel_device(fb->fd) &&
	    intel_display_ver(intel_get_drm_devid(fb->fd)) <= 3) {
		uint64_t size = (uint64_t) fb->strides[plane] *
			fb->plane_height[plane];
		uint64_t min_size = 1024 * 1024;

		/* Round the tiling up to the next power-of-two and the region
		 * up to the next pot fence size so that this works on all
		 * generations.
		 *
		 * This can still fail if the framebuffer is too large to be
		 * tiled. But then that failure is expected.
		 */

		return roundup_power_of_two(max(size, min_size));
	} else if (fb->modifier != DRM_FORMAT_MOD_LINEAR && is_amdgpu_device(fb->fd)) {
		/*
		 * For amdgpu device with tiling mode
		 */
		unsigned int tile_width, tile_height;

		igt_amd_fb_calculate_tile_dimension(fb->plane_bpp[plane],
				     &tile_width, &tile_height);
		tile_height *= (fb->plane_bpp[plane] / 8);

		return (uint64_t) fb->strides[plane] *
			ALIGN(fb->plane_height[plane], tile_height);
	} else if (is_gen12_ccs_plane(fb, plane)) {
		uint64_t size;

		/* The AUX CCS surface must be page aligned */
		size = ALIGN((uint64_t)fb->strides[plane] *
			     fb->plane_height[plane], 4096);

		return size;
	} else {
		unsigned int tile_width, tile_height;
		uint64_t size;

		igt_get_fb_tile_size(fb->fd, fb->modifier, fb->plane_bpp[plane],
				     &tile_width, &tile_height);

		size = (uint64_t)fb->strides[plane] *
			ALIGN(fb->plane_height[plane], tile_height);

		return size;
	}
}

static unsigned int gcd(unsigned int a, unsigned int b)
{
	while (b) {
		unsigned int m = a % b;

		a = b;
		b = m;
	}

	return a;
}

static unsigned int lcm(unsigned int a, unsigned int b)
{
	unsigned int g = gcd(a, b);

	if (g == 0 || b == 0)
		return 0;

	return a / g * b;
}

static unsigned int get_plane_alignment(struct igt_fb *fb, int color_plane)
{
	unsigned int tile_width, tile_height;
	unsigned int tile_row_size;
	unsigned int alignment;

	if (!(is_intel_device(fb->fd) &&
	      is_gen12_ccs_modifier(fb->modifier) &&
	      is_yuv_semiplanar_plane(fb, color_plane)))
		return 0;

	igt_get_fb_tile_size(fb->fd, fb->modifier, fb->plane_bpp[color_plane],
			     &tile_width, &tile_height);

	tile_row_size = fb->strides[color_plane] * tile_height;

	alignment = lcm(tile_row_size, 64 * 1024);

	if (is_yuv_semiplanar_plane(fb, color_plane) &&
	    fb->modifier == I915_FORMAT_MOD_4_TILED_MTL_MC_CCS &&
	    (alignment & ((1 << 20) - 1)))
		alignment = 1 << 20;

	return alignment;
}

/**
 * igt_calc_fb_size:
 * @fb: the framebuffer
 *
 * This function calculates the framebuffer size/strides/offsets/etc.
 * appropriately. The framebuffer needs to be sufficiently initialized
 * beforehand eg. with igt_init_fb().
 */
void igt_calc_fb_size(struct igt_fb *fb)
{
	uint64_t size = 0;
	int plane;

	for (plane = 0; plane < fb->num_planes; plane++) {
		unsigned int align;

		/* respect the stride requested by the caller */
		if (!fb->strides[plane])
			fb->strides[plane] = calc_plane_stride(fb, plane);

		align = get_plane_alignment(fb, plane);
		if (align)
			size += align - (size % align);

		fb->offsets[plane] = size;

		size += calc_plane_size(fb, plane);
	}

	/*
	 * We always need a clear color on TGL/DG1, make some extra
	 * room for one it if it's not explicit in the modifier.
	 *
	 * TODO: probably better to allocate this as part of the
	 * batch instead so the fb size doesn't need to change...
	 */
	if (fb->modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS)
		size = ALIGN(size + 64, 64);

	if (is_xe_device(fb->fd)) {
		size = ALIGN(size, xe_get_default_alignment(fb->fd));
		if (fb->modifier == I915_FORMAT_MOD_4_TILED_BMG_CCS)
			size = ALIGN(size, SZ_64K);
	}

	/* Respect the size requested by the caller. */
	if (fb->size == 0)
		fb->size = size;
}

/**
 * igt_fb_mod_to_tiling:
 * @modifier: DRM framebuffer modifier
 *
 * This function converts a DRM framebuffer modifier to its corresponding
 * tiling constant.
 *
 * Returns:
 * A tiling constant
 */
uint64_t igt_fb_mod_to_tiling(uint64_t modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR :
		return I915_TILING_NONE;
	case I915_FORMAT_MOD_X_TILED:
		return I915_TILING_X;
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
		return I915_TILING_Y;
	case I915_FORMAT_MOD_4_TILED:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_MTL_MC_CCS:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_BMG_CCS:
	case I915_FORMAT_MOD_4_TILED_LNL_CCS:
		return I915_TILING_4;
	case I915_FORMAT_MOD_Yf_TILED:
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		return I915_TILING_Yf;
	default:
		igt_assert(0);
	}
}

/**
 * igt_fb_tiling_to_mod:
 * @tiling: DRM framebuffer tiling
 *
 * This function converts a DRM framebuffer tiling to its corresponding
 * modifier constant.
 *
 * Returns:
 * A modifier constant
 */
uint64_t igt_fb_tiling_to_mod(uint64_t tiling)
{
	switch (tiling) {
	case I915_TILING_NONE:
		return DRM_FORMAT_MOD_LINEAR;
	case I915_TILING_X:
		return I915_FORMAT_MOD_X_TILED;
	case I915_TILING_Y:
		return I915_FORMAT_MOD_Y_TILED;
	case I915_TILING_4:
		return I915_FORMAT_MOD_4_TILED;
	case I915_TILING_Yf:
		return I915_FORMAT_MOD_Yf_TILED;
	default:
		igt_assert(0);
	}
}

static void memset64(uint64_t *s, uint64_t c, size_t n)
{
	for (int i = 0; i < n; i++)
		s[i] = c;
}

static void clear_yuv_buffer(struct igt_fb *fb)
{
	bool full_range = fb->color_range == IGT_COLOR_YCBCR_FULL_RANGE;
	int num_planes = lookup_drm_format(fb->drm_format)->num_planes;
	size_t plane_size[num_planes];
	void *ptr;

	igt_assert(igt_format_is_yuv(fb->drm_format));

	for (int i = 0; i < lookup_drm_format(fb->drm_format)->num_planes; i++) {
		unsigned int tile_width, tile_height;

		igt_assert_lt(i, num_planes);

		igt_get_fb_tile_size(fb->fd, fb->modifier, fb->plane_bpp[i],
				     &tile_width, &tile_height);
		plane_size[i] = fb->strides[i] *
			ALIGN(fb->plane_height[i], tile_height);
	}

	/* Ensure the framebuffer is preallocated */
	ptr = igt_fb_map_buffer(fb->fd, fb);
	igt_assert(*(uint32_t *)ptr == 0);

	switch (fb->drm_format) {
	case DRM_FORMAT_NV12:
		memset(ptr + fb->offsets[0],
		       full_range ? 0x00 : 0x10,
		       plane_size[0]);
		memset(ptr + fb->offsets[1],
		       0x80,
		       plane_size[1]);
		break;
	case DRM_FORMAT_XYUV8888:
		wmemset(ptr + fb->offsets[0], full_range ? 0x00008080 : 0x00108080,
			plane_size[0] / sizeof(wchar_t));
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		wmemset(ptr + fb->offsets[0],
			full_range ? 0x80008000 : 0x80108010,
			plane_size[0] / sizeof(wchar_t));
		break;
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		wmemset(ptr + fb->offsets[0],
			full_range ? 0x00800080 : 0x10801080,
			plane_size[0] / sizeof(wchar_t));
		break;
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		wmemset(ptr, full_range ? 0 : 0x10001000,
			plane_size[0] / sizeof(wchar_t));
		wmemset(ptr + fb->offsets[1], 0x80008000,
			plane_size[1] / sizeof(wchar_t));
		break;
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
		wmemset(ptr + fb->offsets[0],
			full_range ? 0x80000000 : 0x80001000,
			plane_size[0] / sizeof(wchar_t));
		break;

	case DRM_FORMAT_XVYU2101010:
	case DRM_FORMAT_Y410:
		wmemset(ptr + fb->offsets[0],
			full_range ? 0x20000200 : 0x20010200,
			plane_size[0] / sizeof(wchar_t));
		break;

	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
	case DRM_FORMAT_Y412:
	case DRM_FORMAT_Y416:
		memset64(ptr + fb->offsets[0],
			 full_range ? 0x800000008000ULL : 0x800010008000ULL,
			 plane_size[0] / sizeof(uint64_t));
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
		igt_assert(ARRAY_SIZE(plane_size) == 3);
		memset(ptr + fb->offsets[0],
		       full_range ? 0x00 : 0x10,
		       plane_size[0]);
		memset(ptr + fb->offsets[1],
		       0x80,
		       plane_size[1]);
		memset(ptr + fb->offsets[2],
		       0x80,
		       plane_size[2]);
		break;
	}

	igt_fb_unmap_buffer(fb, ptr);
}

/* helpers to create nice-looking framebuffers */
static int create_bo_for_fb(struct igt_fb *fb, bool prefer_sysmem)
{
	const struct format_desc_struct *fmt = lookup_drm_format(fb->drm_format);
	unsigned int bpp = 0;
	unsigned int plane;
	unsigned *strides = &fb->strides[0];
	bool device_bo = false;
	int fd = fb->fd;

	/*
	 * The current dumb buffer allocation API doesn't really allow to
	 * specify a custom size or stride. Yet the caller is free to specify
	 * them, so we need to make sure to use a device BO then.
	 */
	if (fb->modifier || fb->size || fb->strides[0] ||
	    (is_intel_device(fd) && igt_format_is_yuv(fb->drm_format)) ||
	    (is_intel_device(fd) && igt_format_is_fp16(fb->drm_format)) ||
	    (is_amdgpu_device(fd) && igt_format_is_yuv(fb->drm_format)) ||
	    is_nouveau_device(fd))
		device_bo = true;

	/* Sets offets and stride if necessary. */
	igt_calc_fb_size(fb);

	if (device_bo) {
		fb->is_dumb = false;

		if (is_i915_device(fd)) {
			int err;

			fb->gem_handle = gem_buffer_create_fb_obj(fd, fb->size);
			err = __gem_set_tiling(fd, fb->gem_handle,
					       igt_fb_mod_to_tiling(fb->modifier),
					       fb->strides[0]);
			/* If we can't use fences, we won't use ggtt detiling later. */
			igt_assert(err == 0 || err == -EOPNOTSUPP);
		} else if (is_xe_device(fd)) {
			fb->gem_handle = xe_bo_create(fd, 0, fb->size,
						      vram_if_possible(fd, 0),
						      DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM |
						      DRM_XE_GEM_CREATE_FLAG_SCANOUT);
		} else if (is_vc4_device(fd)) {
			fb->gem_handle = igt_vc4_create_bo(fd, fb->size);

			if (fb->modifier == DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED)
				igt_vc4_set_tiling(fd, fb->gem_handle,
						   fb->modifier);
		} else if (is_amdgpu_device(fd)) {
			fb->gem_handle = igt_amd_create_bo(fd, fb->size);
		} else if (is_nouveau_device(fd)) {
			fb->gem_handle = igt_nouveau_create_bo(fd, prefer_sysmem, fb);
		} else {
			igt_assert(false);
		}

		goto out;
	}

	for (plane = 0; plane < fb->num_planes; plane++)
		bpp += DIV_ROUND_UP(fb->plane_bpp[plane],
				    plane ? fmt->hsub * fmt->vsub : 1);

	fb->is_dumb = true;

	/*
	 * We can't really pass the stride array here since the dumb
	 * buffer allocation is assuming that it operates on one
	 * plane, and therefore will calculate the stride as if each
	 * pixel was stored on a single plane.
	 *
	 * This might cause issues at some point on drivers that would
	 * change the stride of YUV buffers, but we haven't
	 * encountered any yet.
	 */
	if (fb->num_planes > 1)
		strides = NULL;

	fb->gem_handle = kmstest_dumb_create(fd, fb->width, fb->height,
					     bpp, strides, &fb->size);

out:
	if (igt_format_is_yuv(fb->drm_format))
		clear_yuv_buffer(fb);

	return fb->gem_handle;
}

void igt_create_bo_for_fb(int fd, int width, int height,
			  uint32_t format, uint64_t modifier,
			  struct igt_fb *fb /* out */)
{
	igt_init_fb(fb, fd, width, height, format, modifier,
		    IGT_COLOR_YCBCR_BT709, IGT_COLOR_YCBCR_LIMITED_RANGE);
	create_bo_for_fb(fb, false);
}

/**
 * igt_create_bo_with_dimensions:
 * @fd: open drm file descriptor
 * @width: width of the buffer object in pixels
 * @height: height of the buffer object in pixels
 * @format: drm fourcc pixel format code
 * @modifier: modifier corresponding to the tiling layout of the buffer object
 * @stride: stride of the buffer object in bytes (0 for automatic stride)
 * @size_ret: size of the buffer object as created by the kernel
 * @stride_ret: stride of the buffer object as created by the kernel
 * @is_dumb: whether the created buffer object is a dumb buffer or not
 *
 * This function allocates a gem buffer object matching the requested
 * properties.
 *
 * Returns:
 * The kms id of the created buffer object.
 */
int igt_create_bo_with_dimensions(int fd, int width, int height,
				  uint32_t format, uint64_t modifier,
				  unsigned stride, uint64_t *size_ret,
				  unsigned *stride_ret, bool *is_dumb)
{
	struct igt_fb fb;

	igt_init_fb(&fb, fd, width, height, format, modifier,
		    IGT_COLOR_YCBCR_BT709, IGT_COLOR_YCBCR_LIMITED_RANGE);

	for (int i = 0; i < fb.num_planes; i++)
		fb.strides[i] = stride;

	create_bo_for_fb(&fb, false);

	if (size_ret)
		*size_ret = fb.size;
	if (stride_ret)
		*stride_ret = fb.strides[0];
	if (is_dumb)
		*is_dumb = fb.is_dumb;

	return fb.gem_handle;
}

#define get_u16_bit(x, n) 	((x & (1 << n)) >> n )
#define set_u16_bit(x, n, val)	((x & ~(1 << n)) | (val << n))
/*
 * update_crc16_dp:
 * @crc_old: old 16-bit CRC value to be updated
 * @d: input 16-bit data on which to calculate 16-bit CRC
 *
 * CRC algorithm implementation described in DP 1.4 spec Appendix J
 * the 16-bit CRC IBM is applied, with the following polynomial:
 *
 *       f(x) = x ^ 16 + x ^ 15 + x ^ 2 + 1
 *
 * the MSB is shifted in first, for any color format that is less than 16 bits
 * per component, the LSB is zero-padded.
 *
 * The following implementation is based on the hardware parallel 16-bit CRC
 * generation and ported to C code.
 *
 * Reference: VESA DisplayPort Standard v1.4, appendix J
 *
 * Returns:
 * updated 16-bit CRC value.
 */
static uint16_t update_crc16_dp(uint16_t crc_old, uint16_t d)
{
	uint16_t crc_new = 0;	/* 16-bit CRC output */

	/* internal use */
	uint16_t b = crc_old;
	uint8_t val;

	/* b[15] */
	val = get_u16_bit(b, 0) ^ get_u16_bit(b, 1) ^ get_u16_bit(b, 2) ^
	      get_u16_bit(b, 3) ^ get_u16_bit(b, 4) ^ get_u16_bit(b, 5) ^
	      get_u16_bit(b, 6) ^ get_u16_bit(b, 7) ^ get_u16_bit(b, 8) ^
	      get_u16_bit(b, 9) ^ get_u16_bit(b, 10) ^ get_u16_bit(b, 11) ^
	      get_u16_bit(b, 12) ^ get_u16_bit(b, 14) ^ get_u16_bit(b, 15) ^
	      get_u16_bit(d, 0) ^ get_u16_bit(d, 1) ^ get_u16_bit(d, 2) ^
	      get_u16_bit(d, 3) ^ get_u16_bit(d, 4) ^ get_u16_bit(d, 5) ^
	      get_u16_bit(d, 6) ^ get_u16_bit(d, 7) ^ get_u16_bit(d, 8) ^
	      get_u16_bit(d, 9) ^ get_u16_bit(d, 10) ^ get_u16_bit(d, 11) ^
	      get_u16_bit(d, 12) ^ get_u16_bit(d, 14) ^ get_u16_bit(d, 15);
	crc_new = set_u16_bit(crc_new, 15, val);

	/* b[14] */
	val = get_u16_bit(b, 12) ^ get_u16_bit(b, 13) ^
	      get_u16_bit(d, 12) ^ get_u16_bit(d, 13);
	crc_new = set_u16_bit(crc_new, 14, val);

	/* b[13] */
	val = get_u16_bit(b, 11) ^ get_u16_bit(b, 12) ^
	      get_u16_bit(d, 11) ^ get_u16_bit(d, 12);
	crc_new = set_u16_bit(crc_new, 13, val);

	/* b[12] */
	val = get_u16_bit(b, 10) ^ get_u16_bit(b, 11) ^
	      get_u16_bit(d, 10) ^ get_u16_bit(d, 11);
	crc_new = set_u16_bit(crc_new, 12, val);

	/* b[11] */
	val = get_u16_bit(b, 9) ^ get_u16_bit(b, 10) ^
	      get_u16_bit(d, 9) ^ get_u16_bit(d, 10);
	crc_new = set_u16_bit(crc_new, 11, val);

	/* b[10] */
	val = get_u16_bit(b, 8) ^ get_u16_bit(b, 9) ^
	      get_u16_bit(d, 8) ^ get_u16_bit(d, 9);
	crc_new = set_u16_bit(crc_new, 10, val);

	/* b[9] */
	val = get_u16_bit(b, 7) ^ get_u16_bit(b, 8) ^
	      get_u16_bit(d, 7) ^ get_u16_bit(d, 8);
	crc_new = set_u16_bit(crc_new, 9, val);

	/* b[8] */
	val = get_u16_bit(b, 6) ^ get_u16_bit(b, 7) ^
	      get_u16_bit(d, 6) ^ get_u16_bit(d, 7);
	crc_new = set_u16_bit(crc_new, 8, val);

	/* b[7] */
	val = get_u16_bit(b, 5) ^ get_u16_bit(b, 6) ^
	      get_u16_bit(d, 5) ^ get_u16_bit(d, 6);
	crc_new = set_u16_bit(crc_new, 7, val);

	/* b[6] */
	val = get_u16_bit(b, 4) ^ get_u16_bit(b, 5) ^
	      get_u16_bit(d, 4) ^ get_u16_bit(d, 5);
	crc_new = set_u16_bit(crc_new, 6, val);

	/* b[5] */
	val = get_u16_bit(b, 3) ^ get_u16_bit(b, 4) ^
	      get_u16_bit(d, 3) ^ get_u16_bit(d, 4);
	crc_new = set_u16_bit(crc_new, 5, val);

	/* b[4] */
	val = get_u16_bit(b, 2) ^ get_u16_bit(b, 3) ^
	      get_u16_bit(d, 2) ^ get_u16_bit(d, 3);
	crc_new = set_u16_bit(crc_new, 4, val);

	/* b[3] */
	val = get_u16_bit(b, 1) ^ get_u16_bit(b, 2) ^ get_u16_bit(b, 15) ^
	      get_u16_bit(d, 1) ^ get_u16_bit(d, 2) ^ get_u16_bit(d, 15);
	crc_new = set_u16_bit(crc_new, 3, val);

	/* b[2] */
	val = get_u16_bit(b, 0) ^ get_u16_bit(b, 1) ^ get_u16_bit(b, 14) ^
	      get_u16_bit(d, 0) ^ get_u16_bit(d, 1) ^ get_u16_bit(d, 14);
	crc_new = set_u16_bit(crc_new, 2, val);

	/* b[1] */
	val = get_u16_bit(b, 1) ^ get_u16_bit(b, 2) ^ get_u16_bit(b, 3) ^
	      get_u16_bit(b, 4) ^ get_u16_bit(b, 5) ^ get_u16_bit(b, 6) ^
	      get_u16_bit(b, 7) ^ get_u16_bit(b, 8) ^ get_u16_bit(b, 9) ^
	      get_u16_bit(b, 10) ^ get_u16_bit(b, 11) ^ get_u16_bit(b, 12) ^
	      get_u16_bit(b, 13) ^ get_u16_bit(b, 14) ^
	      get_u16_bit(d, 1) ^ get_u16_bit(d, 2) ^ get_u16_bit(d, 3) ^
	      get_u16_bit(d, 4) ^ get_u16_bit(d, 5) ^ get_u16_bit(d, 6) ^
	      get_u16_bit(d, 7) ^ get_u16_bit(d, 8) ^ get_u16_bit(d, 9) ^
	      get_u16_bit(d, 10) ^ get_u16_bit(d, 11) ^ get_u16_bit(d, 12) ^
	      get_u16_bit(d, 13) ^ get_u16_bit(d, 14);
	crc_new = set_u16_bit(crc_new, 1, val);

	/* b[0] */
	val = get_u16_bit(b, 0) ^ get_u16_bit(b, 1) ^ get_u16_bit(b, 2) ^
	      get_u16_bit(b, 3) ^ get_u16_bit(b, 4) ^ get_u16_bit(b, 5) ^
	      get_u16_bit(b, 6) ^ get_u16_bit(b, 7) ^ get_u16_bit(b, 8) ^
	      get_u16_bit(b, 9) ^ get_u16_bit(b, 10) ^ get_u16_bit(b, 11) ^
	      get_u16_bit(b, 12) ^ get_u16_bit(b, 13) ^ get_u16_bit(b, 15) ^
	      get_u16_bit(d, 0) ^ get_u16_bit(d, 1) ^ get_u16_bit(d, 2) ^
	      get_u16_bit(d, 3) ^ get_u16_bit(d, 4) ^ get_u16_bit(d, 5) ^
	      get_u16_bit(d, 6) ^ get_u16_bit(d, 7) ^ get_u16_bit(d, 8) ^
	      get_u16_bit(d, 9) ^ get_u16_bit(d, 10) ^ get_u16_bit(d, 11) ^
	      get_u16_bit(d, 12) ^ get_u16_bit(d, 13) ^ get_u16_bit(d, 15);
	crc_new = set_u16_bit(crc_new, 0, val);

	return crc_new;
}

/**
 * igt_fb_calc_crc:
 * @fb: pointer to an #igt_fb structure
 * @crc: pointer to an #igt_crc_t structure
 *
 * This function calculate the 16-bit frame CRC of RGB components over all
 * the active pixels.
 */
void igt_fb_calc_crc(struct igt_fb *fb, igt_crc_t *crc)
{
	int x, y, i;
	void *ptr;
	uint8_t *data;
	uint16_t din;

	igt_assert(fb && crc);

	ptr = igt_fb_map_buffer(fb->fd, fb);
	igt_assert(ptr);

	/* set for later CRC comparison */
	crc->has_valid_frame = true;
	crc->frame = 0;
	crc->n_words = 3;
	crc->crc[0] = 0;	/* R */
	crc->crc[1] = 0;	/* G */
	crc->crc[2] = 0;	/* B */

	data = ptr + fb->offsets[0];
	for (y = 0; y < fb->height; ++y) {
		for (x = 0; x < fb->width; ++x) {
			switch (fb->drm_format) {
			case DRM_FORMAT_XRGB8888:
				i = x * 4 + y * fb->strides[0];

				din = data[i + 2] << 8; /* padding-zeros */
				crc->crc[0] = update_crc16_dp(crc->crc[0], din);

				/* Green-component */
				din = data[i + 1] << 8;
				crc->crc[1] = update_crc16_dp(crc->crc[1], din);

				/* Blue-component */
				din = data[i] << 8;
				crc->crc[2] = update_crc16_dp(crc->crc[2], din);
				break;
			default:
				igt_assert_f(0, "DRM Format Invalid");
				break;
			}
		}
	}

	igt_fb_unmap_buffer(fb, ptr);
}

/**
 * igt_paint_color:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 *
 * This functions draws a solid rectangle with the given color using the drawing
 * context @cr.
 */
void igt_paint_color(cairo_t *cr, int x, int y, int w, int h,
		     double r, double g, double b)
{
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_fill(cr);
}

/**
 * igt_paint_color_rand:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 *
 * This functions draws a solid rectangle with random colors using the drawing
 * context @cr.
 */
void igt_paint_color_rand(cairo_t *cr, int x, int y, int w, int h)
{
	double r = rand() / (double)RAND_MAX;
	double g = rand() / (double)RAND_MAX;
	double b = rand() / (double)RAND_MAX;

	igt_paint_color(cr, x, y, w, h, r, g, b);
}

/**
 *
 * igt_fill_cts_color_square_framebuffer:
 * @pixmap: handle to mapped buffer
 * @video_width: required width for pattern
 * @video_height: required height for pattern
 * @bitdepth: required bitdepth fot pattern
 * @alpha: required alpha for the pattern
 *
 * This function draws a color square pattern for given width and height
 * as per the specifications mentioned in section 3.2.5.3 of DP CTS spec.
 */
int igt_fill_cts_color_square_framebuffer(uint32_t *pixmap,
		uint32_t video_width, uint32_t video_height,
		uint32_t bitdepth, int alpha)
{
	uint32_t pmax = 0;
	uint32_t pmin = 0;
	int tile_width = 64;
	int tile_height = 64;
	int reverse = 0;
	int i;
	uint32_t colors[8][3];
	uint32_t reverse_colors[8][3];

	switch (bitdepth) {
	case 8:
		pmax  = 235;
		pmin  = 16;
		break;
	case 10:
		pmax  = 940;
		pmin  = 64;
		break;
	}

	/*
	 * According to the requirement stated in the 3.2.5.3  DP CTS spec
	 * the required pattern for color square should look like below
	 *
	 *   white | yellow | cyan    | green | magenta | red    | blue  | black | white | ... | ..
	 *   -------------------------------------------------------------------------------
	 *   blue  | red    | magenta | green | cyan    | yellow | white | black | blue  | ... | ..
	 *   -------------------------------------------------------------------------------
	 *   white | yellow | cyan    | green | magenta | red    | blue  | black | white | ... | ..
	 *   -------------------------------------------------------------------------------
	 *   blue  | red    | magenta | green | cyan    | yellow | white | black | blue  | ... | ..
	 *   --------------------------------------------------------------------------------
	 *	  .    |   .      |	  .	|  .	|   .     |	.  |   .   |   .   |   .   |  .
	 *
	 *	  .    |   .      |	  .	|  .	|   .	  |	.  |   .   |   .   |   .   |  .
	 *
	 *
	 */

	for (i = 0; i < 8; i++) {
		if ((i % 8) == 0) {
			/* White Color */
			colors[i][0] = pmax;
			colors[i][1] = pmax;
			colors[i][2] = pmax;
			/* Blue Color */
			reverse_colors[i][0] = pmin;
			reverse_colors[i][1] = pmin;
			reverse_colors[i][2] = pmax;
		} else if ((i % 8) == 1) {
			/* Yellow Color */
			colors[i][0] = pmax;
			colors[i][1] = pmax;
			colors[i][2] = pmin;
			/* Red Color */
			reverse_colors[i][0] = pmax;
			reverse_colors[i][1] = pmin;
			reverse_colors[i][2] = pmin;
		} else if ((i % 8) == 2) {
			/* Cyan Color */
			colors[i][0] = pmin;
			colors[i][1] = pmax;
			colors[i][2] = pmax;
			/* Magenta Color */
			reverse_colors[i][0] = pmax;
			reverse_colors[i][1] = pmin;
			reverse_colors[i][2] = pmax;
		} else if ((i % 8) == 3) {
			/* Green Color */
			colors[i][0] = pmin;
			colors[i][1] = pmax;
			colors[i][2] = pmin;
			/* Green Color */
			reverse_colors[i][0] = pmin;
			reverse_colors[i][1] = pmax;
			reverse_colors[i][2] = pmin;
		} else if ((i % 8) == 4) {
			/* Magenta Color */
			colors[i][0] = pmax;
			colors[i][1] = pmin;
			colors[i][2] = pmax;
			/* Cyan Color */
			reverse_colors[i][0] = pmin;
			reverse_colors[i][1] = pmax;
			reverse_colors[i][2] = pmax;
		} else if ((i % 8) == 5) {
			/* Red Color */
			colors[i][0] = pmax;
			colors[i][1] = pmin;
			colors[i][2] = pmin;
			/* Yellow Color */
			reverse_colors[i][0] = pmax;
			reverse_colors[i][1] = pmax;
			reverse_colors[i][2] = pmin;
		} else if ((i % 8) == 6) {
			/* Blue Color */
			colors[i][0] = pmin;
			colors[i][1] = pmin;
			colors[i][2] = pmax;
			/* White Color */
			reverse_colors[i][0] = pmax;
			reverse_colors[i][1] = pmax;
			reverse_colors[i][2] = pmax;
		} else if ((i % 8) == 7) {
			/* Black Color */
			colors[i][0] = pmin;
			colors[i][1] = pmin;
			colors[i][2] = pmin;
			/* Black Color */
			reverse_colors[i][0] = pmin;
			reverse_colors[i][1] = pmin;
			reverse_colors[i][2] = pmin;
		}
	}

	for (uint32_t height = 0; height < video_height; height++) {
		uint32_t color = 0;
		uint8_t *temp = (uint8_t *)pixmap;
		uint8_t **buf = &temp;
		uint32_t (*clr_arr)[3];

		temp += (4 * video_width * height);

		for (uint32_t width = 0; width < video_width; width++) {

			if (reverse == 0)
				clr_arr = colors;
			else
				clr_arr = reverse_colors;

			/* using BGRA8888 format */
			*(*buf)++ = (((uint8_t)clr_arr[color][2]) & 0xFF);
			*(*buf)++ = (((uint8_t)clr_arr[color][1]) & 0xFF);
			*(*buf)++ = (((uint8_t)clr_arr[color][0]) & 0xFF);
			*(*buf)++ = ((uint8_t)alpha & 0xFF);

			if (((width + 1) % tile_width) == 0)
				color = (color + 1) % 8;

		}
		if (((height + 1) % tile_height) == 0) {
			if (reverse == 0)
				reverse = 1;
			else
				reverse = 0;
		}
	}
	return 0;
}
/**
 * igt_fill_cts_color_ramp_framebuffer:
 * @pixmap: handle to the mapped buffer
 * @video_width: required width for the CTS pattern
 * @video_height: required height for the CTS pattern
 * @bitdepth: required bitdepth for the CTS pattern
 * @alpha: required alpha for the CTS pattern
 * This functions draws the CTS test pattern for a given width, height.
 */
int igt_fill_cts_color_ramp_framebuffer(uint32_t *pixmap, uint32_t video_width,
		uint32_t video_height, uint32_t bitdepth, int alpha)
{
	uint32_t tile_height, tile_width;
	uint32_t *red_ptr, *green_ptr, *blue_ptr;
	uint32_t *white_ptr, *src_ptr, *dst_ptr;
	int x, y;
	int32_t pixel_val;

	tile_height = 64;
	tile_width = 1 << bitdepth;

	red_ptr = pixmap;
	green_ptr = red_ptr + (video_width * tile_height);
	blue_ptr = green_ptr + (video_width * tile_height);
	white_ptr = blue_ptr + (video_width * tile_height);
	x = 0;

	/* Fill the frame buffer with video pattern from CTS 3.1.5 */
	while (x < video_width) {
		for (pixel_val = 0; pixel_val < 256;
		     pixel_val = pixel_val + (256 / tile_width)) {
			red_ptr[x] = alpha << 24 | pixel_val << 16;
			green_ptr[x] = alpha << 24 | pixel_val << 8;
			blue_ptr[x] = alpha << 24 | pixel_val << 0;
			white_ptr[x] = alpha << 24 | red_ptr[x] | green_ptr[x] |
				       blue_ptr[x];
			if (++x >= video_width)
				break;
		}
	}
	for (y = 0; y < video_height; y++) {
		if (y == 0 || y == 64 || y == 128 || y == 192)
			continue;
		switch ((y / tile_height) % 4) {
		case 0:
			src_ptr = red_ptr;
			break;
		case 1:
			src_ptr = green_ptr;
			break;
		case 2:
			src_ptr = blue_ptr;
			break;
		case 3:
			src_ptr = white_ptr;
			break;
		}
		dst_ptr = pixmap + (y * video_width);
		memcpy(dst_ptr, src_ptr, (video_width * 4));
	}

	return 0;
}

/**
 * igt_paint_color_alpha:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 * @a: alpha value to use as fill color
 *
 * This functions draws a rectangle with the given color and alpha values using
 * the drawing context @cr.
 */
void igt_paint_color_alpha(cairo_t *cr, int x, int y, int w, int h,
			   double r, double g, double b, double a)
{
	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_fill(cr);
}

/**
 * igt_paint_color_gradient:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 *
 * This functions draws a gradient into the rectangle which fades in from black
 * to the given values using the drawing context @cr.
 */
void
igt_paint_color_gradient(cairo_t *cr, int x, int y, int w, int h,
			 int r, int g, int b)
{
	cairo_pattern_t *pat;

	pat = cairo_pattern_create_linear(x, y, x + w, y + h);
	cairo_pattern_add_color_stop_rgba(pat, 1, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(pat, 0, r, g, b, 1);

	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
}

/**
 * igt_paint_color_gradient_range:
 * @cr: cairo drawing context
 * @x: pixel x-coordination of the fill rectangle
 * @y: pixel y-coordination of the fill rectangle
 * @w: width of the fill rectangle
 * @h: height of the fill rectangle
 * @sr: red value to use as start gradient color
 * @sg: green value to use as start gradient color
 * @sb: blue value to use as start gradient color
 * @er: red value to use as end gradient color
 * @eg: green value to use as end gradient color
 * @eb: blue value to use as end gradient color
 *
 * This functions draws a gradient into the rectangle which fades in
 * from one color to the other using the drawing context @cr.
 */
void
igt_paint_color_gradient_range(cairo_t *cr, int x, int y, int w, int h,
			       double sr, double sg, double sb,
			       double er, double eg, double eb)
{
	cairo_pattern_t *pat;

	pat = cairo_pattern_create_linear(x, y, x + w, y + h);
	cairo_pattern_add_color_stop_rgba(pat, 1, sr, sg, sb, 1);
	cairo_pattern_add_color_stop_rgba(pat, 0, er, eg, eb, 1);

	cairo_rectangle(cr, x, y, w, h);
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);
}

static void
paint_test_patterns(cairo_t *cr, int width, int height)
{
	double gr_height, gr_width;
	int x, y;

	y = height * 0.10;
	gr_width = width * 0.75;
	gr_height = height * 0.08;
	x = (width / 2) - (gr_width / 2);

	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 0, 0);

	y += gr_height;
	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 1, 0);

	y += gr_height;
	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 0, 0, 1);

	y += gr_height;
	igt_paint_color_gradient(cr, x, y, gr_width, gr_height, 1, 1, 1);
}

/**
 * igt_cairo_printf_line:
 * @cr: cairo drawing context
 * @align: text alignment
 * @yspacing: additional y-direction feed after this line
 * @fmt: format string
 * @...: optional arguments used in the format string
 *
 * This is a little helper to draw text onto framebuffers. All the initial setup
 * (like setting the font size and the moving to the starting position) still
 * needs to be done manually with explicit cairo calls on @cr.
 *
 * Returns:
 * The width of the drawn text.
 */
int igt_cairo_printf_line(cairo_t *cr, enum igt_text_align align,
				double yspacing, const char *fmt, ...)
{
	double x, y, xofs, yofs;
	cairo_text_extents_t extents;
	char *text;
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vasprintf(&text, fmt, ap);
	igt_assert(ret >= 0);
	va_end(ap);

	cairo_text_extents(cr, text, &extents);

	xofs = yofs = 0;
	if (align & align_right)
		xofs = -extents.width;
	else if (align & align_hcenter)
		xofs = -extents.width / 2;

	if (align & align_top)
		yofs = extents.height;
	else if (align & align_vcenter)
		yofs = extents.height / 2;

	cairo_get_current_point(cr, &x, &y);
	if (xofs || yofs)
		cairo_rel_move_to(cr, xofs, yofs);

	cairo_text_path(cr, text);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	cairo_move_to(cr, x, y + extents.height + yspacing);

	free(text);

	return extents.width;
}

static void
paint_marker(cairo_t *cr, int x, int y)
{
	enum igt_text_align align;
	int xoff, yoff;

	cairo_move_to(cr, x, y - 20);
	cairo_line_to(cr, x, y + 20);
	cairo_move_to(cr, x - 20, y);
	cairo_line_to(cr, x + 20, y);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x, y, 10, 0, M_PI * 2);
	cairo_set_line_width(cr, 4);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke_preserve(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	xoff = x ? -20 : 20;
	align = x ? align_right : align_left;

	yoff = y ? -20 : 20;
	align |= y ? align_bottom : align_top;

	cairo_move_to(cr, x + xoff, y + yoff);
	cairo_set_font_size(cr, 18);
	igt_cairo_printf_line(cr, align, 0, "(%d, %d)", x, y);
}

/**
 * igt_paint_test_pattern:
 * @cr: cairo drawing context
 * @width: width of the visible area
 * @height: height of the visible area
 *
 * This functions draws an entire set of test patterns for the given visible
 * area using the drawing context @cr. This is useful for manual visual
 * inspection of displayed framebuffers.
 *
 * The test patterns include
 *  - corner markers to check for over/underscan and
 *  - a set of color and b/w gradients.
 */
void igt_paint_test_pattern(cairo_t *cr, int width, int height)
{
	paint_test_patterns(cr, width, height);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	/* Paint corner markers */
	paint_marker(cr, 0, 0);
	paint_marker(cr, width, 0);
	paint_marker(cr, 0, height);
	paint_marker(cr, width, height);

	igt_assert(!cairo_status(cr));
}

static cairo_status_t
stdio_read_func(void *closure, unsigned char* data, unsigned int size)
{
	if (fread(data, 1, size, (FILE*)closure) != size)
		return CAIRO_STATUS_READ_ERROR;

	return CAIRO_STATUS_SUCCESS;
}

cairo_surface_t *igt_cairo_image_surface_create_from_png(const char *filename)
{
	cairo_surface_t *image;
	FILE *f;

	f = igt_fopen_data(filename);
	image = cairo_image_surface_create_from_png_stream(&stdio_read_func, f);
	fclose(f);

	return image;
}

/**
 * igt_paint_image:
 * @cr: cairo drawing context
 * @filename: filename of the png image to draw
 * @dst_x: pixel x-coordination of the destination rectangle
 * @dst_y: pixel y-coordination of the destination rectangle
 * @dst_width: width of the destination rectangle
 * @dst_height: height of the destination rectangle
 *
 * This function can be used to draw a scaled version of the supplied png image,
 * which is loaded from the package data directory.
 */
void igt_paint_image(cairo_t *cr, const char *filename,
		     int dst_x, int dst_y, int dst_width, int dst_height)
{
	cairo_surface_t *image;
	int img_width, img_height;
	double scale_x, scale_y;

	image = igt_cairo_image_surface_create_from_png(filename);
	igt_assert(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);

	img_width = cairo_image_surface_get_width(image);
	img_height = cairo_image_surface_get_height(image);

	scale_x = (double)dst_width / img_width;
	scale_y = (double)dst_height / img_height;

	cairo_save(cr);

	cairo_translate(cr, dst_x, dst_y);
	cairo_scale(cr, scale_x, scale_y);
	cairo_set_source_surface(cr, image, 0, 0);
	cairo_paint(cr);

	cairo_surface_destroy(image);

	cairo_restore(cr);
}

/**
 * igt_create_fb_with_bo_size:
 * @fd: open i915 drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @modifier: tiling layout of the framebuffer (as framebuffer modifier)
 * @color_encoding: color encoding for YCbCr formats (ignored otherwise)
 * @color_range: color range for YCbCr formats (ignored otherwise)
 * @fb: pointer to an #igt_fb structure
 * @bo_size: size of the backing bo (0 for automatic size)
 * @bo_stride: stride of the backing bo (0 for automatic stride)
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object of the requested size. All metadata is stored in @fb.
 *
 * The backing storage of the framebuffer is filled with all zeros, i.e. black
 * for rgb pixel formats.
 *
 * Returns:
 * The kms id of the created framebuffer.
 */
unsigned int
igt_create_fb_with_bo_size(int fd, int width, int height,
			   uint32_t format, uint64_t modifier,
			   enum igt_color_encoding color_encoding,
			   enum igt_color_range color_range,
			   struct igt_fb *fb, uint64_t bo_size,
			   unsigned bo_stride)
{
	uint32_t flags = 0;

	igt_init_fb(fb, fd, width, height, format, modifier,
		    color_encoding, color_range);

	for (int i = 0; i < fb->num_planes; i++)
		fb->strides[i] = bo_stride;

	fb->size = bo_size;

	igt_debug("%s(width=%d, height=%d, format=" IGT_FORMAT_FMT
		  ", modifier=0x%"PRIx64", size=%"PRIu64")\n",
		  __func__, width, height, IGT_FORMAT_ARGS(format), modifier,
		  bo_size);

	create_bo_for_fb(fb, false);
	igt_assert(fb->gem_handle > 0);

	igt_debug("%s(handle=%d, pitch=%d)\n",
		  __func__, fb->gem_handle, fb->strides[0]);

	if (fb->modifier || igt_has_fb_modifiers(fd))
		flags = DRM_MODE_FB_MODIFIERS;

	do_or_die(__kms_addfb(fb->fd, fb->gem_handle,
			      fb->width, fb->height,
			      fb->drm_format, fb->modifier,
			      fb->strides, fb->offsets, fb->num_planes, flags,
			      &fb->fb_id));

	return fb->fb_id;
}

/**
 * igt_create_fb:
 * @fd: open drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @modifier: tiling layout of the framebuffer
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * The backing storage of the framebuffer is filled with all zeros, i.e. black
 * for rgb pixel formats.
 *
 * Returns:
 * The kms id of the created framebuffer.
 */
unsigned int igt_create_fb(int fd, int width, int height, uint32_t format,
			   uint64_t modifier, struct igt_fb *fb)
{
	return igt_create_fb_with_bo_size(fd, width, height, format, modifier,
					  IGT_COLOR_YCBCR_BT709,
					  IGT_COLOR_YCBCR_LIMITED_RANGE,
					  fb, 0, 0);
}

/**
 * igt_create_color_fb:
 * @fd: open drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @modifier: tiling layout of the framebuffer
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * Compared to igt_create_fb() this function also fills the entire framebuffer
 * with the given color, which is useful for some simple pipe crc based tests.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_color_fb(int fd, int width, int height,
				 uint32_t format, uint64_t modifier,
				 double r, double g, double b,
				 struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(fd, width, height, format, modifier, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_color(cr, 0, 0, width, height, r, g, b);
	igt_put_cairo_ctx(cr);

	return fb_id;
}

/**
 * igt_create_pattern_fb:
 * @fd: open drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @modifier: tiling layout of the framebuffer
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * Compared to igt_create_fb() this function also draws the standard test pattern
 * into the framebuffer.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_pattern_fb(int fd, int width, int height,
				   uint32_t format, uint64_t modifier,
				   struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(fd, width, height, format, modifier, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_test_pattern(cr, width, height);
	igt_put_cairo_ctx(cr);

	return fb_id;
}

/**
 * igt_create_color_pattern_fb:
 * @fd: open drm file descriptor
 * @width: width of the framebuffer in pixel
 * @height: height of the framebuffer in pixel
 * @format: drm fourcc pixel format code
 * @modifier: tiling layout of the framebuffer
 * @r: red value to use as fill color
 * @g: green value to use as fill color
 * @b: blue value to use as fill color
 * @fb: pointer to an #igt_fb structure
 *
 * This function allocates a gem buffer object suitable to back a framebuffer
 * with the requested properties and then wraps it up in a drm framebuffer
 * object. All metadata is stored in @fb.
 *
 * Compared to igt_create_fb() this function also fills the entire framebuffer
 * with the given color, and then draws the standard test pattern into the
 * framebuffer.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_color_pattern_fb(int fd, int width, int height,
					 uint32_t format, uint64_t modifier,
					 double r, double g, double b,
					 struct igt_fb *fb /* out */)
{
	unsigned int fb_id;
	cairo_t *cr;

	fb_id = igt_create_fb(fd, width, height, format, modifier, fb);
	igt_assert(fb_id);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_color(cr, 0, 0, width, height, r, g, b);
	igt_paint_test_pattern(cr, width, height);
	igt_put_cairo_ctx(cr);

	return fb_id;
}

/**
 * igt_create_image_fb:
 * @drm_fd: open drm file descriptor
 * @width: width of the framebuffer in pixel or 0
 * @height: height of the framebuffer in pixel or 0
 * @format: drm fourcc pixel format code
 * @modifier: tiling layout of the framebuffer
 * @filename: filename of the png image to draw
 * @fb: pointer to an #igt_fb structure
 *
 * Create a framebuffer with the specified image. If @width is zero the
 * image width will be used. If @height is zero the image height will be used.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_image_fb(int fd, int width, int height,
				 uint32_t format, uint64_t modifier,
				 const char *filename,
				 struct igt_fb *fb /* out */)
{
	cairo_surface_t *image;
	uint32_t fb_id;
	cairo_t *cr;

	image = igt_cairo_image_surface_create_from_png(filename);
	igt_assert(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);
	if (width == 0)
		width = cairo_image_surface_get_width(image);
	if (height == 0)
		height = cairo_image_surface_get_height(image);
	cairo_surface_destroy(image);

	fb_id = igt_create_fb(fd, width, height, format, modifier, fb);

	cr = igt_get_cairo_ctx(fd, fb);
	igt_paint_image(cr, filename, 0, 0, width, height);
	igt_put_cairo_ctx(cr);

	return fb_id;
}

struct box {
	int x, y, width, height;
};

struct stereo_fb_layout {
	int fb_width, fb_height;
	struct box left, right;
};

static void box_init(struct box *box, int x, int y, int bwidth, int bheight)
{
	box->x = x;
	box->y = y;
	box->width = bwidth;
	box->height = bheight;
}


static void stereo_fb_layout_from_mode(struct stereo_fb_layout *layout,
				       drmModeModeInfo *mode)
{
	unsigned int format = mode->flags & DRM_MODE_FLAG_3D_MASK;
	const int hdisplay = mode->hdisplay, vdisplay = mode->vdisplay;
	int middle;

	switch (format) {
	case DRM_MODE_FLAG_3D_TOP_AND_BOTTOM:
		layout->fb_width = hdisplay;
		layout->fb_height = vdisplay;

		middle = vdisplay / 2;
		box_init(&layout->left, 0, 0, hdisplay, middle);
		box_init(&layout->right,
			 0, middle, hdisplay, vdisplay - middle);
		break;
	case DRM_MODE_FLAG_3D_SIDE_BY_SIDE_HALF:
		layout->fb_width = hdisplay;
		layout->fb_height = vdisplay;

		middle = hdisplay / 2;
		box_init(&layout->left, 0, 0, middle, vdisplay);
		box_init(&layout->right,
			 middle, 0, hdisplay - middle, vdisplay);
		break;
	case DRM_MODE_FLAG_3D_FRAME_PACKING:
	{
		int vactive_space = mode->vtotal - vdisplay;

		layout->fb_width = hdisplay;
		layout->fb_height = 2 * vdisplay + vactive_space;

		box_init(&layout->left,
			 0, 0, hdisplay, vdisplay);
		box_init(&layout->right,
			 0, vdisplay + vactive_space, hdisplay, vdisplay);
		break;
	}
	default:
		igt_assert(0);
	}
}

/**
 * igt_create_stereo_fb:
 * @drm_fd: open i915 drm file descriptor
 * @mode: A stereo 3D mode.
 * @format: drm fourcc pixel format code
 * @modifier: tiling layout of the framebuffer
 *
 * Create a framebuffer for use with the stereo 3D mode specified by @mode.
 *
 * Returns:
 * The kms id of the created framebuffer on success or a negative error code on
 * failure.
 */
unsigned int igt_create_stereo_fb(int drm_fd, drmModeModeInfo *mode,
				  uint32_t format, uint64_t modifier)
{
	struct stereo_fb_layout layout;
	cairo_t *cr;
	uint32_t fb_id;
	struct igt_fb fb;

	stereo_fb_layout_from_mode(&layout, mode);
	fb_id = igt_create_fb(drm_fd, layout.fb_width, layout.fb_height, format,
			      modifier, &fb);
	cr = igt_get_cairo_ctx(drm_fd, &fb);

	igt_paint_image(cr, "1080p-left.png",
			layout.left.x, layout.left.y,
			layout.left.width, layout.left.height);
	igt_paint_image(cr, "1080p-right.png",
			layout.right.x, layout.right.y,
			layout.right.width, layout.right.height);

	igt_put_cairo_ctx(cr);

	return fb_id;
}

static pixman_format_code_t drm_format_to_pixman(uint32_t drm_format)
{
	const struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->pixman_id;

	igt_assert_f(0, "can't find a pixman format for %08x (%s)\n",
		     drm_format, igt_format_str(drm_format));
}

static cairo_format_t drm_format_to_cairo(uint32_t drm_format)
{
	const struct format_desc_struct *f;

	for_each_format(f)
		if (f->drm_id == drm_format)
			return f->cairo_id;

	igt_assert_f(0, "can't find a cairo format for %08x (%s)\n",
		     drm_format, igt_format_str(drm_format));
}

static uint32_t cairo_format_to_drm_format(cairo_format_t cairo_format)
{
	const struct format_desc_struct *f;

	if (cairo_format == CAIRO_FORMAT_RGB96F)
		cairo_format = CAIRO_FORMAT_RGBA128F;

	for_each_format(f)
		if (f->cairo_id == cairo_format && !f->convert)
			return f->drm_id;

	igt_assert_f(0, "can't find a drm format for cairo format %u\n",
		     cairo_format);
}

struct fb_blit_linear {
	struct igt_fb fb;
	uint8_t *map;
};

struct fb_blit_upload {
	int fd;
	struct igt_fb *fb;
	struct fb_blit_linear linear;
	struct buf_ops *bops;
	struct intel_bb *ibb;
};

static enum blt_tiling_type fb_tile_to_blt_tile(uint64_t tile)
{
	switch (igt_fb_mod_to_tiling(tile)) {
	case I915_TILING_NONE:
		return T_LINEAR;
	case I915_TILING_X:
		return T_XMAJOR;
	case I915_TILING_Y:
		return T_YMAJOR;
	case I915_TILING_4:
		return T_TILE4;
	case I915_TILING_Yf:
		return T_YFMAJOR;
	default:
		igt_assert_f(0, "Unknown tiling!\n");
	}
}

static bool fast_blit_ok(const struct igt_fb *fb)
{
	return blt_has_fast_copy(fb->fd) &&
		!igt_fb_is_ccs_modifier(fb->modifier) &&
		blt_fast_copy_supports_tiling(fb->fd,
					      fb_tile_to_blt_tile(fb->modifier));
}

static bool block_copy_ok(const struct igt_fb *fb)
{
	return blt_has_block_copy(fb->fd) &&
		blt_block_copy_supports_tiling(fb->fd,
					       fb_tile_to_blt_tile(fb->modifier));
}

static bool ccs_needs_enginecopy(const struct igt_fb *fb)
{
	if (igt_fb_is_gen12_rc_ccs_cc_modifier(fb->modifier))
		return true;

	if (igt_fb_is_gen12_mc_ccs_modifier(fb->modifier))
		return true;

	if (igt_fb_is_ccs_modifier(fb->modifier) &&
	    !HAS_FLATCCS(intel_get_drm_devid(fb->fd)))
		return true;

	return false;
}

static bool blitter_ok(const struct igt_fb *fb)
{
	if (!is_intel_device(fb->fd))
		return false;

	if (ccs_needs_enginecopy(fb))
		return false;

	if (!blt_uses_extended_block_copy(fb->fd) &&
	    fb->modifier == I915_FORMAT_MOD_X_TILED &&
	    is_xe_device(fb->fd))
		return false;

	if (is_xe_device(fb->fd))
		return true;

	for (int i = 0; i < fb->num_planes; i++) {
		int width = fb->plane_width[i];

		/*
		 * XY_SRC blit supports only 32bpp, but we can still use it
		 * for a 64bpp plane by treating that as a 2x wide 32bpp plane.
		 */
		if (!fast_blit_ok(fb) && fb->plane_bpp[i] == 64)
			width *= 2;

		/*
		 * gen4+ stride limit is 4x this with tiling,
		 * but since our blits are always between tiled
		 * and linear surfaces (and we do this check just
		 * for the tiled surface) we must use the lower
		 * linear stride limit here.
		 */
		if (width > 32767 ||
		    fb->plane_height[i] > 32767 ||
		    fb->strides[i] > 32767)
			return false;
	}

	return true;
}

static bool use_enginecopy(const struct igt_fb *fb)
{
	if (!is_intel_device(fb->fd))
		return false;

	if (blitter_ok(fb))
		return false;

	if (ccs_needs_enginecopy(fb))
		return true;

	return fb->modifier == I915_FORMAT_MOD_Yf_TILED ||
		fb->modifier == I915_FORMAT_MOD_X_TILED;
}

static bool use_blitter(const struct igt_fb *fb)
{
	if (!blitter_ok(fb))
		return false;

	return fb->modifier == I915_FORMAT_MOD_4_TILED_BMG_CCS ||
	       fb->modifier == I915_FORMAT_MOD_4_TILED_LNL_CCS ||
	       fb->modifier == I915_FORMAT_MOD_4_TILED ||
	       fb->modifier == I915_FORMAT_MOD_Y_TILED ||
	       fb->modifier == I915_FORMAT_MOD_Yf_TILED ||
	       (is_i915_device(fb->fd) && !gem_has_mappable_ggtt(fb->fd)) ||
	       (is_xe_device(fb->fd) && xe_has_vram(fb->fd));
}

static void init_buf_ccs(struct intel_buf *buf, int ccs_idx,
			 uint32_t offset, uint32_t stride)
{
	buf->ccs[ccs_idx].offset = offset;
	buf->ccs[ccs_idx].stride = stride;
}

static void init_buf_surface(struct intel_buf *buf, int surface_idx,
			     uint32_t offset, uint32_t stride, uint32_t size)
{
	buf->surface[surface_idx].offset = offset;
	buf->surface[surface_idx].stride = stride;
	buf->surface[surface_idx].size = size;
}

static int yuv_semiplanar_bpp(uint32_t drm_format)
{
	switch (drm_format) {
	case DRM_FORMAT_NV12:
		return 8;
	case DRM_FORMAT_P010:
		return 10;
	case DRM_FORMAT_P012:
		return 12;
	case DRM_FORMAT_P016:
		return 16;
	default:
		igt_assert_f(0, "Unsupported format: %08x\n", drm_format);
	}
}

static int intel_num_surfaces(const struct igt_fb *fb)
{
	int num_surfaces;

	if (!igt_fb_is_ccs_modifier(fb->modifier))
		return fb->num_planes;

	num_surfaces = fb->num_planes;

	if (igt_fb_is_gen12_rc_ccs_cc_modifier(fb->modifier))
		num_surfaces--;

	if (!HAS_FLATCCS(intel_get_drm_devid(fb->fd)))
		num_surfaces /= 2;

	return num_surfaces;
}

static int intel_num_ccs_surfaces(const struct igt_fb *fb)
{
	if (!igt_fb_is_ccs_modifier(fb->modifier))
		return 0;

	if (HAS_FLATCCS(intel_get_drm_devid(fb->fd)))
		return 0;

	return intel_num_surfaces(fb);
}

struct intel_buf *
igt_fb_create_intel_buf(int fd, struct buf_ops *bops,
                        const struct igt_fb *fb,
                        const char *name)
{
	struct intel_buf *buf;
	uint32_t bo_name, handle, compression;
	uint64_t region;
	int num_surfaces;
	int i;

	igt_assert_eq(fb->offsets[0], 0);

	if (igt_fb_is_ccs_modifier(fb->modifier)) {
		igt_assert_eq(fb->strides[0] & 127, 0);

		if (is_gen12_ccs_modifier(fb->modifier)) {
			if (!HAS_FLATCCS(intel_get_drm_devid(fb->fd)))
				igt_assert_eq(fb->strides[1] & 63, 0);
		} else
			igt_assert_eq(fb->strides[1] & 127, 0);

		if (igt_fb_is_gen12_mc_ccs_modifier(fb->modifier))
			compression = I915_COMPRESSION_MEDIA;
		else
			compression = I915_COMPRESSION_RENDER;
	} else {
		num_surfaces = fb->num_planes;
		compression = I915_COMPRESSION_NONE;
	}

	bo_name = gem_flink(fd, fb->gem_handle);
	handle = gem_open(fd, bo_name);

	/* For i915 region doesn't matter, for xe does */
	region = buf_ops_get_driver(bops) == INTEL_DRIVER_XE ?
				vram_if_possible(fd, 0) : -1;
	buf = intel_buf_create_full(bops, handle,
				    fb->width, fb->height,
				    fb->plane_bpp[0], 0,
				    igt_fb_mod_to_tiling(fb->modifier),
				    compression, fb->size,
				    fb->strides[0],
				    region,
				    intel_get_pat_idx_uc(fd),
				    DEFAULT_MOCS_INDEX);
	intel_buf_set_name(buf, name);

	/* only really needed for proper CCS handling */
	switch (fb->drm_format) {
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_XRGB2101010:
		buf->depth = 30;
		break;
	default:
		break;
	}

	/* Make sure we close handle on destroy path */
	intel_buf_set_ownership(buf, true);

	buf->format_is_yuv = igt_format_is_yuv(fb->drm_format);
	buf->format_is_yuv_semiplanar =
		igt_format_is_yuv_semiplanar(fb->drm_format);
	if (buf->format_is_yuv_semiplanar)
		buf->yuv_semiplanar_bpp = yuv_semiplanar_bpp(fb->drm_format);

	num_surfaces = intel_num_surfaces(fb);

	for (i = 0; i < intel_num_ccs_surfaces(fb); i++)
		init_buf_ccs(buf, i,
			     fb->offsets[num_surfaces + i],
			     fb->strides[num_surfaces + i]);

	igt_assert(fb->offsets[0] == 0);
	for (i = 0; i < num_surfaces; i++) {
		uint32_t end =
			i == fb->num_planes - 1 ? fb->size : fb->offsets[i + 1];

		init_buf_surface(buf, i,
				 fb->offsets[i],
				 fb->strides[i],
				 end - fb->offsets[i]);
	}

	if (fb->modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC ||
	    fb->modifier == I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC)
		buf->cc.offset = fb->offsets[2];

	if (fb->modifier == I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC)
		buf->cc.offset = fb->offsets[1];

	/*
	 * TGL+ have a feature called "Fast Clear Optimization (FCV)"
	 * which can perform automagic fast clears even when we didn't
	 * ask the hardware to perform fast clears. This can happen
	 * whenever the clear color matches the fragment output. If
	 * no clear color is specified it appears that black output
	 * can get automagically fast cleared.
	 *
	 * Apparently TGL[A0-C0] and DG1 have this feature always
	 * enabled, ADL seems to have it permanently disabled, and
	 * on DG2+ one can control it via 3DSTATE_3DMODE (default
	 * being disabled).
	 *
	 * For the hardware that has this always enabled we'll try
	 * to stop it from happening for non clear color modifiers
	 * by always specifying a clear color which won't match
	 * any valid fragment output (eg. all NaNs).
	 */
	if (fb->modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS) {
		buf->cc.disable = true;
		buf->cc.offset = fb->size - 64;
	}

	return buf;
}

static struct intel_buf *create_buf(struct fb_blit_upload *blit,
				   const struct igt_fb *fb,
				   const char *name)
{
	return igt_fb_create_intel_buf(blit->fd, blit->bops, fb, name);
}

static void fini_buf(struct intel_buf *buf)
{
	intel_buf_destroy(buf);
}

static bool use_vebox_copy(const struct igt_fb *src_fb,
			   const struct igt_fb *dst_fb)
{

	return igt_fb_is_gen12_mc_ccs_modifier(dst_fb->modifier) ||
	       igt_format_is_yuv(src_fb->drm_format) ||
	       igt_format_is_yuv(dst_fb->drm_format);
}

/**
 * copy_with_engine:
 * @blit: context for the copy operation
 * @dst_fb: destination buffer
 * @src_fb: source buffer
 *
 * Copy @src_fb to @dst_fb using either the render or vebox engine. The engine
 * is selected based on the compression surface format required by the @dst_fb
 * FB modifier. On GEN12+ a given compression format (render or media) can be
 * produced only by the selected engine:
 * - For GEN12 media compressed: vebox engine
 * - For uncompressed, pre-GEN12 compressed, GEN12+ render compressed: render engine
 * Note that both GEN12 engine is capable of reading either compression formats.
 */
static void copy_with_engine(struct fb_blit_upload *blit,
			     const struct igt_fb *dst_fb,
			     const struct igt_fb *src_fb)
{
	struct intel_buf *src, *dst;
	igt_render_copyfunc_t render_copy = NULL;
	igt_vebox_copyfunc_t vebox_copy = NULL;

	if (use_vebox_copy(src_fb, dst_fb))
		vebox_copy = igt_get_vebox_copyfunc(intel_get_drm_devid(blit->fd));
	else
		render_copy = igt_get_render_copyfunc(blit->fd);

	igt_require(vebox_copy || render_copy);

	igt_assert_eq(dst_fb->offsets[0], 0);
	igt_assert_eq(src_fb->offsets[0], 0);

	src = create_buf(blit, src_fb, "cairo enginecopy src");
	dst = create_buf(blit, dst_fb, "cairo enginecopy dst");

	if (vebox_copy)
		vebox_copy(blit->ibb, src,
			   dst_fb->plane_width[0], dst_fb->plane_height[0],
			   dst);
	else
		render_copy(blit->ibb,
			    src,
			    0, 0,
			    dst_fb->plane_width[0], dst_fb->plane_height[0],
			    dst,
			    0, 0);

	fini_buf(dst);
	fini_buf(src);
}

static struct blt_copy_object *allocate_and_initialize_blt(const struct igt_fb *fb,
							   uint32_t handle,
							   uint32_t memregion,
							   enum blt_tiling_type blt_tile,
							   uint32_t plane,
							   uint8_t pat_index)
{
	uint64_t stride;
	struct blt_copy_object *blt = calloc(1, sizeof(*blt));

	if (!blt)
		return NULL;

	stride = blt_tile == T_LINEAR ? fb->strides[plane] : fb->strides[plane] / 4;

	blt_set_object(blt, handle, fb->size, memregion,
		       intel_get_uc_mocs_index(fb->fd),
		       pat_index,
		       blt_tile,
		       igt_fb_is_ccs_modifier(fb->modifier) ? COMPRESSION_ENABLED : COMPRESSION_DISABLED,
		       igt_fb_is_gen12_mc_ccs_modifier(fb->modifier) ? COMPRESSION_TYPE_MEDIA : COMPRESSION_TYPE_3D);

	blt_set_geom(blt, stride, 0, 0,
		     fb->plane_width[plane], fb->plane_height[plane], 0, 0);
	blt->plane_offset = fb->offsets[plane];

	return blt;
}

static void *map_buffer(int fd, uint32_t handle, size_t size)
{
	if (is_xe_device(fd))
		return xe_bo_mmap_ext(fd, handle, size, PROT_READ | PROT_WRITE);
	else
		return gem_mmap__device_coherent(fd, handle, 0, size,
						 PROT_READ | PROT_WRITE);
}

static struct blt_copy_object *blt_fb_init(const struct igt_fb *fb,
					   uint32_t plane, uint32_t memregion,
					   uint8_t pat_index)
{
	uint32_t name, handle;
	enum blt_tiling_type blt_tile;
	struct blt_copy_object *blt;

	if (!fb)
		return NULL;

	name = gem_flink(fb->fd, fb->gem_handle);
	handle = gem_open(fb->fd, name);

	if (!handle)
		return NULL;

	blt_tile = fb_tile_to_blt_tile(fb->modifier);
	blt = allocate_and_initialize_blt(fb, handle, memregion, blt_tile,
					  plane, pat_index);

	if (!blt)
		return NULL;

	blt->ptr = map_buffer(fb->fd, handle, fb->size);
	if (!blt->ptr) {
		free(blt);
		return NULL;
	}

	return blt;
}

static enum blt_color_depth blt_get_bpp(const struct igt_fb *fb,
					int color_plane)
{
	switch (fb->plane_bpp[color_plane]) {
	case 8:
		return CD_8bit;
	case 16:
		return CD_16bit;
	case 32:
		return CD_32bit;
	case 64:
		return CD_64bit;
	case 96:
		return CD_96bit;
	case 128:
		return CD_128bit;
	default:
		igt_assert(0);
	}
}

const struct {
	uint32_t format;
	int color_plane;
	enum blt_compression_type type;
	uint32_t return_value;
} compression_mappings[] = {
	{ DRM_FORMAT_XRGB16161616F, 0, COMPRESSION_TYPE_3D, 0x5 }, /* R16G16B16A16_FLOAT */
	{ DRM_FORMAT_XRGB2101010, 0, COMPRESSION_TYPE_3D, 0xc }, /* B10G10R10A2_UNORM */
	{ DRM_FORMAT_XRGB8888, 0, COMPRESSION_TYPE_3D, 0x8 }, /* B8G8R8A8_UNORM */

	/* FIXME why doesn't 0x8/B8G8R8A8_UNORM work here? */
	{ DRM_FORMAT_XYUV8888, 0, COMPRESSION_TYPE_MEDIA, 0x18 }, /* R8_UNORM */

	{ DRM_FORMAT_NV12, 0, COMPRESSION_TYPE_MEDIA, 0x18 }, /* R8_UNORM */
	{ DRM_FORMAT_NV12, 1, COMPRESSION_TYPE_MEDIA, 0xa },  /* R8G8_UNORM */
	{ DRM_FORMAT_P010, 0, COMPRESSION_TYPE_MEDIA, 0x14 }, /* R16_UNORM */
	{ DRM_FORMAT_P010, 1, COMPRESSION_TYPE_MEDIA, 0x6 },  /* R16G16_UNORM */
};

static uint32_t get_compression_return_value(uint32_t format, int color_plane,
					     enum  blt_compression_type type)
{
	for (int i = 0; i < ARRAY_SIZE(compression_mappings); i++) {
		if (compression_mappings[i].format == format &&
		    compression_mappings[i].color_plane == color_plane &&
		    compression_mappings[i].type == type) {
			return compression_mappings[i].return_value;
		}
	}
	igt_assert_f(0, "Unknown compression type or format\n");
	return 0; // This line is to avoid compilation warnings, it will not be reached.
}

static uint32_t blt_compression_format(const struct blt_copy_object *obj,
				       const struct igt_fb *fb, int color_plane)
{
	if (obj->compression == COMPRESSION_DISABLED)
		return 0;

	return get_compression_return_value(igt_reduce_format(fb->drm_format),
					    color_plane, obj->compression_type);
}

static void setup_context_and_memory_region(const struct igt_fb *fb, uint32_t *ctx,
					    uint64_t *ahnd, uint32_t *mem_region,
					    uint32_t *vm, uint32_t *bb,
					    uint64_t *bb_size,
					    const intel_ctx_t **ictx,
					    uint32_t *exec_queue,
					    intel_ctx_t **xe_ctx)
{
	struct drm_xe_engine_class_instance inst = {
		.engine_class = DRM_XE_ENGINE_CLASS_COPY,
	};

	if (is_i915_device(fb->fd) && !gem_has_relocations(fb->fd)) {
		igt_require(gem_has_contexts(fb->fd));
		*ictx = intel_ctx_create_all_physical(fb->fd);
		*mem_region = HAS_FLATCCS(intel_get_drm_devid(fb->fd)) ?
			REGION_LMEM(0) : REGION_SMEM;
		*ctx = gem_context_create(fb->fd);
		*ahnd = get_reloc_ahnd(fb->fd, *ctx);

		igt_assert(__gem_create_in_memory_regions(fb->fd,
							  bb,
							  bb_size,
							  *mem_region) == 0);
	} else if (is_xe_device(fb->fd)) {
		*vm = xe_vm_create(fb->fd, 0, 0);
		*exec_queue = xe_exec_queue_create(fb->fd, *vm, &inst, 0);
		*xe_ctx = intel_ctx_xe(fb->fd, *vm, *exec_queue, 0, 0, 0);
		*mem_region = vram_if_possible(fb->fd, 0);

		*ahnd = intel_allocator_open_full(fb->fd, (*xe_ctx)->vm, 0, 0,
						  INTEL_ALLOCATOR_SIMPLE,
						  ALLOC_STRATEGY_LOW_TO_HIGH, 0);

		*bb_size = xe_bb_size(fb->fd, *bb_size);
		*bb = xe_bo_create(fb->fd, 0, *bb_size, *mem_region, 0);
	}
}

static void cleanup_blt_resources(uint32_t ctx, uint64_t ahnd, bool is_xe,
				  uint32_t xe_bb, uint32_t exec_queue,
				  uint32_t vm, intel_ctx_t *xe_ctx,
				  int fd, const intel_ctx_t *ictx)
{
	if (ctx)
		gem_context_destroy(fd, ctx);
	put_ahnd(ahnd);

	if (is_xe) {
		gem_close(fd, xe_bb);
		xe_exec_queue_destroy(fd, exec_queue);
		xe_vm_destroy(fd, vm);
		free(xe_ctx);
	}

	intel_ctx_destroy(fd, ictx);
}

static void do_block_copy(const struct igt_fb *src_fb,
			  const struct igt_fb *dst_fb,
			  uint32_t mem_region, uint32_t i, uint64_t ahnd,
			  uint32_t xe_bb, uint64_t bb_size,
			  const intel_ctx_t *ctx,
			  struct intel_execution_engine2 *e,
			  uint8_t dst_pat_index)
{
	struct blt_copy_data blt = {};
	struct blt_copy_object *src = blt_fb_init(src_fb, i, mem_region,
						  intel_get_pat_idx_uc(src_fb->fd));
	struct blt_copy_object *dst = blt_fb_init(dst_fb, i, mem_region,
						  dst_pat_index);
	struct blt_block_copy_data_ext ext = {}, *pext = NULL;

	igt_assert(src && dst);

	igt_assert_f(blt.dst.compression == COMPRESSION_DISABLED ||
		     blt.dst.compression_type !=  COMPRESSION_TYPE_MEDIA,
		     "Destination compression not supported on mc ccs\n");

	blt_copy_init(src_fb->fd, &blt);
	blt.color_depth = blt_get_bpp(src_fb, i);
	blt_set_copy_object(&blt.src, src);
	blt_set_copy_object(&blt.dst, dst);

	if (blt_uses_extended_block_copy(src_fb->fd)) {
		blt_set_object_ext(&ext.src,
				   blt_compression_format(&blt.src, src_fb, i),
				   src_fb->plane_width[i], src_fb->plane_height[i],
				   SURFACE_TYPE_2D);

		blt_set_object_ext(&ext.dst,
				   blt_compression_format(&blt.dst, dst_fb, i),
				   dst_fb->plane_width[i], dst_fb->plane_height[i],
				   SURFACE_TYPE_2D);
		pext = &ext;
	}

	blt_set_batch(&blt.bb, xe_bb, bb_size, mem_region);
	blt_block_copy(src_fb->fd, ctx, e, ahnd, &blt, pext);

	if (e)
		gem_sync(src_fb->fd, blt.dst.handle);

	blt_destroy_object(src_fb->fd, src);
	blt_destroy_object(dst_fb->fd, dst);
}

static void blitcopy(const struct igt_fb *dst_fb,
		     const struct igt_fb *src_fb)
{
	uint32_t src_tiling = igt_fb_mod_to_tiling(src_fb->modifier);
	uint32_t dst_tiling = igt_fb_mod_to_tiling(dst_fb->modifier);
	uint32_t ctx = 0, bb, mem_region, vm, exec_queue;
	uint64_t ahnd = 0, bb_size = 4096;
	const intel_ctx_t *ictx = NULL;
	intel_ctx_t *xe_ctx = NULL;
	struct intel_execution_engine2 *e;
	bool is_xe = is_xe_device(dst_fb->fd);

	igt_assert_eq(dst_fb->fd, src_fb->fd);
	igt_assert_eq(dst_fb->num_planes, src_fb->num_planes);
	igt_assert(!igt_fb_is_gen12_rc_ccs_cc_modifier(src_fb->modifier));
	igt_assert(!igt_fb_is_gen12_rc_ccs_cc_modifier(dst_fb->modifier));

	setup_context_and_memory_region(dst_fb, &ctx, &ahnd, &mem_region,
					&vm, &bb, &bb_size, &ictx,
					&exec_queue, &xe_ctx);

	for (int i = 0; i < dst_fb->num_planes; i++) {
		igt_assert_eq(dst_fb->plane_bpp[i], src_fb->plane_bpp[i]);
		igt_assert_eq(dst_fb->plane_width[i], src_fb->plane_width[i]);
		igt_assert_eq(dst_fb->plane_height[i], src_fb->plane_height[i]);

		if (is_xe) {
			do_block_copy(src_fb, dst_fb, mem_region, i, ahnd,
				      bb, bb_size, xe_ctx, NULL,
				      intel_get_pat_idx_uc(dst_fb->fd));
		} else if (fast_blit_ok(src_fb) && fast_blit_ok(dst_fb)) {
			igt_blitter_fast_copy__raw(dst_fb->fd,
						   ahnd, ctx, NULL,
						   src_fb->gem_handle,
						   src_fb->offsets[i],
						   src_fb->strides[i],
						   src_tiling,
						   0, 0, /* src_x, src_y */
						   src_fb->size,
						   dst_fb->plane_width[i],
						   dst_fb->plane_height[i],
						   dst_fb->plane_bpp[i],
						   dst_fb->gem_handle,
						   dst_fb->offsets[i],
						   dst_fb->strides[i],
						   dst_tiling,
						   0, 0 /* dst_x, dst_y */,
						   dst_fb->size);
		} else if (ahnd && block_copy_ok(src_fb) && block_copy_ok(dst_fb)) {
			for_each_ctx_engine(src_fb->fd, ictx, e) {
				if (gem_engine_can_block_copy(src_fb->fd, e)) {
					do_block_copy(src_fb, dst_fb, mem_region, i, ahnd,
						      bb, bb_size, ictx, e,
						      intel_get_pat_idx_uc(dst_fb->fd));
					break;
				}
			}
			igt_assert_f(e, "No block copy capable engine found!\n");
		} else {
			igt_blitter_src_copy(dst_fb->fd,
					     ahnd, ctx, NULL,
					     src_fb->gem_handle,
					     src_fb->offsets[i],
					     src_fb->strides[i],
					     src_tiling,
					     0, 0, /* src_x, src_y */
					     src_fb->size,
					     dst_fb->plane_width[i],
					     dst_fb->plane_height[i],
					     dst_fb->plane_bpp[i],
					     dst_fb->gem_handle,
					     dst_fb->offsets[i],
					     dst_fb->strides[i],
					     dst_tiling,
					     0, 0 /* dst_x, dst_y */,
					     dst_fb->size);
		}
	}

	cleanup_blt_resources(ctx, ahnd, is_xe, bb, exec_queue, vm, xe_ctx,
			      src_fb->fd, ictx);
}

/**
 * igt_xe2_blit_with_dst_pat:
 * @dst_fb: pointer to a destination #igt_fb structure
 * @src_fb: pointer to a source #igt_fb structure
 * @dst_pat_index: uint8_t pat index to set for destination framebuffer
 *
 * Copy matching size src_fb to dst_fb with setting pat index to destination
 * framebuffer
 */
void igt_xe2_blit_with_dst_pat(const struct igt_fb *dst_fb,
			       const struct igt_fb *src_fb,
			       uint8_t dst_pat_index)
{
	uint32_t ctx = 0, bb, mem_region, vm, exec_queue;
	uint64_t ahnd = 0, bb_size = 4096;
	intel_ctx_t *xe_ctx = NULL;

	igt_assert_eq(dst_fb->fd, src_fb->fd);
	igt_assert_eq(dst_fb->num_planes, src_fb->num_planes);
	igt_assert(!igt_fb_is_gen12_rc_ccs_cc_modifier(src_fb->modifier));
	igt_assert(!igt_fb_is_gen12_rc_ccs_cc_modifier(dst_fb->modifier));

	setup_context_and_memory_region(dst_fb, &ctx, &ahnd, &mem_region,
					&vm, &bb, &bb_size, NULL,
					&exec_queue, &xe_ctx);

	for (int i = 0; i < dst_fb->num_planes; i++) {
		igt_assert_eq(dst_fb->plane_bpp[i], src_fb->plane_bpp[i]);
		igt_assert_eq(dst_fb->plane_width[i], src_fb->plane_width[i]);
		igt_assert_eq(dst_fb->plane_height[i], src_fb->plane_height[i]);

		do_block_copy(src_fb, dst_fb, mem_region, i, ahnd, bb, bb_size,
			      xe_ctx, NULL, dst_pat_index);
	}

	cleanup_blt_resources(ctx, ahnd, true, bb, exec_queue, vm, xe_ctx,
			      src_fb->fd, NULL);
}

static void free_linear_mapping(struct fb_blit_upload *blit)
{
	int fd = blit->fd;
	struct igt_fb *fb = blit->fb;
	struct fb_blit_linear *linear = &blit->linear;

	if (igt_vc4_is_tiled(fb->modifier)) {
		void *map = igt_vc4_mmap_bo(fd, fb->gem_handle, fb->size, PROT_WRITE);

		vc4_fb_convert_plane_to_tiled(fb, map, &linear->fb, &linear->map);

		munmap(map, fb->size);
	} else if (igt_amd_is_tiled(fb->modifier)) {
		void *map = igt_amd_mmap_bo(fd, fb->gem_handle, fb->size, PROT_WRITE);

		igt_amd_fb_convert_plane_to_tiled(fb, map, &linear->fb, linear->map);

		munmap(map, fb->size);
	} else if (is_nouveau_device(fd)) {
		igt_nouveau_fb_blit(fb, &linear->fb);
		igt_nouveau_delete_bo(&linear->fb);
	} else if (is_xe_device(fd)) {
		gem_munmap(linear->map, linear->fb.size);

		if (blit->ibb)
			copy_with_engine(blit, fb, &linear->fb);
		else
			blitcopy(fb, &linear->fb);

		gem_close(fd, linear->fb.gem_handle);
	} else {
		gem_munmap(linear->map, linear->fb.size);
		gem_set_domain(fd, linear->fb.gem_handle,
			I915_GEM_DOMAIN_GTT, 0);

		if (blit->ibb)
			copy_with_engine(blit, fb, &linear->fb);
		else
			blitcopy(fb, &linear->fb);

		gem_sync(fd, linear->fb.gem_handle);
		gem_close(fd, linear->fb.gem_handle);
	}

	if (blit->ibb) {
		intel_bb_destroy(blit->ibb);
		buf_ops_destroy(blit->bops);
	}
}

static void destroy_cairo_surface__gpu(void *arg)
{
	struct fb_blit_upload *blit = arg;

	blit->fb->cairo_surface = NULL;

	free_linear_mapping(blit);

	free(blit);
}

static void setup_linear_mapping(struct fb_blit_upload *blit)
{
	int fd = blit->fd;
	struct igt_fb *fb = blit->fb;
	struct fb_blit_linear *linear = &blit->linear;

	if (!igt_vc4_is_tiled(fb->modifier) && use_enginecopy(fb)) {
		blit->bops = buf_ops_create(fd);
		blit->ibb = intel_bb_create(fd, 4096);
	}

	/*
	 * We create a linear BO that we'll map for the CPU to write to (using
	 * cairo). This linear bo will be then blitted to its final
	 * destination, tiling it at the same time.
	 */

	igt_init_fb(&linear->fb, fb->fd, fb->width, fb->height,
		    fb->drm_format, DRM_FORMAT_MOD_LINEAR,
		    fb->color_encoding, fb->color_range);

	create_bo_for_fb(&linear->fb, true);

	igt_assert(linear->fb.gem_handle > 0);

	if (igt_vc4_is_tiled(fb->modifier)) {
		void *map = igt_vc4_mmap_bo(fd, fb->gem_handle, fb->size, PROT_READ);

		linear->map = igt_vc4_mmap_bo(fd, linear->fb.gem_handle,
					      linear->fb.size,
					      PROT_READ | PROT_WRITE);

		vc4_fb_convert_plane_from_tiled(&linear->fb, &linear->map, fb, map);

		munmap(map, fb->size);
	} else if (igt_amd_is_tiled(fb->modifier)) {
		linear->map = igt_amd_mmap_bo(fd, linear->fb.gem_handle,
					      linear->fb.size,
					      PROT_READ | PROT_WRITE);
	} else if (is_nouveau_device(fd)) {
		/* Currently we also blit linear bos instead of mapping them as-is, as mmap() on
		 * nouveau is quite slow right now
		 */
		igt_nouveau_fb_blit(&linear->fb, fb);

		linear->map = igt_nouveau_mmap_bo(&linear->fb, PROT_READ | PROT_WRITE);
	} else if (is_xe_device(fd)) {
		if (blit->ibb)
			copy_with_engine(blit, &linear->fb, fb);
		else
			blitcopy(&linear->fb, fb);

		linear->map = xe_bo_mmap_ext(fd, linear->fb.gem_handle,
					     linear->fb.size, PROT_READ | PROT_WRITE);
	} else {
		/* Copy fb content to linear BO */
		gem_set_domain(fd, linear->fb.gem_handle,
				I915_GEM_DOMAIN_GTT, 0);

		if (blit->ibb)
			copy_with_engine(blit, &linear->fb, fb);
		else
			blitcopy(&linear->fb, fb);

		gem_sync(fd, linear->fb.gem_handle);

		gem_set_domain(fd, linear->fb.gem_handle,
			I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

		/* Setup cairo context */
		linear->map = gem_mmap__cpu(fd, linear->fb.gem_handle,
					0, linear->fb.size, PROT_READ | PROT_WRITE);
	}
}

static void create_cairo_surface__gpu(int fd, struct igt_fb *fb)
{
	struct fb_blit_upload *blit;
	cairo_format_t cairo_format;

	blit = calloc(1, sizeof(*blit));
	igt_assert(blit);

	blit->fd = fd;
	blit->fb = fb;
	setup_linear_mapping(blit);

	cairo_format = drm_format_to_cairo(fb->drm_format);
	fb->cairo_surface =
		cairo_image_surface_create_for_data(blit->linear.map,
						    cairo_format,
						    fb->width, fb->height,
						    blit->linear.fb.strides[0]);
	fb->domain = I915_GEM_DOMAIN_GTT;

	cairo_surface_set_user_data(fb->cairo_surface,
				    (cairo_user_data_key_t *)create_cairo_surface__gpu,
				    blit, destroy_cairo_surface__gpu);
}

/**
 * igt_dirty_fb:
 * @fd: open drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * Flushes out the whole framebuffer.
 *
 * Returns: 0 upon success.
 */
int igt_dirty_fb(int fd, struct igt_fb *fb)
{
	return drmModeDirtyFB(fb->fd, fb->fb_id, NULL, 0);
}

static void unmap_bo(struct igt_fb *fb, void *ptr)
{
	if (is_nouveau_device(fb->fd))
		igt_nouveau_munmap_bo(fb);
	else
		gem_munmap(ptr, fb->size);

	if (fb->is_dumb)
		igt_dirty_fb(fb->fd, fb);
}

static void destroy_cairo_surface__gtt(void *arg)
{
	struct igt_fb *fb = arg;

	unmap_bo(fb, cairo_image_surface_get_data(fb->cairo_surface));
	fb->cairo_surface = NULL;
}

static void *map_bo(int fd, struct igt_fb *fb)
{
	bool is_i915 = is_i915_device(fd);
	void *ptr;

	if (is_i915)
		gem_set_domain(fd, fb->gem_handle,
			       I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

	if (fb->is_dumb)
		ptr = kmstest_dumb_map_buffer(fd, fb->gem_handle, fb->size,
					      PROT_READ | PROT_WRITE);
	else if (is_i915 && gem_has_mappable_ggtt(fd))
		ptr = gem_mmap__gtt(fd, fb->gem_handle, fb->size,
				    PROT_READ | PROT_WRITE);
	else if (is_i915)
		ptr = gem_mmap__device_coherent(fd, fb->gem_handle, 0,
						fb->size,
						PROT_READ | PROT_WRITE);
	else if (is_vc4_device(fd))
		ptr = igt_vc4_mmap_bo(fd, fb->gem_handle, fb->size,
				      PROT_READ | PROT_WRITE);
	else if (is_amdgpu_device(fd))
		ptr = igt_amd_mmap_bo(fd, fb->gem_handle, fb->size,
				      PROT_READ | PROT_WRITE);
	else if (is_nouveau_device(fd))
		ptr = igt_nouveau_mmap_bo(fb, PROT_READ | PROT_WRITE);
	else if (is_xe_device(fd))
		ptr = xe_bo_mmap_ext(fd, fb->gem_handle,
				     fb->size, PROT_READ | PROT_WRITE);
	else
		igt_assert(false);

	return ptr;
}

static void create_cairo_surface__gtt(int fd, struct igt_fb *fb)
{
	void *ptr = map_bo(fd, fb);

	fb->cairo_surface =
		cairo_image_surface_create_for_data(ptr,
						    drm_format_to_cairo(fb->drm_format),
						    fb->width, fb->height, fb->strides[0]);
	igt_require_f(cairo_surface_status(fb->cairo_surface) == CAIRO_STATUS_SUCCESS,
		      "Unable to create a cairo surface: %s\n",
		      cairo_status_to_string(cairo_surface_status(fb->cairo_surface)));

	fb->domain = I915_GEM_DOMAIN_GTT;

	cairo_surface_set_user_data(fb->cairo_surface,
				    (cairo_user_data_key_t *)create_cairo_surface__gtt,
				    fb, destroy_cairo_surface__gtt);
}

struct fb_convert_blit_upload {
	struct fb_blit_upload base;

	struct igt_fb shadow_fb;
	uint8_t *shadow_ptr;
};

static void *igt_fb_create_cairo_shadow_buffer(int fd,
					       unsigned drm_format,
					       unsigned int width,
					       unsigned int height,
					       struct igt_fb *shadow)
{
	void *ptr;

	igt_assert(shadow);

	igt_init_fb(shadow, fd, width, height,
		    drm_format, DRM_FORMAT_MOD_LINEAR,
		    IGT_COLOR_YCBCR_BT709, IGT_COLOR_YCBCR_LIMITED_RANGE);

	shadow->strides[0] = ALIGN(width * (shadow->plane_bpp[0] / 8), 16);
	shadow->size = ALIGN((uint64_t)shadow->strides[0] * height,
			     sysconf(_SC_PAGESIZE));
	ptr = mmap(NULL, shadow->size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	igt_assert(ptr != MAP_FAILED);

	return ptr;
}

static void igt_fb_destroy_cairo_shadow_buffer(struct igt_fb *shadow,
					       void *ptr)
{
	munmap(ptr, shadow->size);
}

static uint8_t clamp8(float val)
{
	return clamp((int)(val + 0.5f), 0, 255);
}

static uint16_t clamp16(float val)
{
	return clamp((int)(val + 0.5f), 0, 65535);
}

static void read_rgb(struct igt_vec4 *rgb, const uint8_t *rgb24)
{
	rgb->d[0] = rgb24[2];
	rgb->d[1] = rgb24[1];
	rgb->d[2] = rgb24[0];
	rgb->d[3] = 1.0f;
}

static void write_rgb(uint8_t *rgb24, const struct igt_vec4 *rgb)
{
	rgb24[2] = clamp8(rgb->d[0]);
	rgb24[1] = clamp8(rgb->d[1]);
	rgb24[0] = clamp8(rgb->d[2]);
}

struct fb_convert_buf {
	void			*ptr;
	struct igt_fb		*fb;
	bool                     slow_reads;
};

struct fb_convert {
	struct fb_convert_buf	dst;
	struct fb_convert_buf	src;
};

static void *convert_src_get(const struct fb_convert *cvt)
{
	void *buf;

	if (!cvt->src.slow_reads)
		return cvt->src.ptr;

	/*
	 * Reading from the BO is awfully slow because of lack of read caching,
	 * it's faster to copy the whole BO to a temporary buffer and convert
	 * from there.
	 */
	buf = malloc(cvt->src.fb->size);
	if (!buf)
		return cvt->src.ptr;

	igt_memcpy_from_wc(buf, cvt->src.ptr, cvt->src.fb->size);

	return buf;
}

static void convert_src_put(const struct fb_convert *cvt,
			    void *src_buf)
{
	if (src_buf != cvt->src.ptr)
		free(src_buf);
}

struct yuv_parameters {
	unsigned	ay_inc;
	unsigned	uv_inc;
	unsigned	ay_stride;
	unsigned	uv_stride;
	unsigned	a_offset;
	unsigned	y_offset;
	unsigned	u_offset;
	unsigned	v_offset;
};

static void get_yuv_parameters(struct igt_fb *fb, struct yuv_parameters *params)
{
	igt_assert(igt_format_is_yuv(fb->drm_format));

	switch (fb->drm_format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		params->ay_inc = 1;
		params->uv_inc = 2;
		break;

	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
		params->ay_inc = 1;
		params->uv_inc = 1;
		break;

	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
		params->ay_inc = 2;
		params->uv_inc = 4;
		break;

	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
	case DRM_FORMAT_Y412:
	case DRM_FORMAT_Y416:
	case DRM_FORMAT_XYUV8888:
		params->ay_inc = 4;
		params->uv_inc = 4;
		break;
	}

	switch (fb->drm_format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		params->ay_stride = fb->strides[0];
		params->uv_stride = fb->strides[1];
		break;

	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
	case DRM_FORMAT_XYUV8888:
	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
	case DRM_FORMAT_Y412:
	case DRM_FORMAT_Y416:
		params->ay_stride = fb->strides[0];
		params->uv_stride = fb->strides[0];
		break;
	}

	switch (fb->drm_format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[1];
		params->v_offset = fb->offsets[1] + 1;
		break;

	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[1] + 1;
		params->v_offset = fb->offsets[1];
		break;

	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[1];
		params->v_offset = fb->offsets[2];
		break;

	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[2];
		params->v_offset = fb->offsets[1];
		break;

	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[1];
		params->v_offset = fb->offsets[1] + 2;
		break;

	case DRM_FORMAT_YUYV:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[0] + 1;
		params->v_offset = fb->offsets[0] + 3;
		break;

	case DRM_FORMAT_YVYU:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[0] + 3;
		params->v_offset = fb->offsets[0] + 1;
		break;

	case DRM_FORMAT_UYVY:
		params->y_offset = fb->offsets[0] + 1;
		params->u_offset = fb->offsets[0];
		params->v_offset = fb->offsets[0] + 2;
		break;

	case DRM_FORMAT_VYUY:
		params->y_offset = fb->offsets[0] + 1;
		params->u_offset = fb->offsets[0] + 2;
		params->v_offset = fb->offsets[0];
		break;

	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
		params->y_offset = fb->offsets[0];
		params->u_offset = fb->offsets[0] + 2;
		params->v_offset = fb->offsets[0] + 6;
		break;

	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
	case DRM_FORMAT_Y412:
	case DRM_FORMAT_Y416:
		params->a_offset = fb->offsets[0] + 6;
		params->y_offset = fb->offsets[0] + 2;
		params->u_offset = fb->offsets[0];
		params->v_offset = fb->offsets[0] + 4;
		break;

	case DRM_FORMAT_XYUV8888:
		params->y_offset = fb->offsets[0] + 2;
		params->u_offset = fb->offsets[0] + 1;
		params->v_offset = fb->offsets[0] + 0;
		break;
	}
}

static void convert_yuv_to_rgb24(struct fb_convert *cvt)
{
	const struct format_desc_struct *src_fmt =
		lookup_drm_format(cvt->src.fb->drm_format);
	int i, j;
	uint8_t bpp = 4;
	uint8_t *y, *u, *v;
	uint8_t *rgb24 = cvt->dst.ptr;
	unsigned int rgb24_stride = cvt->dst.fb->strides[0];
	struct igt_mat4 m = igt_ycbcr_to_rgb_matrix(cvt->src.fb->drm_format,
						    cvt->dst.fb->drm_format,
						    cvt->src.fb->color_encoding,
						    cvt->src.fb->color_range);
	uint8_t *buf;
	struct yuv_parameters params = { };

	igt_assert(cvt->dst.fb->drm_format == DRM_FORMAT_XRGB8888 &&
		   igt_format_is_yuv(cvt->src.fb->drm_format));

	buf = convert_src_get(cvt);
	get_yuv_parameters(cvt->src.fb, &params);
	y = buf + params.y_offset;
	u = buf + params.u_offset;
	v = buf + params.v_offset;

	for (i = 0; i < cvt->dst.fb->height; i++) {
		const uint8_t *y_tmp = y;
		const uint8_t *u_tmp = u;
		const uint8_t *v_tmp = v;
		uint8_t *rgb_tmp = rgb24;

		for (j = 0; j < cvt->dst.fb->width; j++) {
			struct igt_vec4 rgb, yuv;

			yuv.d[0] = *y_tmp;
			yuv.d[1] = *u_tmp;
			yuv.d[2] = *v_tmp;
			yuv.d[3] = 1.0f;

			rgb = igt_matrix_transform(&m, &yuv);
			write_rgb(rgb_tmp, &rgb);

			rgb_tmp += bpp;
			y_tmp += params.ay_inc;

			if ((src_fmt->hsub == 1) || (j % src_fmt->hsub)) {
				u_tmp += params.uv_inc;
				v_tmp += params.uv_inc;
			}
		}

		rgb24 += rgb24_stride;
		y += params.ay_stride;

		if ((src_fmt->vsub == 1) || (i % src_fmt->vsub)) {
			u += params.uv_stride;
			v += params.uv_stride;
		}
	}

	convert_src_put(cvt, buf);
}

static void convert_rgb24_to_yuv(struct fb_convert *cvt)
{
	const struct format_desc_struct *dst_fmt =
		lookup_drm_format(cvt->dst.fb->drm_format);
	int i, j;
	uint8_t *y, *u, *v;
	const uint8_t *rgb24 = cvt->src.ptr;
	uint8_t bpp = 4;
	unsigned rgb24_stride = cvt->src.fb->strides[0];
	struct igt_mat4 m = igt_rgb_to_ycbcr_matrix(cvt->src.fb->drm_format,
						    cvt->dst.fb->drm_format,
						    cvt->dst.fb->color_encoding,
						    cvt->dst.fb->color_range);
	struct yuv_parameters params = { };

	igt_assert(cvt->src.fb->drm_format == DRM_FORMAT_XRGB8888 &&
		   igt_format_is_yuv(cvt->dst.fb->drm_format));

	get_yuv_parameters(cvt->dst.fb, &params);
	y = cvt->dst.ptr + params.y_offset;
	u = cvt->dst.ptr + params.u_offset;
	v = cvt->dst.ptr + params.v_offset;

	for (i = 0; i < cvt->dst.fb->height; i++) {
		const uint8_t *rgb_tmp = rgb24;
		uint8_t *y_tmp = y;
		uint8_t *u_tmp = u;
		uint8_t *v_tmp = v;

		for (j = 0; j < cvt->dst.fb->width; j++) {
			const uint8_t *pair_rgb24 = rgb_tmp;
			struct igt_vec4 pair_rgb, rgb;
			struct igt_vec4 pair_yuv, yuv;

			read_rgb(&rgb, rgb_tmp);
			yuv = igt_matrix_transform(&m, &rgb);

			rgb_tmp += bpp;

			*y_tmp = clamp8(yuv.d[0]);
			y_tmp += params.ay_inc;

			if ((i % dst_fmt->vsub) || (j % dst_fmt->hsub))
				continue;

			/*
			 * We assume the MPEG2 chroma siting convention, where
			 * pixel center for Cb'Cr' is between the left top and
			 * bottom pixel in a 2x2 block, so take the average.
			 *
			 * Therefore, if we use subsampling, we only really care
			 * about two pixels all the time, either the two
			 * subsequent pixels horizontally, vertically, or the
			 * two corners in a 2x2 block.
			 *
			 * The only corner case is when we have an odd number of
			 * pixels, but this can be handled pretty easily by not
			 * incrementing the paired pixel pointer in the
			 * direction it's odd in.
			 */
			if (j != (cvt->dst.fb->width - 1))
				pair_rgb24 += (dst_fmt->hsub - 1) * bpp;

			if (i != (cvt->dst.fb->height - 1))
				pair_rgb24 += rgb24_stride * (dst_fmt->vsub - 1);

			read_rgb(&pair_rgb, pair_rgb24);
			pair_yuv = igt_matrix_transform(&m, &pair_rgb);

			*u_tmp = clamp8((yuv.d[1] + pair_yuv.d[1]) / 2.0f);
			*v_tmp = clamp8((yuv.d[2] + pair_yuv.d[2]) / 2.0f);

			u_tmp += params.uv_inc;
			v_tmp += params.uv_inc;
		}

		rgb24 += rgb24_stride;
		y += params.ay_stride;

		if ((i % dst_fmt->vsub) == (dst_fmt->vsub - 1)) {
			u += params.uv_stride;
			v += params.uv_stride;
		}
	}
}

static void read_rgbf(struct igt_vec4 *rgb, const float *rgb24)
{
	rgb->d[0] = rgb24[0];
	rgb->d[1] = rgb24[1];
	rgb->d[2] = rgb24[2];
	rgb->d[3] = 1.0f;
}

static void write_rgbf(float *rgb24, const struct igt_vec4 *rgb)
{
	rgb24[0] = rgb->d[0];
	rgb24[1] = rgb->d[1];
	rgb24[2] = rgb->d[2];
}

static void convert_yuv16_to_float(struct fb_convert *cvt, bool alpha)
{
	const struct format_desc_struct *src_fmt =
		lookup_drm_format(cvt->src.fb->drm_format);
	int i, j;
	uint8_t fpp = alpha ? 4 : 3;
	uint16_t *a, *y, *u, *v;
	float *ptr = cvt->dst.ptr;
	unsigned int float_stride = cvt->dst.fb->strides[0] / sizeof(*ptr);
	struct igt_mat4 m = igt_ycbcr_to_rgb_matrix(cvt->src.fb->drm_format,
						    cvt->dst.fb->drm_format,
						    cvt->src.fb->color_encoding,
						    cvt->src.fb->color_range);
	uint16_t *buf;
	struct yuv_parameters params = { };

	igt_assert(cvt->dst.fb->drm_format == IGT_FORMAT_FLOAT &&
		   igt_format_is_yuv(cvt->src.fb->drm_format));

	buf = convert_src_get(cvt);
	get_yuv_parameters(cvt->src.fb, &params);
	igt_assert(!(params.y_offset % sizeof(*buf)) &&
		   !(params.u_offset % sizeof(*buf)) &&
		   !(params.v_offset % sizeof(*buf)));

	a = buf + params.a_offset / sizeof(*buf);
	y = buf + params.y_offset / sizeof(*buf);
	u = buf + params.u_offset / sizeof(*buf);
	v = buf + params.v_offset / sizeof(*buf);

	for (i = 0; i < cvt->dst.fb->height; i++) {
		const uint16_t *a_tmp = a;
		const uint16_t *y_tmp = y;
		const uint16_t *u_tmp = u;
		const uint16_t *v_tmp = v;
		float *rgb_tmp = ptr;

		for (j = 0; j < cvt->dst.fb->width; j++) {
			struct igt_vec4 rgb, yuv;

			yuv.d[0] = *y_tmp;
			yuv.d[1] = *u_tmp;
			yuv.d[2] = *v_tmp;
			yuv.d[3] = 1.0f;

			rgb = igt_matrix_transform(&m, &yuv);
			write_rgbf(rgb_tmp, &rgb);

			if (alpha) {
				rgb_tmp[3] = ((float)*a_tmp) / 65535.f;
				a_tmp += params.ay_inc;
			}

			rgb_tmp += fpp;
			y_tmp += params.ay_inc;

			if ((src_fmt->hsub == 1) || (j % src_fmt->hsub)) {
				u_tmp += params.uv_inc;
				v_tmp += params.uv_inc;
			}
		}

		ptr += float_stride;

		a += params.ay_stride / sizeof(*a);
		y += params.ay_stride / sizeof(*y);

		if ((src_fmt->vsub == 1) || (i % src_fmt->vsub)) {
			u += params.uv_stride / sizeof(*u);
			v += params.uv_stride / sizeof(*v);
		}
	}

	convert_src_put(cvt, buf);
}

static void convert_float_to_yuv16(struct fb_convert *cvt, bool alpha)
{
	const struct format_desc_struct *dst_fmt =
		lookup_drm_format(cvt->dst.fb->drm_format);
	int i, j;
	uint16_t *a, *y, *u, *v;
	const float *ptr = cvt->src.ptr;
	uint8_t fpp = alpha ? 4 : 3;
	unsigned float_stride = cvt->src.fb->strides[0] / sizeof(*ptr);
	struct igt_mat4 m = igt_rgb_to_ycbcr_matrix(cvt->src.fb->drm_format,
						    cvt->dst.fb->drm_format,
						    cvt->dst.fb->color_encoding,
						    cvt->dst.fb->color_range);
	struct yuv_parameters params = { };

	igt_assert(cvt->src.fb->drm_format == IGT_FORMAT_FLOAT &&
		   igt_format_is_yuv(cvt->dst.fb->drm_format));

	get_yuv_parameters(cvt->dst.fb, &params);
	igt_assert(!(params.a_offset % sizeof(*a)) &&
		   !(params.y_offset % sizeof(*y)) &&
		   !(params.u_offset % sizeof(*u)) &&
		   !(params.v_offset % sizeof(*v)));

	a = cvt->dst.ptr + params.a_offset;
	y = cvt->dst.ptr + params.y_offset;
	u = cvt->dst.ptr + params.u_offset;
	v = cvt->dst.ptr + params.v_offset;

	for (i = 0; i < cvt->dst.fb->height; i++) {
		const float *rgb_tmp = ptr;
		uint16_t *a_tmp = a;
		uint16_t *y_tmp = y;
		uint16_t *u_tmp = u;
		uint16_t *v_tmp = v;

		for (j = 0; j < cvt->dst.fb->width; j++) {
			const float *pair_float = rgb_tmp;
			struct igt_vec4 pair_rgb, rgb;
			struct igt_vec4 pair_yuv, yuv;

			read_rgbf(&rgb, rgb_tmp);
			yuv = igt_matrix_transform(&m, &rgb);

			if (alpha) {
				*a_tmp = rgb_tmp[3] * 65535.f + .5f;
				a_tmp += params.ay_inc;
			}

			rgb_tmp += fpp;

			*y_tmp = clamp16(yuv.d[0]);
			y_tmp += params.ay_inc;

			if ((i % dst_fmt->vsub) || (j % dst_fmt->hsub))
				continue;

			/*
			 * We assume the MPEG2 chroma siting convention, where
			 * pixel center for Cb'Cr' is between the left top and
			 * bottom pixel in a 2x2 block, so take the average.
			 *
			 * Therefore, if we use subsampling, we only really care
			 * about two pixels all the time, either the two
			 * subsequent pixels horizontally, vertically, or the
			 * two corners in a 2x2 block.
			 *
			 * The only corner case is when we have an odd number of
			 * pixels, but this can be handled pretty easily by not
			 * incrementing the paired pixel pointer in the
			 * direction it's odd in.
			 */
			if (j != (cvt->dst.fb->width - 1))
				pair_float += (dst_fmt->hsub - 1) * fpp;

			if (i != (cvt->dst.fb->height - 1))
				pair_float += float_stride * (dst_fmt->vsub - 1);

			read_rgbf(&pair_rgb, pair_float);
			pair_yuv = igt_matrix_transform(&m, &pair_rgb);

			*u_tmp = clamp16((yuv.d[1] + pair_yuv.d[1]) / 2.0f);
			*v_tmp = clamp16((yuv.d[2] + pair_yuv.d[2]) / 2.0f);

			u_tmp += params.uv_inc;
			v_tmp += params.uv_inc;
		}

		ptr += float_stride;
		a += params.ay_stride / sizeof(*a);
		y += params.ay_stride / sizeof(*y);

		if ((i % dst_fmt->vsub) == (dst_fmt->vsub - 1)) {
			u += params.uv_stride / sizeof(*u);
			v += params.uv_stride / sizeof(*v);
		}
	}
}

static void convert_Y410_to_float(struct fb_convert *cvt, bool alpha)
{
	int i, j;
	const uint32_t *uyv;
	uint32_t *buf;
	float *ptr = cvt->dst.ptr;
	unsigned int float_stride = cvt->dst.fb->strides[0] / sizeof(*ptr);
	unsigned int uyv_stride = cvt->src.fb->strides[0] / sizeof(*uyv);
	struct igt_mat4 m = igt_ycbcr_to_rgb_matrix(cvt->src.fb->drm_format,
						    cvt->dst.fb->drm_format,
						    cvt->src.fb->color_encoding,
						    cvt->src.fb->color_range);
	unsigned bpp = alpha ? 4 : 3;

	igt_assert((cvt->src.fb->drm_format == DRM_FORMAT_Y410 ||
		    cvt->src.fb->drm_format == DRM_FORMAT_XVYU2101010) &&
		   cvt->dst.fb->drm_format == IGT_FORMAT_FLOAT);

	uyv = buf = convert_src_get(cvt);

	for (i = 0; i < cvt->dst.fb->height; i++) {
		for (j = 0; j < cvt->dst.fb->width; j++) {
			/* Convert 2x1 pixel blocks */
			struct igt_vec4 yuv;
			struct igt_vec4 rgb;

			yuv.d[0] = (uyv[j] >> 10) & 0x3ff;
			yuv.d[1] = uyv[j] & 0x3ff;
			yuv.d[2] = (uyv[j] >> 20) & 0x3ff;
			yuv.d[3] = 1.f;

			rgb = igt_matrix_transform(&m, &yuv);

			write_rgbf(&ptr[j * bpp], &rgb);
			if (alpha)
				ptr[j * bpp + 3] = (float)(uyv[j] >> 30) / 3.f;
		}

		ptr += float_stride;
		uyv += uyv_stride;
	}

	convert_src_put(cvt, buf);
}

static void convert_float_to_Y410(struct fb_convert *cvt, bool alpha)
{
	int i, j;
	uint32_t *uyv = cvt->dst.ptr;
	const float *ptr = cvt->src.ptr;
	unsigned float_stride = cvt->src.fb->strides[0] / sizeof(*ptr);
	unsigned uyv_stride = cvt->dst.fb->strides[0] / sizeof(*uyv);
	struct igt_mat4 m = igt_rgb_to_ycbcr_matrix(cvt->src.fb->drm_format,
						    cvt->dst.fb->drm_format,
						    cvt->dst.fb->color_encoding,
						    cvt->dst.fb->color_range);
	unsigned bpp = alpha ? 4 : 3;

	igt_assert(cvt->src.fb->drm_format == IGT_FORMAT_FLOAT &&
		   (cvt->dst.fb->drm_format == DRM_FORMAT_Y410 ||
		    cvt->dst.fb->drm_format == DRM_FORMAT_XVYU2101010));

	for (i = 0; i < cvt->dst.fb->height; i++) {
		for (j = 0; j < cvt->dst.fb->width; j++) {
			struct igt_vec4 rgb;
			struct igt_vec4 yuv;
			uint8_t a = 0;
			uint16_t y, cb, cr;

			read_rgbf(&rgb, &ptr[j * bpp]);
			if (alpha)
				 a = ptr[j * bpp + 3] * 3.f + .5f;

			yuv = igt_matrix_transform(&m, &rgb);
			y = yuv.d[0];
			cb = yuv.d[1];
			cr = yuv.d[2];

			uyv[j] = ((cb & 0x3ff) << 0) |
				  ((y & 0x3ff) << 10) |
				  ((cr & 0x3ff) << 20) |
				  (a << 30);
		}

		ptr += float_stride;
		uyv += uyv_stride;
	}
}

/* { R, G, B, X } */
static const unsigned char swizzle_rgbx[] = { 0, 1, 2, 3 };
static const unsigned char swizzle_bgrx[] = { 2, 1, 0, 3 };

static const unsigned char *rgbx_swizzle(uint32_t format)
{
	switch (format) {
	default:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_XRGB16161616:
	case DRM_FORMAT_ARGB16161616:
		return swizzle_bgrx;
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
	case DRM_FORMAT_XBGR16161616:
	case DRM_FORMAT_ABGR16161616:
		return swizzle_rgbx;
	}
}

static void convert_fp16_to_float(struct fb_convert *cvt)
{
	int i, j;
	uint16_t *fp16;
	float *ptr = cvt->dst.ptr;
	unsigned int float_stride = cvt->dst.fb->strides[0] / sizeof(*ptr);
	unsigned int fp16_stride = cvt->src.fb->strides[0] / sizeof(*fp16);
	const unsigned char *swz = rgbx_swizzle(cvt->src.fb->drm_format);
	bool needs_reswizzle = swz != swizzle_rgbx;

	uint16_t *buf = convert_src_get(cvt);
	fp16 = buf + cvt->src.fb->offsets[0] / sizeof(*buf);

	for (i = 0; i < cvt->dst.fb->height; i++) {
		if (needs_reswizzle) {
			const uint16_t *fp16_tmp = fp16;
			float *rgb_tmp = ptr;

			for (j = 0; j < cvt->dst.fb->width; j++) {
				struct igt_vec4 rgb;

				igt_half_to_float(fp16_tmp, rgb.d, 4);

				rgb_tmp[0] = rgb.d[swz[0]];
				rgb_tmp[1] = rgb.d[swz[1]];
				rgb_tmp[2] = rgb.d[swz[2]];
				rgb_tmp[3] = rgb.d[swz[3]];

				rgb_tmp += 4;
				fp16_tmp += 4;
			}
		} else {
			igt_half_to_float(fp16, ptr, cvt->dst.fb->width * 4);
		}

		ptr += float_stride;
		fp16 += fp16_stride;
	}

	convert_src_put(cvt, buf);
}

static void convert_float_to_fp16(struct fb_convert *cvt)
{
	int i, j;
	uint16_t *fp16 = cvt->dst.ptr + cvt->dst.fb->offsets[0];
	const float *ptr = cvt->src.ptr;
	unsigned float_stride = cvt->src.fb->strides[0] / sizeof(*ptr);
	unsigned fp16_stride = cvt->dst.fb->strides[0] / sizeof(*fp16);
	const unsigned char *swz = rgbx_swizzle(cvt->dst.fb->drm_format);
	bool needs_reswizzle = swz != swizzle_rgbx;

	for (i = 0; i < cvt->dst.fb->height; i++) {
		if (needs_reswizzle) {
			const float *rgb_tmp = ptr;
			uint16_t *fp16_tmp = fp16;

			for (j = 0; j < cvt->dst.fb->width; j++) {
				struct igt_vec4 rgb;

				rgb.d[0] = rgb_tmp[swz[0]];
				rgb.d[1] = rgb_tmp[swz[1]];
				rgb.d[2] = rgb_tmp[swz[2]];
				rgb.d[3] = rgb_tmp[swz[3]];

				igt_float_to_half(rgb.d, fp16_tmp, 4);

				rgb_tmp += 4;
				fp16_tmp += 4;
			}
		} else {
			igt_float_to_half(ptr, fp16, cvt->dst.fb->width * 4);
		}

		ptr += float_stride;
		fp16 += fp16_stride;
	}
}

static void float_to_uint16(const float *f, uint16_t *h, unsigned int num)
{
	for (int i = 0; i < num; i++)
		h[i] = f[i] * 65535.0f + 0.5f;
}

static void uint16_to_float(const uint16_t *h, float *f, unsigned int num)
{
	for (int i = 0; i < num; i++)
		f[i] = ((float) h[i]) / 65535.0f;
}

static void convert_uint16_to_float(struct fb_convert *cvt)
{
	int i, j;
	uint16_t *up16;
	float *ptr = cvt->dst.ptr;
	unsigned int float_stride = cvt->dst.fb->strides[0] / sizeof(*ptr);
	unsigned int up16_stride = cvt->src.fb->strides[0] / sizeof(*up16);
	const unsigned char *swz = rgbx_swizzle(cvt->src.fb->drm_format);
	bool needs_reswizzle = swz != swizzle_rgbx;

	uint16_t *buf = convert_src_get(cvt);
	up16 = buf + cvt->src.fb->offsets[0] / sizeof(*buf);

	for (i = 0; i < cvt->dst.fb->height; i++) {
		if (needs_reswizzle) {
			const uint16_t *u16_tmp = up16;
			float *rgb_tmp = ptr;

			for (j = 0; j < cvt->dst.fb->width; j++) {
				struct igt_vec4 rgb;

				uint16_to_float(u16_tmp, rgb.d, 4);

				rgb_tmp[0] = rgb.d[swz[0]];
				rgb_tmp[1] = rgb.d[swz[1]];
				rgb_tmp[2] = rgb.d[swz[2]];
				rgb_tmp[3] = rgb.d[swz[3]];

				rgb_tmp += 4;
				u16_tmp += 4;
			}
		} else {
			uint16_to_float(up16, ptr, cvt->dst.fb->width * 4);
		}

		ptr += float_stride;
		up16 += up16_stride;
	}

	convert_src_put(cvt, buf);
}

static void convert_float_to_uint16(struct fb_convert *cvt)
{
	int i, j;
	uint16_t *up16 = cvt->dst.ptr + cvt->dst.fb->offsets[0];
	const float *ptr = cvt->src.ptr;
	unsigned float_stride = cvt->src.fb->strides[0] / sizeof(*ptr);
	unsigned up16_stride = cvt->dst.fb->strides[0] / sizeof(*up16);
	const unsigned char *swz = rgbx_swizzle(cvt->dst.fb->drm_format);
	bool needs_reswizzle = swz != swizzle_rgbx;

	for (i = 0; i < cvt->dst.fb->height; i++) {
		if (needs_reswizzle) {
			const float *rgb_tmp = ptr;
			uint16_t *u16_tmp = up16;

			for (j = 0; j < cvt->dst.fb->width; j++) {
				struct igt_vec4 rgb;

				rgb.d[0] = rgb_tmp[swz[0]];
				rgb.d[1] = rgb_tmp[swz[1]];
				rgb.d[2] = rgb_tmp[swz[2]];
				rgb.d[3] = rgb_tmp[swz[3]];

				float_to_uint16(rgb.d, u16_tmp, 4);

				rgb_tmp += 4;
				u16_tmp += 4;
			}
		} else {
			float_to_uint16(ptr, up16, cvt->dst.fb->width * 4);
		}

		ptr += float_stride;
		up16 += up16_stride;
	}
}

static void convert_pixman(struct fb_convert *cvt)
{
	pixman_format_code_t src_pixman = drm_format_to_pixman(cvt->src.fb->drm_format);
	pixman_format_code_t dst_pixman = drm_format_to_pixman(cvt->dst.fb->drm_format);
	pixman_image_t *dst_image, *src_image;
	void *src_ptr;

	igt_assert((src_pixman != PIXMAN_invalid) &&
		   (dst_pixman != PIXMAN_invalid));

	/* Pixman requires the stride to be aligned to 32 bits. */
	igt_assert((cvt->src.fb->strides[0] % sizeof(uint32_t)) == 0);
	igt_assert((cvt->dst.fb->strides[0] % sizeof(uint32_t)) == 0);

	src_ptr = convert_src_get(cvt);

	src_image = pixman_image_create_bits(src_pixman,
					     cvt->src.fb->width,
					     cvt->src.fb->height,
					     src_ptr,
					     cvt->src.fb->strides[0]);
	igt_assert(src_image);

	dst_image = pixman_image_create_bits(dst_pixman,
					     cvt->dst.fb->width,
					     cvt->dst.fb->height,
					     cvt->dst.ptr,
					     cvt->dst.fb->strides[0]);
	igt_assert(dst_image);

	pixman_image_composite(PIXMAN_OP_SRC, src_image, NULL, dst_image,
			       0, 0, 0, 0, 0, 0,
			       cvt->dst.fb->width, cvt->dst.fb->height);
	pixman_image_unref(dst_image);
	pixman_image_unref(src_image);

	convert_src_put(cvt, src_ptr);
}

static void fb_convert(struct fb_convert *cvt)
{
	if ((drm_format_to_pixman(cvt->src.fb->drm_format) != PIXMAN_invalid) &&
	    (drm_format_to_pixman(cvt->dst.fb->drm_format) != PIXMAN_invalid)) {
		convert_pixman(cvt);
		return;
	} else if (cvt->dst.fb->drm_format == DRM_FORMAT_XRGB8888) {
		switch (cvt->src.fb->drm_format) {
		case DRM_FORMAT_XYUV8888:
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_NV16:
		case DRM_FORMAT_NV21:
		case DRM_FORMAT_NV61:
		case DRM_FORMAT_UYVY:
		case DRM_FORMAT_VYUY:
		case DRM_FORMAT_YUV420:
		case DRM_FORMAT_YUV422:
		case DRM_FORMAT_YUYV:
		case DRM_FORMAT_YVU420:
		case DRM_FORMAT_YVU422:
		case DRM_FORMAT_YVYU:
			convert_yuv_to_rgb24(cvt);
			return;
		}
	} else if (cvt->src.fb->drm_format == DRM_FORMAT_XRGB8888) {
		switch (cvt->dst.fb->drm_format) {
		case DRM_FORMAT_XYUV8888:
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_NV16:
		case DRM_FORMAT_NV21:
		case DRM_FORMAT_NV61:
		case DRM_FORMAT_UYVY:
		case DRM_FORMAT_VYUY:
		case DRM_FORMAT_YUV420:
		case DRM_FORMAT_YUV422:
		case DRM_FORMAT_YUYV:
		case DRM_FORMAT_YVU420:
		case DRM_FORMAT_YVU422:
		case DRM_FORMAT_YVYU:
			convert_rgb24_to_yuv(cvt);
			return;
		}
	} else if (cvt->dst.fb->drm_format == IGT_FORMAT_FLOAT) {
		switch (cvt->src.fb->drm_format) {
		case DRM_FORMAT_P010:
		case DRM_FORMAT_P012:
		case DRM_FORMAT_P016:
		case DRM_FORMAT_Y210:
		case DRM_FORMAT_Y212:
		case DRM_FORMAT_Y216:
		case DRM_FORMAT_XVYU12_16161616:
		case DRM_FORMAT_XVYU16161616:
			convert_yuv16_to_float(cvt, false);
			return;
		case DRM_FORMAT_Y410:
			convert_Y410_to_float(cvt, true);
			return;
		case DRM_FORMAT_XVYU2101010:
			convert_Y410_to_float(cvt, false);
			return;
		case DRM_FORMAT_Y412:
		case DRM_FORMAT_Y416:
			convert_yuv16_to_float(cvt, true);
			return;
		case DRM_FORMAT_XRGB16161616F:
		case DRM_FORMAT_XBGR16161616F:
		case DRM_FORMAT_ARGB16161616F:
		case DRM_FORMAT_ABGR16161616F:
			convert_fp16_to_float(cvt);
			return;
		case DRM_FORMAT_XRGB16161616:
		case DRM_FORMAT_XBGR16161616:
		case DRM_FORMAT_ARGB16161616:
		case DRM_FORMAT_ABGR16161616:
			convert_uint16_to_float(cvt);
			return;
		}
	} else if (cvt->src.fb->drm_format == IGT_FORMAT_FLOAT) {
		switch (cvt->dst.fb->drm_format) {
		case DRM_FORMAT_P010:
		case DRM_FORMAT_P012:
		case DRM_FORMAT_P016:
		case DRM_FORMAT_Y210:
		case DRM_FORMAT_Y212:
		case DRM_FORMAT_Y216:
		case DRM_FORMAT_XVYU12_16161616:
		case DRM_FORMAT_XVYU16161616:
			convert_float_to_yuv16(cvt, false);
			return;
		case DRM_FORMAT_Y410:
			convert_float_to_Y410(cvt, true);
			return;
		case DRM_FORMAT_XVYU2101010:
			convert_float_to_Y410(cvt, false);
			return;
		case DRM_FORMAT_Y412:
		case DRM_FORMAT_Y416:
			convert_float_to_yuv16(cvt, true);
			return;
		case DRM_FORMAT_XRGB16161616F:
		case DRM_FORMAT_XBGR16161616F:
		case DRM_FORMAT_ARGB16161616F:
		case DRM_FORMAT_ABGR16161616F:
			convert_float_to_fp16(cvt);
			return;
		case DRM_FORMAT_XRGB16161616:
		case DRM_FORMAT_XBGR16161616:
		case DRM_FORMAT_ARGB16161616:
		case DRM_FORMAT_ABGR16161616:
			convert_float_to_uint16(cvt);
			return;
		}
	}

	igt_assert_f(false,
		     "Conversion not implemented (from format "
		     IGT_FORMAT_FMT " to " IGT_FORMAT_FMT ")\n",
		     IGT_FORMAT_ARGS(cvt->src.fb->drm_format),
		     IGT_FORMAT_ARGS(cvt->dst.fb->drm_format));
}

static void destroy_cairo_surface__convert(void *arg)
{
	struct fb_convert_blit_upload *blit = arg;
	struct igt_fb *fb = blit->base.fb;
	struct fb_convert cvt = {
		.dst	= {
			.ptr	= blit->base.linear.map,
			.fb	= &blit->base.linear.fb,
		},

		.src	= {
			.ptr	= blit->shadow_ptr,
			.fb	= &blit->shadow_fb,
		},
	};

	fb_convert(&cvt);
	igt_fb_destroy_cairo_shadow_buffer(&blit->shadow_fb, blit->shadow_ptr);

	if (blit->base.linear.fb.gem_handle)
		free_linear_mapping(&blit->base);
	else
		unmap_bo(fb, blit->base.linear.map);

	free(blit);

	fb->cairo_surface = NULL;
}

static void create_cairo_surface__convert(int fd, struct igt_fb *fb)
{
	struct fb_convert_blit_upload *blit = calloc(1, sizeof(*blit));
	struct fb_convert cvt = { };
	const struct format_desc_struct *f = lookup_drm_format(fb->drm_format);
	unsigned drm_format = cairo_format_to_drm_format(f->cairo_id);

	igt_assert(blit);

	blit->base.fd = fd;
	blit->base.fb = fb;

	blit->shadow_ptr = igt_fb_create_cairo_shadow_buffer(fd, drm_format,
							     fb->width,
							     fb->height,
							     &blit->shadow_fb);
	igt_assert(blit->shadow_ptr);

	/* Note for nouveau, it's currently faster to copy fbs to/from vram (even linear ones) */
	if (use_enginecopy(fb) || use_blitter(fb) || igt_vc4_is_tiled(fb->modifier) ||
	    is_nouveau_device(fd)) {
		setup_linear_mapping(&blit->base);

		/* speed things up by working from a copy in system memory */
		cvt.src.slow_reads = (is_i915_device(fd) && !gem_has_mappable_ggtt(fd)) ||
			is_xe_device(fd);
	} else {
		blit->base.linear.fb = *fb;
		blit->base.linear.fb.gem_handle = 0;
		blit->base.linear.map = map_bo(fd, fb);
		igt_assert(blit->base.linear.map);

		/* reading via gtt mmap is slow */
		cvt.src.slow_reads = is_intel_device(fd);
	}

	cvt.dst.ptr = blit->shadow_ptr;
	cvt.dst.fb = &blit->shadow_fb;
	cvt.src.ptr = blit->base.linear.map;
	cvt.src.fb = &blit->base.linear.fb;
	fb_convert(&cvt);

	fb->cairo_surface =
		cairo_image_surface_create_for_data(blit->shadow_ptr,
						    f->cairo_id,
						    fb->width, fb->height,
						    blit->shadow_fb.strides[0]);

	cairo_surface_set_user_data(fb->cairo_surface,
				    (cairo_user_data_key_t *)create_cairo_surface__convert,
				    blit, destroy_cairo_surface__convert);
}


/**
 * igt_fb_map_buffer:
 * @fd: open drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * This function will creating a new mapping of the buffer and return a pointer
 * to the content of the supplied framebuffer's plane. This mapping needs to be
 * deleted using igt_fb_unmap_buffer().
 *
 * Returns:
 * A pointer to a buffer with the contents of the framebuffer
 */
void *igt_fb_map_buffer(int fd, struct igt_fb *fb)
{
	return map_bo(fd, fb);
}

/**
 * igt_fb_unmap_buffer:
 * @fb: pointer to the backing igt_fb structure
 * @buffer: pointer to the buffer previously mappped
 *
 * This function will unmap a buffer mapped previously with
 * igt_fb_map_buffer().
 */
void igt_fb_unmap_buffer(struct igt_fb *fb, void *buffer)
{
	return unmap_bo(fb, buffer);
}

static bool use_convert(const struct igt_fb *fb)
{
	const struct format_desc_struct *f = lookup_drm_format(fb->drm_format);

	return f->convert;
}

/**
 * igt_get_cairo_surface:
 * @fd: open drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * This function stores the contents of the supplied framebuffer's plane
 * into a cairo surface and returns it.
 *
 * Returns:
 * A pointer to a cairo surface with the contents of the framebuffer.
 */
cairo_surface_t *igt_get_cairo_surface(int fd, struct igt_fb *fb)
{
	if (fb->cairo_surface == NULL) {
		if (use_convert(fb))
			create_cairo_surface__convert(fd, fb);
		else if (use_blitter(fb) || use_enginecopy(fb) ||
			 igt_vc4_is_tiled(fb->modifier) ||
			 igt_amd_is_tiled(fb->modifier) ||
			 is_nouveau_device(fb->fd))
			create_cairo_surface__gpu(fd, fb);
		else
			create_cairo_surface__gtt(fd, fb);
	}

	igt_assert(cairo_surface_status(fb->cairo_surface) == CAIRO_STATUS_SUCCESS);
	return fb->cairo_surface;
}

/**
 * igt_get_cairo_ctx:
 * @fd: open drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * This initializes a cairo surface for @fb and then allocates a drawing context
 * for it. The return cairo drawing context should be released by calling
 * igt_put_cairo_ctx(). This also sets a default font for drawing text on
 * framebuffers.
 *
 * Returns:
 * The created cairo drawing context.
 */
cairo_t *igt_get_cairo_ctx(int fd, struct igt_fb *fb)
{
	cairo_surface_t *surface;
	cairo_t *cr;

	surface = igt_get_cairo_surface(fd, fb);
	cr = cairo_create(surface);
	cairo_surface_destroy(surface);
	igt_assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);

	cairo_select_font_face(cr, "Helvetica", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	igt_assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);

	return cr;
}

/**
 * igt_put_cairo_ctx:
 * @cr: the cairo context returned by igt_get_cairo_ctx.
 *
 * This releases the cairo surface @cr returned by igt_get_cairo_ctx()
 * for fb, and writes the changes out to the framebuffer if cairo doesn't
 * have native support for the format.
 */
void igt_put_cairo_ctx(cairo_t *cr)
{
	cairo_status_t ret = cairo_status(cr);
	igt_assert_f(ret == CAIRO_STATUS_SUCCESS, "Cairo failed to draw with %s\n", cairo_status_to_string(ret));

	cairo_destroy(cr);
}

/**
 * igt_remove_fb:
 * @fd: open drm file descriptor
 * @fb: pointer to an #igt_fb structure
 *
 * This function releases all resources allocated in igt_create_fb() for @fb.
 * Note that if this framebuffer is still in use on a primary plane the kernel
 * will disable the corresponding crtc.
 */
void igt_remove_fb(int fd, struct igt_fb *fb)
{
	if (!fb || !fb->fb_id)
		return;

	cairo_surface_destroy(fb->cairo_surface);
	do_or_die(drmModeRmFB(fd, fb->fb_id));
	if (fb->is_dumb)
		kmstest_dumb_destroy(fd, fb->gem_handle);
	else if (is_nouveau_device(fd))
		igt_nouveau_delete_bo(fb);
	else
		gem_close(fd, fb->gem_handle);
	fb->fb_id = 0;
}

/**
 * igt_fb_convert_with_stride:
 * @dst: pointer to the #igt_fb structure that will store the conversion result
 * @src: pointer to the #igt_fb structure that stores the frame we convert
 * @dst_fourcc: DRM format specifier to convert to
 * @dst_modifier: DRM format modifier to convert to
 * @dst_stride: Stride for the resulting framebuffer (0 for automatic stride)
 *
 * This will convert a given @src content to the @dst_fourcc format,
 * storing the result in the @dst fb, allocating the @dst fb
 * underlying buffer with a stride of @dst_stride stride.
 *
 * Once done with @dst, the caller will have to call igt_remove_fb()
 * on it to free the associated resources.
 *
 * Returns:
 * The kms id of the created framebuffer.
 */
unsigned int igt_fb_convert_with_stride(struct igt_fb *dst, struct igt_fb *src,
					uint32_t dst_fourcc,
					uint64_t dst_modifier,
					unsigned int dst_stride)
{
	/* Use the cairo api to convert */
	cairo_surface_t *surf = igt_get_cairo_surface(src->fd, src);
	cairo_t *cr;
	int fb_id;

	fb_id = igt_create_fb_with_bo_size(src->fd, src->width,
					   src->height, dst_fourcc,
					   dst_modifier,
					   IGT_COLOR_YCBCR_BT709,
					   IGT_COLOR_YCBCR_LIMITED_RANGE,
					   dst, 0,
					   dst_stride);
	igt_assert(fb_id > 0);

	cr = igt_get_cairo_ctx(dst->fd, dst);
	cairo_set_source_surface(cr, surf, 0, 0);
	cairo_paint(cr);
	igt_put_cairo_ctx(cr);

	cairo_surface_destroy(surf);

	return fb_id;
}

/**
 * igt_fb_convert:
 * @dst: pointer to the #igt_fb structure that will store the conversion result
 * @src: pointer to the #igt_fb structure that stores the frame we convert
 * @dst_fourcc: DRM format specifier to convert to
 * @dst_modifier: DRM format modifier to convert to
 *
 * This will convert a given @src content to the @dst_fourcc format,
 * storing the result in the @dst fb, allocating the @dst fb
 * underlying buffer.
 *
 * Once done with @dst, the caller will have to call igt_remove_fb()
 * on it to free the associated resources.
 *
 * Returns:
 * The kms id of the created framebuffer.
 */
unsigned int igt_fb_convert(struct igt_fb *dst, struct igt_fb *src,
			    uint32_t dst_fourcc, uint64_t dst_modifier)
{
	return igt_fb_convert_with_stride(dst, src, dst_fourcc, dst_modifier,
					  0);
}

/**
 * igt_bpp_depth_to_drm_format:
 * @bpp: desired bits per pixel
 * @depth: desired depth
 *
 * Returns:
 * The rgb drm fourcc pixel format code corresponding to the given @bpp and
 * @depth values.  Fails hard if no match was found.
 */
uint32_t igt_bpp_depth_to_drm_format(int bpp, int depth)
{
	const struct format_desc_struct *f;

	for_each_format(f)
		if (f->plane_bpp[0] == bpp && f->depth == depth)
			return f->drm_id;


	igt_assert_f(0, "can't find drm format with bpp=%d, depth=%d\n", bpp,
		     depth);
}

/**
 * igt_drm_format_to_bpp:
 * @drm_format: drm fourcc pixel format code
 *
 * Returns:
 * The bits per pixel for the given drm fourcc pixel format code. Fails hard if
 * no match was found.
 */
uint32_t igt_drm_format_to_bpp(uint32_t drm_format)
{
	const struct format_desc_struct *f = lookup_drm_format(drm_format);

	igt_assert_f(f, "can't find a bpp format for %08x (%s)\n",
		     drm_format, igt_format_str(drm_format));

	return f->plane_bpp[0];
}

/**
 * igt_format_str:
 * @drm_format: drm fourcc pixel format code
 *
 * Returns:
 * Human-readable fourcc pixel format code for @drm_format or "invalid" no match
 * was found.
 */
const char *igt_format_str(uint32_t drm_format)
{
	const struct format_desc_struct *f = lookup_drm_format(drm_format);

	return f ? f->name : "invalid";
}

/**
 * igt_drm_format_str_to_format:
 * @drm_format: name string of drm_format in format_desc[] table
 *
 * Returns:
 * The drm_id for the format string from the format_desc[] table.
 */
uint32_t igt_drm_format_str_to_format(const char *drm_format)
{
	const struct format_desc_struct *f = lookup_drm_format_str(drm_format);

	igt_assert_f(f, "can't find a DRM format for (%s)\n", drm_format);

	return f->drm_id;
}

/**
 * igt_fb_supported_format:
 * @drm_format: drm fourcc to test.
 *
 * This functions returns whether @drm_format can be succesfully created by
 * igt_create_fb() and drawn to by igt_get_cairo_ctx().
 */
bool igt_fb_supported_format(uint32_t drm_format)
{
	const struct format_desc_struct *f;

	/*
	 * C8 needs a LUT which (at least for the time being)
	 * is the responsibility of each test. Not all tests
	 * have the required code so let's keep C8 hidden from
	 * most eyes.
	 */
	if (drm_format == DRM_FORMAT_C8)
		return false;

	f = lookup_drm_format(drm_format);
	if (!f)
		return false;

	if ((f->cairo_id == CAIRO_FORMAT_RGB96F ||
	     f->cairo_id == CAIRO_FORMAT_RGBA128F) &&
	    cairo_version() < CAIRO_VERSION_ENCODE(1, 17, 2)) {
		igt_info("Cairo version too old for " IGT_FORMAT_FMT ", need 1.17.2, have %s\n",
			 IGT_FORMAT_ARGS(drm_format), cairo_version_string());
		return false;
	}

	if (f->pixman_id == PIXMAN_rgba_float &&
	    pixman_version() < PIXMAN_VERSION_ENCODE(0, 36, 0)) {
		igt_info("Pixman version too old for " IGT_FORMAT_FMT ", need 0.36.0, have %s\n",
			 IGT_FORMAT_ARGS(drm_format), pixman_version_string());
		return false;
	}

	return true;
}

/*
 * This implements the FNV-1a hashing algorithm instead of CRC, for
 * simplicity
 * http://www.isthe.com/chongo/tech/comp/fnv/index.html
 *
 * hash = offset_basis
 * for each octet_of_data to be hashed
 *         hash = hash xor octet_of_data
 *         hash = hash * FNV_prime
 * return hash
 *
 * 32 bit offset_basis = 2166136261
 * 32 bit FNV_prime = 224 + 28 + 0x93 = 16777619
 */
int igt_fb_get_fnv1a_crc(struct igt_fb *fb, igt_crc_t *crc)
{
	const uint32_t FNV1a_OFFSET_BIAS = 2166136261;
	const uint32_t FNV1a_PRIME = 16777619;
	uint32_t *line = NULL;
	uint32_t hash;
	void *map;
	char *ptr;
	int x, y, cpp = igt_drm_format_to_bpp(fb->drm_format) / 8;
	uint32_t stride = fb->strides[0];

	if (fb->num_planes != 1)
		return -EINVAL;

	if (fb->drm_format != DRM_FORMAT_XRGB8888 && fb->drm_format != DRM_FORMAT_XRGB2101010)
		return -EINVAL;

	ptr = igt_fb_map_buffer(fb->fd, fb);
	igt_assert(ptr);
	map = ptr;

	/*
	 * Framebuffers are often uncached, which can make byte-wise accesses
	 * very slow. We copy each line of the FB into a local buffer to speed
	 * up the hashing.
	 */
	line = malloc(stride);
	if (!line) {
		munmap(map, fb->size);
		return -ENOMEM;
	}

	hash = FNV1a_OFFSET_BIAS;

	for (y = 0; y < fb->height; y++, ptr += stride) {

		igt_memcpy_from_wc(line, ptr, fb->width * cpp);

		for (x = 0; x < fb->width; x++) {
			uint32_t pixel = le32_to_cpu(line[x]);

			if (fb->drm_format == DRM_FORMAT_XRGB8888)
				pixel &= 0x00ffffff;
			else if (fb->drm_format == DRM_FORMAT_XRGB2101010)
				pixel &= 0x3fffffff;

			hash ^= pixel;
			hash *= FNV1a_PRIME;
		}
	}

	crc->n_words = 1;
	crc->crc[0] = hash;

	free(line);
	igt_fb_unmap_buffer(fb, map);

	return 0;
}

/**
 * igt_format_is_yuv:
 * @drm_format: drm fourcc
 *
 * This functions returns whether @drm_format is YUV (as opposed to RGB).
 */
bool igt_format_is_yuv(uint32_t drm_format)
{
	switch (drm_format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
	case DRM_FORMAT_XVYU2101010:
	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
	case DRM_FORMAT_Y410:
	case DRM_FORMAT_Y412:
	case DRM_FORMAT_Y416:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_XYUV8888:
		return true;
	default:
		return false;
	}
}

/**
 * igt_format_is_fp16
 * @drm_format: drm fourcc
 *
 * Check if the format is fp16.
 */
bool igt_format_is_fp16(uint32_t drm_format)
{
	switch (drm_format) {
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		return true;
	default:
		return false;
	}
}

/**
 * igt_format_plane_bpp:
 * @drm_format: drm fourcc
 * @plane: format plane index
 *
 * This functions returns the number of bits per pixel for the given @plane
 * index of the @drm_format.
 */
int igt_format_plane_bpp(uint32_t drm_format, int plane)
{
	const struct format_desc_struct *format =
		lookup_drm_format(drm_format);

	return format->plane_bpp[plane];
}

/**
 * igt_format_array_fill:
 * @formats_array: a pointer to the formats array pointer to be allocated
 * @count: a pointer to the number of elements contained in the allocated array
 * @allow_yuv: a boolean indicating whether YUV formats should be included
 *
 * This functions allocates and fills a @formats_array that lists the DRM
 * formats current available.
 */
void igt_format_array_fill(uint32_t **formats_array, unsigned int *count,
			   bool allow_yuv)
{
	const struct format_desc_struct *format;
	unsigned int index = 0;

	*count = 0;

	for_each_format(format) {
		if (!allow_yuv && igt_format_is_yuv(format->drm_id))
			continue;

		(*count)++;
	}

	*formats_array = calloc(*count, sizeof(uint32_t));
	igt_assert(*formats_array);

	for_each_format(format) {
		if (!allow_yuv && igt_format_is_yuv(format->drm_id))
			continue;

		(*formats_array)[index++] = format->drm_id;
	}
}

const char *igt_fb_modifier_name(uint64_t modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		return "linear";
	case I915_FORMAT_MOD_X_TILED:
		return "x";
	case I915_FORMAT_MOD_Y_TILED:
		return "y";
	case I915_FORMAT_MOD_Yf_TILED:
		return "yf";
	case I915_FORMAT_MOD_Y_TILED_CCS:
		return "y-ccs";
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		return "yf-ccs";
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
		return "y-rc-ccs";
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
		return "y-rc-ccs-cc";
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
		return "y-mc-ccs";
	case I915_FORMAT_MOD_4_TILED:
		return "4";
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_BMG_CCS:
	case I915_FORMAT_MOD_4_TILED_LNL_CCS:
		return "4-rc-ccs";
	case I915_FORMAT_MOD_4_TILED_MTL_MC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
		return "4-mc-ccs";
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
		return "4-rc-ccs-cc";
	default:
		return "unknown";
	}
}
