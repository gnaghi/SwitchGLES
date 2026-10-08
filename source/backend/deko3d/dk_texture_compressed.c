/*
 * SwitchGLES - deko3d Backend - Compressed Texture Operations
 *
 * glCompressedTexImage2D / glCompressedTexSubImage2D.
 *
 * Split out of dk_texture.c. Shared helpers/macros live in
 * dk_texture_internal.h.
 */

#include "dk_internal.h"
#include "../../context/sgl_context.h"
#include "dk_texture_internal.h"

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
void dk_compressed_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target,
                                    GLint level, GLenum internalformat, GLsizei width,
                                    GLsizei height, GLsizei imageSize, const void *data) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES)
        return;

    /* Drain a still-in-flight submitted frame before recording into cmdbuf */
    dk_ensure_recordable(dk);

    /* Convert GL compressed format to deko3d format */
    DkImageFormat dkFormat = dk_convert_compressed_format(internalformat);
    if (dkFormat == 0) {
        SGL_ERROR_TEXTURE("Unsupported compressed format 0x%X", internalformat);
        return;
    }

    /* === Cubemap faces: create DkImage as Cubemap type === */
    if (dk_is_cubemap_face(target)) {
        int face_index = dk_cubemap_face_index(target);

        /* Create cubemap GPU image on first face upload */
        if (!dk->texture_initialized[handle]) {
            DkImageLayoutMaker layoutMaker;
            dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
            layoutMaker.flags = 0; /* Compressed formats NOT renderable */
            layoutMaker.format = dkFormat;
            layoutMaker.type = DkImageType_Cubemap;
            layoutMaker.dimensions[0] = width;
            layoutMaker.dimensions[1] = height;
            layoutMaker.dimensions[2] = 1;

            uint32_t max_dim = (uint32_t)(width > height ? width : height);
            uint32_t mip_levels = 1;
            uint32_t temp = max_dim;
            while (temp > 1) {
                temp >>= 1;
                mip_levels++;
            }
            layoutMaker.mipLevels = mip_levels;

            DkImageLayout layout;
            dkImageLayoutInitialize(&layout, &layoutMaker);
            uint64_t texSize = dkImageLayoutGetSize(&layout);
            uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

            uint32_t aligned_offset = dk_texture_alloc(dk, texAlign, (uint32_t)texSize);
            if (aligned_offset == UINT32_MAX) {
                SGL_ERROR_BACKEND("Compressed cubemap texture memory overflow");
                return;
            }

            DkImage *texImage = &dk->textures[handle];
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
            dk->texture_format[handle] = dkFormat;
            dk->texture_gl_format[handle] = internalformat;
            dk->texture_gl_type[handle] = 0;

            dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;
            dk->texture_mag_filter[handle] = GL_LINEAR;
            dk->texture_wrap_s[handle] = GL_REPEAT;
            dk->texture_wrap_t[handle] = GL_REPEAT;

            SGL_TRACE_TEXTURE("compressed cubemap created handle=%u %dx%d mips=%u", handle, width,
                              height, mip_levels);
        }

        /* Validate format and dimension consistency for cubemap faces */
        if (level == 0) {
            if (dkFormat != dk->texture_format[handle] ||
                internalformat != dk->texture_gl_format[handle] ||
                (uint32_t)width != dk->texture_width[handle] ||
                (uint32_t)height != dk->texture_height[handle]) {
                return; /* Mismatch → cubemap incomplete */
            }
        }

        /* Upload compressed data to the correct cubemap face */
        if (data && imageSize > 0) {
            DkImage *texImage = &dk->textures[handle];
            uint32_t saved_client_offset = dk->client_array_offset;
            uint32_t stagingOffset =
                SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
            if (stagingOffset + (uint32_t)imageSize <= dk->uniform_base - dk->client_array_base) {
                uint8_t *staging = (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) +
                                   dk->client_array_base + stagingOffset;
                memcpy(staging, data, imageSize);
                dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

                DkImageView dstView;
                dkImageViewDefaults(&dstView, texImage);
                dstView.layerOffset = face_index;
                if (level > 0)
                    dstView.mipLevelOffset = level;

                DkCopyBuf srcBuf;
                srcBuf.addr =
                    dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;
                srcBuf.rowLength = 0;
                srcBuf.imageHeight = 0;

                DkImageRect dstRect = {0, 0, 0, (uint32_t)width, (uint32_t)height, 1};
                dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

                dk_flush_sync(dk);

                dkCmdBufClear(dk->cmdbuf);
                dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0,
                                  SGL_CMD_MEM_SIZE);
                dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr,
                                               SGL_MAX_TEXTURES);
                dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr,
                                                 SGL_MAX_TEXTURES);
                dk->descriptors_bound = true;

                dk_rebind_render_target(dk);
                dk->client_array_offset = saved_client_offset;
            } else {
                SGL_ERROR_TEXTURE("Compressed cubemap staging memory exhausted");
            }
        }

        /* Track face and level */
        if (level == 0) {
            dk->cubemap_face_mask[handle] |= (1 << face_index);
        }
        dk->texture_level_mask[handle] |= (1u << level);

        /* Create descriptor when all 6 faces uploaded */
        if (dk->cubemap_face_mask[handle] == 0x3F) {
            DkImage *texImage = &dk->textures[handle];
            DkImageView texView;
            dkImageViewDefaults(&texView, texImage);
            texView.type = DkImageType_Cubemap;
            dk_apply_format_swizzle(&texView, dk->texture_gl_format[handle]);
            DkImageDescriptor *desc = &dk->texture_descriptors[handle];
            dkImageDescriptorInitialize(desc, &texView, false, false);
            dk_write_image_descriptor_to_gpu(dk, handle);
            dk_write_sampler_descriptor_to_gpu(dk, handle);

            /* Mark cubemap as needing a barrier before first sampling:
             * invalidate the texture data and descriptor caches, which may hold
             * stale (zero) data for the freshly copied faces. The copy engine
             * goes through the L2, and the upload's WaitIdle already flushed it. */
            dk->cubemap_needs_barrier[handle] = true;
            dk->texture_used_as_rt[handle] = true;
        }

        SGL_TRACE_TEXTURE("compressed cubemap face %d handle=%u level=%d %dx%d", face_index, handle,
                          level, width, height);
        return;
    }

    /* === Mip level > 0: upload to existing 2D texture === */
    if (level > 0) {
        if (!dk->texture_initialized[handle]) {
            return; /* No base level yet — skip silently */
        }
        uint32_t tex_mips = dk->texture_mip_levels[handle];
        if ((uint32_t)level >= tex_mips) {
            return; /* Level out of range — texture incomplete, skip */
        }
        /* Validate dimensions match expected mip size */
        uint32_t expected_w = dk->texture_width[handle] >> level;
        uint32_t expected_h = dk->texture_height[handle] >> level;
        if (expected_w < 1)
            expected_w = 1;
        if (expected_h < 1)
            expected_h = 1;
        if ((uint32_t)width != expected_w || (uint32_t)height != expected_h) {
            return; /* Dimension mismatch — texture incomplete, skip */
        }
        /* Validate format matches base texture */
        if (dkFormat != dk->texture_format[handle]) {
            return;
        }
        /* Upload compressed data to specific mip level */
        if (data && imageSize > 0) {
            DkImage *texImage = &dk->textures[handle];
            uint32_t saved_client_offset = dk->client_array_offset;
            uint32_t stagingOffset =
                SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
            if (stagingOffset + (uint32_t)imageSize <= dk->uniform_base - dk->client_array_base) {
                uint8_t *staging = (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) +
                                   dk->client_array_base + stagingOffset;
                memcpy(staging, data, imageSize);
                dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

                DkImageView dstView;
                dkImageViewDefaults(&dstView, texImage);
                dstView.mipLevelOffset = level;

                DkCopyBuf srcBuf;
                srcBuf.addr =
                    dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;
                srcBuf.rowLength = 0;
                srcBuf.imageHeight = 0;

                DkImageRect dstRect = {0, 0, 0, (uint32_t)width, (uint32_t)height, 1};
                dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

                /* Flush GPU for mip upload (same pattern as non-compressed mips) */
                dk_flush_sync(dk);

                dkCmdBufClear(dk->cmdbuf);
                dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0,
                                  SGL_CMD_MEM_SIZE);
                dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr,
                                               SGL_MAX_TEXTURES);
                dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr,
                                                 SGL_MAX_TEXTURES);
                dk->descriptors_bound = true;

                dk->client_array_offset = saved_client_offset;
                dk_rebind_render_target(dk);
            }
        }
        dk->texture_level_mask[handle] |= (1u << level);
        SGL_TRACE_TEXTURE("compressed_texture_image_2d handle=%u mip level %d %dx%d", handle, level,
                          width, height);
        return;
    }

    /* === Level 0: Create new compressed texture with all mip levels === */

    /* Calculate number of mip levels from dimensions */
    uint32_t max_dim = (uint32_t)(width > height ? width : height);
    uint32_t mip_levels = 1;
    uint32_t temp = max_dim;
    while (temp > 1) {
        temp >>= 1;
        mip_levels++;
    }

    DkImageLayoutMaker layoutMaker;
    dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
    /* Compressed formats are NOT renderable — don't set UsageRender.
     * The copy engine (CopyBufferToImage) doesn't need Usage2DEngine. */
    layoutMaker.flags = 0;
    layoutMaker.format = dkFormat;
    layoutMaker.type = DkImageType_2D;
    layoutMaker.dimensions[0] = width;
    layoutMaker.dimensions[1] = height;
    layoutMaker.dimensions[2] = 1;
    layoutMaker.mipLevels = mip_levels;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layoutMaker);

    uint32_t imageAlign = dkImageLayoutGetAlignment(&layout);
    uint32_t imageSize_layout = dkImageLayoutGetSize(&layout);

    uint32_t aligned_offset = dk_texture_alloc(dk, imageAlign, imageSize_layout);
    if (aligned_offset == UINT32_MAX) {
        SGL_ERROR_TEXTURE("Compressed texture memory exhausted (need %u)", imageSize_layout);
        return;
    }

    /* Create image */
    DkImage *texImage = &dk->textures[handle];
    dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
    dk->texture_gpu_offset[handle] = aligned_offset;
    dk->texture_gpu_size[handle] = imageSize_layout;

    /* Upload compressed data if provided */
    if (data && imageSize > 0) {
        /* Save staging offset — restore after GPU copy (staging is temporary) */
        uint32_t saved_client_offset = dk->client_array_offset;
        uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
        if (stagingOffset + (uint32_t)imageSize <= dk->uniform_base - dk->client_array_base) {
            uint8_t *staging = (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) +
                               dk->client_array_base + stagingOffset;

            /* Copy compressed data to staging */
            memcpy(staging, data, imageSize);
            dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

            /* Copy from staging to texture */
            DkImageView dstView;
            dkImageViewDefaults(&dstView, texImage);

            DkCopyBuf srcBuf;
            srcBuf.addr =
                dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;
            srcBuf.rowLength = 0; /* Tightly packed */
            srcBuf.imageHeight = 0;

            DkImageRect dstRect = {0, 0, 0, (uint32_t)width, (uint32_t)height, 1};
            dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

            /* Submit and wait for copy to complete — MUST happen before
             * restoring client_array_offset, otherwise subsequent mip uploads
             * overwrite the staging data before the GPU copies level 0. */
            dk_flush_sync(dk);

            dkCmdBufClear(dk->cmdbuf);
            dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0,
                              SGL_CMD_MEM_SIZE);
            dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
            dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr,
                                             SGL_MAX_TEXTURES);
            dk->descriptors_bound = true;

            dk_rebind_render_target(dk);
        } else {
            SGL_ERROR_TEXTURE("Compressed texture staging memory exhausted");
        }
        /* Staging data consumed by GPU copy — restore offset to free staging space */
        dk->client_array_offset = saved_client_offset;
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
    dk->texture_gl_format[handle] = internalformat;
    dk->texture_gl_type[handle] = 0; /* Compressed — no GL type */
    dk->texture_mip_levels[handle] = mip_levels;

    /* Initialize default sampler parameters */
    dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;
    dk->texture_mag_filter[handle] = GL_LINEAR;
    dk->texture_wrap_s[handle] = GL_REPEAT;
    dk->texture_wrap_t[handle] = GL_REPEAT;

    dk->texture_level_mask[handle] = (1u << 0);

    /* Write descriptors to GPU memory */
    dk_write_image_descriptor_to_gpu(dk, handle);
    dk_write_sampler_descriptor_to_gpu(dk, handle);

    SGL_TRACE_TEXTURE("compressed_texture_image_2d handle=%u %dx%d mips=%u format=0x%X size=%d",
                      handle, width, height, mip_levels, internalformat, imageSize);
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
void dk_compressed_texture_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target,
                                        GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                        GLsizei height, GLenum format, GLsizei imageSize,
                                        const void *data) {
    (void)target;
    (void)level;
    (void)format;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES)
        return;
    if (!dk->texture_initialized[handle])
        return;
    if (!data || imageSize <= 0)
        return;

    /* Drain a still-in-flight submitted frame before recording into cmdbuf */
    dk_ensure_recordable(dk);

    DkImage *texImage = &dk->textures[handle];

    /* Staging stays allocated until the slot's client-array region is reset
     * (frame start after the slot fence, or a WaitIdle): the copy below is
     * only recorded, not executed, so the next draw must not reuse it. */
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + (uint32_t)imageSize > dk->client_array_slot_end) {
        SGL_ERROR_TEXTURE("Compressed sub-image staging memory exhausted");
        return;
    }

    uint8_t *staging =
        (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;
    memcpy(staging, data, imageSize);
    dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

    /* Copy from staging to texture region */
    DkImageView dstView;
    dkImageViewDefaults(&dstView, texImage);

    DkCopyBuf srcBuf;
    srcBuf.addr = dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + stagingOffset;
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
    dk_barrier(dk->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

    SGL_TRACE_TEXTURE("compressed_texture_sub_image_2d handle=%u offset(%d,%d) %dx%d size=%d",
                      handle, xoffset, yoffset, width, height, imageSize);
}
