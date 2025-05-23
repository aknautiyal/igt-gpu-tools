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
 * Authors:
 *    Tiago Vignatti <tiago.vignatti at intel.com>
 */

/**
 * TEST: kms mmap write crc
 * Category: Display
 * Description: Use the display CRC support to validate mmap write to an already
 *              uncached future scanout buffer.
 * Driver requirement: i915, xe
 * Mega feature: General Display Features
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drmtest.h"
#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_pipe_crc.h"
#include "intel_chipset.h"
#include "ioctl_wrappers.h"
#include "igt_aux.h"

/**
 * SUBTEST: main
 * Description: Tests that caching mode has become UC/WT and flushed using mmap write
 */

IGT_TEST_DESCRIPTION(
   "Use the display CRC support to validate mmap write to an already uncached future scanout buffer.");

#define ROUNDS 10

typedef struct {
	int drm_fd;
	igt_display_t display;
	struct igt_fb fb[2];
	igt_output_t *output;
	igt_plane_t *primary;
	enum pipe pipe;
	igt_crc_t ref_crc;
	igt_pipe_crc_t *pipe_crc;
	uint32_t devid;
} data_t;

static int ioctl_sync = true;
int dma_buf_fd;

static char *dmabuf_mmap_framebuffer(int drm_fd, struct igt_fb *fb)
{
	char *ptr = NULL;

	dma_buf_fd = prime_handle_to_fd_for_mmap(drm_fd, fb->gem_handle);
	igt_skip_on(dma_buf_fd == -1 && errno == EINVAL);

	ptr = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, dma_buf_fd, 0);
	igt_assert(ptr != MAP_FAILED);

	return ptr;
}

static void test(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	struct igt_fb *fb = &data->fb[1];
	drmModeModeInfo *mode;
	cairo_t *cr;
	char *ptr;
	void *buf;
	igt_crc_t crc;

	mode = igt_output_get_mode(output);

	/* create a non-white fb where we can write later */
	igt_create_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
		      DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, fb);

	ptr = dmabuf_mmap_framebuffer(data->drm_fd, fb);

	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_test_pattern(cr, fb->width, fb->height);
	igt_put_cairo_ctx(cr);

	/* flip to it to make it UC/WC and fully flushed */
	igt_plane_set_fb(data->primary, fb);
	igt_display_commit(display);

	/* flip back the original white buffer */
	igt_plane_set_fb(data->primary, &data->fb[0]);
	igt_display_commit(display);

	if (is_i915_device(data->drm_fd) && !gem_has_lmem(data->drm_fd)) {
		uint32_t caching;

		/* make sure caching mode has become UC/WT */
		caching = gem_get_caching(data->drm_fd, fb->gem_handle);
		igt_assert(caching == I915_CACHING_NONE || caching == I915_CACHING_DISPLAY);
	}

	/*
	 * firstly demonstrate the need for DMA_BUF_SYNC_START ("begin_cpu_access")
	 */
	if (ioctl_sync)
		prime_sync_start(dma_buf_fd, true);

	/* use dmabuf pointer to make the other fb all white too */
	buf = malloc(fb->size);
	igt_assert(buf != NULL);
	memset(buf, 0xff, fb->size);
	memcpy(ptr, buf, fb->size);
	free(buf);

	/* and flip to it */
	igt_plane_set_fb(data->primary, fb);
	igt_display_commit(display);

	/* check that the crc is as expected, which requires that caches got flushed */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &data->ref_crc);

	/*
	 * now demonstrates the need for DMA_BUF_SYNC_END ("end_cpu_access")
	 */

	/* start over, writing non-white to the fb again and flip to it to make it
	 * fully flushed */
	cr = igt_get_cairo_ctx(data->drm_fd, fb);
	igt_paint_test_pattern(cr, fb->width, fb->height);
	igt_put_cairo_ctx(cr);

	igt_plane_set_fb(data->primary, fb);
	igt_display_commit(display);

	/* sync start, to move to CPU domain */
	if (ioctl_sync)
		prime_sync_start(dma_buf_fd, true);

	/* use dmabuf pointer in the same fb to make it all white */
	buf = malloc(fb->size);
	igt_assert(buf != NULL);
	memset(buf, 0xff, fb->size);
	memcpy(ptr, buf, fb->size);
	free(buf);

	/* if we don't change to the GTT domain again, the whites won't get flushed
	 * and therefore we demonstrates the need for sync end here */
	if (ioctl_sync)
		prime_sync_end(dma_buf_fd, true);

	do_or_die(drmModeDirtyFB(data->drm_fd, fb->fb_id, NULL, 0));

	/* check that the crc is as expected, which requires that caches got flushed */
	igt_pipe_crc_collect_crc(data->pipe_crc, &crc);
	igt_assert_crc_equal(&crc, &data->ref_crc);

	munmap(ptr, fb->size);
	close(dma_buf_fd);
}

static void prepare_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	drmModeModeInfo *mode;

	igt_display_reset(display);

	/* select the pipe we want to use */
	igt_output_set_pipe(output, data->pipe);

	mode = igt_output_get_mode(output);

	/* create a white reference fb and flip to it */
	igt_create_color_fb(data->drm_fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
			    1.0, 1.0, 1.0, &data->fb[0]);

	data->primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);

	igt_plane_set_fb(data->primary, &data->fb[0]);
	igt_display_commit(display);

	if (data->pipe_crc)
		igt_pipe_crc_free(data->pipe_crc);

	data->pipe_crc = igt_pipe_crc_new(data->drm_fd, data->pipe,
					  IGT_PIPE_CRC_SOURCE_AUTO);

	/* get reference crc for the white fb */
	igt_pipe_crc_collect_crc(data->pipe_crc, &data->ref_crc);
}

static void cleanup_crtc(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;

	igt_pipe_crc_free(data->pipe_crc);
	data->pipe_crc = NULL;

	igt_plane_set_fb(data->primary, NULL);

	igt_output_set_pipe(output, PIPE_NONE);
	igt_display_commit(display);

	igt_remove_fb(data->drm_fd, &data->fb[0]);
	igt_remove_fb(data->drm_fd, &data->fb[1]);
}

struct igt_helper_process hog;

/**
 * fork_cpuhog_helper:
 *
 * Fork a child process that loops indefinitely to consume CPU. This is used to
 * fill the CPU caches with random information so they can get stalled,
 * provoking incoherency with the GPU most likely.
 */
static void fork_cpuhog_helper(void)
{
	igt_fork_helper(&hog) {
		while (1) {
			usleep(10); /* quite ramdom really. */

			if ((int)getppid() == 1) /* Parent has died, so must we. */
				exit(0);
		}
	}
}

static int opt_handler(int opt, int opt_index, void *data)
{
	if (opt == 'n') {
		ioctl_sync = false;
		igt_info("set via cmd line to not use sync ioctls\n");
	} else {
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

static data_t data;

igt_main_args("n", NULL, NULL, opt_handler, NULL)
{
	int i;
	igt_output_t *output;
	enum pipe pipe;

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL | DRIVER_XE);

		data.devid = intel_get_drm_devid(data.drm_fd);

		kmstest_set_vt_graphics_mode();

		igt_require_pipe_crc(data.drm_fd);

		igt_display_require(&data.display, data.drm_fd);
		igt_display_require_output(&data.display);

		fork_cpuhog_helper();
	}

	igt_describe("Tests that caching mode has become UC/WT and flushed using mmap write");

	igt_subtest_with_dynamic("main") {
		for_each_pipe_with_valid_output(&data.display, pipe, output) {
			igt_display_reset(&data.display);

			igt_output_set_pipe(output, pipe);
			if (!intel_pipe_output_combo_valid(&data.display))
				continue;

			igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(pipe),
				      igt_output_name(output)) {
				data.output = output;
				data.pipe = pipe;

				igt_info("Using %d rounds for each pipe in the test\n", ROUNDS);
				prepare_crtc(&data);

				for (i = 0; i < ROUNDS; i++)
					test(&data);

				cleanup_crtc(&data);
			}
			/* once is enough */
			break;
		}
	}

	igt_fixture {
		igt_display_fini(&data.display);
		drm_close_driver(data.drm_fd);

		igt_stop_helper(&hog);
	}
}
