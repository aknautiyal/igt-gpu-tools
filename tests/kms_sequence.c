/*
 * Copyright © 2015 Intel Corporation
 * Copyright © 2017 Keith Packard
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
 * TEST: kms sequence
 * Category: Display
 * Description: Test CrtcGetSequence and CrtcQueueSequence.
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
#include <time.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <drm.h>

/**
 * SUBTEST: get-%s
 * Description: This is a test of drmCrtcGetSequence with %arg[1]
 *
 * arg[1]:
 *
 * @busy:         busy
 * @forked:       forked bo
 * @forked-busy:  forked bo & busy
 * @idle:         idle
 */

/**
 * SUBTEST: queue-%s
 * Description: This is a test of drmCrtcQueueSequence with %arg[1]
 *
 * arg[1]:
 *
 * @busy:         busy
 * @idle:         idle
 */

IGT_TEST_DESCRIPTION("Test CrtcGetSequence and CrtcQueueSequence.");

typedef struct {
	igt_display_t display;
	struct igt_fb primary_fb;
	igt_output_t *output;
	uint32_t crtc_id;
	enum pipe pipe;
	unsigned int flags;
#define IDLE 1
#define BUSY 2
#define FORKED 4
} data_t;

struct local_drm_event_crtc_sequence {
        struct drm_event        base;
        __u64                   user_data;
        __s64                   time_ns;
        __u64                   sequence;
};


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

	igt_display_reset(&data->display);

	/* select the pipe we want to use */
	igt_output_set_pipe(output, data->pipe);

	/* create and set the primary plane fb */
	mode = igt_output_get_mode(output);
	igt_create_color_fb(fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    DRM_FORMAT_MOD_LINEAR,
			    0.0, 0.0, 0.0,
			    &data->primary_fb);

	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, &data->primary_fb);

	data->crtc_id = primary->pipe->crtc_id;

	igt_display_commit(display);

	igt_wait_for_vblank(fd,
			display->pipes[data->pipe].crtc_offset);
}

static void cleanup_crtc(data_t *data, int fd, igt_output_t *output)
{
	igt_display_t *display = &data->display;
	igt_plane_t *primary;

	igt_remove_fb(fd, &data->primary_fb);

	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);

	igt_output_set_pipe(output, PIPE_NONE);
	igt_display_commit2(display, display->is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);
}

static int crtc_get_sequence(int fd, struct drm_crtc_get_sequence *cgs)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_CRTC_GET_SEQUENCE, cgs))
		err = -errno;

	return err;
}

static int crtc_queue_sequence(int fd, struct drm_crtc_queue_sequence *cqs)
{
	int err;

	err = 0;
	if (igt_ioctl(fd, DRM_IOCTL_CRTC_QUEUE_SEQUENCE, cqs))
		err = -errno;
	return err;
}

static void run_test(data_t *data, int fd, void (*testfunc)(data_t *, int, int))
{
	int nchildren =
		data->flags & FORKED ? sysconf(_SC_NPROCESSORS_ONLN) : 1;
	igt_output_t *output = data->output;

	prepare_crtc(data, fd, output);

	igt_info("Beginning %s on pipe %s, connector %s (%d threads)\n",
		 igt_subtest_name(),
		 kmstest_pipe_name(data->pipe),
		 igt_output_name(output),
		 nchildren);

	if (data->flags & BUSY) {
		struct drm_crtc_queue_sequence cqs;

		memset(&cqs, 0, sizeof(cqs));
		cqs.crtc_id = data->crtc_id;
		cqs.flags = DRM_CRTC_SEQUENCE_RELATIVE;
		cqs.sequence = 120 + 12;
		igt_assert_eq(crtc_queue_sequence(fd, &cqs), 0);
	}

	igt_fork(child, nchildren)
	testfunc(data, fd, nchildren);
	igt_waitchildren();

	if (data->flags & BUSY) {
		struct drm_event_vblank buf;

		igt_assert_eq(read(fd, &buf, sizeof(buf)), sizeof(buf));
	}

	igt_assert(poll(&(struct pollfd){fd, POLLIN}, 1, 0) == 0);

	igt_info("\n%s on pipe %s, connector %s: PASSED\n\n",
		 igt_subtest_name(),
		 kmstest_pipe_name(data->pipe),
		 igt_output_name(output));

	/* cleanup what prepare_crtc() has done */
	cleanup_crtc(data, fd, output);
}

static void sequence_get(data_t *data, int fd, int nchildren)
{
	struct drm_crtc_get_sequence cgs;
	struct timespec start, end;
	unsigned long sq, count = 0;

	memset(&cgs, 0, sizeof(cgs));
	cgs.crtc_id = data->crtc_id;
	igt_assert_eq(crtc_get_sequence(fd, &cgs), 0);

	sq = cgs.sequence;

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		igt_assert_eq(crtc_get_sequence(fd, &cgs), 0);
		count++;
	} while ((cgs.sequence - sq) <= 120);
	clock_gettime(CLOCK_MONOTONIC, &end);

	igt_info("Time to get current counter (%s):		%7.3fµs\n",
		 data->flags & BUSY ? "busy" : "idle", elapsed(&start, &end, count));
}

static void sequence_queue(data_t *data, int fd, int nchildren)
{
	struct drm_crtc_get_sequence cgs_start, cgs_end;
	struct drm_crtc_queue_sequence cqs;
	unsigned long target;
	int total = 120 / nchildren;
	int n;
	double frame_time;

	memset(&cgs_start, 0, sizeof(cgs_start));
	cgs_start.crtc_id = data->crtc_id;
	igt_assert_eq(crtc_get_sequence(fd, &cgs_start), 0);

	target = cgs_start.sequence + total;
	for (n = 0; n < total; n++) {
		memset(&cqs, 0, sizeof(cqs));
		cqs.crtc_id = data->crtc_id;
		cqs.flags = 0;
		cqs.sequence = target;
		igt_assert_eq(crtc_queue_sequence(fd, &cqs), 0);
		igt_assert_eq(cqs.sequence, target);
	}

	for (n = 0; n < total; n++) {
		struct local_drm_event_crtc_sequence ev;
		igt_assert_eq(read(fd, &ev, sizeof(ev)), sizeof(ev));
		igt_assert_eq(ev.sequence, target);
	}
	memset(&cgs_end, 0, sizeof(cgs_end));
	cgs_end.crtc_id = data->crtc_id;
	igt_assert_eq(crtc_get_sequence(fd, &cgs_end), 0);
	igt_assert_eq(cgs_end.sequence, target);

	frame_time = (double) (cgs_end.sequence_ns - cgs_start.sequence_ns) / (1e9 * total);
	igt_info("Time per frame from queue to event (%s):      %7.3fms(%7.3fHz)\n",
		 data->flags & BUSY ? "busy" : "idle",
		 frame_time * 1000.0, 1.0/frame_time);
}

igt_main
{
	int fd;
	igt_output_t *output;
	enum pipe p;
	data_t data;
	const struct {
		const char *name;
		void (*func)(data_t *, int, int);
		unsigned int valid;
	} funcs[] = {
		{ "get", sequence_get, IDLE | FORKED | BUSY },
		{ "queue", sequence_queue, IDLE | BUSY },
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
		{ }
	}, *m;

	igt_fixture {
		fd = drm_open_driver_master(DRIVER_ANY);
		kmstest_set_vt_graphics_mode();
		igt_display_require(&data.display, fd);
		igt_display_require_output(&data.display);
	}

	for (f = funcs; f->name; f++) {
		for (m = modes; m->name; m++) {
			if (m->flags & ~f->valid)
				continue;

			igt_describe("This is a test of drmCrtcGetSequence and "
				     "drmCrtcQueueSequence");
			igt_subtest_with_dynamic_f("%s-%s", f->name, m->name) {
				for_each_pipe_with_valid_output(&data.display, p, output) {
					igt_display_reset(&data.display);

					igt_output_set_pipe(output, p);
					if (!intel_pipe_output_combo_valid(&data.display))
						continue;

					igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(p), igt_output_name(output)) {
						data.pipe = p;
						data.output = output;
						data.flags = m->flags;
						run_test(&data, fd, f->func);
					}
				}
			}
		}
	}

	igt_fixture {
		drm_close_driver(fd);
	}
}
