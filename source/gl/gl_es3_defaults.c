/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - GLES 3.0 state that only exists at its default value
 *
 * GLES 3.0 adds binding points and parameters (sampler objects, transform
 * feedback, indexed buffer bindings, draw/read buffers, 3D
 * and array textures, pixel store and texture parameters...) whose features
 * SwitchGLES does not implement yet. Applications and dEQP still set them back
 * to their defaults: dEQP-GLES3 resets the whole context after every test case
 * (framework/opengl/gluStateReset.cpp, resetStateES) and aborts the run on the
 * first GL error. So, in a GLES 3.0 context:
 *
 *  - setting the default value is accepted: the state is that value already,
 *    and can never be anything else;
 *  - every argument is validated with the errors of the GLES 3.0 spec;
 *  - a valid non-default value needs the missing feature: it sets
 *    GL_INVALID_OPERATION and is logged once (SGL_ES3_UNSUPPORTED), never
 *    silently accepted. Where no object name can exist yet (samplers,
 *    transform feedbacks: their glGen* are still stubs), binding a
 *    non-zero name is GL_INVALID_OPERATION by the spec itself;
 *  - queries return the defaults, which are therefore always the real state.
 *
 * In a GLES 2.0 context nothing here applies: the entry points defined below
 * set GL_INVALID_OPERATION (like the stubs) and the sgl_es3_* hooks return
 * false, so the GLES 2.0 code paths run unchanged.
 *
 * A plan step that implements one of these features replaces its part here.
 */

#include "gl_common.h"
#include <GLES3/gl3.h>
#include <stddef.h>

/* Limits reported for features that are not implemented yet: the GLES 3.0
 * minimums, which the planned implementations meet (docs/GLES3_PLAN.md: 24
 * uniform buffer binding points, 12 blocks per stage in deko3d uniform buffer
 * slots 2..13, 4 deko3d transform feedback buffers, 3D and array textures
 * with DkImageType_3D / _2DArray). dEQP reads them to size its resets and its
 * reference renderer (sglrReferenceContext). */
#define SGL_ES3_MAX_UNIFORM_BUFFER_BINDINGS 24
#define SGL_ES3_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS 4
#define SGL_ES3_MAX_STAGE_UNIFORM_BLOCKS 12
#define SGL_ES3_MAX_UNIFORM_BLOCK_SIZE 16384
#define SGL_ES3_MAX_3D_TEXTURE_SIZE 256
#define SGL_ES3_MAX_ARRAY_TEXTURE_LAYERS 256

/* Default-block uniform components per stage: same 256 vec4 as
 * GL_MAX_{VERTEX,FRAGMENT}_UNIFORM_VECTORS */
#define SGL_ES3_MAX_STAGE_UNIFORM_COMPONENTS 1024

/* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: DK_UNIFORM_BUF_ALIGNMENT */
#define SGL_ES3_UNIFORM_BUFFER_OFFSET_ALIGNMENT SGL_UNIFORM_ALIGNMENT

/* Same value as glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS) */
#define SGL_ES3_MAX_TEXTURE_UNITS SGL_MAX_TEXTURE_UNITS

/* Same value as glGetIntegerv(GL_MAX_VERTEX_ATTRIBS) */
#define SGL_ES3_MAX_VERTEX_ATTRIBS 16

/* Draw buffers / color attachments of a framebuffer object (GL_MAX_DRAW_BUFFERS,
 * GL_MAX_COLOR_ATTACHMENTS) */
#define SGL_ES3_MAX_DRAW_BUFFERS 1

void sgl_es3_unsupported(sgl_context_t *ctx, const char *what, bool *logged) {
    if (!*logged) {
        *logged = true;
        SGL_WARN(SGL_LOG_CAT_CORE, "[CORE] %s: GLES 3.0 feature not implemented, GL_INVALID_OPERATION",
                 what);
    }
    sgl_set_error(ctx, GL_INVALID_OPERATION);
}

/* Current context if it is a GLES 3.0 one. A GLES 2.0 context gets
 * GL_INVALID_OPERATION (an ES 3.0 entry point) and NULL. */
static sgl_context_t *sgl_es3_context(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (ctx && !sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return NULL;
    }
    return ctx;
}

/* ============================================================================
 * Capabilities (glEnable / glDisable / glIsEnabled)
 * ============================================================================ */

static bool sgl_es3_is_cap(GLenum cap) {
    return cap == GL_PRIMITIVE_RESTART_FIXED_INDEX || cap == GL_RASTERIZER_DISCARD;
}

bool sgl_es3_enable_cap(sgl_context_t *ctx, GLenum cap, bool enable) {
    if (!sgl_ctx_is_es3(ctx) || !sgl_es3_is_cap(cap))
        return false;
    /* Both are disabled by default */
    if (enable) {
        if (cap == GL_PRIMITIVE_RESTART_FIXED_INDEX)
            SGL_ES3_UNSUPPORTED(ctx, "glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX)");
        else
            SGL_ES3_UNSUPPORTED(ctx, "glEnable(GL_RASTERIZER_DISCARD)");
    }
    return true;
}

bool sgl_es3_is_enabled_cap(sgl_context_t *ctx, GLenum cap) {
    return sgl_ctx_is_es3(ctx) && sgl_es3_is_cap(cap);
}

/* ============================================================================
 * Attribute divisors
 * ============================================================================ */

GL_APICALL void GL_APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    if (index >= SGL_ES3_MAX_VERTEX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (divisor != 0)
        SGL_ES3_UNSUPPORTED(ctx, "glVertexAttribDivisor (instancing)");
}

/* ============================================================================
 * Transform feedback objects
 * ============================================================================ */

GL_APICALL void GL_APIENTRY glBindTransformFeedback(GLenum target, GLuint id) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    if (target != GL_TRANSFORM_FEEDBACK) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    /* Only the default object exists (glGenTransformFeedbacks is not
     * implemented); transform feedback is never active, so binding the
     * default object is always allowed. */
    if (id != 0)
        sgl_set_error(ctx, GL_INVALID_OPERATION);
}

GL_APICALL GLboolean GL_APIENTRY glIsTransformFeedback(GLuint id) {
    (void)id;
    sgl_es3_context();
    return GL_FALSE;
}

/* ============================================================================
 * Sampler objects
 * ============================================================================ */

GL_APICALL void GL_APIENTRY glBindSampler(GLuint unit, GLuint sampler) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    if (unit >= SGL_ES3_MAX_TEXTURE_UNITS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* No sampler object can exist (glGenSamplers is not implemented) */
    if (sampler != 0)
        sgl_set_error(ctx, GL_INVALID_OPERATION);
}

GL_APICALL GLboolean GL_APIENTRY glIsSampler(GLuint sampler) {
    (void)sampler;
    sgl_es3_context();
    return GL_FALSE;
}

/* ============================================================================
 * Buffer binding points
 * ============================================================================ */

/* Indexed binding points of target, 0 if target has none */
static GLuint sgl_es3_indexed_binding_count(GLenum target) {
    if (target == GL_UNIFORM_BUFFER)
        return SGL_ES3_MAX_UNIFORM_BUFFER_BINDINGS;
    if (target == GL_TRANSFORM_FEEDBACK_BUFFER)
        return SGL_ES3_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS;
    return 0;
}

GL_APICALL void GL_APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                                              GLintptr offset, GLsizeiptr size) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    GLuint count = sgl_es3_indexed_binding_count(target);
    if (count == 0) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= count) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* Offset and size are only checked for a non-zero buffer (§2.10.1.1) */
    if (buffer != 0) {
        if (size <= 0) {
            sgl_set_error(ctx, GL_INVALID_VALUE);
            return;
        }
        if (target == GL_TRANSFORM_FEEDBACK_BUFFER && ((offset | size) & 3) != 0) {
            sgl_set_error(ctx, GL_INVALID_VALUE);
            return;
        }
        if (target == GL_UNIFORM_BUFFER && (offset % SGL_UNIFORM_ALIGNMENT) != 0) {
            sgl_set_error(ctx, GL_INVALID_VALUE);
            return;
        }
        SGL_ES3_UNSUPPORTED(ctx, "glBindBufferRange (uniform / transform feedback buffers)");
    }
}

GL_APICALL void GL_APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    GLuint count = sgl_es3_indexed_binding_count(target);
    if (count == 0) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (index >= count) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (buffer != 0)
        SGL_ES3_UNSUPPORTED(ctx, "glBindBufferBase (uniform / transform feedback buffers)");
}

/* Indexed queries: every indexed binding point is unbound */
static bool sgl_es3_get_indexed(sgl_context_t *ctx, GLenum target, GLuint index, GLint64 *value) {
    GLuint count;
    switch (target) {
        case GL_UNIFORM_BUFFER_BINDING:
        case GL_UNIFORM_BUFFER_START:
        case GL_UNIFORM_BUFFER_SIZE:
            count = SGL_ES3_MAX_UNIFORM_BUFFER_BINDINGS;
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
        case GL_TRANSFORM_FEEDBACK_BUFFER_START:
        case GL_TRANSFORM_FEEDBACK_BUFFER_SIZE:
            count = SGL_ES3_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return false;
    }
    if (index >= count) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return false;
    }
    *value = 0;
    return true;
}

GL_APICALL void GL_APIENTRY glGetIntegeri_v(GLenum target, GLuint index, GLint *data) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    GLint64 value;
    if (sgl_es3_get_indexed(ctx, target, index, &value) && data)
        *data = (GLint)value;
}

GL_APICALL void GL_APIENTRY glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    GLint64 value;
    if (sgl_es3_get_indexed(ctx, target, index, &value) && data)
        *data = value;
}

/* ============================================================================
 * Draw and read buffers
 * ============================================================================ */

/* GL_NONE, GL_BACK or GL_COLOR_ATTACHMENT0..31: the enums glDrawBuffers and
 * glReadBuffer accept at all (anything else is GL_INVALID_ENUM) */
static bool sgl_es3_is_buffer_enum(GLenum buf) {
    return buf == GL_NONE || buf == GL_BACK ||
           (buf >= GL_COLOR_ATTACHMENT0 && buf <= GL_COLOR_ATTACHMENT0 + 31);
}

GL_APICALL void GL_APIENTRY glDrawBuffers(GLsizei n, const GLenum *bufs) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (n > 0 && !bufs)
        return;
    for (GLsizei i = 0; i < n; i++) {
        if (!sgl_es3_is_buffer_enum(bufs[i])) {
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
        }
    }
    if (n > SGL_ES3_MAX_DRAW_BUFFERS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    bool default_fb = ctx->bound_draw_framebuffer == 0;
    if (default_fb) {
        /* §4.2.1: exactly one buffer, GL_BACK or GL_NONE */
        if (n != 1 || (bufs[0] != GL_BACK && bufs[0] != GL_NONE)) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    } else {
        /* bufs[i] must be GL_COLOR_ATTACHMENTi or GL_NONE */
        for (GLsizei i = 0; i < n; i++) {
            if (bufs[i] != GL_NONE && bufs[i] != GL_COLOR_ATTACHMENT0 + (GLenum)i) {
                sgl_set_error(ctx, GL_INVALID_OPERATION);
                return;
            }
        }
    }

    /* Default: GL_BACK / GL_COLOR_ATTACHMENT0 for draw buffer 0 */
    if (n != 1 || bufs[0] == GL_NONE)
        SGL_ES3_UNSUPPORTED(ctx, "glDrawBuffers (other than the default buffer)");
}

GL_APICALL void GL_APIENTRY glReadBuffer(GLenum src) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    if (!sgl_es3_is_buffer_enum(src)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (ctx->bound_read_framebuffer == 0) {
        if (src != GL_BACK && src != GL_NONE) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
    } else if (src == GL_BACK || (src != GL_NONE && src - GL_COLOR_ATTACHMENT0 >=
                                                        SGL_ES3_MAX_DRAW_BUFFERS)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Default: GL_BACK / GL_COLOR_ATTACHMENT0 */
    if (src == GL_NONE)
        SGL_ES3_UNSUPPORTED(ctx, "glReadBuffer(GL_NONE)");
}

/* ============================================================================
 * Query objects
 * ============================================================================ */

GL_APICALL void GL_APIENTRY glGetQueryiv(GLenum target, GLenum pname, GLint *params) {
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    if (target != GL_ANY_SAMPLES_PASSED && target != GL_ANY_SAMPLES_PASSED_CONSERVATIVE &&
        target != GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (pname != GL_CURRENT_QUERY) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    /* glBeginQuery is not implemented: no query is ever active */
    if (params)
        *params = 0;
}

/* ============================================================================
 * 3D and 2D array textures, GLES 3.0 texture parameters
 * ============================================================================ */

static bool sgl_es3_is_texture_target(GLenum target) {
    return target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY;
}

bool sgl_es3_bind_texture(sgl_context_t *ctx, GLenum target, GLuint texture) {
    if (!sgl_ctx_is_es3(ctx) || !sgl_es3_is_texture_target(target))
        return false;
    /* The default texture of these targets is the only one */
    if (texture != 0)
        SGL_ES3_UNSUPPORTED(ctx, "glBindTexture (3D / 2D array textures)");
    return true;
}

GL_APICALL void GL_APIENTRY glTexImage3D(GLenum target, GLint level, GLint internalformat,
                                         GLsizei width, GLsizei height, GLsizei depth, GLint border,
                                         GLenum format, GLenum type, const void *pixels) {
    (void)pixels;
    sgl_context_t *ctx = sgl_es3_context();
    if (!ctx)
        return;
    if (!sgl_es3_is_texture_target(target)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || width < 0 || height < 0 || depth < 0 || border != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* An empty image of an unsized format leaves the (default) texture
     * without storage, which is the only state it can have. Anything else
     * needs 3D / array textures or sized formats. */
    bool unsized = format == GL_RGBA || format == GL_RGB || format == GL_LUMINANCE_ALPHA ||
                   format == GL_LUMINANCE || format == GL_ALPHA;
    if (width == 0 && height == 0 && depth == 0 && unsized && (GLenum)internalformat == format &&
        type == GL_UNSIGNED_BYTE)
        return;
    SGL_ES3_UNSUPPORTED(ctx, "glTexImage3D (3D / 2D array textures)");
}

/* Default value of a GLES 3.0 texture parameter. Returns 1 with *def set, 0
 * if pname is not a GLES 3.0 parameter of target (the GLES 2.0 path handles
 * it), -1 if pname is not a parameter of a GLES 3.0 target at all. */
static int sgl_es3_tex_param_default(GLenum target, GLenum pname, GLfloat *def) {
    bool es3_target = sgl_es3_is_texture_target(target);
    switch (pname) {
        /* GLES 2.0 parameters: only handled here for the GLES 3.0 targets */
        case GL_TEXTURE_MIN_FILTER:
            *def = GL_NEAREST_MIPMAP_LINEAR;
            return es3_target ? 1 : 0;
        case GL_TEXTURE_MAG_FILTER:
            *def = GL_LINEAR;
            return es3_target ? 1 : 0;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
            *def = GL_REPEAT;
            return es3_target ? 1 : 0;
        /* GLES 3.0 parameters, every target */
        case GL_TEXTURE_WRAP_R:
            *def = GL_REPEAT;
            return 1;
        case GL_TEXTURE_SWIZZLE_R:
            *def = GL_RED;
            return 1;
        case GL_TEXTURE_SWIZZLE_G:
            *def = GL_GREEN;
            return 1;
        case GL_TEXTURE_SWIZZLE_B:
            *def = GL_BLUE;
            return 1;
        case GL_TEXTURE_SWIZZLE_A:
            *def = GL_ALPHA;
            return 1;
        case GL_TEXTURE_MIN_LOD:
            *def = -1000.0f;
            return 1;
        case GL_TEXTURE_MAX_LOD:
            *def = 1000.0f;
            return 1;
        case GL_TEXTURE_BASE_LEVEL:
            *def = 0.0f;
            return 1;
        case GL_TEXTURE_MAX_LEVEL:
            *def = 1000.0f;
            return 1;
        case GL_TEXTURE_COMPARE_MODE:
            *def = GL_NONE;
            return 1;
        case GL_TEXTURE_COMPARE_FUNC:
            *def = GL_LEQUAL;
            return 1;
        default:
            /* On 2D / cube map the GLES 2.0 path reports it */
            return es3_target ? -1 : 0;
    }
}

/* GL error for setting texture parameter pname to value, GL_NO_ERROR if valid */
static GLenum sgl_es3_tex_param_error(GLenum pname, GLfloat value) {
    GLint v = (GLint)value;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            return (v == GL_NEAREST || v == GL_LINEAR || v == GL_NEAREST_MIPMAP_NEAREST ||
                    v == GL_LINEAR_MIPMAP_NEAREST || v == GL_NEAREST_MIPMAP_LINEAR ||
                    v == GL_LINEAR_MIPMAP_LINEAR)
                       ? GL_NO_ERROR
                       : GL_INVALID_ENUM;
        case GL_TEXTURE_MAG_FILTER:
            return (v == GL_NEAREST || v == GL_LINEAR) ? GL_NO_ERROR : GL_INVALID_ENUM;
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
            return (v == GL_REPEAT || v == GL_CLAMP_TO_EDGE || v == GL_MIRRORED_REPEAT)
                       ? GL_NO_ERROR
                       : GL_INVALID_ENUM;
        case GL_TEXTURE_SWIZZLE_R:
        case GL_TEXTURE_SWIZZLE_G:
        case GL_TEXTURE_SWIZZLE_B:
        case GL_TEXTURE_SWIZZLE_A:
            return (v == GL_RED || v == GL_GREEN || v == GL_BLUE || v == GL_ALPHA || v == GL_ZERO ||
                    v == GL_ONE)
                       ? GL_NO_ERROR
                       : GL_INVALID_ENUM;
        case GL_TEXTURE_BASE_LEVEL:
        case GL_TEXTURE_MAX_LEVEL:
            return value >= 0.0f ? GL_NO_ERROR : GL_INVALID_VALUE;
        case GL_TEXTURE_COMPARE_MODE:
            return (v == GL_NONE || v == GL_COMPARE_REF_TO_TEXTURE) ? GL_NO_ERROR : GL_INVALID_ENUM;
        case GL_TEXTURE_COMPARE_FUNC:
            return (v == GL_NEVER || v == GL_LESS || v == GL_EQUAL || v == GL_LEQUAL ||
                    v == GL_GREATER || v == GL_NOTEQUAL || v == GL_GEQUAL || v == GL_ALWAYS)
                       ? GL_NO_ERROR
                       : GL_INVALID_ENUM;
        default: /* GL_TEXTURE_MIN_LOD / GL_TEXTURE_MAX_LOD: any value */
            return GL_NO_ERROR;
    }
}

static bool sgl_es3_is_tex_param_target(GLenum target) {
    return target == GL_TEXTURE_2D || target == GL_TEXTURE_CUBE_MAP ||
           sgl_es3_is_texture_target(target);
}

bool sgl_es3_tex_parameter(sgl_context_t *ctx, GLenum target, GLenum pname, GLfloat value) {
    if (!sgl_ctx_is_es3(ctx) || !sgl_es3_is_tex_param_target(target))
        return false;

    GLfloat def = 0.0f;
    int known = sgl_es3_tex_param_default(target, pname, &def);
    if (known == 0)
        return false;
    if (known < 0) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return true;
    }
    GLenum error = sgl_es3_tex_param_error(pname, value);
    if (error != GL_NO_ERROR) {
        sgl_set_error(ctx, error);
        return true;
    }
    if (value != def)
        SGL_ES3_UNSUPPORTED(ctx, "glTexParameter (GLES 3.0 texture parameters / targets)");
    return true;
}

int sgl_es3_get_tex_parameter(sgl_context_t *ctx, GLenum target, GLenum pname, GLfloat *value) {
    if (!sgl_ctx_is_es3(ctx) || !sgl_es3_is_tex_param_target(target))
        return 0;

    /* Immutable storage (glTexStorage*) does not exist yet */
    if (pname == GL_TEXTURE_IMMUTABLE_FORMAT || pname == GL_TEXTURE_IMMUTABLE_LEVELS) {
        *value = 0.0f;
        return 1;
    }
    GLfloat def = 0.0f;
    int known = sgl_es3_tex_param_default(target, pname, &def);
    if (known == 0)
        return 0;
    if (known < 0) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return -1;
    }
    *value = def;
    return 1;
}

/* ============================================================================
 * Pixel store
 * ============================================================================ */

bool sgl_es3_pixel_store(sgl_context_t *ctx, GLenum pname, GLint param) {
    if (!sgl_ctx_is_es3(ctx))
        return false;
    switch (pname) {
        case GL_UNPACK_ROW_LENGTH:
        case GL_UNPACK_IMAGE_HEIGHT:
        case GL_UNPACK_SKIP_ROWS:
        case GL_UNPACK_SKIP_PIXELS:
        case GL_UNPACK_SKIP_IMAGES:
        case GL_PACK_ROW_LENGTH:
        case GL_PACK_SKIP_ROWS:
        case GL_PACK_SKIP_PIXELS:
            break;
        default:
            return false;
    }
    if (param < 0)
        sgl_set_error(ctx, GL_INVALID_VALUE);
    else if (param != 0)
        SGL_ES3_UNSUPPORTED(ctx, "glPixelStorei (row length / skip / image height)");
    return true;
}

/* ============================================================================
 * State queries (glGetIntegerv and the glGet* that go through it)
 * ============================================================================ */

bool sgl_es3_get_integer(sgl_context_t *ctx, GLenum pname, GLint *params) {
    if (!sgl_ctx_is_es3(ctx))
        return false;

    switch (pname) {
        case GL_VERTEX_ARRAY_BINDING:
            *params = (GLint)ctx->bound_vertex_array;
            return true;

        /* Default bindings: nothing else can be bound */
        case GL_TRANSFORM_FEEDBACK_BINDING:
        case GL_SAMPLER_BINDING:
        case GL_TEXTURE_BINDING_3D:
        case GL_TEXTURE_BINDING_2D_ARRAY:
        /* Disabled capabilities, inactive transform feedback */
        case GL_PRIMITIVE_RESTART_FIXED_INDEX:
        case GL_RASTERIZER_DISCARD:
        case GL_TRANSFORM_FEEDBACK_ACTIVE:
        case GL_TRANSFORM_FEEDBACK_PAUSED:
        /* Pixel store parameters that only accept 0 */
        case GL_UNPACK_ROW_LENGTH:
        case GL_UNPACK_IMAGE_HEIGHT:
        case GL_UNPACK_SKIP_ROWS:
        case GL_UNPACK_SKIP_PIXELS:
        case GL_UNPACK_SKIP_IMAGES:
        case GL_PACK_ROW_LENGTH:
        case GL_PACK_SKIP_ROWS:
        case GL_PACK_SKIP_PIXELS:
            *params = 0;
            return true;

        case GL_READ_BUFFER:
            *params = ctx->bound_read_framebuffer ? GL_COLOR_ATTACHMENT0 : GL_BACK;
            return true;
        case GL_DRAW_BUFFER0:
            *params = ctx->bound_draw_framebuffer ? GL_COLOR_ATTACHMENT0 : GL_BACK;
            return true;

        /* GLES 3.0 generic buffer bindings */
        case GL_COPY_READ_BUFFER_BINDING:
            *params = (GLint)ctx->bound_copy_read_buffer;
            return true;
        case GL_COPY_WRITE_BUFFER_BINDING:
            *params = (GLint)ctx->bound_copy_write_buffer;
            return true;
        case GL_PIXEL_PACK_BUFFER_BINDING:
            *params = (GLint)ctx->bound_pixel_pack_buffer;
            return true;
        case GL_PIXEL_UNPACK_BUFFER_BINDING:
            *params = (GLint)ctx->bound_pixel_unpack_buffer;
            return true;
        case GL_UNIFORM_BUFFER_BINDING:
            *params = (GLint)ctx->bound_uniform_buffer;
            return true;
        case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
            *params = (GLint)ctx->bound_transform_feedback_buffer;
            return true;

        case GL_MAX_UNIFORM_BUFFER_BINDINGS:
            *params = SGL_ES3_MAX_UNIFORM_BUFFER_BINDINGS;
            return true;
        case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
            *params = SGL_ES3_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS;
            return true;
        case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
            *params = SGL_ES3_UNIFORM_BUFFER_OFFSET_ALIGNMENT;
            return true;
        case GL_MAX_VERTEX_UNIFORM_BLOCKS:
        case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
            *params = SGL_ES3_MAX_STAGE_UNIFORM_BLOCKS;
            return true;
        case GL_MAX_COMBINED_UNIFORM_BLOCKS:
            *params = 2 * SGL_ES3_MAX_STAGE_UNIFORM_BLOCKS;
            return true;
        case GL_MAX_UNIFORM_BLOCK_SIZE:
            *params = SGL_ES3_MAX_UNIFORM_BLOCK_SIZE;
            return true;
        case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
            *params = SGL_ES3_MAX_STAGE_UNIFORM_COMPONENTS;
            return true;
        case GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS:
        case GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS:
            /* GLES 3.0 Table 6.32: blocks * block size / 4 + default-block components */
            *params = SGL_ES3_MAX_STAGE_UNIFORM_BLOCKS * SGL_ES3_MAX_UNIFORM_BLOCK_SIZE / 4 +
                      SGL_ES3_MAX_STAGE_UNIFORM_COMPONENTS;
            return true;
        case GL_MAX_3D_TEXTURE_SIZE:
            *params = SGL_ES3_MAX_3D_TEXTURE_SIZE;
            return true;
        case GL_MAX_ARRAY_TEXTURE_LAYERS:
            *params = SGL_ES3_MAX_ARRAY_TEXTURE_LAYERS;
            return true;

        default:
            /* GL_DRAW_BUFFER1..15: GL_NONE */
            if (pname > GL_DRAW_BUFFER0 && pname <= GL_DRAW_BUFFER15) {
                *params = GL_NONE;
                return true;
            }
            return false;
    }
}
