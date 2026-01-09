/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Viewport/Scissor State Class
 */

#ifndef SGL_STATE_VIEWPORT_H
#define SGL_STATE_VIEWPORT_H

#include <GLES2/gl2.h>
#include <stdbool.h>

typedef struct sgl_state_viewport {
    /* Viewport */
    int viewport_x, viewport_y;
    int viewport_width, viewport_height;
    float depth_near, depth_far;

    /* Scissor */
    int scissor_x, scissor_y;
    int scissor_width, scissor_height;
    bool scissor_enabled;
} sgl_state_viewport_t;

/* Initialize to GL defaults */
void sgl_state_viewport_init(sgl_state_viewport_t *state, int width, int height);

/* Viewport update functions - return true if value changed */
bool sgl_state_viewport_set(sgl_state_viewport_t *state,
                             int x, int y, int width, int height);
bool sgl_state_viewport_set_depth_range(sgl_state_viewport_t *state,
                                         float near_val, float far_val);

/* Scissor update functions */
bool sgl_state_scissor_set(sgl_state_viewport_t *state,
                            int x, int y, int width, int height);
bool sgl_state_scissor_set_enabled(sgl_state_viewport_t *state, bool enabled);

#endif /* SGL_STATE_VIEWPORT_H */
