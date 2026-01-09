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
#include "../../context/sgl_context.h"

/* deko3d requires linear buffer row strides to be 32-byte aligned */
#define DK_LINEAR_STRIDE_ALIGNMENT 32

/* Compute source row stride accounting for GL_UNPACK_ALIGNMENT.
 * Per GLES2 spec, each row starts at a multiple of unpack_alignment bytes. */
static inline uint32_t dk_src_row_stride(int width, int bpp) {
    sgl_context_t *ctx = sgl_get_current_context();
    int align = (ctx && ctx->unpack_alignment > 0) ? ctx->unpack_alignment : 4;
    uint32_t raw = (uint32_t)(width * bpp);
    return (raw + align - 1) & ~((uint32_t)align - 1);
}

/* Swizzle BGRA → RGBA (swap B and R channels, 4 bytes per pixel).
 * Used when GL_BGRA_EXT textures are stored as RGBA8 internally. */
static void dk_swizzle_bgra_to_rgba(uint8_t *staging, const uint8_t *src,
                                      int width, int height,
                                      uint32_t aligned_row_size) {
    for (int y = 0; y < height; y++) {
        uint8_t *dst_row = staging + y * aligned_row_size;
        const uint8_t *src_row = src + y * dk_src_row_stride(width, 4);
        for (int x = 0; x < width; x++) {
            dst_row[x * 4 + 0] = src_row[x * 4 + 2]; /* R ← B */
            dst_row[x * 4 + 1] = src_row[x * 4 + 1]; /* G ← G */
            dst_row[x * 4 + 2] = src_row[x * 4 + 0]; /* B ← R */
            dst_row[x * 4 + 3] = src_row[x * 4 + 3]; /* A ← A */
        }
    }
}

/* Expand RGB half-float (3x fp16) to RGBA half-float (4x fp16, alpha=1.0).
 * IEEE 754 half-float 1.0 = 0x3C00. */
static void dk_expand_rgb16f_to_rgba16f(uint8_t *staging, const uint8_t *src,
                                          int width, int height,
                                          uint32_t aligned_row_size) {
    const uint16_t one_fp16 = 0x3C00;
    for (int y = 0; y < height; y++) {
        uint16_t *dst_row = (uint16_t *)(staging + y * aligned_row_size);
        const uint16_t *src_row = (const uint16_t *)(src + y * dk_src_row_stride(width, 6));
        for (int x = 0; x < width; x++) {
            dst_row[x * 4 + 0] = src_row[x * 3 + 0];
            dst_row[x * 4 + 1] = src_row[x * 3 + 1];
            dst_row[x * 4 + 2] = src_row[x * 3 + 2];
            dst_row[x * 4 + 3] = one_fp16;
        }
    }
}

/* Unpack packed 16-bit pixel formats to RGBA8.
 * Returns true if unpacking was performed, false if not a packed format. */
static bool dk_unpack_packed_to_rgba8(uint8_t *staging, const uint8_t *src,
                                       int width, int height,
                                       uint32_t aligned_row_size,
                                       GLenum format, GLenum type) {
    if (type == GL_UNSIGNED_SHORT_5_6_5 && format == GL_RGB) {
        for (int y = 0; y < height; y++) {
            uint8_t *dst_row = staging + y * aligned_row_size;
            const uint16_t *src_row = (const uint16_t*)(src + y * dk_src_row_stride(width, 2));
            for (int x = 0; x < width; x++) {
                uint16_t p = src_row[x];
                uint8_t r = (uint8_t)((p >> 11) & 0x1F);
                uint8_t g = (uint8_t)((p >> 5) & 0x3F);
                uint8_t b = (uint8_t)(p & 0x1F);
                dst_row[x*4+0] = (r << 3) | (r >> 2);  /* expand 5-bit to 8-bit */
                dst_row[x*4+1] = (g << 2) | (g >> 4);  /* expand 6-bit to 8-bit */
                dst_row[x*4+2] = (b << 3) | (b >> 2);  /* expand 5-bit to 8-bit */
                dst_row[x*4+3] = 255;
            }
        }
        return true;
    }
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 && format == GL_RGBA) {
        for (int y = 0; y < height; y++) {
            uint8_t *dst_row = staging + y * aligned_row_size;
            const uint16_t *src_row = (const uint16_t*)(src + y * dk_src_row_stride(width, 2));
            for (int x = 0; x < width; x++) {
                uint16_t p = src_row[x];
                uint8_t r = (uint8_t)((p >> 12) & 0xF);
                uint8_t g = (uint8_t)((p >> 8) & 0xF);
                uint8_t b = (uint8_t)((p >> 4) & 0xF);
                uint8_t a = (uint8_t)(p & 0xF);
                dst_row[x*4+0] = (r << 4) | r;  /* expand 4-bit to 8-bit */
                dst_row[x*4+1] = (g << 4) | g;
                dst_row[x*4+2] = (b << 4) | b;
                dst_row[x*4+3] = (a << 4) | a;
            }
        }
        return true;
    }
    if (type == GL_UNSIGNED_SHORT_5_5_5_1 && format == GL_RGBA) {
        for (int y = 0; y < height; y++) {
            uint8_t *dst_row = staging + y * aligned_row_size;
            const uint16_t *src_row = (const uint16_t*)(src + y * dk_src_row_stride(width, 2));
            for (int x = 0; x < width; x++) {
                uint16_t p = src_row[x];
                uint8_t r = (uint8_t)((p >> 11) & 0x1F);
                uint8_t g = (uint8_t)((p >> 6) & 0x1F);
                uint8_t b = (uint8_t)((p >> 1) & 0x1F);
                uint8_t a = (uint8_t)(p & 0x1);
                dst_row[x*4+0] = (r << 3) | (r >> 2);  /* expand 5-bit to 8-bit */
                dst_row[x*4+1] = (g << 3) | (g >> 2);
                dst_row[x*4+2] = (b << 3) | (b >> 2);
                dst_row[x*4+3] = a ? 255 : 0;          /* expand 1-bit to 8-bit */
            }
        }
        return true;
    }
    return false;
}

/* Bitmask for all 6 cubemap faces uploaded (bits 0-5) */
#define DK_CUBEMAP_ALL_FACES 0x3F

/* ============================================================================
 * Descriptor Memory Helpers
 *
 * Write image/sampler descriptors directly to GPU-accessible descriptor memory
 * via CPU memcpy (CpuUncached → DRAM). This avoids per-draw PushData DMA
 * commands in the command buffer and eliminates DMA-vs-TIC/TSC cache coherency
 * issues that caused texture flickering with the inline DMA approach.
 * ============================================================================ */

/* Write the image descriptor for a texture handle directly to GPU descriptor memory */
static void dk_write_image_descriptor_to_gpu(dk_backend_data_t *dk, sgl_handle_t handle) {
    uint8_t *desc_cpu = (uint8_t *)dkMemBlockGetCpuAddr(dk->descriptor_memblock);
    memcpy(desc_cpu + handle * sizeof(DkImageDescriptor),
           &dk->texture_descriptors[handle],
           sizeof(DkImageDescriptor));
    DK_ARM_STORE_BARRIER();  /* Flush CPU write buffer to DRAM before GPU reads */
}

/* Build and write the sampler descriptor for a texture handle directly to GPU descriptor memory */
static void dk_write_sampler_descriptor_to_gpu(dk_backend_data_t *dk, sgl_handle_t handle) {
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
        case GL_REPEAT:         sampler.wrapMode[0] = DkWrapMode_Repeat; break;
        case GL_MIRRORED_REPEAT: sampler.wrapMode[0] = DkWrapMode_MirroredRepeat; break;
        case GL_CLAMP_TO_EDGE:
        default:                sampler.wrapMode[0] = DkWrapMode_ClampToEdge; break;
    }
    switch (dk->texture_wrap_t[handle]) {
        case GL_REPEAT:         sampler.wrapMode[1] = DkWrapMode_Repeat; break;
        case GL_MIRRORED_REPEAT: sampler.wrapMode[1] = DkWrapMode_MirroredRepeat; break;
        case GL_CLAMP_TO_EDGE:
        default:                sampler.wrapMode[1] = DkWrapMode_ClampToEdge; break;
    }
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;

    /* Clamp max LOD to the actual number of mip levels.
     * Without this, the sampler may compute LOD values beyond the allocated
     * mip chain, causing incorrect filtering for cubemap mipmaps and
     * vertex texture sampling. */
    uint32_t mip_levels = dk->texture_mip_levels[handle];
    if (mip_levels > 0)
        sampler.lodClampMax = (float)(mip_levels - 1);
    else
        sampler.lodClampMax = 0.0f;

    dkSamplerDescriptorInitialize(&samplerDesc, &sampler);

    /* Write directly to GPU descriptor memory */
    uint8_t *desc_cpu = (uint8_t *)dkMemBlockGetCpuAddr(dk->descriptor_memblock);
    uint32_t sampler_region_offset = SGL_MAX_TEXTURES * sizeof(DkImageDescriptor);
    memcpy(desc_cpu + sampler_region_offset + handle * sizeof(DkSamplerDescriptor),
           &samplerDesc,
           sizeof(DkSamplerDescriptor));
    DK_ARM_STORE_BARRIER();  /* Flush CPU write buffer to DRAM before GPU reads */
}

/* ============================================================================
 * Texture Memory Allocator (free-list + bump)
 *
 * Mirrors the VBO free-list pattern from dk_buffer.c.
 * On alloc: first-fit from free-list, then bump allocator fallback.
 * On free:  insert into sorted free-list with neighbor coalescing.
 * ============================================================================ */

uint32_t dk_texture_alloc(dk_backend_data_t *dk, uint32_t alignment, uint32_t size) {
    /* First-fit search in texture free list */
    for (int i = 0; i < dk->tex_free_count; i++) {
        uint32_t block_offset = dk->tex_free_list[i].offset;
        uint32_t block_size = dk->tex_free_list[i].size;
        uint32_t aligned = (block_offset + alignment - 1) & ~(alignment - 1);
        uint32_t waste = aligned - block_offset;

        if (block_size >= waste + size) {
            uint32_t remaining = block_size - waste - size;
            if (remaining >= alignment * 2) {
                /* Split: keep remainder */
                dk->tex_free_list[i].offset = aligned + size;
                dk->tex_free_list[i].size = remaining;
            } else {
                /* Remove entire block */
                memmove(&dk->tex_free_list[i], &dk->tex_free_list[i + 1],
                        (dk->tex_free_count - i - 1) * sizeof(sgl_vbo_free_block_t));
                dk->tex_free_count--;
            }
            return aligned;
        }
    }

    /* No free block fits — bump allocate */
    uint32_t aligned_offset = (dk->texture_offset + alignment - 1) & ~(alignment - 1);
    if (aligned_offset + size > SGL_TEXTURE_MEM_SIZE) {
        return UINT32_MAX;  /* Out of memory */
    }
    dk->texture_offset = aligned_offset + size;
    return aligned_offset;
}

void dk_texture_free(dk_backend_data_t *dk, uint32_t offset, uint32_t size) {
    if (offset == 0 || size == 0) return;

    /* Find sorted insertion point */
    int insertAt = 0;
    while (insertAt < dk->tex_free_count && dk->tex_free_list[insertAt].offset < offset)
        insertAt++;

    /* Try coalescing with previous */
    if (insertAt > 0) {
        sgl_vbo_free_block_t *prev = &dk->tex_free_list[insertAt - 1];
        if (prev->offset + prev->size == offset) {
            prev->size += size;
            /* Also try merging with next */
            if (insertAt < dk->tex_free_count &&
                prev->offset + prev->size == dk->tex_free_list[insertAt].offset) {
                prev->size += dk->tex_free_list[insertAt].size;
                memmove(&dk->tex_free_list[insertAt], &dk->tex_free_list[insertAt + 1],
                        (dk->tex_free_count - insertAt - 1) * sizeof(sgl_vbo_free_block_t));
                dk->tex_free_count--;
            }
            return;
        }
    }

    /* Try coalescing with next */
    if (insertAt < dk->tex_free_count && offset + size == dk->tex_free_list[insertAt].offset) {
        dk->tex_free_list[insertAt].offset = offset;
        dk->tex_free_list[insertAt].size += size;
        return;
    }

    /* No coalescing — insert new block */
    if (dk->tex_free_count >= SGL_TEX_FREE_LIST_MAX) return;

    memmove(&dk->tex_free_list[insertAt + 1], &dk->tex_free_list[insertAt],
            (dk->tex_free_count - insertAt) * sizeof(sgl_vbo_free_block_t));
    dk->tex_free_list[insertAt].offset = offset;
    dk->tex_free_list[insertAt].size = size;
    dk->tex_free_count++;
}

void dk_delete_texture(sgl_backend_t *be, sgl_handle_t handle) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (!dk->texture_initialized[handle]) return;

    /* Return GPU memory to texture free-list */
    uint32_t offset = dk->texture_gpu_offset[handle];
    uint32_t size = dk->texture_gpu_size[handle];
    if (offset > 0 && size > 0) {
        dk_texture_free(dk, offset, size);
    }

    /* Clear all per-texture state to prevent stale data on handle reuse */
    dk->texture_initialized[handle] = false;
    dk->texture_is_cubemap[handle] = false;
    dk->texture_used_as_rt[handle] = false;
    dk->cubemap_face_mask[handle] = 0;
    dk->cubemap_needs_barrier[handle] = false;
    dk->texture_gpu_offset[handle] = 0;
    dk->texture_gpu_size[handle] = 0;
    dk->texture_width[handle] = 0;
    dk->texture_height[handle] = 0;
    dk->texture_mip_levels[handle] = 0;
    dk->texture_level_mask[handle] = 0;

    /* Zero out descriptor in GPU memory to prevent stale sampling */
    uint8_t *desc_cpu = (uint8_t *)dkMemBlockGetCpuAddr(dk->descriptor_memblock);
    memset(desc_cpu + handle * sizeof(DkImageDescriptor), 0, sizeof(DkImageDescriptor));
    DK_ARM_STORE_BARRIER();
}

/* Invalidate texture backend state without GPU commands.
 * Used when a delete_pending slot is rebound via glBindTexture — prevents
 * the reuse_image optimization from sharing GPU memory with the old FBO
 * attachment. GPU memory is NOT freed (old FBO still references it). */
void dk_invalidate_texture(sgl_backend_t *be, sgl_handle_t handle) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    dk->texture_initialized[handle] = false;
}

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
        case GL_RGB:
            /* RGB stored as RGBA8 internally — force alpha to 1.0.
             * Per GLES2 §3.7.14, table 3.12: RGB texture has A=1.0 */
            view->swizzle[3] = DkImageSwizzle_One;
            break;
        default: break; /* RGBA: default swizzle is identity */
    }
}

/* Get staging bytes-per-pixel. Packed formats are unpacked to RGBA8 during staging,
 * so they also return 4. Only LUMINANCE/ALPHA/LUMINANCE_ALPHA use smaller bpp.
 * Half-float: 2 bytes per component (GL_OES_texture_half_float). */
static uint32_t dk_gl_format_bpp(GLenum gl_format, GLenum gl_type) {
    if (gl_type == GL_HALF_FLOAT_OES) {
        switch (gl_format) {
            case GL_LUMINANCE: case GL_ALPHA: return 2;
            case GL_LUMINANCE_ALPHA: return 4;
            case GL_RGB: return 8; /* expanded to RGBA16F = 4x2 bytes */
            default: return 8;     /* RGBA = 4x2 bytes */
        }
    }
    switch (gl_format) {
        case GL_LUMINANCE: case GL_ALPHA: return 1;
        case GL_LUMINANCE_ALPHA: return 2;
        default: return 4; /* RGBA, RGB, and packed formats (all expanded to RGBA8) */
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
        /* Calculate mip levels from dimensions (same as 2D textures)
         * so glGenerateMipmap can work on cubemaps */
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
            SGL_ERROR_BACKEND("Cubemap texture memory overflow");
            return;
        }

        DkImage *texImage = &dk->textures[handle];
        dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
        dk->texture_gpu_offset[handle] = aligned_offset;
        dk->texture_gpu_size[handle] = (uint32_t)texSize;
        dk->texture_initialized[handle] = true;
        dk->texture_is_cubemap[handle] = true;
        dk->cubemap_face_mask[handle] = 0;  /* No faces uploaded yet */
        dk->cubemap_needs_barrier[handle] = false;

        dk->texture_width[handle] = width;
        dk->texture_height[handle] = height;
        dk->texture_mip_levels[handle] = mip_levels;
        dk->texture_format[handle] = layoutMaker.format;
        dk->texture_gl_format[handle] = (GLenum)internalformat;
        dk->texture_gl_type[handle] = type;

        dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;  /* GL default */
        dk->texture_mag_filter[handle] = GL_LINEAR;
        dk->texture_wrap_s[handle] = GL_REPEAT;
        dk->texture_wrap_t[handle] = GL_REPEAT;

        /* NOTE: Descriptor creation is DEFERRED until all 6 faces are uploaded.
         * This follows the GLOVE pattern where GPU resources are fully initialized
         * before creating the sampling descriptor. */

        SGL_TRACE_TEXTURE("cubemap created handle=%u %dx%d mips=%u (descriptor deferred)",
                          handle, width, height, mip_levels);
    }

    /* Validate format and dimension consistency for all faces.
     * Per GLES2 §3.7.10: a cubemap is complete only if all 6 faces have
     * the same dimensions, format, and type at each defined level.
     * The base format/dimensions are set when the cubemap is created (first face).
     * If a subsequent face mismatches, skip the upload and DON'T set the face_mask
     * bit, which makes the cubemap incomplete → samples as (0,0,0,1). */
    {
        DkImageFormat faceFormat = dk_convert_format(internalformat, format, type);
        if (faceFormat != dk->texture_format[handle] ||
            (GLenum)internalformat != dk->texture_gl_format[handle] ||
            (uint32_t)width != dk->texture_width[handle] ||
            (uint32_t)height != dk->texture_height[handle]) {
            /* Mismatch: clear face_mask bit so cubemap becomes incomplete.
             * This handles re-uploads with wrong dimensions (e.g., 0×0)
             * that must invalidate a previously-complete face. */
            dk->cubemap_face_mask[handle] &= ~(1u << face_index);
            return;
        }
    }

    /* Upload face pixels if provided */
    if (pixels) {
        DkImage *texImage = &dk->textures[handle];

        uint32_t bpp = dk_gl_format_bpp(format, type);
        uint32_t row_size = width * bpp;
        uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
        uint32_t staging_size = aligned_row_size * height;

        uint32_t saved_client_offset = dk->client_array_offset;
        uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
        if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
            SGL_ERROR_BACKEND("cubemap staging buffer overflow");
            return;
        }

        uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                           + dk->client_array_base + stagingOffset;
        const uint8_t *src = (const uint8_t*)pixels;

        /* Copy pixels to staging buffer */
        if (dk_unpack_packed_to_rgba8(staging, src, width, height, aligned_row_size, format, type)) {
            /* Packed format unpacked to RGBA8 */
        } else if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
            dk_swizzle_bgra_to_rgba(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_HALF_FLOAT_OES) {
            dk_expand_rgb16f_to_rgba16f(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
            /* Convert RGB to RGBA (bpp=4 for staging) */
            for (int y = 0; y < height; y++) {
                uint8_t *dst_row = staging + y * aligned_row_size;
                const uint8_t *src_row = src + y * dk_src_row_stride(width, 3);
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
                memcpy(staging + y * aligned_row_size, src + y * dk_src_row_stride(width, bpp), width * bpp);
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

        /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
        dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
        dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
        dk->descriptors_bound = true;

        /* Staging data consumed by GPU copy — restore offset to free staging space */
        dk->client_array_offset = saved_client_offset;

        /* Track this face as uploaded (level 0 only — cubemap mip uploads skipped) */
        dk->cubemap_face_mask[handle] |= (1 << face_index);
        dk->texture_level_mask[handle] |= (1u << 0);

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

            /* Write descriptors directly to GPU memory */
            dk_write_image_descriptor_to_gpu(dk, handle);
            dk_write_sampler_descriptor_to_gpu(dk, handle);

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
    } else {
        /* pixels is NULL: per GL spec, glTexImage2D with NULL allocates storage
         * with undefined content. The face is still "defined" for texture
         * completeness. Mark it so the descriptor gets created once all 6 faces
         * are defined (even with NULL data). glTexSubImage2D can then upload
         * actual pixel data to the already-initialized cubemap image. */
        dk->cubemap_face_mask[handle] |= (1 << face_index);
        dk->texture_level_mask[handle] |= (1u << 0);

        SGL_TRACE_TEXTURE("cubemap face %d defined (NULL pixels) handle=%u (mask=0x%02X)",
                          face_index, handle, dk->cubemap_face_mask[handle]);

        if (dk->cubemap_face_mask[handle] == DK_CUBEMAP_ALL_FACES) {
            DkImage *texImage = &dk->textures[handle];
            DkImageView imageView;
            dkImageViewDefaults(&imageView, texImage);
            imageView.type = DkImageType_Cubemap;
            dk_apply_format_swizzle(&imageView, dk->texture_gl_format[handle]);

            DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
            dkImageDescriptorInitialize(imgDesc, &imageView, false, false);

            dk_write_image_descriptor_to_gpu(dk, handle);
            dk_write_sampler_descriptor_to_gpu(dk, handle);

            dk->cubemap_needs_barrier[handle] = true;
            dk->texture_used_as_rt[handle] = true;

            SGL_TRACE_TEXTURE("cubemap COMPLETE (NULL path) handle=%u - descriptor created",
                              handle);
        }
    }
}

/* ============================================================================
 * Texture Image Upload (glTexImage2D) - 2D textures
 * ============================================================================ */

void dk_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                         GLenum target, GLint level, GLint internalformat,
                         GLsizei width, GLsizei height, GLint border,
                         GLenum format, GLenum type, const void *pixels) {
    (void)border;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;

    /* Handle cubemap faces separately */
    if (dk_is_cubemap_face(target)) {
        if (level == 0) {
            dk_cubemap_face_upload(dk, handle, target, internalformat, width, height, format, type, pixels);
        } else if (dk_is_cubemap_face(target) && level > 0 &&
                   !dk->texture_initialized[handle]) {
            /* Cubemap face at mip level > 0 before ANY level 0 face.
             * GLES2 spec allows defining mip levels in any order.
             * Create the cubemap at inferred level-0 dimensions.
             * Guard: if inferred dimensions exceed max texture size, this is
             * an "extra level" beyond the valid mip chain — silently skip.
             * Without this, uploading level 8 of a 64x64 cubemap infers
             * 256x256 base, then all subsequent real uploads at 64x64 fail
             * the dimension check (dEQP texture.completeness.cube.extra_level). */
            int w0 = width << level;
            int h0 = height << level;
            if (w0 < 1) w0 = 1;
            if (h0 < 1) h0 = 1;
            if (w0 > 8192 || h0 > 8192) {
                /* Inferred dimensions exceed GPU max — this level is beyond
                 * the valid mip chain. Per GLES2 §3.7.10, extra levels don't
                 * affect completeness. Silently skip. */
                return;
            }
            SGL_TRACE_TEXTURE("cubemap mip level %d before level 0, creating %dx%d base", level, w0, h0);

            /* Create cubemap via dk_cubemap_face_upload with NULL pixels at level-0 dimensions.
             * This initializes the DkImage with correct dimensions/mip count.
             * Pass NULL pixels so no data is uploaded — only storage is allocated. */
            dk_cubemap_face_upload(dk, handle, target, internalformat,
                                   (GLsizei)w0, (GLsizei)h0, format, type, NULL);

            /* Now fall through to the level > 0 upload below */
            if (dk->texture_initialized[handle] && dk->texture_is_cubemap[handle]) {
                /* Upload the mip level data */
                uint32_t tex_mips = dk->texture_mip_levels[handle];
                if ((uint32_t)level >= tex_mips) {
                    return;  /* return inside cubemap block */
                }
                uint32_t expected_w = dk->texture_width[handle] >> level;
                uint32_t expected_h = dk->texture_height[handle] >> level;
                if (expected_w < 1) expected_w = 1;
                if (expected_h < 1) expected_h = 1;
                if ((uint32_t)width != expected_w || (uint32_t)height != expected_h) {
                    return;
                }

                if (pixels) {
                    int face_index = dk_cubemap_face_index(target);
                    DkImage *texImage = &dk->textures[handle];
                    uint32_t bpp = dk_gl_format_bpp(format, type);
                    uint32_t row_size = width * bpp;
                    uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
                    uint32_t staging_size = aligned_row_size * height;

                    uint32_t saved_client_offset = dk->client_array_offset;
                    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
                    if (stagingOffset + staging_size <= dk->uniform_base - dk->client_array_base) {
                        uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                                           + dk->client_array_base + stagingOffset;
                        const uint8_t *src = (const uint8_t*)pixels;

                        if (dk_unpack_packed_to_rgba8(staging, src, width, height, aligned_row_size, format, type)) {
                            /* Packed format */
                        } else if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
            dk_swizzle_bgra_to_rgba(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_HALF_FLOAT_OES) {
            dk_expand_rgb16f_to_rgba16f(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
                            for (int y = 0; y < height; y++) {
                                uint8_t *dst_row = staging + y * aligned_row_size;
                                const uint8_t *src_row = src + y * dk_src_row_stride(width, 3);
                                for (int x = 0; x < width; x++) {
                                    dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                                    dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                                    dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                                    dst_row[x * 4 + 3] = 255;
                                }
                            }
                        } else {
                            for (int y = 0; y < height; y++) {
                                memcpy(staging + y * aligned_row_size,
                                       src + y * dk_src_row_stride(width, bpp), width * bpp);
                            }
                        }

                        dk->client_array_offset = stagingOffset + staging_size;

                        DkImageView faceView;
                        dkImageViewDefaults(&faceView, texImage);
                        faceView.type = DkImageType_2D;
                        faceView.layerOffset = face_index;
                        faceView.layerCount = 1;
                        faceView.mipLevelOffset = level;

                        DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                                                + dk->client_array_base + stagingOffset;
                        DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
                        DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

                        dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &faceView, &dstRect, 0);

                        DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
                        dkQueueSubmitCommands(dk->queue, cmdlist);
                        dkQueueWaitIdle(dk->queue);

                        dkCmdBufClear(dk->cmdbuf);
                        dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
                        dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
                        dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
                        dk->descriptors_bound = true;
                        dk->client_array_offset = saved_client_offset;
                        dk_rebind_render_target(dk);
                    }
                }
                dk->texture_level_mask[handle] |= (1u << level);
                if (pixels && level > 0)
                    dk->cubemap_needs_barrier[handle] = true;
            }
        } else if (dk->texture_initialized[handle] && dk->texture_is_cubemap[handle]) {
            /* Level > 0: upload mip data to existing cubemap.
             * Validate level, dimensions, and format before uploading. */
            uint32_t tex_mips = dk->texture_mip_levels[handle];
            if ((uint32_t)level >= tex_mips) return;
            uint32_t expected_w = dk->texture_width[handle] >> level;
            uint32_t expected_h = dk->texture_height[handle] >> level;
            if (expected_w < 1) expected_w = 1;
            if (expected_h < 1) expected_h = 1;
            if ((uint32_t)width != expected_w || (uint32_t)height != expected_h) return;
            DkImageFormat levelFormat = dk_convert_format(internalformat, format, type);
            if (levelFormat != dk->texture_format[handle]) return;
            /* Also check GL format — RGB and RGBA both map to RGBA8_Unorm,
             * so DkImageFormat alone can't distinguish them. Per GLES2 §3.7.10,
             * all mip levels must share the same internal format. */
            if ((GLenum)internalformat != dk->texture_gl_format[handle]) return;

            if (pixels) {
                int face_index = dk_cubemap_face_index(target);
                DkImage *texImage = &dk->textures[handle];
                uint32_t bpp = dk_gl_format_bpp(format, type);
                uint32_t row_size = width * bpp;
                uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
                uint32_t staging_size = aligned_row_size * height;

                uint32_t saved_client_offset = dk->client_array_offset;
                uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
                if (stagingOffset + staging_size <= dk->uniform_base - dk->client_array_base) {
                    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                                       + dk->client_array_base + stagingOffset;
                    const uint8_t *src = (const uint8_t*)pixels;

                    if (dk_unpack_packed_to_rgba8(staging, src, width, height, aligned_row_size, format, type)) {
                        /* Packed format */
                    } else if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
            dk_swizzle_bgra_to_rgba(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_HALF_FLOAT_OES) {
            dk_expand_rgb16f_to_rgba16f(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
                        for (int y = 0; y < height; y++) {
                            uint8_t *dst_row = staging + y * aligned_row_size;
                            const uint8_t *src_row = src + y * dk_src_row_stride(width, 3);
                            for (int x = 0; x < width; x++) {
                                dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                                dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                                dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                                dst_row[x * 4 + 3] = 255;
                            }
                        }
                    } else {
                        for (int y = 0; y < height; y++) {
                            memcpy(staging + y * aligned_row_size,
                                   src + y * dk_src_row_stride(width, bpp), width * bpp);
                        }
                    }

                    dk->client_array_offset = stagingOffset + staging_size;

                    DkImageView faceView;
                    dkImageViewDefaults(&faceView, texImage);
                    faceView.type = DkImageType_2D;
                    faceView.layerOffset = face_index;
                    faceView.layerCount = 1;
                    faceView.mipLevelOffset = level;

                    DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                                            + dk->client_array_base + stagingOffset;
                    DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
                    DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

                    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &faceView, &dstRect, 0);

                    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
                    dkQueueSubmitCommands(dk->queue, cmdlist);
                    dkQueueWaitIdle(dk->queue);

                    dkCmdBufClear(dk->cmdbuf);
                    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
                    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
                    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
                    dk->descriptors_bound = true;
                    dk->client_array_offset = saved_client_offset;
                    dk_rebind_render_target(dk);
                }
            }
            /* Mark this mip level as defined (whether pixels were provided or not) */
            dk->texture_level_mask[handle] |= (1u << level);
            if (pixels)
                dk->cubemap_needs_barrier[handle] = true;
            SGL_TRACE_TEXTURE("cubemap face mip level %d uploaded handle=%u (level_mask=0x%X)",
                              level, handle, dk->texture_level_mask[handle]);
        }
        return;
    }

    /* === Mip level > 0: texture storage already allocated at level 0 === */
    if (level > 0) {
        if (!dk->texture_initialized[handle]) {
            /* GLES2 spec allows defining mip levels in any order.
             * Create the texture at inferred level-0 dimensions, then upload this mip.
             * Level-0 dimensions: w0 = width << level, h0 = height << level */
            int w0 = width << level;
            int h0 = height << level;
            if (w0 < 1) w0 = 1;
            if (h0 < 1) h0 = 1;
            SGL_TRACE_TEXTURE("texture_image_2d: level %d before level 0, creating %dx%d base", level, w0, h0);

            DkImageFormat newFmt = dk_convert_format(internalformat, format, type);
            uint32_t max_dim = (uint32_t)(w0 > h0 ? w0 : h0);
            uint32_t mip_levels = 1;
            uint32_t temp = max_dim;
            while (temp > 1) { temp >>= 1; mip_levels++; }

            DkImageLayoutMaker layoutMaker;
            dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
            layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
            layoutMaker.format = newFmt;
            layoutMaker.dimensions[0] = w0;
            layoutMaker.dimensions[1] = h0;
            layoutMaker.dimensions[2] = 1;
            layoutMaker.mipLevels = mip_levels;

            DkImageLayout layout;
            dkImageLayoutInitialize(&layout, &layoutMaker);
            uint64_t texSize = dkImageLayoutGetSize(&layout);
            uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

            uint32_t aligned_offset = dk_texture_alloc(dk, texAlign, (uint32_t)texSize);
            if (aligned_offset == UINT32_MAX) {
                SGL_ERROR_BACKEND("Texture memory overflow (deferred level %d)", level);
                return;
            }

            DkImage *texImage = &dk->textures[handle];
            dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
            dk->texture_gpu_offset[handle] = aligned_offset;
            dk->texture_gpu_size[handle] = (uint32_t)texSize;
            dk->texture_initialized[handle] = true;
            dk->texture_is_cubemap[handle] = false;
            dk->texture_width[handle] = w0;
            dk->texture_height[handle] = h0;
            dk->texture_mip_levels[handle] = mip_levels;
            dk->texture_format[handle] = newFmt;
            dk->texture_gl_format[handle] = (GLenum)internalformat;
            dk->texture_gl_type[handle] = type;
            dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;
            dk->texture_mag_filter[handle] = GL_LINEAR;
            dk->texture_wrap_s[handle] = GL_REPEAT;
            dk->texture_wrap_t[handle] = GL_REPEAT;
            dk->texture_level_mask[handle] = 0;

            /* Create descriptor for the base texture */
            DkImageView imageView;
            dkImageViewDefaults(&imageView, texImage);
            dk_apply_format_swizzle(&imageView, internalformat);
            dkImageDescriptorInitialize(&dk->texture_descriptors[handle], &imageView, false, false);
            dk_write_image_descriptor_to_gpu(dk, handle);
            dk_write_sampler_descriptor_to_gpu(dk, handle);
        }
        /* Validate mip level index and dimensions against the allocated DkImage.
         * If level is out of range or dimensions don't match the actual mip size,
         * skip the upload — the texture will be incomplete (per GLES2 spec). */
        uint32_t tex_mips = dk->texture_mip_levels[handle];
        if ((uint32_t)level >= tex_mips) {
            /* Mip level doesn't exist in the allocated texture — skip silently */
            return;
        }
        uint32_t expected_w = dk->texture_width[handle] >> level;
        uint32_t expected_h = dk->texture_height[handle] >> level;
        if (expected_w < 1) expected_w = 1;
        if (expected_h < 1) expected_h = 1;
        if ((uint32_t)width != expected_w || (uint32_t)height != expected_h) {
            /* Dimensions mismatch — texture will be incomplete, skip upload */
            return;
        }
        /* Verify format matches the base texture. Different format at a mip level
         * means the texture is incomplete. Uploading data with wrong bpp to a
         * DkImage with a different format would cause a GPU crash.
         * Check BOTH the DkImageFormat and the GL internalformat — some GL formats
         * (e.g. GL_RGB and GL_RGBA) map to the same DkImageFormat (RGBA8_Unorm)
         * but are still a format mismatch per GLES2 spec §3.7.10. */
        DkImageFormat levelFormat = dk_convert_format(internalformat, format, type);
        if (levelFormat != dk->texture_format[handle] ||
            (GLenum)internalformat != dk->texture_gl_format[handle]) {
            return;
        }
        /* Upload pixel data to the specific mip level if provided */
        if (pixels) {
            DkImage *texImage = &dk->textures[handle];
            uint32_t bpp = dk_gl_format_bpp(format, type);
            uint32_t row_size = width * bpp;
            uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
            uint32_t staging_size = aligned_row_size * height;
            const uint8_t *src = (const uint8_t*)pixels;

            uint32_t saved_client_offset = dk->client_array_offset;
            uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
            if (stagingOffset + staging_size <= dk->uniform_base - dk->client_array_base) {
                uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                                   + dk->client_array_base + stagingOffset;

                if (dk_unpack_packed_to_rgba8(staging, src, width, height, aligned_row_size, format, type)) {
                    /* Packed format unpacked to RGBA8 */
                } else if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
            dk_swizzle_bgra_to_rgba(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_HALF_FLOAT_OES) {
            dk_expand_rgb16f_to_rgba16f(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
                    for (int y = 0; y < height; y++) {
                        uint8_t *dst_row = staging + y * aligned_row_size;
                        const uint8_t *src_row = src + y * dk_src_row_stride(width, 3);
                        for (int x = 0; x < width; x++) {
                            dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                            dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                            dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                            dst_row[x * 4 + 3] = 255;
                        }
                    }
                } else {
                    for (int y = 0; y < height; y++) {
                        memcpy(staging + y * aligned_row_size, src + y * dk_src_row_stride(width, bpp), width * bpp);
                    }
                }

                dk->client_array_offset = stagingOffset + staging_size;

                DkImageView imageView;
                dkImageViewDefaults(&imageView, texImage);
                imageView.mipLevelOffset = level;
                dk_apply_format_swizzle(&imageView, dk->texture_gl_format[handle]);

                DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                                        + dk->client_array_base + stagingOffset;
                DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
                DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

                dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &imageView, &dstRect, 0);

                DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
                dkQueueSubmitCommands(dk->queue, cmdlist);
                dkQueueWaitIdle(dk->queue);

                dkCmdBufClear(dk->cmdbuf);
                dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

                /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
                dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
                dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
                dk->descriptors_bound = true;

                dk->client_array_offset = saved_client_offset;
                dk_rebind_render_target(dk);
            }
        }
        /* Mark this mip level as defined for completeness tracking */
        dk->texture_level_mask[handle] |= (1u << level);
        SGL_TRACE_TEXTURE("texture_image_2d handle=%u mip level %d %dx%d", handle, level, width, height);
        return;
    }

    /* === Level 0: Regular 2D texture path === */

    DkImageFormat newFormat = dk_convert_format(internalformat, format, type);

    /* Reuse existing DkImage if same dimensions and format (e.g. cinematic frames).
     * This avoids exhausting the texture bump allocator on every-frame glTexImage2D. */
    bool reuse_image = dk->texture_initialized[handle] &&
                       !dk->texture_is_cubemap[handle] &&
                       dk->texture_width[handle] == (uint32_t)width &&
                       dk->texture_height[handle] == (uint32_t)height &&
                       dk->texture_format[handle] == newFormat;

    DkImage *texImage = &dk->textures[handle];

    if (!reuse_image) {
        /* Free old texture memory before reallocating (prevents texture memory leak
         * on glTexImage2D resize while attached to FBO). Must flush GPU first if the
         * texture is currently bound as a render target. */
        if (dk->texture_initialized[handle] && dk->texture_gpu_size[handle] > 0) {
            if (dk->current_fbo != 0 &&
                dk->current_fbo_color == handle && !dk->current_fbo_color_is_rb) {
                dk_submit_and_reset(dk);
            }
            dk_texture_free(dk, dk->texture_gpu_offset[handle], dk->texture_gpu_size[handle]);
        }

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
        layoutMaker.format = newFormat;
        layoutMaker.dimensions[0] = width;
        layoutMaker.dimensions[1] = height;
        layoutMaker.dimensions[2] = 1;
        layoutMaker.mipLevels = mip_levels;  /* Allocate space for all mip levels */

        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &layoutMaker);

        uint64_t texSize = dkImageLayoutGetSize(&layout);
        uint32_t texAlign = dkImageLayoutGetAlignment(&layout);

        uint32_t aligned_offset = dk_texture_alloc(dk, texAlign, (uint32_t)texSize);
        if (aligned_offset == UINT32_MAX) {
            SGL_ERROR_BACKEND("Texture memory overflow");
            return;
        }

        dkImageInitialize(texImage, &layout, dk->texture_memblock, aligned_offset);
        dk->texture_gpu_offset[handle] = aligned_offset;
        dk->texture_gpu_size[handle] = (uint32_t)texSize;
        dk->texture_initialized[handle] = true;
        dk->texture_is_cubemap[handle] = false;  /* This is a 2D texture */

        /* Store texture dimensions and format for glGenerateMipmap */
        dk->texture_width[handle] = width;
        dk->texture_height[handle] = height;
        dk->texture_mip_levels[handle] = mip_levels;
        dk->texture_format[handle] = newFormat;
        dk->texture_gl_format[handle] = (GLenum)internalformat;
        dk->texture_gl_type[handle] = type;

        /* Initialize default sampler parameters (GL defaults) */
        dk->texture_min_filter[handle] = GL_NEAREST_MIPMAP_LINEAR;  /* GL default */
        dk->texture_mag_filter[handle] = GL_LINEAR;                  /* GL default */
        dk->texture_wrap_s[handle] = GL_REPEAT;
        dk->texture_wrap_t[handle] = GL_REPEAT;

        /* Track defined mip levels for completeness check */
        dk->texture_level_mask[handle] = 0;
    }

    /* Mark level 0 as defined (even for NULL data — glTexImage2D with NULL
     * allocates storage and defines the level per GLES2 spec) */
    dk->texture_level_mask[handle] |= (1u << 0);

    /* Create image descriptor with format-specific swizzle */
    DkImageView imageView;
    dkImageViewDefaults(&imageView, texImage);
    dk_apply_format_swizzle(&imageView, internalformat);

    DkImageDescriptor *imgDesc = &dk->texture_descriptors[handle];
    dkImageDescriptorInitialize(imgDesc, &imageView, false, false);

    /* Write descriptors directly to GPU memory (CpuUncached → DRAM).
     * This avoids per-draw PushData DMA and eliminates TIC/TSC coherency issues. */
    dk_write_image_descriptor_to_gpu(dk, handle);
    dk_write_sampler_descriptor_to_gpu(dk, handle);

    /* Re-bind render target if this texture was reallocated and is attached to
     * the current FBO (color, depth, or stencil). Without this, FBO resize
     * tests fail because the GPU render target still points to the old DkImage. */
    if (!reuse_image && dk->current_fbo != 0) {
        if ((dk->current_fbo_color == handle && !dk->current_fbo_color_is_rb) ||
            (dk->current_fbo_depth == handle && !dk->current_fbo_depth_is_rb) ||
            (dk->current_fbo_stencil == handle && !dk->current_fbo_stencil_is_rb)) {
            dk_rebind_render_target(dk);
        }
    }

    /* Upload pixel data if provided - use staging buffer and GPU copy like legacy */
    if (pixels) {
        /* Calculate source size with stride alignment (bpp-aware) */
        uint32_t bpp = dk_gl_format_bpp(format, type);
        uint32_t row_size = width * bpp;
        uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
        uint32_t staging_size = aligned_row_size * height;

        const uint8_t *src = (const uint8_t*)pixels;

        /* Use client array region as staging */
        uint32_t saved_client_offset = dk->client_array_offset;
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
            if (dk_unpack_packed_to_rgba8(staging, src, width, height, aligned_row_size, format, type)) {
                /* Packed format unpacked to RGBA8 */
            } else if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
            dk_swizzle_bgra_to_rgba(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_HALF_FLOAT_OES) {
            dk_expand_rgb16f_to_rgba16f(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
                /* Convert RGB to RGBA, no Y-flip */
                for (int y = 0; y < height; y++) {
                    uint8_t *dst_row = staging + y * aligned_row_size;
                    const uint8_t *src_row = src + y * dk_src_row_stride(width, 3);
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
                    memcpy(staging + y * aligned_row_size, src + y * dk_src_row_stride(width, bpp), width * bpp);
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

            /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
            dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
            dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
            dk->descriptors_bound = true;

            /* Staging data consumed by GPU copy — restore offset to free staging space */
            dk->client_array_offset = saved_client_offset;

            dk_rebind_render_target(dk);

            /* NOTE: Do NOT set texture_used_as_rt here. This texture was uploaded via
             * CopyBufferToImage + WaitIdle — the copy is complete and data is in GPU
             * Image memory. No barrier is needed. The legacy code has no per-texture
             * barrier for regular uploads. texture_used_as_rt should ONLY be set for
             * textures actually rendered to via FBO. */
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
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (!dk->texture_initialized[handle]) {
        SGL_ERROR_BACKEND("texture_sub_image_2d: texture %u not initialized", handle);
        return;
    }
    if (!pixels) return;

    /* Get the existing DkImage */
    DkImage *texImage = &dk->textures[handle];

    /* Calculate source size with stride alignment (bpp from current upload type) */
    uint32_t bpp = dk_gl_format_bpp(format, type);
    uint32_t row_size = width * bpp;
    uint32_t aligned_row_size = SGL_ALIGN_UP(row_size, DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    const uint8_t *src = (const uint8_t*)pixels;

    /* Use client array region as staging */
    uint32_t saved_client_offset = dk->client_array_offset;
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("texture_sub_image_2d: staging buffer overflow");
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;

    /* Copy pixel data to staging buffer with proper stride.
     * No Y-flip needed - texture storage matches GL row order (see glTexImage2D comment). */
    if (dk_unpack_packed_to_rgba8(staging, src, width, height, aligned_row_size, format, type)) {
        /* Packed format unpacked to RGBA8 */
    } else if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
            dk_swizzle_bgra_to_rgba(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_HALF_FLOAT_OES) {
            dk_expand_rgb16f_to_rgba16f(staging, src, width, height, aligned_row_size);
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
        /* Convert RGB to RGBA, no Y-flip */
        for (int y = 0; y < height; y++) {
            uint8_t *dst_row = staging + y * aligned_row_size;
            const uint8_t *src_row = src + y * dk_src_row_stride(width, 3);
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
            memcpy(staging + y * aligned_row_size, src + y * dk_src_row_stride(width, bpp), width * bpp);
        }
    }

    dk->client_array_offset = stagingOffset + staging_size;

    /* Create image view for the existing texture */
    DkImageView imageView;
    dkImageViewDefaults(&imageView, texImage);
    if (level > 0) {
        imageView.mipLevelOffset = level;
    }

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

    /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    /* Staging data consumed by GPU copy — restore offset to free staging space */
    dk->client_array_offset = saved_client_offset;

    dk_rebind_render_target(dk);

    /* Mark texture as needing L2 cache barrier before next sampling.
     * The DMA copy engine writes new data directly to DRAM, but the GPU's
     * texture cache (L2) may still hold stale data from before the update.
     * Without invalidation, the GPU reads old texture content instead of
     * the freshly uploaded data. Critical for cinematic video frames. */
    dk->texture_used_as_rt[handle] = true;

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

    /* Check if value actually changed — avoid redundant GPU memory writes.
     * sgl_prepare_draw calls texture_parameter for every draw; early-out
     * when the value hasn't changed avoids 1500+ needless memcpy's per frame. */
    GLenum *stored = NULL;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: stored = &dk->texture_min_filter[handle]; break;
        case GL_TEXTURE_MAG_FILTER: stored = &dk->texture_mag_filter[handle]; break;
        case GL_TEXTURE_WRAP_S:     stored = &dk->texture_wrap_s[handle]; break;
        case GL_TEXTURE_WRAP_T:     stored = &dk->texture_wrap_t[handle]; break;
        default: return;
    }

    if (*stored == (GLenum)param) return;  /* No change */
    *stored = (GLenum)param;

    /* Parameter changed — rebuild sampler descriptor and write to GPU memory.
     * Only happens when the app actually calls glTexParameteri with a new value,
     * NOT on every draw (thanks to the early-out above). */
    if (dk->texture_initialized[handle]) {
        dk_write_sampler_descriptor_to_gpu(dk, handle);
    }

    SGL_TRACE_TEXTURE("texture_parameter handle=%u pname=0x%X param=0x%X", handle, pname, param);
}

/* ============================================================================
 * Texture Binding for Sampling
 * ============================================================================ */

/* ============================================================================
 * Black Fallback Texture (1x1 RGBA 0,0,0,255)
 *
 * Created at backend init for incomplete texture sampling.
 * Per GLES2 §3.7.10, incomplete textures return (0,0,0,1).
 * ============================================================================ */

void dk_create_black_texture(dk_backend_data_t *dk) {
    const sgl_handle_t bh = 0;
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, dk->device);
    lm.flags = 0;
    lm.format = DkImageFormat_RGBA8_Unorm;
    lm.type = DkImageType_2D;
    lm.dimensions[0] = 1;
    lm.dimensions[1] = 1;
    lm.dimensions[2] = 1;
    lm.mipLevels = 1;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    uint64_t bsz = dkImageLayoutGetSize(&layout);
    uint32_t bal = dkImageLayoutGetAlignment(&layout);
    uint32_t boff = dk_texture_alloc(dk, bal, (uint32_t)bsz);
    if (boff == UINT32_MAX) {
        SGL_ERROR_BACKEND("dk_create_black_texture: texture alloc failed");
        return;
    }

    dkImageInitialize(&dk->textures[bh], &layout, dk->texture_memblock, boff);

    /* Upload black pixel (0,0,0,255) via staging */
    uint32_t soff = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    uint8_t *stg = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock) + dk->client_array_base + soff;
    stg[0] = 0; stg[1] = 0; stg[2] = 0; stg[3] = 255;

    DkImageView biv;
    dkImageViewDefaults(&biv, &dk->textures[bh]);
    DkGpuAddr sAddr = dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + soff;
    DkCopyBuf srcBuf = { sAddr, 4, 1 };
    DkImageRect dstRect = { 0, 0, 0, 1, 1, 1 };
    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &biv, &dstRect, 0);

    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Create image and sampler descriptors */
    dkImageDescriptorInitialize(&dk->texture_descriptors[bh], &biv, false, false);
    dk_write_image_descriptor_to_gpu(dk, bh);

    dk->texture_min_filter[bh] = GL_NEAREST;
    dk->texture_mag_filter[bh] = GL_NEAREST;
    dk->texture_wrap_s[bh] = GL_CLAMP_TO_EDGE;
    dk->texture_wrap_t[bh] = GL_CLAMP_TO_EDGE;
    dk_write_sampler_descriptor_to_gpu(dk, bh);

    dk->texture_initialized[bh] = true;
    dk->texture_width[bh] = 1;
    dk->texture_height[bh] = 1;
    dk->texture_mip_levels[bh] = 1;
    dk->texture_level_mask[bh] = 1;  /* Level 0 defined */
    dk->texture_gpu_offset[bh] = boff;
    dk->texture_gpu_size[bh] = (uint32_t)bsz;

    /* Also create a 1x1 black CUBEMAP fallback at slot 1.
     * Incomplete cubemap textures need a cubemap fallback — binding a 2D
     * texture to a samplerCube is an image type mismatch on the GPU. */
    const sgl_handle_t ch = 1;
    DkImageLayoutMaker clm;
    dkImageLayoutMakerDefaults(&clm, dk->device);
    clm.flags = 0;
    clm.format = DkImageFormat_RGBA8_Unorm;
    clm.type = DkImageType_Cubemap;
    clm.dimensions[0] = 1;
    clm.dimensions[1] = 1;
    clm.dimensions[2] = 1;
    clm.mipLevels = 1;

    DkImageLayout clayout;
    dkImageLayoutInitialize(&clayout, &clm);
    uint64_t csz = dkImageLayoutGetSize(&clayout);
    uint32_t cal = dkImageLayoutGetAlignment(&clayout);
    uint32_t coff = dk_texture_alloc(dk, cal, (uint32_t)csz);
    if (coff == UINT32_MAX) {
        SGL_ERROR_BACKEND("dk_create_black_texture: cubemap alloc failed");
    } else {
        dkImageInitialize(&dk->textures[ch], &clayout, dk->texture_memblock, coff);

        /* Upload black pixel to all 6 faces */
        uint32_t csoff = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
        uint8_t *cstg = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock) + dk->client_array_base + csoff;
        cstg[0] = 0; cstg[1] = 0; cstg[2] = 0; cstg[3] = 255;

        DkGpuAddr csAddr = dkMemBlockGetGpuAddr(dk->data_memblock) + dk->client_array_base + csoff;
        DkCopyBuf csrcBuf = { csAddr, 4, 1 };
        for (int face = 0; face < 6; face++) {
            DkImageView civ;
            dkImageViewDefaults(&civ, &dk->textures[ch]);
            civ.type = DkImageType_2D;
            civ.layerOffset = face;
            civ.layerCount = 1;
            DkImageRect cdstRect = { 0, 0, 0, 1, 1, 1 };
            dkCmdBufCopyBufferToImage(dk->cmdbuf, &csrcBuf, &civ, &cdstRect, 0);
        }

        DkCmdList ccmdlist = dkCmdBufFinishList(dk->cmdbuf);
        dkQueueSubmitCommands(dk->queue, ccmdlist);
        dkQueueWaitIdle(dk->queue);

        dkCmdBufClear(dk->cmdbuf);
        dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

        DkImageView civ_full;
        dkImageViewDefaults(&civ_full, &dk->textures[ch]);
        dkImageDescriptorInitialize(&dk->texture_descriptors[ch], &civ_full, false, false);
        dk_write_image_descriptor_to_gpu(dk, ch);

        dk->texture_min_filter[ch] = GL_NEAREST;
        dk->texture_mag_filter[ch] = GL_NEAREST;
        dk->texture_wrap_s[ch] = GL_CLAMP_TO_EDGE;
        dk->texture_wrap_t[ch] = GL_CLAMP_TO_EDGE;
        dk_write_sampler_descriptor_to_gpu(dk, ch);

        dk->texture_initialized[ch] = true;
        dk->texture_is_cubemap[ch] = true;
        dk->cubemap_face_mask[ch] = 0x3F;
        dk->texture_width[ch] = 1;
        dk->texture_height[ch] = 1;
        dk->texture_mip_levels[ch] = 1;
        dk->texture_level_mask[ch] = 1;
        dk->texture_gpu_offset[ch] = coff;
        dk->texture_gpu_size[ch] = (uint32_t)csz;
    }
}

/* Check if a texture is complete per GLES2 §3.7.10.
 * Incomplete textures must sample as (0,0,0,1). */
static bool dk_texture_is_complete(dk_backend_data_t *dk, sgl_handle_t handle) {
    if (!dk->texture_initialized[handle]) return false;

    uint32_t w = dk->texture_width[handle];
    uint32_t h = dk->texture_height[handle];
    if (w == 0 || h == 0) return false;

    /* Cubemap: all 6 faces must be defined */
    if (dk->texture_is_cubemap[handle] && dk->cubemap_face_mask[handle] != 0x3F)
        return false;

    /* NPOT texture completeness per GLES2 §3.7.10:
     * An NPOT texture is incomplete if it uses a mipmap filter or if any
     * wrap mode is not GL_CLAMP_TO_EDGE. We do NOT advertise GL_OES_texture_npot. */
    bool is_npot = ((w & (w - 1)) != 0) || ((h & (h - 1)) != 0);
    if (is_npot) {
        if (dk->texture_wrap_s[handle] != GL_CLAMP_TO_EDGE ||
            dk->texture_wrap_t[handle] != GL_CLAMP_TO_EDGE)
            return false;
    }

    GLenum min_f = dk->texture_min_filter[handle];
    bool needs_mipmaps = (min_f == GL_NEAREST_MIPMAP_NEAREST ||
                          min_f == GL_LINEAR_MIPMAP_NEAREST ||
                          min_f == GL_NEAREST_MIPMAP_LINEAR ||
                          min_f == GL_LINEAR_MIPMAP_LINEAR);

    if (needs_mipmaps) {
        /* NPOT with mipmap filter is always incomplete per §3.7.10 */
        if (is_npot) return false;

        /* All mip levels must be defined when mipmap filtering is active.
         * Per GLES2 §3.7.10: a texture using a mipmap filter is complete only
         * if all levels from 0 to floor(log2(max(w,h))) are defined with
         * matching format and correct dimensions. */
        uint32_t mip_levels = dk->texture_mip_levels[handle];
        uint32_t required_mask = (mip_levels >= 32) ? 0xFFFFFFFF : ((1u << mip_levels) - 1);
        if ((dk->texture_level_mask[handle] & required_mask) != required_mask)
            return false;
    } else {
        /* Non-mipmap filter: only level 0 must be defined */
        if (!(dk->texture_level_mask[handle] & 1u))
            return false;
    }

    return true;
}

#define SGL_BLACK_TEXTURE_HANDLE   0  /* Slot 0: reserved 1x1 black 2D (0,0,0,255) fallback */
#define SGL_BLACK_CUBEMAP_HANDLE   1  /* Slot 1: reserved 1x1 black cubemap fallback */

void dk_bind_texture(sgl_backend_t *be, GLuint unit, sgl_handle_t handle, int stage) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle >= SGL_MAX_TEXTURES) {
        return;
    }

    /* Per GLES2 §3.7.10: incomplete textures sample as (0,0,0,1).
     * Select the correct fallback type (2D or cubemap) to match the
     * shader's sampler type — binding a 2D texture to samplerCube is a
     * GPU image type mismatch that produces undefined output. */
    if (!dk_texture_is_complete(dk, handle)) {
        if (dk->texture_is_cubemap[handle])
            handle = SGL_BLACK_CUBEMAP_HANDLE;
        else
            handle = SGL_BLACK_TEXTURE_HANDLE;
        if (!dk->texture_initialized[handle]) return;
    }

    /* Insert barrier if this texture was used as a render target (FBO),
     * freshly-completed cubemap, or updated via sub-image/copy needing cache coherency.
     * Invalidate Image (texture cache), L2, AND TIC/TSC descriptor caches. */
    if (dk->texture_used_as_rt[handle] || dk->cubemap_needs_barrier[handle]) {
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                        DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);
        dk->texture_used_as_rt[handle] = false;
        dk->cubemap_needs_barrier[handle] = false;
    }

    /* Fallback: bind descriptor sets if somehow not already bound.
     * Normally bound eagerly at frame start / cmdbuf clear. */
    if (!dk->descriptors_bound) {
        dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
        dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
        dk->descriptors_bound = true;
    }

    dk->diag_texture_binds++;

    /* Bind texture handle using HANDLE index for both image and sampler descriptors.
     * Descriptors are pre-written to GPU memory at create/update time via CPU memcpy
     * (CpuUncached → DRAM), NOT via per-draw PushData DMA. This eliminates
     * DMA-vs-TIC/TSC cache coherency issues that caused texture flickering.
     * Each texture has its own descriptor slot (per-handle), so switching textures
     * on the same unit just changes which slot the GPU reads — no overwrites. */
    DkResHandle texHandle = dkMakeTextureHandle(handle, handle);
    /* Bind to specified stage(s) — stage: -1=both, 0=vertex, 1=fragment */
    if (stage <= 0) /* vertex or both */
        dkCmdBufBindTexture(dk->cmdbuf, DkStage_Vertex, unit, texHandle);
    if (stage != 0) /* fragment or both */
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
    bool is_cubemap = dk->texture_is_cubemap[handle];
    int num_faces = is_cubemap ? 6 : 1;

    /* Generate each mip level by blitting from the previous level.
     * For cubemaps, iterate over all 6 faces per mip level. */
    for (int face = 0; face < num_faces; face++) {
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

            if (is_cubemap) {
                /* Blit each face individually as 2D slices */
                srcView.type = DkImageType_2D;
                srcView.layerOffset = face;
                srcView.layerCount = 1;
                dstView.type = DkImageType_2D;
                dstView.layerOffset = face;
                dstView.layerCount = 1;
            }

            /* Define source and destination rectangles */
            DkImageRect srcRect = { 0, 0, 0, src_width, src_height, 1 };
            DkImageRect dstRect = { 0, 0, 0, dst_width, dst_height, 1 };

            /* Blit with linear filtering for smooth downscaling */
            dkCmdBufBlitImage(dk->cmdbuf, &srcView, &srcRect, &dstView, &dstRect,
                              DkBlitFlag_FilterLinear, 0);

            /* Add barrier between mip levels to ensure proper synchronization */
            dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

            src_width = dst_width;
            src_height = dst_height;
        }
    }

    /* Final barrier to ensure all mipmap generation is complete before sampling */
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);

    /* All mip levels are now defined */
    dk->texture_level_mask[handle] = (mip_levels >= 32) ? 0xFFFFFFFF : ((1u << mip_levels) - 1);

    SGL_TRACE_TEXTURE("generate_mipmap handle=%u levels=%u%s", handle, mip_levels,
                      is_cubemap ? " (cubemap)" : "");
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
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    bool is_cubemap_face = dk_is_cubemap_face(target);

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (width <= 0 || height <= 0) return;

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
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

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

    /* With DkDeviceFlags_OriginLowerLeft, CopyImageToBuffer uses GL-style
     * coordinates where y=0 is at the bottom — same as dk_read_pixels.
     * No Y-flip needed in the srcRect. */
    uint32_t dk_src_y = (uint32_t)y;

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

    /* === Step 3: Create/reuse destination texture === */
    DkImage *texImage = &dk->textures[handle];

    if (level == 0) {
        if (is_cubemap_face && !dk->texture_initialized[handle]) {
            /* Cubemap face: create cubemap DkImage on first face (allocates for all 6) */
            uint32_t max_dim = (uint32_t)(width > height ? width : height);
            uint32_t mip_levels = 1;
            uint32_t temp = max_dim;
            while (temp > 1) { temp >>= 1; mip_levels++; }

            DkImageLayoutMaker layoutMaker;
            dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
            layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
            layoutMaker.format = dk_convert_format(internalformat, internalformat, GL_UNSIGNED_BYTE);
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
            while (temp > 1) { temp >>= 1; mip_levels++; }

            DkImageLayoutMaker layoutMaker;
            dkImageLayoutMakerDefaults(&layoutMaker, dk->device);
            layoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
            layoutMaker.format = dk_convert_format(internalformat, internalformat, GL_UNSIGNED_BYTE);
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
        if (expected_w < 1) expected_w = 1;
        if (expected_h < 1) expected_h = 1;
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

    uint32_t aligned_row_size = SGL_ALIGN_UP((uint32_t)(width * dst_bpp), DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    uint32_t saved_client_offset = dk->client_array_offset;
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("copy_tex_image_2d: staging buffer overflow");
        dkMemBlockDestroy(readbackMem);
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;

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
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    DkGpuAddr stagingAddr = dkMemBlockGetGpuAddr(dk->data_memblock)
                            + dk->client_array_base + stagingOffset;
    DkCopyBuf srcBuf = { stagingAddr, aligned_row_size, (uint32_t)height };
    DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };

    dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &texView, &dstRect, 0);

    cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

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

    /* CRITICAL: Mark texture as needing L2 cache barrier before first sampling.
     * CopyBufferToImage uses the DMA/2D engine which writes directly to DRAM.
     * The 3D engine's texture sampler reads through its own L2 cache.
     * Without invalidation, the sampler may read stale (zero/white) data.
     * The standalone deko3d test proves this barrier is required. */
    dk->texture_used_as_rt[handle] = true;

    /* Track this level as defined for completeness */
    dk->texture_level_mask[handle] |= (1u << level);

    /* === Step 7: Restore command buffer state === */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    /* Staging data consumed by GPU copy — restore offset to free staging space */
    dk->client_array_offset = saved_client_offset;

    dk_rebind_render_target(dk);

    SGL_TRACE_TEXTURE("copy_tex_image_2d handle=%u target=0x%X (%d,%d) %dx%d%s",
                      handle, target, x, y, width, height,
                      is_cubemap_face ? " (cubemap)" : "");
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
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;
    if (!dk->texture_initialized[handle]) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: texture %u not initialized", handle);
        return;
    }
    if (width <= 0 || height <= 0) return;

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
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    dkCmdBufClear(dk->cmdbuf);
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

    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);

    DkImageView srcView;
    dkImageViewDefaults(&srcView, srcImage);

    DkImageRect srcRect = { (uint32_t)x, dk_src_y, 0, (uint32_t)width, (uint32_t)height, 1 };
    DkCopyBuf readbackBuf = { dkMemBlockGetGpuAddr(readbackMem), (uint32_t)(width * 4), (uint32_t)height };

    dkCmdBufCopyImageToBuffer(dk->cmdbuf, &srcView, &srcRect, &readbackBuf, 0);

    cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* === Step 3: CPU copy from readback to staging with format conversion === */
    uint8_t *gpuData = (uint8_t *)dkMemBlockGetCpuAddr(readbackMem);
    GLenum tex_gl_fmt = dk->texture_gl_format[handle];
    uint32_t dst_bpp = dk_gl_format_bpp(tex_gl_fmt, GL_UNSIGNED_BYTE);

    uint32_t aligned_row_size = SGL_ALIGN_UP((uint32_t)(width * dst_bpp), DK_LINEAR_STRIDE_ALIGNMENT);
    uint32_t staging_size = aligned_row_size * height;

    uint32_t saved_client_offset = dk->client_array_offset;
    uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
    if (stagingOffset + staging_size > dk->uniform_base - dk->client_array_base) {
        SGL_ERROR_BACKEND("copy_tex_sub_image_2d: staging buffer overflow");
        dkMemBlockDestroy(readbackMem);
        return;
    }

    uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                       + dk->client_array_base + stagingOffset;

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
    dkCmdBufClear(dk->cmdbuf);
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
    dk_write_image_descriptor_to_gpu(dk, handle);

    /* === Step 5: Restore command buffer state === */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    /* Staging data consumed by GPU copy — restore offset to free staging space */
    dk->client_array_offset = saved_client_offset;

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
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (handle == 0 || handle >= SGL_MAX_TEXTURES) return;

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
            layoutMaker.flags = 0;  /* Compressed formats NOT renderable */
            layoutMaker.format = dkFormat;
            layoutMaker.type = DkImageType_Cubemap;
            layoutMaker.dimensions[0] = width;
            layoutMaker.dimensions[1] = height;
            layoutMaker.dimensions[2] = 1;

            uint32_t max_dim = (uint32_t)(width > height ? width : height);
            uint32_t mip_levels = 1;
            uint32_t temp = max_dim;
            while (temp > 1) { temp >>= 1; mip_levels++; }
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

            SGL_TRACE_TEXTURE("compressed cubemap created handle=%u %dx%d mips=%u",
                              handle, width, height, mip_levels);
        }

        /* Validate format and dimension consistency for cubemap faces */
        if (level == 0) {
            if (dkFormat != dk->texture_format[handle] ||
                internalformat != dk->texture_gl_format[handle] ||
                (uint32_t)width != dk->texture_width[handle] ||
                (uint32_t)height != dk->texture_height[handle]) {
                return;  /* Mismatch → cubemap incomplete */
            }
        }

        /* Upload compressed data to the correct cubemap face */
        if (data && imageSize > 0) {
            DkImage *texImage = &dk->textures[handle];
            uint32_t saved_client_offset = dk->client_array_offset;
            uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
            if (stagingOffset + (uint32_t)imageSize <= dk->uniform_base - dk->client_array_base) {
                uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                                   + dk->client_array_base + stagingOffset;
                memcpy(staging, data, imageSize);
                dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

                DkImageView dstView;
                dkImageViewDefaults(&dstView, texImage);
                dstView.layerOffset = face_index;
                if (level > 0) dstView.mipLevelOffset = level;

                DkCopyBuf srcBuf;
                srcBuf.addr = dkMemBlockGetGpuAddr(dk->data_memblock)
                              + dk->client_array_base + stagingOffset;
                srcBuf.rowLength = 0;
                srcBuf.imageHeight = 0;

                DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };
                dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

                DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
                dkQueueSubmitCommands(dk->queue, cmdlist);
                dkQueueWaitIdle(dk->queue);

                dkCmdBufClear(dk->cmdbuf);
                dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
                dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
                dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
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

            /* Mark cubemap as needing L2 cache barrier before first sampling.
             * The DMA copy engine writes directly to DRAM, but the texture sampler
             * reads through L2 cache. Without invalidation, the sampler may read
             * stale (zero) data from L2 instead of the freshly DMA'd face data. */
            dk->cubemap_needs_barrier[handle] = true;
            dk->texture_used_as_rt[handle] = true;
        }

        SGL_TRACE_TEXTURE("compressed cubemap face %d handle=%u level=%d %dx%d",
                          face_index, handle, level, width, height);
        return;
    }

    /* === Mip level > 0: upload to existing 2D texture === */
    if (level > 0) {
        if (!dk->texture_initialized[handle]) {
            return;  /* No base level yet — skip silently */
        }
        uint32_t tex_mips = dk->texture_mip_levels[handle];
        if ((uint32_t)level >= tex_mips) {
            return;  /* Level out of range — texture incomplete, skip */
        }
        /* Validate dimensions match expected mip size */
        uint32_t expected_w = dk->texture_width[handle] >> level;
        uint32_t expected_h = dk->texture_height[handle] >> level;
        if (expected_w < 1) expected_w = 1;
        if (expected_h < 1) expected_h = 1;
        if ((uint32_t)width != expected_w || (uint32_t)height != expected_h) {
            return;  /* Dimension mismatch — texture incomplete, skip */
        }
        /* Validate format matches base texture */
        if (dkFormat != dk->texture_format[handle]) {
            return;
        }
        /* Upload compressed data to specific mip level */
        if (data && imageSize > 0) {
            DkImage *texImage = &dk->textures[handle];
            uint32_t saved_client_offset = dk->client_array_offset;
            uint32_t stagingOffset = SGL_ALIGN_UP(dk->client_array_offset, DK_LINEAR_STRIDE_ALIGNMENT);
            if (stagingOffset + (uint32_t)imageSize <= dk->uniform_base - dk->client_array_base) {
                uint8_t *staging = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock)
                                   + dk->client_array_base + stagingOffset;
                memcpy(staging, data, imageSize);
                dk->client_array_offset = stagingOffset + (uint32_t)imageSize;

                DkImageView dstView;
                dkImageViewDefaults(&dstView, texImage);
                dstView.mipLevelOffset = level;

                DkCopyBuf srcBuf;
                srcBuf.addr = dkMemBlockGetGpuAddr(dk->data_memblock)
                              + dk->client_array_base + stagingOffset;
                srcBuf.rowLength = 0;
                srcBuf.imageHeight = 0;

                DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };
                dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

                /* Flush GPU for mip upload (same pattern as non-compressed mips) */
                DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
                dkQueueSubmitCommands(dk->queue, cmdlist);
                dkQueueWaitIdle(dk->queue);

                dkCmdBufClear(dk->cmdbuf);
                dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
                dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
                dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
                dk->descriptors_bound = true;

                dk->client_array_offset = saved_client_offset;
                dk_rebind_render_target(dk);
            }
        }
        dk->texture_level_mask[handle] |= (1u << level);
        SGL_TRACE_TEXTURE("compressed_texture_image_2d handle=%u mip level %d %dx%d", handle, level, width, height);
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

            DkImageRect dstRect = { 0, 0, 0, (uint32_t)width, (uint32_t)height, 1 };
            dkCmdBufCopyBufferToImage(dk->cmdbuf, &srcBuf, &dstView, &dstRect, 0);

            /* Submit and wait for copy to complete — MUST happen before
             * restoring client_array_offset, otherwise subsequent mip uploads
             * overwrite the staging data before the GPU copies level 0. */
            DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
            dkQueueSubmitCommands(dk->queue, cmdlist);
            dkQueueWaitIdle(dk->queue);

            dkCmdBufClear(dk->cmdbuf);
            dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
            dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
            dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
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
    dk->texture_gl_type[handle] = 0;  /* Compressed — no GL type */
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

    /* Save staging offset — restore after GPU copy (staging is temporary) */
    uint32_t saved_client_offset = dk->client_array_offset;
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
    /* Restore — staging data consumed by GPU copy, can be reused */
    dk->client_array_offset = saved_client_offset;

    SGL_TRACE_TEXTURE("compressed_texture_sub_image_2d handle=%u offset(%d,%d) %dx%d size=%d",
                      handle, xoffset, yoffset, width, height, imageSize);
}
