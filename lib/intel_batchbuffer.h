#ifndef INTEL_BATCHBUFFER_H
#define INTEL_BATCHBUFFER_H

#include <stdint.h>
#include <i915_drm.h>

#include "igt_core.h"
#include "igt_list.h"
#include "intel_reg.h"
#include "drmtest.h"
#include "intel_allocator.h"

#define BATCH_SZ 4096

/*
 * Yf/Ys/4/64 tiling
 *
 * Tiling mode in the I915_TILING_... namespace for new tiling modes which are
 * defined in the kernel. (They are not fenceable so the kernel does not need
 * to know about them.)
 *
 * They are to be used the the blitting routines below.
 */
#define I915_TILING_4	(I915_TILING_LAST + 1)
#define I915_TILING_Yf	(I915_TILING_LAST + 2)
#define I915_TILING_Ys	(I915_TILING_LAST + 3)
#define I915_TILING_64	(I915_TILING_LAST + 4)

enum i915_compression {
	I915_COMPRESSION_NONE,
	I915_COMPRESSION_RENDER,
	I915_COMPRESSION_MEDIA,
};

uint32_t fast_copy_dword0(unsigned int src_tiling,
			  unsigned int dst_tiling);
uint32_t fast_copy_dword1(int fd, unsigned int src_tiling,
			  unsigned int dst_tiling,
			  int bpp);
void igt_blitter_copy(int fd,
		      uint64_t ahnd,
		      uint32_t ctx,
		      const intel_ctx_cfg_t *cfg,
		      /* src */
		      uint32_t src_handle,
		      uint32_t src_delta,
		      uint32_t src_stride,
		      uint32_t src_tiling,
		      uint32_t src_x, uint32_t src_y,
		      uint64_t src_size,
		      /* size */
		      uint32_t width, uint32_t height,
		      /* bpp */
		      uint32_t bpp,
		      /* dst */
		      uint32_t dst_handle,
		      uint32_t dst_delta,
		      uint32_t dst_stride,
		      uint32_t dst_tiling,
		      uint32_t dst_x, uint32_t dst_y,
		      uint64_t dst_size);

void igt_blitter_src_copy(int fd,
			  uint64_t ahnd,
			  uint32_t ctx,
			  const intel_ctx_cfg_t *cfg,
			  /* src */
			  uint32_t src_handle,
			  uint32_t src_delta,
			  uint32_t src_stride,
			  uint32_t src_tiling,
			  uint32_t src_x, uint32_t src_y,
			  uint64_t src_size,

			  /* size */
			  uint32_t width, uint32_t height,

			  /* bpp */
			  uint32_t bpp,

			  /* dst */
			  uint32_t dst_handle,
			  uint32_t dst_delta,
			  uint32_t dst_stride,
			  uint32_t dst_tiling,
			  uint32_t dst_x, uint32_t dst_y,
			  uint64_t dst_size);

void igt_blitter_fast_copy__raw(int fd,
				uint64_t ahnd,
				uint32_t ctx,
				const intel_ctx_cfg_t *cfg,
				/* src */
				uint32_t src_handle,
				unsigned int src_delta,
				unsigned int src_stride,
				unsigned int src_tiling,
				unsigned int src_x, unsigned src_y,
				uint64_t src_size,

				/* size */
				unsigned int width, unsigned int height,

				/* bpp */
				int bpp,

				/* dst */
				uint32_t dst_handle,
				unsigned int dst_delta,
				unsigned int dst_stride,
				unsigned int dst_tiling,
				unsigned int dst_x, unsigned dst_y,
				uint64_t dst_size);

/**
 * igt_render_copyfunc_t:
 * @ibb: batchbuffer
 * @ctx: context to use
 * @src: intel_buf source object
 * @src_x: source pixel x-coordination
 * @src_y: source pixel y-coordination
 * @width: width of the copied rectangle
 * @height: height of the copied rectangle
 * @dst: intel_buf destination object
 * @dst_x: destination pixel x-coordination
 * @dst_y: destination pixel y-coordination
 *
 * This is the type of the per-platform render copy functions. The
 * platform-specific implementation can be obtained by calling
 * igt_get_render_copyfunc().
 *
 * A render copy function will emit a batchbuffer to the kernel which executes
 * the specified blit copy operation using the render engine. @ctx is
 * optional and can be 0.
 */
struct intel_bb;
struct intel_buf;

typedef void (*igt_render_copyfunc_t)(struct intel_bb *ibb,
				      struct intel_buf *src,
				      uint32_t src_x, uint32_t src_y,
				      uint32_t width, uint32_t height,
				      struct intel_buf *dst,
				      uint32_t dst_x, uint32_t dst_y);

igt_render_copyfunc_t igt_get_render_copyfunc(int fd);


/**
 * igt_vebox_copyfunc_t:
 * @ibb: batchbuffer
 * @src: intel_buf source object
 * @width: width of the copied rectangle
 * @height: height of the copied rectangle
 * @dst: intel_buf destination object
 *
 * This is the type of the per-platform vebox copy functions. The
 * platform-specific implementation can be obtained by calling
 * igt_get_vebox_copyfunc().
 *
 * A vebox copy function will emit a batchbuffer to the kernel which executes
 * the specified blit copy operation using the vebox engine.
 */
typedef void (*igt_vebox_copyfunc_t)(struct intel_bb *ibb,
				     struct intel_buf *src,
				     unsigned int width, unsigned int height,
				     struct intel_buf *dst);

igt_vebox_copyfunc_t igt_get_vebox_copyfunc(int devid);

typedef void (*igt_render_clearfunc_t)(struct intel_bb *ibb,
				       struct intel_buf *dst, unsigned int dst_x, unsigned int dst_y,
				       unsigned int width, unsigned int height,
				       const float cc_color[4]);
igt_render_clearfunc_t igt_get_render_clearfunc(int devid);

/**
 * igt_fillfunc_t:
 * @i915: drm fd
 * @buf: destination intel_buf object
 * @x: destination pixel x-coordination
 * @y: destination pixel y-coordination
 * @width: width of the filled rectangle
 * @height: height of the filled rectangle
 * @color: fill color to use
 *
 * This is the type of the per-platform fill functions using media
 * or gpgpu pipeline. The platform-specific implementation can be obtained
 * by calling igt_get_media_fillfunc() or igt_get_gpgpu_fillfunc().
 *
 * A fill function will emit a batchbuffer to the kernel which executes
 * the specified blit fill operation using the media/gpgpu engine.
 */
typedef void (*igt_fillfunc_t)(int i915,
			       struct intel_buf *buf,
			       unsigned x, unsigned y,
			       unsigned width, unsigned height,
			       uint8_t color);

igt_fillfunc_t igt_get_gpgpu_fillfunc(int devid);
igt_fillfunc_t igt_get_media_fillfunc(int devid);

typedef void (*igt_vme_func_t)(int i915,
			       uint32_t ctx,
			       struct intel_buf *src,
			       unsigned int width, unsigned int height,
			       struct intel_buf *dst);

igt_vme_func_t igt_get_media_vme_func(int devid);

/**
 * igt_media_spinfunc_t:
 * @i915: drm fd
 * @buf: destination buffer object
 * @spins: number of loops to execute
 *
 * This is the type of the per-platform media spin functions. The
 * platform-specific implementation can be obtained by calling
 * igt_get_media_spinfunc().
 *
 * The media spin function emits a batchbuffer for the render engine with
 * the media pipeline selected. The workload consists of a single thread
 * which spins in a tight loop the requested number of times. Each spin
 * increments a counter whose final 32-bit value is written to the
 * destination buffer on completion. This utility provides a simple way
 * to keep the render engine busy for a set time for various tests.
 */
typedef void (*igt_media_spinfunc_t)(int i915,
				     struct intel_buf *buf, uint32_t spins);

igt_media_spinfunc_t igt_get_media_spinfunc(int devid);

struct igt_pxp {
	bool     enabled;
	uint32_t apptype;
	uint32_t appid;
};

/*
 * Batchbuffer without libdrm dependency
 */
struct intel_bb {
	struct igt_list_head link;

	uint64_t allocator_handle;
	uint64_t allocator_start, allocator_end;
	uint8_t allocator_type;
	enum allocator_strategy allocator_strategy;

	enum intel_driver driver;
	int fd;
	unsigned int gen;
	bool debug;
	bool dump_base64;
	bool enforce_relocs;
	uint32_t devid;
	uint32_t handle;
	uint32_t size;
	uint32_t *batch;
	uint32_t *ptr;
	uint64_t alignment;
	int fence;

	uint64_t gtt_size;
	bool supports_48b_address;
	bool uses_full_ppgtt;
	bool allows_obj_alignment;

	struct igt_pxp pxp;
	uint32_t ctx;
	uint32_t vm_id;

	bool xe_bound;
	uint32_t engine_syncobj;
	uint32_t engine_id;
	uint32_t last_engine;

	/* Context configuration */
	intel_ctx_cfg_t *cfg;

	/* Cache */
	void *root;

	/* Current objects for execbuf */
	void *current;

	/* Objects for current execbuf */
	struct drm_i915_gem_exec_object2 **objects;
	uint32_t num_objects;
	uint32_t allocated_objects;
	uint64_t batch_offset;

	struct drm_i915_gem_relocation_entry *relocs;
	uint32_t num_relocs;
	uint32_t allocated_relocs;

	/* Tracked intel_bufs */
	struct igt_list_head intel_bufs;

	/*
	 * BO recreate in reset path only when refcount == 0
	 * Currently we don't need to use atomics because intel_bb
	 * is not thread-safe.
	 */
	int32_t refcount;

	/* long running mode */
	bool lr_mode;
	int64_t user_fence_offset;
	uint64_t user_fence_value;
};

struct intel_bb *
intel_bb_create_full(int fd, uint32_t ctx, uint32_t vm,
		     const intel_ctx_cfg_t *cfg, uint32_t size, uint64_t start,
		     uint64_t end, uint64_t alignment, uint8_t allocator_type,
		     enum allocator_strategy strategy, uint64_t region);
struct intel_bb *
intel_bb_create_with_allocator(int fd, uint32_t ctx, uint32_t vm,
			       const intel_ctx_cfg_t *cfg, uint32_t size,
			       uint8_t allocator_type);
struct intel_bb *intel_bb_create(int fd, uint32_t size);
struct intel_bb *
intel_bb_create_with_context(int fd, uint32_t ctx, uint32_t vm,
			     const intel_ctx_cfg_t *cfg, uint32_t size);
struct intel_bb *
intel_bb_create_with_context_in_region(int fd, uint32_t ctx, uint32_t vm,
				       const intel_ctx_cfg_t *cfg, uint32_t size, uint64_t region);
struct intel_bb *intel_bb_create_with_relocs(int fd, uint32_t size);
struct intel_bb *
intel_bb_create_with_relocs_and_context(int fd, uint32_t ctx,
					const intel_ctx_cfg_t *cfg, uint32_t size);
struct intel_bb *intel_bb_create_no_relocs(int fd, uint32_t size);
void intel_bb_destroy(struct intel_bb *ibb);

/* make it safe to use intel_allocator after failed test */
void intel_bb_reinit_allocator(void);
void intel_bb_track(bool do_tracking);

static inline void intel_bb_ref(struct intel_bb *ibb)
{
	ibb->refcount++;
}

static inline void intel_bb_unref(struct intel_bb *ibb)
{
	igt_assert_f(ibb->refcount > 0, "intel_bb refcount is 0!");
	ibb->refcount--;
}

void intel_bb_reset(struct intel_bb *ibb, bool purge_objects_cache);
int intel_bb_sync(struct intel_bb *ibb);

void intel_bb_print(struct intel_bb *ibb);
void intel_bb_dump(struct intel_bb *ibb, const char *filename, bool in_hex);
void intel_bb_set_debug(struct intel_bb *ibb, bool debug);
void intel_bb_set_dump_base64(struct intel_bb *ibb, bool dump);

static inline uint32_t intel_bb_offset(struct intel_bb *ibb)
{
	return (uint32_t) ((uint8_t *) ibb->ptr - (uint8_t *) ibb->batch);
}

static inline void *intel_bb_ptr_get(struct intel_bb *ibb, uint32_t offset)
{
	igt_assert(offset < ibb->size);

	return ((uint8_t *) ibb->batch + offset);
}

static inline void intel_bb_ptr_set(struct intel_bb *ibb, uint32_t offset)
{
	ibb->ptr = (void *) ((uint8_t *) ibb->batch + offset);

	igt_assert(intel_bb_offset(ibb) <= ibb->size);
}

static inline void intel_bb_ptr_add(struct intel_bb *ibb, uint32_t offset)
{
	intel_bb_ptr_set(ibb, intel_bb_offset(ibb) + offset);
}

static inline uint32_t intel_bb_ptr_add_return_prev_offset(struct intel_bb *ibb,
							   uint32_t offset)
{
	uint32_t previous_offset = intel_bb_offset(ibb);

	intel_bb_ptr_set(ibb, previous_offset + offset);

	return previous_offset;
}

static inline void *intel_bb_ptr_align(struct intel_bb *ibb, uint32_t alignment)
{
	intel_bb_ptr_set(ibb, ALIGN(intel_bb_offset(ibb), alignment));

	return (void *) ibb->ptr;
}

static inline void *intel_bb_ptr(struct intel_bb *ibb)
{
	return (void *) ibb->ptr;
}

static inline void intel_bb_out(struct intel_bb *ibb, uint32_t dword)
{
	*ibb->ptr = dword;
	ibb->ptr++;

	igt_assert(intel_bb_offset(ibb) <= ibb->size);
}

void intel_bb_set_pxp(struct intel_bb *ibb, bool new_state,
		      uint32_t apptype, uint32_t appid);

static inline bool intel_bb_pxp_enabled(struct intel_bb *ibb)
{
	igt_assert(ibb);
	return ibb->pxp.enabled;
}

static inline uint32_t intel_bb_pxp_apptype(struct intel_bb *ibb)
{
	igt_assert(ibb);
	return ibb->pxp.apptype;
}

static inline uint32_t intel_bb_pxp_appid(struct intel_bb *ibb)
{
	igt_assert(ibb);
	return ibb->pxp.appid;
}

static inline void intel_bb_set_lr_mode(struct intel_bb *ibb, bool lr_mode)
{
	igt_assert(ibb);
	ibb->lr_mode = lr_mode;
}

static inline bool intel_bb_get_lr_mode(struct intel_bb *ibb)
{
	igt_assert(ibb);
	return ibb->lr_mode;
}

struct drm_i915_gem_exec_object2 *
intel_bb_add_object(struct intel_bb *ibb, uint32_t handle, uint64_t size,
		    uint64_t offset, uint64_t alignment, bool write);
bool intel_bb_remove_object(struct intel_bb *ibb, uint32_t handle,
			    uint64_t offset, uint64_t size);
struct drm_i915_gem_exec_object2 *
intel_bb_add_intel_buf(struct intel_bb *ibb, struct intel_buf *buf, bool write);
struct drm_i915_gem_exec_object2 *
intel_bb_add_intel_buf_with_alignment(struct intel_bb *ibb, struct intel_buf *buf,
				      uint64_t alignment, bool write);
bool intel_bb_remove_intel_buf(struct intel_bb *ibb, struct intel_buf *buf);
void intel_bb_print_intel_bufs(struct intel_bb *ibb);
struct drm_i915_gem_exec_object2 *
intel_bb_find_object(struct intel_bb *ibb, uint32_t handle);

bool
intel_bb_object_set_flag(struct intel_bb *ibb, uint32_t handle, uint64_t flag);
bool
intel_bb_object_clear_flag(struct intel_bb *ibb, uint32_t handle, uint64_t flag);

uint64_t intel_bb_emit_reloc(struct intel_bb *ibb,
			 uint32_t handle,
			 uint32_t read_domains,
			 uint32_t write_domain,
			 uint64_t delta,
			 uint64_t presumed_offset);

uint64_t intel_bb_emit_reloc_fenced(struct intel_bb *ibb,
				    uint32_t handle,
				    uint32_t read_domains,
				    uint32_t write_domain,
				    uint64_t delta,
				    uint64_t presumed_offset);

uint64_t intel_bb_offset_reloc(struct intel_bb *ibb,
			       uint32_t handle,
			       uint32_t read_domains,
			       uint32_t write_domain,
			       uint32_t offset,
			       uint64_t presumed_offset);

uint64_t intel_bb_offset_reloc_with_delta(struct intel_bb *ibb,
					  uint32_t handle,
					  uint32_t read_domains,
					  uint32_t write_domain,
					  uint32_t delta,
					  uint32_t offset,
					  uint64_t presumed_offset);

uint64_t intel_bb_offset_reloc_to_object(struct intel_bb *ibb,
					 uint32_t handle,
					 uint32_t to_handle,
					 uint32_t read_domains,
					 uint32_t write_domain,
					 uint32_t delta,
					 uint32_t offset,
					 uint64_t presumed_offset);

int __intel_bb_exec(struct intel_bb *ibb, uint32_t end_offset,
			uint64_t flags, bool sync);

void intel_bb_dump_cache(struct intel_bb *ibb);

void intel_bb_exec(struct intel_bb *ibb, uint32_t end_offset,
		   uint64_t flags, bool sync);
int __xe_bb_exec(struct intel_bb *ibb, uint64_t flags, bool sync);

uint64_t intel_bb_get_object_offset(struct intel_bb *ibb, uint32_t handle);
bool intel_bb_object_offset_to_buf(struct intel_bb *ibb, struct intel_buf *buf);

uint32_t intel_bb_emit_bbe(struct intel_bb *ibb);
uint32_t intel_bb_emit_flush_common(struct intel_bb *ibb);
void intel_bb_flush(struct intel_bb *ibb, uint32_t ring);
void intel_bb_flush_render(struct intel_bb *ibb);
void intel_bb_flush_blit(struct intel_bb *ibb);

uint32_t intel_bb_copy_data(struct intel_bb *ibb,
			    const void *data, unsigned int bytes,
			    uint32_t align);

void intel_bb_blit_start(struct intel_bb *ibb, uint32_t flags);
void intel_bb_emit_blt_copy(struct intel_bb *ibb,
			    struct intel_buf *src,
			    int src_x1, int src_y1, int src_pitch,
			    struct intel_buf *dst,
			    int dst_x1, int dst_y1, int dst_pitch,
			    int width, int height, int bpp);
void intel_bb_blt_copy(struct intel_bb *ibb,
		       struct intel_buf *src,
		       int src_x1, int src_y1, int src_pitch,
		       struct intel_buf *dst,
		       int dst_x1, int dst_y1, int dst_pitch,
		       int width, int height, int bpp);
void intel_bb_copy_intel_buf(struct intel_bb *ibb,
			     struct intel_buf *dst, struct intel_buf *src,
			     long int size);

/**
 * igt_huc_copyfunc_t:
 * @fd: drm fd
 * @ahnd: allocator handle, if it is equal to 0 we use relocations
 * @obj: drm_i915_gem_exec_object2 buffer array
 *       obj[0] is source buffer
 *       obj[1] is destination buffer
 *       obj[2] is execution buffer
 * @objsize: corresponding buffer sizes to @obj
 *
 * This is the type of the per-platform huc copy functions.
 *
 * The huc copy function emits a batchbuffer to the VDBOX engine to
 * invoke the HuC Copy kernel to copy 4K bytes from the source buffer
 * to the destination buffer.
 */
typedef void (*igt_huc_copyfunc_t)(int fd, uint64_t ahnd,
		struct drm_i915_gem_exec_object2 *obj, uint64_t *objsize);

igt_huc_copyfunc_t	igt_get_huc_copyfunc(int devid);
#endif

