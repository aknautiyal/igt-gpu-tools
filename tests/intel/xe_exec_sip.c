// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

/**
 * TEST: Tests for GPGPU shader and system routine (SIP) execution
 * Category: Core
 * Mega feature: EUdebug
 * Sub-category: EUdebug tests
 * Functionality: system routine
 * Description: Exercise interaction between GPGPU shader and system routine
 *              (SIP), which should handle exceptions raised on Execution Unit.
 * Test category: functionality test
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>

#include "gpgpu_shader.h"
#include "igt.h"
#include "igt_sysfs.h"
#include "xe/xe_ioctl.h"
#include "xe/xe_query.h"

#define WIDTH 64
#define HEIGHT 64

#define COLOR_C4 0xc4c4c4c4

#define SHADER_CANARY 0x01010101
#define SHADER_CANARY2 0x02020202
#define SIP_CANARY 0x03030303
#define SIP_CANARY2 0x04040404

enum shader_type {
	SHADER_HANG,
	SHADER_INV_INSTR_DISABLED,
	SHADER_INV_INSTR_THREAD_ENABLED,
	SHADER_INV_INSTR_WALKER_ENABLED,
	SHADER_WRITE,
};

enum sip_type {
	SIP_INV_INSTR,
	SIP_NULL,
};

/* Control Register cr0.1 bits for exception handling */
#define ILLEGAL_OPCODE_ENABLE BIT(12)
#define ILLEGAL_OPCODE_STATUS BIT(28)

static struct intel_buf *
create_fill_buf(int fd, int width, int height, uint8_t color)
{
	struct intel_buf *buf;
	uint8_t *ptr;

	buf = calloc(1, sizeof(*buf));
	igt_assert(buf);

	intel_buf_init(buf_ops_create(fd), buf, width / 4, height, 32, 0,
		       I915_TILING_NONE, 0);

	ptr = xe_bo_map(fd, buf->handle, buf->surface[0].size);
	memset(ptr, color, buf->surface[0].size);
	munmap(ptr, buf->surface[0].size);

	return buf;
}

static struct gpgpu_shader *get_shader(int fd, enum shader_type shader_type)
{
	static struct gpgpu_shader *shader;
	uint32_t expected_cr0_bit;

	shader = gpgpu_shader_create(fd);
	if (shader_type == SHADER_INV_INSTR_WALKER_ENABLED)
		shader->illegal_opcode_exception_enable = true;

	gpgpu_shader__write_dword(shader, SHADER_CANARY, 0);

	switch (shader_type) {
	case SHADER_HANG:
		gpgpu_shader__label(shader, 0);
		gpgpu_shader__nop(shader);
		gpgpu_shader__jump(shader, 0);
		break;
	case SHADER_WRITE:
		break;
	case SHADER_INV_INSTR_THREAD_ENABLED:
		gpgpu_shader__set_exception(shader, ILLEGAL_OPCODE_ENABLE);
		__attribute__ ((fallthrough));
	case SHADER_INV_INSTR_DISABLED:
	case SHADER_INV_INSTR_WALKER_ENABLED:
		expected_cr0_bit = shader_type == SHADER_INV_INSTR_DISABLED ?
				   0 : ILLEGAL_OPCODE_ENABLE;
		gpgpu_shader__write_on_exception(shader, SHADER_CANARY2, 1, 0,
						 ILLEGAL_OPCODE_ENABLE, expected_cr0_bit);
		gpgpu_shader__nop(shader);
		gpgpu_shader__nop(shader);
		/* modify second nop, set only opcode bits[6:0] */
		shader->instr[gpgpu_shader_last_instr(shader)][0] = 0x7f;
		/* SIP should clear exception bit, negative check */
		gpgpu_shader__write_on_exception(shader, SHADER_CANARY2, 0, 0,
						 ILLEGAL_OPCODE_STATUS, ILLEGAL_OPCODE_STATUS);
		break;
	}

	gpgpu_shader__eot(shader);
	return shader;
}

static struct gpgpu_shader *get_sip(int fd, enum sip_type sip_type, unsigned int y_offset)
{
	static struct gpgpu_shader *sip;

	if (sip_type == SIP_NULL)
		return NULL;

	sip = gpgpu_shader_create(fd);
	gpgpu_shader__write_dword(sip, SIP_CANARY, y_offset);

	switch (sip_type) {
	case SIP_INV_INSTR:
		gpgpu_shader__write_on_exception(sip, SIP_CANARY2, 0, y_offset,
						 ILLEGAL_OPCODE_STATUS, 0);
		/* skip invalid instruction */
		gpgpu_shader__increase_aip(sip, 16);
		break;
	default:
		break;
	}

	gpgpu_shader__end_system_routine(sip, false);

	return sip;
}

static uint32_t gpgpu_shader(int fd, struct intel_bb *ibb, enum shader_type shader_type,
			     enum sip_type sip_type, unsigned int threads, unsigned int width,
			     unsigned int height)
{
	struct intel_buf *buf = create_fill_buf(fd, width, height, (uint8_t)COLOR_C4);
	struct gpgpu_shader *sip = get_sip(fd, sip_type, height / 2);
	struct gpgpu_shader *shader = get_shader(fd, shader_type);

	gpgpu_shader_exec(ibb, buf, 1, threads, shader, sip, 0, 0);

	if (sip)
		gpgpu_shader_destroy(sip);

	gpgpu_shader_destroy(shader);
	return buf->handle;
}

static void check_fill_buf(uint32_t *ptr, const int dword_width, const int x, const int y,
			   const uint32_t color)
{
	const uint32_t val = ptr[y * dword_width + x];

	igt_assert_f(val == color,
		     "Expected 0x%08x, found 0x%08x at (%d,%d)\n",
		     color, val, x, y);
}

static void check_buf(int fd, uint32_t handle, int width, int height, int thread_count_expected,
		      enum shader_type shader_type, enum sip_type sip_type, uint32_t poison_c)
{
	int thread_count = 0, sip_count = 0, invalidinstr_count = 0;
	unsigned int sz = ALIGN(width * height, 4096);
	const uint32_t dword_width = width / 4;
	uint32_t *ptr;
	int i, j;

	ptr = xe_bo_mmap_ext(fd, handle, sz, PROT_READ);

	for (i = 1, j = 0; j < height / 2; ++j) {
		if (ptr[j * dword_width] == SHADER_CANARY)
			++thread_count;
		else
			check_fill_buf(ptr, dword_width, 0, j, poison_c);

		if (ptr[j * dword_width + 1] == SHADER_CANARY2) {
			++invalidinstr_count;
			++i;
		}

		for (; i < dword_width; i++)
			check_fill_buf(ptr, dword_width, i, j, poison_c);

		i = 1;
	}

	for (i = 0, j = height / 2; j < height; ++j) {
		if (ptr[j * dword_width] == SIP_CANARY) {
			++sip_count;
			i = 4;
		}

		for (; i < dword_width; i++)
			check_fill_buf(ptr, dword_width, i, j, poison_c);

		i = 0;
	}

	igt_assert(thread_count);

	if (shader_type >= SHADER_INV_INSTR_DISABLED &&
	    shader_type <= SHADER_INV_INSTR_WALKER_ENABLED)
		igt_assert_f(thread_count == invalidinstr_count,
			     "Thread and invalid instruction count mismatch, %d != %d\n",
			     thread_count, invalidinstr_count);
	else
		igt_assert_eq(invalidinstr_count, 0);

	if (sip_type == SIP_INV_INSTR && shader_type != SHADER_INV_INSTR_DISABLED)
		igt_assert_f(thread_count == sip_count,
			     "Thread and SIP count mismatch, %d != %d\n",
			     thread_count, sip_count);
	else
		igt_assert_eq(sip_count, 0);

	munmap(ptr, sz);
}

static uint64_t
xe_sysfs_get_job_timeout_ms(int fd, struct drm_xe_engine_class_instance *eci)
{
	int engine_fd = -1;
	uint64_t ret;

	engine_fd = xe_sysfs_engine_open(fd, eci->gt_id, eci->engine_class);
	ret = igt_sysfs_get_u64(engine_fd, "job_timeout_ms");
	close(engine_fd);

	return ret;
}

/**
 * SUBTEST: sanity
 * Description: check basic shader with write operation
 *
 * SUBTEST: sanity-after-timeout
 * Description: check basic shader execution after job timeout
 *
 * SUBTEST: invalidinstr-disabled
 * Description: Verify that we don't enter SIP after running into an invalid
 *		instruction when exception is not enabled.
 *
 * SUBTEST: invalidinstr-thread-enabled
 * Description: Verify that we enter SIP after running into an invalid instruction
 *              when exception is enabled from thread.
 *
 * SUBTEST: invalidinstr-walker-enabled
 * Description: Verify that we enter SIP after running into an invalid instruction
 *              when exception is enabled from COMPUTE_WALKER.
 */
static void test_sip(enum shader_type shader_type, enum sip_type sip_type,
		     struct drm_xe_engine_class_instance *eci, uint32_t flags)
{
	unsigned int threads = 512;
	unsigned int height = max_t(threads, HEIGHT, threads * 2);
	uint32_t exec_queue_id, handle, vm_id;
	unsigned int width = WIDTH;
	struct timespec ts = { };
	uint64_t timeout;
	struct intel_bb *ibb;
	int fd;

	igt_debug("Using %s\n", xe_engine_class_string(eci->engine_class));

	fd = drm_open_driver(DRIVER_XE);
	xe_device_get(fd);

	vm_id = xe_vm_create(fd, 0, 0);

	/* Get timeout for job, and add 8s to ensure timeout processes in subtest. */
	timeout = xe_sysfs_get_job_timeout_ms(fd, eci) + 8ull * MSEC_PER_SEC;
	timeout *= NSEC_PER_MSEC;
	timeout *= igt_run_in_simulation() ? 10 : 1;

	exec_queue_id = xe_exec_queue_create(fd, vm_id, eci, 0);
	ibb = intel_bb_create_with_context(fd, exec_queue_id, vm_id, NULL, 4096);

	igt_nsec_elapsed(&ts);
	handle = gpgpu_shader(fd, ibb, shader_type, sip_type, threads, width, height);

	intel_bb_sync(ibb);
	igt_assert_lt_u64(igt_nsec_elapsed(&ts), timeout);

	check_buf(fd, handle, width, height, threads, shader_type, sip_type, COLOR_C4);

	gem_close(fd, handle);
	intel_bb_destroy(ibb);

	xe_exec_queue_destroy(fd, exec_queue_id);
	xe_vm_destroy(fd, vm_id);
	xe_device_put(fd);
	close(fd);
}

#define test_render_and_compute(t, __fd, __eci) \
	igt_subtest_with_dynamic(t) \
		xe_for_each_engine(__fd, __eci) \
			if (__eci->engine_class == DRM_XE_ENGINE_CLASS_RENDER || \
			    __eci->engine_class == DRM_XE_ENGINE_CLASS_COMPUTE) \
				igt_dynamic_f("%s%d", xe_engine_class_string(__eci->engine_class), \
					      __eci->engine_instance)

igt_main
{
	struct drm_xe_engine_class_instance *eci;
	int fd;

	igt_fixture
		fd = drm_open_driver(DRIVER_XE);

	test_render_and_compute("sanity", fd, eci)
		test_sip(SHADER_WRITE, SIP_NULL, eci, 0);

	test_render_and_compute("sanity-after-timeout", fd, eci) {
		test_sip(SHADER_HANG, SIP_NULL, eci, 0);

		xe_for_each_engine(fd, eci)
			if (eci->engine_class == DRM_XE_ENGINE_CLASS_RENDER ||
			    eci->engine_class == DRM_XE_ENGINE_CLASS_COMPUTE)
				test_sip(SHADER_WRITE, SIP_NULL, eci, 0);
	}

	test_render_and_compute("invalidinstr-disabled", fd, eci)
		test_sip(SHADER_INV_INSTR_DISABLED, SIP_INV_INSTR, eci, 0);

	test_render_and_compute("invalidinstr-thread-enabled", fd, eci)
		test_sip(SHADER_INV_INSTR_THREAD_ENABLED, SIP_INV_INSTR, eci, 0);

	test_render_and_compute("invalidinstr-walker-enabled", fd, eci)
		test_sip(SHADER_INV_INSTR_WALKER_ENABLED, SIP_INV_INSTR, eci, 0);

	igt_fixture
		drm_close_driver(fd);
}
