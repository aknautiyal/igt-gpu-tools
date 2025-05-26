/*
 * Test for I915_GET_AVAILABLE_CRTC_FOR_CONNECTOR ioctl
 *
 * Copyright © 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "igt.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/i915_drm.h>

/**
 * TEST: kms vblank
 * Category: Display
 * Description: Test speed of WaitVblank.
 * Driver requirement: i915, xe
 * Mega feature: General Display Features
 */

/**
 * SUBTEST: get-available-crtc
 * Description: Test the i915 ioctl to get available CRTC for a connector
 */

IGT_TEST_DESCRIPTION("Test the i915 ioctl to get available CRTC for a connector");

static int get_available_crtc(int fd, uint32_t connector_id)
{
	int crtc_id;

	crtc_id = ioctl(fd, DRM_IOCTL_I915_GET_AVAILABLE_CRTC_FOR_CONNECTOR, &connector_id);
	if (crtc_id < 0)
		return -errno;

	return crtc_id;
}

igt_main
{
	int drm_fd = -1;
	int pipe;
	bool found = false;
	int crtc_id;
	igt_display_t display;
	igt_output_t *output;
	struct igt_fb fb;
	drmModeModeInfo *mode;
	igt_plane_t *primary;

	igt_fixture {
		drm_fd = drm_open_driver(DRIVER_INTEL);
		igt_require(drm_fd >= 0);
		igt_display_require(&display, drm_fd);
	}

	igt_subtest("get-available-crtc") {
		drmModeRes *res = drmModeGetResources(drm_fd);
		igt_require(res);

		for (int i = 0; i < res->count_connectors; i++) {
			drmModeConnector *connector = drmModeGetConnector(drm_fd, res->connectors[i]);
			if (!connector || connector->connection != DRM_MODE_CONNECTED) {
				drmModeFreeConnector(connector);
				continue;
			}

			igt_info("Testing connector %u\n", connector->connector_id);

			crtc_id = get_available_crtc(drm_fd, connector->connector_id);

			if (crtc_id > 0) {
				igt_info("Available CRTC for connector %u: %d\n", connector->connector_id, crtc_id);
				pipe = kmstest_get_pipe_from_crtc_id(drm_fd, crtc_id);
				igt_assert(pipe != PIPE_NONE);

				output = igt_output_from_connector(&display, connector);
				igt_require(output);

				igt_info("Using (pipe %s + %s) to run the subtest\n", kmstest_pipe_name(pipe), igt_output_name(output));

				if (!igt_pipe_connector_valid(pipe, output)) {
					igt_info("pipe %s + %s is not valid\n", kmstest_pipe_name(pipe), igt_output_name(output));
					continue;
				}

				igt_output_set_pipe(output, pipe);
				mode = igt_output_get_mode(output);

				igt_create_color_fb(drm_fd, mode->hdisplay, mode->vdisplay,
						    DRM_FORMAT_XRGB8888,
						    DRM_FORMAT_MOD_LINEAR,
						    1.0, 1.0, 1.0, &fb);
				primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);

				igt_plane_set_fb(primary, &fb);

				igt_display_commit2(&display, COMMIT_ATOMIC);

				igt_output_set_pipe(output, PIPE_NONE);
				igt_display_commit2(&display, COMMIT_ATOMIC);

				found = true;
			} else {
				igt_info("No available CRTC for connector %u (errno=%d: %s)\n",
				connector->connector_id, -crtc_id, strerror(-crtc_id));
			}

            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(res);

        igt_assert_f(found, "No available CRTC found for any connector\n");
    }

    igt_fixture {
        close(drm_fd);
    }
}
