/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Backend-agnostic types
 */

#ifndef SGL_BACKEND_TYPES_H
#define SGL_BACKEND_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <GLES2/gl2.h>

/* Handle types - backend uses opaque uint32_t handles */
typedef uint32_t sgl_handle_t;
#define SGL_INVALID_HANDLE 0

/* Forward declaration */
typedef struct sgl_backend sgl_backend_t;

/* Viewport state */
typedef struct sgl_viewport_state {
    int x, y;
    int width, height;
    float near_val, far_val;
} sgl_viewport_state_t;

/* Scissor state */
typedef struct sgl_scissor_state {
    int x, y;
    int width, height;
    bool enabled;
} sgl_scissor_state_t;

/* Blend state */
typedef struct sgl_blend_state {
    bool enabled;
    GLenum src_rgb, dst_rgb;
    GLenum src_alpha, dst_alpha;
    GLenum equation_rgb, equation_alpha;
} sgl_blend_state_t;

/* Depth state */
typedef struct sgl_depth_state {
    bool test_enabled;
    bool write_enabled;
    GLenum func;
    float clear_value;
} sgl_depth_state_t;

/* Stencil face state */
typedef struct sgl_stencil_face_state {
    GLenum func;
    GLint ref;
    GLuint func_mask;
    GLuint write_mask;
    GLenum fail_op;
    GLenum zfail_op;
    GLenum zpass_op;
} sgl_stencil_face_state_t;

/* Stencil state */
typedef struct sgl_stencil_state {
    bool enabled;
    sgl_stencil_face_state_t front;
    sgl_stencil_face_state_t back;
    int clear_value;
} sgl_stencil_state_t;

/* Combined depth-stencil state (for atomic binding) */
typedef struct sgl_depth_stencil_state {
    /* Depth state */
    bool depth_test_enabled;
    bool depth_write_enabled;
    GLenum depth_func;
    float depth_clear_value;
    /* Stencil state */
    bool stencil_test_enabled;
    sgl_stencil_face_state_t stencil_front;
    sgl_stencil_face_state_t stencil_back;
    int stencil_clear_value;
} sgl_depth_stencil_state_t;

/* Rasterizer state */
typedef struct sgl_raster_state {
    bool cull_enabled;
    GLenum cull_mode;
    GLenum front_face;
} sgl_raster_state_t;

/* Color state */
typedef struct sgl_color_state {
    bool mask[4];  /* RGBA */
    float clear_color[4];
} sgl_color_state_t;

/* Maximum constants */
#define SGL_MAX_VERTEX_ATTRIBS  8
#define SGL_MAX_TEXTURE_UNITS   8
#define SGL_MAX_UNIFORMS        8

#endif /* SGL_BACKEND_TYPES_H */
