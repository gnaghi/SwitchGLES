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

/* ============================================================================
 * Framebuffer Binding
 *
 * Switches the current render target:
 * - handle=0: Bind default framebuffer (swapchain image)
 * - handle>0: Bind FBO with specified color attachment texture
 * ============================================================================ */

void dk_bind_framebuffer(sgl_backend_t *be, sgl_handle_t handle,
                         sgl_handle_t color_tex, sgl_handle_t depth_rb) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Track current FBO state */
    dk->current_fbo = handle;
    dk->current_fbo_color = color_tex;
    dk->current_fbo_depth = depth_rb;

    /* Mark texture as used as render target (for barrier optimization) */
    if (color_tex > 0 && color_tex < SGL_MAX_TEXTURES) {
        dk->texture_used_as_rt[color_tex] = true;
    }

    /* Insert barrier before switching render targets
     * This ensures any previous rendering is complete before we switch
     * Include L2Cache invalidation for proper cache coherency when switching
     * between render target and texture sampling */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);

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
    } else if (color_tex > 0 && color_tex < SGL_MAX_TEXTURES && dk->texture_initialized[color_tex]) {
        /* Bind FBO with texture as color attachment */
        DkImageView colorView;
        dkImageViewDefaults(&colorView, &dk->textures[color_tex]);

        /* Check for depth renderbuffer attachment */
        DkImageView *pDepthView = NULL;
        DkImageView depthView;
        if (depth_rb > 0 && depth_rb < SGL_MAX_RENDERBUFFERS &&
            dk->renderbuffer_initialized[depth_rb]) {
            dkImageViewDefaults(&depthView, &dk->renderbuffer_images[depth_rb]);
            pDepthView = &depthView;
        }

        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, pDepthView);
    }

    SGL_TRACE_FBO("bind_framebuffer handle=%u color_tex=%u depth_rb=%u", handle, color_tex, depth_rb);
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
    (void)format;
    (void)type;

    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (!pixels || width <= 0 || height <= 0) {
        return;
    }

    /* Get current render target - check if FBO is bound */
    DkImage *srcImage = NULL;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0 &&
        dk->current_fbo_color < SGL_MAX_TEXTURES &&
        dk->texture_initialized[dk->current_fbo_color]) {
        /* FBO is bound - read from FBO color texture */
        srcImage = &dk->textures[dk->current_fbo_color];
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
    bufferSize = (bufferSize + 0xFFF) & ~0xFFF;  /* Align to 4KB */

    DkMemBlock readbackMem;
    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, dk->device, bufferSize);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    readbackMem = dkMemBlockCreate(&memMaker);

    if (!readbackMem) {
        return;
    }

    /* Add barrier to ensure all rendering is complete before copy */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    /* Create copy command */
    DkImageView srcView;
    dkImageViewDefaults(&srcView, srcImage);

    /* Note: deko3d Y=0 is top, OpenGL Y=0 is bottom
     * The copy reads from deko3d coordinates directly */
    DkImageRect srcRect = { (uint32_t)x, (uint32_t)y, 0, (uint32_t)width, (uint32_t)height, 1 };
    DkCopyBuf dstBuf = { dkMemBlockGetGpuAddr(readbackMem), (uint32_t)(width * 4), (uint32_t)height };

    dkCmdBufCopyImageToBuffer(dk->cmdbuf, &srcView, &srcRect, &dstBuf, 0);

    /* Flush and wait for copy to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Copy data to user buffer */
    void *cpuAddr = dkMemBlockGetCpuAddr(readbackMem);
    memcpy(pixels, cpuAddr, (size_t)width * (size_t)height * 4);

    /* Cleanup readback memory */
    dkMemBlockDestroy(readbackMem);

    /* Wait for fence before reusing command memory */
    if (dk->fence_active[dk->current_slot]) {
        dkFenceWait(&dk->fences[dk->current_slot], -1);
    }

    /* Reset command buffer for continued use */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Re-bind render target after clearing command buffer */
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0 &&
        dk->current_fbo_color < SGL_MAX_TEXTURES &&
        dk->texture_initialized[dk->current_fbo_color]) {
        /* Restore FBO binding */
        DkImageView colorView;
        dkImageViewDefaults(&colorView, &dk->textures[dk->current_fbo_color]);

        DkImageView *pDepthView = NULL;
        DkImageView depthView;
        if (dk->current_fbo_depth > 0 && dk->current_fbo_depth < SGL_MAX_RENDERBUFFERS &&
            dk->renderbuffer_initialized[dk->current_fbo_depth]) {
            dkImageViewDefaults(&depthView, &dk->renderbuffer_images[dk->current_fbo_depth]);
            pDepthView = &depthView;
        }

        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, pDepthView);
    } else if (dk->framebuffers) {
        /* Restore default framebuffer */
        DkImageView colorView;
        dkImageViewDefaults(&colorView, &dk->framebuffers[dk->current_slot]);
        if (dk->depth_images[dk->current_slot]) {
            DkImageView depthView;
            dkImageViewDefaults(&depthView, dk->depth_images[dk->current_slot]);
            dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, &depthView);
        } else {
            dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, NULL);
        }
    }

    /* Reset descriptors_bound flag */
    dk->descriptors_bound = false;

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

    /* Determine deko3d format
     * Use Z24S8 to match default depth buffer (Z16 may have issues)
     * GLES2 only defines GL_DEPTH_COMPONENT16 and GL_STENCIL_INDEX8
     * Other formats are extensions or GLES3 */
    DkImageFormat dkFormat;
    switch (internalformat) {
        case GL_DEPTH_COMPONENT16:
        case GL_DEPTH_COMPONENT:  /* Use Z24S8 for better compatibility */
            dkFormat = DkImageFormat_Z24S8;
            break;
        case GL_STENCIL_INDEX8:
            dkFormat = DkImageFormat_S8;
            break;
        default:
            dkFormat = DkImageFormat_Z24S8;
            break;
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
    uint32_t alignedSize = (imgSize + imgAlign - 1) & ~(imgAlign - 1);

    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, dk->device, alignedSize);
    memMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;

    /* Destroy old memblock if it exists */
    if (dk->renderbuffer_memblocks[handle]) {
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

    /* Destroy the dedicated memblock */
    if (dk->renderbuffer_memblocks[handle]) {
        dkMemBlockDestroy(dk->renderbuffer_memblocks[handle]);
        dk->renderbuffer_memblocks[handle] = NULL;
    }

    dk->renderbuffer_initialized[handle] = false;
    dk->renderbuffer_width[handle] = 0;
    dk->renderbuffer_height[handle] = 0;

    SGL_TRACE_FBO("delete_renderbuffer handle=%u", handle);
}
