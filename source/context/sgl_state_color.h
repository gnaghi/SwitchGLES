/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Color State Class
 */

#ifndef SGL_STATE_COLOR_H
#define SGL_STATE_COLOR_H

#include <GLES2/gl2.h>
#include <stdbool.h>

typedef struct sgl_state_color {
    bool mask[4];  /* RGBA write mask */
    float clear_color[4];  /* RGBA clear color */
} sgl_state_color_t;

/* Initialize to GL defaults */
void sgl_state_color_init(sgl_state_color_t *state);

/* Update functions - return true if value changed */
bool sgl_state_color_set_mask(sgl_state_color_t *state,
                               bool r, bool g, bool b, bool a);
bool sgl_state_color_set_clear(sgl_state_color_t *state,
                                float r, float g, float b, float a);

#endif /* SGL_STATE_COLOR_H */
