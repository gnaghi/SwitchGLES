/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Main Context Structure
 */

#ifndef SGL_CONTEXT_H
#define SGL_CONTEXT_H

#include "sgl_gl_types.h"
#include "sgl_state_blend.h"
#include "sgl_state_depth.h"
#include "sgl_state_raster.h"
#include "sgl_state_viewport.h"
#include "sgl_state_color.h"
#include "sgl_resource_manager.h"
#include "../backend/sgl_backend.h"

/* Forward declarations for EGL types */
typedef struct sgl_surface sgl_surface_t;

/* GL Context */
typedef struct sgl_context {
    /* State classes (GLOVE pattern) */
    sgl_state_blend_t blend_state;
    sgl_state_depth_t depth_state;
    sgl_state_raster_t raster_state;
    sgl_state_viewport_t viewport_state;
    sgl_state_color_t color_state;

    /* Resource manager */
    sgl_resource_manager_t res_mgr;

    /* Backend (opaque) */
    sgl_backend_t *backend;

    /* Current bindings */
    GLuint current_program;
    GLuint bound_array_buffer;
    GLuint bound_element_buffer;
    GLuint bound_textures[SGL_MAX_TEXTURE_UNITS];         /* GL_TEXTURE_2D bindings */
    GLuint bound_cubemap_textures[SGL_MAX_TEXTURE_UNITS]; /* GL_TEXTURE_CUBE_MAP bindings */
    GLuint active_texture_unit;
    GLuint bound_framebuffer;      /* GL_FRAMEBUFFER (legacy compat) */
    GLuint bound_read_framebuffer; /* GL_READ_FRAMEBUFFER */
    GLuint bound_draw_framebuffer; /* GL_DRAW_FRAMEBUFFER */
    GLuint bound_renderbuffer;
    GLuint bound_vertex_array; /* GLES 3.0 vertex array object (0 = default) */
    /* GLES 3.0 generic buffer binding points (sgl_buffer_binding) */
    GLuint bound_copy_read_buffer;
    GLuint bound_copy_write_buffer;
    GLuint bound_pixel_pack_buffer;
    GLuint bound_pixel_unpack_buffer;
    GLuint bound_uniform_buffer;
    GLuint bound_transform_feedback_buffer;

    /* Vertex attributes */
    sgl_vertex_attrib_t vertex_attribs[SGL_MAX_ATTRIBS];

    /* Bound surfaces (from EGL) */
    sgl_surface_t *draw_surface;
    sgl_surface_t *read_surface;

    /* Pixel store state */
    GLint pack_alignment;   /* GL_PACK_ALIGNMENT (default 4) */
    GLint unpack_alignment; /* GL_UNPACK_ALIGNMENT (default 4) */
    /* GLES 3.0 GL_UNPACK_ROW_LENGTH / SKIP_ROWS / SKIP_PIXELS (default 0;
     * always 0 in a GLES 2.0 context) */
    GLint unpack_row_length;
    GLint unpack_skip_rows;
    GLint unpack_skip_pixels;

    /* Sample coverage (MSAA not supported but values stored for query) */
    float sample_coverage_value; /* default 1.0 */
    bool sample_coverage_invert; /* default false */

    /* Caps tracked for query but not affecting rendering on this hardware */
    bool dither_enabled;           /* default true (GLES2 spec) */
    bool sample_alpha_to_coverage; /* default false */
    bool sample_coverage_enabled;  /* default false */
    GLenum generate_mipmap_hint;   /* default GL_DONT_CARE */

    /* Error */
    GLenum error;

    /* Flags */
    bool initialized;
    bool used;
    /* eglDestroyContext called while the context was current: the handle is
     * invalid but the context lives until it is no longer current (EGL §3.7.2). */
    bool delete_pending;
    int client_version; /* GLES major version: 2, or 3 (SGL_ENABLE_ES3_CONTEXT builds only) */
    int config_id;      /* EGLConfig id this context was created with */
} sgl_context_t;

/* True for a GLES 3.0 context. Entry points and enums introduced by ES 3.0
 * check it, so that an ES 2.0 context keeps the exact ES 2.0 behaviour. Only
 * builds with SGL_ENABLE_ES3_CONTEXT can create such a context (development
 * path, see docs/GLES3_PLAN.md). */
static inline bool sgl_ctx_is_es3(const sgl_context_t *ctx) {
    return ctx->client_version >= 3;
}

/* Context lifecycle */
void sgl_context_init(sgl_context_t *ctx);
void sgl_context_destroy(sgl_context_t *ctx);

/* Get/set current context */
sgl_context_t *sgl_get_current_context(void);
void sgl_set_current_context(sgl_context_t *ctx);

/* Error handling */
void sgl_set_error(sgl_context_t *ctx, GLenum error);
GLenum sgl_get_error(sgl_context_t *ctx);

/* Initialize GL state to defaults */
void sgl_context_init_state(sgl_context_t *ctx);

#endif /* SGL_CONTEXT_H */
