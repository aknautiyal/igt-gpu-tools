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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "i915/gem_create.h"
#include "igt.h"
#include "igt_gt.h"
#include "intel_io.h"
#include "intel_chipset.h"

#include "intel_reg_spec.h"
#include "igt_device_scan.h"


#ifdef HAVE_SYS_IO_H
#include <sys/io.h>
#else

static inline int _not_supported(void)
{
       fprintf(stderr, "portio-vga not supported\n");
       exit(EXIT_FAILURE);
}
#define inb(port)              _not_supported()
#define outb(value, port)      _not_supported()
#define iopl(level)

#endif /* HAVE_SYS_IO_H */

struct config {
	struct pci_device *pci_dev;
	struct intel_mmio_data mmio_data;
	char *mmiofile;
	uint32_t devid;

	/* read: number of registers to read */
	uint32_t count;

	/* write: do a posting read */
	bool post;

	/* decode registers, otherwise use just raw values */
	bool decode;

	/* decode register for all platforms */
	bool all_platforms;

	/* spread out bits for convenience */
	bool binary;

	/* register spec */
	char *specfile;

	/* fd for engine access avoiding reopens */
	int fd;

	struct reg *regs;
	ssize_t regcount;

	int verbosity;
};

struct igt_pci_slot {
	int domain;
	int bus;
	int dev;
	int func;
};

/* port desc must have been set */
static int set_reg_by_addr(struct config *config, struct reg *reg,
			   uint32_t addr)
{
	int i;

	reg->addr = addr;
	if (reg->name)
		free(reg->name);
	reg->name = NULL;

	for (i = 0; i < config->regcount; i++) {
		struct reg *r = &config->regs[i];

		if (reg->port_desc.port != r->port_desc.port)
			continue;

		/* ->mmio_offset should be 0 for non-MMIO ports. */
		if (addr + reg->mmio_offset == r->addr + r->mmio_offset) {
			/* Always output the "normalized" offset+addr. */
			reg->mmio_offset = r->mmio_offset;
			reg->addr = r->addr;

			reg->name = r->name ? strdup(r->name) : NULL;
			break;
		}
	}

	return 0;
}

/* port desc must have been set */
static int set_reg_by_name(struct config *config, struct reg *reg,
			   const char *name)
{
	int i;

	reg->name = strdup(name);
	reg->addr = 0;

	for (i = 0; i < config->regcount; i++) {
		struct reg *r = &config->regs[i];

		if (reg->port_desc.port != r->port_desc.port)
			continue;

		if (!r->name)
			continue;

		if (strcasecmp(name, r->name) == 0) {
			reg->addr = r->addr;

			/* Also get MMIO offset if not already specified. */
			if (!reg->mmio_offset && r->mmio_offset)
				reg->mmio_offset = r->mmio_offset;

			return 0;
		}
	}

	return -1;
}

static void to_binary(char *buf, size_t buflen, uint32_t val)
{
	int i;

	if (!buflen)
		return;

	*buf = '\0';

	/* XXX: This quick and dirty implementation makes eyes hurt. */
	for (i = 31; i >= 0; i--) {
		if (i % 8 == 0)
			snprintf(buf, buflen, " %2d", i);
		else
			snprintf(buf, buflen, "  ");
		buflen -= strlen(buf);
		buf += strlen(buf);
	}
	snprintf(buf, buflen, "\n");
	buflen -= strlen(buf);
	buf += strlen(buf);

	for (i = 31; i >= 0; i--) {
		snprintf(buf, buflen, " %s%d", i % 8 == 7 ? " " : "",
			 !!(val & (1 << i)));
		buflen -= strlen(buf);
		buf += strlen(buf);
	}
	snprintf(buf, buflen, "\n");
}

static bool port_is_mmio(enum port_addr port)
{
	switch (port) {
	case PORT_MMIO_32:
	case PORT_MMIO_16:
	case PORT_MMIO_8:
	case PORT_MCHBAR_32:
	case PORT_MCHBAR_16:
	case PORT_MCHBAR_8:
		return true;
	default:
		return false;
	}
}

static void dump_regval(struct config *config, struct reg *reg, uint32_t val)
{
	char decode[1300];
	char tmp[1024];
	char bin[200];

	if (config->binary)
		to_binary(bin, sizeof(bin), val);
	else
		*bin = '\0';

	if (config->decode)
		intel_reg_spec_decode(tmp, sizeof(tmp), reg, val,
				      config->all_platforms ? 0 : config->devid);
	else
		*tmp = '\0';

	if (*tmp) {
		/* We have a decode result, and maybe binary decode. */
		if (config->all_platforms)
			snprintf(decode, sizeof(decode), "\n%s%s", tmp, bin);
		else
			snprintf(decode, sizeof(decode), " (%s)\n%s", tmp, bin);
	} else if (*bin) {
		/* No decode result, but binary decode. */
		snprintf(decode, sizeof(decode), "\n%s", bin);
	} else {
		/* No decode nor binary decode. */
		snprintf(decode, sizeof(decode), "\n");
	}

	if (port_is_mmio(reg->port_desc.port)) {
		/* Omit port name for MMIO, optionally include MMIO offset. */
		if (reg->mmio_offset)
			printf("%24s (0x%08x:0x%08x): 0x%08x%s",
			       reg->name ?: "",
			       reg->mmio_offset, reg->addr,
			       val, decode);
		else
			printf("%35s (0x%08x): 0x%08x%s",
			       reg->name ?: "",
			       reg->addr,
			       val, decode);
	} else {
		char name[100], addr[100];

		/* If no name, use addr as name for easier copy pasting. */
		if (reg->name)
			snprintf(name, sizeof(name), "%s:%s",
				 reg->port_desc.name, reg->name);
		else
			snprintf(name, sizeof(name), "%s:0x%08x",
				 reg->port_desc.name, reg->addr);

		/* Negative port numbers are not real sideband ports. */
		if (reg->port_desc.port > PORT_NONE)
			snprintf(addr, sizeof(addr), "0x%02x:0x%08x",
				 reg->port_desc.port, reg->addr);
		else
			snprintf(addr, sizeof(addr), "%s:0x%08x",
				 reg->port_desc.name, reg->addr);

		printf("%24s (%s): 0x%08x%s", name, addr, val, decode);
	}
}

static const struct intel_execution_engine2 *find_engine(const char *name)
{
	const struct intel_execution_engine2 *e;

	if (strlen(name) < 2)
		return NULL;

	if (name[0] == '-')
		name++;

	__for_each_static_engine(e) {
		if (!strcasecmp(e->name, name))
			return e;
	}

	return NULL;
}

static int register_srm(struct config *config, struct reg *reg,
			uint32_t *val_in)
{
	const int gen = intel_gen(config->devid);
	const bool r64b = gen >= 8;
	const uint32_t ctx = 0;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc[1];
	struct drm_i915_gem_execbuffer2 execbuf;
	uint32_t *batch, *r;
	const struct intel_execution_engine2 *engine;
	bool secure;
	int fd, i;
	uint32_t val;

	if (config->fd == -1) {
		config->fd = __drm_open_driver(DRIVER_INTEL);
		if (config->fd == -1) {
			fprintf(stderr, "Error opening driver: %s",
				strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	fd = config->fd;
	engine = find_engine(reg->engine);
	if (engine == NULL)
		exit(EXIT_FAILURE);

	secure = reg->engine[0] != '-';

	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(fd, 4096);
	obj[1].handle = gem_create(fd, 4096);
	obj[1].relocs_ptr = to_user_pointer(reloc);
	obj[1].relocation_count = 1;

	batch = gem_mmap__cpu(fd, obj[1].handle, 0, 4096, PROT_WRITE);
	gem_set_domain(fd, obj[1].handle,
		       I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);

	i = 0;
	if (val_in) {
		batch[i++] = MI_NOOP;
		batch[i++] = MI_NOOP;

		batch[i++] = MI_LOAD_REGISTER_IMM(1);
		batch[i++] = reg->addr;
		batch[i++] = *val_in;
		batch[i++] = MI_NOOP;
	}

	batch[i++] = 0x24 << 23 | (1 + r64b); /* SRM */
	batch[i++] = reg->addr;
	reloc[0].target_handle = obj[0].handle;
	reloc[0].presumed_offset = obj[0].offset;
	reloc[0].offset = i * sizeof(uint32_t);
	reloc[0].delta = 0;
	reloc[0].read_domains = I915_GEM_DOMAIN_RENDER;
	reloc[0].write_domain = I915_GEM_DOMAIN_RENDER;
	batch[i++] = reloc[0].delta;
	if (r64b)
		batch[i++] = 0;

	batch[i++] = MI_BATCH_BUFFER_END;
	munmap(batch, 4096);

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(obj);
	execbuf.buffer_count = 2;
	execbuf.flags = engine->flags;
	if (secure)
		execbuf.flags |= I915_EXEC_SECURE;

	if (config->verbosity > 0)
		printf("%s: using %sprivileged batch\n",
		       engine->name,
		       secure ? "" : "non-");

	execbuf.rsvd1 = ctx;
	gem_execbuf(fd, &execbuf);
	gem_close(fd, obj[1].handle);

	r = gem_mmap__cpu(fd, obj[0].handle, 0, 4096, PROT_READ);
	gem_set_domain(fd, obj[0].handle, I915_GEM_DOMAIN_CPU, 0);

	val = r[0];
	munmap(r, 4096);

	gem_close(fd, obj[0].handle);

	return val;
}

static uint32_t mcbar_offset(uint32_t devid)
{
	return intel_gen(devid) >= 6 ? 0x140000 : 0x10000;
}

static uint8_t vga_read(uint16_t reg, bool mmio)
{
	uint8_t val;

	if (mmio) {
		val = INREG(reg);
	} else {
		iopl(3);
		val = inb(reg);
		iopl(0);
	}

	return val;
}

static void vga_write(uint16_t reg, uint8_t val, bool mmio)
{
	if (mmio) {
		OUTREG(reg, val);
	} else {
		iopl(3);
		outb(val, reg);
		iopl(0);
	}
}

static bool vga_is_cga_mode(bool mmio)
{
	return vga_read(MSR_R, mmio) & IO_ADDR_SELECT;
}

static uint16_t vga_st01(bool mmio)
{
	if (vga_is_cga_mode(mmio))
		return ST01_CGA;
	else
		return ST01_MDA;
}

static void vga_ar_reset_flip_flop(bool mmio)
{
	vga_read(vga_st01(mmio), mmio);
}

static uint16_t vga_crx(bool mmio)
{
	if (vga_is_cga_mode(mmio))
		return CRX_CGA;
	else
		return CRX_MDA;
}

static uint16_t vga_crd(bool mmio)
{
	if (vga_is_cga_mode(mmio))
		return CRD_CGA;
	else
		return CRD_MDA;
}

static uint8_t vga_idx_read(uint16_t index_reg, uint16_t data_reg,
			    uint8_t index, bool mmio)
{
	vga_write(index_reg, index, mmio);
	return vga_read(data_reg, mmio);
}

static void vga_idx_write(uint16_t index_reg, uint16_t data_reg,
			  uint8_t index, uint8_t value, bool mmio)
{
	vga_write(index_reg, index, mmio);
	vga_write(data_reg, value, mmio);
}

static uint8_t vga_ar_read(uint8_t index, bool mmio)
{
	vga_ar_reset_flip_flop(mmio);
	return vga_idx_read(ARX, ARD_R, index, mmio);
}

static void vga_ar_write(uint8_t index, uint8_t value, bool mmio)
{
	vga_ar_reset_flip_flop(mmio);
	vga_idx_write(ARX, ARD_W, index, value, mmio);
}

static uint8_t vga_sr_read(uint8_t index, bool mmio)
{
	return vga_idx_read(SRX, SRD, index, mmio);
}

static void vga_sr_write(uint8_t index, uint8_t value, bool mmio)
{
	vga_idx_write(SRX, SRD, index, value, mmio);
}

static uint8_t vga_gr_read(uint8_t index, bool mmio)
{
	return vga_idx_read(GRX, GRD, index, mmio);
}

static void vga_gr_write(uint8_t index, uint8_t value, bool mmio)
{
	vga_idx_write(GRX, GRD, index, value, mmio);
}

static uint8_t vga_cr_read(uint8_t index, bool mmio)
{
	return vga_idx_read(vga_crx(mmio), vga_crd(mmio), index, mmio);
}

static void vga_cr_write(uint8_t index, uint8_t value, bool mmio)
{
	vga_idx_write(vga_crx(mmio), vga_crd(mmio), index, value, mmio);
}

static int read_register(struct config *config, struct reg *reg, uint32_t *valp)
{
	uint32_t val = 0;

	switch (reg->port_desc.port) {
	case PORT_MCHBAR_32:
	case PORT_MMIO_32:
		if (reg->engine)
			val = register_srm(config, reg, NULL);
		else
			val = INREG(reg->mmio_offset + reg->addr);
		break;
	case PORT_MCHBAR_16:
	case PORT_MMIO_16:
		val = INREG16(reg->mmio_offset + reg->addr);
		break;
	case PORT_MCHBAR_8:
	case PORT_MMIO_8:
		val = INREG8(reg->mmio_offset + reg->addr);
		break;
	case PORT_MMIO_VGA_AR:
		val = vga_ar_read(reg->addr, true);
		break;
	case PORT_MMIO_VGA_SR:
		val = vga_sr_read(reg->addr, true);
		break;
	case PORT_MMIO_VGA_GR:
		val = vga_gr_read(reg->addr, true);
		break;
	case PORT_MMIO_VGA_CR:
		val = vga_cr_read(reg->addr, true);
		break;
	case PORT_PORTIO:
		iopl(3);
		val = inb(reg->addr);
		iopl(0);
		break;
	case PORT_PORTIO_VGA_AR:
		val = vga_ar_read(reg->addr, false);
		break;
	case PORT_PORTIO_VGA_SR:
		val = vga_sr_read(reg->addr, false);
		break;
	case PORT_PORTIO_VGA_GR:
		val = vga_gr_read(reg->addr, false);
		break;
	case PORT_PORTIO_VGA_CR:
		val = vga_cr_read(reg->addr, false);
		break;
	case PORT_BUNIT:
	case PORT_PUNIT:
	case PORT_NC:
	case PORT_DPIO:
	case PORT_GPIO_NC:
	case PORT_CCK:
	case PORT_CCU:
	case PORT_DPIO2:
	case PORT_FLISDSI:
		if (!IS_VALLEYVIEW(config->devid) &&
		    !IS_CHERRYVIEW(config->devid)) {
			fprintf(stderr, "port %s only supported on vlv/chv\n",
				reg->port_desc.name);
			return -1;
		}
		val = intel_iosf_sb_read(&config->mmio_data, reg->port_desc.port, reg->addr);
		break;
	default:
		fprintf(stderr, "port %d not supported\n", reg->port_desc.port);
		return -1;
	}

	if (valp)
		*valp = val;

	return 0;
}

static void dump_register(struct config *config, struct reg *reg)
{
	uint32_t val;

	if (read_register(config, reg, &val) == 0)
		dump_regval(config, reg, val);
}

static int write_register(struct config *config, struct reg *reg, uint32_t val)
{
	int ret = 0;

	if (config->verbosity > 0) {
		printf("Before:\n");
		dump_register(config, reg);
	}

	switch (reg->port_desc.port) {
	case PORT_MCHBAR_32:
	case PORT_MMIO_32:
		if (reg->engine) {
			register_srm(config, reg, &val);
		} else {
			OUTREG(reg->mmio_offset + reg->addr, val);
		}
		break;
	case PORT_MCHBAR_16:
	case PORT_MMIO_16:
		if (val > 0xffff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		OUTREG16(reg->mmio_offset + reg->addr, val);
		break;
	case PORT_MCHBAR_8:
	case PORT_MMIO_8:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		OUTREG8(reg->mmio_offset + reg->addr, val);
		break;
	case PORT_MMIO_VGA_AR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_ar_write(reg->addr, val, true);
		break;
	case PORT_MMIO_VGA_SR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_sr_write(reg->addr, val, true);
		break;
	case PORT_MMIO_VGA_GR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_gr_write(reg->addr, val, true);
		break;
	case PORT_MMIO_VGA_CR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_cr_write(reg->addr, val, true);
		break;
	case PORT_PORTIO:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		iopl(3);
		outb(val, reg->addr);
		iopl(0);
		break;
	case PORT_PORTIO_VGA_AR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_ar_write(reg->addr, val, false);
		break;
	case PORT_PORTIO_VGA_SR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_sr_write(reg->addr, val, false);
		break;
	case PORT_PORTIO_VGA_GR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_gr_write(reg->addr, val, false);
		break;
	case PORT_PORTIO_VGA_CR:
		if (val > 0xff) {
			fprintf(stderr, "value 0x%08x out of range for port %s\n",
				val, reg->port_desc.name);
			return -1;
		}
		vga_cr_write(reg->addr, val, false);
		break;
	case PORT_BUNIT:
	case PORT_PUNIT:
	case PORT_NC:
	case PORT_DPIO:
	case PORT_GPIO_NC:
	case PORT_CCK:
	case PORT_CCU:
	case PORT_DPIO2:
	case PORT_FLISDSI:
		if (!IS_VALLEYVIEW(config->devid) &&
		    !IS_CHERRYVIEW(config->devid)) {
			fprintf(stderr, "port %s only supported on vlv/chv\n",
				reg->port_desc.name);
			return -1;
		}
		intel_iosf_sb_write(&config->mmio_data, reg->port_desc.port, reg->addr, val);
		break;
	default:
		fprintf(stderr, "port %d not supported\n", reg->port_desc.port);
		ret = -1;
	}

	if (config->verbosity > 0) {
		printf("After:\n");
		dump_register(config, reg);
	} else if (config->post) {
		read_register(config, reg, NULL);
	}

	return ret;
}

static int parse_engine(struct reg *reg, const char *s)
{
	const struct intel_execution_engine2 *e;

	e = find_engine(s);
	if (e) {
		reg->port_desc.port = PORT_MMIO_32;
		reg->port_desc.name = strdup(s);
		reg->port_desc.stride = 4;
		reg->engine = strdup(s);
		reg->mmio_offset = 0;
	} else {
		reg->engine = NULL;
	}

	return reg->engine == NULL;
}

/* s has [(PORTNAME|PORTNUM|ENGINE|MMIO-OFFSET):](REGNAME|REGADDR) */
static int parse_reg(struct config *config, struct reg *reg, const char *s)
{
	unsigned long addr;
	char *endp;
	const char *p;
	int ret;

	memset(reg, 0, sizeof(*reg));

	p = strchr(s, ':');
	if (p == s) {
		ret = -1;
	} else if (p) {
		char *port_name = strndup(s, p - s);

		ret = parse_engine(reg, port_name);
		if (ret)
			ret = parse_port_desc(reg, port_name);

		free(port_name);
		p++;
	} else {
		/*
		 * XXX: If port is not specified in input, see if the register
		 * matches by name, and initialize port desc based on that.
		 */
		ret = parse_port_desc(reg, NULL);
		p = s;
	}

	if (ret) {
		fprintf(stderr, "invalid port in '%s'\n", s);
		return ret;
	}

	switch (reg->port_desc.port) {
	case PORT_MCHBAR_32:
	case PORT_MCHBAR_16:
	case PORT_MCHBAR_8:
		reg->mmio_offset = mcbar_offset(config->devid);
		break;
	default:
		break;
	}

	addr = strtoul(p, &endp, 16);
	if (endp > p && *endp == 0) {
		/* It's a number. */
		ret = set_reg_by_addr(config, reg, addr);
	} else {
		/* Not a number, it's a name. */
		ret = set_reg_by_name(config, reg, p);
	}

	return ret;
}

/* XXX: add support for register ranges, maybe REGISTER..REGISTER */
static int intel_reg_read(struct config *config, int argc, char *argv[])
{
	int i, j;

	if (argc == 1) {
		fprintf(stderr, "read: no registers specified\n");
		return EXIT_FAILURE;
	}

	if (config->mmiofile)
		intel_mmio_use_dump_file(&config->mmio_data, config->mmiofile);
	else
		intel_register_access_init(&config->mmio_data, config->pci_dev, 0);

	for (i = 1; i < argc; i++) {
		struct reg reg;

		if (parse_reg(config, &reg, argv[i]))
			continue;

		for (j = 0; j < config->count; j++) {
			dump_register(config, &reg);
			/* Update addr and name. */
			set_reg_by_addr(config, &reg,
					reg.addr + reg.port_desc.stride);
		}
	}

	intel_register_access_fini(&config->mmio_data);

	return EXIT_SUCCESS;
}

static int intel_reg_write(struct config *config, int argc, char *argv[])
{
	int i;

	if (argc == 1) {
		fprintf(stderr, "write: no registers specified\n");
		return EXIT_FAILURE;
	}

	intel_register_access_init(&config->mmio_data, config->pci_dev, 0);

	for (i = 1; i < argc; i += 2) {
		struct reg reg;
		uint32_t val;
		char *endp;

		if (parse_reg(config, &reg, argv[i]))
			continue;

		if (i + 1 == argc) {
			fprintf(stderr, "write: no value\n");
			break;
		}

		val = strtoul(argv[i + 1], &endp, 16);
		if (endp == argv[i + 1] || *endp) {
			fprintf(stderr, "write: invalid value '%s'\n",
				argv[i + 1]);
			continue;
		}

		write_register(config, &reg, val);
	}

	intel_register_access_fini(&config->mmio_data);

	return EXIT_SUCCESS;
}

static int intel_reg_dump(struct config *config, int argc, char *argv[])
{
	struct reg *reg;
	int i;

	if (config->mmiofile)
		intel_mmio_use_dump_file(&config->mmio_data, config->mmiofile);
	else
		intel_register_access_init(&config->mmio_data, config->pci_dev, 0);

	for (i = 0; i < config->regcount; i++) {
		reg = &config->regs[i];

		/* can't dump sideband with mmiofile */
		if (config->mmiofile && !port_is_mmio(reg->port_desc.port))
			continue;

		dump_register(config, &config->regs[i]);
	}

	intel_register_access_fini(&config->mmio_data);

	return EXIT_SUCCESS;
}

static int intel_reg_snapshot(struct config *config, int argc, char *argv[])
{
	int mmio_bar = IS_GEN2(config->devid) ? 1 : 0;

	if (config->mmiofile) {
		fprintf(stderr, "specifying --mmio=FILE is not compatible\n");
		return EXIT_FAILURE;
	}

	intel_mmio_use_pci_bar(&config->mmio_data, config->pci_dev);

	/* XXX: error handling */
	if (write(1, igt_global_mmio, config->pci_dev->regions[mmio_bar].size) == -1)
		fprintf(stderr, "Error writing snapshot: %s", strerror(errno));

	if (config->verbosity > 0)
		printf("use this with --mmio=FILE --devid=0x%04X\n",
		       config->devid);

	return EXIT_SUCCESS;
}

/* XXX: add support for reading and re-decoding a previously done dump */
static int intel_reg_decode(struct config *config, int argc, char *argv[])
{
	int i;

	if (argc == 1) {
		fprintf(stderr, "decode: no registers specified\n");
		return EXIT_FAILURE;
	}

	for (i = 1; i < argc; i += 2) {
		struct reg reg;
		uint32_t val;
		char *endp;

		if (parse_reg(config, &reg, argv[i]))
			continue;

		if (i + 1 == argc) {
			fprintf(stderr, "decode: no value\n");
			break;
		}

		val = strtoul(argv[i + 1], &endp, 16);
		if (endp == argv[i + 1] || *endp) {
			fprintf(stderr, "decode: invalid value '%s'\n",
				argv[i + 1]);
			continue;
		}

		dump_regval(config, &reg, val);
	}

	return EXIT_SUCCESS;
}

static int intel_reg_list(struct config *config, int argc, char *argv[])
{
	int i;

	for (i = 0; i < config->regcount; i++) {
		printf("%s\n", config->regs[i].name);
	}

	return EXIT_SUCCESS;
}

static int intel_reg_help(struct config *config, int argc, char *argv[]);

struct command {
	const char *name;
	const char *description;
	const char *synopsis;
	bool decode;
	int (*function)(struct config *config, int argc, char *argv[]);
};

static const struct command commands[] = {
	{
		.name = "help",
		.function = intel_reg_help,
		.description = "show this help",
	},
	{
		.name = "read",
		.function = intel_reg_read,
		.synopsis = "[--count=N] REGISTER [...]",
		.description = "read and decode specified register(s)",
	},
	{
		.name = "write",
		.function = intel_reg_write,
		.synopsis = "[--post] REGISTER VALUE [REGISTER VALUE ...]",
		.description = "write value(s) to specified register(s)",
	},
	{
		.name = "snapshot",
		.function = intel_reg_snapshot,
		.description = "create a snapshot of the MMIO bar to stdout",
	},
	{
		.name = "dump",
		.function = intel_reg_dump,
		.description = "dump all known registers",
		.decode = true,
	},
	{
		.name = "decode",
		.function = intel_reg_decode,
		.synopsis = "REGISTER VALUE [REGISTER VALUE ...]",
		.description = "decode value(s) for specified register(s)",
		.decode = true,
	},
	{
		.name = "list",
		.function = intel_reg_list,
		.description = "list all known register names",
		.decode = true,
	},
};

static int intel_reg_help(struct config *config, int argc, char *argv[])
{
	const struct intel_execution_engine2 *e;
	int i;

	printf("Intel graphics register multitool\n\n");
	printf("Usage: intel_reg [OPTION ...] COMMAND\n\n");
	printf("COMMAND is one of:\n");
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		printf("  %-14s%s\n", commands[i].name,
		       commands[i].synopsis ?: "");
		printf("  %-14s%s\n", "", commands[i].description);
	}

	printf("\n");
	printf("REGISTER is defined as:\n");
        printf("  [(PORTNAME|PORTNUM|ENGINE|MMIO-OFFSET):](REGNAME|REGADDR)\n");

	printf("\n");
	printf("PORTNAME is one of:\n");
	intel_reg_spec_print_ports();
	printf("\n\n");

	printf("ENGINE is one of:\n");
	__for_each_static_engine(e)
		printf("%s -%s ", e->name, e->name);
	printf("\n\n");

	printf("OPTIONS common to most COMMANDS:\n");
	printf(" --spec=PATH    Read register spec from directory or file. Implies --decode\n");
	printf(" --mmio=FILE    Use an MMIO snapshot\n");
	printf(" --devid=DEVID  Specify PCI device ID for --mmio=FILE\n");
	printf(" --decode       Decode registers. Implied by commands that require it\n");
	printf(" --all          Decode registers for all known platforms. Implies --decode\n");
	printf(" --pci-slot=BDF Decode registers for platform described by PCI slot\n"
	       "		<domain>:<bus>:<device>[.<func>]\n"
	       "                When this option is not provided use first matched Intel GPU\n");
	printf(" --binary       Binary dump registers\n");
	printf(" --verbose      Increase verbosity\n");
	printf(" --quiet        Reduce verbosity\n");

	printf("\n");
	printf("Environment variables:\n");
	printf(" INTEL_REG_SPEC Read register spec from directory or file\n");

	return EXIT_SUCCESS;
}

/*
 * Get codename for a gen5+ platform to be used for finding register spec file.
 */
static const char *get_codename(uint32_t devid)
{
	return intel_get_device_info(devid)->codename;
}

/*
 * Get register definitions filename for devid in dir. Return 0 if found,
 * negative error code otherwise.
 */
static int get_reg_spec_file(char *buf, size_t buflen, const char *dir,
			     uint32_t devid)
{
	const char *codename;

	/* First, try file named after devid, e.g. "0412" for Haswell GT2. */
	snprintf(buf, buflen, "%s/%04x", dir, devid);
	if (!access(buf, F_OK))
		return 0;

	/*
	 * Second, for gen5+, try file named after codename, e.g. "haswell" for
         * Haswell.
	 */
	codename = get_codename(devid);
	if (codename) {
		snprintf(buf, buflen, "%s/%s", dir, codename);
		if (!access(buf, F_OK))
			return 0;
	}

	/*
	 * Third, try file named after gen, e.g. "gen7" for Haswell (which is
	 * technically 7.5 but this is how it works).
	 */
	snprintf(buf, buflen, "%s/gen%d", dir, intel_gen(devid));
	if (!access(buf, F_OK))
		return 0;

	return -ENOENT;
}

/*
 * Read register spec.
 */
static int read_reg_spec(struct config *config)
{
	char buf[PATH_MAX];
	const char *path;
	struct stat st;
	int r;

	if (!config->decode)
		return 0;

	path = config->specfile;
	if (!path)
		path = getenv("INTEL_REG_SPEC");

	if (!path)
		path = IGT_DATADIR"/registers";

	r = stat(path, &st);
	if (r) {
		fprintf(stderr, "Warning: stat '%s' failed: %s. "
			"Using builtin register spec.\n",
			path, strerror(errno));
		goto builtin;
	}

	if (S_ISDIR(st.st_mode)) {
		r = get_reg_spec_file(buf, sizeof(buf), path, config->devid);
		if (r) {
			fprintf(stderr, "Warning: register spec not found in "
				"'%s'. Using builtin register spec.\n", path);
			goto builtin;
		}
		path = buf;
	}

	config->regcount = intel_reg_spec_file(&config->regs, path);
	if (config->regcount <= 0) {
		fprintf(stderr, "Warning: reading '%s' failed. "
			"Using builtin register spec.\n", path);
		goto builtin;
	}

	return config->regcount;

builtin:
	/* Fallback to builtin register spec. */
	config->regcount = intel_reg_spec_builtin(&config->regs, config->devid);

	return config->regcount;
}

static int parse_pci_slot_name(struct igt_pci_slot *st, const char *slot_name)
{
	st->domain = 0;
	st->bus = 0;
	st->dev = 0;
	st->func = 0;

	return sscanf(slot_name, "%x:%x:%x.%x", &st->domain, &st->bus, &st->dev, &st->func);
}

static bool is_intel_card_valid(struct pci_device *pci_dev)
{
	if (!pci_dev) {
		fprintf(stderr, "PCI card not found\n");
		return false;
	}

	if (pci_device_probe(pci_dev) != 0) {
		fprintf(stderr, "Couldn't probe PCI card\n");
		return false;
	}

	if (pci_dev->vendor_id != 0x8086) {
		fprintf(stderr, "PCI card is non-Intel\n");
		return false;
	}

	return true;
}

static bool find_dev_from_slot(struct pci_device **pci_dev, char *opt_slot)
{
	struct igt_pci_slot bdf;
	bool ret;

	if (parse_pci_slot_name(&bdf, opt_slot) < 3) {
		fprintf(stderr, "Cannot decode PCI slot from '%s'\n", opt_slot);
		return false;
	}

	if (pci_system_init() != 0) {
		fprintf(stderr, "Couldn't initialize PCI system\n");
		return false;
	}

	igt_devices_scan();
	*pci_dev = pci_device_find_by_slot(bdf.domain, bdf.bus, bdf.dev, bdf.func);
	ret = is_intel_card_valid(*pci_dev);
	igt_devices_free();

	if (!ret)
		fprintf(stderr, "Cannot find PCI card given by slot '%s'\n", opt_slot);

	return ret;
}

enum opt {
	OPT_UNKNOWN = '?',
	OPT_END = -1,
	OPT_MMIO,
	OPT_DEVID,
	OPT_COUNT,
	OPT_POST,
	OPT_DECODE,
	OPT_ALL,
	OPT_SLOT,
	OPT_BINARY,
	OPT_SPEC,
	OPT_VERBOSE,
	OPT_QUIET,
	OPT_HELP,
};

int main(int argc, char *argv[])
{
	int ret, i, index;
	char *endp;
	char *opt_slot = NULL;
	enum opt opt;
	const struct command *command = NULL;
	struct config config = {
		.count = 1,
		.fd = -1,
	};
	bool help = false;

	static struct option options[] = {
		/* global options */
		{ "spec",	required_argument,	NULL,	OPT_SPEC },
		{ "verbose",	no_argument,		NULL,	OPT_VERBOSE },
		{ "quiet",	no_argument,		NULL,	OPT_QUIET },
		{ "help",	no_argument,		NULL,	OPT_HELP },
		/* options specific to read and dump */
		{ "mmio",	required_argument,	NULL,	OPT_MMIO },
		{ "devid",	required_argument,	NULL,	OPT_DEVID },
		/* options specific to read */
		{ "count",	required_argument,	NULL,	OPT_COUNT },
		/* options specific to write */
		{ "post",	no_argument,		NULL,	OPT_POST },
		/* options specific to read, dump and decode */
		{ "decode",	no_argument,		NULL,	OPT_DECODE },
		{ "all",	no_argument,		NULL,	OPT_ALL },
		{ "pci-slot",	required_argument,	NULL,	OPT_SLOT },
		{ "binary",	no_argument,		NULL,	OPT_BINARY },
		{ 0 }
	};

	for (opt = 0; opt != OPT_END; ) {
		opt = getopt_long(argc, argv, "", options, &index);

		switch (opt) {
		case OPT_MMIO:
			config.mmiofile = strdup(optarg);
			if (!config.mmiofile) {
				fprintf(stderr, "strdup: %s\n",
					strerror(errno));
				return EXIT_FAILURE;
			}
			break;
		case OPT_DEVID:
			config.devid = strtoul(optarg, &endp, 16);
			if (*endp) {
				fprintf(stderr, "invalid devid '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_COUNT:
			config.count = strtol(optarg, &endp, 10);
			if (*endp) {
				fprintf(stderr, "invalid count '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			break;
		case OPT_POST:
			config.post = true;
			break;
		case OPT_SPEC:
			config.decode = true;
			config.specfile = strdup(optarg);
			if (!config.specfile) {
				fprintf(stderr, "strdup: %s\n",
					strerror(errno));
				return EXIT_FAILURE;
			}
			break;
		case OPT_ALL:
			config.all_platforms = true;
			config.decode = true;
			break;
		case OPT_DECODE:
			config.decode = true;
			break;
		case OPT_SLOT:
			opt_slot = strdup(optarg);
			if (!opt_slot) {
				fprintf(stderr, "strdup: %s\n",
					strerror(errno));
				return EXIT_FAILURE;
			}
			break;
		case OPT_BINARY:
			config.binary = true;
			break;
		case OPT_VERBOSE:
			config.verbosity++;
			break;
		case OPT_QUIET:
			config.verbosity--;
			break;
		case OPT_HELP:
			help = true;
			break;
		case OPT_END:
			break;
		case OPT_UNKNOWN:
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (help || (argc > 0 && strcmp(argv[0], "help") == 0))
		return intel_reg_help(&config, argc, argv);

	if (argc == 0) {
		fprintf(stderr, "Command missing. Try intel_reg help.\n");
		return EXIT_FAILURE;
	}

	if (config.mmiofile) {
		if (!config.devid) {
			fprintf(stderr, "--mmio requires --devid\n");
			return EXIT_FAILURE;
		}
	} else {
		/* XXX: devid without --mmio could be useful for decode. */
		if (config.devid) {
			fprintf(stderr, "--devid without --mmio\n");
			return EXIT_FAILURE;
		}

		if (opt_slot) {
			if (!find_dev_from_slot(&config.pci_dev, opt_slot))
				return EXIT_FAILURE;
		} else {
			config.pci_dev = intel_get_pci_device();
		}

		config.devid = config.pci_dev->device_id;
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0) {
			command = &commands[i];
			break;
		}
	}

	if (!command) {
		fprintf(stderr, "'%s' is not an intel-reg command\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (command->decode)
		config.decode = true;

	if (read_reg_spec(&config) < 0)
		return EXIT_FAILURE;

	ret = command->function(&config, argc, argv);

	free(config.mmiofile);

	if (config.fd >= 0)
		close(config.fd);

	if (opt_slot)
		free(opt_slot);

	return ret;
}
