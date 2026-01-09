/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Resource Manager - manages GL object pools
 */

#ifndef SGL_RESOURCE_MANAGER_H
#define SGL_RESOURCE_MANAGER_H

#include "sgl_gl_types.h"

typedef struct sgl_resource_manager {
    sgl_buffer_t buffers[SGL_MAX_BUFFERS];
    sgl_shader_t shaders[SGL_MAX_SHADERS];
    sgl_program_t programs[SGL_MAX_PROGRAMS];
    sgl_texture_t textures[SGL_MAX_TEXTURES];
    sgl_framebuffer_t framebuffers[SGL_MAX_FRAMEBUFFERS];
    sgl_renderbuffer_t renderbuffers[SGL_MAX_RENDERBUFFERS];
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

/* Framebuffer operations */
GLuint sgl_res_mgr_alloc_framebuffer(sgl_resource_manager_t *mgr);
void sgl_res_mgr_free_framebuffer(sgl_resource_manager_t *mgr, GLuint id);
sgl_framebuffer_t *sgl_res_mgr_get_framebuffer(sgl_resource_manager_t *mgr, GLuint id);

/* Renderbuffer operations */
GLuint sgl_res_mgr_alloc_renderbuffer(sgl_resource_manager_t *mgr);
void sgl_res_mgr_free_renderbuffer(sgl_resource_manager_t *mgr, GLuint id);
sgl_renderbuffer_t *sgl_res_mgr_get_renderbuffer(sgl_resource_manager_t *mgr, GLuint id);

#endif /* SGL_RESOURCE_MANAGER_H */
