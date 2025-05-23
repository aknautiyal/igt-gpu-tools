/*
 * Copyright © 2019 Intel Corporation
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

/**
 * TEST: kms psr2 su
 * Category: Display
 * Description: Test PSR2 selective update
 * Driver requirement: i915, xe
 * Mega feature: PSR
 */

#include "igt.h"
#include "igt_sysfs.h"
#include "igt_psr.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>

/**
 * SUBTEST: frontbuffer-XRGB8888
 * Description: Test that selective update works when screen changes
 *
 * SUBTEST: page_flip-%s
 * Description: Test the selective update with %arg[1] when screen changes
 *
 * arg[1]:
 *
 * @NV12:        NV12 format
 * @P010:        P010 format
 * @XRGB8888:    XRGB8888 format
 */

IGT_TEST_DESCRIPTION("Test PSR2 selective update");

#define SQUARE_SIZE   100
#define SQUARE_OFFSET 100
/* each selective update block is 4 lines tall */
#define EXPECTED_NUM_SU_BLOCKS ((SQUARE_SIZE / 4) + (SQUARE_SIZE % 4 ? 1 : 0))

/*
 * Minimum is 15 as the number of frames to active PSR2 could be configured
 * to 15 frames plus a few more in case we miss a selective update between
 * debugfs reads.
 */
#define MAX_SCREEN_CHANGES 20

enum operations {
	PAGE_FLIP,
	FRONTBUFFER,
	LAST
};

static const uint32_t formats_page_flip[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_NV12,
	DRM_FORMAT_P010,
	DRM_FORMAT_INVALID,
};

static const uint32_t formats_frontbuffer[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_INVALID,
};

static const uint32_t *formats[] = {
				    [PAGE_FLIP] = formats_page_flip,
				    [FRONTBUFFER] = formats_frontbuffer,
};

static const char *op_str(enum operations op)
{
	static const char * const name[] = {
		[PAGE_FLIP] = "page_flip",
		[FRONTBUFFER] = "frontbuffer"
	};

	return name[op];
}

typedef struct {
	int drm_fd;
	int debugfs_fd;
	igt_display_t display;
	drmModeModeInfo *mode;
	igt_output_t *output;
	struct igt_fb fb[2];
	enum operations op;
	uint32_t format;
	cairo_t *cr;
	int change_screen_timerfd;
	uint32_t screen_changes;
} data_t;

static void setup_output(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe pipe;

	for_each_pipe_with_valid_output(display, pipe, output) {
		drmModeConnectorPtr c = output->config.connector;

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		igt_display_reset(display);
		igt_output_set_pipe(output, pipe);
		if (!intel_pipe_output_combo_valid(display))
			continue;

		data->output = output;
		data->mode = igt_output_get_mode(output);

		return;
	}
}

static void display_init(data_t *data)
{
	igt_display_require(&data->display, data->drm_fd);
	setup_output(data);

	igt_require_f(data->output, "No available output found\n");
	igt_require_f(data->mode, "No available mode found on %s\n", data->output->name);
}

static void display_fini(data_t *data)
{
	igt_display_fini(&data->display);
}

static void prepare(data_t *data, igt_output_t *output)
{
	igt_plane_t *primary;

	/* all green frame */
	igt_create_color_fb(data->drm_fd,
			    data->mode->hdisplay, data->mode->vdisplay,
			    data->format,
			    DRM_FORMAT_MOD_LINEAR,
			    0.0, 1.0, 0.0,
			    &data->fb[0]);

	if (data->op == PAGE_FLIP) {
		cairo_t *cr;

		igt_create_color_fb(data->drm_fd,
				    data->mode->hdisplay, data->mode->vdisplay,
				    data->format,
				    DRM_FORMAT_MOD_LINEAR,
				    0.0, 1.0, 0.0,
				    &data->fb[1]);

		cr = igt_get_cairo_ctx(data->drm_fd, &data->fb[1]);
		/* paint a white square */
		igt_paint_color_alpha(cr, SQUARE_OFFSET, SQUARE_OFFSET, SQUARE_SIZE, SQUARE_SIZE,
				      1.0, 1.0, 1.0, 1.0);
		igt_put_cairo_ctx(cr);
	} else if (data->op == FRONTBUFFER) {
		data->cr = igt_get_cairo_ctx(data->drm_fd, &data->fb[0]);
	}

	primary = igt_output_get_plane_type(output,
					    DRM_PLANE_TYPE_PRIMARY);

	igt_plane_set_fb(primary, &data->fb[0]);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);
}

static bool update_screen_and_test(data_t *data, igt_output_t *output)
{
	uint16_t su_blocks;
	bool ret = false;

	switch (data->op) {
	case PAGE_FLIP: {
		igt_plane_t *primary;

		primary = igt_output_get_plane_type(output,
						    DRM_PLANE_TYPE_PRIMARY);

		if (igt_plane_has_prop(primary, IGT_PLANE_FB_DAMAGE_CLIPS)) {
			struct drm_mode_rect clip;

			clip.x1 = clip.y1 = SQUARE_OFFSET;
			clip.x2 = clip.y2 = SQUARE_OFFSET + SQUARE_SIZE;

			igt_plane_replace_prop_blob(primary, IGT_PLANE_FB_DAMAGE_CLIPS,
						    &clip, sizeof(clip));
		}

		igt_plane_set_fb(primary, &data->fb[data->screen_changes & 1]);
		igt_display_commit2(&data->display, COMMIT_ATOMIC);
		break;
	}
	case FRONTBUFFER: {
		drmModeClip clip;

		clip.x1 = clip.y1 = SQUARE_OFFSET;
		clip.x2 = clip.y2 = SQUARE_OFFSET + SQUARE_SIZE;

		if (data->screen_changes & 1) {
			/* go back to all green frame with a square */
			igt_paint_color_alpha(data->cr, SQUARE_OFFSET,
					      SQUARE_OFFSET, SQUARE_SIZE,
					      SQUARE_SIZE, 1.0, 1.0, 1.0, 1.0);
		} else {
			/* go back to all green frame */
			igt_paint_color_alpha(data->cr, SQUARE_OFFSET,
					      SQUARE_OFFSET, SQUARE_SIZE,
					      SQUARE_SIZE, 0, 1.0, 0, 1.0);
		}

		drmModeDirtyFB(data->drm_fd, data->fb[0].fb_id, &clip, 1);
		break;
	}
	default:
		igt_assert_f(data->op, "Operation not handled\n");
	}

	if (psr2_wait_su(data->debugfs_fd, &su_blocks)) {
		ret = su_blocks == EXPECTED_NUM_SU_BLOCKS;

		if (!ret)
			igt_debug("Not matching SU blocks read: %u\n", su_blocks);
	}

	return ret;
}

static void run(data_t *data, igt_output_t *output)
{
	bool result = false;

	igt_assert(psr_wait_entry(data->debugfs_fd, PSR_MODE_2, output));

	for (data->screen_changes = 0;
	     data->screen_changes < MAX_SCREEN_CHANGES && !result;
	     data->screen_changes++) {
		uint64_t exp;
		int r;

		r = read(data->change_screen_timerfd, &exp, sizeof(exp));
		if (r == sizeof(uint64_t) && exp)
			result = update_screen_and_test(data, output);
	}

	igt_assert_f(result,
		     "No matching selective update blocks read from debugfs\n");

	psr_sink_error_check(data->debugfs_fd, PSR_MODE_2, output);
}

static void cleanup(data_t *data, igt_output_t *output)
{
	igt_plane_t *primary;

	primary = igt_output_get_plane_type(output,
					    DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);
	igt_display_commit2(&data->display, COMMIT_ATOMIC);

	if (data->op == PAGE_FLIP)
		igt_remove_fb(data->drm_fd, &data->fb[1]);
	else if (data->op == FRONTBUFFER)
		igt_put_cairo_ctx(data->cr);

	igt_remove_fb(data->drm_fd, &data->fb[0]);
}

static int check_psr2_support(data_t *data, enum pipe pipe)
{
	int status;

	igt_output_t *output;
	igt_display_t *display = &data->display;

	igt_display_reset(display);
	output = data->output;
	igt_output_set_pipe(output, pipe);

	prepare(data, output);
	status = psr_wait_entry(data->debugfs_fd, PSR_MODE_2, output);
	cleanup(data, output);

	return status;
}

igt_main
{
	data_t data = {};
	enum pipe pipe;
	int r, i;
	igt_output_t *outputs[IGT_MAX_PIPES * IGT_MAX_PIPES];
	int pipes[IGT_MAX_PIPES * IGT_MAX_PIPES];
	int n_pipes = 0;

	igt_fixture {
		struct itimerspec interval;

		data.drm_fd = drm_open_driver_master(DRIVER_INTEL | DRIVER_XE);
		data.debugfs_fd = igt_debugfs_dir(data.drm_fd);
		kmstest_set_vt_graphics_mode();

		igt_require_f(psr_sink_support(data.drm_fd,
					       data.debugfs_fd,
					       PSR_MODE_2, NULL),
			      "Sink does not support PSR2\n");

		igt_require_f(intel_display_ver(intel_get_drm_devid(data.drm_fd)) < 13,
			      "Registers used by this test do not work on display 13+\n");

		display_init(&data);

		/* Test if PSR2 can be enabled */
		igt_require_f(psr_enable(data.drm_fd,
					 data.debugfs_fd, PSR_MODE_2, NULL),
			      "Error enabling PSR2\n");
		data.op = FRONTBUFFER;
		data.format = DRM_FORMAT_XRGB8888;

		/* blocking timerfd */
		data.change_screen_timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
		igt_require(data.change_screen_timerfd != -1);
		/* Changing screen at 30hz to support 30hz panels */
		interval.it_value.tv_nsec = NSEC_PER_SEC / 30;
		interval.it_value.tv_sec = 0;
		interval.it_interval.tv_nsec = interval.it_value.tv_nsec;
		interval.it_interval.tv_sec = interval.it_value.tv_sec;
		r = timerfd_settime(data.change_screen_timerfd, 0, &interval, NULL);
		igt_require_f(r != -1, "Error setting timerfd\n");

		for_each_pipe_with_valid_output(&data.display, pipe, data.output) {
			if (check_psr2_support(&data, pipe)) {
				pipes[n_pipes] = pipe;
				outputs[n_pipes] = data.output;
				n_pipes++;
			}
		}
	}

	for (data.op = PAGE_FLIP; data.op < LAST; data.op++) {
		const uint32_t *format = formats[data.op];

		while (*format != DRM_FORMAT_INVALID) {
			data.format = *format++;
			igt_describe("Test that selective update works when screen changes");
			igt_subtest_with_dynamic_f("%s-%s", op_str(data.op), igt_format_str(data.format)) {
				for (i = 0; i < n_pipes; i++) {
					igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(pipes[i]),
							igt_output_name(outputs[i])) {
						igt_output_set_pipe(outputs[i], pipes[i]);
						if (data.op == FRONTBUFFER &&
						    intel_display_ver(intel_get_drm_devid(data.drm_fd)) >= 12) {
							/*
							 * FIXME: Display 12+ platforms now have PSR2
							 * selective fetch enabled by default but we
							 * still can't properly handle frontbuffer
							 * rendering, so right it does full frame
							 * fetches at every frontbuffer rendering.
							 * So it is expected that this test will fail
							 * in display 12+ platform for now.
							 */
							igt_skip("PSR2 selective fetch is doing full frame fetches for frontbuffer rendering\n");
						}
						prepare(&data, outputs[i]);
						run(&data, outputs[i]);
						cleanup(&data, outputs[i]);
					}
				}
			}
		}
	}

	igt_fixture {
		close(data.debugfs_fd);
		display_fini(&data);
		drm_close_driver(data.drm_fd);
	}
}
