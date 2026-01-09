/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Shared GL Types
 */

#ifndef SGL_GL_TYPES_H
#define SGL_GL_TYPES_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdint.h>

/* Configuration */
#define SGL_FB_NUM              3       /* Triple buffering */
#define SGL_FB_WIDTH            1280
#define SGL_FB_HEIGHT           720

#define SGL_CODE_MEM_SIZE       (64 * 1024)
#define SGL_CMD_MEM_SIZE        (1 * 1024 * 1024)  /* 1MB - reset each frame via wait_fence */
#define SGL_DATA_MEM_SIZE       (16 * 1024 * 1024)
#define SGL_UNIFORM_BUF_SIZE    (64 * 1024)
#define SGL_UNIFORM_ALIGNMENT   0x100   /* DK_UNIFORM_BUF_ALIGNMENT */
#define SGL_TEXTURE_MEM_SIZE    (32 * 1024 * 1024)
#define SGL_DESCRIPTOR_MEM_SIZE (4 * 1024)

/* Maximum resources */
#define SGL_MAX_SURFACES        4
#define SGL_MAX_CONTEXTS        4
#define SGL_MAX_BUFFERS         64
#define SGL_MAX_SHADERS         32
#define SGL_MAX_PROGRAMS        16
#define SGL_MAX_TEXTURES        32
#define SGL_MAX_FRAMEBUFFERS    8
#define SGL_MAX_RENDERBUFFERS   8
#define SGL_MAX_ATTRIBS         8
#define SGL_MAX_UNIFORMS        8
#define SGL_MAX_TEXTURE_UNITS   8

/* Buffer object */
typedef struct sgl_buffer {
    bool used;
    GLenum target;
    GLsizeiptr size;
    GLenum usage;
    uint32_t backend_handle;
    uint32_t data_offset;
} sgl_buffer_t;

/* Shader object */
typedef struct sgl_shader {
    bool used;
    GLenum type;
    bool compiled;
    uint32_t backend_handle;
    uint32_t code_offset;
    uint32_t code_size;
} sgl_shader_t;

/* Uniform binding info */
typedef struct sgl_uniform_binding {
    bool valid;
    uint32_t offset;
    uint32_t size;        /* Aligned size (256 bytes for deko3d) */
    uint32_t data_size;   /* Actual data size (e.g., 64 for mat4) */
    bool dirty;
} sgl_uniform_binding_t;

/* Program object */
typedef struct sgl_program {
    bool used;
    bool linked;
    bool shaders_initialized;
    GLuint vertex_shader;
    GLuint fragment_shader;
    uint32_t backend_handle;
    sgl_uniform_binding_t vertex_uniforms[SGL_MAX_UNIFORMS];
    sgl_uniform_binding_t fragment_uniforms[SGL_MAX_UNIFORMS];
} sgl_program_t;

/* Texture object */
typedef struct sgl_texture {
    bool used;
    GLenum target;
    GLsizei width, height;
    GLenum internal_format;
    uint32_t backend_handle;
    /* Sampler parameters */
    GLenum min_filter;
    GLenum mag_filter;
    GLenum wrap_s;
    GLenum wrap_t;
} sgl_texture_t;

/* Framebuffer object */
typedef struct sgl_framebuffer {
    bool used;
    GLuint color_attachment;    /* Texture ID */
    GLuint depth_attachment;    /* Renderbuffer ID or 0 */
    GLuint stencil_attachment;  /* Renderbuffer ID or 0 */
    uint32_t backend_handle;
    bool is_complete;
} sgl_framebuffer_t;

/* Renderbuffer object */
typedef struct sgl_renderbuffer {
    bool used;
    GLenum internal_format;
    GLsizei width, height;
    uint32_t backend_handle;
} sgl_renderbuffer_t;

/* Vertex attribute state */
typedef struct sgl_vertex_attrib {
    bool enabled;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    const void *pointer;
    GLuint buffer;  /* Bound VBO or 0 for client array */
    uint32_t buffer_offset;  /* GPU buffer offset (computed before draw) */
} sgl_vertex_attrib_t;

#endif /* SGL_GL_TYPES_H */
