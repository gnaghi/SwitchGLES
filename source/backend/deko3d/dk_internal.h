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
#include "../../util/sgl_perf.h"
#include <GLES2/gl2ext.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * ARM Memory Barrier
 *
 * On ARM Cortex-A57 (Tegra X1), even CpuUncached memory writes go through
 * CPU write buffers. Without an explicit Data Synchronization Barrier (DSB),
 * writes may not reach DRAM before the GPU reads them — causing the GPU to
 * read stale/zero descriptor data, which manifests as texture flickering.
 *
 * DSB ST ensures all prior stores reach the point of coherency before
 * continuing. Combined with DkInvalidateFlags_Descriptors in frame-start
 * barriers (which forces TIC/TSC re-read from DRAM), this ensures the GPU
 * sees fresh descriptor data.
 * NOTE: DSB SY (full system barrier) CRASHES on Switch — do NOT use it.
 * ============================================================================ */
#ifdef __aarch64__
#define DK_ARM_STORE_BARRIER() __asm__ volatile("dsb st" ::: "memory")
#else
/* No-op on non-ARM platforms (Windows cross-compilation, etc.) */
#define DK_ARM_STORE_BARRIER()                                                                     \
    do {                                                                                           \
    } while (0)
#endif

/* ============================================================================
 * Debug Configuration
 * ============================================================================ */

/* Verbose debug output - disable for production */
#define DK_DEBUG_VERBOSE 0

#if DK_DEBUG_VERBOSE
#define DK_VERBOSE_PRINT(...)                                                                      \
    do {                                                                                           \
        printf(__VA_ARGS__);                                                                       \
        fflush(stdout);                                                                            \
    } while (0)
#else
#define DK_VERBOSE_PRINT(...)                                                                      \
    do {                                                                                           \
    } while (0)
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
 * Record a dkCmdBufBarrier. Every backend barrier goes through here so that
 * SGL_PERF can count Full drains and whole-L2 invalidations per frame.
 *
 * L2Cache (L2FlushDirty + L2SysmemInvalidate on the whole L2) is only needed
 * when the CPU and the GPU exchange data: CPU writes to CpuUncached memory the
 * GPU may already have cached, or GPU results read back by the CPU. Every GPU
 * client (ROP, texture, copy and 2D engines) goes through the L2, so GPU->GPU
 * dependencies (render target -> sampling, blits, mipmaps) need Image only.
 */
static inline void dk_barrier(DkCmdBuf cmdbuf, DkBarrier mode, uint32_t flags) {
    if (mode == DkBarrier_Full)
        SGL_PERF_ADD(SGL_PERF_BARRIER_FULL, 1);
    if (flags & DkInvalidateFlags_L2Cache)
        SGL_PERF_ADD(SGL_PERF_BARRIER_L2, 1);
    dkCmdBufBarrier(cmdbuf, mode, flags);
}

/**
 * The only way the backend clears a command buffer. Whatever was recorded
 * and not yet submitted is dropped, so every "skip if already recorded"
 * shortcut must be forgotten here: the shaders bound by dk_bind_program
 * (bound_program) and the fixed-function state of dk_state.c (state_cache)
 * are recorded again at the next draw. (The packed-UBO shortcut is handled
 * separately through dk_bump_uniform_generation.)
 */
static inline void dk_cmdbuf_clear(dk_backend_data_t *dk, DkCmdBuf cmdbuf) {
    dkCmdBufClear(cmdbuf);
    dk->bound_program = 0;
    dk_state_cache_invalidate(dk);
}

/**
 * `dsb st` if a CPU write to GPU-visible memory (VBO data, vertex staging,
 * constant block of the disabled attributes) is still pending. Called before
 * the vertex bindings and the draw command are recorded, so that the GPU
 * never executes a draw whose data may sit in the ARM store buffers. The
 * paths that write and record in the same place (index staging, textures,
 * descriptors) keep their own immediate barrier.
 */
static inline void dk_flush_cpu_stores(dk_backend_data_t *dk) {
    if (dk->cpu_store_pending) {
        DK_ARM_STORE_BARRIER();
        dk->cpu_store_pending = false;
    }
}

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
 * Make sync fence `index` signal after every command issued so far, and
 * submit those commands (glFenceSync).
 *
 * @param be     Backend pointer
 * @param index  Sync object index (< SGL_MAX_SYNCS)
 * @return false if the GPU queue is in error state (nothing recorded)
 */
bool dk_fence_sync(sgl_backend_t *be, uint32_t index);

/**
 * Wait for sync fence `index` (glClientWaitSync / sync status queries).
 *
 * @param be          Backend pointer
 * @param index       Sync object index (< SGL_MAX_SYNCS)
 * @param timeout_ns  Maximum wait, 0 = poll
 * @return true once the fence has signaled
 */
bool dk_wait_sync(sgl_backend_t *be, uint32_t index, uint64_t timeout_ns);

/**
 * Submit current command buffer, wait for GPU, and reset for continued use.
 * Called by flush, finish, and orphan overflow recovery.
 *
 * @param dk    Backend data pointer (not sgl_backend_t)
 */
void dk_submit_and_reset(dk_backend_data_t *dk);

/**
 * Synchronous flush: finish the current command list, submit it, and block
 * until the GPU is idle.
 *
 * Used by the synchronous texture/FBO upload and readback paths that must see
 * GPU results immediately (staging copies, glReadPixels, CopyTexImage). Unlike
 * dk_submit_and_reset(), it does NOT drain the deferred VBO free list — these
 * call sites run outside the frame loop. Pair with dk_ensure_recordable() when
 * the operation may follow a swap.
 *
 * @param dk    Backend data pointer (not sgl_backend_t)
 */
void dk_flush_sync(dk_backend_data_t *dk);

/**
 * Restart the uniform allocator at the beginning of a frame slot's sub-region.
 * Only valid once the GPU no longer reads that sub-region (slot fence waited,
 * or queue idle).
 */
void dk_reset_uniform_slot(dk_backend_data_t *dk, int slot);

/**
 * Invalidate every "already pushed" packed UBO address (see
 * dk_backend_data_t::uniform_generation). Called by dk_reset_uniform_slot and
 * by every path that clears the cmdbuf outside it.
 */
void dk_bump_uniform_generation(dk_backend_data_t *dk);

/* Size of one frame slot's uniform / client-array sub-region. */
static inline uint32_t dk_uniform_slot_size(void) {
    return (SGL_UNIFORM_BUF_SIZE / SGL_FB_NUM) & ~(SGL_UNIFORM_ALIGNMENT - 1);
}
static inline uint32_t dk_client_array_slot_size(const dk_backend_data_t *dk) {
    return (dk->uniform_base - dk->client_array_base) / SGL_FB_NUM;
}

/* Draws recorded without a cmdbuf reset after which glFlush recycles the
 * frame's resources (dk_flush): half the 4 MB cmdbuf at ~1.1 KB per draw. */
#define DK_FLUSH_RESET_DRAWS 2000

/**
 * Insert a freed block into the sorted VBO free-list, coalescing with adjacent
 * blocks. Shared by dk_buffer_free() and the deferred-free processing
 * (dk_submit_and_reset, dk_wait_fence).
 *
 * @param dk      Backend data pointer (not sgl_backend_t)
 * @param offset  Byte offset of the freed block in the data memblock
 * @param size    Size of the freed block in bytes
 */
void dk_vbo_free_insert(dk_backend_data_t *dk, uint32_t offset, uint32_t size);
bool dk_vbo_free_reserve(dk_backend_data_t *dk, int extra);

/**
 * Command buffer overflow callback.
 * Safety net called by deko3d when cmdbuf runs out of memory during recording.
 * Submits pending work, waits for GPU, recycles memory, and re-binds essentials.
 */
void dk_cmdbuf_overflow_cb(void *userData, DkCmdBuf cmdbuf, size_t minReqSize);

/**
 * Re-bind the default framebuffer's render target for the current slot.
 * Called after command buffer resets to restore rendering state.
 *
 * @param dk    Backend data pointer (not sgl_backend_t)
 */
void dk_rebind_default_render_target(dk_backend_data_t *dk);

/**
 * Re-bind the current render target (FBO-aware).
 *
 * If an FBO is currently bound, re-binds the FBO color+depth attachments.
 * Otherwise, falls back to dk_rebind_default_render_target() (swapchain).
 *
 * Call this after clearing/resetting the command buffer to restore the
 * render target state that was lost when the cmdbuf was cleared.
 *
 * @param dk    Backend data pointer (not sgl_backend_t)
 */
void dk_rebind_render_target(dk_backend_data_t *dk);

/**
 * Ensure dk->cmdbuf is safe to record into for a synchronous mid-stream
 * operation (texture upload, readback, blit).
 *
 * After eglSwapBuffers, dk_end_frame has already FinishList'd + submitted the
 * slot's cmdbuf and the frame may still be in flight. Recording new commands
 * and calling dkCmdBufFinishList again without an intervening clear would
 * corrupt the in-flight frame and double-finish the list (a documented crash).
 * If cmdbuf_submitted is set, this drains the GPU and resets the cmdbuf to a
 * clean recordable state. No-op otherwise.
 *
 * @param dk    Backend data pointer (not sgl_backend_t)
 */
void dk_ensure_recordable(dk_backend_data_t *dk);

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
 * Apply combined depth and stencil state atomically.
 * This is the only depth/stencil apply function -- separate apply_depth
 * and apply_stencil have been removed to prevent state overwrite bugs.
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
uint32_t dk_buffer_data(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLsizeiptr size,
                        const void *data, GLenum usage);

/**
 * Update a portion of a GPU buffer.
 *
 * @param be            Backend pointer
 * @param handle        Buffer handle
 * @param buffer_offset Offset within the buffer to update
 * @param size          Size of data to update
 * @param data          Pointer to source data
 */
void dk_buffer_sub_data(sgl_backend_t *be, sgl_handle_t handle, uint32_t buffer_offset,
                        GLsizeiptr size, const void *data);

/**
 * Allocate a new buffer region for orphaning (from VBO region with deferred free).
 * Used when glBufferData(data=NULL) is called to orphan an existing buffer.
 * Old allocation is deferred-freed after next GPU sync.
 *
 * @param be          Backend pointer
 * @param size        Size to allocate
 * @param old_offset  Previous allocation offset (for deferred free)
 * @param old_size    Previous allocation size
 * @return Absolute offset into data_memblock, or 0 on failure
 */
uint32_t dk_buffer_data_orphan(sgl_backend_t *be, GLsizeiptr size, uint32_t old_offset,
                               uint32_t old_size);

/**
 * Return a VBO allocation to the free list for reuse.
 * Only frees blocks within the VBO region (offset < client_array_base).
 */
void dk_buffer_free(sgl_backend_t *be, uint32_t offset, uint32_t size);

/**
 * Get CPU pointer to data memblock at given offset.
 * Used by GL layer to scan EBO indices for max vertex index.
 */
const void *dk_get_data_cpu_ptr(sgl_backend_t *be, uint32_t offset);
void dk_buffer_written(sgl_backend_t *be);

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
void dk_draw_elements(sgl_backend_t *be, GLenum mode, GLsizei count, GLenum type,
                      const void *indices, sgl_handle_t ebo);

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
void dk_bind_vertex_attribs(sgl_backend_t *be, const sgl_vertex_attrib_t *attribs, int num_attribs,
                            GLint first, GLsizei count);

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
bool dk_load_shader_binary(sgl_backend_t *be, sgl_handle_t handle, const void *data, size_t size);

/**
 * Delete a shader and decrement active shader count.
 * When all shaders are deleted, resets code memory bump allocator.
 *
 * @param be        Backend pointer
 * @param handle    Shader handle to delete
 */
void dk_delete_shader(sgl_backend_t *be, sgl_handle_t handle);

/**
 * Delete a program and decrement active program count.
 * When all programs and shaders are deleted, resets code memory.
 *
 * @param be        Backend pointer
 * @param handle    Program handle to delete
 */
void dk_delete_program(sgl_backend_t *be, sgl_handle_t handle);

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
bool dk_link_program(sgl_backend_t *be, sgl_handle_t program, sgl_handle_t vertex_shader,
                     sgl_handle_t fragment_shader);

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
void dk_bind_program(sgl_backend_t *be, sgl_handle_t program, sgl_handle_t vertex_shader,
                     sgl_handle_t fragment_shader, const sgl_uniform_binding_t *vertex_uniforms,
                     const sgl_uniform_binding_t *fragment_uniforms, int max_uniforms,
                     sgl_packed_ubo_t *packed_vertex, sgl_packed_ubo_t *packed_fragment,
                     int max_packed_ubos);

/* ============================================================================
 * Texture Operations (dk_texture.c)
 * ============================================================================ */

/**
 * Allocate texture memory from free-list or bump allocator.
 * Returns the aligned offset within texture_memblock, or UINT32_MAX on failure.
 */
uint32_t dk_texture_alloc(dk_backend_data_t *dk, uint32_t alignment, uint32_t size);

/**
 * Create the 1x1 black fallback texture at slot 0.
 * Used for incomplete texture sampling per GLES2 §3.7.10.
 */
void dk_create_black_texture(dk_backend_data_t *dk);

/**
 * Free texture GPU memory back to the texture free-list.
 * Called from dk_delete_texture when a texture is destroyed.
 */
void dk_texture_free(dk_backend_data_t *dk, uint32_t offset, uint32_t size);

/**
 * Delete a texture — clear backend state and return GPU memory to free-list.
 */
void dk_delete_texture(sgl_backend_t *be, sgl_handle_t handle);
void dk_invalidate_texture(sgl_backend_t *be, sgl_handle_t handle);

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
void dk_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLint level,
                         GLint internalformat, GLsizei width, GLsizei height, GLint border,
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
void dk_texture_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLint level,
                             GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
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
void dk_texture_parameter(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLenum pname,
                          GLint param);

/**
 * Bind a texture to a texture unit for sampling.
 *
 * @param be        Backend pointer
 * @param unit      Texture unit index
 * @param handle    Texture handle
 */
void dk_bind_texture(sgl_backend_t *be, GLuint unit, sgl_handle_t handle, int stage);

/**
 * Bind several textures to consecutive binding slots of one stage with a
 * single dkCmdBufBindTextures (one driver-constbuf load instead of one per
 * texture and per stage).
 *
 * @param be        Backend pointer
 * @param stage     0 = vertex, 1 = fragment
 * @param first     First binding slot
 * @param handles   Texture handles for slots first..first+count-1
 * @param count     Number of handles (<= 16)
 */
void dk_bind_textures(sgl_backend_t *be, int stage, GLuint first, const sgl_handle_t *handles,
                      int count);

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
void dk_copy_tex_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLint level,
                          GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height);

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
void dk_copy_tex_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLint level,
                              GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                              GLsizei height);

/**
 * Upload compressed texture data (glCompressedTexImage2D).
 * Supports ASTC, ETC2, and BC (S3TC) formats natively.
 */
void dk_compressed_texture_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target,
                                    GLint level, GLenum internalformat, GLsizei width,
                                    GLsizei height, GLsizei imageSize, const void *data);

/**
 * Update a region of a compressed texture (glCompressedTexSubImage2D).
 */
void dk_compressed_texture_sub_image_2d(sgl_backend_t *be, sgl_handle_t handle, GLenum target,
                                        GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                        GLsizei height, GLenum format, GLsizei imageSize,
                                        const void *data);

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
void dk_bind_framebuffer(sgl_backend_t *be, sgl_handle_t handle, sgl_handle_t color_tex,
                         sgl_handle_t depth_rb, bool color_is_rb, bool depth_is_rb,
                         sgl_handle_t stencil_rb, bool stencil_is_rb);

/**
 * Allocate GPU storage for a renderbuffer (depth/stencil).
 *
 * @param be                Backend pointer
 * @param handle            Renderbuffer handle
 * @param internalformat    Format (GL_DEPTH_COMPONENT16, GL_STENCIL_INDEX8, etc.)
 * @param width             Width in pixels
 * @param height            Height in pixels
 */
void dk_renderbuffer_storage(sgl_backend_t *be, sgl_handle_t handle, GLenum internalformat,
                             GLsizei width, GLsizei height);

/**
 * Delete a renderbuffer's GPU resources.
 *
 * @param be            Backend pointer
 * @param handle        Renderbuffer handle
 */
void dk_delete_renderbuffer(sgl_backend_t *be, sgl_handle_t handle);

/**
 * Blit (copy) between framebuffers using GPU 2D engine.
 *
 * @param be              Backend pointer
 * @param read_fbo        Source FBO handle (0 = default framebuffer)
 * @param read_color_tex  Source color attachment texture handle
 * @param write_fbo       Dest FBO handle (0 = default framebuffer)
 * @param write_color_tex Dest color attachment texture handle
 * @param srcX0..srcY1    Source rectangle
 * @param dstX0..dstY1    Destination rectangle
 * @param mask            GL_COLOR_BUFFER_BIT, GL_DEPTH_BUFFER_BIT, etc.
 * @param filter          GL_NEAREST or GL_LINEAR
 */
void dk_blit_framebuffer(sgl_backend_t *be, sgl_handle_t read_fbo, sgl_handle_t read_color_tex,
                         sgl_handle_t write_fbo, sgl_handle_t write_color_tex, GLint srcX0,
                         GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0,
                         GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);

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
void dk_read_pixels(sgl_backend_t *be, GLint x, GLint y, GLsizei width, GLsizei height,
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
