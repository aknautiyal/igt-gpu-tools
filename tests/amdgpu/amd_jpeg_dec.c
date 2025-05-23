// SPDX-License-Identifier: MIT
// Copyright 2023 Advanced Micro Devices, Inc.
// Copyright 2017 Advanced Micro Devices, Inc.

#include "lib/amdgpu/amd_mmd_shared.h"

/* jpeg registers */
#define mmUVD_JPEG_CNTL				0x0200
#define mmUVD_JPEG_RB_BASE			0x0201
#define mmUVD_JPEG_RB_WPTR			0x0202
#define mmUVD_JPEG_RB_RPTR			0x0203
#define mmUVD_JPEG_RB_SIZE			0x0204
#define mmUVD_JPEG_TIER_CNTL2			0x021a
#define mmUVD_JPEG_UV_TILING_CTRL		0x021c
#define mmUVD_JPEG_TILING_CTRL			0x021e
#define mmUVD_JPEG_OUTBUF_RPTR			0x0220
#define mmUVD_JPEG_OUTBUF_WPTR			0x0221
#define mmUVD_JPEG_PITCH			0x0222
#define mmUVD_JPEG_INT_EN			0x0229
#define mmUVD_JPEG_UV_PITCH			0x022b
#define mmUVD_JPEG_INDEX			0x023e
#define mmUVD_JPEG_DATA				0x023f
#define mmUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH	0x0438
#define mmUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW	0x0439
#define mmUVD_LMI_JPEG_READ_64BIT_BAR_HIGH	0x045a
#define mmUVD_LMI_JPEG_READ_64BIT_BAR_LOW	0x045b
#define mmUVD_CTX_INDEX				0x0528
#define mmUVD_CTX_DATA				0x0529
#define mmUVD_SOFT_RESET			0x05a0

#define vcnipUVD_JPEG_DEC_SOFT_RST		0x402f
#define vcnipUVD_JRBC_IB_COND_RD_TIMER		0x408e
#define vcnipUVD_JRBC_IB_REF_DATA		0x408f
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH	0x40e1
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW	0x40e0
#define vcnipUVD_JPEG_RB_BASE			0x4001
#define vcnipUVD_JPEG_RB_SIZE			0x4004
#define vcnipUVD_JPEG_RB_WPTR			0x4002
#define vcnipUVD_JPEG_PITCH			0x401f
#define vcnipUVD_JPEG_UV_PITCH			0x4020
#define vcnipJPEG_DEC_ADDR_MODE			0x4027
#define vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE	0x4024
#define vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE	0x4025
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH	0x40e3
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW	0x40e2
#define vcnipUVD_JPEG_INDEX			0x402c
#define vcnipUVD_JPEG_DATA			0x402d
#define vcnipUVD_JPEG_TIER_CNTL2		0x400f
#define vcnipUVD_JPEG_OUTBUF_RPTR		0x401e
#define vcnipUVD_JPEG_OUTBUF_CNTL		0x401c
#define vcnipUVD_JPEG_INT_EN			0x400a
#define vcnipUVD_JPEG_CNTL			0x4000
#define vcnipUVD_JPEG_RB_RPTR			0x4003
#define vcnipUVD_JPEG_OUTBUF_WPTR		0x401d

#define vcnipUVD_JPEG_DEC_SOFT_RST_1             0x4051
#define vcnipUVD_JPEG_PITCH_1                    0x4043
#define vcnipUVD_JPEG_UV_PITCH_1                 0x4044
#define vcnipJPEG_DEC_ADDR_MODE_1                0x404B
#define vcnipUVD_JPEG_TIER_CNTL2_1               0x400E
#define vcnipUVD_JPEG_OUTBUF_CNTL_1              0x4040
#define vcnipUVD_JPEG_OUTBUF_WPTR_1              0x4041
#define vcnipUVD_JPEG_OUTBUF_RPTR_1              0x4042
#define vcnipUVD_JPEG_LUMA_BASE0_0               0x41C0
#define vcnipUVD_JPEG_CHROMA_BASE0_0             0x41C1
#define vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE_1   0x4048
#define vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE_1  0x4049
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH_1 0x40B5
#define vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW_1  0x40B4
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH_1  0x40B3
#define vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW_1   0x40B2

static uint32_t jpeg_dec_soft_rst;
static uint32_t jrbc_ib_cond_rd_timer;
static uint32_t jrbc_ib_ref_data;
static uint32_t lmi_jpeg_read_64bit_bar_high;
static uint32_t lmi_jpeg_read_64bit_bar_low;
static uint32_t jpeg_rb_base;
static uint32_t jpeg_rb_size;
static uint32_t jpeg_rb_wptr;
static uint32_t jpeg_pitch;
static uint32_t jpeg_uv_pitch;
static uint32_t dec_addr_mode;
static uint32_t dec_y_gfx10_tiling_surface;
static uint32_t dec_uv_gfx10_tiling_surface;
static uint32_t lmi_jpeg_write_64bit_bar_high;
static uint32_t lmi_jpeg_write_64bit_bar_low;
static uint32_t jpeg_tier_cntl2;
static uint32_t jpeg_outbuf_rptr;
static uint32_t jpeg_outbuf_cntl;
static uint32_t jpeg_int_en;
static uint32_t jpeg_cntl;
static uint32_t jpeg_rb_rptr;
static uint32_t jpeg_outbuf_wptr;
static uint32_t jpeg_luma_base0_0;
static uint32_t jpeg_chroma_base0_0;

#define RDECODE_PKT_REG_J(x)		((unsigned int)(x)&0x3FFFF)
#define RDECODE_PKT_RES_J(x)		(((unsigned int)(x)&0x3F) << 18)
#define RDECODE_PKT_COND_J(x)		(((unsigned int)(x)&0xF) << 24)
#define RDECODE_PKT_TYPE_J(x)		(((unsigned int)(x)&0xF) << 28)
#define RDECODE_PKTJ(reg, cond, type)	(RDECODE_PKT_REG_J(reg) | \
					 RDECODE_PKT_RES_J(0) | \
					 RDECODE_PKT_COND_J(cond) | \
					 RDECODE_PKT_TYPE_J(type))

#define UVD_BASE_INST0_SEG1		0x00007E00
#define SOC15_REG_ADDR(reg)		(UVD_BASE_INST0_SEG1 + reg)

#define COND0				0
#define COND1				1
#define COND3				3
#define TYPE0				0
#define TYPE1				1
#define TYPE3				3
#define JPEG_DEC_DT_PITCH       0x100
#define WIDTH                   64
#define JPEG_DEC_BSD_SIZE       0x200
#define JPEG_DEC_LUMA_OFFSET    0
#define JPEG_DEC_CHROMA_OFFSET  0x4000
#define JPEG_DEC_SUM            262144
#define IB_SIZE                 4096
#define MAX_RESOURCES           16

static bool
is_jpeg_tests_enable(amdgpu_device_handle device_handle,
		struct mmd_shared_context *context)
{
	struct drm_amdgpu_info_hw_ip info;
	int r;

	r = amdgpu_query_hw_ip_info(device_handle, AMDGPU_HW_IP_VCN_JPEG, 0, &info);

	if (r != 0 || !info.available_rings ||
			(context->family_id < AMDGPU_FAMILY_RV &&
			(context->family_id == AMDGPU_FAMILY_AI &&
			(context->chip_id - context->chip_rev) < 0x32))) { /* Arcturus */
		igt_info("\n\nThe ASIC NOT support JPEG, test disabled\n");
		return false;
	}

	if (info.hw_ip_version_major == 1)
		context->jpeg_direct_reg = false;
	else if (info.hw_ip_version_major > 1 && info.hw_ip_version_major <= 4)
		context->jpeg_direct_reg = true;
	else
		return false;

	jrbc_ib_cond_rd_timer = vcnipUVD_JRBC_IB_COND_RD_TIMER;
	jrbc_ib_ref_data = vcnipUVD_JRBC_IB_REF_DATA;
	jpeg_rb_base = vcnipUVD_JPEG_RB_BASE;
	jpeg_rb_size = vcnipUVD_JPEG_RB_SIZE;
	jpeg_rb_wptr = vcnipUVD_JPEG_RB_WPTR;
	jpeg_int_en = vcnipUVD_JPEG_INT_EN;
	jpeg_cntl = vcnipUVD_JPEG_CNTL;
	jpeg_rb_rptr = vcnipUVD_JPEG_RB_RPTR;

	if (context->family_id == AMDGPU_FAMILY_AI &&
		(context->chip_id - context->chip_rev) > 0x3c) { /* gfx940 */
		jpeg_dec_soft_rst = vcnipUVD_JPEG_DEC_SOFT_RST_1;
		lmi_jpeg_read_64bit_bar_high = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH_1;
		lmi_jpeg_read_64bit_bar_low = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW_1;
		jpeg_pitch = vcnipUVD_JPEG_PITCH_1;
		jpeg_uv_pitch = vcnipUVD_JPEG_UV_PITCH_1;
		dec_addr_mode = vcnipJPEG_DEC_ADDR_MODE_1;
		dec_y_gfx10_tiling_surface = vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE_1;
		dec_uv_gfx10_tiling_surface = vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE_1;
		lmi_jpeg_write_64bit_bar_high = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH_1;
		lmi_jpeg_write_64bit_bar_low = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW_1;
		jpeg_tier_cntl2 = vcnipUVD_JPEG_TIER_CNTL2_1;
		jpeg_outbuf_cntl = vcnipUVD_JPEG_OUTBUF_CNTL_1;
		jpeg_outbuf_rptr = vcnipUVD_JPEG_OUTBUF_RPTR_1;
		jpeg_outbuf_wptr = vcnipUVD_JPEG_OUTBUF_WPTR_1;
		jpeg_luma_base0_0 = vcnipUVD_JPEG_LUMA_BASE0_0;
		jpeg_chroma_base0_0 = vcnipUVD_JPEG_CHROMA_BASE0_0;
	} else {
		jpeg_dec_soft_rst = vcnipUVD_JPEG_DEC_SOFT_RST;
		lmi_jpeg_read_64bit_bar_high = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_HIGH;
		lmi_jpeg_read_64bit_bar_low = vcnipUVD_LMI_JPEG_READ_64BIT_BAR_LOW;
		jpeg_pitch = vcnipUVD_JPEG_PITCH;
		jpeg_uv_pitch = vcnipUVD_JPEG_UV_PITCH;
		dec_addr_mode = vcnipJPEG_DEC_ADDR_MODE;
		dec_y_gfx10_tiling_surface = vcnipJPEG_DEC_Y_GFX10_TILING_SURFACE;
		dec_uv_gfx10_tiling_surface = vcnipJPEG_DEC_UV_GFX10_TILING_SURFACE;
		lmi_jpeg_write_64bit_bar_high = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH;
		lmi_jpeg_write_64bit_bar_low = vcnipUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW;
		jpeg_tier_cntl2 = vcnipUVD_JPEG_TIER_CNTL2;
		jpeg_outbuf_cntl = vcnipUVD_JPEG_OUTBUF_CNTL;
		jpeg_outbuf_rptr = vcnipUVD_JPEG_OUTBUF_RPTR;
		jpeg_outbuf_wptr = vcnipUVD_JPEG_OUTBUF_WPTR;
	}

	return true;
}


static void
set_reg_jpeg(struct mmd_context *context, uint32_t reg, uint32_t cond,
		uint32_t type, uint32_t val, uint32_t *idx)
{
	context->ib_cpu[(*idx)++] = RDECODE_PKTJ(reg, cond, type);
	context->ib_cpu[(*idx)++] = val;
}

/* send a bitstream buffer command */
static void
send_cmd_bitstream(struct mmd_context *context, uint64_t addr, uint32_t *idx)
{
	/* jpeg soft reset */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 1, idx);

	/* ensuring the Reset is asserted in SCLK domain */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0,
			0x01C2, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0,
			0x01400200, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0,
			0x01C3, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0,
			(1 << 9), idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3,
			(1 << 9), idx);

	/* wait mem */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0,
			0, idx);

	/* ensuring the Reset is de-asserted in SCLK domain */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0,
			0x01C3, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0,
			(0 << 9), idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3,
			(1 << 9), idx);

	/* set UVD_LMI_JPEG_READ_64BIT_BAR_LOW/HIGH based on bitstream buffer address */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_LMI_JPEG_READ_64BIT_BAR_HIGH),
			COND0, TYPE0, (addr >> 32), idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_LMI_JPEG_READ_64BIT_BAR_LOW),
			COND0, TYPE0, (uint32_t)addr, idx);

	/* set jpeg_rb_base */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_RB_BASE), COND0, TYPE0,
			0, idx);

	/* set jpeg_rb_base */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_RB_SIZE), COND0, TYPE0,
			0xFFFFFFF0, idx);

	/* set jpeg_rb_wptr */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_RB_WPTR), COND0, TYPE0,
			(JPEG_DEC_BSD_SIZE >> 2), idx);
}

/* send a target buffer command */
static void
send_cmd_target(struct mmd_context *context, uint64_t addr,
		uint32_t *idx)
{

	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_PITCH), COND0, TYPE0,
				(JPEG_DEC_DT_PITCH >> 4), idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_UV_PITCH), COND0, TYPE0,
				(JPEG_DEC_DT_PITCH >> 4), idx);

	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_TILING_CTRL), COND0, TYPE0,
			0, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_UV_TILING_CTRL), COND0, TYPE0,
			0, idx);

	/* set UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW/HIGH based on target buffer address */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_LMI_JPEG_WRITE_64BIT_BAR_HIGH),
			COND0, TYPE0, (addr >> 32), idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_LMI_JPEG_WRITE_64BIT_BAR_LOW),
			COND0, TYPE0, (uint32_t)addr, idx);

	/* set output buffer data address */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_INDEX), COND0, TYPE0, 0, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_DATA), COND0, TYPE0,
			JPEG_DEC_LUMA_OFFSET, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_INDEX), COND0, TYPE0, 1, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_DATA), COND0, TYPE0,
			JPEG_DEC_CHROMA_OFFSET, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_TIER_CNTL2), COND0, TYPE3,
			0, idx);

	/* set output buffer read pointer */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_OUTBUF_RPTR), COND0, TYPE0,
			0, idx);

	/* enable error interrupts */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_INT_EN), COND0, TYPE0,
			0xFFFFFFFE, idx);

	/* start engine command */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0,
			0x6, idx);

	/* wait for job completion, wait for job JBSI fetch done */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0,
			0x01C3, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0,
			(JPEG_DEC_BSD_SIZE >> 2), idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0,
			0x01C2, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0,
			0x01400200, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_RB_RPTR), COND0, TYPE3,
			0xFFFFFFFF, idx);

	/* wait for job jpeg outbuf idle */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0,
			0x01C3, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0,
			0xFFFFFFFF, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_OUTBUF_WPTR), COND0, TYPE3,
			0x00000001, idx);

	/* stop engine */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0,
			0x4, idx);

	/* asserting jpeg lmi drop */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0,
			0x0005, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0,
			(1 << 23 | 1 << 0), idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE1, 0, idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0, idx);

	/* asserting jpeg reset */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 1, idx);

	/* ensure reset is asserted in sclk domain */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3,
			idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (1 << 9),
			idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9),
			idx);

	/* de-assert jpeg reset */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_JPEG_CNTL), COND0, TYPE0, 0, idx);

	/* ensure reset is de-asserted in sclk domain */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x01C3,
			idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, (0 << 9),
			idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_SOFT_RESET), COND0, TYPE3, (1 << 9),
			idx);

	/* de-asserting jpeg lmi drop */
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_INDEX), COND0, TYPE0, 0x0005,
			idx);
	set_reg_jpeg(context, SOC15_REG_ADDR(mmUVD_CTX_DATA), COND0, TYPE0, 0, idx);
}

/* send a bitstream buffer command */
static void
send_cmd_bitstream_direct(struct mmd_context *context, uint64_t addr,
		uint32_t *idx)
{

	/* jpeg soft reset */
	set_reg_jpeg(context, jpeg_dec_soft_rst, COND0, TYPE0, 1, idx);

	/* ensuring the Reset is asserted in SCLK domain */
	set_reg_jpeg(context, jrbc_ib_cond_rd_timer, COND0, TYPE0, 0x01400200, idx);
	set_reg_jpeg(context, jrbc_ib_ref_data, COND0, TYPE0, (0x1 << 0x10), idx);
	set_reg_jpeg(context, jpeg_dec_soft_rst, COND3, TYPE3, (0x1 << 0x10), idx);

	/* wait mem */
	set_reg_jpeg(context, jpeg_dec_soft_rst, COND0, TYPE0, 0, idx);

	/* ensuring the Reset is de-asserted in SCLK domain */
	set_reg_jpeg(context, jrbc_ib_ref_data, COND0, TYPE0, (0 << 0x10), idx);
	set_reg_jpeg(context, jpeg_dec_soft_rst, COND3, TYPE3, (0x1 << 0x10), idx);

	/* set UVD_LMI_JPEG_READ_64BIT_BAR_LOW/HIGH based on bitstream buffer address */
	set_reg_jpeg(context, lmi_jpeg_read_64bit_bar_high, COND0, TYPE0,
			(addr >> 32), idx);
	set_reg_jpeg(context, lmi_jpeg_read_64bit_bar_low, COND0, TYPE0,
			addr, idx);

	/* set jpeg_rb_base */
	set_reg_jpeg(context, jpeg_rb_base, COND0, TYPE0, 0, idx);

	/* set jpeg_rb_base */
	set_reg_jpeg(context, jpeg_rb_size, COND0, TYPE0, 0xFFFFFFF0, idx);

	/* set jpeg_rb_wptr */
	set_reg_jpeg(context, jpeg_rb_wptr, COND0, TYPE0,
			(JPEG_DEC_BSD_SIZE >> 2), idx);
}

/* send a target buffer command */
static void
send_cmd_target_direct(struct mmd_context *context, uint64_t addr,
		uint32_t *idx)
{

	set_reg_jpeg(context, jpeg_pitch, COND0, TYPE0,
			(JPEG_DEC_DT_PITCH >> 4), idx);
	set_reg_jpeg(context, jpeg_uv_pitch, COND0, TYPE0,
			(JPEG_DEC_DT_PITCH >> 4), idx);

	set_reg_jpeg(context, dec_addr_mode, COND0, TYPE0, 0, idx);
	set_reg_jpeg(context, dec_y_gfx10_tiling_surface, COND0, TYPE0,
			0, idx);
	set_reg_jpeg(context, dec_uv_gfx10_tiling_surface, COND0, TYPE0,
			0, idx);

	/* set UVD_LMI_JPEG_WRITE_64BIT_BAR_LOW/HIGH based on target buffer address */
	set_reg_jpeg(context, lmi_jpeg_write_64bit_bar_high, COND0, TYPE0,
			(addr >> 32), idx);
	set_reg_jpeg(context, lmi_jpeg_write_64bit_bar_low, COND0, TYPE0,
			addr, idx);

	/* set output buffer data address */
	if (jpeg_luma_base0_0) {
		set_reg_jpeg(context, jpeg_luma_base0_0, COND0, TYPE0,
			JPEG_DEC_LUMA_OFFSET, idx);
		set_reg_jpeg(context, jpeg_chroma_base0_0, COND0, TYPE0,
			JPEG_DEC_CHROMA_OFFSET, idx);
	} else {
		set_reg_jpeg(context, vcnipUVD_JPEG_INDEX, COND0, TYPE0, 0, idx);
		set_reg_jpeg(context, vcnipUVD_JPEG_DATA, COND0, TYPE0,
			JPEG_DEC_LUMA_OFFSET, idx);
		set_reg_jpeg(context, vcnipUVD_JPEG_INDEX, COND0, TYPE0, 1, idx);
		set_reg_jpeg(context, vcnipUVD_JPEG_DATA, COND0, TYPE0,
			JPEG_DEC_CHROMA_OFFSET, idx);
	}
	set_reg_jpeg(context, jpeg_tier_cntl2, COND0, 0, 0, idx);

	/* set output buffer read pointer */
	set_reg_jpeg(context, jpeg_outbuf_rptr, COND0, TYPE0, 0, idx);
	set_reg_jpeg(context, jpeg_outbuf_cntl, COND0, TYPE0,
				((0x00001587 & (~0x00000180L)) | (0x1 << 0x7) | (0x1 << 0x6)),
				 idx);

	/* enable error interrupts */
	set_reg_jpeg(context, jpeg_int_en, COND0, TYPE0, 0xFFFFFFFE, idx);

	/* start engine command */
	set_reg_jpeg(context, jpeg_cntl, COND0, TYPE0, 0xE, idx);

	/* wait for job completion, wait for job JBSI fetch done */
	set_reg_jpeg(context, jrbc_ib_ref_data, COND0, TYPE0,
			(JPEG_DEC_BSD_SIZE >> 2), idx);
	set_reg_jpeg(context, jrbc_ib_cond_rd_timer, COND0, TYPE0,
			0x01400200, idx);
	set_reg_jpeg(context, jpeg_rb_rptr, COND3, TYPE3, 0xFFFFFFFF, idx);

	/* wait for job jpeg outbuf idle */
	set_reg_jpeg(context, jrbc_ib_ref_data, COND0, TYPE0, 0xFFFFFFFF,
			idx);
	set_reg_jpeg(context, jpeg_outbuf_wptr, COND3, TYPE3, 0x00000001,
			idx);

	/* stop engine */
	set_reg_jpeg(context, jpeg_cntl, COND0, TYPE0, 0x4, idx);
}

static void
amdgpu_cs_jpeg_decode(amdgpu_device_handle device_handle,
		struct mmd_shared_context *shared_context)
{

	struct amdgpu_mmd_bo dec_buf;
	int size, r;
	uint8_t *dec;
	int sum = 0, i, j;
	uint32_t idx;
	struct mmd_context acontext = {0};
	struct mmd_context *context = &acontext;

	r = mmd_context_init(device_handle, context);
	igt_assert_eq(r, 0);
	size = 32 * 1024; /* 8K bitstream + 24K output */

	context->num_resources = 0;
	alloc_resource(device_handle, &dec_buf, size, AMDGPU_GEM_DOMAIN_VRAM);
	context->resources[context->num_resources++] = dec_buf.handle;
	context->resources[context->num_resources++] = context->ib_handle;
	r = amdgpu_bo_cpu_map(dec_buf.handle, (void **)&dec_buf.ptr);
	igt_assert_eq(r, 0);
	memcpy(dec_buf.ptr, jpeg_bitstream, sizeof(jpeg_bitstream));

	idx = 0;

	if (shared_context->jpeg_direct_reg == true) {
		send_cmd_bitstream_direct(context, dec_buf.addr, &idx);
		send_cmd_target_direct(context, dec_buf.addr + (size / 4), &idx);
	} else {
		send_cmd_bitstream(context, dec_buf.addr, &idx);
		send_cmd_target(context, dec_buf.addr + (size / 4), &idx);
	}

	amdgpu_bo_cpu_unmap(dec_buf.handle);
	r = submit(device_handle, context, idx, AMDGPU_HW_IP_VCN_JPEG);
	igt_assert_eq(r, 0);

	r = amdgpu_bo_cpu_map(dec_buf.handle, (void **)&dec_buf.ptr);
	igt_assert_eq(r, 0);

	dec = dec_buf.ptr + (size / 4);

	/* calculate result checksum */
	for (i = 0; i < WIDTH; i++)
		for (j = 0; j < WIDTH; j++)
			sum += *((dec + JPEG_DEC_LUMA_OFFSET + i * JPEG_DEC_DT_PITCH) + j);
	for (i = 0; i < (WIDTH/2); i++)
		for (j = 0; j < WIDTH; j++)
			sum += *((dec + JPEG_DEC_CHROMA_OFFSET + i * JPEG_DEC_DT_PITCH) + j);

	amdgpu_bo_cpu_unmap(dec_buf.handle);
	igt_assert_eq(sum, JPEG_DEC_SUM);

	free_resource(&dec_buf);
	mmd_context_clean(device_handle, context);
}

igt_main
{
	amdgpu_device_handle device;
	struct mmd_shared_context shared_context = {};
	int fd = -1;

	igt_fixture {
		uint32_t major, minor;
		int err;

		fd = drm_open_driver(DRIVER_AMDGPU);
		err = amdgpu_device_initialize(fd, &major, &minor, &device);
		igt_require(err == 0);
		igt_info("Initialized amdgpu, driver version %d.%d\n",
			 major, minor);
		err = mmd_shared_context_init(device, &shared_context);
		igt_require(err == 0);
		igt_skip_on(!is_jpeg_tests_enable(device, &shared_context));
	}
	igt_describe("Test whether jpeg dec decodes");
	igt_subtest("amdgpu_cs_jpeg_decode")
	amdgpu_cs_jpeg_decode(device, &shared_context);

	igt_fixture {
		amdgpu_device_deinitialize(device);
		drm_close_driver(fd);
	}

}
