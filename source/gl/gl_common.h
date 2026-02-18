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
#define GET_SHADER(id) sgl_res_mgr_get_shader(&ctx->res_mgr, id)
#define GET_PROGRAM(id) sgl_res_mgr_get_program(&ctx->res_mgr, id)
#define GET_FRAMEBUFFER(id) sgl_res_mgr_get_framebuffer(&ctx->res_mgr, id)
#define GET_RENDERBUFFER(id) sgl_res_mgr_get_renderbuffer(&ctx->res_mgr, id)

/* Trace macros are already defined in sgl_log.h */

/* Ensure frame is ready for rendering */
extern void sgl_ensure_frame_ready(void);

/* Bind program and uniforms before drawing (calls backend) */
bool sgl_bind_program_for_draw(sgl_context_t *ctx, GLuint program_id);

#endif /* GL_COMMON_H */
