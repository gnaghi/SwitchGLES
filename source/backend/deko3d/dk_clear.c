/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Clear Operations
 *
 * This module handles framebuffer clearing:
 * - Color buffer clear
 * - Depth buffer clear
 * - Stencil buffer clear
 *
 * Per GL spec, glClear is affected by:
 * - Scissor test (when GL_SCISSOR_TEST enabled)
 * - Color write mask (glColorMask)
 * - Depth write mask (glDepthMask)
 * - Stencil write mask (glStencilMask)
 */

#include "dk_internal.h"
#include "../../context/sgl_context.h"

/* ============================================================================
 * Clear Operation
 * ============================================================================ */

void dk_clear(sgl_backend_t *be, GLbitfield mask, const float *color, float depth, int stencil) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    sgl_context_t *ctx = sgl_get_current_context();

    /* No flush before the clear: unlike a Vulkan render pass loadOp (GLOVE),
     * a deko3d clear is a 3D-engine command recorded in order with the draws
     * of the same command buffer. Accumulation without eglSwapBuffers is
     * bounded by the draw/allocator thresholds and the cmdbuf overflow
     * callback. */

    /* Per GL spec: glClear is affected by the scissor test.
     * If GL_SCISSOR_TEST is enabled, only the scissor region is cleared.
     * If disabled, the entire framebuffer is cleared.
     *
     * DkDeviceFlags_OriginLowerLeft makes deko3d use GL-style coordinates
     * where y=0 is at the bottom. No manual Y-flip needed.
     * GL scissor rect can have negative x/y — must clip to window boundaries. */
    /* Use actual framebuffer dimensions (supports both 720p handheld and 1080p docked,
     * as well as FBO render targets with arbitrary dimensions).
     * Use type flag to pick correct array (avoids renderbuffer/texture ID collision). */
    int fb_w = (int)dk->fb_width;
    int fb_h = (int)dk->fb_height;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0) {
        if (dk->current_fbo_color_is_rb) {
            if (dk->current_fbo_color < SGL_MAX_RENDERBUFFERS &&
                dk->renderbuffer_initialized[dk->current_fbo_color]) {
                fb_w = (int)dk->renderbuffer_width[dk->current_fbo_color];
                fb_h = (int)dk->renderbuffer_height[dk->current_fbo_color];
            }
        } else {
            if (dk->current_fbo_color < SGL_MAX_TEXTURES &&
                dk->texture_initialized[dk->current_fbo_color]) {
                fb_w = (int)dk->texture_width[dk->current_fbo_color];
                fb_h = (int)dk->texture_height[dk->current_fbo_color];
            }
        }
    }

    DkScissor clearScissor;
    bool scissor_valid = true;
    if (ctx && ctx->viewport_state.scissor_enabled) {
        int sx = ctx->viewport_state.scissor_x;
        int sy = ctx->viewport_state.scissor_y;
        int sw = ctx->viewport_state.scissor_width;
        int sh = ctx->viewport_state.scissor_height;

        /* Clip negative coordinates: shrink width/height accordingly */
        if (sx < 0) {
            sw += sx;
            sx = 0;
        }
        if (sy < 0) {
            sh += sy;
            sy = 0;
        }

        /* Clip to framebuffer right/bottom edges */
        if (sx + sw > fb_w)
            sw = fb_w - sx;
        if (sy + sh > fb_h)
            sh = fb_h - sy;

        /* If rect is entirely off-screen, skip the clear */
        if (sw <= 0 || sh <= 0) {
            scissor_valid = false;
        } else {
            clearScissor = (DkScissor){(uint32_t)sx, (uint32_t)sy, (uint32_t)sw, (uint32_t)sh};
        }
    } else {
        /* Whole bound render target: an FBO can be larger than the window
         * (e.g. 1024x1024 shadow maps, 1920x1080 offscreen targets). */
        clearScissor = (DkScissor){0, 0, (uint32_t)fb_w, (uint32_t)fb_h};
    }

    if (!scissor_valid) {
        SGL_TRACE_DRAW("clear mask=0x%X (scissor off-screen, skipped)", mask);
        return;
    }
    dkCmdBufSetScissors(dk->cmdbuf, 0, &clearScissor, 1);
    /* The scissor registers now hold the clear rectangle, not what the
     * draw-time cache recorded: the next draw records its scissor again. */
    dk_state_cache_invalidate_mask(dk, DK_SC_SCISSOR);

    if (mask & GL_COLOR_BUFFER_BIT) {
        /* Per GL spec: glClear is affected by glColorMask */
        uint32_t dkMask = 0;
        if (ctx) {
            if (ctx->color_state.mask[0])
                dkMask |= DkColorMask_R;
            if (ctx->color_state.mask[1])
                dkMask |= DkColorMask_G;
            if (ctx->color_state.mask[2])
                dkMask |= DkColorMask_B;
            if (ctx->color_state.mask[3])
                dkMask |= DkColorMask_A;
        } else {
            dkMask = DkColorMask_RGBA;
        }

        /* Per GLES spec: If the color buffer does not store an alpha component,
         * alpha writes have no effect. Mask out A for RGB/RGB565 FBOs.
         * Without this, clearing an RGB FBO writes alpha=0 (from clear color)
         * into the RGBA8 backing texture, causing readback to return wrong alpha. */
        if (dk->current_fbo != 0 && ctx) {
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
                    dkMask &= ~DkColorMask_A;
                }
            }
        }

        /* No barrier after the clear: dkCmdBufClearColor records a 3D-engine
         * ClearBuffers macro, ordered by the hardware with the draws that
         * follow into the same render target. Every consumer of the cleared
         * image that is not a draw already waits for it: dk_bind_framebuffer
         * (RT switch, Full), dk_bind_texture (first sampling of a texture that
         * was a render target, Full + Image), dk_read_pixels, the blit, copy
         * and mipmap paths (Full + Image before reading), and the swapchain
         * present (deko3d signals the present fence with a cache flush). */
        dkCmdBufClearColorFloat(dk->cmdbuf, 0, dkMask, color[0], color[1], color[2], color[3]);
    }

    if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        /* Skip depth/stencil clear entirely if FBO has no depth/stencil attachment.
         * Calling dkCmdBufClearDepthStencil with pDepthView=NULL corrupts GPU state
         * and causes subsequent color renders to produce wrong results. */
        bool has_depth_stencil = true;
        if (dk->current_fbo != 0 && dk->current_fbo_depth == 0 && dk->current_fbo_stencil == 0) {
            has_depth_stencil = false;
        }

        if (!has_depth_stencil) {
            /* No depth/stencil attachment on this FBO — skip */
        } else {
            /* Per GL spec: glClear respects glDepthMask and glStencilMask.
             * If glDepthMask(GL_FALSE), depth clear is suppressed.
             * If glStencilMask(mask), only those stencil bits are cleared. */
            bool depthMaskEnabled = ctx ? ctx->depth_state.depth_write_enabled : true;
            bool clearDepth = (mask & GL_DEPTH_BUFFER_BIT) && depthMaskEnabled;

            uint8_t stencilWriteMask = ctx ? (uint8_t)ctx->depth_state.front.write_mask : 0xFF;
            uint8_t stencilMask = (mask & GL_STENCIL_BUFFER_BIT) ? stencilWriteMask : 0x00;

            /* For stencil-only FBOs (no GL depth attachment), force-clear the depth
             * channel when clearing stencil. The STENCIL_INDEX8 RBO is stored as
             * Z24S8 (deko3d requirement), but the 24-bit depth portion is undefined.
             * Uninitialized depth values confuse Maxwell's Zcull/HW compression and
             * cause incorrect stencil test results on subsequent draws.
             * This doesn't affect GL semantics — there's no logical depth buffer. */
            if (!clearDepth && stencilMask != 0x00 && dk->current_fbo != 0 &&
                dk->current_fbo_depth == 0 && dk->current_fbo_stencil > 0) {
                clearDepth = true;
            }

            if (!clearDepth && stencilMask == 0x00) {
                /* Nothing to clear — both masks suppress the operation */
            } else {
                /* Enable depth writes in hardware for the clear to take effect.
                 * Also set stencil state: NVIDIA hardware uses the dynamic stencil
                 * write mask (from dkCmdBufSetStencil) for clear operations too.
                 * Without this, stale stencil write mask from a prior draw could
                 * gate the stencil clear and produce incorrect results. */
                DkDepthStencilState dsState;
                memset(&dsState, 0, sizeof(dsState));
                dkDepthStencilStateDefaults(&dsState);
                dsState.depthTestEnable = false;
                dsState.depthWriteEnable = clearDepth;
                dsState.stencilTestEnable = (stencilMask != 0x00);
                dkCmdBufBindDepthStencilState(dk->cmdbuf, &dsState);

                /* Set stencil write mask to match the clear mask.
                 * Per GL spec, stencil clear is affected by glStencilMask. */
                dkCmdBufSetStencil(dk->cmdbuf, DkFace_Front, stencilMask, 0, 0xFF);
                dkCmdBufSetStencil(dk->cmdbuf, DkFace_Back, stencilMask, 0, 0xFF);
                /* The depth-stencil registers now hold the clear's state (and
                 * dkCmdBufClearDepthStencil itself rewrites StencilFrontMask):
                 * the next draw, or the re-apply glClear does after a stencil
                 * clear, records the group again. */
                dk_state_cache_invalidate_mask(dk, DK_SC_DEPTH_STENCIL);

                dkCmdBufClearDepthStencil(dk->cmdbuf, clearDepth, depth, stencilMask,
                                          (uint8_t)stencil);

                /* No pipeline drain and no Zcull invalidation after the clear:
                 * like the color clear above, ClearBuffers is ordered by the 3D
                 * engine with the depth and stencil tests of the draws that
                 * follow into the same target. The clear is what initialises the
                 * hierarchical depth metadata: dkCmdBufClearDepthStencil writes
                 * ZcullClearDepth (IsLessThanHalf / IsOneOrZero hints) right
                 * before ClearBuffers so that Zcull takes the clear value.
                 * Invalidating Zcull here threw that away and left the whole
                 * pass without hierarchical depth rejection. Zcull is already
                 * invalidated where the depth buffer actually changes: by
                 * deko3d in dkCmdBufBindRenderTargets (ConditionalZcullInvalidate
                 * on a new depth target address), by dk_bind_framebuffer at every
                 * FBO switch, by dk_rebind_render_target after uploads and
                 * reallocations, and at frame start / cmdbuf resets. No render
                 * target rebind either: the clear leaves the RT registers
                 * untouched (gpu_3d_base.cpp), the rebind only re-wrote the same
                 * values behind a Full + Zcull barrier. */

                /* Tiled-cache barrier, stencil clears only. It was added in March
                 * 2026 as a hypothesis for the stencil-op failures (Replace, Zero,
                 * Invert, IncrWrap, DecrWrap); docs/conformance_assessment.md and
                 * docs/stencil_analysis.md record that it brought no improvement,
                 * and neither SwitchGLES nor deko3d's engine setup ever enables the
                 * tiled cache (dkCmdBufTiledCacheOp is never called). It is kept
                 * for stencil clears only, so the command stream of every stencil
                 * test stays the one validated by the March 23 full regression,
                 * and dropped for depth-only clears, which passed before it was
                 * introduced (DepthMinusOneToOne, March 11). */
                if (stencilMask != 0x00) {
                    dk_barrier(dk->cmdbuf, DkBarrier_Tiles, 0);
                }
            }
        } /* has_depth_stencil */
    }

    dk->draws_since_flush++; /* Count clears for mid-frame flush threshold */
    dk->diag_draw_count++;
    SGL_TRACE_DRAW("clear mask=0x%X", mask);
}
