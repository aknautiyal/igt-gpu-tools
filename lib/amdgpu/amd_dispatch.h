/* SPDX-License-Identifier: MIT
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#ifndef AMD_DISPATCH_H
#define AMD_DISPATCH_H

#include <amdgpu.h>
#include "amd_dispatch_helpers.h"

void amdgpu_gfx_dispatch_test(amdgpu_device_handle device_handle,
			      uint32_t ip_type, enum cmd_error_type hang,
				  const struct pci_addr *pci);

int amdgpu_memcpy_dispatch_test(amdgpu_device_handle device_handle,
					amdgpu_context_handle context_handle,
					uint32_t ip_type,
					uint32_t ring,
					uint32_t priority,
					uint32_t version,
					enum cmd_error_type hang,
					struct amdgpu_cs_err_codes *err_codes);

void amdgpu_dispatch_hang_slow_helper(amdgpu_device_handle device_handle,
				      uint32_t ip_type, const struct pci_addr *pci);


#endif
