/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Clear, Viewport, Scissor
 */

#include "gl_common.h"
#include <stdio.h>

/* Note: glGetError is in gl_query.c */

GL_APICALL void GL_APIENTRY glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    sgl_state_color_set_clear(&ctx->color_state, red, green, blue, alpha);
    SGL_TRACE_STATE("glClearColor(%.2f, %.2f, %.2f, %.2f)", red, green, blue, alpha);
}

GL_APICALL void GL_APIENTRY glClearDepthf(GLfloat depth) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    sgl_state_depth_set_clear(&ctx->depth_state, depth);
    SGL_TRACE_STATE("glClearDepthf(%.2f)", depth);
}

GL_APICALL void GL_APIENTRY glClearStencil(GLint s) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    sgl_state_stencil_set_clear(&ctx->depth_state, s);
    SGL_TRACE_STATE("glClearStencil(%d)", s);
}

GL_APICALL void GL_APIENTRY glClear(GLbitfield mask) {
    sgl_ensure_frame_ready();
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    /* Validate mask: only COLOR, DEPTH, STENCIL bits are allowed */
    if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Delegate to backend for actual clear operations */
    if (ctx->backend->ops->clear) {
        ctx->backend->ops->clear(ctx->backend, mask, ctx->color_state.clear_color,
                                 ctx->depth_state.clear_depth, ctx->depth_state.clear_stencil);
    }

    /* Re-apply combined depth-stencil state after clear if stencil was cleared.
     * GLOVE does NOT restore DSS after clears — clear values are part of the
     * render pass setup. We only restore for stencil to avoid write mask issues. */
    if ((mask & GL_STENCIL_BUFFER_BIT) && ctx->backend->ops->apply_depth_stencil) {
        sgl_depth_stencil_state_t dss;
        sgl_build_depth_stencil(ctx, &dss);
        ctx->backend->ops->apply_depth_stencil(ctx->backend, &dss);
    }
}

GL_APICALL void GL_APIENTRY glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* State only: recorded at the next draw by sgl_prepare_draw (the backend
     * records the viewport when it differs from the one in the cmdbuf). A
     * clear does not use the viewport, and the frame start sets it itself. */
    sgl_state_viewport_set(&ctx->viewport_state, x, y, width, height);

    SGL_TRACE_STATE("glViewport(%d, %d, %d, %d)", x, y, width, height);
}

GL_APICALL void GL_APIENTRY glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* State only: the draw path records the scissor (or the viewport when the
     * test is disabled) and glClear computes its own rectangle from the
     * context (dk_clear.c). */
    sgl_state_scissor_set(&ctx->viewport_state, x, y, width, height);

    SGL_TRACE_STATE("glScissor(%d, %d, %d, %d)", x, y, width, height);
}

GL_APICALL void GL_APIENTRY glDepthRangef(GLfloat nearVal, GLfloat farVal) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    /* State only: part of the viewport group, recorded at the next draw. */
    sgl_state_viewport_set_depth_range(&ctx->viewport_state, nearVal, farVal);

    SGL_TRACE_STATE("glDepthRangef(%.2f, %.2f)", nearVal, farVal);
}
