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
 *
 * Every apply first derives the deko3d-side values (FBO clamping of the
 * scissor, depth forced off on FBOs without depth, alpha masked out on RGB
 * targets, ...) and compares them with the entry of its group in
 * dk->state_cache: identical values record nothing. The entry is written
 * after the commands were recorded, so an overflow callback or a
 * dk_submit_and_reset that fires during the recording (both go through
 * dk_cmdbuf_clear, which drops every entry) leaves the group marked as
 * recorded in the fresh cmdbuf, where the commands actually landed. The
 * groups are invalidated wherever their GPU registers are written behind
 * the cache's back (dk_clear: scissor + depth-stencil) or the recorded
 * commands may be lost (cmdbuf clears, render-target binds, frame start,
 * context switch): see dk_state_cache_invalidate() in dk_internal.h.
 */

#include "dk_internal.h"
#include "../../context/sgl_context.h"

/* deko3d 0.5.0 blend.dst workaround — accesses DkCmdBuf internal layout.
 * Offsets 112/120 = m_cmdPos/m_cmdEnd pointers in deko3d 0.5.0.
 * GPU method 0x786 = Maxwell IndependentBlend[0].DstAlphaFactor.
 * If the deko3d struct layout changes, this silently breaks. */
#define DK_CMDBUF_CMDPOS_OFFSET 112 /* DkCmdBuf::m_cmdPos (deko3d 0.5.0) */
#define DK_CMDBUF_CMDEND_OFFSET 120 /* DkCmdBuf::m_cmdEnd (deko3d 0.5.0) */
#define NV_BLEND0_DST_ALPHA_METHOD 0x786
/* Worst-case 32-bit words emitted by the blend recording sequence
 * (BindColorState + BindBlendStates + raw patch + SetBlendConst). Generous. */
#define DK_BLEND_SEQ_RESERVE_WORDS 64

/* True when the group's entry is recorded and equal to `key`. */
#define DK_SC_SAME(dk, bit, field, key)                                                            \
    (((dk)->state_cache.valid & (bit)) && memcmp(&(dk)->state_cache.field, (key), sizeof(*(key))) == 0)

#define DK_SC_STORE(dk, bit, field, key)                                                           \
    do {                                                                                           \
        memcpy(&(dk)->state_cache.field, (key), sizeof(*(key)));                                   \
        (dk)->state_cache.valid |= (bit);                                                          \
    } while (0)

/* ============================================================================
 * Viewport State
 * ============================================================================ */

/* Client array / uniform exhaustion thresholds.
 * Flush (submit + wait idle, which restarts both allocators) when either
 * sub-region of the current frame slot is nearly full. A draw needs at most
 * 4 packed UBOs of SGL_MAX_PACKED_UBO_SIZE plus the legacy uniform blocks.
 * cbAddMem callback handles cmdbuf overflow. */
#define DK_CLIENT_ARRAY_MIN_REMAIN (64 * 1024)
#define DK_UNIFORM_MIN_REMAIN (128 * 1024)

void dk_apply_viewport(sgl_backend_t *be, const sgl_viewport_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Uniform memory is NOT reused between draws of a frame. dkCmdBufPushConstants
     * makes the GPU write the data into the UBO when it reaches the command, but
     * the fragments of the previous draws may still be reading constants then:
     * two draws sharing a UBO address made the earlier one read the later one's
     * constants (GFXBench Egypt: black ceiling / wrong far room after a program
     * switch, fixed by a wait-for-idle between the two draws). Each draw gets
     * fresh space from the frame slot's sub-region, restarted only once the GPU
     * is done with it (slot fence or wait idle). */

    /* Pre-draw overflow check: flush when client_array or uniform space is
     * running low. This runs BEFORE any state is recorded into the cmdbuf, so
     * after flush sgl_prepare_draw will cleanly re-establish all state in the
     * fresh cmdbuf. cbAddMem callback handles cmdbuf overflow (safety net).
     * It is the first thing sgl_prepare_draw does, and it runs for EVERY
     * draw, before the cache lookup below: an unchanged viewport must not
     * skip it. */
    {
        uint32_t client_remaining = dk->client_array_slot_end - dk->client_array_offset;
        uint32_t uniform_remaining = dk->uniform_slot_end - dk->uniform_offset;
        if (client_remaining < DK_CLIENT_ARRAY_MIN_REMAIN ||
            uniform_remaining < DK_UNIFORM_MIN_REMAIN) {
            dk_submit_and_reset(dk);
        }
    }

    /* DkDeviceFlags_OriginLowerLeft makes deko3d use GL-style coordinates
     * where y=0 is at the bottom of the window. No manual Y-flip needed —
     * GL viewport coordinates pass through directly. */
    DkViewport viewport = {(float)state->x,      (float)state->y, (float)state->width,
                           (float)state->height, state->near_val, state->far_val};

    if (DK_SC_SAME(dk, DK_SC_VIEWPORT, viewport, &viewport))
        return;

    dkCmdBufSetViewports(dk->cmdbuf, 0, &viewport, 1);
    DK_SC_STORE(dk, DK_SC_VIEWPORT, viewport, &viewport);

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
    if (sx < 0) {
        sw += sx;
        sx = 0;
    }
    if (sy < 0) {
        sh += sy;
        sy = 0;
    }
    if (sx + sw > (int)rt_width)
        sw = (int)rt_width - sx;
    if (sy + sh > (int)rt_height)
        sh = (int)rt_height - sy;
    if (sw <= 0 || sh <= 0) {
        sw = 0;
        sh = 0;
    }

    /* The key is the clamped rectangle: a render target of another size
     * changes it, so an FBO switch re-records the scissor even when the GL
     * rectangle did not change (the binds invalidate the group as well). */
    DkScissor scissor = {(uint32_t)sx, (uint32_t)sy, (uint32_t)sw, (uint32_t)sh};

    if (DK_SC_SAME(dk, DK_SC_SCISSOR, scissor, &scissor))
        return;

    dkCmdBufSetScissors(dk->cmdbuf, 0, &scissor, 1);
    DK_SC_STORE(dk, DK_SC_SCISSOR, scissor, &scissor);

    SGL_TRACE_STATE("apply_scissor %d,%d %dx%d", state->x, state->y, state->width, state->height);
}

/* ============================================================================
 * Blend State
 * ============================================================================ */

void dk_apply_blend(sgl_backend_t *be, const sgl_blend_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Derived values first: factors, ops and constant color are only
     * recorded when blending is enabled, so they are zero in the key
     * otherwise (a change while disabled records nothing). */
    dk_blend_key_t key;
    memset(&key, 0, sizeof(key));
    key.enabled = state->enabled ? 1u : 0u;
    if (state->enabled) {
        dkBlendStateDefaults(&key.blend);
        dkBlendStateSetFactors(&key.blend, dk_convert_blend_factor(state->src_rgb),
                               dk_convert_blend_factor(state->dst_rgb),
                               dk_convert_blend_factor(state->src_alpha),
                               dk_convert_blend_factor(state->dst_alpha));
        dkBlendStateSetOps(&key.blend, dk_convert_blend_op(state->equation_rgb),
                           dk_convert_blend_op(state->equation_alpha));
        memcpy(key.color, state->color, sizeof(key.color));
    }

    if (DK_SC_SAME(dk, DK_SC_BLEND, blend, &key))
        return;

    /* The blend.dst workaround below patches a raw GPU register directly into
     * the cmdbuf and MUST land in the same cmdbuf as its BindBlendStates. If
     * the cmdbuf is nearly full, flush NOW — before recording any blend
     * command — so the whole sequence stays coherent. Flushing mid-sequence
     * would split BindBlendStates from its patch and reset the uniform/client
     * allocators in the middle of state application. */
    if (state->enabled) {
        uint32_t **pos_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + DK_CMDBUF_CMDPOS_OFFSET);
        uint32_t **end_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + DK_CMDBUF_CMDEND_OFFSET);
        if (*pos_ptr + DK_BLEND_SEQ_RESERVE_WORDS > *end_ptr) {
            dk_submit_and_reset(dk);
        }
    }

    DkColorState colorState;
    memset(&colorState, 0, sizeof(colorState));
    dkColorStateDefaults(&colorState);

    if (state->enabled) {
        dkColorStateSetBlendEnable(&colorState, 0, true);
    }

    dkCmdBufBindColorState(dk->cmdbuf, &colorState);

    if (state->enabled) {
        const DkBlendState *blendState = &key.blend;

        dkCmdBufBindBlendStates(dk->cmdbuf, 0, blendState, 1);

        /* Workaround: deko3d 0.5.0 has a copy-paste bug where
         * dkCmdBufBindBlendStates writes dstColorBlendFactor into BOTH
         * the FuncRgbDst and FuncAlphaDst GPU registers. Fix by writing
         * the correct dstAlphaBlendFactor directly to the GPU register.
         * Fixed in deko3d commit 63744e9 but we link the pre-built lib.
         * Room was already reserved at the top of this function, so no
         * mid-sequence flush is needed here (see DK_BLEND_SEQ_RESERVE_WORDS).
         * The patch is part of the blend sequence: it is recorded with every
         * BindBlendStates, never skipped on its own. */
        {
            uint32_t alpha_dst = (uint32_t)blendState->dstAlphaBlendFactor;
            uint32_t gpu_val = (alpha_dst > 31) ? ((alpha_dst & 0x1f) | 0xc000) : alpha_dst;
            /* NV method header: mode=1(incr), count=1, subchannel=0 */
            uint32_t cmd[2] = {0x20010000 | NV_BLEND0_DST_ALPHA_METHOD, gpu_val};
            uint32_t **pos_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + DK_CMDBUF_CMDPOS_OFFSET);
            uint32_t **end_ptr = (uint32_t **)((uint8_t *)dk->cmdbuf + DK_CMDBUF_CMDEND_OFFSET);
            uint32_t *pos = *pos_ptr;
            if (pos + 2 <= *end_ptr) {
                pos[0] = cmd[0];
                pos[1] = cmd[1];
                *pos_ptr = pos + 2;
            } else {
                /* Should not happen: reserve at function top guarantees room.
                 * Skip the patch rather than flush mid-sequence (which would
                 * split it from its BindBlendStates). */
                SGL_ERROR_BACKEND("apply_blend: no room for dst-alpha patch");
            }
        }

        /* Apply blend constant color */
        dkCmdBufSetBlendConst(dk->cmdbuf, state->color[0], state->color[1], state->color[2],
                              state->color[3]);
    }

    DK_SC_STORE(dk, DK_SC_BLEND, blend, &key);

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

    dk_depth_stencil_key_t key;
    memset(&key, 0, sizeof(key));
    DkDepthStencilState *dsState = &key.ds;
    dkDepthStencilStateDefaults(dsState);

    /* Depth state */
    dsState->depthTestEnable = state->depth_test_enabled;
    dsState->depthWriteEnable = state->depth_write_enabled;
    dsState->depthCompareOp = dk_convert_compare_op(state->depth_func);

    /* Stencil state */
    dsState->stencilTestEnable = state->stencil_test_enabled;

    /* Front face stencil operations */
    dsState->stencilFrontFailOp = dk_convert_stencil_op(state->stencil_front.fail_op);
    dsState->stencilFrontDepthFailOp = dk_convert_stencil_op(state->stencil_front.zfail_op);
    dsState->stencilFrontPassOp = dk_convert_stencil_op(state->stencil_front.zpass_op);
    dsState->stencilFrontCompareOp = dk_convert_compare_op(state->stencil_front.func);

    /* Back face stencil operations */
    dsState->stencilBackFailOp = dk_convert_stencil_op(state->stencil_back.fail_op);
    dsState->stencilBackDepthFailOp = dk_convert_stencil_op(state->stencil_back.zfail_op);
    dsState->stencilBackPassOp = dk_convert_stencil_op(state->stencil_back.zpass_op);
    dsState->stencilBackCompareOp = dk_convert_compare_op(state->stencil_back.func);

    /* GLES2 spec: "If the currently bound framebuffer is framebuffer complete
     * without a depth buffer, the depth test always passes."
     * Stencil-only FBOs use Z24S8 backing (deko3d requires combined format),
     * so force depth off to prevent the Z24 portion from interfering. */
    if (dk->current_fbo != 0 && dk->current_fbo_depth == 0) {
        dsState->depthTestEnable = false;
        dsState->depthWriteEnable = false;
    }

    /* Dynamic stencil state (write mask, ref, func mask), both faces */
    key.front[0] = (uint8_t)state->stencil_front.write_mask;
    key.front[1] = (uint8_t)state->stencil_front.ref;
    key.front[2] = (uint8_t)state->stencil_front.func_mask;
    key.back[0] = (uint8_t)state->stencil_back.write_mask;
    key.back[1] = (uint8_t)state->stencil_back.ref;
    key.back[2] = (uint8_t)state->stencil_back.func_mask;

    /* One key for the whole group: the state and the two SetStencil are
     * always recorded together (never one without the others). */
    if (DK_SC_SAME(dk, DK_SC_DEPTH_STENCIL, depth_stencil, &key))
        return;

    /* Bind combined depth-stencil state */
    dkCmdBufBindDepthStencilState(dk->cmdbuf, dsState);

    /* Always set stencil dynamic state (write mask, ref, func mask).
     * NVIDIA hardware uses the stencil write mask for BOTH draw-time stencil
     * test writes AND clear operations. If we skip this when stencil test is
     * disabled, stale values from a previous draw persist on the GPU, causing
     * incorrect stencil clears and wrong results when stencil is re-enabled. */
    dkCmdBufSetStencil(dk->cmdbuf, DkFace_Front, key.front[0], key.front[1], key.front[2]);

    dkCmdBufSetStencil(dk->cmdbuf, DkFace_Back, key.back[0], key.back[1], key.back[2]);

    DK_SC_STORE(dk, DK_SC_DEPTH_STENCIL, depth_stencil, &key);

    SGL_TRACE_STATE("apply_depth_stencil depth_test=%d stencil_test=%d", state->depth_test_enabled,
                    state->stencil_test_enabled);
}

/* ============================================================================
 * Rasterizer State
 * ============================================================================ */

void dk_apply_raster(sgl_backend_t *be, const sgl_raster_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    dk_raster_key_t key;
    memset(&key, 0, sizeof(key));
    DkRasterizerState *rasterState = &key.raster;
    dkRasterizerStateDefaults(rasterState);

    if (state->cull_enabled) {
        switch (state->cull_mode) {
            case GL_FRONT:
                rasterState->cullMode = DkFace_Front;
                break;
            case GL_BACK:
                rasterState->cullMode = DkFace_Back;
                break;
            case GL_FRONT_AND_BACK:
                rasterState->cullMode = DkFace_FrontAndBack;
                break;
            default:
                rasterState->cullMode = DkFace_Back;
                break;
        }
    } else {
        rasterState->cullMode = DkFace_None;
    }

    /* Note: DkDeviceFlags_OriginLowerLeft only affects image storage, NOT clip space Y.
     * YAxisPointsUp is the DEFAULT in deko3d, same as OpenGL.
     * So no winding order inversion is needed. */
    rasterState->frontFace = (state->front_face == GL_CW) ? DkFrontFace_CW : DkFrontFace_CCW;

    /* Polygon offset (depth bias) for shadow passes and decals.
     * depthBiasEnableMask bit 2 = fill mode (GL_POLYGON_OFFSET_FILL) */
    rasterState->depthBiasEnableMask = state->polygon_offset_fill_enabled ? 4 : 0;

    /* Bias values are only recorded while polygon offset is enabled: zero in
     * the key otherwise, so glPolygonOffset while disabled records nothing. */
    if (state->polygon_offset_fill_enabled) {
        key.bias_units = state->polygon_offset_units;
        key.bias_factor = state->polygon_offset_factor;
    }

    if (DK_SC_SAME(dk, DK_SC_RASTER, raster, &key))
        return;

    dkCmdBufBindRasterizerState(dk->cmdbuf, rasterState);

    /* Set depth bias values via separate command (the only place they are
     * recorded: glPolygonOffset just stores them).
     * dkCmdBufSetDepthBias(constantFactor, clamp, slopeFactor)
     * GL: factor = slope scale, units = constant offset
     * So: constantFactor = units, slopeFactor = factor */
    if (state->polygon_offset_fill_enabled) {
        dkCmdBufSetDepthBias(dk->cmdbuf, state->polygon_offset_units, 0.0f,
                             state->polygon_offset_factor);
    }

    DK_SC_STORE(dk, DK_SC_RASTER, raster, &key);

    SGL_TRACE_STATE("apply_raster cull=%d mode=0x%X front=0x%X polyOffset=%d", state->cull_enabled,
                    state->cull_mode, state->front_face, state->polygon_offset_fill_enabled);
}

/* ============================================================================
 * Color Mask State
 * ============================================================================ */

void dk_apply_color_mask(sgl_backend_t *be, const sgl_color_state_t *state) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    uint32_t mask = 0;
    if (state->mask[0])
        mask |= DkColorMask_R;
    if (state->mask[1])
        mask |= DkColorMask_G;
    if (state->mask[2])
        mask |= DkColorMask_B;
    if (state->mask[3])
        mask |= DkColorMask_A;

    /* Per GLES spec: If the color buffer does not store an alpha component,
     * alpha writes have no effect. Mask out A for RGB/RGB565 FBOs. */
    if (dk->current_fbo != 0) {
        sgl_context_t *ctx = sgl_get_current_context();
        if (ctx) {
            sgl_framebuffer_t *fbo = sgl_res_mgr_get_framebuffer(&ctx->res_mgr, dk->current_fbo);
            if (fbo) {
                GLenum fmt = 0;
                if (fbo->color_is_renderbuffer) {
                    sgl_renderbuffer_t *rb =
                        sgl_res_mgr_get_renderbuffer(&ctx->res_mgr, fbo->color_attachment);
                    if (rb)
                        fmt = rb->internal_format;
                } else {
                    sgl_texture_t *tex =
                        sgl_res_mgr_get_texture(&ctx->res_mgr, fbo->color_attachment);
                    if (tex)
                        fmt = tex->internal_format;
                }
                if (fmt == GL_RGB || fmt == GL_RGB8 || fmt == GL_RGB565) {
                    mask &= ~DkColorMask_A;
                }
            }
        }
    }

    /* The key is the final mask (after the RGB-target rule), so a switch to
     * or from an RGB FBO re-records it. */
    if (DK_SC_SAME(dk, DK_SC_COLOR_MASK, color_mask, &mask))
        return;

    DkColorWriteState cwState;
    dkColorWriteStateDefaults(&cwState);
    dkColorWriteStateSetMask(&cwState, 0, mask);
    dkCmdBufBindColorWriteState(dk->cmdbuf, &cwState);

    DK_SC_STORE(dk, DK_SC_COLOR_MASK, color_mask, &mask);

    SGL_TRACE_STATE("apply_color_mask [%d%d%d%d]", state->mask[0], state->mask[1], state->mask[2],
                    state->mask[3]);
}
