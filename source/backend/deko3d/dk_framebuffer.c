/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Framebuffer Operations
 *
 * This module handles:
 * - Framebuffer binding (switching render targets)
 * - Reading pixels from framebuffer (glReadPixels)
 *
 * FBO (Framebuffer Object) workflow:
 * 1. Create FBO texture with glGenTextures + glTexImage2D
 * 2. Attach texture to FBO with glFramebufferTexture2D
 * 3. Bind FBO with glBindFramebuffer (switches render target)
 * 4. Render to FBO
 * 5. Bind default framebuffer with glBindFramebuffer(GL_FRAMEBUFFER, 0)
 * 6. Use FBO texture as source for sampling
 *
 * Important: Barriers are inserted when switching render targets to ensure
 * proper synchronization between rendering and texture sampling.
 */

#include "dk_internal.h"
#include "../../context/sgl_context.h"

/* ============================================================================
 * Framebuffer Binding
 *
 * Switches the current render target:
 * - handle=0: Bind default framebuffer (swapchain image)
 * - handle>0: Bind FBO with specified color attachment texture
 * ============================================================================ */

void dk_bind_framebuffer(sgl_backend_t *be, sgl_handle_t handle,
                         sgl_handle_t color_tex, sgl_handle_t depth_rb,
                         bool color_is_rb, bool depth_is_rb,
                         sgl_handle_t stencil_rb, bool stencil_is_rb) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Track current FBO state (including type flags for correct lookup) */
    dk->current_fbo = handle;
    dk->current_fbo_color = color_tex;
    dk->current_fbo_depth = depth_rb;
    dk->current_fbo_stencil = stencil_rb;
    dk->current_fbo_color_is_rb = color_is_rb;
    dk->current_fbo_depth_is_rb = depth_is_rb;
    dk->current_fbo_stencil_is_rb = stencil_is_rb;

    /* Mark texture as used as render target (for barrier optimization) */
    if (color_tex > 0 && color_tex < SGL_MAX_TEXTURES && !color_is_rb) {
        dk->texture_used_as_rt[color_tex] = true;
    }

    /* Insert barrier before switching render targets.
     * This ensures any previous rendering is complete before we switch.
     * Include L2Cache invalidation for proper cache coherency when switching
     * between render target and texture sampling.
     * Zcull: NVIDIA's fast depth metadata is render-target-specific.
     * When (re)binding a render target, stale Zcull data from prior binds
     * causes GPU errors in subsequent depth clears/tests (observed when
     * glu::resetState repeatedly rebinds FBO 0 between dEQP tests). */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors |
                    DkInvalidateFlags_L2Cache | DkInvalidateFlags_Zcull);

    if (handle == 0) {
        /* Bind default framebuffer (swapchain image) - use per-slot depth buffer */
        if (dk->framebuffers) {
            DkImageView colorView, depthView;
            dkImageViewDefaults(&colorView, &dk->framebuffers[dk->current_slot]);
            if (dk->depth_images[dk->current_slot]) {
                dkImageViewDefaults(&depthView, dk->depth_images[dk->current_slot]);
                dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, &depthView);
            } else {
                dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, NULL);
            }
        }
    } else if (color_tex > 0) {
        /* Bind FBO with color attachment — use type flags to pick correct array.
         * Renderbuffer and texture IDs come from separate GL namespaces but share
         * the same numeric range (both start at 1). Without the type flag, the
         * backend would guess wrong when IDs collide (e.g. renderbuffer 1 and
         * texture 1 both exist), binding a depth texture as color or vice versa. */
        DkImageView colorView;
        bool color_found = false;
        if (color_is_rb) {
            if (color_tex < SGL_MAX_RENDERBUFFERS && dk->renderbuffer_initialized[color_tex]) {
                dkImageViewDefaults(&colorView, &dk->renderbuffer_images[color_tex]);
                color_found = true;
            }
        } else {
            if (color_tex < SGL_MAX_TEXTURES && dk->texture_initialized[color_tex]) {
                dkImageViewDefaults(&colorView, &dk->textures[color_tex]);
                color_found = true;
            }
        }
        if (!color_found) {
            SGL_TRACE_FBO("bind_framebuffer: color_tex=%u (is_rb=%d) not found", color_tex, color_is_rb);
            return;
        }

        /* Check for depth/stencil attachment.
         * dkCmdBufBindRenderTarget takes a single depthStencil view.
         * Priority: depth attachment first, then stencil-only if no depth. */
        DkImageView *pDepthView = NULL;
        DkImageView depthView;
        if (depth_rb > 0) {
            if (depth_is_rb) {
                if (depth_rb < SGL_MAX_RENDERBUFFERS &&
                    dk->renderbuffer_initialized[depth_rb]) {
                    dkImageViewDefaults(&depthView, &dk->renderbuffer_images[depth_rb]);
                    pDepthView = &depthView;
                }
            } else {
                if (depth_rb < SGL_MAX_TEXTURES &&
                    dk->texture_initialized[depth_rb]) {
                    dkImageViewDefaults(&depthView, &dk->textures[depth_rb]);
                    pDepthView = &depthView;
                }
            }
        }
        /* If no depth, check for stencil-only (GL_STENCIL_INDEX8 → DkImageFormat_S8) */
        if (!pDepthView && stencil_rb > 0) {
            if (stencil_is_rb) {
                if (stencil_rb < SGL_MAX_RENDERBUFFERS &&
                    dk->renderbuffer_initialized[stencil_rb]) {
                    dkImageViewDefaults(&depthView, &dk->renderbuffer_images[stencil_rb]);
                    pDepthView = &depthView;
                }
            }
        }

        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, pDepthView);
    }

    SGL_TRACE_FBO("bind_framebuffer handle=%u color=%u(rb=%d) depth=%u(rb=%d) stencil=%u(rb=%d)",
                  handle, color_tex, color_is_rb, depth_rb, depth_is_rb, stencil_rb, stencil_is_rb);
}

/* ============================================================================
 * Blit Framebuffer (glBlitFramebuffer)
 *
 * Copies a rectangle of pixels from the read framebuffer to the draw
 * framebuffer using the GPU 2D blit engine (dkCmdBufBlitImage).
 *
 * Supports:
 * - FBO → FBO
 * - FBO → default framebuffer (swapchain)
 * - Default framebuffer → FBO
 * - Default framebuffer → default framebuffer
 * - Scaling (source and dest can be different sizes)
 * - GL_NEAREST and GL_LINEAR filtering
 * ============================================================================ */

void dk_blit_framebuffer(sgl_backend_t *be,
                          sgl_handle_t read_fbo, sgl_handle_t read_color_tex,
                          sgl_handle_t write_fbo, sgl_handle_t write_color_tex,
                          GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                          GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                          GLbitfield mask, GLenum filter) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Only color blit is supported (depth/stencil blit not implemented) */
    if (!(mask & GL_COLOR_BUFFER_BIT)) {
        return;
    }

    /* Resolve source image */
    DkImage *srcImage = NULL;
    if (read_fbo == 0) {
        /* Default framebuffer (swapchain) */
        if (dk->framebuffers) {
            srcImage = &dk->framebuffers[dk->current_slot];
        }
    } else if (read_color_tex > 0 && read_color_tex < SGL_MAX_TEXTURES &&
               dk->texture_initialized[read_color_tex]) {
        srcImage = &dk->textures[read_color_tex];
    }

    /* Resolve destination image */
    DkImage *dstImage = NULL;
    if (write_fbo == 0) {
        /* Default framebuffer (swapchain) */
        if (dk->framebuffers) {
            dstImage = &dk->framebuffers[dk->current_slot];
        }
    } else if (write_color_tex > 0 && write_color_tex < SGL_MAX_TEXTURES &&
               dk->texture_initialized[write_color_tex]) {
        dstImage = &dk->textures[write_color_tex];
    }

    if (!srcImage || !dstImage) {
        return;
    }

    /* Ensure all prior rendering is complete before blit */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

    /* Create image views */
    DkImageView srcView, dstView;
    dkImageViewDefaults(&srcView, srcImage);
    dkImageViewDefaults(&dstView, dstImage);

    /* Compute source rect (handle negative-width/height for flipping) */
    uint32_t sx0, sy0, sw, sh;
    if (srcX1 >= srcX0) { sx0 = srcX0; sw = srcX1 - srcX0; }
    else { sx0 = srcX1; sw = srcX0 - srcX1; }
    if (srcY1 >= srcY0) { sy0 = srcY0; sh = srcY1 - srcY0; }
    else { sy0 = srcY1; sh = srcY0 - srcY1; }

    /* Compute dest rect */
    uint32_t dx0, dy0, dw, dh;
    if (dstX1 >= dstX0) { dx0 = dstX0; dw = dstX1 - dstX0; }
    else { dx0 = dstX1; dw = dstX0 - dstX1; }
    if (dstY1 >= dstY0) { dy0 = dstY0; dh = dstY1 - dstY0; }
    else { dy0 = dstY1; dh = dstY0 - dstY1; }

    DkImageRect srcRect = { sx0, sy0, 0, sw, sh, 1 };
    DkImageRect dstRect = { dx0, dy0, 0, dw, dh, 1 };

    /* Select blit filter */
    uint32_t blitFlags = (filter == GL_LINEAR) ? DkBlitFlag_FilterLinear : 0;

    /* Mark destination texture as used as render target (for barrier tracking) */
    if (write_color_tex > 0 && write_color_tex < SGL_MAX_TEXTURES) {
        dk->texture_used_as_rt[write_color_tex] = true;
    }

    dkCmdBufBlitImage(dk->cmdbuf, &srcView, &srcRect, &dstView, &dstRect,
                      blitFlags, 0);

    /* Barrier after blit to ensure data is visible */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

    SGL_TRACE_FBO("blit_framebuffer read_fbo=%u(tex=%u) -> write_fbo=%u(tex=%u) %ux%u->%ux%u",
                  read_fbo, read_color_tex, write_fbo, write_color_tex, sw, sh, dw, dh);
}

/* ============================================================================
 * Read Pixels (glReadPixels)
 *
 * Reads pixel data from the current framebuffer back to CPU memory.
 * This is a synchronous operation that:
 * 1. Allocates a readback memory block
 * 2. Copies framebuffer region to the readback buffer
 * 3. Waits for the copy to complete
 * 4. Copies data to the user buffer
 * 5. Frees the readback memory
 * ============================================================================ */

void dk_read_pixels(sgl_backend_t *be, GLint x, GLint y,
                    GLsizei width, GLsizei height,
                    GLenum format, GLenum type, void *pixels) {
    (void)type;
    /* format used below for BGRA R/B swap */

    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (!pixels || width <= 0 || height <= 0 || x < 0 || y < 0) {
        return;
    }

    /* Get current render target - check if FBO is bound.
     * Use type flag to pick correct array (avoids renderbuffer/texture ID collision). */
    DkImage *srcImage = NULL;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0) {
        /* FBO is bound - read from color attachment */
        if (dk->current_fbo_color_is_rb) {
            if (dk->current_fbo_color < SGL_MAX_RENDERBUFFERS &&
                dk->renderbuffer_initialized[dk->current_fbo_color]) {
                srcImage = &dk->renderbuffer_images[dk->current_fbo_color];
            }
        } else {
            if (dk->current_fbo_color < SGL_MAX_TEXTURES &&
                dk->texture_initialized[dk->current_fbo_color]) {
                srcImage = &dk->textures[dk->current_fbo_color];
            }
        }
    } else if (dk->framebuffers) {
        /* Default framebuffer */
        srcImage = &dk->framebuffers[dk->current_slot];
    }

    if (!srcImage) {
        return;
    }

    /* Allocate separate memory block for readback
     * Using a dedicated block ensures proper CPU visibility */
    size_t bufferSize = (size_t)width * (size_t)height * 4;
    bufferSize = SGL_ALIGN_UP(bufferSize, SGL_PAGE_ALIGNMENT);  /* Align to 4KB */

    DkMemBlock readbackMem;
    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, dk->device, bufferSize);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    readbackMem = dkMemBlockCreate(&memMaker);

    if (!readbackMem) {
        return;
    }

    /* First, submit and flush all pending rendering commands (clears, draws, etc.)
     * This ensures the render target contents are finalized BEFORE we copy.
     * deko3d requires render pass to be complete before CopyImageToBuffer. */
    {
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
        DkCmdList flushList = dkCmdBufFinishList(dk->cmdbuf);
        dkQueueSubmitCommands(dk->queue, flushList);
        dkQueueWaitIdle(dk->queue);

        if (dkQueueIsInErrorState(dk->queue)) {
            SGL_ERROR_BACKEND("read_pixels: GPU error! slot=%d fbo=%u depth=%p draws=%u cmdbuf=%p submitted=%d",
                             dk->current_slot, dk->current_fbo,
                             (void*)dk->depth_images[dk->current_slot],
                             dk->diag_draw_count, (void*)dk->cmdbuf,
                             dk->cmdbuf_submitted);
            memset(pixels, 0, (size_t)width * (size_t)height * 4);
            dkMemBlockDestroy(readbackMem);
            dkCmdBufClear(dk->cmdbuf);
            dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
            dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
            dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
            dk->descriptors_bound = true;
            dk_rebind_default_render_target(dk);
            return;
        }

        /* Reset cmdbuf for the copy command */
        dkCmdBufClear(dk->cmdbuf);
        dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
        dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
        dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
        dk->descriptors_bound = true;
        /* All pending draws were flushed — reset counter so next dk_clear
         * doesn't spuriously call dk_submit_and_reset on an empty cmdbuf. */
        dk->draws_since_flush = 0;
    }

    /* Now record and submit the copy command in a clean cmdbuf */
    DkImageView srcView;
    dkImageViewDefaults(&srcView, srcImage);

    /* DkDeviceFlags_OriginLowerLeft makes deko3d use GL-style coordinates
     * where y=0 is at the bottom. The CopyImageToBuffer source rect uses
     * the same coordinate space — no Y-flip needed. */
    uint32_t src_height;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0) {
        if (dk->current_fbo_color_is_rb) {
            if (dk->current_fbo_color < SGL_MAX_RENDERBUFFERS &&
                dk->renderbuffer_initialized[dk->current_fbo_color]) {
                src_height = dk->renderbuffer_height[dk->current_fbo_color];
            } else {
                src_height = dk->fb_height;
            }
        } else {
            if (dk->current_fbo_color < SGL_MAX_TEXTURES &&
                dk->texture_initialized[dk->current_fbo_color]) {
                src_height = dk->texture_height[dk->current_fbo_color];
            } else {
                src_height = dk->fb_height;
            }
        }
    } else {
        src_height = dk->fb_height;
    }
    /* Bounds check */
    if ((uint32_t)y + (uint32_t)height > src_height) {
        dkMemBlockDestroy(readbackMem);
        dk_rebind_render_target(dk);
        return;
    }
    uint32_t dk_y = (uint32_t)y;

    DkImageRect srcRect = { (uint32_t)x, dk_y, 0, (uint32_t)width, (uint32_t)height, 1 };
    DkCopyBuf dstBuf = { dkMemBlockGetGpuAddr(readbackMem), (uint32_t)(width * 4), (uint32_t)height };

    dkCmdBufCopyImageToBuffer(dk->cmdbuf, &srcView, &srcRect, &dstBuf, 0);

    /* Submit and wait for copy to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Check if the GPU queue entered an error state during this submit */
    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("read_pixels: GPU queue entered ERROR STATE during copy!");
    }

    /* Copy data to user buffer.
     * The source Y-flip (dk_y = src_height - y - height) already maps the
     * GL read region to the correct framebuffer rows. With OriginLowerLeft
     * and viewport Y-flip, the GPU copy output is already in GL row order:
     * output row 0 = GL bottom of read region, output row N = GL top.
     * No additional row reversal is needed.
     *
     * Per GL spec, glReadPixels output respects GL_PACK_ALIGNMENT:
     * each row is padded to a multiple of pack_alignment bytes. */
    sgl_context_t *ctx = sgl_get_current_context();
    int pack_alignment = (ctx && ctx->pack_alignment > 0) ? ctx->pack_alignment : 4;
    size_t row_bytes = (size_t)width * 4;  /* actual pixel data per row */
    size_t row_stride = ((row_bytes + pack_alignment - 1) / pack_alignment) * pack_alignment;  /* padded */

    void *cpuAddr = dkMemBlockGetCpuAddr(readbackMem);
    uint8_t *src_ptr = (uint8_t *)cpuAddr;
    uint8_t *dst_ptr = (uint8_t *)pixels;
    for (int row = 0; row < height; row++) {
        memcpy(dst_ptr + row * row_stride,
               src_ptr + row * row_bytes,
               row_bytes);
    }

    /* Per GLES spec §4.3.1: "If the color buffer does not store an alpha
     * component, the alpha component of the result is set to 1.0."
     * Since RGB/RGB565 FBOs use RGBA8 internally, force alpha to 255. */
    if (dk->current_fbo != 0 && ctx) {
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
                for (int row = 0; row < height; row++) {
                    uint8_t *p = dst_ptr + row * row_stride;
                    for (int col = 0; col < width; col++) {
                        p[col * 4 + 3] = 255;
                    }
                }
            }
        }
    }

    /* Handle R/B channel swap for BGRA.
     * Two cases need swapping:
     * 1. Source is BGRA texture (DkImageFormat_BGRA8_Unorm), caller wants GL_RGBA
     *    → raw data is BGRA, need to swap to RGBA
     * 2. Source is RGBA (any RB or RGBA texture), caller wants GL_BGRA_EXT
     *    → raw data is RGBA, need to swap to BGRA
     * Detect source format from the FBO's color texture format. */
    {
        bool source_is_bgra = false;
        if (dk->current_fbo != 0 && dk->current_fbo_color > 0 && !dk->current_fbo_color_is_rb) {
            if (dk->current_fbo_color < SGL_MAX_TEXTURES &&
                dk->texture_format[dk->current_fbo_color] == DkImageFormat_BGRA8_Unorm) {
                source_is_bgra = true;
            }
        }
        bool need_swap = (source_is_bgra && format != GL_BGRA_EXT) ||
                         (!source_is_bgra && format == GL_BGRA_EXT);
        if (need_swap) {
            for (int row = 0; row < height; row++) {
                uint8_t *p = dst_ptr + row * row_stride;
                for (int col = 0; col < width; col++) {
                    uint8_t tmp = p[col * 4 + 0];
                    p[col * 4 + 0] = p[col * 4 + 2];
                    p[col * 4 + 2] = tmp;
                }
            }
        }
    }

    /* Cleanup readback memory */
    dkMemBlockDestroy(readbackMem);

    /* Reset command buffer for continued use */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Re-bind descriptor sets and render target after cmdbuf clear (matches legacy pattern) */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    dk_rebind_render_target(dk);

    SGL_TRACE_FBO("read_pixels %d,%d %dx%d", x, y, width, height);
}

/* ============================================================================
 * Renderbuffer Storage
 *
 * Allocates GPU memory for depth/stencil renderbuffers.
 * These are used as FBO attachments for depth testing without a depth texture.
 * ============================================================================ */

void dk_renderbuffer_storage(sgl_backend_t *be, sgl_handle_t handle,
                              GLenum internalformat, GLsizei width, GLsizei height) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_RENDERBUFFERS) {
        return;
    }

    /* Zero-size renderbuffers: valid per spec (stores format/size for queries)
     * but don't allocate GPU memory. The old memblock must still be freed. */
    if (width <= 0 || height <= 0) {
        if (dk->renderbuffer_memblocks[handle]) {
            dk_submit_and_reset(dk);
            dkMemBlockDestroy(dk->renderbuffer_memblocks[handle]);
            dk->renderbuffer_memblocks[handle] = NULL;
        }
        dk->renderbuffer_initialized[handle] = false;
        dk->renderbuffer_width[handle] = 0;
        dk->renderbuffer_height[handle] = 0;
        return;
    }

    /* Determine deko3d format */
    DkImageFormat dkFormat;
    switch (internalformat) {
        case GL_RGBA:
        case GL_RGBA8:
        case GL_RGB:
        case GL_RGB8:
        case GL_RGBA4:
        case GL_RGB5_A1:
        case GL_RGB565:
            /* All color formats use RGBA8 internally.
             * Sub-byte formats (RGBA4, RGB565, RGB5_A1) get promoted to RGBA8
             * to simplify readback (CopyImageToBuffer assumes 4 bytes/pixel). */
            dkFormat = DkImageFormat_RGBA8_Unorm;
            break;
        case GL_BGRA_EXT:
        case GL_BGRA8_EXT:
            /* BGRA renderbuffers use RGBA8 internally.
             * The BGRA extension only requires accepting the format enum —
             * internal storage can be RGBA8. This avoids R/B swap issues
             * in clear, render, and readback paths. */
            dkFormat = DkImageFormat_RGBA8_Unorm;
            break;
        case GL_DEPTH_COMPONENT16:
            /* Use Z24S8 instead of Z16: deko3d has only ONE depth/stencil slot.
             * GLES2 allows separate depth (DEPTH_COMPONENT16) and stencil
             * (STENCIL_INDEX8) attachments, but deko3d can't bind them separately.
             * Z24S8 provides ≥16 depth bits (spec-compliant) PLUS 8 stencil bits,
             * so the stencil channel is available when both are attached. */
            dkFormat = DkImageFormat_Z24S8;
            break;
        case GL_DEPTH_COMPONENT:
        case GL_DEPTH_COMPONENT24:
            dkFormat = DkImageFormat_Z24S8;
            break;
        case GL_STENCIL_INDEX8:
            /* Use Z24S8 instead of S8: deko3d requires combined depth/stencil
             * format for the depth/stencil attachment. S8 alone causes GPU errors.
             * Z24S8 gives a valid stencil channel; depth bits are unused but harmless. */
            dkFormat = DkImageFormat_Z24S8;
            break;
        default:
            /* Unknown format — don't create the renderbuffer.
             * glCheckFramebufferStatus will report INCOMPLETE_ATTACHMENT. */
            return;
    }

    /* Create the depth/stencil image */
    DkImageLayoutMaker imgMaker;
    dkImageLayoutMakerDefaults(&imgMaker, dk->device);
    imgMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine | DkImageFlags_HwCompression;
    imgMaker.format = dkFormat;
    imgMaker.dimensions[0] = width;
    imgMaker.dimensions[1] = height;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &imgMaker);

    uint32_t imgSize = dkImageLayoutGetSize(&layout);
    uint32_t imgAlign = dkImageLayoutGetAlignment(&layout);

    /* Create dedicated memory block for this renderbuffer (like default depth buffer)
     * Align size to alignment requirement */
    uint32_t alignedSize = SGL_ALIGN_UP(imgSize, imgAlign);

    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, dk->device, alignedSize);
    memMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;

    /* Destroy old memblock if it exists.
     * Must flush pending GPU commands first — the current command buffer may
     * contain RT bind/clear commands referencing the old DkImage backed by
     * this memblock (e.g. when handle is reused after glDeleteRenderbuffers). */
    if (dk->renderbuffer_memblocks[handle]) {
        dk_submit_and_reset(dk);
        dkMemBlockDestroy(dk->renderbuffer_memblocks[handle]);
    }

    dk->renderbuffer_memblocks[handle] = dkMemBlockCreate(&memMaker);
    if (!dk->renderbuffer_memblocks[handle]) {
        return;
    }

    /* Initialize the image in the dedicated memblock */
    dkImageInitialize(&dk->renderbuffer_images[handle], &layout,
                      dk->renderbuffer_memblocks[handle], 0);

    dk->renderbuffer_width[handle] = width;
    dk->renderbuffer_height[handle] = height;
    dk->renderbuffer_initialized[handle] = true;

    /* Re-bind render target if this renderbuffer is currently attached to the bound FBO.
     * After reallocation, the GPU render target still points to the old (destroyed) DkImage.
     * Without this, FBO resize tests fail because draws go to freed memory. */
    if (dk->current_fbo != 0) {
        if ((dk->current_fbo_color == handle && dk->current_fbo_color_is_rb) ||
            (dk->current_fbo_depth == handle && dk->current_fbo_depth_is_rb) ||
            (dk->current_fbo_stencil == handle && dk->current_fbo_stencil_is_rb)) {
            dk_rebind_render_target(dk);
        }
    }

    SGL_TRACE_FBO("renderbuffer_storage handle=%u format=0x%X %dx%d", handle, internalformat, width, height);
}

/* ============================================================================
 * Delete Renderbuffer
 *
 * Marks a renderbuffer's GPU resources as available for reuse.
 * Note: Actual memory is not freed (bump allocator), but the slot is cleared.
 * ============================================================================ */

void dk_delete_renderbuffer(sgl_backend_t *be, sgl_handle_t handle) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_RENDERBUFFERS) {
        return;
    }

    /* Flush pending GPU commands before destroying the memblock —
     * the command buffer may reference this renderbuffer's DkImage. */
    if (dk->renderbuffer_memblocks[handle]) {
        dk_submit_and_reset(dk);
        dkMemBlockDestroy(dk->renderbuffer_memblocks[handle]);
        dk->renderbuffer_memblocks[handle] = NULL;
    }

    dk->renderbuffer_initialized[handle] = false;
    dk->renderbuffer_width[handle] = 0;
    dk->renderbuffer_height[handle] = 0;

    SGL_TRACE_FBO("delete_renderbuffer handle=%u", handle);
}
