/*
 * Copyright © 2014 Intel Corporation
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
 * TEST: kms 3d
 * Category: Display
 * Description: Tests 3D mode setting.
 * Driver requirement: i915, xe
 * Mega feature: General Display Features
 */

#include "igt.h"
#include "xe/xe_query.h"

/**
 * SUBTEST:
 * Description: Tests 3D mode setting.
 */

IGT_TEST_DESCRIPTION("Tests 3D mode setting.");

igt_simple_main
{
	int drm_fd;
	drmModeRes *res;
	drmModeConnector *connector = NULL;
	const struct edid *edid;
	int mode_count, connector_id;

	drm_fd = drm_open_driver_master(DRIVER_ANY);

	res = drmModeGetResources(drm_fd);
	igt_require(res);

	igt_assert_f(drmSetClientCap(drm_fd, DRM_CLIENT_CAP_STEREO_3D, 1) >= 0,
		     "Failed to enable STEREO_3D capability.\n");

	/* find an hdmi connector */
	for (int i = 0; i < res->count_connectors; i++) {
		connector = drmModeGetConnectorCurrent(drm_fd, res->connectors[i]);
		if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA)
			break;
		drmModeFreeConnector(connector);
		connector = NULL;
	}
	igt_require_f(connector, "No HDMI connector found.\n");

	kmstest_unset_all_crtcs(drm_fd, res);

	edid = igt_kms_get_3d_edid();

	kmstest_force_edid(drm_fd, connector, edid);
	if (!kmstest_force_connector(drm_fd, connector, FORCE_CONNECTOR_ON))
		igt_skip("Could not force connector on\n");

	connector_id = connector->connector_id;

	/* check for 3D modes */
	mode_count = 0;
	connector = drmModeGetConnectorCurrent(drm_fd, connector_id);
	for (int i = 0; i < connector->count_modes; i++) {
		if (connector->modes[i].flags & DRM_MODE_FLAG_3D_MASK)
			mode_count++;
	}

	igt_assert_f(mode_count, "3D modes not detected.\n");

	/* set 3D modes */
	igt_info("Testing:\n");
	for (int i = 0; i < connector->count_modes; i++) {
		int fb_id;
		struct kmstest_connector_config config;
		int crtc_mask = -1;
		int ret;

		if (!(connector->modes[i].flags & DRM_MODE_FLAG_3D_MASK))
			continue;

		/* create a configuration */
		ret = kmstest_get_connector_config(drm_fd, connector_id,
						   crtc_mask, &config);
		if (ret != true) {
			igt_info("Error creating configuration for:\n  ");
			kmstest_dump_mode(&connector->modes[i]);

			continue;
		}

		igt_info("  ");
		kmstest_dump_mode(&connector->modes[i]);

		/* create stereo framebuffer */
		fb_id = igt_create_stereo_fb(drm_fd, &connector->modes[i],
					     igt_bpp_depth_to_drm_format(32, 24),
					     DRM_FORMAT_MOD_LINEAR);

		ret = drmModeSetCrtc(drm_fd, config.crtc->crtc_id, fb_id, 0, 0,
				     &connector->connector_id, 1,
				     &connector->modes[i]);

		igt_assert(ret == 0);
	}

	kmstest_force_connector(drm_fd, connector, FORCE_CONNECTOR_UNSPECIFIED);
	kmstest_force_edid(drm_fd, connector, NULL);

	drmModeFreeConnector(connector);

	drm_close_driver(drm_fd);
}
