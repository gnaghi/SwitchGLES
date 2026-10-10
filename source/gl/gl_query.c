/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Query and Misc Functions
 */

#include "gl_common.h"
#include "../util/sgl_perf.h"
#include <GLES3/gl3.h>
#include <string.h>
#include <stdio.h>

/* Compressed texture formats supported by deko3d (Tegra X1 Maxwell GPU) */
static const GLint s_compressed_formats[] = {
    /* ETC1 */
    GL_ETC1_RGB8_OES,
    /* ETC2/EAC */
    GL_COMPRESSED_RGB8_ETC2,
    GL_COMPRESSED_SRGB8_ETC2,
    GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
    GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2,
    GL_COMPRESSED_RGBA8_ETC2_EAC,
    GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC,
    GL_COMPRESSED_R11_EAC,
    GL_COMPRESSED_SIGNED_R11_EAC,
    GL_COMPRESSED_RG11_EAC,
    GL_COMPRESSED_SIGNED_RG11_EAC,
    /* S3TC/DXT */
    GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
    GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
    GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
    GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
    /* ASTC LDR */
    GL_COMPRESSED_RGBA_ASTC_4x4_KHR,
    GL_COMPRESSED_RGBA_ASTC_5x4_KHR,
    GL_COMPRESSED_RGBA_ASTC_5x5_KHR,
    GL_COMPRESSED_RGBA_ASTC_6x5_KHR,
    GL_COMPRESSED_RGBA_ASTC_6x6_KHR,
    GL_COMPRESSED_RGBA_ASTC_8x5_KHR,
    GL_COMPRESSED_RGBA_ASTC_8x6_KHR,
    GL_COMPRESSED_RGBA_ASTC_8x8_KHR,
    GL_COMPRESSED_RGBA_ASTC_10x5_KHR,
    GL_COMPRESSED_RGBA_ASTC_10x6_KHR,
    GL_COMPRESSED_RGBA_ASTC_10x8_KHR,
    GL_COMPRESSED_RGBA_ASTC_10x10_KHR,
    GL_COMPRESSED_RGBA_ASTC_12x10_KHR,
    GL_COMPRESSED_RGBA_ASTC_12x12_KHR,
};
#define NUM_COMPRESSED_FORMATS (sizeof(s_compressed_formats) / sizeof(s_compressed_formats[0]))

/* GLES 3.0 64-bit limits (glGetInteger64v; glGetIntegerv clamps them to
 * INT_MAX). Indices are fetched as 32-bit values (GL_UNSIGNED_INT, uint8 is
 * widened to uint16), and glWaitSync never waits (gl_sync.c). */
#define SGL_MAX_ELEMENT_INDEX 0xFFFFFFFFll
#define SGL_MAX_SERVER_WAIT_TIMEOUT 0ll

/* Extensions, in the order glGetString(GL_EXTENSIONS) lists them. glGetStringi
 * (ES 3.0) indexes the same table. */
static const char *const s_extensions[] = {
    "GL_OES_rgb8_rgba8",
    "GL_OES_depth24",
    "GL_OES_packed_depth_stencil",
    "GL_OES_element_index_uint",
    "GL_OES_compressed_ETC1_RGB8_texture",
    "GL_EXT_blend_minmax",
    "GL_EXT_texture_compression_s3tc",
    "GL_KHR_texture_compression_astc_ldr",
    "GL_OES_standard_derivatives",
    "GL_OES_texture_half_float",
    "GL_OES_texture_half_float_linear",
    "GL_EXT_texture_format_BGRA8888",
    "GL_ARB_framebuffer_object",
    "GL_EXT_shader_texture_lod",
    "GL_EXT_debug_marker",
};
#define NUM_EXTENSIONS (sizeof(s_extensions) / sizeof(s_extensions[0]))

/* Space-separated list of s_extensions, built on first use */
static const char *sgl_extensions_string(void) {
    static char s_buf[512];
    if (s_buf[0] == '\0') {
        size_t len = 0;
        for (size_t i = 0; i < NUM_EXTENSIONS; i++) {
            len += (size_t)snprintf(s_buf + len, sizeof(s_buf) - len, "%s%s", i ? " " : "",
                                    s_extensions[i]);
        }
    }
    return s_buf;
}

/* Error Function */

GL_APICALL GLenum GL_APIENTRY glGetError(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return GL_NO_ERROR;

    GLenum error = ctx->error;
    ctx->error = GL_NO_ERROR;
    return error;
}

/* String Queries */

GL_APICALL const GLubyte *GL_APIENTRY glGetString(GLenum name) {
    switch (name) {
        case GL_VENDOR:
            return (const GLubyte *)"SwitchGLES";
        case GL_RENDERER:
            return (const GLubyte *)"deko3d/NVIDIA Tegra X1";
        case GL_VERSION:
            return (const GLubyte *)"OpenGL ES 2.0 SwitchGLES";
        case GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte *)"OpenGL ES GLSL ES 1.00";
        case GL_EXTENSIONS:
            return (const GLubyte *)sgl_extensions_string();
        default: {
            sgl_context_t *ctx = sgl_get_current_context();
            if (!ctx)
                return NULL;
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return NULL;
        }
    }
}

/* Integer Queries */

GL_APICALL void GL_APIENTRY glGetIntegerv(GLenum pname, GLint *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (!params)
        return;

    switch (pname) {
        /* Implementation limits */
        case GL_MAX_TEXTURE_SIZE:
            *params = 8192;
            break;
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
            *params = 8192;
            break;
        case GL_MAX_VIEWPORT_DIMS:
            params[0] = 8192;
            params[1] = 8192;
            break;
        case GL_MAX_VERTEX_ATTRIBS:
            *params = 16; /* Hardware native limit (MaxNativeAttribs). Internal arrays
                           * are SGL_MAX_ATTRIBS=32 to support aliased-inactive attributes,
                           * but the advertised limit must match hardware to prevent GPU crash
                           * (mat2/3/4 consume multiple slots per attribute). */
            break;
        case GL_MAX_VERTEX_UNIFORM_VECTORS:
            *params = 256;
            break;
        case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
            *params = 256;
            break;
        case GL_MAX_VARYING_VECTORS:
            *params = 15;
            break;
        case GL_MAX_TEXTURE_IMAGE_UNITS:
            *params = SGL_MAX_TEXTURE_UNITS;
            break;
        case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
            *params = SGL_MAX_TEXTURE_UNITS;
            break;
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
            *params = SGL_MAX_TEXTURE_UNITS;
            break;
        case GL_MAX_RENDERBUFFER_SIZE:
            *params = 8192;
            break;
        case GL_MAX_DRAW_BUFFERS:
            *params = 1;
            break;

        /* Current state */
        case GL_VIEWPORT:
            params[0] = ctx->viewport_state.viewport_x;
            params[1] = ctx->viewport_state.viewport_y;
            params[2] = ctx->viewport_state.viewport_width;
            params[3] = ctx->viewport_state.viewport_height;
            break;
        case GL_SCISSOR_BOX:
            params[0] = ctx->viewport_state.scissor_x;
            params[1] = ctx->viewport_state.scissor_y;
            params[2] = ctx->viewport_state.scissor_width;
            params[3] = ctx->viewport_state.scissor_height;
            break;
        case GL_DEPTH_BITS:
            *params = 24;
            break;
        case GL_STENCIL_BITS:
            *params = 8;
            break;
        case GL_RED_BITS:
        case GL_GREEN_BITS:
        case GL_BLUE_BITS:
        case GL_ALPHA_BITS:
            *params = 8;
            break;
        case GL_SUBPIXEL_BITS:
            *params = 4;
            break;
        case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
            *params = NUM_COMPRESSED_FORMATS;
            break;
        case GL_COMPRESSED_TEXTURE_FORMATS:
            for (int i = 0; i < (int)NUM_COMPRESSED_FORMATS; i++) {
                params[i] = s_compressed_formats[i];
            }
            break;
        case GL_NUM_SHADER_BINARY_FORMATS:
            *params = 1;
            break;
        case GL_SHADER_BINARY_FORMATS:
            *params = GL_DKSH_BINARY_FORMAT_NX;
            break;
        case GL_SHADER_COMPILER:
            *params = GL_TRUE;
            break;
        case GL_GENERATE_MIPMAP_HINT:
            *params = ctx->generate_mipmap_hint;
            break;

        /* Bindings */
        case GL_ARRAY_BUFFER_BINDING:
            *params = ctx->bound_array_buffer;
            break;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING:
            *params = ctx->bound_element_buffer;
            break;
        case GL_FRAMEBUFFER_BINDING:
            *params = ctx->bound_framebuffer;
            break;
        case GL_RENDERBUFFER_BINDING:
            *params = ctx->bound_renderbuffer;
            break;
        case GL_CURRENT_PROGRAM:
            *params = ctx->current_program;
            break;
        case GL_ACTIVE_TEXTURE:
            *params = GL_TEXTURE0 + ctx->active_texture_unit;
            break;
        case GL_TEXTURE_BINDING_2D:
            *params = ctx->bound_textures[ctx->active_texture_unit];
            break;
        case GL_TEXTURE_BINDING_CUBE_MAP:
            *params = ctx->bound_cubemap_textures[ctx->active_texture_unit];
            break;

        /* Blend state */
        case GL_BLEND_SRC_RGB:
            *params = ctx->blend_state.src_rgb;
            break;
        case GL_BLEND_DST_RGB:
            *params = ctx->blend_state.dst_rgb;
            break;
        case GL_BLEND_SRC_ALPHA:
            *params = ctx->blend_state.src_alpha;
            break;
        case GL_BLEND_DST_ALPHA:
            *params = ctx->blend_state.dst_alpha;
            break;
        case GL_BLEND_EQUATION_RGB:
            *params = ctx->blend_state.equation_rgb;
            break;
        case GL_BLEND_EQUATION_ALPHA:
            *params = ctx->blend_state.equation_alpha;
            break;

        /* Depth state */
        case GL_DEPTH_FUNC:
            *params = ctx->depth_state.depth_func;
            break;
        case GL_DEPTH_WRITEMASK:
            *params = ctx->depth_state.depth_write_enabled ? GL_TRUE : GL_FALSE;
            break;

        /* Stencil state */
        case GL_STENCIL_FUNC:
            *params = ctx->depth_state.front.func;
            break;
        case GL_STENCIL_REF:
            *params = ctx->depth_state.front.ref;
            break;
        case GL_STENCIL_VALUE_MASK:
            *params = ctx->depth_state.front.func_mask;
            break;
        case GL_STENCIL_WRITEMASK:
            *params = ctx->depth_state.front.write_mask;
            break;
        case GL_STENCIL_BACK_FUNC:
            *params = ctx->depth_state.back.func;
            break;
        case GL_STENCIL_BACK_REF:
            *params = ctx->depth_state.back.ref;
            break;
        case GL_STENCIL_BACK_VALUE_MASK:
            *params = ctx->depth_state.back.func_mask;
            break;
        case GL_STENCIL_BACK_WRITEMASK:
            *params = ctx->depth_state.back.write_mask;
            break;
        case GL_STENCIL_FAIL:
            *params = ctx->depth_state.front.fail_op;
            break;
        case GL_STENCIL_PASS_DEPTH_FAIL:
            *params = ctx->depth_state.front.zfail_op;
            break;
        case GL_STENCIL_PASS_DEPTH_PASS:
            *params = ctx->depth_state.front.zpass_op;
            break;
        case GL_STENCIL_BACK_FAIL:
            *params = ctx->depth_state.back.fail_op;
            break;
        case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
            *params = ctx->depth_state.back.zfail_op;
            break;
        case GL_STENCIL_BACK_PASS_DEPTH_PASS:
            *params = ctx->depth_state.back.zpass_op;
            break;

        /* Cull state */
        case GL_CULL_FACE_MODE:
            *params = ctx->raster_state.cull_mode;
            break;
        case GL_FRONT_FACE:
            *params = ctx->raster_state.front_face;
            break;

        /* Color mask */
        case GL_COLOR_WRITEMASK:
            params[0] = ctx->color_state.mask[0] ? GL_TRUE : GL_FALSE;
            params[1] = ctx->color_state.mask[1] ? GL_TRUE : GL_FALSE;
            params[2] = ctx->color_state.mask[2] ? GL_TRUE : GL_FALSE;
            params[3] = ctx->color_state.mask[3] ? GL_TRUE : GL_FALSE;
            break;

        /* Clear values */
        case GL_DEPTH_CLEAR_VALUE:
            *params = (GLint)(ctx->depth_state.clear_depth * 2147483647.0f);
            break;
        case GL_STENCIL_CLEAR_VALUE:
            *params = ctx->depth_state.clear_stencil;
            break;

        /* Implementation info */
        case GL_SAMPLE_BUFFERS:
            *params = 0;
            break;
        case GL_SAMPLES:
            *params = 0;
            break;
        case GL_IMPLEMENTATION_COLOR_READ_TYPE:
            *params = GL_UNSIGNED_BYTE;
            break;
        case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
            *params = GL_RGBA;
            break;

        /* Pack/unpack alignment */
        case GL_PACK_ALIGNMENT:
            *params = ctx->pack_alignment;
            break;
        case GL_UNPACK_ALIGNMENT:
            *params = ctx->unpack_alignment;
            break;

        /* Boolean enable states (GLES2 spec: queryable as integer 0/1) */
        case GL_DEPTH_TEST:
            *params = ctx->depth_state.depth_test_enabled ? 1 : 0;
            break;
        case GL_STENCIL_TEST:
            *params = ctx->depth_state.stencil_test_enabled ? 1 : 0;
            break;
        case GL_BLEND:
            *params = ctx->blend_state.enabled ? 1 : 0;
            break;
        case GL_CULL_FACE:
            *params = ctx->raster_state.cull_enabled ? 1 : 0;
            break;
        case GL_SCISSOR_TEST:
            *params = ctx->viewport_state.scissor_enabled ? 1 : 0;
            break;
        case GL_POLYGON_OFFSET_FILL:
            *params = ctx->raster_state.polygon_offset_fill_enabled ? 1 : 0;
            break;
        case GL_DITHER:
            *params = ctx->dither_enabled ? 1 : 0;
            break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            *params = ctx->sample_alpha_to_coverage ? 1 : 0;
            break;
        case GL_SAMPLE_COVERAGE:
            *params = ctx->sample_coverage_enabled ? 1 : 0;
            break;
        case GL_SAMPLE_COVERAGE_INVERT:
            *params = ctx->sample_coverage_invert ? 1 : 0;
            break;

        /* Float states queried as integer (GLES2 spec §6.1.2: float→int via
         * round(clamp(f, -1, 1) * (2^31 - 1)) for signed normalization) */
        case GL_DEPTH_RANGE:
            params[0] = (GLint)(ctx->viewport_state.depth_near * 2147483647.0f);
            params[1] = (GLint)(ctx->viewport_state.depth_far * 2147483647.0f);
            break;
        case GL_COLOR_CLEAR_VALUE:
            params[0] = (GLint)(ctx->color_state.clear_color[0] * 0x7FFFFFFF);
            params[1] = (GLint)(ctx->color_state.clear_color[1] * 0x7FFFFFFF);
            params[2] = (GLint)(ctx->color_state.clear_color[2] * 0x7FFFFFFF);
            params[3] = (GLint)(ctx->color_state.clear_color[3] * 0x7FFFFFFF);
            break;
        case GL_BLEND_COLOR:
            params[0] = (GLint)(ctx->blend_state.color[0] * 0x7FFFFFFF);
            params[1] = (GLint)(ctx->blend_state.color[1] * 0x7FFFFFFF);
            params[2] = (GLint)(ctx->blend_state.color[2] * 0x7FFFFFFF);
            params[3] = (GLint)(ctx->blend_state.color[3] * 0x7FFFFFFF);
            break;
        case GL_LINE_WIDTH: {
            GLfloat f = ctx->raster_state.line_width;
            *params = (GLint)(f >= 0.0f ? (f + 0.5f) : (f - 0.5f));
            break;
        }
        case GL_POLYGON_OFFSET_FACTOR: {
            GLfloat f = ctx->raster_state.polygon_offset_factor;
            *params = (GLint)(f >= 0.0f ? (f + 0.5f) : (f - 0.5f));
            break;
        }
        case GL_POLYGON_OFFSET_UNITS: {
            GLfloat f = ctx->raster_state.polygon_offset_units;
            *params = (GLint)(f >= 0.0f ? (f + 0.5f) : (f - 0.5f));
            break;
        }
        case GL_SAMPLE_COVERAGE_VALUE: {
            GLfloat f = ctx->sample_coverage_value;
            *params = (GLint)(f >= 0.0f ? (f + 0.5f) : (f - 0.5f));
            break;
        }
        case GL_ALIASED_POINT_SIZE_RANGE:
            params[0] = 1;
            params[1] = 1;
            break;
        case GL_ALIASED_LINE_WIDTH_RANGE:
            params[0] = (GLint)SGL_MIN_LINE_WIDTH;
            params[1] = (GLint)SGL_MAX_LINE_WIDTH;
            break;

        /* GL 3.0+ queries (used by Spearmint/ioquake3) */
        case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
            *params = 1024; /* 256 vec4 * 4 components */
            break;
        case GL_MAX_SAMPLES:
            *params = 0; /* No MSAA */
            break;
        case GL_MAX_COLOR_ATTACHMENTS:
            *params = 1;
            break;
        case GL_NUM_EXTENSIONS:
            /* ES 2.0 contexts keep answering 0 (Spearmint then falls back to
             * glGetString(GL_EXTENSIONS)); ES 3.0 counts the glGetStringi table */
            *params = sgl_ctx_is_es3(ctx) ? (GLint)NUM_EXTENSIONS : 0;
            break;

        /* GLES 3.0 state (GL_INVALID_ENUM in an ES 2.0 context) */
        case GL_MAX_ELEMENT_INDEX:
        case GL_MAX_SERVER_WAIT_TIMEOUT:
            if (!sgl_ctx_is_es3(ctx)) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                break;
            }
            {
                GLint64 v = (pname == GL_MAX_ELEMENT_INDEX) ? SGL_MAX_ELEMENT_INDEX
                                                            : SGL_MAX_SERVER_WAIT_TIMEOUT;
                *params = (v > 0x7FFFFFFF) ? 0x7FFFFFFF : (GLint)v;
            }
            break;

        default:
            /* GLES 3.0 state that only exists at its default (gl_es3_defaults.c) */
            if (sgl_es3_get_integer(ctx, pname, params))
                break;
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

/* Number of values glGetIntegerv writes for pname */
static int sgl_get_value_count(GLenum pname) {
    switch (pname) {
        case GL_VIEWPORT:
        case GL_SCISSOR_BOX:
        case GL_COLOR_WRITEMASK:
        case GL_COLOR_CLEAR_VALUE:
        case GL_BLEND_COLOR:
            return 4;
        case GL_MAX_VIEWPORT_DIMS:
        case GL_DEPTH_RANGE:
        case GL_ALIASED_POINT_SIZE_RANGE:
        case GL_ALIASED_LINE_WIDTH_RANGE:
            return 2;
        case GL_COMPRESSED_TEXTURE_FORMATS:
            return (int)NUM_COMPRESSED_FORMATS;
        default:
            return 1;
    }
}

/* GLES 3.0 §6.1.1: every integer state can be read as a 64-bit integer */
GL_APICALL void GL_APIENTRY glGetInteger64v(GLenum pname, GLint64 *data) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!data)
        return;

    switch (pname) {
        case GL_MAX_ELEMENT_INDEX:
            *data = SGL_MAX_ELEMENT_INDEX;
            return;
        case GL_MAX_SERVER_WAIT_TIMEOUT:
            *data = SGL_MAX_SERVER_WAIT_TIMEOUT;
            return;
        /* Unsigned 32-bit masks: no sign extension (glGetIntegerv returns
         * 0xFFFFFFFF as -1) */
        case GL_STENCIL_VALUE_MASK:
            *data = (GLuint)ctx->depth_state.front.func_mask;
            return;
        case GL_STENCIL_BACK_VALUE_MASK:
            *data = (GLuint)ctx->depth_state.back.func_mask;
            return;
        case GL_STENCIL_WRITEMASK:
            *data = (GLuint)ctx->depth_state.front.write_mask;
            return;
        case GL_STENCIL_BACK_WRITEMASK:
            *data = (GLuint)ctx->depth_state.back.write_mask;
            return;
        default:
            break;
    }

    /* Everything else is the glGetIntegerv value (same errors) */
    GLint temp[32] = {0};
    GLenum prev_error = ctx->error;
    ctx->error = GL_NO_ERROR;
    glGetIntegerv(pname, temp);
    GLenum error = ctx->error;
    ctx->error = prev_error;
    if (error != GL_NO_ERROR) {
        sgl_set_error(ctx, error);
        return;
    }
    int count = sgl_get_value_count(pname);
    for (int i = 0; i < count; i++)
        data[i] = temp[i];
}

GL_APICALL void GL_APIENTRY glGetBooleanv(GLenum pname, GLboolean *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (!params)
        return;

    switch (pname) {
        case GL_DEPTH_TEST:
            *params = ctx->depth_state.depth_test_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_DEPTH_WRITEMASK:
            *params = ctx->depth_state.depth_write_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_STENCIL_TEST:
            *params = ctx->depth_state.stencil_test_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_BLEND:
            *params = ctx->blend_state.enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_CULL_FACE:
            *params = ctx->raster_state.cull_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_SCISSOR_TEST:
            *params = ctx->viewport_state.scissor_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_POLYGON_OFFSET_FILL:
            *params = ctx->raster_state.polygon_offset_fill_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_DITHER:
            *params = ctx->dither_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            *params = ctx->sample_alpha_to_coverage ? GL_TRUE : GL_FALSE;
            break;
        case GL_SAMPLE_COVERAGE:
            *params = ctx->sample_coverage_enabled ? GL_TRUE : GL_FALSE;
            break;
        case GL_SHADER_COMPILER:
            *params = GL_TRUE;
            break;
        case GL_SAMPLE_COVERAGE_INVERT:
            *params = ctx->sample_coverage_invert ? GL_TRUE : GL_FALSE;
            break;
        case GL_COLOR_WRITEMASK:
            params[0] = ctx->color_state.mask[0] ? GL_TRUE : GL_FALSE;
            params[1] = ctx->color_state.mask[1] ? GL_TRUE : GL_FALSE;
            params[2] = ctx->color_state.mask[2] ? GL_TRUE : GL_FALSE;
            params[3] = ctx->color_state.mask[3] ? GL_TRUE : GL_FALSE;
            break;
        default: {
            /* For float-native states, read as float then convert to boolean
             * (non-zero float → TRUE). Going through glGetIntegerv truncates
             * fractional values to 0, giving wrong boolean results. */
            switch (pname) {
                case GL_DEPTH_RANGE:
                case GL_COLOR_CLEAR_VALUE:
                case GL_BLEND_COLOR:
                case GL_LINE_WIDTH:
                case GL_POLYGON_OFFSET_FACTOR:
                case GL_POLYGON_OFFSET_UNITS:
                case GL_SAMPLE_COVERAGE_VALUE:
                case GL_DEPTH_CLEAR_VALUE:
                case GL_ALIASED_POINT_SIZE_RANGE:
                case GL_ALIASED_LINE_WIDTH_RANGE: {
                    int fcount = 1;
                    switch (pname) {
                        case GL_DEPTH_RANGE:
                        case GL_ALIASED_POINT_SIZE_RANGE:
                        case GL_ALIASED_LINE_WIDTH_RANGE:
                            fcount = 2;
                            break;
                        case GL_COLOR_CLEAR_VALUE:
                        case GL_BLEND_COLOR:
                            fcount = 4;
                            break;
                        default:
                            fcount = 1;
                            break;
                    }
                    GLfloat ftemp[4] = {0};
                    glGetFloatv(pname, ftemp);
                    for (int i = 0; i < fcount; i++)
                        params[i] = (ftemp[i] != 0.0f) ? GL_TRUE : GL_FALSE;
                    break;
                }
                default: {
                    /* Integer states → bool via glGetIntegerv */
                    int count = 1;
                    switch (pname) {
                        case GL_VIEWPORT:
                        case GL_SCISSOR_BOX:
                            count = 4;
                            break;
                        case GL_MAX_VIEWPORT_DIMS:
                            count = 2;
                            break;
                        case GL_COMPRESSED_TEXTURE_FORMATS:
                            count = (int)NUM_COMPRESSED_FORMATS;
                            break;
                        default:
                            count = 1;
                            break;
                    }
                    GLint temp[32] = {0};
                    glGetIntegerv(pname, temp);
                    for (int i = 0; i < count; i++)
                        params[i] = temp[i] ? GL_TRUE : GL_FALSE;
                    break;
                }
            }
            break;
        }
    }
}

GL_APICALL void GL_APIENTRY glGetFloatv(GLenum pname, GLfloat *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (!params)
        return;

    switch (pname) {
        /* Float-native states */
        case GL_DEPTH_RANGE:
            params[0] = ctx->viewport_state.depth_near;
            params[1] = ctx->viewport_state.depth_far;
            break;
        case GL_DEPTH_CLEAR_VALUE:
            *params = ctx->depth_state.clear_depth;
            break;
        case GL_COLOR_CLEAR_VALUE:
            params[0] = ctx->color_state.clear_color[0];
            params[1] = ctx->color_state.clear_color[1];
            params[2] = ctx->color_state.clear_color[2];
            params[3] = ctx->color_state.clear_color[3];
            break;
        case GL_BLEND_COLOR:
            params[0] = ctx->blend_state.color[0];
            params[1] = ctx->blend_state.color[1];
            params[2] = ctx->blend_state.color[2];
            params[3] = ctx->blend_state.color[3];
            break;
        case GL_LINE_WIDTH:
            *params = ctx->raster_state.line_width;
            break;
        case GL_POLYGON_OFFSET_FACTOR:
            *params = ctx->raster_state.polygon_offset_factor;
            break;
        case GL_POLYGON_OFFSET_UNITS:
            *params = ctx->raster_state.polygon_offset_units;
            break;
        case GL_SAMPLE_COVERAGE_VALUE:
            *params = ctx->sample_coverage_value;
            break;
        case GL_ALIASED_POINT_SIZE_RANGE:
            params[0] = 1.0f;
            params[1] = 1.0f;
            break;
        case GL_ALIASED_LINE_WIDTH_RANGE:
            params[0] = SGL_MIN_LINE_WIDTH;
            params[1] = SGL_MAX_LINE_WIDTH;
            break;

        /* Boolean enable states as float 0.0/1.0 */
        case GL_DEPTH_TEST:
            *params = ctx->depth_state.depth_test_enabled ? 1.0f : 0.0f;
            break;
        case GL_STENCIL_TEST:
            *params = ctx->depth_state.stencil_test_enabled ? 1.0f : 0.0f;
            break;
        case GL_BLEND:
            *params = ctx->blend_state.enabled ? 1.0f : 0.0f;
            break;
        case GL_CULL_FACE:
            *params = ctx->raster_state.cull_enabled ? 1.0f : 0.0f;
            break;
        case GL_SCISSOR_TEST:
            *params = ctx->viewport_state.scissor_enabled ? 1.0f : 0.0f;
            break;
        case GL_POLYGON_OFFSET_FILL:
            *params = ctx->raster_state.polygon_offset_fill_enabled ? 1.0f : 0.0f;
            break;
        case GL_DITHER:
            *params = ctx->dither_enabled ? 1.0f : 0.0f;
            break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            *params = ctx->sample_alpha_to_coverage ? 1.0f : 0.0f;
            break;
        case GL_SAMPLE_COVERAGE:
            *params = ctx->sample_coverage_enabled ? 1.0f : 0.0f;
            break;
        case GL_SAMPLE_COVERAGE_INVERT:
            *params = ctx->sample_coverage_invert ? 1.0f : 0.0f;
            break;
        case GL_DEPTH_WRITEMASK:
            *params = ctx->depth_state.depth_write_enabled ? 1.0f : 0.0f;
            break;
        case GL_SHADER_COMPILER:
            *params = 1.0f;
            break;

        /* Stencil masks: unsigned 32-bit values must be converted as unsigned.
         * Going through glGetIntegerv would cast 0xFFFFFFFF to -1 then to -1.0f. */
        case GL_STENCIL_VALUE_MASK:
            *params = (GLfloat)(GLuint)ctx->depth_state.front.func_mask;
            break;
        case GL_STENCIL_BACK_VALUE_MASK:
            *params = (GLfloat)(GLuint)ctx->depth_state.back.func_mask;
            break;
        case GL_STENCIL_WRITEMASK:
            *params = (GLfloat)(GLuint)ctx->depth_state.front.write_mask;
            break;
        case GL_STENCIL_BACK_WRITEMASK:
            *params = (GLfloat)(GLuint)ctx->depth_state.back.write_mask;
            break;

        default: {
            /* GLES 3.0: the 64-bit value, not the INT_MAX clamp of glGetIntegerv */
            if (pname == GL_MAX_ELEMENT_INDEX && sgl_ctx_is_es3(ctx)) {
                *params = (GLfloat)SGL_MAX_ELEMENT_INDEX;
                break;
            }

            /* Fall through to glGetIntegerv for integer states → float conversion */
            int count = 1;
            switch (pname) {
                case GL_VIEWPORT:
                case GL_SCISSOR_BOX:
                case GL_COLOR_WRITEMASK:
                    count = 4;
                    break;
                case GL_MAX_VIEWPORT_DIMS:
                    count = 2;
                    break;
                case GL_COMPRESSED_TEXTURE_FORMATS:
                    count = (int)NUM_COMPRESSED_FORMATS;
                    break;
                default:
                    count = 1;
                    break;
            }
            GLint temp[32] = {0};
            glGetIntegerv(pname, temp);
            for (int i = 0; i < count; i++)
                params[i] = (GLfloat)temp[i];
            break;
        }
    }
}

/* Flush/Finish */

GL_APICALL void GL_APIENTRY glFlush(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    SGL_PERF_BEGIN(perf);
    if (ctx->backend && ctx->backend->ops->flush) {
        ctx->backend->ops->flush(ctx->backend);
    }
    SGL_PERF_END(SGL_PERF_GLFLUSH, perf);
    SGL_PERF_FRAME();
}

GL_APICALL void GL_APIENTRY glFinish(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    SGL_PERF_BEGIN(perf);
    if (ctx->backend && ctx->backend->ops->finish) {
        ctx->backend->ops->finish(ctx->backend);
    }
    SGL_PERF_END(SGL_PERF_GLFINISH, perf);
    SGL_PERF_FRAME();
}

/* Hints (ignored) */

GL_APICALL void GL_APIENTRY glHint(GLenum target, GLenum mode) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    /* Validate target */
    if (target != GL_GENERATE_MIPMAP_HINT && target != GL_FRAGMENT_SHADER_DERIVATIVE_HINT_OES) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate mode */
    if (mode != GL_FASTEST && mode != GL_NICEST && mode != GL_DONT_CARE) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Store hint for query; hardware uses fixed behavior */
    if (target == GL_GENERATE_MIPMAP_HINT)
        ctx->generate_mipmap_hint = mode;
}

/* Line Width */

GL_APICALL void GL_APIENTRY glLineWidth(GLfloat width) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (width <= 0.0f) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Pure state update: recorded with the rasterizer group at the next draw
     * (dk_apply_raster), clamped there to [SGL_MIN_LINE_WIDTH,
     * SGL_MAX_LINE_WIDTH]; the stored value stays unclamped for glGet. */
    ctx->raster_state.line_width = width;
}

/* Polygon Offset */

GL_APICALL void GL_APIENTRY glPolygonOffset(GLfloat factor, GLfloat units) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    /* Recorded with the rasterizer group at the next draw (dk_apply_raster),
     * only while GL_POLYGON_OFFSET_FILL is enabled. */
    ctx->raster_state.polygon_offset_factor = factor;
    ctx->raster_state.polygon_offset_units = units;

    SGL_TRACE_STATE("glPolygonOffset(%.2f, %.2f)", factor, units);
}

/* Pixel Store */

GL_APICALL void GL_APIENTRY glPixelStorei(GLenum pname, GLint param) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    /* GLES 3.0 row length / skip / image height: only the default 0 for now */
    if (sgl_es3_pixel_store(ctx, pname, param))
        return;

    /* Only valid alignment values are 1, 2, 4, 8 */
    if (param != 1 && param != 2 && param != 4 && param != 8) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    switch (pname) {
        case GL_PACK_ALIGNMENT:
            ctx->pack_alignment = param;
            break;
        case GL_UNPACK_ALIGNMENT:
            ctx->unpack_alignment = param;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

/* Buffer Queries */

static bool sgl_get_buffer_parameter(sgl_context_t *ctx, GLenum target, GLenum pname,
                                     GLint64 *value);

GL_APICALL void GL_APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (!params)
        return;

    GLint64 value;
    if (sgl_get_buffer_parameter(ctx, target, pname, &value))
        *params = (GLint)value;
}

/* Buffer parameter of the buffer bound to target, with the GLES errors.
 * Shared by glGetBufferParameteriv and glGetBufferParameteri64v. */
static bool sgl_get_buffer_parameter(sgl_context_t *ctx, GLenum target, GLenum pname,
                                     GLint64 *value) {
    GLuint *point = sgl_buffer_binding(ctx, target);
    if (!point) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return false;
    }

    GLuint buffer_id = *point;
    if (buffer_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return false;
    }

    sgl_buffer_t *buf = GET_BUFFER(buffer_id);
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return false;
    }

    switch (pname) {
        case GL_BUFFER_SIZE:
            *value = buf->size;
            return true;
        case GL_BUFFER_USAGE:
            *value = buf->usage;
            return true;
        default:
            break;
    }

    /* GLES 3.0 mapping state */
    if (sgl_ctx_is_es3(ctx)) {
        switch (pname) {
            case GL_BUFFER_MAPPED:
                *value = buf->mapped ? GL_TRUE : GL_FALSE;
                return true;
            case GL_BUFFER_ACCESS_FLAGS:
                *value = buf->map_access;
                return true;
            case GL_BUFFER_MAP_OFFSET:
                *value = buf->map_offset;
                return true;
            case GL_BUFFER_MAP_LENGTH:
                *value = buf->map_length;
                return true;
            default:
                break;
        }
    }
    sgl_set_error(ctx, GL_INVALID_ENUM);
    return false;
}

GL_APICALL void GL_APIENTRY glGetBufferParameteri64v(GLenum target, GLenum pname,
                                                     GLint64 *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!params)
        return;
    GLint64 value;
    if (sgl_get_buffer_parameter(ctx, target, pname, &value))
        *params = value;
}

GL_APICALL void GL_APIENTRY glGetBufferPointerv(GLenum target, GLenum pname, void **params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (!params)
        return;
    GLuint *point = sgl_buffer_binding(ctx, target);
    if (!point || pname != GL_BUFFER_MAP_POINTER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_buffer_t *buf = *point ? GET_BUFFER(*point) : NULL;
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    *params = NULL;
    if (buf->mapped && ctx->backend && ctx->backend->ops->get_data_cpu_ptr)
        *params = (uint8_t *)ctx->backend->ops->get_data_cpu_ptr(ctx->backend, buf->data_offset) +
                  buf->map_offset;
}

/* Shader Compiler (stubs) */

GL_APICALL void GL_APIENTRY glReleaseShaderCompiler(void) {
    /* No-op - libuam manages its own resources per-compiler instance */
}

GL_APICALL void GL_APIENTRY glShaderBinary(GLsizei count, const GLuint *shaders,
                                           GLenum binaryformat, const void *binary,
                                           GLsizei length) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    /* Validate binary format FIRST — dEQP expects GL_INVALID_ENUM for invalid
     * format even when other params are bad (per GLES2 spec error precedence). */
    if (binaryformat != GL_DKSH_BINARY_FORMAT_NX) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (count < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (count == 0 || !shaders || !binary || length <= 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Load precompiled DKSH shader binary into each specified shader */
    for (GLsizei i = 0; i < count; i++) {
        sgl_shader_t *shader = GET_SHADER(shaders[i]);
        if (!shader)
            continue;

        if (ctx->backend && ctx->backend->ops->load_shader_binary) {
            if (ctx->backend->ops->load_shader_binary(ctx->backend, shaders[i], binary, length)) {
                shader->compiled = true;
            }
        }
    }
}

GL_APICALL void GL_APIENTRY glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                                       GLint *range, GLint *precision) {
    /* Validate shadertype */
    if (shadertype != GL_VERTEX_SHADER && shadertype != GL_FRAGMENT_SHADER) {
        sgl_context_t *ctx = sgl_get_current_context();
        if (!ctx)
            return;
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Tegra X1 Maxwell GPU: all precisions map to full IEEE 754 (same for VS and FS) */
    switch (precisiontype) {
        case GL_LOW_FLOAT:
        case GL_MEDIUM_FLOAT:
        case GL_HIGH_FLOAT:
            if (range) {
                range[0] = 127;
                range[1] = 127;
            }
            if (precision)
                *precision = 23;
            break;
        case GL_LOW_INT:
        case GL_MEDIUM_INT:
        case GL_HIGH_INT:
            if (range) {
                range[0] = 31;
                range[1] = 30;
            }
            if (precision)
                *precision = 0;
            break;
        default: {
            sgl_context_t *ctx = sgl_get_current_context();
            if (!ctx)
                return;
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
        }
    }
}

/* Sample Coverage (stubs) */

GL_APICALL void GL_APIENTRY glSampleCoverage(GLfloat value, GLboolean invert) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    /* Store values for glGetFloatv/glGetBooleanv query (MSAA not supported on hardware) */
    ctx->sample_coverage_value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    ctx->sample_coverage_invert = invert != 0;
}

/* Indexed string query (ES 3.0 §6.1.6). ES 2.0 contexts have no such entry
 * point; they keep the former behaviour (an empty string, no error) because
 * Spearmint loads it through eglGetProcAddress. */

GL_APICALL const GLubyte *GL_APIENTRY glGetStringi(GLenum name, GLuint index) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !sgl_ctx_is_es3(ctx))
        return (const GLubyte *)"";

    if (name != GL_EXTENSIONS) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return NULL;
    }
    if (index >= NUM_EXTENSIONS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return NULL;
    }
    return (const GLubyte *)s_extensions[index];
}

/* Fixed-function stubs (loaded by Spearmint QGL_1_1_PROCS but never called by renderergl2) */

GL_APICALL void GL_APIENTRY glFogf(GLenum pname, GLfloat param) {
    (void)pname;
    (void)param;
}

GL_APICALL void GL_APIENTRY glFogfv(GLenum pname, const GLfloat *params) {
    (void)pname;
    (void)params;
}

/* GL_EXT_debug_marker — no-op stubs (spec says these must never report errors) */

GL_APICALL void GL_APIENTRY glInsertEventMarkerEXT(GLsizei length, const GLchar *marker) {
    (void)length;
    (void)marker;
}

GL_APICALL void GL_APIENTRY glPushGroupMarkerEXT(GLsizei length, const GLchar *marker) {
    (void)length;
    (void)marker;
}

GL_APICALL void GL_APIENTRY glPopGroupMarkerEXT(void) {
}

/* glDepthRangef is implemented in gl_clear.c */
