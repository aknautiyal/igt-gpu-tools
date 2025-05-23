/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */
#ifndef _INTEL_DRM_LOCAL_H_
#define _INTEL_DRM_LOCAL_H_

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * It is necessary on occasion to add uapi declarations to IGT before they
 * appear in imported kernel uapi headers. This header is provided for this
 * purpose.

 * Early uapi declarations should be added here exactly as they are
 * expected to appear in the kernel uapi headers, i.e. without the LOCAL_
 * or local_ prefix and without any #ifndef's. Attempt should be made to
 * clean these up when kernel uapi headers are sync'd.
 */

#define DRM_XE_MMAP_OFFSET_FLAG_PCI_BARRIER	(1 << 0)

#define DRM_XE_EXEC_QUEUE_LOW_LATENCY_HINT      (1 << 0)
#define DRM_XE_QUERY_CONFIG_FLAG_HAS_LOW_LATENCY        (1 << 1)

#if defined(__cplusplus)
}
#endif

#endif /* _INTEL_DRM_LOCAL_H_ */
