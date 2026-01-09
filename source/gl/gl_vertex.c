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
    (void)index; (void)pname; (void)params;
}

GL_APICALL void GL_APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
    (void)index; (void)pname; (void)params;
}

GL_APICALL void GL_APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) {
    (void)index; (void)pname; (void)pointer;
}

/* Vertex Attrib Constant Values (stubs) */
GL_APICALL void GL_APIENTRY glVertexAttrib1f(GLuint index, GLfloat x) { (void)index; (void)x; }
GL_APICALL void GL_APIENTRY glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) { (void)index; (void)x; (void)y; }
GL_APICALL void GL_APIENTRY glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) { (void)index; (void)x; (void)y; (void)z; }
GL_APICALL void GL_APIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { (void)index; (void)x; (void)y; (void)z; (void)w; }

GL_APICALL void GL_APIENTRY glVertexAttrib1fv(GLuint index, const GLfloat *v) { (void)index; (void)v; }
GL_APICALL void GL_APIENTRY glVertexAttrib2fv(GLuint index, const GLfloat *v) { (void)index; (void)v; }
GL_APICALL void GL_APIENTRY glVertexAttrib3fv(GLuint index, const GLfloat *v) { (void)index; (void)v; }
GL_APICALL void GL_APIENTRY glVertexAttrib4fv(GLuint index, const GLfloat *v) { (void)index; (void)v; }
