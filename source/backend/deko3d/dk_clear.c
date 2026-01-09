/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Clear Operations
 *
 * This module handles framebuffer clearing:
 * - Color buffer clear
 * - Depth buffer clear
 * - Stencil buffer clear
 */

#include "dk_internal.h"

/* ============================================================================
 * Clear Operation
 * ============================================================================ */

void dk_clear(sgl_backend_t *be, GLbitfield mask, const float *color, float depth, int stencil) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Set large scissor BEFORE any clears to ensure full buffer is cleared */
    DkScissor fullScissor = { 0, 0, 4096, 4096 };
    dkCmdBufSetScissors(dk->cmdbuf, 0, &fullScissor, 1);

    if (mask & GL_COLOR_BUFFER_BIT) {
        dkCmdBufClearColorFloat(dk->cmdbuf, 0, DkColorMask_RGBA,
            color[0], color[1], color[2], color[3]);

        /* Add barrier after color clear to ensure it's committed before any RT switch
         * Include L2Cache invalidation for proper cache coherency with subsequent sampling */
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    }

    if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        /* IMPORTANT: Ensure depth writes are enabled before clearing depth buffer.
         * Some hardware requires depth write enable for depth clears to work. */
        DkDepthStencilState dsState;
        memset(&dsState, 0, sizeof(dsState));
        dkDepthStencilStateDefaults(&dsState);
        dsState.depthTestEnable = false;  /* Test off for clear */
        dsState.depthWriteEnable = true;  /* Write MUST be on for clear to work */
        dkCmdBufBindDepthStencilState(dk->cmdbuf, &dsState);

        bool clearDepth = (mask & GL_DEPTH_BUFFER_BIT) != 0;
        uint8_t stencilMask = (mask & GL_STENCIL_BUFFER_BIT) ? 0xFF : 0x00;
        dkCmdBufClearDepthStencil(dk->cmdbuf, clearDepth, depth, stencilMask, (uint8_t)stencil);

        /* Barrier after depth/stencil clear - include Image and L2Cache invalidation */
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

        /* Rebind render target after depth clear if FBO is active */
        if (dk->current_fbo != 0 && dk->current_fbo_color > 0 && dk->current_fbo_depth > 0) {
            if (dk->current_fbo_color < SGL_MAX_TEXTURES && dk->texture_initialized[dk->current_fbo_color] &&
                dk->current_fbo_depth < SGL_MAX_RENDERBUFFERS && dk->renderbuffer_initialized[dk->current_fbo_depth]) {
                DkImageView colorView, depthView;
                dkImageViewDefaults(&colorView, &dk->textures[dk->current_fbo_color]);
                dkImageViewDefaults(&depthView, &dk->renderbuffer_images[dk->current_fbo_depth]);
                dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, &depthView);
            }
        }
    }

    SGL_TRACE_DRAW("clear mask=0x%X", mask);
}
