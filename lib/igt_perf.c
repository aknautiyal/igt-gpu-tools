#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#include <sys/sysmacros.h>
#endif
#include <time.h>
#include <unistd.h>

#include "igt_perf.h"

static char *bus_address(int i915, char *path, int pathlen)
{
	struct stat st;
	int len = -1;
	int dir;
	char *s;

	if (fstat(i915, &st) || !S_ISCHR(st.st_mode))
		return NULL;

	snprintf(path, pathlen, "/sys/dev/char/%d:%d",
		 major(st.st_rdev), minor(st.st_rdev));

	dir = open(path, O_RDONLY);
	if (dir != -1) {
		len = readlinkat(dir, "device", path, pathlen - 1);
		close(dir);
	}
	if (len < 0)
		return NULL;

	path[len] = '\0';

	/* strip off the relative path */
	s = strrchr(path, '/');
	if (s)
		memmove(path, s + 1, len - (s - path) + 1);

	return path;
}

const char *i915_perf_device(int i915, char *buf, int buflen)
{
	char *s;

#define prefix "i915_"
#define plen strlen(prefix)

	if (!buf || buflen < plen)
		return "i915";

	memcpy(buf, prefix, plen);

	if (!bus_address(i915, buf + plen, buflen - plen) ||
	    strcmp(buf + plen, "0000:00:02.0") == 0) /* legacy name for igfx */
		buf[plen - 1] = '\0';

	/* Convert all colons in the address to '_', thanks perf! */
	for (s = buf; *s; s++)
		if (*s == ':')
			*s = '_';

	return buf;
}

const char *xe_perf_device(int xe, char *buf, int buflen)
{
	char *s;
	char pref[] = "xe_";
	int len = strlen(pref);


	if (!buf || buflen < len)
		return "xe";

	memcpy(buf, pref, len);

	if (!bus_address(xe, buf + len, buflen - len))
		buf[len - 1] = '\0';

	/* Convert all colons in the address to '_', thanks perf! */
	for (s = buf; *s; s++)
		if (*s == ':')
			*s = '_';

	return buf;
}

/**
 * perf_event_format: Returns the shift for format param
 * @device: PMU device
 * @param: Parameter for which you need the format
 * @shift: bit shift
 *
 * Returns: 0 on success or negative error code
 */
int perf_event_format(const char *device, const char *param, uint32_t *shift)
{
	char buf[NAME_MAX];
	ssize_t bytes;
	uint32_t end;
	int ret, fd;

	snprintf(buf, sizeof(buf),
		 "/sys/bus/event_source/devices/%s/format/%s",
		 device, param);

	fd = open(buf, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	bytes = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (bytes < 1)
		return -EINVAL;

	buf[bytes] = '\0';
	ret = sscanf(buf, "config:%u-%u", shift, &end);
	if (ret != 2)
		return -EINVAL;

	return 0;
}

/**
 * perf_event_config:
 * @device: Device string in driver:pci format
 * @event: The event name
 * @config: Pointer to the config
 * Returns: 0 for success, negative value on error
 */
int perf_event_config(const char *device, const char *event, uint64_t *config)
{
	char buf[NAME_MAX];
	ssize_t bytes;
	int ret;
	int fd;

	snprintf(buf, sizeof(buf),
		 "/sys/bus/event_source/devices/%s/events/%s",
		 device,
		 event);

	fd = open(buf, O_RDONLY);
	if (fd < 0)
		return -errno;

	bytes = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (bytes < 1)
		return -EINVAL;

	buf[bytes] = '\0';
	ret = sscanf(buf, "event=0x%" SCNx64, config);
	if (ret != 1)
		return -EINVAL;

	return 0;
}

uint64_t xe_perf_type_id(int xe)
{
	char buf[80];

	return igt_perf_type_id(xe_perf_device(xe, buf, sizeof(buf)));
}

uint64_t i915_perf_type_id(int i915)
{
	char buf[80];

	return igt_perf_type_id(i915_perf_device(i915, buf, sizeof(buf)));
}

uint64_t igt_perf_type_id(const char *device)
{
	char buf[64];
	ssize_t ret;
	int fd;

	snprintf(buf, sizeof(buf),
		 "/sys/bus/event_source/devices/%s/type", device);

	fd = open(buf, O_RDONLY);
	if (fd < 0)
		return 0;

	ret = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (ret < 1)
		return 0;

	buf[ret] = '\0';

	return strtoull(buf, NULL, 0);
}

int igt_perf_events_dir(int i915)
{
	char buf[80];
	char path[PATH_MAX];

	i915_perf_device(i915, buf, sizeof(buf));
	snprintf(path, sizeof(path), "/sys/bus/event_source/devices/%s/events", buf);
	return open(path, O_RDONLY);
}

static int
_perf_open(uint64_t type, uint64_t config, int group, uint64_t format)
{
	struct perf_event_attr attr = { };
	int nr_cpus = get_nprocs_conf();
	int cpu = 0, ret;

	attr.type = type;
	if (attr.type == 0)
		return -ENOENT;

	if (group >= 0)
		format &= ~PERF_FORMAT_GROUP;

	attr.read_format = format;
	attr.config = config;
	attr.use_clockid = 1;
	attr.clockid = CLOCK_MONOTONIC;

	do {
		ret = perf_event_open(&attr, -1, cpu++, group, 0);
	} while ((ret < 0 && errno == EINVAL) && (cpu < nr_cpus));

	return ret;
}

int perf_igfx_open(uint64_t config)
{
	return _perf_open(igt_perf_type_id("i915"), config, -1,
			  PERF_FORMAT_TOTAL_TIME_ENABLED);
}

int perf_igfx_open_group(uint64_t config, int group)
{
	return _perf_open(igt_perf_type_id("i915"), config, group,
			  PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_GROUP);
}

int perf_xe_open(int xe, uint64_t config)
{
	return _perf_open(xe_perf_type_id(xe), config, -1,
			PERF_FORMAT_TOTAL_TIME_ENABLED);
}

int perf_i915_open(int i915, uint64_t config)
{
	return _perf_open(i915_perf_type_id(i915), config, -1,
			  PERF_FORMAT_TOTAL_TIME_ENABLED);
}

int perf_i915_open_group(int i915, uint64_t config, int group)
{
	return _perf_open(i915_perf_type_id(i915), config, group,
			  PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_GROUP);
}

int igt_perf_open(uint64_t type, uint64_t config)
{
	return _perf_open(type, config, -1,
			  PERF_FORMAT_TOTAL_TIME_ENABLED);
}

int igt_perf_open_group(uint64_t type, uint64_t config, int group)
{
	return _perf_open(type, config, group,
			  PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_GROUP);
}
