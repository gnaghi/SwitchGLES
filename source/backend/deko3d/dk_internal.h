/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Internal header for module files
 *
 * This header is included by all dk_*.c module files and provides:
 * - Common includes
 * - Debug macros
 * - Internal function declarations shared between modules
 */

#ifndef DK_INTERNAL_H
#define DK_INTERNAL_H

#include "dk_backend.h"
#include "../../util/sgl_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Debug Configuration
 * ============================================================================ */

/* Verbose debug output - disable for production */
#define DK_DEBUG_VERBOSE 0

#if DK_DEBUG_VERBOSE
#define DK_VERBOSE_PRINT(...) do { printf(__VA_ARGS__); fflush(stdout); } while(0)
#else
#define DK_VERBOSE_PRINT(...) do {} while(0)
#endif

/* ============================================================================
 * Lifecycle Operations (dk_backend.c)
 * ============================================================================ */

/**
 * Initialize the deko3d backend.
 * Allocates GPU memory for command buffers, shaders, data, textures, and descriptors.
 *
 * @param be    Backend pointer
 * @param device    Deko3d device handle (DkDevice)
 * @return 0 on success, -1 on failure
 */
int dk_init(sgl_backend_t *be, void *device);

/**
 * Shutdown the deko3d backend.
 * Releases all GPU memory and resources.
 *
 * @param be    Backend pointer
 */
void dk_shutdown(sgl_backend_t *be);

/* ============================================================================
 * Frame/Command Management (dk_command.c)
 * ============================================================================ */

/**
 * Begin a new frame on the specified swapchain slot.
 * Sets up the command buffer for the new frame.
 *
 * @param be    Backend pointer
 * @param slot  Framebuffer slot index
 */
void dk_begin_frame(sgl_backend_t *be, int slot);

/**
 * End the current frame on the specified slot.
 * Finalizes the command buffer and signals the fence.
 *
 * @param be    Backend pointer
 * @param slot  Framebuffer slot index
 */
void dk_end_frame(sgl_backend_t *be, int slot);

/**
 * Present the frame on the specified slot to the display.
 *
 * @param be    Backend pointer
 * @param slot  Framebuffer slot index
 */
void dk_present(sgl_backend_t *be, int slot);

/**
 * Acquire the next available swapchain image.
 *
 * @param be    Backend pointer
 * @return Slot index of acquired image
 */
int dk_acquire_image(sgl_backend_t *be);

/**
 * Wait for the GPU to finish using the specified slot's resources.
 *
 * @param be    Backend pointer
 * @param slot  Framebuffer slot index
 */
void dk_wait_fence(sgl_backend_t *be, int slot);

/**
 * Flush pending GPU commands without waiting.
 *
 * @param be    Backend pointer
 */
void dk_flush(sgl_backend_t *be);

/**
 * Flush and wait for all GPU commands to complete.
 *
 * @param be    Backend pointer
 */
void dk_finish(sgl_backend_t *be);

/**
 * Insert a full pipeline barrier.
 *
 * @param be    Backend pointer
 */
void dk_insert_barrier(sgl_backend_t *be);

/* ============================================================================
 * State Application (dk_state.c)
 * ============================================================================ */

/**
 * Apply viewport state to the command buffer.
 *
 * @param be    Backend pointer
 * @param state Viewport state (x, y, width, height)
 */
void dk_apply_viewport(sgl_backend_t *be, const sgl_viewport_state_t *state);

/**
 * Apply scissor state to the command buffer.
 *
 * @param be    Backend pointer
 * @param state Scissor state (enabled, x, y, width, height)
 */
void dk_apply_scissor(sgl_backend_t *be, const sgl_scissor_state_t *state);

/**
 * Apply blend state to the command buffer.
 *
 * @param be    Backend pointer
 * @param state Blend state (enabled, factors, equations)
 */
void dk_apply_blend(sgl_backend_t *be, const sgl_blend_state_t *state);

/**
 * Apply depth test state to the command buffer.
 *
 * @param be    Backend pointer
 * @param state Depth state (enabled, func, write mask)
 */
void dk_apply_depth(sgl_backend_t *be, const sgl_depth_state_t *state);

/**
 * Apply stencil test state to the command buffer.
 *
 * @param be    Backend pointer
 * @param state Stencil state (enabled, func, ops, ref, masks)
 */
void dk_apply_stencil(sgl_backend_t *be, const sgl_stencil_state_t *state);

/**
 * Apply combined depth and stencil state atomically.
 * This avoids the issue where separate apply_depth/apply_stencil calls
 * would overwrite each other's DkDepthStencilState.
 *
 * @param be    Backend pointer
 * @param state Combined depth-stencil state
 */
void dk_apply_depth_stencil(sgl_backend_t *be, const sgl_depth_stencil_state_t *state);

/**
 * Apply rasterizer state (culling, front face, polygon mode) to the command buffer.
 *
 * @param be    Backend pointer
 * @param state Rasterizer state
 */
void dk_apply_raster(sgl_backend_t *be, const sgl_raster_state_t *state);

/**
 * Apply color write mask to the command buffer.
 *
 * @param be    Backend pointer
 * @param state Color state (RGBA masks)
 */
void dk_apply_color_mask(sgl_backend_t *be, const sgl_color_state_t *state);

/**
 * Set depth bias (polygon offset) values.
 *
 * @param be    Backend pointer
 * @param factor    Slope-dependent depth bias factor
 * @param units Constant depth bias units
 */
void dk_set_depth_bias(sgl_backend_t *be, GLfloat factor, GLfloat units);

/* ============================================================================
 * Clear Operations (dk_clear.c)
 * ============================================================================ */

/**
 * Clear framebuffer attachments.
 *
 * @param be        Backend pointer
 * @param mask      Bitmask of GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT, GL_STENCIL_BUFFER_BIT
 * @param color     Clear color (RGBA float array)
 * @param depth     Clear depth value
 * @param stencil   Clear stencil value
 */
void dk_clear(sgl_backend_t *be, GLbitfield mask, const float *color, float depth, int stencil);

/* ============================================================================
 * Buffer Operations (dk_buffer.c)
 * ============================================================================ */

/**
 * Create a new GPU buffer handle.
 *
 * @param be    Backend pointer
 * @return Handle to the new buffer, or 0 on failure
 */
sgl_handle_t dk_create_buffer(sgl_backend_t *be);

/**
 * Delete a GPU buffer.
 *
 * @param be        Backend pointer
 * @param handle    Buffer handle to delete
 */
void dk_delete_buffer(sgl_backend_t *be, sgl_handle_t handle);

/**
 * Upload data to a GPU buffer.
 *
 * @param be        Backend pointer
 * @param handle    Buffer handle
 * @param target    GL buffer target (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER)
 * @param size      Size of data in bytes
 * @param data      Pointer to source data (may be NULL for allocation only)
 * @param usage     GL usage hint
 * @return GPU memory offset of the allocated buffer, or 0 on failure
 */
uint32_t dk_buffer_data(sgl_backend_t *be, sgl_handle_t handle, GLenum target,
                        GLsizeiptr size, const void *data, GLenum usage);

/**
 * Update a portion of a GPU buffer.
 *
 * @param be            Backend pointer
 * @param handle        Buffer handle
 * @param buffer_offset Offset within the buffer to update
 * @param size          Size of data to update
 * @param data          Pointer to source data
 */
void dk_buffer_sub_data(sgl_backend_t *be, sgl_handle_t handle,
                        uint32_t buffer_offset, GLsizeiptr size, const void *data);

/* ============================================================================
 * Draw Operations (dk_draw.c)
 * ============================================================================ */

/**
 * Draw primitives from vertex arrays.
 *
 * @param be    Backend pointer
 * @param mode  Primitive type (GL_TRIANGLES, GL_TRIANGLE_STRIP, etc.)
 * @param first Index of first vertex
 * @param count Number of vertices to draw
 */
void dk_draw_arrays(sgl_backend_t *be, GLenum mode, GLint first, GLsizei count);

/**
 * Draw indexed primitives.
 *
 * @param be        Backend pointer
 * @param mode      Primitive type
 * @param count     Number of indices to draw
 * @param type      Index type (GL_UNSIGNED_SHORT, GL_UNSIGNED_INT)
 * @param indices   Pointer to indices (client array) or offset if EBO bound
 * @param ebo       Element buffer object handle (0 for client-side indices)
 */
void dk_draw_elements(sgl_backend_t *be, GLenum mode, GLsizei count,
                      GLenum type, const void *indices, sgl_handle_t ebo);

/**
 * Bind vertex attributes for drawing.
 * Configures vertex buffer bindings and attribute formats.
 *
 * @param be            Backend pointer
 * @param attribs       Array of vertex attribute states
 * @param num_attribs   Number of attributes in array
 * @param first         First vertex index (for offset calculation)
 * @param count         Number of vertices (for size calculation)
 */
void dk_bind_vertex_attribs(sgl_backend_t *be, const sgl_vertex_attrib_t *attribs,
                            int num_attribs, GLint first, GLsizei count);

/* ============================================================================
 * Uniform Operations (dk_uniform.c)
 * ============================================================================ */

/**
 * Allocate space in the uniform buffer.
 *
 * @param be    Backend pointer
 * @param size  Size to allocate (will be aligned to 256 bytes)
 * @return Offset within uniform buffer region
 */
uint32_t dk_alloc_uniform(sgl_backend_t *be, uint32_t size);

/**
 * Write data to uniform buffer at specified offset.
 *
 * @param be        Backend pointer
 * @param offset    Offset within uniform buffer region
 * @param data      Pointer to source data
 * @param size      Size of data to write
 */
void dk_write_uniform(sgl_backend_t *be, uint32_t offset, const void *data, uint32_t size);

/* ============================================================================
 * Shader Operations (dk_shader.c)
 * ============================================================================ */

/**
 * Load a compiled shader from a .dksh file.
 *
 * @param be        Backend pointer
 * @param handle    Shader handle to load into
 * @param path      Path to the .dksh file
 * @return true on success, false on failure
 */
bool dk_load_shader_file(sgl_backend_t *be, sgl_handle_t handle, const char *path);

/**
 * Load a compiled shader from a memory buffer.
 *
 * @param be        Backend pointer
 * @param handle    Shader handle to load into
 * @param data      Pointer to DKSH binary data
 * @param size      Size of binary data in bytes
 * @return true on success, false on failure
 */
bool dk_load_shader_binary(sgl_backend_t *be, sgl_handle_t handle,
                           const void *data, size_t size);

/**
 * Link vertex and fragment shaders into a program.
 * Copies shader code to per-program storage for independent binding.
 *
 * @param be                Backend pointer
 * @param program           Program handle
 * @param vertex_shader     Vertex shader handle
 * @param fragment_shader   Fragment shader handle
 * @return true on success, false on failure
 */
bool dk_link_program(sgl_backend_t *be, sgl_handle_t program,
                     sgl_handle_t vertex_shader, sgl_handle_t fragment_shader);

/**
 * Bind a program for rendering.
 * Binds shaders and uniform buffers with pushConstants for data capture.
 *
 * @param be                Backend pointer
 * @param program           Program handle
 * @param vertex_shader     Vertex shader handle (unused, kept for interface)
 * @param fragment_shader   Fragment shader handle (unused, kept for interface)
 * @param vertex_uniforms   Vertex stage uniform bindings
 * @param fragment_uniforms Fragment stage uniform bindings
 * @param max_uniforms      Maximum number of uniform bindings to process
 */
void dk_bind_program(sgl_backend_t *be, sgl_handle_t program,
                     sgl_handle_t vertex_shader, sgl_handle_t fragment_shader,
                     const sgl_uniform_binding_t *vertex_uniforms,
                     const sgl_uniform_binding_t *fragment_uniforms,
                     int max_uniforms,
                     const sgl_packed_ubo_t *packed_vertex,
                     const sgl_packed_ubo_t *packed_fragment,
                     int max_packed_ubos);

/* ============================================================================
 * Texture Operations (dk_texture.c)
 * ============================================================================ */

/**
 * Upload 2D texture image data.
 *
 * @param be                Backend pointer
 * @param handle            Texture handle
 * @param target            Texture target (GL_TEXTURE_2D)
 * @param level             Mipmap level
 * @param internalformat    Internal format
 * @param width             Texture width
 * @param height            Texture height
 * @param border            Border width (must be 0)
 * @param format            Pixel data format
 * @param type              Pixel data type
 * @param pixels            Pointer to pixel data (may be NULL)
 */
void dk_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                         GLenum target, GLint level, GLint internalformat,
                         GLsizei width, GLsizei height, GLint border,
                         GLenum format, GLenum type, const void *pixels);

/**
 * Update a sub-region of a 2D texture.
 *
 * @param be        Backend pointer
 * @param handle    Texture handle
 * @param target    Texture target
 * @param level     Mipmap level
 * @param xoffset   X offset of region
 * @param yoffset   Y offset of region
 * @param width     Region width
 * @param height    Region height
 * @param format    Pixel data format
 * @param type      Pixel data type
 * @param pixels    Pointer to pixel data
 */
void dk_texture_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                             GLenum target, GLint level,
                             GLint xoffset, GLint yoffset,
                             GLsizei width, GLsizei height,
                             GLenum format, GLenum type, const void *pixels);

/**
 * Set a texture parameter.
 *
 * @param be        Backend pointer
 * @param handle    Texture handle
 * @param target    Texture target
 * @param pname     Parameter name (GL_TEXTURE_MIN_FILTER, etc.)
 * @param param     Parameter value
 */
void dk_texture_parameter(sgl_backend_t *be, sgl_handle_t handle,
                          GLenum target, GLenum pname, GLint param);

/**
 * Bind a texture to a texture unit for sampling.
 *
 * @param be        Backend pointer
 * @param unit      Texture unit index
 * @param handle    Texture handle
 */
void dk_bind_texture(sgl_backend_t *be, GLuint unit, sgl_handle_t handle);

/**
 * Generate mipmaps for a texture.
 *
 * @param be        Backend pointer
 * @param handle    Texture handle
 */
void dk_generate_mipmap(sgl_backend_t *be, sgl_handle_t handle);

/**
 * Copy framebuffer to a new texture.
 *
 * @param be                Backend pointer
 * @param handle            Texture handle
 * @param target            Texture target
 * @param level             Mipmap level
 * @param internalformat    Internal format
 * @param x                 Framebuffer X coordinate
 * @param y                 Framebuffer Y coordinate
 * @param width             Copy width
 * @param height            Copy height
 */
void dk_copy_tex_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                          GLenum target, GLint level, GLenum internalformat,
                          GLint x, GLint y, GLsizei width, GLsizei height);

/**
 * Copy framebuffer to a texture sub-region.
 *
 * @param be        Backend pointer
 * @param handle    Texture handle
 * @param target    Texture target
 * @param level     Mipmap level
 * @param xoffset   Texture X offset
 * @param yoffset   Texture Y offset
 * @param x         Framebuffer X coordinate
 * @param y         Framebuffer Y coordinate
 * @param width     Copy width
 * @param height    Copy height
 */
void dk_copy_tex_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                              GLenum target, GLint level,
                              GLint xoffset, GLint yoffset,
                              GLint x, GLint y, GLsizei width, GLsizei height);

/**
 * Upload compressed texture data (glCompressedTexImage2D).
 * Supports ASTC, ETC2, and BC (S3TC) formats natively.
 */
void dk_compressed_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                                     GLenum target, GLint level, GLenum internalformat,
                                     GLsizei width, GLsizei height,
                                     GLsizei imageSize, const void *data);

/**
 * Update a region of a compressed texture (glCompressedTexSubImage2D).
 */
void dk_compressed_texture_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle,
                                         GLenum target, GLint level,
                                         GLint xoffset, GLint yoffset,
                                         GLsizei width, GLsizei height,
                                         GLenum format, GLsizei imageSize, const void *data);

/* ============================================================================
 * Framebuffer Operations (dk_framebuffer.c)
 * ============================================================================ */

/**
 * Bind a framebuffer for rendering.
 *
 * @param be            Backend pointer
 * @param handle        FBO handle (0 for default framebuffer)
 * @param color_tex     Color attachment texture handle
 * @param depth_rb      Depth renderbuffer handle (0 for none/default)
 */
void dk_bind_framebuffer(sgl_backend_t *be, sgl_handle_t handle,
                         sgl_handle_t color_tex, sgl_handle_t depth_rb);

/**
 * Allocate GPU storage for a renderbuffer (depth/stencil).
 *
 * @param be                Backend pointer
 * @param handle            Renderbuffer handle
 * @param internalformat    Format (GL_DEPTH_COMPONENT16, GL_STENCIL_INDEX8, etc.)
 * @param width             Width in pixels
 * @param height            Height in pixels
 */
void dk_renderbuffer_storage(sgl_backend_t *be, sgl_handle_t handle,
                              GLenum internalformat, GLsizei width, GLsizei height);

/**
 * Delete a renderbuffer's GPU resources.
 *
 * @param be            Backend pointer
 * @param handle        Renderbuffer handle
 */
void dk_delete_renderbuffer(sgl_backend_t *be, sgl_handle_t handle);

/**
 * Read pixels from the current framebuffer.
 *
 * @param be        Backend pointer
 * @param x         X coordinate
 * @param y         Y coordinate
 * @param width     Read width
 * @param height    Read height
 * @param format    Pixel format (GL_RGBA)
 * @param type      Pixel type (GL_UNSIGNED_BYTE)
 * @param pixels    Destination buffer
 */
void dk_read_pixels(sgl_backend_t *be, GLint x, GLint y,
                    GLsizei width, GLsizei height,
                    GLenum format, GLenum type, void *pixels);

/* ============================================================================
 * Utility/Conversion Functions (dk_utils.c)
 *
 * These are already declared in dk_backend.h but listed here for reference:
 * - DkCompareOp dk_convert_compare_op(GLenum func);
 * - DkStencilOp dk_convert_stencil_op(GLenum op);
 * - DkBlendFactor dk_convert_blend_factor(GLenum factor);
 * - DkBlendOp dk_convert_blend_op(GLenum op);
 * - DkPrimitive dk_convert_primitive(GLenum mode);
 * - DkImageFormat dk_convert_format(GLenum internalformat, GLenum format, GLenum type);
 * - DkImageFormat dk_convert_compressed_format(GLenum internalformat);
 * - void dk_get_compressed_block_size(GLenum internalformat, int *blockWidth, int *blockHeight);
 * - int dk_get_compressed_block_bytes(GLenum internalformat);
 * - void dk_get_attrib_format(GLenum type, GLint size, GLboolean normalized,
 *                             DkVtxAttribSize *outSize, DkVtxAttribType *outType);
 * - GLsizei dk_get_type_size(GLenum type);
 * ============================================================================ */

/* Compressed texture format helpers */
DkImageFormat dk_convert_compressed_format(GLenum internalformat);
void dk_get_compressed_block_size(GLenum internalformat, int *blockWidth, int *blockHeight);
int dk_get_compressed_block_bytes(GLenum internalformat);

#endif /* DK_INTERNAL_H */
