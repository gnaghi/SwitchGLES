/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Color State Implementation
 */

#include "sgl_state_color.h"

void sgl_state_color_init(sgl_state_color_t *state) {
    state->mask[0] = true;
    state->mask[1] = true;
    state->mask[2] = true;
    state->mask[3] = true;

    state->clear_color[0] = 0.0f;
    state->clear_color[1] = 0.0f;
    state->clear_color[2] = 0.0f;
    state->clear_color[3] = 0.0f;
}

bool sgl_state_color_set_mask(sgl_state_color_t *state,
                               bool r, bool g, bool b, bool a) {
    if (state->mask[0] == r &&
        state->mask[1] == g &&
        state->mask[2] == b &&
        state->mask[3] == a) {
        return false;
    }
    state->mask[0] = r;
    state->mask[1] = g;
    state->mask[2] = b;
    state->mask[3] = a;
    return true;
}

bool sgl_state_color_set_clear(sgl_state_color_t *state,
                                float r, float g, float b, float a) {
    if (state->clear_color[0] == r &&
        state->clear_color[1] == g &&
        state->clear_color[2] == b &&
        state->clear_color[3] == a) {
        return false;
    }
    state->clear_color[0] = r;
    state->clear_color[1] = g;
    state->clear_color[2] = b;
    state->clear_color[3] = a;
    return true;
}
