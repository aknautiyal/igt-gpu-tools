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

/**
 * TEST: kms pm dc
 * Category: Display
 * Description: Tests to validate display power DC states.
 * Driver requirement: i915, xe
 * Mega feature: Display Power Management
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "igt.h"
#include "igt_kmod.h"
#include "igt_psr.h"
#include "igt_sysfs.h"
#include "igt_device.h"
#include "igt_device_scan.h"
#include "limits.h"
#include "time.h"
#include "igt_pm.h"
#include "intel_common.h"

/**
 * SUBTEST: dc3co-vpb-simulation
 * Description: Make sure that system enters DC3CO when PSR2 is active and system
 *              is in SLEEP state
 *
 * SUBTEST: dc5-dpms
 * Description: Validate display engine entry to DC5 state while all connectors's
 *              DPMS property set to OFF
 *
 * SUBTEST: dc5-dpms-negative
 * Description: Validate negative scenario of DC5 display engine entry to DC5 state
 *              while all connectors's DPMS property set to ON
 *
 * SUBTEST: dc5-psr
 * Description: This test validates display engine entry to DC5 state while PSR is active
 *
 * SUBTEST: dc6-dpms
 * Description: Validate display engine entry to DC6 state while all connectors's
 *              DPMS property set to OFF
 *
 * SUBTEST: dc6-psr
 * Description: This test validates display engine entry to DC6 state while PSR is active
 *
 * SUBTEST: dc9-dpms
 * Description: This test validates display engine entry to DC9 state
 *
 * SUBTEST: deep-pkgc
 * Description: This test validates display engine entry to PKGC10 state for extended vblank
 *
 * SUBTEST: dc5-retention-flops
 * Description: This test validates display engine entry to DC5 state while PSR is active on Pipe B
 */

/* DC State Flags */
#define CHECK_DC5	(1 << 0)
#define CHECK_DC6	(1 << 1)
#define CHECK_DC3CO	(1 << 2)

#define PWR_DOMAIN_INFO "i915_power_domain_info"
#define RPM_STATUS "i915_runtime_pm_status"
#define KMS_HELPER "/sys/module/drm_kms_helper/parameters/"
#define PACKAGE_CSTATE_PATH  "pmc_core/package_cstate_show"
#define KMS_POLL_DISABLE 0
#define DC9_RESETS_DC_COUNTERS(devid) (!(IS_DG1(devid) || IS_DG2(devid) || intel_display_ver(devid) >= 14))
#define SEC 1
#define MSEC (SEC * 1000)

IGT_TEST_DESCRIPTION("Tests to validate display power DC states.");

bool kms_poll_saved_state;

typedef struct {
	double r, g, b;
} color_t;

typedef struct {
	int drm_fd;
	int msr_fd;
	int debugfs_fd;
	int debugfs_root_fd;
	uint32_t devid;
	char *debugfs_dump;
	igt_display_t display;
	struct igt_fb fb_white, fb_rgb, fb_rgr;
	enum psr_mode op_psr_mode;
	drmModeModeInfo *mode;
	igt_output_t *output;
	bool runtime_suspend_disabled;
} data_t;

static bool dc_state_wait_entry(int drm_fd, int dc_flag, int prev_dc_count);
static void check_dc_counter(data_t *data, int dc_flag, uint32_t prev_dc_count);

static void set_output_on_pipe_b(data_t *data)
{
	igt_display_t *display = &data->display;
	igt_output_t *output;
	enum pipe pipe;

	for_each_pipe_with_valid_output(display, pipe, output) {
		drmModeConnectorPtr c = output->config.connector;

		/* DC5 with PIPE_B transaction */
		if (pipe != PIPE_B)
			continue;

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		igt_output_set_pipe(output, pipe);
		if (!intel_pipe_output_combo_valid(display))
			continue;

		data->output = output;
		data->mode = igt_output_get_mode(output);
	}
}

static void setup_output(data_t *data)
{
	int disp_ver = intel_display_ver(data->devid);
	igt_display_t *display = &data->display;
	igt_output_t *output;
	bool is_low_power;
	enum pipe pipe;

	for_each_pipe_with_valid_output(display, pipe, output) {
		drmModeConnectorPtr c = output->config.connector;

		if (disp_ver >= 13) {
			if (disp_ver == 20 || IS_BATTLEMAGE(data->devid) || IS_DG2(data->devid))
				is_low_power = (pipe == PIPE_A);
			else
				is_low_power = (pipe == PIPE_A || pipe == PIPE_B);
		} else {
			is_low_power = (pipe == PIPE_A);
		}

		igt_skip_on_f(!is_low_power, "Low power pipe was not selected for the DC5 transaction.\n");

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		igt_output_set_pipe(output, pipe);
		data->output = output;
		data->mode = igt_output_get_mode(output);

		return;
	}
}

static void display_fini(data_t *data)
{
	igt_display_fini(&data->display);
}

static void cleanup_dc_psr(data_t *data)
{
	igt_plane_t *primary;

	primary = igt_output_get_plane_type(data->output,
					    DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);
	igt_display_commit(&data->display);
	igt_remove_fb(data->drm_fd, &data->fb_white);
}

static void cleanup_dc3co_fbs(data_t *data)
{
	igt_plane_t *primary;

	primary = igt_output_get_plane_type(data->output,
					    DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);
	/* Clear Frame Buffers */
	igt_display_commit(&data->display);
	igt_remove_fb(data->drm_fd, &data->fb_rgb);
	igt_remove_fb(data->drm_fd, &data->fb_rgr);
}

static void paint_rectangles(data_t *data,
			     drmModeModeInfo *mode,
			     color_t *colors,
			     igt_fb_t *fb)
{
	cairo_t *cr = igt_get_cairo_ctx(data->drm_fd, fb);
	int i, l = mode->hdisplay / 3;
	int rows_remaining = mode->hdisplay % 3;

	/* Paint 3 solid rectangles. */
	for (i = 0 ; i < 3; i++) {
		igt_paint_color(cr, i * l, 0, l, mode->vdisplay,
				colors[i].r, colors[i].g, colors[i].b);
	}

	if (rows_remaining > 0)
		igt_paint_color(cr, i * l, 0, rows_remaining, mode->vdisplay,
				colors[i - 1].r, colors[i - 1].g,
				colors[i - 1].b);

	igt_put_cairo_ctx(cr);
}

static void setup_primary(data_t *data)
{
	igt_plane_t *primary;

	primary = igt_output_get_plane_type(data->output,
					    DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);
	igt_create_color_fb(data->drm_fd,
			    data->mode->hdisplay, data->mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    DRM_FORMAT_MOD_LINEAR,
			    1.0, 1.0, 1.0,
			    &data->fb_white);
	igt_plane_set_fb(primary, &data->fb_white);
	igt_display_commit(&data->display);
}

static void create_color_fb(data_t *data, igt_fb_t *fb, color_t *fb_color)
{
	int fb_id;

	fb_id = igt_create_fb(data->drm_fd,
			      data->mode->hdisplay,
			      data->mode->vdisplay,
			      DRM_FORMAT_XRGB8888,
			      DRM_FORMAT_MOD_LINEAR,
			      fb);
	igt_assert(fb_id);
	paint_rectangles(data, data->mode, fb_color, fb);
}

static uint32_t get_dc_counter(char *dc_data)
{
	char *e;
	long ret;
	char *s = strchr(dc_data, ':');

	igt_assert(s);
	s++;
	ret = strtol(s, &e, 10);
	igt_assert(((ret != LONG_MIN && ret != LONG_MAX) || errno != ERANGE) && e > s && *e == '\n' && ret >= 0);
	return ret;
}

static char *get_dc6_counter(const char *buf)
{
	char *str;

	str = strstr(buf, "DC5 -> DC6 count");
	if (!str)
		str = strstr(buf, "DC5 -> DC6 allowed count");

	return str;
}

static uint32_t read_dc_counter(uint32_t debugfs_fd, int dc_flag)
{
	char buf[4096];
	char *str;

	igt_debugfs_simple_read(debugfs_fd, "i915_dmc_info", buf, sizeof(buf));

	if (dc_flag & CHECK_DC5) {
		str = strstr(buf, "DC3 -> DC5 count");
		igt_assert_f(str, "DC5 counter is not available\n");
	} else if (dc_flag & CHECK_DC6) {
		str = get_dc6_counter(buf);
		igt_assert_f(str, "No DC6 counter available\n");
	} else if (dc_flag & CHECK_DC3CO) {
		str = strstr(buf, "DC3CO count");
		igt_assert_f(str, "DC3CO counter is not available\n");
	} else {
		igt_assert(!"reached");
		str = NULL;
	}

	return get_dc_counter(str);
}

static bool dc_state_wait_entry(int debugfs_fd, int dc_flag, int prev_dc_count)
{
	return igt_wait(read_dc_counter(debugfs_fd, dc_flag) >
			prev_dc_count, 3000, 100);
}

static const char *dc_state_name(int dc_flag)
{
	if (dc_flag & CHECK_DC3CO)
		return "DC3CO";
	else if (dc_flag & CHECK_DC5)
		return "DC5";
	else
		return "DC6";
}

static void check_dc_counter(data_t *data, int dc_flag, uint32_t prev_dc_count)
{
	igt_assert_f(dc_state_wait_entry(data->debugfs_fd, dc_flag, prev_dc_count),
		     "%s state is not achieved\n%s:\n%s\n", dc_state_name(dc_flag), PWR_DOMAIN_INFO,
		     data->debugfs_dump = igt_sysfs_get(data->debugfs_fd, PWR_DOMAIN_INFO));
}

static void check_dc_counter_negative(data_t *data, int dc_flag, uint32_t prev_dc_count)
{
	igt_assert_f(!dc_state_wait_entry(data->debugfs_fd, dc_flag, prev_dc_count),
		     "%s state is achieved\n%s:\n%s\n", dc_state_name(dc_flag), PWR_DOMAIN_INFO,
		     data->debugfs_dump = igt_sysfs_get(data->debugfs_fd, PWR_DOMAIN_INFO));
}

static void setup_videoplayback(data_t *data)
{
	color_t red_green_blue[] = {
		{ 1.0, 0.0, 0.0 },
		{ 0.0, 1.0, 0.0 },
		{ 0.0, 0.0, 1.0 },
	};
	color_t red_green_red[] = {
		{ 1.0, 0.0, 0.0 },
		{ 0.0, 1.0, 0.0 },
		{ 1.0, 0.0, 0.0 },
	};

	create_color_fb(data, &data->fb_rgb, red_green_blue);
	create_color_fb(data, &data->fb_rgr, red_green_red);
}

static void check_dc3co_with_videoplayback_like_load(data_t *data)
{
	igt_plane_t *primary;
	uint32_t dc3co_prev_cnt;
	int delay;
	time_t secs = 6;
	time_t startTime = time(NULL);

	primary = igt_output_get_plane_type(data->output,
					    DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);
	dc3co_prev_cnt = read_dc_counter(data->debugfs_fd, CHECK_DC3CO);

	/* Calculate delay to generate idle frame in usec*/
	delay = 1.5 * ((1000 * 1000) / data->mode->vrefresh);

	while (time(NULL) - startTime < secs) {
		igt_plane_set_fb(primary, &data->fb_rgb);
		igt_display_commit(&data->display);
		usleep(delay);

		igt_plane_set_fb(primary, &data->fb_rgr);
		igt_display_commit(&data->display);
		usleep(delay);
	}

	igt_require_f(dc_state_wait_entry(data->debugfs_fd,
		      CHECK_DC3CO, dc3co_prev_cnt), "dc3co-vpb-simulation not enabled\n");
}

static void require_dc_counter(int debugfs_fd, int dc_flag)
{
	char *str;
	char buf[4096];

	igt_debugfs_simple_read(debugfs_fd, "i915_dmc_info",
				buf, sizeof(buf));

	switch (dc_flag) {
	case CHECK_DC3CO:
		igt_skip_on_f(!strstr(buf, "DC3CO count"),
			      "DC3CO counter is not available\n");
		break;
	case CHECK_DC5:
		igt_skip_on_f(!strstr(buf, "DC3 -> DC5 count"),
			      "DC5 counter is not available\n");
		break;
	case CHECK_DC6:
		str = get_dc6_counter(buf);
		igt_skip_on_f(!str, "No DC6 counter available\n");
		break;
	default:
		igt_assert_f(0, "Unknown DC counter %d\n", dc_flag);
	}
}

static void setup_dc3co(data_t *data)
{
	data->op_psr_mode = PSR_MODE_2;
	psr_enable(data->drm_fd, data->debugfs_fd, data->op_psr_mode, NULL);
	igt_require_f(psr_wait_entry(data->debugfs_fd, data->op_psr_mode, NULL),
		      "PSR2 is not enabled\n");
}

static void test_dc3co_vpb_simulation(data_t *data)
{
	require_dc_counter(data->debugfs_fd, CHECK_DC3CO);
	setup_output(data);
	setup_dc3co(data);
	setup_videoplayback(data);
	check_dc3co_with_videoplayback_like_load(data);
	cleanup_dc3co_fbs(data);
}

static void test_dc5_retention_flops(data_t *data, int dc_flag)
{
	uint32_t dc_counter_before_psr;

	require_dc_counter(data->debugfs_fd, dc_flag);
	dc_counter_before_psr = read_dc_counter(data->debugfs_fd, dc_flag);
	set_output_on_pipe_b(data);
	setup_primary(data);
	igt_assert(psr_wait_entry(data->debugfs_fd, data->op_psr_mode, NULL));
	check_dc_counter(data, dc_flag, dc_counter_before_psr);
	cleanup_dc_psr(data);
}

static void test_dc_state_psr(data_t *data, int dc_flag)
{
	uint32_t dc_counter_before_psr;

	require_dc_counter(data->debugfs_fd, dc_flag);
	dc_counter_before_psr = read_dc_counter(data->debugfs_fd, dc_flag);
	setup_output(data);
	setup_primary(data);
	igt_require(!psr_disabled_check(data->debugfs_fd));
	igt_assert(psr_wait_entry(data->debugfs_fd, data->op_psr_mode, NULL));
	check_dc_counter(data, dc_flag, dc_counter_before_psr);
	psr_sink_error_check(data->debugfs_fd, data->op_psr_mode, data->output);
	cleanup_dc_psr(data);
}

static void cleanup_dc_dpms(data_t *data)
{
	/*
	 * if runtime PM is disabled for i915 restore it,
	 * so any other sub-test can use runtime-PM.
	 */
	if (data->runtime_suspend_disabled) {
		igt_restore_runtime_pm();
		igt_setup_runtime_pm(data->drm_fd);
	}
}

static void setup_dc_dpms(data_t *data)
{
	if (IS_BROXTON(data->devid) || IS_GEMINILAKE(data->devid) ||
	    intel_display_ver(data->devid) >= 11) {
		igt_disable_runtime_pm();
		data->runtime_suspend_disabled = true;
	} else {
		data->runtime_suspend_disabled = false;
	}
}

static void dpms_off(data_t *data)
{
	for (int i = 0; i < data->display.n_outputs; i++) {
		kmstest_set_connector_dpms(data->drm_fd,
					   data->display.outputs[i].config.connector,
					   DRM_MODE_DPMS_OFF);
	}

	if (!data->runtime_suspend_disabled)
		igt_assert(igt_wait_for_pm_status
			   (IGT_RUNTIME_PM_STATUS_SUSPENDED));
}

static void dpms_on(data_t *data)
{
	for (int i = 0; i < data->display.n_outputs; i++) {
		kmstest_set_connector_dpms(data->drm_fd,
					   data->display.outputs[i].config.connector,
					   DRM_MODE_DPMS_ON);
	}

	if (!data->runtime_suspend_disabled)
		igt_assert(igt_wait_for_pm_status
			   (IGT_RUNTIME_PM_STATUS_ACTIVE));
}

static void test_dc_state_dpms(data_t *data, int dc_flag)
{
	uint32_t dc_counter;

	require_dc_counter(data->debugfs_fd, dc_flag);
	setup_dc_dpms(data);
	dc_counter = read_dc_counter(data->debugfs_fd, dc_flag);
	dpms_off(data);
	check_dc_counter(data, dc_flag, dc_counter);
	dpms_on(data);
	cleanup_dc_dpms(data);
}

static void test_dc_state_dpms_negative(data_t *data, int dc_flag)
{
	uint32_t dc_counter;

	require_dc_counter(data->debugfs_fd, dc_flag);
	setup_dc_dpms(data);
	dc_counter = read_dc_counter(data->debugfs_fd, dc_flag);
	dpms_on(data);
	check_dc_counter_negative(data, dc_flag, dc_counter);
	cleanup_dc_dpms(data);
}

static bool support_dc6(int debugfs_fd)
{
	char buf[4096];

	igt_debugfs_simple_read(debugfs_fd, "i915_dmc_info",
				buf, sizeof(buf));
	return strstr(buf, "DC5 -> DC6 count");
}

static uint64_t read_runtime_suspended_time(int drm_fd)
{
	struct pci_device *i915;
	uint64_t ret;

	i915 = igt_device_get_pci_device(drm_fd);
	ret = igt_pm_get_runtime_suspended_time(i915);
	igt_assert_lte(0, ret);

	return ret;
}

static bool dc9_wait_entry(data_t *data, int dc_target, int prev_dc, uint64_t prev_rpm, int msecs)
{
	/*
	 * Runtime suspended residency should increment once DC9 is achieved;
	 * this condition is valid for all platforms.
	 * However, resetting of dc5/dc6 counter to check if display engine was in DC9;
	 * this condition at present can be skipped for dg1, dg2 and MTL+ platforms.
	 */
	return igt_wait((read_runtime_suspended_time(data->drm_fd) > prev_rpm) &&
			(!DC9_RESETS_DC_COUNTERS(data->devid) ||
			(read_dc_counter(data->debugfs_fd, dc_target) < prev_dc)), msecs, 1000);
}

static void check_dc9(data_t *data, int dc_target, int prev_dc, int prev_rpm)
{
	igt_assert_f(dc9_wait_entry(data, dc_target, prev_dc, prev_rpm, 3000),
			"DC9 state is not achieved\n%s:\n%s\n", RPM_STATUS,
			data->debugfs_dump = igt_sysfs_get(data->debugfs_fd, RPM_STATUS));
}

static void setup_dc9_dpms(data_t *data, int dc_target)
{
	uint64_t prev_rpm;
	int prev_dc = 0, sysfs_fd;

	igt_require((sysfs_fd = open(KMS_HELPER, O_RDONLY)) >= 0);
	__igt_sysfs_get_boolean(sysfs_fd, "poll", &kms_poll_saved_state);
	__igt_sysfs_set_boolean(sysfs_fd, "poll", KMS_POLL_DISABLE);
	close(sysfs_fd);
	if (DC9_RESETS_DC_COUNTERS(data->devid)) {
		prev_dc = read_dc_counter(data->debugfs_fd, dc_target);
		setup_dc_dpms(data);
		dpms_off(data);
		igt_skip_on_f(!(igt_wait(read_dc_counter(data->debugfs_fd, dc_target) >
				prev_dc, 3000, 100)), "Unable to enters shallow DC states\n");
		prev_dc = read_dc_counter(data->debugfs_fd, dc_target);
		dpms_on(data);
		cleanup_dc_dpms(data);
	}
	prev_rpm = read_runtime_suspended_time(data->drm_fd);
	dpms_off(data);
	check_dc9(data, dc_target, prev_dc, prev_rpm);
	dpms_on(data);
}

static void test_dc9_dpms(data_t *data)
{
	int dc_target;

	require_dc_counter(data->debugfs_fd, CHECK_DC5);
	dc_target = support_dc6(data->debugfs_fd) ? CHECK_DC6 : CHECK_DC5;
	setup_dc9_dpms(data, dc_target);
}

static int has_panels_without_dc_support(igt_display_t *display)
{
	igt_output_t *output;
	int external_panel = 0;

	for_each_connected_output(display, output) {
		drmModeConnectorPtr c = output->config.connector;

		if (c->connector_type != DRM_MODE_CONNECTOR_eDP)
			external_panel++;
	}

	return external_panel;
}

static unsigned int read_pkgc_counter(int debugfs_root_fd)
{
	char buf[4096];
	char *str;
	int len;

	len = igt_sysfs_read(debugfs_root_fd, PACKAGE_CSTATE_PATH, buf, sizeof(buf) - 1);
	igt_skip_on_f(len < 0, "PKGC state file not found\n");
	buf[len] = '\0';
	str = strstr(buf, "Package C10");
	igt_skip_on_f(!str, "PKGC10 is not supported.\n");

	return get_dc_counter(str);
}

static void test_deep_pkgc_state(data_t *data)
{
	unsigned int pre_val = 0, cur_val = 0;
	time_t start = time(NULL);
	time_t duration = (4 * SEC);
	time_t delay;
	enum pipe pipe;
	bool pkgc_flag = false;
	bool flip = true, edp_found = false;

	igt_display_t *display = &data->display;
	igt_plane_t *primary;
	igt_output_t *output = NULL;

	for_each_pipe_with_valid_output(display, pipe, output) {
		if (output->config.connector->connector_type == DRM_MODE_CONNECTOR_eDP) {

			edp_found = true;
			/* Check VRR capabilities before setting up */
			if (igt_output_has_prop(output, IGT_CONNECTOR_VRR_CAPABLE) &&
			    igt_output_get_prop(output, IGT_CONNECTOR_VRR_CAPABLE)) {
				/*
				 * TODO: Add check for vmin = vmax = flipline if VRR enabled
				 * when KMD allows for such capability.
				 */
				igt_pipe_set_prop_value(display, pipe,
							IGT_CRTC_VRR_ENABLED, false);
				igt_assert(igt_display_try_commit_atomic(display,
									 DRM_MODE_ATOMIC_ALLOW_MODESET,
									 NULL) == 0);
			}
		break;
		}
	}

	if (!edp_found) {
		igt_skip("No eDP output found, skipping the test.\n");
		return;
	}

	igt_display_reset(display);

	igt_output_set_pipe(output, pipe);
	for_each_connector_mode(output) {
		data->mode = &output->config.connector->modes[j__];
		delay = (MSEC / (data->mode->vrefresh));
		/*
		 * Should be 5ms vblank time required to program higher
		 * watermark levels
		 */
		if (delay >= (5 * MSEC))
			break;
	}

	data->output = output;
	setup_videoplayback(data);

	primary = igt_output_get_plane_type(data->output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, &data->fb_rgb);
	igt_display_commit(&data->display);
	/* Wait for the vblank to sync the frame time */
	igt_wait_for_vblank_count(data->drm_fd, data->display.pipes[pipe].crtc_offset, 1);
	pre_val = read_pkgc_counter(data->debugfs_root_fd);
	/* Add a half-frame delay to ensure the flip occurs when the frame is active. */
	usleep(delay * 0.5);

	while (time(NULL) - start < duration) {
		flip = !flip;
		igt_plane_set_fb(primary, flip ? &data->fb_rgb : &data->fb_rgr);
		igt_display_commit(&data->display);

		igt_wait((cur_val = read_pkgc_counter(data->debugfs_root_fd)) > pre_val,
						      (delay * 2), (5 * MSEC));
		if (cur_val > pre_val) {
			pkgc_flag = true;
			break;
		}
	}

	cleanup_dc3co_fbs(data);
	igt_assert_f(pkgc_flag, "PKGC10 is not achieved.\n");
}

static void kms_poll_state_restore(int sig)
{
	int sysfs_fd;

	sysfs_fd = open(KMS_HELPER, O_RDONLY);
	if (sysfs_fd >= 0) {
		__igt_sysfs_set_boolean(sysfs_fd, "poll", kms_poll_saved_state);
		close(sysfs_fd);
	}
}

igt_main
{
	data_t data = {};

	igt_fixture {
		data.drm_fd = drm_open_driver_master(DRIVER_INTEL | DRIVER_XE);
		data.debugfs_fd = igt_debugfs_dir(data.drm_fd);
		igt_require(data.debugfs_fd != -1);
		data.debugfs_root_fd = open(igt_debugfs_mount(), O_RDONLY);
		igt_require(data.debugfs_root_fd != -1);
		kmstest_set_vt_graphics_mode();
		data.devid = intel_get_drm_devid(data.drm_fd);
		igt_pm_enable_sata_link_power_management();
		igt_require(igt_setup_runtime_pm(data.drm_fd));
		igt_require(igt_pm_dmc_loaded(data.debugfs_fd));
		igt_display_require(&data.display, data.drm_fd);
		/* Make sure our Kernel supports MSR and the module is loaded */
		igt_require(igt_kmod_load("msr", NULL) == 0);

		data.msr_fd = open("/dev/cpu/0/msr", O_RDONLY);
		igt_assert_f(data.msr_fd >= 0,
			     "Can't open /dev/cpu/0/msr.\n");
		igt_install_exit_handler(kms_poll_state_restore);
	}

	igt_describe("In this test we make sure that system enters DC3CO "
		     "when PSR2 is active and system is in SLEEP state");
	igt_subtest("dc3co-vpb-simulation") {
		igt_require(psr_sink_support(data.drm_fd, data.debugfs_fd,
					     PSR_MODE_2, NULL));
		test_dc3co_vpb_simulation(&data);
	}

	igt_describe("This test validates display engine entry to DC5 state "
		     "while PSR is active");
	igt_subtest("dc5-psr") {
		igt_require(psr_sink_support(data.drm_fd, data.debugfs_fd,
					     PSR_MODE_1, NULL));
		data.op_psr_mode = PSR_MODE_1;
		psr_enable(data.drm_fd, data.debugfs_fd, data.op_psr_mode, NULL);
		test_dc_state_psr(&data, CHECK_DC5);
	}

	igt_describe("This test validates display engine entry to DC6 state "
		     "while PSR is active");
	igt_subtest("dc6-psr") {
		igt_require(psr_sink_support(data.drm_fd, data.debugfs_fd,
					     PSR_MODE_1, NULL));
		data.op_psr_mode = PSR_MODE_1;
		psr_enable(data.drm_fd, data.debugfs_fd, data.op_psr_mode, NULL);
		igt_require_f(igt_pm_pc8_plus_residencies_enabled(data.msr_fd),
			      "PC8+ residencies not supported\n");
		test_dc_state_psr(&data, CHECK_DC6);
	}

	igt_describe("This test validates display engine entry to PKGC10 state "
		     "during extended vblank");
	igt_subtest("deep-pkgc") {
		igt_require_f(igt_pm_pc8_plus_residencies_enabled(data.msr_fd),
			      "PC8+ residencies not supported\n");
		igt_require(intel_display_ver(data.devid) >= 20);
		test_deep_pkgc_state(&data);
	}

	igt_describe("This test validates display engine entry to DC5 state "
		     "while all connectors's DPMS property set to OFF");
	igt_subtest("dc5-dpms") {
		test_dc_state_dpms(&data, CHECK_DC5);
	}

	igt_describe("This test validates display engine entry to DC5 state "
		     "while PSR is active on Pipe B");
	igt_subtest("dc5-retention-flops") {
		igt_require_f(intel_display_ver(data.devid) >= 30,
			      "Test not supported on this platform.\n");
		igt_require(psr_sink_support(data.drm_fd, data.debugfs_fd,
					     PSR_MODE_1, NULL));
		data.op_psr_mode = PSR_MODE_1;
		psr_enable(data.drm_fd, data.debugfs_fd, data.op_psr_mode, NULL);
		igt_require(!psr_disabled_check(data.debugfs_fd));
		test_dc5_retention_flops(&data, CHECK_DC5);
	}

	igt_describe("This test validates negative scenario of DC5 display "
		     "engine entry to DC5 state while all connectors's DPMS "
		     "property set to ON");
	igt_subtest("dc5-dpms-negative") {
		igt_require_f(has_panels_without_dc_support(&data.display),
			      "External panel not detected, skip execution\n");
		test_dc_state_dpms_negative(&data, CHECK_DC5);
	}

	igt_describe("This test validates display engine entry to DC6 state "
		     "while all connectors's DPMS property set to OFF");
	igt_subtest("dc6-dpms") {
		igt_require_f(igt_pm_pc8_plus_residencies_enabled(data.msr_fd),
			      "PC8+ residencies not supported\n");
		test_dc_state_dpms(&data, CHECK_DC6);

	}

	igt_describe("This test validates display engine entry to DC9 state");
	igt_subtest("dc9-dpms") {
		if (!is_intel_dgfx(data.drm_fd))
			igt_require_f(igt_pm_pc8_plus_residencies_enabled(data.msr_fd),
				      "PC8+ residencies not supported\n");
		test_dc9_dpms(&data);
	}

	igt_fixture {
		free(data.debugfs_dump);
		close(data.debugfs_fd);
		close(data.debugfs_root_fd);
		close(data.msr_fd);
		display_fini(&data);
		drm_close_driver(data.drm_fd);
	}

	igt_exit();
}
