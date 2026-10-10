/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Buffer Operations
 *
 * This module handles GPU buffer management:
 * - Buffer creation and deletion
 * - Buffer data upload (glBufferData)
 * - Buffer sub-data update (glBufferSubData)
 * - Buffer orphaning (per-frame allocation for dynamic data)
 * - Free-list allocator for VBO memory reclamation
 *
 * Buffers are allocated from a single large memory block (data_memblock).
 * Static VBOs use a first-fit free-list allocator with coalescing.
 * Dynamic/orphaned buffers use a per-frame bump allocator (client_array region).
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
    return 1; /* Non-zero to indicate success */
}

void dk_delete_buffer(sgl_backend_t *be, sgl_handle_t handle) {
    (void)be;
    (void)handle;
    /* Actual memory reclamation is done via dk_buffer_free() from GL layer,
     * which has the offset and size information. */
}

/* ============================================================================
 * VBO Free-List Allocator
 *
 * Maintains a sorted (by offset) list of free blocks in the VBO region.
 * On free: insert block and coalesce with neighbors.
 * On alloc: first-fit search, then fall back to bump allocator.
 * Offsets and sizes are all multiples of SGL_UNIFORM_ALIGNMENT: a freed block
 * then fits exactly a later request of the same size. With unrounded sizes,
 * each free block ended mid-granule and lost up to 255 bytes to the next
 * request's alignment, so a stream of same-size orphans (spearmint in
 * splitscreen) never reused them and kept bumping.
 * ============================================================================ */

/* Makes room for `extra` more entries in the VBO free list. Returns false only
 * when the list cannot grow (out of memory). */
bool dk_vbo_free_reserve(dk_backend_data_t *dk, int extra) {
    if (dk->vbo_free_count + extra <= dk->vbo_free_capacity)
        return true;
    int capacity = dk->vbo_free_capacity ? dk->vbo_free_capacity * 2 : 512;
    while (capacity < dk->vbo_free_count + extra)
        capacity *= 2;
    sgl_vbo_free_block_t *grown =
        (sgl_vbo_free_block_t *)realloc(dk->vbo_free_list, (size_t)capacity * sizeof(*grown));
    if (!grown)
        return false;
    dk->vbo_free_list = grown;
    dk->vbo_free_capacity = capacity;
    return true;
}

/* Insert a freed block into the sorted VBO free-list, coalescing with the
 * previous and/or next block. Shared by dk_buffer_free() and the deferred-free
 * processing in dk_submit_and_reset() (both return blocks to the same list). */
void dk_vbo_free_insert(dk_backend_data_t *dk, uint32_t offset, uint32_t size) {
    if (offset == 0 || size == 0)
        return;

    /* Same rounding as dk_buffer_data, which reserved the whole rounded size */
    size = SGL_ALIGN_UP(size, SGL_UNIFORM_ALIGNMENT);

    /* Only free blocks from the VBO region (not client_array or uniform) */
    if (offset >= dk->client_array_base)
        return;

    /* Find insertion point (keep list sorted by offset) */
    int insertAt = 0;
    while (insertAt < dk->vbo_free_count && dk->vbo_free_list[insertAt].offset < offset)
        insertAt++;

    /* Try coalescing with previous block */
    if (insertAt > 0) {
        sgl_vbo_free_block_t *prev = &dk->vbo_free_list[insertAt - 1];
        if (prev->offset + prev->size == offset) {
            /* Merge with previous */
            prev->size += size;

            /* Also try merging with next */
            if (insertAt < dk->vbo_free_count &&
                prev->offset + prev->size == dk->vbo_free_list[insertAt].offset) {
                prev->size += dk->vbo_free_list[insertAt].size;
                /* Remove the next block */
                memmove(&dk->vbo_free_list[insertAt], &dk->vbo_free_list[insertAt + 1],
                        (dk->vbo_free_count - insertAt - 1) * sizeof(sgl_vbo_free_block_t));
                dk->vbo_free_count--;
            }

            SGL_TRACE_BUFFER(
                "buffer_free: coalesced with prev -> offset=%u size=%u (free_count=%d)",
                prev->offset, prev->size, dk->vbo_free_count);
            return;
        }
    }

    /* Try coalescing with next block */
    if (insertAt < dk->vbo_free_count && offset + size == dk->vbo_free_list[insertAt].offset) {
        dk->vbo_free_list[insertAt].offset = offset;
        dk->vbo_free_list[insertAt].size += size;

        SGL_TRACE_BUFFER("buffer_free: coalesced with next -> offset=%u size=%u (free_count=%d)",
                         offset, dk->vbo_free_list[insertAt].size, dk->vbo_free_count);
        return;
    }

    /* No coalescing possible — insert new block */
    if (!dk_vbo_free_reserve(dk, 1)) {
        SGL_ERROR_BACKEND("buffer_free: out of memory for the free list, leaking %u bytes at %u",
                          size, offset);
        return;
    }

    memmove(&dk->vbo_free_list[insertAt + 1], &dk->vbo_free_list[insertAt],
            (dk->vbo_free_count - insertAt) * sizeof(sgl_vbo_free_block_t));
    dk->vbo_free_list[insertAt].offset = offset;
    dk->vbo_free_list[insertAt].size = size;
    dk->vbo_free_count++;

    SGL_TRACE_BUFFER("buffer_free: added block offset=%u size=%u (free_count=%d)", offset, size,
                     dk->vbo_free_count);
}

void dk_buffer_free(sgl_backend_t *be, uint32_t offset, uint32_t size) {
    dk_vbo_free_insert((dk_backend_data_t *)be->impl_data, offset, size);
}

const void *dk_get_data_cpu_ptr(sgl_backend_t *be, uint32_t offset) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    return (const uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) + offset;
}

void dk_buffer_written(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    /* Same flags as dk_buffer_sub_data: dsb st and an L2 invalidate before
     * the next draw (dk_draw.c) */
    dk->vbo_data_dirty = true;
    dk->cpu_store_pending = true;
}

/* ============================================================================
 * Buffer Data Upload
 * ============================================================================ */

uint32_t dk_buffer_data(sgl_backend_t *be, sgl_handle_t handle, GLenum target, GLsizeiptr size,
                        const void *data, GLenum usage) {
    (void)handle;
    (void)target;
    (void)usage;

    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    uint32_t alloc_size = SGL_ALIGN_UP((uint32_t)size, SGL_UNIFORM_ALIGNMENT);

    /* First-fit search in free list */
    for (int i = 0; i < dk->vbo_free_count; i++) {
        uint32_t block_offset = dk->vbo_free_list[i].offset;
        uint32_t block_size = dk->vbo_free_list[i].size;
        uint32_t aligned = SGL_ALIGN_UP(block_offset, SGL_UNIFORM_ALIGNMENT);
        uint32_t alignment_waste = aligned - block_offset;

        if (block_size >= alignment_waste + alloc_size) {
            /* Found a fitting block */
            uint32_t remaining = block_size - alignment_waste - alloc_size;

            if (remaining > 0) {
                /* Split: keep the tail remainder free. If there is alignment
                 * waste at the head (the block offset was not 256-aligned),
                 * keep it as its own free block instead of leaking it — it
                 * coalesces with neighbours on a later free. */
                if (alignment_waste > 0 && dk_vbo_free_reserve(dk, 1)) {
                    memmove(&dk->vbo_free_list[i + 2], &dk->vbo_free_list[i + 1],
                            (dk->vbo_free_count - i - 1) * sizeof(sgl_vbo_free_block_t));
                    dk->vbo_free_list[i].offset = block_offset;
                    dk->vbo_free_list[i].size = alignment_waste;
                    dk->vbo_free_list[i + 1].offset = aligned + alloc_size;
                    dk->vbo_free_list[i + 1].size = remaining;
                    dk->vbo_free_count++;
                } else {
                    dk->vbo_free_list[i].offset = aligned + alloc_size;
                    dk->vbo_free_list[i].size = remaining;
                }
            } else if (alignment_waste > 0) {
                /* Allocation consumes the block. Preserve the head alignment
                 * waste rather than leaking it. */
                dk->vbo_free_list[i].offset = block_offset;
                dk->vbo_free_list[i].size = alignment_waste;
            } else {
                /* Remove entire block */
                memmove(&dk->vbo_free_list[i], &dk->vbo_free_list[i + 1],
                        (dk->vbo_free_count - i - 1) * sizeof(sgl_vbo_free_block_t));
                dk->vbo_free_count--;
            }

            /* Copy data if provided */
            if (data && size > 0) {
                void *dst = (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) + aligned;
                memcpy(dst, data, size);
                dk->vbo_data_dirty = true;
                dk->cpu_store_pending = true;
            }

            SGL_TRACE_BUFFER("buffer_data: reused free block at offset=%u size=%zu (free_count=%d)",
                             aligned, (size_t)size, dk->vbo_free_count);
            return aligned;
        }
    }

    /* No free block fits — bump allocate */
    uint32_t aligned_offset = SGL_ALIGN_UP(dk->data_offset, SGL_UNIFORM_ALIGNMENT);
    if (aligned_offset + alloc_size > dk->client_array_base) {
        SGL_ERROR_BACKEND(
            "Buffer allocation failed: out of memory (need %u at offset %u, limit %u)",
            (unsigned)size, aligned_offset, dk->client_array_base);
        return 0;
    }

    /* Copy data if provided */
    if (data && size > 0) {
        void *dst = (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) + aligned_offset;
        memcpy(dst, data, size);
        dk->vbo_data_dirty = true;
        dk->cpu_store_pending = true;
    }

    dk->data_offset = aligned_offset + alloc_size;
    if (dk->data_offset > dk->data_offset_watermark)
        dk->data_offset_watermark = dk->data_offset;
    return aligned_offset;
}

/* ============================================================================
 * Buffer Orphaning (VBO allocation with deferred free)
 *
 * When glBufferData is called with data=NULL on an existing buffer, this is the
 * "buffer orphaning" pattern: the app wants a NEW memory region so it can write
 * new data without conflicting with GPU reads of the previous region.
 *
 * We allocate from the VBO region (free-list + bump). The old allocation is
 * added to a deferred free list, processed once the GPU is done with it: when
 * the fence of the current frame slot is waited, or at the next WaitIdle.
 * This avoids client_array which is reset by dk_submit_and_reset (called by
 * glReadPixels), which would corrupt orphaned buffer data during verification.
 * ============================================================================ */

uint32_t dk_buffer_data_orphan(sgl_backend_t *be, GLsizeiptr size, uint32_t old_offset,
                               uint32_t old_size) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Defer-free old allocation: can't free immediately because in-flight
     * draws may still reference it. Every such draw was recorded before this
     * call, so the fence signalled at the end of the current slot's frame
     * covers them all: freed in dk_wait_fence for that slot, or earlier in
     * dk_submit_and_reset after WaitIdle. */
    if (old_offset != 0 && old_size != 0 &&
        old_offset < dk->client_array_base) { /* Only VBO region blocks */
        if (dk->deferred_free_count == dk->deferred_free_capacity) {
            int capacity = dk->deferred_free_capacity ? dk->deferred_free_capacity * 2 : 256;
            dk_deferred_free_t *grown = (dk_deferred_free_t *)realloc(
                dk->deferred_free, (size_t)capacity * sizeof(*grown));
            if (!grown) {
                /* No memory to remember the block: wait for the GPU and
                 * reuse it now rather than lose it. */
                SGL_ERROR_BACKEND("deferred free list: out of memory, waiting idle");
                dk_submit_and_reset(dk);
                dk_vbo_free_insert(dk, old_offset, old_size);
                old_size = 0;
            } else {
                dk->deferred_free = grown;
                dk->deferred_free_capacity = capacity;
            }
        }
    }
    if (old_offset != 0 && old_size != 0 && old_offset < dk->client_array_base) {
        dk->deferred_free[dk->deferred_free_count].offset = old_offset;
        dk->deferred_free[dk->deferred_free_count].size = old_size;
        dk->deferred_free[dk->deferred_free_count].slot = dk->current_slot;
        dk->deferred_free_count++;
    }

    /* Allocate new block from VBO region (free-list + bump).
     * Using VBO instead of client_array because dk_submit_and_reset
     * (called by glReadPixels) resets client_array_offset, which would
     * overwrite orphaned buffer data during subsequent draw staging. */
    uint32_t new_offset = dk_buffer_data(be, 0, GL_ARRAY_BUFFER, size, NULL, GL_DYNAMIC_DRAW);

    SGL_TRACE_BUFFER("buffer_data_orphan: size=%zu -> offset=%u (VBO, deferred_free=%d)",
                     (size_t)size, new_offset, dk->deferred_free_count);
    return new_offset;
}

/* ============================================================================
 * Buffer Sub-Data Update
 * ============================================================================ */

void dk_buffer_sub_data(sgl_backend_t *be, sgl_handle_t handle, uint32_t buffer_offset,
                        GLsizeiptr size, const void *data) {
    (void)handle;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (data && size > 0) {
        void *dst = (uint8_t *)dkMemBlockGetCpuAddr(dk->data_memblock) + buffer_offset;
        memcpy(dst, data, size);
        dk->vbo_data_dirty = true;
        dk->cpu_store_pending = true;
    }
}
