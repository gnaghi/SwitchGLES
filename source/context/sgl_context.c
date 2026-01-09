/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Context Implementation
 */

#include "sgl_context.h"
#include "../util/sgl_log.h"
#include <string.h>

/* Global current context (single-threaded on Switch) */
static sgl_context_t *g_current_context = NULL;

void sgl_context_init(sgl_context_t *ctx) {
    memset(ctx, 0, sizeof(sgl_context_t));

    /* Initialize state classes */
    sgl_state_blend_init(&ctx->blend_state);
    sgl_state_depth_init(&ctx->depth_state);
    sgl_state_raster_init(&ctx->raster_state);
    sgl_state_viewport_init(&ctx->viewport_state, SGL_FB_WIDTH, SGL_FB_HEIGHT);
    sgl_state_color_init(&ctx->color_state);

    /* Initialize resource manager */
    sgl_res_mgr_init(&ctx->res_mgr);

    /* Clear bindings */
    ctx->current_program = 0;
    ctx->bound_array_buffer = 0;
    ctx->bound_element_buffer = 0;
    ctx->active_texture_unit = 0;
    ctx->bound_framebuffer = 0;
    ctx->bound_renderbuffer = 0;

    for (int i = 0; i < SGL_MAX_TEXTURE_UNITS; i++) {
        ctx->bound_textures[i] = 0;
    }

    /* Clear vertex attributes */
    for (int i = 0; i < SGL_MAX_ATTRIBS; i++) {
        ctx->vertex_attribs[i].enabled = false;
        ctx->vertex_attribs[i].size = 4;
        ctx->vertex_attribs[i].type = GL_FLOAT;
        ctx->vertex_attribs[i].normalized = GL_FALSE;
        ctx->vertex_attribs[i].stride = 0;
        ctx->vertex_attribs[i].pointer = NULL;
        ctx->vertex_attribs[i].buffer = 0;
    }

    ctx->error = GL_NO_ERROR;
    ctx->initialized = true;

    SGL_TRACE_CORE("Context initialized");
}

void sgl_context_destroy(sgl_context_t *ctx) {
    if (!ctx) return;

    /* Backend will be destroyed separately */
    ctx->backend = NULL;

    /* Clear everything */
    memset(ctx, 0, sizeof(sgl_context_t));

    SGL_TRACE_CORE("Context destroyed");
}

sgl_context_t *sgl_get_current_context(void) {
    return g_current_context;
}

void sgl_set_current_context(sgl_context_t *ctx) {
    g_current_context = ctx;
}

void sgl_set_error(sgl_context_t *ctx, GLenum error) {
    if (ctx && ctx->error == GL_NO_ERROR) {
        ctx->error = error;
    }
}

GLenum sgl_get_error(sgl_context_t *ctx) {
    if (!ctx) return GL_NO_ERROR;

    GLenum error = ctx->error;
    ctx->error = GL_NO_ERROR;
    return error;
}

void sgl_context_init_state(sgl_context_t *ctx) {
    if (!ctx) return;

    /* Re-initialize all state to GL defaults */
    sgl_state_blend_init(&ctx->blend_state);
    sgl_state_depth_init(&ctx->depth_state);
    sgl_state_raster_init(&ctx->raster_state);
    sgl_state_viewport_init(&ctx->viewport_state, SGL_FB_WIDTH, SGL_FB_HEIGHT);
    sgl_state_color_init(&ctx->color_state);
}
