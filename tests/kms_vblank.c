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
 */

/**
 * TEST: kms vblank
 * Category: Display
 * Description: Test speed of WaitVblank.
 * Driver requirement: i915, xe
 * Mega feature: General Display Features
 */

#include "igt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <drm.h>

/**
 * SUBTEST: crtc-id
 * Description: Check the vblank and flip events works with given crtc id
 *
 * SUBTEST: invalid
 * Description: Negative test for vblank request
 *
 * SUBTEST: ts-continuation-dpms-rpm
 * Description: Test TS continuty with DPMS & RPM while hanging by introducing
 *              NOHANG flag
 *
 * SUBTEST: ts-continuation-dpms-suspend
 * Description: Test TS continuty with DPMS & Suspend while hanging by introducing
 *              NOHANG flag
 *
 * SUBTEST: ts-continuation-suspend
 * Description: Test TS continuty with Suspend while hanging by introducing NOHANG
 *              flag
 *
 * SUBTEST: ts-continuation-modeset-rpm
 * Description: Test TS continuty during Modeset with Suspend while hanging by
 *              introducing NOHANG flag
 *
 * SUBTEST: accuracy-idle
 * Description: Test Accuracy of vblank events while hanging by introducing NOHANG
 *              flag
 *
 * SUBTEST: %s
 * Description: Test %arg[1] while hanging by introducing NOHANG flag
 *
 * SUBTEST: %s-hang
 * Description: Test %arg[1] with injected hang is working properly
 *
 * arg[1]:
 *
 * @query-idle:              Time taken to Query vblank counters
 * @query-forked:            Time taken to Query vblank counters (multithreaded)
 * @query-busy:              Time taken to Query vblank counters (during V-active)
 * @query-forked-busy:       Time taken to Query vblank counters (during V-active mutithreaded)
 * @wait-idle:               Time taken to wait for vblanks
 * @wait-forked:             Time taken to wait for vblanks (multithreaded)
 * @wait-busy:               Time taken to wait for vblanks (during V-active)
 * @wait-forked-busy:        Time taken to wait for vblanks (during V-active mutithreaded)
 * @ts-continuation-idle:    TS continuty
 * @ts-continuation-modeset: TS continuty during modeset
 */

IGT_TEST_DESCRIPTION("Test speed of WaitVblank.");

typedef struct {
	igt_display_t display;
	struct igt_fb primary_fb;
	igt_output_t *output;
	enum pipe pipe;
	unsigned int flags;
#define IDLE	0x1
#define BUSY	0x2
#define FORKED	0x4
#define NOHANG	0x8
#define MODESET 0x10
#define DPMS	0x20
#define SUSPEND 0x40
#define RPM	0x80
} data_t;

static bool all_pipes;
static enum pipe active_pipes[IGT_MAX_PIPES];
static uint32_t last_pipe;

static double elapsed(const struct timespec *start,
		      const struct timespec *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec)/1000)/loop;
}

static void prepare_crtc(data_t *data, int fd, igt_output_t *output)
{
	drmModeModeInfo *mode;
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	igt_display_reset(display);

	/* select the pipe we want to use */
	igt_output_set_pipe(output, data->pipe);

	/* create and set the primary plane fb */
	mode = igt_output_get_mode(output);
	igt_create_fb(fd, mode->hdisplay, mode->vdisplay,
		      DRM_FORMAT_XRGB8888,
		      DRM_FORMAT_MOD_LINEAR,
		      &data->primary_fb);

	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, &data->primary_fb);

	igt_display_commit(display);

	igt_wait_for_vblank(fd,
			display->pipes[data->pipe].crtc_offset);
}

static void cleanup_crtc(data_t *data, int fd, igt_output_t *output)
{
	igt_output_set_pipe(output, PIPE_NONE);
	igt_display_commit(&data->display);
	igt_remove_fb(fd, &data->primary_fb);
}

static int wait_vblank(int fd, union drm_wait_vblank *vbl)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, vbl))
		err = -errno;

	return err;
}

static void run_test(data_t *data, void (*testfunc)(data_t *, int, int))
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	int fd = display->drm_fd;
	igt_hang_t hang;
	uint64_t ahnd = 0;

	prepare_crtc(data, fd, output);

	if (data->flags & RPM)
		igt_require(igt_setup_runtime_pm(fd));

	if (!(data->flags & NOHANG)) {
		ahnd = is_i915_device(fd) ?
			get_reloc_ahnd(fd, 0) :
			intel_allocator_open(fd, 0, INTEL_ALLOCATOR_RELOC);

		hang = igt_hang_ring_with_ahnd(fd, I915_EXEC_DEFAULT, ahnd);
	}

	if (data->flags & BUSY) {
		union drm_wait_vblank vbl;

		memset(&vbl, 0, sizeof(vbl));
		vbl.request.type =
			DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
		vbl.request.type |= kmstest_get_vbl_flag(data->pipe);
		vbl.request.sequence = 120 + 12;
		igt_assert_eq(wait_vblank(fd, &vbl), 0);
	}

	if (data->flags & FORKED) {
		int nchildren = sysconf(_SC_NPROCESSORS_ONLN);

		igt_debug("Spawning %d threads\n", nchildren);

		igt_fork(child, nchildren)
			testfunc(data, fd, nchildren);
		igt_waitchildren();
	} else
		testfunc(data, fd, 1);

	if (data->flags & BUSY) {
		struct drm_event_vblank buf;
		igt_assert_eq(read(fd, &buf, sizeof(buf)), sizeof(buf));
	}

	igt_assert(poll(&(struct pollfd){fd, POLLIN}, 1, 0) == 0);

	if (!(data->flags & NOHANG))
		igt_post_hang_ring(fd, hang);

	put_ahnd(ahnd);

	/* cleanup what prepare_crtc() has done */
	cleanup_crtc(data, fd, output);
}

static bool
pipe_output_combo_valid(igt_display_t *display,
			enum pipe pipe, igt_output_t *output)
{
	bool ret = true;

	igt_display_reset(display);

	igt_output_set_pipe(output, pipe);
	if (!intel_pipe_output_combo_valid(display))
		ret = false;
	igt_output_set_pipe(output, PIPE_NONE);

	return ret;
}

static void crtc_id_subtest(data_t *data, int fd)
{
	igt_display_t *display = &data->display;
	enum pipe p = data->pipe;
	igt_output_t *output = data->output;
	struct drm_event_vblank buf;
	const uint32_t pipe_id_flag = kmstest_get_vbl_flag(p);
	unsigned crtc_id, expected_crtc_id;
	uint64_t val;
	union drm_wait_vblank vbl;

	crtc_id = display->pipes[p].crtc_id;
	if (drmGetCap(display->drm_fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &val) == 0)
		expected_crtc_id = crtc_id;
	else
		expected_crtc_id = 0;

	prepare_crtc(data, fd, output);

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
	vbl.request.type |= pipe_id_flag;
	vbl.request.sequence = 1;
	igt_assert_eq(wait_vblank(fd, &vbl), 0);

	igt_assert_eq(read(fd, &buf, sizeof(buf)), sizeof(buf));
	igt_assert_eq(buf.crtc_id, expected_crtc_id);

	do_or_die(drmModePageFlip(fd, crtc_id,
				  data->primary_fb.fb_id,
				  DRM_MODE_PAGE_FLIP_EVENT, NULL));

	igt_assert_eq(read(fd, &buf, sizeof(buf)), sizeof(buf));
	igt_assert_eq(buf.crtc_id, expected_crtc_id);

	if (display->is_atomic) {
		igt_plane_t *primary = igt_output_get_plane(output, 0);

		igt_plane_set_fb(primary, &data->primary_fb);
		igt_display_commit_atomic(display, DRM_MODE_PAGE_FLIP_EVENT, NULL);

		igt_assert_eq(read(fd, &buf, sizeof(buf)), sizeof(buf));
		igt_assert_eq(buf.crtc_id, expected_crtc_id);
	}

	cleanup_crtc(data, fd, output);
	return;
}

static void accuracy(data_t *data, int fd, int nchildren)
{
	const uint32_t pipe_id_flag = kmstest_get_vbl_flag(data->pipe);
	union drm_wait_vblank vbl;
	unsigned long target;
	int total = 120 / nchildren;
	int n;

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	vbl.request.type |= pipe_id_flag;
	vbl.request.sequence = 1;
	igt_assert_eq(wait_vblank(fd, &vbl), 0);

	target = vbl.reply.sequence + total;
	for (n = 0; n < total; n++) {
		vbl.request.type = _DRM_VBLANK_RELATIVE;
		vbl.request.type |= pipe_id_flag;
		vbl.request.sequence = 1;
		igt_assert_eq(wait_vblank(fd, &vbl), 0);

		vbl.request.type = DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT;
		vbl.request.type |= pipe_id_flag;
		vbl.request.sequence = target;
		igt_assert_eq(wait_vblank(fd, &vbl), 0);
	}
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	vbl.request.type |= pipe_id_flag;
	vbl.request.sequence = 0;
	igt_assert_eq(wait_vblank(fd, &vbl), 0);
	igt_assert_eq(vbl.reply.sequence, target);

	for (n = 0; n < total; n++) {
		struct drm_event_vblank ev;
		igt_assert_eq(read(fd, &ev, sizeof(ev)), sizeof(ev));
		igt_assert_eq(ev.sequence, target);
	}
}

static void vblank_query(data_t *data, int fd, int nchildren)
{
	const uint32_t pipe_id_flag = kmstest_get_vbl_flag(data->pipe);
	union drm_wait_vblank vbl;
	struct timespec start, end;
	unsigned long sq, count = 0;

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	vbl.request.type |= pipe_id_flag;
	vbl.request.sequence = 0;
	igt_assert_eq(wait_vblank(fd, &vbl), 0);

	sq = vbl.reply.sequence;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		vbl.request.type = _DRM_VBLANK_RELATIVE;
		vbl.request.type |= pipe_id_flag;
		vbl.request.sequence = 0;
		igt_assert_eq(wait_vblank(fd, &vbl), 0);
		count++;
	} while ((vbl.reply.sequence - sq) <= 120);
	clock_gettime(CLOCK_MONOTONIC, &end);

	igt_info("Time to query current counter (%s):		%7.3fµs\n",
		 data->flags & BUSY ? "busy" : "idle", elapsed(&start, &end, count));
}

static void vblank_wait(data_t *data, int fd, int nchildren)
{
	const uint32_t pipe_id_flag = kmstest_get_vbl_flag(data->pipe);
	union drm_wait_vblank vbl;
	struct timespec start, end;
	unsigned long sq, count = 0;

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	vbl.request.type |= pipe_id_flag;
	vbl.request.sequence = 0;
	igt_assert_eq(wait_vblank(fd, &vbl), 0);

	sq = vbl.reply.sequence;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		vbl.request.type = _DRM_VBLANK_RELATIVE;
		vbl.request.type |= pipe_id_flag;
		vbl.request.sequence = 1;
		igt_assert_eq(wait_vblank(fd, &vbl), 0);
		count++;
	} while ((vbl.reply.sequence - sq) <= 120);
	clock_gettime(CLOCK_MONOTONIC, &end);

	igt_info("Time to wait for %ld/%d vblanks (%s):		%7.3fµs\n",
		 count, (int)(vbl.reply.sequence - sq),
		 data->flags & BUSY ? "busy" : "idle",
		 elapsed(&start, &end, count));
}

static int get_vblank(int fd, enum pipe pipe, unsigned flags)
{
	union drm_wait_vblank vbl;

	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = DRM_VBLANK_RELATIVE | kmstest_get_vbl_flag(pipe) | flags;
	do_or_die(igt_ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl));

	return vbl.reply.sequence;
}

#define VBLANK_ERR 5

static void vblank_ts_cont(data_t *data, int fd, int nchildren)
{
	igt_display_t *display = &data->display;
	igt_output_t *output = data->output;
	int seq1, seq2;
	union drm_wait_vblank vbl;
	struct timespec start, end;
	int estimated_vblanks = 0;
	int vrefresh = igt_output_get_mode(output)->vrefresh;
	double time_elapsed;

	seq1 = get_vblank(fd, data->pipe, 0);
	clock_gettime(CLOCK_MONOTONIC, &start);

	if (data->flags & DPMS) {
		igt_output_set_prop_value(output, IGT_CONNECTOR_DPMS, DRM_MODE_DPMS_OFF);
		igt_display_commit(display);
	}

	if (data->flags & MODESET) {
		igt_output_set_pipe(output, PIPE_NONE);
		igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
	}

	if (data->flags & RPM)
		igt_require(igt_wait_for_pm_status(IGT_RUNTIME_PM_STATUS_SUSPENDED));

	if (data->flags & SUSPEND)
		igt_system_suspend_autoresume(SUSPEND_STATE_MEM,
					      SUSPEND_TEST_NONE);

	if (data->flags & (MODESET | DPMS)) {
		/* Attempting to do a vblank while disabled should return -EINVAL */
		memset(&vbl, 0, sizeof(vbl));
		vbl.request.type = _DRM_VBLANK_RELATIVE;
		vbl.request.type |= kmstest_get_vbl_flag(data->pipe);
		igt_assert_eq(wait_vblank(fd, &vbl), -EINVAL);
	}

	if (data->flags & DPMS) {
		igt_output_set_prop_value(output, IGT_CONNECTOR_DPMS, DRM_MODE_DPMS_ON);
		igt_display_commit(display);
	}

	if (data->flags & MODESET) {
		igt_output_set_pipe(output, data->pipe);
		igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
	}

	seq2 = get_vblank(fd, data->pipe, 0);
	clock_gettime(CLOCK_MONOTONIC, &end);

	time_elapsed = igt_time_elapsed(&start, &end);
	estimated_vblanks = (int)(time_elapsed * vrefresh);

	igt_debug("testing ts continuity: Current frame %u, old frame %u\n", seq2, seq1);

	igt_assert_f(seq2 - seq1 >= 0, "elapsed %f(%d vblanks) unexpected vblank seq %u, should be > %u\n", time_elapsed,
			estimated_vblanks, seq2, seq1);
	igt_assert_f(seq2 - seq1 <= estimated_vblanks + VBLANK_ERR, "elapsed %f(%d vblanks) unexpected vblank seq %u, should be <= %u\n", time_elapsed,
			estimated_vblanks, seq2, seq1 + estimated_vblanks);
}

static void run_subtests(data_t *data)
{
	const struct {
		const char *name;
		void (*func)(data_t *, int, int);
		unsigned int valid;
	} funcs[] = {
		/*
		 * GPU reset recovery may disable irqs or reset display, so
		 * accuracy tests will fail in the hang case, disable this test.
		 */
		{ "accuracy", accuracy, IDLE | NOHANG },
		{ "query", vblank_query, IDLE | FORKED | BUSY },
		{ "wait", vblank_wait, IDLE | FORKED | BUSY },
		{ "ts-continuation", vblank_ts_cont, IDLE | SUSPEND | MODESET | DPMS | RPM },
		{ }
	}, *f;

	const struct {
		const char *name;
		unsigned int flags;
	} modes[] = {
		{ "idle", IDLE },
		{ "forked", IDLE | FORKED },
		{ "busy", BUSY },
		{ "forked-busy", BUSY | FORKED },
		{ "dpms-rpm", DPMS | RPM | NOHANG },
		{ "dpms-suspend", DPMS | SUSPEND | NOHANG},
		{ "suspend", SUSPEND | NOHANG },
		{ "modeset", MODESET },
		{ "modeset-rpm", MODESET | RPM | NOHANG},
		{ }
	}, *m;

	for (f = funcs; f->name; f++) {
		for (m = modes; m->name; m++) {
			if (m->flags & ~(f->valid | NOHANG))
				continue;

			igt_describe("Check if test run while hanging by introducing NOHANG flag.");
			igt_subtest_with_dynamic_f("%s-%s", f->name, m->name) {
				for_each_pipe_with_valid_output(&data->display, data->pipe, data->output) {
					if (!pipe_output_combo_valid(&data->display, data->pipe, data->output))
						continue;

					if (!all_pipes && data->pipe != active_pipes[0] &&
					    data->pipe != active_pipes[last_pipe]) {
						igt_info("Skipping pipe %s\n", kmstest_pipe_name(data->pipe));
						continue;
					}

					igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(data->pipe), data->output->name) {
						data->flags = m->flags | NOHANG;
						run_test(data, f->func);
					}
				}
			}

			/* Skip the -hang version if NOHANG flag is set */
			if (f->valid & NOHANG || m->flags & NOHANG)
				continue;

			igt_describe("Check if injected hang is working properly.");
			igt_subtest_with_dynamic_f("%s-%s-hang", f->name, m->name) {
				igt_hang_t hang;

				hang = igt_allow_hang(data->display.drm_fd, 0, 0);
				for_each_pipe_with_valid_output(&data->display, data->pipe, data->output) {
					if (!pipe_output_combo_valid(&data->display, data->pipe, data->output))
						continue;

					if (!all_pipes && data->pipe != active_pipes[0] &&
					    data->pipe != active_pipes[last_pipe]) {
						igt_info("Skipping pipe %s\n", kmstest_pipe_name(data->pipe));
						continue;
					}

					igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(data->pipe), data->output->name) {
						data->flags = m->flags;
						run_test(data, f->func);
					}
				}
				igt_disallow_hang(data->display.drm_fd, hang);
			}
		}
	}
}

static void invalid_subtest(data_t *data, int fd)
{
	union drm_wait_vblank vbl;
	unsigned long valid_flags;
	igt_output_t *output = data->output;

	prepare_crtc(data, fd, output);

	/* First check all is well with a simple query */
	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	igt_assert_eq(wait_vblank(fd, &vbl), 0);

	valid_flags = (_DRM_VBLANK_TYPES_MASK |
		       _DRM_VBLANK_FLAGS_MASK |
		       _DRM_VBLANK_HIGH_CRTC_MASK);

	/* pick some interesting invalid permutations */
	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = _DRM_VBLANK_RELATIVE | ~valid_flags;
	igt_assert_eq(wait_vblank(fd, &vbl), -EINVAL);
	for (int bit = 0; bit < 32; bit++) {
		int err;

		if (valid_flags & (1 << bit))
			continue;

		memset(&vbl, 0, sizeof(vbl));
		vbl.request.type = _DRM_VBLANK_RELATIVE | (1 << bit);
		err = wait_vblank(fd, &vbl);
		igt_assert_f(err == -EINVAL,
			     "vblank wait with invalid request.type bit %d [0x%08x] did not report -EINVAL, got %d\n",
			     bit, 1 << bit, err);
	}

	/* check the maximum pipe, nobody should have that many pipes! */
	memset(&vbl, 0, sizeof(vbl));
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	vbl.request.type |= _DRM_VBLANK_SECONDARY;
	vbl.request.type |= _DRM_VBLANK_FLAGS_MASK;
	igt_assert_eq(wait_vblank(fd, &vbl), -EINVAL);

	cleanup_crtc(data, fd, output);
}

static int opt_handler(int opt, int opt_index, void *data)
{
	switch (opt) {
		case 'e':
			all_pipes = true;
			break;
		default:
			return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

const char *help_str =
	"  -e \tRun on all pipes. (By default subtests will run on two pipes)\n";

igt_main_args("e", NULL, help_str, opt_handler, NULL)
{
	int fd;
	data_t data;

	igt_fixture {
		fd = drm_open_driver_master(DRIVER_ANY);
		kmstest_set_vt_graphics_mode();
		igt_display_require(&data.display, fd);
		igt_display_require_output(&data.display);

		/* Get active pipes. */
		for_each_pipe(&data.display, data.pipe)
			active_pipes[last_pipe++] = data.pipe;
		last_pipe--;
	}

	igt_describe("Negative test for vblank request.");
	igt_subtest_with_dynamic("invalid") {
		for_each_pipe_with_valid_output(&data.display, data.pipe, data.output) {
			if (!pipe_output_combo_valid(&data.display, data.pipe, data.output))
				continue;

			igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(data.pipe), data.output->name)
				invalid_subtest(&data, fd);
			/* one pipe/output combination is enough */
				break;
		}
	}

	igt_describe("Check the vblank and flip events works with given crtc id.");
	igt_subtest_with_dynamic("crtc-id") {
		for_each_pipe_with_valid_output(&data.display, data.pipe, data.output) {
			if (!pipe_output_combo_valid(&data.display, data.pipe, data.output))
				continue;

			if (!all_pipes && data.pipe != active_pipes[0] &&
					  data.pipe != active_pipes[last_pipe]) {
				igt_info("Skipping pipe %s\n", kmstest_pipe_name(data.pipe));
				continue;
			}

			igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(data.pipe), data.output->name)
				crtc_id_subtest(&data, fd);
		}
	}

	run_subtests(&data);

	igt_fixture {
		igt_display_fini(&data.display);
		drm_close_driver(fd);
	}
}
