/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Internal header - EGL structures and deko3d integration
 *
 * This header bridges EGL with the new GLOVE-style architecture.
 */

#ifndef EGL_INTERNAL_H
#define EGL_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <switch.h>
#include <deko3d.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* Include new architecture headers */
#include "context/sgl_context.h"
#include "backend/deko3d/dk_backend.h"

/* Forward declarations for EGL types */
typedef struct sgl_display sgl_display;
typedef struct sgl_config sgl_config;

/* EGL Display - represents the deko3d device */
struct sgl_display {
    bool initialized;
    DkDevice device;
    EGLint major_version;
    EGLint minor_version;
};

/* EGL Config - describes a framebuffer configuration */
struct sgl_config {
    EGLint config_id;
    EGLint red_size;
    EGLint green_size;
    EGLint blue_size;
    EGLint alpha_size;
    EGLint depth_size;
    EGLint stencil_size;
    EGLint samples;
    EGLint surface_type;
    EGLint renderable_type;
};

/* EGL Surface - represents the swapchain and framebuffers */
typedef struct sgl_surface {
    bool used;
    EGLint width;
    EGLint height;

    /* deko3d swapchain */
    DkSwapchain swapchain;

    /* Framebuffer memory and images */
    DkMemBlock framebuffer_memblock;
    DkImage framebuffers[SGL_FB_NUM];

    /* Depth buffers - one per framebuffer slot for proper synchronization */
    DkMemBlock depthbuffer_memblocks[SGL_FB_NUM];
    DkImage depthbuffers[SGL_FB_NUM];

    /* Current framebuffer slot */
    int current_slot;

    /* Flag to defer acquire to next frame start */
    bool need_acquire;
} sgl_surface;

/* Global state */
typedef struct {
    /* EGL error */
    EGLint last_error;

    /* Current API */
    EGLenum current_api;

    /* Display (singleton for Switch) */
    sgl_display display;

    /* Surfaces pool */
    sgl_surface surfaces[SGL_MAX_SURFACES];

    /* Contexts pool - now using new sgl_context_t */
    sgl_context_t contexts[SGL_MAX_CONTEXTS];

    /* Backends pool - one per context */
    sgl_backend_t *backends[SGL_MAX_CONTEXTS];

    /* Current context */
    sgl_context_t *current_context;
    sgl_display *current_display;

    /* Predefined configs */
    sgl_config configs[2]; /* RGBA8, RGBA8+D24S8 */
    int num_configs;
} sgl_egl_state;

/* Global instance */
extern sgl_egl_state g_sgl;

/* EGL internal helpers */
void sgl_egl_set_error(EGLint error);

/* Ensure frame is ready for rendering */
void sgl_ensure_frame_ready(void);

#endif /* EGL_INTERNAL_H */
