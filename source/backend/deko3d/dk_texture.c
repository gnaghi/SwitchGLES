/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Texture Operations
 *
 * This module handles:
 * - Texture image upload (glTexImage2D) - 2D and Cubemap
 * - Texture sub-image update (glTexSubImage2D)
 * - Texture parameter setting (glTexParameteri)
 * - Texture binding for sampling
 * - Mipmap generation (glGenerateMipmap)
 * - Copy from framebuffer (glCopyTexImage2D, glCopyTexSubImage2D)
 *
 * Texture memory management:
 * - Textures are allocated from texture_memblock (bump allocator)
 * - Image descriptors are stored in texture_descriptors array
 * - Sampler parameters are stored per-texture and applied at bind time
 */

#include "dk_internal.h"

/* ============================================================================
 * Cubemap Helpers
 * ============================================================================ */

static bool dk_is_cubemap_face(GLenum target) {
    return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
           target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

static int dk_cubemap_face_index(GLenum target) {
    return (int)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
}

/* ============================================================================
 * Cubemap Texture Upload (internal)
 * ============================================================================ */

static void dk_cubemap_face_upload(dk_backend_data_t *dk, sgl_handle_t handle,
                                    GLenum target, GLint internalformat,
                                    GLsizei width, GLsizei height,
                                    GLenum format, GLenum type, const void *pixels) {
    int face_index = dk_cubemap_face_index(target);

    /* Create cubemap on first face upload */
    if (!dk->texture_initialized[handle]) {
        /* Initialize DkImage as cubemap */
        DkImageLayoutMaker layoutMaker;
        dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
        layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
        layoutMaker.format = dk_convert_format(internalformat, format, type);
        layoutMaker.type = DkImageType_Cubemap;
        layoutMaker.dimensions[0] = width;
        layoutMaker.dimensions[1] = height;
        layoutMaker.dimensions[2] = 1;
        layoutMaker.mipLevels = 1;

        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &layoutMaker);

        uint64_t texSize = dkImageLayoutGetSize(&layout);
        uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

        uint32_t aligned_offset = (dk->texture_offset + texAlign - 1) & ~(texAlign - 1);
        if (aligned_offset + texSize > SGL_TEXTURE_MEM_SIZE) {
            SGL_ERROR_BACKEND("Cubemap texture memory overflow");
            return;
        }

        DkImage *texImage = &dk->textures[handle];
        dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
        dk->texture_offset = aligned_offset + texSize;
        dk->texture_initialized[handle] = true;
        dk->texture_is_cubemap[handle] = true;

        dk->texture_width[handle] = width;
        dk->texture_height[handle] = height;
        dk->texture_mip_levels[handle] = 1;
        dk->texture_format[handle] = layoutMaker.format;

        dk->texture_min_filter[handle] = GL_LINEAR;
        dk->texture_mag_filter[handle] = GL_LINEAR;
        dk->texture_wrap_s[handle] = GL_CLAMP_TO_EDGE;
        dk->texture_wrap_t[handle] = GL_CLAMP_TO_EDGE;

        /* Create cubemap image descriptor */
        DkImageView imageView;
        dkImageViewDefaults(&imageView, texImage);
        imageView.type = DkImageType_Cubemap;

        DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
        dkImageDescriptorInitialize(imgDesc, &imageView, false, false);

        SGL_TRACE_TEXTURE("cubemap created handle=%u %dx%d", handle, width, height);
    }

    /* Upload face pixels if provided */
    if (pixels) {
        DkImage *texImage = &dk->textures[handle];

        uint32_t row_size = width * 4;
        uint32_t aligned_row_size = (row_size + 31) & ~31;
        uint32_t staging_size = aligned_row_size * height;

        uint32_t stagingOffset = (dk->client_array_offset + 31) & ~31;
        if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
            SGL_ERROR_BACKEND("cubemap staging buffer overflow");
            return;
        }

        uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                           + dk->client_array_base + stagingOffset;
        const uint8_t *src = (const uint8_t*)pixels;

        /* Copy pixels to staging buffer */
        if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
            for (int y = 0; y < height; y++) {
                memcpy(staging + y * aligned_row_size, src + y * width * 4, width * 4);
            }
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
            for (int y = 0; y < height; y++) {
                uint8_t *dst_row = staging + y * aligned_row_size;
                const uint8_t *src_row = src + y * width * 3;
                for (int x = 0; x < width; x++) {
                    dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                    dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                    dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                    dst_row[x * 4 + 3] = 255;
                }
            }
        } else {
            for (int y = 0; y < height; y++) {
                memcpy(staging + y * aligned_row_size, src + y * width * 4, width * 4);
            }
        }

        dk->client_array_offset = stagingOffset + staging_size;

        /* Create image view targeting specific face */
        DkImageView faceView;
        dkImageViewDefaults(&faceView, texImage);
        faceView.type = DkImageType_2D;  /* Upload as 2D slice */
        faceView.layerOffset = face_index;
        faceView.layerCount = 1;

        DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                                + dk->client_array_base + stagingOffset;
        DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
        DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

        dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &faceView, &dstRect, 0);

        /* Flush and wait */
        DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
        dkQueueSubmitCommands(dk->queue, cmdlist);
        dkQueueWaitIdle(dk->queue);

        /* Reset command buffer */
        dkCmdBufClear(dk->cmdbuf);
        dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
        dk->descriptors_bound = false;

        if (dk->framebuffers) {
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

        SGL_TRACE_TEXTURE("cubemap face %d uploaded handle=%u", face_index, handle);
    }
}

/* ============================================================================
 * Texture Image Upload (glTexImage2D) - 2D textures
 * ============================================================================ */

void dk_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                         GLenum target, GLint level, GLint internalformat,
                         GLsizei width, GLsizei height, GLint border,
                         GLenum format, GLenum type, const void *pixels) {
    (void)level;
    (void)border;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;

    /* Handle cubemap faces separately */
    if (dk_is_cubemap_face(target)) {
        dk_cubemap_face_upload(dk, handle, target, internalformat, width, height, format, type, pixels);
        return;
    }

    /* === Regular 2D texture path (unchanged) === */

    /* Calculate number of mip levels (for potential glGenerateMipmap) */
    uint32_t max_dim = (uint32_t)(width > height ? width : height);
    uint32_t mip_levels = 1;
    uint32_t temp = max_dim;
    while (temp > 1) {
        temp >>= 1;
        mip_levels++;
    }

    /* Initialize DkImage for this texture */
    DkImageLayoutMaker layoutMaker;
    dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
    layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layoutMaker.format = dk_convert_format(internalformat, format, type);
    layoutMaker.dimensions[0] = width;
    layoutMaker.dimensions[1] = height;
    layoutMaker.dimensions[2] = 1;
    layoutMaker.mipLevels = mip_levels;  /* Allocate space for all mip levels */

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layoutMaker);

    uint64_t texSize = dkImageLayoutGetSize(&layout);
    uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

    /* Align texture offset */
    uint32_t aligned_offset = (dk->texture_offset + texAlign - 1) & ~(texAlign - 1);
    if (aligned_offset + texSize > SGL_TEXTURE_MEM_SIZE) {
        SGL_ERROR_BACKEND("Texture memory overflow");
        return;
    }

    DkImage *texImage = &dk->textures[handle];
    dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
    dk->texture_offset = aligned_offset + texSize;
    dk->texture_initialized[handle] = true;
    dk->texture_is_cubemap[handle] = false;  /* This is a 2D texture */

    /* Store texture dimensions and format for glGenerateMipmap */
    dk->texture_width[handle] = width;
    dk->texture_height[handle] = height;
    dk->texture_mip_levels[handle] = mip_levels;
    dk->texture_format[handle] = layoutMaker.format;

    /* Initialize default sampler parameters (GL defaults) */
    dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;  /* GL default */
    dk->texture_mag_filter[handle] = GL_LINEAR;                  /* GL default */
    dk->texture_wrap_s[handle] = GL_REPEAT;
    dk->texture_wrap_t[handle] = GL_REPEAT;

    /* Create image descriptor */
    DkImageView imageView;
    dkImageViewDefaults(&imageView, texImage);

    DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(imgDesc, &imageView, false, false);

    /* Upload pixel data if provided - use staging buffer and GPU copy like legacy */
    if (pixels) {
        /* Calculate source size with stride alignment (like legacy) */
        uint32_t row_size = width * 4;  /* RGBA */
        uint32_t aligned_row_size = (row_size + 31) & ~31;  /* DK_IMAGE_LINEAR_STRIDE_ALIGNMENT */
        uint32_t staging_size = aligned_row_size * height;

        const uint8_t *src = (const uint8_t*)pixels;

        /* Use client array region as staging */
        uint32_t stagingOffset = (dk->client_array_offset + 31) & ~31;
        if (stagingOffset + staging_size <= dk->uniform_base - dk->client_array_base) {
            uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                               + dk->client_array_base + stagingOffset;

            /* Copy pixel data to staging buffer with proper stride.
             *
             * IMPORTANT: NO Y-flip here!
             * OpenGL texture V=0 samples the bottom of the texture content.
             * deko3d texture V=0 samples the TOP of the texture storage (row 0).
             * By storing GL row 0 (bottom) at storage row 0 (top), deko3d V=0
             * will sample what GL expects at V=0 (bottom content).
             *
             * This means the texture appears "upside down" in GPU memory
             * compared to GL's logical view, but sampling works correctly. */
            if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
                for (int y = 0; y < height; y++) {
                    memcpy(staging + y * aligned_row_size, src + y * width * 4, width * 4);
                }
            } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
                /* Convert RGB to RGBA, no Y-flip */
                for (int y = 0; y < height; y++) {
                    uint8_t *dst_row = staging + y * aligned_row_size;
                    const uint8_t *src_row = src + y * width * 3;
                    for (int x = 0; x < width; x++) {
                        dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                        dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                        dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                        dst_row[x * 4 + 3] = 255;
                    }
                }
            } else {
                /* For other formats, just copy with stride, no Y-flip */
                int src_bpp = 4;  /* Default to RGBA */
                for (int y = 0; y < height; y++) {
                    memcpy(staging + y * aligned_row_size, src + y * width * src_bpp, width * src_bpp);
                }
            }

            dk->client_array_offset = stagingOffset + staging_size;

            /* Copy staging to texture */
            DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                                    + dk->client_array_base + stagingOffset;
            DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
            DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

            dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &imageView, &dstRect, 0);

            /* Flush and wait for copy to complete (like legacy) */
            DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
            dkQueueSubmitCommands(dk->queue, cmdlist);
            dkQueueWaitIdle(dk->queue);

            /* Reset command buffer for continued use */
            dkCmdBufClear(dk->cmdbuf);
            dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

            /* Reset descriptors_bound since we cleared the command buffer */
            dk->descriptors_bound = false;

            /* Re-bind render target if we have framebuffers */
            if (dk->framebuffers) {
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
        }
    }

    SGL_TRACE_TEXTURE("texture_image_2d handle=%u %dx%d", handle, width, height);
}

/* ============================================================================
 * Texture Sub-Image Update (glTexSubImage2D)
 * ============================================================================ */

void dk_texture_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                             GLenum target, GLint level,
                             GLint xoffset, GLint yoffset,
                             GLsizei width, GLsizei height,
                             GLenum format, GLenum type, const void *pixels) {
    (void)target;
    (void)level;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (!dk->texture_initialized[handle]) {
        SGL_ERROR_BACKEND("texture_sub_image_2d: texture %u not initialized", handle);
        return;
    }
    if (!pixels) return;

    /* Get the existing DkImage */
    DkImage *texImage = &dk->textures[handle];

    /* Calculate source size with stride alignment */
    uint32_t row_size = width * 4;  /* RGBA */
    uint32_t aligned_row_size = (row_size + 31) & ~31;  /* DK_IMAGE_LINEAR_STRIDE_ALIGNMENT */
    uint32_t staging_size = aligned_row_size * height;

    const uint8_t *src = (const uint8_t*)pixels;

    /* Use client array region as staging */
    uint32_t stagingOffset = (dk->client_array_offset + 31) & ~31;
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("texture_sub_image_2d: staging buffer overflow");
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;

    /* Copy pixel data to staging buffer with proper stride.
     * No Y-flip needed - texture storage matches GL row order (see glTexImage2D comment). */
    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
        for (int y = 0; y < height; y++) {
            memcpy(staging + y * aligned_row_size, src + y * width * 4, width * 4);
        }
    } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
        /* Convert RGB to RGBA, no Y-flip */
        for (int y = 0; y < height; y++) {
            uint8_t *dst_row = staging + y * aligned_row_size;
            const uint8_t *src_row = src + y * width * 3;
            for (int x = 0; x < width; x++) {
                dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                dst_row[x * 4 + 3] = 255;
            }
        }
    } else {
        /* For other formats, just copy with stride, no Y-flip */
        for (int y = 0; y < height; y++) {
            memcpy(staging + y * aligned_row_size, src + y * width * 4, width * 4);
        }
    }

    dk->client_array_offset = stagingOffset + staging_size;

    /* Create image view for the existing texture */
    DkImageView imageView;
    dkImageViewDefaults(&imageView, texImage);

    /* Copy staging to texture at the specified offset.
     * Since glTexImage2D stores GL row 0 at storage row 0 (no flip),
     * GL yoffset maps directly to storage row offset. */
    uint32_t dk_yoffset = (uint32_t)yoffset;

    DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                            + dk->client_array_base + stagingOffset;
    DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
    /* Note: DkImageRect is { x, y, z, width, height, depth } */
    DkImageRect dstRect = { (uint32_t)xoffset, dk_yoffset, 0, (uint32_t)width, (uint32_t)height, 1 };

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &imageView, &dstRect, 0);

    /* Flush and wait for copy to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Reset command buffer for continued use */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Reset descriptors_bound since we cleared the command buffer */
    dk->descriptors_bound = false;

    /* Re-bind render target if we have framebuffers */
    if (dk->framebuffers) {
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

    SGL_TRACE_TEXTURE("texture_sub_image_2d handle=%u offset=(%d,%d) %dx%d",
                      handle, xoffset, yoffset, width, height);
}

/* ============================================================================
 * Texture Parameter Setting (glTexParameteri)
 * ============================================================================ */

void dk_texture_parameter(sgl_backend_t *be, sgl_handle_t handle,
                          GLenum target, GLenum pname, GLint param) {
    (void)target;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;

    /* Store sampler parameters - used when binding texture */
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            dk->texture_min_filter[handle] = (GLenum)param;
            break;
        case GL_TEXTURE_MAG_FILTER:
            dk->texture_mag_filter[handle] = (GLenum)param;
            break;
        case GL_TEXTURE_WRAP_S:
            dk->texture_wrap_s[handle] = (GLenum)param;
            break;
        case GL_TEXTURE_WRAP_T:
            dk->texture_wrap_t[handle] = (GLenum)param;
            break;
        default:
            break;
    }

    SGL_TRACE_TEXTURE("texture_parameter handle=%u pname=0x%X param=0x%X", handle, pname, param);
}

/* ============================================================================
 * Texture Binding for Sampling
 * ============================================================================ */

void dk_bind_texture(sgl_backend_t *be, GLuint unit, sgl_handle_t handle) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES || !dk->texture_initialized[handle]) {
        return;
    }

    /* Add barrier to ensure any prior rendering to this texture is visible.
     * This is needed when a texture was used as a render target (FBO) and
     * is now being sampled. */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);

    /* Bind descriptor block if not already done */
    if (!dk->descriptors_bound) {
        dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
        dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
        dk->descriptors_bound = true;
    }

    /* CRITICAL FIX: Push descriptors at UNIT index, not handle index!
     * The shader samples from descriptor[unit], so we must place the texture's
     * descriptor at that slot. Previously we were pushing at descriptor[handle]
     * which meant sampling from wrong descriptor when unit != handle. */

    /* Push image descriptor at UNIT position */
    DkGpuAddr imgDescAddr = dk->image_descriptor_addr + unit * sizeof(DkImageDescriptor);
    dkCmdBufPushData(dk->cmdbuf, imgDescAddr, &dk->texture_descriptors[handle], sizeof(DkImageDescriptor));

    /* Create and push sampler descriptor at UNIT position using stored parameters */
    DkSamplerDescriptor samplerDesc;
    DkSampler sampler;
    dkSamplerDefaults(&sampler);

    /* Convert GL min filter to deko3d */
    switch (dk->texture_min_filter[handle]) {
        case GL_NEAREST:
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
            sampler.minFilter = DkFilter_Nearest;
            break;
        case GL_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
        default:
            sampler.minFilter = DkFilter_Linear;
            break;
    }

    /* Convert GL mag filter to deko3d */
    switch (dk->texture_mag_filter[handle]) {
        case GL_NEAREST:
            sampler.magFilter = DkFilter_Nearest;
            break;
        case GL_LINEAR:
        default:
            sampler.magFilter = DkFilter_Linear;
            break;
    }

    /* Convert GL wrap modes to deko3d */
    switch (dk->texture_wrap_s[handle]) {
        case GL_REPEAT:
            sampler.wrapMode[0] = DkWrapMode_Repeat;
            break;
        case GL_MIRRORED_REPEAT:
            sampler.wrapMode[0] = DkWrapMode_MirroredRepeat;
            break;
        case GL_CLAMP_TO_EDGE:
        default:
            sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
            break;
    }

    switch (dk->texture_wrap_t[handle]) {
        case GL_REPEAT:
            sampler.wrapMode[1] = DkWrapMode_Repeat;
            break;
        case GL_MIRRORED_REPEAT:
            sampler.wrapMode[1] = DkWrapMode_MirroredRepeat;
            break;
        case GL_CLAMP_TO_EDGE:
        default:
            sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
            break;
    }
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;

    dkSamplerDescriptorInitialize(&samplerDesc, &sampler);

    DkGpuAddr sampDescAddr = dk->sampler_descriptor_addr + unit * sizeof(DkSamplerDescriptor);
    dkCmdBufPushData(dk->cmdbuf, sampDescAddr, &samplerDesc, sizeof(DkSamplerDescriptor));

    /* CRITICAL: Bind the texture handle to the fragment shader stage!
     * This tells the shader which image/sampler descriptor indices to use.
     * The handle combines image descriptor index (unit) and sampler descriptor index (unit). */
    DkResHandle texHandle = dkMakeTextureHandle(unit, unit);
    dkCmdBufBindTexture(dk->cmdbuf, DkStage_Fragment, unit, texHandle);

    SGL_TRACE_TEXTURE("bind_texture unit=%u handle=%u", unit, handle);
}

/* ============================================================================
 * Mipmap Generation (glGenerateMipmap)
 * ============================================================================ */

void dk_generate_mipmap(sgl_backend_t *be, sgl_handle_t handle) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES || !dk->texture_initialized[handle]) {
        return;
    }

    uint32_t width = dk->texture_width[handle];
    uint32_t height = dk->texture_height[handle];
    uint32_t mip_levels = dk->texture_mip_levels[handle];

    if (mip_levels <= 1) {
        return;
    }

    DkImage *texImage = &dk->textures[handle];

    /* Generate each mip level by blitting from the previous level */
    uint32_t src_width = width;
    uint32_t src_height = height;

    for (uint32_t level = 1; level < mip_levels; level++) {
        uint32_t dst_width = src_width > 1 ? src_width >> 1 : 1;
        uint32_t dst_height = src_height > 1 ? src_height >> 1 : 1;

        /* Create image views for source (level-1) and destination (level) */
        DkImageView srcView, dstView;
        dkImageViewDefaults(&srcView, texImage);
        srcView.mipLevelOffset = level - 1;
        srcView.mipLevelCount = 1;

        dkImageViewDefaults(&dstView, texImage);
        dstView.mipLevelOffset = level;
        dstView.mipLevelCount = 1;

        /* Define source and destination rectangles */
        DkImageRect srcRect = { 0, 0, 0, src_width, src_height, 1 };
        DkImageRect dstRect = { 0, 0, 0, dst_width, dst_height, 1 };

        /* Blit with linear filtering for smooth downscaling */
        dkCmdBufBlitImage(dk->cmdbuf, &srcView, &srcRect, &dstView, &dstRect,
                          DkBlitFlag_FilterLinear, 0);

        /* Add barrier between mip levels to ensure proper synchronization
         * The previous blit must complete before the next level reads from it */
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

        src_width = dst_width;
        src_height = dst_height;
    }

    /* Final barrier to ensure all mipmap generation is complete before sampling */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

    SGL_TRACE_TEXTURE("generate_mipmap handle=%u levels=%u", handle, mip_levels);
}

/* ============================================================================
 * Copy Framebuffer to Texture (glCopyTexImage2D)
 * ============================================================================ */

void dk_copy_tex_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                          GLenum target, GLint level, GLenum internalformat,
                          GLint x, GLint y, GLsizei width, GLsizei height) {
    (void)target;
    (void)level;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (width <= 0 || height <= 0) return;

    /* Get current render target (source) */
    DkImage *srcImage = dk->framebuffers ? &dk->framebuffers[dk->current_slot] : NULL;
    if (!srcImage) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: no framebuffer");
        return;
    }

    /* Create new texture with specified dimensions */
    DkImageLayoutMaker layoutMaker;
    dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
    layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layoutMaker.format = dk_convert_format(internalformat, GL_RGBA, GL_UNSIGNED_BYTE);
    layoutMaker.dimensions[0] = width;
    layoutMaker.dimensions[1] = height;
    layoutMaker.dimensions[2] = 1;
    layoutMaker.mipLevels = 1;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layoutMaker);

    uint64_t texSize = dkImageLayoutGetSize(&layout);
    uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

    /* Align texture offset */
    uint32_t aligned_offset = (dk->texture_offset + texAlign - 1) & ~(texAlign - 1);
    if (aligned_offset + texSize > SGL_TEXTURE_MEM_SIZE) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: texture memory overflow");
        return;
    }

    DkImage *texImage = &dk->textures[handle];
    dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
    dk->texture_offset = aligned_offset + texSize;
    dk->texture_initialized[handle] = true;

    /* Store texture dimensions */
    dk->texture_width[handle] = width;
    dk->texture_height[handle] = height;
    dk->texture_mip_levels[handle] = 1;
    dk->texture_format[handle] = layoutMaker.format;

    /* Initialize default sampler parameters */
    dk->texture_min_filter[handle] = GL_NEAREST;
    dk->texture_mag_filter[handle] = GL_LINEAR;
    dk->texture_wrap_s[handle] = GL_REPEAT;
    dk->texture_wrap_t[handle] = GL_REPEAT;

    /* Create image descriptor */
    DkImageView imageView;
    dkImageViewDefaults(&imageView, texImage);
    DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(imgDesc, &imageView, false, false);

    /* Add barrier before copy to ensure framebuffer rendering is complete */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    /* Copy from framebuffer to texture
     * OpenGL Y=0 is bottom, deko3d Y=0 is top
     * Framebuffer height is 720 (SCREEN_HEIGHT) */
    uint32_t fb_height = 720;  /* TODO: get actual framebuffer height */
    uint32_t dk_src_y = fb_height - (uint32_t)y - (uint32_t)height;

    DkImageView srcView, dstView;
    dkImageViewDefaults(&srcView, srcImage);
    dkImageViewDefaults(&dstView, texImage);

    DkImageRect srcRect = { (uint32_t)x, dk_src_y, 0, (uint32_t)width, (uint32_t)height, 1 };
    DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

    /* Use BlitImage for potential format conversion */
    dkCmdBufBlitImage(dk->cmdbuf, &srcView, &srcRect, &dstView, &dstRect, 0, 0);

    /* Submit and wait for copy to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Reset command buffer */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dk->descriptors_bound = false;

    /* Re-bind render target */
    if (dk->framebuffers) {
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

    SGL_TRACE_TEXTURE("copy_tex_image_2d handle=%u (%d,%d) %dx%d", handle, x, y, width, height);
}

/* ============================================================================
 * Copy Framebuffer to Texture Sub-Region (glCopyTexSubImage2D)
 * ============================================================================ */

void dk_copy_tex_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                              GLenum target, GLint level,
                              GLint xoffset, GLint yoffset,
                              GLint x, GLint y, GLsizei width, GLsizei height) {
    (void)target;
    (void)level;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (!dk->texture_initialized[handle]) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: texture %u not initialized", handle);
        return;
    }
    if (width <= 0 || height <= 0) return;

    /* Get current render target (source) */
    DkImage *srcImage = dk->framebuffers ? &dk->framebuffers[dk->current_slot] : NULL;
    if (!srcImage) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: no framebuffer");
        return;
    }

    DkImage *texImage = &dk->textures[handle];
    uint32_t tex_height = dk->texture_height[handle];

    /* Add barrier before copy to ensure framebuffer rendering is complete */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    /* Copy from framebuffer to texture
     * OpenGL Y=0 is bottom, deko3d Y=0 is top */
    uint32_t fb_height = 720;  /* TODO: get actual framebuffer height */
    uint32_t dk_src_y = fb_height - (uint32_t)y - (uint32_t)height;
    uint32_t dk_dst_y = tex_height - (uint32_t)yoffset - (uint32_t)height;

    DkImageView srcView, dstView;
    dkImageViewDefaults(&srcView, srcImage);
    dkImageViewDefaults(&dstView, texImage);

    DkImageRect srcRect = { (uint32_t)x, dk_src_y, 0, (uint32_t)width, (uint32_t)height, 1 };
    DkImageRect dstRect = { (uint32_t)xoffset, dk_dst_y, 0, (uint32_t)width, (uint32_t)height, 1 };

    /* Use BlitImage for potential format conversion */
    dkCmdBufBlitImage(dk->cmdbuf, &srcView, &srcRect, &dstView, &dstRect, 0, 0);

    /* Submit and wait for copy to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Reset command buffer */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dk->descriptors_bound = false;

    /* Re-bind render target */
    if (dk->framebuffers) {
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

    SGL_TRACE_TEXTURE("copy_tex_sub_image_2d handle=%u fb(%d,%d)->tex(%d,%d) %dx%d",
                      handle, x, y, xoffset, yoffset, width, height);
}

/* ============================================================================
 * Compressed Texture Operations
 * ============================================================================ */

/**
 * Upload compressed texture data (glCompressedTexImage2D).
 *
 * The compressed data is uploaded directly to GPU memory - no decompression
 * is needed as the GPU handles compressed texture sampling natively.
 *
 * @param be            Backend pointer
 * @param handle        Texture handle
 * @param target        Texture target (GL_TEXTURE_2D)
 * @param level         Mipmap level (0 for base)
 * @param internalformat Compressed format (GL_COMPRESSED_RGBA_ASTC_4x4_KHR, etc.)
 * @param width         Texture width
 * @param height        Texture height
 * @param imageSize     Size of compressed data in bytes
 * @param data          Compressed texture data
 */
void dk_compressed_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                                     GLenum target, GLint level, GLenum internalformat,
                                     GLsizei width, GLsizei height,
                                     GLsizei imageSize, const void *data) {
    (void)target;
    (void)level;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;

    /* Convert GL compressed format to deko3d format */
    DkImageFormat dkFormat = dk_convert_compressed_format(internalformat);
    if (dkFormat == 0) {
        SGL_ERROR_TEXTURE("Unsupported compressed format 0x%X", internalformat);
        return;
    }

    /* Create DkImage with compressed format */
    DkImageLayoutMaker layoutMaker;
    dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
    layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layoutMaker.format = dkFormat;
    layoutMaker.type = DkImageType_2D;
    layoutMaker.dimensions[0] = width;
    layoutMaker.dimensions[1] = height;
    layoutMaker.dimensions[2] = 1;
    layoutMaker.mipLevels = 1;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layoutMaker);

    uint32_t imageAlign = dkImageLayoutGetAlignment(&layout);
    uint32_t imageSize_layout = dkImageLayoutGetSize(&layout);

    /* Align texture offset */
    dk->texture_offset = (dk->texture_offset + imageAlign - 1) & ~(imageAlign - 1);

    /* Check memory availability */
    if (dk->texture_offset + imageSize_layout > SGL_TEXTURE_MEM_SIZE) {
        SGL_ERROR_TEXTURE("Compressed texture memory exhausted (need %u, have %u)",
                         imageSize_layout, SGL_TEXTURE_MEM_SIZE - dk->texture_offset);
        return;
    }

    /* Create image */
    DkImage *texImage = &dk->textures[handle];
    dkImageInitialize(texImage, &layout, dk->texture_memblock, dk->texture_offset);
    dk->texture_offset += imageSize_layout;

    /* Upload compressed data if provided */
    if (data && imageSize > 0) {
        /* Get staging memory */
        void *staging = dkMemBlockGetCpuAddr(dk->data_memblock);
        if (!staging) {
            SGL_ERROR_TEXTURE("Failed to get CPU addr for compressed texture upload");
            return;
        }

        /* Copy compressed data to staging */
        memcpy(staging, data, imageSize);

        /* Copy from staging to texture */
        DkImageView srcView, dstView;
        dkImageViewDefaults(&srcView, texImage);
        dkImageViewDefaults(&dstView, texImage);

        DkCopyBuf srcBuf;
        srcBuf.addr = dkMemBlockGetGpuAddr(dk->data_memblock);
        srcBuf.rowLength = 0;  /* Tightly packed */
        srcBuf.imageHeight = 0;

        dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, NULL, 0);
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    }

    /* Create image descriptor */
    DkImageView texView;
    dkImageViewDefaults(&texView, texImage);
    DkImageDescriptor *desc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(desc, &texView, false, false);

    /* Store texture info */
    dk->texture_initialized[handle] = true;
    dk->texture_is_cubemap[handle] = false;
    dk->texture_width[handle] = width;
    dk->texture_height[handle] = height;
    dk->texture_format[handle] = dkFormat;
    dk->texture_mip_levels[handle] = 1;

    /* Initialize default sampler parameters */
    dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;
    dk->texture_mag_filter[handle] = GL_LINEAR;
    dk->texture_wrap_s[handle] = GL_REPEAT;
    dk->texture_wrap_t[handle] = GL_REPEAT;

    SGL_TRACE_TEXTURE("compressed_texture_image_2d handle=%u %dx%d format=0x%X size=%d",
                      handle, width, height, internalformat, imageSize);
}

/**
 * Update a region of a compressed texture (glCompressedTexSubImage2D).
 *
 * @param be            Backend pointer
 * @param handle        Texture handle
 * @param target        Texture target (GL_TEXTURE_2D)
 * @param level         Mipmap level
 * @param xoffset       X offset in texels (must be block-aligned)
 * @param yoffset       Y offset in texels (must be block-aligned)
 * @param width         Width in texels (must be block-aligned or reach edge)
 * @param height        Height in texels (must be block-aligned or reach edge)
 * @param format        Compressed format
 * @param imageSize     Size of compressed data in bytes
 * @param data          Compressed texture data
 */
void dk_compressed_texture_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                                         GLenum target, GLint level,
                                         GLint xoffset, GLint yoffset,
                                         GLsizei width, GLsizei height,
                                         GLenum format, GLsizei imageSize, const void *data) {
    (void)target;
    (void)level;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (!dk->texture_initialized[handle]) return;
    if (!data || imageSize <= 0) return;

    /* Get block dimensions */
    int blockWidth, blockHeight;
    dk_get_compressed_block_size(format, &blockWidth, &blockHeight);

    /* Calculate destination region in blocks */
    DkImage *texImage = &dk->textures[handle];

    /* Copy compressed data to staging */
    void *staging = dkMemBlockGetCpuAddr(dk->data_memblock);
    if (!staging) return;

    memcpy(staging, data, imageSize);

    /* Copy from staging to texture region */
    DkImageView dstView;
    dkImageViewDefaults(&dstView, texImage);

    DkCopyBuf srcBuf;
    srcBuf.addr = dkMemBlockGetGpuAddr(dk->data_memblock);
    srcBuf.rowLength = 0;
    srcBuf.imageHeight = 0;

    DkImageRect dstRect;
    dstRect.x = xoffset;
    dstRect.y = yoffset;
    dstRect.z = 0;
    dstRect.width = width;
    dstRect.height = height;
    dstRect.depth = 1;

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

    SGL_TRACE_TEXTURE("compressed_texture_sub_image_2d handle=%u offset(%d,%d) %dx%d size=%d",
                      handle, xoffset, yoffset, width, height, imageSize);
}
