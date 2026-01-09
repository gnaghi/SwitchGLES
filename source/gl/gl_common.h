/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Common includes and macros
 *
 * IMPORTANT: This layer must NOT include any deko3d headers!
 * All GPU operations go through ctx->backend->ops->xxx()
 */

#ifndef GL_COMMON_H
#define GL_COMMON_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "../context/sgl_context.h"
#include "../backend/sgl_backend.h"
#include "../util/sgl_log.h"

/* Get current context with error check */
#define GET_CTX() \
    sgl_context_t *ctx = sgl_get_current_context(); \
    if (!ctx) { return; }

#define GET_CTX_RET(ret) \
    sgl_context_t *ctx = sgl_get_current_context(); \
    if (!ctx) { return (ret); }

/* Check backend is available */
#define CHECK_BACKEND() \
    if (!ctx->backend || !ctx->backend->ops) { return; }

#define CHECK_BACKEND_RET(ret) \
    if (!ctx->backend || !ctx->backend->ops) { return (ret); }

/* Resource access macros */
#define GET_BUFFER(id) sgl_res_mgr_get_buffer(&ctx->res_mgr, id)
#define GET_TEXTURE(id) sgl_res_mgr_get_texture(&ctx->res_mgr, id)
#define GET_TEXTURE_ANY(id) sgl_res_mgr_get_texture_any(&ctx->res_mgr, id)
#define GET_SHADER(id) sgl_res_mgr_get_shader(&ctx->res_mgr, id)
#define GET_PROGRAM(id) sgl_res_mgr_get_program(&ctx->res_mgr, id)
#define GET_FRAMEBUFFER(id) sgl_res_mgr_get_framebuffer(&ctx->res_mgr, id)
#define GET_RENDERBUFFER(id) sgl_res_mgr_get_renderbuffer(&ctx->res_mgr, id)
#define GET_RENDERBUFFER_ANY(id) sgl_res_mgr_get_renderbuffer_any(&ctx->res_mgr, id)

/* Trace macros are already defined in sgl_log.h */

/* Get bound texture for a given target (resolves 2D vs cubemap binding) */
static inline GLuint sgl_get_bound_texture(sgl_context_t *ctx, GLenum target) {
    if (target == GL_TEXTURE_CUBE_MAP ||
        (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
         target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
        GLuint cube = ctx->bound_cubemap_textures[ctx->active_texture_unit];
        /* Fallback: some tests bind GL_TEXTURE_2D but upload cubemap faces.
         * If no cubemap is bound, try the 2D binding. */
        if (cube == 0) cube = ctx->bound_textures[ctx->active_texture_unit];
        return cube;
    }
    return ctx->bound_textures[ctx->active_texture_unit];
}

/* Ensure frame is ready for rendering */
extern void sgl_ensure_frame_ready(void);

/* Bind program and uniforms before drawing (calls backend) */
bool sgl_bind_program_for_draw(sgl_context_t *ctx, GLuint program_id);

/* Check if a dimension is a power of two */
static inline bool sgl_is_pot(GLsizei n) {
    return n > 0 && (n & (n - 1)) == 0;
}

#endif /* GL_COMMON_H */
