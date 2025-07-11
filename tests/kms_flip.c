/*
 * Copyright 2012 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * TEST: kms flip
 * Category: Display
 * Description: Tests for validating modeset, dpms and pageflips
 * Driver requirement: i915, xe
 * Mega feature: General Display Features
 */

#include "config.h"

#include "igt.h"
#include "i915/intel_drrs.h"

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#ifdef HAVE_LINUX_KD_H
#include <linux/kd.h>
#elif HAVE_SYS_KD_H
#include <sys/kd.h>
#endif
#include <time.h>
#include <pthread.h>

#include "i915/gem_create.h"
#include "igt_stats.h"
#include "xe/xe_query.h"

/**
 * SUBTEST: %s
 * Description: %arg[1] test to validate pageflips with available fences
 * Driver requirement: i915
 *
 * SUBTEST: 2x-%s
 * Description: %arg[1] test to validate pageflips along with available fences
 *              on a pair of connected displays
 * Driver requirement: i915
 *
 * arg[1]:
 *
 * @flip-vs-fences:                 Basic
 * @flip-vs-fences-interruptible:   Interrupt
 */

/**
 * SUBTEST: dpms-off-%s
 * Description: %arg[1] test to validate pageflips by disabling other connectors usng dpms
 *
 * arg[1]:
 *
 * @confusion:                      Basic
 * @confusion-interruptible:        Interrupt
 */

/**
 * SUBTEST: %s
 * Description: %arg[1] test to validate pageflips with large BO in size
 *
 * arg[1]:
 *
 * @bo-too-big:                     Basic
 * @bo-too-big-interruptible:       Interrupt
 */

/**
 * SUBTEST: %s
 * Description: Basic test to validate %arg[1]
 *
 * SUBTEST: 2x-%s
 * Description: Test to validate %arg[1] on a pair of connected displays
 *
 * arg[1]:
 *
 * @flip-vs-modeset-vs-hang:      pageflip and modeset by hang injection
 * @flip-vs-panning-vs-hang:      pageflip with panning by hang injection
 */

/**
 * SUBTEST: %s
 * Description: Basic test to validate %arg[1]
 *
 * SUBTEST: 2x-%s
 * Description: Test to validate %arg[1] on a pair of connected displays
 *
 * arg[1]:
 *
 * @wf_vblank-ts-check:           wait for the vblank and check timestamps
 * @blocking-wf_vblank:           wait for the vblank synchronous
 * @absolute-wf_vblank:           wait for the absolute vblank
 * @blocking-absolute-wf_vblank:  wait for the absolute vblank synchronous
 * @busy-flip:                    pageflip with busy buffers
 * @plain-flip-ts-check:          pageflip and check timestamps
 * @plain-flip-fb-recreate:       pageflip by recreating the fb
 * @flip-vs-rmfb:                 pageflip by recreating the fb (rmfb)
 * @flip-vs-panning:              pageflip with panning
 * @flip-vs-expired-vblank:       pageflip by checking the vbalnk sequence
 * @flip-vs-absolute-wf_vblank:   pageflip and wait for the absolute vblank
 * @flip-vs-blocking-wf-vblank:   pageflip and wait for the absolute vblank synchronous
 * @nonexisting-fb:               expired framebuffer
 * @modeset-vs-vblank-race:       modeset and check for vblank
 */

/**
 * SUBTEST: %s
 * Description: %arg[1] test to validate pageflips with suspend cycle
 *
 * SUBTEST: 2x-%s
 * Description: %arg[1] test to validate pageflips with suspend cycle on a pair
 *              of connected displays
 *
 * arg[1]:
 *
 * @flip-vs-suspend:                   Basic
 * @flip-vs-suspend-interruptible:     Interrupt
 */

/**
 * SUBTEST: %s
 * Description: Basic test to validate %arg[1]
 *
 * SUBTEST: 2x-%s
 * Description: Basic test to validate %arg[1] on a pair of connected displays
 *
 * SUBTEST: %s-interruptible
 * Description: Basic test to validate %arg[1]
 *
 * SUBTEST: 2x-%s-interruptible
 * Description: Basic test to validate %arg[1] on a pair of connected displays
 *
 * arg[1]:
 *
 * @flip-vs-dpms-off-vs-modeset:               pageflips along with modeset and
 *                                             dpms off.
 * @single-buffer-flip-vs-dpms-off-vs-modeset: pageflip of same buffer along with
 *                                             the modeset and dpms off
 * @dpms-vs-vblank-race:                       vblank along with the dpms & modeset
 * @flip-vs-dpms-on-nop:                       pageflip and issue nop DPMS ON
 */

/**
 * SUBTEST: 2x-flip-vs-dpms
 * Description: Basic test to validate pageflip along with dpms on a pair of
 *              connected displays
 *
 * SUBTEST: 2x-%s
 * Description: Basic test to validate %arg[1] on a pair of connected displays
 *
 * arg[1]:
 *
 * @plain-flip:          pageflip
 * @flip-vs-modeset:     pageflip along with modeset
 * @flip-vs-wf_vblank:   pageflip along with waiting for vblank
 */

/**
 * SUBTEST: %s-interruptible
 * Description: Basic test for validating modeset, dpms and pageflips
 *
 * SUBTEST: 2x-%s-interruptible
 * Description: Test for validating modeset, dpms and pageflips with a pair of
 *              connected displays
 *
 * arg[1]:
 *
 * @wf_vblank-ts-check:           wait for the vblank and check timestamps
 * @absolute-wf_vblank:           wait for the absolute vblank
 * @blocking-absolute-wf_vblank:  wait for the absolute vblank synchronous
 * @plain-flip:                   pageflip
 * @plain-flip-ts-check:          pageflip and check timestamps
 * @plain-flip-fb-recreate:       pageflip by recreating the fb
 * @flip-vs-rmfb:                 pageflip by recreating the fb (rmfb)
 * @flip-vs-panning:              pageflip with panning
 * @flip-vs-expired-vblank:       pageflip by checking the vbalnk sequence
 * @flip-vs-absolute-wf_vblank:   pageflip and wait for the absolute vblank
 * @flip-vs-wf_vblank:            pageflip and wait for vblank
 * @nonexisting-fb:               expired framebuffer
 * @modeset-vs-vblank-race:       modeset and check for vblank
 */

/**
 * SUBTEST: basic-plain-flip
 * Description: Basic test for validating page flip
 *
 * SUBTEST: nonblocking-read
 * Description: Tests that nonblocking reading fails correctly
 *
 * SUBTEST: basic-flip-vs-dpms
 * Description: Basic test to valide pageflip with dpms
 *
 * SUBTEST: basic-flip-vs-%s
 * Description: Basic test to valide pageflip with %arg[1]
 *
 * arg[1]:
 *
 * @modeset:      modeset
 * @wf_vblank:    wait for vblank
 */

#define TEST_DPMS		(1 << 0)
#define TEST_DPMS_ON_NOP	(1 << 1)

#define TEST_PAN		(1 << 3)
#define TEST_MODESET		(1 << 4)
#define TEST_CHECK_TS		(1 << 5)
#define TEST_EBUSY		(1 << 6)
#define TEST_EINVAL		(1 << 7)
#define TEST_FLIP		(1 << 8)
#define TEST_VBLANK		(1 << 9)
#define TEST_VBLANK_BLOCK	(1 << 10)
#define TEST_VBLANK_ABSOLUTE	(1 << 11)
#define TEST_VBLANK_EXPIRED_SEQ	(1 << 12)
#define TEST_FB_RECREATE	(1 << 13)
#define TEST_RMFB		(1 << 14)
#define TEST_HANG		(1 << 15)
#define TEST_NOEVENT		(1 << 16)

#define TEST_SINGLE_BUFFER	(1 << 18)
#define TEST_DPMS_OFF		(1 << 19)
#define TEST_NO_2X_OUTPUT	(1 << 20)
#define TEST_DPMS_OFF_OTHERS	(1 << 21)
#define TEST_ENOENT		(1 << 22)
#define TEST_FENCE_STRESS	(1 << 23)
#define TEST_VBLANK_RACE	(1 << 24)
#define TEST_SUSPEND		(1 << 26)
#define TEST_BO_TOOBIG		(1 << 28)

#define TEST_NO_VBLANK		(1 << 29)
#define TEST_BASIC		(1 << 30)

#define EVENT_FLIP		(1 << 0)
#define EVENT_VBLANK		(1 << 1)

#define RUN_TEST		1
#define RUN_PAIR		2

#ifndef DRM_CAP_TIMESTAMP_MONOTONIC
#define DRM_CAP_TIMESTAMP_MONOTONIC 6
#endif

static bool all_pipes = false;

drmModeRes *resources;
int drm_fd;
static struct buf_ops *bops;
uint32_t devid;
int test_time = 3;
static bool monotonic_timestamp;
static pthread_t vblank_wait_thread;
static int max_dotclock;

static drmModeConnector *last_connector;

uint32_t *fb_ptr;

static igt_display_t display;

struct type_name {
	int type;
	const char *name;
};

struct event_state {
	const char *name;

	/*
	 * Event data for the last event that has already passed our check.
	 * Updated using the below current_* vars in update_state().
	 */
	struct timeval last_ts;			/* kernel reported timestamp */
	struct timeval last_received_ts;	/* the moment we received it */
	unsigned int last_seq;			/* kernel reported seq. num */

	/*
	 * Event data for for the current event that we just received and
	 * going to check for validity. Set in event_handler().
	 */
	struct timeval current_ts;		/* kernel reported timestamp */
	struct timeval current_received_ts;	/* the moment we received it */
	unsigned int current_seq;		/* kernel reported seq. num */

	int count;				/* # of events of this type */
	int err_frames;				/* # of unexpected events */

	/* Step between the current and next 'target' sequence number. */
	int seq_step;
};

static bool should_skip_ts_checks(void) {
	/* Mediatek devices have a HW issue with sending their vblank IRQ at the same time interval
	 * everytime. The drift can be below or above the expected frame time, causing the
	 * timestamp to drift with a relatively larger standard deviation over a large sample.
	 * As it's a known issue, skip any Timestamp or Sequence checks for MTK drivers.
	 */
	return is_mtk_device(drm_fd);
}

static bool vblank_dependence(int flags)
{
	int vblank_flags = TEST_VBLANK | TEST_VBLANK_BLOCK |
			   TEST_VBLANK_ABSOLUTE | TEST_VBLANK_EXPIRED_SEQ |
			   TEST_CHECK_TS | TEST_VBLANK_RACE | TEST_EBUSY;

	if (flags & vblank_flags)
		return true;

	return false;
}

static float timeval_float(const struct timeval *tv)
{
	return tv->tv_sec + tv->tv_usec / 1000000.0f;
}

static void dump_event_state(const struct event_state *es)
{
	igt_debug("name = %s\n"
		  "last_ts = %.06f\n"
		  "last_received_ts = %.06f\n"
		  "last_seq = %u\n"
		  "current_ts = %.06f\n"
		  "current_received_ts = %.06f\n"
		  "current_seq = %u\n"
		  "count = %u\n"
		  "seq_step = %d\n",
		  es->name,
		  timeval_float(&es->last_ts),
		  timeval_float(&es->last_received_ts),
		  es->last_seq,
		  timeval_float(&es->current_ts),
		  timeval_float(&es->current_received_ts),
		  es->current_seq,
		  es->count,
		  es->seq_step);
}

struct test_output {
	int mode_valid;
	drmModeModeInfo kmode[4];
	drmModeEncoder *kencoder[4];
	drmModeConnector *kconnector[4];
	uint32_t _connector[4];
	uint32_t _crtc[4];
	int _pipe[4];
	int count; /* 1:1 mapping between crtc:connector */
	int flags;
	int pipe; /* primary pipe for vblank */
	unsigned int current_fb_id;
	unsigned int fb_width;
	unsigned int fb_height;
	unsigned int fb_ids[3];
	int bpp, depth;
	struct igt_fb fb_info[3];

	struct event_state flip_state;
	struct event_state vblank_state;
	/* Overall step between each round */
	int seq_step;
	unsigned int pending_events;
	int flip_count;

	double vblank_interval;
};


static unsigned long gettime_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void emit_fence_stress(struct test_output *o)
{
	const int num_fences = gem_available_fences(drm_fd);
	struct igt_fb *fb_info = &o->fb_info[o->current_fb_id];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 *exec;
	uint32_t buf[2] = { MI_BATCH_BUFFER_END, 0 };
	struct intel_buf **bo;
	int i;

	igt_require(bops);

	igt_assert(num_fences);
	bo = calloc(num_fences, sizeof(*bo));
	exec = calloc(num_fences+1, sizeof(*exec));
	for (i = 0; i < num_fences - 1; i++) {
		uint32_t tiling = I915_TILING_X;
		bo[i] = intel_buf_create(bops, 1024, 1024, 32, 0, tiling,
					 I915_COMPRESSION_NONE);

		exec[i].handle = bo[i]->handle;
		exec[i].flags = EXEC_OBJECT_NEEDS_FENCE;
	}
	exec[i].handle = fb_info->gem_handle;
	exec[i].flags = EXEC_OBJECT_NEEDS_FENCE;
	exec[++i].handle = gem_create(drm_fd, 4096);
	gem_write(drm_fd, exec[i].handle, 0, buf, sizeof(buf));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = i + 1;
	execbuf.batch_len = sizeof(buf);
	if (HAS_BLT_RING(intel_get_drm_devid(drm_fd)))
		execbuf.flags = I915_EXEC_BLT;

	gem_execbuf(drm_fd, &execbuf);

	gem_close(drm_fd, exec[i].handle);
	for (i = 0; i < num_fences - 1; i++)
		intel_buf_destroy(bo[i]);
	free(bo);
	free(exec);
}

static void dpms_off_other_outputs(struct test_output *o)
{
	int i, n;
	drmModeConnector *connector;
	uint32_t connector_id;

	for (i = 0; i < resources->count_connectors; i++) {
		connector_id = resources->connectors[i];

		for (n = 0; n < o->count; n++) {
			if (connector_id == o->kconnector[n]->connector_id)
				goto next;
		}

		connector = drmModeGetConnectorCurrent(drm_fd, connector_id);

		kmstest_set_connector_dpms(drm_fd, connector,  DRM_MODE_DPMS_ON);
		kmstest_set_connector_dpms(drm_fd, connector,  DRM_MODE_DPMS_OFF);

		drmModeFreeConnector(connector);
next:
		;
	}
}

static void set_dpms(struct test_output *o, int mode)
{
	for (int n = 0; n < o->count; n++)
		kmstest_set_connector_dpms(drm_fd, o->kconnector[n], mode);
}

static void set_flag(unsigned int *v, unsigned int flag)
{
	igt_assert(!(*v & flag));
	*v |= flag;
}

static void clear_flag(unsigned int *v, unsigned int flag)
{
	igt_assert(*v & flag);
	*v &= ~flag;
}

static int do_page_flip(struct test_output *o, uint32_t fb_id, bool event)
{
	int n, ret = 0;

	o->flip_count = 0;

	for (n = 0; ret == 0 && n < o->count; n++)
		ret = drmModePageFlip(drm_fd, o->_crtc[n], fb_id,
				      event ? DRM_MODE_PAGE_FLIP_EVENT : 0,
				      event ? (void *)((unsigned long)o | (n==0)) : NULL);

	if (ret == 0 && event)
		set_flag(&o->pending_events, EVENT_FLIP);

	return ret;
}

struct vblank_reply {
	unsigned int sequence;
	struct timeval ts;
};

static int __wait_for_vblank(unsigned int flags, int crtc_idx,
			      int target_seq, unsigned long ret_data,
			      struct vblank_reply *reply)
{
	drmVBlank wait_vbl;
	int ret;
	uint32_t pipe_id_flag;
	bool event = !(flags & TEST_VBLANK_BLOCK);

	memset(&wait_vbl, 0, sizeof(wait_vbl));
	pipe_id_flag = kmstest_get_vbl_flag(crtc_idx);

	wait_vbl.request.type = pipe_id_flag;
	if (flags & TEST_VBLANK_ABSOLUTE)
		wait_vbl.request.type |= DRM_VBLANK_ABSOLUTE;
	else
		wait_vbl.request.type |= DRM_VBLANK_RELATIVE;
	if (event) {
		wait_vbl.request.type |= DRM_VBLANK_EVENT;
		wait_vbl.request.signal = ret_data;
	}
	wait_vbl.request.sequence = target_seq;

	ret = drmWaitVBlank(drm_fd, &wait_vbl);

	if (ret == 0) {
		reply->ts.tv_sec = wait_vbl.reply.tval_sec;
		reply->ts.tv_usec = wait_vbl.reply.tval_usec;
		reply->sequence = wait_vbl.reply.sequence;
	} else
		ret = -errno;

	return ret;
}

static int do_wait_for_vblank(struct test_output *o, int pipe_id,
			      int target_seq, struct vblank_reply *reply)
{
	int ret;
	unsigned flags = o->flags;

	/* Absolute waits only works once we have a frame counter. */
	if (!(o->vblank_state.count > 0))
		flags &= ~TEST_VBLANK_ABSOLUTE;

	ret = __wait_for_vblank(flags, pipe_id, target_seq, (unsigned long)o,
				reply);
	if (ret == 0 && !(o->flags & TEST_VBLANK_BLOCK))
		set_flag(&o->pending_events, EVENT_VBLANK);

	return ret;
}

static bool
analog_tv_connector(const struct test_output *o)
{
	uint32_t connector_type = o->kconnector[0]->connector_type;

	return connector_type == DRM_MODE_CONNECTOR_TV ||
		connector_type == DRM_MODE_CONNECTOR_9PinDIN ||
		connector_type == DRM_MODE_CONNECTOR_SVIDEO ||
		connector_type == DRM_MODE_CONNECTOR_Composite;
}

static void event_handler(struct event_state *es, unsigned int frame,
			  unsigned int sec, unsigned int usec)
{
	struct timeval now;

	if (monotonic_timestamp) {
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		now.tv_sec = ts.tv_sec;
		now.tv_usec = ts.tv_nsec / 1000;
	} else {
		gettimeofday(&now, NULL);
	}
	es->current_received_ts = now;

	es->current_ts.tv_sec = sec;
	es->current_ts.tv_usec = usec;
	es->current_seq = frame;
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	int primary = (unsigned long)data & 1;
	struct test_output *o = (void *)((unsigned long)data & ~ 1);

	if (++o->flip_count == o->count)
		clear_flag(&o->pending_events, EVENT_FLIP);
	if (primary)
		event_handler(&o->flip_state, frame, sec, usec);
}

static double mode_frame_time(const struct test_output *o)
{
	return 1000.0 * o->kmode[0].htotal * o->kmode[0].vtotal / o->kmode[0].clock;
}

static double actual_frame_time(const struct test_output *o)
{
	return o->vblank_interval;
}

static void *vblank_wait_thread_func(void *data)
{
	struct test_output *o = data;
	struct vblank_reply reply;
	int i;

	for (i = 0; i < 32; i++) {
		unsigned long start = gettime_us();
		__wait_for_vblank(TEST_VBLANK_BLOCK, o->pipe, 20, (unsigned long)o, &reply);
		if (gettime_us() - start > 2 * mode_frame_time(o))
			return (void*)1;
	}

	return 0;
}

static void spawn_vblank_wait_thread(struct test_output *o)
{
	igt_assert(pthread_create(&vblank_wait_thread, NULL,
				  vblank_wait_thread_func, o) == 0);
}

static void join_vblank_wait_thread(void)
{
	igt_assert(pthread_join(vblank_wait_thread, NULL) == 0);
}

static void fixup_premature_vblank_ts(struct test_output *o,
				      struct event_state *es)
{
	/*
	 * In case a power off event preempts the completion of a
	 * wait-for-vblank event the kernel will return a wf-vblank event with
	 * a zeroed-out timestamp. In order that check_state() doesn't
	 * complain replace this ts with a valid ts. As we can't calculate the
	 * exact timestamp, just use the time we received the event.
	 */
	struct timeval tv;

	if (!(o->flags & (TEST_DPMS | TEST_MODESET)))
		return;

	if (o->vblank_state.current_ts.tv_sec != 0 ||
	    o->vblank_state.current_ts.tv_usec != 0)
		return;

	tv.tv_sec = 0;
	tv.tv_usec = 1;
	timersub(&es->current_received_ts, &tv, &es->current_ts);
}

static void vblank_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	struct test_output *o = data;

	clear_flag(&o->pending_events, EVENT_VBLANK);
	event_handler(&o->vblank_state, frame, sec, usec);
	fixup_premature_vblank_ts(o, &o->vblank_state);
}

static bool check_state(const struct test_output *o, struct event_state *es)
{
	struct timeval diff;

	dump_event_state(es);

	timersub(&es->current_ts, &es->current_received_ts, &diff);
	if (!analog_tv_connector(o)) {
		igt_assert_f(diff.tv_sec < 0 || (diff.tv_sec == 0 && diff.tv_usec <= 2000),
			     "%s ts delayed for too long: %.06f\n",
			     es->name, timeval_float(&diff));
	}

	if (es->count == 0)
		return true;

	timersub(&es->current_ts, &es->last_received_ts, &diff);
	igt_assert_f(timercmp(&es->last_received_ts, &es->current_ts, <),
		     "%s ts before the %s was issued!\n"
		     "timerdiff %.06f\n",
		     es->name, es->name, timeval_float(&diff));

	/* check only valid if no modeset happens in between, that increments by
	 * (1 << 23) on each step. This bounding matches the one in
	 * DRM_IOCTL_WAIT_VBLANK. */
	if (!(o->flags & (TEST_DPMS | TEST_MODESET | TEST_NO_VBLANK)) &&
	    es->current_seq - (es->last_seq + o->seq_step) > 1UL << 23) {
		igt_debug("unexpected %s seq %u, should be >= %u\n",
			  es->name, es->current_seq, es->last_seq + o->seq_step);
		es->err_frames++;
		return true;
	}

	if (o->flags & TEST_CHECK_TS) {
		double elapsed, expected;

		timersub(&es->current_ts, &es->last_ts, &diff);
		elapsed = 1e6*diff.tv_sec + diff.tv_usec;
		expected = (es->current_seq - es->last_seq) * actual_frame_time(o);

		igt_debug("%s ts/seq: last %.06f/%u, current %.06f/%u: elapsed=%.1fus expected=%.1fus +- %.1fus, error %.1f%%\n",
			  es->name, timeval_float(&es->last_ts), es->last_seq,
			  timeval_float(&es->current_ts), es->current_seq,
			  elapsed, expected, expected * 0.005,
			  fabs((elapsed - expected) / expected) * 100);

		if (fabs((elapsed - expected) / expected) > 0.005) {
			igt_debug("inconsistent %s ts/seq: last %.06f/%u, current %.06f/%u: elapsed=%.1fus expected=%.1fus\n",
				  es->name, timeval_float(&es->last_ts), es->last_seq,
				  timeval_float(&es->current_ts), es->current_seq,
				  elapsed, expected);
			es->err_frames++;
			return true;
		}

		if (es->current_seq != es->last_seq + o->seq_step) {
			igt_debug("unexpected %s seq %u, expected %u\n",
				  es->name, es->current_seq,
				  es->last_seq + o->seq_step);
			es->err_frames++;
			return true;
		}
	}

	return true;
}

static void check_state_correlation(struct test_output *o,
				    struct event_state *es1,
				    struct event_state *es2)
{
	struct timeval tv_diff;
	double ftime;
	double usec_diff;
	int seq_diff;

	if (es1->count == 0 || es2->count == 0)
		return;

	timersub(&es2->current_ts, &es1->current_ts, &tv_diff);
	usec_diff = tv_diff.tv_sec * USEC_PER_SEC + tv_diff.tv_usec;

	seq_diff = es2->current_seq - es1->current_seq;
	ftime = mode_frame_time(o);
	usec_diff -= seq_diff * ftime;

	igt_assert_f(fabs(usec_diff) / ftime <= 0.005,
		     "timestamp mismatch between %s and %s (diff %.6f sec)\n",
		     es1->name, es2->name, usec_diff / USEC_PER_SEC);
}

static bool check_all_state(struct test_output *o,
			    unsigned int completed_events)
{
	bool flip, vblank;

	flip = completed_events & EVENT_FLIP;
	vblank = completed_events & EVENT_VBLANK;

	if (flip && !check_state(o, &o->flip_state))
		return false;

	if (vblank && !check_state(o, &o->vblank_state))
		return false;

	/* FIXME: Correlation check is broken. */
	if (flip && vblank && 0)
		check_state_correlation(o, &o->flip_state, &o->vblank_state);

	return true;
}

static void recreate_fb(struct test_output *o)
{
	drmModeFBPtr r;
	struct igt_fb *fb_info = &o->fb_info[o->current_fb_id];
	uint32_t new_fb_id;

	/* Call rmfb/getfb/addfb to ensure those don't introduce stalls */
	r = drmModeGetFB(drm_fd, fb_info->fb_id);
	igt_assert(r);

	do_or_die(drmModeAddFB(drm_fd, o->fb_width, o->fb_height, o->depth,
			       o->bpp, fb_info->strides[0],
			       r->handle, &new_fb_id));

	gem_close(drm_fd, r->handle);
	drmFree(r);
	do_or_die(drmModeRmFB(drm_fd, fb_info->fb_id));

	o->fb_ids[o->current_fb_id] = new_fb_id;
	o->fb_info[o->current_fb_id].fb_id = new_fb_id;
}

static igt_hang_t hang_gpu(int fd, uint64_t ahnd)
{
	return igt_hang_ring_with_ahnd(fd, I915_EXEC_DEFAULT, ahnd);
}

static void unhang_gpu(int fd, igt_hang_t hang)
{
	igt_post_hang_ring(fd, hang);
}

static bool is_wedged(int fd)
{
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_THROTTLE, 0) == 0)
		return false;

	return errno == EIO;
}

static int set_mode(struct test_output *o, uint32_t fb, int x, int y)
{
	int n, ret;

	for (n = o->count - 1; n >= 0; n--) {
		uint32_t buffer_id = fb, x_crtc = x, y_crtc = y;
		uint32_t *conn = &o->_connector[n];
		int count = 1;
		drmModeModeInfoPtr mode = &o->kmode[n];

		if (fb == 0) {
			buffer_id = x_crtc = y_crtc = count = 0;
			conn = NULL; mode = NULL;
		}

		ret = drmModeSetCrtc(drm_fd, o->_crtc[n],
				     buffer_id, x_crtc, y_crtc,
				     conn, count, mode);
		if (ret)
			return ret;

		if (is_intel_device(drm_fd))
			intel_drrs_disable(drm_fd, o->pipe);
	}

	return 0;
}

/*
 * Return true if the test steps were run successfully, false in case of a
 * failure that requires rerunning the test steps. On success events will
 * contain the mask of completed events.
 */
static bool run_test_step(struct test_output *o, unsigned int *events)
{
	unsigned int new_fb_id;
	/* for funny reasons page_flip returns -EBUSY on disabled crtcs ... */
	int expected_einval = o->flags & TEST_MODESET ? -EBUSY : -EINVAL;
	unsigned int completed_events = 0;
	bool do_flip;
	bool do_vblank;
	struct vblank_reply vbl_reply;
	unsigned int target_seq;
	igt_hang_t hang;
	uint64_t ahnd = 0;

	target_seq = o->vblank_state.seq_step;
	/* Absolute waits only works once we have a frame counter. */
	if (o->flags & TEST_VBLANK_ABSOLUTE && o->vblank_state.count > 0)
		target_seq += o->vblank_state.last_seq;

	/*
	 * It's possible that we don't have a pending flip here, in case both
	 * wf-vblank and flip were scheduled and the wf-vblank event was
	 * delivered earlier. The same applies to vblank events w.r.t flip.
	 */
	do_flip = (o->flags & TEST_FLIP) && !(o->pending_events & EVENT_FLIP);
	do_vblank = (o->flags & TEST_VBLANK) &&
		    !(o->pending_events & EVENT_VBLANK);

	if (o->flags & TEST_DPMS_OFF_OTHERS)
		dpms_off_other_outputs(o);

	if (!(o->flags & TEST_SINGLE_BUFFER))
		o->current_fb_id = !o->current_fb_id;

	if (o->flags & TEST_FB_RECREATE)
		recreate_fb(o);
	new_fb_id = o->fb_ids[o->current_fb_id];

	if ((o->flags & TEST_VBLANK_EXPIRED_SEQ) &&
	    !(o->pending_events & EVENT_VBLANK) && o->flip_state.count > 0) {
		struct vblank_reply reply;
		unsigned int exp_seq;
		unsigned long start, end;

		exp_seq = o->flip_state.current_seq;
		start = gettime_us();
		do_or_die(__wait_for_vblank(TEST_VBLANK_ABSOLUTE |
					    TEST_VBLANK_BLOCK, o->pipe, exp_seq,
					    0, &reply));
		end = gettime_us();
		igt_debug("Vblank took %luus\n", end - start);
		igt_assert(end - start < 500);
		if (reply.sequence != exp_seq) {
			igt_debug("unexpected vblank seq %u, should be %u\n",
				  reply.sequence, exp_seq);
			return false;
		}
		igt_assert(timercmp(&reply.ts, &o->flip_state.last_ts, ==));
	}

	if (o->flags & TEST_ENOENT) {
		/* hope that fb 0xfffffff0 does not exist */
		igt_assert_eq(do_page_flip(o, 0xfffffff0, false), -ENOENT);
		igt_assert_eq(set_mode(o, 0xfffffff0, 0, 0), -ENOENT);
	}

	if (do_flip && (o->flags & TEST_EINVAL) && o->flip_state.count > 0)
		igt_assert_eq(do_page_flip(o, new_fb_id, false), expected_einval);

	if (do_vblank && (o->flags & TEST_EINVAL) && o->vblank_state.count > 0)
		igt_assert_eq(do_wait_for_vblank(o, o->pipe, target_seq, &vbl_reply), -EINVAL);

	if (o->flags & TEST_VBLANK_RACE) {
		spawn_vblank_wait_thread(o);

		if (o->flags & TEST_MODESET)
			igt_assert_f(set_mode(o, 0 /* no fb */, 0, 0) == 0,
				     "failed to disable output: %s\n",
				     strerror(errno));
	}

	if (o->flags & TEST_DPMS_OFF)
		set_dpms(o, DRM_MODE_DPMS_OFF);

	if (o->flags & TEST_MODESET)
		igt_assert(set_mode(o, o->fb_ids[o->current_fb_id], 0, 0) == 0);

	if (o->flags & (TEST_DPMS | TEST_DPMS_ON_NOP))
		set_dpms(o, DRM_MODE_DPMS_ON);

	if (o->flags & TEST_VBLANK_RACE) {
		struct vblank_reply reply;
		unsigned long start, end;

		/* modeset/DPMS is done, vblank wait should work normally now */
		start = gettime_us();
		igt_assert(__wait_for_vblank(TEST_VBLANK_BLOCK, o->pipe, 2, 0, &reply) == 0);
		end = gettime_us();

		if (!should_skip_ts_checks()) {
			/*
			 * we waited for two vblanks, so verify that
			 * we were blocked for ~1-2 frames. And due
			 * to scheduling latencies we give it an extra
			 * half a frame or so.
			 */
			igt_assert_f(end - start > 0.9 * actual_frame_time(o) &&
							 end - start < 2.6 * actual_frame_time(o),
						 "wait for two vblanks took %lu usec (frame time %f usec)\n",
						 end - start, mode_frame_time(o));
		}
		join_vblank_wait_thread();
	}

	igt_print_activity();

	memset(&hang, 0, sizeof(hang));
	if (do_flip && (o->flags & TEST_HANG)) {
		igt_require_intel(drm_fd);

		ahnd = is_i915_device(drm_fd) ?
			get_reloc_ahnd(drm_fd, 0) :
			intel_allocator_open(drm_fd, 0, INTEL_ALLOCATOR_RELOC);
		hang = hang_gpu(drm_fd, ahnd);
	}

	/* try to make sure we can issue two flips during the same frame */
	if (do_flip && (o->flags & TEST_EBUSY)) {
		struct vblank_reply reply;
		igt_assert(__wait_for_vblank(TEST_VBLANK_BLOCK, o->pipe, 1, 0, &reply) == 0);
	}

	if (do_flip)
		do_or_die(do_page_flip(o, new_fb_id, !(o->flags & TEST_NOEVENT)));

	if (o->flags & TEST_FENCE_STRESS)
		emit_fence_stress(o);

	if (do_vblank) {
		do_or_die(do_wait_for_vblank(o, o->pipe, target_seq,
					     &vbl_reply));
		if (o->flags & TEST_VBLANK_BLOCK) {
			event_handler(&o->vblank_state, vbl_reply.sequence,
				      vbl_reply.ts.tv_sec,
				      vbl_reply.ts.tv_usec);
			completed_events = EVENT_VBLANK;
		}
	}

	if (do_flip && (o->flags & TEST_EBUSY))
		igt_assert_eq(do_page_flip(o, new_fb_id, false), -EBUSY);

	if (do_flip && (o->flags & TEST_RMFB))
		recreate_fb(o);

	/* pan before the flip completes */
	if (o->flags & TEST_PAN) {
		int count = do_flip ?
			o->flip_state.count : o->vblank_state.count;
		int width = o->fb_width - o->kmode[0].hdisplay;
		int x_ofs = count * 10 % (2 * width);
		if (x_ofs >= width)
			x_ofs = 2 * width - x_ofs;

		/* Make sure DSPSURF changes value */
		if (o->flags & TEST_HANG)
			o->current_fb_id = !o->current_fb_id;

		igt_assert_f(set_mode(o, o->fb_ids[o->current_fb_id], x_ofs, 0) == 0,
			     "failed to pan (%dx%d@%dHz)+%d: %s\n",
			     o->kmode[0].hdisplay, o->kmode[0].vdisplay, o->kmode[0].vrefresh,
			     x_ofs, strerror(errno));
	}

	if (o->flags & TEST_DPMS)
		set_dpms(o, DRM_MODE_DPMS_OFF);

	if (o->flags & TEST_MODESET && !(o->flags & TEST_RMFB) && !(o->flags & TEST_VBLANK_RACE))
		igt_assert_f(set_mode(o, 0 /* no fb */, 0, 0) == 0,
			     "failed to disable output: %s\n",
			     strerror(errno));

	if (o->flags & TEST_SUSPEND)
		igt_system_suspend_autoresume(SUSPEND_STATE_MEM,
					      SUSPEND_TEST_NONE);

	if (do_vblank && (o->flags & TEST_EINVAL) && o->vblank_state.count > 0)
		igt_assert(do_wait_for_vblank(o, o->pipe, target_seq, &vbl_reply)
			   == -EINVAL);

	if (do_flip && (o->flags & TEST_EINVAL))
		igt_assert(do_page_flip(o, new_fb_id, false) == expected_einval);

	unhang_gpu(drm_fd, hang);
	put_ahnd(ahnd);

	*events = completed_events;

	return true;
}

static void update_state(struct event_state *es)
{
	es->last_received_ts = es->current_received_ts;
	es->last_ts = es->current_ts;
	es->last_seq = es->current_seq;
	es->count++;
}

static void update_all_state(struct test_output *o,
			     unsigned int completed_events)
{
	if (completed_events & EVENT_FLIP)
		update_state(&o->flip_state);

	if (completed_events & EVENT_VBLANK)
		update_state(&o->vblank_state);
}

static void connector_find_preferred_mode(uint32_t connector_id, int crtc_idx,
					  struct test_output *o)
{
	struct kmstest_connector_config config;

	if (!kmstest_get_connector_config(drm_fd, connector_id, 1 << crtc_idx,
					  &config)) {
		o->mode_valid = 0;
		return;
	}

	o->pipe = config.pipe;
	o->kconnector[0] = config.connector;
	o->kencoder[0] = config.encoder;
	o->_crtc[0] = config.crtc->crtc_id;
	o->_pipe[0] = config.pipe;
	o->kmode[0] = config.default_mode;
	o->mode_valid = 1;

	o->fb_width = o->kmode[0].hdisplay;
	o->fb_height = o->kmode[0].vdisplay;

	drmModeFreeCrtc(config.crtc);
}

static bool mode_compatible(const drmModeModeInfo *a, const drmModeModeInfo *b)
{
	int d_refresh;

	if (a->hdisplay != b->hdisplay)
		return false;

	if (a->vdisplay != b->vdisplay)
		return false;

	d_refresh = a->vrefresh - b->vrefresh;
	if (d_refresh < -1 || d_refresh > 1)
		return false;

	return true;
}

static bool get_compatible_modes(drmModeModeInfo *a, drmModeModeInfo *b,
				 drmModeConnector *c1, drmModeConnector *c2)
{
	int n, m;

	*a = c1->modes[0];
	*b = c2->modes[0];

	if (!mode_compatible(a, b)) {
		for (n = 0; n < c1->count_modes; n++) {
			*a = c1->modes[n];
			for (m = 0; m < c2->count_modes; m++) {
				*b = c2->modes[m];
				if (mode_compatible(a, b))
					return true;
			}
		}

		return false;
	}

	return true;
}

static void connector_find_compatible_mode(int crtc_idx0, int crtc_idx1,
					   struct test_output *o)
{
	struct kmstest_connector_config config[2];
	drmModeModeInfo mode[2];

	if (!kmstest_get_connector_config(drm_fd, o->_connector[0],
					  1 << crtc_idx0, &config[0]))
		return;

	if (!kmstest_get_connector_config(drm_fd, o->_connector[1],
					  1 << crtc_idx1, &config[1])) {
		kmstest_free_connector_config(&config[0]);
		return;
	}

	o->mode_valid = get_compatible_modes(&mode[0], &mode[1],
					     config[0].connector, config[1].connector);

	o->pipe = config[0].pipe;
	o->fb_width = mode[0].hdisplay;
	o->fb_height = mode[0].vdisplay;

	o->kconnector[0] = config[0].connector;
	o->kencoder[0] = config[0].encoder;
	o->_crtc[0] = config[0].crtc->crtc_id;
	o->_pipe[0] = config[0].pipe;
	o->kmode[0] = mode[0];

	o->kconnector[1] = config[1].connector;
	o->kencoder[1] = config[1].encoder;
	o->_crtc[1] = config[1].crtc->crtc_id;
	o->_pipe[1] = config[1].pipe;
	o->kmode[1] = mode[1];

	drmModeFreeCrtc(config[0].crtc);
	drmModeFreeCrtc(config[1].crtc);
}

static void paint_flip_mode(struct igt_fb *fb, bool odd_frame)
{
	cairo_t *cr = igt_get_cairo_ctx(drm_fd, fb);
	int width = fb->width;
	int height = fb->height;

	igt_paint_test_pattern(cr, width, height);

	if (odd_frame)
		cairo_rectangle(cr, width/4, height/2, width/4, height/8);
	else
		cairo_rectangle(cr, width/2, height/2, width/4, height/8);

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);

	igt_put_cairo_ctx(cr);
}

static bool fb_is_bound(struct test_output *o, int fb)
{
	int n;

	for (n = 0; n < o->count; n++) {
		struct drm_mode_crtc mode = {
			.crtc_id = o->_crtc[n]
		};

		if (drmIoctl(drm_fd, DRM_IOCTL_MODE_GETCRTC, &mode))
			return false;

		if (!mode.mode_valid || mode.fb_id != fb)
			return false;
	}

	return true;
}

static bool check_final_state(const struct test_output *o,
			      const struct event_state *es,
			      unsigned int elapsed)
{
	int threshold = 85;
	igt_assert_f(es->count > 0,
		     "no %s event received\n", es->name);

	/* Verify we drop no frames, but only if it's not a TV encoder, since
	 * those use some funny fake timings behind userspace's back. */
	if (o->flags & TEST_CHECK_TS) {
		int count = es->count * o->seq_step;
		int error_count = es->err_frames * o->seq_step;
		int expected = elapsed / actual_frame_time(o);
		float pass_rate = ((float)(count - error_count) / count) * 100;

		if ((1000000.0/actual_frame_time(o)) > 120)
			threshold = 75;

		igt_info("Event %s: expected %d, counted %d, passrate = %.2f%%, encoder type %d\n",
			 es->name, expected, count, pass_rate, o->kencoder[0]->encoder_type);

		/*
		 * TODO: Review the use of the hardcoded threshold (85/75). This value is
		 * currently a placeholder for the acceptable pass rate. In the future,
		 * we should either justify this value or refine the logic to skip
		 * frames near the evasion time.
		 */
		if (pass_rate < threshold) {
			igt_debug("dropped frames, expected %d, counted %d, passrate = %.2f%%, encoder type %d\n",
				  expected, count, pass_rate, o->kencoder[0]->encoder_type);
			return false;
		}
	}

	return true;
}

/*
 * Wait until at least one pending event completes. Return mask of completed
 * events.
 */
static unsigned int wait_for_events(struct test_output *o)
{
	drmEventContext evctx;
	struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
	fd_set fds;
	unsigned int event_mask;
	int ret;

	event_mask = o->pending_events;
	igt_assert(event_mask);

	memset(&evctx, 0, sizeof evctx);
	evctx.version = 2;
	evctx.vblank_handler = vblank_handler;
	evctx.page_flip_handler = page_flip_handler;

	FD_ZERO(&fds);
	FD_SET(drm_fd, &fds);
	do {
		do {
			ret = select(drm_fd + 1, &fds, NULL, NULL, &timeout);
		} while (ret < 0 && errno == EINTR);

		igt_assert_f(ret >= 0,
			     "select error (errno %i)\n", errno);
		igt_assert_f(ret > 0,
			     "select timed out or error (ret %d)\n", ret);
		igt_assert_f(!FD_ISSET(0, &fds),
			     "no fds active, breaking\n");

		do_or_die(drmHandleEvent(drm_fd, &evctx));
	} while (o->pending_events);

	event_mask ^= o->pending_events;
	igt_assert(event_mask);

	return event_mask;
}

/* Returned the elapsed time in us */
static bool event_loop(struct test_output *o, unsigned duration_ms,
		       unsigned *elapsed)
{
	unsigned long start, end;
	int count = 0;

	start = gettime_us();

	while (1) {
		unsigned int completed_events;

		if (!run_test_step(o, &completed_events))
			return false;

		if (o->pending_events)
			completed_events |= wait_for_events(o);

		if (!check_all_state(o, completed_events))
			return false;

		update_all_state(o, completed_events);

		if (count && (gettime_us() - start) / 1000 >= duration_ms)
			break;

		count++;
	}

	end = gettime_us();

	/* Flush any remaining events */
	if (o->pending_events)
		wait_for_events(o);

	*elapsed = end - start;

	return true;
}

static void free_test_output(struct test_output *o)
{
	int i;

	for (i = 0; i < o->count; i++) {
		drmModeFreeEncoder(o->kencoder[i]);
		drmModeFreeConnector(o->kconnector[i]);
	}
}

static bool calibrate_ts(struct test_output *o, int crtc_idx)
{
#define CALIBRATE_TS_STEPS 16
	drmVBlank wait;
	igt_stats_t stats;
	uint32_t last_seq;
	uint64_t last_timestamp;
	double expected;
	double mean;
	double stddev;
	bool failed = false;
	int n;

	memset(&wait, 0, sizeof(wait));
	wait.request.type = kmstest_get_vbl_flag(crtc_idx);
	wait.request.type |= DRM_VBLANK_RELATIVE | DRM_VBLANK_NEXTONMISS;
	do_or_die(drmWaitVBlank(drm_fd, &wait));

	last_seq = wait.reply.sequence;
	last_timestamp = wait.reply.tval_sec;
	last_timestamp *= 1000000;
	last_timestamp += wait.reply.tval_usec;

	memset(&wait, 0, sizeof(wait));
	wait.request.type = kmstest_get_vbl_flag(crtc_idx);
	wait.request.type |= DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT;
	wait.request.sequence = last_seq;
	for (n = 0; n < CALIBRATE_TS_STEPS; n++) {
		drmVBlank check = {};

		++wait.request.sequence;
		do_or_die(drmWaitVBlank(drm_fd, &wait));

		/* Double check that haven't already missed the vblank */
		check.request.type = kmstest_get_vbl_flag(crtc_idx);
		check.request.type |= DRM_VBLANK_RELATIVE;
		do_or_die(drmWaitVBlank(drm_fd, &check));

		igt_assert(!igt_vblank_after(check.reply.sequence, wait.request.sequence));
	}

	igt_stats_init_with_size(&stats, CALIBRATE_TS_STEPS);
	for (n = 0; n < CALIBRATE_TS_STEPS; n++) {
		struct drm_event_vblank ev;
		uint64_t now;
		int poll_ret;

		while (1) {
			/*
			 * In case of the interruptible tests, this poll may
			 * be interrupted with -EINTR, handle this by restarting
			 * until we poll timeout or success.
			 */
			poll_ret = poll(&(struct pollfd){drm_fd, POLLIN}, 1, -1);

			if (poll_ret == 1)
				break;

			igt_assert_neq(poll_ret, 0);
			igt_assert_eq(errno, EINTR);
		}
		igt_assert(read(drm_fd, &ev, sizeof(ev)) == sizeof(ev));

		if (failed)
			continue;

		if (ev.sequence != last_seq + 1) {
			igt_debug("Unexpected frame sequence %d vs. expected %d\n",
				  ev.sequence, last_seq + 1);
			failed = true;

			/* Continue to flush all the events queued up */
			continue;
		}

		now = ev.tv_sec;
		now *= 1000000;
		now += ev.tv_usec;

		igt_stats_push(&stats, now - last_timestamp);

		last_timestamp = now;
		last_seq = ev.sequence;
	}

	if (failed)
		return false;

	expected = mode_frame_time(o);

	mean = igt_stats_get_mean(&stats);
	stddev = igt_stats_get_std_deviation(&stats);

	igt_info("Expected frametime: %.0fus; measured %.1fus +- %.3fus accuracy %.2f%%\n",
		 expected, mean, stddev, 100 * 3 * stddev / mean);
	if (!should_skip_ts_checks())
		/* 99.7% samples within 0.5% of the mean */
		igt_assert(3 * stddev / mean < 0.005);

	/* 84% samples within 0.5% of the expected value.
	 * See comments in check_timings() in kms_setmode.c
	 */
	if (fabs(mean - expected) > 2*stddev) {
		igt_info("vblank interval differs from modeline! expected %.1fus, measured %1.fus +- %.3fus, difference %.1fus (%.1f sigma)\n",
			 expected, mean, stddev,
			 fabs(mean - expected), fabs(mean - expected) / stddev);
	}

	o->vblank_interval = mean;

	return true;
}

/*
 * Some monitors with odd behavior signal a bad link after waking from a power
 * saving state and the subsequent (successful) modeset. This will result in a
 * link-retraining (DP) or async modeset (HDMI), which in turn makes the test
 * miss vblank/flip events and fail.  Work around this by retrying the test
 * once in case of such a link reset event, which the driver signals with a
 * hotplug event.
 */
static bool needs_retry_after_link_reset(struct udev_monitor *mon)
{
	bool hotplug_detected;

	igt_suspend_signal_helper();
	hotplug_detected = igt_hotplug_detected(mon, 3);
	igt_resume_signal_helper();

	if (hotplug_detected)
		igt_debug("Retrying after a hotplug event\n");

	return hotplug_detected;
}

static void discard_any_stale_events(void) {
	fd_set fds;
	int ret;
	struct timeval timeout = { .tv_sec = 0, .tv_usec = 20000 };
	FD_ZERO(&fds);
	FD_SET(drm_fd, &fds);
	ret = select(drm_fd + 1, &fds, NULL, NULL, &timeout);

	if (ret > 0) {
		drmEventContext evctx;
		memset(&evctx, 0, sizeof evctx);
		evctx.version = 2;
		igt_info("Stale Event found - Discarding now\n");
		drmHandleEvent(drm_fd, &evctx);
	}
	else {
		igt_debug("No stale events found\n");
	}
}

static void get_suitable_modes(struct test_output *o)
{
	drmModeModeInfo mode[2];
	int i;

	for (i = 0; i < RUN_PAIR; i++)
		igt_sort_connector_modes(o->kconnector[i],
					 sort_drm_modes_by_clk_asc);

	o->mode_valid = get_compatible_modes(&mode[0], &mode[1],
					     o->kconnector[0], o->kconnector[1]);

	o->fb_width = mode[0].hdisplay;
	o->fb_height = mode[0].vdisplay;
	o->kmode[0] = mode[0];
	o->kmode[1] = mode[1];
}

static void __run_test_on_crtc_set(struct test_output *o, int *crtc_idxs,
				   int crtc_count, int duration_ms)
{
	struct udev_monitor *mon = igt_watch_uevents();
	unsigned bo_size = 0;
	bool vblank = true;
	bool retried = false, restart = false;
	bool state_ok;
	unsigned elapsed;
	uint64_t modifier;
	int i, ret;

restart:
	last_connector = o->kconnector[0];

	if (o->flags & TEST_PAN)
		o->fb_width *= 2;

	if (igt_display_has_format_mod(&display, DRM_FORMAT_XRGB8888,
				       I915_FORMAT_MOD_4_TILED))
		modifier = I915_FORMAT_MOD_4_TILED;
	else if (igt_display_has_format_mod(&display, DRM_FORMAT_XRGB8888,
					    I915_FORMAT_MOD_X_TILED))
		modifier = I915_FORMAT_MOD_X_TILED;
	else
		modifier = DRM_FORMAT_MOD_LINEAR;

	if (o->flags & TEST_FENCE_STRESS)
		modifier = I915_FORMAT_MOD_X_TILED;

	/* 256 MB is usually the maximum mappable aperture,
	 * (make it 4x times that to ensure failure) */
	if (o->flags & TEST_BO_TOOBIG) {
		bo_size = 4*gem_mappable_aperture_size(drm_fd);

		if (is_i915_device(drm_fd))
			igt_require(bo_size < gem_global_aperture_size(drm_fd));
		else
			igt_require(bo_size < (1ULL << xe_va_bits(drm_fd)));
	}

	o->fb_ids[0] = igt_create_fb(drm_fd, o->fb_width, o->fb_height,
					 igt_bpp_depth_to_drm_format(o->bpp, o->depth),
					 modifier, &o->fb_info[0]);
	o->fb_ids[1] = igt_create_fb_with_bo_size(drm_fd, o->fb_width, o->fb_height,
						  igt_bpp_depth_to_drm_format(o->bpp, o->depth),
						  modifier, IGT_COLOR_YCBCR_BT709,
						  IGT_COLOR_YCBCR_LIMITED_RANGE,
						  &o->fb_info[1], bo_size, 0);

	igt_assert(o->fb_ids[0]);
	igt_assert(o->fb_ids[1]);

	paint_flip_mode(&o->fb_info[0], false);
	if (!(o->flags & TEST_BO_TOOBIG))
		paint_flip_mode(&o->fb_info[1], true);
	if (o->fb_ids[2])
		paint_flip_mode(&o->fb_info[2], true);

	for (i = 0; i < o->count; i++)
		kmstest_dump_mode(&o->kmode[i]);

retry:
	/* Discard any pending event that hasn't been consumed from a previous retry or subtest. */
	discard_any_stale_events();

	memset(&o->vblank_state, 0, sizeof(o->vblank_state));
	memset(&o->flip_state, 0, sizeof(o->flip_state));
	o->flip_state.name = "flip";
	o->vblank_state.name = "vblank";

	kmstest_unset_all_crtcs(drm_fd, resources);

	igt_flush_uevents(mon);

	ret = set_mode(o, o->fb_ids[0], 0, 0);

	/* In case of DP-MST find suitable mode(s) to fit into the link BW. */
	if (ret < 0 && errno == ENOSPC &&
	    crtc_count == RUN_PAIR) {

		if (restart) {
			igt_info("No suitable modes found to fit into the link BW.\n");
			goto out;
		}

		get_suitable_modes(o);

		if (o->mode_valid) {
			igt_remove_fb(drm_fd, &o->fb_info[2]);
			igt_remove_fb(drm_fd, &o->fb_info[1]);
			igt_remove_fb(drm_fd, &o->fb_info[0]);

			restart = true;
			goto restart;
		}

		goto out;
	}

	igt_assert(!ret);
	igt_assert(fb_is_bound(o, o->fb_ids[0]));

	vblank = kms_has_vblank(drm_fd);
	if (!vblank) {
		if (vblank_dependence(o->flags))
			igt_require_f(vblank, "There is no VBlank\n");
		else
			o->flags |= TEST_NO_VBLANK;
	}

	/* quiescent the hw a bit so ensure we don't miss a single frame */
	if (o->flags & TEST_CHECK_TS && !calibrate_ts(o, crtc_idxs[0])) {
		igt_assert(!retried);

		/*
		 * FIXME: Retried logic is currently breaking due to an HPD
		 * (Hot Plug Detect) issue. Temporarily removing this from
		 * the assertion. This needs to be debugged separately.
		 * Revert this patch once the HPD issue is resolved.
		 */
		if (!needs_retry_after_link_reset(mon))
			igt_debug("Retrying without a hotplug event\n");

		retried = true;

		goto retry;
	}

	if (o->flags & TEST_BO_TOOBIG) {
		int err = do_page_flip(o, o->fb_ids[1], true);
		igt_assert(err == 0 || err == -E2BIG);
		if (err)
			goto out;
	} else {
		igt_assert_eq(do_page_flip(o, o->fb_ids[1], true), 0);
	}
	wait_for_events(o);

	o->current_fb_id = 1;

	if (o->flags & TEST_FLIP)
		o->flip_state.seq_step = 1;
	else
		o->flip_state.seq_step = 0;
	if (o->flags & TEST_VBLANK)
		o->vblank_state.seq_step = 10;
	else
		o->vblank_state.seq_step = 0;

	/* We run the vblank and flip actions in parallel by default. */
	o->seq_step = max(o->vblank_state.seq_step, o->flip_state.seq_step);

	state_ok = event_loop(o, duration_ms, &elapsed);

	if (o->flags & TEST_FLIP && !(o->flags & TEST_NOEVENT))
		state_ok &= check_final_state(o, &o->flip_state, elapsed);
	if (o->flags & TEST_VBLANK)
		state_ok &= check_final_state(o, &o->vblank_state, elapsed);

	if (!state_ok) {
		igt_assert(!retried);

		/*
		 * FIXME: Retried logic is currently breaking due to an HPD
		 * (Hot Plug Detect) issue. Temporarily removing this from
		 * the assertion. This needs to be debugged separately.
		 * Revert this patch once the HPD issue is resolved.
		 */
		if (!needs_retry_after_link_reset(mon))
			igt_debug("Retrying without a hotplug event\n");

		retried = true;

		goto retry;
	}

out:
	igt_remove_fb(drm_fd, &o->fb_info[2]);
	igt_remove_fb(drm_fd, &o->fb_info[1]);
	igt_remove_fb(drm_fd, &o->fb_info[0]);

	last_connector = NULL;

	free_test_output(o);

	igt_cleanup_uevents(mon);
}

static void run_test_on_crtc_set(struct test_output *o, int *crtc_idxs,
				 int crtc_count, int total_crtcs,
				 int duration_ms)
{
	char test_name[128];
	int i;

	switch (crtc_count) {
	case RUN_TEST:
		connector_find_preferred_mode(o->_connector[0], crtc_idxs[0], o);
		if (!o->mode_valid)
			return;
		snprintf(test_name, sizeof(test_name),
			 "%s-%s%d",
			 kmstest_pipe_name(o->_pipe[0]),
			 kmstest_connector_type_str(o->kconnector[0]->connector_type),
			 o->kconnector[0]->connector_type_id);
		break;
	case RUN_PAIR:
		connector_find_compatible_mode(crtc_idxs[0], crtc_idxs[1], o);
		if (!o->mode_valid)
			return;
		snprintf(test_name, sizeof(test_name),
			 "%s%s-%s%d-%s%d",
			 kmstest_pipe_name(o->_pipe[0]),
			 kmstest_pipe_name(o->_pipe[1]),
			 kmstest_connector_type_str(o->kconnector[0]->connector_type),
			 o->kconnector[0]->connector_type_id,
			 kmstest_connector_type_str(o->kconnector[1]->connector_type),
			 o->kconnector[1]->connector_type_id);
		break;
	default:
		igt_assert(0);
	}

	igt_assert_eq(o->count, crtc_count);

	/*
	 * Handle BW limitations on intel hardware:
	 *
	 * if force joiner (or) mode resolution > 5K (or) mode clock > max_dotclock, then ignore
	 *  - last crtc in single/multi-connector config
	 *  - consecutive crtcs in multi-connector config
	 *
	 * in multi-connector config ignore if
	 *  - previous crtc (force joiner or mode resolution > 5K or mode clock > max_dotclock) and
	 *  - current & previous crtcs are consecutive
	 */
	if (!is_intel_device(drm_fd))
		goto test;

	for (i = 0; i < crtc_count; i++) {
		char conn_name[24], prev_conn_name[24];

		snprintf(conn_name, sizeof(conn_name),
			 "%s-%d",
			 kmstest_connector_type_str(o->kconnector[i]->connector_type),
			 o->kconnector[i]->connector_type_id);

		if (i > 0)
			snprintf(prev_conn_name, sizeof(prev_conn_name),
				 "%s-%d",
				 kmstest_connector_type_str(o->kconnector[i - 1]->connector_type),
				 o->kconnector[i - 1]->connector_type_id);

		if (((igt_check_force_joiner_status(drm_fd, conn_name) ||
		      igt_bigjoiner_possible(drm_fd, &o->kmode[i], max_dotclock)) &&
		     ((crtc_idxs[i] >= (total_crtcs - 1)) ||
		      ((i < (crtc_count - 1)) && (abs(crtc_idxs[i + 1] - crtc_idxs[i]) <= 1)))) ||
		    ((i > 0) && (igt_check_force_joiner_status(drm_fd, prev_conn_name) ||
				 igt_bigjoiner_possible(drm_fd, &o->kmode[i - 1], max_dotclock)) &&
		     (abs(crtc_idxs[i] - crtc_idxs[i - 1]) <= 1))) {

			igt_debug("Combo: %s is not possible with selected mode(s).\n", test_name);
			return;
		}
	}

test:
	igt_dynamic_f("%s", test_name)
		__run_test_on_crtc_set(o, crtc_idxs, crtc_count, duration_ms);
}

static void run_test(int duration, int flags)
{
	struct test_output o;
	int i, n, modes = 0;

	/* No tiling support in XE. */
	if (is_xe_device(drm_fd) && flags & TEST_FENCE_STRESS)
		return;

	if (flags & TEST_BO_TOOBIG && !is_intel_device(drm_fd))
		return;

	if ((flags & TEST_HANG) == 0 && is_i915_device(drm_fd))
		igt_require(!is_wedged(drm_fd));

	igt_require(!(flags & TEST_FENCE_STRESS) ||
		    (is_i915_device(drm_fd) && gem_available_fences(drm_fd)));

	resources = drmModeGetResources(drm_fd);
	igt_require(resources);

	/* Count output configurations to scale test runtime. */
	for (i = 0; i < resources->count_connectors; i++) {
		for (n = 0; n < resources->count_crtcs; n++) {
			/* Limit the execution to 2 CRTCs (first & last) for hang tests */
			if ((flags & TEST_HANG) && !all_pipes &&
			    n != 0 && n != (resources->count_crtcs - 1))
				continue;

			memset(&o, 0, sizeof(o));
			o.count = 1;
			o._connector[0] = resources->connectors[i];
			o.flags = flags;
			o.bpp = 32;
			o.depth = 24;

			connector_find_preferred_mode(o._connector[0], n, &o);
			if (o.mode_valid)
				modes++;

			free_test_output(&o);
		}
	}

	igt_require(modes);

	if (duration) {
		duration = duration * 1000 / modes;
		duration = max(500, duration);
	}

	/* Find any connected displays */
	for (i = 0; i < resources->count_connectors; i++) {
		for (n = 0; n < resources->count_crtcs; n++) {
			int crtc_idx;

			/* Limit the execution to 2 CRTCs (first & last) for hang tests */
			if ((flags & TEST_HANG) && !all_pipes &&
			    n != 0 && n != (resources->count_crtcs - 1))
				continue;

			memset(&o, 0, sizeof(o));
			o.count = 1;
			o._connector[0] = resources->connectors[i];
			o.flags = flags;
			o.bpp = 32;
			o.depth = 24;

			crtc_idx = n;
			run_test_on_crtc_set(&o, &crtc_idx, RUN_TEST,
					     resources->count_crtcs, duration);
		}
	}

	drmModeFreeResources(resources);
}

static void run_pair(int duration, int flags)
{
	struct test_output o;
	int i, j, m, n, modes = 0;

	/* No tiling support in XE. */
	if (is_xe_device(drm_fd) && flags & TEST_FENCE_STRESS)
		return;

	if (flags & TEST_BO_TOOBIG && !is_intel_device(drm_fd))
		return;

	if ((flags & TEST_HANG) == 0 && is_i915_device(drm_fd))
		igt_require(!is_wedged(drm_fd));

	igt_require(!(flags & TEST_FENCE_STRESS) ||
		    (is_i915_device(drm_fd) && gem_available_fences(drm_fd)));

	resources = drmModeGetResources(drm_fd);
	igt_require(resources);

	/* Find a pair of connected displays */
	for (i = 0; i < resources->count_connectors; i++) {
		for (n = 0; n < resources->count_crtcs; n++) {
			for (j = i + 1; j < resources->count_connectors; j++) {
				for (m = n + 1; m < resources->count_crtcs; m++) {
					memset(&o, 0, sizeof(o));
					o.count = 2;
					o._connector[0] = resources->connectors[i];
					o._connector[1] = resources->connectors[j];
					o.flags = flags;
					o.bpp = 32;
					o.depth = 24;

					connector_find_compatible_mode(n, m, &o);
					if (o.mode_valid)
						modes++;

					free_test_output(&o);
				}
			}
		}
	}

	/* If we have fewer than 2 connected outputs then we won't have any
	 * configuration at all. So skip in that case. */
	igt_require_f(modes, "At least two displays with same modes are required\n");

	if (duration) {
		duration = duration * 1000 / modes;
		duration = max(duration, 500);
	}

	/* Find a pair of connected displays */
	for (i = 0; i < resources->count_connectors; i++) {
		for (n = 0; n < resources->count_crtcs; n++) {
			for (j = i + 1; j < resources->count_connectors; j++) {
				for (m = n + 1; m < resources->count_crtcs; m++) {
					int crtc_idxs[2];

					memset(&o, 0, sizeof(o));
					o.count = 2;
					o._connector[0] = resources->connectors[i];
					o._connector[1] = resources->connectors[j];
					o.flags = flags;
					o.bpp = 32;
					o.depth = 24;

					crtc_idxs[0] = n;
					crtc_idxs[1] = m;

					/* Limit the execution to 2 CRTCs (first & last) for hang tests */
					if ((flags & TEST_HANG) && !all_pipes &&
					    ((n != 0 && n != resources->count_crtcs) ||
					    m != resources->count_crtcs - 1))
						continue;

					run_test_on_crtc_set(&o, crtc_idxs,
							     RUN_PAIR,
							     resources->count_crtcs,
							     duration);
				}
			}
		}
	}

	drmModeFreeResources(resources);
}

static void get_timestamp_format(void)
{
	uint64_t cap_mono;
	int ret;

	ret = drmGetCap(drm_fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap_mono);
	igt_assert(ret == 0 || errno == EINVAL);
	monotonic_timestamp = ret == 0 && cap_mono == 1;
	igt_info("Using %s timestamps\n",
		 monotonic_timestamp ? "monotonic" : "real");
}

static void kms_flip_exit_handler(int sig)
{
	if (last_connector)
		kmstest_set_connector_dpms(drm_fd, last_connector, DRM_MODE_DPMS_ON);
}

static void test_nonblocking_read(int in)
{
	char buffer[1024];
	int fd = dup(in);
	int ret;

	ret = -1;
	if (fd != -1)
		ret = fcntl(fd, F_GETFL);
	if (ret != -1) {
		ret |= O_NONBLOCK;
		ret = fcntl(fd, F_SETFL, ret);
	}
	igt_require(ret != -1);

	igt_set_timeout(5, "Nonblocking DRM fd reading");
	ret = read(fd, buffer, sizeof(buffer));
	igt_reset_timeout();

	igt_assert_eq(ret, -1);
	igt_assert_eq(errno, EAGAIN);

	close(fd);
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
	struct {
		int duration;
		int flags;
		const char *name;
	} tests[] = {
		{ 30, TEST_VBLANK | TEST_CHECK_TS, "wf_vblank-ts-check" },
		{ 30, TEST_VBLANK | TEST_VBLANK_BLOCK | TEST_CHECK_TS,
					"blocking-wf_vblank" },
		{ 30,  TEST_VBLANK | TEST_VBLANK_ABSOLUTE,
					"absolute-wf_vblank" },
		{ 30,  TEST_VBLANK | TEST_VBLANK_BLOCK | TEST_VBLANK_ABSOLUTE,
					"blocking-absolute-wf_vblank" },
		{ 2, TEST_FLIP | TEST_BASIC, "plain-flip" },
		{ 1, TEST_FLIP | TEST_EBUSY, "busy-flip" },
		{ 30, TEST_FLIP | TEST_FENCE_STRESS , "flip-vs-fences" },
		{ 30, TEST_FLIP | TEST_CHECK_TS, "plain-flip-ts-check" },
		{ 30, TEST_FLIP | TEST_CHECK_TS | TEST_FB_RECREATE,
			"plain-flip-fb-recreate" },
		{ 30, TEST_FLIP | TEST_RMFB | TEST_MODESET , "flip-vs-rmfb" },
		{ 2, TEST_FLIP | TEST_DPMS | TEST_EINVAL | TEST_BASIC, "flip-vs-dpms" },
		{ 2, TEST_FLIP | TEST_DPMS_ON_NOP | TEST_CHECK_TS, "flip-vs-dpms-on-nop" },
		{ 30,  TEST_FLIP | TEST_PAN, "flip-vs-panning" },
		{ 2, TEST_FLIP | TEST_MODESET | TEST_EINVAL | TEST_BASIC, "flip-vs-modeset" },
		{ 30,  TEST_FLIP | TEST_VBLANK_EXPIRED_SEQ,
					"flip-vs-expired-vblank" },

		{ 30, TEST_FLIP | TEST_VBLANK | TEST_VBLANK_ABSOLUTE |
		      TEST_CHECK_TS, "flip-vs-absolute-wf_vblank" },
		{ 2, TEST_FLIP | TEST_VBLANK | TEST_CHECK_TS | TEST_BASIC,
					"flip-vs-wf_vblank" },
		{ 30, TEST_FLIP | TEST_VBLANK | TEST_VBLANK_BLOCK |
			TEST_CHECK_TS, "flip-vs-blocking-wf-vblank" },
		{ 1, TEST_FLIP | TEST_MODESET | TEST_HANG | TEST_NOEVENT, "flip-vs-modeset-vs-hang" },
		{ 1, TEST_FLIP | TEST_PAN | TEST_HANG, "flip-vs-panning-vs-hang" },

		{ 1, TEST_DPMS_OFF | TEST_MODESET | TEST_FLIP,
					"flip-vs-dpms-off-vs-modeset" },
		{ 1, TEST_DPMS_OFF | TEST_MODESET | TEST_FLIP | TEST_SINGLE_BUFFER,
					"single-buffer-flip-vs-dpms-off-vs-modeset" },
		{ 30, TEST_FLIP | TEST_NO_2X_OUTPUT | TEST_DPMS_OFF_OTHERS , "dpms-off-confusion" },
		{ 0, TEST_ENOENT | TEST_NOEVENT, "nonexisting-fb" },
		{ 10, TEST_DPMS_OFF | TEST_DPMS | TEST_VBLANK_RACE | TEST_CHECK_TS, "dpms-vs-vblank-race" },
		{ 10, TEST_MODESET | TEST_VBLANK_RACE | TEST_CHECK_TS, "modeset-vs-vblank-race" },
		{ 0, TEST_BO_TOOBIG | TEST_NO_2X_OUTPUT, "bo-too-big" },
		{ 10, TEST_FLIP | TEST_SUSPEND, "flip-vs-suspend" },
	};
	int i;

	igt_fixture {
		drm_fd = drm_open_driver_master(DRIVER_ANY);

		igt_display_require(&display, drm_fd);

		kmstest_set_vt_graphics_mode();
		igt_install_exit_handler(kms_flip_exit_handler);
		get_timestamp_format();

		if (is_i915_device(drm_fd)) {
			bops = buf_ops_create(drm_fd);
		}

		if (should_skip_ts_checks()) {
			igt_info("Skipping timestamp checks\n");
			for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
				tests[i].flags &= ~(TEST_CHECK_TS | TEST_VBLANK_EXPIRED_SEQ);
		}
		max_dotclock = igt_get_max_dotclock(drm_fd);
	}

	igt_describe("Tests that nonblocking reading fails correctly");
	igt_subtest("nonblocking-read")
		test_nonblocking_read(drm_fd);

	for (i = 0; i < sizeof(tests) / sizeof (tests[0]); i++) {
		igt_describe("Basic test for validating modeset, dpms and pageflips");
		igt_subtest_with_dynamic_f("%s%s",
			      tests[i].flags & TEST_BASIC ? "basic-" : "",
			      tests[i].name)
			run_test(tests[i].duration, tests[i].flags);

		if (tests[i].flags & TEST_NO_2X_OUTPUT)
			continue;

		igt_describe("Test for validating modeset, dpms and pageflips with a pair of connected displays");
		igt_subtest_with_dynamic_f("2x-%s", tests[i].name)
			run_pair(tests[i].duration, tests[i].flags);
	}

	igt_fork_signal_helper();
	for (i = 0; i < sizeof(tests) / sizeof (tests[0]); i++) {
		/* relative blocking vblank waits that get constantly interrupt
		 * take forver. So don't do them. */
		if ((tests[i].flags & TEST_VBLANK_BLOCK) &&
		    !(tests[i].flags & TEST_VBLANK_ABSOLUTE))
			continue;

		/*
		 * -EINVAL are negative API tests, they are rejected before
		 *  any waits and so not subject to interruptiblity.
		 *
		 * -EBUSY needs to complete in a single vblank, skip them for
		 * interruptible tests
		 *
		 * HANGs are slow enough and interruptible hang testing is
		 * an oxymoron (can't force the wait-for-hang if being
		 * interrupted all the time).
		 */
		if (tests[i].flags & (TEST_EINVAL | TEST_EBUSY | TEST_HANG))
			continue;

		igt_describe("Interrupt test for validating modeset, dpms and pageflips");
		igt_subtest_with_dynamic_f("%s-interruptible", tests[i].name)
			run_test(tests[i].duration, tests[i].flags);

		if (tests[i].flags & TEST_NO_2X_OUTPUT)
			continue;

		 igt_describe("Interrupt test for validating modeset, dpms and pageflips with pair of connected displays");
		igt_subtest_with_dynamic_f("2x-%s-interruptible", tests[i].name)
			run_pair(tests[i].duration, tests[i].flags);
	}
	igt_stop_signal_helper();

	igt_fixture {
		igt_display_fini(&display);
		drm_close_driver(drm_fd);
	}
}
