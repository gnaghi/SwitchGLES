/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Uniform Operations
 *
 * This module handles uniform buffer management:
 * - Uniform buffer allocation from the uniform region
 * - Uniform data writes to CPU-accessible memory
 *
 * Note: The actual uniform binding with pushConstants is done in dk_shader.c
 * as part of dk_bind_program() to capture uniform values at draw time.
 *
 * Memory layout:
 * - Uniform buffer region is at the end of data_memblock
 * - Each uniform allocation is aligned to 256 bytes (DK_UNIFORM_BUF_ALIGNMENT)
 */

#include "dk_internal.h"

/* ============================================================================
 * Uniform Allocation
 *
 * Allocates space in the uniform buffer region.
 * Returns the offset relative to uniform_base that can be used with
 * write_uniform and for binding.
 * ============================================================================ */

uint32_t dk_alloc_uniform(sgl_backend_t *be, uint32_t size) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Align size to 256 bytes (DK_UNIFORM_BUF_ALIGNMENT) */
    uint32_t alignedSize = SGL_ALIGN_UP(size, SGL_UNIFORM_ALIGNMENT);

    /* Check for overflow — reuse last allocation instead of corrupting offset 0.
     * This avoids data corruption when the uniform buffer is full: subsequent
     * draws will share the same uniform slot (wrong data) but won't overwrite
     * the first draw's data at offset 0. */
    if (dk->uniform_offset + alignedSize > SGL_UNIFORM_BUF_SIZE) {
        dk->diag_uniform_overflows++;
        SGL_ERROR_BACKEND("alloc_uniform: out of uniform memory (need %u, have %u)",
                          alignedSize, SGL_UNIFORM_BUF_SIZE - dk->uniform_offset);
        /* Return last valid offset (at least 256 bytes from end) so writes
         * go to a safe location rather than offset 0 */
        uint32_t fallback = SGL_UNIFORM_BUF_SIZE > alignedSize
                          ? SGL_UNIFORM_BUF_SIZE - alignedSize : 0;
        return fallback;
    }

    uint32_t offset = dk->uniform_offset;
    dk->uniform_offset += alignedSize;

    DK_VERBOSE_PRINT("[DK] alloc_uniform: size=%u aligned=%u offset=%u\n",
                     size, alignedSize, offset);
    SGL_TRACE_UNIFORM("alloc_uniform size=%u -> offset=%u", size, offset);

    return offset;
}

/* ============================================================================
 * Uniform Write
 *
 * Writes uniform data to the CPU-accessible uniform buffer region.
 * The data will be captured by pushConstants when bind_program is called.
 * ============================================================================ */

void dk_write_uniform(sgl_backend_t *be, uint32_t offset, const void *data, uint32_t size) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (!data || size == 0) {
        return;
    }

    /* Calculate CPU address in uniform buffer region */
    uint8_t *cpu_base = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock);
    void *dst = cpu_base + dk->uniform_base + offset;

    /* Copy uniform data */
    memcpy(dst, data, size);

    DK_VERBOSE_PRINT("[DK] write_uniform: offset=%u size=%u\n", offset, size);
    SGL_TRACE_UNIFORM("write_uniform offset=%u size=%u", offset, size);
}
