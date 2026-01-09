/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Abstract Backend Interface
 */

#ifndef SGL_BACKEND_H
#define SGL_BACKEND_H

#include "sgl_backend_types.h"
#include <stddef.h>

/* Forward declarations */
typedef struct sgl_backend_ops sgl_backend_ops_t;
typedef struct sgl_uniform_binding sgl_uniform_binding_t;
typedef struct sgl_vertex_attrib sgl_vertex_attrib_t;

/* Backend interface */
struct sgl_backend_ops {
    /* ======== Lifecycle ======== */
    int  (*init)(sgl_backend_t *be, void *device);
    void (*shutdown)(sgl_backend_t *be);

    /* ======== Frame Management ======== */
    void (*begin_frame)(sgl_backend_t *be, int slot);
    void (*end_frame)(sgl_backend_t *be, int slot);
    void (*present)(sgl_backend_t *be, int slot);
    int  (*acquire_image)(sgl_backend_t *be);
    void (*wait_fence)(sgl_backend_t *be, int slot);

    /* ======== State Application ======== */
    void (*apply_viewport)(sgl_backend_t *be, const sgl_viewport_state_t *state);
    void (*apply_scissor)(sgl_backend_t *be, const sgl_scissor_state_t *state);
    void (*apply_blend)(sgl_backend_t *be, const sgl_blend_state_t *state);
    void (*apply_depth)(sgl_backend_t *be, const sgl_depth_state_t *state);
    void (*apply_stencil)(sgl_backend_t *be, const sgl_stencil_state_t *state);
    /* Combined depth-stencil state (preferred - avoids state overwrite issues) */
    void (*apply_depth_stencil)(sgl_backend_t *be, const sgl_depth_stencil_state_t *state);
    void (*apply_raster)(sgl_backend_t *be, const sgl_raster_state_t *state);
    void (*apply_color_mask)(sgl_backend_t *be, const sgl_color_state_t *state);

    /* ======== Clear Operations ======== */
    void (*clear)(sgl_backend_t *be, GLbitfield mask,
                  const float *color, float depth, int stencil);

    /* ======== Buffer Operations ======== */
    sgl_handle_t (*create_buffer)(sgl_backend_t *be);
    void (*delete_buffer)(sgl_backend_t *be, sgl_handle_t handle);
    /* Returns allocated GPU buffer offset, or 0 on failure */
    uint32_t (*buffer_data)(sgl_backend_t *be, sgl_handle_t handle,
                            GLenum target, GLsizeiptr size, const void *data, GLenum usage);
    void (*buffer_sub_data)(sgl_backend_t *be, sgl_handle_t handle,
                            uint32_t buffer_offset, GLsizeiptr size, const void *data);

    /* ======== Texture Operations ======== */
    sgl_handle_t (*create_texture)(sgl_backend_t *be);
    void (*delete_texture)(sgl_backend_t *be, sgl_handle_t handle);
    void (*texture_image_2d)(sgl_backend_t *be, sgl_handle_t handle,
                             GLenum target, GLint level, GLint internalformat,
                             GLsizei width, GLsizei height, GLint border,
                             GLenum format, GLenum type, const void *pixels);
    void (*texture_sub_image_2d)(sgl_backend_t *be, sgl_handle_t handle,
                                 GLenum target, GLint level,
                                 GLint xoffset, GLint yoffset,
                                 GLsizei width, GLsizei height,
                                 GLenum format, GLenum type, const void *pixels);
    void (*texture_parameter)(sgl_backend_t *be, sgl_handle_t handle,
                              GLenum target, GLenum pname, GLint param);
    void (*bind_texture)(sgl_backend_t *be, GLuint unit, sgl_handle_t handle);
    void (*generate_mipmap)(sgl_backend_t *be, sgl_handle_t handle);
    void (*copy_tex_image_2d)(sgl_backend_t *be, sgl_handle_t handle,
                              GLenum target, GLint level, GLenum internalformat,
                              GLint x, GLint y, GLsizei width, GLsizei height);
    void (*copy_tex_sub_image_2d)(sgl_backend_t *be, sgl_handle_t handle,
                                   GLenum target, GLint level,
                                   GLint xoffset, GLint yoffset,
                                   GLint x, GLint y, GLsizei width, GLsizei height);

    /* Compressed texture operations */
    void (*compressed_texture_image_2d)(sgl_backend_t *be, sgl_handle_t handle,
                                         GLenum target, GLint level, GLenum internalformat,
                                         GLsizei width, GLsizei height,
                                         GLsizei imageSize, const void *data);
    void (*compressed_texture_sub_image_2d)(sgl_backend_t *be, sgl_handle_t handle,
                                             GLenum target, GLint level,
                                             GLint xoffset, GLint yoffset,
                                             GLsizei width, GLsizei height,
                                             GLenum format, GLsizei imageSize, const void *data);

    /* ======== Shader Operations ======== */
    sgl_handle_t (*create_shader)(sgl_backend_t *be, GLenum type);
    void (*delete_shader)(sgl_backend_t *be, sgl_handle_t handle);
    bool (*load_shader_binary)(sgl_backend_t *be, sgl_handle_t handle,
                               const void *data, size_t size);
    bool (*load_shader_file)(sgl_backend_t *be, sgl_handle_t handle,
                             const char *path);

    /* ======== Program Operations ======== */
    sgl_handle_t (*create_program)(sgl_backend_t *be);
    void (*delete_program)(sgl_backend_t *be, sgl_handle_t handle);
    void (*attach_shader)(sgl_backend_t *be, sgl_handle_t program, sgl_handle_t shader);
    /* Link program - copies shaders to per-program storage */
    bool (*link_program)(sgl_backend_t *be, sgl_handle_t program,
                         sgl_handle_t vertex_shader, sgl_handle_t fragment_shader);
    void (*use_program)(sgl_backend_t *be, sgl_handle_t handle);
    /* Binds shaders AND uniform buffers with pushConstants - call before draw */
    void (*bind_program)(sgl_backend_t *be, sgl_handle_t program,
                         sgl_handle_t vertex_shader, sgl_handle_t fragment_shader,
                         const sgl_uniform_binding_t *vertex_uniforms,
                         const sgl_uniform_binding_t *fragment_uniforms,
                         int max_uniforms);

    /* ======== Uniform Operations ======== */
    /* Write uniform data to CPU buffer (backend handles offset allocation) */
    void (*set_uniform_4f)(sgl_backend_t *be, sgl_handle_t program, GLint location,
                           GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
    void (*set_uniform_matrix4fv)(sgl_backend_t *be, sgl_handle_t program,
                                   GLint location, GLsizei count,
                                   GLboolean transpose, const GLfloat *value);
    /* Get/set uniform offset for a location */
    uint32_t (*alloc_uniform)(sgl_backend_t *be, uint32_t size);
    void (*write_uniform)(sgl_backend_t *be, uint32_t offset, const void *data, uint32_t size);

    /* ======== Vertex Attribute Operations ======== */
    void (*bind_vertex_attribs)(sgl_backend_t *be,
                                 const sgl_vertex_attrib_t *attribs,
                                 int num_attribs, GLint first, GLsizei count);

    /* ======== Draw Operations ======== */
    void (*draw_arrays)(sgl_backend_t *be, GLenum mode, GLint first, GLsizei count);
    /* draw_elements: ebo_offset is pre-computed GPU buffer offset if EBO bound, 0 for client indices */
    void (*draw_elements)(sgl_backend_t *be, GLenum mode, GLsizei count,
                          GLenum type, const void *indices, uint32_t ebo_offset);

    /* ======== Framebuffer Operations ======== */
    sgl_handle_t (*create_framebuffer)(sgl_backend_t *be);
    void (*delete_framebuffer)(sgl_backend_t *be, sgl_handle_t handle);
    /* Bind FBO and switch render target (handle=0 for default FB)
     * depth_rb: renderbuffer handle for depth attachment (0 = none) */
    void (*bind_framebuffer)(sgl_backend_t *be, sgl_handle_t handle,
                              sgl_handle_t color_tex, sgl_handle_t depth_rb);
    void (*framebuffer_texture)(sgl_backend_t *be, sgl_handle_t fbo,
                                 GLenum attachment, sgl_handle_t texture, GLint level);
    GLenum (*check_framebuffer_status)(sgl_backend_t *be, sgl_handle_t handle);

    /* ======== Renderbuffer Operations ======== */
    /* Allocate GPU storage for renderbuffer (depth/stencil) */
    void (*renderbuffer_storage)(sgl_backend_t *be, sgl_handle_t handle,
                                  GLenum internalformat, GLsizei width, GLsizei height);
    void (*delete_renderbuffer)(sgl_backend_t *be, sgl_handle_t handle);

    /* ======== Read Operations ======== */
    void (*read_pixels)(sgl_backend_t *be, GLint x, GLint y,
                        GLsizei width, GLsizei height,
                        GLenum format, GLenum type, void *pixels);

    /* ======== Sync Operations ======== */
    void (*flush)(sgl_backend_t *be);
    void (*finish)(sgl_backend_t *be);
    void (*insert_barrier)(sgl_backend_t *be);

    /* ======== Misc Operations ======== */
    void (*set_line_width)(sgl_backend_t *be, GLfloat width);
    void (*set_depth_bias)(sgl_backend_t *be, GLfloat factor, GLfloat units);
    void (*set_blend_color)(sgl_backend_t *be, GLfloat r, GLfloat g, GLfloat b, GLfloat a);
};

/* Backend structure */
struct sgl_backend {
    const sgl_backend_ops_t *ops;
    void *impl_data;  /* Backend-specific data (e.g., dk_backend_data_t*) */
};

/* Create/destroy backend */
sgl_backend_t *sgl_backend_create_deko3d(void *device);
void sgl_backend_destroy(sgl_backend_t *be);

#endif /* SGL_BACKEND_H */
