/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Sync Objects (GLES 3.0 §5.2)
 *
 * Fence syncs only (the one sync type of ES 3.0). Each sync object owns the
 * backend fence of the same index; glFenceSync submits the commands issued
 * so far, so a fence always signals without SYNC_FLUSH_COMMANDS_BIT.
 *
 * A context has a single in-order GPU queue and contexts do not share
 * objects, so the commands issued after glWaitSync already run after the
 * fence's commands: glWaitSync only validates its arguments.
 */

#include "gl_common.h"
#include <GLES3/gl3.h>

/* GLsync handle <-> sync object index (handle = index + 1, 0 is no sync) */
static inline GLsync sgl_sync_handle(uint32_t index) {
    return (GLsync)(uintptr_t)(index + 1);
}

/* Live sync object named by `sync`, or NULL. *index receives its index. */
static sgl_sync_t *sgl_get_sync(sgl_context_t *ctx, GLsync sync, uint32_t *index) {
    uintptr_t handle = (uintptr_t)sync;
    if (handle == 0 || handle > SGL_MAX_SYNCS)
        return NULL;
    sgl_sync_t *s = &ctx->res_mgr.syncs[handle - 1];
    if (!s->used)
        return NULL;
    *index = (uint32_t)(handle - 1);
    return s;
}

/* Poll (timeout_ns = 0) or wait for the fence; remembers a signaled fence */
static bool sgl_sync_wait(sgl_context_t *ctx, sgl_sync_t *s, uint32_t index,
                          uint64_t timeout_ns) {
    if (!s->signaled && ctx->backend && ctx->backend->ops->wait_sync)
        s->signaled = ctx->backend->ops->wait_sync(ctx->backend, index, timeout_ns);
    return s->signaled;
}

GL_APICALL GLsync GL_APIENTRY glFenceSync(GLenum condition, GLbitfield flags) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return 0;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return 0;
    }
    CHECK_BACKEND_RET(0);

    if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    if (flags != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return 0;
    }

    uint32_t index = 0;
    while (index < SGL_MAX_SYNCS && ctx->res_mgr.syncs[index].used)
        index++;
    if (index == SGL_MAX_SYNCS || !ctx->backend->ops->fence_sync) {
        sgl_set_error(ctx, GL_OUT_OF_MEMORY);
        return 0;
    }

    sgl_sync_t *s = &ctx->res_mgr.syncs[index];
    s->used = true;
    /* A queue in error state runs nothing more: report the fence signaled
     * rather than let the application wait forever. */
    s->signaled = !ctx->backend->ops->fence_sync(ctx->backend, index);

    SGL_TRACE_CORE("glFenceSync -> %u", index + 1);
    return sgl_sync_handle(index);
}

GL_APICALL GLboolean GL_APIENTRY glIsSync(GLsync sync) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return GL_FALSE;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return GL_FALSE;
    }

    uint32_t index;
    return sgl_get_sync(ctx, sync, &index) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glDeleteSync(GLsync sync) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    if (sync == 0)
        return; /* silently ignored (§5.2) */

    uint32_t index;
    sgl_sync_t *s = sgl_get_sync(ctx, sync, &index);
    if (!s) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* The fence was already submitted (glFenceSync): deko3d keeps no pointer
     * to it, so the slot can be reused at once even if still pending. */
    s->used = false;
    s->signaled = false;
}

GL_APICALL GLenum GL_APIENTRY glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return GL_WAIT_FAILED;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return GL_WAIT_FAILED;
    }

    uint32_t index;
    sgl_sync_t *s = sgl_get_sync(ctx, sync, &index);
    if (!s || (flags & ~(GLbitfield)GL_SYNC_FLUSH_COMMANDS_BIT) != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }

    /* SYNC_FLUSH_COMMANDS_BIT needs no action: glFenceSync submitted already */
    if (sgl_sync_wait(ctx, s, index, 0))
        return GL_ALREADY_SIGNALED;
    if (timeout == 0)
        return GL_TIMEOUT_EXPIRED;
    return sgl_sync_wait(ctx, s, index, timeout) ? GL_CONDITION_SATISFIED : GL_TIMEOUT_EXPIRED;
}

GL_APICALL void GL_APIENTRY glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    uint32_t index;
    if (!sgl_get_sync(ctx, sync, &index) || flags != 0 || timeout != GL_TIMEOUT_IGNORED) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    /* Nothing to do: see the file header (single in-order queue) */
}

GL_APICALL void GL_APIENTRY glGetSynciv(GLsync sync, GLenum pname, GLsizei count, GLsizei *length,
                                        GLint *values) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!sgl_ctx_is_es3(ctx)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    uint32_t index;
    sgl_sync_t *s = sgl_get_sync(ctx, sync, &index);
    if (!s || count < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    GLint value;
    switch (pname) {
        case GL_OBJECT_TYPE:
            value = GL_SYNC_FENCE;
            break;
        case GL_SYNC_STATUS:
            value = sgl_sync_wait(ctx, s, index, 0) ? GL_SIGNALED : GL_UNSIGNALED;
            break;
        case GL_SYNC_CONDITION:
            value = GL_SYNC_GPU_COMMANDS_COMPLETE;
            break;
        case GL_SYNC_FLAGS:
            value = 0;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* Every property is a single integer */
    GLsizei written = 0;
    if (count >= 1 && values) {
        values[0] = value;
        written = 1;
    }
    if (length)
        *length = written;
}
