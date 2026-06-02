/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * State builders - single source of truth for GL context -> backend state.
 *
 * These translate the context's GLOVE-style state classes into the
 * backend-agnostic sgl_*_state_t structs. They are shared by the draw path,
 * glClear, the glEnable/state-change apply functions, and frame setup, so the
 * field copies live in exactly one place (a forgotten field is the root cause
 * of the polygon-offset bug B1). Behaviour is identical to the inline copies
 * they replaced — same field-by-field assignment.
 */

#ifndef SGL_STATE_BUILD_H
#define SGL_STATE_BUILD_H

#include "sgl_context.h"

static inline void sgl_build_depth_stencil(const sgl_context_t *ctx,
                                           sgl_depth_stencil_state_t *dss) {
    /* Depth */
    dss->depth_test_enabled  = ctx->depth_state.depth_test_enabled;
    dss->depth_write_enabled = ctx->depth_state.depth_write_enabled;
    dss->depth_func          = ctx->depth_state.depth_func;
    dss->depth_clear_value   = ctx->depth_state.clear_depth;
    /* Stencil */
    dss->stencil_test_enabled = ctx->depth_state.stencil_test_enabled;
    dss->stencil_front.func       = ctx->depth_state.front.func;
    dss->stencil_front.ref        = ctx->depth_state.front.ref;
    dss->stencil_front.func_mask  = ctx->depth_state.front.func_mask;
    dss->stencil_front.write_mask = ctx->depth_state.front.write_mask;
    dss->stencil_front.fail_op    = ctx->depth_state.front.fail_op;
    dss->stencil_front.zfail_op   = ctx->depth_state.front.zfail_op;
    dss->stencil_front.zpass_op   = ctx->depth_state.front.zpass_op;
    dss->stencil_back.func        = ctx->depth_state.back.func;
    dss->stencil_back.ref         = ctx->depth_state.back.ref;
    dss->stencil_back.func_mask   = ctx->depth_state.back.func_mask;
    dss->stencil_back.write_mask  = ctx->depth_state.back.write_mask;
    dss->stencil_back.fail_op     = ctx->depth_state.back.fail_op;
    dss->stencil_back.zfail_op    = ctx->depth_state.back.zfail_op;
    dss->stencil_back.zpass_op    = ctx->depth_state.back.zpass_op;
    dss->stencil_clear_value      = ctx->depth_state.clear_stencil;
}

static inline void sgl_build_blend(const sgl_context_t *ctx,
                                   sgl_blend_state_t *bs) {
    bs->enabled        = ctx->blend_state.enabled;
    bs->src_rgb        = ctx->blend_state.src_rgb;
    bs->dst_rgb        = ctx->blend_state.dst_rgb;
    bs->src_alpha      = ctx->blend_state.src_alpha;
    bs->dst_alpha      = ctx->blend_state.dst_alpha;
    bs->equation_rgb   = ctx->blend_state.equation_rgb;
    bs->equation_alpha = ctx->blend_state.equation_alpha;
    bs->color[0]       = ctx->blend_state.color[0];
    bs->color[1]       = ctx->blend_state.color[1];
    bs->color[2]       = ctx->blend_state.color[2];
    bs->color[3]       = ctx->blend_state.color[3];
}

static inline void sgl_build_raster(const sgl_context_t *ctx,
                                    sgl_raster_state_t *rs) {
    rs->cull_enabled                 = ctx->raster_state.cull_enabled;
    rs->cull_mode                    = ctx->raster_state.cull_mode;
    rs->front_face                   = ctx->raster_state.front_face;
    rs->polygon_offset_fill_enabled  = ctx->raster_state.polygon_offset_fill_enabled;
    rs->polygon_offset_factor        = ctx->raster_state.polygon_offset_factor;
    rs->polygon_offset_units         = ctx->raster_state.polygon_offset_units;
}

static inline void sgl_build_color(const sgl_context_t *ctx,
                                   sgl_color_state_t *cs) {
    cs->mask[0]        = ctx->color_state.mask[0];
    cs->mask[1]        = ctx->color_state.mask[1];
    cs->mask[2]        = ctx->color_state.mask[2];
    cs->mask[3]        = ctx->color_state.mask[3];
    cs->clear_color[0] = ctx->color_state.clear_color[0];
    cs->clear_color[1] = ctx->color_state.clear_color[1];
    cs->clear_color[2] = ctx->color_state.clear_color[2];
    cs->clear_color[3] = ctx->color_state.clear_color[3];
}

#endif /* SGL_STATE_BUILD_H */
