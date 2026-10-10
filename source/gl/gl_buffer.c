/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Buffer Objects
 */

#include "gl_common.h"
#include "../util/sgl_perf.h"
#include <GLES3/gl3.h>
#include <string.h>

GLuint *sgl_buffer_binding(sgl_context_t *ctx, GLenum target) {
    switch (target) {
        case GL_ARRAY_BUFFER:
            return &ctx->bound_array_buffer;
        case GL_ELEMENT_ARRAY_BUFFER:
            return &ctx->bound_element_buffer;
        default:
            break;
    }
    if (!sgl_ctx_is_es3(ctx))
        return NULL;
    switch (target) {
        case GL_COPY_READ_BUFFER:
            return &ctx->bound_copy_read_buffer;
        case GL_COPY_WRITE_BUFFER:
            return &ctx->bound_copy_write_buffer;
        case GL_PIXEL_PACK_BUFFER:
            return &ctx->bound_pixel_pack_buffer;
        case GL_PIXEL_UNPACK_BUFFER:
            return &ctx->bound_pixel_unpack_buffer;
        case GL_UNIFORM_BUFFER:
            return &ctx->bound_uniform_buffer;
        case GL_TRANSFORM_FEEDBACK_BUFFER:
            return &ctx->bound_transform_feedback_buffer;
        default:
            return NULL;
    }
}

/* GLES 3.0 usage hints add READ and COPY to the GLES 2.0 DRAW ones */
static bool sgl_valid_buffer_usage(sgl_context_t *ctx, GLenum usage) {
    if (usage == GL_STATIC_DRAW || usage == GL_DYNAMIC_DRAW || usage == GL_STREAM_DRAW)
        return true;
    return sgl_ctx_is_es3(ctx) &&
           (usage == GL_STATIC_READ || usage == GL_DYNAMIC_READ || usage == GL_STREAM_READ ||
            usage == GL_STATIC_COPY || usage == GL_DYNAMIC_COPY || usage == GL_STREAM_COPY);
}

static void sgl_buffer_unmap(sgl_buffer_t *buf) {
    buf->mapped = false;
    buf->map_access = 0;
    buf->map_offset = 0;
    buf->map_length = 0;
}

GL_APICALL void GL_APIENTRY glGenBuffers(GLsizei n, GLuint *buffers) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (n == 0 || !buffers)
        return;

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
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!buffers)
        return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = buffers[i];
        if (id == 0)
            continue;

        /* Return VBO allocation to free list before releasing handle */
        sgl_buffer_t *buf = GET_BUFFER(id);
        if (buf && buf->data_offset != 0 && buf->size > 0 && ctx->backend &&
            ctx->backend->ops->buffer_free) {
            ctx->backend->ops->buffer_free(ctx->backend, buf->data_offset, (uint32_t)buf->size);
        }

        if (ctx->bound_array_buffer == id)
            ctx->bound_array_buffer = 0;
        if (ctx->bound_element_buffer == id)
            ctx->bound_element_buffer = 0;
        /* GLES 3.0 binding points (always 0 in a GLES 2.0 context) */
        GLuint *es3_points[] = {&ctx->bound_copy_read_buffer,   &ctx->bound_copy_write_buffer,
                                &ctx->bound_pixel_pack_buffer,  &ctx->bound_pixel_unpack_buffer,
                                &ctx->bound_uniform_buffer, &ctx->bound_transform_feedback_buffer};
        for (size_t p = 0; p < sizeof(es3_points) / sizeof(es3_points[0]); p++) {
            if (*es3_points[p] == id)
                *es3_points[p] = 0;
        }

        sgl_res_mgr_free_buffer(&ctx->res_mgr, id);

        /* Remove from overflow list if present */
        for (int j = 0; j < ctx->res_mgr.num_overflow_buffers; j++) {
            if (ctx->res_mgr.overflow_buffer_ids[j] == id) {
                ctx->res_mgr.overflow_buffer_ids[j] =
                    ctx->res_mgr.overflow_buffer_ids[--ctx->res_mgr.num_overflow_buffers];
                ctx->res_mgr.overflow_buffer_targets[j] =
                    ctx->res_mgr.overflow_buffer_targets[ctx->res_mgr.num_overflow_buffers];
                break;
            }
        }
    }

    SGL_TRACE_BUFFER("glDeleteBuffers(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsBuffer(GLuint buffer) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return GL_FALSE;
    if (buffer == 0)
        return GL_FALSE;
    sgl_buffer_t *buf = GET_BUFFER(buffer);
    /* GLES2: name becomes a buffer object only after first glBindBuffer */
    if (buf && buf->target != 0)
        return GL_TRUE;
    /* Check overflow IDs (for IDs outside normal array range) */
    for (int i = 0; i < ctx->res_mgr.num_overflow_buffers; i++) {
        if (ctx->res_mgr.overflow_buffer_ids[i] == buffer &&
            ctx->res_mgr.overflow_buffer_targets[i] != 0)
            return GL_TRUE;
    }
    return GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;


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
                if (ctx->res_mgr.overflow_buffer_ids[i] == buffer) {
                    found = true;
                    break;
                }
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
                if (buf)
                    buf->target = target;
                else {
                    /* Set target on overflow entry */
                    for (int i = 0; i < ctx->res_mgr.num_overflow_buffers; i++)
                        if (ctx->res_mgr.overflow_buffer_ids[i] == buffer) {
                            ctx->res_mgr.overflow_buffer_targets[i] = target;
                            break;
                        }
                }
            }
            break;
        case GL_ELEMENT_ARRAY_BUFFER:
            ctx->bound_element_buffer = buffer;
            if (buffer) {
                sgl_buffer_t *buf = GET_BUFFER(buffer);
                if (buf)
                    buf->target = target;
                else {
                    for (int i = 0; i < ctx->res_mgr.num_overflow_buffers; i++)
                        if (ctx->res_mgr.overflow_buffer_ids[i] == buffer) {
                            ctx->res_mgr.overflow_buffer_targets[i] = target;
                            break;
                        }
                }
            }
            break;
        default: {
            /* GLES 3.0 targets (GLES 3.0 contexts only) */
            GLuint *point = sgl_buffer_binding(ctx, target);
            if (!point) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
                return;
            }
            *point = buffer;
            sgl_buffer_t *buf = buffer ? GET_BUFFER(buffer) : NULL;
            if (buf)
                buf->target = target; /* the name is a buffer object now */
            break;
        }
    }

    SGL_TRACE_BUFFER("glBindBuffer(0x%X, %u)", target, buffer);
}

static void sgl_buffer_data_impl(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
GL_APICALL void GL_APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data,
                                         GLenum usage) {
    SGL_PERF_BEGIN(perf);
    sgl_buffer_data_impl(target, size, data, usage);
    SGL_PERF_END(SGL_PERF_BUFFER, perf);
}

static void sgl_buffer_data_impl(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    /* Validate target */
    GLuint *point = sgl_buffer_binding(ctx, target);
    if (!point) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Validate size */
    if (size < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate usage */
    if (!sgl_valid_buffer_usage(ctx, usage)) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    GLuint buffer_id = *point;
    sgl_buffer_t *buf = GET_BUFFER(buffer_id);
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* GLES 3.0: a new data store releases any mapping of the old one */
    if (buf->mapped)
        sgl_buffer_unmap(buf);

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
                buf->size = size;
            } else {
                /* The old block is already queued for freeing: drop it so a
                 * later glBufferData/glDeleteBuffers doesn't free it twice. */
                buf->data_offset = 0;
                buf->size = 0;
                sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            }
        } else {
            buf->size = size;
        }
        SGL_TRACE_BUFFER("glBufferData(0x%X, %zu, usage=0x%X, offset=%u)", target, (size_t)size,
                         usage, buf->data_offset);
        return;
    }

    /* Reuse existing allocation if new size fits (avoids bump allocator waste) */
    if (buf->data_offset != 0 && data && size <= buf->size && size > 0) {
        buf->usage = usage;
        if (ctx->backend->ops->buffer_sub_data) {
            ctx->backend->ops->buffer_sub_data(ctx->backend, buffer_id, buf->data_offset, size,
                                               data);
        }
        buf->size = size;
        SGL_TRACE_BUFFER("glBufferData(0x%X, %zu, usage=0x%X, offset=%u)", target, (size_t)size,
                         usage, buf->data_offset);
        return;
    }

    /* Free old VBO allocation before allocating new one */
    if (buf->data_offset != 0 && buf->size > 0 && ctx->backend->ops->buffer_free) {
        ctx->backend->ops->buffer_free(ctx->backend, buf->data_offset, (uint32_t)buf->size);
    }

    /* Update GL-level buffer state */
    buf->size = size;
    buf->usage = usage;

    /* Delegate to backend for actual GPU memory allocation and upload */
    if (ctx->backend->ops->buffer_data) {
        buf->data_offset =
            ctx->backend->ops->buffer_data(ctx->backend, buffer_id, target, size, data, usage);
        if (buf->data_offset == 0 && size > 0) {
            sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }

    SGL_TRACE_BUFFER("glBufferData(0x%X, %zu, usage=0x%X, offset=%u)", target, (size_t)size, usage,
                     buf->data_offset);
}

static void sgl_buffer_sub_data_impl(GLenum target, GLintptr offset, GLsizeiptr size,
                                     const void *data);
GL_APICALL void GL_APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                                            const void *data) {
    SGL_PERF_BEGIN(perf);
    sgl_buffer_sub_data_impl(target, offset, size, data);
    SGL_PERF_END(SGL_PERF_BUFFER, perf);
}

static void sgl_buffer_sub_data_impl(GLenum target, GLintptr offset, GLsizeiptr size,
                                     const void *data) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    /* Validate target BEFORE checking data pointer — dEQP negative_api
     * passes data=NULL and expects target/offset/size errors to still fire. */
    GLuint *point = sgl_buffer_binding(ctx, target);
    if (!point) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    GLuint buffer_id = *point;
    sgl_buffer_t *buf = GET_BUFFER(buffer_id);
    if (!buf) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Range check without computing offset+size (avoids signed overflow). */
    if (offset < 0 || size < 0 || offset > buf->size || size > buf->size - offset) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* GLES 3.0 §2.10.2: not while the buffer is mapped */
    if (buf->mapped) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    if (!data)
        return;

    /* Delegate to backend for actual data write */
    if (ctx->backend->ops->buffer_sub_data) {
        ctx->backend->ops->buffer_sub_data(ctx->backend, buffer_id,
                                           buf->data_offset + (uint32_t)offset, size, data);
    }

    SGL_TRACE_BUFFER("glBufferSubData(0x%X, %td, %zu)", target, offset, (size_t)size);
}

/* ============================================================================
 * GLES 3.0: mapping and copies
 *
 * Buffers live in CPU-visible memory (the backend data memblock), so a mapping
 * is a pointer into the buffer's data store. Synchronisation (GLES 3.0
 * §2.10.3):
 *  - nothing on the GPU writes buffer memory (no transform feedback yet,
 *    glReadPixels and copies are done by the CPU), so reading needs no wait;
 *  - writing waits for the GPU to finish the commands already issued, which
 *    may still read the old contents, unless GL_MAP_UNSYNCHRONIZED_BIT is set;
 *    GL_MAP_INVALIDATE_BUFFER_BIT orphans the data store instead (the old one
 *    is freed once the GPU is done with it, as for glBufferData(NULL));
 *  - CPU writes reach the next draws through backend->buffer_written, at
 *    glUnmapBuffer or glFlushMappedBufferRange.
 * glGetBufferParameter* and glGetBufferPointerv are in gl_query.c.
 * ============================================================================ */

static sgl_context_t *sgl_buffer_es3_context(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (ctx && !sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return NULL;
    }
    return ctx;
}

/* Buffer bound to target, with the GLES errors: GL_INVALID_ENUM for an
 * invalid target, GL_INVALID_OPERATION if zero is bound. */
static sgl_buffer_t *sgl_bound_buffer(sgl_context_t *ctx, GLenum target) {
    GLuint *point = sgl_buffer_binding(ctx, target);
    if (!point) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return NULL;
    }
    sgl_buffer_t *buf = *point ? GET_BUFFER(*point) : NULL;
    if (!buf)
        sgl_set_error(ctx, GL_INVALID_OPERATION);
    return buf;
}

#define SGL_MAP_ACCESS_BITS                                                                        \
    (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |                            \
     GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT | GL_MAP_UNSYNCHRONIZED_BIT)

GL_APICALL void *GL_APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                              GLbitfield access) {
    sgl_context_t *ctx = sgl_buffer_es3_context();
    if (!ctx)
        return NULL;
    CHECK_BACKEND_RET(NULL);

    sgl_buffer_t *buf = sgl_bound_buffer(ctx, target);
    if (!buf)
        return NULL;

    if (offset < 0 || length < 0 || offset > buf->size || length > buf->size - offset ||
        (access & ~(GLbitfield)SGL_MAP_ACCESS_BITS) != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return NULL;
    }
    bool read = (access & GL_MAP_READ_BIT) != 0;
    bool write = (access & GL_MAP_WRITE_BIT) != 0;
    if (buf->mapped || length == 0 || (!read && !write) ||
        (read && (access & (GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                            GL_MAP_UNSYNCHRONIZED_BIT))) ||
        ((access & GL_MAP_FLUSH_EXPLICIT_BIT) && !write)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return NULL;
    }

    if (write && !(access & GL_MAP_UNSYNCHRONIZED_BIT)) {
        if ((access & GL_MAP_INVALIDATE_BUFFER_BIT) && ctx->backend->ops->buffer_data_orphan) {
            /* Previous contents may be discarded: a new data store, no wait */
            uint32_t new_offset = ctx->backend->ops->buffer_data_orphan(
                ctx->backend, buf->size, buf->data_offset, (uint32_t)buf->size);
            if (new_offset == 0) {
                buf->data_offset = 0;
                buf->size = 0;
                sgl_set_error(ctx, GL_OUT_OF_MEMORY);
                return NULL;
            }
            buf->data_offset = new_offset;
        } else if (ctx->backend->ops->finish) {
            ctx->backend->ops->finish(ctx->backend);
        }
    }

    if (buf->data_offset == 0 || !ctx->backend->ops->get_data_cpu_ptr) {
        sgl_set_error(ctx, GL_OUT_OF_MEMORY);
        return NULL;
    }

    buf->mapped = true;
    buf->map_access = access;
    buf->map_offset = offset;
    buf->map_length = length;
    SGL_TRACE_BUFFER("glMapBufferRange(0x%X, %td, %td, 0x%X)", target, offset, length, access);
    return (uint8_t *)ctx->backend->ops->get_data_cpu_ptr(ctx->backend, buf->data_offset) + offset;
}

GL_APICALL void GL_APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset,
                                                     GLsizeiptr length) {
    sgl_context_t *ctx = sgl_buffer_es3_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    sgl_buffer_t *buf = sgl_bound_buffer(ctx, target);
    if (!buf)
        return;
    if (!buf->mapped || !(buf->map_access & GL_MAP_FLUSH_EXPLICIT_BIT)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    /* offset and length are relative to the mapping */
    if (offset < 0 || length < 0 || offset > buf->map_length || length > buf->map_length - offset) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (ctx->backend->ops->buffer_written)
        ctx->backend->ops->buffer_written(ctx->backend);
}

GL_APICALL GLboolean GL_APIENTRY glUnmapBuffer(GLenum target) {
    sgl_context_t *ctx = sgl_buffer_es3_context();
    if (!ctx)
        return GL_FALSE;
    CHECK_BACKEND_RET(GL_FALSE);

    sgl_buffer_t *buf = sgl_bound_buffer(ctx, target);
    if (!buf)
        return GL_FALSE;
    if (!buf->mapped) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    /* With GL_MAP_FLUSH_EXPLICIT_BIT only the flushed ranges count, and
     * glFlushMappedBufferRange already published them */
    if ((buf->map_access & GL_MAP_WRITE_BIT) && !(buf->map_access & GL_MAP_FLUSH_EXPLICIT_BIT) &&
        ctx->backend->ops->buffer_written)
        ctx->backend->ops->buffer_written(ctx->backend);
    sgl_buffer_unmap(buf);
    return GL_TRUE; /* the data store cannot be corrupted while mapped */
}

GL_APICALL void GL_APIENTRY glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                                                GLintptr readOffset, GLintptr writeOffset,
                                                GLsizeiptr size) {
    sgl_context_t *ctx = sgl_buffer_es3_context();
    if (!ctx)
        return;
    CHECK_BACKEND();

    GLuint *read_point = sgl_buffer_binding(ctx, readTarget);
    GLuint *write_point = sgl_buffer_binding(ctx, writeTarget);
    if (!read_point || !write_point) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    sgl_buffer_t *src = *read_point ? GET_BUFFER(*read_point) : NULL;
    sgl_buffer_t *dst = *write_point ? GET_BUFFER(*write_point) : NULL;
    if (!src || !dst) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (readOffset < 0 || writeOffset < 0 || size < 0 || readOffset > src->size ||
        size > src->size - readOffset || writeOffset > dst->size || size > dst->size - writeOffset) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* Overlapping ranges of the same buffer */
    if (src == dst && readOffset < writeOffset + size && writeOffset < readOffset + size) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (src->mapped || dst->mapped) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (size == 0 || !ctx->backend->ops->get_data_cpu_ptr || !ctx->backend->ops->buffer_sub_data)
        return;

    /* Done by the CPU, in command order: wait for the commands already issued,
     * which may still read the destination, then copy (the GPU never writes
     * buffers, so the source is current). */
    if (ctx->backend->ops->finish)
        ctx->backend->ops->finish(ctx->backend);
    const uint8_t *from =
        (const uint8_t *)ctx->backend->ops->get_data_cpu_ptr(ctx->backend, src->data_offset) +
        readOffset;
    ctx->backend->ops->buffer_sub_data(ctx->backend, *write_point,
                                       dst->data_offset + (uint32_t)writeOffset, size, from);
    SGL_TRACE_BUFFER("glCopyBufferSubData(0x%X, 0x%X, %td, %td, %td)", readTarget, writeTarget,
                     readOffset, writeOffset, size);
}
