/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Depth/Stencil State Class
 */

#ifndef SGL_STATE_DEPTH_H
#define SGL_STATE_DEPTH_H

#include <GLES2/gl2.h>
#include <stdbool.h>

/* Stencil face state */
typedef struct sgl_stencil_face {
    GLenum func;
    GLint ref;
    GLuint func_mask;
    GLuint write_mask;
    GLenum fail_op;
    GLenum zfail_op;
    GLenum zpass_op;
} sgl_stencil_face_t;

/* Depth/stencil state */
typedef struct sgl_state_depth {
    /* Depth */
    bool depth_test_enabled;
    bool depth_write_enabled;
    GLenum depth_func;
    float clear_depth;

    /* Stencil */
    bool stencil_test_enabled;
    int clear_stencil;
    sgl_stencil_face_t front;
    sgl_stencil_face_t back;
} sgl_state_depth_t;

/* Initialize to GL defaults */
void sgl_state_depth_init(sgl_state_depth_t *state);

/* Depth update functions - return true if value changed */
bool sgl_state_depth_set_test_enabled(sgl_state_depth_t *state, bool enabled);
bool sgl_state_depth_set_write_enabled(sgl_state_depth_t *state, bool enabled);
bool sgl_state_depth_set_func(sgl_state_depth_t *state, GLenum func);
bool sgl_state_depth_set_clear(sgl_state_depth_t *state, float depth);

/* Stencil update functions */
bool sgl_state_stencil_set_test_enabled(sgl_state_depth_t *state, bool enabled);
bool sgl_state_stencil_set_func(sgl_state_depth_t *state, GLenum face,
                                 GLenum func, GLint ref, GLuint mask);
bool sgl_state_stencil_set_op(sgl_state_depth_t *state, GLenum face,
                               GLenum sfail, GLenum dpfail, GLenum dppass);
bool sgl_state_stencil_set_write_mask(sgl_state_depth_t *state, GLenum face, GLuint mask);
bool sgl_state_stencil_set_clear(sgl_state_depth_t *state, int stencil);

#endif /* SGL_STATE_DEPTH_H */
