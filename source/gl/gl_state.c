/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Enable/Disable, Blend, Depth, Stencil, Cull, ColorMask
 */

#include "gl_common.h"

/* Apply blend state to backend */
static void apply_blend(sgl_context_t *ctx) {
    if (ctx->backend && ctx->backend->ops->apply_blend) {
        sgl_blend_state_t bs;
        bs.enabled = ctx->blend_state.enabled;
        bs.src_rgb = ctx->blend_state.src_rgb;
        bs.dst_rgb = ctx->blend_state.dst_rgb;
        bs.src_alpha = ctx->blend_state.src_alpha;
        bs.dst_alpha = ctx->blend_state.dst_alpha;
        bs.equation_rgb = ctx->blend_state.equation_rgb;
        bs.equation_alpha = ctx->blend_state.equation_alpha;
        bs.color[0] = ctx->blend_state.color[0];
        bs.color[1] = ctx->blend_state.color[1];
        bs.color[2] = ctx->blend_state.color[2];
        bs.color[3] = ctx->blend_state.color[3];
        ctx->backend->ops->apply_blend(ctx->backend, &bs);
    }
}

/* Apply combined depth-stencil state to backend (preferred - avoids overwrite issues) */
static void apply_depth_stencil(sgl_context_t *ctx) {
    if (ctx->backend && ctx->backend->ops->apply_depth_stencil) {
        sgl_depth_stencil_state_t dss;
        /* Depth state */
        dss.depth_test_enabled = ctx->depth_state.depth_test_enabled;
        dss.depth_write_enabled = ctx->depth_state.depth_write_enabled;
        dss.depth_func = ctx->depth_state.depth_func;
        dss.depth_clear_value = ctx->depth_state.clear_depth;
        /* Stencil state */
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

/* Legacy separate apply functions - now just call the combined version */
static void apply_depth(sgl_context_t *ctx) {
    apply_depth_stencil(ctx);
}

static void apply_stencil(sgl_context_t *ctx) {
    apply_depth_stencil(ctx);
}

/* Apply raster state to backend */
static void apply_raster(sgl_context_t *ctx) {
    if (ctx->backend && ctx->backend->ops->apply_raster) {
        sgl_raster_state_t rs;
        rs.cull_enabled = ctx->raster_state.cull_enabled;
        rs.cull_mode = ctx->raster_state.cull_mode;
        rs.front_face = ctx->raster_state.front_face;
        ctx->backend->ops->apply_raster(ctx->backend, &rs);
    }
}

/* Apply color mask to backend */
static void apply_color_mask(sgl_context_t *ctx) {
    if (ctx->backend && ctx->backend->ops->apply_color_mask) {
        sgl_color_state_t cs;
        cs.mask[0] = ctx->color_state.mask[0];
        cs.mask[1] = ctx->color_state.mask[1];
        cs.mask[2] = ctx->color_state.mask[2];
        cs.mask[3] = ctx->color_state.mask[3];
        cs.clear_color[0] = ctx->color_state.clear_color[0];
        cs.clear_color[1] = ctx->color_state.clear_color[1];
        cs.clear_color[2] = ctx->color_state.clear_color[2];
        cs.clear_color[3] = ctx->color_state.clear_color[3];
        ctx->backend->ops->apply_color_mask(ctx->backend, &cs);
    }
}

/* Enable/Disable */

GL_APICALL void GL_APIENTRY glEnable(GLenum cap) {
    GET_CTX();

    switch (cap) {
        case GL_DEPTH_TEST:
            if (sgl_state_depth_set_test_enabled(&ctx->depth_state, true)) {
                apply_depth(ctx);
            }
            break;
        case GL_STENCIL_TEST:
            if (sgl_state_stencil_set_test_enabled(&ctx->depth_state, true)) {
                apply_stencil(ctx);
            }
            break;
        case GL_BLEND:
            if (sgl_state_blend_set_enabled(&ctx->blend_state, true)) {
                apply_blend(ctx);
            }
            break;
        case GL_CULL_FACE:
            if (sgl_state_raster_set_cull_enabled(&ctx->raster_state, true)) {
                apply_raster(ctx);
            }
            break;
        case GL_SCISSOR_TEST:
            sgl_state_scissor_set_enabled(&ctx->viewport_state, true);
            break;
        case GL_POLYGON_OFFSET_FILL:
            ctx->raster_state.polygon_offset_fill_enabled = true;
            /* Apply current offset values */
            if (ctx->backend && ctx->backend->ops->set_depth_bias) {
                ctx->backend->ops->set_depth_bias(ctx->backend,
                    ctx->raster_state.polygon_offset_factor,
                    ctx->raster_state.polygon_offset_units);
            }
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
    GET_CTX();

    switch (cap) {
        case GL_DEPTH_TEST:
            if (sgl_state_depth_set_test_enabled(&ctx->depth_state, false)) {
                apply_depth(ctx);
            }
            break;
        case GL_STENCIL_TEST:
            if (sgl_state_stencil_set_test_enabled(&ctx->depth_state, false)) {
                apply_stencil(ctx);
            }
            break;
        case GL_BLEND:
            if (sgl_state_blend_set_enabled(&ctx->blend_state, false)) {
                apply_blend(ctx);
            }
            break;
        case GL_CULL_FACE:
            if (sgl_state_raster_set_cull_enabled(&ctx->raster_state, false)) {
                apply_raster(ctx);
            }
            break;
        case GL_SCISSOR_TEST:
            sgl_state_scissor_set_enabled(&ctx->viewport_state, false);
            break;
        case GL_POLYGON_OFFSET_FILL:
            ctx->raster_state.polygon_offset_fill_enabled = false;
            /* Disable depth bias */
            if (ctx->backend && ctx->backend->ops->set_depth_bias) {
                ctx->backend->ops->set_depth_bias(ctx->backend, 0.0f, 0.0f);
            }
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
    GET_CTX_RET(GL_FALSE);

    switch (cap) {
        case GL_DEPTH_TEST:              return ctx->depth_state.depth_test_enabled ? GL_TRUE : GL_FALSE;
        case GL_STENCIL_TEST:            return ctx->depth_state.stencil_test_enabled ? GL_TRUE : GL_FALSE;
        case GL_BLEND:                   return ctx->blend_state.enabled ? GL_TRUE : GL_FALSE;
        case GL_CULL_FACE:               return ctx->raster_state.cull_enabled ? GL_TRUE : GL_FALSE;
        case GL_SCISSOR_TEST:            return ctx->viewport_state.scissor_enabled ? GL_TRUE : GL_FALSE;
        case GL_POLYGON_OFFSET_FILL:     return ctx->raster_state.polygon_offset_fill_enabled ? GL_TRUE : GL_FALSE;
        case GL_DITHER:                  return ctx->dither_enabled ? GL_TRUE : GL_FALSE;
        case GL_SAMPLE_ALPHA_TO_COVERAGE: return ctx->sample_alpha_to_coverage ? GL_TRUE : GL_FALSE;
        case GL_SAMPLE_COVERAGE:         return ctx->sample_coverage_enabled ? GL_TRUE : GL_FALSE;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return GL_FALSE;
    }
}

/* ---- Validation Helpers ---- */

static int sgl_valid_compare_func(GLenum func) {
    return func == GL_NEVER || func == GL_LESS || func == GL_EQUAL ||
           func == GL_LEQUAL || func == GL_GREATER || func == GL_NOTEQUAL ||
           func == GL_GEQUAL || func == GL_ALWAYS;
}

static int sgl_valid_stencil_op(GLenum op) {
    return op == GL_KEEP || op == GL_ZERO || op == GL_REPLACE ||
           op == GL_INCR || op == GL_DECR || op == GL_INVERT ||
           op == GL_INCR_WRAP || op == GL_DECR_WRAP;
}

static int sgl_valid_stencil_face(GLenum face) {
    return face == GL_FRONT || face == GL_BACK || face == GL_FRONT_AND_BACK;
}

static int sgl_valid_blend_factor(GLenum f) {
    return f == GL_ZERO || f == GL_ONE ||
           f == GL_SRC_COLOR || f == GL_ONE_MINUS_SRC_COLOR ||
           f == GL_DST_COLOR || f == GL_ONE_MINUS_DST_COLOR ||
           f == GL_SRC_ALPHA || f == GL_ONE_MINUS_SRC_ALPHA ||
           f == GL_DST_ALPHA || f == GL_ONE_MINUS_DST_ALPHA ||
           f == GL_CONSTANT_COLOR || f == GL_ONE_MINUS_CONSTANT_COLOR ||
           f == GL_CONSTANT_ALPHA || f == GL_ONE_MINUS_CONSTANT_ALPHA ||
           f == GL_SRC_ALPHA_SATURATE;
}

static int sgl_valid_blend_equation(GLenum mode) {
    return mode == GL_FUNC_ADD || mode == GL_FUNC_SUBTRACT ||
           mode == GL_FUNC_REVERSE_SUBTRACT ||
           mode == GL_MIN || mode == GL_MAX;
}

/* Depth Functions */

GL_APICALL void GL_APIENTRY glDepthFunc(GLenum func) {
    GET_CTX();
    if (!sgl_valid_compare_func(func)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_state_depth_set_func(&ctx->depth_state, func)) {
        apply_depth(ctx);
    }
    SGL_TRACE_STATE("glDepthFunc(0x%X)", func);
}

GL_APICALL void GL_APIENTRY glDepthMask(GLboolean flag) {
    GET_CTX();
    if (sgl_state_depth_set_write_enabled(&ctx->depth_state, flag != 0)) {
        apply_depth(ctx);
    }
    SGL_TRACE_STATE("glDepthMask(%d)", flag);
}

/* Blend Functions */

GL_APICALL void GL_APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor) {
    GET_CTX();
    if (!sgl_valid_blend_factor(sfactor) || !sgl_valid_blend_factor(dfactor)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_state_blend_set_func(&ctx->blend_state, sfactor, dfactor, sfactor, dfactor)) {
        apply_blend(ctx);
    }
    SGL_TRACE_STATE("glBlendFunc(0x%X, 0x%X)", sfactor, dfactor);
}

GL_APICALL void GL_APIENTRY glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    GET_CTX();
    if (!sgl_valid_blend_factor(srcRGB) || !sgl_valid_blend_factor(dstRGB) ||
        !sgl_valid_blend_factor(srcAlpha) || !sgl_valid_blend_factor(dstAlpha)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_state_blend_set_func(&ctx->blend_state, srcRGB, dstRGB, srcAlpha, dstAlpha)) {
        apply_blend(ctx);
    }
    SGL_TRACE_STATE("glBlendFuncSeparate(0x%X, 0x%X, 0x%X, 0x%X)", srcRGB, dstRGB, srcAlpha, dstAlpha);
}

GL_APICALL void GL_APIENTRY glBlendEquation(GLenum mode) {
    GET_CTX();
    if (!sgl_valid_blend_equation(mode)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_state_blend_set_equation(&ctx->blend_state, mode, mode)) {
        apply_blend(ctx);
    }
    SGL_TRACE_STATE("glBlendEquation(0x%X)", mode);
}

GL_APICALL void GL_APIENTRY glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    GET_CTX();
    if (!sgl_valid_blend_equation(modeRGB) || !sgl_valid_blend_equation(modeAlpha)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_state_blend_set_equation(&ctx->blend_state, modeRGB, modeAlpha)) {
        apply_blend(ctx);
    }
    SGL_TRACE_STATE("glBlendEquationSeparate(0x%X, 0x%X)", modeRGB, modeAlpha);
}

GL_APICALL void GL_APIENTRY glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    GET_CTX();
    ctx->blend_state.color[0] = red;
    ctx->blend_state.color[1] = green;
    ctx->blend_state.color[2] = blue;
    ctx->blend_state.color[3] = alpha;
    apply_blend(ctx);
    SGL_TRACE_STATE("glBlendColor(%.2f, %.2f, %.2f, %.2f)", red, green, blue, alpha);
}

/* Cull Functions */

GL_APICALL void GL_APIENTRY glCullFace(GLenum mode) {
    GET_CTX();
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_state_raster_set_cull_mode(&ctx->raster_state, mode)) {
        apply_raster(ctx);
    }
    SGL_TRACE_STATE("glCullFace(0x%X)", mode);
}

GL_APICALL void GL_APIENTRY glFrontFace(GLenum mode) {
    GET_CTX();
    if (mode != GL_CW && mode != GL_CCW) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_state_raster_set_front_face(&ctx->raster_state, mode)) {
        apply_raster(ctx);
    }
    SGL_TRACE_STATE("glFrontFace(0x%X)", mode);
}

/* Color Mask */

GL_APICALL void GL_APIENTRY glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    GET_CTX();
    if (sgl_state_color_set_mask(&ctx->color_state, red != 0, green != 0,
                                  blue != 0, alpha != 0)) {
        apply_color_mask(ctx);
    }
    SGL_TRACE_STATE("glColorMask(%d, %d, %d, %d)", red, green, blue, alpha);
}

/* Stencil Functions */

GL_APICALL void GL_APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    GET_CTX();
    if (!sgl_valid_compare_func(func)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_func(&ctx->depth_state, GL_FRONT_AND_BACK, func, ref, mask);
    apply_stencil(ctx);
    SGL_TRACE_STATE("glStencilFunc(0x%X, %d, 0x%X)", func, ref, mask);
}

GL_APICALL void GL_APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    GET_CTX();
    if (!sgl_valid_stencil_face(face)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!sgl_valid_compare_func(func)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_func(&ctx->depth_state, face, func, ref, mask);
    apply_stencil(ctx);
    SGL_TRACE_STATE("glStencilFuncSeparate(0x%X, 0x%X, %d, 0x%X)", face, func, ref, mask);
}

GL_APICALL void GL_APIENTRY glStencilMask(GLuint mask) {
    GET_CTX();
    sgl_state_stencil_set_write_mask(&ctx->depth_state, GL_FRONT_AND_BACK, mask);
    apply_stencil(ctx);
    SGL_TRACE_STATE("glStencilMask(0x%X)", mask);
}

GL_APICALL void GL_APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask) {
    GET_CTX();
    if (!sgl_valid_stencil_face(face)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_write_mask(&ctx->depth_state, face, mask);
    apply_stencil(ctx);
    SGL_TRACE_STATE("glStencilMaskSeparate(0x%X, 0x%X)", face, mask);
}

GL_APICALL void GL_APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    GET_CTX();
    if (!sgl_valid_stencil_op(fail) || !sgl_valid_stencil_op(zfail) || !sgl_valid_stencil_op(zpass)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_op(&ctx->depth_state, GL_FRONT_AND_BACK, fail, zfail, zpass);
    apply_stencil(ctx);
    SGL_TRACE_STATE("glStencilOp(0x%X, 0x%X, 0x%X)", fail, zfail, zpass);
}

GL_APICALL void GL_APIENTRY glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    GET_CTX();
    if (!sgl_valid_stencil_face(face)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!sgl_valid_stencil_op(sfail) || !sgl_valid_stencil_op(dpfail) || !sgl_valid_stencil_op(dppass)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_state_stencil_set_op(&ctx->depth_state, face, sfail, dpfail, dppass);
    apply_stencil(ctx);
    SGL_TRACE_STATE("glStencilOpSeparate(0x%X, 0x%X, 0x%X, 0x%X)", face, sfail, dpfail, dppass);
}
