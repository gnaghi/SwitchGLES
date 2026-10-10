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
#include "../context/sgl_state_build.h"
#include "../backend/sgl_backend.h"
#include "../util/sgl_log.h"

/* Check backend is available */
#define CHECK_BACKEND()                                                                            \
    if (!ctx->backend || !ctx->backend->ops) {                                                     \
        return;                                                                                    \
    }

#define CHECK_BACKEND_RET(ret)                                                                     \
    if (!ctx->backend || !ctx->backend->ops) {                                                     \
        return (ret);                                                                              \
    }

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
        (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
        GLuint cube = ctx->bound_cubemap_textures[ctx->active_texture_unit];
        /* Fallback: some tests bind GL_TEXTURE_2D but upload cubemap faces.
         * If no cubemap is bound, try the 2D binding. */
        if (cube == 0)
            cube = ctx->bound_textures[ctx->active_texture_unit];
        return cube;
    }
    return ctx->bound_textures[ctx->active_texture_unit];
}

/* Ensure frame is ready for rendering */
extern void sgl_ensure_frame_ready(void);

/* Bind program and uniforms before drawing (calls backend) */
bool sgl_bind_program_for_draw(sgl_context_t *ctx, GLuint program_id);

/* Location -> info memo used by glUniform* (gl_uniform.c). Rebuild after
 * (re)link, once program_uniforms/active_uniforms/samplers/mirrors are final;
 * invalidate whenever one of those tables changes afterwards. */
void sgl_uniform_cache_rebuild(sgl_program_t *prog);
void sgl_uniform_cache_invalidate(sgl_program_t *prog);

/* GLES 3.0 state that is only supported at its default value (gl_es3_defaults.c).
 * SGL_ES3_UNSUPPORTED sets GL_INVALID_OPERATION for a valid value that needs a
 * feature not implemented yet, and logs it once per call site. The hooks
 * below return false (nothing done) in a GLES 2.0 context or for state that is
 * not theirs, so the GLES 2.0 code that calls them runs unchanged. */
void sgl_es3_unsupported(sgl_context_t *ctx, const char *what, bool *logged);
#define SGL_ES3_UNSUPPORTED(ctx, what)                                                             \
    do {                                                                                           \
        static bool s_logged;                                                                      \
        sgl_es3_unsupported(ctx, what, &s_logged);                                                 \
    } while (0)
bool sgl_es3_enable_cap(sgl_context_t *ctx, GLenum cap, bool enable);
bool sgl_es3_is_enabled_cap(sgl_context_t *ctx, GLenum cap);
bool sgl_es3_bind_texture(sgl_context_t *ctx, GLenum target, GLuint texture);
bool sgl_es3_tex_parameter(sgl_context_t *ctx, GLenum target, GLenum pname, GLfloat value);
/* 1: *value set, -1: GL error set, 0: not a GLES 3.0 query (GLES 2.0 path) */
int sgl_es3_get_tex_parameter(sgl_context_t *ctx, GLenum target, GLenum pname, GLfloat *value);
bool sgl_es3_pixel_store(sgl_context_t *ctx, GLenum pname, GLint param);
bool sgl_es3_get_integer(sgl_context_t *ctx, GLenum pname, GLint *params);

/* Binding point of a buffer target: GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER,
 * plus the GLES 3.0 targets in a GLES 3.0 context. NULL for any other target
 * (GL_INVALID_ENUM). gl_buffer.c */
GLuint *sgl_buffer_binding(sgl_context_t *ctx, GLenum target);

/* Check if a dimension is a power of two */
static inline bool sgl_is_pot(GLsizei n) {
    return n > 0 && (n & (n - 1)) == 0;
}

#endif /* GL_COMMON_H */
