#include "igt.h"
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <unistd.h>

#define FLIP_INTERVAL_US(fps) (1000000 / (fps))
#define MAX_BUFFERS 3
#define MAX_FRAMES 10000000
#define TEST_DURATION_SEC 5
#define NUM_ROTATIONS 2

typedef enum { FREE, IN_FLIGHT } buffer_state;

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
    buffer_state state[MAX_BUFFERS];
    pthread_mutex_t lock[MAX_BUFFERS];
    int frame_index;
    bool running;
    struct timespec start_time;
    struct time_log_t time_log[MAX_FRAMES];
    int log_count;
    pthread_mutex_t time_log_lock;
    bool vrr;
    int fps;
    drmModeModeInfo mode;

} data_t;

static data_t data;

static void get_now(struct timespec *ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static double time_diff_us(struct timespec *t1, struct timespec *t2) {
    return (t2->tv_sec - t1->tv_sec) * 1e6 + (t2->tv_nsec - t1->tv_nsec) / 1e3;
}

static void get_dial_size(drmModeModeInfo *mode, double *xpos, double *ypos, double *radius)
{
	*xpos = mode->hdisplay / 2;
	*ypos = mode->vdisplay / 2;
	*radius = mode->vdisplay / 3;
}

static void draw_needle(cairo_t *cr, double angle_deg)
{
	double cx, cy, r, rad, x, y;
	cairo_set_source_surface(cr, data.bg_surface, 0, 0);
	cairo_paint(cr);

	cairo_save(cr);

	get_dial_size(&data.mode, &cx, &cy, &r);

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
	struct timespec now, next_flip, last_flip;
	double angle_step;

	get_now(&last_flip);
	angle_step = (360.0 * NUM_ROTATIONS) / (data.fps * TEST_DURATION_SEC);

	next_flip = data.start_time;
	while (data.running) {
		int fb_idx, ret;
		int *userdata;
		double angle;

		get_now(&now);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_flip, NULL);
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
		next_flip.tv_nsec += FLIP_INTERVAL_US(data.fps) * 1000;

		while (next_flip.tv_nsec >= 1e9) {
			next_flip.tv_sec += 1;
			next_flip.tv_nsec -= 1e9;
		}

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
	double xpos, ypos, radius;

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
	data.mode = *igt_output_get_mode(output);
	data.primary = igt_output_get_plane_type(output, DRM_PLANE_TYPE_PRIMARY);

	for (int i = 0; i < MAX_BUFFERS; i++) {
		igt_create_fb(data.drm_fd, data.mode.hdisplay, data.mode.vdisplay,
				DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR,
				&data.fb[i]);
		data.cr[i] = igt_get_cairo_ctx(data.drm_fd, &data.fb[i]);
		pthread_mutex_init(&data.lock[i], NULL);
		data.state[i] = FREE;
	}

	data.bg_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, data.mode.hdisplay, data.mode.vdisplay);
	data.bg_cr = cairo_create(data.bg_surface);

	cairo_set_source_rgb(data.bg_cr, 0,0,0);
	cairo_paint(data.bg_cr);

	cairo_set_source_rgb(data.bg_cr, 1,1,1);
	get_dial_size(&data.mode, &xpos, &ypos, &radius);
	cairo_arc(data.bg_cr, xpos, ypos, radius, 0, 2 * M_PI);
	cairo_stroke(data.bg_cr);

	pthread_mutex_init(&data.time_log_lock, NULL);
	data.log_count = 0;

	igt_plane_set_fb(data.primary, &data.fb[0]);
	igt_display_commit2(&data.display, COMMIT_ATOMIC);
	printf("DRM + IGT setup complete: %dx%d\n", data.mode.hdisplay, data.mode.vdisplay);

	set_vrr_on_pipe(data.pipe, true, data.vrr);
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

static void print_usage(void)
{
	printf("Usage: intel_vrrtest [OPTIONS]\n");
	printf("Options:\n");
	printf("-f 60,	--fps		Set fps (frames per seconds)\n");
	printf("-v 1	--vrr		Enable/Disable vrr\n");
	printf("-h,	--help		Display this message\n");
}

int main(int argc, char **argv)
{
	pthread_t event_thread;
	int i, j, k;
	int option;
	static const char optstr[] = "fv";
	struct option long_opts[] = {
		{"fps",	required_argument, NULL, 'f'},
		{"vrr",	required_argument, NULL, 'v'},
		{NULL,		0,		NULL,  0 }
	};
	struct timespec flip_diff[100000];

	data.fps = 60;
	data.vrr = false;

	while ((option = getopt_long(argc, argv, optstr, long_opts, NULL)) != -1) {
		switch (option) {
		case 'f':
			data.fps = atoi(optarg);
			break;
		case 'v':
			data.vrr = atoi(optarg) ? true : false;
			break;
		default:
			print_usage();
			return 0;
		}
	}

	printf("Using FPS: %d (interval %d us) VRR: %s\n", data.fps, FLIP_INTERVAL_US(data.fps), data.vrr ? "ON" : "OFF");

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
	for (i = 0, k = 0; i < data.log_count; i++) {
		struct timespec time_diff;
		if (strcmp(data.time_log[i].log, "FLIP_DONE") == 0) {
			for (j = i + 1; j < data.log_count; j++)
				if (strcmp(data.time_log[j].log, "FLIP_DONE") == 0)
					break;
			if (j == data.log_count)
				break;
			timespec_diff(&data.time_log[i].time, &data.time_log[j].time, &time_diff);
			flip_diff[k] = time_diff;
			k++;
		}
	}

	for (int count = 0; count < k; count++) {
		int length = (flip_diff[count].tv_nsec / 100000) / 5;
		printf("Flip:\t%d\t",count + 1);
		for (i = 0; i < length; i++)
			printf("=");
		printf(" %f msec\n", ((double) flip_diff[count].tv_nsec) / 1000000.00);
	}

	pthread_mutex_destroy(&data.time_log_lock);
}
