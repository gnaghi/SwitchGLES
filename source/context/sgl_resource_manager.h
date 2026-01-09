/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Resource Manager - manages GL object pools
 */

#ifndef SGL_RESOURCE_MANAGER_H
#define SGL_RESOURCE_MANAGER_H

#include "sgl_gl_types.h"

/* GLES spec: shaders and programs share a single name namespace.
 * gl_name_type[] tracks what each ID was allocated as, so
 * GET_SHADER(program_id) correctly returns NULL and vice versa. */
#define SGL_NAME_FREE    0
#define SGL_NAME_SHADER  1
#define SGL_NAME_PROGRAM 2

/* Overflow tracking for IDs outside normal array range.
 * GLES2 spec allows glBind{Buffer,Texture} with any non-zero name
 * (implicit creation). dEQP bind_no_gen uses random uint32 IDs. */
#define SGL_MAX_OVERFLOW_IDS 16

typedef struct sgl_resource_manager {
    uint8_t gl_name_type[SGL_MAX_SHADERS]; /* shared namespace: shader/program type per ID */
    sgl_buffer_t buffers[SGL_MAX_BUFFERS];
    sgl_shader_t shaders[SGL_MAX_SHADERS];
    sgl_program_t programs[SGL_MAX_PROGRAMS];
    sgl_texture_t textures[SGL_MAX_TEXTURES];
    sgl_framebuffer_t framebuffers[SGL_MAX_FRAMEBUFFERS];
    sgl_renderbuffer_t renderbuffers[SGL_MAX_RENDERBUFFERS];

    /* Overflow IDs: tracks objects with IDs >= SGL_MAX_* */
    GLuint overflow_buffer_ids[SGL_MAX_OVERFLOW_IDS];
    GLenum overflow_buffer_targets[SGL_MAX_OVERFLOW_IDS];
    int num_overflow_buffers;
    GLuint overflow_texture_ids[SGL_MAX_OVERFLOW_IDS];
    GLenum overflow_texture_targets[SGL_MAX_OVERFLOW_IDS];
    int num_overflow_textures;
    GLuint overflow_fbo_ids[SGL_MAX_OVERFLOW_IDS];
    int num_overflow_fbos;
    GLuint overflow_rbo_ids[SGL_MAX_OVERFLOW_IDS];
    int num_overflow_rbos;
} sgl_resource_manager_t;

/* Initialize resource manager */
void sgl_res_mgr_init(sgl_resource_manager_t *mgr);

/* Buffer operations */
GLuint sgl_res_mgr_alloc_buffer(sgl_resource_manager_t *mgr);
void sgl_res_mgr_free_buffer(sgl_resource_manager_t *mgr, GLuint id);
sgl_buffer_t *sgl_res_mgr_get_buffer(sgl_resource_manager_t *mgr, GLuint id);

/* Shader operations */
GLuint sgl_res_mgr_alloc_shader(sgl_resource_manager_t *mgr, GLenum type);
void sgl_res_mgr_free_shader(sgl_resource_manager_t *mgr, GLuint id);
sgl_shader_t *sgl_res_mgr_get_shader(sgl_resource_manager_t *mgr, GLuint id);

/* Program operations */
GLuint sgl_res_mgr_alloc_program(sgl_resource_manager_t *mgr);
void sgl_res_mgr_free_program(sgl_resource_manager_t *mgr, GLuint id);
sgl_program_t *sgl_res_mgr_get_program(sgl_resource_manager_t *mgr, GLuint id);

/* Texture operations */
GLuint sgl_res_mgr_alloc_texture(sgl_resource_manager_t *mgr);
void sgl_res_mgr_free_texture(sgl_resource_manager_t *mgr, GLuint id);
sgl_texture_t *sgl_res_mgr_get_texture(sgl_resource_manager_t *mgr, GLuint id);
sgl_texture_t *sgl_res_mgr_get_texture_any(sgl_resource_manager_t *mgr, GLuint id); /* includes delete_pending */

/* Framebuffer operations */
GLuint sgl_res_mgr_alloc_framebuffer(sgl_resource_manager_t *mgr);
void sgl_res_mgr_free_framebuffer(sgl_resource_manager_t *mgr, GLuint id);
sgl_framebuffer_t *sgl_res_mgr_get_framebuffer(sgl_resource_manager_t *mgr, GLuint id);

/* Renderbuffer operations */
GLuint sgl_res_mgr_alloc_renderbuffer(sgl_resource_manager_t *mgr);
void sgl_res_mgr_free_renderbuffer(sgl_resource_manager_t *mgr, GLuint id);
sgl_renderbuffer_t *sgl_res_mgr_get_renderbuffer(sgl_resource_manager_t *mgr, GLuint id);
sgl_renderbuffer_t *sgl_res_mgr_get_renderbuffer_any(sgl_resource_manager_t *mgr, GLuint id); /* includes delete_pending */

#endif /* SGL_RESOURCE_MANAGER_H */
