/*
 * Copyrights 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * TEST: kms bw
 * Category: Display
 * Description: BW test with different resolutions
 * Driver requirement: i915, xe
 * Mega feature: Display Latency/Bandwidth
 */

#include "drm_mode.h"
#include "igt.h"
#include "drm.h"
#include <stdio.h>
#include <xf86drmMode.h>

/**
 * SUBTEST: linear-tiling-%d-displays-%s
 * Description: bw test with %arg[2]
 *
 * arg[1].values: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
 *
 * arg[2]:
 *
 * @1920x1080p:       1920x1080 resolution
 * @2560x1440p:       2560x1440 resolution
 * @3840x2160p:       3840x2160 resolution
 * @2160x1440p:       2160x1440 resolution
 *
 * SUBTEST: connected-linear-tiling-%d-displays-%s
 * Description: bw test with %arg[2]
 *
 * arg[1].values: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
 *
 * arg[2]:
 *
 * @1920x1080p:       1920x1080 resolution
 * @2560x1440p:       2560x1440 resolution
 * @3840x2160p:       3840x2160 resolution
 * @2160x1440p:       2160x1440 resolution
 *
 */

/* Common test data. */
typedef struct data {
        igt_display_t display;
        igt_plane_t *primary[IGT_MAX_PIPES];
        igt_output_t *output[IGT_MAX_PIPES];
	igt_output_t *connected_output[IGT_MAX_PIPES];
        igt_pipe_t *pipe[IGT_MAX_PIPES];
        igt_pipe_crc_t *pipe_crc[IGT_MAX_PIPES];
        drmModeModeInfo mode[IGT_MAX_PIPES];
        enum pipe pipe_id[IGT_MAX_PIPES];
        int w[IGT_MAX_PIPES];
        int h[IGT_MAX_PIPES];
        int fd;
	int connected_outputs;
} data_t;

static drmModeModeInfo test_mode[] = {
	{ 147840,
	1920, 1968, 2000, 2200, 0,
	1080, 1083, 1089, 1120, 0,
	60,
	DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_PHSYNC,
	0x48,
	"1920x1080p\0",
	}, /* test_mode_1 */

	{ 312250,
	2560, 2752, 3024, 3488, 0,
	1440, 1443, 1448, 1493, 0,
	60,
	DRM_MODE_FLAG_NHSYNC,
	0x40,
	"2560x1440p\0",
	}, /* test_mode_2 */

	{ 533000,
	3840, 3888, 3920, 4000, 0,
	2160, 2163, 2168, 2222, 0,
	60,
	DRM_MODE_FLAG_NHSYNC,
	0x40,
	"3840x2160p\0",
	}, /* test_mode_3 */

	{ 207800,
	2160, 2208, 2240, 2340, 0,
	1440, 1443, 1449, 1480, 0,
	60,
	DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_PHSYNC,
	0x48,
	"2160x1440p\0",
	}, /* test_mode_4 */

};

static void test_init(data_t *data, bool physical)
{
	igt_display_t *display = &data->display;
	int i, max_pipes = display->n_pipes;
	igt_output_t *output;
	data->connected_outputs = 0;

	for_each_pipe(display, i) {
		data->pipe_id[i] = i;
		data->pipe[i] = &data->display.pipes[data->pipe_id[i]];
		data->primary[i] = igt_pipe_get_plane_type(
			data->pipe[i], DRM_PLANE_TYPE_PRIMARY);
		data->pipe_crc[i] =
			igt_pipe_crc_new(data->fd, data->pipe_id[i],
					 IGT_PIPE_CRC_SOURCE_AUTO);
	}

	for (i = 0; i < display->n_outputs && i < max_pipes; i++) {
		if (!data->pipe[i] && !physical)
			continue;

		output = &display->outputs[i];
		data->output[i] = output;

		/* Only allow physically connected displays for the tests. */
		if (!igt_output_is_connected(output))
			continue;
		data->connected_output[data->connected_outputs++] = output;

		igt_assert(kmstest_get_connector_default_mode(
			data->fd, output->config.connector, &data->mode[i]));

		data->w[i] = data->mode[i].hdisplay;
		data->h[i] = data->mode[i].vdisplay;
	}


	igt_require(data->output[0]);
	igt_display_reset(display);
}

static void test_fini(data_t *data)
{
	igt_display_t *display = &data->display;
	int i;

	for_each_pipe(display, i) {
		if (data->pipe_crc[i])
			igt_pipe_crc_free(data->pipe_crc[i]);
	}

	igt_display_reset(display);
	igt_display_commit_atomic(display, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
}

/* Forces a mode for a connector. */
static void force_output_mode(data_t *d, igt_output_t *output,
			      const drmModeModeInfo *mode)
{
	/* This allows us to create a virtual sink. */
	if (!igt_output_is_connected(output)) {
		kmstest_force_edid(d->fd, output->config.connector,
				   igt_kms_get_4k_edid());

		kmstest_force_connector(d->fd, output->config.connector,
					FORCE_CONNECTOR_DIGITAL);
	}

	igt_output_override_mode(output, mode);
}

static void run_test_linear_tiling(data_t *data, int pipe, const drmModeModeInfo *mode, bool physical) {
	igt_display_t *display = &data->display;
	igt_output_t *output;
	struct igt_fb buffer[IGT_MAX_PIPES];
	igt_crc_t zero, captured[IGT_MAX_PIPES];
	int i = 0, num_pipes = 0;
	enum pipe p;
	int ret;

	/* Cannot use igt_display_get_n_pipes() due to fused pipes on i915 where they do
	 * not give the numver of valid crtcs and always return IGT_MAX_PIPES */
	for_each_pipe(display, p) num_pipes++;

	igt_skip_on_f(pipe >= num_pipes,
                      "ASIC does not have %d pipes\n", pipe);

	test_init(data, physical);

	igt_skip_on_f(physical && pipe >= data->connected_outputs,
		      "Only %d connected need %d connected\n",data->connected_outputs, pipe+1);

	/* create buffers */
	for (i = 0; i <= pipe; i++) {
		output = physical ? data->connected_output[i] : data->output[i];
		if (!output) {
			continue;
		}

		force_output_mode(data, output, mode);

		igt_create_color_fb(display->drm_fd, mode->hdisplay,
				    mode->vdisplay, DRM_FORMAT_XRGB8888,
				    DRM_FORMAT_MOD_LINEAR, 1.f, 0.f, 0.f,
				    &buffer[i]);

		igt_output_set_pipe(output, i);

		igt_plane_set_fb(data->primary[i], &buffer[i]);
		igt_info("Assigning pipe %s to output %s with mode %s\n",
			 kmstest_pipe_name(i), igt_output_name(output), mode->name);
	}

	ret = igt_display_try_commit_atomic(display,
					    DRM_MODE_ATOMIC_ALLOW_MODESET |
					    DRM_MODE_ATOMIC_TEST_ONLY,
					    NULL);
	igt_skip_on_f(ret != 0, "Unsupported mode\n");

	igt_display_commit_atomic(display, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

	for (i = 0; i <= pipe; i++) {
		output = physical ? data->connected_output[i] : data->output[i];
		if (!output) {
			continue;
		}

		igt_pipe_crc_collect_crc(data->pipe_crc[i], &captured[i]);
		igt_assert_f(!igt_check_crc_equal(&zero, &captured[i]),
			     "CRC is zero\n");
	}

	for (i = pipe; i >= 0; i--) {
		output = physical ? data->connected_output[i] : data->output[i];
		if (!output)
			continue;

		igt_remove_fb(display->drm_fd, &buffer[i]);
	}

	test_fini(data);
}

igt_main
{
	data_t data;
	int i = 0, j = 0;

	memset(&data, 0, sizeof(data));

	igt_fixture
	{
		data.fd = drm_open_driver_master(DRIVER_ANY);

		kmstest_set_vt_graphics_mode();

		igt_display_require(&data.display, data.fd);
		igt_require(&data.display.is_atomic);
		igt_display_require_output(&data.display);

	}

	/* We're not using for_each_pipe_static because we need the
	 * _amount_ of pipes */
	for (i = 0; i < IGT_MAX_PIPES; i++) {
		for (j = 0; j < ARRAY_SIZE(test_mode); j++) {
			igt_subtest_f("linear-tiling-%d-displays-%s", i+1,
			      test_mode[j].name)
			run_test_linear_tiling(&data, i, &test_mode[j], false);
		}
	}

        for (i = 0; i < IGT_MAX_PIPES; i++) {
                for (j = 0; j < ARRAY_SIZE(test_mode); j++) {
                        igt_subtest_f("connected-linear-tiling-%d-displays-%s", i+1,
                              test_mode[j].name)
                        run_test_linear_tiling(&data, i, &test_mode[j], true);
                }
        }


	igt_fixture
	{
		igt_display_fini(&data.display);
	}
}
