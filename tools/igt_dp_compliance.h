/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright 2017 Intel Corporation
 * Manasi Navare <manasi.d.navare@intel.com>
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

#ifndef __IGT_DP_COMPLIANCE_H__
#define __IGT_DP_COMPLIANCE_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef ANDROID
#include "android/glib.h"
#else
#include <glib.h>
#endif

extern int drm_fd;

gboolean igt_dp_compliance_setup_hotplug(void);
void igt_dp_compliance_cleanup_hotplug(void);

void enter_exec_path(char **argv);
void set_termio_mode(void);

/* called by the hotplug code */
int update_display(int mode, bool is_compliance_test);

#endif /* __IGT_DP_COMPLIANCE_H__ */
