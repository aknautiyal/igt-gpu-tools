/*
 * Copyright © 2011 Daniel Vetter
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
 * Partially based upon gem_tiled_fence_blits.c
 */

/** @file gem_stress.c
 *
 * This is a general gem coherency test. It's designed to eventually replicate
 * any possible sequence of access patterns. It works by copying a set of tiles
 * between two sets of backing buffer objects, randomly permutating the assinged
 * position on each copy operations.
 *
 * The copy operation are done in tiny portions (to reduce any race windows
 * for corruptions, hence increasing the chances for observing one) and are
 * constantly switched between all means to copy stuff (fenced blitter, unfenced
 * render, mmap, pwrite/read).
 *
 * After every complete move of a set tiling parameters of a buffer are randomly
 * changed to simulate the effects of libdrm caching.
 *
 * Buffers are 1mb big to nicely fit into fences on gen2/3. A few are further
 * split up to test relaxed fencing. Using this to push the average working set
 * size over the available gtt space forces objects to be mapped as unfenceable
 * (and as a side-effect tests gtt map/unmap coherency).
 *
 * In short: designed for maximum evilness.
 */

#include "igt.h"
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <drm.h>
/**
 * TEST: gem stress
 * Description: General gem coherency test.
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: CMD submission
 * Functionality: coherency
 *
 * SUBTEST:
 */

IGT_TEST_DESCRIPTION("General gem coherency test.");

#define CMD_POLY_STIPPLE_OFFSET       0x7906

#define DUCTAPE 0xdead0001
#define TILESZ	0xdead0002
#define CHCK_RENDER 0xdead0003

/** TODO:
 * - beat on relaxed fencing (i.e. mappable/fenceable tracking in the kernel)
 * - render copy (to check fence tracking and cache coherency management by the
 *   kernel)
 * - multi-threading: probably just a wrapper script to launch multiple
 *   instances + an option to accordingly reduce the working set
 * - gen6 inter-ring coherency (needs render copy, first)
 * - variable buffer size
 * - add an option to fork a second process that randomly sends signals to the
 *   first one (to check consistency of the kernel recovery paths)
 */

struct buf_ops *bops;
struct intel_bb *ibb;
int drm_fd;
int devid;
int num_fences;

struct intel_buf busy_bo;

struct option_struct {
    unsigned scratch_buf_size;
    unsigned max_dimension;
    unsigned num_buffers;
    int trace_tile;
    int no_hw;
    int gpu_busy_load;
    int use_render;
    int use_blt;
    int forced_tiling;
    int use_cpu_maps;
    int total_rounds;
    int fail;
    int tiles_per_buf;
    int ducttape;
    int tile_size;
    int check_render_cpyfn;
    int use_signal_helper;
};

#define MAX_BUFS		4096
#define SCRATCH_BUF_SIZE	1024*1024
#define BUSY_BUF_SIZE		(256*4096)
#define TILE_BYTES(size)	((size)*(size)*sizeof(uint32_t))

struct option_struct options = {
	.scratch_buf_size = BUSY_BUF_SIZE,
	.no_hw = 0,
	.use_signal_helper = 1,
	.gpu_busy_load = 0,
	.num_buffers = 0,
	.trace_tile = -1,
	.use_render = 1,
	.use_blt = 1,
	.forced_tiling = -1,
	.use_cpu_maps = 0,
	.total_rounds = 512,
	.fail = 1,
	.ducttape = 1,
	.tile_size = 16,
	.tiles_per_buf = BUSY_BUF_SIZE / TILE_BYTES(16),
	.check_render_cpyfn = 0,
};

static struct intel_buf buffers[2][MAX_BUFS];
/* tile i is at logical position tile_permutation[i] */
static unsigned *tile_permutation;
static unsigned num_buffers = 0;
static unsigned current_set = 0;
static unsigned target_set = 0;
static unsigned num_total_tiles = 0;

int fence_storm = 0;
static int gpu_busy_load = 10;

struct {
	unsigned num_failed;
	unsigned max_failed_reads;
} stats;

static void tile2xy(struct intel_buf *buf, unsigned tile, unsigned *x, unsigned *y)
{
	igt_assert(tile < options.tiles_per_buf);
	*x = (tile*options.tile_size) % (buf->surface[0].stride/sizeof(uint32_t));
	*y = ((tile*options.tile_size) / (buf->surface[0].stride/sizeof(uint32_t))) * options.tile_size;
}

static void emit_blt(struct intel_buf *src, uint32_t src_tiling, unsigned src_pitch,
		     unsigned src_x, unsigned src_y, unsigned w, unsigned h,
		     struct intel_buf *dst, uint32_t dst_tiling, unsigned dst_pitch,
		     unsigned dst_x, unsigned dst_y)
{
	uint32_t cmd_bits = 0;

	if (IS_965(devid) && src_tiling) {
		src_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_SRC_TILED;
	}

	if (IS_965(devid) && dst_tiling) {
		dst_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_DST_TILED;
	}

	/* copy lower half to upper half */
	intel_bb_blit_start(ibb, cmd_bits);
	intel_bb_out(ibb, (3 << 24) | /* 32 bits */
		     (0xcc << 16) | /* copy ROP */
		     dst_pitch);
	intel_bb_out(ibb, dst_y << 16 | dst_x);
	intel_bb_out(ibb, (dst_y+h) << 16 | (dst_x+w));
	intel_bb_emit_reloc_fenced(ibb, dst->handle,
				   I915_GEM_DOMAIN_RENDER,
				   I915_GEM_DOMAIN_RENDER,
				   0, dst->addr.offset);
	intel_bb_out(ibb, src_y << 16 | src_x);
	intel_bb_out(ibb, src_pitch);
	intel_bb_emit_reloc_fenced(ibb, src->handle,
				   I915_GEM_DOMAIN_RENDER, 0,
				   0, src->addr.offset);

	if (ibb->gen >= 6) {
		intel_bb_out(ibb, XY_SETUP_CLIP_BLT_CMD);
		intel_bb_out(ibb, 0);
		intel_bb_out(ibb, 0);
	}
}

/* All this gem trashing wastes too much cpu time, so give the gpu something to
 * do to increase changes for races. */
static void keep_gpu_busy(void)
{
	int tmp;

	tmp = 1 << gpu_busy_load;
	igt_assert_lte(tmp, 1024);

	emit_blt(&busy_bo, 0, 4096, 0, 0, tmp, 128,
		 &busy_bo, 0, 4096, 0, 128);
}

static void set_to_cpu_domain(struct intel_buf *buf, int writing)
{
	gem_set_domain(drm_fd, buf->handle, I915_GEM_DOMAIN_CPU,
		       writing ? I915_GEM_DOMAIN_CPU : 0);
}

static void set_to_gtt_domain(struct intel_buf *buf, int writing)
{
	gem_set_domain(drm_fd, buf->handle, I915_GEM_DOMAIN_GTT,
		       writing ? I915_GEM_DOMAIN_GTT : 0);
}

static unsigned int copyfunc_seq = 0;
static void (*copyfunc)(struct intel_buf *src, unsigned src_x, unsigned src_y,
			struct intel_buf *dst, unsigned dst_x, unsigned dst_y,
			unsigned logical_tile_no);

/* stride, x, y in units of uint32_t! */
static void cpucpy2d(uint32_t *src, unsigned src_stride, unsigned src_x, unsigned src_y,
		     uint32_t *dst, unsigned dst_stride, unsigned dst_x, unsigned dst_y,
		     unsigned logical_tile_no)
{
	int i, j;
	int failed = 0;

	for (i = 0; i < options.tile_size; i++) {
		for (j = 0; j < options.tile_size; j++) {
			unsigned dst_ofs = dst_x + j + dst_stride * (dst_y + i);
			unsigned src_ofs = src_x + j + src_stride * (src_y + i);
			unsigned expect = logical_tile_no*options.tile_size*options.tile_size
			    + i*options.tile_size + j;
			uint32_t tmp = src[src_ofs];
			if (tmp != expect) {
			    igt_info("mismatch at tile %i pos %i, read %i, expected %i, diff %i\n", logical_tile_no, i * options.tile_size + j, tmp, expect, (int)tmp - expect);
			    igt_fail_on(options.trace_tile >= 0 && options.fail);
			    failed++;
			}
			/* when not aborting, correct any errors */
			dst[dst_ofs] = expect;
		}
	}
	igt_fail_on(failed && options.fail);

	if (failed > stats.max_failed_reads)
		stats.max_failed_reads = failed;
	if (failed)
		stats.num_failed++;
}

static void cpu_copyfunc(struct intel_buf *src, unsigned src_x, unsigned src_y,
			 struct intel_buf *dst, unsigned dst_x, unsigned dst_y,
			 unsigned logical_tile_no)
{
	igt_assert(src->ptr);
	igt_assert(dst->ptr);

	if (options.ducttape)
		set_to_gtt_domain(dst, 1);

	if (options.use_cpu_maps) {
		set_to_cpu_domain(src, 0);
		set_to_cpu_domain(dst, 1);
	}

	cpucpy2d(src->ptr, src->surface[0].stride/sizeof(uint32_t), src_x, src_y,
		 dst->ptr, dst->surface[0].stride/sizeof(uint32_t), dst_x, dst_y,
		 logical_tile_no);
}

static void prw_copyfunc(struct intel_buf *src, unsigned src_x, unsigned src_y,
			 struct intel_buf *dst, unsigned dst_x, unsigned dst_y,
			 unsigned logical_tile_no)
{
	uint32_t tmp_tile[options.tile_size*options.tile_size];
	int i;

	igt_assert(src->ptr);
	igt_assert(dst->ptr);

	igt_info("prw\n");

	if (options.ducttape)
		set_to_gtt_domain(dst, 1);

	if (src->tiling == I915_TILING_NONE) {
		for (i = 0; i < options.tile_size; i++) {
			unsigned ofs = src_x*sizeof(uint32_t) + src->surface[0].stride*(src_y + i);
			gem_read(drm_fd, src->handle, ofs,
				 tmp_tile + options.tile_size*i,
				 options.tile_size*sizeof(uint32_t));
		}
	} else {
		if (options.use_cpu_maps)
			set_to_cpu_domain(src, 0);

		cpucpy2d(src->ptr, src->surface[0].stride/sizeof(uint32_t),
			 src_x, src_y,
			 tmp_tile, options.tile_size, 0, 0, logical_tile_no);
	}

	if (dst->tiling == I915_TILING_NONE) {
		for (i = 0; i < options.tile_size; i++) {
			unsigned ofs = dst_x*sizeof(uint32_t) + dst->surface[0].stride*(dst_y + i);
			gem_write(drm_fd, dst->handle, ofs,
				  tmp_tile + options.tile_size*i,
				  options.tile_size*sizeof(uint32_t));
		}
	} else {
		if (options.use_cpu_maps)
			set_to_cpu_domain(dst, 1);

		cpucpy2d(tmp_tile, options.tile_size, 0, 0,
			 dst->ptr, dst->surface[0].stride/sizeof(uint32_t),
			 dst_x, dst_y,
			 logical_tile_no);
	}
}

static void blitter_copyfunc(struct intel_buf *src, unsigned src_x, unsigned src_y,
			     struct intel_buf *dst, unsigned dst_x, unsigned dst_y,
			     unsigned logical_tile_no)
{
	static unsigned keep_gpu_busy_counter = 0;

	/* check both edges of the fence usage */
	if (keep_gpu_busy_counter & 1 && !fence_storm)
		keep_gpu_busy();

	emit_blt(src, src->tiling, src->surface[0].stride, src_x, src_y,
		 options.tile_size, options.tile_size,
		 dst, dst->tiling, dst->surface[0].stride, dst_x, dst_y);

	if (!(keep_gpu_busy_counter & 1) && !fence_storm)
		keep_gpu_busy();

	keep_gpu_busy_counter++;

	if (src->tiling)
		fence_storm--;
	if (dst->tiling)
		fence_storm--;

	if (fence_storm <= 1) {
		fence_storm = 0;
		intel_bb_flush_blit(ibb);
	}
}

static void render_copyfunc(struct intel_buf *src, unsigned src_x, unsigned src_y,
			    struct intel_buf *dst, unsigned dst_x, unsigned dst_y,
			    unsigned logical_tile_no)
{
	static unsigned keep_gpu_busy_counter = 0;
	igt_render_copyfunc_t rendercopy = igt_get_render_copyfunc(drm_fd);

	/* check both edges of the fence usage */
	if (keep_gpu_busy_counter & 1)
		keep_gpu_busy();

	if (rendercopy) {
		/*
		 * Flush outstanding blts so that they don't end up on
		 * the render ring when that's not allowed (gen6+).
		 */
		intel_bb_flush_blit(ibb);

		rendercopy(ibb, src, src_x, src_y,
		     options.tile_size, options.tile_size,
		     dst, dst_x, dst_y);
	} else
		blitter_copyfunc(src, src_x, src_y,
				 dst, dst_x, dst_y,
				 logical_tile_no);
	if (!(keep_gpu_busy_counter & 1))
		keep_gpu_busy();

	keep_gpu_busy_counter++;
	intel_bb_flush_blit(ibb);
}

static void next_copyfunc(int tile)
{
	if (fence_storm) {
		if (tile == options.trace_tile)
			igt_info(" using fence storm\n");
		return;
	}

	if (copyfunc_seq % 61 == 0
			&& options.forced_tiling != I915_TILING_NONE) {
		if (tile == options.trace_tile)
			igt_info(" using fence storm\n");
		fence_storm = num_fences;
		copyfunc = blitter_copyfunc;
	} else if (copyfunc_seq % 17 == 0) {
		if (tile == options.trace_tile)
			igt_info(" using cpu\n");
		copyfunc = cpu_copyfunc;
	} else if (copyfunc_seq % 19 == 0) {
		if (tile == options.trace_tile)
			igt_info(" using prw\n");
		copyfunc = prw_copyfunc;
	} else if (copyfunc_seq % 3 == 0 && options.use_render) {
		if (tile == options.trace_tile)
			igt_info(" using render\n");
		copyfunc = render_copyfunc;
	} else if (options.use_blt){
		if (tile == options.trace_tile)
			igt_info(" using blitter\n");
		copyfunc = blitter_copyfunc;
	} else if (options.use_render){
		if (tile == options.trace_tile)
			igt_info(" using render\n");
		copyfunc = render_copyfunc;
	} else {
		copyfunc = cpu_copyfunc;
	}

	copyfunc_seq++;
}

static void fan_out(void)
{
	uint32_t tmp_tile[options.tile_size*options.tile_size];
	uint32_t seq = 0;
	int i, k;
	unsigned tile, buf_idx, x, y;

	for (i = 0; i < num_total_tiles; i++) {
		tile = i;
		buf_idx = tile / options.tiles_per_buf;
		tile %= options.tiles_per_buf;

		tile2xy(&buffers[current_set][buf_idx], tile, &x, &y);

		for (k = 0; k < options.tile_size*options.tile_size; k++)
			tmp_tile[k] = seq++;

		if (options.use_cpu_maps)
			set_to_cpu_domain(&buffers[current_set][buf_idx], 1);

		cpucpy2d(tmp_tile, options.tile_size, 0, 0,
			 buffers[current_set][buf_idx].ptr,
			 buffers[current_set][buf_idx].surface[0].stride / sizeof(uint32_t),
			 x, y, i);
	}

	for (i = 0; i < num_total_tiles; i++)
		tile_permutation[i] = i;
}

static void fan_in_and_check(void)
{
	uint32_t tmp_tile[options.tile_size*options.tile_size];
	unsigned tile, buf_idx, x, y;
	int i;
	for (i = 0; i < num_total_tiles; i++) {
		tile = tile_permutation[i];
		buf_idx = tile / options.tiles_per_buf;
		tile %= options.tiles_per_buf;

		tile2xy(&buffers[current_set][buf_idx], tile, &x, &y);

		if (options.use_cpu_maps)
			set_to_cpu_domain(&buffers[current_set][buf_idx], 0);

		cpucpy2d(buffers[current_set][buf_idx].ptr,
			 buffers[current_set][buf_idx].surface[0].stride / sizeof(uint32_t),
			 x, y,
			 tmp_tile, options.tile_size, 0, 0,
			 i);
	}
}

static void sanitize_stride(struct intel_buf *buf)
{

	if (intel_buf_height(buf) > options.max_dimension)
		buf->surface[0].stride = buf->surface[0].size / options.max_dimension;

	if (intel_buf_height(buf) < options.tile_size)
		buf->surface[0].stride = buf->surface[0].size / options.tile_size;

	if (intel_buf_width(buf) < options.tile_size)
		buf->surface[0].stride = options.tile_size * sizeof(uint32_t);

	igt_assert(buf->surface[0].stride <= 8192);
	igt_assert(intel_buf_width(buf) <= options.max_dimension);
	igt_assert(intel_buf_height(buf) <= options.max_dimension);

	igt_assert(intel_buf_width(buf) >= options.tile_size);
	igt_assert(intel_buf_height(buf) >= options.tile_size);

}

static void init_buffer(struct intel_buf *buf, unsigned size)
{
	uint32_t stride, width, height, bpp;

	stride = 4096;
	bpp = 32;
	width = stride / (bpp / 8);
	height = size / stride;

	intel_buf_init(bops, buf, width, height, bpp, 0,
		       I915_TILING_NONE, I915_COMPRESSION_NONE);

	sanitize_stride(buf);

	if (options.no_hw)
		buf->ptr = malloc(size);
	else {
		if (options.use_cpu_maps)
			intel_buf_cpu_map(buf, true);
		else
			intel_buf_device_map(buf, true);
	}
}

static void exchange_buf(void *array, unsigned i, unsigned j)
{
	struct intel_buf *buf_arr, tmp;
	buf_arr = array;

	memcpy(&tmp, &buf_arr[i], sizeof(struct intel_buf));
	memcpy(&buf_arr[i], &buf_arr[j], sizeof(struct intel_buf));
	memcpy(&buf_arr[j], &tmp, sizeof(struct intel_buf));
}


static void init_set(unsigned set)
{
	long int r;
	int i;

	igt_permute_array(buffers[set], num_buffers, exchange_buf);

	if (current_set == 1 && options.gpu_busy_load == 0) {
		gpu_busy_load++;
		if (gpu_busy_load > 10)
			gpu_busy_load = 6;
	}

	for (i = 0; i < num_buffers; i++) {
		r = random();
		if ((r & 3) != 0)
		    continue;
		r >>= 2;

		if ((r & 3) != 0)
			buffers[set][i].tiling = I915_TILING_X;
		else
			buffers[set][i].tiling = I915_TILING_NONE;
		r >>= 2;
		if (options.forced_tiling >= 0)
			buffers[set][i].tiling = options.forced_tiling;

		if (buffers[set][i].tiling == I915_TILING_NONE) {
			/* min 64 byte stride */
			r %= 8;
			buffers[set][i].surface[0].stride = 64 * (1 << r);
		} else if (IS_GEN2(devid)) {
			/* min 128 byte stride */
			r %= 7;
			buffers[set][i].surface[0].stride = 128 * (1 << r);
		} else {
			/* min 512 byte stride */
			r %= 5;
			buffers[set][i].surface[0].stride = 512 * (1 << r);
		}

		sanitize_stride(&buffers[set][i]);

		gem_set_tiling(drm_fd, buffers[set][i].handle,
			       buffers[set][i].tiling,
			       buffers[set][i].surface[0].stride);

		if (options.trace_tile != -1 && i == options.trace_tile/options.tiles_per_buf)
			igt_info("changing buffer %i containing tile %i: tiling %i, stride %i\n", i, options.trace_tile, buffers[set][i].tiling,
				 buffers[set][i].surface[0].stride);
	}
}

static void exchange_uint(void *array, unsigned i, unsigned j)
{
	unsigned *i_arr = array;

	igt_swap(i_arr[i], i_arr[j]);
}

static void copy_tiles(unsigned *permutation)
{
	unsigned src_tile, src_buf_idx, src_x, src_y;
	unsigned dst_tile, dst_buf_idx, dst_x, dst_y;
	struct intel_buf *src_buf, *dst_buf;
	int i, idx;

	for (i = 0; i < num_total_tiles; i++) {
		/* tile_permutation is independent of current_permutation, so
		 * abuse it to randomize the order of the src bos */
		idx  = tile_permutation[i];
		src_buf_idx = idx / options.tiles_per_buf;
		src_tile = idx % options.tiles_per_buf;
		src_buf = &buffers[current_set][src_buf_idx];

		tile2xy(src_buf, src_tile, &src_x, &src_y);

		dst_buf_idx = permutation[idx] / options.tiles_per_buf;
		dst_tile = permutation[idx] % options.tiles_per_buf;
		dst_buf = &buffers[target_set][dst_buf_idx];

		tile2xy(dst_buf, dst_tile, &dst_x, &dst_y);

		if (options.trace_tile == i)
			igt_info("copying tile %i from %i (%i, %i) to %i (%i, %i)", i, tile_permutation[i], src_buf_idx, src_tile, permutation[idx], dst_buf_idx, dst_tile);

		if (options.no_hw) {
			cpucpy2d(src_buf->ptr,
				 src_buf->surface[0].stride / sizeof(uint32_t),
				 src_x, src_y,
				 dst_buf->ptr,
				 dst_buf->surface[0].stride / sizeof(uint32_t),
				 dst_x, dst_y,
				 i);
		} else {
			next_copyfunc(i);

			copyfunc(src_buf, src_x, src_y, dst_buf, dst_x, dst_y,
				 i);
		}
	}

	intel_bb_flush_blit(ibb);
}

static void sanitize_tiles_per_buf(void)
{
	if (options.tiles_per_buf > options.scratch_buf_size / TILE_BYTES(options.tile_size))
		options.tiles_per_buf = options.scratch_buf_size / TILE_BYTES(options.tile_size);
}

static int parse_options(int opt, int opt_index, void *data)
{
	int tmp;

	switch(opt) {
	case 'd':
		options.no_hw = 1;
		igt_info("no-hw debug mode\n");
		break;
	case 'S':
		options.use_signal_helper = 0;
		igt_info("disabling that pesky nuisance who keeps interrupting us\n");
		break;
	case 's':
		tmp = atoi(optarg);
		if (tmp < options.tile_size*8192)
			igt_info("scratch buffer size needs to be at least %i\n", options.tile_size * 8192);
		else if (tmp & (tmp - 1)) {
			igt_info("scratch buffer size needs to be a power-of-two\n");
		} else {
			igt_info("fixed scratch buffer size to %u\n", tmp);
			options.scratch_buf_size = tmp;
			sanitize_tiles_per_buf();
		}
		break;
	case 'g':
		tmp = atoi(optarg);
		if (tmp < 0 || tmp > 10)
			igt_info("gpu busy load needs to be bigger than 0 and smaller than 10\n");
		else {
			igt_info("gpu busy load factor set to %i\n", tmp);
			gpu_busy_load = options.gpu_busy_load = tmp;
		}
		break;
	case 'c':
		options.num_buffers = atoi(optarg);
		igt_info("buffer count set to %i\n", options.num_buffers);
		break;
	case 't':
		options.trace_tile = atoi(optarg);
		igt_info("tracing tile %i\n", options.trace_tile);
		break;
	case 'r':
		options.use_render = 0;
		igt_info("disabling render copy\n");
		break;
	case 'b':
		options.use_blt = 0;
		igt_info("disabling blt copy\n");
		break;
	case 'u':
		options.forced_tiling = I915_TILING_NONE;
		igt_info("disabling tiling\n");
		break;
	case 'x':
		if (options.use_cpu_maps) {
			igt_info("tiling not possible with cpu maps\n");
		} else {
			options.forced_tiling = I915_TILING_X;
			igt_info("using only X-tiling\n");
		}
		break;
	case 'm':
		options.use_cpu_maps = 1;
		options.forced_tiling = I915_TILING_NONE;
		igt_info("disabling tiling\n");
		break;
	case 'o':
		options.total_rounds = atoi(optarg);
		igt_info("total rounds %i\n", options.total_rounds);
		break;
	case 'f':
		options.fail = 0;
		igt_info("not failing when detecting errors\n");
		break;
	case 'p':
		options.tiles_per_buf = atoi(optarg);
		igt_info("tiles per buffer %i\n", options.tiles_per_buf);
		break;
	case DUCTAPE:
		options.ducttape = 0;
		igt_info("applying duct-tape\n");
		break;
	case TILESZ:
		options.tile_size = atoi(optarg);
		sanitize_tiles_per_buf();
		igt_info("til size %i\n", options.tile_size);
		break;
	case CHCK_RENDER:
		options.check_render_cpyfn = 1;
		igt_info("checking render copy function\n");
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	/* actually 32767, according to docs, but that kills our nice pot calculations. */
	options.max_dimension = 16*1024;
	if (options.use_render) {
		if (IS_GEN2(devid) || IS_GEN3(devid))
			options.max_dimension = 2048;
		else
			options.max_dimension = 8192;
	}
	igt_info("Limiting buffer to %dx%d\n", options.max_dimension, options.max_dimension);

	return IGT_OPT_HANDLER_SUCCESS;
}

static void init(void)
{
	int i;
	unsigned tmp;
	uint32_t stride, width, height, bpp;

	if (options.num_buffers == 0) {
		tmp = gem_aperture_size(drm_fd);
		tmp = min(256 * 1024 * 1024u, tmp);
		num_buffers = 2 * tmp / options.scratch_buf_size / 3;
		num_buffers /= 2;
		igt_info("using %u buffers\n", num_buffers);
	} else
		num_buffers = options.num_buffers;

	num_fences = gem_available_fences(drm_fd);
	igt_assert_lt(4, num_fences);

	bops = buf_ops_create(drm_fd);
	ibb = intel_bb_create(drm_fd, 4096);

	stride = 4096;
	bpp = 32;
	width = stride / (bpp / 8);
	height = BUSY_BUF_SIZE / stride;
	intel_buf_init(bops, &busy_bo,
		       width, height, bpp, 0, options.forced_tiling,
		       I915_COMPRESSION_NONE);

	for (i = 0; i < num_buffers; i++) {
		init_buffer(&buffers[0][i], options.scratch_buf_size);
		init_buffer(&buffers[1][i], options.scratch_buf_size);

		num_total_tiles += options.tiles_per_buf;
	}
	current_set = 0;

	/* just in case it helps reproducability */
	srandom(0xdeadbeef);
}

static void check_render_copyfunc(void)
{
	struct intel_buf src, dst;
	uint32_t *ptr;
	int i, j, pass;

	if (!options.check_render_cpyfn)
		return;

	init_buffer(&src, options.scratch_buf_size);
	init_buffer(&dst, options.scratch_buf_size);

	for (pass = 0; pass < 16; pass++) {
		int sx = random() % (intel_buf_width(&src)-options.tile_size);
		int sy = random() % (intel_buf_height(&src)-options.tile_size);
		int dx = random() % (intel_buf_width(&dst)-options.tile_size);
		int dy = random() % (intel_buf_height(&dst)-options.tile_size);

		if (options.use_cpu_maps)
			set_to_cpu_domain(&src, 1);

		memset(src.ptr, 0xff, options.scratch_buf_size);
		for (j = 0; j < options.tile_size; j++) {
			ptr = (uint32_t*)((char *)src.ptr + sx*4 +
					  (sy+j) * src.surface[0].stride);
			for (i = 0; i < options.tile_size; i++)
				ptr[i] = j * options.tile_size + i;
		}

		render_copyfunc(&src, sx, sy, &dst, dx, dy, 0);

		if (options.use_cpu_maps)
			set_to_cpu_domain(&dst, 0);

		for (j = 0; j < options.tile_size; j++) {
			ptr = (uint32_t*)((char *)dst.ptr + dx*4 +
					  (dy+j) * dst.surface[0].stride);
			for (i = 0; i < options.tile_size; i++)
				if (ptr[i] != j * options.tile_size + i) {
					igt_info("render copyfunc mismatch at (%d, %d): found %d, expected %d\n", i, j, ptr[i], j * options.tile_size + i);
				}
		}
	}
}

static struct option long_options[] = {
	{"no-hw", 0, 0, 'd'},
	{"buf-size", 1, 0, 's'},
	{"gpu-busy-load", 1, 0, 'g'},
	{"no-signals", 0, 0, 'S'},
	{"buffer-count", 1, 0, 'c'},
	{"trace-tile", 1, 0, 't'},
	{"disable-blt", 0, 0, 'b'},
	{"disable-render", 0, 0, 'r'},
	{"untiled", 0, 0, 'u'},
	{"x-tiled", 0, 0, 'x'},
	{"use-cpu-maps", 0, 0, 'm'},
	{"rounds", 1, 0, 'o'},
	{"no-fail", 0, 0, 'f'},
	{"tiles-per-buf", 0, 0, 'p'},
	{"remove-duct-tape", 0, 0, DUCTAPE},
	{"tile-size", 1, 0, TILESZ},
	{"check-render-cpyfn", 0, 0, CHCK_RENDER},
	{NULL, 0, 0, 0},
};

igt_simple_main_args("ds:g:c:t:rbuxmo:fp:",
		     long_options, NULL, parse_options, NULL)
{
	int i, j;
	unsigned *current_permutation, *tmp_permutation;

	drm_fd = drm_open_driver(DRIVER_INTEL);
	devid = intel_get_drm_devid(drm_fd);

	/* start our little helper early before too may allocations occur */
	if (options.use_signal_helper)
		igt_fork_signal_helper();

	init();

	check_render_copyfunc();

	tile_permutation = malloc(num_total_tiles*sizeof(uint32_t));
	current_permutation = malloc(num_total_tiles*sizeof(uint32_t));
	tmp_permutation = malloc(num_total_tiles*sizeof(uint32_t));
	igt_assert(tile_permutation);
	igt_assert(current_permutation);
	igt_assert(tmp_permutation);

	fan_out();

	for (i = 0; i < options.total_rounds; i++) {
		igt_info("round %i\n", i);
		if (i % 64 == 63) {
			fan_in_and_check();
			igt_info("everything correct after %i rounds\n", i + 1);
		}

		target_set = (current_set + 1) & 1;
		init_set(target_set);

		for (j = 0; j < num_total_tiles; j++)
			current_permutation[j] = j;
		igt_permute_array(current_permutation, num_total_tiles, exchange_uint);

		copy_tiles(current_permutation);

		memcpy(tmp_permutation, tile_permutation, sizeof(unsigned)*num_total_tiles);

		/* accumulate the permutations */
		for (j = 0; j < num_total_tiles; j++)
			tile_permutation[j] = current_permutation[tmp_permutation[j]];

		current_set = target_set;
	}

	fan_in_and_check();

	igt_info("num failed tiles %u, max incoherent bytes %zd\n", stats.num_failed, stats.max_failed_reads * sizeof(uint32_t));

	intel_bb_destroy(ibb);
	buf_ops_destroy(bops);

	drm_close_driver(drm_fd);

	igt_stop_signal_helper();
}
