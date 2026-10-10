/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Vertex Attributes
 */

#include "gl_common.h"
#include <stdlib.h>
#include <string.h>

/* Advertised max vertex attribs (matches glGetIntegerv GL_MAX_VERTEX_ATTRIBS).
 * Internal arrays are SGL_GL_MAX_VERTEX_ATTRIBS=32 for aliased-inactive attributes,
 * but the API must reject indices >= 16 per GLES2 spec. */
#define SGL_GL_MAX_VERTEX_ATTRIBS 16

GL_APICALL void GL_APIENTRY glEnableVertexAttribArray(GLuint index) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    ctx->vertex_attribs[index].enabled = true;
    SGL_TRACE_VERTEX("glEnableVertexAttribArray(%u)", index);
}

GL_APICALL void GL_APIENTRY glDisableVertexAttribArray(GLuint index) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    ctx->vertex_attribs[index].enabled = false;
    SGL_TRACE_VERTEX("glDisableVertexAttribArray(%u)", index);
}

GL_APICALL void GL_APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                                  GLboolean normalized, GLsizei stride,
                                                  const void *pointer) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS || size < 1 || size > 4 || stride < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_FLOAT:
        case GL_FIXED:
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* GLES 3.0 §2.8: a vertex array object other than the default one
     * cannot use client memory */
    if (ctx->bound_vertex_array != 0 && ctx->bound_array_buffer == 0 && pointer != NULL) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_vertex_attrib_t *attr = &ctx->vertex_attribs[index];
    attr->size = size;
    attr->type = type;
    attr->normalized = normalized;
    attr->stride = stride;
    attr->pointer = pointer;
    attr->buffer = ctx->bound_array_buffer;

    SGL_TRACE_VERTEX("glVertexAttribPointer(%u, %d, 0x%X, %d, %d)", index, size, type, normalized,
                     stride);
}

GL_APICALL void GL_APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS || !params) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    const sgl_vertex_attrib_t *attr = &ctx->vertex_attribs[index];

    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *params = attr->enabled ? 1.0f : 0.0f;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *params = (GLfloat)attr->size;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *params = (GLfloat)attr->stride;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *params = (GLfloat)attr->type;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *params = attr->normalized ? 1.0f : 0.0f;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            *params = (GLfloat)attr->buffer;
            break;
        case GL_CURRENT_VERTEX_ATTRIB:
            params[0] = attr->current_value[0];
            params[1] = attr->current_value[1];
            params[2] = attr->current_value[2];
            params[3] = attr->current_value[3];
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

GL_APICALL void GL_APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS || !params) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    const sgl_vertex_attrib_t *attr = &ctx->vertex_attribs[index];

    switch (pname) {
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *params = attr->enabled ? 1 : 0;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *params = attr->size;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *params = attr->stride;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *params = (GLint)attr->type;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *params = attr->normalized ? 1 : 0;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            *params = (GLint)attr->buffer;
            break;
        case GL_CURRENT_VERTEX_ATTRIB:
            params[0] = (GLint)attr->current_value[0];
            params[1] = (GLint)attr->current_value[1];
            params[2] = (GLint)attr->current_value[2];
            params[3] = (GLint)attr->current_value[3];
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

GL_APICALL void GL_APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS || !pointer) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    *pointer = (void *)ctx->vertex_attribs[index].pointer;
}

/* Vertex Attrib Constant Values */
GL_APICALL void GL_APIENTRY glVertexAttrib1f(GLuint index, GLfloat x) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = 0.0f;
    ctx->vertex_attribs[index].current_value[2] = 0.0f;
    ctx->vertex_attribs[index].current_value[3] = 1.0f;
}

GL_APICALL void GL_APIENTRY glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = y;
    ctx->vertex_attribs[index].current_value[2] = 0.0f;
    ctx->vertex_attribs[index].current_value[3] = 1.0f;
}

GL_APICALL void GL_APIENTRY glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = y;
    ctx->vertex_attribs[index].current_value[2] = z;
    ctx->vertex_attribs[index].current_value[3] = 1.0f;
}

GL_APICALL void GL_APIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                                             GLfloat w) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (index >= SGL_GL_MAX_VERTEX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = y;
    ctx->vertex_attribs[index].current_value[2] = z;
    ctx->vertex_attribs[index].current_value[3] = w;
}

GL_APICALL void GL_APIENTRY glVertexAttrib1fv(GLuint index, const GLfloat *v) {
    if (!v)
        return;
    glVertexAttrib1f(index, v[0]);
}

GL_APICALL void GL_APIENTRY glVertexAttrib2fv(GLuint index, const GLfloat *v) {
    if (!v)
        return;
    glVertexAttrib2f(index, v[0], v[1]);
}

GL_APICALL void GL_APIENTRY glVertexAttrib3fv(GLuint index, const GLfloat *v) {
    if (!v)
        return;
    glVertexAttrib3f(index, v[0], v[1], v[2]);
}

GL_APICALL void GL_APIENTRY glVertexAttrib4fv(GLuint index, const GLfloat *v) {
    if (!v)
        return;
    glVertexAttrib4f(index, v[0], v[1], v[2], v[3]);
}

/* ============================================================================
 * Vertex array objects (GLES 3.0)
 *
 * The bound vertex array is the context state itself (ctx->vertex_attribs,
 * ctx->bound_element_buffer): glBindVertexArray saves it into the object
 * being unbound and loads the new object's copy. Draws, glVertexAttrib*
 * and glDeleteBuffers (which unbinds a deleted buffer from the bound vertex
 * array only, GLES 3.0 §2.9.1) keep working on the context unchanged.
 * ============================================================================ */

static sgl_context_t *sgl_vao_context(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (ctx && !sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return NULL;
    }
    return ctx;
}

/* Copy the context's vertex array state into vao. False if out of memory. */
static bool sgl_vao_save(sgl_context_t *ctx, sgl_vertex_array_t *vao) {
    if (!vao->attribs) {
        vao->attribs = (sgl_vertex_attrib_t *)malloc(SGL_MAX_ATTRIBS * sizeof(sgl_vertex_attrib_t));
        if (!vao->attribs)
            return false;
    }
    memcpy(vao->attribs, ctx->vertex_attribs, SGL_MAX_ATTRIBS * sizeof(sgl_vertex_attrib_t));
    vao->element_buffer = ctx->bound_element_buffer;
    return true;
}

/* Load vao's state into the context (initial state if it was never saved),
 * keeping the current generic attribute values, which are context state. */
static void sgl_vao_load(sgl_context_t *ctx, const sgl_vertex_array_t *vao) {
    for (int i = 0; i < SGL_MAX_ATTRIBS; i++) {
        sgl_vertex_attrib_t *attr = &ctx->vertex_attribs[i];
        GLfloat current[4];
        memcpy(current, attr->current_value, sizeof(current));
        if (vao->attribs) {
            *attr = vao->attribs[i];
        } else {
            memset(attr, 0, sizeof(*attr));
            attr->size = 4;
            attr->type = GL_FLOAT;
            attr->normalized = GL_FALSE;
        }
        memcpy(attr->current_value, current, sizeof(current));
    }
    ctx->bound_element_buffer = vao->element_buffer;
}

GL_APICALL void GL_APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays) {
    sgl_context_t *ctx = sgl_vao_context();
    if (!ctx)
        return;
    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!arrays)
        return;

    sgl_vertex_array_t *table = ctx->res_mgr.vertex_arrays;
    GLuint next = 1;
    for (GLsizei i = 0; i < n; i++) {
        while (next < SGL_MAX_VERTEX_ARRAYS && table[next].used)
            next++;
        if (next == SGL_MAX_VERTEX_ARRAYS) {
            /* Release the names generated by this call */
            for (GLsizei j = 0; j < i; j++)
                table[arrays[j]].used = false;
            sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
        table[next].used = true;
        table[next].created = false;
        arrays[i] = next;
    }
}

GL_APICALL void GL_APIENTRY glBindVertexArray(GLuint array) {
    sgl_context_t *ctx = sgl_vao_context();
    if (!ctx)
        return;

    sgl_vertex_array_t *table = ctx->res_mgr.vertex_arrays;
    if (array != 0 && (array >= SGL_MAX_VERTEX_ARRAYS || !table[array].used)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION); /* not a name from glGenVertexArrays */
        return;
    }
    if (array == ctx->bound_vertex_array) {
        table[array].created = true;
        return;
    }

    if (!sgl_vao_save(ctx, &table[ctx->bound_vertex_array])) {
        sgl_set_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }
    sgl_vao_load(ctx, &table[array]);
    table[array].created = true;
    ctx->bound_vertex_array = array;
    SGL_TRACE_VERTEX("glBindVertexArray(%u)", array);
}

GL_APICALL void GL_APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    sgl_context_t *ctx = sgl_vao_context();
    if (!ctx)
        return;
    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!arrays)
        return;

    sgl_vertex_array_t *table = ctx->res_mgr.vertex_arrays;
    for (GLsizei i = 0; i < n; i++) {
        GLuint array = arrays[i];
        if (array == 0 || array >= SGL_MAX_VERTEX_ARRAYS || !table[array].used)
            continue; /* unused names and 0 are silently ignored */
        if (array == ctx->bound_vertex_array) {
            /* Deleting the bound vertex array binds the default one */
            sgl_vao_load(ctx, &table[0]);
            ctx->bound_vertex_array = 0;
        }
        free(table[array].attribs);
        memset(&table[array], 0, sizeof(table[array]));
    }
}

GL_APICALL GLboolean GL_APIENTRY glIsVertexArray(GLuint array) {
    sgl_context_t *ctx = sgl_vao_context();
    if (!ctx || array == 0 || array >= SGL_MAX_VERTEX_ARRAYS)
        return GL_FALSE;
    return ctx->res_mgr.vertex_arrays[array].created ? GL_TRUE : GL_FALSE;
}
