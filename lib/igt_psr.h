/*
 * Copyright © 2018 Intel Corporation
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

#ifndef IGT_PSR_H
#define IGT_PSR_H

#include "igt_debugfs.h"
#include "igt_core.h"
#include "igt_aux.h"
#include "igt_kms.h"

#define PSR_STATUS_MAX_LEN 512

enum psr_mode {
	PSR_MODE_1,
	PSR_MODE_2,
	PSR_MODE_2_SEL_FETCH,
	PSR_MODE_2_ET,
	PR_MODE,
	PR_MODE_SEL_FETCH,
	PR_MODE_SEL_FETCH_ET,
	PSR_DISABLED,
};

enum fbc_mode {
	FBC_DISABLED,
	FBC_ENABLED,
};

bool psr_disabled_check(int debugfs_fd);
bool selective_fetch_check(int debugfs_fd, igt_output_t *output);
bool early_transport_check(int debugfs_fd);
bool psr_wait_entry(int debugfs_fd, enum psr_mode mode, igt_output_t *output);
bool psr_wait_update(int debugfs_fd, enum psr_mode mode, igt_output_t *output);
bool psr_long_wait_update(int debugfs_fd, enum psr_mode mode, igt_output_t *output);
bool psr_enable(int device, int debugfs_fd, enum psr_mode, igt_output_t *output);
bool psr_disable(int device, int debugfs_fd, igt_output_t *output);
bool psr_sink_support(int device, int debugfs_fd, enum psr_mode mode, igt_output_t *output);
void psr_sink_error_check(int debugfs_fd, enum psr_mode mode, igt_output_t *output);
bool psr2_wait_su(int debugfs_fd, uint16_t *num_su_blocks);
void psr_print_debugfs(int debugfs_fd);
enum psr_mode psr_get_mode(int debugfs_fd, igt_output_t *output);

bool i915_psr2_selective_fetch_check(int drm_fd, igt_output_t *output);

bool i915_psr2_sel_fetch_to_psr1(int drm_fd, igt_output_t *output);
void i915_psr2_sel_fetch_restore(int drm_fd, igt_output_t *output);
bool is_psr_enable_possible(int drm_fd, enum psr_mode mode);

#endif
