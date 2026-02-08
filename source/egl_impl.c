/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * EGL implementation using deko3d with new GLOVE architecture
 *
 * Reference: C:/devkitPro/examples/switch/graphics/deko3d/deko_basic/source/main.c
 */

#include "egl_internal.h"
#include <string.h>
#include <stdio.h>

/* Global state */
sgl_egl_state g_sgl = {0};

/* Helper to set EGL error */
void sgl_egl_set_error(EGLint error) {
    g_sgl.last_error = error;
}

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
    if (!dk) return;

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
            ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                 fbo->color_attachment, fbo->depth_attachment);
        }
    }

    /* Set viewport and scissor */
    DkViewport viewport = {
        (float)ctx->viewport_state.viewport_x,
        (float)ctx->viewport_state.viewport_y,
        (float)ctx->viewport_state.viewport_width,
        (float)ctx->viewport_state.viewport_height,
        ctx->viewport_state.depth_near, ctx->viewport_state.depth_far
    };
    DkScissor scissor = {
        (uint32_t)ctx->viewport_state.scissor_x,
        (uint32_t)ctx->viewport_state.scissor_y,
        (uint32_t)ctx->viewport_state.scissor_width,
        (uint32_t)ctx->viewport_state.scissor_height
    };
    dkCmdBufSetViewports(dk->cmdbuf, 0, &viewport, 1);
    dkCmdBufSetScissors(dk->cmdbuf, 0, &scissor, 1);

    /* Re-apply all GL state to the new command buffer.
     * This is CRITICAL because each frame uses a different cmdbuf slot,
     * and state bindings are recorded per-cmdbuf. Without this, only
     * the first frame would have correct state (e.g., depth test, culling). */

    /* Apply raster state (face culling) */
    if (ctx->backend->ops->apply_raster) {
        sgl_raster_state_t rs;
        rs.cull_enabled = ctx->raster_state.cull_enabled;
        rs.cull_mode = ctx->raster_state.cull_mode;
        rs.front_face = ctx->raster_state.front_face;
        ctx->backend->ops->apply_raster(ctx->backend, &rs);
    }

    /* Apply combined depth-stencil state (avoids overwrite issues) */
    if (ctx->backend->ops->apply_depth_stencil) {
        sgl_depth_stencil_state_t dss;
        dss.depth_test_enabled = ctx->depth_state.depth_test_enabled;
        dss.depth_write_enabled = ctx->depth_state.depth_write_enabled;
        dss.depth_func = ctx->depth_state.depth_func;
        dss.depth_clear_value = ctx->depth_state.clear_depth;
        dss.stencil_test_enabled = ctx->depth_state.stencil_test_enabled;
        dss.stencil_front.func = ctx->depth_state.front.func;
        dss.stencil_front.ref = ctx->depth_state.front.ref;
        dss.stencil_front.func_mask = ctx->depth_state.front.func_mask;
        dss.stencil_front.write_mask = ctx->depth_state.front.write_mask;
        dss.stencil_front.fail_op = ctx->depth_state.front.fail_op;
        dss.stencil_front.zfail_op = ctx->depth_state.front.zfail_op;
        dss.stencil_front.zpass_op = ctx->depth_state.front.zpass_op;
        dss.stencil_back.func = ctx->depth_state.back.func;
        dss.stencil_back.ref = ctx->depth_state.back.ref;
        dss.stencil_back.func_mask = ctx->depth_state.back.func_mask;
        dss.stencil_back.write_mask = ctx->depth_state.back.write_mask;
        dss.stencil_back.fail_op = ctx->depth_state.back.fail_op;
        dss.stencil_back.zfail_op = ctx->depth_state.back.zfail_op;
        dss.stencil_back.zpass_op = ctx->depth_state.back.zpass_op;
        dss.stencil_clear_value = ctx->depth_state.clear_stencil;
        ctx->backend->ops->apply_depth_stencil(ctx->backend, &dss);
    }

    /* Apply blend state */
    if (ctx->backend->ops->apply_blend) {
        sgl_blend_state_t bs;
        bs.enabled = ctx->blend_state.enabled;
        bs.src_rgb = ctx->blend_state.src_rgb;
        bs.dst_rgb = ctx->blend_state.dst_rgb;
        bs.src_alpha = ctx->blend_state.src_alpha;
        bs.dst_alpha = ctx->blend_state.dst_alpha;
        bs.equation_rgb = ctx->blend_state.equation_rgb;
        bs.equation_alpha = ctx->blend_state.equation_alpha;
        ctx->backend->ops->apply_blend(ctx->backend, &bs);
    }

    /* Apply color mask */
    if (ctx->backend->ops->apply_color_mask) {
        sgl_color_state_t cs;
        cs.mask[0] = ctx->color_state.mask[0];
        cs.mask[1] = ctx->color_state.mask[1];
        cs.mask[2] = ctx->color_state.mask[2];
        cs.mask[3] = ctx->color_state.mask[3];
        cs.clear_color[0] = ctx->color_state.clear_color[0];
        cs.clear_color[1] = ctx->color_state.clear_color[1];
        cs.clear_color[2] = ctx->color_state.clear_color[2];
        cs.clear_color[3] = ctx->color_state.clear_color[3];
        ctx->backend->ops->apply_color_mask(ctx->backend, &cs);
    }
}

/* ============================================================================
 * EGL 1.0 Core Functions
 * ============================================================================ */

EGLAPI EGLint EGLAPIENTRY eglGetError(void) {
    EGLint error = g_sgl.last_error;
    g_sgl.last_error = EGL_SUCCESS;
    return error;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id) {
    (void)display_id;
    return (EGLDisplay)&g_sgl.display;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (display->initialized) {
        if (major) *major = display->major_version;
        if (minor) *minor = display->minor_version;
        return EGL_TRUE;
    }

    /* Create deko3d device with OpenGL-compatible settings */
    DkDeviceMaker deviceMaker;
    dkDeviceMakerDefaults(&deviceMaker);
    /* Use OpenGL conventions:
     * - Depth range [-1, 1] instead of Vulkan [0, 1]
     * - Origin lower-left (Y=0 at bottom) instead of upper-left */
    deviceMaker.flags = DkDeviceFlags_DepthMinusOneToOne | DkDeviceFlags_OriginLowerLeft;
    display->device = dkDeviceCreate(&deviceMaker);

    if (!display->device) {
        sgl_egl_set_error(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    /* Initialize predefined configs */
    g_sgl.configs[0].config_id = 1;
    g_sgl.configs[0].red_size = 8;
    g_sgl.configs[0].green_size = 8;
    g_sgl.configs[0].blue_size = 8;
    g_sgl.configs[0].alpha_size = 8;
    g_sgl.configs[0].depth_size = 0;
    g_sgl.configs[0].stencil_size = 0;
    g_sgl.configs[0].samples = 0;
    g_sgl.configs[0].surface_type = EGL_WINDOW_BIT;
    g_sgl.configs[0].renderable_type = EGL_OPENGL_ES2_BIT;

    g_sgl.configs[1].config_id = 2;
    g_sgl.configs[1].red_size = 8;
    g_sgl.configs[1].green_size = 8;
    g_sgl.configs[1].blue_size = 8;
    g_sgl.configs[1].alpha_size = 8;
    g_sgl.configs[1].depth_size = 24;
    g_sgl.configs[1].stencil_size = 8;
    g_sgl.configs[1].samples = 0;
    g_sgl.configs[1].surface_type = EGL_WINDOW_BIT;
    g_sgl.configs[1].renderable_type = EGL_OPENGL_ES2_BIT;

    g_sgl.num_configs = 2;

    display->major_version = 1;
    display->minor_version = 4;
    display->initialized = true;

    if (major) *major = display->major_version;
    if (minor) *minor = display->minor_version;

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy) {
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!display->initialized) {
        return EGL_TRUE;
    }

    /* Destroy all contexts and backends */
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        if (g_sgl.contexts[i].used) {
            if (g_sgl.backends[i]) {
                dk_backend_data_t *dk = (dk_backend_data_t *)g_sgl.backends[i]->impl_data;
                if (dk && dk->queue) {
                    dkQueueWaitIdle(dk->queue);
                }
                dk_backend_destroy(g_sgl.backends[i]);
                g_sgl.backends[i] = NULL;
            }
            sgl_context_destroy(&g_sgl.contexts[i]);
        }
    }

    /* Destroy all surfaces */
    for (int i = 0; i < SGL_MAX_SURFACES; i++) {
        if (g_sgl.surfaces[i].used) {
            sgl_surface *surf = &g_sgl.surfaces[i];
            if (surf->swapchain) dkSwapchainDestroy(surf->swapchain);
            if (surf->framebuffer_memblock) dkMemBlockDestroy(surf->framebuffer_memblock);
            for (int j = 0; j < SGL_FB_NUM; j++) {
                if (surf->depthbuffer_memblocks[j]) dkMemBlockDestroy(surf->depthbuffer_memblocks[j]);
            }
            memset(surf, 0, sizeof(sgl_surface));
        }
    }

    if (display->device) {
        dkDeviceDestroy(display->device);
        display->device = NULL;
    }

    display->initialized = false;
    sgl_set_current_context(NULL);
    g_sgl.current_context = NULL;
    g_sgl.current_display = NULL;

    return EGL_TRUE;
}

EGLAPI const char * EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name) {
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return NULL;
    }

    switch (name) {
        case EGL_VENDOR:      return "SwitchGLES";
        case EGL_VERSION:     return "1.4 SwitchGLES";
        case EGL_EXTENSIONS:  return "";
        case EGL_CLIENT_APIS: return "OpenGL_ES";
        default:
            sgl_egl_set_error(EGL_BAD_PARAMETER);
            return NULL;
    }
}

/* ============================================================================
 * EGL Config Functions
 * ============================================================================ */

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                             EGLint config_size, EGLint *num_config) {
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!num_config) {
        sgl_egl_set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    if (!configs) {
        *num_config = g_sgl.num_configs;
        return EGL_TRUE;
    }

    int count = (config_size < g_sgl.num_configs) ? config_size : g_sgl.num_configs;
    for (int i = 0; i < count; i++) {
        configs[i] = (EGLConfig)&g_sgl.configs[i];
    }
    *num_config = count;

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                               EGLConfig *configs, EGLint config_size,
                                               EGLint *num_config) {
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!num_config) {
        sgl_egl_set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    EGLint req_red = 0, req_green = 0, req_blue = 0, req_alpha = 0;
    EGLint req_depth = 0, req_stencil = 0;
    EGLint req_renderable = 0;

    if (attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            switch (attrib_list[i]) {
                case EGL_RED_SIZE:      req_red = attrib_list[i+1]; break;
                case EGL_GREEN_SIZE:    req_green = attrib_list[i+1]; break;
                case EGL_BLUE_SIZE:     req_blue = attrib_list[i+1]; break;
                case EGL_ALPHA_SIZE:    req_alpha = attrib_list[i+1]; break;
                case EGL_DEPTH_SIZE:    req_depth = attrib_list[i+1]; break;
                case EGL_STENCIL_SIZE:  req_stencil = attrib_list[i+1]; break;
                case EGL_RENDERABLE_TYPE: req_renderable = attrib_list[i+1]; break;
                default: break;
            }
        }
    }

    int match_count = 0;
    for (int i = 0; i < g_sgl.num_configs && (!configs || match_count < config_size); i++) {
        sgl_config *cfg = &g_sgl.configs[i];

        if (cfg->red_size < req_red) continue;
        if (cfg->green_size < req_green) continue;
        if (cfg->blue_size < req_blue) continue;
        if (cfg->alpha_size < req_alpha) continue;
        if (cfg->depth_size < req_depth) continue;
        if (cfg->stencil_size < req_stencil) continue;
        if (req_renderable && !(cfg->renderable_type & req_renderable)) continue;

        if (configs) {
            configs[match_count] = (EGLConfig)cfg;
        }
        match_count++;
    }

    *num_config = match_count;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                  EGLint attribute, EGLint *value) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_config *cfg = (sgl_config *)config;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!value) {
        sgl_egl_set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    switch (attribute) {
        case EGL_CONFIG_ID:        *value = cfg->config_id; break;
        case EGL_RED_SIZE:         *value = cfg->red_size; break;
        case EGL_GREEN_SIZE:       *value = cfg->green_size; break;
        case EGL_BLUE_SIZE:        *value = cfg->blue_size; break;
        case EGL_ALPHA_SIZE:       *value = cfg->alpha_size; break;
        case EGL_DEPTH_SIZE:       *value = cfg->depth_size; break;
        case EGL_STENCIL_SIZE:     *value = cfg->stencil_size; break;
        case EGL_SAMPLES:          *value = cfg->samples; break;
        case EGL_SAMPLE_BUFFERS:   *value = (cfg->samples > 0) ? 1 : 0; break;
        case EGL_SURFACE_TYPE:     *value = cfg->surface_type; break;
        case EGL_RENDERABLE_TYPE:  *value = cfg->renderable_type; break;
        case EGL_BUFFER_SIZE:      *value = cfg->red_size + cfg->green_size + cfg->blue_size + cfg->alpha_size; break;
        case EGL_COLOR_BUFFER_TYPE: *value = EGL_RGB_BUFFER; break;
        case EGL_CONFIG_CAVEAT:    *value = EGL_NONE; break;
        case EGL_CONFORMANT:       *value = cfg->renderable_type; break;
        case EGL_LEVEL:            *value = 0; break;
        case EGL_MAX_PBUFFER_HEIGHT: *value = SGL_FB_HEIGHT; break;
        case EGL_MAX_PBUFFER_WIDTH:  *value = SGL_FB_WIDTH; break;
        case EGL_MAX_PBUFFER_PIXELS: *value = SGL_FB_WIDTH * SGL_FB_HEIGHT; break;
        case EGL_MIN_SWAP_INTERVAL:  *value = 0; break;
        case EGL_MAX_SWAP_INTERVAL:  *value = 4; break;
        case EGL_NATIVE_RENDERABLE:  *value = EGL_FALSE; break;
        case EGL_NATIVE_VISUAL_ID:   *value = 0; break;
        case EGL_NATIVE_VISUAL_TYPE: *value = EGL_NONE; break;
        case EGL_TRANSPARENT_TYPE:   *value = EGL_NONE; break;
        default:
            sgl_egl_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }

    return EGL_TRUE;
}

/* ============================================================================
 * EGL Surface Functions
 * ============================================================================ */

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                      EGLNativeWindowType win,
                                                      const EGLint *attrib_list) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_config *cfg = (sgl_config *)config;
    (void)attrib_list;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
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
    imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
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
        dkImageInitialize(&surf->framebuffers[i], &fbLayout, surf->framebuffer_memblock, i * fbSize);
    }

    /* Create depth buffers if needed - ONE PER FRAMEBUFFER SLOT for proper sync */
    if (cfg->depth_size > 0) {
        DkImageLayoutMaker depthLayoutMaker;
        dkImageLayoutMakerDefaults(&depthLayoutMaker, display->device);
        depthLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine | DkImageFlags_HwCompression;
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
                dkImageInitialize(&surf->depthbuffers[i], &depthLayout, surf->depthbuffer_memblocks[i], 0);
            }
        }
    }

    /* Create swapchain */
    NWindow *nwin = win ? (NWindow *)win : nwindowGetDefault();

    DkSwapchainMaker swapchainMaker;
    dkSwapchainMakerDefaults(&swapchainMaker, display->device, nwin, swapchainImages, SGL_FB_NUM);
    surf->swapchain = dkSwapchainCreate(&swapchainMaker);

    if (!surf->swapchain) {
        dkMemBlockDestroy(surf->framebuffer_memblock);
        for (int i = 0; i < SGL_FB_NUM; i++) {
            if (surf->depthbuffer_memblocks[i]) dkMemBlockDestroy(surf->depthbuffer_memblocks[i]);
        }
        memset(surf, 0, sizeof(sgl_surface));
        sgl_egl_set_error(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    surf->used = true;
    surf->current_slot = -1;

    return (EGLSurface)surf;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_surface *surf = (sgl_surface *)surface;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!surf || !surf->used) {
        sgl_egl_set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    /* Wait for GPU to finish */
    if (sgl_get_current_context() && sgl_get_current_context()->backend) {
        dk_backend_data_t *dk = (dk_backend_data_t *)sgl_get_current_context()->backend->impl_data;
        if (dk && dk->queue) {
            dkQueueWaitIdle(dk->queue);
        }
    }

    if (surf->swapchain) dkSwapchainDestroy(surf->swapchain);
    if (surf->framebuffer_memblock) dkMemBlockDestroy(surf->framebuffer_memblock);
    for (int i = 0; i < SGL_FB_NUM; i++) {
        if (surf->depthbuffer_memblocks[i]) dkMemBlockDestroy(surf->depthbuffer_memblocks[i]);
    }

    memset(surf, 0, sizeof(sgl_surface));
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                               EGLint attribute, EGLint *value) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_surface *surf = (sgl_surface *)surface;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!surf || !surf->used || !value) {
        sgl_egl_set_error(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    switch (attribute) {
        case EGL_WIDTH:  *value = surf->width; break;
        case EGL_HEIGHT: *value = surf->height; break;
        case EGL_CONFIG_ID: *value = 1; break;
        case EGL_LARGEST_PBUFFER: *value = EGL_FALSE; break;
        case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
        case EGL_SWAP_BEHAVIOR: *value = EGL_BUFFER_DESTROYED; break;
        default:
            sgl_egl_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }

    return EGL_TRUE;
}

/* ============================================================================
 * EGL Context Functions
 * ============================================================================ */

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                                EGLContext share_context,
                                                const EGLint *attrib_list) {
    sgl_display *display = (sgl_display *)dpy;
    (void)config;
    (void)share_context;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
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
    sgl_display *display = (sgl_display *)dpy;
    sgl_context_t *ctx = (sgl_context_t *)context;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!ctx || !ctx->used) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    if (sgl_get_current_context() == ctx) {
        sgl_set_current_context(NULL);
        g_sgl.current_context = NULL;
    }

    /* Find and destroy backend */
    for (int i = 0; i < SGL_MAX_CONTEXTS; i++) {
        if (&g_sgl.contexts[i] == ctx && g_sgl.backends[i]) {
            dk_backend_destroy(g_sgl.backends[i]);
            g_sgl.backends[i] = NULL;
            break;
        }
    }

    sgl_context_destroy(ctx);
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                              EGLSurface read, EGLContext context) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_surface *draw_surf = (sgl_surface *)draw;
    sgl_surface *read_surf = (sgl_surface *)read;
    sgl_context_t *ctx = (sgl_context_t *)context;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (context == EGL_NO_CONTEXT) {
        sgl_set_current_context(NULL);
        g_sgl.current_context = NULL;
        g_sgl.current_display = NULL;
        return EGL_TRUE;
    }

    if (!ctx || !ctx->used) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
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

    /* Store framebuffer info - per-slot depth buffers */
    if (draw_surf) {
        dk->framebuffers = draw_surf->framebuffers;
        for (int i = 0; i < SGL_FB_NUM; i++) {
            dk->depth_images[i] = draw_surf->depthbuffer_memblocks[i] ? &draw_surf->depthbuffers[i] : NULL;
        }
        dk->num_framebuffers = SGL_FB_NUM;
        dk->swapchain = draw_surf->swapchain;
        dk->fb_width = draw_surf->width;
        dk->fb_height = draw_surf->height;
    }

    /* Begin first frame */
    if (draw_surf && draw_surf->swapchain && dk->queue) {
        /* Acquire first framebuffer */
        int slot = dkQueueAcquireImage(dk->queue, draw_surf->swapchain);
        draw_surf->current_slot = slot;
        draw_surf->need_acquire = false;

        /* Begin frame */
        if (ctx->backend->ops->begin_frame) {
            ctx->backend->ops->begin_frame(ctx->backend, slot);
        }

        /* Bind render target */
        DkImageView colorView;
        dkImageViewDefaults(&colorView, &draw_surf->framebuffers[slot]);

        if (draw_surf->depthbuffer_memblocks[slot]) {
            DkImageView depthView;
            dkImageViewDefaults(&depthView, &draw_surf->depthbuffers[slot]);
            dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, &depthView);
        } else {
            dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, NULL);
        }
    }

    return EGL_TRUE;
}

/* ============================================================================
 * EGL Swap/Presentation Functions
 * ============================================================================ */

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_surface *surf = (sgl_surface *)surface;

    if (display != &g_sgl.display || !display->initialized) {
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

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    sgl_display *display = (sgl_display *)dpy;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->draw_surface) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

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
    if (!sgl_get_current_context()) return EGL_NO_SURFACE;
    if (readdraw == EGL_DRAW) return (EGLSurface)sgl_get_current_context()->draw_surface;
    if (readdraw == EGL_READ) return (EGLSurface)sgl_get_current_context()->read_surface;
    sgl_egl_set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

/* ============================================================================
 * EGL 1.2 Functions
 * ============================================================================ */

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api) {
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

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void) { return eglWaitClient(); }
EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine) { (void)engine; return EGL_TRUE; }

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target) {
    (void)dpy; (void)surface; (void)target;
    sgl_egl_set_error(EGL_BAD_NATIVE_PIXMAP);
    return EGL_FALSE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list) {
    (void)dpy; (void)config; (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_MATCH);
    return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list) {
    (void)dpy; (void)config; (void)pixmap; (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_NATIVE_PIXMAP);
    return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_context_t *context = (sgl_context_t *)ctx;

    if (display != &g_sgl.display || !display->initialized) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!context || !context->used || !value) {
        sgl_egl_set_error(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }

    switch (attribute) {
        case EGL_CONFIG_ID: *value = 1; break;
        case EGL_CONTEXT_CLIENT_TYPE: *value = EGL_OPENGL_ES_API; break;
        case EGL_CONTEXT_CLIENT_VERSION: *value = context->client_version; break;
        case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
        default:
            sgl_egl_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value) {
    (void)dpy; (void)surface; (void)attribute; (void)value;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    (void)dpy; (void)surface; (void)buffer;
    sgl_egl_set_error(EGL_BAD_SURFACE);
    return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    (void)dpy; (void)surface; (void)buffer;
    sgl_egl_set_error(EGL_BAD_SURFACE);
    return EGL_FALSE;
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *procname) {
    (void)procname;
    return NULL;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list) {
    (void)dpy; (void)buftype; (void)buffer; (void)config; (void)attrib_list;
    sgl_egl_set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}
