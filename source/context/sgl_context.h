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
    sgl_state_blend_t       blend_state;
    sgl_state_depth_t       depth_state;
    sgl_state_raster_t      raster_state;
    sgl_state_viewport_t    viewport_state;
    sgl_state_color_t       color_state;

    /* Resource manager */
    sgl_resource_manager_t  res_mgr;

    /* Backend (opaque) */
    sgl_backend_t          *backend;

    /* Current bindings */
    GLuint                  current_program;
    GLuint                  bound_array_buffer;
    GLuint                  bound_element_buffer;
    GLuint                  bound_textures[SGL_MAX_TEXTURE_UNITS];
    GLuint                  active_texture_unit;
    GLuint                  bound_framebuffer;
    GLuint                  bound_renderbuffer;

    /* Vertex attributes */
    sgl_vertex_attrib_t     vertex_attribs[SGL_MAX_ATTRIBS];

    /* Bound surfaces (from EGL) */
    sgl_surface_t          *draw_surface;
    sgl_surface_t          *read_surface;

    /* Pixel store state */
    GLint                   pack_alignment;    /* GL_PACK_ALIGNMENT (default 4) */
    GLint                   unpack_alignment;  /* GL_UNPACK_ALIGNMENT (default 4) */

    /* Sample coverage (MSAA not supported but values stored for query) */
    float                   sample_coverage_value;    /* default 1.0 */
    bool                    sample_coverage_invert;   /* default false */

    /* Error */
    GLenum                  error;

    /* Flags */
    bool                    initialized;
    bool                    used;
    int                     client_version;  /* 2 for GLES2 */
} sgl_context_t;

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
