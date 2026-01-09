/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Buffer Objects
 */

#include "gl_common.h"
#include <string.h>

GL_APICALL void GL_APIENTRY glGenBuffers(GLsizei n, GLuint *buffers) {
    GET_CTX();

    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (n == 0 || !buffers) return;

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

    if (n < 0) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    if (!buffers) return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = buffers[i];
        if (id == 0) continue;

        /* Return VBO allocation to free list before releasing handle */
        sgl_buffer_t *buf = GET_BUFFER(id);
        if (buf && buf->data_offset != 0 && buf->size > 0 &&
            ctx->backend && ctx->backend->ops->buffer_free) {
            ctx->backend->ops->buffer_free(ctx->backend, buf->data_offset, (uint32_t)buf->size);
        }

        if (ctx->bound_array_buffer == id) ctx->bound_array_buffer = 0;
        if (ctx->bound_element_buffer == id) ctx->bound_element_buffer = 0;

        sgl_res_mgr_free_buffer(&ctx->res_mgr, id);

        /* Remove from overflow list if present */
        for (int j = 0; j < ctx->res_mgr.num_overflow_buffers; j++) {
            if (ctx->res_mgr.overflow_buffer_ids[j] == id) {
                ctx->res_mgr.overflow_buffer_ids[j] = ctx->res_mgr.overflow_buffer_ids[--ctx->res_mgr.num_overflow_buffers];
                ctx->res_mgr.overflow_buffer_targets[j] = ctx->res_mgr.overflow_buffer_targets[ctx->res_mgr.num_overflow_buffers];
                break;
            }
        }
    }

    SGL_TRACE_BUFFER("glDeleteBuffers(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsBuffer(GLuint buffer) {
    GET_CTX_RET(GL_FALSE);
    if (buffer == 0) return GL_FALSE;
    sgl_buffer_t *buf = GET_BUFFER(buffer);
    /* GLES2: name becomes a buffer object only after first glBindBuffer */
    if (buf && buf->target != 0) return GL_TRUE;
    /* Check overflow IDs (for IDs outside normal array range) */
    for (int i = 0; i < ctx->res_mgr.num_overflow_buffers; i++) {
        if (ctx->res_mgr.overflow_buffer_ids[i] == buffer &&
            ctx->res_mgr.overflow_buffer_targets[i] != 0)
            return GL_TRUE;
    }
    return GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer) {
    GET_CTX();

    if (buffer != 0 && !GET_BUFFER(buffer)) {
        /* GLES2 spec: binding an unused name implicitly creates the object */
        if (buffer > 0 && buffer < SGL_MAX_BUFFERS) {
            memset(&ctx->res_mgr.buffers[buffer], 0, sizeof(sgl_buffer_t));
            ctx->res_mgr.buffers[buffer].used = true;
            ctx->res_mgr.buffers[buffer].usage = GL_STATIC_DRAW; /* spec default */
        } else {
            /* Overflow: ID outside array range. Track for glIsBuffer but
             * no real storage (can't upload data to these). */
            bool found = false;
            for (int i = 0; i < ctx->res_mgr.num_overflow_buffers; i++) {
                if (ctx->res_mgr.overflow_buffer_ids[i] == buffer) { found = true; break; }
            }
            if (!found && ctx->res_mgr.num_overflow_buffers < SGL_MAX_OVERFLOW_IDS) {
                int idx = ctx->res_mgr.num_overflow_buffers++;
                ctx->res_mgr.overflow_buffer_ids[idx] = buffer;
                ctx->res_mgr.overflow_buffer_targets[idx] = 0;
            }
        }
    }

    switch (target) {
        case GL_ARRAY_BUFFER:
            ctx->bound_array_buffer = buffer;
            if (buffer) {
                sgl_buffer_t *buf = GET_BUFFER(buffer);
                if (buf) buf->target = target;
                else {
                    /* Set target on overflow entry */
                    for (int i = 0; i < ctx->res_mgr.num_overflow_buffers; i++)
                        if (ctx->res_mgr.overflow_buffer_ids[i] == buffer)
                            { ctx->res_mgr.overflow_buffer_targets[i] = target; break; }
                }
            }
            break;
        case GL_ELEMENT_ARRAY_BUFFER:
            ctx->bound_element_buffer = buffer;
            if (buffer) {
                sgl_buffer_t *buf = GET_BUFFER(buffer);
                if (buf) buf->target = target;
                else {
                    for (int i = 0; i < ctx->res_mgr.num_overflow_buffers; i++)
                        if (ctx->res_mgr.overflow_buffer_ids[i] == buffer)
                            { ctx->res_mgr.overflow_buffer_targets[i] = target; break; }
                }
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

    /* Validate target */
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate size */
    if (size < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate usage */
    if (usage != GL_STATIC_DRAW && usage != GL_DYNAMIC_DRAW && usage != GL_STREAM_DRAW) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    GLuint buffer_id = (target == GL_ARRAY_BUFFER) ? ctx->bound_array_buffer : ctx->bound_element_buffer;
    sgl_buffer_t *buf = GET_BUFFER(buffer_id);
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Buffer orphaning: data=NULL means "discard old contents, give me new memory".
     * Allocates from VBO region (with deferred free of old allocation).
     * Old allocation freed only after GPU sync to prevent use-after-free. */
    if (!data && size > 0 && buf->data_offset != 0) {
        buf->usage = usage;
        if (ctx->backend->ops->buffer_data_orphan) {
            uint32_t new_offset = ctx->backend->ops->buffer_data_orphan(
                ctx->backend, size, buf->data_offset, (uint32_t)buf->size);
            if (new_offset != 0) {
                buf->data_offset = new_offset;
            } else {
                sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            }
        }
        buf->size = size;
        SGL_TRACE_BUFFER("glBufferData(0x%X, %zu, usage=0x%X, offset=%u)", target, (size_t)size, usage, buf->data_offset);
        return;
    }

    /* Reuse existing allocation if new size fits (avoids bump allocator waste) */
    if (buf->data_offset != 0 && data && size <= buf->size && size > 0) {
        buf->usage = usage;
        if (ctx->backend->ops->buffer_sub_data) {
            ctx->backend->ops->buffer_sub_data(ctx->backend, buffer_id, buf->data_offset, size, data);
        }
        buf->size = size;
        SGL_TRACE_BUFFER("glBufferData(0x%X, %zu, usage=0x%X, offset=%u)", target, (size_t)size, usage, buf->data_offset);
        return;
    }

    /* Free old VBO allocation before allocating new one */
    if (buf->data_offset != 0 && buf->size > 0 &&
        ctx->backend->ops->buffer_free) {
        ctx->backend->ops->buffer_free(ctx->backend, buf->data_offset, (uint32_t)buf->size);
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

    /* Validate target BEFORE checking data pointer — dEQP negative_api
     * passes data=NULL and expects target/offset/size errors to still fire. */
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

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

    if (!data) return;

    /* Delegate to backend for actual data write */
    if (ctx->backend->ops->buffer_sub_data) {
        ctx->backend->ops->buffer_sub_data(ctx->backend, buffer_id,
                                           buf->data_offset + (uint32_t)offset, size, data);
    }

    SGL_TRACE_BUFFER("glBufferSubData(0x%X, %td, %zu)", target, offset, (size_t)size);
}

/* Note: glGetBufferParameteriv is in gl_query.c */
