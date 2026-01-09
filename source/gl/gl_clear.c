/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Clear, Viewport, Scissor
 */

#include "gl_common.h"
#include <stdio.h>

/* Note: glGetError is in gl_query.c */

GL_APICALL void GL_APIENTRY glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    GET_CTX();
    sgl_state_color_set_clear(&ctx->color_state, red, green, blue, alpha);
    SGL_TRACE_STATE("glClearColor(%.2f, %.2f, %.2f, %.2f)", red, green, blue, alpha);
}

GL_APICALL void GL_APIENTRY glClearDepthf(GLfloat depth) {
    GET_CTX();
    sgl_state_depth_set_clear(&ctx->depth_state, depth);
    SGL_TRACE_STATE("glClearDepthf(%.2f)", depth);
}

GL_APICALL void GL_APIENTRY glClearStencil(GLint s) {
    GET_CTX();
    sgl_state_stencil_set_clear(&ctx->depth_state, s);
    SGL_TRACE_STATE("glClearStencil(%d)", s);
}

GL_APICALL void GL_APIENTRY glClear(GLbitfield mask) {
    sgl_ensure_frame_ready();
    GET_CTX();
    CHECK_BACKEND();

    /* Delegate to backend for actual clear operations */
    if (ctx->backend->ops->clear) {
        ctx->backend->ops->clear(ctx->backend, mask,
                                  ctx->color_state.clear_color,
                                  ctx->depth_state.clear_depth,
                                  ctx->depth_state.clear_stencil);
    }

    /* Re-apply combined depth-stencil state after clear if stencil was cleared
     * This uses the combined function to avoid overwriting depth state */
    if ((mask & GL_STENCIL_BUFFER_BIT) && ctx->backend->ops->apply_depth_stencil) {
        sgl_depth_stencil_state_t dss;
        dss.depth_test_enabled = ctx->depth_state.depth_test_enabled;
        dss.depth_write_enabled = ctx->depth_state.depth_write_enabled;
        dss.depth_func = ctx->depth_state.depth_func;
        dss.depth_clear_value = ctx->depth_state.clear_depth;
        dss.stencil_test_enabled = ctx->depth_state.stencil_test_enabled;
        dss.stencil_front.func = ctx->depth_state.front.func;
        dss.stencil_front.ref = ctx->depth_state.front.ref;
        dss.stencil_front.func_mask = ctx->depth_state.front.func_mask;
        dss.stencil_front.write_mask = ctx->depth_state.front.write_mask;
        dss.stencil_front.fail_op = ctx->depth_state.front.fail_op;
        dss.stencil_front.zfail_op = ctx->depth_state.front.zfail_op;
        dss.stencil_front.zpass_op = ctx->depth_state.front.zpass_op;
        dss.stencil_back.func = ctx->depth_state.back.func;
        dss.stencil_back.ref = ctx->depth_state.back.ref;
        dss.stencil_back.func_mask = ctx->depth_state.back.func_mask;
        dss.stencil_back.write_mask = ctx->depth_state.back.write_mask;
        dss.stencil_back.fail_op = ctx->depth_state.back.fail_op;
        dss.stencil_back.zfail_op = ctx->depth_state.back.zfail_op;
        dss.stencil_back.zpass_op = ctx->depth_state.back.zpass_op;
        dss.stencil_clear_value = ctx->depth_state.clear_stencil;
        ctx->backend->ops->apply_depth_stencil(ctx->backend, &dss);
    }
}

GL_APICALL void GL_APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    GET_CTX();
    CHECK_BACKEND();

    if (sgl_state_viewport_set(&ctx->viewport_state, x, y, width, height)) {
        /* Apply via backend */
        if (ctx->backend->ops->apply_viewport) {
            sgl_viewport_state_t vs = {
                x, y, width, height,
                ctx->viewport_state.depth_near,
                ctx->viewport_state.depth_far
            };
            ctx->backend->ops->apply_viewport(ctx->backend, &vs);
        }
    }

    SGL_TRACE_STATE("glViewport(%d, %d, %d, %d)", x, y, width, height);
}

GL_APICALL void GL_APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    GET_CTX();
    CHECK_BACKEND();

    if (sgl_state_scissor_set(&ctx->viewport_state, x, y, width, height)) {
        /* Apply via backend */
        if (ctx->backend->ops->apply_scissor) {
            sgl_scissor_state_t ss = { x, y, width, height, true };
            ctx->backend->ops->apply_scissor(ctx->backend, &ss);
        }
    }

    SGL_TRACE_STATE("glScissor(%d, %d, %d, %d)", x, y, width, height);
}

GL_APICALL void GL_APIENTRY glDepthRangef(GLfloat nearVal, GLfloat farVal) {
    GET_CTX();
    CHECK_BACKEND();

    if (sgl_state_viewport_set_depth_range(&ctx->viewport_state, nearVal, farVal)) {
        /* Apply via backend */
        if (ctx->backend->ops->apply_viewport) {
            sgl_viewport_state_t vs = {
                ctx->viewport_state.viewport_x,
                ctx->viewport_state.viewport_y,
                ctx->viewport_state.viewport_width,
                ctx->viewport_state.viewport_height,
                nearVal, farVal
            };
            ctx->backend->ops->apply_viewport(ctx->backend, &vs);
        }
    }

    SGL_TRACE_STATE("glDepthRangef(%.2f, %.2f)", nearVal, farVal);
}
