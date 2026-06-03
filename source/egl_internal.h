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
    EGLint config_id;   /* config the surface was created with (for EGL_CONFIG_ID) */

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

    /* eglDestroySurface called while the surface was current: the handle is
     * invalid but the resources live until it is no longer current (EGL §3.5.4). */
    bool delete_pending;
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

/* Verbose trace macro for debugging dEQP crashes (set to 0 to disable) */
#include <stdio.h>
#define SGL_EGL_VERBOSE 0
#if SGL_EGL_VERBOSE
#define SGL_EGL_VTRACE(fmt, ...) do { printf("[SGL] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while(0)
#else
#define SGL_EGL_VTRACE(fmt, ...) do {} while(0)
#endif

/* EGL internal helpers */
void sgl_egl_set_error(EGLint error);

/* True if display is our one valid, initialized display. The && short-circuits
 * before dereferencing a foreign pointer. Shared by all EGL translation units. */
static inline bool sgl_egl_display_valid(const sgl_display *display) {
    return display == &g_sgl.display && display->initialized;
}

/* Ensure frame is ready for rendering */
void sgl_ensure_frame_ready(void);

/* Shared EGL helpers (defined in egl_impl.c, used by egl_surface.c/egl_context.c) */
sgl_config *sgl_egl_get_config(EGLConfig config);
void sgl_egl_destroy_surface_now(sgl_surface *surf);
void sgl_egl_destroy_context_now(sgl_context_t *ctx);

#endif /* EGL_INTERNAL_H */
