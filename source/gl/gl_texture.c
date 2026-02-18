/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Texture Objects
 */

#include "gl_common.h"

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

/* Validate GLES2 format/type combinations per Table 3.4 of the GLES2 spec.
 * Returns true if the combination is valid. */
static bool sgl_validate_tex_format_type(GLenum format, GLenum type) {
    switch (format) {
        case GL_RGBA:
            return type == GL_UNSIGNED_BYTE ||
                   type == GL_UNSIGNED_SHORT_4_4_4_4 ||
                   type == GL_UNSIGNED_SHORT_5_5_5_1;
        case GL_RGB:
            return type == GL_UNSIGNED_BYTE ||
                   type == GL_UNSIGNED_SHORT_5_6_5;
        case GL_LUMINANCE_ALPHA:
        case GL_LUMINANCE:
        case GL_ALPHA:
            return type == GL_UNSIGNED_BYTE;
        default:
            return false;
    }
}

GL_APICALL void GL_APIENTRY glGenTextures(GLsizei n, GLuint *textures) {
    GET_CTX();

    if (n < 0 || !textures) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

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
            if (ctx->bound_textures[unit] == id) {
                ctx->bound_textures[unit] = 0;
            }
        }

        sgl_res_mgr_free_texture(&ctx->res_mgr, id);
    }

    SGL_TRACE_TEXTURE("glDeleteTextures(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsTexture(GLuint texture) {
    GET_CTX_RET(GL_FALSE);
    return GET_TEXTURE(texture) ? GL_TRUE : GL_FALSE;
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

    /* Validate that texture name exists (was generated by glGenTextures) */
    if (texture != 0 && !GET_TEXTURE(texture)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    ctx->bound_textures[ctx->active_texture_unit] = texture;

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

    /* Validate level */
    if (level < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate dimensions */
    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

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

    /* GLES2: internalformat must equal format */
    if ((GLenum)internalformat != format) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Validate format/type combination (GLES2 Table 3.4) */
    if (!sgl_validate_tex_format_type(format, type)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Empty texture - silently return */
    if (width == 0 || height == 0) {
        return;
    }

    /* Get bound texture */
    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex || tex_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Update GL-level texture state */
    tex->used = true;  /* Mark texture as used (important for draw-time binding) */
    tex->width = width;
    tex->height = height;
    tex->internal_format = internalformat;
    /* For cubemap faces, store the parent cubemap target */
    if (sgl_is_cubemap_face(target)) {
        tex->target = GL_TEXTURE_CUBE_MAP;
    } else {
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
    if (level < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate format/type combination (GLES2 Table 3.4) */
    if (!sgl_validate_tex_format_type(format, type)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (width == 0 || height == 0) return;

    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex || tex_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Bounds checking: offsets + size must fit within texture */
    if (xoffset < 0 || yoffset < 0 ||
        xoffset + width > (GLsizei)tex->width ||
        yoffset + height > (GLsizei)tex->height) {
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

    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            if (param != GL_NEAREST && param != GL_LINEAR &&
                param != GL_NEAREST_MIPMAP_NEAREST && param != GL_LINEAR_MIPMAP_NEAREST &&
                param != GL_NEAREST_MIPMAP_LINEAR && param != GL_LINEAR_MIPMAP_LINEAR) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            tex->min_filter = (GLenum)param;
            break;
        case GL_TEXTURE_MAG_FILTER:
            if (param != GL_NEAREST && param != GL_LINEAR) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            tex->mag_filter = (GLenum)param;
            break;
        case GL_TEXTURE_WRAP_S:
            if (param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_MIRRORED_REPEAT) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            tex->wrap_s = (GLenum)param;
            break;
        case GL_TEXTURE_WRAP_T:
            if (param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_MIRRORED_REPEAT) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            tex->wrap_t = (GLenum)param;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
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

    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
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
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }
}

GL_APICALL void GL_APIENTRY glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    GET_CTX();
    if (!params) return;

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
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
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
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
    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    if (tex_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
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

    /* GLES2: border must be 0 */
    if (border != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (level != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex || tex_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Update GL-level texture state */
    tex->used = true;  /* Mark texture as used (critical for draw-time binding) */
    tex->width = width;
    tex->height = height;
    tex->internal_format = internalformat;
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

    if (level != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex || tex_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Validate offsets against texture dimensions */
    if (xoffset < 0 || yoffset < 0 ||
        xoffset + width > (GLsizei)tex->width ||
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
    (void)border;

    /* Ensure frame is ready before GPU work */
    sgl_ensure_frame_ready();

    GET_CTX();
    CHECK_BACKEND();

    /* Validate target: GL_TEXTURE_2D or cubemap face */
    if (target != GL_TEXTURE_2D && !sgl_is_cubemap_face(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (level < 0) {
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
    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex || tex_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Update GL-level texture state */
    tex->used = true;
    tex->width = width;
    tex->height = height;
    tex->internal_format = internalformat;
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

    GLuint tex_id = ctx->bound_textures[ctx->active_texture_unit];
    sgl_texture_t *tex = GET_TEXTURE(tex_id);
    if (!tex || tex_id == 0) {
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
