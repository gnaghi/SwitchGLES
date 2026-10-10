/*
 * SwitchGLES - EGL config queries (eglGetConfigs / eglChooseConfig /
 * eglGetConfigAttrib). Split out of egl_impl.c.
 */

#include "egl_internal.h"
#include "util/sgl_log.h"
#include <string.h>

/* ============================================================================
 * EGL Config Functions
 * ============================================================================ */

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size,
                                            EGLint *num_config) {
    sgl_display *display = (sgl_display *)dpy;

    if (!sgl_egl_display_valid(display)) {
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
    SGL_EGL_VTRACE("eglChooseConfig(%p)", dpy);
    sgl_display *display = (sgl_display *)dpy;

    if (!sgl_egl_display_valid(display)) {
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
                case EGL_RED_SIZE:
                    req_red = attrib_list[i + 1];
                    break;
                case EGL_GREEN_SIZE:
                    req_green = attrib_list[i + 1];
                    break;
                case EGL_BLUE_SIZE:
                    req_blue = attrib_list[i + 1];
                    break;
                case EGL_ALPHA_SIZE:
                    req_alpha = attrib_list[i + 1];
                    break;
                case EGL_DEPTH_SIZE:
                    req_depth = attrib_list[i + 1];
                    break;
                case EGL_STENCIL_SIZE:
                    req_stencil = attrib_list[i + 1];
                    break;
                case EGL_RENDERABLE_TYPE:
                    req_renderable = attrib_list[i + 1];
                    break;
                default:
                    break;
            }
        }
    }

    int match_count = 0;
    for (int i = 0; i < g_sgl.num_configs && (!configs || match_count < config_size); i++) {
        sgl_config *cfg = &g_sgl.configs[i];

        if (cfg->red_size < req_red)
            continue;
        if (cfg->green_size < req_green)
            continue;
        if (cfg->blue_size < req_blue)
            continue;
        if (cfg->alpha_size < req_alpha)
            continue;
        if (cfg->depth_size < req_depth)
            continue;
        if (cfg->stencil_size < req_stencil)
            continue;
        if (req_renderable && !(cfg->renderable_type & req_renderable))
            continue;

        if (configs) {
            configs[match_count] = (EGLConfig)cfg;
        }
        match_count++;
    }

    *num_config = match_count;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute,
                                                 EGLint *value) {
    sgl_display *display = (sgl_display *)dpy;
    sgl_config *cfg = (sgl_config *)config;

    if (!sgl_egl_display_valid(display)) {
        sgl_egl_set_error(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (!value) {
        sgl_egl_set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    switch (attribute) {
        case EGL_CONFIG_ID:
            *value = cfg->config_id;
            break;
        case EGL_RED_SIZE:
            *value = cfg->red_size;
            break;
        case EGL_GREEN_SIZE:
            *value = cfg->green_size;
            break;
        case EGL_BLUE_SIZE:
            *value = cfg->blue_size;
            break;
        case EGL_ALPHA_SIZE:
            *value = cfg->alpha_size;
            break;
        case EGL_DEPTH_SIZE:
            *value = cfg->depth_size;
            break;
        case EGL_STENCIL_SIZE:
            *value = cfg->stencil_size;
            break;
        case EGL_SAMPLES:
            *value = cfg->samples;
            break;
        case EGL_SAMPLE_BUFFERS:
            *value = (cfg->samples > 0) ? 1 : 0;
            break;
        case EGL_SURFACE_TYPE:
            *value = cfg->surface_type;
            break;
        case EGL_RENDERABLE_TYPE:
            *value = cfg->renderable_type;
            break;
        case EGL_BUFFER_SIZE:
            *value = cfg->red_size + cfg->green_size + cfg->blue_size + cfg->alpha_size;
            break;
        case EGL_COLOR_BUFFER_TYPE:
            *value = EGL_RGB_BUFFER;
            break;
        case EGL_CONFIG_CAVEAT:
            *value = EGL_NONE;
            break;
        case EGL_CONFORMANT:
            /* GLES 3.0 contexts (SGL_ENABLE_ES3_CONTEXT) are not conformant */
            *value = cfg->renderable_type & ~EGL_OPENGL_ES3_BIT;
            break;
        case EGL_LEVEL:
            *value = 0;
            break;
        case EGL_MAX_PBUFFER_HEIGHT:
            *value = SGL_FB_HEIGHT;
            break;
        case EGL_MAX_PBUFFER_WIDTH:
            *value = SGL_FB_WIDTH;
            break;
        case EGL_MAX_PBUFFER_PIXELS:
            *value = SGL_FB_WIDTH * SGL_FB_HEIGHT;
            break;
        case EGL_MIN_SWAP_INTERVAL:
            *value = 0;
            break;
        case EGL_MAX_SWAP_INTERVAL:
            *value = 4;
            break;
        case EGL_NATIVE_RENDERABLE:
            *value = EGL_FALSE;
            break;
        case EGL_NATIVE_VISUAL_ID:
            *value = 0;
            break;
        case EGL_NATIVE_VISUAL_TYPE:
            *value = EGL_NONE;
            break;
        case EGL_TRANSPARENT_TYPE:
            *value = EGL_NONE;
            break;
        default:
            sgl_egl_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }

    return EGL_TRUE;
}
