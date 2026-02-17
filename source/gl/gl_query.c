/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Query and Misc Functions
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

/* Compressed texture formats supported by deko3d (Tegra X1 Maxwell GPU) */
static const GLint s_compressed_formats[] = {
    /* ETC1 */
    0x8D64,  /* GL_ETC1_RGB8_OES */
    /* ETC2 */
    0x9274,  /* GL_COMPRESSED_RGB8_ETC2 */
    0x9275,  /* GL_COMPRESSED_SRGB8_ETC2 */
    0x9276,  /* GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 */
    0x9277,  /* GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2 */
    0x9278,  /* GL_COMPRESSED_RGBA8_ETC2_EAC */
    0x9279,  /* GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC */
    0x9270,  /* GL_COMPRESSED_R11_EAC */
    0x9271,  /* GL_COMPRESSED_SIGNED_R11_EAC */
    0x9272,  /* GL_COMPRESSED_RG11_EAC */
    0x9273,  /* GL_COMPRESSED_SIGNED_RG11_EAC */
    /* S3TC / BC */
    0x83F0,  /* GL_COMPRESSED_RGB_S3TC_DXT1_EXT */
    0x83F1,  /* GL_COMPRESSED_RGBA_S3TC_DXT1_EXT */
    0x83F2,  /* GL_COMPRESSED_RGBA_S3TC_DXT3_EXT */
    0x83F3,  /* GL_COMPRESSED_RGBA_S3TC_DXT5_EXT */
    /* ASTC LDR */
    0x93B0,  /* GL_COMPRESSED_RGBA_ASTC_4x4_KHR */
    0x93B1,  /* GL_COMPRESSED_RGBA_ASTC_5x4_KHR */
    0x93B2,  /* GL_COMPRESSED_RGBA_ASTC_5x5_KHR */
    0x93B3,  /* GL_COMPRESSED_RGBA_ASTC_6x5_KHR */
    0x93B4,  /* GL_COMPRESSED_RGBA_ASTC_6x6_KHR */
    0x93B5,  /* GL_COMPRESSED_RGBA_ASTC_8x5_KHR */
    0x93B6,  /* GL_COMPRESSED_RGBA_ASTC_8x6_KHR */
    0x93B7,  /* GL_COMPRESSED_RGBA_ASTC_8x8_KHR */
    0x93B8,  /* GL_COMPRESSED_RGBA_ASTC_10x5_KHR */
    0x93B9,  /* GL_COMPRESSED_RGBA_ASTC_10x6_KHR */
    0x93BA,  /* GL_COMPRESSED_RGBA_ASTC_10x8_KHR */
    0x93BB,  /* GL_COMPRESSED_RGBA_ASTC_10x10_KHR */
    0x93BC,  /* GL_COMPRESSED_RGBA_ASTC_12x10_KHR */
    0x93BD,  /* GL_COMPRESSED_RGBA_ASTC_12x12_KHR */
};
#define NUM_COMPRESSED_FORMATS (sizeof(s_compressed_formats) / sizeof(s_compressed_formats[0]))

/* Error Function */

GL_APICALL GLenum GL_APIENTRY glGetError(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx) return GL_NO_ERROR;

    GLenum error = ctx->error;
    ctx->error = GL_NO_ERROR;
    return error;
}

/* String Queries */

GL_APICALL const GLubyte *GL_APIENTRY glGetString(GLenum name) {
    switch (name) {
        case GL_VENDOR:
            return (const GLubyte*)"SwitchGLES";
        case GL_RENDERER:
            return (const GLubyte*)"deko3d/NVIDIA Tegra X1";
        case GL_VERSION:
            return (const GLubyte*)"OpenGL ES 2.0 SwitchGLES";
        case GL_SHADING_LANGUAGE_VERSION:
            return (const GLubyte*)"OpenGL ES GLSL ES 1.00 (dksh precompiled)";
        case GL_EXTENSIONS:
            return (const GLubyte*)
                "GL_OES_rgb8_rgba8 "
                "GL_OES_depth24 "
                "GL_OES_packed_depth_stencil "
                "GL_OES_element_index_uint "
                "GL_OES_texture_npot "
                "GL_OES_compressed_ETC1_RGB8_texture "
                "GL_EXT_blend_minmax "
                "GL_EXT_texture_compression_s3tc "
                "GL_KHR_texture_compression_astc_ldr";
        default:
            return NULL;
    }
}

/* Integer Queries */

GL_APICALL void GL_APIENTRY glGetIntegerv(GLenum pname, GLint *params) {
    GET_CTX();

    if (!params) return;

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
            *params = SGL_MAX_ATTRIBS;
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
            *params = SGL_MAX_TEXTURE_UNITS * 2;
            break;
        case GL_MAX_RENDERBUFFER_SIZE:
            *params = 8192;
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
            *params = 0x10DE0001;  /* GL_DKSH_BINARY_FORMAT_NX */
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
            *params = 0;  /* Not tracked separately */
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
            *params = (GLint)(ctx->depth_state.clear_depth * 0x7FFFFFFF);
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

        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

GL_APICALL void GL_APIENTRY glGetBooleanv(GLenum pname, GLboolean *params) {
    GET_CTX();

    if (!params) return;

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
        case GL_DITHER:
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
            /* Use array large enough for multi-value queries (GL_VIEWPORT, GL_SCISSOR_BOX, etc.) */
            GLint temp[4] = {0};
            glGetIntegerv(pname, temp);
            *params = temp[0] ? GL_TRUE : GL_FALSE;
            break;
        }
    }
}

GL_APICALL void GL_APIENTRY glGetFloatv(GLenum pname, GLfloat *params) {
    GET_CTX();

    if (!params) return;

    switch (pname) {
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
        default: {
            GLint temp[4] = {0};
            glGetIntegerv(pname, temp);
            *params = (GLfloat)temp[0];
            break;
        }
    }
}

/* Flush/Finish */

GL_APICALL void GL_APIENTRY glFlush(void) {
    GET_CTX();
    if (ctx->backend && ctx->backend->ops->flush) {
        ctx->backend->ops->flush(ctx->backend);
    }
}

GL_APICALL void GL_APIENTRY glFinish(void) {
    GET_CTX();
    if (ctx->backend && ctx->backend->ops->finish) {
        ctx->backend->ops->finish(ctx->backend);
    }
}

/* Hints (ignored) */

GL_APICALL void GL_APIENTRY glHint(GLenum target, GLenum mode) {
    (void)target;
    (void)mode;
    /* All hints are ignored in this implementation */
}

/* Line Width */

GL_APICALL void GL_APIENTRY glLineWidth(GLfloat width) {
    GET_CTX();

    if (width <= 0.0f) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    ctx->raster_state.line_width = width;
}

/* Polygon Offset */

GL_APICALL void GL_APIENTRY glPolygonOffset(GLfloat factor, GLfloat units) {
    GET_CTX();

    ctx->raster_state.polygon_offset_factor = factor;
    ctx->raster_state.polygon_offset_units = units;

    /* Apply to backend */
    if (ctx->backend && ctx->backend->ops->set_depth_bias) {
        ctx->backend->ops->set_depth_bias(ctx->backend, factor, units);
    }

    SGL_TRACE_STATE("glPolygonOffset(%.2f, %.2f)", factor, units);
}

/* Pixel Store */

GL_APICALL void GL_APIENTRY glPixelStorei(GLenum pname, GLint param) {
    GET_CTX();

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

GL_APICALL void GL_APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    GET_CTX();

    if (!params) return;

    GLuint buffer_id = 0;
    if (target == GL_ARRAY_BUFFER) {
        buffer_id = ctx->bound_array_buffer;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        buffer_id = ctx->bound_element_buffer;
    } else {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (buffer_id == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_buffer_t *buf = GET_BUFFER(buffer_id);
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    switch (pname) {
        case GL_BUFFER_SIZE:
            *params = (GLint)buf->size;
            break;
        case GL_BUFFER_USAGE:
            *params = buf->usage;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

/* Shader Compiler (stubs) */

GL_APICALL void GL_APIENTRY glReleaseShaderCompiler(void) {
    /* No-op - libuam manages its own resources per-compiler instance */
}

GL_APICALL void GL_APIENTRY glShaderBinary(GLsizei count, const GLuint *shaders,
                                            GLenum binaryformat, const void *binary, GLsizei length) {
    (void)binaryformat;  /* SwitchGLES only supports DKSH format */
    GET_CTX();

    if (count < 1 || !shaders || !binary || length <= 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Load precompiled DKSH shader binary into each specified shader */
    for (GLsizei i = 0; i < count; i++) {
        sgl_shader_t *shader = GET_SHADER(shaders[i]);
        if (!shader) continue;

        if (ctx->backend && ctx->backend->ops->load_shader_binary) {
            if (ctx->backend->ops->load_shader_binary(ctx->backend, shaders[i], binary, length)) {
                shader->compiled = true;
            }
        }
    }
}

GL_APICALL void GL_APIENTRY glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                                        GLint *range, GLint *precision) {
    (void)shadertype; /* Maxwell GPU uses same precision for VS and FS */

    /* Tegra X1 Maxwell GPU: all precisions map to full IEEE 754 */
    switch (precisiontype) {
        case GL_LOW_FLOAT:
        case GL_MEDIUM_FLOAT:
        case GL_HIGH_FLOAT:
            if (range) { range[0] = 127; range[1] = 127; }
            if (precision) *precision = 23;
            break;
        case GL_LOW_INT:
        case GL_MEDIUM_INT:
        case GL_HIGH_INT:
            if (range) { range[0] = 31; range[1] = 30; }
            if (precision) *precision = 0;
            break;
        default: {
            GET_CTX();
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
        }
    }
}

/* Sample Coverage (stubs) */

GL_APICALL void GL_APIENTRY glSampleCoverage(GLfloat value, GLboolean invert) {
    GET_CTX();
    /* Store values for glGetFloatv/glGetBooleanv query (MSAA not supported on hardware) */
    ctx->sample_coverage_value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    ctx->sample_coverage_invert = invert != 0;
}

/* glDepthRangef is implemented in gl_clear.c */
