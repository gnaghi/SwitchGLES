/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Vertex Attributes
 */

#include "gl_common.h"
#include <string.h>

GL_APICALL void GL_APIENTRY glEnableVertexAttribArray(GLuint index) {
    GET_CTX();

    if (index >= SGL_MAX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    ctx->vertex_attribs[index].enabled = true;
    SGL_TRACE_VERTEX("glEnableVertexAttribArray(%u)", index);
}

GL_APICALL void GL_APIENTRY glDisableVertexAttribArray(GLuint index) {
    GET_CTX();

    if (index >= SGL_MAX_ATTRIBS) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    ctx->vertex_attribs[index].enabled = false;
    SGL_TRACE_VERTEX("glDisableVertexAttribArray(%u)", index);
}

GL_APICALL void GL_APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                                   GLboolean normalized, GLsizei stride,
                                                   const void *pointer) {
    GET_CTX();

    if (index >= SGL_MAX_ATTRIBS || size < 1 || size > 4 || stride < 0) {
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

    sgl_vertex_attrib_t *attr = &ctx->vertex_attribs[index];
    attr->size = size;
    attr->type = type;
    attr->normalized = normalized;
    attr->stride = stride;
    attr->pointer = pointer;
    attr->buffer = ctx->bound_array_buffer;

    SGL_TRACE_VERTEX("glVertexAttribPointer(%u, %d, 0x%X, %d, %d)", index, size, type, normalized, stride);
}

GL_APICALL void GL_APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {
    GET_CTX();

    if (index >= SGL_MAX_ATTRIBS || !params) {
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
    GET_CTX();

    if (index >= SGL_MAX_ATTRIBS || !params) {
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
    GET_CTX();

    if (index >= SGL_MAX_ATTRIBS || !pointer) {
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
    GET_CTX();
    if (index >= SGL_MAX_ATTRIBS) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = 0.0f;
    ctx->vertex_attribs[index].current_value[2] = 0.0f;
    ctx->vertex_attribs[index].current_value[3] = 1.0f;
}

GL_APICALL void GL_APIENTRY glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
    GET_CTX();
    if (index >= SGL_MAX_ATTRIBS) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = y;
    ctx->vertex_attribs[index].current_value[2] = 0.0f;
    ctx->vertex_attribs[index].current_value[3] = 1.0f;
}

GL_APICALL void GL_APIENTRY glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    GET_CTX();
    if (index >= SGL_MAX_ATTRIBS) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = y;
    ctx->vertex_attribs[index].current_value[2] = z;
    ctx->vertex_attribs[index].current_value[3] = 1.0f;
}

GL_APICALL void GL_APIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    GET_CTX();
    if (index >= SGL_MAX_ATTRIBS) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    ctx->vertex_attribs[index].current_value[0] = x;
    ctx->vertex_attribs[index].current_value[1] = y;
    ctx->vertex_attribs[index].current_value[2] = z;
    ctx->vertex_attribs[index].current_value[3] = w;
}

GL_APICALL void GL_APIENTRY glVertexAttrib1fv(GLuint index, const GLfloat *v) {
    if (!v) return;
    glVertexAttrib1f(index, v[0]);
}

GL_APICALL void GL_APIENTRY glVertexAttrib2fv(GLuint index, const GLfloat *v) {
    if (!v) return;
    glVertexAttrib2f(index, v[0], v[1]);
}

GL_APICALL void GL_APIENTRY glVertexAttrib3fv(GLuint index, const GLfloat *v) {
    if (!v) return;
    glVertexAttrib3f(index, v[0], v[1], v[2]);
}

GL_APICALL void GL_APIENTRY glVertexAttrib4fv(GLuint index, const GLfloat *v) {
    if (!v) return;
    glVertexAttrib4f(index, v[0], v[1], v[2], v[3]);
}
