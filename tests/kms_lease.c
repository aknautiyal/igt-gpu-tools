/*
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
 * TEST: kms lease
 * Category: Display
 * Description: Test of CreateLease.
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

#include <libudev.h>

#include <drm.h>
#include "igt_device.h"
#include "xe_drm.h"
#include "xe/xe_query.h"

/**
 * SUBTEST: atomic-implicit-crtc
 * Description: Negative test by using a different crtc with atomic ioctl
 *
 * SUBTEST: cursor-implicit-plane
 * Description: Negative test by using a non-primary plane with setcursor ioctl
 *
 * SUBTEST: empty-lease
 * Description: Check that creating an empty lease works
 *
 * SUBTEST: implicit-plane-lease
 * Description: Tests the implicitly added planes.
 *
 * SUBTEST: invalid-create-leases
 * Description: Tests error handling while creating invalid corner-cases for
 *              create-lease ioctl
 *
 * SUBTEST: lease-uevent
 * Description: Tests all the uevent cases
 *
 * SUBTEST: lease-again
 * Description: Tests leasing objects more than once
 *
 * SUBTEST: lease-get
 * Description: Tests getting the required contents of a lease
 *
 * SUBTEST: lease-invalid-connector
 * Description: Tests leasing an invalid connector
 *
 * SUBTEST: lease-invalid-crtc
 * Description: Tests leasing an invalid crtc
 *
 * SUBTEST: lease-invalid-plane
 * Description: Tests leasing an invalid plane
 *
 * SUBTEST: lease-revoke
 * Description: Tests revocation of lease
 *
 * SUBTEST: lease-unleased-connector
 * Description: Negative test by trying to use an unleased connector
 *
 * SUBTEST: lease-unleased-crtc
 * Description: Negative test by trying to use an unleased crtc
 *
 * SUBTEST: lessee-list
 * Description: Check if listed lease is same as created one
 *
 * SUBTEST: master-vs-lease
 * Description: Tests the drop/set_master interactions.
 *
 * SUBTEST: multimaster-lease
 * Description: Tests that the 2nd master can only create leases while being
 *              active master, and that leases on the first master don't prevent
 *              lease creation for the 2nd master.
 *
 * SUBTEST: page-flip-implicit-plane
 * Description: Negative test by using a non-primary plane with the page flip ioctl
 *
 * SUBTEST: possible-crtcs-filtering
 * Description: Tests that  possible_crtcs logically match between master and
 *              lease, and that the values are correctly renumbered on the lease
 *              side.
 *
 * SUBTEST: setcrtc-implicit-plane
 * Description: Negative test by using a non-primary plane with the setcrtc ioctl
 *
 * SUBTEST: simple-lease
 * Description: Check if create lease ioctl call works
 */

IGT_TEST_DESCRIPTION("Test of CreateLease.");

typedef struct {
	int fd;
	uint32_t lessee_id;
	igt_display_t display;
	struct igt_fb primary_fb;
	igt_output_t *output;
	drmModeModeInfo *mode;
} lease_t;

typedef struct {
	lease_t lease;
	lease_t master;
	enum pipe pipe;
	uint32_t crtc_id;
	uint32_t connector_id;
	uint32_t plane_id;
} data_t;

static igt_output_t *connector_id_to_output(igt_display_t *display, uint32_t connector_id)
{
	drmModeConnector		connector;

	connector.connector_id = connector_id;
	return igt_output_from_connector(display, &connector);
}

static int prepare_crtc(data_t *data, bool is_master)
{
	drmModeModeInfo *mode;
	lease_t *lease = is_master ? &data->master : &data->lease;
	igt_display_t *display = &lease->display;
	igt_output_t *output = connector_id_to_output(display, data->connector_id);
	enum pipe pipe = display->pipes[data->pipe].pipe;
	igt_plane_t *primary;
	int ret;

	if (!output)
		return -ENOENT;

	/* select the pipe we want to use */
	igt_output_set_pipe(output, pipe);

	/* create and set the primary plane fb */
	mode = igt_output_get_mode(output);
	igt_create_color_fb(lease->fd, mode->hdisplay, mode->vdisplay,
			    DRM_FORMAT_XRGB8888,
			    DRM_FORMAT_MOD_LINEAR,
			    0.0, 0.0, 0.0,
			    &lease->primary_fb);

	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, &lease->primary_fb);

	ret = igt_display_try_commit2(display, COMMIT_LEGACY);

	if (ret)
		return ret;

	igt_wait_for_vblank(lease->fd, display->pipes[pipe].crtc_offset);

	lease->output = output;
	lease->mode = mode;
	return 0;
}

static void cleanup_crtc(lease_t *lease, igt_output_t *output)
{
	igt_display_t *display = &lease->display;
	igt_plane_t *primary;

	igt_remove_fb(lease->fd, &lease->primary_fb);

	primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);
	igt_plane_set_fb(primary, NULL);

	igt_output_set_pipe(output, PIPE_NONE);
	igt_display_commit(display);
}

static int _create_lease(int fd, struct drm_mode_create_lease *mcl)
{
	int err = 0;

	if (igt_ioctl(fd, DRM_IOCTL_MODE_CREATE_LEASE, mcl))
		err = -errno;
	return err;
}

static int create_lease(int fd, struct drm_mode_create_lease *mcl, int *lease_fd)
{
	int ret;

	ret = _create_lease(fd, mcl);

	if (lease_fd != NULL)
		*lease_fd = mcl->fd;
	return ret;
}

static int revoke_lease(int fd, struct drm_mode_revoke_lease *mrl)
{
	int err = 0;

	if (igt_ioctl(fd, DRM_IOCTL_MODE_REVOKE_LEASE, mrl))
		err = -errno;
	return err;
}

static int list_lessees(int fd, struct drm_mode_list_lessees *mll)
{
	int err = 0;

	if (igt_ioctl(fd, DRM_IOCTL_MODE_LIST_LESSEES, mll))
		err = -errno;
	return err;
}

static int get_lease(int fd, struct drm_mode_get_lease *mgl)
{
	int err = 0;

	if (igt_ioctl(fd, DRM_IOCTL_MODE_GET_LEASE, mgl))
		err = -errno;
	return err;
}

static int make_lease(data_t *data)
{
	uint32_t object_ids[3];
	struct drm_mode_create_lease mcl;
	int ret;

	mcl.object_ids = (uint64_t) (uintptr_t) &object_ids[0];
	mcl.object_count = 0;
	mcl.flags = 0;

	object_ids[mcl.object_count++] = data->connector_id;
	object_ids[mcl.object_count++] = data->crtc_id;
	/* We use universal planes, must add the primary plane */
	object_ids[mcl.object_count++] = data->plane_id;

	ret = create_lease(data->master.fd, &mcl, &data->lease.fd);

	if (ret)
		return ret;

	/* Cache xe_device struct */
	if (is_xe_device(data->lease.fd))
		xe_device_get(data->lease.fd);

	data->lease.lessee_id = mcl.lessee_id;
	return 0;
}

static void terminate_lease(int lease_fd)
{
	/* Remove xe_device from cache. */
	if (is_xe_device(lease_fd))
		xe_device_put(lease_fd);

	close(lease_fd);
}

static int paint_fb(int drm_fd, struct igt_fb *fb, const char *test_name,
		    const char *mode_format_str, const char *connector_str, const char *pipe_str)
{
	cairo_t *cr;

	cr = igt_get_cairo_ctx(drm_fd, fb);

	igt_paint_color_gradient(cr, 0, 0, fb->width, fb->height, 1, 1, 1);
	igt_paint_test_pattern(cr, fb->width, fb->height);

	cairo_move_to(cr, fb->width / 2, fb->height / 2);
	cairo_set_font_size(cr, 36);
	igt_cairo_printf_line(cr, align_hcenter, 10, "%s", test_name);
	igt_cairo_printf_line(cr, align_hcenter, 10, "%s", mode_format_str);
	igt_cairo_printf_line(cr, align_hcenter, 10, "%s", connector_str);
	igt_cairo_printf_line(cr, align_hcenter, 10, "%s", pipe_str);

	igt_put_cairo_ctx(cr);

	return 0;
}

static void simple_lease(data_t *data)
{
	enum pipe pipe = data->pipe;

	/* Create a valid lease */
	igt_assert_eq(make_lease(data), 0);

	igt_display_require(&data->lease.display, data->lease.fd);

	/* Set a mode on the leased output */
	igt_assert_eq(0, prepare_crtc(data, false));

	/* Paint something attractive */
	paint_fb(data->lease.fd, &data->lease.primary_fb, "simple-lease",
		 data->lease.mode->name, igt_output_name(data->lease.output),
		 kmstest_pipe_name(pipe));
	igt_debug_wait_for_keypress("lease");
	cleanup_crtc(&data->lease,
		     connector_id_to_output(&data->lease.display, data->connector_id));
}

static void empty_lease(data_t *data)
{
	struct drm_mode_create_lease mcl = {0};

	igt_assert_eq(create_lease(data->master.fd, &mcl, &data->lease.fd), 0);
}

static void page_flip_implicit_plane(data_t *data)
{
	uint32_t object_ids[3];
	struct drm_mode_create_lease mcl;
	drmModePlaneRes *plane_resources;
	uint32_t wrong_plane_id = 0;
	int i;
	igt_display_t *display;
	enum pipe pipe = data->pipe;

	/* find a plane which isn't the primary one for us */
	plane_resources = drmModeGetPlaneResources(data->master.fd);
	for (i = 0; i < plane_resources->count_planes; i++) {
		if (plane_resources->planes[i] != data->plane_id) {
			wrong_plane_id = plane_resources->planes[i];
			break;
		}
	}
	drmModeFreePlaneResources(plane_resources);
	igt_require(wrong_plane_id);

	mcl.object_ids = (uint64_t) (uintptr_t) &object_ids[0];
	mcl.object_count = 0;
	mcl.flags = 0;

	object_ids[mcl.object_count++] = data->connector_id;
	object_ids[mcl.object_count++] = data->crtc_id;

	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
	do_or_die(create_lease(data->master.fd, &mcl, &data->lease.fd));
	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	/* Set a mode on the leased output */
	igt_assert_eq(0, prepare_crtc(data, true));

	/* sanity check */
	do_or_die(drmModePageFlip(data->master.fd, data->crtc_id,
			      data->master.primary_fb.fb_id,
			      0, NULL));

	display = &data->master.display;

	igt_wait_for_vblank(data->master.fd,
			display->pipes[pipe].crtc_offset);

	do_or_die(drmModePageFlip(data->lease.fd, data->crtc_id,
			      data->master.primary_fb.fb_id,
			      0, NULL));
	close(data->lease.fd);

	object_ids[mcl.object_count++] = wrong_plane_id;
	do_or_die(create_lease(data->master.fd, &mcl, &data->lease.fd));

	igt_wait_for_vblank(data->master.fd,
			display->pipes[pipe].crtc_offset);

	igt_assert_eq(drmModePageFlip(data->lease.fd, data->crtc_id,
				      data->master.primary_fb.fb_id,
				      0, NULL),
		      -EACCES);

	cleanup_crtc(&data->master,
		     connector_id_to_output(&data->master.display, data->connector_id));
}

static void setcrtc_implicit_plane(data_t *data)
{
	uint32_t object_ids[3];
	struct drm_mode_create_lease mcl;
	drmModePlaneRes *plane_resources;
	uint32_t wrong_plane_id = 0;
	int ret = 0, ret_mcl = 0;
	igt_output_t *output =
		connector_id_to_output(&data->master.display,
				       data->connector_id);
	drmModeModeInfo *mode = igt_output_get_mode(output);
	int i;

	/* find a plane which isn't the primary one for us */
	plane_resources = drmModeGetPlaneResources(data->master.fd);
	for (i = 0; i < plane_resources->count_planes; i++) {
		if (plane_resources->planes[i] != data->plane_id) {
			wrong_plane_id = plane_resources->planes[i];
			break;
		}
	}
	drmModeFreePlaneResources(plane_resources);
	igt_require(wrong_plane_id);

	mcl.object_ids = (uint64_t) (uintptr_t) &object_ids[0];
	mcl.object_count = 0;
	mcl.flags = 0;

	object_ids[mcl.object_count++] = data->connector_id;
	object_ids[mcl.object_count++] = data->crtc_id;

	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
	do_or_die(create_lease(data->master.fd, &mcl, &data->lease.fd));
	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	/*
	 * For Legacy commit, If userspace wants to send modes with aspect-ratio bits
	 * then the client cap for aspect-ratio bits must be set.
	 */
	if (mode->flags & DRM_MODE_FLAG_PIC_AR_MASK) {
		drmSetClientCap(data->lease.fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);
	}

	/* Set a mode on the leased output */
	igt_assert_eq(0, prepare_crtc(data, true));

	/* sanity check */
	ret = drmModeSetCrtc(data->master.fd, data->crtc_id, -1,
			     0, 0, object_ids, 1, mode);
	ret_mcl = drmModeSetCrtc(data->lease.fd, data->crtc_id, -1,
				 0, 0, object_ids, 1, mode);
	close(data->lease.fd);
	igt_assert_eq(ret, 0);
	igt_assert_eq(ret_mcl, 0);

	object_ids[mcl.object_count++] = wrong_plane_id;
	do_or_die(create_lease(data->master.fd, &mcl, &data->lease.fd));
	igt_assert_eq(drmModeSetCrtc(data->lease.fd, data->crtc_id, -1,
				     0, 0, object_ids, 1, mode),
		      -EACCES);
	/* make sure we are allowed to turn the CRTC off */
	do_or_die(drmModeSetCrtc(data->lease.fd, data->crtc_id,
				 0, 0, 0, NULL, 0, NULL));

	cleanup_crtc(&data->master,
		     connector_id_to_output(&data->master.display, data->connector_id));
}

static void cursor_implicit_plane(data_t *data)
{
	uint32_t object_ids[3];
	struct drm_mode_create_lease mcl;

	mcl.object_ids = (uint64_t) (uintptr_t) &object_ids[0];
	mcl.object_count = 0;
	mcl.flags = 0;

	object_ids[mcl.object_count++] = data->connector_id;
	object_ids[mcl.object_count++] = data->crtc_id;

	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
	do_or_die(create_lease(data->master.fd, &mcl, &data->lease.fd));
	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	/* Set a mode on the leased output */
	igt_assert_eq(0, prepare_crtc(data, true));

	/* sanity check */
	do_or_die(drmModeSetCursor(data->master.fd, data->crtc_id, 0, 0, 0));
	do_or_die(drmModeSetCursor(data->lease.fd, data->crtc_id, 0, 0, 0));
	close(data->lease.fd);

	/* primary plane is never the cursor */
	object_ids[mcl.object_count++] = data->plane_id;
	do_or_die(create_lease(data->master.fd, &mcl, &data->lease.fd));

	igt_assert_eq(drmModeSetCursor(data->lease.fd, data->crtc_id, 0, 0, 0),
		      -EACCES);

	cleanup_crtc(&data->master,
		     connector_id_to_output(&data->master.display, data->connector_id));
}

static void atomic_implicit_crtc(data_t *data)
{
	uint32_t object_ids[3];
	struct drm_mode_create_lease mcl;
	drmModeRes *resources;
	drmModeObjectPropertiesPtr props;
	uint32_t wrong_crtc_id = 0;
	uint32_t crtc_id_prop = 0;
	drmModeAtomicReqPtr req = NULL;
	int ret;

	igt_require(data->master.display.is_atomic);

	mcl.object_ids = (uint64_t) (uintptr_t) &object_ids[0];
	mcl.object_count = 0;
	mcl.flags = 0;

	object_ids[mcl.object_count++] = data->connector_id;
	object_ids[mcl.object_count++] = data->plane_id;

	/* find a plane which isn't the primary one for us */
	resources = drmModeGetResources(data->master.fd);
	igt_assert(resources);
	for (int i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] != data->crtc_id) {
			wrong_crtc_id = resources->crtcs[i];
			break;
		}
	}
	drmModeFreeResources(resources);
	igt_require(wrong_crtc_id);
	object_ids[mcl.object_count++] = wrong_crtc_id;

	/* find the CRTC_ID prop, it's global */
	props = drmModeObjectGetProperties(data->master.fd, data->plane_id,
					   DRM_MODE_OBJECT_PLANE);
	igt_assert(props);
	for (int i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(data->master.fd,
							     props->props[i]);
		if (strcmp(prop->name, "CRTC_ID") == 0)
			crtc_id_prop = props->props[i];

		igt_info("prop name %s, prop id %u, prop id %u\n", prop->name,
			 props->props[i], prop->prop_id);
		drmModeFreeProperty(prop);
		if (crtc_id_prop)
			break;
	}
	drmModeFreeObjectProperties(props);
	igt_assert(crtc_id_prop);

	do_or_die(create_lease(data->master.fd, &mcl, &data->lease.fd));
	do_or_die(drmSetClientCap(data->lease.fd, DRM_CLIENT_CAP_ATOMIC, 1));

	/* check CRTC_ID property on the plane */
	req = drmModeAtomicAlloc();
	igt_assert(req);
	ret = drmModeAtomicAddProperty(req, data->plane_id,
				       crtc_id_prop, data->crtc_id);
	igt_assert_lte(0, ret);

	/* sanity check */
	ret = drmModeAtomicCommit(data->master.fd, req, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
	igt_assert(ret == 0 || ret == -EINVAL);

	ret = drmModeAtomicCommit(data->lease.fd, req, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
	igt_assert(ret == -EACCES);
	drmModeAtomicFree(req);

	/* check CRTC_ID property on the connector */
	req = drmModeAtomicAlloc();
	igt_assert(req);
	ret = drmModeAtomicAddProperty(req, data->connector_id,
				       crtc_id_prop, data->crtc_id);
	igt_assert_lte(0, ret);

	/* sanity check */
	ret = drmModeAtomicCommit(data->master.fd, req, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
	igt_assert(ret == 0 || ret == -EINVAL);

	ret = drmModeAtomicCommit(data->lease.fd, req, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
	igt_assert(ret == -EACCES);
	drmModeAtomicFree(req);
}

/* Test listing lessees */
static void lessee_list(data_t *data)
{
	struct drm_mode_list_lessees mll;
	uint32_t lessees[1];

	mll.pad = 0;

	/* Create a valid lease */
	igt_assert_eq(make_lease(data), 0);

	/* check for nested leases */
	mll.count_lessees = 0;
	mll.lessees_ptr = 0;
	igt_assert_eq(list_lessees(data->lease.fd, &mll), 0);
	igt_assert_eq(mll.count_lessees, 0);

	/* Get the number of lessees */
	mll.count_lessees = 0;
	mll.lessees_ptr = 0;
	igt_assert_eq(list_lessees(data->master.fd, &mll), 0);

	/* Make sure there's a single lessee */
	igt_assert_eq(mll.count_lessees, 1);

	/* invalid ptr */
	igt_assert_eq(list_lessees(data->master.fd, &mll), -EFAULT);

	mll.lessees_ptr = (uint64_t) (uintptr_t) &lessees[0];

	igt_assert_eq(list_lessees(data->master.fd, &mll), 0);

	/* Make sure there's a single lessee */
	igt_assert_eq(mll.count_lessees, 1);

	/* Make sure the listed lease is the same as the one we created */
	igt_assert_eq(lessees[0], data->lease.lessee_id);

	/* invalid pad */
	mll.pad = -1;
	igt_assert_eq(list_lessees(data->master.fd, &mll), -EINVAL);
	mll.pad = 0;

	terminate_lease(data->lease.fd);

	/* Make sure the lease is gone */
	igt_assert_eq(list_lessees(data->master.fd, &mll), 0);
	igt_assert_eq(mll.count_lessees, 0);
}

/* Test getting the contents of a lease */
static void lease_get(data_t *data)
{
	struct drm_mode_get_lease mgl;
	int num_leased_obj = 3;
	uint32_t objects[num_leased_obj];
	int o;

	mgl.pad = 0;

	/* Create a valid lease */
	igt_assert_eq(make_lease(data), 0);

	/* Get the number of objects */
	mgl.count_objects = 0;
	mgl.objects_ptr = 0;
	igt_assert_eq(get_lease(data->lease.fd, &mgl), 0);

	/* Make sure it's 2 */
	igt_assert_eq(mgl.count_objects, num_leased_obj);

	/* Get the objects */
	mgl.objects_ptr = (uint64_t) (uintptr_t) objects;

	igt_assert_eq(get_lease(data->lease.fd, &mgl), 0);

	/* Make sure it's 2 */
	igt_assert_eq(mgl.count_objects, num_leased_obj);

	/* Make sure we got the connector, crtc and plane back */
	for (o = 0; o < num_leased_obj; o++)
		if (objects[o] == data->connector_id)
			break;

	igt_assert_neq(o, num_leased_obj);

	for (o = 0; o < num_leased_obj; o++)
		if (objects[o] == data->crtc_id)
			break;

	igt_assert_neq(o, num_leased_obj);

	for (o = 0; o < num_leased_obj; o++)
		if (objects[o] == data->plane_id)
			break;

	igt_assert_neq(o, num_leased_obj);

	/* invalid pad */
	mgl.pad = -1;
	igt_assert_eq(get_lease(data->lease.fd, &mgl), -EINVAL);
	mgl.pad = 0;

	/* invalid pointer */
	mgl.objects_ptr = 0;
	igt_assert_eq(get_lease(data->lease.fd, &mgl), -EFAULT);
}

static void lease_unleased_crtc(data_t *data)
{
	enum pipe p;
	uint32_t bad_crtc_id;
	drmModeCrtc *crtc;
	int ret;

	/* Create a valid lease */
	igt_assert_eq(make_lease(data), 0);

	igt_display_require(&data->lease.display, data->lease.fd);

	/* Find another CRTC that we don't control */
	bad_crtc_id = 0;

	for_each_pipe(&data->master.display, p) {
		if (bad_crtc_id != 0)
			break;
		if (data->master.display.pipes[p].crtc_id != data->crtc_id)
			bad_crtc_id = data->master.display.pipes[p].crtc_id;
	}

	/* Give up if there isn't another crtc */
	igt_skip_on(bad_crtc_id == 0);

	/* sanity check */
	ret = drmModeSetCrtc(data->lease.fd, data->crtc_id, 0, 0, 0, NULL, 0, NULL);
	igt_assert_eq(ret, 0);
	crtc = drmModeGetCrtc(data->lease.fd, data->crtc_id);
	igt_assert(crtc);
	drmModeFreeCrtc(crtc);

	/* Attempt to use the unleased crtc id. We need raw ioctl to bypass the
	 * igt_kms helpers.
	 */
	ret = drmModeSetCrtc(data->lease.fd, bad_crtc_id, 0, 0, 0, NULL, 0, NULL);
	igt_assert_eq(ret, -ENOENT);
	crtc = drmModeGetCrtc(data->lease.fd, bad_crtc_id);
	igt_assert(!crtc);
	igt_assert_eq(errno, ENOENT);
	drmModeFreeCrtc(crtc);
}

static void lease_unleased_connector(data_t *data)
{
	int o;
	uint32_t bad_connector_id;
	drmModeConnector *c;

	/* Create a valid lease */
	igt_assert_eq(make_lease(data), 0);

	igt_display_require(&data->lease.display, data->lease.fd);

	/* Find another connector that we don't control */
	bad_connector_id = 0;
	for (o = 0; bad_connector_id == 0 && o < data->master.display.n_outputs; o++) {
		if (data->master.display.outputs[o].id != data->connector_id)
			bad_connector_id = data->master.display.outputs[o].id;
	}

	/* Give up if there isn't another connector */
	igt_skip_on(bad_connector_id == 0);

	/* sanity check */
	c = drmModeGetConnector(data->lease.fd, data->connector_id);
	igt_assert(c);

	/* Attempt to use the unleased connector id. Note that the
	 */
	c = drmModeGetConnector(data->lease.fd, bad_connector_id);
	igt_assert(!c);
	igt_assert_eq(errno, ENOENT);
}

/* Test revocation of lease */
static void lease_revoke(data_t *data)
{
	struct drm_mode_revoke_lease mrl;
	int ret;

	/* Create a valid lease */
	igt_assert_eq(make_lease(data), 0);

	igt_display_require(&data->lease.display, data->lease.fd);

	/* try to revoke an invalid lease */
	mrl.lessee_id = 0;
	igt_assert_eq(revoke_lease(data->master.fd, &mrl), -ENOENT);

	/* try to revoke with the wrong fd */
	mrl.lessee_id = data->lease.lessee_id;
	igt_assert_eq(revoke_lease(data->lease.fd, &mrl), -EACCES);

	/* Revoke the lease using the master fd */
	mrl.lessee_id = data->lease.lessee_id;
	igt_assert_eq(revoke_lease(data->master.fd, &mrl), 0);

	/* Try to use the leased objects */
	ret = prepare_crtc(data, false);

	/* Ensure that the expected error is returned */
	igt_assert_eq(ret, -ENOENT);

	terminate_lease(data->lease.fd);

	/* make sure the lease is gone */
	mrl.lessee_id = data->lease.lessee_id;
	igt_assert_eq(revoke_lease(data->master.fd, &mrl), -ENOENT);
}

/* Test leasing objects more than once */
static void lease_again(data_t *data)
{

	/* Create a valid lease */
	igt_assert_eq(make_lease(data), 0);

	/* Attempt to re-lease the same objects */
	igt_assert_eq(make_lease(data), -EBUSY);

	terminate_lease(data->lease.fd);

	/* Now attempt to lease the same objects */
	igt_assert_eq(make_lease(data), 0);
}

#define assert_unleased(ret) \
	igt_assert_f((ret) == -EINVAL || (ret) == -ENOENT, \
		     "wrong return code %i, %s\n", ret, \
		     strerror(ret))
/* Test leasing an invalid connector */
static void lease_invalid_connector(data_t *data)
{
	uint32_t save_connector_id;
	int ret;

	/* Create an invalid lease */
	save_connector_id = data->connector_id;
	data->connector_id = 0xbaadf00d;
	ret = make_lease(data);
	data->connector_id = save_connector_id;
	assert_unleased(ret);
}

/* Test leasing an invalid crtc */
static void lease_invalid_crtc(data_t *data)
{
	uint32_t save_crtc_id;
	int ret;

	/* Create an invalid lease */
	save_crtc_id = data->crtc_id;
	data->crtc_id = 0xbaadf00d;
	ret = make_lease(data);
	data->crtc_id = save_crtc_id;
	assert_unleased(ret);
}

static void lease_invalid_plane(data_t *data)
{
	uint32_t save_plane_id;
	int ret;

	/* Create an invalid lease */
	save_plane_id = data->plane_id;
	data->plane_id = 0xbaadf00d;
	ret = make_lease(data);
	data->plane_id = save_plane_id;
	assert_unleased(ret);
}

#define assert_double_id_err(ret) \
	igt_assert_f((ret) == -EBUSY || (ret) == -ENOSPC, \
		     "wrong return code %i, %s\n", ret, \
		     strerror(ret))
static void invalid_create_leases(data_t *data)
{
	uint32_t object_ids[4];
	struct drm_mode_create_lease mcl = {0};
	drmModeRes *resources;
	int tmp_fd, ret;

	/* NULL array pointer */
	mcl.object_count = 1;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -EFAULT);

	/* nil object */
	object_ids[0] = 0;
	mcl.object_ids = (uint64_t) (uintptr_t) object_ids;
	mcl.object_count = 1;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -ENOENT);

	/* no crtc, non-universal_plane */
	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);
	object_ids[0] = data->master.display.outputs[0].id;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -EINVAL);

	/* no connector, non-universal_plane */
	object_ids[0] = data->master.display.pipes[0].crtc_id;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -EINVAL);

	/* sanity check */
	object_ids[0] = data->master.display.pipes[0].crtc_id;
	object_ids[1] = data->master.display.outputs[0].id;
	mcl.object_count = 2;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), 0);
	close(mcl.fd);

	/* no plane, universal planes */
	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -EINVAL);

	/* sanity check */
	object_ids[2] = igt_pipe_get_plane_type(&data->master.display.pipes[0],
						DRM_PLANE_TYPE_PRIMARY)->drm_plane->plane_id;
	mcl.object_count = 3;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), 0);
	close(mcl.fd);

	/* array overflow, do a small scan around overflow sizes */
	for (int i = 1; i <= 4; i++) {
		mcl.object_count = UINT32_MAX / sizeof(object_ids[0]) + i;
		igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -ENOMEM);
	}

	/* sanity check */
	mcl.object_count = 3;
	mcl.flags = O_CLOEXEC | O_NONBLOCK;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), 0);
	close(mcl.fd);

	/* invalid flags */
	mcl.flags = -1;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -EINVAL);

	/* no subleasing */
	mcl.object_count = 3;
	mcl.flags = 0;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), 0);
	tmp_fd = mcl.fd;
	igt_assert_eq(create_lease(tmp_fd, &mcl, NULL), -EINVAL);
	close(tmp_fd);

	/* no double-leasing */
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), 0);
	tmp_fd = mcl.fd;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -EBUSY);
	close(tmp_fd);

	/* no double leasing */
	object_ids[3] = object_ids[2];
	mcl.object_count = 4;
	/* Note: the ENOSPC is from idr double-insertion failing */
	ret = create_lease(data->master.fd, &mcl, NULL);
	assert_double_id_err(ret);

	/* no encoder leasing */
	resources = drmModeGetResources(data->master.fd);
	igt_assert(resources);
	igt_assert(resources->count_encoders > 0);
	object_ids[3] = resources->encoders[0];
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), -EINVAL);
	drmModeFreeResources(resources);
}

static void check_crtc_masks(int master_fd, int lease_fd, uint32_t crtc_mask)
{
	drmModeRes *resources;
	drmModePlaneRes *plane_resources;
	int i;

	resources = drmModeGetResources(master_fd);
	igt_assert(resources);
	plane_resources = drmModeGetPlaneResources(master_fd);
	igt_assert(plane_resources);

	for (i = 0; i < resources->count_encoders; i++) {
		drmModeEncoder *master_e, *lease_e;
		bool possible;

		master_e = drmModeGetEncoder(master_fd, resources->encoders[i]);
		igt_assert(master_e);
		lease_e = drmModeGetEncoder(lease_fd, resources->encoders[i]);
		igt_assert(lease_e);

		possible = master_e->possible_crtcs & crtc_mask;

		igt_assert_eq(lease_e->possible_crtcs,
			      possible ? 1 : 0);
		igt_assert_eq(master_e->possible_crtcs & crtc_mask,
			      possible ? crtc_mask : 0);
		drmModeFreeEncoder(master_e);
		drmModeFreeEncoder(lease_e);
	}

	for (i = 0; i < plane_resources->count_planes; i++) {
		drmModePlane *master_p, *lease_p;
		bool possible;

		master_p = drmModeGetPlane(master_fd, plane_resources->planes[i]);
		igt_assert(master_p);
		lease_p = drmModeGetPlane(lease_fd, plane_resources->planes[i]);
		igt_assert(lease_p);

		possible = master_p->possible_crtcs & crtc_mask;

		igt_assert_eq(lease_p->possible_crtcs,
			      possible ? 1 : 0);
		igt_assert_eq(master_p->possible_crtcs & crtc_mask,
			      possible ? crtc_mask : 0);
		drmModeFreePlane(master_p);
		drmModeFreePlane(lease_p);
	}

	drmModeFreePlaneResources(plane_resources);
	drmModeFreeResources(resources);
}

static void possible_crtcs_filtering(data_t *data)
{
	uint32_t *object_ids;
	struct drm_mode_create_lease mcl;
	drmModeRes *resources;
	drmModePlaneRes *plane_resources;
	int i;
	int master_fd = data->master.fd;

	resources = drmModeGetResources(master_fd);
	igt_assert(resources);
	plane_resources = drmModeGetPlaneResources(master_fd);
	igt_assert(plane_resources);
	mcl.object_count = resources->count_connectors +
		plane_resources->count_planes + 1;
	object_ids = calloc(mcl.object_count, sizeof(*object_ids));
	igt_assert(object_ids);

	for (i = 0; i < resources->count_connectors; i++)
		object_ids[i] = resources->connectors[i];

	for (i = 0; i < plane_resources->count_planes; i++)
		object_ids[i + resources->count_connectors] =
			plane_resources->planes[i];

	mcl.object_ids = (uint64_t) (uintptr_t) object_ids;
	mcl.flags = 0;

	for (i = 0; i < resources->count_crtcs; i++) {
		int lease_fd;

		object_ids[mcl.object_count - 1] =
			resources->crtcs[i];

		igt_assert_eq(create_lease(master_fd, &mcl, NULL), 0);
		lease_fd = mcl.fd;

		drmSetClientCap(lease_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

		check_crtc_masks(master_fd, lease_fd, 1 << i);

		close(lease_fd);
	}

	free(object_ids);
	drmModeFreePlaneResources(plane_resources);
	drmModeFreeResources(resources);
}

static bool is_master(int fd)
{
	/* FIXME: replace with drmIsMaster once we bumped libdrm version */
	return drmAuthMagic(fd, 0) != -EACCES;
}

static int _create_simple_lease(int master_fd, data_t *data, int expected_ret)
{
	uint32_t object_ids[3];
	struct drm_mode_create_lease mcl;

	object_ids[0] = data->master.display.pipes[0].crtc_id;
	object_ids[1] = data->master.display.outputs[0].id;
	object_ids[2] = igt_pipe_get_plane_type(&data->master.display.pipes[0],
						DRM_PLANE_TYPE_PRIMARY)->drm_plane->plane_id;
	mcl.object_ids = (uint64_t) (uintptr_t) object_ids;
	mcl.object_count = 3;
	mcl.flags = 0;

	igt_assert_eq(create_lease(master_fd, &mcl, NULL), expected_ret);

	return expected_ret == 0 ? mcl.fd : 0;
}

static int create_simple_lease(int master_fd, data_t *data)
{
	return _create_simple_lease(master_fd, data, 0);
}

/* check lease master status in lockdep with lessors, but can't change it
 * themselves */
static void master_vs_lease(data_t *data)
{
	int lease_fd;

	lease_fd = create_simple_lease(data->master.fd, data);

	igt_assert_eq(drmDropMaster(lease_fd), -1);
	igt_assert_eq(errno, EINVAL);

	igt_assert(is_master(data->master.fd));
	igt_assert(is_master(lease_fd));

	igt_device_drop_master(data->master.fd);

	igt_assert(!is_master(data->master.fd));
	igt_assert(!is_master(lease_fd));

	igt_assert_eq(drmSetMaster(lease_fd), -1);
	igt_assert_eq(errno, EINVAL);

	igt_device_set_master(data->master.fd);

	igt_assert(is_master(data->master.fd));
	igt_assert(is_master(lease_fd));

	close(lease_fd);
}

static void multimaster_lease(data_t *data)
{
	int lease_fd, master2_fd, lease2_fd;

	lease_fd = create_simple_lease(data->master.fd, data);

	igt_assert(is_master(data->master.fd));
	igt_assert(is_master(lease_fd));

	master2_fd = drm_open_driver(DRIVER_ANY);

	igt_assert(!is_master(master2_fd));

	_create_simple_lease(master2_fd, data, -EACCES);

	igt_device_drop_master(data->master.fd);
	igt_device_set_master(master2_fd);

	igt_assert(!is_master(data->master.fd));
	igt_assert(!is_master(lease_fd));
	igt_assert(is_master(master2_fd));

	drmSetClientCap(master2_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	lease2_fd = create_simple_lease(master2_fd, data);

	drm_close_driver(master2_fd); /* close is an implicit DropMaster */
	igt_assert(!is_master(lease2_fd));

	igt_device_set_master(data->master.fd);
	igt_assert(is_master(data->master.fd));
	igt_assert(is_master(lease_fd));

	close(lease2_fd);
	close(lease_fd);
}

static void implicit_plane_lease(data_t *data)
{
	uint32_t object_ids[3];
	struct drm_mode_create_lease mcl;
	struct drm_mode_get_lease mgl;
	int ret;
	uint32_t cursor_id = igt_pipe_get_plane_type(&data->master.display.pipes[0],
						     DRM_PLANE_TYPE_CURSOR)->drm_plane->plane_id;

	object_ids[0] = data->master.display.pipes[0].crtc_id;
	object_ids[1] = data->master.display.outputs[0].id;
	object_ids[2] = igt_pipe_get_plane_type(&data->master.display.pipes[0],
						DRM_PLANE_TYPE_PRIMARY)->drm_plane->plane_id;
	mcl.object_ids = (uint64_t) (uintptr_t) object_ids;
	mcl.object_count = 3;
	mcl.flags = 0;

	/* sanity check */
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), 0);
	close(mcl.fd);
	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 0);

	/* non universal plane automatically adds primary/cursor plane */
	mcl.object_count = 2;
	igt_assert_eq(create_lease(data->master.fd, &mcl, NULL), 0);

	mgl.pad = 0;
	mgl.count_objects = 0;
	mgl.objects_ptr = 0;
	igt_assert_eq(get_lease(mcl.fd, &mgl), 0);

	igt_assert_eq(mgl.count_objects, 3 + (cursor_id ? 1 : 0));

	close(mcl.fd);

	/* check that implicit lease doesn't lead to confusion when
	 * explicitly adding primary plane */
	mcl.object_count = 3;
	ret = create_lease(data->master.fd, &mcl, NULL);
	assert_double_id_err(ret);

	/* same for the cursor */
	object_ids[2] = cursor_id;
	ret = create_lease(data->master.fd, &mcl, NULL);
	assert_double_id_err(ret);

	drmSetClientCap(data->master.fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
}

static void lease_uevent(data_t *data)
{
	int lease_fd;
	struct drm_mode_list_lessees mll;
	struct udev_monitor *uevent_monitor;

	uevent_monitor = igt_watch_uevents();

	igt_flush_uevents(uevent_monitor);

	lease_fd = create_simple_lease(data->master.fd, data);

	igt_assert(!igt_lease_change_detected(uevent_monitor, 1));

	mll.pad = 0;
	mll.count_lessees = 0;
	mll.lessees_ptr = 0;
	igt_assert_eq(list_lessees(data->master.fd, &mll), 0);
	igt_assert_eq(mll.count_lessees, 1);

	close(lease_fd);

	igt_assert(igt_lease_change_detected(uevent_monitor, 1));

	mll.lessees_ptr = 0;
	igt_assert_eq(list_lessees(data->master.fd, &mll), 0);
	igt_assert_eq(mll.count_lessees, 0);

	igt_cleanup_uevents(uevent_monitor);
}

igt_main
{
	data_t data;
	igt_output_t *output;
	igt_display_t *display = &data.master.display;

	igt_fixture {
		data.master.fd = drm_open_driver_master(DRIVER_ANY);
		kmstest_set_vt_graphics_mode();
		igt_display_require(display, data.master.fd);
	}

	//Display dependent subtests
	igt_subtest_group {

		const struct {
			const char *name;
			void (*func)(data_t *);
			const char *desc;
		} funcs[] = {
			{ "simple-lease", simple_lease, "Check if create lease ioctl call works" },
			{ "empty-lease", empty_lease, "Check that creating an empty lease works" },
			{ "lessee-list", lessee_list, "Check if listed lease is same as created one" },
			{ "lease-get", lease_get, "Tests getting the required contents of a lease" },
			{ "lease-unleased-connector", lease_unleased_connector, "Negative test by trying to"
				" use an unleased connector " },
			{ "lease-unleased-crtc", lease_unleased_crtc, "Negative test by trying to use an unleased crtc" },
			{ "lease-revoke", lease_revoke, "Tests revocation of lease" },
			{ "lease-again", lease_again, "Tests leasing objects more than once" },
			{ "lease-invalid-connector", lease_invalid_connector, "Tests leasing an invalid connector" },
			{ "lease-invalid-crtc", lease_invalid_crtc, "Tests leasing an invalid crtc" },
			{ "lease-invalid-plane", lease_invalid_plane, "Tests leasing an invalid plane" },
			{ "page-flip-implicit-plane", page_flip_implicit_plane, "Negative test by using a "
				"non-primary plane with the page flip ioctl" },
			{ "setcrtc-implicit-plane", setcrtc_implicit_plane, "Negative test by using a "
				"non-primary plane with the setcrtc ioctl" },
			{ "cursor-implicit-plane", cursor_implicit_plane, "Negative test by using a non-primary"
				" plane with setcursor ioctl" },
			{ "atomic-implicit-crtc", atomic_implicit_crtc, "Negative test by using a different"
				" crtc with atomic ioctl" },
			{ }
		}, *f;

		igt_fixture
			igt_display_require_output(display);

		for (f = funcs; f->name; f++) {

			igt_describe(f->desc);
			igt_subtest_with_dynamic_f("%s", f->name) {
				for_each_pipe_with_valid_output(display, data.pipe, output) {
					igt_display_reset(display);

					igt_output_set_pipe(output, data.pipe);
					if (!intel_pipe_output_combo_valid(display))
						continue;

					igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(data.pipe),
						      igt_output_name(output)) {
						data.crtc_id = display->pipes[data.pipe].crtc_id;
						data.connector_id = output->id;
						data.plane_id =
							igt_pipe_get_plane_type(&data.master.display.pipes[data.pipe],
									DRM_PLANE_TYPE_PRIMARY)->drm_plane->plane_id;
						f->func(&data);
					}
					terminate_lease(data.lease.fd);
				}
			}
		}
	}

	//Display independent subtests
	igt_subtest_group {

		igt_describe("Tests error handling while creating invalid corner-cases for "
			     "create-lease ioctl");
		igt_subtest("invalid-create-leases")
			invalid_create_leases(&data);

		igt_describe("Tests that  possible_crtcs logically match between master and "
			     "lease, and that the values are correctly renumbered on the lease side.");
		igt_subtest("possible-crtcs-filtering")
			possible_crtcs_filtering(&data);

		igt_describe("Tests the drop/set_master interactions.");
		igt_subtest("master-vs-lease")
			master_vs_lease(&data);

		igt_describe("Tests that the 2nd master can only create leases while being active "
			     "master, and that leases on the first master don't prevent lease creation "
			     "for the 2nd master.");
		igt_subtest("multimaster-lease")
			multimaster_lease(&data);

		igt_describe("Tests the implicitly added planes.");
		igt_subtest("implicit-plane-lease")
			implicit_plane_lease(&data);

		igt_describe("Tests all the uevent cases");
		igt_subtest("lease-uevent")
			lease_uevent(&data);
	}

	igt_fixture {
		igt_display_fini(display);
		drm_close_driver(data.master.fd);
	}
}
