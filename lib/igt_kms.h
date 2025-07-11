/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright © 2013 Intel Corporation
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
 * 	Daniel Vetter <daniel.vetter@ffwll.ch>
 * 	Damien Lespiau <damien.lespiau@intel.com>
 */

#ifndef __IGT_KMS_H__
#define __IGT_KMS_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <limits.h>

#include <xf86drmMode.h>

#include "igt_fb.h"
#include "ioctl_wrappers.h"

/* Low-level helpers with kmstest_ prefix */

/**
 * Clients which do set cursor hotspot and treat the cursor plane
 * like a mouse cursor should set this property.
 */
#define LOCAL_DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT	6

/**
 * pipe:
 * @PIPE_NONE: Invalid pipe, used for disconnecting a output from a pipe.
 * @PIPE_ANY: Deprecated alias for @PIPE_NONE.
 * @PIPE_A: First crtc.
 * @PIPE_B: Second crtc.
 * @PIPE_C: Third crtc.
 * @PIPE_D: Fourth crtc.
 * @PIPE_E: Fifth crtc.
 * @PIPE_F: Sixth crtc.
 * @PIPE_G: Seventh crtc.
 * @PIPE_H: Eighth crtc.
 * @PIPE_I: Ninth crtc.
 * @PIPE_J: Tenth crtc.
 * @PIPE_K: Eleventh crtc.
 * @PIPE_L: Twelfth crtc.
 * @PIPE_M: Thirteenth crtc.
 * @PIPE_N: Fourteenth crtc.
 * @PIPE_O: Fifteenth crtc.
 * @PIPE_P: Sixteenth crtc.
 * @IGT_MAX_PIPES: Max number of pipes allowed.
 */
enum pipe {
        PIPE_NONE = -1,
        PIPE_ANY = PIPE_NONE,
        PIPE_A = 0,
        PIPE_B,
        PIPE_C,
        PIPE_D,
        PIPE_E,
        PIPE_F,
        PIPE_G,
        PIPE_H,
        PIPE_I,
        PIPE_J,
        PIPE_K,
        PIPE_L,
        PIPE_M,
        PIPE_N,
        PIPE_O,
        PIPE_P,
        IGT_MAX_PIPES
};
const char *kmstest_pipe_name(enum pipe pipe);
int kmstest_pipe_to_index(char pipe);
const char *kmstest_plane_type_name(int plane_type);

enum port {
        PORT_A = 0,
        PORT_B,
        PORT_C,
        PORT_D,
        PORT_E,
        I915_MAX_PORTS
};

/**
 * igt_custom_edid_type:
 *
 * Enum used for the helper function igt_custom_edid_type
 * @IGT_CUSTOM_EDID_BASE: Returns base edid
 * @IGT_CUSTOM_EDID_FULL: Returns edid with full list of standard timings.
 * @IGT_CUSTOM_EDID_ALT: Returns alternate edid
 * @IGT_CUSTOM_EDID_HDMI_AUDIO: Returns edid with HDMI audio block
 * @IGT_CUSTOM_EDID_DP_AUDIO: Returns edid with DP audio block
 * @IGT_CUSTOM_EDID_ASPECT_RATIO: Returns base edid with apect ratio data block
 */
enum igt_custom_edid_type {
	IGT_CUSTOM_EDID_BASE,
	IGT_CUSTOM_EDID_FULL,
	IGT_CUSTOM_EDID_ALT,
	IGT_CUSTOM_EDID_HDMI_AUDIO,
	IGT_CUSTOM_EDID_DP_AUDIO,
	IGT_CUSTOM_EDID_ASPECT_RATIO,
};
#define IGT_CUSTOM_EDID_COUNT 6

/**
 * kmstest_port_name:
 * @port: display plane
 *
 * Returns: String representing @port, e.g. "A".
 */
#define kmstest_port_name(port) ((port) + 'A')

enum dsc_output_format {
	DSC_FORMAT_RGB = 0,
	DSC_FORMAT_YCBCR420,
	DSC_FORMAT_YCBCR444,
};

const char *kmstest_encoder_type_str(int type);
const char *kmstest_connector_status_str(int status);
const char *kmstest_connector_type_str(int type);
const char *kmstest_scaling_filter_str(int filter);
const char *kmstest_dsc_output_format_str(int output_format);

void kmstest_dump_mode(drmModeModeInfo *mode);
#define HDISPLAY_6K_PER_PIPE 6144
#define HDISPLAY_5K_PER_PIPE 5120

int kmstest_get_pipe_from_crtc_id(int fd, int crtc_id);
void kmstest_set_vt_graphics_mode(void);
void kmstest_restore_vt_mode(void);
void kmstest_set_vt_text_mode(void);

enum igt_atomic_crtc_properties {
       IGT_CRTC_CTM = 0,
       IGT_CRTC_GAMMA_LUT,
       IGT_CRTC_GAMMA_LUT_SIZE,
       IGT_CRTC_DEGAMMA_LUT,
       IGT_CRTC_DEGAMMA_LUT_SIZE,
       IGT_CRTC_MODE_ID,
       IGT_CRTC_ACTIVE,
       IGT_CRTC_OUT_FENCE_PTR,
       IGT_CRTC_VRR_ENABLED,
       IGT_CRTC_SCALING_FILTER,
       IGT_NUM_CRTC_PROPS
};

/**
 * igt_crtc_prop_names
 *
 * igt_crtc_prop_names contains a list of crtc property names,
 * as indexed by the igt_atomic_crtc_properties enum.
 */
extern const char * const igt_crtc_prop_names[];

enum igt_atomic_connector_properties {
       IGT_CONNECTOR_SCALING_MODE = 0,
       IGT_CONNECTOR_CRTC_ID,
       IGT_CONNECTOR_DPMS,
       IGT_CONNECTOR_BROADCAST_RGB,
       IGT_CONNECTOR_CONTENT_PROTECTION,
       IGT_CONNECTOR_VRR_CAPABLE,
       IGT_CONNECTOR_HDCP_CONTENT_TYPE,
       IGT_CONNECTOR_LINK_STATUS,
       IGT_CONNECTOR_MAX_BPC,
       IGT_CONNECTOR_HDR_OUTPUT_METADATA,
       IGT_CONNECTOR_WRITEBACK_PIXEL_FORMATS,
       IGT_CONNECTOR_WRITEBACK_FB_ID,
       IGT_CONNECTOR_WRITEBACK_OUT_FENCE_PTR,
       IGT_CONNECTOR_DITHERING_MODE,
       IGT_NUM_CONNECTOR_PROPS
};

/**
 * igt_connector_prop_names
 *
 * igt_connector_prop_names contains a list of crtc property names,
 * as indexed by the igt_atomic_connector_properties enum.
 */
extern const char * const igt_connector_prop_names[];

struct kmstest_connector_config {
	drmModeCrtc *crtc;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
	drmModeModeInfo default_mode;

	int pipe;
	unsigned valid_crtc_idx_mask;
	char *connector_path;
};

struct kmstest_plane {
	int id;
	int index;
	int type;
	int pos_x;
	int pos_y;
	int width;
	int height;
};

struct kmstest_crtc {
	int id;
	int pipe;
	bool active;
	int width;
	int height;
	int n_planes;
	struct kmstest_plane *planes;
};

/**
 * kmstest_force_connector_state:
 * @FORCE_CONNECTOR_UNSPECIFIED: Unspecified
 * @FORCE_CONNECTOR_ON: On
 * @FORCE_CONNECTOR_DIGITAL: Digital
 * @FORCE_CONNECTOR_OFF: Off
 */
enum kmstest_force_connector_state {
	FORCE_CONNECTOR_UNSPECIFIED,
	FORCE_CONNECTOR_ON,
	FORCE_CONNECTOR_DIGITAL,
	FORCE_CONNECTOR_OFF
};

/**
 * intel_broadcast_rgb_mode:
 * @BROADCAST_RGB_AUTO: Choose the color range to use automatically
 * @BROADCAST_RGB_FULL: Force the connector to use full color range
 * @BROADCAST_RGB_16_235: Force the connector to use a limited 16:235 color
 * range
 */
enum intel_broadcast_rgb_mode {
	BROADCAST_RGB_AUTO = 0,
	BROADCAST_RGB_FULL,
	BROADCAST_RGB_16_235
};

struct edid;

/**
 * joined_pipes:
 * @JOINED_PIPES_DEFAULT: Default setting with no force joiner
 * @JOINED_PIPES_NONE: Force to exactly one pipe
 * @JOINED_PIPES_BIG_JOINER: Join two pipes big joiner
 * @JOINED_PIPES_ULTRA_JOINER: Join four pipes for ultra joiner
 */
enum joined_pipes {
	JOINED_PIPES_DEFAULT,
	JOINED_PIPES_NONE,
	JOINED_PIPES_BIG_JOINER,
	JOINED_PIPES_ULTRA_JOINER = 4
};

uint64_t igt_kms_frame_time_from_vrefresh(uint32_t vrefresh);

bool kmstest_force_connector(int fd, drmModeConnector *connector,
			     enum kmstest_force_connector_state state);
bool kmstest_force_connector_joiner(int drm_fd, drmModeConnector *connector, int joined_pipes);
void kmstest_force_edid(int drm_fd, drmModeConnector *connector,
			const struct edid *edid);

bool kmstest_get_connector_default_mode(int drm_fd, drmModeConnector *connector,
					drmModeModeInfo *mode);
bool kmstest_get_connector_config(int drm_fd, uint32_t connector_id,
				  unsigned long crtc_idx_mask,
				  struct kmstest_connector_config *config);
drmModePropertyBlobPtr kmstest_get_path_blob(int drm_fd, uint32_t connector_id);
bool kmstest_probe_connector_config(int drm_fd, uint32_t connector_id,
				    unsigned long crtc_idx_mask,
				    struct kmstest_connector_config *config);
void kmstest_free_connector_config(struct kmstest_connector_config *config);

void kmstest_set_connector_dpms(int fd, drmModeConnector *connector, int mode);
bool kmstest_get_property(int drm_fd, uint32_t object_id, uint32_t object_type,
			  const char *name, uint32_t *prop_id, uint64_t *value,
			  drmModePropertyPtr *prop);
void kmstest_unset_all_crtcs(int drm_fd, drmModeResPtr resources);
int kmstest_get_crtc_idx(drmModeRes *res, uint32_t crtc_id);
uint32_t kmstest_find_crtc_for_connector(int fd, drmModeRes *res,
					 drmModeConnector *connector,
					 uint32_t crtc_blacklist_idx_mask);

uint32_t kmstest_dumb_create(int fd, int width, int height, int bpp,
			     unsigned *stride, uint64_t *size);

void *kmstest_dumb_map_buffer(int fd, uint32_t handle, uint64_t size,
			      unsigned prot);
void kmstest_dumb_destroy(int fd, uint32_t handle);
void kmstest_wait_for_pageflip(int fd);
void kmstest_wait_for_pageflip_timeout(int fd, uint64_t timeout_us);
unsigned int kmstest_get_vblank(int fd, int pipe, unsigned int flags);

bool kms_has_vblank(int fd);

/*
 * A small modeset API
 */

/* High-level kms api with igt_ prefix */

/**
 * igt_commit_style:
 * @COMMIT_LEGACY: Changes will be committed using the legacy API.
 * @COMMIT_UNIVERSAL: Changes will be committed with the universal plane API, no modesets are allowed.
 * @COMMIT_ATOMIC: Changes will be committed using the atomic API.
 */
enum igt_commit_style {
	COMMIT_LEGACY = 0,
	COMMIT_UNIVERSAL,
	COMMIT_ATOMIC,
};

enum igt_atomic_plane_properties {
       IGT_PLANE_SRC_X = 0,
       IGT_PLANE_SRC_Y,
       IGT_PLANE_SRC_W,
       IGT_PLANE_SRC_H,

       IGT_PLANE_CRTC_X,
       IGT_PLANE_CRTC_Y,
       IGT_PLANE_CRTC_W,
       IGT_PLANE_CRTC_H,

/* Append new properties after IGT_PLANE_COORD_CHANGED_MASK */
#define IGT_PLANE_COORD_CHANGED_MASK 0xff

       IGT_PLANE_FB_ID,
       IGT_PLANE_CRTC_ID,
       IGT_PLANE_IN_FENCE_FD,
       IGT_PLANE_TYPE,
       IGT_PLANE_ROTATION,
       IGT_PLANE_IN_FORMATS,
       IGT_PLANE_COLOR_ENCODING,
       IGT_PLANE_COLOR_RANGE,
       IGT_PLANE_PIXEL_BLEND_MODE,
       IGT_PLANE_ALPHA,
       IGT_PLANE_ZPOS,
       IGT_PLANE_FB_DAMAGE_CLIPS,
       IGT_PLANE_SCALING_FILTER,
       IGT_PLANE_HOTSPOT_X,
       IGT_PLANE_HOTSPOT_Y,
       IGT_PLANE_SIZE_HINTS,
       IGT_PLANE_IN_FORMATS_ASYNC,
       IGT_NUM_PLANE_PROPS,
};

/**
 * igt_plane_prop_names
 *
 * igt_plane_prop_names contains a list of crtc property names,
 * as indexed by the igt_atomic_plane_properties enum.
 */
extern const char * const igt_plane_prop_names[];

typedef struct igt_display igt_display_t;
typedef struct igt_pipe igt_pipe_t;
typedef uint32_t igt_fixed_t;			/* 16.16 fixed point */

typedef enum {
	/* this maps to the kernel API */
	IGT_ROTATION_0   = 1 << 0,
	IGT_ROTATION_90  = 1 << 1,
	IGT_ROTATION_180 = 1 << 2,
	IGT_ROTATION_270 = 1 << 3,
	IGT_REFLECT_X    = 1 << 4,
	IGT_REFLECT_Y    = 1 << 5,
} igt_rotation_t;

#define IGT_ROTATION_MASK \
	(IGT_ROTATION_0 | IGT_ROTATION_90 | IGT_ROTATION_180 | IGT_ROTATION_270)

/**
 * igt_rotation_90_or_270:
 * @rotation: Target rotation
 *
 * Returns: True if the given @rotation contains 90 or 270 degrees,
 * else False.
 */
static inline bool igt_rotation_90_or_270(igt_rotation_t rotation)
{
	return rotation & (IGT_ROTATION_90 | IGT_ROTATION_270);
}

typedef struct igt_plane {
	/*< private >*/
	igt_pipe_t *pipe;
	struct igt_plane *ref;
	int index;
	/* capabilities */
	int type;

	/*
	 * drm_plane can be NULL for primary and cursor planes (when not
	 * using the atomic modeset API)
	 */
	drmModePlane *drm_plane;

	/* gem handle for fb */
	uint32_t gem_handle;

	struct {
		uint64_t values[IGT_NUM_COLOR_ENCODINGS];
	} color_encoding;
	struct {
		uint64_t values[IGT_NUM_COLOR_RANGES];
	} color_range;

	igt_rotation_t rotations;

	uint64_t changed;
	uint32_t props[IGT_NUM_PLANE_PROPS];
	uint64_t values[IGT_NUM_PLANE_PROPS];

	uint64_t *modifiers;
	uint32_t *formats;
	int format_mod_count;

	uint64_t *async_modifiers;
	uint32_t *async_formats;
	int async_format_mod_count;
} igt_plane_t;

/*
 * This struct represents a hardware pipe
 *
 * DRM_IOCTL_WAIT_VBLANK notion of pipe is confusing and we are using
 * crtc_offset instead (refer people to #igt_wait_for_vblank_count)
 */
struct igt_pipe {
	igt_display_t *display;
	/* ID of a hardware pipe */
	enum pipe pipe;
	/* pipe is enabled or not */
	bool enabled;

	int n_planes;
	int num_primary_planes;
	int plane_cursor;
	int plane_primary;
	igt_plane_t *planes;

	uint64_t changed;
	uint32_t props[IGT_NUM_CRTC_PROPS];
	uint64_t values[IGT_NUM_CRTC_PROPS];

	/* ID of KMS CRTC object */
	uint32_t crtc_id;
	/* offset of a pipe in drmModeRes.crtcs */
	uint32_t crtc_offset;

	int32_t out_fence_fd;
};

typedef struct {
	/*< private >*/
	igt_display_t *display;
	uint32_t id;					/* KMS id */
	struct kmstest_connector_config config;
	char *name;
	bool force_reprobe;
	enum pipe pending_pipe;
	bool use_override_mode;
	drmModeModeInfo override_mode;

	int32_t writeback_out_fence_fd;

	/* bitmask of changed properties */
	uint64_t changed;

	uint32_t props[IGT_NUM_CONNECTOR_PROPS];
	uint64_t values[IGT_NUM_CONNECTOR_PROPS];
} igt_output_t;

struct igt_display {
	int drm_fd;
	int log_shift;
	int n_pipes;
	int n_planes;
	int n_outputs;
	igt_output_t *outputs;
	igt_plane_t *planes;
	igt_pipe_t *pipes;
	bool has_cursor_plane;
	bool is_atomic;
	bool has_virt_cursor_plane;
	bool first_commit;

	uint64_t *modifiers;
	uint32_t *formats;
	int format_mod_count;
};

typedef struct {
	int tile_group_id;
	bool tile_is_single_monitor;
	uint8_t num_h_tile, num_v_tile;
	uint8_t tile_h_loc, tile_v_loc;
	uint16_t tile_h_size, tile_v_size;
} igt_tile_info_t;

/* Backlight context*/
typedef struct {
	int max;
	int old;
	igt_output_t *output;
	char path[PATH_MAX];
	char backlight_dir_path[PATH_MAX];
} igt_backlight_context_t;

void igt_display_reset_outputs(igt_display_t *display);
void igt_display_require(igt_display_t *display, int drm_fd);
void igt_display_fini(igt_display_t *display);
void igt_display_reset(igt_display_t *display);
int  igt_display_commit2(igt_display_t *display, enum igt_commit_style s);
int  igt_display_commit(igt_display_t *display);
int  igt_display_try_commit_atomic(igt_display_t *display, uint32_t flags, void *user_data);
void igt_display_commit_atomic(igt_display_t *display, uint32_t flags, void *user_data);
int  igt_display_try_commit2(igt_display_t *display, enum igt_commit_style s);
int  igt_display_drop_events(igt_display_t *display);
int  igt_display_get_n_pipes(igt_display_t *display);
void igt_display_require_output(igt_display_t *display);
void igt_display_require_output_on_pipe(igt_display_t *display, enum pipe pipe);

const char *igt_output_name(igt_output_t *output);
drmModeModeInfo *igt_output_get_mode(igt_output_t *output);
drmModeModeInfo *igt_output_get_highres_mode(igt_output_t *output);
drmModeModeInfo *igt_output_get_lowres_mode(igt_output_t *output);
void igt_output_override_mode(igt_output_t *output, const drmModeModeInfo *mode);
int igt_output_preferred_vrefresh(igt_output_t *output);
void igt_output_set_pipe(igt_output_t *output, enum pipe pipe);
igt_plane_t *igt_output_get_plane(igt_output_t *output, int plane_idx);
igt_plane_t *igt_output_get_plane_type(igt_output_t *output, int plane_type);
int igt_output_count_plane_type(igt_output_t *output, int plane_type);
igt_plane_t *igt_output_get_plane_type_index(igt_output_t *output,
					     int plane_type, int index);
igt_output_t *igt_output_from_connector(igt_display_t *display,
    drmModeConnector *connector);
void igt_output_refresh(igt_output_t *output);
drmModeModeInfo *igt_std_1024_mode_get(int vrefresh);
void igt_output_set_writeback_fb(igt_output_t *output, struct igt_fb *fb);
void igt_modeset_disable_all_outputs(igt_display_t *display);

igt_plane_t *igt_pipe_get_plane_type(igt_pipe_t *pipe, int plane_type);
int igt_pipe_count_plane_type(igt_pipe_t *pipe, int plane_type);
igt_plane_t *igt_pipe_get_plane_type_index(igt_pipe_t *pipe, int plane_type,
					   int index);
bool output_is_internal_panel(igt_output_t *output);
igt_output_t *igt_get_single_output_for_pipe(igt_display_t *display, enum pipe pipe);

void igt_pipe_request_out_fence(igt_pipe_t *pipe);

void igt_plane_set_fb(igt_plane_t *plane, struct igt_fb *fb);
void igt_plane_set_fence_fd(igt_plane_t *plane, int fence_fd);
void igt_plane_set_pipe(igt_plane_t *plane, igt_pipe_t *pipe);
void igt_plane_set_position(igt_plane_t *plane, int x, int y);
void igt_plane_set_size(igt_plane_t *plane, int w, int h);
void igt_plane_set_rotation(igt_plane_t *plane, igt_rotation_t rotation);
void igt_fb_set_position(struct igt_fb *fb, igt_plane_t *plane,
	uint32_t x, uint32_t y);
void igt_fb_set_size(struct igt_fb *fb, igt_plane_t *plane,
	uint32_t w, uint32_t h);

/**
 * igt_plane_has_rotation:
 * @plane: Plane pointer for which rotation is to be queried
 * @rotation: Plane rotation value (0, 90, 180, 270)
 *
 * Check whether @plane potentially supports the given @rotation.
 * Note that @rotation may still rejected later due to other
 * constraints (eg. incompatible pixel format or modifier).
 */
static inline bool igt_plane_has_rotation(igt_plane_t *plane, igt_rotation_t rotation)
{
	return (plane->rotations & rotation) == rotation;
}
const char *igt_plane_rotation_name(igt_rotation_t rotation);

void igt_wait_for_vblank(int drm_fd, int crtc_offset);
void igt_wait_for_vblank_count(int drm_fd, int crtc_offset, int count);

/**
 * igt_output_is_connected:
 * @output: #igt_output_t to check.
 *
 * Returns: True if given @output's connection status is CONNECTED,
 * else False.
 */
static inline bool igt_output_is_connected(igt_output_t *output)
{
	/* Something went wrong during probe? */
	if (!output->config.connector ||
	    !output->config.connector->count_modes)
		return false;

	if (output->config.connector->connection == DRM_MODE_CONNECTED)
		return true;

	return false;
}

/**
 * igt_pipe_connector_valid:
 * @pipe: pipe to check.
 * @output: #igt_output_t to check.
 *
 * Checks whether the given pipe and output can be used together.
 */
#define igt_pipe_connector_valid(pipe, output) \
	(igt_output_is_connected((output)) && \
	       (output->config.valid_crtc_idx_mask & (1 << (pipe))))

#define for_each_if(condition) if (!(condition)) {} else

/**
 * for_each_output:
 * @display: a pointer to an #igt_display_t structure
 * @output: The output to iterate.
 *
 * This for loop iterates over all outputs.
 */
#define for_each_output(display, output)		\
	for (int i__ = 0;  assert(igt_can_fail()), i__ < (display)->n_outputs; i__++)	\
		for_each_if (((output) = (&(display)->outputs[i__])))

/**
 * for_each_connected_output:
 * @display: a pointer to an #igt_display_t structure
 * @output: The output to iterate.
 *
 * This for loop iterates over all connected outputs.
 */
#define for_each_connected_output(display, output)		\
	for_each_output((display), (output))	\
		for_each_if ((igt_output_is_connected((output))))

/**
 * for_each_disconnected_output:
 * @display: a pointer to an #igt_display_t structure
 * @output: The output to iterate.
 *
 * This for loop iterates over all disconnected outputs.
 */
#define for_each_disconnected_output(display, output)		\
	for_each_output((display), (output))	\
		for_each_if ((!(igt_output_is_connected((output)))))

/**
 * for_each_pipe_static:
 * @pipe: The pipe to iterate.
 *
 * This for loop iterates over all pipes supported by IGT libraries.
 *
 * This should be used to enumerate per-pipe subtests since it has no runtime
 * depencies.
 */
#define for_each_pipe_static(pipe) \
	for (pipe = 0; pipe < IGT_MAX_PIPES; pipe++)

/**
 * for_each_pipe:
 * @display: a pointer to an #igt_display_t structure
 * @pipe: The pipe to iterate.
 *
 * This for loop iterates over all pipes.
 *
 * Note that this cannot be used to enumerate per-pipe subtest names since it
 * depends upon runtime probing of the actual kms driver that is being tested.
 * Use #for_each_pipe_static instead.
 */
#define for_each_pipe(display, pipe) \
	for_each_pipe_static(pipe) \
		for_each_if((display)->pipes[(pipe)].enabled)

/**
 * for_each_pipe_with_valid_output:
 * @display: a pointer to an #igt_display_t structure
 * @pipe: The pipe for which this @pipe / @output combination is valid.
 * @output: The output for which this @pipe / @output combination is valid.
 *
 * This for loop is called over all connected outputs. This function
 * will try every combination of @pipe and @output.
 *
 * If you only need to test a single output for each pipe, use
 * for_each_pipe_with_single_output(), if you only need an
 * output for a single pipe, use igt_get_single_output_for_pipe().
 */
#define for_each_pipe_with_valid_output(display, pipe, output) \
	for (int con__ = (pipe) = 0; \
	     assert(igt_can_fail()), (pipe) < igt_display_get_n_pipes((display)) && con__ < (display)->n_outputs; \
	     con__ = (con__ + 1 < (display)->n_outputs) ? con__ + 1 : (pipe = pipe + 1, 0)) \
		 for_each_if((display)->pipes[pipe].enabled) \
			for_each_if ((((output) = &(display)->outputs[con__]), \
						igt_pipe_connector_valid((pipe), (output))))

igt_output_t **__igt_pipe_populate_outputs(igt_display_t *display,
					   igt_output_t **chosen_outputs);

/**
 * for_each_pipe_with_single_output:
 * @display: a pointer to an #igt_display_t structure
 * @pipe: The pipe for which this @pipe / @output combination is valid.
 * @output: The output for which this @pipe / @output combination is valid.
 *
 * This loop is called over all pipes, and will try to find a compatible output
 * for each pipe. Unlike for_each_pipe_with_valid_output(), this function will
 * be called at most once for each pipe.
 */
#define for_each_pipe_with_single_output(display, pipe, output) \
	for (igt_output_t *__outputs[(display)->n_pipes], \
	     **__output = __igt_pipe_populate_outputs((display), __outputs); \
		 __output < &__outputs[(display)->n_pipes]; __output++) \
		for_each_if (*__output && \
			     ((pipe) = (__output - __outputs), (output) = *__output, 1))

/**
 * for_each_valid_output_on_pipe:
 * @display: a pointer to an #igt_display_t structure
 * @pipe: Pipe to enumerate valid outputs over
 * @output: The enumerated output.
 *
 * This for loop is called over all connected @output that can be used
 * on this @pipe . If there are no valid outputs for this pipe, nothing
 * happens.
 */
#define for_each_valid_output_on_pipe(display, pipe, output) \
	for_each_connected_output((display), (output)) \
		for_each_if (igt_pipe_connector_valid((pipe), (output)))

/**
 * for_each_plane_on_pipe:
 * @display: a pointer to an #igt_display_t structure
 * @pipe: Pipe to enumerate valid outputs over
 * @plane: The enumerated plane.
 *
 * This for loop iterates over all planes associated to the given @pipe.
 * If there are no valid planes for this pipe, nothing happens.
 */
#define for_each_plane_on_pipe(display, pipe, plane)			\
	for (int j__ = 0; assert(igt_can_fail()), (plane) = &(display)->pipes[(pipe)].planes[j__], \
		     j__ < (display)->pipes[(pipe)].n_planes; j__++)

/**
 * for_each_connector_mode:
 * @output: Output to enumerate available modes.
 *
 * This for loop iterates over all modes associated to the given @output.
 * If there are no mode available for this output, nothing happens.
 */
#define for_each_connector_mode(output)		\
	for (int j__ = 0;  j__ < output->config.connector->count_modes; j__++)

#define IGT_FIXED(i,f)	((i) << 16 | (f))

/**
 * igt_plane_has_prop:
 * @plane: Plane to check.
 * @prop: Property to check.
 *
 * Check whether plane supports a given property.
 *
 * Returns: True if the property is supported, otherwise false.
 */
static inline bool
igt_plane_has_prop(igt_plane_t *plane, enum igt_atomic_plane_properties prop)
{
	return plane->props[prop];
}

uint64_t igt_plane_get_prop(igt_plane_t *plane, enum igt_atomic_plane_properties prop);

/**
 * igt_plane_is_prop_changed:
 * @plane: Plane to check.
 * @prop: Property to check.
 *
 * Check whether a given @prop changed for the @plane.
 */
#define igt_plane_is_prop_changed(plane, prop) \
	(!!((plane)->changed & (1 << (prop))))

/**
 * igt_plane_set_prop_changed:
 * @plane: Plane to check.
 * @prop: Property to check.
 *
 * Sets the given @prop for the @plane.
 */
#define igt_plane_set_prop_changed(plane, prop) \
	(plane)->changed |= 1 << (prop)

/**
 * igt_plane_clear_prop_changed:
 * @plane: Plane to check.
 * @prop: Property to check.
 *
 * Clears the given @prop for the @plane.
 */
#define igt_plane_clear_prop_changed(plane, prop) \
	(plane)->changed &= ~(1 << (prop))

/**
 * igt_plane_set_prop_value:
 * @plane: Plane to check.
 * @prop: Property to check.
 * @value: Value to set.
 *
 * Sets the given @prop with the @value for the @plane.
 */
#define igt_plane_set_prop_value(plane, prop, value) \
	do { \
		plane->values[prop] = value; \
		igt_plane_set_prop_changed(plane, prop); \
	} while (0)

extern bool igt_plane_try_prop_enum(igt_plane_t *plane,
				    enum igt_atomic_plane_properties prop,
				    const char *val);

extern void igt_plane_set_prop_enum(igt_plane_t *plane,
				    enum igt_atomic_plane_properties prop,
				    const char *val);

extern void igt_plane_replace_prop_blob(igt_plane_t *plane,
					enum igt_atomic_plane_properties prop,
					const void *ptr, size_t length);

extern bool igt_plane_check_prop_is_mutable(igt_plane_t *plane,
					    enum igt_atomic_plane_properties igt_prop);
/**
 * igt_output_has_prop:
 * @output: Output to check.
 * @prop: Property to check.
 *
 * Check whether output supports a given property.
 *
 * Returns: True if the property is supported, otherwise false.
 */
static inline bool
igt_output_has_prop(igt_output_t *output, enum igt_atomic_connector_properties prop)
{
	return output->props[prop];
}

uint64_t igt_output_get_prop(igt_output_t *output, enum igt_atomic_connector_properties prop);

/**
 * igt_output_is_prop_changed:
 * @output: Output to check.
 * @prop: Property to check.
 *
 * Check whether a given @prop changed for the @Output.
 */
#define igt_output_is_prop_changed(output, prop) \
	(!!((output)->changed & (1 << (prop))))

/**
 * igt_output_set_prop_changed:
 * @output: Output to check.
 * @prop: Property to check.
 *
 * Sets the given @prop for the @output.
 */
#define igt_output_set_prop_changed(output, prop) \
	(output)->changed |= 1 << (prop)

/**
 * igt_output_clear_prop_changed:
 * @output: Output to check.
 * @prop: Property to check.
 *
 * Clears the given @prop for the @output.
 */
#define igt_output_clear_prop_changed(output, prop) \
	(output)->changed &= ~(1 << (prop))

/**
 * igt_output_set_prop_value:
 * @output: Output to check.
 * @prop: Property to check.
 * @value: Value to set.
 *
 * Sets the given @prop with the @value for the @output.
 */
#define igt_output_set_prop_value(output, prop, value) \
	do { \
		(output)->values[prop] = (value); \
		igt_output_set_prop_changed(output, prop); \
	} while (0)

extern bool igt_output_try_prop_enum(igt_output_t *output,
				     enum igt_atomic_connector_properties prop,
				     const char *val);

extern void igt_output_set_prop_enum(igt_output_t *output,
				     enum igt_atomic_connector_properties prop,
				     const char *val);

extern void igt_output_replace_prop_blob(igt_output_t *output,
					 enum igt_atomic_connector_properties prop,
					 const void *ptr, size_t length);
/**
 * igt_pipe_obj_has_prop:
 * @pipe: Pipe to check.
 * @prop: Property to check.
 *
 * Check whether pipe supports a given property.
 *
 * Returns: True if the property is supported, otherwise false.
 */
static inline bool
igt_pipe_obj_has_prop(igt_pipe_t *pipe, enum igt_atomic_crtc_properties prop)
{
	return pipe->props[prop];
}

uint64_t igt_pipe_obj_get_prop(igt_pipe_t *pipe, enum igt_atomic_crtc_properties prop);

/**
 * igt_pipe_get_prop:
 * @display: Pointer to display.
 * @pipe: Target pipe.
 * @prop: Property to return.
 *
 * Return current value on a pipe for a given property.
 *
 * Returns: The value the property is set to, if this
 * is a blob, the blob id is returned. This can be passed
 * to drmModeGetPropertyBlob() to get the contents of the blob.
 */
static inline uint64_t
igt_pipe_get_prop(igt_display_t *display, enum pipe pipe,
		  enum igt_atomic_crtc_properties prop)
{
	return igt_pipe_obj_get_prop(&display->pipes[pipe], prop);
}

/**
 * igt_pipe_has_prop:
 * @display: Pointer to display.
 * @pipe: Pipe to check.
 * @prop: Property to check.
 *
 * Check whether pipe supports a given property.
 *
 * Returns: True if the property is supported, otherwise false.
 */
static inline bool
igt_pipe_has_prop(igt_display_t *display, enum pipe pipe,
		  enum igt_atomic_crtc_properties prop)
{
	return display->pipes[pipe].props[prop];
}

/**
 * igt_pipe_obj_is_prop_changed:
 * @pipe_obj: Pipe object to check.
 * @prop: Property to check.
 *
 * Check whether a given @prop changed for the @pipe_obj.
 */
#define igt_pipe_obj_is_prop_changed(pipe_obj, prop) \
	(!!((pipe_obj)->changed & (1 << (prop))))

/**
 * igt_pipe_is_prop_changed:
 * @pipe: Pipe object to check.
 * @prop: Property to check.
 *
 * Check whether a given @prop changed for the @pipe.
 */
#define igt_pipe_is_prop_changed(display, pipe, prop) \
	igt_pipe_obj_is_prop_changed(&(display)->pipes[(pipe)], prop)

/**
 * igt_pipe_obj_set_prop_changed:
 * @pipe_obj: Pipe object to check.
 * @prop: Property to check.
 *
 * Sets the given @prop for the @pipe_obj.
 */
#define igt_pipe_obj_set_prop_changed(pipe_obj, prop) \
	(pipe_obj)->changed |= 1 << (prop)

/**
 * igt_pipe_set_prop_changed:
 * @pipe: Pipe object to check.
 * @prop: Property to check.
 *
 * Sets the given @prop for the @pipe.
 */
#define igt_pipe_set_prop_changed(display, pipe, prop) \
	igt_pipe_obj_set_prop_changed(&(display)->pipes[(pipe)], prop)

/**
 * igt_pipe_obj_clear_prop_changed:
 * @pipe_obj: Pipe object to check.
 * @prop: Property to check.
 *
 * Clears the given @prop for the @pipe_obj.
 */
#define igt_pipe_obj_clear_prop_changed(pipe_obj, prop) \
	(pipe_obj)->changed &= ~(1 << (prop))

/**
 * igt_pipe_clear_prop_changed:
 * @pipe: Pipe object to check.
 * @prop: Property to check.
 *
 * Clears the given @prop for the @pipe.
 */
#define igt_pipe_clear_prop_changed(display, pipe, prop) \
	igt_pipe_obj_clear_prop_changed(&(display)->pipes[(pipe)], prop)

/**
 * igt_pipe_obj_set_prop_value:
 * @pipe_obj: Pipe object to check.
 * @prop: Property to check.
 * @value: Value to set.
 *
 * Sets the given @prop with the @value for the @pipe_obj.
 */
#define igt_pipe_obj_set_prop_value(pipe_obj, prop, value) \
	do { \
		(pipe_obj)->values[prop] = (value); \
		igt_pipe_obj_set_prop_changed(pipe_obj, prop); \
	} while (0)

/**
 * igt_pipe_set_prop_value:
 * @pipe: Pipe to check.
 * @prop: Property to check.
 * @value: Value to set.
 *
 * Sets the given @prop with the @value for the @pipe.
 */
#define igt_pipe_set_prop_value(display, pipe, prop, value) \
	igt_pipe_obj_set_prop_value(&(display)->pipes[(pipe)], prop, value)

extern bool igt_pipe_obj_try_prop_enum(igt_pipe_t *pipe,
				       enum igt_atomic_crtc_properties prop,
				       const char *val);

extern void igt_pipe_obj_set_prop_enum(igt_pipe_t *pipe,
				       enum igt_atomic_crtc_properties prop,
				       const char *val);

/**
 * igt_pipe_try_prop_enum:
 * @pipe: Target pipe.
 * @prop: Property to check.
 * @val: Value to set.
 *
 * Returns: False if the given @pipe doesn't have the enum @prop or
 * failed to set the enum property @val else True.
 */
#define igt_pipe_try_prop_enum(display, pipe, prop, val) \
	igt_pipe_obj_try_prop_enum(&(display)->pipes[(pipe)], prop, val)

/**
 * igt_pipe_set_prop_enum:
 * @pipe: Target pipe.
 * @prop: Property to check.
 * @val: Value to set.
 *
 * This function tries to set given enum property @prop value @val to
 * the given @pipe, and terminate the execution if its failed.
 */
#define igt_pipe_set_prop_enum(display, pipe, prop, val) \
	igt_pipe_obj_set_prop_enum(&(display)->pipes[(pipe)], prop, val)

extern void igt_pipe_obj_replace_prop_blob(igt_pipe_t *pipe,
					   enum igt_atomic_crtc_properties prop,
					   const void *ptr, size_t length);

/**
 * igt_pipe_replace_prop_blob:
 * @pipe: pipe to set property on.
 * @prop: property for which the blob will be replaced.
 * @ptr: Pointer to contents for the property.
 * @length: Length of contents.
 *
 * This function will destroy the old property blob for the given property,
 * and will create a new property blob with the values passed to this function.
 *
 * The new property blob will be committed when you call igt_display_commit(),
 * igt_display_commit2() or igt_display_commit_atomic().
 *
 * Please use igt_output_override_mode() if you want to set #IGT_CRTC_MODE_ID,
 * it works better with legacy commit.
 */
#define igt_pipe_replace_prop_blob(display, pipe, prop, ptr, length) \
	igt_pipe_obj_replace_prop_blob(&(display)->pipes[(pipe)], prop, ptr, length)

void igt_pipe_refresh(igt_display_t *display, enum pipe pipe, bool force);

void igt_enable_connectors(int drm_fd);
void igt_reset_connectors(void);

uint32_t kmstest_get_vbl_flag(int crtc_offset);

const struct edid *igt_kms_get_base_edid(void);
const struct edid *igt_kms_get_full_edid(void);
const struct edid *igt_kms_get_base_tile_edid(void);
const struct edid *igt_kms_get_alt_edid(void);
const struct edid *igt_kms_get_hdmi_audio_edid(void);
const struct edid *igt_kms_get_dp_audio_edid(void);
const struct edid *igt_kms_get_4k_edid(void);
const struct edid *igt_kms_get_3d_edid(void);
const struct edid *igt_kms_get_aspect_ratio_edid(void);
struct edid **igt_kms_get_tiled_edid(uint8_t htile, uint8_t vtile);
const struct edid *igt_kms_get_custom_edid(enum igt_custom_edid_type edid);
struct udev_monitor *igt_watch_uevents(void);
bool igt_hotplug_detected(struct udev_monitor *mon,
			  int timeout_secs);
bool igt_lease_change_detected(struct udev_monitor *mon,
			       int timeout_secs);
bool igt_connector_event_detected(struct udev_monitor *mon, uint32_t conn_id,
				  uint32_t prop_id, int timeout_msecs);
void igt_flush_uevents(struct udev_monitor *mon);
void igt_cleanup_uevents(struct udev_monitor *mon);

bool igt_display_has_format_mod(igt_display_t *display, uint32_t format, uint64_t modifier);
bool igt_plane_has_format_mod(igt_plane_t *plane, uint32_t format, uint64_t modifier);

/**
 * igt_vblank_after_eq:
 * @a: First vblank sequence number.
 * @b: Second vblank sequence number.
 *
 * Compare vblank sequence numbers,
 * handling wraparound correctly.
 *
 * Returns: @a >= @b
 */
static inline bool igt_vblank_after_eq(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b) >= 0;
}

/**
 * igt_vblank_before_eq:
 * @a: First vblank sequence number.
 * @b: Second vblank sequence number.
 *
 * Compare vblank sequence numbers,
 * handling wraparound correctly.
 *
 * Returns: @a <= @b
 */
static inline bool igt_vblank_before_eq(uint32_t a, uint32_t b)
{
	return igt_vblank_after_eq(b, a);
}

/**
 * igt_vblank_after:
 * @a: First vblank sequence number.
 * @b: Second vblank sequence number.
 *
 * Compare vblank sequence numbers,
 * handling wraparound correctly.
 *
 * Returns: @a > @b
 */
static inline bool igt_vblank_after(uint32_t a, uint32_t b)
{
	return (int32_t)(b - a) < 0;
}

/**
 * igt_vblank_before:
 * @a: First vblank sequence number.
 * @b: Second vblank sequence number.
 *
 * Compare vblank sequence numbers,
 * handling wraparound correctly.
 *
 * Returns: @a < @b
 */
static inline bool igt_vblank_before(uint32_t a, uint32_t b)
{
	return igt_vblank_after(b, a);
}

void igt_parse_connector_tile_blob(drmModePropertyBlobPtr blob,
		igt_tile_info_t *tile);

int igt_connector_sysfs_open(int drm_fd,
			     drmModeConnector *connector);
uint32_t igt_reduce_format(uint32_t format);


/*
 * igt_require_pipe:
 * @display: pointer to igt_display_t
 * @pipe: pipe which need to check
 *
 * Skip a (sub-)test if the pipe not enabled.
 *
 * Should be used everywhere where a test checks pipe and skip
 * test when pipe is not enabled.
 */
void igt_require_pipe(igt_display_t *display,
		enum pipe pipe);

void igt_dump_connectors_fd(int drmfd);
void igt_dump_crtcs_fd(int drmfd);
bool igt_override_all_active_output_modes_to_fit_bw(igt_display_t *display);
bool igt_fit_modes_in_bw(igt_display_t *display);
bool igt_get_i915_edp_lobf_status(int drmfd, char *connector_name);
unsigned int igt_get_output_max_bpc(int drmfd, char *connector_name);
unsigned int igt_get_pipe_current_bpc(int drmfd, enum pipe pipe);
void igt_assert_output_bpc_equal(int drmfd, enum pipe pipe,
				char *output_name, unsigned int bpc);
bool igt_check_output_bpc_equal(int drmfd, enum pipe pipe,
				char *output_name, unsigned int bpc);

int sort_drm_modes_by_clk_dsc(const void *a, const void *b);
int sort_drm_modes_by_clk_asc(const void *a, const void *b);
int sort_drm_modes_by_res_dsc(const void *a, const void *b);
int sort_drm_modes_by_res_asc(const void *a, const void *b);
void igt_sort_connector_modes(drmModeConnector *connector,
		int (*comparator)(const void *, const void*));

bool igt_max_bpc_constraint(igt_display_t *display, enum pipe pipe,
		igt_output_t *output, int bpc);
int igt_get_max_dotclock(int fd);
int igt_get_max_cdclk(int fd);
int igt_get_current_cdclk(int fd);
bool igt_bigjoiner_possible(int drm_fd, drmModeModeInfo *mode, int max_dotclock);
bool bigjoiner_mode_found(int drm_fd, drmModeConnector *connector,
			  int max_dotclock, drmModeModeInfo *mode);
bool max_non_joiner_mode_found(int drm_fd, drmModeConnector *connector,
			       int max_dotclock, drmModeModeInfo *mode);
bool igt_is_joiner_enabled_for_pipe(int drmfd, enum pipe pipe);
bool igt_ultrajoiner_possible(int drmfd, drmModeModeInfo *mode, int max_dotclock);
bool ultrajoiner_mode_found(int drm_fd, drmModeConnector *connector,
			  int max_dotclock, drmModeModeInfo *mode);
bool igt_has_force_joiner_debugfs(int drmfd, char *conn_name);
bool is_joiner_mode(int drm_fd, igt_output_t *output);
bool igt_check_force_joiner_status(int drmfd, char *connector_name);
bool igt_check_bigjoiner_support(igt_display_t *display);
bool igt_parse_mode_string(const char *mode_string, drmModeModeInfo *mode);
bool intel_pipe_output_combo_valid(igt_display_t *display);
bool igt_check_output_is_dp_mst(igt_output_t *output);
int igt_get_dp_mst_connector_id(igt_output_t *output);
int get_num_scalers(igt_display_t *display, enum pipe pipe);
int igt_get_current_lane_count(int drm_fd, igt_output_t *output);
int igt_get_current_link_rate(int drm_fd, igt_output_t *output);
int igt_get_max_link_rate(int drm_fd, igt_output_t *output);
int igt_get_max_lane_count(int drm_fd, igt_output_t *output);
void igt_force_link_retrain(int drm_fd, igt_output_t *output, int retrain_count);
void igt_force_lt_failure(int drm_fd, igt_output_t *output, int failure_count);
bool igt_get_dp_link_retrain_disabled(int drm_fd, igt_output_t *output);
bool igt_has_force_link_training_failure_debugfs(int drmfd, igt_output_t *output);
int igt_get_dp_pending_lt_failures(int drm_fd, igt_output_t *output);
int igt_get_dp_pending_retrain(int drm_fd, igt_output_t *output);
void igt_reset_link_params(int drm_fd, igt_output_t *output);
void igt_set_link_params(int drm_fd, igt_output_t *output,
			   char *link_rate, char *lane_count);
int igt_backlight_read(int *result, const char *fname, igt_backlight_context_t *context);
int igt_backlight_write(int value, const char *fname, igt_backlight_context_t *context);

drmModePropertyBlobRes *igt_get_writeback_formats_blob(igt_output_t *output);

#endif /* __IGT_KMS_H__ */
