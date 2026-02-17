/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - State Application
 *
 * This module applies GL state changes to the deko3d command buffer:
 * - Viewport
 * - Scissor test
 * - Blend state
 * - Depth test
 * - Stencil test
 * - Rasterizer state (culling, front face)
 * - Color write mask
 * - Depth bias (polygon offset)
 */

#include "dk_internal.h"

/* ============================================================================
 * Viewport State
 * ============================================================================ */

void dk_apply_viewport(sgl_backend_t *be, const sgl_viewport_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkViewport viewport = {
        (float)state->x, (float)state->y,
        (float)state->width, (float)state->height,
        state->near_val, state->far_val
    };
    dkCmdBufSetViewports(dk->cmdbuf, 0, &viewport, 1);

    SGL_TRACE_STATE("apply_viewport %d,%d %dx%d", state->x, state->y, state->width, state->height);
}

/* ============================================================================
 * Scissor State
 * ============================================================================ */

void dk_apply_scissor(sgl_backend_t *be, const sgl_scissor_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkScissor scissor = {
        (uint32_t)(state->x < 0 ? 0 : state->x),
        (uint32_t)(state->y < 0 ? 0 : state->y),
        (uint32_t)(state->width < 0 ? 0 : state->width),
        (uint32_t)(state->height < 0 ? 0 : state->height)
    };
    dkCmdBufSetScissors(dk->cmdbuf, 0, &scissor, 1);

    SGL_TRACE_STATE("apply_scissor %d,%d %dx%d", state->x, state->y, state->width, state->height);
}

/* ============================================================================
 * Blend State
 * ============================================================================ */

void dk_apply_blend(sgl_backend_t *be, const sgl_blend_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkColorState colorState;
    memset(&colorState, 0, sizeof(colorState));
    dkColorStateDefaults(&colorState);

    if (state->enabled) {
        dkColorStateSetBlendEnable(&colorState, 0, true);
    }

    dkCmdBufBindColorState(dk->cmdbuf, &colorState);

    if (state->enabled) {
        DkBlendState blendState;
        dkBlendStateDefaults(&blendState);

        dkBlendStateSetFactors(&blendState,
            dk_convert_blend_factor(state->src_rgb),
            dk_convert_blend_factor(state->dst_rgb),
            dk_convert_blend_factor(state->src_alpha),
            dk_convert_blend_factor(state->dst_alpha));

        dkBlendStateSetOps(&blendState,
            dk_convert_blend_op(state->equation_rgb),
            dk_convert_blend_op(state->equation_alpha));

        dkCmdBufBindBlendStates(dk->cmdbuf, 0, &blendState, 1);

        /* Apply blend constant color */
        dkCmdBufSetBlendConst(dk->cmdbuf, state->color[0], state->color[1],
                              state->color[2], state->color[3]);
    }

    SGL_TRACE_STATE("apply_blend enabled=%d", state->enabled);
}

/* ============================================================================
 * Depth State
 * ============================================================================ */

void dk_apply_depth(sgl_backend_t *be, const sgl_depth_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkDepthStencilState dsState;
    memset(&dsState, 0, sizeof(dsState));
    dkDepthStencilStateDefaults(&dsState);

    dsState.depthTestEnable = state->test_enabled;
    dsState.depthWriteEnable = state->write_enabled;
    dsState.depthCompareOp = dk_convert_compare_op(state->func);

    dkCmdBufBindDepthStencilState(dk->cmdbuf, &dsState);

    SGL_TRACE_STATE("apply_depth test=%d write=%d func=0x%X",
                    state->test_enabled, state->write_enabled, state->func);
}

/* ============================================================================
 * Stencil State
 * ============================================================================ */

void dk_apply_stencil(sgl_backend_t *be, const sgl_stencil_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkDepthStencilState dsState;
    memset(&dsState, 0, sizeof(dsState));
    dkDepthStencilStateDefaults(&dsState);

    dsState.stencilTestEnable = state->enabled;

    /* Front face stencil operations */
    dsState.stencilFrontFailOp = dk_convert_stencil_op(state->front.fail_op);
    dsState.stencilFrontDepthFailOp = dk_convert_stencil_op(state->front.zfail_op);
    dsState.stencilFrontPassOp = dk_convert_stencil_op(state->front.zpass_op);
    dsState.stencilFrontCompareOp = dk_convert_compare_op(state->front.func);

    /* Back face stencil operations */
    dsState.stencilBackFailOp = dk_convert_stencil_op(state->back.fail_op);
    dsState.stencilBackDepthFailOp = dk_convert_stencil_op(state->back.zfail_op);
    dsState.stencilBackPassOp = dk_convert_stencil_op(state->back.zpass_op);
    dsState.stencilBackCompareOp = dk_convert_compare_op(state->back.func);

    dkCmdBufBindDepthStencilState(dk->cmdbuf, &dsState);

    /* Apply stencil reference values */
    dkCmdBufSetStencil(dk->cmdbuf, DkFace_Front,
        (uint8_t)state->front.write_mask,
        (uint8_t)state->front.ref,
        (uint8_t)state->front.func_mask);

    dkCmdBufSetStencil(dk->cmdbuf, DkFace_Back,
        (uint8_t)state->back.write_mask,
        (uint8_t)state->back.ref,
        (uint8_t)state->back.func_mask);

    SGL_TRACE_STATE("apply_stencil enabled=%d", state->enabled);
}

/* ============================================================================
 * Combined Depth-Stencil State
 *
 * IMPORTANT: This function sets BOTH depth and stencil in a single
 * DkDepthStencilState to avoid one overwriting the other. Use this
 * instead of separate apply_depth/apply_stencil calls.
 * ============================================================================ */

void dk_apply_depth_stencil(sgl_backend_t *be, const sgl_depth_stencil_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkDepthStencilState dsState;
    memset(&dsState, 0, sizeof(dsState));
    dkDepthStencilStateDefaults(&dsState);

    /* Depth state */
    dsState.depthTestEnable = state->depth_test_enabled;
    dsState.depthWriteEnable = state->depth_write_enabled;
    dsState.depthCompareOp = dk_convert_compare_op(state->depth_func);

    /* Stencil state */
    dsState.stencilTestEnable = state->stencil_test_enabled;

    /* Front face stencil operations */
    dsState.stencilFrontFailOp = dk_convert_stencil_op(state->stencil_front.fail_op);
    dsState.stencilFrontDepthFailOp = dk_convert_stencil_op(state->stencil_front.zfail_op);
    dsState.stencilFrontPassOp = dk_convert_stencil_op(state->stencil_front.zpass_op);
    dsState.stencilFrontCompareOp = dk_convert_compare_op(state->stencil_front.func);

    /* Back face stencil operations */
    dsState.stencilBackFailOp = dk_convert_stencil_op(state->stencil_back.fail_op);
    dsState.stencilBackDepthFailOp = dk_convert_stencil_op(state->stencil_back.zfail_op);
    dsState.stencilBackPassOp = dk_convert_stencil_op(state->stencil_back.zpass_op);
    dsState.stencilBackCompareOp = dk_convert_compare_op(state->stencil_back.func);

    /* Bind combined depth-stencil state */
    dkCmdBufBindDepthStencilState(dk->cmdbuf, &dsState);

    /* Apply stencil reference values (these are set separately from state) */
    if (state->stencil_test_enabled) {
        dkCmdBufSetStencil(dk->cmdbuf, DkFace_Front,
            (uint8_t)state->stencil_front.write_mask,
            (uint8_t)state->stencil_front.ref,
            (uint8_t)state->stencil_front.func_mask);

        dkCmdBufSetStencil(dk->cmdbuf, DkFace_Back,
            (uint8_t)state->stencil_back.write_mask,
            (uint8_t)state->stencil_back.ref,
            (uint8_t)state->stencil_back.func_mask);
    }

    SGL_TRACE_STATE("apply_depth_stencil depth_test=%d stencil_test=%d",
                    state->depth_test_enabled, state->stencil_test_enabled);
}

/* ============================================================================
 * Rasterizer State
 * ============================================================================ */

void dk_apply_raster(sgl_backend_t *be, const sgl_raster_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkRasterizerState rasterState;
    dkRasterizerStateDefaults(&rasterState);

    if (state->cull_enabled) {
        switch (state->cull_mode) {
            case GL_FRONT:          rasterState.cullMode = DkFace_Front; break;
            case GL_BACK:           rasterState.cullMode = DkFace_Back; break;
            case GL_FRONT_AND_BACK: rasterState.cullMode = DkFace_FrontAndBack; break;
            default:                rasterState.cullMode = DkFace_Back; break;
        }
    } else {
        rasterState.cullMode = DkFace_None;
    }

    /* Note: DkDeviceFlags_OriginLowerLeft only affects image storage, NOT clip space Y.
     * YAxisPointsUp is the DEFAULT in deko3d, same as OpenGL.
     * So no winding order inversion is needed. */
    rasterState.frontFace = (state->front_face == GL_CW) ? DkFrontFace_CW : DkFrontFace_CCW;

    dkCmdBufBindRasterizerState(dk->cmdbuf, &rasterState);

    SGL_TRACE_STATE("apply_raster cull=%d mode=0x%X front=0x%X",
                    state->cull_enabled, state->cull_mode, state->front_face);
}

/* ============================================================================
 * Color Mask State
 * ============================================================================ */

void dk_apply_color_mask(sgl_backend_t *be, const sgl_color_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkColorWriteState cwState;
    dkColorWriteStateDefaults(&cwState);

    uint32_t mask = 0;
    if (state->mask[0]) mask |= DkColorMask_R;
    if (state->mask[1]) mask |= DkColorMask_G;
    if (state->mask[2]) mask |= DkColorMask_B;
    if (state->mask[3]) mask |= DkColorMask_A;

    dkColorWriteStateSetMask(&cwState, 0, mask);
    dkCmdBufBindColorWriteState(dk->cmdbuf, &cwState);

    SGL_TRACE_STATE("apply_color_mask [%d%d%d%d]",
                    state->mask[0], state->mask[1], state->mask[2], state->mask[3]);
}

/* ============================================================================
 * Depth Bias (Polygon Offset)
 * ============================================================================ */

void dk_set_depth_bias(sgl_backend_t *be, GLfloat factor, GLfloat units) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    dkCmdBufSetDepthBias(dk->cmdbuf, factor, 0.0f, units);

    SGL_TRACE_STATE("set_depth_bias factor=%f units=%f", factor, units);
}
