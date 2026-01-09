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
#include "../../context/sgl_context.h"

/* ============================================================================
 * Viewport State
 * ============================================================================ */

/* Client array exhaustion threshold.
 * Flush when client_array is nearly full. Uniform exhaustion is no longer
 * possible because uniform_offset is reset per-draw (pushConstants captures
 * data in the cmdbuf immediately). cbAddMem callback handles cmdbuf overflow. */
#define DK_CLIENT_ARRAY_MIN_REMAIN  (64 * 1024)

void dk_apply_viewport(sgl_backend_t *be, const sgl_viewport_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Reset uniform staging offset — pushConstants already captured previous
     * draw's data into the cmdbuf, so the staging area can be freely reused.
     * This eliminates uniform exhaustion entirely regardless of draw count. */
    dk->uniform_offset = 0;

    /* Pre-draw overflow check: flush when client_array is running low.
     * This runs BEFORE any state is recorded into the cmdbuf, so after flush
     * sgl_prepare_draw will cleanly re-establish all state in the fresh cmdbuf.
     * cbAddMem callback handles cmdbuf overflow (safety net). */
    {
        uint32_t client_remaining = dk->client_array_slot_end - dk->client_array_offset;
        if (client_remaining < DK_CLIENT_ARRAY_MIN_REMAIN) {
            dk_submit_and_reset(dk);
        }
    }

    /* DkDeviceFlags_OriginLowerLeft makes deko3d use GL-style coordinates
     * where y=0 is at the bottom of the window. No manual Y-flip needed —
     * GL viewport coordinates pass through directly. */
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

    /* DkDeviceFlags_OriginLowerLeft makes deko3d use GL-style coordinates
     * where y=0 is at the bottom. No manual Y-flip needed. */
    int sx = state->x;
    int sy = state->y;
    int sw = state->width;
    int sh = state->height;

    /* Determine current render target dimensions.
     * When an FBO is bound, clamp to the FBO color attachment size,
     * not the default framebuffer size (which is always 1280x720). */
    uint32_t rt_width = dk->fb_width;
    uint32_t rt_height = dk->fb_height;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0) {
        if (dk->current_fbo_color_is_rb) {
            if (dk->current_fbo_color < SGL_MAX_RENDERBUFFERS &&
                dk->renderbuffer_initialized[dk->current_fbo_color]) {
                rt_width = dk->renderbuffer_width[dk->current_fbo_color];
                rt_height = dk->renderbuffer_height[dk->current_fbo_color];
            }
        } else {
            if (dk->current_fbo_color < SGL_MAX_TEXTURES &&
                dk->texture_initialized[dk->current_fbo_color]) {
                rt_width = dk->texture_width[dk->current_fbo_color];
                rt_height = dk->texture_height[dk->current_fbo_color];
            }
        }
    }

    /* Clip negative coordinates */
    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw > (int)rt_width) sw = (int)rt_width - sx;
    if (sy + sh > (int)rt_height) sh = (int)rt_height - sy;
    if (sw <= 0 || sh <= 0) { sw = 0; sh = 0; }

    DkScissor scissor = { (uint32_t)sx, (uint32_t)sy, (uint32_t)sw, (uint32_t)sh };
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

        /* Workaround: deko3d 0.5.0 has a copy-paste bug where
         * dkCmdBufBindBlendStates writes dstColorBlendFactor into BOTH
         * the FuncRgbDst and FuncAlphaDst GPU registers. Fix by writing
         * the correct dstAlphaBlendFactor directly to the GPU register.
         * GPU method 0x786 = IndependentBlend[0].DstAlphaFactor.
         * Fixed in deko3d commit 63744e9 but we link the pre-built lib. */
        {
            uint32_t alpha_dst = (uint32_t)blendState.dstAlphaBlendFactor;
            uint32_t gpu_val = (alpha_dst > 31) ? ((alpha_dst & 0x1f) | 0xc000) : alpha_dst;
            /* NV method header: mode=1(incr), count=1, subchannel=0, method=0x786 */
            uint32_t cmd[2] = { 0x20010786, gpu_val };
            /* Write directly to cmdbuf internal position (offset 112 = m_cmdPos) */
            uint32_t **pos_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + 112);
            uint32_t **end_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + 120);
            uint32_t *pos = *pos_ptr;
            if (pos + 2 > *end_ptr) {
                /* Cmdbuf nearly full — flush to make room. Without this,
                 * the blend workaround silently fails, leaving the GPU with
                 * a wrong dstAlpha factor → potential GPU hang under state churn. */
                dk_submit_and_reset(dk);
                pos_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + 112);
                end_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + 120);
                pos = *pos_ptr;
            }
            if (pos + 2 <= *end_ptr) {
                pos[0] = cmd[0];
                pos[1] = cmd[1];
                *pos_ptr = pos + 2;
            }
        }

        /* Apply blend constant color */
        dkCmdBufSetBlendConst(dk->cmdbuf, state->color[0], state->color[1],
                              state->color[2], state->color[3]);
    }

    SGL_TRACE_STATE("apply_blend enabled=%d", state->enabled);
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

    /* GLES2 spec: "If the currently bound framebuffer is framebuffer complete
     * without a depth buffer, the depth test always passes."
     * Stencil-only FBOs use Z24S8 backing (deko3d requires combined format),
     * so force depth off to prevent the Z24 portion from interfering. */
    if (dk->current_fbo != 0 && dk->current_fbo_depth == 0) {
        dsState.depthTestEnable = false;
        dsState.depthWriteEnable = false;
    }

    /* Bind combined depth-stencil state */
    dkCmdBufBindDepthStencilState(dk->cmdbuf, &dsState);

    /* Always set stencil dynamic state (write mask, ref, func mask).
     * NVIDIA hardware uses the stencil write mask for BOTH draw-time stencil
     * test writes AND clear operations. If we skip this when stencil test is
     * disabled, stale values from a previous draw persist on the GPU, causing
     * incorrect stencil clears and wrong results when stencil is re-enabled. */
    dkCmdBufSetStencil(dk->cmdbuf, DkFace_Front,
        (uint8_t)state->stencil_front.write_mask,
        (uint8_t)state->stencil_front.ref,
        (uint8_t)state->stencil_front.func_mask);

    dkCmdBufSetStencil(dk->cmdbuf, DkFace_Back,
        (uint8_t)state->stencil_back.write_mask,
        (uint8_t)state->stencil_back.ref,
        (uint8_t)state->stencil_back.func_mask);

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

    /* Polygon offset (depth bias) for shadow passes and decals.
     * depthBiasEnableMask bit 2 = fill mode (GL_POLYGON_OFFSET_FILL) */
    rasterState.depthBiasEnableMask = state->polygon_offset_fill_enabled ? 4 : 0;

    dkCmdBufBindRasterizerState(dk->cmdbuf, &rasterState);

    /* Set depth bias values via separate command */
    if (state->polygon_offset_fill_enabled) {
        dkCmdBufSetDepthBias(dk->cmdbuf, state->polygon_offset_units, 0.0f, state->polygon_offset_factor);
    }

    SGL_TRACE_STATE("apply_raster cull=%d mode=0x%X front=0x%X polyOffset=%d",
                    state->cull_enabled, state->cull_mode, state->front_face,
                    state->polygon_offset_fill_enabled);
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

    /* Per GLES spec: If the color buffer does not store an alpha component,
     * alpha writes have no effect. Mask out A for RGB/RGB565 FBOs. */
    if (dk->current_fbo != 0) {
        sgl_context_t *ctx = sgl_get_current_context();
        if (ctx) {
            sgl_framebuffer_t *fbo = sgl_res_mgr_get_framebuffer(&ctx->res_mgr, dk->current_fbo);
            if (fbo) {
                GLenum fmt = 0;
                if (fbo->color_is_renderbuffer) {
                    sgl_renderbuffer_t *rb = sgl_res_mgr_get_renderbuffer(&ctx->res_mgr, fbo->color_attachment);
                    if (rb) fmt = rb->internal_format;
                } else {
                    sgl_texture_t *tex = sgl_res_mgr_get_texture(&ctx->res_mgr, fbo->color_attachment);
                    if (tex) fmt = tex->internal_format;
                }
                if (fmt == GL_RGB || fmt == GL_RGB8 || fmt == GL_RGB565) {
                    mask &= ~DkColorMask_A;
                }
            }
        }
    }

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

    /* dkCmdBufSetDepthBias(constantFactor, clamp, slopeFactor)
     * GL: factor = slope scale, units = constant offset
     * So: constantFactor = units, slopeFactor = factor */
    dkCmdBufSetDepthBias(dk->cmdbuf, units, 0.0f, factor);

    SGL_TRACE_STATE("set_depth_bias factor=%f units=%f", factor, units);
}
