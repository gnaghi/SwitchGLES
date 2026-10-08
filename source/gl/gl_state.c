/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Enable/Disable, Blend, Depth, Stencil, Cull, ColorMask
 *
 * The setters below only update the context state. Nothing is recorded into
 * the command buffer here: sgl_prepare_draw hands every fixed-function group
 * to the backend before each draw, and the backend records a group only when
 * its derived values changed (dk_state.c, dk->state_cache). glClear builds
 * the state it needs (color mask, scissor, depth/stencil write masks) from
 * the context itself (dk_clear.c), so it never depended on these setters
 * recording anything.
 */

#include "gl_common.h"

/* Enable/Disable */

GL_APICALL void GL_APIENTRY glEnable(GLenum cap) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    switch (cap) {
        case GL_DEPTH_TEST:
            sgl_state_depth_set_test_enabled(&ctx->depth_state, true);
            break;
        case GL_STENCIL_TEST:
            sgl_state_stencil_set_test_enabled(&ctx->depth_state, true);
            break;
        case GL_BLEND:
            sgl_state_blend_set_enabled(&ctx->blend_state, true);
            break;
        case GL_CULL_FACE:
            sgl_state_raster_set_cull_enabled(&ctx->raster_state, true);
            break;
        case GL_SCISSOR_TEST:
            sgl_state_scissor_set_enabled(&ctx->viewport_state, true);
            break;
        case GL_POLYGON_OFFSET_FILL:
            /* The bias values are recorded with the rasterizer group at the
             * next draw (dk_apply_raster), only while the offset is enabled. */
            ctx->raster_state.polygon_offset_fill_enabled = true;
            break;
        case GL_DITHER:
            ctx->dither_enabled = true;
            break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            ctx->sample_alpha_to_coverage = true;
            break;
        case GL_SAMPLE_COVERAGE:
            ctx->sample_coverage_enabled = true;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    SGL_TRACE_STATE("glEnable(0x%X)", cap);
}

GL_APICALL void GL_APIENTRY glDisable(GLenum cap) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    switch (cap) {
        case GL_DEPTH_TEST:
            sgl_state_depth_set_test_enabled(&ctx->depth_state, false);
            break;
        case GL_STENCIL_TEST:
            sgl_state_stencil_set_test_enabled(&ctx->depth_state, false);
            break;
        case GL_BLEND:
            sgl_state_blend_set_enabled(&ctx->blend_state, false);
            break;
        case GL_CULL_FACE:
            sgl_state_raster_set_cull_enabled(&ctx->raster_state, false);
            break;
        case GL_SCISSOR_TEST:
            sgl_state_scissor_set_enabled(&ctx->viewport_state, false);
            break;
        case GL_POLYGON_OFFSET_FILL:
            ctx->raster_state.polygon_offset_fill_enabled = false;
            break;
        case GL_DITHER:
            ctx->dither_enabled = false;
            break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            ctx->sample_alpha_to_coverage = false;
            break;
        case GL_SAMPLE_COVERAGE:
            ctx->sample_coverage_enabled = false;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    SGL_TRACE_STATE("glDisable(0x%X)", cap);
}

GL_APICALL GLboolean GL_APIENTRY glIsEnabled(GLenum cap) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return GL_FALSE;

    switch (cap) {
        case GL_DEPTH_TEST:
            return ctx->depth_state.depth_test_enabled ? GL_TRUE : GL_FALSE;
        case GL_STENCIL_TEST:
            return ctx->depth_state.stencil_test_enabled ? GL_TRUE : GL_FALSE;
        case GL_BLEND:
            return ctx->blend_state.enabled ? GL_TRUE : GL_FALSE;
        case GL_CULL_FACE:
            return ctx->raster_state.cull_enabled ? GL_TRUE : GL_FALSE;
        case GL_SCISSOR_TEST:
            return ctx->viewport_state.scissor_enabled ? GL_TRUE : GL_FALSE;
        case GL_POLYGON_OFFSET_FILL:
            return ctx->raster_state.polygon_offset_fill_enabled ? GL_TRUE : GL_FALSE;
        case GL_DITHER:
            return ctx->dither_enabled ? GL_TRUE : GL_FALSE;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            return ctx->sample_alpha_to_coverage ? GL_TRUE : GL_FALSE;
        case GL_SAMPLE_COVERAGE:
            return ctx->sample_coverage_enabled ? GL_TRUE : GL_FALSE;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return GL_FALSE;
    }
}

/* ---- Validation Helpers ---- */

static int sgl_valid_compare_func(GLenum func) {
    return func == GL_NEVER || func == GL_LESS || func == GL_EQUAL || func == GL_LEQUAL ||
           func == GL_GREATER || func == GL_NOTEQUAL || func == GL_GEQUAL || func == GL_ALWAYS;
}

static int sgl_valid_stencil_op(GLenum op) {
    return op == GL_KEEP || op == GL_ZERO || op == GL_REPLACE || op == GL_INCR || op == GL_DECR ||
           op == GL_INVERT || op == GL_INCR_WRAP || op == GL_DECR_WRAP;
}

static int sgl_valid_stencil_face(GLenum face) {
    return face == GL_FRONT || face == GL_BACK || face == GL_FRONT_AND_BACK;
}

static int sgl_valid_blend_factor(GLenum f) {
    return f == GL_ZERO || f == GL_ONE || f == GL_SRC_COLOR || f == GL_ONE_MINUS_SRC_COLOR ||
           f == GL_DST_COLOR || f == GL_ONE_MINUS_DST_COLOR || f == GL_SRC_ALPHA ||
           f == GL_ONE_MINUS_SRC_ALPHA || f == GL_DST_ALPHA || f == GL_ONE_MINUS_DST_ALPHA ||
           f == GL_CONSTANT_COLOR || f == GL_ONE_MINUS_CONSTANT_COLOR || f == GL_CONSTANT_ALPHA ||
           f == GL_ONE_MINUS_CONSTANT_ALPHA || f == GL_SRC_ALPHA_SATURATE;
}

static int sgl_valid_blend_equation(GLenum mode) {
    return mode == GL_FUNC_ADD || mode == GL_FUNC_SUBTRACT || mode == GL_FUNC_REVERSE_SUBTRACT ||
           mode == GL_MIN || mode == GL_MAX;
}

/* Depth Functions */

GL_APICALL void GL_APIENTRY glDepthFunc(GLenum func) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_compare_func(func)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_depth_set_func(&ctx->depth_state, func);
    SGL_TRACE_STATE("glDepthFunc(0x%X)", func);
}

GL_APICALL void GL_APIENTRY glDepthMask(GLboolean flag) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    sgl_state_depth_set_write_enabled(&ctx->depth_state, flag != 0);
    SGL_TRACE_STATE("glDepthMask(%d)", flag);
}

/* Blend Functions */

GL_APICALL void GL_APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_blend_factor(sfactor) || !sgl_valid_blend_factor(dfactor)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_blend_set_func(&ctx->blend_state, sfactor, dfactor, sfactor, dfactor);
    SGL_TRACE_STATE("glBlendFunc(0x%X, 0x%X)", sfactor, dfactor);
}

GL_APICALL void GL_APIENTRY glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                                                GLenum dstAlpha) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_blend_factor(srcRGB) || !sgl_valid_blend_factor(dstRGB) ||
        !sgl_valid_blend_factor(srcAlpha) || !sgl_valid_blend_factor(dstAlpha)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_blend_set_func(&ctx->blend_state, srcRGB, dstRGB, srcAlpha, dstAlpha);
    SGL_TRACE_STATE("glBlendFuncSeparate(0x%X, 0x%X, 0x%X, 0x%X)", srcRGB, dstRGB, srcAlpha,
                    dstAlpha);
}

GL_APICALL void GL_APIENTRY glBlendEquation(GLenum mode) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_blend_equation(mode)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_blend_set_equation(&ctx->blend_state, mode, mode);
    SGL_TRACE_STATE("glBlendEquation(0x%X)", mode);
}

GL_APICALL void GL_APIENTRY glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_blend_equation(modeRGB) || !sgl_valid_blend_equation(modeAlpha)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_blend_set_equation(&ctx->blend_state, modeRGB, modeAlpha);
    SGL_TRACE_STATE("glBlendEquationSeparate(0x%X, 0x%X)", modeRGB, modeAlpha);
}

GL_APICALL void GL_APIENTRY glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    ctx->blend_state.color[0] = red;
    ctx->blend_state.color[1] = green;
    ctx->blend_state.color[2] = blue;
    ctx->blend_state.color[3] = alpha;
    SGL_TRACE_STATE("glBlendColor(%.2f, %.2f, %.2f, %.2f)", red, green, blue, alpha);
}

/* Cull Functions */

GL_APICALL void GL_APIENTRY glCullFace(GLenum mode) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_raster_set_cull_mode(&ctx->raster_state, mode);
    SGL_TRACE_STATE("glCullFace(0x%X)", mode);
}

GL_APICALL void GL_APIENTRY glFrontFace(GLenum mode) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (mode != GL_CW && mode != GL_CCW) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_raster_set_front_face(&ctx->raster_state, mode);
    SGL_TRACE_STATE("glFrontFace(0x%X)", mode);
}

/* Color Mask */

GL_APICALL void GL_APIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue,
                                        GLboolean alpha) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    sgl_state_color_set_mask(&ctx->color_state, red != 0, green != 0, blue != 0, alpha != 0);
    SGL_TRACE_STATE("glColorMask(%d, %d, %d, %d)", red, green, blue, alpha);
}

/* Stencil Functions */

GL_APICALL void GL_APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_compare_func(func)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_func(&ctx->depth_state, GL_FRONT_AND_BACK, func, ref, mask);
    SGL_TRACE_STATE("glStencilFunc(0x%X, %d, 0x%X)", func, ref, mask);
}

GL_APICALL void GL_APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                                                  GLuint mask) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_stencil_face(face)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!sgl_valid_compare_func(func)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_func(&ctx->depth_state, face, func, ref, mask);
    SGL_TRACE_STATE("glStencilFuncSeparate(0x%X, 0x%X, %d, 0x%X)", face, func, ref, mask);
}

GL_APICALL void GL_APIENTRY glStencilMask(GLuint mask) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    sgl_state_stencil_set_write_mask(&ctx->depth_state, GL_FRONT_AND_BACK, mask);
    SGL_TRACE_STATE("glStencilMask(0x%X)", mask);
}

GL_APICALL void GL_APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_stencil_face(face)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_write_mask(&ctx->depth_state, face, mask);
    SGL_TRACE_STATE("glStencilMaskSeparate(0x%X, 0x%X)", face, mask);
}

GL_APICALL void GL_APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_stencil_op(fail) || !sgl_valid_stencil_op(zfail) ||
        !sgl_valid_stencil_op(zpass)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_op(&ctx->depth_state, GL_FRONT_AND_BACK, fail, zfail, zpass);
    SGL_TRACE_STATE("glStencilOp(0x%X, 0x%X, 0x%X)", fail, zfail, zpass);
}

GL_APICALL void GL_APIENTRY glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                                                GLenum dppass) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_valid_stencil_face(face)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!sgl_valid_stencil_op(sfail) || !sgl_valid_stencil_op(dpfail) ||
        !sgl_valid_stencil_op(dppass)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_op(&ctx->depth_state, face, sfail, dpfail, dppass);
    SGL_TRACE_STATE("glStencilOpSeparate(0x%X, 0x%X, 0x%X, 0x%X)", face, sfail, dpfail, dppass);
}
