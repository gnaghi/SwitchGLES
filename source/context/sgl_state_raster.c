/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Rasterizer State Implementation
 */

#include "sgl_state_raster.h"

void sgl_state_raster_init(sgl_state_raster_t *state) {
    state->cull_enabled = false;
    state->cull_mode = GL_BACK;
    state->front_face = GL_CCW;
    state->line_width = 1.0f;
    state->polygon_offset_factor = 0.0f;
    state->polygon_offset_units = 0.0f;
    state->polygon_offset_fill_enabled = false;
}

bool sgl_state_raster_set_cull_enabled(sgl_state_raster_t *state, bool enabled) {
    if (state->cull_enabled == enabled) {
        return false;
    }
    state->cull_enabled = enabled;
    return true;
}

bool sgl_state_raster_set_cull_mode(sgl_state_raster_t *state, GLenum mode) {
    if (state->cull_mode == mode) {
        return false;
    }
    state->cull_mode = mode;
    return true;
}

bool sgl_state_raster_set_front_face(sgl_state_raster_t *state, GLenum mode) {
    if (state->front_face == mode) {
        return false;
    }
    state->front_face = mode;
    return true;
}
