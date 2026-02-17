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

#define SGL_CODE_MEM_SIZE       (4 * 1024 * 1024)   /* 4MB for precompiled shaders */
#define SGL_CMD_MEM_SIZE        (1 * 1024 * 1024)  /* 1MB - reset each frame via wait_fence */
#define SGL_DATA_MEM_SIZE       (16 * 1024 * 1024)
#define SGL_UNIFORM_BUF_SIZE    (256 * 1024)
#define SGL_UNIFORM_ALIGNMENT   0x100   /* DK_UNIFORM_BUF_ALIGNMENT */
#define SGL_TEXTURE_MEM_SIZE    (32 * 1024 * 1024)
#define SGL_DESCRIPTOR_MEM_SIZE (SGL_MAX_TEXTURES * 64)  /* 64 = sizeof(DkImageDescriptor) + sizeof(DkSamplerDescriptor) */

/* Maximum resources */
#define SGL_MAX_SURFACES        4
#define SGL_MAX_CONTEXTS        4
#define SGL_MAX_BUFFERS         256
#define SGL_MAX_SHADERS         1024
#define SGL_MAX_PROGRAMS        512
#define SGL_MAX_TEXTURES        256
#define SGL_MAX_FRAMEBUFFERS    8
#define SGL_MAX_RENDERBUFFERS   8
#define SGL_MAX_ATTRIBS         17
#define SGL_MAX_UNIFORMS        16
#define SGL_MAX_TEXTURE_UNITS   8

/* Packed UBO configuration */
#define SGL_MAX_PACKED_UBO_SIZE  8192  /* Max bytes per packed UBO (supports 128 bones) */
#define SGL_MAX_PACKED_UBOS      2     /* Per stage: 0=main, 1=bones */
#define SGL_ATTRIB_NAME_MAX      64    /* Max attribute name length */

/* Packed UBO shadow buffer (CPU-side, flushed to GPU at draw time) */
typedef struct sgl_packed_ubo {
    uint8_t data[SGL_MAX_PACKED_UBO_SIZE];  /* CPU shadow buffer */
    uint32_t size;       /* Total used size (set at registration time) */
    bool dirty;          /* Any uniform written since last bind? */
    bool valid;          /* Has been configured? */
} sgl_packed_ubo_t;

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
    char *source;       /* GLSL source (from glShaderSource), NULL if precompiled */
    char *info_log;     /* Compilation info/error log */
} sgl_shader_t;

/* Uniform binding info */
typedef struct sgl_uniform_binding {
    bool valid;
    uint32_t offset;
    uint32_t size;        /* Aligned size (256 bytes for deko3d) */
    uint32_t data_size;   /* Actual data size (e.g., 64 for mat4) */
    bool dirty;
    /* Shadow copy for glGetUniformfv/iv readback (max mat4 = 64 bytes) */
    uint8_t shadow[64];
    uint32_t shadow_size;
    uint32_t shadow_components; /* 1-4 for vec, 4/9/16 for mat */
    GLenum shadow_type;         /* GL_FLOAT or GL_INT */
} sgl_uniform_binding_t;

/* Attribute binding (from glBindAttribLocation or built-in defaults) */
typedef struct sgl_attrib_binding {
    char name[SGL_ATTRIB_NAME_MAX];
    GLuint index;
    bool used;
} sgl_attrib_binding_t;

/* Active uniform info (populated when glGetUniformLocation succeeds) */
typedef struct sgl_active_uniform_info {
    char name[SGL_ATTRIB_NAME_MAX];
    GLint location;
    GLenum type;        /* GL_FLOAT, GL_FLOAT_VEC2, ..., GL_FLOAT_MAT4, GL_INT, etc. */
    GLint size;         /* 1 for non-arrays */
    bool active;
} sgl_active_uniform_info_t;

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
    /* Packed UBO shadow buffers (per-stage, per-binding) */
    sgl_packed_ubo_t packed_vertex[SGL_MAX_PACKED_UBOS];
    sgl_packed_ubo_t packed_fragment[SGL_MAX_PACKED_UBOS];
    /* Attribute bindings (from glBindAttribLocation) */
    sgl_attrib_binding_t attrib_bindings[SGL_MAX_ATTRIBS];
    int num_attrib_bindings;
    /* Active uniform tracking (populated by glGetUniformLocation) */
    sgl_active_uniform_info_t active_uniforms[SGL_MAX_UNIFORMS * 2]; /* VS + FS */
    int num_active_uniforms;
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
    bool depth_is_renderbuffer;   /* true if depth_attachment is a renderbuffer */
    bool stencil_is_renderbuffer; /* true if stencil_attachment is a renderbuffer */
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
    GLfloat current_value[4]; /* Constant value when array is disabled (default: 0,0,0,1) */
} sgl_vertex_attrib_t;

#endif /* SGL_GL_TYPES_H */
