/*
 * SwitchGLES - EGL context lifecycle
 *
 * eglCreateContext / eglDestroyContext / eglQueryContext.
 * Split out of egl_impl.c; shared helpers are in egl_internal.h.
 */

#include "egl_internal.h"
#include "context/sgl_state_build.h"
#include "util/sgl_log.h"
#include <GLES2/gl2sgl.h>
#include <string.h>
#include <stdio.h>
#include <switch.h>

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                                EGLContext share_context,
                                                const EGLint *attrib_list) {
    SGL_EGL_VTRACE("eglCreateContext(%p, %p)", dpy, config);
    sgl_display *display = (sgl_display *)dpy;
    (void)share_context;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_NO_CONTEXT;
    }

    sgl_config *cfg = sgl_egl_get_config(config);
    if (!cfg) {
        sgl_egl_set_error(EGL_BAD_CONFIG);
        return EGL_NO_CONTEXT;
    }

    EGLint client_version = 1;
    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_CONTEXT_CLIENT_VERSION) {
                client_version = attrib_list[i+1];
            }
        }
    }

    if (client_version != 2) {
        sgl_egl_set_error(EGL_BAD_ATTRIBUTE);
        return EGL_NO_CONTEXT;
    }

    /* Find free context slot */
    int ctx_idx = -1;
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        if (!g_sgl.contexts[i].used) {
            ctx_idx = i;
            break;
        }
    }

    if (ctx_idx < 0) {
        sgl_egl_set_error(EGL_BAD_ALLOC);
        return EGL_NO_CONTEXT;
    }

    sgl_context_t *ctx = &g_sgl.contexts[ctx_idx];

    /* Initialize context */
    sgl_context_init(ctx);
    ctx->client_version = client_version;
    ctx->config_id = cfg->config_id;

    /* Create backend */
    sgl_backend_t *backend = dk_backend_create(display->device);
    if (!backend) {
        sgl_egl_set_error(EGL_BAD_ALLOC);
        return EGL_NO_CONTEXT;
    }

    /* Initialize backend */
    if (backend->ops->init(backend, display->device) != 0) {
        dk_backend_destroy(backend);
        sgl_egl_set_error(EGL_BAD_ALLOC);
        return EGL_NO_CONTEXT;
    }

    ctx->backend = backend;
    g_sgl.backends[ctx_idx] = backend;
    ctx->used = true;

    /* Initialize GL state to defaults */
    sgl_context_init_state(ctx);

    return (EGLContext)ctx;
}
EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext context) {
    SGL_EGL_VTRACE("eglDestroyContext(%p, %p)", dpy, context);
    sgl_display *display = (sgl_display *)dpy;
    sgl_context_t *ctx = (sgl_context_t *)context;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!ctx || !ctx->used) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    /* If the context is current to the (single) thread, defer destruction: the
     * handle is invalid from here on, but the context keeps working until a
     * later eglMakeCurrent makes it non-current (EGL §3.7.2). The actual
     * teardown then happens in sgl_egl_reap_deferred(). */
    if (sgl_get_current_context() == ctx) {
        ctx->delete_pending = true;
        return EGL_TRUE;
    }

    sgl_egl_destroy_context_now(ctx);
    return EGL_TRUE;
}
EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_context_t *context = (sgl_context_t *)ctx;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!context || !context->used || !value) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    switch (attribute) {
        case EGL_CONFIG_ID: *value = context->config_id; break;
        case EGL_CONTEXT_CLIENT_TYPE: *value = EGL_OPENGL_ES_API; break;
        case EGL_CONTEXT_CLIENT_VERSION: *value = context->client_version; break;
        case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
        default:
            sgl_egl_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }

    return EGL_TRUE;
}
