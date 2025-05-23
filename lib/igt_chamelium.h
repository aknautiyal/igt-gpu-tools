/*
 * Copyright © 2016 Red Hat Inc.
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
 * Authors: Lyude Paul <lyude@redhat.com>
 */

#ifndef IGT_CHAMELIUM_H
#define IGT_CHAMELIUM_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <xf86drmMode.h>

#include "igt_debugfs.h"
#include "igt_kms.h"
#include "igt_list.h"

struct igt_fb;
struct edid;
typedef struct _igt_crc igt_crc_t;

struct chamelium;
struct chamelium_port;
struct chamelium_frame_dump;
struct chamelium_fb_crc_async_data;

/**
 * chamelium_check:
 * @CHAMELIUM_CHECK_ANALOG: Fuzzy checking method for analog interfaces
 * @CHAMELIUM_CHECK_CRC: CRC-based checking method for pixel-perfect interfaces
 *
 * Checking method for comparing between reference and captured frames.
 */
enum chamelium_check {
	CHAMELIUM_CHECK_ANALOG,
	CHAMELIUM_CHECK_CHECKERBOARD,
	CHAMELIUM_CHECK_CRC,
};

struct chamelium_video_params {
	double clock;
	int htotal, hactive, hsync_offset, hsync_width, hsync_polarity;
	int vtotal, vactive, vsync_offset, vsync_width, vsync_polarity;
};

struct chamelium_audio_file {
	char *path;
	int rate; /* Hz */
	int channels;
};

enum chamelium_infoframe_type {
	CHAMELIUM_INFOFRAME_AVI,
	CHAMELIUM_INFOFRAME_AUDIO,
	CHAMELIUM_INFOFRAME_MPEG,
	CHAMELIUM_INFOFRAME_VENDOR,
};

struct chamelium_infoframe {
	int version;
	size_t payload_size;
	uint8_t *payload;
};

/**
 * CHAMELIUM_MAX_PORTS: the maximum number of ports supported by igt_chamelium.
 *
 * On V2: 1 VGA, 1 HDMI and 2 DisplayPort ports.
 * On V3: 2 HDMI and 2 DisplayPort ports.
 */
#define CHAMELIUM_MAX_PORTS 4

/**
 * CHAMELIUM_DEFAULT_EDID: provide this ID to #chamelium_port_set_edid to use
 * the default EDID.
 */
#define CHAMELIUM_DEFAULT_EDID 0

/**
 * CHAMELIUM_MAX_AUDIO_CHANNELS: the maximum number of audio capture channels
 * supported by Chamelium.
 */
#define CHAMELIUM_MAX_AUDIO_CHANNELS 8

extern bool igt_chamelium_allow_fsm_handling;

#define CHAMELIUM_HOTPLUG_TIMEOUT 20 /* seconds */

/**
 * chamelium_edid:
 * @chamelium: instance of the chamelium where the EDID will be applied
 * @base: Unaltered EDID that would be used for all ports. Matches what you
 * would get from a real monitor.
 * @raw: EDID to be applied for each port.
 * @ids: The ID received from Chamelium after it's created for specific ports.
 */
struct chamelium_edid {
	struct chamelium *chamelium;
	struct edid *base;
	struct edid *raw[CHAMELIUM_MAX_PORTS];
	int ids[CHAMELIUM_MAX_PORTS];
	struct igt_list_head link;
};

void chamelium_deinit_rpc_only(struct chamelium *chamelium);
struct chamelium *chamelium_init_rpc_only(void);
struct chamelium *chamelium_init(int drm_fd, igt_display_t *display);
void chamelium_deinit(struct chamelium *chamelium);
void chamelium_reset(struct chamelium *chamelium);

struct chamelium_port **chamelium_get_ports(struct chamelium *chamelium,
					    int *count);
unsigned int chamelium_port_get_type(const struct chamelium_port *port);
drmModeConnector *chamelium_port_get_connector(struct chamelium *chamelium,
					       struct chamelium_port *port,
					       bool reprobe);
const char *chamelium_port_get_name(struct chamelium_port *port);
void
chamelium_require_connector_present(struct chamelium_port **ports,
				    unsigned int type,
				    int port_count,
				    int count);
drmModeConnection
chamelium_reprobe_connector(igt_display_t *display,
			    struct chamelium *chamelium,
			    struct chamelium_port *port);
void
chamelium_wait_for_conn_status_change(igt_display_t *display,
				      struct chamelium *chamelium,
				      struct chamelium_port *port,
				      drmModeConnection status);
void
chamelium_reset_state(igt_display_t *display,
		      struct chamelium *chamelium,
		      struct chamelium_port *port,
		      struct chamelium_port **ports,
		      int port_count);

bool chamelium_wait_reachable(struct chamelium *chamelium, int timeout);
void chamelium_assert_reachable(struct chamelium *chamelium, int timeout);
void chamelium_plug(struct chamelium *chamelium, struct chamelium_port *port);
void chamelium_unplug(struct chamelium *chamelium, struct chamelium_port *port);
bool chamelium_is_plugged(struct chamelium *chamelium,
			  struct chamelium_port *port);
bool chamelium_port_wait_video_input_stable(struct chamelium *chamelium,
					    struct chamelium_port *port,
					    int timeout_secs);
void chamelium_fire_mixed_hpd_pulses(struct chamelium *chamelium,
				     struct chamelium_port *port, ...);
void chamelium_fire_hpd_pulses(struct chamelium *chamelium,
			       struct chamelium_port *port,
			       int width_msec, int count);
void chamelium_schedule_hpd_toggle(struct chamelium *chamelium,
				   struct chamelium_port *port, int delay_ms,
				   bool rising_edge);
struct chamelium_edid *chamelium_new_edid(struct chamelium *chamelium,
					  const struct edid *edid);
const struct edid *chamelium_edid_get_raw(struct chamelium_edid *edid,
					  struct chamelium_port *port);
struct edid *chamelium_edid_get_editable_raw(struct chamelium_edid *edid,
					     struct chamelium_port *port);
void chamelium_port_set_edid(struct chamelium *chamelium,
			     struct chamelium_port *port,
			     struct chamelium_edid *edid);
void chamelium_port_set_tiled_edid(struct chamelium *chamelium,
				   struct chamelium_port *port,
				   struct chamelium_edid *edid);
bool chamelium_port_get_ddc_state(struct chamelium *chamelium,
				  struct chamelium_port *port);
void chamelium_port_set_ddc_state(struct chamelium *chamelium,
				  struct chamelium_port *port,
				  bool enabled);
void chamelium_port_get_resolution(struct chamelium *chamelium,
				   struct chamelium_port *port,
				   int *x, int *y);
bool chamelium_supports_get_video_params(struct chamelium *chamelium);
void chamelium_port_get_video_params(struct chamelium *chamelium,
				     struct chamelium_port *port,
				     struct chamelium_video_params *params);
igt_crc_t *chamelium_get_crc_for_area(struct chamelium *chamelium,
				      struct chamelium_port *port,
				      int x, int y, int w, int h);
void chamelium_start_capture(struct chamelium *chamelium,
			     struct chamelium_port *port,
			     int x, int y, int w, int h);
void chamelium_stop_capture(struct chamelium *chamelium, int frame_count);
void chamelium_capture(struct chamelium *chamelium, struct chamelium_port *port,
		       int x, int y, int w, int h, int frame_count);
bool chamelium_supports_get_last_infoframe(struct chamelium *chamelium);
struct chamelium_infoframe *
chamelium_get_last_infoframe(struct chamelium *chamelium,
			     struct chamelium_port *port,
			     enum chamelium_infoframe_type type);
bool chamelium_has_audio_support(struct chamelium *chamelium,
				 struct chamelium_port *port);
void chamelium_get_audio_channel_mapping(struct chamelium *chamelium,
					 struct chamelium_port *port,
					 int mapping[static CHAMELIUM_MAX_AUDIO_CHANNELS]);
void chamelium_get_audio_format(struct chamelium *chamelium,
				struct chamelium_port *port,
				int *rate, int *channels);
void chamelium_start_capturing_audio(struct chamelium *chamelium,
				    struct chamelium_port *port, bool save_to_file);
struct chamelium_audio_file *chamelium_stop_capturing_audio(struct chamelium *chamelium,
							    struct chamelium_port *port);
igt_crc_t *chamelium_read_captured_crcs(struct chamelium *chamelium,
					int *frame_count);
struct chamelium_frame_dump *chamelium_read_captured_frame(struct chamelium *chamelium,
							   unsigned int index);
struct chamelium_frame_dump *chamelium_port_dump_pixels(struct chamelium *chamelium,
							struct chamelium_port *port,
							int x, int y,
							int w, int h);
igt_crc_t *chamelium_calculate_fb_crc(int fd, struct igt_fb *fb);
struct chamelium_fb_crc_async_data *chamelium_calculate_fb_crc_async_start(int fd,
									   struct igt_fb *fb);
igt_crc_t *chamelium_calculate_fb_crc_async_finish(struct chamelium_fb_crc_async_data *fb_crc);
int chamelium_get_captured_frame_count(struct chamelium *chamelium);
int chamelium_get_frame_limit(struct chamelium *chamelium,
			      struct chamelium_port *port,
			      int w, int h);
void chamelium_assert_frame_eq(const struct chamelium *chamelium,
			       const struct chamelium_frame_dump *dump,
			       struct igt_fb *fb);
void chamelium_assert_crc_eq_or_dump(struct chamelium *chamelium,
				     igt_crc_t *reference_crc,
				     igt_crc_t *capture_crc, struct igt_fb *fb,
				     int index);
void chamelium_assert_frame_match_or_dump(struct chamelium *chamelium,
					  struct chamelium_port *port,
					  const struct chamelium_frame_dump *frame,
					  struct igt_fb *fb,
					  enum chamelium_check check);
bool chamelium_frame_match_or_dump(struct chamelium *chamelium,
				   struct chamelium_port *port,
				   const struct chamelium_frame_dump *frame,
				   struct igt_fb *fb,
				   enum chamelium_check check);
bool chamelium_frame_match_or_dump_frame_pair(struct chamelium *chamelium,
					      struct chamelium_port *port,
					      const struct chamelium_frame_dump *frame0,
					      const struct chamelium_frame_dump *frame1,
					      enum chamelium_check check);
void chamelium_crop_analog_frame(struct chamelium_frame_dump *dump, int width,
				 int height);
void chamelium_destroy_frame_dump(struct chamelium_frame_dump *dump);
void chamelium_destroy_audio_file(struct chamelium_audio_file *audio_file);
void chamelium_infoframe_destroy(struct chamelium_infoframe *infoframe);
bool chamelium_plug_all(struct chamelium *chamelium);
bool chamelium_wait_all_configured_ports_connected(struct chamelium *chamelium,
						   int drm_fd);

#endif /* IGT_CHAMELIUM_H */
