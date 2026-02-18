/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Types and declarations
 */

#ifndef DK_BACKEND_H
#define DK_BACKEND_H

#include "../sgl_backend.h"
#include "../../context/sgl_gl_types.h"
#include <deko3d.h>

/* deko3d backend-specific data */
typedef struct dk_backend_data {
    /* Device (shared with display) */
    DkDevice device;

    /* Queue */
    DkQueue queue;

    /* Command buffers - one per framebuffer slot */
    DkMemBlock cmdbuf_memblock[SGL_FB_NUM];
    DkCmdBuf cmdbufs[SGL_FB_NUM];
    DkCmdBuf cmdbuf;  /* Active command buffer */
    int current_cmdbuf;

    /* Fences for synchronization */
    DkFence fences[SGL_FB_NUM];
    bool fence_active[SGL_FB_NUM];

    /* Shader code memory */
    DkMemBlock code_memblock;
    uint32_t code_offset;

    /* Data memory (vertices, indices, uniforms) */
    DkMemBlock data_memblock;
    uint32_t data_offset;

    /* Uniform buffer region */
    uint32_t uniform_base;
    uint32_t uniform_offset;

    /* Client array region (per-frame, per-slot to avoid GPU race conditions) */
    uint32_t client_array_base;
    uint32_t client_array_offset;
    uint32_t client_array_slot_end;  /* End boundary for current slot's sub-region */

    /* Texture memory */
    DkMemBlock texture_memblock;
    uint32_t texture_offset;

    /* Descriptor memory */
    DkMemBlock descriptor_memblock;
    DkGpuAddr image_descriptor_addr;
    DkGpuAddr sampler_descriptor_addr;
    bool descriptors_bound;
    bool cmdbuf_submitted;  /* true after dk_end_frame finishes the cmdbuf */

    /* Swapchain (from surface) */
    DkSwapchain swapchain;

    /* Framebuffer images */
    DkImage *framebuffers;
    DkImage *depth_images[SGL_FB_NUM];  /* Per-slot depth buffers */
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
    bool texture_is_cubemap[SGL_MAX_TEXTURES];  /* true if texture is cubemap, false if 2D */
    bool texture_used_as_rt[SGL_MAX_TEXTURES];  /* true if texture was used as FBO render target */
    uint8_t cubemap_face_mask[SGL_MAX_TEXTURES]; /* bitmask of uploaded cubemap faces (6 bits) */
    bool cubemap_needs_barrier[SGL_MAX_TEXTURES]; /* true after cubemap complete, cleared after first barrier */

    /* Texture dimensions and mipmap info - indexed by texture ID */
    uint32_t texture_width[SGL_MAX_TEXTURES];
    uint32_t texture_height[SGL_MAX_TEXTURES];
    uint32_t texture_mip_levels[SGL_MAX_TEXTURES];
    DkImageFormat texture_format[SGL_MAX_TEXTURES];
    GLenum texture_gl_format[SGL_MAX_TEXTURES];  /* Original GL internalformat (for swizzle/bpp) */

    /* Texture sampler parameters - indexed by texture ID */
    GLenum texture_min_filter[SGL_MAX_TEXTURES];
    GLenum texture_mag_filter[SGL_MAX_TEXTURES];
    GLenum texture_wrap_s[SGL_MAX_TEXTURES];
    GLenum texture_wrap_t[SGL_MAX_TEXTURES];

    /* Renderbuffer depth images - indexed by renderbuffer ID */
    DkImage renderbuffer_images[SGL_MAX_RENDERBUFFERS];
    DkMemBlock renderbuffer_memblocks[SGL_MAX_RENDERBUFFERS];  /* Dedicated memblock per renderbuffer */
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
    sgl_handle_t current_fbo;        /* Currently bound FBO (0 = default) */
    sgl_handle_t current_fbo_color;  /* Color attachment texture */
    sgl_handle_t current_fbo_depth;  /* Depth attachment renderbuffer */
} dk_backend_data_t;

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
void dk_get_attrib_format(GLenum type, GLint size, GLboolean normalized,
                          DkVtxAttribSize *outSize, DkVtxAttribType *outType);
GLsizei dk_get_type_size(GLenum type);

#endif /* DK_BACKEND_H */
