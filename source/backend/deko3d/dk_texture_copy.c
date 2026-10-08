/*
 * SwitchGLES - deko3d Backend - Copy Framebuffer to Texture
 *
 * glCopyTexImage2D / glCopyTexSubImage2D (GPU->CPU->GPU roundtrip).
 *
 * Split out of dk_texture.c. Shared helpers/macros live in
 * dk_texture_internal.h.
 */

#include "dk_internal.h"
#include "../../context/sgl_context.h"
#include "dk_texture_internal.h"

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

void dk_copy_tex_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLint level,
                          GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    bool is_cubemap_face = dk_is_cubemap_face(target);

    if (handle == 0 || handle >= SGL_MAX_TEXTURES)
        return;
    if (width <= 0 || height <= 0)
        return;

    /* Drain a still-in-flight submitted frame before recording into cmdbuf */
    dk_ensure_recordable(dk);

    /* Get current render target - check FBO binding (like dk_read_pixels).
     * Use type flag to pick correct array (avoids renderbuffer/texture ID collision). */
    DkImage *srcImage = NULL;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0) {
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
    }
    if (!srcImage && dk->framebuffers) {
        srcImage = &dk->framebuffers[dk->current_slot];
    }
    if (!srcImage) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: no framebuffer");
        return;
    }

    /* === Step 1: Finish() — submit pending rendering, wait for idle ===
     * GLOVE pattern: rendering MUST be fully completed in a SEPARATE
     * submission before the readback begins. Not just a barrier. */
    dk_flush_sync(dk);

    dk_cmdbuf_clear(dk, dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    /* === Step 2: Read framebuffer to CPU-accessible memory ===
     * Same approach as dk_read_pixels (proven to work). */
    size_t pixelBufSize = (size_t)width * (size_t)height * 4;
    size_t alignedBufSize = SGL_ALIGN_UP(pixelBufSize, SGL_PAGE_ALIGNMENT); /* 4KB align */

    DkMemBlock readbackMem;
    DkMemBlockMaker memMaker;
    dkMemBlockMakerDefaults(&memMaker, dk->device, alignedBufSize);
    memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    readbackMem = dkMemBlockCreate(&memMaker);
    if (!readbackMem) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: failed to allocate readback buffer");
        return;
    }

    /* With DkDeviceFlags_OriginLowerLeft, CopyImageToBuffer uses GL-style
     * coordinates where y=0 is at the bottom — same as dk_read_pixels.
     * No Y-flip needed in the srcRect. */
    uint32_t dk_src_y = (uint32_t)y;

    /* Readback in a separate command list (GLOVE uses auxiliary command buffer) */
    dk_barrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    DkImageView srcView;
    dkImageViewDefaults(&srcView, srcImage);

    /* Use width*4 as rowLength — matches dk_read_pixels (no extra alignment) */
    DkImageRect srcRect = {(uint32_t)x, dk_src_y, 0, (uint32_t)width, (uint32_t)height, 1};
    DkCopyBuf readbackBuf = {dkMemBlockGetGpuAddr(readbackMem), (uint32_t)(width * 4),
                             (uint32_t)height};

    dkCmdBufCopyImageToBuffer(dk->cmdbuf, &srcView, &srcRect, &readbackBuf, 0);

    dk_flush_sync(dk);

    /* === Step 3: Create/reuse destination texture === */
    DkImage *texImage = &dk->textures[handle];

    if (level == 0) {
        if (is_cubemap_face && !dk->texture_initialized[handle]) {
            /* Cubemap face: create cubemap DkImage on first face (allocates for all 6) */
            uint32_t max_dim = (uint32_t)(width > height ? width : height);
            uint32_t mip_levels = 1;
            uint32_t temp = max_dim;
            while (temp > 1) {
                temp >>= 1;
                mip_levels++;
            }

            DkImageLayoutMaker layoutMaker;
            dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
            layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
            layoutMaker.format =
                dk_convert_format(internalformat, internalformat, GL_UNSIGNED_BYTE);
            layoutMaker.type = DkImageType_Cubemap;
            layoutMaker.dimensions[0] = width;
            layoutMaker.dimensions[1] = height;
            layoutMaker.dimensions[2] = 1;
            layoutMaker.mipLevels = mip_levels;

            DkImageLayout layout;
            dkImageLayoutInitialize(&layout, &layoutMaker);

            uint64_t texSize = dkImageLayoutGetSize(&layout);
            uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

            uint32_t aligned_offset = dk_texture_alloc(dk, texAlign, (uint32_t)texSize);
            if (aligned_offset == UINT32_MAX) {
                SGL_ERROR_BACKEND("copy_tex_image_2d: cubemap texture memory overflow");
                dkMemBlockDestroy(readbackMem);
                return;
            }

            dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
            dk->texture_gpu_offset[handle] = aligned_offset;
            dk->texture_gpu_size[handle] = (uint32_t)texSize;
            dk->texture_initialized[handle] = true;
            dk->texture_is_cubemap[handle] = true;
            dk->cubemap_face_mask[handle] = 0;
            dk->cubemap_needs_barrier[handle] = false;

            dk->texture_width[handle] = width;
            dk->texture_height[handle] = height;
            dk->texture_mip_levels[handle] = mip_levels;
            dk->texture_format[handle] = layoutMaker.format;
            dk->texture_gl_format[handle] = (GLenum)internalformat;
            dk->texture_gl_type[handle] = GL_UNSIGNED_BYTE;

            dk->texture_min_filter[handle] = GL_LINEAR;
            dk->texture_mag_filter[handle] = GL_LINEAR;
            dk->texture_wrap_s[handle] = GL_CLAMP_TO_EDGE;
            dk->texture_wrap_t[handle] = GL_CLAMP_TO_EDGE;

            dk->texture_level_mask[handle] = (1u << 0);
        } else if (!is_cubemap_face) {
            /* 2D texture: create new texture with proper mip allocation */
            uint32_t max_dim = (uint32_t)(width > height ? width : height);
            uint32_t mip_levels = 1;
            uint32_t temp = max_dim;
            while (temp > 1) {
                temp >>= 1;
                mip_levels++;
            }

            DkImageLayoutMaker layoutMaker;
            dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
            layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
            layoutMaker.format =
                dk_convert_format(internalformat, internalformat, GL_UNSIGNED_BYTE);
            layoutMaker.dimensions[0] = width;
            layoutMaker.dimensions[1] = height;
            layoutMaker.dimensions[2] = 1;
            layoutMaker.mipLevels = mip_levels;

            DkImageLayout layout;
            dkImageLayoutInitialize(&layout, &layoutMaker);

            uint64_t texSize = dkImageLayoutGetSize(&layout);
            uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

            uint32_t aligned_offset = dk_texture_alloc(dk, texAlign, (uint32_t)texSize);
            if (aligned_offset == UINT32_MAX) {
                SGL_ERROR_BACKEND("copy_tex_image_2d: texture memory overflow");
                dkMemBlockDestroy(readbackMem);
                return;
            }

            dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
            dk->texture_gpu_offset[handle] = aligned_offset;
            dk->texture_gpu_size[handle] = (uint32_t)texSize;
            dk->texture_initialized[handle] = true;
            dk->texture_is_cubemap[handle] = false;

            dk->texture_width[handle] = width;
            dk->texture_height[handle] = height;
            dk->texture_mip_levels[handle] = mip_levels;
            dk->texture_format[handle] = layoutMaker.format;
            dk->texture_gl_format[handle] = (GLenum)internalformat;
            dk->texture_gl_type[handle] = GL_UNSIGNED_BYTE;

            dk->texture_min_filter[handle] = GL_NEAREST;
            dk->texture_mag_filter[handle] = GL_LINEAR;
            dk->texture_wrap_s[handle] = GL_REPEAT;
            dk->texture_wrap_t[handle] = GL_REPEAT;

            dk->texture_level_mask[handle] = (1u << 0);
        }
        /* else: cubemap face and texture already initialized — just upload the face */
    } else {
        /* Level > 0: texture must already exist */
        if (!dk->texture_initialized[handle]) {
            SGL_ERROR_BACKEND("copy_tex_image_2d: level %d but texture not initialized", level);
            dkMemBlockDestroy(readbackMem);
            return;
        }
        uint32_t tex_mips = dk->texture_mip_levels[handle];
        if ((uint32_t)level >= tex_mips) {
            dkMemBlockDestroy(readbackMem);
            return;
        }
        uint32_t expected_w = dk->texture_width[handle] >> level;
        uint32_t expected_h = dk->texture_height[handle] >> level;
        if (expected_w < 1)
            expected_w = 1;
        if (expected_h < 1)
            expected_h = 1;
        if ((uint32_t)width != expected_w || (uint32_t)height != expected_h) {
            dkMemBlockDestroy(readbackMem);
            return;
        }
    }

    /* Set up destination image view */
    DkImageView texView;
    dkImageViewDefaults(&texView, texImage);
    if (is_cubemap_face) {
        /* Target specific cubemap face layer */
        texView.type = DkImageType_2D;
        texView.layerOffset = dk_cubemap_face_index(target);
        texView.layerCount = 1;
    }
    if (level > 0) {
        texView.mipLevelOffset = level;
    }
    dk_apply_format_swizzle(&texView, dk->texture_gl_format[handle]);

    /* === Step 4: CPU reads readback data, writes to staging ===
     * With OriginLowerLeft, readback row 0 = GL y=0 (bottom of framebuffer).
     * Readback is always RGBA8. Convert to target format during staging copy. */
    uint8_t *gpuData = (uint8_t *)dkMemBlockGetCpuAddr(readbackMem);
    uint32_t dst_bpp = dk_gl_format_bpp(internalformat, GL_UNSIGNED_BYTE);

    uint32_t aligned_row_size =
        SGL_ALIGN_UP((uint32_t)(width * dst_bpp), DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    uint32_t saved_client_offset = dk->client_array_offset;
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: staging buffer overflow");
        dkMemBlockDestroy(readbackMem);
        return;
    }

    uint8_t *staging =
        (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;

    /* Convert RGBA readback → target format during staging copy */
    size_t src_row_bytes = (size_t)width * 4;
    for (int row = 0; row < height; row++) {
        const uint8_t *src_row = gpuData + row * src_row_bytes;
        uint8_t *dst_row = staging + row * aligned_row_size;
        if (dst_bpp == 4 && internalformat == GL_RGB) {
            /* GL_RGB stored as RGBA8: copy RGB from framebuffer, force A=255.
             * Per GLES2 spec, RGB textures sample with alpha=1.0.
             * Framebuffer alpha may not be 255, so we must fix it here. */
            memcpy(dst_row, src_row, (size_t)width * 4);
            for (int px = 0; px < width; px++)
                dst_row[px * 4 + 3] = 255;
        } else if (dst_bpp == 4) {
            /* RGBA: direct copy */
            memcpy(dst_row, src_row, (size_t)width * 4);
        } else if (dst_bpp == 1 && internalformat == GL_ALPHA) {
            /* GL_ALPHA → R8: extract A channel from RGBA */
            for (int px = 0; px < width; px++)
                dst_row[px] = src_row[px * 4 + 3];
        } else if (dst_bpp == 1) {
            /* GL_LUMINANCE → R8: extract R channel from RGBA */
            for (int px = 0; px < width; px++)
                dst_row[px] = src_row[px * 4 + 0];
        } else if (dst_bpp == 2) {
            /* GL_LUMINANCE_ALPHA → RG8: L=R, A=A */
            for (int px = 0; px < width; px++) {
                dst_row[px * 2 + 0] = src_row[px * 4 + 0]; /* L = R */
                dst_row[px * 2 + 1] = src_row[px * 4 + 3]; /* A = A */
            }
        }
    }

    dk->client_array_offset = stagingOffset + staging_size;

    /* Done with readback buffer */
    dkMemBlockDestroy(readbackMem);

    /* === Step 5: Upload staging to texture (same as dk_texture_image_2d) === */
    dk_cmdbuf_clear(dk, dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    DkGpuAddr stagingAddr =
        dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;
    DkCopyBuf srcBuf = {stagingAddr, aligned_row_size, (uint32_t)height};
    DkImageRect dstRect = {0, 0, 0, (uint32_t)width, (uint32_t)height, 1};

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &texView, &dstRect, 0);

    dk_flush_sync(dk);

    /* === Step 6: Create descriptor AFTER upload completes === */
    if (is_cubemap_face) {
        /* Track cubemap face upload */
        int face_index = dk_cubemap_face_index(target);
        dk->cubemap_face_mask[handle] |= (1 << face_index);
        dk->texture_level_mask[handle] |= (1u << 0);

        /* Create descriptor only when all 6 faces are uploaded (GLOVE pattern) */
        if (dk->cubemap_face_mask[handle] == DK_CUBEMAP_ALL_FACES) {
            DkImageView descView;
            dkImageViewDefaults(&descView, texImage);
            dk_apply_format_swizzle(&descView, dk->texture_gl_format[handle]);
            DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
            dkImageDescriptorInitialize(imgDesc, &descView, false, false);
            dk_write_image_descriptor_to_gpu(dk, handle);
            dk_write_sampler_descriptor_to_gpu(dk, handle);
            DK_ARM_STORE_BARRIER();
            dk->cubemap_needs_barrier[handle] = true;
        }
    } else if (level == 0) {
        /* 2D texture: create descriptor immediately */
        DkImageView descView;
        dkImageViewDefaults(&descView, texImage);
        dk_apply_format_swizzle(&descView, dk->texture_gl_format[handle]);
        DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
        dkImageDescriptorInitialize(imgDesc, &descView, false, false);
        dk_write_image_descriptor_to_gpu(dk, handle);
        dk_write_sampler_descriptor_to_gpu(dk, handle);
    }

    /* CRITICAL: Mark texture as needing a barrier before first sampling.
     * Without invalidating the texture data cache, the sampler may read stale
     * (zero/white) data. The standalone deko3d test proves this barrier is
     * required. */
    dk->texture_used_as_rt[handle] = true;

    /* Track this level as defined for completeness */
    dk->texture_level_mask[handle] |= (1u << level);

    /* === Step 7: Restore command buffer state === */
    dk_cmdbuf_clear(dk, dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    /* Staging data consumed by GPU copy — restore offset to free staging space */
    dk->client_array_offset = saved_client_offset;

    dk_rebind_render_target(dk);

    SGL_TRACE_TEXTURE("copy_tex_image_2d handle=%u target=0x%X (%d,%d) %dx%d%s", handle, target, x,
                      y, width, height, is_cubemap_face ? " (cubemap)" : "");
}

/* ============================================================================
 * Copy Framebuffer to Texture Sub-Region (glCopyTexSubImage2D)
 *
 * Same GPU → CPU → GPU approach as CopyTexImage2D (GLOVE pattern).
 * Writes to a sub-region of an existing texture.
 * ============================================================================ */

void dk_copy_tex_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLint level,
                              GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                              GLsizei height) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES)
        return;
    if (!dk->texture_initialized[handle]) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: texture %u not initialized", handle);
        return;
    }
    if (width <= 0 || height <= 0)
        return;

    /* Drain a still-in-flight submitted frame before recording into cmdbuf */
    dk_ensure_recordable(dk);

    /* Get current render target - check FBO binding (like dk_read_pixels).
     * Use type flag to pick correct array (avoids renderbuffer/texture ID collision). */
    DkImage *srcImage = NULL;
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0) {
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
    }
    if (!srcImage && dk->framebuffers) {
        srcImage = &dk->framebuffers[dk->current_slot];
    }
    if (!srcImage) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: no framebuffer");
        return;
    }

    DkImage *texImage = &dk->textures[handle];

    /* === Step 1: Finish() — submit pending rendering, wait for idle === */
    dk_flush_sync(dk);

    dk_cmdbuf_clear(dk, dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

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

    /* With OriginLowerLeft, CopyImageToBuffer uses GL coordinates — no Y-flip. */
    uint32_t dk_src_y = (uint32_t)y;

    dk_barrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    DkImageView srcView;
    dkImageViewDefaults(&srcView, srcImage);

    DkImageRect srcRect = {(uint32_t)x, dk_src_y, 0, (uint32_t)width, (uint32_t)height, 1};
    DkCopyBuf readbackBuf = {dkMemBlockGetGpuAddr(readbackMem), (uint32_t)(width * 4),
                             (uint32_t)height};

    dkCmdBufCopyImageToBuffer(dk->cmdbuf, &srcView, &srcRect, &readbackBuf, 0);

    dk_flush_sync(dk);

    /* === Step 3: CPU copy from readback to staging with format conversion === */
    uint8_t *gpuData = (uint8_t *)dkMemBlockGetCpuAddr(readbackMem);
    GLenum tex_gl_fmt = dk->texture_gl_format[handle];
    uint32_t dst_bpp = dk_gl_format_bpp(tex_gl_fmt, GL_UNSIGNED_BYTE);

    uint32_t aligned_row_size =
        SGL_ALIGN_UP((uint32_t)(width * dst_bpp), DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    uint32_t saved_client_offset = dk->client_array_offset;
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: staging buffer overflow");
        dkMemBlockDestroy(readbackMem);
        return;
    }

    uint8_t *staging =
        (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;

    /* Convert RGBA readback → target format */
    size_t src_row_bytes = (size_t)width * 4;
    for (int row = 0; row < height; row++) {
        const uint8_t *src_row = gpuData + row * src_row_bytes;
        uint8_t *dst_row = staging + row * aligned_row_size;
        if (dst_bpp == 4 && tex_gl_fmt == GL_RGB) {
            /* GL_RGB stored as RGBA8: force alpha=255 */
            memcpy(dst_row, src_row, (size_t)width * 4);
            for (int px = 0; px < width; px++)
                dst_row[px * 4 + 3] = 255;
        } else if (dst_bpp == 4) {
            memcpy(dst_row, src_row, (size_t)width * 4);
        } else if (dst_bpp == 1 && tex_gl_fmt == GL_ALPHA) {
            for (int px = 0; px < width; px++)
                dst_row[px] = src_row[px * 4 + 3];
        } else if (dst_bpp == 1) {
            for (int px = 0; px < width; px++)
                dst_row[px] = src_row[px * 4 + 0];
        } else if (dst_bpp == 2) {
            for (int px = 0; px < width; px++) {
                dst_row[px * 2 + 0] = src_row[px * 4 + 0];
                dst_row[px * 2 + 1] = src_row[px * 4 + 3];
            }
        }
    }

    dk->client_array_offset = stagingOffset + staging_size;
    dkMemBlockDestroy(readbackMem);

    /* === Step 4: Upload staging to texture sub-region === */
    dk_cmdbuf_clear(dk, dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    DkImageView dstView;
    dkImageViewDefaults(&dstView, texImage);
    /* For cubemap face targets, select the specific face layer */
    if (dk_is_cubemap_face(target) && dk->texture_is_cubemap[handle]) {
        dstView.type = DkImageType_2D;
        dstView.layerOffset = dk_cubemap_face_index(target);
        dstView.layerCount = 1;
    }
    if (level > 0) {
        dstView.mipLevelOffset = level;
    }

    /* Destination Y: GL yoffset maps directly to storage row
     * (same convention as glTexSubImage2D upload) */
    DkGpuAddr stagingAddr =
        dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;
    DkCopyBuf srcBuf = {stagingAddr, aligned_row_size, (uint32_t)height};
    DkImageRect dstRect = {(uint32_t)xoffset, (uint32_t)yoffset, 0,
                           (uint32_t)width,   (uint32_t)height,  1};

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

    dk_flush_sync(dk);

    /* CRITICAL: Mark texture as needing a barrier before next sampling.
     * Same reason as CopyTexImage2D: stale texture data cache.
     * Also recreate the descriptor to ensure consistency after the DMA copy. */
    dk->texture_used_as_rt[handle] = true;

    /* Refresh descriptor after sub-image update (preserve swizzle) */
    DkImageView updatedView;
    dkImageViewDefaults(&updatedView, texImage);
    dk_apply_format_swizzle(&updatedView, dk->texture_gl_format[handle]);
    DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(imgDesc, &updatedView, false, false);
    dk_write_image_descriptor_to_gpu(dk, handle);

    /* === Step 5: Restore command buffer state === */
    dk_cmdbuf_clear(dk, dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    /* Staging data consumed by GPU copy — restore offset to free staging space */
    dk->client_array_offset = saved_client_offset;

    dk_rebind_render_target(dk);

    SGL_TRACE_TEXTURE("copy_tex_sub_image_2d handle=%u fb(%d,%d)->tex(%d,%d) %dx%d", handle, x, y,
                      xoffset, yoffset, width, height);
}
