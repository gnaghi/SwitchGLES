/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * EGL implementation using deko3d with new GLOVE architecture
 *
 * Reference: C:/devkitPro/examples/switch/graphics/deko3d/deko_basic/source/main.c
 */

#include "egl_internal.h"
#include "context/sgl_state_build.h"
#include "util/sgl_log.h"
#include "util/sgl_perf.h"
#include <GLES2/gl2sgl.h>
#include <string.h>
#include <stdio.h>
#include <switch.h> /* svcSleepThread */
#ifdef SGL_ENABLE_RUNTIME_COMPILER
#include <uam.h>
#endif

/* SGL_EGL_VERBOSE / SGL_EGL_VTRACE moved to egl_internal.h (shared). */

/* Global state */
sgl_egl_state g_sgl = {0};

/* Ensure last_error starts as EGL_SUCCESS (0x3000), not 0 */
__attribute__((constructor)) static void sgl_init_egl_error(void) {
    g_sgl.last_error = EGL_SUCCESS;
}

/* Helper to set EGL error */
void sgl_egl_set_error(EGLint error) {
    g_sgl.last_error = error;
}

/* sgl_egl_display_valid() is a static inline in egl_internal.h (shared). */

/*
 * Ensure frame is ready for rendering - called at start of frame (e.g., from glClear).
 * This implements the deko_basic pattern of acquiring at frame START, not end.
 */
void sgl_ensure_frame_ready(void) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->draw_surface) {
        return;
    }

    sgl_surface_t *surf = ctx->draw_surface;
    if (!surf->need_acquire) {
        return;
    }

    dk_backend_data_t *dk = ctx->backend ? (dk_backend_data_t *)ctx->backend->impl_data : NULL;
    if (!dk)
        return;

    /* Acquire next framebuffer (blocks until available) */
    int slot = dkQueueAcquireImage(dk->queue, surf->swapchain);
    surf->current_slot = slot;
    surf->need_acquire = false;

    /* Wait for any previous work on this slot to complete and reset command buffer.
     * CRITICAL: Must call wait_fence before reusing the slot's command buffer! */
    if (ctx->backend && ctx->backend->ops->wait_fence) {
        ctx->backend->ops->wait_fence(ctx->backend, slot);
    }

    /* Begin frame in backend */
    if (ctx->backend && ctx->backend->ops->begin_frame) {
        ctx->backend->ops->begin_frame(ctx->backend, slot);
    }

    /* Bind render target - always bind default first to set up backend state */
    DkImageView colorView;
    dkImageViewDefaults(&colorView, &surf->framebuffers[slot]);

    if (surf->depthbuffer_memblocks[slot]) {
        DkImageView depthView;
        dkImageViewDefaults(&depthView, &surf->depthbuffers[slot]);
        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, &depthView);
    } else {
        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, NULL);
    }
    /* Fresh cmdbuf + render-target bind: the state applied below and at the
     * first draw must be recorded whatever the backend cache remembers. */
    dk_state_cache_invalidate(dk);

    /* Store framebuffer info in backend - per-slot depth buffers */
    dk->framebuffers = surf->framebuffers;
    for (int i = 0; i < SGL_FB_NUM; i++) {
        dk->depth_images[i] = surf->depthbuffer_memblocks[i] ? &surf->depthbuffers[i] : NULL;
    }
    dk->num_framebuffers = SGL_FB_NUM;
    dk->swapchain = surf->swapchain;
    dk->fb_width = surf->width;
    dk->fb_height = surf->height;

    /* CRITICAL FIX: If an FBO is bound, re-bind it as render target.
     * We had to bind the default FB first to set up backend state,
     * but if user had an FBO bound, we need to restore that binding. */
    if (ctx->bound_framebuffer != 0 && ctx->backend->ops->bind_framebuffer) {
        sgl_framebuffer_t *fbo = sgl_res_mgr_get_framebuffer(&ctx->res_mgr, ctx->bound_framebuffer);
        if (fbo && fbo->color_attachment != 0) {
            ctx->backend->ops->bind_framebuffer(
                ctx->backend, ctx->bound_framebuffer, fbo->color_attachment, fbo->depth_attachment,
                fbo->color_is_renderbuffer, fbo->depth_is_renderbuffer, fbo->stencil_attachment,
                fbo->stencil_is_renderbuffer);
        }
    }

    /* Set viewport and scissor — OriginLowerLeft handles Y-flip natively.
     * No manual Y-flip needed, matching dk_apply_viewport/dk_apply_scissor. */
    DkViewport viewport = {(float)ctx->viewport_state.viewport_x,
                           (float)ctx->viewport_state.viewport_y,
                           (float)ctx->viewport_state.viewport_width,
                           (float)ctx->viewport_state.viewport_height,
                           ctx->viewport_state.depth_near,
                           ctx->viewport_state.depth_far};

    int sc_sx = ctx->viewport_state.scissor_x;
    int sc_sy = ctx->viewport_state.scissor_y;
    int sc_sw = ctx->viewport_state.scissor_width;
    int sc_sh = ctx->viewport_state.scissor_height;
    if (sc_sx < 0) {
        sc_sw += sc_sx;
        sc_sx = 0;
    }
    if (sc_sy < 0) {
        sc_sh += sc_sy;
        sc_sy = 0;
    }
    if (sc_sw < 0)
        sc_sw = 0;
    if (sc_sh < 0)
        sc_sh = 0;
    DkScissor scissor = {(uint32_t)sc_sx, (uint32_t)sc_sy, (uint32_t)sc_sw, (uint32_t)sc_sh};

    dkCmdBufSetViewports(dk->cmdbuf, 0, &viewport, 1);
    dkCmdBufSetScissors(dk->cmdbuf, 0, &scissor, 1);

    /* Re-apply all GL state to the new command buffer.
     * This is CRITICAL because each frame uses a different cmdbuf slot,
     * and state bindings are recorded per-cmdbuf. Without this, only
     * the first frame would have correct state (e.g., depth test, culling). */

    /* Apply raster state (face culling + polygon offset) */
    if (ctx->backend->ops->apply_raster) {
        sgl_raster_state_t rs;
        sgl_build_raster(ctx, &rs);
        ctx->backend->ops->apply_raster(ctx->backend, &rs);
    }

    /* Apply combined depth-stencil state (avoids overwrite issues) */
    if (ctx->backend->ops->apply_depth_stencil) {
        sgl_depth_stencil_state_t dss;
        sgl_build_depth_stencil(ctx, &dss);
        ctx->backend->ops->apply_depth_stencil(ctx->backend, &dss);
    }

    /* Apply blend state */
    if (ctx->backend->ops->apply_blend) {
        sgl_blend_state_t bs;
        sgl_build_blend(ctx, &bs);
        ctx->backend->ops->apply_blend(ctx->backend, &bs);
    }

    /* Apply color mask */
    if (ctx->backend->ops->apply_color_mask) {
        sgl_color_state_t cs;
        sgl_build_color(ctx, &cs);
        ctx->backend->ops->apply_color_mask(ctx->backend, &cs);
    }
}

/* ============================================================================
 * EGL 1.0 Core Functions
 * ============================================================================ */

EGLAPI EGLint EGLAPIENTRY eglGetError(void) {
    EGLint error = g_sgl.last_error;
    g_sgl.last_error = EGL_SUCCESS;
    if (error != EGL_SUCCESS)
        SGL_EGL_VTRACE("eglGetError() = 0x%04x", error);
    return error;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id) {
    SGL_EGL_VTRACE("eglGetDisplay(%p)", (void *)(uintptr_t)display_id);
    (void)display_id;
    return (EGLDisplay)&g_sgl.display;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    SGL_EGL_VTRACE("eglInitialize(%p)", dpy);
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (display->initialized) {
        if (major)
            *major = display->major_version;
        if (minor)
            *minor = display->minor_version;
        return EGL_TRUE;
    }

    /* Create deko3d device with OpenGL-compatible settings.
     * The device is KEPT ALIVE across eglTerminate/eglInitialize cycles to
     * prevent GPU memory fragmentation. After ~20 cycles of device create/destroy,
     * the GPU address space fragments and dkMemBlockCreate fails with OOM. */
    if (!display->device) {
        DkDeviceMaker deviceMaker;
        dkDeviceMakerDefaults(&deviceMaker);
        deviceMaker.flags = DkDeviceFlags_OriginLowerLeft | DkDeviceFlags_DepthMinusOneToOne;

        for (int attempt = 0; attempt < 3; attempt++) {
            display->device = dkDeviceCreate(&deviceMaker);
            if (display->device)
                break;
            svcSleepThread(200000000ULL); /* 200ms */
        }
    }

    if (!display->device) {
        sgl_egl_set_error(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    /* Initialize predefined configs.
     * Config 1 (with depth/stencil) is first so eglChooseConfig returns
     * it by default — most apps and dEQP expect depth on the default FB. */
    g_sgl.configs[0].config_id = 1;
    g_sgl.configs[0].red_size = 8;
    g_sgl.configs[0].green_size = 8;
    g_sgl.configs[0].blue_size = 8;
    g_sgl.configs[0].alpha_size = 8;
    g_sgl.configs[0].depth_size = 24;
    g_sgl.configs[0].stencil_size = 8;
    g_sgl.configs[0].samples = 0;
    g_sgl.configs[0].surface_type = EGL_WINDOW_BIT;
    g_sgl.configs[0].renderable_type = EGL_OPENGL_ES2_BIT;

    g_sgl.configs[1].config_id = 2;
    g_sgl.configs[1].red_size = 8;
    g_sgl.configs[1].green_size = 8;
    g_sgl.configs[1].blue_size = 8;
    g_sgl.configs[1].alpha_size = 8;
    g_sgl.configs[1].depth_size = 0;
    g_sgl.configs[1].stencil_size = 0;
    g_sgl.configs[1].samples = 0;
    g_sgl.configs[1].surface_type = EGL_WINDOW_BIT;
    g_sgl.configs[1].renderable_type = EGL_OPENGL_ES2_BIT;

    g_sgl.num_configs = 2;

    display->major_version = 1;
    display->minor_version = 4;
    display->initialized = true;

#ifdef SGL_ENABLE_RUNTIME_COMPILER
    /* Keep Mesa's GLSL frontend (types + builtin functions) alive while the
     * display is initialized. Otherwise every per-shader uam compiler rebuilds
     * all builtins from scratch (~28 ms per shader on Switch, ~3x the compile). */
    uam_retain_frontend();
#endif

    if (major)
        *major = display->major_version;
    if (minor)
        *minor = display->minor_version;

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy) {
    SGL_EGL_VTRACE("eglTerminate(%p)", dpy);
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!display->initialized) {
        return EGL_TRUE;
    }

    /* Proper deko3d shutdown order (matches deko_basic):
     *   1. Wait queue idle
     *   2. Destroy swapchain + surface memblocks (needs device alive)
     *   3. Destroy backends (cmdbufs, memblocks, queue)
     *   4. Destroy device
     * This is critical for multi-context apps like dEQP that call
     * eglTerminate + eglInitialize multiple times in one process. */
    SGL_TRACE_EGL("eglTerminate: starting cleanup");

    /* Step 1: Wait all queues idle before destroying anything */
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        if (g_sgl.backends[i]) {
            dk_backend_data_t *dk = (dk_backend_data_t *)g_sgl.backends[i]->impl_data;
            if (dk && dk->queue) {
                dkQueueWaitIdle(dk->queue);
            }
        }
    }
    SGL_TRACE_EGL("eglTerminate: queues idle");

    /* Step 2: Destroy swapchains + surface memblocks (device must still be alive) */
    for (int i = 0; i < SGL_MAX_SURFACES; i++) {
        if (g_sgl.surfaces[i].used && g_sgl.surfaces[i].swapchain) {
            dkSwapchainDestroy(g_sgl.surfaces[i].swapchain);
            g_sgl.surfaces[i].swapchain = NULL;
            if (g_sgl.surfaces[i].framebuffer_memblock) {
                dkMemBlockDestroy(g_sgl.surfaces[i].framebuffer_memblock);
                g_sgl.surfaces[i].framebuffer_memblock = NULL;
            }
            for (int j = 0; j < SGL_FB_NUM; j++) {
                if (g_sgl.surfaces[i].depthbuffer_memblocks[j]) {
                    dkMemBlockDestroy(g_sgl.surfaces[i].depthbuffer_memblocks[j]);
                    g_sgl.surfaces[i].depthbuffer_memblocks[j] = NULL;
                }
            }
            g_sgl.surfaces[i].used = false;
        }
    }
    SGL_TRACE_EGL("eglTerminate: swapchain destroyed");

    /* Step 3: Destroy backends (cmdbufs, backend memblocks, queue) */
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        if (g_sgl.contexts[i].used && g_sgl.backends[i]) {
            dk_backend_destroy(g_sgl.backends[i]);
            g_sgl.backends[i] = NULL;
            sgl_context_destroy(&g_sgl.contexts[i]);
        }
    }
    SGL_TRACE_EGL("eglTerminate: backends destroyed");

    /* Step 4: Keep the deko3d device alive across eglTerminate/eglInitialize.
     * Destroying and recreating the device causes GPU address space fragmentation
     * (OOM after ~20 cycles). The device is only destroyed at process exit. */
    SGL_TRACE_EGL("eglTerminate: device kept alive (anti-fragmentation)");

#ifdef SGL_ENABLE_RUNTIME_COMPILER
    uam_release_frontend(); /* Balances the retain in eglInitialize */
#endif

    display->initialized = false;
    sgl_set_current_context(NULL);
    g_sgl.current_context = NULL;
    g_sgl.current_display = NULL;

    return EGL_TRUE;
}

GL_APICALL void GL_APIENTRY sglShutdown(void) {
    /* Final process-exit teardown. eglTerminate keeps the DkDevice alive
     * (anti-fragmentation), so this is the ONLY place that destroys it and
     * releases the GPU/nvservices session. Without it, the next process on
     * the same console inherits a wedged GPU and crashes on first use.
     * Idempotent and tolerant of a partially torn-down state. */

    /* Tear down any backends/contexts still live (covers the path where
     * eglTerminate was not called, e.g. an aborted run). Normally these are
     * already gone and the loops are no-ops. Backends are destroyed before
     * the device since they hold queues/memblocks owned by it. */
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        if (g_sgl.backends[i]) {
            dk_backend_destroy(g_sgl.backends[i]);
            g_sgl.backends[i] = NULL;
        }
        if (g_sgl.contexts[i].used) {
            sgl_context_destroy(&g_sgl.contexts[i]);
        }
    }

    for (int i = 0; i < SGL_MAX_SURFACES; i++) {
        sgl_surface *s = &g_sgl.surfaces[i];
        if (!s->used)
            continue;
        if (s->swapchain)
            dkSwapchainDestroy(s->swapchain);
        if (s->framebuffer_memblock)
            dkMemBlockDestroy(s->framebuffer_memblock);
        for (int j = 0; j < SGL_FB_NUM; j++) {
            if (s->depthbuffer_memblocks[j])
                dkMemBlockDestroy(s->depthbuffer_memblocks[j]);
        }
        memset(s, 0, sizeof(*s));
    }

    /* The device itself — the gap eglTerminate deliberately leaves open. */
    if (g_sgl.display.device) {
        dkDeviceDestroy(g_sgl.display.device);
        g_sgl.display.device = NULL;
    }

#ifdef SGL_ENABLE_RUNTIME_COMPILER
    if (g_sgl.display.initialized)
        uam_release_frontend(); /* eglTerminate was skipped: balance eglInitialize */
#endif

    g_sgl.display.initialized = false;
    sgl_set_current_context(NULL);
    g_sgl.current_context = NULL;
    g_sgl.current_display = NULL;

    SGL_TRACE_EGL("sglShutdown: device destroyed, GPU released");
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name) {
    sgl_display *display = (sgl_display *)dpy;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return NULL;
    }

    switch (name) {
        case EGL_VENDOR:
            return "SwitchGLES";
        case EGL_VERSION:
            return "1.4 SwitchGLES";
        case EGL_EXTENSIONS:
            return "";
        case EGL_CLIENT_APIS:
            return "OpenGL_ES";
        default:
            sgl_egl_set_error(EGL_BAD_PARAMETER);
            return NULL;
    }
}

/* ============================================================================
 * EGL config/surface/context helpers (validation + deferred teardown)
 * ============================================================================ */

/* Validate an EGLConfig handle: it must point at one of our predefined
 * configs. Returns the config, or NULL (caller should set EGL_BAD_CONFIG). */
sgl_config *sgl_egl_get_config(EGLConfig config) {
    for (int i = 0; i < g_sgl.num_configs; i++) {
        if ((sgl_config *)config == &g_sgl.configs[i]) {
            return &g_sgl.configs[i];
        }
    }
    return NULL;
}

/* Validate an EGLSurface handle: must point at a live surface in the pool.
 * Returns the surface, or NULL (caller should set EGL_BAD_SURFACE). */
static sgl_surface *sgl_egl_get_surface(EGLSurface surface) {
    for (int i = 0; i < SGL_MAX_SURFACES; i++) {
        if ((sgl_surface *)surface == &g_sgl.surfaces[i]) {
            return g_sgl.surfaces[i].used ? &g_sgl.surfaces[i] : NULL;
        }
    }
    return NULL;
}

/* Actually tear down a surface's GPU resources and free the slot. Shared by the
 * immediate-destroy path and deferred reaping from eglMakeCurrent. */
void sgl_egl_destroy_surface_now(sgl_surface *surf) {
    /* Wait for GPU to finish */
    if (sgl_get_current_context() && sgl_get_current_context()->backend) {
        dk_backend_data_t *dk = (dk_backend_data_t *)sgl_get_current_context()->backend->impl_data;
        if (dk && dk->queue) {
            dkQueueWaitIdle(dk->queue);
        }
    }

    /* Detach this surface from any context still referencing it so the
     * memset below cannot leave a dangling draw_surface/read_surface. */
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        sgl_context_t *c = &g_sgl.contexts[i];
        if (!c->used)
            continue;
        if (c->draw_surface == surf)
            c->draw_surface = NULL;
        if (c->read_surface == surf)
            c->read_surface = NULL;
    }

    if (surf->swapchain)
        dkSwapchainDestroy(surf->swapchain);
    if (surf->framebuffer_memblock)
        dkMemBlockDestroy(surf->framebuffer_memblock);
    for (int i = 0; i < SGL_FB_NUM; i++) {
        if (surf->depthbuffer_memblocks[i])
            dkMemBlockDestroy(surf->depthbuffer_memblocks[i]);
    }

    memset(surf, 0, sizeof(sgl_surface));
}

/* Actually tear down a context's backend and free the slot. */
void sgl_egl_destroy_context_now(sgl_context_t *ctx) {
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        if (&g_sgl.contexts[i] == ctx && g_sgl.backends[i]) {
            dk_backend_destroy(g_sgl.backends[i]);
            g_sgl.backends[i] = NULL;
            break;
        }
    }
    sgl_context_destroy(ctx);
}

/* Reap objects whose destruction was deferred because they were current. Called
 * from eglMakeCurrent once the current binding has changed: any prior
 * context/surface flagged delete_pending that is no longer current is torn down
 * now (EGL §3.5.4 / §3.7.2). prev_draw/prev_read may alias or be NULL. */
static void sgl_egl_reap_deferred(sgl_context_t *prev_ctx, sgl_surface *prev_draw,
                                  sgl_surface *prev_read) {
    sgl_context_t *cur = sgl_get_current_context();

    if (prev_draw && prev_draw->used && prev_draw->delete_pending &&
        !(cur && (cur->draw_surface == prev_draw || cur->read_surface == prev_draw))) {
        sgl_egl_destroy_surface_now(prev_draw);
    }
    if (prev_read && prev_read != prev_draw && prev_read->used && prev_read->delete_pending &&
        !(cur && (cur->draw_surface == prev_read || cur->read_surface == prev_read))) {
        sgl_egl_destroy_surface_now(prev_read);
    }
    if (prev_ctx && prev_ctx != cur && prev_ctx->delete_pending) {
        sgl_egl_destroy_context_now(prev_ctx);
    }
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                                             EGLContext context) {
    SGL_EGL_VTRACE("eglMakeCurrent(%p, %p, %p, %p)", dpy, draw, read, context);
    sgl_display *display = (sgl_display *)dpy;
    sgl_surface *draw_surf = (sgl_surface *)draw;
    sgl_surface *read_surf = (sgl_surface *)read;
    sgl_context_t *ctx = (sgl_context_t *)context;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    /* Capture the outgoing context and its surfaces so that, once the binding
     * changes below, we can reap any of them whose destruction was deferred
     * while they were current (EGL §3.5.4 / §3.7.2). */
    sgl_context_t *prev_ctx = sgl_get_current_context();
    sgl_surface *prev_draw = prev_ctx ? prev_ctx->draw_surface : NULL;
    sgl_surface *prev_read = prev_ctx ? prev_ctx->read_surface : NULL;

    /* Release case: a NULL context requires both surfaces to be EGL_NO_SURFACE
     * (EGL 1.4 §3.7.3), otherwise it is a mismatch. */
    if (context == EGL_NO_CONTEXT) {
        if (draw != EGL_NO_SURFACE || read != EGL_NO_SURFACE) {
            sgl_egl_set_error(EGL_BAD_MATCH);
            return EGL_FALSE;
        }
        sgl_set_current_context(NULL);
        g_sgl.current_context = NULL;
        g_sgl.current_display = NULL;
        sgl_egl_reap_deferred(prev_ctx, prev_draw, prev_read);
        return EGL_TRUE;
    }

    if (!ctx || !ctx->used) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    /* No surfaceless-context support: a non-NULL context requires both a draw
     * and a read surface (EGL 1.4 §3.7.3 → EGL_BAD_MATCH if either is absent). */
    if (draw == EGL_NO_SURFACE || read == EGL_NO_SURFACE) {
        sgl_egl_set_error(EGL_BAD_MATCH);
        return EGL_FALSE;
    }

    /* The surface handles must reference live surfaces. */
    draw_surf = sgl_egl_get_surface(draw);
    read_surf = sgl_egl_get_surface(read);
    if (!draw_surf || !read_surf) {
        sgl_egl_set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    ctx->draw_surface = draw_surf;
    ctx->read_surface = read_surf;

    sgl_set_current_context(ctx);
    g_sgl.current_context = ctx;
    g_sgl.current_display = display;

    /* Get backend */
    dk_backend_data_t *dk = ctx->backend ? (dk_backend_data_t *)ctx->backend->impl_data : NULL;
    if (!dk) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    /* Context switch: the fixed-function state recorded for this backend is
     * not trusted across a MakeCurrent; the next applies record everything. */
    dk_state_cache_invalidate(dk);

    /* Store framebuffer info - per-slot depth buffers */
    if (draw_surf) {
        dk->framebuffers = draw_surf->framebuffers;
        for (int i = 0; i < SGL_FB_NUM; i++) {
            dk->depth_images[i] =
                draw_surf->depthbuffer_memblocks[i] ? &draw_surf->depthbuffers[i] : NULL;
        }
        dk->num_framebuffers = SGL_FB_NUM;
        dk->swapchain = draw_surf->swapchain;
        dk->fb_width = draw_surf->width;
        dk->fb_height = draw_surf->height;
    }

    /* Acquire first frame via sgl_ensure_frame_ready() rather than inline code.
     * This ensures full GPU state sync (viewport, scissor, depth, blend, etc.)
     * which the old inline acquisition omitted. The full state sync is critical
     * for test suites where postIterate() calls eglSwapBuffers between tests,
     * leaving need_acquire=true for the next gluStateReset → glBindFramebuffer
     * → sgl_ensure_frame_ready cycle. */
    sgl_ensure_frame_ready();

    /* The previously-current context/surfaces are no longer current; reap any
     * that were flagged for deferred destruction. */
    sgl_egl_reap_deferred(prev_ctx, prev_draw, prev_read);

    return EGL_TRUE;
}

/* ============================================================================
 * EGL Swap/Presentation Functions
 * ============================================================================ */

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    SGL_EGL_VTRACE("eglSwapBuffers(%p, %p)", dpy, surface);
    sgl_display *display = (sgl_display *)dpy;
    sgl_surface *surf = (sgl_surface *)surface;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!surf || !surf->used) {
        sgl_egl_set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    dk_backend_data_t *dk = (dk_backend_data_t *)ctx->backend->impl_data;
    if (!dk) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    /* If no rendering happened since last swap (need_acquire still true),
       skip the swap - the previous frame is still displayed */
    if (surf->need_acquire) {
        return EGL_TRUE;
    }

    int slot = surf->current_slot;
    SGL_PERF_BEGIN(perf_swap);

    /* End frame */
    if (ctx->backend->ops->end_frame) {
        ctx->backend->ops->end_frame(ctx->backend, slot);
    }

    /* Present */
    if (ctx->backend->ops->present) {
        ctx->backend->ops->present(ctx->backend, slot);
    }

    /* Mark that we need to acquire at start of next frame */
    surf->need_acquire = true;
    SGL_PERF_END(SGL_PERF_SWAP, perf_swap);
    SGL_PERF_FRAME();

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    SGL_EGL_VTRACE("eglSwapInterval(%p, %d)", dpy, interval);
    sgl_display *display = (sgl_display *)dpy;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->draw_surface) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    /* Clamp to [EGL_MIN_SWAP_INTERVAL, EGL_MAX_SWAP_INTERVAL] (0..4 here);
     * a negative interval would otherwise wrap to a huge uint32_t. */
    if (interval < 0)
        interval = 0;
    if (interval > 4)
        interval = 4;

    sgl_surface *surf = ctx->draw_surface;
    if (surf->swapchain) {
        dkSwapchainSetSwapInterval(surf->swapchain, (uint32_t)interval);
    }

    return EGL_TRUE;
}

/* ============================================================================
 * EGL Query Functions
 * ============================================================================ */

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void) {
    return (EGLContext)sgl_get_current_context();
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void) {
    return (EGLDisplay)g_sgl.current_display;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw) {
    if (!sgl_get_current_context())
        return EGL_NO_SURFACE;
    if (readdraw == EGL_DRAW)
        return (EGLSurface)sgl_get_current_context()->draw_surface;
    if (readdraw == EGL_READ)
        return (EGLSurface)sgl_get_current_context()->read_surface;
    sgl_egl_set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

/* ============================================================================
 * EGL 1.2 Functions
 * ============================================================================ */

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api) {
    SGL_EGL_VTRACE("eglBindAPI(0x%x)", api);
    if (api != EGL_OPENGL_ES_API) {
        sgl_egl_set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    g_sgl.current_api = api;
    return EGL_TRUE;
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void) {
    return g_sgl.current_api ? g_sgl.current_api : EGL_OPENGL_ES_API;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient(void) {
    if (sgl_get_current_context() && sgl_get_current_context()->backend) {
        if (sgl_get_current_context()->backend->ops->finish) {
            sgl_get_current_context()->backend->ops->finish(sgl_get_current_context()->backend);
        }
    }
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void) {
    return EGL_TRUE;
}

/* ============================================================================
 * Stub Functions
 * ============================================================================ */

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void) {
    return eglWaitClient();
}
EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine) {
    (void)engine;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                                             EGLNativePixmapType target) {
    (void)dpy;
    (void)surface;
    (void)target;
    sgl_egl_set_error(EGL_BAD_NATIVE_PIXMAP);
    return EGL_FALSE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                      const EGLint *attrib_list) {
    (void)dpy;
    (void)config;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_MATCH);
    return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativePixmapType pixmap,
                                                     const EGLint *attrib_list) {
    (void)dpy;
    (void)config;
    (void)pixmap;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_NATIVE_PIXMAP);
    return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                                               EGLint value) {
    (void)dpy;
    (void)surface;
    (void)attribute;
    (void)value;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    (void)dpy;
    (void)surface;
    (void)buffer;
    sgl_egl_set_error(EGL_BAD_SURFACE);
    return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface,
                                                 EGLint buffer) {
    (void)dpy;
    (void)surface;
    (void)buffer;
    sgl_egl_set_error(EGL_BAD_SURFACE);
    return EGL_FALSE;
}

/* ============================================================================
 * eglGetProcAddress - GL function pointer dispatch table
 *
 * Spearmint (and other engines) load all GL functions through this.
 * ============================================================================ */

/* Forward-declare stubs for functions not in GLES2 headers */
GL_APICALL void GL_APIENTRY glFogf(GLenum pname, GLfloat param);
GL_APICALL void GL_APIENTRY glFogfv(GLenum pname, const GLfloat *params);
GL_APICALL void GL_APIENTRY glBlitFramebuffer(GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                                              GLint, GLbitfield, GLenum);
GL_APICALL void GL_APIENTRY glRenderbufferStorageMultisample(GLenum, GLsizei, GLenum, GLsizei,
                                                             GLsizei);
GL_APICALL const GLubyte *GL_APIENTRY glGetStringi(GLenum name, GLuint index);
GL_APICALL void GL_APIENTRY glInsertEventMarkerEXT(GLsizei length, const GLchar *marker);
GL_APICALL void GL_APIENTRY glPushGroupMarkerEXT(GLsizei length, const GLchar *marker);
GL_APICALL void GL_APIENTRY glPopGroupMarkerEXT(void);

typedef struct {
    const char *name;
    void (*func)(void);
} sgl_proc_entry_t;

#define PROC_ENTRY(fn) {#fn, (void (*)(void))fn}

static const sgl_proc_entry_t s_proc_table[] = {
    /* QGL_1_1_PROCS */
    PROC_ENTRY(glBindTexture),
    PROC_ENTRY(glBlendFunc),
    PROC_ENTRY(glClearColor),
    PROC_ENTRY(glClear),
    PROC_ENTRY(glClearStencil),
    PROC_ENTRY(glColorMask),
    PROC_ENTRY(glCopyTexSubImage2D),
    PROC_ENTRY(glCullFace),
    PROC_ENTRY(glDeleteTextures),
    PROC_ENTRY(glDepthFunc),
    PROC_ENTRY(glDepthMask),
    PROC_ENTRY(glDisable),
    PROC_ENTRY(glDrawArrays),
    PROC_ENTRY(glDrawElements),
    PROC_ENTRY(glEnable),
    PROC_ENTRY(glFinish),
    PROC_ENTRY(glFlush),
    PROC_ENTRY(glFrontFace),
    PROC_ENTRY(glGenTextures),
    PROC_ENTRY(glGetBooleanv),
    PROC_ENTRY(glGetError),
    PROC_ENTRY(glGetIntegerv),
    PROC_ENTRY(glGetString),
    PROC_ENTRY(glHint),
    PROC_ENTRY(glLineWidth),
    PROC_ENTRY(glPolygonOffset),
    PROC_ENTRY(glReadPixels),
    PROC_ENTRY(glScissor),
    PROC_ENTRY(glStencilFunc),
    PROC_ENTRY(glStencilMask),
    PROC_ENTRY(glStencilOp),
    PROC_ENTRY(glTexImage2D),
    PROC_ENTRY(glTexParameterf),
    PROC_ENTRY(glTexParameteri),
    PROC_ENTRY(glTexSubImage2D),
    PROC_ENTRY(glViewport),
    /* Fixed-function stubs */
    PROC_ENTRY(glFogf),
    PROC_ENTRY(glFogfv),

    /* QGL_ES_1_1_PROCS */
    PROC_ENTRY(glClearDepthf),
    PROC_ENTRY(glDepthRangef),

    /* QGL_1_3_PROCS */
    PROC_ENTRY(glActiveTexture),
    PROC_ENTRY(glCompressedTexImage2D),
    PROC_ENTRY(glCompressedTexSubImage2D),

    /* QGL_1_5_PROCS */
    PROC_ENTRY(glBindBuffer),
    PROC_ENTRY(glDeleteBuffers),
    PROC_ENTRY(glGenBuffers),
    PROC_ENTRY(glBufferData),
    PROC_ENTRY(glBufferSubData),

    /* QGL_2_0_PROCS */
    PROC_ENTRY(glAttachShader),
    PROC_ENTRY(glBindAttribLocation),
    PROC_ENTRY(glCompileShader),
    PROC_ENTRY(glCreateProgram),
    PROC_ENTRY(glCreateShader),
    PROC_ENTRY(glDeleteProgram),
    PROC_ENTRY(glDeleteShader),
    PROC_ENTRY(glDetachShader),
    PROC_ENTRY(glDisableVertexAttribArray),
    PROC_ENTRY(glEnableVertexAttribArray),
    PROC_ENTRY(glGetActiveUniform),
    PROC_ENTRY(glGetProgramiv),
    PROC_ENTRY(glGetProgramInfoLog),
    PROC_ENTRY(glGetShaderiv),
    PROC_ENTRY(glGetShaderInfoLog),
    PROC_ENTRY(glGetShaderSource),
    PROC_ENTRY(glGetUniformLocation),
    PROC_ENTRY(glLinkProgram),
    PROC_ENTRY(glShaderSource),
    PROC_ENTRY(glUseProgram),
    PROC_ENTRY(glUniform1f),
    PROC_ENTRY(glUniform2f),
    PROC_ENTRY(glUniform3f),
    PROC_ENTRY(glUniform4f),
    PROC_ENTRY(glUniform1i),
    PROC_ENTRY(glUniform1fv),
    PROC_ENTRY(glUniform1iv),
    PROC_ENTRY(glUniformMatrix4fv),
    PROC_ENTRY(glValidateProgram),
    PROC_ENTRY(glVertexAttribPointer),

    /* QGL_ARB_framebuffer_object_PROCS */
    PROC_ENTRY(glBindRenderbuffer),
    PROC_ENTRY(glDeleteRenderbuffers),
    PROC_ENTRY(glGenRenderbuffers),
    PROC_ENTRY(glRenderbufferStorage),
    PROC_ENTRY(glBindFramebuffer),
    PROC_ENTRY(glDeleteFramebuffers),
    PROC_ENTRY(glGenFramebuffers),
    PROC_ENTRY(glCheckFramebufferStatus),
    PROC_ENTRY(glFramebufferTexture2D),
    PROC_ENTRY(glFramebufferRenderbuffer),
    PROC_ENTRY(glGenerateMipmap),
    PROC_ENTRY(glBlitFramebuffer),
    PROC_ENTRY(glRenderbufferStorageMultisample),

    /* QGL_3_0_PROCS */
    PROC_ENTRY(glGetStringi),

    /* Additional GLES2 functions that engines may query */
    PROC_ENTRY(glGetFloatv),
    PROC_ENTRY(glIsEnabled),
    PROC_ENTRY(glBlendFuncSeparate),
    PROC_ENTRY(glBlendEquation),
    PROC_ENTRY(glBlendEquationSeparate),
    PROC_ENTRY(glBlendColor),
    PROC_ENTRY(glStencilFuncSeparate),
    PROC_ENTRY(glStencilOpSeparate),
    PROC_ENTRY(glStencilMaskSeparate),
    PROC_ENTRY(glPixelStorei),
    PROC_ENTRY(glTexParameterfv),
    PROC_ENTRY(glTexParameteriv),
    PROC_ENTRY(glGetTexParameterfv),
    PROC_ENTRY(glGetTexParameteriv),
    PROC_ENTRY(glCopyTexImage2D),
    PROC_ENTRY(glGetAttribLocation),
    PROC_ENTRY(glGetActiveAttrib),
    PROC_ENTRY(glGetUniformfv),
    PROC_ENTRY(glGetUniformiv),
    PROC_ENTRY(glGetVertexAttribfv),
    PROC_ENTRY(glGetVertexAttribiv),
    PROC_ENTRY(glGetVertexAttribPointerv),
    PROC_ENTRY(glVertexAttrib1f),
    PROC_ENTRY(glVertexAttrib2f),
    PROC_ENTRY(glVertexAttrib3f),
    PROC_ENTRY(glVertexAttrib4f),
    PROC_ENTRY(glVertexAttrib1fv),
    PROC_ENTRY(glVertexAttrib2fv),
    PROC_ENTRY(glVertexAttrib3fv),
    PROC_ENTRY(glVertexAttrib4fv),
    PROC_ENTRY(glUniform2i),
    PROC_ENTRY(glUniform3i),
    PROC_ENTRY(glUniform4i),
    PROC_ENTRY(glUniform2fv),
    PROC_ENTRY(glUniform3fv),
    PROC_ENTRY(glUniform4fv),
    PROC_ENTRY(glUniform2iv),
    PROC_ENTRY(glUniform3iv),
    PROC_ENTRY(glUniform4iv),
    PROC_ENTRY(glUniformMatrix2fv),
    PROC_ENTRY(glUniformMatrix3fv),
    PROC_ENTRY(glIsBuffer),
    PROC_ENTRY(glIsTexture),
    PROC_ENTRY(glIsFramebuffer),
    PROC_ENTRY(glIsRenderbuffer),
    PROC_ENTRY(glIsProgram),
    PROC_ENTRY(glIsShader),
    PROC_ENTRY(glSampleCoverage),
    PROC_ENTRY(glGetShaderPrecisionFormat),
    PROC_ENTRY(glShaderBinary),
    PROC_ENTRY(glReleaseShaderCompiler),
    PROC_ENTRY(glGetBufferParameteriv),
    PROC_ENTRY(glGetFramebufferAttachmentParameteriv),
    PROC_ENTRY(glGetRenderbufferParameteriv),
    PROC_ENTRY(glGetAttachedShaders),

    /* GL_EXT_debug_marker */
    PROC_ENTRY(glInsertEventMarkerEXT),
    PROC_ENTRY(glPushGroupMarkerEXT),
    PROC_ENTRY(glPopGroupMarkerEXT),

    {NULL, NULL}};

#undef PROC_ENTRY

EGLAPI
    __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *procname) {
    if (!procname)
        return NULL;
    for (const sgl_proc_entry_t *e = s_proc_table; e->name; e++) {
        if (strcmp(e->name, procname) == 0)
            return (__eglMustCastToProperFunctionPointerType)e->func;
    }
    return NULL;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
                                                               EGLClientBuffer buffer,
                                                               EGLConfig config,
                                                               const EGLint *attrib_list) {
    (void)dpy;
    (void)buftype;
    (void)buffer;
    (void)config;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

/* ---- EGL 1.5 stubs (SwitchGLES implements EGL 1.4 only) ---- */

EGLAPI EGLint EGLAPIENTRY eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags,
                                            EGLTime timeout) {
    (void)dpy;
    (void)sync;
    (void)flags;
    (void)timeout;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_FALSE;
}

EGLAPI EGLImage EGLAPIENTRY eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                                           EGLClientBuffer buffer, const EGLAttrib *attrib_list) {
    (void)dpy;
    (void)ctx;
    (void)target;
    (void)buffer;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_NO_IMAGE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config,
                                                             void *native_pixmap,
                                                             const EGLAttrib *attrib_list) {
    (void)dpy;
    (void)config;
    (void)native_pixmap;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                             void *native_window,
                                                             const EGLAttrib *attrib_list) {
    (void)dpy;
    (void)config;
    (void)native_window;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_NO_SURFACE;
}

EGLAPI EGLSync EGLAPIENTRY eglCreateSync(EGLDisplay dpy, EGLenum type,
                                         const EGLAttrib *attrib_list) {
    (void)dpy;
    (void)type;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_NO_SYNC;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImage(EGLDisplay dpy, EGLImage image) {
    (void)dpy;
    (void)image;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySync(EGLDisplay dpy, EGLSync sync) {
    (void)dpy;
    (void)sync;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_FALSE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplay(EGLenum platform, void *native_display,
                                                    const EGLAttrib *attrib_list) {
    (void)platform;
    (void)native_display;
    (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_PARAMETER);
    return EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute,
                                               EGLAttrib *value) {
    (void)dpy;
    (void)sync;
    (void)attribute;
    (void)value;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) {
    (void)dpy;
    (void)sync;
    (void)flags;
    sgl_egl_set_error(EGL_BAD_DISPLAY);
    return EGL_FALSE;
}
