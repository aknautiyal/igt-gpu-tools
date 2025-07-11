/*
 * Copyright © 2015 Intel Corporation
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
 * Authors: Paulo Zanoni <paulo.r.zanoni@intel.com>
 *
 */

/**
 * TEST: kms frontbuffer tracking
 * Category: Display
 * Description: Test the Kernel's frontbuffer tracking mechanism and its related features: FBC, PSR and DRRS
 * Driver requirement: i915, xe
 * Mega feature: General Display Features
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <limits.h>

#include "i915/gem.h"
#include "i915/gem_create.h"
#include "i915/intel_drrs.h"
#include "i915/intel_fbc.h"
#include "igt.h"
#include "igt_sysfs.h"
#include "igt_psr.h"

/**
 * SUBTEST: basic
 * Description: Do some basic operations regardless of which features are enabled
 *
 * SUBTEST: plane-fbc-rte
 * Description: Sanity test to enable FBC on a plane.
 *
 * SUBTEST: pipe-fbc-rte
 * Description: Sanity test to enable FBC on each pipe.
 *
 * SUBTEST: drrs-%dp-rte
 * Description: Sanity test to enable DRRS with %arg[1] panels.
 *
 * SUBTEST: fbc-%dp-rte
 * Description: Sanity test to enable FBC with %arg[1] panels.
 *
 * SUBTEST: psr-%dp-rte
 * Description: Sanity test to enable PSR with %arg[1] panels.
 *
 * SUBTEST: fbcdrrs-%dp-rte
 * Description: Sanity test to enable FBC & DRRS with %arg[1] panels.
 *
 * SUBTEST: fbcpsr-%dp-rte
 * Description: Sanity test to enable FBC & PSR with %arg[1] panels.
 *
 * arg[1].values:   1, 2
 */

/**
 * SUBTEST: drrs-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbc-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: psr-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcdrrs-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcpsr-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * arg[1]:
 *
 * @indfb:          Individual fb
 * @shrfb:          Shared fb
 *
 * arg[2]:
 *
 * @blt:            Blitter
 * @mmap-wc:        MMAP-WC
 * @render:         Render
 */

/**
 * SUBTEST: drrs-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbc-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: psr-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-1p-offscren-pri-%s-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * arg[1]:
 *
 * @indfb:          Individual fb
 * @shrfb:          Shared fb
 *
 * arg[2]:
 *
 * @mmap-cpu:       MMAP-CPU
 * @mmap-gtt:       MMAP-GTT
 * @pwrite:         PWRITE
 */

/**
 * SUBTEST: drrs-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbc-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: psr-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @cur:            Cursor plane
 * @pri:            Primary plane
 * @spr:            Sprite plane
 *
 * arg[3]:
 *
 * @mmap-cpu:       MMAP-CPU
 * @mmap-gtt:       MMAP-GTT
 * @pwrite:         PWRITE
 */

/**
 * SUBTEST: drrs-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbc-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: psr-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcpsr-%dp-primscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @cur:            Cursor plane
 * @pri:            Primary plane
 * @spr:            Sprite plane
 *
 * arg[3]:
 *
 * @blt:            Blitter
 * @mmap-wc:        MMAP-WC
 * @render:         Render
 */

/**
 * SUBTEST: drrs-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbc-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: psr-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcpsr-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @blt:            Blitter
 * @mmap-wc:        MMAP-WC
 * @render:         Render
 */

/**
 * SUBTEST: drrs-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbc-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: psr-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-%dp-primscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @mmap-cpu:       MMAP-CPU
 * @mmap-gtt:       MMAP-GTT
 * @pwrite:         PWRITE
 */

/**
 * SUBTEST: drrs-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbc-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: psr-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcpsr-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * arg[1]:
 *
 * @cur:            Cursor plane
 * @pri:            Primary plane
 * @spr:            Sprite plane
 *
 * arg[2]:
 *
 * @blt:            Blitter
 * @mmap-wc:        MMAP-WC
 * @render:         Render
 */

/**
 * SUBTEST: drrs-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbc-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: psr-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-2p-scndscrn-%s-indfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * arg[1]:
 *
 * @cur:            Cursor plane
 * @pri:            Primary plane
 * @spr:            Sprite plane
 *
 * arg[2]:
 *
 * @mmap-cpu:       MMAP-CPU
 * @mmap-gtt:       MMAP-GTT
 * @pwrite:         PWRITE
 */

/**
 * SUBTEST: drrs-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbc-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: psr-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * SUBTEST: fbcpsr-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 *
 * arg[1]:
 *
 * @blt:            Blitter
 * @mmap-wc:        MMAP-WC
 * @render:         Render
 */

/**
 * SUBTEST: drrs-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbc-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: psr-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-2p-scndscrn-pri-shrfb-draw-%s
 * Description: Draw a set of rectangles on the screen using the provided method
 * Driver requirement: i915
 *
 * arg[1]:
 *
 * @mmap-cpu:       MMAP-CPU
 * @mmap-gtt:       MMAP-GTT
 * @pwrite:         PWRITE
 */

/**
 * SUBTEST: drrs-%dp-pri-indfb-multidraw
 * Description: Draw a set of rectangles on the screen using alternated drawing methods
 *
 * SUBTEST: fbc-%dp-pri-indfb-multidraw
 * Description: Draw a set of rectangles on the screen using alternated drawing methods
 *
 * SUBTEST: psr-%dp-pri-indfb-multidraw
 * Description: Draw a set of rectangles on the screen using alternated drawing methods
 *
 * SUBTEST: fbcdrrs-%dp-pri-indfb-multidraw
 * Description: Draw a set of rectangles on the screen using alternated drawing methods
 *
 * SUBTEST: fbcpsr-%dp-pri-indfb-multidraw
 * Description: Draw a set of rectangles on the screen using alternated drawing methods
 *
 * arg[1].values:   1, 2
 */

/**
 * SUBTEST: drrs-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 *
 * SUBTEST: fbc-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 *
 * SUBTEST: psr-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 *
 * SUBTEST: fbcdrrs-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 *
 * SUBTEST: fbcpsr-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 *
 * arg[1]:
 *
 * @rgb101010:      FORMAT_RGB101010
 * @rgb565:         FORMAT_RGB565
 *
 * arg[2]:
 *
 * @blt:            Blitter
 * @mmap-wc:        MMAP-WC
 * @render:         Render
 */

/**
 * SUBTEST: drrs-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 * Driver requirement: i915
 *
 * SUBTEST: fbc-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 * Driver requirement: i915
 *
 * SUBTEST: psr-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-%s-draw-%s
 * Description: Test pixel formats (%arg[1]) that are not FORMAT_DEFAULT
 * Driver requirement: i915
 *
 * arg[1]:
 *
 * @rgb101010:      FORMAT_RGB101010
 * @rgb565:         FORMAT_RGB565
 *
 * arg[2]:
 *
 * @mmap-cpu:       MMAP-CPU
 * @mmap-gtt:       MMAP-GTT
 * @pwrite:         PWRITE
 */

/**
 * SUBTEST: drrs-slowdraw
 * Description: Sleep a little bit between drawing operations with DRRS
 *
 * SUBTEST: psr-slowdraw
 * Description: Sleep a little bit between drawing operations with PSR
 *
 * SUBTEST: fbcdrrs-slowdraw
 * Description: Sleep a little bit between drawing operations with FBC & DRRS
 *
 * SUBTEST: fbcpsr-slowdraw
 * Description: Sleep a little bit between drawing operations with FBC & PSR
 */

/**
 * SUBTEST: drrs-%dp-primscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: fbc-%dp-primscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: psr-%dp-primscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: fbcpsr-%dp-primscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @indfb:          Individual fb
 * @shrfb:          Shared fb
 *
 * arg[3]:
 *
 * @ms:             Modeset
 * @pg:             Page flip
 * @pl:             Plane change
 */

/**
 * SUBTEST: drrs-2p-scndscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: fbc-2p-scndscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: psr-2p-scndscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * SUBTEST: fbcpsr-2p-scndscrn-%s-%sflip-blt
 * Description: Just exercise page flips with the patterns we have
 *
 * arg[1]:
 *
 * @indfb:          Individual fb
 * @shrfb:          Shared fb
 *
 * arg[2]:
 *
 * @ms:             Modeset
 * @pg:             Page flip
 * @pl:             Plane change
 */

/**
 * SUBTEST: fbc-%dp-%s-fliptrack-mmap-gtt
 * Description: Check if the hardware tracking works after page flips
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-%dp-%s-fliptrack-mmap-gtt
 * Description: Check if the hardware tracking works after page flips
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-%dp-%s-fliptrack-mmap-gtt
 * Description: Check if the hardware tracking works after page flips
 * Driver requirement: i915
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @indfb:          Individual fb
 * @shrfb:          Shared fb
 */

/**
 * SUBTEST: drrs-%dp-primscrn-%s-indfb-move
 * Description: Just move the %arg[2] around
 *
 * SUBTEST: fbc-%dp-primscrn-%s-indfb-move
 * Description: Just move the %arg[2] around
 *
 * SUBTEST: psr-%dp-primscrn-%s-indfb-move
 * Description: Just move the %arg[2] around
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-%s-indfb-move
 * Description: Just move the %arg[2] around
 *
 * SUBTEST: fbcpsr-%dp-primscrn-%s-indfb-move
 * Description: Just move the %arg[2] around
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @spr:            Sprite plane
 * @cur:            Cursor plane
 */

/**
 * SUBTEST: drrs-2p-scndscrn-%s-indfb-move
 * Description: Just move the %arg[1] around
 *
 * SUBTEST: fbc-2p-scndscrn-%s-indfb-move
 * Description: Just move the %arg[1] around
 *
 * SUBTEST: psr-2p-scndscrn-%s-indfb-move
 * Description: Just move the %arg[1] around
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-%s-indfb-move
 * Description: Just move the %arg[1] around
 *
 * SUBTEST: fbcpsr-2p-scndscrn-%s-indfb-move
 * Description: Just move the %arg[1] around
 *
 * arg[1]:
 *
 * @spr:            Sprite plane
 * @cur:            Cursor plane
 */

/**
 * SUBTEST: drrs-%dp-primscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[2] a few times
 *
 * SUBTEST: fbc-%dp-primscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[2] a few times
 *
 * SUBTEST: psr-%dp-primscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[2] a few times
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[2] a few times
 *
 * SUBTEST: fbcpsr-%dp-primscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[2] a few times
 *
 * arg[1].values:   1, 2
 *
 * arg[2]:
 *
 * @spr:            Sprite plane
 * @cur:            Cursor plane
 */

/**
 * SUBTEST: drrs-2p-scndscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[1] a few times
 *
 * SUBTEST: fbc-2p-scndscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[1] a few times
 *
 * SUBTEST: psr-2p-scndscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[1] a few times
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[1] a few times
 *
 * SUBTEST: fbcpsr-2p-scndscrn-%s-indfb-onoff
 * Description: Just enable and disable the %arg[1] a few times
 *
 * arg[1]:
 *
 * @spr:            Sprite plane
 * @cur:            Cursor plane
 */

/**
 * SUBTEST: drrs-%dp-primscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: fbc-%dp-primscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: psr-%dp-primscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: fbcdrrs-%dp-primscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: fbcpsr-%dp-primscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: drrs-2p-scndscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: fbc-2p-scndscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: psr-2p-scndscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: fbcdrrs-2p-scndscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * SUBTEST: fbcpsr-2p-scndscrn-spr-indfb-fullscreen
 * Description: Put a fullscreen plane covering the whole screen
 *
 * arg[1].values:   1, 2
 */

/**
 * SUBTEST: drrs-%s-scaledprimary
 * Description: Try different primary plane scaling strategies
 *
 * SUBTEST: fbc-%s-scaledprimary
 * Description: Try different primary plane scaling strategies
 *
 * SUBTEST: psr-%s-scaledprimary
 * Description: Try different primary plane scaling strategies
 *
 * SUBTEST: fbcdrrs-%s-scaledprimary
 * Description: Try different primary plane scaling strategies
 *
 * SUBTEST: fbcpsr-%s-scaledprimary
 * Description: Try different primary plane scaling strategies
 *
 * arg[1]:
 *
 * @indfb:          Individual fb
 * @shrfb:          Shared fb
 */

/**
 * SUBTEST: drrs-modesetfrombusy
 * Description: Modeset from a busy buffer to a non-busy buffer with DRRS
 *
 * SUBTEST: fbc-modesetfrombusy
 * Description: Modeset from a busy buffer to a non-busy buffer with FBC
 *
 * SUBTEST: psr-modesetfrombusy
 * Description: Modeset from a busy buffer to a non-busy buffer with PSR
 *
 * SUBTEST: fbcdrrs-modesetfrombusy
 * Description: Modeset from a busy buffer to a non-busy buffer with FBC & DRRS
 *
 * SUBTEST: fbcpsr-modesetfrombusy
 * Description: Modeset from a busy buffer to a non-busy buffer with FBC & PSR
 */

/**
 * SUBTEST: drrs-suspend
 * Description: Make sure suspend/resume keeps us on the same state of DRRS
 *
 * SUBTEST: fbc-suspend
 * Description: Make sure suspend/resume keeps us on the same state of FBC
 *
 * SUBTEST: psr-suspend
 * Description: Make sure suspend/resume keeps us on the same state of PSR
 *
 * SUBTEST: fbcdrrs-suspend
 * Description: Make sure suspend/resume keeps us on the same state of FBC & DRRS
 *
 * SUBTEST: fbcpsr-suspend
 * Description: Make sure suspend/resume keeps us on the same state of FBC & PSR
 */

/**
 * SUBTEST: drrs-farfromfence-mmap-gtt
 * Description: Test drawing as far from the fence start as possible
 * Driver requirement: i915
 *
 * SUBTEST: fbc-farfromfence-mmap-gtt
 * Description: Test drawing as far from the fence start as possible
 * Driver requirement: i915
 *
 * SUBTEST: psr-farfromfence-mmap-gtt
 * Description: Test drawing as far from the fence start as possible
 * Driver requirement: i915
 *
 * SUBTEST: fbcdrrs-farfromfence-mmap-gtt
 * Description: Test drawing as far from the fence start as possible
 * Driver requirement: i915
 *
 * SUBTEST: fbcpsr-farfromfence-mmap-gtt
 * Description: Test drawing as far from the fence start as possible
 * Driver requirement: i915
 */

/**
 * SUBTEST: fbc-stridechange
 * Description: Change the frontbuffer stride by doing a modeset
 *
 * SUBTEST: fbcdrrs-stridechange
 * Description: Change the frontbuffer stride by doing a modeset
 *
 * SUBTEST: fbcpsr-stridechange
 * Description: Change the frontbuffer stride by doing a modeset
 */

/**
 * SUBTEST: fbc-tiling-%s
 * Description: Test %arg[1] formats, if the tiling format supports FBC do the
 *              basic drawing test, else set the mode & test if FBC is disabled
 *
 * SUBTEST: fbcdrrs-tiling-%s
 * Description: Test %arg[1] formats, if the tiling format supports FBC do the
 *              basic drawing test, else set the mode & test if FBC is disabled
 *
 * SUBTEST: fbcpsr-tiling-%s
 * Description: Test %arg[1] formats, if the tiling format supports FBC do the
 *              basic drawing test, else set the mode & test if FBC is disabled
 *
 * arg[1]:
 *
 * @4:              4 tiling
 * @linear:         Linear tiling
 * @y:              Y tiling
 */

#define TIME SLOW_QUICK(1000, 10000)

IGT_TEST_DESCRIPTION("Test the Kernel's frontbuffer tracking mechanism and "
		     "its related features: FBC, PSR and DRRS");

/*
 * One of the aspects of this test is that, for every subtest, we try different
 * combinations of the parameters defined by the struct below. Because of this,
 * a single addition of a new parameter or subtest function can lead to hundreds
 * of new subtests.
 *
 * In order to reduce the number combinations we cut the cases that don't make
 * sense, such as writing on the secondary screen when there is only a single
 * pipe, or flipping when the target is the offscreen buffer. We also hide some
 * combinations that are somewhat redundant and don't add much value to the
 * test. For example, since we already do the offscreen testing with a single
 * pipe enabled, there's no much value in doing it again with dual pipes. If you
 * still want to try these redundant tests, you need to use the --show-hidden
 * option.
 *
 * The most important hidden thing is the FEATURE_NONE set of tests. Whenever
 * you get a failure on any test, it is important to check whether the same test
 * fails with FEATURE_NONE - replace the feature name for "nop". If the nop test
 * also fails, then it's likely the problem will be on the IGT side instead of
 * the Kernel side. We don't expose this set of tests by default because (i)
 * they take a long time to test; and (ii) if the feature tests work, then it's
 * very likely that the nop tests will also work.
 */
struct test_mode {
	/* Are we going to enable just one monitor, or are we going to setup a
	 * dual screen environment for the test? */
	enum {
		PIPE_SINGLE = 0,
		PIPE_DUAL,
		PIPE_COUNT,
	} pipes;

	/* The primary screen is the one that's supposed to have the "feature"
	 * enabled on, but we have the option to draw on the secondary screen or
	 * on some offscreen buffer. We also only theck the CRC of the primary
	 * screen. */
	enum {
		SCREEN_PRIM = 0,
		SCREEN_SCND,
		SCREEN_OFFSCREEN,
		SCREEN_COUNT,
	} screen;

	/* When we draw, we can draw directly on the primary plane, on the
	 * cursor or on the sprite plane. */
	enum {
		PLANE_PRI = 0,
		PLANE_CUR,
		PLANE_SPR,
		PLANE_COUNT,
	} plane;

	/* We can organize the screens in a way that each screen has its own
	 * framebuffer, or in a way that all screens point to the same
	 * framebuffer, but on different places. This includes the offscreen
	 * screen. */
	enum {
		FBS_INDIVIDUAL = 0,
		FBS_SHARED,
		FBS_COUNT,
	} fbs;

	/* Which features are we going to test now? This is a mask!
	 * FEATURE_DEFAULT is a special value which instruct the test to just
	 * keep what's already enabled by default in the Kernel. */
	enum {
		FEATURE_NONE  = 0,
		FEATURE_FBC   = 1,
		FEATURE_PSR   = 2,
		FEATURE_DRRS  = 4,
		FEATURE_COUNT = 8,
		FEATURE_DEFAULT = 8,
	} feature;

	/* Possible pixel formats. We just use FORMAT_DEFAULT for most tests and
	 * only test a few things on the other formats. */
	enum pixel_format {
		FORMAT_RGB888 = 0,
		FORMAT_RGB565,
		FORMAT_RGB101010,
		FORMAT_COUNT,
		FORMAT_DEFAULT = FORMAT_RGB888,
	} format;

	/* There are multiple APIs where we can do the equivalent of a page flip
	 * and they exercise slightly different codepaths inside the Kernel. */
	enum flip_type {
		FLIP_PAGEFLIP,
		FLIP_MODESET,
		FLIP_PLANES,
		FLIP_COUNT,
	} flip;

	enum tiling_type {
		TILING_LINEAR = 0,
		TILING_X,
		TILING_Y,
		TILING_4,
		TILING_COUNT,
		TILING_AUTOSELECT,
	} tiling;

	enum igt_draw_method method;
};

enum color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE,
	COLOR_MAGENTA,
	COLOR_CYAN,
	COLOR_SCND_BG,
	COLOR_PRIM_BG = COLOR_BLUE,
	COLOR_OFFSCREEN_BG = COLOR_SCND_BG,
};

struct rect {
	int x;
	int y;
	int w;
	int h;
	uint32_t color;
};

struct {
	int fd;
	int debugfs;
	igt_display_t display;

	struct buf_ops *bops;
	uint32_t devid;
	unsigned display_ver;
} drm;

struct {
	bool can_test;

	bool supports_last_action;

	struct timespec last_action;
} fbc = {
	.can_test = false,
	.supports_last_action = false,
};

struct {
	bool can_test;
} psr = {
	.can_test = false,
};

#define MAX_DRRS_STATUS_BUF_LEN 256

struct {
	bool can_test;
} drrs = {
	.can_test = false,
};

igt_pipe_crc_t *pipe_crc;
igt_crc_t *wanted_crc;
struct {
	bool initialized;
	igt_crc_t crc;
} blue_crcs[FORMAT_COUNT];

/* The goal of this structure is to easily allow us to deal with cases where we
 * have a big framebuffer and the CRTC is just displaying a subregion of this
 * big FB. */
struct fb_region {
	igt_plane_t *plane;
	struct igt_fb *fb;
	int x;
	int y;
	int w;
	int h;
};

struct draw_pattern_info {
	bool frames_stack;
	int n_rects;
	struct rect (*get_rect)(struct fb_region *fb, int r);

	bool initialized[FORMAT_COUNT];
	igt_crc_t *crcs[FORMAT_COUNT];
};

/* Draw big rectangles on the screen. */
struct draw_pattern_info pattern1;
/* 64x64 rectangles at x:0,y:0, just so we can draw on the cursor and sprite. */
struct draw_pattern_info pattern2;
/* 64x64 rectangles at different positions, same color, for the move test. */
struct draw_pattern_info pattern3;
/* Just a fullscreen green square. */
struct draw_pattern_info pattern4;

/* Command line parameters. */
struct {
	bool check_status;
	bool check_crc;
	bool fbc_check_compression;
	bool fbc_check_last_action;
	bool no_edp;
	bool small_modes;
	bool show_hidden;
	int step;
	int only_pipes;
	int shared_fb_x_offset;
	int shared_fb_y_offset;
	enum tiling_type tiling;
} opt = {
	.check_status = true,
	.check_crc = true,
	.fbc_check_compression = true,
	.fbc_check_last_action = true,
	.no_edp = false,
	.small_modes = false,
	.show_hidden= false,
	.step = 0,
	.only_pipes = PIPE_COUNT,
	.shared_fb_x_offset = 248,
	.shared_fb_y_offset = 500,
	.tiling = TILING_AUTOSELECT,
};

struct modeset_params {
	enum pipe pipe;
	igt_output_t *output;
	drmModeModeInfo mode;

	struct fb_region primary;
	struct fb_region cursor;
	struct fb_region sprite;
};

struct modeset_params prim_mode_params;
struct modeset_params scnd_mode_params;

struct fb_region offscreen_fb;
struct screen_fbs {
	bool initialized;

	struct igt_fb prim_pri;
	struct igt_fb prim_cur;
	struct igt_fb prim_spr;

	struct igt_fb scnd_pri;
	struct igt_fb scnd_cur;
	struct igt_fb scnd_spr;

	struct igt_fb offscreen;
	struct igt_fb big;
} fbs[FORMAT_COUNT];

struct {
	pthread_t thread;
	bool stop;

	uint32_t handle;
	uint32_t size;
	uint32_t stride;
	int width;
	int height;
	uint32_t color;
	int bpp;
	uint32_t tiling;
} busy_thread = {
	.stop = true,
};

static drmModeModeInfo *get_connector_smallest_mode(igt_output_t *output)
{
	drmModeConnector *c = output->config.connector;
	const drmModeModeInfo *smallest = NULL;
	int i;

	if (c->connector_type == DRM_MODE_CONNECTOR_eDP)
		return igt_std_1024_mode_get(igt_output_preferred_vrefresh(output));

	for (i = 0; i < c->count_modes; i++) {
		const drmModeModeInfo *mode = &c->modes[i];

		if (!smallest ||
		    mode->hdisplay * mode->vdisplay <
		    smallest->hdisplay * smallest->vdisplay)
			smallest = mode;
	}

	if (smallest)
		return igt_memdup(smallest, sizeof(*smallest));
	else
		return igt_std_1024_mode_get(60);
}

static drmModeModeInfo *connector_get_mode(igt_output_t *output)
{
	/* On HSW the CRC WA is so awful that it makes you think everything is
	  * bugged. */
	if (IS_HASWELL(intel_get_drm_devid(drm.fd)) &&
	    output->config.connector->connector_type == DRM_MODE_CONNECTOR_eDP)
		return igt_std_1024_mode_get(igt_output_preferred_vrefresh(output));

	if (opt.small_modes)
		return get_connector_smallest_mode(output);
	else
		return igt_memdup(&output->config.default_mode,
				  sizeof(output->config.default_mode));
}

static void init_mode_params(struct modeset_params *params,
			     igt_output_t *output, enum pipe pipe)
{
	int i;
	drmModeModeInfo *mode;

	igt_output_override_mode(output, NULL);
	mode = connector_get_mode(output);

	params->pipe = pipe;
	params->output = output;
	params->mode = *mode;

	params->primary.plane = igt_pipe_get_plane_type(&drm.display.pipes[pipe], DRM_PLANE_TYPE_PRIMARY);
	params->primary.fb = NULL;
	params->primary.x = 0;
	params->primary.y = 0;
	params->primary.w = mode->hdisplay;
	params->primary.h = mode->vdisplay;

	params->cursor.plane = igt_pipe_get_plane_type(&drm.display.pipes[pipe], DRM_PLANE_TYPE_CURSOR);
	params->cursor.fb = NULL;
	params->cursor.x = 0;
	params->cursor.y = 0;
	params->cursor.w = 64;
	params->cursor.h = 64;

	params->sprite.plane = igt_pipe_get_plane_type(&drm.display.pipes[pipe], DRM_PLANE_TYPE_OVERLAY);
	igt_require(params->sprite.plane);
	params->sprite.fb = NULL;
	params->sprite.x = 0;
	params->sprite.y = 0;
	params->sprite.w = 64;
	params->sprite.h = 64;

	/* If we endup changing the primary mode parameters, we need to
	 * invalidate any existing cached stuff from a previous configuration. */
	if (params == &prim_mode_params) {
		if (pipe_crc) {
			igt_pipe_crc_free(pipe_crc);
			pipe_crc = NULL;
		}

		for (i = 0; i < FORMAT_COUNT; i++)
			blue_crcs[i].initialized = false;
	}

	free(mode);
}

static bool find_connector(bool edp_only, bool pipe_a,
			   igt_output_t *forbidden_output,
			   enum pipe forbidden_pipe,
			   igt_output_t **ret_output,
			   enum pipe *ret_pipe)
{
	igt_output_t *output;
	enum pipe pipe;

	for_each_pipe_with_valid_output(&drm.display, pipe, output) {
		drmModeConnectorPtr c = output->config.connector;

		if (edp_only && c->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		if (pipe_a && pipe != PIPE_A)
			continue;

		if (output == forbidden_output || pipe == forbidden_pipe) {
			igt_output_set_pipe(output, pipe);
			igt_output_override_mode(output, connector_get_mode(output));

			continue;
		}

		if (c->connector_type == DRM_MODE_CONNECTOR_eDP && opt.no_edp)
			continue;

		igt_output_set_pipe(output, pipe);
		igt_output_override_mode(output, connector_get_mode(output));
		if (intel_pipe_output_combo_valid(&drm.display)) {
			*ret_output = output;
			*ret_pipe = pipe;
			return true;
		}
	}

	return false;
}

static bool init_modeset_cached_params(void)
{
	igt_output_t *prim_output = NULL, *scnd_output = NULL;
	enum pipe prim_pipe, scnd_pipe;

	/*
	 * We have this problem where PSR is only present on eDP monitors and
	 * FBC is only present on pipe A for some platforms. So we search first
	 * for the ideal case of eDP supporting pipe A, and try the less optimal
	 * configs later, sacrificing  one of the features.
	 * TODO: refactor the code in a way that allows us to have different
	 * sets of prim/scnd structs for different features.
	 */
	find_connector(true, true, NULL, PIPE_NONE, &prim_output, &prim_pipe);
	if (!prim_output)
		find_connector(true, false, NULL, PIPE_NONE, &prim_output, &prim_pipe);
	if (!prim_output)
		find_connector(false, true, NULL, PIPE_NONE, &prim_output, &prim_pipe);
	if (!prim_output)
		find_connector(false, false, NULL, PIPE_NONE, &prim_output, &prim_pipe);

	if (!prim_output)
		return false;

	find_connector(false, false, prim_output, prim_pipe,
		       &scnd_output, &scnd_pipe);

	init_mode_params(&prim_mode_params, prim_output, prim_pipe);

	if (!scnd_output) {
		scnd_mode_params.pipe = PIPE_NONE;
		scnd_mode_params.output = NULL;
		return true;
	}

	init_mode_params(&scnd_mode_params, scnd_output, scnd_pipe);
	return true;
}

static uint64_t tiling_to_modifier(enum tiling_type tiling)
{
	switch (tiling) {
	case TILING_LINEAR:
		return DRM_FORMAT_MOD_LINEAR;
	case TILING_X:
		return I915_FORMAT_MOD_X_TILED;
	case TILING_Y:
		return I915_FORMAT_MOD_Y_TILED;
	case TILING_4:
		return I915_FORMAT_MOD_4_TILED;
	default:
		igt_assert(false);
	}
}

static void create_fb(enum pixel_format pformat, int width, int height,
		      enum tiling_type tiling, int plane, struct igt_fb *fb)
{
	uint32_t format;
	uint64_t modifier;

	switch (pformat) {
	case FORMAT_RGB888:
		if (plane == PLANE_CUR)
			format = DRM_FORMAT_ARGB8888;
		else
			format = DRM_FORMAT_XRGB8888;
		break;
	case FORMAT_RGB565:
		/* Only the primary plane supports 16bpp! */
		if (plane == PLANE_PRI)
			format = DRM_FORMAT_RGB565;
		else if (plane == PLANE_CUR)
			format = DRM_FORMAT_ARGB8888;
		else
			format = DRM_FORMAT_XRGB8888;
		break;
	case FORMAT_RGB101010:
		if (plane == PLANE_PRI)
			format = DRM_FORMAT_XRGB2101010;
		else if (plane == PLANE_CUR)
			format = DRM_FORMAT_ARGB8888;
		else
			format = DRM_FORMAT_XRGB8888;
		break;
	default:
		igt_assert(false);
	}

	modifier = tiling_to_modifier(tiling);

	igt_warn_on(plane == PLANE_CUR && tiling != TILING_LINEAR);

	igt_create_fb(drm.fd, width, height, format, modifier, fb);
}

static uint32_t pick_color(struct igt_fb *fb, enum color ecolor)
{
	uint32_t color, r, g, b, b2, a;
	bool alpha = false;

	switch (fb->drm_format) {
	case DRM_FORMAT_RGB565:
		a =  0x0;
		r =  0x1F << 11;
		g =  0x3F << 5;
		b =  0x1F;
		b2 = 0x10;
		break;
	case DRM_FORMAT_ARGB8888:
		alpha = true;
	case DRM_FORMAT_XRGB8888:
		a =  0xFF << 24;
		r =  0xFF << 16;
		g =  0xFF << 8;
		b =  0xFF;
		b2 = 0x80;
		break;
	case DRM_FORMAT_ARGB2101010:
		alpha = true;
	case DRM_FORMAT_XRGB2101010:
		a = 0x3 << 30;
		r = 0x3FF << 20;
		g = 0x3FF << 10;
		b = 0x3FF;
		b2 = 0x200;
		break;
	default:
		igt_assert(false);
	}

	switch (ecolor) {
	case COLOR_RED:
		color = r;
		break;
	case COLOR_GREEN:
		color = g;
		break;
	case COLOR_BLUE:
		color = b;
		break;
	case COLOR_MAGENTA:
		color = r | b;
		break;
	case COLOR_CYAN:
		color = g | b;
		break;
	case COLOR_SCND_BG:
		color = b2;
		break;
	default:
		igt_assert(false);
	}

	if (alpha)
		color |= a;

	return color;
}

static void fill_fb(struct igt_fb *fb, enum color ecolor)
{
	igt_draw_fill_fb(drm.fd, fb, pick_color(fb, ecolor));
}

/*
 * This is how the prim, scnd and offscreen FBs should be positioned inside the
 * shared FB. The prim buffer starts at the X and Y offsets defined by
 * opt.shared_fb_{x,y}_offset, then scnd starts at the same X pixel offset,
 * right after prim ends on the Y axis, then the offscreen fb starts after scnd
 * ends. Just like the picture:
 *
 * +-------------------------+
 * | shared fb               |
 * |   +------------------+  |
 * |   | prim             |  |
 * |   |                  |  |
 * |   |                  |  |
 * |   |                  |  |
 * |   +------------------+--+
 * |   | scnd                |
 * |   |                     |
 * |   |                     |
 * |   +---------------+-----+
 * |   | offscreen     |     |
 * |   |               |     |
 * |   |               |     |
 * +---+---------------+-----+
 *
 * We do it vertically instead of the more common horizontal case in order to
 * avoid super huge strides not supported by FBC.
 */
static void create_shared_fb(enum pixel_format format, enum tiling_type tiling)
{
	int prim_w, prim_h, scnd_w, scnd_h, offs_w, offs_h, big_w, big_h;
	struct screen_fbs *s = &fbs[format];

	prim_w = prim_mode_params.mode.hdisplay;
	prim_h = prim_mode_params.mode.vdisplay;

	if (scnd_mode_params.output) {
		scnd_w = scnd_mode_params.mode.hdisplay;
		scnd_h = scnd_mode_params.mode.vdisplay;
	} else {
		scnd_w = 0;
		scnd_h = 0;
	}
	offs_w = offscreen_fb.w;
	offs_h = offscreen_fb.h;

	big_w = prim_w;
	if (scnd_w > big_w)
		big_w = scnd_w;
	if (offs_w > big_w)
		big_w = offs_w;
	big_w += opt.shared_fb_x_offset;

	big_h = prim_h + scnd_h + offs_h + opt.shared_fb_y_offset;

	create_fb(format, big_w, big_h, tiling, PLANE_PRI, &s->big);
}

static void destroy_fbs(enum pixel_format format)
{
	struct screen_fbs *s = &fbs[format];

	if (!s->initialized)
		return;

	if (scnd_mode_params.output) {
		igt_remove_fb(drm.fd, &s->scnd_pri);
		igt_remove_fb(drm.fd, &s->scnd_cur);
		igt_remove_fb(drm.fd, &s->scnd_spr);
	}
	igt_remove_fb(drm.fd, &s->prim_pri);
	igt_remove_fb(drm.fd, &s->prim_cur);
	igt_remove_fb(drm.fd, &s->prim_spr);
	igt_remove_fb(drm.fd, &s->offscreen);
	igt_remove_fb(drm.fd, &s->big);
}

static void create_fbs(enum pixel_format format, enum tiling_type tiling)
{
	struct screen_fbs *s = &fbs[format];

	if (s->initialized)
		destroy_fbs(format);

	s->initialized = true;

	create_fb(format, prim_mode_params.mode.hdisplay,
		  prim_mode_params.mode.vdisplay, tiling, PLANE_PRI,
		  &s->prim_pri);
	create_fb(format, prim_mode_params.cursor.w,
		  prim_mode_params.cursor.h, DRM_FORMAT_MOD_LINEAR,
		  PLANE_CUR, &s->prim_cur);
	create_fb(format, prim_mode_params.sprite.w,
		  prim_mode_params.sprite.h, tiling, PLANE_SPR, &s->prim_spr);

	create_fb(format, offscreen_fb.w, offscreen_fb.h, tiling, PLANE_PRI,
		  &s->offscreen);

	create_shared_fb(format, tiling);

	if (!scnd_mode_params.output)
		return;

	create_fb(format, scnd_mode_params.mode.hdisplay,
		  scnd_mode_params.mode.vdisplay, tiling, PLANE_PRI,
		  &s->scnd_pri);
	create_fb(format, scnd_mode_params.cursor.w, scnd_mode_params.cursor.h,
		  DRM_FORMAT_MOD_LINEAR, PLANE_CUR, &s->scnd_cur);
	create_fb(format, scnd_mode_params.sprite.w, scnd_mode_params.sprite.h,
		  tiling, PLANE_SPR, &s->scnd_spr);
}

static void __set_prim_plane_for_params(struct modeset_params *params)
{
	igt_plane_set_fb(params->primary.plane, params->primary.fb);
	igt_plane_set_position(params->primary.plane, 0, 0);
	igt_plane_set_size(params->primary.plane, params->mode.hdisplay, params->mode.vdisplay);
	igt_fb_set_position(params->primary.fb, params->primary.plane,
			    params->primary.x, params->primary.y);
	igt_fb_set_size(params->primary.fb, params->primary.plane,
			params->mode.hdisplay, params->mode.vdisplay);
}

static void __set_mode_for_params(struct modeset_params *params)
{
	igt_output_override_mode(params->output, &params->mode);
	igt_output_set_pipe(params->output, params->pipe);

	__set_prim_plane_for_params(params);
}

static void set_mode_for_params(struct modeset_params *params)
{
	__set_mode_for_params(params);
	igt_display_commit(&drm.display);
}

static void __debugfs_read_crtc(const char *param, char *buf, int len)
{
	int dir;
	enum pipe pipe;

	pipe = prim_mode_params.pipe;
	dir = igt_debugfs_pipe_dir(drm.fd, pipe, O_DIRECTORY);
	igt_require_fd(dir);
	igt_debugfs_simple_read(dir, param, buf, len);
	close(dir);
}

#define debugfs_read_crtc(p, arr) __debugfs_read_crtc(p, arr, sizeof(arr))

static bool is_drrs_high(void)
{
	char buf[MAX_DRRS_STATUS_BUF_LEN];

	debugfs_read_crtc("i915_drrs_status", buf);
	return strstr(buf, "DRRS refresh rate: high");
}

static bool is_drrs_low(void)
{
	char buf[MAX_DRRS_STATUS_BUF_LEN];

	debugfs_read_crtc("i915_drrs_status", buf);
	return strstr(buf, "DRRS refresh rate: low");
}

static void drrs_print_status(void)
{
	char buf[MAX_DRRS_STATUS_BUF_LEN];

	debugfs_read_crtc("i915_drrs_status", buf);
	igt_info("DRRS STATUS :\n%s\n", buf);
}

static struct timespec fbc_get_last_action(void)
{
	struct timespec ret = { 0, 0 };
	char buf[128];
	char *action;
	ssize_t n_read;


	debugfs_read_crtc("i915_fbc_status", buf);
	action = strstr(buf, "\nLast action:");
	igt_assert(action);

	n_read = sscanf(action, "Last action: %ld.%ld",
			&ret.tv_sec, &ret.tv_nsec);
	igt_assert(n_read == 2);

	return ret;
}

static bool fbc_last_action_changed(void)
{
	struct timespec t_new, t_old;

	t_old = fbc.last_action;
	t_new = fbc_get_last_action();

	fbc.last_action = t_new;

#if 0
	igt_info("old: %ld.%ld\n", t_old.tv_sec, t_old.tv_nsec);
	igt_info("new: %ld.%ld\n", t_new.tv_sec, t_new.tv_nsec);
#endif

	return t_old.tv_sec != t_new.tv_sec ||
	       t_old.tv_nsec != t_new.tv_nsec;
}

static void fbc_update_last_action(void)
{
	if (!fbc.supports_last_action)
		return;

	fbc.last_action = fbc_get_last_action();

#if 0
	igt_info("Last action: %ld.%ld\n",
		 fbc.last_action.tv_sec, fbc.last_action.tv_nsec);
#endif
}

static void fbc_setup_last_action(void)
{
	ssize_t n_read;
	char buf[128];
	char *action;


	debugfs_read_crtc("i915_fbc_status", buf);
	action = strstr(buf, "\nLast action:");
	if (!action) {
		igt_info("FBC last action not supported\n");
		return;
	}

	fbc.supports_last_action = true;

	n_read = sscanf(action, "Last action: %ld.%ld",
			&fbc.last_action.tv_sec, &fbc.last_action.tv_nsec);
	igt_assert(n_read == 2);
}

static bool fbc_is_compressing(void)
{
	char buf[128];

	debugfs_read_crtc("i915_fbc_status", buf);
	return strstr(buf, "\nCompressing: yes\n") != NULL;
}

static bool fbc_wait_for_compression(void)
{
	return igt_wait(fbc_is_compressing(), 2000, 1);
}

static bool fbc_not_enough_stolen(void)
{
	char buf[128];

	debugfs_read_crtc("i915_fbc_status", buf);
	return strstr(buf, "FBC disabled: not enough stolen memory\n");
}

static bool fbc_stride_not_supported(void)
{
	char buf[128];

	debugfs_read_crtc("i915_fbc_status", buf);
	return strstr(buf, "FBC disabled: framebuffer stride not supported\n");
}

static bool fbc_mode_too_large(void)
{
	char buf[128];

	debugfs_read_crtc("i915_fbc_status", buf);
	return strstr(buf, "FBC disabled: mode too large for compression\n");
}

static bool fbc_psr_not_possible(void)
{
	char buf[128];

	debugfs_read_crtc("i915_fbc_status", buf);
	return strstr(buf, "FBC disabled: PSR1 enabled (Wa_14016291713)");
}

static bool fbc_enable_per_plane(int plane_index, enum pipe pipe)
{
	char buf[PATH_MAX];
	char buf_plane[128];

	sprintf(buf_plane, "%d%s", plane_index, kmstest_pipe_name(pipe));

	debugfs_read_crtc("i915_fbc_status", buf);
	return strstr(strstr(buf, "*"), buf_plane);
}

static bool drrs_wait_until_rr_switch_to_low(void)
{
	return igt_wait(is_drrs_low(), 5000, 1);
}

static struct rect pat1_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	switch (r) {
	case 0:
		rect.x = 0;
		rect.y = 0;
		rect.w = fb->w / 8;
		rect.h = fb->h / 8;
		rect.color = pick_color(fb->fb, COLOR_GREEN);
		break;
	case 1:
		rect.x = fb->w / 8 * 4;
		rect.y = fb->h / 8 * 4;
		rect.w = fb->w / 8 * 2;
		rect.h = fb->h / 8 * 2;
		rect.color = pick_color(fb->fb, COLOR_RED);
		break;
	case 2:
		rect.x = fb->w / 16 + 1;
		rect.y = fb->h / 16 + 1;
		rect.w = fb->w / 8 + 1;
		rect.h = fb->h / 8 + 1;
		rect.color = pick_color(fb->fb, COLOR_MAGENTA);
		break;
	case 3:
		rect.x = fb->w - 1;
		rect.y = fb->h - 1;
		rect.w = 1;
		rect.h = 1;
		rect.color = pick_color(fb->fb, COLOR_CYAN);
		break;
	default:
		igt_assert(false);
	}

	return rect;
}

static struct rect pat2_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	rect.x = 0;
	rect.y = 0;
	rect.w = 64;
	rect.h = 64;

	switch (r) {
	case 0:
		rect.color = pick_color(fb->fb, COLOR_GREEN);
		break;
	case 1:
		rect.x = 31;
		rect.y = 31;
		rect.w = 31;
		rect.h = 31;
		rect.color = pick_color(fb->fb, COLOR_RED);
		break;
	case 2:
		rect.x = 16;
		rect.y = 16;
		rect.w = 32;
		rect.h = 32;
		rect.color = pick_color(fb->fb, COLOR_MAGENTA);
		break;
	case 3:
		rect.color = pick_color(fb->fb, COLOR_CYAN);
		break;
	default:
		igt_assert(false);
	}

	return rect;
}

static struct rect pat3_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	rect.w = 64;
	rect.h = 64;
	rect.color = pick_color(fb->fb, COLOR_GREEN);

	switch (r) {
	case 0:
		rect.x = 0;
		rect.y = 0;
		break;
	case 1:
		rect.x = 64;
		rect.y = 64;
		break;
	case 2:
		rect.x = 1;
		rect.y = 1;
		break;
	case 3:
		rect.x = fb->w - 64;
		rect.y = fb->h - 64;
		break;
	case 4:
		rect.x = fb->w / 2 - 32;
		rect.y = fb->h / 2 - 32;
		break;
	default:
		igt_assert(false);
	}

	return rect;
}

static struct rect pat4_get_rect(struct fb_region *fb, int r)
{
	struct rect rect;

	igt_assert_eq(r, 0);

	rect.x = 0;
	rect.y = 0;
	rect.w = fb->w;
	rect.h = fb->h;
	rect.color = pick_color(fb->fb, COLOR_GREEN);

	return rect;
}

static void fb_dirty_ioctl(struct fb_region *fb, struct rect *rect)
{
	int rc;
	drmModeClip clip = {
		.x1 = rect->x,
		.x2 = rect->x + rect->w,
		.y1 = rect->y,
		.y2 = rect->y + rect->h,
	};

	rc = drmModeDirtyFB(drm.fd, fb->fb->fb_id, &clip, 1);

	igt_assert(rc == 0 || rc == -ENOSYS);
}

static void draw_rect(struct draw_pattern_info *pattern, struct fb_region *fb,
		      enum igt_draw_method method, int r)
{
	struct rect rect = pattern->get_rect(fb, r);

	igt_draw_rect_fb(drm.fd, drm.bops, 0, fb->fb, method,
			 fb->x + rect.x, fb->y + rect.y,
			 rect.w, rect.h, rect.color);

	fb_dirty_ioctl(fb, &rect);
}

static void draw_rect_igt_fb(struct draw_pattern_info *pattern,
			     struct igt_fb *fb, enum igt_draw_method method,
			     int r)
{
	struct fb_region region = {
		.fb = fb,
		.x = 0,
		.y = 0,
		.w = fb->width,
		.h = fb->height,
	};

	draw_rect(pattern, &region, method, r);
}

static void fill_fb_region(struct fb_region *region,
			   enum igt_draw_method method,
			   enum color ecolor)
{
	uint32_t color = pick_color(region->fb, ecolor);

	igt_draw_rect_fb(drm.fd, drm.bops, 0, region->fb, method,
			 region->x, region->y, region->w, region->h,
			 color);
}

static void _fb_dirty_ioctl(struct fb_region *region)
{
	struct rect rect;

	rect.x = region->x;
	rect.y = region->y;
	rect.w = region->w;
	rect.h = region->h;
	fb_dirty_ioctl(region, &rect);
}

static void unset_all_crtcs(void)
{
	igt_display_reset(&drm.display);
	igt_display_commit(&drm.display);
}

static bool disable_features(const struct test_mode *t)
{
	if (t->feature == FEATURE_DEFAULT)
		return false;

	intel_fbc_disable(drm.fd);
	intel_drrs_disable(drm.fd, prim_mode_params.pipe);

	return psr.can_test ? psr_disable(drm.fd, drm.debugfs, NULL) : false;
}

static void *busy_thread_func(void *data)
{
	while (!busy_thread.stop)
		igt_draw_rect(drm.fd, drm.bops, 0, busy_thread.handle,
			      busy_thread.size, busy_thread.stride,
			      busy_thread.width, busy_thread.height,
			      busy_thread.tiling, IGT_DRAW_BLT, 0, 0,
			      busy_thread.width, busy_thread.height,
			      busy_thread.color, busy_thread.bpp);

	pthread_exit(0);
}

static void start_busy_thread(struct igt_fb *fb)
{
	int rc;

	igt_assert(busy_thread.stop == true);
	busy_thread.stop = false;
	busy_thread.handle = fb->gem_handle;
	busy_thread.size = fb->size;
	busy_thread.stride = fb->strides[0];
	busy_thread.width = fb->width;
	busy_thread.height = fb->height;
	busy_thread.color = pick_color(fb, COLOR_PRIM_BG);
	busy_thread.bpp = igt_drm_format_to_bpp(fb->drm_format);
	busy_thread.tiling = igt_fb_mod_to_tiling(fb->modifier);

	rc = pthread_create(&busy_thread.thread, NULL, busy_thread_func, NULL);
	igt_assert_eq(rc, 0);
}

static void stop_busy_thread(void)
{
	if (!busy_thread.stop) {
		busy_thread.stop = true;
		igt_assert(pthread_join(busy_thread.thread, NULL) == 0);
	}
}

static void print_crc(const char *str, igt_crc_t *crc)
{
	char *pipe_str;

	pipe_str = igt_crc_to_string(crc);

	igt_debug("%s pipe:[%s]\n", str, pipe_str);

	free(pipe_str);
}

static void collect_crc(igt_crc_t *crc)
{
	igt_pipe_crc_collect_crc(pipe_crc, crc);
}

static void init_blue_crc(enum pixel_format format, enum tiling_type tiling)
{
	struct igt_fb blue;

	if (blue_crcs[format].initialized)
		return;

	create_fb(format, prim_mode_params.mode.hdisplay,
		  prim_mode_params.mode.vdisplay, tiling, PLANE_PRI,
		  &blue);

	fill_fb(&blue, COLOR_PRIM_BG);

	igt_output_set_pipe(prim_mode_params.output, prim_mode_params.pipe);
	igt_output_override_mode(prim_mode_params.output, &prim_mode_params.mode);
	igt_plane_set_fb(prim_mode_params.primary.plane, &blue);
	igt_display_commit(&drm.display);

	if (!pipe_crc) {
		pipe_crc = igt_pipe_crc_new(drm.fd, prim_mode_params.pipe,
					    IGT_PIPE_CRC_SOURCE_AUTO);
		igt_assert(pipe_crc);
	}

	collect_crc(&blue_crcs[format].crc);

	print_crc("Blue CRC:  ", &blue_crcs[format].crc);

	igt_display_reset(&drm.display);

	igt_remove_fb(drm.fd, &blue);

	blue_crcs[format].initialized = true;
}

static void init_crcs(enum pixel_format format, enum tiling_type tiling,
		      struct draw_pattern_info *pattern)
{
	int r, r_;
	struct igt_fb tmp_fbs[pattern->n_rects];

	if (pattern->initialized[format])
		return;

	pattern->crcs[format] = calloc(pattern->n_rects,
				       sizeof(*(pattern->crcs[format])));

	for (r = 0; r < pattern->n_rects; r++)
		create_fb(format, prim_mode_params.mode.hdisplay,
			  prim_mode_params.mode.vdisplay, tiling,
			  PLANE_PRI, &tmp_fbs[r]);

	for (r = 0; r < pattern->n_rects; r++)
		fill_fb(&tmp_fbs[r], COLOR_PRIM_BG);

	if (pattern->frames_stack) {
		for (r = 0; r < pattern->n_rects; r++)
			for (r_ = 0; r_ <= r; r_++)
				draw_rect_igt_fb(pattern, &tmp_fbs[r],
						 igt_draw_supports_method(drm.fd, IGT_DRAW_PWRITE) ?
						 IGT_DRAW_PWRITE : IGT_DRAW_BLT,
						 r_);
	} else {
		for (r = 0; r < pattern->n_rects; r++)
			draw_rect_igt_fb(pattern, &tmp_fbs[r], igt_draw_supports_method(drm.fd, IGT_DRAW_PWRITE) ?
					 IGT_DRAW_PWRITE : IGT_DRAW_BLT, r);
	}

	igt_output_set_pipe(prim_mode_params.output, prim_mode_params.pipe);
	igt_output_override_mode(prim_mode_params.output, &prim_mode_params.mode);
	for (r = 0; r < pattern->n_rects; r++) {
		igt_plane_set_fb(prim_mode_params.primary.plane, &tmp_fbs[r]);
		igt_display_commit(&drm.display);

		collect_crc(&pattern->crcs[format][r]);
	}

	for (r = 0; r < pattern->n_rects; r++) {
		igt_debug("Rect %d CRC:", r);
		print_crc("", &pattern->crcs[format][r]);
	}

	igt_display_reset(&drm.display);

	for (r = 0; r < pattern->n_rects; r++)
		igt_remove_fb(drm.fd, &tmp_fbs[r]);

	pattern->initialized[format] = true;
}

static void setup_drm(void)
{
	drm.fd = drm_open_driver_master(DRIVER_INTEL | DRIVER_XE);
	drm.debugfs = igt_debugfs_dir(drm.fd);

	kmstest_set_vt_graphics_mode();
	igt_display_require(&drm.display, drm.fd);

	drm.bops = buf_ops_create(drm.fd);
}

static void teardown_drm(void)
{
	buf_ops_destroy(drm.bops);
	igt_display_fini(&drm.display);
	drm_close_driver(drm.fd);
}

static void setup_modeset(void)
{
	igt_require(init_modeset_cached_params());
	offscreen_fb.fb = NULL;
	offscreen_fb.w = 1024;
	offscreen_fb.h = 1024;

	create_fbs(FORMAT_DEFAULT, opt.tiling);
}

static void teardown_modeset(void)
{
	enum pixel_format f;

	for (f = 0; f < FORMAT_COUNT; f++)
		destroy_fbs(f);
}

static void setup_crcs(void)
{
	enum pixel_format f;

	for (f = 0; f < FORMAT_COUNT; f++)
		blue_crcs[f].initialized = false;

	pattern1.frames_stack = true;
	pattern1.n_rects = 4;
	pattern1.get_rect = pat1_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern1.initialized[f] = false;
		pattern1.crcs[f] = NULL;
	}

	pattern2.frames_stack = true;
	pattern2.n_rects = 4;
	pattern2.get_rect = pat2_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern2.initialized[f] = false;
		pattern2.crcs[f] = NULL;
	}

	pattern3.frames_stack = false;
	pattern3.n_rects = 5;
	pattern3.get_rect = pat3_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern3.initialized[f] = false;
		pattern3.crcs[f] = NULL;
	}

	pattern4.frames_stack = false;
	pattern4.n_rects = 1;
	pattern4.get_rect = pat4_get_rect;
	for (f = 0; f < FORMAT_COUNT; f++) {
		pattern4.initialized[f] = false;
		pattern4.crcs[f] = NULL;
	}
}

static void teardown_crcs(void)
{
	enum pixel_format f;

	for (f = 0; f < FORMAT_COUNT; f++) {
		if (pattern1.crcs[f])
			free(pattern1.crcs[f]);
		if (pattern2.crcs[f])
			free(pattern2.crcs[f]);
		if (pattern3.crcs[f])
			free(pattern3.crcs[f]);
		if (pattern4.crcs[f])
			free(pattern4.crcs[f]);
	}

	igt_pipe_crc_free(pipe_crc);
}

static void setup_fbc(void)
{
	if (!intel_fbc_supported_on_chipset(drm.fd, prim_mode_params.pipe)) {
		igt_info("Can't test FBC: not supported on this chipset\n");
		return;
	}

	fbc.can_test = true;

	fbc_setup_last_action();
}

static void teardown_fbc(void)
{
}

static void setup_psr(void)
{
	if (prim_mode_params.output->config.connector->connector_type !=
	    DRM_MODE_CONNECTOR_eDP) {
		igt_info("Can't test PSR: no usable eDP screen.\n");
		return;
	}

	if (!psr_sink_support(drm.fd, drm.debugfs, PSR_MODE_1, NULL)) {
		igt_info("Can't test PSR: not supported by sink.\n");
		return;
	}
	psr.can_test = true;
}

static void teardown_psr(void)
{
}

static void setup_drrs(void)
{
	if (!intel_output_has_drrs(drm.fd, prim_mode_params.output)) {
		igt_info("Can't test DRRS: no usable screen.\n");
		return;
	}

	if (!intel_is_drrs_supported(drm.fd, prim_mode_params.pipe)) {
		igt_info("Can't test DRRS: Not supported.\n");
		return;
	}

	drrs.can_test = true;
}

static void setup_environment(void)
{
	setup_modeset();

	setup_fbc();
	setup_psr();
	setup_drrs();

	setup_crcs();
}

static void teardown_environment(void)
{
	stop_busy_thread();

	teardown_crcs();
	teardown_psr();
	teardown_fbc();
	teardown_modeset();
	teardown_drm();
}

static void wait_user(int step, const char *msg)
{
	if (opt.step < step)
		return;

	igt_info("%s Press enter...\n", msg);
	while (getchar() != '\n')
		;
}

static struct modeset_params *pick_params(const struct test_mode *t)
{
	switch (t->screen) {
	case SCREEN_PRIM:
		return &prim_mode_params;
	case SCREEN_SCND:
		return &scnd_mode_params;
	case SCREEN_OFFSCREEN:
		return NULL;
	default:
		igt_assert(false);
	}
}

static struct fb_region *pick_target(const struct test_mode *t,
				     struct modeset_params *params)
{
	if (!params)
		return &offscreen_fb;

	switch (t->plane) {
	case PLANE_PRI:
		return &params->primary;
	case PLANE_CUR:
		return &params->cursor;
	case PLANE_SPR:
		return &params->sprite;
	default:
		igt_assert(false);
	}
}

static void do_flush(const struct test_mode *t)
{
	struct modeset_params *params = pick_params(t);
	struct fb_region *target = pick_target(t, params);

	if (is_i915_device(drm.fd))
		gem_set_domain(drm.fd, target->fb->gem_handle, I915_GEM_DOMAIN_GTT, 0);
}

#define DONT_ASSERT_CRC			(1 << 0)
#define DONT_ASSERT_FEATURE_STATUS	(1 << 1)
#define DONT_ASSERT_FBC_STATUS		(1 << 12)

#define FBC_ASSERT_FLAGS		(0xF << 2)
#define ASSERT_FBC_ENABLED		(1 << 2)
#define ASSERT_FBC_DISABLED		(1 << 3)
#define ASSERT_LAST_ACTION_CHANGED	(1 << 4)
#define ASSERT_NO_ACTION_CHANGE		(1 << 5)

#define PSR_ASSERT_FLAGS		(3 << 6)
#define ASSERT_PSR_ENABLED		(1 << 6)
#define ASSERT_PSR_DISABLED		(1 << 7)

#define DRRS_ASSERT_FLAGS		(7 << 8)
#define ASSERT_DRRS_HIGH		(1 << 8)
#define ASSERT_DRRS_LOW			(1 << 9)
#define ASSERT_DRRS_INACTIVE		(1 << 10)

#define ASSERT_NO_IDLE_GPU		(1 << 11)

static int adjust_assertion_flags(const struct test_mode *t, int flags)
{
	if (!(flags & DONT_ASSERT_FEATURE_STATUS)) {
		if (!(flags & ASSERT_FBC_DISABLED))
			flags |= ASSERT_FBC_ENABLED;
		if (!(flags & ASSERT_PSR_DISABLED))
			flags |= ASSERT_PSR_ENABLED;
		if (!((flags & ASSERT_DRRS_LOW) ||
		    (flags & ASSERT_DRRS_INACTIVE)))
			flags |= ASSERT_DRRS_HIGH;
	}

	if ((t->feature & FEATURE_FBC) == 0 || (flags & DONT_ASSERT_FBC_STATUS))
		flags &= ~FBC_ASSERT_FLAGS;
	if ((t->feature & FEATURE_PSR) == 0)
		flags &= ~PSR_ASSERT_FLAGS;
	if ((t->feature & FEATURE_DRRS) == 0)
		flags &= ~DRRS_ASSERT_FLAGS;

	return flags;
}

static void do_crc_assertions(int flags)
{
	igt_crc_t crc;

	if (!opt.check_crc || (flags & DONT_ASSERT_CRC))
		return;

	collect_crc(&crc);
	print_crc("Calculated CRC:", &crc);

	igt_assert(wanted_crc);
	igt_assert_crc_equal(&crc, wanted_crc);
}

static void do_status_assertions(int flags)
{
	if (!opt.check_status) {
		/* Make sure we settle before continuing. */
		sleep(1);
		return;
	}

	if (flags & ASSERT_DRRS_HIGH) {
		if (!is_drrs_high()) {
			drrs_print_status();
			igt_assert_f(false, "DRRS HIGH\n");
		}
	} else if (flags & ASSERT_DRRS_LOW) {
		if (!drrs_wait_until_rr_switch_to_low()) {
			drrs_print_status();
			igt_assert_f(false, "DRRS LOW\n");
		}
	} else if (flags & ASSERT_DRRS_INACTIVE) {
		if (!intel_is_drrs_inactive(drm.fd, prim_mode_params.pipe)) {
			drrs_print_status();
			igt_assert_f(false, "DRRS INACTIVE\n");
		}
	}

	if (flags & ASSERT_FBC_ENABLED) {
		igt_require(!fbc_not_enough_stolen());
		igt_require(!fbc_stride_not_supported());
		igt_require(!fbc_mode_too_large());
		igt_require(!fbc_psr_not_possible());
		if (!intel_fbc_wait_until_enabled(drm.fd, prim_mode_params.pipe)) {
			igt_assert_f(intel_fbc_is_enabled(drm.fd,
						    prim_mode_params.pipe,
						    IGT_LOG_WARN),
				     "FBC disabled\n");
		}

		if (opt.fbc_check_compression)
			igt_assert(fbc_wait_for_compression());
	} else if (flags & ASSERT_FBC_DISABLED) {
		igt_assert(!intel_fbc_wait_until_enabled(drm.fd,
						   prim_mode_params.pipe));
	}

	if (flags & ASSERT_PSR_ENABLED) {
		igt_assert_f(psr_wait_entry(drm.debugfs, PSR_MODE_1, NULL),
			     "PSR still disabled\n");
		psr_sink_error_check(drm.debugfs, PSR_MODE_1, prim_mode_params.output);
	} else if (flags & ASSERT_PSR_DISABLED)
		igt_assert_f(psr_wait_update(drm.debugfs, PSR_MODE_1, NULL),
			     "PSR still enabled\n");
}

static void __do_assertions(const struct test_mode *t, int flags,
			    int line)
{
	flags = adjust_assertion_flags(t, flags);

	/* Make sure any submitted rendering is now idle. */
	if (!(flags & ASSERT_NO_IDLE_GPU))
		gem_quiescent_gpu(drm.fd);

	igt_debug("checking asserts in line %i\n", line);

	wait_user(2, "Paused before assertions.");

	/* Check the CRC to make sure the drawing operations work
	 * immediately, independently of the features being enabled. */
	do_crc_assertions(flags);

	/* Now we can flush things to make the test faster. */
	do_flush(t);

	do_status_assertions(flags);

	/* Check CRC again to make sure the compressed screen is ok,
	 * except if we're not drawing on the primary screen. On this
	 * case, the first check should be enough and a new CRC check
	 * would only delay the test suite while adding no value to the
	 * test suite. */
	if (t->screen == SCREEN_PRIM)
		do_crc_assertions(flags);

	if (fbc.supports_last_action && opt.fbc_check_last_action) {
		if (flags & ASSERT_LAST_ACTION_CHANGED)
			igt_assert(fbc_last_action_changed());
		else if (flags & ASSERT_NO_ACTION_CHANGE)
			igt_assert(!fbc_last_action_changed());
	}

	wait_user(1, "Paused after assertions.");
}

#define do_assertions(__flags) __do_assertions(t, (__flags), __LINE__)

static void enable_prim_screen_and_wait(const struct test_mode *t)
{
	fill_fb_region(&prim_mode_params.primary, t->method, COLOR_PRIM_BG);
	set_mode_for_params(&prim_mode_params);

	wanted_crc = &blue_crcs[t->format].crc;
	fbc_update_last_action();

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void update_modeset_cached_params(enum igt_draw_method method)
{
	bool found = false;

	igt_output_set_pipe(prim_mode_params.output, prim_mode_params.pipe);
	igt_output_set_pipe(scnd_mode_params.output, scnd_mode_params.pipe);

	found = igt_override_all_active_output_modes_to_fit_bw(&drm.display);
	igt_require_f(found, "No valid mode combo found.\n");

	prim_mode_params.mode = *igt_output_get_mode(prim_mode_params.output);
	prim_mode_params.primary.w = prim_mode_params.mode.hdisplay;
	prim_mode_params.primary.h = prim_mode_params.mode.vdisplay;

	scnd_mode_params.mode = *igt_output_get_mode(scnd_mode_params.output);
	scnd_mode_params.primary.w = scnd_mode_params.mode.hdisplay;
	scnd_mode_params.primary.h = scnd_mode_params.mode.vdisplay;

	fill_fb_region(&prim_mode_params.primary, method, COLOR_PRIM_BG);
	fill_fb_region(&scnd_mode_params.primary, method, COLOR_SCND_BG);

	__set_mode_for_params(&prim_mode_params);
	__set_mode_for_params(&scnd_mode_params);
}

static void enable_both_screens_and_wait(const struct test_mode *t)
{
	int ret;

	fill_fb_region(&prim_mode_params.primary, t->method, COLOR_PRIM_BG);
	fill_fb_region(&scnd_mode_params.primary, t->method, COLOR_SCND_BG);

	__set_mode_for_params(&prim_mode_params);
	__set_mode_for_params(&scnd_mode_params);

	if (drm.display.is_atomic)
		ret = igt_display_try_commit_atomic(&drm.display,
				DRM_MODE_ATOMIC_TEST_ONLY |
				DRM_MODE_ATOMIC_ALLOW_MODESET,
				NULL);
	else
		ret = igt_display_try_commit2(&drm.display, COMMIT_LEGACY);

	if (ret)
		update_modeset_cached_params(t->method);

	igt_display_commit2(&drm.display, drm.display.is_atomic ? COMMIT_ATOMIC : COMMIT_LEGACY);

	wanted_crc = &blue_crcs[t->format].crc;
	fbc_update_last_action();

	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void set_region_for_test(const struct test_mode *t,
				struct fb_region *reg)
{
	fill_fb_region(reg, t->method, COLOR_PRIM_BG);

	igt_plane_set_fb(reg->plane, reg->fb);
	igt_plane_set_position(reg->plane, 0, 0);
	igt_plane_set_size(reg->plane, reg->w, reg->h);
	igt_fb_set_size(reg->fb, reg->plane, reg->w, reg->h);

	igt_display_commit(&drm.display);
	do_assertions(ASSERT_NO_ACTION_CHANGE);
}

static void set_plane_for_test_fbc(const struct test_mode *t, igt_plane_t *plane)
{
	struct igt_fb fb;
	uint32_t color;

	igt_info("Testing fbc on plane %i%s\n", plane->index + 1, kmstest_pipe_name(prim_mode_params.pipe));

	create_fb(t->format, prim_mode_params.mode.hdisplay, prim_mode_params.mode.vdisplay, t->tiling, t->plane, &fb);
	color = pick_color(&fb, COLOR_PRIM_BG);
	igt_draw_rect_fb(drm.fd, drm.bops, 0, &fb, t->method,
			 0, 0, fb.width, fb.height,
			 color);

	igt_plane_set_fb(plane, &fb);
	igt_plane_set_position(plane, 0, 0);
	igt_plane_set_size(plane, prim_mode_params.mode.hdisplay, prim_mode_params.mode.vdisplay);
	igt_fb_set_size(&fb, plane, prim_mode_params.mode.hdisplay, prim_mode_params.mode.vdisplay);
	igt_display_commit_atomic(&drm.display, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);

	fbc_update_last_action();
	do_assertions(ASSERT_FBC_ENABLED | ASSERT_NO_ACTION_CHANGE);
	igt_assert_f(fbc_enable_per_plane(plane->index + 1, prim_mode_params.pipe), "FBC disabled\n");

	igt_remove_fb(drm.fd, &fb);
	igt_plane_set_fb(plane, NULL);
	igt_display_commit2(&drm.display, COMMIT_ATOMIC);
}

static bool enable_features_for_test(const struct test_mode *t)
{
	bool ret = false;

	if (t->feature == FEATURE_DEFAULT)
		return false;

	if (t->feature & FEATURE_FBC)
		intel_fbc_enable(drm.fd);
	if (t->feature & FEATURE_PSR)
		ret = psr_enable(drm.fd, drm.debugfs, PSR_MODE_1, NULL);
	if (t->feature & FEATURE_DRRS)
		intel_drrs_enable(drm.fd, prim_mode_params.pipe);

	return ret;
}

static void check_test_requirements(const struct test_mode *t)
{
	if (t->pipes == PIPE_DUAL)
		igt_require_f(scnd_mode_params.output,
			    "Can't test dual pipes with the current outputs\n");

	if (t->feature & FEATURE_FBC)
		igt_require_f(fbc.can_test,
			      "Can't test FBC with this chipset\n");

	if (t->feature & FEATURE_PSR) {
		igt_require_f(psr.can_test,
			      "Can't test PSR with the current outputs\n");
	}

	if (t->feature & FEATURE_DRRS)
		igt_require_f(drrs.can_test,
			      "Can't test DRRS with the current outputs\n");

	/*
	 * In kernel, When PSR is enabled, DRRS will be disabled. So If a test
	 * case needs DRRS + PSR enabled, that will be skipped.
	 */
	igt_require_f(!((t->feature & FEATURE_PSR) &&
		      (t->feature & FEATURE_DRRS)),
		      "Can't test PSR and DRRS together\n");

	if (opt.only_pipes != PIPE_COUNT)
		igt_require(t->pipes == opt.only_pipes);
}

static void set_crtc_fbs(const struct test_mode *t)
{
	struct screen_fbs *s = &fbs[t->format];

	create_fbs(t->format, t->tiling);

	switch (t->fbs) {
	case FBS_INDIVIDUAL:
		prim_mode_params.primary.fb = &s->prim_pri;
		scnd_mode_params.primary.fb = &s->scnd_pri;
		offscreen_fb.fb = &s->offscreen;

		prim_mode_params.primary.x = 0;
		scnd_mode_params.primary.x = 0;
		offscreen_fb.x = 0;

		prim_mode_params.primary.y = 0;
		scnd_mode_params.primary.y = 0;
		offscreen_fb.y = 0;
		break;
	case FBS_SHARED:
		/* Please see the comment at the top of create_shared_fb(). */
		prim_mode_params.primary.fb = &s->big;
		scnd_mode_params.primary.fb = &s->big;
		offscreen_fb.fb = &s->big;

		prim_mode_params.primary.x = opt.shared_fb_x_offset;
		scnd_mode_params.primary.x = opt.shared_fb_x_offset;
		offscreen_fb.x = opt.shared_fb_x_offset;

		prim_mode_params.primary.y = opt.shared_fb_y_offset;
		scnd_mode_params.primary.y = prim_mode_params.primary.y +
					prim_mode_params.primary.h;
		offscreen_fb.y = scnd_mode_params.primary.y + scnd_mode_params.primary.h;
		break;
	default:
		igt_assert(false);
	}

	prim_mode_params.cursor.fb = &s->prim_cur;
	prim_mode_params.sprite.fb = &s->prim_spr;
	scnd_mode_params.cursor.fb = &s->scnd_cur;
	scnd_mode_params.sprite.fb = &s->scnd_spr;
}

static void prepare_subtest_data(const struct test_mode *t,
				 struct draw_pattern_info *pattern)
{
	bool need_modeset;

	check_test_requirements(t);

	stop_busy_thread();

	need_modeset = disable_features(t);
	set_crtc_fbs(t);

	if (t->screen == SCREEN_OFFSCREEN)
		fill_fb_region(&offscreen_fb, t->method, COLOR_OFFSCREEN_BG);

	igt_display_reset(&drm.display);
	if (need_modeset)
		igt_display_commit(&drm.display);

	init_blue_crc(t->format, t->tiling);
	if (pattern)
		init_crcs(t->format, t->tiling, pattern);

	need_modeset = enable_features_for_test(t);
	if (need_modeset)
		igt_display_commit(&drm.display);
}

static void prepare_subtest_screens(const struct test_mode *t)
{
	/* FBC disabled: Wa_16023588340 */
	igt_skip_on_f(t->feature == FEATURE_FBC && intel_is_fbc_disabled_by_wa(drm.fd),
		      "WA has disabled FBC on BMG\n");

	if (t->pipes == PIPE_DUAL)
		enable_both_screens_and_wait(t);
	else
		enable_prim_screen_and_wait(t);

	if (t->screen == SCREEN_PRIM) {
		if (t->plane == PLANE_CUR)
			set_region_for_test(t, &prim_mode_params.cursor);
		if (t->plane == PLANE_SPR)
			set_region_for_test(t, &prim_mode_params.sprite);
	}

	if (t->pipes == PIPE_DUAL && t->screen == SCREEN_SCND) {
		if (t->plane == PLANE_CUR)
			set_region_for_test(t, &scnd_mode_params.cursor);
		if (t->plane == PLANE_SPR)
			set_region_for_test(t, &scnd_mode_params.sprite);
	}
}

static void prepare_subtest(const struct test_mode *t,
			    struct draw_pattern_info *pattern)
{
	prepare_subtest_data(t, pattern);
	prepare_subtest_screens(t);
}

/*
 * rte - the basic sanity test
 *
 * METHOD
 *   Just disable all screens, assert everything is disabled, then enable all
 *   screens - including primary, cursor and sprite planes - and assert that
 *   the tested feature is enabled.
 *
 * EXPECTED RESULTS
 *   Blue screens and t->feature enabled.
 *
 * FAILURES
 *   A failure here means that every other subtest will probably fail too. It
 *   probably means that the Kernel is just not enabling the feature we want.
 */
static void rte_subtest(const struct test_mode *t)
{
	/* FBC disabled: Wa_16023588340 */
	igt_skip_on_f(t->feature == FEATURE_FBC && intel_is_fbc_disabled_by_wa(drm.fd),
		      "WA has disabled FBC on BMG\n");

	prepare_subtest_data(t, NULL);

	unset_all_crtcs();
	do_assertions(ASSERT_FBC_DISABLED | ASSERT_PSR_DISABLED |
		      DONT_ASSERT_CRC | ASSERT_DRRS_INACTIVE);

	if (t->pipes == PIPE_SINGLE)
		enable_prim_screen_and_wait(t);
	else
		enable_both_screens_and_wait(t);

	set_region_for_test(t, &prim_mode_params.cursor);
	set_region_for_test(t, &prim_mode_params.sprite);

	if (t->pipes == PIPE_DUAL) {
		set_region_for_test(t, &scnd_mode_params.cursor);
		set_region_for_test(t, &scnd_mode_params.sprite);
	}
}

static bool is_valid_plane(igt_plane_t *plane)
{
	int index = plane->index;

	if (plane->type == DRM_PLANE_TYPE_CURSOR)
		return false;
	/*
	 * Execute test only on first three planes
	 */
	return ((index >= 0) && (index < 3));
}

/**
 * plane-fbc-rte - the basic sanity test
 *
 * METHOD
 *   Just disable primary screen, assert everything is disabled, then enable single
 *   screens and single plane one by one  and assert that the tested fbc is enabled
 *   for the particular plane.
 *
 * EXPECTED RESULTS
 *   Blue screens and t->feature enabled.
 *
 * FAILURES
 *   A failure here means that fbc is not getting enabled for requested plane. It means
 *   kernel is not able to enable fbc on the requested plane.
 */

static void plane_fbc_rte_subtest(const struct test_mode *t)
{
	igt_plane_t *plane;

	igt_require_f((drm.display_ver >= 20), "Can't test fbc for each plane\n");

	prepare_subtest_data(t, NULL);
	unset_all_crtcs();
	do_assertions(ASSERT_FBC_DISABLED | DONT_ASSERT_CRC);

	igt_output_override_mode(prim_mode_params.output, &prim_mode_params.mode);
	igt_output_set_pipe(prim_mode_params.output, prim_mode_params.pipe);

	wanted_crc = &blue_crcs[t->format].crc;

	for_each_plane_on_pipe(&drm.display, prim_mode_params.pipe, plane) {
		if (!is_valid_plane(plane))
			continue;

		set_plane_for_test_fbc(t, plane);
	}

	igt_display_reset(&drm.display);
}

static void update_wanted_crc(const struct test_mode *t, igt_crc_t *crc)
{
	if (t->screen == SCREEN_PRIM)
		wanted_crc = crc;
}

static bool op_disables_psr(const struct test_mode *t,
			    enum igt_draw_method method)
{
	if (method != IGT_DRAW_MMAP_GTT)
		return false;
	if (t->screen == SCREEN_PRIM)
		return true;
	/* On FBS_SHARED, even if the target is not the PSR screen
	 * (SCREEN_PRIM), all primary planes share the same frontbuffer, so a
	 * write to the second screen primary plane - or offscreen plane - will
	 * touch the framebuffer that's also used by the primary screen. */
	if (t->fbs == FBS_SHARED && t->plane == PLANE_PRI)
		return true;

	return false;
}

/*
 * draw - draw a set of rectangles on the screen using the provided method
 *
 * METHOD
 *   Just set the screens as appropriate and then start drawing a series of
 *   rectangles on the target screen. The important guy here is the drawing
 *   method used.
 *
 * EXPECTED RESULTS
 *   The feature either stays enabled or gets reenabled after the oprations. You
 *   will also see the rectangles on the target screen.
 *
 * FAILURES
 *   A failure here indicates a problem somewhere between the Kernel's
 *   frontbuffer tracking infrastructure or the feature itself. You need to pay
 *   attention to which drawing method is being used.
 */
static void draw_subtest(const struct test_mode *t)
{
	int r;
	int assertions = 0;
	struct draw_pattern_info *pattern;
	struct modeset_params *params = pick_params(t);
	struct fb_region *target;

	switch (t->screen) {
	case SCREEN_PRIM:
		if (t->method != IGT_DRAW_MMAP_GTT && t->plane == PLANE_PRI)
			assertions |= ASSERT_LAST_ACTION_CHANGED;
		else
			assertions |= ASSERT_NO_ACTION_CHANGE;
		break;
	case SCREEN_SCND:
	case SCREEN_OFFSCREEN:
		assertions |= ASSERT_NO_ACTION_CHANGE;
		break;
	default:
		igt_assert(false);
	}

	switch (t->plane) {
	case PLANE_PRI:
		pattern = &pattern1;
		break;
	case PLANE_CUR:
	case PLANE_SPR:
		pattern = &pattern2;
		break;
	default:
		igt_assert(false);
	}

	if (op_disables_psr(t, t->method))
		assertions |= ASSERT_PSR_DISABLED;

	/*
	 * On FBS_INDIVIDUAL, write to offscreen plane will not touch the
	 * current frambuffer. Hence assert for DRRS_LOW.
	 */
	if ((t->fbs == FBS_INDIVIDUAL) && (t->screen == SCREEN_OFFSCREEN))
		assertions |= ASSERT_DRRS_LOW;

	prepare_subtest(t, pattern);
	target = pick_target(t, params);

	for (r = 0; r < pattern->n_rects; r++) {
		igt_debug("Drawing rect %d\n", r);
		draw_rect(pattern, target, t->method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);
		do_assertions(assertions);
	}
}

/*
 * multidraw - draw a set of rectangles on the screen using alternated drawing
 *             methods
 *
 * METHOD
 *   This is just like the draw subtest, but now we keep alternating between two
 *   drawing methods. Each time we run multidraw_subtest we will test all the
 *   possible pairs of drawing methods.
 *
 * EXPECTED RESULTS
 *   The same as the draw subtest.
 *
 * FAILURES
 *   If you get a failure here, first you need to check whether you also get
 *   failures on the individual draw subtests. If yes, then go fix every single
 *   draw subtest first. If all the draw subtests pass but this one fails, then
 *   you have to study how one drawing method is stopping the other from
 *   properly working.
 */
static void multidraw_subtest(const struct test_mode *t)
{
	int r;
	int assertions = 0;
	struct draw_pattern_info *pattern;
	struct modeset_params *params = pick_params(t);
	struct fb_region *target;
	enum igt_draw_method m1, m2, used_method;
	bool wc_used = false;

	switch (t->plane) {
	case PLANE_PRI:
		pattern = &pattern1;
		break;
	case PLANE_CUR:
	case PLANE_SPR:
		pattern = &pattern2;
		break;
	default:
		igt_assert(false);
	}

	prepare_subtest(t, pattern);
	target = pick_target(t, params);

	for (m1 = 0; m1 < IGT_DRAW_METHOD_COUNT; m1++) {
		for (m2 = m1 + 1; m2 < IGT_DRAW_METHOD_COUNT; m2++) {

			igt_debug("Methods %s and %s\n",
				  igt_draw_get_method_name(m1),
				  igt_draw_get_method_name(m2));

			if (!igt_draw_supports_method(drm.fd, m1) ||
			    !igt_draw_supports_method(drm.fd, m2))
				continue;

			for (r = 0; r < pattern->n_rects; r++) {
				used_method = (r % 2 == 0) ? m1 : m2;

				igt_debug("Used method %s\n",
					igt_draw_get_method_name(used_method));

				draw_rect(pattern, target, used_method, r);

				if (used_method == IGT_DRAW_MMAP_WC ||
				    used_method == IGT_DRAW_MMAP_GTT)
					wc_used = true;

				update_wanted_crc(t,
						  &pattern->crcs[t->format][r]);

				assertions = used_method != IGT_DRAW_MMAP_GTT ?
					     ASSERT_LAST_ACTION_CHANGED :
					     ASSERT_NO_ACTION_CHANGE;
				if (op_disables_psr(t, used_method) &&
				    !wc_used)
					assertions |= ASSERT_PSR_DISABLED;

				do_assertions(assertions);
			}

			fill_fb_region(target, m2, COLOR_PRIM_BG);
			_fb_dirty_ioctl(target);

			update_wanted_crc(t, &blue_crcs[t->format].crc);
			do_assertions(ASSERT_NO_ACTION_CHANGE);
		}
	}
}

static bool format_is_valid(int feature_flags,
			    enum pixel_format format)
{
	if (!(feature_flags & FEATURE_FBC))
		return true;

	switch (format) {
	case FORMAT_RGB888:
		return true;
	case FORMAT_RGB565:
		if (IS_GEN2(drm.devid) || IS_G4X(drm.devid))
			return false;
		return true;
	case FORMAT_RGB101010:
		return false;
	default:
		igt_assert(false);
	}
}

/*
 * badformat - test pixel formats that are not supported by at least one feature
 *
 * METHOD
 *   We just do a modeset on a buffer with the given pixel format and check the
 *   status of the relevant features.
 *
 * EXPECTED RESULTS
 *   No assertion failures :)
 *
 * FAILURES
 *   If you get a feature enabled/disabled assertion failure, then you should
 *   probably check the Kernel code for the feature that checks the pixel
 *   formats. If you get a CRC assertion failure, then you should use the
 *   appropriate command line arguments that will allow you to look at the
 *   screen, then judge what to do based on what you see.
 */
static void badformat_subtest(const struct test_mode *t)
{
	bool fbc_valid = format_is_valid(FEATURE_FBC, t->format);
	bool psr_valid = format_is_valid(FEATURE_PSR, t->format);
	int assertions = ASSERT_NO_ACTION_CHANGE;

	prepare_subtest_data(t, NULL);

	fill_fb_region(&prim_mode_params.primary, t->method, COLOR_PRIM_BG);
	set_mode_for_params(&prim_mode_params);

	wanted_crc = &blue_crcs[t->format].crc;

	if (!fbc_valid)
		assertions |= ASSERT_FBC_DISABLED;
	if (!psr_valid)
		assertions |= ASSERT_PSR_DISABLED;
	do_assertions(assertions);
}

/*
 * format_draw - test pixel formats that are not FORMAT_DEFAULT
 *
 * METHOD
 *   The real subtest to be executed depends on whether the pixel format is
 *   supported by the features being tested or not. Check the documentation of
 *   each subtest.
 *
 * EXPECTED RESULTS
 *   See the documentation for each subtest.
 *
 * FAILURES
 *   See the documentation for each subtest.
 */
static void format_draw_subtest(const struct test_mode *t)
{
	if (format_is_valid(t->feature, t->format))
		draw_subtest(t);
	else
		badformat_subtest(t);
}

static bool tiling_is_valid(int feature_flags, enum tiling_type tiling)
{
	if (!(feature_flags & FEATURE_FBC))
		return true;

	switch (tiling) {
	case TILING_LINEAR:
		return intel_gen(drm.devid) >= 9;
	case TILING_X:
		return (intel_get_device_info(drm.devid)->display_ver > 29) ? false : true;
	case TILING_Y:
		return true;
	case TILING_4:
		return intel_gen(drm.devid) >= 12;
	default:
		igt_assert(false);
		return false;
	}
}

/*
 * slow_draw - sleep a little bit between drawing operations
 *
 * METHOD
 *   This test is basically the same as the draw subtest, except that we sleep a
 *   little bit after each drawing operation. The goal is to detect problems
 *   that can happen in case a drawing operation is done while the machine is in
 *   some deep sleep states.
 *
 * EXPECTED RESULTS
 *   The pattern appears on the screen as expected.
 *
 * FAILURES
 *   I've seen this happen in a SKL machine and still haven't investigated it.
 *   My guess would be that preventing deep sleep states fixes the problem.
 */
static void slow_draw_subtest(const struct test_mode *t)
{
	int r;
	struct draw_pattern_info *pattern = &pattern1;
	struct modeset_params *params = pick_params(t);
	struct fb_region *target;

	prepare_subtest(t, pattern);
	sleep(2);
	target = pick_target(t, params);

	for (r = 0; r < pattern->n_rects; r++) {
		sleep(2);
		draw_rect(pattern, target, t->method, r);
		sleep(2);

		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		if (t->feature & FEATURE_DRRS)
			do_assertions(ASSERT_DRRS_LOW);
		else
			do_assertions(0);
	}
}

static void flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
			 unsigned int tv_usec, void *data)
{
	igt_debug("Flip event received.\n");
}

static void wait_flip_event(void)
{
	int rc;
	drmEventContext evctx;
	struct pollfd pfd;

	evctx.version = 2;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = flip_handler;

	pfd.fd = drm.fd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	rc = poll(&pfd, 1, TIME);
	switch (rc) {
	case 0:
		igt_assert_f(false, "Poll timeout\n");
		break;
	case 1:
		rc = drmHandleEvent(drm.fd, &evctx);
		igt_assert_eq(rc, 0);
		break;
	default:
		igt_assert_f(false, "Unexpected poll rc %d\n", rc);
		break;
	}
}

static void set_prim_plane_for_params(struct modeset_params *params)
{
	__set_prim_plane_for_params(params);
	igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);
}

static void page_flip_for_params(struct modeset_params *params,
				 enum flip_type type)
{
	int rc;

	switch (type) {
	case FLIP_PAGEFLIP:
		rc = drmModePageFlip(drm.fd, drm.display.pipes[params->pipe].crtc_id,
				     params->primary.fb->fb_id,
				     DRM_MODE_PAGE_FLIP_EVENT, NULL);
		igt_assert_eq(rc, 0);
		wait_flip_event();
		break;
	case FLIP_MODESET:
		set_mode_for_params(params);
		break;
	case FLIP_PLANES:
		set_prim_plane_for_params(params);
		break;
	default:
		igt_assert(false);
	}
}

/*
 * flip - just exercise page flips with the patterns we have
 *
 * METHOD
 *   We draw the pattern on a backbuffer using the provided method, then we
 *   flip, making this the frontbuffer. We can flip both using the dedicated
 *   pageflip IOCTL or the modeset IOCTL.
 *
 * EXPECTED RESULTS
 *   Everything works as expected, screen contents are properly updated.
 *
 * FAILURES
 *   On a failure here you need to go directly to the Kernel's flip code and see
 *   how it interacts with the feature being tested.
 */
static void flip_subtest(const struct test_mode *t)
{
	int r;
	int assertions = 0;
	struct igt_fb fb2, *orig_fb;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern1;
	enum color bg_color;

	switch (t->screen) {
	case SCREEN_PRIM:
		assertions |= ASSERT_LAST_ACTION_CHANGED;
		bg_color = COLOR_PRIM_BG;
		break;
	case SCREEN_SCND:
		assertions |= ASSERT_NO_ACTION_CHANGE;
		bg_color = COLOR_SCND_BG;
		break;
	default:
		igt_assert(false);
	}

	prepare_subtest(t, pattern);

	create_fb(t->format, params->primary.fb->width, params->primary.fb->height,
		  t->tiling, t->plane, &fb2);
	fill_fb(&fb2, bg_color);
	orig_fb = params->primary.fb;

	for (r = 0; r < pattern->n_rects; r++) {
		params->primary.fb = (r % 2 == 0) ? &fb2 : orig_fb;

		if (r != 0)
			draw_rect(pattern, &params->primary, t->method, r - 1);
		draw_rect(pattern, &params->primary, t->method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		page_flip_for_params(params, t->flip);

		do_assertions(assertions);
	}

	igt_remove_fb(drm.fd, &fb2);
}

/*
 * fliptrack - check if the hardware tracking works after page flips
 *
 * METHOD
 *   Flip to a new buffer, then draw on it using MMAP_GTT and check the CRC to
 *   make sure the hardware tracking detected the write.
 *
 * EXPECTED RESULTS
 *   Everything works as expected, screen contents are properly updated.
 *
 * FAILURES
 *   First you need to check if the draw and flip subtests pass. Only after both
 *   are passing this test can be useful. If we're failing only on this subtest,
 *   then maybe we are not properly updating the hardware tracking registers
 *   during the flip operations.
 */
static void fliptrack_subtest(const struct test_mode *t, enum flip_type type)
{
	int r;
	struct igt_fb fb2, *orig_fb;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern1;

	prepare_subtest(t, pattern);

	create_fb(t->format, params->primary.fb->width, params->primary.fb->height,
		  t->tiling, t->plane, &fb2);
	fill_fb(&fb2, COLOR_PRIM_BG);
	orig_fb = params->primary.fb;

	for (r = 0; r < pattern->n_rects; r++) {
		params->primary.fb = (r % 2 == 0) ? &fb2 : orig_fb;

		if (r != 0)
			draw_rect(pattern, &params->primary, t->method, r - 1);

		page_flip_for_params(params, type);
		do_assertions(0);

		draw_rect(pattern, &params->primary, t->method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		do_assertions(ASSERT_PSR_DISABLED);
	}

	igt_remove_fb(drm.fd, &fb2);
}

/*
 * move - just move the sprite or cursor around
 *
 * METHOD
 *   Move the surface around, following the defined pattern.
 *
 * EXPECTED RESULTS
 *   The move operations are properly detected by the Kernel, and the screen is
 *   properly updated every time.
 *
 * FAILURES
 *   If you get a failure here, check how the Kernel is enabling or disabling
 *   your feature when it moves the planes around.
 */
static void move_subtest(const struct test_mode *t)
{
	int r;
	int assertions = ASSERT_NO_ACTION_CHANGE;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern3;
	struct fb_region *reg = pick_target(t, params);
	bool repeat = false;

	prepare_subtest(t, pattern);

	/* Just paint the right color since we start at 0x0. */
	draw_rect(pattern, reg, t->method, 0);
	update_wanted_crc(t, &pattern->crcs[t->format][0]);

	do_assertions(assertions);

	for (r = 1; r < pattern->n_rects; r++) {
		struct rect rect = pattern->get_rect(&params->primary, r);

		igt_plane_set_fb(reg->plane, reg->fb);
		igt_plane_set_position(reg->plane, rect.x, rect.y);
		igt_plane_set_size(reg->plane, rect.w, rect.h);
		igt_fb_set_size(reg->fb, reg->plane, rect.w, rect.h);
		igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);

		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		do_assertions(assertions);

		/* "Move" the last rect to the same position just to make sure
		 * this works too. */
		if (r+1 == pattern->n_rects && !repeat) {
			repeat = true;
			r--;
		}
	}
}

/*
 * onoff - just enable and disable the sprite or cursor plane a few times
 *
 * METHOD
 *   Just enable and disable the desired plane a few times.
 *
 * EXPECTED RESULTS
 *   Everything is properly detected by the Kernel and the screen contents are
 *   accurate.
 *
 * FAILURES
 *   As usual, if you get a failure here you need to check how the feature is
 *   being handled when the planes are enabled or disabled.
 */
static void onoff_subtest(const struct test_mode *t)
{
	int r;
	int assertions = ASSERT_NO_ACTION_CHANGE;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern3;

	prepare_subtest(t, pattern);

	/* Just paint the right color since we start at 0x0. */
	draw_rect(pattern, pick_target(t, params), t->method, 0);
	update_wanted_crc(t, &pattern->crcs[t->format][0]);
	do_assertions(assertions);

	for (r = 0; r < 4; r++) {
		struct fb_region *reg = pick_target(t, params);

		if (r % 2 == 0) {
			igt_plane_set_fb(reg->plane, NULL);
			igt_display_commit(&drm.display);

			update_wanted_crc(t, &blue_crcs[t->format].crc);

		} else {
			igt_plane_set_fb(reg->plane, reg->fb);
			igt_plane_set_size(reg->plane, reg->w, reg->h);
			igt_fb_set_size(reg->fb, reg->plane, reg->w, reg->h);
			igt_display_commit(&drm.display);

			update_wanted_crc(t, &pattern->crcs[t->format][0]);
		}

		do_assertions(assertions);
	}
}

static bool prim_plane_disabled(void)
{
	/*
	 * Cannot use igt_plane_get_prop here to retrieve fb_id,
	 * the testsuite doesn't require ATOMIC.
	 */
	return !prim_mode_params.primary.plane->values[IGT_PLANE_FB_ID];
}

/*
 * fullscreen_plane - put a fullscreen plane covering the whole screen
 *
 * METHOD
 *   As simple as the description above.
 *
 * EXPECTED RESULTS
 *   It depends on the feature being tested. FBC gets disabled, but PSR doesn't.
 *
 * FAILURES
 *   Again, if you get failures here you need to dig into the Kernel code, see
 *   how it is handling your feature on this specific case.
 */
static void fullscreen_plane_subtest(const struct test_mode *t)
{
	struct draw_pattern_info *pattern = &pattern4;
	struct igt_fb fullscreen_fb;
	struct rect rect;
	struct modeset_params *params = pick_params(t);
	int assertions;

	prepare_subtest(t, pattern);

	rect = pattern->get_rect(&params->primary, 0);
	create_fb(t->format, rect.w, rect.h, t->tiling, t->plane,
		  &fullscreen_fb);
	/* Call pick_color() again since PRI and SPR may not support the same
	 * pixel formats. */
	rect.color = pick_color(&fullscreen_fb, COLOR_GREEN);
	igt_draw_fill_fb(drm.fd, &fullscreen_fb, rect.color);

	igt_plane_set_fb(params->sprite.plane, &fullscreen_fb);
	igt_display_commit(&drm.display);
	update_wanted_crc(t, &pattern->crcs[t->format][0]);

	switch (t->screen) {
	case SCREEN_PRIM:
		assertions = ASSERT_LAST_ACTION_CHANGED;

		if (prim_plane_disabled())
			assertions |= ASSERT_FBC_DISABLED;
		break;
	case SCREEN_SCND:
		assertions = ASSERT_NO_ACTION_CHANGE;
		break;
	default:
		igt_assert(false);
	}
	do_assertions(assertions);

	igt_plane_set_fb(params->sprite.plane, NULL);
	igt_display_commit(&drm.display);

	if (t->screen == SCREEN_PRIM)
		assertions = ASSERT_LAST_ACTION_CHANGED;
	update_wanted_crc(t, &blue_crcs[t->format].crc);
	do_assertions(assertions);

	igt_remove_fb(drm.fd, &fullscreen_fb);
}

/*
 * scaledprimary - try different primary plane scaling strategies
 *
 * METHOD
 *    Enable the primary plane, use drmModeSetPlane to force scaling in
 *    different ways.
 *
 * EXPECTED RESULTS
 *   SKIP on platforms that don't support primary plane scaling. Success on all
 *   others.
 *
 * FAILURES
 *   TODO: although we're exercising the code here, we're not really doing
 *   assertions in order to check if things are working properly. The biggest
 *   issue this code would be able to find would be an incorrectly calculated
 *   CFB size, and today we don't have means to assert this. One day we might
 *   implement some sort of stolen memory checking mechanism, then we'll be able
 *   to force it to run after every drmModeSetPlane call here, so we'll be
 *   checking if the expected CFB size is actually what we think it is.
 */
static void scaledprimary_subtest(const struct test_mode *t)
{
	struct igt_fb new_fb, *old_fb;
	struct modeset_params *params = pick_params(t);
	struct fb_region *reg = &params->primary;
	int gen = drm.display_ver;
	int src_y_upscale = ALIGN(reg->h / 4, 4);

	igt_require_f(gen >= 9,
		      "Can't test primary plane scaling before gen 9\n");

	prepare_subtest(t, NULL);

	old_fb = reg->fb;

	create_fb(t->format, reg->fb->width, reg->fb->height,
		  t->tiling, t->plane, &new_fb);
	fill_fb(&new_fb, COLOR_BLUE);

	igt_draw_rect_fb(drm.fd, drm.bops, 0, &new_fb, t->method,
			 reg->x, reg->y, reg->w / 2, reg->h / 2,
			 pick_color(&new_fb, COLOR_GREEN));
	igt_draw_rect_fb(drm.fd, drm.bops, 0, &new_fb, t->method,
			 reg->x + reg->w / 2, reg->y + reg->h / 2,
			 reg->w / 2, reg->h / 2,
			 pick_color(&new_fb, COLOR_RED));
	igt_draw_rect_fb(drm.fd, drm.bops, 0, &new_fb, t->method,
			 reg->x + reg->w / 2, reg->y + reg->h / 2,
			 reg->w / 4, reg->h / 4,
			 pick_color(&new_fb, COLOR_MAGENTA));

	/* No scaling. */
	igt_plane_set_fb(reg->plane, &new_fb);
	igt_fb_set_position(&new_fb, reg->plane, reg->x, reg->y);
	igt_fb_set_size(&new_fb, reg->plane, reg->w, reg->h);
	igt_plane_set_size(reg->plane, params->mode.hdisplay, params->mode.vdisplay);
	igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);
	do_assertions(DONT_ASSERT_CRC);

	/* Source upscaling. */
	igt_fb_set_size(&new_fb, reg->plane, reg->w / 2, reg->h / 2);
	igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);
	do_assertions(DONT_ASSERT_CRC);

	/* Destination doesn't fill the entire CRTC, no scaling. */
	igt_fb_set_size(&new_fb, reg->plane, reg->w / 2, reg->h / 2);
	igt_plane_set_position(reg->plane,
			       params->mode.hdisplay / 4,
			       params->mode.vdisplay / 4);
	igt_plane_set_size(reg->plane,
			   params->mode.hdisplay / 2,
			   params->mode.vdisplay / 2);
	igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);
	do_assertions(DONT_ASSERT_CRC);

	/* Destination doesn't fill the entire CRTC, upscaling. */
	igt_fb_set_position(&new_fb, reg->plane,
			    reg->x + reg->w / 4, reg->y + src_y_upscale);
	igt_fb_set_size(&new_fb, reg->plane, reg->w / 2, reg->h / 2);
	igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);
	do_assertions(DONT_ASSERT_CRC);

	/*
	 * On gen >= 9 HW, FBC is not enabled on a plane with a Y offset
	 * that isn't divisible by 4, because it causes FIFO underruns.
	 *
	 * Check that FBC is disabled.
	 */
	igt_fb_set_position(&new_fb, reg->plane,
			    reg->x + reg->w / 4, (reg->y + src_y_upscale) | 3);
	igt_fb_set_size(&new_fb, reg->plane, reg->w / 2, reg->h / 2);
	igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);
	do_assertions(DONT_ASSERT_CRC | (gen >= 9 ? ASSERT_FBC_DISABLED : 0));

	/* Back to the good and old blue fb. */
	igt_plane_set_fb(reg->plane, old_fb);
	igt_plane_set_position(params->primary.plane, 0, 0);
	igt_plane_set_size(reg->plane, params->mode.hdisplay, params->mode.vdisplay);
	igt_fb_set_position(reg->fb, reg->plane, reg->x, reg->y);
	igt_fb_set_size(reg->fb, reg->plane, reg->w, reg->h);
	igt_display_commit2(&drm.display, COMMIT_UNIVERSAL);
	do_assertions(0);

	igt_remove_fb(drm.fd, &new_fb);
}

/**
 * modesetfrombusy - modeset from a busy buffer to a non-busy buffer
 *
 * METHOD
 *   Set a mode, make the frontbuffer busy using BLT writes, do a modeset to a
 *   non-busy buffer, then check if the features are enabled. The goal of this
 *   test is to exercise a bug we had on the frontbuffer tracking infrastructure
 *   code.
 *
 * EXPECTED RESULTS
 *   No assertions fail.
 *
 * FAILURES
 *   If you're failing this test, then you probably need "drm/i915: Clear
 *   fb_tracking.busy_bits also for synchronous flips" or any other patch that
 *   properly updates dev_priv->fb_tracking.busy_bits when we're alternating
 *   between buffers with different busyness.
 */
static void modesetfrombusy_subtest(const struct test_mode *t)
{
	struct modeset_params *params = pick_params(t);
	struct igt_fb fb2;

	prepare_subtest(t, NULL);

	create_fb(t->format, params->primary.fb->width, params->primary.fb->height,
		  t->tiling, t->plane, &fb2);
	fill_fb(&fb2, COLOR_PRIM_BG);

	start_busy_thread(params->primary.fb);
	usleep(10000);

	unset_all_crtcs();
	params->primary.fb = &fb2;
	set_mode_for_params(params);

	do_assertions(ASSERT_NO_IDLE_GPU);

	stop_busy_thread();

	igt_remove_fb(drm.fd, &fb2);
}

/**
 * suspend - make sure suspend/resume keeps us on the same state
 *
 * METHOD
 *   Set a mode, assert FBC is there, suspend, resume, assert FBC is still
 *   there. Unset modes, assert FBC is disabled, resuspend, resume, assert FBC
 *   is still disabled.
 *
 * EXPECTED RESULTS
 *   Suspend/resume doesn't affect the FBC state.
 *
 * FAILURES
 *   A lot of different things could lead to a bug here, you'll have to check
 *   the Kernel code.
 */
static void suspend_subtest(const struct test_mode *t)
{
	struct modeset_params *params = pick_params(t);

	prepare_subtest(t, NULL);
	igt_system_suspend_autoresume(SUSPEND_STATE_MEM, SUSPEND_TEST_NONE);
	do_assertions(ASSERT_DRRS_LOW);

	unset_all_crtcs();
	igt_system_suspend_autoresume(SUSPEND_STATE_MEM, SUSPEND_TEST_NONE);
	do_assertions(ASSERT_FBC_DISABLED | ASSERT_PSR_DISABLED |
		      DONT_ASSERT_CRC | ASSERT_DRRS_INACTIVE);

	set_mode_for_params(params);
	do_assertions(0);
}

/**
 * farfromfence - test drawing as far from the fence start as possible
 *
 * METHOD
 *   One of the possible problems with FBC is that if the mode being displayed
 *   is very far away from the fence we might setup the hardware frontbuffer
 *   tracking in the wrong way. So this test tries to set a really tall FB,
 *   makes the CRTC point to the bottom of that FB, then it tries to exercise
 *   the hardware frontbuffer tracking through GTT mmap operations.
 *
 * EXPECTED RESULTS
 *   Everything succeeds.
 *
 * FAILURES
 *   If you're getting wrong CRC calulations, then the hardware tracking might
 *   be misconfigured and needs to be checked. If we're failing because FBC is
 *   disabled and the reason is that there's not enough stolen memory, then the
 *   Kernel might be calculating the amount of stolen memory needed based on the
 *   whole framebuffer size, and not just on the needed size: in this case, you
 *   need a newer Kernel.
 */
static void farfromfence_subtest(const struct test_mode *t)
{
	int r;
	struct igt_fb tall_fb;
	struct modeset_params *params = pick_params(t);
	struct draw_pattern_info *pattern = &pattern1;
	struct fb_region *target;
	int max_height, assertions = 0;
	int gen = drm.display_ver;

	switch (gen) {
	case 2:
		max_height = 2048;
		break;
	case 3:
		max_height = 4096;
		break;
	default:
		max_height = 8192;
		break;
	}

	/* Gen 9 doesn't do the same dspaddr_offset magic as the older
	 * gens, so FBC may not be enabled there. */
	if (gen >= 9)
		assertions |= DONT_ASSERT_FEATURE_STATUS;

	prepare_subtest(t, pattern);
	target = pick_target(t, params);

	create_fb(t->format, params->mode.hdisplay, max_height, t->tiling,
		  t->plane, &tall_fb);

	fill_fb(&tall_fb, COLOR_PRIM_BG);

	params->primary.fb = &tall_fb;
	params->primary.x = 0;
	params->primary.y = max_height - params->mode.vdisplay;
	set_mode_for_params(params);
	do_assertions(assertions);

	for (r = 0; r < pattern->n_rects; r++) {
		draw_rect(pattern, target, t->method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);

		/* GTT draws disable PSR. */
		do_assertions(assertions | ASSERT_PSR_DISABLED);
	}

	igt_remove_fb(drm.fd, &tall_fb);
}

/**
 * stridechange - change the frontbuffer stride by doing a modeset
 *
 * METHOD
 *   This test sets a mode on a CRTC, then creates a buffer with a different
 *   stride - still compatible with FBC -, and sets the mode on it. The Kernel
 *   currently shortcuts the modeset path for this case, so it won't trigger
 *   calls to xx_crtc_enable or xx_crtc_disable, and that could lead to
 *   problems, so test the case.
 *
 * EXPECTED RESULTS
 *   With the current Kernel, FBC may or may not remain enabled on this case,
 *   but we can still check the CRC values.
 *
 * FAILURES
 *   A bad Kernel may just not resize the CFB while keeping FBC enabled, and
 *   this can lead to underruns or stolen memory corruption. Underruns usually
 *   lead to CRC check errors, and stolen memory corruption can't be easily
 *   checked currently. A bad Kernel may also just throw some WARNs on dmesg.
 */
static void stridechange_subtest(const struct test_mode *t)
{
	struct igt_fb *new_fb, *old_fb;
	struct modeset_params *params = pick_params(t);
	int rc;

	prepare_subtest(t, NULL);

	old_fb = params->primary.fb;

	/* We can't assert that FBC will be enabled since there may not be
	 * enough space for the CFB, but we can check the CRC. */
	new_fb = &fbs[t->format].big;
	igt_assert(old_fb->strides[0] != new_fb->strides[0]);

	params->primary.fb = new_fb;
	fill_fb_region(&params->primary, t->method, COLOR_PRIM_BG);

	set_mode_for_params(params);
	do_assertions(DONT_ASSERT_FBC_STATUS);

	/* Go back to the fb that can have FBC. */
	params->primary.fb = old_fb;
	set_mode_for_params(params);
	do_assertions(0);

	/* This operation is the same as above, but with the planes API. */
	params->primary.fb = new_fb;
	set_prim_plane_for_params(params);
	do_assertions(DONT_ASSERT_FBC_STATUS);

	params->primary.fb = old_fb;
	set_prim_plane_for_params(params);
	do_assertions(0);

	/*
	 * Try to set a new stride. with the page flip api. This is allowed
	 * with the atomic page flip helper, but not with the legacy page flip.
	 */
	rc = drmModePageFlip(drm.fd, drm.display.pipes[params->pipe].crtc_id, new_fb->fb_id, 0, NULL);
	igt_assert(rc == -EINVAL || rc == 0);
	do_assertions(rc ? 0 : DONT_ASSERT_FBC_STATUS);
}

/**
 * tiling_disable_fbc_subtest - Check if tiling is unsupported by FBC
 *
 * METHOD
 *   This test alternates between a FBC supported and non-supported tiled
 *   frontbuffers of the same size and format through multiple different
 *   APIs: the page flip IOCTL, normal modesets and the plane APIs.
 *
 * EXPECTED RESULTS
 *   FBC gets properly disabled for the non-supported tiling and reenabled for
 *   the supported tiling.
 *
 * FAILURES
 *   Bad Kernels may somehow leave FBC enabled, which can cause FIFO underruns
 *   that lead to CRC assertion failures.
 */
static void tiling_disable_fbc_subtest(const struct test_mode *t)
{
	struct igt_fb new_fb, *supported_fb;
	struct modeset_params *params = pick_params(t);
	enum flip_type flip_type;
	struct test_mode supported_mode;

	memcpy(&supported_mode, t, sizeof(*t));
	supported_mode.tiling = TILING_X;
	prepare_subtest(&supported_mode, NULL);
	supported_fb = params->primary.fb;

	create_fb(t->format, params->primary.fb->width,
		  params->primary.fb->height, t->tiling, t->plane, &new_fb);
	fill_fb(&new_fb, COLOR_PRIM_BG);

	for (flip_type = 0; flip_type < FLIP_COUNT; flip_type++) {
		igt_debug("Flip type: %d\n", flip_type);

		/* Set a buffer with new tiling. */
		params->primary.fb = &new_fb;
		page_flip_for_params(params, flip_type);
		do_assertions(ASSERT_FBC_DISABLED);

		/* Put FBC back in a working state. */
		params->primary.fb = supported_fb;
		page_flip_for_params(params, flip_type);
		do_assertions(0);
	}

	igt_remove_fb(drm.fd, &new_fb);
}

/*
 * basic - do some basic operations regardless of which features are enabled
 *
 * METHOD
 *   This subtest does page flips and draw operations and checks the CRCs of the
 *   results. The big difference between this and the others is that here we
 *   don't enable/disable any features such as FBC or PSR: we go with whatever
 *   the Kernel has enabled by default for us. This subtest only does things
 *   that are exercised by the other subtests and in a less exhaustive way: it's
 *   completely redundant. On the other hand, it is very quick and was created
 *   with the CI system in mind: it's a quick way to detect regressions, so if
 *   it fails, then we can run the other subtests to find out why.
 *
 * EXPECTED RESULTS
 *   Passed CRC assertions.
 *
 * FAILURES
 *   If you get a failure here, you should run the more specific draw and flip
 *   subtests of each feature in order to discover what exactly is failing and
 *   why.
 */
static void basic_subtest(const struct test_mode *t)
{
	struct draw_pattern_info *pattern = &pattern1;
	struct modeset_params *params = pick_params(t);
	enum igt_draw_method method;
	struct igt_fb *fb1, fb2;
	int r;
	int assertions = DONT_ASSERT_FEATURE_STATUS;

	prepare_subtest(t, pattern);

	create_fb(t->format, params->primary.fb->width, params->primary.fb->height,
		  t->tiling, t->plane, &fb2);
	fb1 = params->primary.fb;

	for (r = 0, method = 0; method < IGT_DRAW_METHOD_COUNT; method++) {
		if (!igt_draw_supports_method(drm.fd, method))
			continue;

		if (r == pattern->n_rects) {
			params->primary.fb = (params->primary.fb == fb1) ? &fb2 : fb1;

			fill_fb_region(&params->primary, method, COLOR_PRIM_BG);
			update_wanted_crc(t, &blue_crcs[t->format].crc);

			page_flip_for_params(params, t->flip);
			do_assertions(assertions);

			r = 0;
		}

		draw_rect(pattern, &params->primary, method, r);
		update_wanted_crc(t, &pattern->crcs[t->format][r]);
		do_assertions(assertions);

		r++;
	}

	igt_remove_fb(drm.fd, &fb2);
}

static int opt_handler(int option, int option_index, void *data)
{
	switch (option) {
	case 's':
		opt.check_status = false;
		break;
	case 'c':
		opt.check_crc = false;
		break;
	case 'o':
		opt.fbc_check_compression = false;
		break;
	case 'a':
		opt.fbc_check_last_action = false;
		break;
	case 'e':
		opt.no_edp = true;
		break;
	case 'm':
		opt.small_modes = true;
		break;
	case 'i':
		opt.show_hidden = true;
		break;
	case 't':
		opt.step++;
		break;
	case 'x':
		errno = 0;
		opt.shared_fb_x_offset = strtol(optarg, NULL, 0);
		if (errno != 0)
			return IGT_OPT_HANDLER_ERROR;
		break;
	case 'y':
		errno = 0;
		opt.shared_fb_y_offset = strtol(optarg, NULL, 0);
		if (errno != 0)
			return IGT_OPT_HANDLER_ERROR;
		break;
	case '1':
		if (opt.only_pipes != PIPE_COUNT)
			return IGT_OPT_HANDLER_ERROR;
		opt.only_pipes = PIPE_SINGLE;
		break;
	case '2':
		if (opt.only_pipes != PIPE_COUNT)
			return IGT_OPT_HANDLER_ERROR;
		opt.only_pipes = PIPE_DUAL;
		break;
	case 'l':
		if (!strcmp(optarg, "x"))
			opt.tiling = TILING_X;
		else if (!strcmp(optarg, "y"))
			opt.tiling = TILING_Y;
		else if (!strcmp(optarg, "4"))
			opt.tiling = TILING_4;
		else if (!strcmp(optarg, "l")) {
			opt.tiling = TILING_LINEAR;
		} else {
			igt_warn("Bad tiling value: %s\n", optarg);
			return IGT_OPT_HANDLER_ERROR;
		}
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

const char *help_str =
"  --no-status-check           Don't check for enable/disable status\n"
"  --no-crc-check              Don't check for CRC values\n"
"  --no-fbc-compression-check  Don't check for the FBC compression status\n"
"  --no-fbc-action-check       Don't check for the FBC last action\n"
"  --no-edp                    Don't use eDP monitors\n"
"  --use-small-modes           Use smaller resolutions for the modes\n"
"  --show-hidden               Show hidden subtests\n"
"  --step                      Stop on each step so you can check the screen\n"
"  --shared-fb-x offset        Use 'offset' as the X offset for the shared FB\n"
"  --shared-fb-y offset        Use 'offset' as the Y offset for the shared FB\n"
"  --1p-only                   Only run subtests that use 1 pipe\n"
"  --2p-only                   Only run subtests that use 2 pipes\n"
"  --tiling tiling             Use 'tiling' as the tiling mode, which can be\n"
"                              either 'x' (default) or 'y'\n";

static const char *pipes_str(int pipes)
{
	switch (pipes) {
	case PIPE_SINGLE:
		return "1p";
	case PIPE_DUAL:
		return "2p";
	default:
		igt_assert(false);
	}
}

static const char *screen_str(int screen)
{
	switch (screen) {
	case SCREEN_PRIM:
		return "primscrn";
	case SCREEN_SCND:
		return "scndscrn";
	case SCREEN_OFFSCREEN:
		return "offscren";
	default:
		igt_assert(false);
	}
}

static const char *plane_str(int plane)
{
	switch (plane) {
	case PLANE_PRI:
		return "pri";
	case PLANE_CUR:
		return "cur";
	case PLANE_SPR:
		return "spr";
	default:
		igt_assert(false);
	}
}

static const char *fbs_str(int fb)
{
	switch (fb) {
	case FBS_INDIVIDUAL:
		return "indfb";
	case FBS_SHARED:
		return "shrfb";
	default:
		igt_assert(false);
	}
}

static const char *feature_str(int feature)
{
	switch (feature) {
	case FEATURE_NONE:
		return "nop";
	case FEATURE_FBC:
		return "fbc";
	case FEATURE_PSR:
		return "psr";
	case FEATURE_FBC | FEATURE_PSR:
		return "fbcpsr";
	case FEATURE_DRRS:
		return "drrs";
	case FEATURE_FBC | FEATURE_DRRS:
		return "fbcdrrs";
	default:
		igt_assert(false);
	}
}

static const char *format_str(enum pixel_format format)
{
	switch (format) {
	case FORMAT_RGB888:
		return "rgb888";
	case FORMAT_RGB565:
		return "rgb565";
	case FORMAT_RGB101010:
		return "rgb101010";
	default:
		igt_assert(false);
	}
}

static const char *flip_str(enum flip_type flip)
{
	switch (flip) {
	case FLIP_PAGEFLIP:
		return "pg";
	case FLIP_MODESET:
		return "ms";
	case FLIP_PLANES:
		return "pl";
	default:
		igt_assert(false);
	}
}

static const char *tiling_str(enum tiling_type tiling)
{
	switch (tiling) {
	case TILING_LINEAR:
		return "linear";
	case TILING_X:
		return "x";
	case TILING_Y:
		return "y";
	case TILING_4:
		return "4";
	default:
		igt_assert(false);
	}
}

#define TEST_MODE_ITER_BEGIN(t) \
	t.format = FORMAT_DEFAULT;					   \
	t.flip = FLIP_PAGEFLIP;						   \
	t.tiling = opt.tiling;;						   \
	for (t.feature = 0; t.feature < FEATURE_COUNT; t.feature++) {	   \
	for (t.pipes = 0; t.pipes < PIPE_COUNT; t.pipes++) {		   \
	for (t.screen = 0; t.screen < SCREEN_COUNT; t.screen++) {	   \
	for (t.plane = 0; t.plane < PLANE_COUNT; t.plane++) {		   \
	for (t.fbs = 0; t.fbs < FBS_COUNT; t.fbs++) {			   \
	for (t.method = 0; t.method < IGT_DRAW_METHOD_COUNT; t.method++) { \
		if (t.pipes == PIPE_SINGLE && t.screen == SCREEN_SCND)	   \
			continue;					   \
		if (t.screen == SCREEN_OFFSCREEN && t.plane != PLANE_PRI)  \
			continue;					   \
		if (!opt.show_hidden && t.pipes == PIPE_DUAL &&		   \
		    t.screen == SCREEN_OFFSCREEN)			   \
			continue;					   \
		if (!opt.show_hidden && t.feature == FEATURE_NONE)	   \
			continue;					   \
		if ((t.feature & FEATURE_PSR) && (t.feature & FEATURE_DRRS))\
			continue;					   \
		if (!opt.show_hidden && t.fbs == FBS_SHARED &&		   \
		    (t.plane == PLANE_CUR || t.plane == PLANE_SPR))	   \
			continue;


#define TEST_MODE_ITER_END } } } } } }

struct option long_options[] = {
	{ "no-status-check",          0, 0, 's'},
	{ "no-crc-check",             0, 0, 'c'},
	{ "no-fbc-compression-check", 0, 0, 'o'},
	{ "no-fbc-action-check",      0, 0, 'a'},
	{ "no-edp",                   0, 0, 'e'},
	{ "use-small-modes",          0, 0, 'm'},
	{ "show-hidden",              0, 0, 'i'},
	{ "step",                     0, 0, 't'},
	{ "shared-fb-x",              1, 0, 'x'},
	{ "shared-fb-y",              1, 0, 'y'},
	{ "1p-only",                  0, 0, '1'},
	{ "2p-only",                  0, 0, '2'},
	{ "tiling",                   1, 0, 'l'},
	{ 0, 0, 0, 0 }
};

igt_main_args("", long_options, help_str, opt_handler, NULL)
{
	struct test_mode t;
	enum pipe pipe;
	igt_output_t *output;

	igt_fixture {
		setup_drm();
		drm.devid = intel_get_drm_devid(drm.fd);
		drm.display_ver = intel_display_ver(drm.devid);

		/* TILING_X is not supported from Xe3 onwards. If the tiling
		 * is not set explicitly using the commandline parameter,
		 * handle the default tiling based on the platform.
		 */
		if (opt.tiling == TILING_AUTOSELECT)
			opt.tiling = drm.display_ver >= 30 ? TILING_4 : TILING_X;

		setup_environment();
	}

	for (t.feature = 0; t.feature < FEATURE_COUNT; t.feature++) {
		if (!opt.show_hidden && t.feature == FEATURE_NONE)
			continue;

		/* PSR + DRRS is not a valid combo. */
		if ((t.feature & FEATURE_PSR) && (t.feature & FEATURE_DRRS))
			continue;

		for (t.pipes = 0; t.pipes < PIPE_COUNT; t.pipes++) {
			t.screen = SCREEN_PRIM;
			t.plane = PLANE_PRI;
			t.fbs = FBS_INDIVIDUAL;
			t.format = FORMAT_DEFAULT;
			t.method = IGT_DRAW_BLT;
			/* Make sure nothing is using these values. */
			t.flip = -1;
			t.tiling = opt.tiling;

			igt_subtest_f("%s-%s-rte",
				      feature_str(t.feature),
				      pipes_str(t.pipes))
				rte_subtest(&t);
		}
	}

	t.pipes = PIPE_SINGLE;
	t.feature = FEATURE_FBC;
	t.screen = SCREEN_PRIM;
	t.fbs = FBS_INDIVIDUAL;
	t.format = FORMAT_DEFAULT;
	t.method = IGT_DRAW_BLT;
	/* Make sure nothing is using these values. */
	t.flip = -1;
	t.tiling = opt.tiling;

	igt_subtest_f("plane-fbc-rte") {
		plane_fbc_rte_subtest(&t);
	}

	igt_subtest_group {
		igt_subtest_with_dynamic("pipe-fbc-rte") {

			enum pipe default_pipe = prim_mode_params.pipe;

			t.pipes = PIPE_SINGLE;
			t.feature = FEATURE_FBC;
			t.screen = SCREEN_PRIM;
			t.fbs = FBS_INDIVIDUAL;
			t.format = FORMAT_DEFAULT;
			t.method = IGT_DRAW_BLT;
			/* Make sure nothing is using these values. */
			t.flip = -1;
			t.tiling = opt.tiling;

			/* FBC disabled: Wa_16023588340 */
			igt_skip_on_f(t.feature == FEATURE_FBC && intel_is_fbc_disabled_by_wa(drm.fd),
				      "WA has disabled FBC on BMG\n");

			for_each_pipe(&drm.display, pipe) {
				if (pipe == default_pipe) {
					igt_info("pipe-%s: FBC validated in other subtest\n", kmstest_pipe_name(pipe));
					continue;
				}

				if (!intel_fbc_supported_on_chipset(drm.fd, pipe)) {
					igt_info("Can't test FBC: not supported on pipe-%s\n", kmstest_pipe_name(pipe));
					continue;
				}

				pipe_crc = NULL;
				setup_crcs();

				for_each_valid_output_on_pipe(&drm.display, pipe, output) {
					init_mode_params(&prim_mode_params, output, pipe);
					setup_fbc();

					igt_dynamic_f("pipe-%s-%s", kmstest_pipe_name(pipe),
						      igt_output_name(output))
						rte_subtest(&t);

					break; /* One output is enough. */
				}
			}
		}

		igt_fixture
			init_modeset_cached_params();
	}

	TEST_MODE_ITER_BEGIN(t)

		igt_subtest_f("%s-%s-%s-%s-%s-draw-%s",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs),
			      igt_draw_get_method_name(t.method))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			draw_subtest(&t);
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.plane != PLANE_PRI ||
		    t.screen == SCREEN_OFFSCREEN ||
		    (!opt.show_hidden && t.method != IGT_DRAW_BLT))
			continue;

		for (t.flip = 0; t.flip < FLIP_COUNT; t.flip++)
			igt_subtest_f("%s-%s-%s-%s-%sflip-%s",
				      feature_str(t.feature),
				      pipes_str(t.pipes),
				      screen_str(t.screen),
				      fbs_str(t.fbs),
				      flip_str(t.flip),
				      igt_draw_get_method_name(t.method))
			{
				igt_require(igt_draw_supports_method(drm.fd, t.method));
				flip_subtest(&t);
			}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.plane != PLANE_PRI ||
		    t.screen != SCREEN_PRIM ||
		    t.method != IGT_DRAW_MMAP_GTT ||
		    (t.feature & FEATURE_FBC) == 0)
			continue;

		igt_subtest_f("%s-%s-%s-fliptrack-%s",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      fbs_str(t.fbs),
			      igt_draw_get_method_name(t.method))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			fliptrack_subtest(&t, FLIP_PAGEFLIP);
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.screen == SCREEN_OFFSCREEN ||
		    t.method != IGT_DRAW_BLT ||
		    t.plane == PLANE_PRI)
			continue;

		igt_subtest_f("%s-%s-%s-%s-%s-move",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			move_subtest(&t);
		}

		igt_subtest_f("%s-%s-%s-%s-%s-onoff",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			onoff_subtest(&t);
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.screen == SCREEN_OFFSCREEN ||
		    t.method != IGT_DRAW_BLT ||
		    t.plane != PLANE_SPR)
			continue;


		igt_subtest_f("%s-%s-%s-%s-%s-fullscreen",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      screen_str(t.screen),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			fullscreen_plane_subtest(&t);
		}

	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.screen != SCREEN_PRIM ||
		    t.method != IGT_DRAW_BLT ||
		    (!opt.show_hidden && t.plane != PLANE_PRI) ||
		    (!opt.show_hidden && t.fbs != FBS_INDIVIDUAL))
			continue;

		igt_subtest_f("%s-%s-%s-%s-multidraw",
			      feature_str(t.feature),
			      pipes_str(t.pipes),
			      plane_str(t.plane),
			      fbs_str(t.fbs))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			multidraw_subtest(&t);
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.fbs != FBS_INDIVIDUAL ||
		    t.method != IGT_DRAW_MMAP_GTT)
			continue;

		igt_subtest_f("%s-farfromfence-%s",
			      feature_str(t.feature),
			      igt_draw_get_method_name(t.method))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			farfromfence_subtest(&t);
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.fbs != FBS_INDIVIDUAL)
			continue;

		for (t.format = 0; t.format < FORMAT_COUNT; t.format++) {
			/* Skip what we already tested. */
			if (t.format == FORMAT_DEFAULT)
				continue;

			igt_subtest_f("%s-%s-draw-%s",
				      feature_str(t.feature),
				      format_str(t.format),
				      igt_draw_get_method_name(t.method))
			{
				igt_require(igt_draw_supports_method(drm.fd, t.method));
				format_draw_subtest(&t);
			}
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.method != IGT_DRAW_BLT)
			continue;

		igt_subtest_f("%s-%s-scaledprimary",
			      feature_str(t.feature),
			      fbs_str(t.fbs))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			scaledprimary_subtest(&t);
		}
	TEST_MODE_ITER_END

	TEST_MODE_ITER_BEGIN(t)
		if (t.pipes != PIPE_SINGLE ||
		    t.screen != SCREEN_PRIM ||
		    t.plane != PLANE_PRI ||
		    t.fbs != FBS_INDIVIDUAL ||
		    t.method != IGT_DRAW_BLT)
			continue;

		igt_subtest_f("%s-modesetfrombusy", feature_str(t.feature))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			modesetfrombusy_subtest(&t);
		}

		if (t.feature & FEATURE_FBC) {
			igt_subtest_f("%s-stridechange", feature_str(t.feature))
			{
				igt_require(igt_draw_supports_method(drm.fd, t.method));
				stridechange_subtest(&t);
			}

			for (t.tiling = TILING_LINEAR; t.tiling < TILING_COUNT;
			     t.tiling++) {
				if (t.tiling == TILING_X)
					continue;

				igt_describe("Test the tiling formats, if the "
					     "tiling format supports FBC do"
					     "the basic drawing test if not "
					     "set the mode and test if FBC is "
					     "disabled");
				igt_subtest_f("%s-tiling-%s",
					      feature_str(t.feature),
					      tiling_str(t.tiling)) {

					igt_require(igt_draw_supports_method(drm.fd, t.method));

					if (t.tiling == TILING_Y) {
						igt_require(intel_gen(drm.devid) >= 9);
						igt_require(!intel_get_device_info(drm.devid)->has_4tile);
					}

					if (t.tiling == TILING_4)
						igt_require(intel_get_device_info(drm.devid)->has_4tile);

					if (tiling_is_valid(t.feature, t.tiling))
						draw_subtest(&t);
					else
						tiling_disable_fbc_subtest(&t);
				}
			}
			t.tiling = opt.tiling;
		}

		if ((t.feature & FEATURE_PSR) || (t.feature & FEATURE_DRRS))
			igt_subtest_f("%s-slowdraw", feature_str(t.feature))
			{
				igt_require(igt_draw_supports_method(drm.fd, t.method));
				slow_draw_subtest(&t);
			}

		igt_subtest_f("%s-suspend", feature_str(t.feature))
		{
			igt_require(igt_draw_supports_method(drm.fd, t.method));
			suspend_subtest(&t);
		}
	TEST_MODE_ITER_END

	t.pipes = PIPE_SINGLE;
	t.screen = SCREEN_PRIM;
	t.plane = PLANE_PRI;
	t.fbs = FBS_INDIVIDUAL;
	t.feature = FEATURE_DEFAULT;
	t.format = FORMAT_DEFAULT;
	t.flip = FLIP_PAGEFLIP;
	t.method = IGT_DRAW_BLT;
	t.tiling = opt.tiling;

	igt_subtest("basic") {
		if (!is_xe_device(drm.fd))
			igt_require_gem(drm.fd);
		basic_subtest(&t);
	}

	igt_fixture
		teardown_environment();
}
