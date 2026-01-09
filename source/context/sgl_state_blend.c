/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Blend State Implementation
 */

#include "sgl_state_blend.h"

void sgl_state_blend_init(sgl_state_blend_t *state) {
    state->enabled = false;
    state->src_rgb = GL_ONE;
    state->dst_rgb = GL_ZERO;
    state->src_alpha = GL_ONE;
    state->dst_alpha = GL_ZERO;
    state->equation_rgb = GL_FUNC_ADD;
    state->equation_alpha = GL_FUNC_ADD;
    state->color[0] = 0.0f;
    state->color[1] = 0.0f;
    state->color[2] = 0.0f;
    state->color[3] = 0.0f;
}

bool sgl_state_blend_set_enabled(sgl_state_blend_t *state, bool enabled) {
    if (state->enabled == enabled) {
        return false;
    }
    state->enabled = enabled;
    return true;
}

bool sgl_state_blend_set_func(sgl_state_blend_t *state,
                               GLenum src_rgb, GLenum dst_rgb,
                               GLenum src_alpha, GLenum dst_alpha) {
    if (state->src_rgb == src_rgb &&
        state->dst_rgb == dst_rgb &&
        state->src_alpha == src_alpha &&
        state->dst_alpha == dst_alpha) {
        return false;
    }
    state->src_rgb = src_rgb;
    state->dst_rgb = dst_rgb;
    state->src_alpha = src_alpha;
    state->dst_alpha = dst_alpha;
    return true;
}

bool sgl_state_blend_set_equation(sgl_state_blend_t *state,
                                   GLenum rgb, GLenum alpha) {
    if (state->equation_rgb == rgb && state->equation_alpha == alpha) {
        return false;
    }
    state->equation_rgb = rgb;
    state->equation_alpha = alpha;
    return true;
}
