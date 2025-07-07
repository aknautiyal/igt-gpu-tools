#include "igt.h"
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <unistd.h>

#define WIDTH 3840
#define HEIGHT 2160
#define FPS 120
#define FLIP_INTERVAL_US (1000000 / FPS)
#define MAX_BUFFERS 3
#define TEST_DURATION_SEC 60

typedef enum { FREE, IN_FLIGHT } BufferState;

struct time_log_t {
	struct timespec time;
	char log[25];
};

typedef struct {
    int drm_fd;
    igt_display_t display;
    igt_output_t *output;
    igt_plane_t *primary;
    enum pipe pipe;
    igt_fb_t fb[MAX_BUFFERS];
    cairo_t *cr[MAX_BUFFERS];
    cairo_surface_t *bg_surface;
    cairo_t *bg_cr;
    BufferState state[MAX_BUFFERS];
    pthread_mutex_t lock[MAX_BUFFERS];
    int frame_index;
    bool running;
    struct timespec start_time;
    struct time_log_t time_log[10000000];
    int log_count;
    pthread_mutex_t time_log_lock;

} data_t;

static data_t data;

static void get_now(struct timespec *ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static double time_diff_us(struct timespec *t1, struct timespec *t2) {
    return (t2->tv_sec - t1->tv_sec) * 1e6 + (t2->tv_nsec - t1->tv_nsec) / 1e3;
}

static void draw_needle(cairo_t *cr, double angle_deg)
{
	double cx, cy, r, rad, x, y;
	cairo_set_source_surface(cr, data.bg_surface, 0, 0);
	cairo_paint(cr);

	cairo_save(cr);

	cx = WIDTH / 2;
	cy = HEIGHT / 2;
	r = HEIGHT / 3;

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
	cairo_stroke(cr);

	rad = angle_deg * M_PI / 180.0;
	x = cx + r * cos(rad);
	y = cy + r * sin(rad);

	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_set_line_width(cr, 4);
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, x, y);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static int get_free_buffer(void)
{
	for (int i = 0; i < MAX_BUFFERS; i++) {
		pthread_mutex_lock(&data.lock[i]);
		if (data.state[i] == FREE) {
			data.state[i] = IN_FLIGHT;
			pthread_mutex_unlock(&data.lock[i]);
			return i;
		}
		pthread_mutex_unlock(&data.lock[i]);
	}
	return -1;
}

static void vblank_handler(int fd, unsigned int frame, unsigned int sec,
                         unsigned int usec, void *user_data)
{
}

static void flip_handler(int fd, unsigned int frame, unsigned int sec,
                         unsigned int usec, void *user_data)
{
	struct timespec now;
	int *fb_index = (int *)user_data;

	pthread_mutex_lock(&data.lock[*fb_index]);
	data.state[*fb_index] = FREE;
	pthread_mutex_unlock(&data.lock[*fb_index]);
	free(fb_index);

	get_now(&now);
	pthread_mutex_lock(&data.time_log_lock);
	data.time_log[data.log_count].time = now;
	strcpy(data.time_log[data.log_count].log, "FLIP_DONE");
	data.log_count++;
	pthread_mutex_unlock(&data.time_log_lock);
}

static void *event_thread_fn(void *arg)
{
	drmEventContext ev = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.vblank_handler = vblank_handler,
		.page_flip_handler = flip_handler,
	};

	while (data.running)
		drmHandleEvent(data.drm_fd, &ev);

	return NULL;
}

static void flip_loop(void)
{
	struct timespec now, last_flip;
	double angle_step;

	get_now(&last_flip);
	angle_step = 360.0 / (FPS * TEST_DURATION_SEC);

	while (data.running) {
		int fb_idx, ret;
		int *userdata;
		double angle;

		get_now(&now);
		if (time_diff_us(&last_flip, &now) < FLIP_INTERVAL_US) {
			usleep(500);
			continue;
		}
		last_flip = now;

		fb_idx = get_free_buffer();
		if (fb_idx < 0) continue;

		angle = fmod(data.frame_index * angle_step, 360.0);
		draw_needle(data.cr[fb_idx], angle);

		userdata = malloc(sizeof(int));
		*userdata = fb_idx;

		ret = drmModePageFlip(data.drm_fd, data.display.pipes[data.pipe].crtc_id,
				      data.fb[fb_idx].fb_id,
				      DRM_MODE_PAGE_FLIP_EVENT, userdata);

		if (ret) {
			igt_warn("Page flip failed: %m\n");
			pthread_mutex_lock(&data.lock[fb_idx]);
			data.state[fb_idx] = FREE;
			pthread_mutex_unlock(&data.lock[fb_idx]);
			free(userdata);
			continue;
		}

		data.frame_index++;
		get_now(&now);
		if (time_diff_us(&data.start_time, &now) > TEST_DURATION_SEC * 1e6)
			data.running = false;
		pthread_mutex_lock(&data.time_log_lock);
		data.time_log[data.log_count].time = now;
		strcpy(data.time_log[data.log_count].log, "FLIP");
		data.log_count++;
		pthread_mutex_unlock(&data.time_log_lock);

	}
}

/* Toggles variable refresh rate on the pipe. */
static void set_vrr_on_pipe(enum pipe pipe,
			    bool need_modeset, bool enabled)
{
	igt_pipe_set_prop_value(&data.display, pipe, IGT_CRTC_VRR_ENABLED,
				enabled);

	igt_assert(igt_display_try_commit_atomic(&data.display,
						 need_modeset ? DRM_MODE_ATOMIC_ALLOW_MODESET : 0,
						 NULL) == 0);
}

static void setup_drm_and_buffers(void)
{
	igt_output_t *output = NULL;
	enum pipe pipe = PIPE_A;
	drmModeModeInfo *mode = NULL;

	data.drm_fd = __drm_open_driver(DRIVER_ANY);
	kmstest_set_vt_graphics_mode();
	igt_display_require(&data.display, data.drm_fd);

	for_each_connected_output(&data.display, output) {
		if (strcmp(output->name, "eDP-1") != 0)
			continue;
		igt_output_set_pipe(output, pipe);
		break;
	}

	data.output = output;
	data.pipe = pipe;
	mode = igt_output_get_mode(output);
	data.primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);

	for (int i = 0; i < MAX_BUFFERS; i++) {
		igt_create_fb(data.drm_fd, mode->hdisplay, mode->vdisplay,
				DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
				&data.fb[i]);
		data.cr[i] = igt_get_cairo_ctx(data.drm_fd, &data.fb[i]);
		pthread_mutex_init(&data.lock[i], NULL);
		data.state[i] = FREE;
	}

	data.bg_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, mode->hdisplay, mode->vdisplay);
	data.bg_cr = cairo_create(data.bg_surface);

	cairo_set_source_rgb(data.bg_cr, 0,0,0);
	cairo_paint(data.bg_cr);

	cairo_set_source_rgb(data.bg_cr, 1,1,1);
	cairo_arc(data.bg_cr, WIDTH/2, HEIGHT/2, HEIGHT/3, 0, 2 * M_PI);
	cairo_stroke(data.bg_cr);

	pthread_mutex_init(&data.time_log_lock, NULL);
	data.log_count = 0;

	igt_plane_set_fb(data.primary, &data.fb[0]);
	igt_display_commit2(&data.display, COMMIT_ATOMIC);
	igt_info("DRM + IGT setup complete: %dx%d\n", mode->hdisplay, mode->vdisplay);

	set_vrr_on_pipe(data.pipe, true, true);
}

static void timespec_diff(struct timespec *start, struct timespec *end, struct timespec *diff) {
	if ((end->tv_nsec - start->tv_nsec) < 0) {
		diff->tv_sec = end->tv_sec - start->tv_sec - 1;
		diff->tv_nsec = 1000000000 + end->tv_nsec - start->tv_nsec;
	} else {
		diff->tv_sec = end->tv_sec - start->tv_sec;
		diff->tv_nsec = end->tv_nsec - start->tv_nsec;
	}
}

int main(int argc, char **argv)
{
	pthread_t event_thread;
	int i, k;

	setup_drm_and_buffers();
	data.running = true;
	data.frame_index = 0;
	get_now(&data.start_time);
	pthread_create(&event_thread, NULL, event_thread_fn, NULL);
	flip_loop();

	data.running = false;
	pthread_join(event_thread, NULL);

	for (i = 0; i < MAX_BUFFERS; i++) {
		if (data.cr[i]) {
			cairo_destroy(data.cr[i]);
			data.cr[i] = NULL;
		}
		igt_remove_fb(data.drm_fd, &data.fb[i]);
		pthread_mutex_destroy(&data.lock[i]);
	}

	if (data.bg_cr)
		cairo_destroy(data.bg_cr);
	if (data.bg_surface)
		cairo_surface_destroy(data.bg_surface);
	igt_display_fini(&data.display);
	close(data.drm_fd);
	igt_info("Complete...\n");
	for (i = 0, k = 1; i < data.log_count; i++) {
	//	printf("%ld.%09ld :: %s\n", data.time_log[i].time.tv_sec, data.time_log[i].time.tv_nsec, data.time_log[i].log);
		if (i + 2 < data.log_count) {
			struct timespec time_diff;

			timespec_diff(&data.time_log[i].time, &data.time_log[i+2].time, &time_diff);
			printf("%s%d::diff::%ldsec:: %f msec\n", i % 2 ? "FLIP" : "FLIP_DONE", k, time_diff.tv_sec, ((double) time_diff.tv_nsec) / 1000000.00);
		}
		if (i > 0 && i % 2)
			k++;
	}

	pthread_mutex_destroy(&data.time_log_lock);
}
