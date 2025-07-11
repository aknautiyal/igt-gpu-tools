// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */
#include <limits.h>

#include "igt.h"
#include "igt_configfs.h"
#include "igt_device.h"
#include "igt_fs.h"
#include "igt_kmod.h"
#include "igt_sysfs.h"
#include "xe/xe_query.h"

/**
 * TEST: Check configfs userspace API
 * Category: Core
 * Mega feature: General Core features
 * Sub-category: uapi
 * Functionality: configfs
 * Description: validate configfs entries
 * Test category: functionality test
 */

static char bus_addr[NAME_MAX];

static void restore(int sig)
{
	/* Restore after survivability mode */
	igt_kmod_unbind("xe", bus_addr);
	igt_kmod_bind("xe", bus_addr);
}

static void set_survivability_mode(int configfs_device_fd, bool value)
{
	igt_kmod_unbind("xe", bus_addr);
	igt_sysfs_set_boolean(configfs_device_fd, "survivability_mode", value);
	igt_kmod_bind("xe", bus_addr);
}

/**
 * SUBTEST: survivability-mode
 * Description: Validate survivability mode by setting configfs
 */
static void test_survivability_mode(int configfs_device_fd)
{
	char path[PATH_MAX];
	int fd;

	/* Enable survivability mode */
	set_survivability_mode(configfs_device_fd, true);

	/* check presence of survivability mode sysfs */
	snprintf(path, PATH_MAX, "/sys/bus/pci/devices/%s/survivability_mode", bus_addr);

	fd = open(path, O_RDONLY);
	igt_assert_f(fd >= 0, "Survivability mode not set\n");
	close(fd);
}

/**
 * SUBTEST: engines-allowed-invalid
 * Description: Validate engines_allowed attribute for invalid values
 */
static void test_engines_allowed_invalid(int configfs_device_fd)
{
	static const char *values[] = {
		"xcs0",
		"abcsdcs0",
		"rcs0,abcsdcs0",
		"rcs9",
		"rcs10",
		"rcs0asdf",
	};

	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		const char *v = values[i];

		igt_debug("Writing '%s' to engines_allowed\n", v);
		igt_assert(!igt_sysfs_set(configfs_device_fd, "engines_allowed", v));
	}
}

/**
 * SUBTEST: engines-allowed
 * Description: Validate engines_allowed attribute
 */
static void test_engines_allowed(int configfs_device_fd)
{
	static const char *values[] = {
		"rcs0", "rcs*", "rcs0,bcs0", "bcs0,rcs0",
		"bcs0\nrcs0", "bcs0\nrcs0\n",
		"rcs000",
	};

	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		const char *v = values[i];

		igt_debug("Writing '%s' to engines_allowed\n", v);
		igt_assert(igt_sysfs_set(configfs_device_fd, "engines_allowed", v));
	}
}

static int create_device_configfs_group(int configfs_fd, int fd)
{
	int configfs_device_fd;
	struct pci_device *pci_dev;
	mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	pci_dev = igt_device_get_pci_device(fd);
	snprintf(bus_addr, sizeof(bus_addr), "%04x:%02x:%02x.%01x",
		 pci_dev->domain, pci_dev->bus, pci_dev->dev, pci_dev->func);

	configfs_device_fd = igt_fs_create_dir(configfs_fd, bus_addr, mode);
	igt_assert(configfs_device_fd);

	return configfs_device_fd;
}

igt_main
{
	int fd, configfs_fd, configfs_device_fd;

	igt_fixture {
		fd = drm_open_driver(DRIVER_XE);
		configfs_fd = igt_configfs_open("xe");
		igt_require(configfs_fd != -1);
		configfs_device_fd = create_device_configfs_group(configfs_fd, fd);
	}

	igt_describe("Validate survivability mode");
	igt_subtest("survivability-mode") {
		igt_require(IS_BATTLEMAGE(intel_get_drm_devid(fd)));
		igt_install_exit_handler(restore);
		test_survivability_mode(configfs_device_fd);
	}

	igt_describe("Validate engines_allowed with invalid options");
	igt_subtest("engines-allowed-invalid") {
		igt_install_exit_handler(restore);
		test_engines_allowed_invalid(configfs_device_fd);
	}

	igt_describe("Validate engines_allowed");
	igt_subtest("engines-allowed") {
		igt_install_exit_handler(restore);
		test_engines_allowed(configfs_device_fd);
	}

	igt_fixture {
		igt_fs_remove_dir(configfs_fd, bus_addr);
		close(configfs_device_fd);
		close(configfs_fd);
		close(fd);
	}
}
