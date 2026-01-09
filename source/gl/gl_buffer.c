/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Buffer Objects
 */

#include "gl_common.h"
#include <string.h>

GL_APICALL void GL_APIENTRY glGenBuffers(GLsizei n, GLuint *buffers) {
    GET_CTX();

    if (n < 0 || !buffers) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    for (GLsizei i = 0; i < n; i++) {
        buffers[i] = sgl_res_mgr_alloc_buffer(&ctx->res_mgr);
        if (buffers[i] == 0) {
            sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }

    SGL_TRACE_BUFFER("glGenBuffers(%d)", n);
}

GL_APICALL void GL_APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    GET_CTX();

    if (n < 0) return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = buffers[i];
        if (id == 0) continue;

        if (ctx->bound_array_buffer == id) ctx->bound_array_buffer = 0;
        if (ctx->bound_element_buffer == id) ctx->bound_element_buffer = 0;

        sgl_res_mgr_free_buffer(&ctx->res_mgr, id);
    }

    SGL_TRACE_BUFFER("glDeleteBuffers(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsBuffer(GLuint buffer) {
    GET_CTX_RET(GL_FALSE);
    return GET_BUFFER(buffer) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer) {
    GET_CTX();

    if (buffer != 0 && !GET_BUFFER(buffer)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    switch (target) {
        case GL_ARRAY_BUFFER:
            ctx->bound_array_buffer = buffer;
            if (buffer) {
                sgl_buffer_t *buf = GET_BUFFER(buffer);
                if (buf) buf->target = target;
            }
            break;
        case GL_ELEMENT_ARRAY_BUFFER:
            ctx->bound_element_buffer = buffer;
            if (buffer) {
                sgl_buffer_t *buf = GET_BUFFER(buffer);
                if (buf) buf->target = target;
            }
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    SGL_TRACE_BUFFER("glBindBuffer(0x%X, %u)", target, buffer);
}

GL_APICALL void GL_APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    GET_CTX();
    CHECK_BACKEND();

    GLuint buffer_id = (target == GL_ARRAY_BUFFER) ? ctx->bound_array_buffer : ctx->bound_element_buffer;
    sgl_buffer_t *buf = GET_BUFFER(buffer_id);
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Update GL-level buffer state */
    buf->size = size;
    buf->usage = usage;

    /* Delegate to backend for actual GPU memory allocation and upload */
    if (ctx->backend->ops->buffer_data) {
        buf->data_offset = ctx->backend->ops->buffer_data(ctx->backend, buffer_id, target, size, data, usage);
        if (buf->data_offset == 0 && size > 0) {
            sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }

    SGL_TRACE_BUFFER("glBufferData(0x%X, %zu, usage=0x%X, offset=%u)", target, (size_t)size, usage, buf->data_offset);
}

GL_APICALL void GL_APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    GET_CTX();
    CHECK_BACKEND();

    if (!data) return;

    GLuint buffer_id = (target == GL_ARRAY_BUFFER) ? ctx->bound_array_buffer : ctx->bound_element_buffer;
    sgl_buffer_t *buf = GET_BUFFER(buffer_id);
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    if (offset < 0 || size < 0 || (size_t)(offset + size) > (size_t)buf->size) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Delegate to backend for actual data write */
    if (ctx->backend->ops->buffer_sub_data) {
        ctx->backend->ops->buffer_sub_data(ctx->backend, buffer_id,
                                           buf->data_offset + (uint32_t)offset, size, data);
    }

    SGL_TRACE_BUFFER("glBufferSubData(0x%X, %td, %zu)", target, offset, (size_t)size);
}

/* Note: glGetBufferParameteriv is in gl_query.c */
