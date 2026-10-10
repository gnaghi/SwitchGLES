/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - GLES 3.0 sized texture formats and immutable storage
 *
 * The GLES 3.0 texture specification rules (GLES 3.0 §3.8.3, Table 3.2 and
 * Table 3.13): the sized internal formats, the format/type pairs each of them
 * accepts, and glTexStorage2D. The deko3d image format of each sized format is
 * chosen by dk_convert_format (dk_utils.c).
 *
 * Only reached from a GLES 3.0 context: in a GLES 2.0 context the hooks below
 * return 0 and glTexImage2D / glTexSubImage2D keep their GLES 2.0 rules.
 *
 * Uploads whose data needs a conversion that is not implemented (float to
 * half float, float to the shared-exponent / packed float formats, 2_10_10_10
 * to RGB5_A1, and the depth formats other than DEPTH_COMPONENT32F) are refused
 * with GL_INVALID_OPERATION and a log line (SGL_ES3_UNSUPPORTED) instead of
 * storing wrong texels. The same formats without data (NULL, as glTexStorage2D
 * and render targets do) are accepted.
 */

#include "gl_common.h"
#include <GLES3/gl3.h>

/* One row of GLES 3.0 Table 3.2: a valid internalformat/format/type triple */
typedef struct {
    GLenum internalformat;
    GLenum format;
    GLenum type;
    bool data_supported; /* false: only a NULL (no data) upload is supported */
} sgl_es3_tex_format_t;

static const sgl_es3_tex_format_t s_sized_formats[] = {
    /* GL_RGBA */
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true},
    {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_BYTE, true},
    {GL_RGBA4, GL_RGBA, GL_UNSIGNED_BYTE, true},
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, true},
    {GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, true},
    {GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, true},
    {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, true},
    {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, true},
    {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, false},
    {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, true},
    {GL_RGBA32F, GL_RGBA, GL_FLOAT, true},
    {GL_RGBA16F, GL_RGBA, GL_FLOAT, false},
    /* GL_RGBA_INTEGER */
    {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, true},
    {GL_RGBA8I, GL_RGBA_INTEGER, GL_BYTE, true},
    {GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, true},
    {GL_RGBA16I, GL_RGBA_INTEGER, GL_SHORT, true},
    {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, true},
    {GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, true},
    {GL_RGB10_A2UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, true},
    /* GL_RGB */
    {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, true},
    {GL_RGB565, GL_RGB, GL_UNSIGNED_BYTE, true},
    {GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE, true},
    {GL_RGB8_SNORM, GL_RGB, GL_BYTE, true},
    {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, true},
    {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, true},
    {GL_RGB9_E5, GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, true},
    {GL_RGB16F, GL_RGB, GL_HALF_FLOAT, true},
    {GL_R11F_G11F_B10F, GL_RGB, GL_HALF_FLOAT, false},
    {GL_RGB9_E5, GL_RGB, GL_HALF_FLOAT, false},
    {GL_RGB32F, GL_RGB, GL_FLOAT, true},
    {GL_RGB16F, GL_RGB, GL_FLOAT, false},
    {GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT, false},
    {GL_RGB9_E5, GL_RGB, GL_FLOAT, false},
    /* GL_RGB_INTEGER */
    {GL_RGB8UI, GL_RGB_INTEGER, GL_UNSIGNED_BYTE, true},
    {GL_RGB8I, GL_RGB_INTEGER, GL_BYTE, true},
    {GL_RGB16UI, GL_RGB_INTEGER, GL_UNSIGNED_SHORT, true},
    {GL_RGB16I, GL_RGB_INTEGER, GL_SHORT, true},
    {GL_RGB32UI, GL_RGB_INTEGER, GL_UNSIGNED_INT, true},
    {GL_RGB32I, GL_RGB_INTEGER, GL_INT, true},
    /* GL_RG */
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, true},
    {GL_RG8_SNORM, GL_RG, GL_BYTE, true},
    {GL_RG16F, GL_RG, GL_HALF_FLOAT, true},
    {GL_RG32F, GL_RG, GL_FLOAT, true},
    {GL_RG16F, GL_RG, GL_FLOAT, false},
    /* GL_RG_INTEGER */
    {GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE, true},
    {GL_RG8I, GL_RG_INTEGER, GL_BYTE, true},
    {GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, true},
    {GL_RG16I, GL_RG_INTEGER, GL_SHORT, true},
    {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, true},
    {GL_RG32I, GL_RG_INTEGER, GL_INT, true},
    /* GL_RED */
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE, true},
    {GL_R8_SNORM, GL_RED, GL_BYTE, true},
    {GL_R16F, GL_RED, GL_HALF_FLOAT, true},
    {GL_R32F, GL_RED, GL_FLOAT, true},
    {GL_R16F, GL_RED, GL_FLOAT, false},
    /* GL_RED_INTEGER */
    {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, true},
    {GL_R8I, GL_RED_INTEGER, GL_BYTE, true},
    {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, true},
    {GL_R16I, GL_RED_INTEGER, GL_SHORT, true},
    {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, true},
    {GL_R32I, GL_RED_INTEGER, GL_INT, true},
    /* Depth / stencil: the layout of the Z24S8 image that DEPTH_COMPONENT16/24
     * and DEPTH24_STENCIL8 use is not verified on hardware yet, so only
     * DEPTH_COMPONENT32F (ZF32, one float per texel) takes data */
    {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, false},
    {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false},
    {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false},
    {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, true},
    {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, false},
    {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, false},
};

#define SGL_NUM_SIZED_FORMATS (sizeof(s_sized_formats) / sizeof(s_sized_formats[0]))

/* Unsized internal formats (GLES 3.0 Table 3.3, plus GL_EXT_texture_format_
 * BGRA8888 and the GLES 2.0 depth texture), which keep the GLES 2.0 rules */
static bool sgl_es3_is_unsized_format(GLenum internalformat) {
    return internalformat == GL_RGBA || internalformat == GL_RGB ||
           internalformat == GL_LUMINANCE_ALPHA || internalformat == GL_LUMINANCE ||
           internalformat == GL_ALPHA || internalformat == GL_BGRA_EXT ||
           internalformat == GL_DEPTH_COMPONENT;
}

static bool sgl_es3_is_sized_format(GLenum internalformat) {
    for (size_t i = 0; i < SGL_NUM_SIZED_FORMATS; i++)
        if (s_sized_formats[i].internalformat == internalformat)
            return true;
    return false;
}

static bool sgl_es3_is_tex_format(GLenum format) {
    switch (format) {
        case GL_RGBA:
        case GL_RGB:
        case GL_RG:
        case GL_RED:
        case GL_RGBA_INTEGER:
        case GL_RGB_INTEGER:
        case GL_RG_INTEGER:
        case GL_RED_INTEGER:
        case GL_DEPTH_COMPONENT:
        case GL_DEPTH_STENCIL:
        case GL_LUMINANCE_ALPHA:
        case GL_LUMINANCE:
        case GL_ALPHA:
        case GL_BGRA_EXT:
            return true;
        default:
            return false;
    }
}

static bool sgl_es3_is_tex_type(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_HALF_FLOAT:
        case GL_HALF_FLOAT_OES:
        case GL_FLOAT:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_10F_11F_11F_REV:
        case GL_UNSIGNED_INT_5_9_9_9_REV:
        case GL_UNSIGNED_INT_24_8:
        case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
            return true;
        default:
            return false;
    }
}

/* Formats and types of the unsized internal formats (GLES 2.0 Table 3.4 plus
 * the GLES 2.0 extensions SwitchGLES exposes). In a GLES 3.0 context another
 * format or type (a GLES 3.0 one) with an unsized internal format is a
 * combination not in Table 3.2/3.3: GL_INVALID_OPERATION, not the
 * GL_INVALID_ENUM the GLES 2.0 checks give for an enum GLES 2.0 lacks. */
static bool sgl_es3_is_unsized_format_type(GLenum format, GLenum type) {
    if (!sgl_es3_is_unsized_format(format))
        return false;
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_SHORT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
        case GL_HALF_FLOAT_OES:
            return true;
        default:
            return false;
    }
}

static const sgl_es3_tex_format_t *sgl_es3_find_format(GLenum internalformat, GLenum format,
                                                       GLenum type) {
    for (size_t i = 0; i < SGL_NUM_SIZED_FORMATS; i++) {
        const sgl_es3_tex_format_t *f = &s_sized_formats[i];
        if (f->internalformat == internalformat && f->format == format && f->type == type)
            return f;
    }
    return NULL;
}

/* GLES 3.0 checks shared by glTexImage2D and glTexSubImage2D, for a sized
 * internal format. Returns false with the error set. */
static bool sgl_es3_check_sized(sgl_context_t *ctx, GLenum internalformat, GLenum format,
                                GLenum type, bool has_data) {
    const sgl_es3_tex_format_t *f = sgl_es3_find_format(internalformat, format, type);
    if (!f) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return false;
    }
    if (has_data && !f->data_supported) {
        SGL_ES3_UNSUPPORTED(ctx, "texture upload needing a data conversion (float to half "
                                 "float / packed float, depth data other than 32F)");
        return false;
    }
    return true;
}

int sgl_es3_tex_image_validate(sgl_context_t *ctx, GLint internalformat, GLenum format,
                               GLenum type, bool has_data) {
    if (!sgl_ctx_is_es3(ctx))
        return 0;
    /* GLES 3.0 §3.8.3: INVALID_ENUM for format/type, INVALID_VALUE for
     * internalformat, INVALID_OPERATION for a combination not in Table 3.2 */
    if (!sgl_es3_is_tex_format(format) || !sgl_es3_is_tex_type(type)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return -1;
    }
    GLenum ifmt = (GLenum)internalformat;
    if (sgl_es3_is_unsized_format(ifmt)) {
        if (format != ifmt || !sgl_es3_is_unsized_format_type(format, type)) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return -1;
        }
        return 0; /* GLES 2.0 rules (Table 3.3 is the GLES 2.0 table) */
    }
    if (!sgl_es3_is_sized_format(ifmt)) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return -1;
    }
    return sgl_es3_check_sized(ctx, ifmt, format, type, has_data) ? 1 : -1;
}

int sgl_es3_tex_sub_image_validate(sgl_context_t *ctx, GLenum tex_internalformat, GLenum format,
                                   GLenum type, bool has_data) {
    if (!sgl_ctx_is_es3(ctx))
        return 0;
    if (!sgl_es3_is_tex_format(format) || !sgl_es3_is_tex_type(type)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return -1;
    }
    /* A texture never specified, or one of an unsized format (including the
     * GL_OES_texture_half_float ones recorded as RGBA16F/RGB16F): GLES 2.0 rules */
    if (tex_internalformat == 0 || type == GL_HALF_FLOAT_OES)
        return 0;
    if (!sgl_es3_is_sized_format(tex_internalformat)) {
        if (!sgl_es3_is_unsized_format_type(format, type)) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return -1;
        }
        return 0;
    }
    return sgl_es3_check_sized(ctx, tex_internalformat, format, type, has_data) ? 1 : -1;
}

/* ============================================================================
 * glTexStorage2D (GLES 3.0 §3.8.4)
 * ============================================================================ */

static bool sgl_es3_is_compressed_format(GLenum internalformat) {
    switch (internalformat) {
        case GL_COMPRESSED_R11_EAC:
        case GL_COMPRESSED_SIGNED_R11_EAC:
        case GL_COMPRESSED_RG11_EAC:
        case GL_COMPRESSED_SIGNED_RG11_EAC:
        case GL_COMPRESSED_RGB8_ETC2:
        case GL_COMPRESSED_SRGB8_ETC2:
        case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
            return true;
        default:
            return false;
    }
}

GL_APICALL void GL_APIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                                           GLsizei width, GLsizei height) {
    sgl_ensure_frame_ready();
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (sgl_es3_is_compressed_format(internalformat)) {
        SGL_ES3_UNSUPPORTED(ctx, "glTexStorage2D with a compressed format");
        return;
    }
    if (!sgl_es3_is_sized_format(internalformat)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (width < 1 || height < 1 || levels < 1) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (width > 8192 || height > 8192 || (target == GL_TEXTURE_CUBE_MAP && width != height)) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLsizei max_dim = width > height ? width : height;
    GLsizei max_levels = 1;
    while (max_dim >>= 1)
        max_levels++;
    if (levels > max_levels) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    GLuint tex_id = sgl_get_bound_texture(ctx, target);
    sgl_texture_t *tex = tex_id ? GET_TEXTURE(tex_id) : NULL;
    if (!tex || tex->immutable) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* The format/type of the first Table 3.2 row of the internal format: no
     * data is uploaded, they only select the image format */
    const sgl_es3_tex_format_t *f = NULL;
    for (size_t i = 0; i < SGL_NUM_SIZED_FORMATS && !f; i++)
        if (s_sized_formats[i].internalformat == internalformat)
            f = &s_sized_formats[i];

    tex->used = true;
    tex->width = width;
    tex->height = height;
    tex->internal_format = internalformat;
    tex->target = target;
    tex->cubemap_incomplete = false;

    GLenum first_face = target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : target;
    int faces = target == GL_TEXTURE_CUBE_MAP ? 6 : 1;
    if (ctx->backend->ops->texture_image_2d) {
        for (GLsizei level = 0; level < levels; level++) {
            GLsizei w = width >> level, h = height >> level;
            if (w < 1)
                w = 1;
            if (h < 1)
                h = 1;
            for (int face = 0; face < faces; face++)
                ctx->backend->ops->texture_image_2d(ctx->backend, tex_id,
                                                    first_face + (GLenum)face, level,
                                                    (GLint)internalformat, w, h, 0, f->format,
                                                    f->type, NULL);
        }
    }

    tex->immutable = true;
    tex->immutable_levels = levels;
}
