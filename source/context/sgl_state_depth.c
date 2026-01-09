/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Depth/Stencil State Implementation
 */

#include "sgl_state_depth.h"

static void init_stencil_face(sgl_stencil_face_t *face) {
    face->func = GL_ALWAYS;
    face->ref = 0;
    face->func_mask = 0xFFFFFFFF;
    face->write_mask = 0xFFFFFFFF;
    face->fail_op = GL_KEEP;
    face->zfail_op = GL_KEEP;
    face->zpass_op = GL_KEEP;
}

void sgl_state_depth_init(sgl_state_depth_t *state) {
    state->depth_test_enabled = false;
    state->depth_write_enabled = true;
    state->depth_func = GL_LESS;
    state->clear_depth = 1.0f;

    state->stencil_test_enabled = false;
    state->clear_stencil = 0;
    init_stencil_face(&state->front);
    init_stencil_face(&state->back);
}

bool sgl_state_depth_set_test_enabled(sgl_state_depth_t *state, bool enabled) {
    if (state->depth_test_enabled == enabled) {
        return false;
    }
    state->depth_test_enabled = enabled;
    return true;
}

bool sgl_state_depth_set_write_enabled(sgl_state_depth_t *state, bool enabled) {
    if (state->depth_write_enabled == enabled) {
        return false;
    }
    state->depth_write_enabled = enabled;
    return true;
}

bool sgl_state_depth_set_func(sgl_state_depth_t *state, GLenum func) {
    if (state->depth_func == func) {
        return false;
    }
    state->depth_func = func;
    return true;
}

bool sgl_state_depth_set_clear(sgl_state_depth_t *state, float depth) {
    if (state->clear_depth == depth) {
        return false;
    }
    state->clear_depth = depth;
    return true;
}

bool sgl_state_stencil_set_test_enabled(sgl_state_depth_t *state, bool enabled) {
    if (state->stencil_test_enabled == enabled) {
        return false;
    }
    state->stencil_test_enabled = enabled;
    return true;
}

bool sgl_state_stencil_set_func(sgl_state_depth_t *state, GLenum face,
                                 GLenum func, GLint ref, GLuint mask) {
    bool changed = false;

    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        if (state->front.func != func ||
            state->front.ref != ref ||
            state->front.func_mask != mask) {
            state->front.func = func;
            state->front.ref = ref;
            state->front.func_mask = mask;
            changed = true;
        }
    }

    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        if (state->back.func != func ||
            state->back.ref != ref ||
            state->back.func_mask != mask) {
            state->back.func = func;
            state->back.ref = ref;
            state->back.func_mask = mask;
            changed = true;
        }
    }

    return changed;
}

bool sgl_state_stencil_set_op(sgl_state_depth_t *state, GLenum face,
                               GLenum sfail, GLenum dpfail, GLenum dppass) {
    bool changed = false;

    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        if (state->front.fail_op != sfail ||
            state->front.zfail_op != dpfail ||
            state->front.zpass_op != dppass) {
            state->front.fail_op = sfail;
            state->front.zfail_op = dpfail;
            state->front.zpass_op = dppass;
            changed = true;
        }
    }

    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        if (state->back.fail_op != sfail ||
            state->back.zfail_op != dpfail ||
            state->back.zpass_op != dppass) {
            state->back.fail_op = sfail;
            state->back.zfail_op = dpfail;
            state->back.zpass_op = dppass;
            changed = true;
        }
    }

    return changed;
}

bool sgl_state_stencil_set_write_mask(sgl_state_depth_t *state, GLenum face, GLuint mask) {
    bool changed = false;

    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        if (state->front.write_mask != mask) {
            state->front.write_mask = mask;
            changed = true;
        }
    }

    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        if (state->back.write_mask != mask) {
            state->back.write_mask = mask;
            changed = true;
        }
    }

    return changed;
}

bool sgl_state_stencil_set_clear(sgl_state_depth_t *state, int stencil) {
    if (state->clear_stencil == stencil) {
        return false;
    }
    state->clear_stencil = stencil;
    return true;
}
