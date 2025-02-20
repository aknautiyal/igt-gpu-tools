/*
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
 * Copyright 2015 Philip Taylor <philip@zaynar.co.uk>
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <math.h>

#include "igt_halffloat.h"
#include "igt_x86.h"

typedef union { float f; int32_t i; uint32_t u; } fi_type;

/**
 * Convert a 4-byte float to a 2-byte half float.
 *
 * Not all float32 values can be represented exactly as a float16 value. We
 * round such intermediate float32 values to the nearest float16. When the
 * float32 lies exactly between to float16 values, we round to the one with
 * an even mantissa.
 *
 * This rounding behavior has several benefits:
 *   - It has no sign bias.
 *
 *   - It reproduces the behavior of real hardware: opcode F32TO16 in Intel's
 *     GPU ISA.
 *
 *   - By reproducing the behavior of the GPU (at least on Intel hardware),
 *     compile-time evaluation of constant packHalf2x16 GLSL expressions will
 *     result in the same value as if the expression were executed on the GPU.
 */
static inline uint16_t _float_to_half(float val)
{
	const fi_type fi = {val};
	const int flt_m = fi.i & 0x7fffff;
	const int flt_e = (fi.i >> 23) & 0xff;
	const int flt_s = (fi.i >> 31) & 0x1;
	int s, e, m = 0;
	uint16_t result;

	/* sign bit */
	s = flt_s;

	/* handle special cases */
	if ((flt_e == 0) && (flt_m == 0)) {
		/* zero */
		/* m = 0; - already set */
		e = 0;
	} else if ((flt_e == 0) && (flt_m != 0)) {
		/* denorm -- denorm float maps to 0 half */
		/* m = 0; - already set */
		e = 0;
	} else if ((flt_e == 0xff) && (flt_m == 0)) {
		/* infinity */
		/* m = 0; - already set */
		e = 31;
	} else if ((flt_e == 0xff) && (flt_m != 0)) {
		/* NaN */
		m = 1;
		e = 31;
	} else {
		/* regular number */
		const int new_exp = flt_e - 127;
		if (new_exp < -14) {
			/* The float32 lies in the range (0.0, min_normal16) and
			 * is rounded to a nearby float16 value. The result will
			 * be either zero, subnormal, or normal.
			 */
			e = 0;
			m = lrintf((1 << 24) * fabsf(fi.f));
		} else if (new_exp > 15) {
			/* map this value to infinity */
			/* m = 0; - already set */
			e = 31;
		} else {
			/* The float32 lies in the range
			 *   [min_normal16, max_normal16 + max_step16)
			 * and is rounded to a nearby float16 value. The result
			 * will be either normal or infinite.
			 */
			e = new_exp + 15;
			m = lrintf(flt_m / (float)(1 << 13));
		}
	}

	assert(0 <= m && m <= 1024);
	if (m == 1024) {
		/* The float32 was rounded upwards into the range of the next
		 * exponent, so bump the exponent. This correctly handles the
		 * case where f32 should be rounded up to float16 infinity.
		 */
		++e;
		m = 0;
	}

	result = (s << 15) | (e << 10) | m;
	return result;
}

/**
 * Convert a 2-byte half float to a 4-byte float.
 * Based on code from:
 * http://www.opengl.org/discussion_boards/ubb/Forum3/HTML/008786.html
 */
static inline float _half_to_float(uint16_t val)
{
	/* XXX could also use a 64K-entry lookup table */
	const int m = val & 0x3ff;
	const int e = (val >> 10) & 0x1f;
	const int s = (val >> 15) & 0x1;
	int flt_m, flt_e, flt_s;
	fi_type fi;

	/* sign bit */
	flt_s = s;

	/* handle special cases */
	if ((e == 0) && (m == 0)) {
		/* zero */
		flt_m = 0;
		flt_e = 0;
	} else if ((e == 0) && (m != 0)) {
		/* denorm -- denorm half will fit in non-denorm single */
		const float half_denorm = 1.0f / 16384.0f; /* 2^-14 */
		float mantissa = ((float) (m)) / 1024.0f;
		float sign = s ? -1.0f : 1.0f;
		return sign * mantissa * half_denorm;
	} else if ((e == 31) && (m == 0)) {
		/* infinity */
		flt_e = 0xff;
		flt_m = 0;
	} else if ((e == 31) && (m != 0)) {
		/* NaN */
		flt_e = 0xff;
		flt_m = 1;
	} else {
		/* regular */
		flt_e = e + 112;
		flt_m = m << 13;
	}

	fi.i = (flt_s << 31) | (flt_e << 23) | flt_m;
	return fi.f;
}

#if defined(__x86_64__) && !defined(__clang__) && defined(__GLIBC__) && !defined(__UCLIBC__)
#pragma GCC push_options
#pragma GCC target("f16c")

#include <immintrin.h>

static void float_to_half_f16c(const float *f, uint16_t *h, unsigned int num)
{
	for (int i = 0; i < num; i++)
		h[i] = _cvtss_sh(f[i], 0);
}

static void half_to_float_f16c(const uint16_t *h, float *f, unsigned int num)
{
	for (int i = 0; i < num; i++)
		f[i] = _cvtsh_ss(h[i]);
}

#pragma GCC pop_options

static void float_to_half(const float *f, uint16_t *h, unsigned int num)
{
	for (int i = 0; i < num; i++)
		h[i] = _float_to_half(f[i]);
}

static void half_to_float(const uint16_t *h, float *f, unsigned int num)
{
	for (int i = 0; i < num; i++)
		f[i] = _half_to_float(h[i]);
}

/* The PLT is not initialized when ifunc resolvers run, so all external
 * functions must be inlined with __attribute__((flatten)).
 */
__attribute__((flatten))
static void (*resolve_float_to_half(void))(const float *f, uint16_t *h, unsigned int num)
{
	if (igt_x86_features() & F16C)
		return float_to_half_f16c;

	return float_to_half;
}

void igt_float_to_half(const float *f, uint16_t *h, unsigned int num)
	__attribute__((ifunc("resolve_float_to_half")));

/* The PLT is not initialized when ifunc resolvers run, so all external
 * functions must be inlined with __attribute__((flatten)).
 */
__attribute__((flatten))
static void (*resolve_half_to_float(void))(const uint16_t *h, float *f, unsigned int num)
{
	if (igt_x86_features() & F16C)
		return half_to_float_f16c;

	return half_to_float;
}

void igt_half_to_float(const uint16_t *h, float *f, unsigned int num)
	__attribute__((ifunc("resolve_half_to_float")));

#else

void igt_float_to_half(const float *f, uint16_t *h, unsigned int num)
{
	for (int i = 0; i < num; i++)
		h[i] = _float_to_half(f[i]);
}

void igt_half_to_float(const uint16_t *h, float *f, unsigned int num)
{
	for (int i = 0; i < num; i++)
		f[i] = _half_to_float(h[i]);
}

#endif

