/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Buffer Operations
 *
 * This module handles GPU buffer management:
 * - Buffer creation and deletion
 * - Buffer data upload (glBufferData)
 * - Buffer sub-data update (glBufferSubData)
 *
 * Buffers are allocated from a single large memory block (data_memblock).
 * The allocation is a simple bump allocator that does not support deallocation.
 */

#include "dk_internal.h"

/* ============================================================================
 * Buffer Handle Management
 *
 * Note: Backend doesn't allocate separate handles - uses data_memblock offsets.
 * Handle management is done at the GL layer.
 * ============================================================================ */

sgl_handle_t dk_create_buffer(sgl_backend_t *be) {
    (void)be;
    /* Backend doesn't allocate separate handles - uses data_memblock offsets */
    return 1;  /* Non-zero to indicate success */
}

void dk_delete_buffer(sgl_backend_t *be, sgl_handle_t handle) {
    (void)be;
    (void)handle;
    /* Memory is in a pool, not individually freed */
}

/* ============================================================================
 * Buffer Data Upload
 * ============================================================================ */

uint32_t dk_buffer_data(sgl_backend_t *be, sgl_handle_t handle, GLenum target,
                        GLsizeiptr size, const void *data, GLenum usage) {
    (void)handle;
    (void)target;
    (void)usage;

    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Allocate from data memory with 256-byte alignment */
    uint32_t aligned_offset = (dk->data_offset + 255) & ~255;
    if (aligned_offset + size > dk->client_array_base) {
        SGL_ERROR_BACKEND("Buffer allocation failed: out of memory");
        return 0;
    }

    /* Copy data if provided */
    if (data && size > 0) {
        void *dst = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock) + aligned_offset;
        memcpy(dst, data, size);
    }

    dk->data_offset = aligned_offset + size;
    return aligned_offset;
}

/* ============================================================================
 * Buffer Sub-Data Update
 * ============================================================================ */

void dk_buffer_sub_data(sgl_backend_t *be, sgl_handle_t handle,
                        uint32_t buffer_offset, GLsizeiptr size, const void *data) {
    (void)handle;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (data && size > 0) {
        void *dst = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock) + buffer_offset;
        memcpy(dst, data, size);
    }
}
