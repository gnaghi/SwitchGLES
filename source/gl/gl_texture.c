/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Texture Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

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

    if (n < 0 || !textures) return;

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
    if (!tex) return;

    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            tex->min_filter = (GLenum)param;
            break;
        case GL_TEXTURE_MAG_FILTER:
            tex->mag_filter = (GLenum)param;
            break;
        case GL_TEXTURE_WRAP_S:
            tex->wrap_s = (GLenum)param;
            break;
        case GL_TEXTURE_WRAP_T:
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
    (void)border;
    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_TEXTURE_2D) {
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
    tex->target = target;

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

    if (target != GL_TEXTURE_2D) {
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

    /* Validate target */
    if (target != GL_TEXTURE_2D) {
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
    tex->target = target;

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

    if (target != GL_TEXTURE_2D) {
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
