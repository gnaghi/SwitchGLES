/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Texture Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

/* Compute expected imageSize for compressed textures.
 * Returns 0 if the format is unknown (caller should skip validation). */
static GLsizei sgl_compressed_image_size(GLenum format, GLsizei width, GLsizei height) {
    /* All block-compressed formats use ceil(dim / blockW) * ceil(dim / blockH) * bytesPerBlock */
    int bw = 4, bh = 4, bpb = 0;

    switch (format) {
        /* 8 bytes/block, 4x4 */
        case GL_ETC1_RGB8_OES:
        case GL_COMPRESSED_RGB8_ETC2:
        case GL_COMPRESSED_SRGB8_ETC2:
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_R11_EAC:
        case GL_COMPRESSED_SIGNED_R11_EAC:
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
            bpb = 8; break;

        /* 16 bytes/block, 4x4 */
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
        case GL_COMPRESSED_RG11_EAC:
        case GL_COMPRESSED_SIGNED_RG11_EAC:
        case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
            bpb = 16; break;

        /* ASTC — all 16 bytes/block, varying block sizes */
        case GL_COMPRESSED_RGBA_ASTC_4x4_KHR:   bw=4;  bh=4;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_5x4_KHR:   bw=5;  bh=4;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_5x5_KHR:   bw=5;  bh=5;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_6x5_KHR:   bw=6;  bh=5;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_6x6_KHR:   bw=6;  bh=6;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_8x5_KHR:   bw=8;  bh=5;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_8x6_KHR:   bw=8;  bh=6;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_8x8_KHR:   bw=8;  bh=8;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_10x5_KHR:  bw=10; bh=5;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_10x6_KHR:  bw=10; bh=6;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_10x8_KHR:  bw=10; bh=8;  bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_10x10_KHR: bw=10; bh=10; bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_12x10_KHR: bw=12; bh=10; bpb=16; break;
        case GL_COMPRESSED_RGBA_ASTC_12x12_KHR: bw=12; bh=12; bpb=16; break;

        default:
            return 0; /* Unknown format — skip validation */
    }

    int blocks_w = (width + bw - 1) / bw;
    int blocks_h = (height + bh - 1) / bh;
    return (GLsizei)(blocks_w * blocks_h * bpb);
}

/* Check if a format enum is a valid GLES2 texture format */
static bool sgl_is_valid_tex_format(GLenum format) {
    return format == GL_RGBA || format == GL_RGB ||
           format == GL_LUMINANCE_ALPHA || format == GL_LUMINANCE ||
           format == GL_ALPHA || format == GL_DEPTH_COMPONENT ||
           format == GL_BGRA_EXT;
}

/* Check if a type enum is a valid GLES2 texture type */
static bool sgl_is_valid_tex_type(GLenum type) {
    return type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_5_6_5 ||
           type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1 ||
           type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT || type == GL_FLOAT ||
           type == GL_HALF_FLOAT_OES;
}

/* Validate GLES2 format/type combinations per Table 3.4 of the GLES2 spec.
 * Returns:  0 = valid
 *           GL_INVALID_ENUM = format or type is not a recognized value
 *           GL_INVALID_OPERATION = format and type are valid but incompatible */
static GLenum sgl_validate_tex_format_type(GLenum format, GLenum type) {
    if (!sgl_is_valid_tex_format(format) || !sgl_is_valid_tex_type(type))
        return GL_INVALID_ENUM;

    /* Check valid combinations per GLES2 spec Table 3.4
     * + GL_OES_texture_half_float: allows GL_HALF_FLOAT_OES with all color formats */
    switch (format) {
        case GL_RGBA:
            if (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
                type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_HALF_FLOAT_OES)
                return 0;
            return GL_INVALID_OPERATION;
        case GL_RGB:
            if (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_5_6_5 ||
                type == GL_HALF_FLOAT_OES)
                return 0;
            return GL_INVALID_OPERATION;
        case GL_LUMINANCE_ALPHA:
        case GL_LUMINANCE:
        case GL_ALPHA:
            if (type == GL_UNSIGNED_BYTE || type == GL_HALF_FLOAT_OES)
                return 0;
            return GL_INVALID_OPERATION;
        case GL_DEPTH_COMPONENT:
            if (type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT)
                return 0;
            return GL_INVALID_OPERATION;
        case GL_BGRA_EXT:
            if (type == GL_UNSIGNED_BYTE)
                return 0;
            return GL_INVALID_OPERATION;
        default:
            return GL_INVALID_ENUM;
    }
}

GL_APICALL void GL_APIENTRY glGenTextures(GLsizei n, GLuint *textures) {
    GET_CTX();

    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (n == 0 || !textures) return;

    for (GLsizei i = 0; i < n; i++) {
        textures[i] = sgl_res_mgr_alloc_texture(&ctx->res_mgr);
        if (textures[i] == 0) {
            sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }

    SGL_TRACE_TEXTURE("glGenTextures(%d)", n);
}

GL_APICALL void GL_APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures) {
    GET_CTX();

    if (n < 0) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    if (!textures) return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = textures[i];
        if (id == 0) continue;

        for (int unit = 0; unit < SGL_MAX_TEXTURE_UNITS; unit++) {
            if (ctx->bound_textures[unit] == id)
                ctx->bound_textures[unit] = 0;
            if (ctx->bound_cubemap_textures[unit] == id)
                ctx->bound_cubemap_textures[unit] = 0;
        }

        /* GLES2 spec §5.4.1: "If a texture object is deleted while its image
         * is attached to one or more attachment points in the currently bound
         * framebuffer, then it is as if FramebufferTexture2D had been called,
         * with a texture of 0, for each attachment point [...] Note that the
         * texture image is specifically not detached from any other framebuffer
         * objects. Detaching the texture image from any other framebuffer
         * objects is the responsibility of the application."
         * Only detach from the CURRENTLY BOUND FBO, not all FBOs. */
        sgl_texture_t *tex = GET_TEXTURE(id);
        bool detached = false;
        if (ctx->bound_framebuffer != 0) {
            sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
            if (fbo && fbo->used) {
                if (!fbo->color_is_renderbuffer && fbo->color_attachment == id) {
                    fbo->color_attachment = 0; fbo->color_textarget = 0;
                    if (tex) tex->fbo_ref_count--;
                    detached = true;
                }
                if (!fbo->depth_is_renderbuffer && fbo->depth_attachment == id) {
                    fbo->depth_attachment = 0; fbo->depth_textarget = 0;
                    if (tex) tex->fbo_ref_count--;
                    detached = true;
                }
                if (!fbo->stencil_is_renderbuffer && fbo->stencil_attachment == id) {
                    fbo->stencil_attachment = 0; fbo->stencil_textarget = 0;
                    if (tex) tex->fbo_ref_count--;
                    detached = true;
                }
            }
        }

        /* Re-bind FBO to update backend render target BEFORE deleting */
        if (detached && ctx->backend && ctx->backend->ops->bind_framebuffer) {
            sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
            if (fbo) {
                ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                     fbo->color_attachment, fbo->depth_attachment,
                                                     fbo->color_is_renderbuffer,
                                                     fbo->depth_is_renderbuffer,
                                                     fbo->stencil_attachment,
                                                     fbo->stencil_is_renderbuffer);
            }
        }

        /* Check if texture is still referenced by non-current FBOs.
         * If so, defer GPU deletion until all FBO references are released. */
        if (tex && tex->fbo_ref_count > 0) {
            tex->delete_pending = true;
            /* Mark name as unused (glIsTexture returns false) but keep GPU data */
            tex->used = false;
        } else {
            /* No FBO references — safe to free GPU memory immediately */
            if (ctx->backend && ctx->backend->ops && ctx->backend->ops->delete_texture) {
                ctx->backend->ops->delete_texture(ctx->backend, id);
            }
            sgl_res_mgr_free_texture(&ctx->res_mgr, id);
        }

        /* Remove from overflow list if present */
        for (int j = 0; j < ctx->res_mgr.num_overflow_textures; j++) {
            if (ctx->res_mgr.overflow_texture_ids[j] == id) {
                ctx->res_mgr.overflow_texture_ids[j] = ctx->res_mgr.overflow_texture_ids[--ctx->res_mgr.num_overflow_textures];
                ctx->res_mgr.overflow_texture_targets[j] = ctx->res_mgr.overflow_texture_targets[ctx->res_mgr.num_overflow_textures];
                break;
            }
        }
    }

    SGL_TRACE_TEXTURE("glDeleteTextures(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsTexture(GLuint texture) {
    GET_CTX_RET(GL_FALSE);
    if (texture == 0) return GL_FALSE;
    sgl_texture_t *tex = GET_TEXTURE(texture);
    /* GLES2: name becomes a texture object only after first glBindTexture */
    if (tex && tex->target != 0) return GL_TRUE;
    /* Check overflow IDs */
    for (int i = 0; i < ctx->res_mgr.num_overflow_textures; i++) {
        if (ctx->res_mgr.overflow_texture_ids[i] == texture)
            return GL_TRUE;
    }
    return GL_FALSE;
}

GL_APICALL void GL_APIENTRY glActiveTexture(GLenum texture) {
    GET_CTX();

    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + SGL_MAX_TEXTURE_UNITS) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    ctx->active_texture_unit = texture - GL_TEXTURE0;
    SGL_TRACE_TEXTURE("glActiveTexture(GL_TEXTURE%d)", ctx->active_texture_unit);
}

GL_APICALL void GL_APIENTRY glBindTexture(GLenum target, GLuint texture) {
    GET_CTX();

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* GLES2 spec: binding an unused name implicitly creates the object */
    if (texture != 0) {
        sgl_texture_t *tex = GET_TEXTURE(texture);
        if (!tex) {
            /* Implicitly create if ID is in valid range */
            if (texture > 0 && texture < SGL_MAX_TEXTURES) {
                memset(&ctx->res_mgr.textures[texture], 0, sizeof(sgl_texture_t));
                ctx->res_mgr.textures[texture].used = true;
                ctx->res_mgr.textures[texture].min_filter = GL_NEAREST_MIPMAP_LINEAR;
                ctx->res_mgr.textures[texture].mag_filter = GL_LINEAR;
                ctx->res_mgr.textures[texture].wrap_s = GL_REPEAT;
                ctx->res_mgr.textures[texture].wrap_t = GL_REPEAT;
                tex = &ctx->res_mgr.textures[texture];
            } else {
                /* Overflow: ID outside array range. Track for glIsTexture. */
                bool found = false;
                for (int i = 0; i < ctx->res_mgr.num_overflow_textures; i++) {
                    if (ctx->res_mgr.overflow_texture_ids[i] == texture) { found = true; break; }
                }
                if (!found && ctx->res_mgr.num_overflow_textures < SGL_MAX_OVERFLOW_IDS) {
                    int idx = ctx->res_mgr.num_overflow_textures++;
                    ctx->res_mgr.overflow_texture_ids[idx] = texture;
                    ctx->res_mgr.overflow_texture_targets[idx] = target;
                }
                /* Track binding but skip target validation (no tex struct) */
                goto do_bind;
            }
        }
        /* GLES2: once a texture is bound to a target, it stays that type forever.
         * Binding to a different target is GL_INVALID_OPERATION. */
        if (tex->target != 0 && tex->target != target) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        /* Set target on first bind (makes it a texture object) */
        if (tex->target == 0) {
            tex->target = target;
        }
    }

do_bind:
    /* Track 2D and cubemap bindings separately per GLES2 spec */
    if (target == GL_TEXTURE_CUBE_MAP) {
        ctx->bound_cubemap_textures[ctx->active_texture_unit] = texture;
    } else {
        ctx->bound_textures[ctx->active_texture_unit] = texture;
    }

    SGL_TRACE_TEXTURE("glBindTexture(0x%X, %u)", target, texture);
}

/* Helper to check if target is a valid cubemap face */
static int sgl_is_cubemap_face(GLenum target) {
    return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
           target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

GL_APICALL void GL_APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat,
                                          GLsizei width, GLsizei height, GLint border,
                                          GLenum format, GLenum type, const void *pixels) {
    /* Ensure frame is ready before GPU work */
    sgl_ensure_frame_ready();

    GET_CTX();
    CHECK_BACKEND();

    /* Validate target: GL_TEXTURE_2D or one of the cubemap face targets */
    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* GLES2: border must be 0 */
    if (border != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate level: must be >= 0 and <= log2(max_texture_size) = 13 */
    if (level < 0 || level > 13) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate dimensions */
    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Normalize GL_BGRA8_EXT (0x93A1) to GL_BGRA_EXT (0x80E1).
     * Both map to the same format; BGRA8_EXT is the sized variant. */
    if ((GLenum)internalformat == GL_BGRA8_EXT) internalformat = GL_BGRA_EXT;
    if (format == GL_BGRA8_EXT) format = GL_BGRA_EXT;

    /* Check max texture size (8192 for Tegra X1) */
    if (width > 8192 || height > 8192) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Cubemaps must be square */
    if (sgl_is_cubemap_face(target) && width != height) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate format and type enums individually, then check combination.
     * GLES2 spec: GL_INVALID_ENUM for unrecognized values,
     * GL_INVALID_OPERATION for valid but incompatible combos. */
    if (!sgl_is_valid_tex_format(format) || !sgl_is_valid_tex_type(type)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* GLES2: internalformat must be a valid format value.
     * 0 is not a valid internalformat → GL_INVALID_VALUE per spec. */
    if (!sgl_is_valid_tex_format((GLenum)internalformat)) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* GLES2: internalformat must equal format */
    if ((GLenum)internalformat != format) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Validate format/type combination (GLES2 Table 3.4) */
    {
        GLenum err = sgl_validate_tex_format_type(format, type);
        if (err != 0) {
            sgl_set_error(ctx, err);
            return;
        }
    }

    /* Empty texture - silently return */
    if (width == 0 || height == 0) {
        return;
    }

    /* Get bound texture */
    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) {
        /* Default texture (ID 0): GLES2 spec allows operations on it,
         * but we don't track the default texture object. Silently no-op
         * to avoid spurious GL_INVALID_OPERATION (dEQP completeness tests
         * upload cubemap faces to the default cubemap texture). */
        return;
    }
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Update GL-level texture state.
     * Only store base dimensions (level 0). Mip levels have smaller dimensions
     * but the GL texture object tracks the level-0 size for bounds checking.
     * Apps like spearmint pre-allocate all mip levels with glTexImage2D(level, NULL)
     * before uploading data via glTexSubImage2D — storing mip dimensions would
     * cause the SubImage bounds check to reject the actual level-0 upload. */
    tex->used = true;
    if (level == 0) {
        /* Track cubemap face dimension consistency for glGenerateMipmap */
        if (sgl_is_cubemap_face(target) && tex->width > 0 &&
            (tex->width != width || tex->height != height)) {
            tex->cubemap_incomplete = true;
        }
        tex->width = width;
        tex->height = height;
        /* Store actual sized format for half-float textures so
         * glCheckFramebufferStatus can detect them as non-color-renderable
         * (GL_OES_texture_half_float does NOT imply color-renderable). */
        if (type == GL_HALF_FLOAT_OES) {
            if (internalformat == GL_RGBA) tex->internal_format = GL_RGBA16F;
            else if (internalformat == GL_RGB) tex->internal_format = GL_RGB16F;
            else tex->internal_format = internalformat;
        } else {
            tex->internal_format = internalformat;
        }
    } else if (sgl_is_cubemap_face(target) && tex->width > 0) {
        /* Mip level > 0: check dimensions match expected mip chain.
         * Per GLES2 §3.7.10, a cubemap with inconsistent mip dimensions
         * is incomplete when using mipmap filtering → black fallback. */
        int expected_w = tex->width >> level;
        int expected_h = tex->height >> level;
        if (expected_w < 1) expected_w = 1;
        if (expected_h < 1) expected_h = 1;
        if (width != expected_w || height != expected_h) {
            tex->cubemap_incomplete = true;
        }
    }
    /* For cubemap faces, store the parent cubemap target */
    if (sgl_is_cubemap_face(target)) {
        tex->target = GL_TEXTURE_CUBE_MAP;
    } else if (level == 0) {
        tex->target = target;
    }

    /* Delegate to backend for actual GPU texture creation and upload */
    if (ctx->backend->ops->texture_image_2d) {
        ctx->backend->ops->texture_image_2d(ctx->backend, tex_id,
                                            target, level, internalformat,
                                            width, height, border,
                                            format, type, pixels);
    }

    SGL_TRACE_TEXTURE("glTexImage2D(target=0x%X, %dx%d, format=0x%X)", target, width, height, format);
}

GL_APICALL void GL_APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                             GLsizei width, GLsizei height, GLenum format,
                                             GLenum type, const void *pixels) {
    /* Ensure frame is ready before GPU work */
    sgl_ensure_frame_ready();

    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level > 13) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate format/type combination */
    {
        GLenum err = sgl_validate_tex_format_type(format, type);
        if (err != 0) {
            sgl_set_error(ctx, err);
            return;
        }
    }

    /* Validate offsets BEFORE the width/height==0 early return —
     * dEQP neg_offset tests pass width=0,height=0 with xoffset=-1. */
    if (xoffset < 0 || yoffset < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (width == 0 || height == 0) return;

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) return;  /* Default texture — no-op */
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Bounds checking: offsets + size must fit within mip-level dimensions. */
    GLsizei level_w = (GLsizei)tex->width;
    GLsizei level_h = (GLsizei)tex->height;
    for (GLint l = 0; l < level; l++) {
        level_w = level_w > 1 ? level_w >> 1 : 1;
        level_h = level_h > 1 ? level_h >> 1 : 1;
    }
    if (xoffset + width > level_w || yoffset + height > level_h) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Delegate to backend for actual GPU texture update */
    if (ctx->backend->ops->texture_sub_image_2d) {
        ctx->backend->ops->texture_sub_image_2d(ctx->backend, tex_id,
                                                 target, level,
                                                 xoffset, yoffset,
                                                 width, height,
                                                 format, type, pixels);
    }

    SGL_TRACE_TEXTURE("glTexSubImage2D(offset=%d,%d size=%dx%d)", xoffset, yoffset, width, height);
}

GL_APICALL void GL_APIENTRY glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    glTexParameteri(target, pname, (GLint)param);
}

GL_APICALL void GL_APIENTRY glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (!params) {
        GET_CTX();
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    glTexParameteri(target, pname, (GLint)params[0]);
}

GL_APICALL void GL_APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    GET_CTX();

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate pname and param BEFORE checking texture binding.
     * GLES2 spec: errors must be generated even for default texture object. */
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            if (param != GL_NEAREST && param != GL_LINEAR &&
                param != GL_NEAREST_MIPMAP_NEAREST && param != GL_LINEAR_MIPMAP_NEAREST &&
                param != GL_NEAREST_MIPMAP_LINEAR && param != GL_LINEAR_MIPMAP_LINEAR) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            break;
        case GL_TEXTURE_MAG_FILTER:
            if (param != GL_NEAREST && param != GL_LINEAR) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            break;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
            if (param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_MIRRORED_REPEAT) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) return;  /* Default texture — validated but silently ignored */
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) return;

    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: tex->min_filter = (GLenum)param; break;
        case GL_TEXTURE_MAG_FILTER: tex->mag_filter = (GLenum)param; break;
        case GL_TEXTURE_WRAP_S:     tex->wrap_s = (GLenum)param; break;
        case GL_TEXTURE_WRAP_T:     tex->wrap_t = (GLenum)param; break;
    }

    SGL_TRACE_TEXTURE("glTexParameteri(0x%X, 0x%X, %d)", target, pname, param);
}

GL_APICALL void GL_APIENTRY glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
    if (!params) {
        GET_CTX();
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    glTexParameteri(target, pname, params[0]);
}

GL_APICALL void GL_APIENTRY glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
    GET_CTX();
    if (!params) return;

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate pname BEFORE texture lookup — dEQP expects GL_INVALID_ENUM
     * for bad pname even when no texture is bound. */
    if (pname != GL_TEXTURE_MIN_FILTER && pname != GL_TEXTURE_MAG_FILTER &&
        pname != GL_TEXTURE_WRAP_S && pname != GL_TEXTURE_WRAP_T) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    sgl_texture_t *tex = (tex_id > 0) ? sgl_res_mgr_get_texture(&ctx->res_mgr, tex_id) : NULL;
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: *params = (GLfloat)tex->min_filter; break;
        case GL_TEXTURE_MAG_FILTER: *params = (GLfloat)tex->mag_filter; break;
        case GL_TEXTURE_WRAP_S:     *params = (GLfloat)tex->wrap_s;     break;
        case GL_TEXTURE_WRAP_T:     *params = (GLfloat)tex->wrap_t;     break;
    }
}

GL_APICALL void GL_APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    GET_CTX();
    if (!params) return;

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate pname BEFORE texture lookup */
    if (pname != GL_TEXTURE_MIN_FILTER && pname != GL_TEXTURE_MAG_FILTER &&
        pname != GL_TEXTURE_WRAP_S && pname != GL_TEXTURE_WRAP_T) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    sgl_texture_t *tex = (tex_id > 0) ? sgl_res_mgr_get_texture(&ctx->res_mgr, tex_id) : NULL;
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: *params = (GLint)tex->min_filter; break;
        case GL_TEXTURE_MAG_FILTER: *params = (GLint)tex->mag_filter; break;
        case GL_TEXTURE_WRAP_S:     *params = (GLint)tex->wrap_s;     break;
        case GL_TEXTURE_WRAP_T:     *params = (GLint)tex->wrap_t;     break;
    }
}

GL_APICALL void GL_APIENTRY glGenerateMipmap(GLenum target) {
    sgl_ensure_frame_ready();

    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Get currently bound texture */
    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex || tex->width == 0 || tex->height == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* GLES2 §3.7.11: glGenerateMipmap requires power-of-two base level
     * dimensions. NPOT textures cannot have mipmaps in GLES2. */
    if (!sgl_is_pot(tex->width) || !sgl_is_pot(tex->height)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Cubemaps must be cube-complete: all 6 faces same size, same format,
     * and dimensions must be square. */
    if (target == GL_TEXTURE_CUBE_MAP) {
        if (tex->width == 0 || tex->height == 0 || tex->width != tex->height ||
            tex->cubemap_incomplete) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    }

    /* Call backend to generate mipmaps */
    if (ctx->backend->ops->generate_mipmap) {
        ctx->backend->ops->generate_mipmap(ctx->backend, tex_id);
    }

    SGL_TRACE_TEXTURE("glGenerateMipmap(0x%X) tex=%u", target, tex_id);
}

GL_APICALL void GL_APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                                              GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate internalformat: only color-renderable formats are valid */
    if (internalformat != GL_RGBA && internalformat != GL_RGB &&
        internalformat != GL_LUMINANCE_ALPHA && internalformat != GL_LUMINANCE &&
        internalformat != GL_ALPHA) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* GLES2: border must be 0 */
    if (border != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate level */
    if (level < 0 || level > 13) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate dimensions */
    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Check max texture size */
    if (width > 8192 || height > 8192) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Cubemaps must be square */
    if (sgl_is_cubemap_face(target) && width != height) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Check framebuffer completeness */
    if (ctx->bound_framebuffer != 0) {
        sgl_framebuffer_t *read_fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
        if (!read_fbo || !read_fbo->color_attachment) {
            sgl_set_error(ctx, GL_INVALID_FRAMEBUFFER_OPERATION);
            return;
        }
    }

    if (width == 0 || height == 0) {
        return;
    }

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) return;  /* Default texture — no-op */
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Update GL-level texture state — only update base dimensions at level 0 */
    tex->used = true;  /* Mark texture as used (critical for draw-time binding) */
    if (level == 0) {
        tex->width = width;
        tex->height = height;
        tex->internal_format = internalformat;
    }
    tex->target = sgl_is_cubemap_face(target) ? GL_TEXTURE_CUBE_MAP : target;

    /* Delegate to backend */
    if (ctx->backend->ops->copy_tex_image_2d) {
        ctx->backend->ops->copy_tex_image_2d(ctx->backend, tex_id,
                                              target, level, internalformat,
                                              x, y, width, height);
    }

    SGL_TRACE_TEXTURE("glCopyTexImage2D(target=0x%X, %dx%d from (%d,%d))", target, width, height, x, y);
}

GL_APICALL void GL_APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                                 GLint x, GLint y, GLsizei width, GLsizei height) {
    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (level < 0 || level > 13) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate offsets BEFORE width/height==0 early return */
    if (xoffset < 0 || yoffset < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Check framebuffer completeness */
    if (ctx->bound_framebuffer != 0) {
        sgl_framebuffer_t *read_fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
        if (!read_fbo || !read_fbo->color_attachment) {
            sgl_set_error(ctx, GL_INVALID_FRAMEBUFFER_OPERATION);
            return;
        }
    }

    if (width == 0 || height == 0) {
        return;
    }

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) return;  /* Default texture — no-op */
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Validate offsets + size against texture dimensions */
    if (xoffset + width > (GLsizei)tex->width ||
        yoffset + height > (GLsizei)tex->height) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Delegate to backend */
    if (ctx->backend->ops->copy_tex_sub_image_2d) {
        ctx->backend->ops->copy_tex_sub_image_2d(ctx->backend, tex_id,
                                                  target, level,
                                                  xoffset, yoffset,
                                                  x, y, width, height);
    }

    SGL_TRACE_TEXTURE("glCopyTexSubImage2D(offset=%d,%d from (%d,%d) %dx%d)",
                      xoffset, yoffset, x, y, width, height);
}

GL_APICALL void GL_APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                                                    GLsizei width, GLsizei height, GLint border,
                                                    GLsizei imageSize, const void *data) {
    /* Ensure frame is ready before GPU work */
    sgl_ensure_frame_ready();

    GET_CTX();
    CHECK_BACKEND();

    /* Validate target: GL_TEXTURE_2D or cubemap face */
    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate compressed format is a known supported format */
    if (sgl_compressed_image_size(internalformat, 4, 4) == 0) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* GLES2: border must be 0 */
    if (border != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (level < 0 || level > 13) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (imageSize < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (width > 8192 || height > 8192) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate imageSize against expected size for this format */
    {
        GLsizei expected = sgl_compressed_image_size(internalformat, width, height);
        if (expected > 0 && imageSize != expected) {
            sgl_set_error(ctx, GL_INVALID_VALUE);
            return;
        }
    }

    /* Empty texture - silently return */
    if (width == 0 || height == 0) {
        return;
    }

    /* Get bound texture */
    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) return;  /* Default texture — no-op */
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Update GL-level texture state — only update base dimensions at level 0 */
    tex->used = true;
    if (level == 0) {
        tex->width = width;
        tex->height = height;
        tex->internal_format = internalformat;
    }
    tex->target = sgl_is_cubemap_face(target) ? GL_TEXTURE_CUBE_MAP : target;

    /* Delegate to backend for actual GPU texture creation and upload */
    if (ctx->backend->ops->compressed_texture_image_2d) {
        ctx->backend->ops->compressed_texture_image_2d(ctx->backend, tex_id,
                                                        target, level, internalformat,
                                                        width, height,
                                                        imageSize, data);
    }

    SGL_TRACE_TEXTURE("glCompressedTexImage2D(target=0x%X, %dx%d, format=0x%X, size=%d)",
                      target, width, height, internalformat, imageSize);
}

GL_APICALL void GL_APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                                       GLsizei width, GLsizei height, GLenum format,
                                                       GLsizei imageSize, const void *data) {
    /* Ensure frame is ready before GPU work */
    sgl_ensure_frame_ready();

    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (level < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (width < 0 || height < 0 || imageSize < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (xoffset < 0 || yoffset < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate imageSize against expected size for this format */
    {
        GLsizei expected = sgl_compressed_image_size(format, width, height);
        if (expected > 0 && imageSize != expected) {
            sgl_set_error(ctx, GL_INVALID_VALUE);
            return;
        }
    }

    if (width == 0 || height == 0) return;

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    if (tex_id == 0) return;  /* Default texture — no-op */
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Delegate to backend for actual GPU texture update */
    if (ctx->backend->ops->compressed_texture_sub_image_2d) {
        ctx->backend->ops->compressed_texture_sub_image_2d(ctx->backend, tex_id,
                                                            target, level,
                                                            xoffset, yoffset,
                                                            width, height,
                                                            format, imageSize, data);
    }

    SGL_TRACE_TEXTURE("glCompressedTexSubImage2D(offset=%d,%d size=%dx%d)", xoffset, yoffset, width, height);
}
