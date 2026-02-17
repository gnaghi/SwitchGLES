/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Blend State Class
 */

#ifndef SGL_STATE_BLEND_H
#define SGL_STATE_BLEND_H

#include <GLES2/gl2.h>
#include <stdbool.h>

typedef struct sgl_state_blend {
    bool enabled;
    GLenum src_rgb, dst_rgb;
    GLenum src_alpha, dst_alpha;
    GLenum equation_rgb, equation_alpha;
    GLfloat color[4];  /* Blend constant color (RGBA) */
} sgl_state_blend_t;

/* Initialize to GL defaults */
void sgl_state_blend_init(sgl_state_blend_t *state);

/* Update functions - return true if value changed */
bool sgl_state_blend_set_enabled(sgl_state_blend_t *state, bool enabled);
bool sgl_state_blend_set_func(sgl_state_blend_t *state,
                               GLenum src_rgb, GLenum dst_rgb,
                               GLenum src_alpha, GLenum dst_alpha);
bool sgl_state_blend_set_equation(sgl_state_blend_t *state,
                                   GLenum rgb, GLenum alpha);

#endif /* SGL_STATE_BLEND_H */
