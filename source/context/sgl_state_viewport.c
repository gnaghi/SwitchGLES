/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Viewport/Scissor State Implementation
 */

#include "sgl_state_viewport.h"

void sgl_state_viewport_init(sgl_state_viewport_t *state, int width, int height) {
    state->viewport_x = 0;
    state->viewport_y = 0;
    state->viewport_width = width;
    state->viewport_height = height;
    state->depth_near = 0.0f;
    state->depth_far = 1.0f;

    state->scissor_x = 0;
    state->scissor_y = 0;
    state->scissor_width = width;
    state->scissor_height = height;
    state->scissor_enabled = false;
}

bool sgl_state_viewport_set(sgl_state_viewport_t *state,
                             int x, int y, int width, int height) {
    if (state->viewport_x == x &&
        state->viewport_y == y &&
        state->viewport_width == width &&
        state->viewport_height == height) {
        return false;
    }
    state->viewport_x = x;
    state->viewport_y = y;
    state->viewport_width = width;
    state->viewport_height = height;
    return true;
}

bool sgl_state_viewport_set_depth_range(sgl_state_viewport_t *state,
                                         float near_val, float far_val) {
    if (state->depth_near == near_val && state->depth_far == far_val) {
        return false;
    }
    state->depth_near = near_val;
    state->depth_far = far_val;
    return true;
}

bool sgl_state_scissor_set(sgl_state_viewport_t *state,
                            int x, int y, int width, int height) {
    if (state->scissor_x == x &&
        state->scissor_y == y &&
        state->scissor_width == width &&
        state->scissor_height == height) {
        return false;
    }
    state->scissor_x = x;
    state->scissor_y = y;
    state->scissor_width = width;
    state->scissor_height = height;
    return true;
}

bool sgl_state_scissor_set_enabled(sgl_state_viewport_t *state, bool enabled) {
    if (state->scissor_enabled == enabled) {
        return false;
    }
    state->scissor_enabled = enabled;
    return true;
}
