/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Types and declarations
 */

#ifndef DK_BACKEND_H
#define DK_BACKEND_H

#include "../sgl_backend.h"
#include "../../context/sgl_gl_types.h"
#include <deko3d.h>

/* Free-list block — shared type for VBO and texture memory reclamation */
#define SGL_TEX_FREE_LIST_MAX 256

typedef struct {
    uint32_t offset;
    uint32_t size;
} sgl_vbo_free_block_t;

/* Orphaned VBO block awaiting reuse, tagged with the frame slot whose fence
 * covers every draw that may still read it. */
typedef struct {
    uint32_t offset;
    uint32_t size;
    int slot;
} dk_deferred_free_t;

/* Fixed-function state recorded in the current command buffer, per group
 * (dk_state.c). Each entry holds the deko3d-side values that were last
 * recorded (after FBO clamping / forcing), so an apply whose derived values
 * are identical records nothing. A group's bit in `valid` is dropped whenever
 * the recorded commands may be lost or the GPU registers overwritten behind
 * the cache's back: dk_cmdbuf_clear (every cmdbuf clear), render-target
 * (re)binds, dk_clear (own scissor + depth-stencil), frame start, context
 * switch. See dk_state_cache_invalidate(). */
#define DK_SC_VIEWPORT (1u << 0)
#define DK_SC_SCISSOR (1u << 1)
#define DK_SC_BLEND (1u << 2)
#define DK_SC_DEPTH_STENCIL (1u << 3)
#define DK_SC_RASTER (1u << 4)
#define DK_SC_COLOR_MASK (1u << 5)

typedef struct dk_blend_key {
    uint32_t enabled;
    DkBlendState blend; /* zero when disabled (not recorded) */
    float color[4];     /* zero when disabled (not recorded) */
} dk_blend_key_t;

typedef struct dk_depth_stencil_key {
    DkDepthStencilState ds;
    uint8_t front[3]; /* write mask, ref, func mask */
    uint8_t back[3];
    uint8_t pad[2];
} dk_depth_stencil_key_t;

typedef struct dk_raster_key {
    DkRasterizerState raster;
    float bias_units;  /* zero when polygon offset is disabled (not recorded) */
    float bias_factor; /* idem */
    float line_width;  /* clamped to [SGL_MIN_LINE_WIDTH, SGL_MAX_LINE_WIDTH] */
} dk_raster_key_t;

typedef struct dk_state_cache {
    uint32_t valid; /* DK_SC_* bits of the groups whose entry is recorded */
    DkViewport viewport;
    DkScissor scissor;
    dk_blend_key_t blend;
    dk_depth_stencil_key_t depth_stencil;
    dk_raster_key_t raster;
    uint32_t color_mask;
} dk_state_cache_t;

/* deko3d backend-specific data */
typedef struct dk_backend_data {
    /* Device (shared with display) */
    DkDevice device;

    /* Queue */
    DkQueue queue;

    /* Command buffers - one per framebuffer slot */
    DkMemBlock cmdbuf_memblock[SGL_FB_NUM];
    DkCmdBuf cmdbufs[SGL_FB_NUM];
    DkCmdBuf cmdbuf; /* Active command buffer */
    int current_cmdbuf;

    /* Fences for synchronization */
    DkFence fences[SGL_FB_NUM];
    bool fence_active[SGL_FB_NUM];
    /* GLES 3.0 fence sync objects (dk_fence_sync), indexed like the GL ones */
    DkFence sync_fences[SGL_MAX_SYNCS];

    /* Shader code memory */
    DkMemBlock code_memblock;
    uint32_t code_offset;
    uint32_t active_shader_count; /* Track loaded shaders for code memory reset */
    uint32_t
        active_program_count; /* Track linked programs — code memory only resets when both are 0 */

    /* Data memory (vertices, indices, uniforms) */
    DkMemBlock data_memblock;
    uint32_t data_offset;
    uint32_t data_offset_watermark; /* Peak VBO bump allocator usage */

    /* VBO free list for memory reclamation, sorted by offset. Grown on demand
     * (dk_vbo_free_reserve): a freed block that did not fit used to be
     * dropped, and fragmentation under heavy orphaning (spearmint, 4-player
     * split screen) leaked the VBO region that way. */
    sgl_vbo_free_block_t *vbo_free_list;
    int vbo_free_count;
    int vbo_free_capacity;

    /* Uniform buffer region */
    uint32_t uniform_base;
    uint32_t uniform_offset;   /* Bump allocator, never reused within a frame */
    uint32_t uniform_slot_end; /* End of the current frame slot's sub-region */
    /* Uniform-space generation: bumped (never 0) every time the bump allocator
     * restarts or the cmdbuf is cleared, i.e. whenever an address handed out
     * earlier may be reused or a recorded push may not reach the GPU. A packed
     * UBO is rebound without a push only if its gpu_generation matches. */
    uint32_t uniform_generation;

    /* Client array region (per-frame, per-slot to avoid GPU race conditions) */
    uint32_t client_array_base;
    uint32_t client_array_offset;
    uint32_t client_array_slot_end; /* End boundary for current slot's sub-region */

    /* Texture memory */
    DkMemBlock texture_memblock;
    uint32_t texture_offset;

    /* Texture free list for memory reclamation (like VBO free-list) */
    sgl_vbo_free_block_t tex_free_list[SGL_TEX_FREE_LIST_MAX];
    int tex_free_count;

    /* Per-texture GPU allocation tracking (for free-list return) */
    uint32_t texture_gpu_offset[SGL_MAX_TEXTURES];
    uint32_t texture_gpu_size[SGL_MAX_TEXTURES];

    /* Descriptor memory */
    DkMemBlock descriptor_memblock;
    DkGpuAddr image_descriptor_addr;
    DkGpuAddr sampler_descriptor_addr;
    bool descriptors_bound;
    bool program_bound;    /* true after valid program bound (both VS+FS), skip draw if false */
    /* Program whose dkCmdBufBindShaders is recorded in the current cmdbuf and
     * still valid (0 = none): dk_bind_program skips the bind for it. Cleared by
     * dk_cmdbuf_clear (recorded commands dropped), dk_link_program (new shader
     * copies) and dk_delete_program (handle may be reused). */
    sgl_handle_t bound_program;
    bool cmdbuf_submitted; /* true after dk_end_frame finishes the cmdbuf */

    /* Swapchain (from surface) */
    DkSwapchain swapchain;

    /* Framebuffer images */
    DkImage *framebuffers;
    DkImage *depth_images[SGL_FB_NUM]; /* Per-slot depth buffers */
    int num_framebuffers;

    /* Current framebuffer slot */
    int current_slot;

    /* Default framebuffer dimensions (from surface) */
    uint32_t fb_width;
    uint32_t fb_height;

    /* Texture data for binding - store directly, indexed by texture ID */
    DkImage textures[SGL_MAX_TEXTURES];
    DkImageDescriptor texture_descriptors[SGL_MAX_TEXTURES];
    bool texture_initialized[SGL_MAX_TEXTURES];
    bool texture_is_cubemap[SGL_MAX_TEXTURES];   /* true if texture is cubemap, false if 2D */
    bool texture_used_as_rt[SGL_MAX_TEXTURES];   /* true if texture was used as FBO render target */
    bool sampler_dirty[SGL_MAX_TEXTURES];        /* descriptor rewritten by the CPU mid-frame */
    uint8_t cubemap_face_mask[SGL_MAX_TEXTURES]; /* bitmask of uploaded cubemap faces (6 bits) */
    bool cubemap_needs_barrier[SGL_MAX_TEXTURES]; /* true after cubemap complete, cleared after
                                                     first barrier */

    /* Texture dimensions and mipmap info - indexed by texture ID */
    uint32_t texture_width[SGL_MAX_TEXTURES];
    uint32_t texture_height[SGL_MAX_TEXTURES];
    uint32_t texture_mip_levels[SGL_MAX_TEXTURES];
    uint32_t texture_level_mask[SGL_MAX_TEXTURES]; /* Bitmask of defined mip levels (bit N = level N
                                                      uploaded) */
    DkImageFormat texture_format[SGL_MAX_TEXTURES];
    GLenum texture_gl_format[SGL_MAX_TEXTURES]; /* Original GL internalformat (for swizzle/bpp) */
    GLenum texture_gl_type[SGL_MAX_TEXTURES];   /* Original GL type (for packed format bpp) */

    /* Texture sampler parameters - indexed by texture ID */
    GLenum texture_min_filter[SGL_MAX_TEXTURES];
    GLenum texture_mag_filter[SGL_MAX_TEXTURES];
    GLenum texture_wrap_s[SGL_MAX_TEXTURES];
    GLenum texture_wrap_t[SGL_MAX_TEXTURES];

    /* Renderbuffer depth images - indexed by renderbuffer ID */
    DkImage renderbuffer_images[SGL_MAX_RENDERBUFFERS];
    DkMemBlock
        renderbuffer_memblocks[SGL_MAX_RENDERBUFFERS]; /* Dedicated memblock per renderbuffer */
    bool renderbuffer_initialized[SGL_MAX_RENDERBUFFERS];
    uint32_t renderbuffer_width[SGL_MAX_RENDERBUFFERS];
    uint32_t renderbuffer_height[SGL_MAX_RENDERBUFFERS];

    /* Shader data - indexed by shader handle (temporary storage until link) */
    DkShader dk_shaders[SGL_MAX_SHADERS];
    bool shader_loaded[SGL_MAX_SHADERS];

    /* Per-program shader copies - captured at link time */
    DkShader program_shaders[SGL_MAX_PROGRAMS][2];  /* [prog][0]=VS, [prog][1]=FS */
    bool program_shader_valid[SGL_MAX_PROGRAMS][2]; /* [prog][0]=VS valid, [prog][1]=FS valid */

    /* Program uniform tracking */
    sgl_uniform_binding_t *current_vertex_uniforms;
    sgl_uniform_binding_t *current_fragment_uniforms;

    /* State tracking */
    bool state_initialized;

    /* Current FBO tracking - for debug and clear operations */
    sgl_handle_t current_fbo;         /* Currently bound FBO (0 = default) */
    sgl_handle_t current_fbo_color;   /* Color attachment handle (texture or renderbuffer) */
    sgl_handle_t current_fbo_depth;   /* Depth attachment handle (texture or renderbuffer) */
    sgl_handle_t current_fbo_stencil; /* Stencil attachment handle (renderbuffer) */
    bool current_fbo_color_is_rb;     /* true if color attachment is a renderbuffer */
    bool current_fbo_depth_is_rb;     /* true if depth attachment is a renderbuffer */
    bool current_fbo_stencil_is_rb;   /* true if stencil attachment is a renderbuffer */

    /* Diagnostic counters (per-frame, reset in dk_begin_frame) */
    uint32_t diag_orphan_flushes;    /* Times dk_submit_and_reset called from orphan overflow */
    uint32_t diag_uniform_overflows; /* Times dk_alloc_uniform returned fallback offset */
    uint32_t diag_draw_count;        /* Total draw calls this frame */
    uint32_t diag_texture_binds;     /* Total texture bind calls this frame */

    /* Mid-frame flush tracking for overflow protection.
     * flush_finish tests do up to 2^20 draws without eglSwapBuffers.
     * We periodically flush to avoid cmdbuf (~4MB, ~4K draws) and
     * client_array (~16MB/slot) overflow. */
    uint32_t draws_since_flush; /* Draws since last dk_submit_and_reset */
    bool in_overflow_callback;  /* Re-entrancy guard for overflow callback */
    bool vbo_data_dirty;        /* true after CPU writes to VBO region — need GPU L2 invalidation */
    /* true after a CPU write to GPU-visible memory (VBO data, vertex staging,
     * constant block) that has not been followed by a `dsb st` yet; drained by
     * dk_flush_cpu_stores before the vertex bindings / draw command. */
    bool cpu_store_pending;

    /* Shared constant buffer of the disabled vertex attributes (dk_draw.c):
     * one SGL_MAX_ATTRIBS x vec4 block in the frame slot's client-array space,
     * reused by every draw whose disabled slots hold the values recorded in
     * the shadow; a new block is written otherwise. Invalidated with the
     * client-array allocator (dk_begin_frame, dk_submit_and_reset, overflow
     * callback). */
    bool attrib_const_valid;
    uint32_t attrib_const_addr; /* offset within data_memblock */
    float attrib_const_shadow[SGL_MAX_ATTRIBS][4];

    /* Last fixed-function state recorded per group (see dk_state_cache_t). */
    dk_state_cache_t state_cache;

/* Deferred VBO free list — blocks freed only after GPU sync: the fence of the
 * slot recorded with each entry (dk_wait_fence), or WaitIdle
 * (dk_submit_and_reset). Used by buffer orphaning: old allocation can't be
 * freed immediately because in-flight draws may still reference it.
 * Grown on demand (dk_buffer_data_orphan): it holds up to SGL_FB_NUM frames
 * of orphaning, and an entry that did not fit would leak its block for good
 * (spearmint in 4-player split screen orphans more than 256 blocks across
 * three frames and filled the 192 MB VBO region that way). */
    dk_deferred_free_t *deferred_free;
    int deferred_free_count;
    int deferred_free_capacity;
} dk_backend_data_t;

/**
 * Forget the fixed-function state recorded by dk_state.c for the groups in
 * `mask` (DK_SC_* bits): their next apply records the state again whatever
 * its value. Called wherever the recorded commands may be dropped or the
 * GPU registers of a group written behind the cache's back (dk_cmdbuf_clear,
 * render-target binds, dk_clear, frame start, eglMakeCurrent).
 */
static inline void dk_state_cache_invalidate_mask(dk_backend_data_t *dk, uint32_t mask) {
    dk->state_cache.valid &= ~mask;
}

static inline void dk_state_cache_invalidate(dk_backend_data_t *dk) {
    dk->state_cache.valid = 0;
}

/* Backend operations table */
extern const sgl_backend_ops_t dk_backend_ops;

/* Create/destroy deko3d backend */
sgl_backend_t *dk_backend_create(DkDevice device);
void dk_backend_destroy(sgl_backend_t *be);

/* Internal helpers */
DkCompareOp dk_convert_compare_op(GLenum func);
DkStencilOp dk_convert_stencil_op(GLenum op);
DkBlendFactor dk_convert_blend_factor(GLenum factor);
DkBlendOp dk_convert_blend_op(GLenum op);
DkPrimitive dk_convert_primitive(GLenum mode);
DkImageFormat dk_convert_format(GLenum internalformat, GLenum format, GLenum type);

/* Vertex attribute helpers */
void dk_get_attrib_format(GLenum type, GLint size, GLboolean normalized, bool integer,
                          DkVtxAttribSize *outSize,
                          DkVtxAttribType *outType);
GLsizei dk_get_type_size(GLenum type);
GLsizei dk_get_attrib_bytes(GLenum type, GLint size);

#endif /* DK_BACKEND_H */
