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

/* deko3d requires linear buffer row strides to be 32-byte aligned */
#define DK_LINEAR_STRIDE_ALIGNMENT 32

/* Bitmask for all 6 cubemap faces uploaded (bits 0-5) */
#define DK_CUBEMAP_ALL_FACES 0x3F

/* ============================================================================
 * GLES2 Format Helpers (Luminance / Alpha / LuminanceAlpha)
 *
 * deko3d stores these as R8 or RG8; DkImageView.swizzle remaps channels
 * so the shader sees the correct RGBA values at zero GPU cost.
 * ============================================================================ */

/* Apply GLES2 format-specific channel swizzle to an image view */
static void dk_apply_format_swizzle(DkImageView *view, GLenum gl_format) {
    switch (gl_format) {
        case GL_LUMINANCE:
            view->swizzle[0] = DkImageSwizzle_Red;   /* R = L */
            view->swizzle[1] = DkImageSwizzle_Red;   /* G = L */
            view->swizzle[2] = DkImageSwizzle_Red;   /* B = L */
            view->swizzle[3] = DkImageSwizzle_One;   /* A = 1.0 */
            break;
        case GL_ALPHA:
            view->swizzle[0] = DkImageSwizzle_Zero;  /* R = 0 */
            view->swizzle[1] = DkImageSwizzle_Zero;  /* G = 0 */
            view->swizzle[2] = DkImageSwizzle_Zero;  /* B = 0 */
            view->swizzle[3] = DkImageSwizzle_Red;   /* A = stored in R channel */
            break;
        case GL_LUMINANCE_ALPHA:
            view->swizzle[0] = DkImageSwizzle_Red;   /* R = L (stored in R) */
            view->swizzle[1] = DkImageSwizzle_Red;   /* G = L */
            view->swizzle[2] = DkImageSwizzle_Red;   /* B = L */
            view->swizzle[3] = DkImageSwizzle_Green; /* A = A (stored in G) */
            break;
        default: break; /* RGBA/RGB: default swizzle is identity */
    }
}

/* Get bytes-per-pixel for staging buffer size computation */
static uint32_t dk_gl_format_bpp(GLenum gl_format) {
    switch (gl_format) {
        case GL_LUMINANCE: case GL_ALPHA: return 1;
        case GL_LUMINANCE_ALPHA: return 2;
        default: return 4; /* RGBA and RGB (RGB expanded to RGBA on upload) */
    }
}

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

    /* Create cubemap GPU image on first face upload (allocates memory for all 6 faces) */
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
        dk->cubemap_face_mask[handle] = 0;  /* No faces uploaded yet */
        dk->cubemap_needs_barrier[handle] = false;

        dk->texture_width[handle] = width;
        dk->texture_height[handle] = height;
        dk->texture_mip_levels[handle] = 1;
        dk->texture_format[handle] = layoutMaker.format;
        dk->texture_gl_format[handle] = (GLenum)internalformat;

        dk->texture_min_filter[handle] = GL_LINEAR;
        dk->texture_mag_filter[handle] = GL_LINEAR;
        dk->texture_wrap_s[handle] = GL_CLAMP_TO_EDGE;
        dk->texture_wrap_t[handle] = GL_CLAMP_TO_EDGE;

        /* NOTE: Descriptor creation is DEFERRED until all 6 faces are uploaded.
         * This follows the GLOVE pattern where GPU resources are fully initialized
         * before creating the sampling descriptor. */

        SGL_TRACE_TEXTURE("cubemap created handle=%u %dx%d (descriptor deferred)", handle, width, height);
    }

    /* Upload face pixels if provided */
    if (pixels) {
        DkImage *texImage = &dk->textures[handle];

        uint32_t bpp = dk_gl_format_bpp(format);
        uint32_t row_size = width * bpp;
        uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
        uint32_t staging_size = aligned_row_size * height;

        uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
        if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
            SGL_ERROR_BACKEND("cubemap staging buffer overflow");
            return;
        }

        uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                           + dk->client_array_base + stagingOffset;
        const uint8_t *src = (const uint8_t*)pixels;

        /* Copy pixels to staging buffer */
        if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
            /* Convert RGB to RGBA (bpp=4 for staging) */
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
            /* RGBA, LUMINANCE, ALPHA, LUMINANCE_ALPHA: copy bpp bytes per pixel */
            for (int y = 0; y < height; y++) {
                memcpy(staging + y * aligned_row_size, src + y * width * bpp, width * bpp);
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

        /* Submit and wait for copy to complete */
        DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
        dkQueueSubmitCommands(dk->queue, cmdlist);
        dkQueueWaitIdle(dk->queue);

        /* Reset command buffer */
        dkCmdBufClear(dk->cmdbuf);
        dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
        dk->descriptors_bound = false;

        /* Track this face as uploaded */
        dk->cubemap_face_mask[handle] |= (1 << face_index);

        SGL_TRACE_TEXTURE("cubemap face %d uploaded handle=%u (mask=0x%02X)",
                          face_index, handle, dk->cubemap_face_mask[handle]);

        /* GLOVE pattern: Create descriptor only after ALL 6 faces are uploaded.
         * This ensures the cubemap image is fully populated before creating
         * the sampling descriptor. */
        if (dk->cubemap_face_mask[handle] == DK_CUBEMAP_ALL_FACES) {
            /* All 6 faces uploaded - create the cubemap image descriptor now */
            DkImageView imageView;
            dkImageViewDefaults(&imageView, texImage);
            imageView.type = DkImageType_Cubemap;
            dk_apply_format_swizzle(&imageView, dk->texture_gl_format[handle]);

            DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
            dkImageDescriptorInitialize(imgDesc, &imageView, false, false);

            /* Mark cubemap as needing L2 cache barrier before first sampling.
             * The DMA copy engine writes directly to DRAM, but the texture sampler
             * reads through L2 cache. Without invalidation, the sampler may read
             * stale (zero) data from L2 instead of the freshly DMA'd face data. */
            dk->cubemap_needs_barrier[handle] = true;
            dk->texture_used_as_rt[handle] = true;  /* Belt-and-suspenders: also set RT flag */

            SGL_TRACE_TEXTURE("cubemap COMPLETE handle=%u - descriptor created, barrier pending",
                              handle);
        }

        dk_rebind_render_target(dk);
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
    dk->texture_gl_format[handle] = (GLenum)internalformat;

    /* Initialize default sampler parameters (GL defaults) */
    dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;  /* GL default */
    dk->texture_mag_filter[handle] = GL_LINEAR;                  /* GL default */
    dk->texture_wrap_s[handle] = GL_REPEAT;
    dk->texture_wrap_t[handle] = GL_REPEAT;

    /* Create image descriptor with format-specific swizzle */
    DkImageView imageView;
    dkImageViewDefaults(&imageView, texImage);
    dk_apply_format_swizzle(&imageView, internalformat);

    DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(imgDesc, &imageView, false, false);

    /* Upload pixel data if provided - use staging buffer and GPU copy like legacy */
    if (pixels) {
        /* Calculate source size with stride alignment (bpp-aware) */
        uint32_t bpp = dk_gl_format_bpp(format);
        uint32_t row_size = width * bpp;
        uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
        uint32_t staging_size = aligned_row_size * height;

        const uint8_t *src = (const uint8_t*)pixels;

        /* Use client array region as staging */
        uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
        if (stagingOffset + staging_size <= dk->uniform_base - dk->client_array_base) {
            uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                               + dk->client_array_base + stagingOffset;

            /* Copy pixel data to staging buffer with proper stride.
             *
             * IMPORTANT: NO Y-flip here!
             * OpenGL texture V=0 samples the bottom of the texture content.
             * deko3d texture V=0 samples the TOP of the texture storage (row 0).
             * By storing GL row 0 (bottom) at storage row 0 (top), deko3d V=0
             * will sample what GL expects at V=0 (bottom content). */
            if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
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
                /* RGBA, LUMINANCE, ALPHA, LUMINANCE_ALPHA: copy bpp bytes per pixel */
                for (int y = 0; y < height; y++) {
                    memcpy(staging + y * aligned_row_size, src + y * width * bpp, width * bpp);
                }
            }

            dk->client_array_offset = stagingOffset + staging_size;

            /* Copy staging to texture */
            DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                                    + dk->client_array_base + stagingOffset;
            DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
            DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

            dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &imageView, &dstRect, 0);

            /* Submit and wait for copy to complete */
            DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
            dkQueueSubmitCommands(dk->queue, cmdlist);
            dkQueueWaitIdle(dk->queue);

            /* Reset command buffer for continued use */
            dkCmdBufClear(dk->cmdbuf);
            dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

            /* Reset descriptors_bound since we cleared the command buffer */
            dk->descriptors_bound = false;

            dk_rebind_render_target(dk);
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

    /* Calculate source size with stride alignment (bpp from stored GL format) */
    uint32_t bpp = dk_gl_format_bpp(dk->texture_gl_format[handle]);
    uint32_t row_size = width * bpp;
    uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    const uint8_t *src = (const uint8_t*)pixels;

    /* Use client array region as staging */
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("texture_sub_image_2d: staging buffer overflow");
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;

    /* Copy pixel data to staging buffer with proper stride.
     * No Y-flip needed - texture storage matches GL row order (see glTexImage2D comment). */
    if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
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
        /* RGBA, LUMINANCE, ALPHA, LUMINANCE_ALPHA: copy bpp bytes per pixel */
        for (int y = 0; y < height; y++) {
            memcpy(staging + y * aligned_row_size, src + y * width * bpp, width * bpp);
        }
    }

    dk->client_array_offset = stagingOffset + staging_size;

    /* Create image view for the existing texture */
    DkImageView imageView;
    dkImageViewDefaults(&imageView, texImage);

    /* For cubemap face targets, select the specific face layer */
    uint32_t dst_z = 0;
    if (dk_is_cubemap_face(target) && dk->texture_is_cubemap[handle]) {
        dst_z = (uint32_t)dk_cubemap_face_index(target);
    }

    /* Copy staging to texture at the specified offset.
     * Since glTexImage2D stores GL row 0 at storage row 0 (no flip),
     * GL yoffset maps directly to storage row offset. */
    uint32_t dk_yoffset = (uint32_t)yoffset;

    DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                            + dk->client_array_base + stagingOffset;
    DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
    /* Note: DkImageRect is { x, y, z, width, height, depth } */
    DkImageRect dstRect = { (uint32_t)xoffset, dk_yoffset, dst_z, (uint32_t)width, (uint32_t)height, 1 };

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &imageView, &dstRect, 0);

    /* Submit and wait for copy to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Reset command buffer for continued use */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Reset descriptors_bound since we cleared the command buffer */
    dk->descriptors_bound = false;

    dk_rebind_render_target(dk);

    SGL_TRACE_TEXTURE("texture_sub_image_2d handle=%u target=0x%X offset=(%d,%d) %dx%d",
                      handle, target, xoffset, yoffset, width, height);
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

    /* Skip binding incomplete cubemaps (not all 6 faces uploaded yet).
     * The descriptor is only created after all 6 faces, so binding an
     * incomplete cubemap would push an uninitialized descriptor. */
    if (dk->texture_is_cubemap[handle] && dk->cubemap_face_mask[handle] != 0x3F) {
        SGL_TRACE_TEXTURE("bind_texture: skipping incomplete cubemap handle=%u (mask=0x%02X)",
                          handle, dk->cubemap_face_mask[handle]);
        return;
    }

    /* Insert barrier if this texture was used as a render target (FBO)
     * or if it's a freshly-completed cubemap needing L2 cache coherency.
     * This avoids expensive full barriers on every texture bind when sampling
     * normal textures that were never rendered to. */
    if (dk->texture_used_as_rt[handle] || dk->cubemap_needs_barrier[handle]) {
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                        DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);
        dk->texture_used_as_rt[handle] = false;
        dk->cubemap_needs_barrier[handle] = false;
    }

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

    /* Convert GL min filter to deko3d (minFilter + mipFilter) */
    switch (dk->texture_min_filter[handle]) {
        case GL_NEAREST:
            sampler.minFilter = DkFilter_Nearest;
            sampler.mipFilter = DkMipFilter_None;
            break;
        case GL_LINEAR:
            sampler.minFilter = DkFilter_Linear;
            sampler.mipFilter = DkMipFilter_None;
            break;
        case GL_NEAREST_MIPMAP_NEAREST:
            sampler.minFilter = DkFilter_Nearest;
            sampler.mipFilter = DkMipFilter_Nearest;
            break;
        case GL_NEAREST_MIPMAP_LINEAR:
            sampler.minFilter = DkFilter_Nearest;
            sampler.mipFilter = DkMipFilter_Linear;
            break;
        case GL_LINEAR_MIPMAP_NEAREST:
            sampler.minFilter = DkFilter_Linear;
            sampler.mipFilter = DkMipFilter_Nearest;
            break;
        case GL_LINEAR_MIPMAP_LINEAR:
        default:
            sampler.minFilter = DkFilter_Linear;
            sampler.mipFilter = DkMipFilter_Linear;
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
 *
 * Based on GLOVE (GL Over Vulkan) pattern: GPU → CPU → GPU.
 * 1. Finish() — submit all pending rendering, wait for GPU idle
 * 2. ReadBack — CopyImageToBuffer to CPU-accessible memory (like glReadPixels)
 * 3. Upload — CPU pixels to staging, CopyBufferToImage (like glTexImage2D)
 *
 * Both readback and upload are individually proven operations in SwitchGLES.
 * Previous attempts using direct GPU→GPU copies (BlitImage, DMA copy) all
 * failed with white textures. The CPU roundtrip avoids GPU copy issues.
 * ============================================================================ */

void dk_copy_tex_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                          GLenum target, GLint level, GLenum internalformat,
                          GLint x, GLint y, GLsizei width, GLsizei height) {
    (void)target;
    (void)level;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (width <= 0 || height <= 0) return;

    /* Get current render target - check FBO binding (like dk_read_pixels) */
    DkImage *srcImage = NULL;
    uint32_t src_height;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0 &&
        dk->current_fbo_color < SGL_MAX_TEXTURES &&
        dk->texture_initialized[dk->current_fbo_color]) {
        srcImage = &dk->textures[dk->current_fbo_color];
        src_height = dk->texture_height[dk->current_fbo_color];
    } else if (dk->framebuffers) {
        srcImage = &dk->framebuffers[dk->current_slot];
        src_height = dk->fb_height;
    }
    if (!srcImage) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: no framebuffer");
        return;
    }

    /* === Step 1: Finish() — submit pending rendering, wait for idle ===
     * GLOVE pattern: rendering MUST be fully completed in a SEPARATE
     * submission before the readback begins. Not just a barrier. */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* === Step 2: Read framebuffer to CPU-accessible memory ===
     * Same approach as dk_read_pixels (proven to work). */
    size_t pixelBufSize = (size_t)width * (size_t)height * 4;
    size_t alignedBufSize = SGL_ALIGN_UP(pixelBufSize, SGL_PAGE_ALIGNMENT);  /* 4KB align */

    DkMemBlock readbackMem;
    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, dk->device, alignedBufSize);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    readbackMem = dkMemBlockCreate(&memMaker);
    if (!readbackMem) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: failed to allocate readback buffer");
        return;
    }

    /* Source Y: GL Y=0 is bottom, storage Y=0 is top */
    uint32_t dk_src_y = src_height - (uint32_t)y - (uint32_t)height;

    /* Readback in a separate command list (GLOVE uses auxiliary command buffer) */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    DkImageView srcView;
    dkImageViewDefaults(&srcView, srcImage);

    /* Use width*4 as rowLength — matches dk_read_pixels (no extra alignment) */
    DkImageRect srcRect = { (uint32_t)x, dk_src_y, 0, (uint32_t)width, (uint32_t)height, 1 };
    DkCopyBuf readbackBuf = { dkMemBlockGetGpuAddr(readbackMem), (uint32_t)(width * 4), (uint32_t)height };

    dkCmdBufCopyImageToBuffer(dk->cmdbuf, &srcView, &srcRect, &readbackBuf, 0);

    cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* === Step 3: Create destination texture === */
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

    uint32_t aligned_offset = (dk->texture_offset + texAlign - 1) & ~(texAlign - 1);
    if (aligned_offset + texSize > SGL_TEXTURE_MEM_SIZE) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: texture memory overflow");
        dkMemBlockDestroy(readbackMem);
        return;
    }

    DkImage *texImage = &dk->textures[handle];
    dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
    dk->texture_offset = aligned_offset + texSize;
    dk->texture_initialized[handle] = true;

    dk->texture_width[handle] = width;
    dk->texture_height[handle] = height;
    dk->texture_mip_levels[handle] = 1;
    dk->texture_format[handle] = layoutMaker.format;
    dk->texture_gl_format[handle] = (GLenum)internalformat;

    dk->texture_min_filter[handle] = GL_NEAREST;
    dk->texture_mag_filter[handle] = GL_LINEAR;
    dk->texture_wrap_s[handle] = GL_REPEAT;
    dk->texture_wrap_t[handle] = GL_REPEAT;

    /* NOTE: Descriptor creation is DEFERRED to after the pixel upload.
     * This follows the proven pattern from the standalone deko3d test where
     * the descriptor is created after CopyBufferToImage completes. */

    DkImageView texView;
    dkImageViewDefaults(&texView, texImage);
    dk_apply_format_swizzle(&texView, internalformat);

    /* === Step 4: CPU reads readback data, Y-flips, writes to staging ===
     * Readback buffer has storage order (top of screen at row 0).
     * We flip rows so row 0 = bottom of captured region = GL y origin.
     * Then upload without flip (same as glTexImage2D convention).
     * GLOVE does this in CopyPixelsToHost → InvertImageYAxis. */
    uint8_t *gpuData = (uint8_t *)dkMemBlockGetCpuAddr(readbackMem);
    size_t row_bytes = (size_t)width * 4;

    uint32_t aligned_row_size = SGL_ALIGN_UP((uint32_t)(width * 4), DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: staging buffer overflow");
        dkMemBlockDestroy(readbackMem);
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;

    /* Copy with Y-flip from readback → staging (matching GLOVE's InvertImageYAxis) */
    for (int row = 0; row < height; row++) {
        memcpy(staging + row * aligned_row_size,
               gpuData + (height - 1 - row) * row_bytes,
               row_bytes);
    }

    dk->client_array_offset = stagingOffset + staging_size;

    /* Done with readback buffer */
    dkMemBlockDestroy(readbackMem);

    /* === Step 5: Upload staging to texture (same as dk_texture_image_2d) === */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                            + dk->client_array_base + stagingOffset;
    DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
    DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &texView, &dstRect, 0);

    cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* === Step 6: Create descriptor AFTER upload completes ===
     * The standalone deko3d test creates the descriptor after CopyBufferToImage.
     * This ensures the image metadata is fully consistent after the DMA copy.
     * Swizzle was already applied to texView above. */
    DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(imgDesc, &texView, false, false);

    /* CRITICAL: Mark texture as needing L2 cache barrier before first sampling.
     * CopyBufferToImage uses the DMA/2D engine which writes directly to DRAM.
     * The 3D engine's texture sampler reads through its own L2 cache.
     * Without invalidation, the sampler may read stale (zero/white) data.
     * The standalone deko3d test proves this barrier is required. */
    dk->texture_used_as_rt[handle] = true;

    /* === Step 7: Restore command buffer state === */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dk->descriptors_bound = false;

    dk_rebind_render_target(dk);

    SGL_TRACE_TEXTURE("copy_tex_image_2d handle=%u (%d,%d) %dx%d", handle, x, y, width, height);
}

/* ============================================================================
 * Copy Framebuffer to Texture Sub-Region (glCopyTexSubImage2D)
 *
 * Same GPU → CPU → GPU approach as CopyTexImage2D (GLOVE pattern).
 * Writes to a sub-region of an existing texture.
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

    /* Get current render target - check FBO binding (like dk_read_pixels) */
    DkImage *srcImage = NULL;
    uint32_t src_height;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0 &&
        dk->current_fbo_color < SGL_MAX_TEXTURES &&
        dk->texture_initialized[dk->current_fbo_color]) {
        srcImage = &dk->textures[dk->current_fbo_color];
        src_height = dk->texture_height[dk->current_fbo_color];
    } else if (dk->framebuffers) {
        srcImage = &dk->framebuffers[dk->current_slot];
        src_height = dk->fb_height;
    }
    if (!srcImage) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: no framebuffer");
        return;
    }

    DkImage *texImage = &dk->textures[handle];

    /* === Step 1: Finish() — submit pending rendering, wait for idle === */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* === Step 2: Read framebuffer to CPU-accessible memory === */
    size_t pixelBufSize = (size_t)width * (size_t)height * 4;
    size_t alignedBufSize = SGL_ALIGN_UP(pixelBufSize, SGL_PAGE_ALIGNMENT);

    DkMemBlock readbackMem;
    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, dk->device, alignedBufSize);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    readbackMem = dkMemBlockCreate(&memMaker);
    if (!readbackMem) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: failed to allocate readback buffer");
        return;
    }

    uint32_t dk_src_y = src_height - (uint32_t)y - (uint32_t)height;

    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    DkImageView srcView;
    dkImageViewDefaults(&srcView, srcImage);

    DkImageRect srcRect = { (uint32_t)x, dk_src_y, 0, (uint32_t)width, (uint32_t)height, 1 };
    DkCopyBuf readbackBuf = { dkMemBlockGetGpuAddr(readbackMem), (uint32_t)(width * 4), (uint32_t)height };

    dkCmdBufCopyImageToBuffer(dk->cmdbuf, &srcView, &srcRect, &readbackBuf, 0);

    cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* === Step 3: CPU Y-flip from readback to staging === */
    uint8_t *gpuData = (uint8_t *)dkMemBlockGetCpuAddr(readbackMem);
    size_t row_bytes = (size_t)width * 4;

    uint32_t aligned_row_size = SGL_ALIGN_UP((uint32_t)(width * 4), DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: staging buffer overflow");
        dkMemBlockDestroy(readbackMem);
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;

    for (int row = 0; row < height; row++) {
        memcpy(staging + row * aligned_row_size,
               gpuData + (height - 1 - row) * row_bytes,
               row_bytes);
    }

    dk->client_array_offset = stagingOffset + staging_size;
    dkMemBlockDestroy(readbackMem);

    /* === Step 4: Upload staging to texture sub-region === */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    DkImageView dstView;
    dkImageViewDefaults(&dstView, texImage);

    /* Destination Y: GL yoffset maps directly to storage row
     * (same convention as glTexSubImage2D upload) */
    DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                            + dk->client_array_base + stagingOffset;
    DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
    DkImageRect dstRect = { (uint32_t)xoffset, (uint32_t)yoffset, 0, (uint32_t)width, (uint32_t)height, 1 };

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

    cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* CRITICAL: Mark texture as needing L2 cache barrier before next sampling.
     * Same reason as CopyTexImage2D: DMA writes bypass the 3D engine's L2 cache.
     * Also recreate the descriptor to ensure consistency after the DMA copy. */
    dk->texture_used_as_rt[handle] = true;

    /* Refresh descriptor after sub-image update (preserve swizzle) */
    DkImageView updatedView;
    dkImageViewDefaults(&updatedView, texImage);
    dk_apply_format_swizzle(&updatedView, dk->texture_gl_format[handle]);
    DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(imgDesc, &updatedView, false, false);

    /* === Step 5: Restore command buffer state === */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dk->descriptors_bound = false;

    dk_rebind_render_target(dk);

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
        /* Use client array region as staging (NOT offset 0 which overwrites VBOs!) */
        uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
        if (stagingOffset + (uint32_t)imageSize <= dk->uniform_base - dk->client_array_base) {
            uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                               + dk->client_array_base + stagingOffset;

            /* Copy compressed data to staging */
            memcpy(staging, data, imageSize);
            dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

            /* Copy from staging to texture */
            DkImageView dstView;
            dkImageViewDefaults(&dstView, texImage);

            DkCopyBuf srcBuf;
            srcBuf.addr = dkMemBlockGetGpuAddr(dk->data_memblock)
                          + dk->client_array_base + stagingOffset;
            srcBuf.rowLength = 0;  /* Tightly packed */
            srcBuf.imageHeight = 0;

            dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, NULL, 0);
            dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
        } else {
            SGL_ERROR_TEXTURE("Compressed texture staging memory exhausted");
        }
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
    (void)format;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (!dk->texture_initialized[handle]) return;
    if (!data || imageSize <= 0) return;

    DkImage *texImage = &dk->textures[handle];

    /* Use client array region as staging (NOT offset 0 which overwrites VBOs!) */
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + (uint32_t)imageSize > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_TEXTURE("Compressed sub-image staging memory exhausted");
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;
    memcpy(staging, data, imageSize);
    dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

    /* Copy from staging to texture region */
    DkImageView dstView;
    dkImageViewDefaults(&dstView, texImage);

    DkCopyBuf srcBuf;
    srcBuf.addr = dkMemBlockGetGpuAddr(dk->data_memblock)
                  + dk->client_array_base + stagingOffset;
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
