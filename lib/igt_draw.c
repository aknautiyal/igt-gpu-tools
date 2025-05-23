/*
 * Copyright © 2015 Intel Corporation
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

#include <sys/mman.h>

#include "igt_draw.h"

#include "drmtest.h"
#include "intel_bufops.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "intel_mocs.h"
#include "intel_pat.h"
#include "igt_core.h"
#include "igt_fb.h"
#include "ioctl_wrappers.h"
#include "i830_reg.h"
#include "i915/gem_create.h"
#include "i915/gem_mman.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#ifndef PAGE_ALIGN
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define PAGE_ALIGN(x) ALIGN(x, PAGE_SIZE)
#endif

/**
 * SECTION:igt_draw
 * @short_description: drawing helpers for tests
 * @title: Draw
 * @include: igt.h
 *
 * This library contains some functions for drawing rectangles on buffers using
 * the many different drawing methods we have. It also contains some wrappers
 * that make the process easier if you have the abstract objects in hand.
 *
 * This library only claims support for some pixel formats, but adding support
 * for more formats should be faily easy now that we support both 16bpp and
 * 32bpp. If you need a new pixel format, make sure you update both this file
 * and tests/kms_draw_crc.c.
 */

/* Some internal data structures to avoid having to pass tons of parameters
 * around everything. */
struct cmd_data {
	struct buf_ops *bops;
	uint32_t ctx;
};

struct buf_data {
	uint32_t handle;
	uint32_t size;
	uint32_t stride;
	int width;
	int height;
	int bpp;
	uint8_t pat_index;
};

struct rect {
	int x;
	int y;
	int w;
	int h;
};

/**
 * igt_draw_get_method_name:
 * @method: draw method
 *
 * Simple function to transform the enum into a string. Useful when naming
 * subtests and printing debug messages.
 */
const char *igt_draw_get_method_name(enum igt_draw_method method)
{
	switch (method) {
	case IGT_DRAW_MMAP_CPU:
		return "mmap-cpu";
	case IGT_DRAW_MMAP_GTT:
		return "mmap-gtt";
	case IGT_DRAW_MMAP_WC:
		return "mmap-wc";
	case IGT_DRAW_PWRITE:
		return "pwrite";
	case IGT_DRAW_BLT:
		return "blt";
	case IGT_DRAW_RENDER:
		return "render";
	default:
		igt_assert(false);
	}
}

static unsigned long swizzle_bit(unsigned int bit, unsigned long offset)
{
	return (offset & (1ul << bit)) >> (bit - 6);
}

static int swizzle_addr(unsigned long addr, int swizzle)
{
	switch (swizzle) {
	case I915_BIT_6_SWIZZLE_NONE:
		return addr;
	case I915_BIT_6_SWIZZLE_9:
		return addr ^ swizzle_bit(9, addr);
	case I915_BIT_6_SWIZZLE_9_10:
		return addr ^ swizzle_bit(9, addr) ^ swizzle_bit(10, addr);
	case I915_BIT_6_SWIZZLE_9_11:
		return addr ^ swizzle_bit(9, addr) ^ swizzle_bit(11, addr);
	case I915_BIT_6_SWIZZLE_9_10_11:
		return (addr ^
			swizzle_bit(9, addr) ^
			swizzle_bit(10, addr) ^
			swizzle_bit(11, addr));

	case I915_BIT_6_SWIZZLE_UNKNOWN:
	case I915_BIT_6_SWIZZLE_9_17:
	case I915_BIT_6_SWIZZLE_9_10_17:
	default:
		/* If we hit this case, we need to implement support for the
		 * appropriate swizzling method. */
		igt_require(false);
		return addr;
	}
}

static int tile(int x, int y, uint32_t x_tile_size, uint32_t y_tile_size,
		uint32_t line_size, bool xmajor)
{
	int tile_size, tiles_per_line, x_tile_n, y_tile_n, tile_off, pos;
	int tile_n, x_tile_off, y_tile_off;

	tiles_per_line = line_size / x_tile_size;
	tile_size = x_tile_size * y_tile_size;

	x_tile_n = x / x_tile_size;
	y_tile_n = y / y_tile_size;
	tile_n = y_tile_n * tiles_per_line + x_tile_n;

	x_tile_off = x % x_tile_size;
	y_tile_off = y % y_tile_size;

	if (xmajor)
		tile_off = y_tile_off * x_tile_size + x_tile_off;
	else
		tile_off = x_tile_off * y_tile_size + y_tile_off;

	pos = tile_n * tile_size + tile_off;

	return pos;
}

static void untile(int tiled_pos, int x_tile_size, int y_tile_size,
		   uint32_t line_size, bool xmajor, int *x, int *y)
{
	int tile_n, tile_off, tiles_per_line;
	int x_tile_off, y_tile_off;
	int x_tile_n, y_tile_n;
	int tile_size;

	tile_size = x_tile_size * y_tile_size;
	tiles_per_line = line_size / x_tile_size;

	tile_n = tiled_pos / tile_size;
	tile_off = tiled_pos % tile_size;

	if (xmajor) {
		y_tile_off = tile_off / x_tile_size;
		x_tile_off = tile_off % x_tile_size;
	} else {
		y_tile_off = tile_off % y_tile_size;
		x_tile_off = tile_off / y_tile_size;
	}

	x_tile_n = tile_n % tiles_per_line;
	y_tile_n = tile_n / tiles_per_line;

	*x = (x_tile_n * x_tile_size + x_tile_off);
	*y = y_tile_n * y_tile_size + y_tile_off;
}

static int linear_x_y_to_xtiled_pos(int x, int y, uint32_t stride, int swizzle,
				    int bpp, int x_tile_size, int y_tile_size)
{
	int pos;
	int pixel_size = bpp / 8;

	x *= pixel_size;
	pos = tile(x, y, x_tile_size, y_tile_size, stride, true);
	pos = swizzle_addr(pos, swizzle);
	return pos / pixel_size;
}

static int gen2_linear_x_y_to_xtiled_pos(int x, int y, uint32_t stride, int swizzle,
					 int bpp)
{
	return linear_x_y_to_xtiled_pos(x, y, stride, swizzle, bpp, 128, 16);
}

static int gen3_linear_x_y_to_xtiled_pos(int x, int y, uint32_t stride, int swizzle,
					 int bpp)
{
	return linear_x_y_to_xtiled_pos(x, y, stride, swizzle, bpp, 512, 8);
}

static int linear_x_y_to_ytiled_pos(int x, int y, uint32_t stride, int swizzle,
				    int bpp, int x_tile_size, int y_tile_size,
				    int ow_size)
{
	int ow_tile_n, pos;
	int pixel_size = bpp / 8;

	/* We have an Y tiling of OWords, so use the tile() function to get the
	 * OW number, then adjust to the fact that the OW may have more than one
	 * pixel. */
	x *= pixel_size;
	ow_tile_n = tile(x / ow_size, y, x_tile_size / ow_size,
			 y_tile_size, stride / ow_size, false);
	pos = ow_tile_n * ow_size + (x % ow_size);
	pos = swizzle_addr(pos, swizzle);
	return pos / pixel_size;
}

static int gen2_linear_x_y_to_ytiled_pos(int x, int y, uint32_t stride, int swizzle,
					 int bpp)
{
	return linear_x_y_to_ytiled_pos(x, y, stride, swizzle, bpp, 128, 16, 8);
}

static int i915_linear_x_y_to_ytiled_pos(int x, int y, uint32_t stride, int swizzle,
					 int bpp)
{
	return linear_x_y_to_ytiled_pos(x, y, stride, swizzle, bpp, 512, 8, 32);
}

static int i945_linear_x_y_to_ytiled_pos(int x, int y, uint32_t stride, int swizzle,
					 int bpp)
{
	return linear_x_y_to_ytiled_pos(x, y, stride, swizzle, bpp, 128, 32, 16);
}

#define OW_SIZE 16			/* in bytes */
#define TILE_4_SUBTILE_SIZE 64		/* in bytes */
#define TILE_4_WIDTH 128		/* in bytes */
#define TILE_4_HEIGHT 32		/* in pixels */
#define TILE_4_SUBTILE_WIDTH  OW_SIZE	/* in bytes */
#define TILE_4_SUBTILE_HEIGHT 4		/* in pixels */

/*
 * Subtile remapping for tile 4.  Note that map[a]==b implies map[b]==a
 * so we can use the same table to tile and until.
 */
static const int tile4_subtile_map[] = {
	0,  1,  2,  3,  8,  9, 10, 11,
	4,  5,  6,  7, 12, 13, 14, 15,
	16, 17, 18, 19, 24, 25, 26, 27,
	20, 21, 22, 23, 28, 29, 30, 31,
	32, 33, 34, 35, 40, 41, 42, 43,
	36, 37, 38, 39, 44, 45, 46, 47,
	48, 49, 50, 51, 56, 57, 58, 59,
	52, 53, 54, 55, 60, 61, 62, 63
};

static int linear_x_y_to_4tiled_pos(int x, int y, uint32_t stride, int swizzle,
				    int bpp)
{
	int tile_base_pos;
	int tile_x, tile_y;
	int subtile_col, subtile_row, subtile_num, new_subtile_num;
	int pixel_size = bpp / 8;
	int byte_x = x * pixel_size;
	int pos;

	/* Modern platforms that have 4-tiling don't use old bit 6 swizzling */
	igt_assert_eq(swizzle, I915_BIT_6_SWIZZLE_NONE);

	/*
	* Where does the 4k tile start (in bytes)?  This is the same for Y and
	* F so we can use the Y-tile algorithm to get to that point.
	*/
	tile_base_pos = (y / TILE_4_HEIGHT) * stride * TILE_4_HEIGHT +
		4096 * (byte_x / TILE_4_WIDTH);

	/* Find pixel within tile */
	tile_x = (byte_x % TILE_4_WIDTH);
	tile_y = y % TILE_4_HEIGHT;

	/* And figure out the subtile within the 4k tile */
	subtile_col = tile_x / TILE_4_SUBTILE_WIDTH;
	subtile_row = tile_y / TILE_4_SUBTILE_HEIGHT;
	subtile_num = subtile_row * 8 + subtile_col;

	/* Swizzle the subtile number according to the bspec diagram */
	new_subtile_num = tile4_subtile_map[subtile_num];

	/* Calculate new position */
	pos = tile_base_pos +
		new_subtile_num * TILE_4_SUBTILE_SIZE +
		(tile_y % TILE_4_SUBTILE_HEIGHT) * OW_SIZE +
		tile_x % TILE_4_SUBTILE_WIDTH;
	igt_assert(pos % pixel_size == 0);
	pos /= pixel_size;

	return pos;
}

static void xtiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
				     int swizzle, int bpp, int *x, int *y,
				     int x_tile_size, int y_tile_size)
{
	int pixel_size = bpp / 8;

	tiled_pos = swizzle_addr(tiled_pos, swizzle);

	untile(tiled_pos, x_tile_size, y_tile_size, stride, true, x, y);
	*x /= pixel_size;
}

static void gen2_xtiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
					  int swizzle, int bpp, int *x, int *y)
{
	return xtiled_pos_to_x_y_linear(tiled_pos, stride, swizzle, bpp, x, y, 128, 16);
}

static void gen3_xtiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
					  int swizzle, int bpp, int *x, int *y)
{
	return xtiled_pos_to_x_y_linear(tiled_pos, stride, swizzle, bpp, x, y, 512, 8);
}

static void ytiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
				     int swizzle, int bpp, int *x, int *y,
				     int x_tile_size, int y_tile_size, int ow_size)
{
	int ow_tile_n;
	int pixel_size = bpp / 8;

	tiled_pos = swizzle_addr(tiled_pos, swizzle);

	ow_tile_n = tiled_pos / ow_size;
	untile(ow_tile_n, x_tile_size / ow_size, y_tile_size, stride / ow_size, false, x, y);
	*x *= ow_size;
	*x += tiled_pos % ow_size;
	*x /= pixel_size;
}

static void gen2_ytiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
					  int swizzle, int bpp, int *x, int *y)
{
	ytiled_pos_to_x_y_linear(tiled_pos, stride, swizzle, bpp, x, y, 128, 16, 8);
}

static void i915_ytiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
					  int swizzle, int bpp, int *x, int *y)
{
	ytiled_pos_to_x_y_linear(tiled_pos, stride, swizzle, bpp, x, y, 512, 8, 32);
}

static void i945_ytiled_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
					  int swizzle, int bpp, int *x, int *y)
{
	ytiled_pos_to_x_y_linear(tiled_pos, stride, swizzle, bpp, x, y, 128, 32, 16);
}

static void tile4_pos_to_x_y_linear(int tiled_pos, uint32_t stride,
				    int swizzle, int bpp, int *x, int *y)
{
	int pixel_size = bpp / 8;
	int tiles_per_line = stride / TILE_4_WIDTH;
	int tile_num, tile_offset, tile_row, tile_col;
	int tile_origin_x, tile_origin_y;
	int subtile_num, subtile_offset, subtile_row, subtile_col;
	int subtile_origin_x, subtile_origin_y;
	int oword_num, byte_num;

	/* Modern platforms that have 4-tiling don't use old bit 6 swizzling */
	igt_assert_eq(swizzle, I915_BIT_6_SWIZZLE_NONE);

	/* Calculate the x,y of the start of the 4k tile */
	tile_num = tiled_pos / 4096;
	tile_row = tile_num / tiles_per_line;
	tile_col = tile_num % tiles_per_line;
	tile_origin_x = tile_col * TILE_4_WIDTH;
	tile_origin_y = tile_row * TILE_4_HEIGHT;

	/* Now calculate the x,y offset of the start of the subtile */
	tile_offset = tiled_pos % 4096;
	subtile_num = tile4_subtile_map[tile_offset / TILE_4_SUBTILE_SIZE];
	subtile_row = subtile_num / 8;
	subtile_col = subtile_num % 8;
	subtile_origin_x = subtile_col * TILE_4_SUBTILE_WIDTH;
	subtile_origin_y = subtile_row * TILE_4_SUBTILE_HEIGHT;

	/* Next the oword and byte within the subtile */
	subtile_offset = tiled_pos % TILE_4_SUBTILE_SIZE;
	oword_num = subtile_offset / OW_SIZE;
	byte_num = subtile_offset % OW_SIZE;

	*x = (tile_origin_x + subtile_origin_x + byte_num) / pixel_size;
	*y = tile_origin_y + subtile_origin_y + oword_num;
}

static void set_pixel(void *_ptr, int index, uint64_t color, int bpp)
{
	if (bpp == 8) {
		uint8_t *ptr = _ptr;
		ptr[index] = color;
	} else if (bpp == 16) {
		uint16_t *ptr = _ptr;
		ptr[index] = color;
	} else if (bpp == 32) {
		uint32_t *ptr = _ptr;
		ptr[index] = color;
	} else if (bpp == 64) {
		uint64_t *ptr = _ptr;
		ptr[index] = color;
	} else {
		igt_assert_f(false, "bpp: %d\n", bpp);
	}
}

static void switch_blt_tiling(struct intel_bb *ibb, uint32_t tiling, bool on)
{
	uint32_t bcs_swctrl;

	/* Default is X-tile */
	if (tiling != I915_TILING_Y && tiling != I915_TILING_4)
		return;

	igt_require(ibb->gen >= 6);

	bcs_swctrl = (0x3 << 16) | (on ? 0x3 : 0x0);

	/* To change the tile register, insert an MI_FLUSH_DW followed by an
	 * MI_LOAD_REGISTER_IMM
	 */
	intel_bb_out(ibb, MI_FLUSH_DW_CMD | 2);
	intel_bb_out(ibb, 0x0);
	intel_bb_out(ibb, 0x0);
	intel_bb_out(ibb, 0x0);

	intel_bb_out(ibb, MI_LOAD_REGISTER_IMM(1));
	intel_bb_out(ibb, 0x22200); /* BCS_SWCTRL */
	intel_bb_out(ibb, bcs_swctrl);
	intel_bb_out(ibb, MI_NOOP);
}

static void draw_rect_ptr_linear(void *ptr, uint32_t stride,
				 struct rect *rect, uint64_t color, int bpp)
{
	int x, y, line_begin;

	for (y = rect->y; y < rect->y + rect->h; y++) {
		line_begin = y * stride / (bpp / 8);
		for (x = rect->x; x < rect->x + rect->w; x++)
			set_pixel(ptr, line_begin + x, color, bpp);
	}
}

typedef int (*linear_x_y_to_tiled_pos_fn)(int x, int y, uint32_t stride, int swizzle,
					  int bpp);

static linear_x_y_to_tiled_pos_fn linear_to_tiled_fn(int fd, uint32_t tiling)
{
	const struct intel_device_info *info =
		intel_get_device_info(intel_get_drm_devid(fd));

	switch (tiling) {
	case I915_TILING_X:
		if (info->graphics_ver == 2)
			return gen2_linear_x_y_to_xtiled_pos;
		else
			return gen3_linear_x_y_to_xtiled_pos;
	case I915_TILING_Y:
		if (info->graphics_ver == 2)
			return gen2_linear_x_y_to_ytiled_pos;
		else if (info->is_grantsdale || info->is_alviso)
			return i915_linear_x_y_to_ytiled_pos;
		else
			return i945_linear_x_y_to_ytiled_pos;
	case I915_TILING_4:
		return linear_x_y_to_4tiled_pos;
	default:
		igt_assert(false);
	}
}

static void draw_rect_ptr_tiled(int fd, void *ptr, uint32_t stride, uint32_t tiling,
				int swizzle, struct rect *rect, uint64_t color,
				int bpp)
{
	linear_x_y_to_tiled_pos_fn linear_x_y_to_tiled_pos =
		linear_to_tiled_fn(fd, tiling);
	int x, y, pos;

	for (y = rect->y; y < rect->y + rect->h; y++) {
		for (x = rect->x; x < rect->x + rect->w; x++) {
			pos = linear_x_y_to_tiled_pos(x, y, stride, swizzle, bpp);
			set_pixel(ptr, pos, color, bpp);
		}
	}
}

static void draw_rect_mmap_cpu(int fd, struct buf_data *buf, struct rect *rect,
			       uint32_t tiling, uint32_t swizzle, uint64_t color)
{
	void *ptr;

	gem_set_domain(fd, buf->handle, I915_GEM_DOMAIN_CPU,
		       I915_GEM_DOMAIN_CPU);

	ptr = gem_mmap__cpu_coherent(fd, buf->handle, 0, PAGE_ALIGN(buf->size),
				     PROT_READ | PROT_WRITE);

	switch (tiling) {
	case I915_TILING_NONE:
		draw_rect_ptr_linear(ptr, buf->stride, rect, color, buf->bpp);
		break;
	case I915_TILING_X:
	case I915_TILING_Y:
	case I915_TILING_4:
		draw_rect_ptr_tiled(fd, ptr, buf->stride, tiling, swizzle, rect,
				    color, buf->bpp);
		break;
	default:
		igt_assert(false);
		break;
	}

	gem_sw_finish(fd, buf->handle);

	igt_assert(gem_munmap(ptr, buf->size) == 0);
}

static void draw_rect_mmap_gtt(int fd, struct buf_data *buf, struct rect *rect,
			       uint64_t color)
{
	void *ptr;

	gem_set_domain(fd, buf->handle, I915_GEM_DOMAIN_GTT,
		       I915_GEM_DOMAIN_GTT);

	ptr = gem_mmap__gtt(fd, buf->handle, PAGE_ALIGN(buf->size),
			    PROT_READ | PROT_WRITE);

	draw_rect_ptr_linear(ptr, buf->stride, rect, color, buf->bpp);

	igt_assert(gem_munmap(ptr, buf->size) == 0);
}

static void draw_rect_mmap_wc(int fd, struct buf_data *buf, struct rect *rect,
			      uint32_t tiling, uint32_t swizzle, uint64_t color)
{
	void *ptr;

	if (is_i915_device(fd)) {
		gem_set_domain(fd, buf->handle, I915_GEM_DOMAIN_GTT,
			       I915_GEM_DOMAIN_GTT);

		if (gem_has_lmem(fd))
			ptr = gem_mmap_offset__fixed(fd, buf->handle, 0,
						     PAGE_ALIGN(buf->size),
						     PROT_READ | PROT_WRITE);
		else if (gem_has_legacy_mmap(fd))
			ptr = gem_mmap__wc(fd, buf->handle, 0, PAGE_ALIGN(buf->size),
					   PROT_READ | PROT_WRITE);
		else
			ptr = gem_mmap_offset__wc(fd, buf->handle, 0,
						  PAGE_ALIGN(buf->size),
						  PROT_READ | PROT_WRITE);
	} else {
		ptr = xe_bo_mmap_ext(fd, buf->handle, buf->size,
				     PROT_READ | PROT_WRITE);
	}

	switch (tiling) {
	case I915_TILING_NONE:
		draw_rect_ptr_linear(ptr, buf->stride, rect, color, buf->bpp);
		break;
	case I915_TILING_X:
	case I915_TILING_Y:
	case I915_TILING_4:
		draw_rect_ptr_tiled(fd, ptr, buf->stride, tiling, swizzle, rect,
				    color, buf->bpp);
		break;
	default:
		igt_assert(false);
		break;
	}

	igt_assert(gem_munmap(ptr, buf->size) == 0);
}

static void draw_rect_pwrite_untiled(int fd, struct buf_data *buf,
				     struct rect *rect, uint64_t color)
{
	int i, y, offset;
	int pixel_size = buf->bpp / 8;
	uint8_t tmp[rect->w * pixel_size];

	for (i = 0; i < rect->w; i++)
		set_pixel(tmp, i, color, buf->bpp);

	for (y = rect->y; y < rect->y + rect->h; y++) {
		offset = (y * buf->stride) + (rect->x * pixel_size);
		gem_write(fd, buf->handle, offset, tmp, rect->w * pixel_size);
	}
}

typedef void (*tiled_pos_to_x_y_linear_fn)(int tiled_pos, uint32_t stride,
					   int swizzle, int bpp, int *x, int *y);

static tiled_pos_to_x_y_linear_fn tiled_to_linear_fn(int fd, uint32_t tiling)
{
	const struct intel_device_info *info =
		intel_get_device_info(intel_get_drm_devid(fd));

	switch (tiling) {
	case I915_TILING_X:
		if (info->graphics_ver == 2)
			return gen2_xtiled_pos_to_x_y_linear;
		else
			return gen3_xtiled_pos_to_x_y_linear;
	case I915_TILING_Y:
		if (info->graphics_ver == 2)
			return gen2_ytiled_pos_to_x_y_linear;
		else if (info->is_grantsdale || info->is_alviso)
			return i915_ytiled_pos_to_x_y_linear;
		else
			return i945_ytiled_pos_to_x_y_linear;
	case I915_TILING_4:
		return tile4_pos_to_x_y_linear;
	default:
		igt_assert(false);
	}
}

static void draw_rect_pwrite_tiled(int fd, struct buf_data *buf,
				   uint32_t tiling, struct rect *rect,
				   uint64_t color, uint32_t swizzle)
{
	tiled_pos_to_x_y_linear_fn tiled_pos_to_x_y_linear =
		tiled_to_linear_fn(fd, tiling);
	int i;
	int tiled_pos, x, y, pixel_size;
	uint8_t tmp[4096];
	int tmp_used = 0, tmp_size;
	bool flush_tmp = false;
	int tmp_start_pos = 0;
	int pixels_written = 0;

	pixel_size = buf->bpp / 8;
	tmp_size = sizeof(tmp) / pixel_size;

	/* Instead of doing one pwrite per pixel, we try to group the maximum
	 * amount of consecutive pixels we can in a single pwrite: that's why we
	 * use the "tmp" variables. */
	for (i = 0; i < tmp_size; i++)
		set_pixel(tmp, i, color, buf->bpp);

	for (tiled_pos = 0; tiled_pos < buf->size; tiled_pos += pixel_size) {
		tiled_pos_to_x_y_linear(tiled_pos, buf->stride,
					swizzle, buf->bpp, &x, &y);

		if (x >= rect->x && x < rect->x + rect->w &&
		    y >= rect->y && y < rect->y + rect->h) {
			if (tmp_used == 0)
				tmp_start_pos = tiled_pos;
			tmp_used++;
		} else {
			flush_tmp = true;
		}

		if (tmp_used == tmp_size || (flush_tmp && tmp_used > 0) ||
		    tiled_pos + pixel_size >= buf->size) {
			gem_write(fd, buf->handle, tmp_start_pos, tmp,
				  tmp_used * pixel_size);
			flush_tmp = false;
			pixels_written += tmp_used;
			tmp_used = 0;

			if (pixels_written == rect->w * rect->h)
				break;
		}
	}
}

static void draw_rect_pwrite(int fd, struct buf_data *buf,
			     struct rect *rect, uint32_t tiling,
			     uint32_t swizzle, uint64_t color)
{
	switch (tiling) {
	case I915_TILING_NONE:
		draw_rect_pwrite_untiled(fd, buf, rect, color);
		break;
	case I915_TILING_X:
	case I915_TILING_Y:
	case I915_TILING_4:
		draw_rect_pwrite_tiled(fd, buf, tiling, rect, color, swizzle);
		break;
	default:
		igt_assert(false);
		break;
	}
}

static struct intel_buf *create_buf(int fd, struct buf_ops *bops,
				    struct buf_data *from, uint32_t tiling)
{
	struct intel_buf *buf;
	enum intel_driver driver = buf_ops_get_driver(bops);
	uint32_t handle, name, width, height;
	uint64_t region = driver == INTEL_DRIVER_XE ? vram_if_possible(fd, 0) : -1;
	uint64_t size = from->size;

	width = from->stride / (from->bpp / 8);
	height = from->height;
	if (driver == INTEL_DRIVER_XE)
		size = ALIGN(size, xe_get_default_alignment(fd));

	name = gem_flink(fd, from->handle);
	handle = gem_open(fd, name);

	buf = intel_buf_create_full(bops, handle,
				    width, height, from->bpp, 0,
				    tiling, 0,
				    size, 0,
				    region,
				    from->pat_index,
				    DEFAULT_MOCS_INDEX);

	/* Make sure we close handle on destroy path */
	intel_buf_set_ownership(buf, true);

	return buf;
}

static void draw_rect_blt(int fd, struct cmd_data *cmd_data,
			  struct buf_data *buf, struct rect *rect,
			  uint32_t tiling, uint64_t color)
{
	struct intel_bb *ibb;
	struct intel_buf *dst;
	int blt_cmd_len, blt_cmd_tiling, blt_cmd_depth;
	uint32_t devid = intel_get_drm_devid(fd);
	int ver = intel_display_ver(devid);
	int pitch;
	uint32_t mocs;

	dst = create_buf(fd, cmd_data->bops, buf, tiling);
	ibb = intel_bb_create(fd, PAGE_SIZE);
	intel_bb_add_intel_buf(ibb, dst, true);

	if (HAS_4TILE(intel_get_drm_devid(fd))) {
		int buf_height = buf->height;

		switch (buf->bpp) {
		case 8:
			blt_cmd_depth = 0;
			break;
		case 16: /* we're assuming 565 */
			blt_cmd_depth = 1 << 19;
			break;
		case 32:
			blt_cmd_depth = 2 << 19;
			break;
		case 64:
			blt_cmd_depth = 3 << 19;
			break;
		default:
			igt_assert(false);
		}

		switch (tiling) {
		case I915_TILING_NONE:
			blt_cmd_tiling = 0;
			break;
		case I915_TILING_X:
			blt_cmd_tiling = 1 << 30;
			break;
		case I915_TILING_4:
			blt_cmd_tiling = 2 << 30;
			break;
		default:
			igt_assert(false);
		}

		pitch = tiling ? buf->stride / 4 : buf->stride;

		if (ver >= 20)
			mocs = dst->mocs_index << XE2_XY_FAST_COLOR_BLT_MOCS_INDEX_SHIFT;
		else
			mocs = dst->mocs_index << XY_FAST_COLOR_BLT_MOCS_INDEX_SHIFT;

		intel_bb_out(ibb, XY_FAST_COLOR_BLT | blt_cmd_depth);
		intel_bb_out(ibb, blt_cmd_tiling | mocs | (pitch-1));
		intel_bb_out(ibb, (rect->y << 16) | rect->x);
		intel_bb_out(ibb, ((rect->y + rect->h) << 16) | (rect->x + rect->w));
		intel_bb_emit_reloc_fenced(ibb, dst->handle, 0,
					   I915_GEM_DOMAIN_RENDER, 0,
					   dst->addr.offset);
		intel_bb_out(ibb, 0);	/* TODO: Pass down enough info for target memory hint */
		intel_bb_out(ibb, color);
		intel_bb_out(ibb, color >> 32);	/* 64 bit color */
		intel_bb_out(ibb, 0);	/* 96 bit color */
		intel_bb_out(ibb, 0);	/* 128 bit color */
		intel_bb_out(ibb, 0);	/* clear address */
		intel_bb_out(ibb, 0);	/* clear address */
		intel_bb_out(ibb, (1 << 29) | ((pitch-1) << 14) | (buf_height-1));
		intel_bb_out(ibb, 0);	/* mipmap levels / qpitch */
		intel_bb_out(ibb, 0);	/* mipmap index / alignment */
	} else if (buf->bpp == 64) {
		int x = rect->x * 2;
		int w = rect->w * 2;

		blt_cmd_depth = 3 << 24; /* 32bpp */
		blt_cmd_len = ((ver >= 8) ?  0x4 : 0x3) + 8*8;
		blt_cmd_tiling = (tiling) ? XY_COLOR_BLT_TILED : 0;
		pitch = (ver >= 4 && tiling) ? buf->stride / 4 : buf->stride;

		switch_blt_tiling(ibb, tiling, true);

		intel_bb_out(ibb, XY_PAT_BLT_IMMEDIATE_CMD_NOLEN | XY_COLOR_BLT_WRITE_ALPHA |
			     XY_COLOR_BLT_WRITE_RGB | blt_cmd_tiling | blt_cmd_len);
		intel_bb_out(ibb, blt_cmd_depth | (0xF0 << 16) | pitch);
		intel_bb_out(ibb, (rect->y << 16) | x);
		intel_bb_out(ibb, ((rect->y + rect->h) << 16) | (x + w));
		intel_bb_emit_reloc_fenced(ibb, dst->handle, 0, I915_GEM_DOMAIN_RENDER,
					   0, dst->addr.offset);
		for (int i = 0; i < 8*8; i += 2) {
			intel_bb_out(ibb, color);
			intel_bb_out(ibb, color >> 32);
		}

		switch_blt_tiling(ibb, tiling, false);
	} else {
		switch (buf->bpp) {
		case 8:
			blt_cmd_depth = 0;
			break;
		case 16: /* we're assuming 565 */
			blt_cmd_depth = 1 << 24;
			break;
		case 32:
			blt_cmd_depth = 3 << 24;
			break;
		default:
			igt_assert(false);
		}

		blt_cmd_len = (ver >= 8) ?  0x5 : 0x4;
		blt_cmd_tiling = (tiling) ? XY_COLOR_BLT_TILED : 0;
		pitch = (ver >= 4 && tiling) ? buf->stride / 4 : buf->stride;

		switch_blt_tiling(ibb, tiling, true);

		intel_bb_out(ibb, XY_COLOR_BLT_CMD_NOLEN | XY_COLOR_BLT_WRITE_ALPHA |
			     XY_COLOR_BLT_WRITE_RGB | blt_cmd_tiling | blt_cmd_len);
		intel_bb_out(ibb, blt_cmd_depth | (0xF0 << 16) | pitch);
		intel_bb_out(ibb, (rect->y << 16) | rect->x);
		intel_bb_out(ibb, ((rect->y + rect->h) << 16) | (rect->x + rect->w));
		intel_bb_emit_reloc_fenced(ibb, dst->handle, 0, I915_GEM_DOMAIN_RENDER,
					   0, dst->addr.offset);
		intel_bb_out(ibb, color);

		switch_blt_tiling(ibb, tiling, false);
	}

	intel_bb_flush_blit(ibb);
	intel_bb_destroy(ibb);
	intel_buf_destroy(dst);
}

static void draw_rect_render(int fd, struct cmd_data *cmd_data,
			     struct buf_data *buf, struct rect *rect,
			     uint32_t tiling, uint64_t color)
{
	struct intel_buf *src, *dst;
	igt_render_copyfunc_t rendercopy = igt_get_render_copyfunc(fd);
	struct intel_bb *ibb;
	struct buf_data tmp;
	int pixel_size = buf->bpp / 8;

	igt_skip_on(!rendercopy);

	/* We create a temporary buffer and copy from it using rendercopy. */
	tmp.pat_index = buf->pat_index;
	tmp.size = rect->w * rect->h * pixel_size;
	if (is_i915_device(fd))
		tmp.handle = gem_create(fd, tmp.size);
	else
		tmp.handle = xe_bo_create(fd, 0,
					  ALIGN(tmp.size, xe_get_default_alignment(fd)),
					  vram_if_possible(fd, 0),
					  DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM |
					  DRM_XE_GEM_CREATE_FLAG_SCANOUT);

	tmp.stride = rect->w * pixel_size;
	tmp.bpp = buf->bpp;
	tmp.width = rect->w;
	tmp.height = rect->h;
	if (is_i915_device(fd))
		draw_rect_mmap_cpu(fd, &tmp, &(struct rect){0, 0, rect->w, rect->h},
				   I915_TILING_NONE, I915_BIT_6_SWIZZLE_NONE, color);
	else
		draw_rect_mmap_wc(fd, &tmp, &(struct rect){0, 0, rect->w, rect->h},
				  I915_TILING_NONE, I915_BIT_6_SWIZZLE_NONE, color);

	src = create_buf(fd, cmd_data->bops, &tmp, I915_TILING_NONE);
	dst = create_buf(fd, cmd_data->bops, buf, tiling);
	ibb = intel_bb_create_with_context(fd, cmd_data->ctx, 0, NULL, PAGE_SIZE);

	rendercopy(ibb, src, 0, 0, rect->w, rect->h, dst, rect->x, rect->y);

	intel_bb_destroy(ibb);
	intel_buf_destroy(src);
	intel_buf_destroy(dst);
	gem_close(fd, tmp.handle);
}

/**
 * igt_draw_rect:
 * @fd: the DRM file descriptor
 * @bops: buf ops, only required for IGT_DRAW_BLT and IGT_DRAW_RENDER
 * @ctx: the context, can be 0 if you don't want to think about it
 * @buf_handle: the handle of the buffer where you're going to draw to
 * @buf_size: the size of the buffer
 * @buf_stride: the stride of the buffer
 * @buf_width: the width of the buffer
 * @buf_height: the height of the buffer
 * @tiling: the tiling of the buffer
 * @method: method you're going to use to write to the buffer
 * @rect_x: horizontal position on the buffer where your rectangle starts
 * @rect_y: vertical position on the buffer where your rectangle starts
 * @rect_w: width of the rectangle
 * @rect_h: height of the rectangle
 * @color: color of the rectangle
 * @bpp: bits per pixel
 *
 * This function draws a colored rectangle on the destination buffer, allowing
 * you to specify the method used to draw the rectangle.
 */
void igt_draw_rect(int fd, struct buf_ops *bops, uint32_t ctx,
		   uint32_t buf_handle, uint32_t buf_size, uint32_t buf_stride,
		   int buf_width, int buf_height,
		   uint32_t tiling, enum igt_draw_method method,
		   int rect_x, int rect_y, int rect_w, int rect_h,
		   uint64_t color, int bpp)
{
	uint32_t buf_tiling, swizzle;

	struct cmd_data cmd_data = {
		.bops = bops,
		.ctx = ctx,
	};
	struct buf_data buf = {
		.handle = buf_handle,
		.size = buf_size,
		.stride = buf_stride,
		.width = buf_width,
		.height = buf_height,
		.bpp = bpp,
		.pat_index = intel_get_pat_idx_uc(fd),
	};
	struct rect rect = {
		.x = rect_x,
		.y = rect_y,
		.w = rect_w,
		.h = rect_h,
	};

	swizzle = I915_BIT_6_SWIZZLE_NONE;
	if (is_i915_device(fd) && tiling != I915_TILING_NONE && gem_available_fences(fd)) {
		gem_get_tiling(fd, buf_handle, &buf_tiling, &swizzle);
		igt_assert(tiling == buf_tiling);
	}

	switch (method) {
	case IGT_DRAW_MMAP_CPU:
		draw_rect_mmap_cpu(fd, &buf, &rect, tiling, swizzle, color);
		break;
	case IGT_DRAW_MMAP_GTT:
		draw_rect_mmap_gtt(fd, &buf, &rect, color);
		break;
	case IGT_DRAW_MMAP_WC:
		draw_rect_mmap_wc(fd, &buf, &rect, tiling, swizzle, color);
		break;
	case IGT_DRAW_PWRITE:
		draw_rect_pwrite(fd, &buf, &rect, tiling, swizzle, color);
		break;
	case IGT_DRAW_BLT:
		draw_rect_blt(fd, &cmd_data, &buf, &rect, tiling, color);
		break;
	case IGT_DRAW_RENDER:
		draw_rect_render(fd, &cmd_data, &buf, &rect, tiling, color);
		break;
	default:
		igt_assert(false);
		break;
	}
}

/**
 * igt_draw_rect_fb:
 * @fd: the DRM file descriptor
 * @bops: buf ops, only required for IGT_DRAW_BLT and IGT_DRAW_RENDER
 * @ctx: context, can be 0 if you don't want to think about it
 * @fb: framebuffer
 * @method: method you're going to use to write to the buffer
 * @rect_x: horizontal position on the buffer where your rectangle starts
 * @rect_y: vertical position on the buffer where your rectangle starts
 * @rect_w: width of the rectangle
 * @rect_h: height of the rectangle
 * @color: color of the rectangle
 *
 * This is exactly the same as igt_draw_rect, but you can pass an igt_fb instead
 * of manually providing its details. See igt_draw_rect.
 */
void igt_draw_rect_fb(int fd, struct buf_ops *bops,
		      uint32_t ctx, struct igt_fb *fb,
		      enum igt_draw_method method, int rect_x, int rect_y,
		      int rect_w, int rect_h, uint64_t color)
{
	igt_draw_rect(fd, bops, ctx, fb->gem_handle, fb->size, fb->strides[0],
		      fb->width, fb->height,
		      igt_fb_mod_to_tiling(fb->modifier), method,
		      rect_x, rect_y, rect_w, rect_h, color,
		      igt_drm_format_to_bpp(fb->drm_format));
}

/**
 * igt_draw_fill_fb:
 * @fd: the DRM file descriptor
 * @fb: the FB that is going to be filled
 * @color: the color you're going to paint it
 *
 * This function just paints an igt_fb using the provided color.
 */
void igt_draw_fill_fb(int fd, struct igt_fb *fb, uint64_t color)
{
	igt_draw_rect_fb(fd, NULL, 0, fb,
			 igt_draw_supports_method(fd, IGT_DRAW_MMAP_GTT) ?
			 IGT_DRAW_MMAP_GTT : IGT_DRAW_MMAP_WC,
			 0, 0, fb->width, fb->height, color);
}

bool igt_draw_supports_method(int fd, enum igt_draw_method method)
{
	if (method == IGT_DRAW_MMAP_GTT)
		return is_i915_device(fd) && gem_has_mappable_ggtt(fd);

	if (method == IGT_DRAW_MMAP_WC)
		return (is_i915_device(fd) && gem_mmap__has_wc(fd)) || is_xe_device(fd);

	if (method == IGT_DRAW_MMAP_CPU || method == IGT_DRAW_PWRITE)
		return is_i915_device(fd);

	if (method == IGT_DRAW_RENDER)
		return !!igt_get_render_copyfunc(fd);

	return true;
}
