/*
 * SwitchGLES - EGL surface lifecycle
 *
 * eglCreateWindowSurface / eglDestroySurface / eglQuerySurface.
 * Split out of egl_impl.c; shared helpers are in egl_internal.h.
 */

#include "egl_internal.h"
#include "context/sgl_state_build.h"
#include "util/sgl_log.h"
#include <GLES2/gl2sgl.h>
#include <string.h>
#include <stdio.h>
#include <switch.h>

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativeWindowType win,
                                                     const EGLint *attrib_list) {
    SGL_EGL_VTRACE("eglCreateWindowSurface(%p, %p, %p)", dpy, config, (void *)win);
    sgl_display *display = (sgl_display *)dpy;
    sgl_config *cfg = (sgl_config *)config;
    (void)attrib_list;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }

    /* Validate config before dereferencing it (cfg->depth_size below). */
    cfg = sgl_egl_get_config(config);
    if (!cfg) {
        sgl_egl_set_error(EGL_BAD_CONFIG);
        return EGL_NO_SURFACE;
    }

    /* Find free surface slot */
    sgl_surface *surf = NULL;
    for (int i = 0; i < SGL_MAX_SURFACES; i++) {
        if (!g_sgl.surfaces[i].used) {
            surf = &g_sgl.surfaces[i];
            break;
        }
    }

    if (!surf) {
        sgl_egl_set_error(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    memset(surf, 0, sizeof(sgl_surface));
    surf->width = SGL_FB_WIDTH;
    surf->height = SGL_FB_HEIGHT;

    /* Create framebuffer layout */
    DkImageLayoutMaker imageLayoutMaker;
    dkImageLayoutMakerDefaults(&imageLayoutMaker, display->device);
    imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                             DkImageFlags_Usage2DEngine | DkImageFlags_HwCompression;
    imageLayoutMaker.format = DkImageFormat_RGBA8_Unorm;
    imageLayoutMaker.dimensions[0] = surf->width;
    imageLayoutMaker.dimensions[1] = surf->height;

    DkImageLayout fbLayout;
    dkImageLayoutInitialize(&fbLayout, &imageLayoutMaker);

    uint32_t fbSize = dkImageLayoutGetSize(&fbLayout);
    uint32_t fbAlign = dkImageLayoutGetAlignment(&fbLayout);
    fbSize = (fbSize + fbAlign - 1) & ~(fbAlign - 1);

    /* Create framebuffer memory block */
    DkMemBlockMaker memBlockMaker;
    dkMemBlockMakerDefaults(&memBlockMaker, display->device, SGL_FB_NUM * fbSize);
    memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    surf->framebuffer_memblock = dkMemBlockCreate(&memBlockMaker);

    if (!surf->framebuffer_memblock) {
        sgl_egl_set_error(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    /* Initialize framebuffer images */
    DkImage const *swapchainImages[SGL_FB_NUM];
    for (int i = 0; i < SGL_FB_NUM; i++) {
        swapchainImages[i] = &surf->framebuffers[i];
        dkImageInitialize(&surf->framebuffers[i], &fbLayout, surf->framebuffer_memblock,
                          i * fbSize);
    }

    /* Create depth buffers if needed - ONE PER FRAMEBUFFER SLOT for proper sync */
    if (cfg->depth_size > 0) {
        DkImageLayoutMaker depthLayoutMaker;
        dkImageLayoutMakerDefaults(&depthLayoutMaker, display->device);
        depthLayoutMaker.flags =
            DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine | DkImageFlags_HwCompression;
        depthLayoutMaker.format = DkImageFormat_Z24S8;
        depthLayoutMaker.dimensions[0] = surf->width;
        depthLayoutMaker.dimensions[1] = surf->height;

        DkImageLayout depthLayout;
        dkImageLayoutInitialize(&depthLayout, &depthLayoutMaker);

        uint32_t depthSize = dkImageLayoutGetSize(&depthLayout);
        uint32_t depthAlign = dkImageLayoutGetAlignment(&depthLayout);
        depthSize = (depthSize + depthAlign - 1) & ~(depthAlign - 1);

        /* Create one depth buffer per framebuffer slot */
        for (int i = 0; i < SGL_FB_NUM; i++) {
            dkMemBlockMakerDefaults(&memBlockMaker, display->device, depthSize);
            memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
            surf->depthbuffer_memblocks[i] = dkMemBlockCreate(&memBlockMaker);

            if (surf->depthbuffer_memblocks[i]) {
                dkImageInitialize(&surf->depthbuffers[i], &depthLayout,
                                  surf->depthbuffer_memblocks[i], 0);
            } else {
                /* Cleanup previously allocated depth buffers and framebuffer */
                for (int j = 0; j < i; j++) {
                    if (surf->depthbuffer_memblocks[j])
                        dkMemBlockDestroy(surf->depthbuffer_memblocks[j]);
                }
                dkMemBlockDestroy(surf->framebuffer_memblock);
                memset(surf, 0, sizeof(sgl_surface));
                sgl_egl_set_error(EGL_BAD_ALLOC);
                return EGL_NO_SURFACE;
            }
        }
    }

    /* Create swapchain. Retry up to 3 times — NWindow may not be ready
     * immediately after a previous app was killed via nxlink. */
    NWindow *nwin = win ? (NWindow *)win : nwindowGetDefault();

    DkSwapchainMaker swapchainMaker;
    dkSwapchainMakerDefaults(&swapchainMaker, display->device, nwin, swapchainImages, SGL_FB_NUM);

    surf->swapchain = NULL;
    for (int attempt = 0; attempt < 3; attempt++) {
        surf->swapchain = dkSwapchainCreate(&swapchainMaker);
        if (surf->swapchain)
            break;
        svcSleepThread(200000000ULL); /* 200ms */
    }

    if (!surf->swapchain) {
        dkMemBlockDestroy(surf->framebuffer_memblock);
        for (int i = 0; i < SGL_FB_NUM; i++) {
            if (surf->depthbuffer_memblocks[i])
                dkMemBlockDestroy(surf->depthbuffer_memblocks[i]);
        }
        memset(surf, 0, sizeof(sgl_surface));
        sgl_egl_set_error(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    surf->used = true;
    surf->current_slot = -1;
    surf->need_acquire = true;
    surf->config_id = cfg->config_id;

    return (EGLSurface)surf;
}
EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    SGL_EGL_VTRACE("eglDestroySurface(%p, %p)", dpy, surface);
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

    /* If the surface is current (bound as the draw or read surface of the
     * current context), defer destruction: invalidate the handle now but keep
     * the resources until it is no longer current (EGL §3.5.4). */
    sgl_context_t *cur = sgl_get_current_context();
    if (cur && (cur->draw_surface == surf || cur->read_surface == surf)) {
        surf->delete_pending = true;
        return EGL_TRUE;
    }

    sgl_egl_destroy_surface_now(surf);
    return EGL_TRUE;
}
EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                                              EGLint *value) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_surface *surf = (sgl_surface *)surface;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!surf || !surf->used || !value) {
        sgl_egl_set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    switch (attribute) {
        case EGL_WIDTH:
            *value = surf->width;
            break;
        case EGL_HEIGHT:
            *value = surf->height;
            break;
        case EGL_CONFIG_ID:
            *value = surf->config_id;
            break;
        case EGL_LARGEST_PBUFFER:
            *value = EGL_FALSE;
            break;
        case EGL_RENDER_BUFFER:
            *value = EGL_BACK_BUFFER;
            break;
        case EGL_SWAP_BEHAVIOR:
            *value = EGL_BUFFER_DESTROYED;
            break;
        default:
            sgl_egl_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }

    return EGL_TRUE;
}
