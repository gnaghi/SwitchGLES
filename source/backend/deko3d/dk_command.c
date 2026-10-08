/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Frame and Command Management
 *
 * This module handles:
 * - Frame lifecycle (begin, end, present)
 * - Swapchain image acquisition
 * - Fence synchronization
 * - Flush/finish operations
 * - Pipeline barriers
 */

#include "dk_internal.h"
#include "../../util/sgl_perf.h"

/* ============================================================================
 * Command Buffer Overflow Callback
 *
 * Safety net: called by deko3d when the cmdbuf runs out of memory during
 * command recording. Submits pending work, waits, and recycles the memory.
 * Under normal operation the pre-draw threshold in dk_apply_viewport should
 * prevent this from ever firing; this handles edge cases (very large state
 * or unexpected command sizes).
 * ============================================================================ */

void dk_cmdbuf_overflow_cb(void *userData, DkCmdBuf cmdbuf, size_t minReqSize) {
    dk_backend_data_t *dk = (dk_backend_data_t *)userData;

    /* Re-entrancy guard: dkCmdBufFinishList itself may need a few bytes,
     * triggering this callback recursively when the cmdbuf is completely full.
     * In the recursive case, just clear and re-add memory (pending commands
     * will be lost, but it prevents an infinite loop / stack overflow). */
    if (dk->in_overflow_callback) {
        SGL_TRACE_BACKEND("cbAddMem: re-entrant overflow — emergency clear");
        dkCmdBufClear(cmdbuf);
        dkCmdBufAddMemory(cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
        return;
    }
    dk->in_overflow_callback = true;

    SGL_TRACE_BACKEND("cbAddMem: cmdbuf overflow (need %zu bytes, draws=%u) — flushing", minReqSize,
                      dk->draws_since_flush);

    /* Submit whatever commands have been recorded so far */
    DkCmdList cmdlist = dkCmdBufFinishList(cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Recycle the same memory block */
    dkCmdBufClear(cmdbuf);
    dkCmdBufAddMemory(cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Reset client array and uniform allocators */
    {
        uint32_t total_client_size = dk->uniform_base - dk->client_array_base;
        uint32_t per_slot_size = total_client_size / SGL_FB_NUM;
        dk->client_array_offset = dk->current_slot * per_slot_size;
        dk->client_array_slot_end = (dk->current_slot + 1) * per_slot_size;
    }
    dk_reset_uniform_slot(dk, dk->current_slot);
    dk->draws_since_flush = 0;

    /* Re-bind essentials lost when cmdbuf was cleared */
    dkCmdBufBindImageDescriptorSet(cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    dk_rebind_render_target(dk);

    dk_barrier(cmdbuf, DkBarrier_None,
                    DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors |
                        DkInvalidateFlags_Zcull);

    dk->in_overflow_callback = false;
}

/* ============================================================================
 * Shared Helpers
 * ============================================================================ */

void dk_rebind_default_render_target(dk_backend_data_t *dk) {
    if (!dk->framebuffers)
        return;

    DkImageView colorView;
    dkImageViewDefaults(&colorView, &dk->framebuffers[dk->current_slot]);
    if (dk->depth_images[dk->current_slot]) {
        DkImageView depthView;
        dkImageViewDefaults(&depthView, dk->depth_images[dk->current_slot]);
        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, &depthView);
    } else {
        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, NULL);
    }
}

void dk_rebind_render_target(dk_backend_data_t *dk) {
    if (dk->current_fbo != 0 && dk->current_fbo_color > 0) {
        /* Barrier before switching render targets.
         * Without this, GPU caches (L2, Zcull) from the previous render target
         * can interfere with rendering to the new target — especially after
         * glTexImage2D / glRenderbufferStorage resize while attached to FBO. */
        dk_barrier(dk->cmdbuf, DkBarrier_Full,
                   DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors |
                       DkInvalidateFlags_Zcull);

        /* Use type flags to pick correct array (avoids renderbuffer/texture ID collision) */
        DkImageView colorView;
        bool color_found = false;
        if (dk->current_fbo_color_is_rb) {
            if (dk->current_fbo_color < SGL_MAX_RENDERBUFFERS &&
                dk->renderbuffer_initialized[dk->current_fbo_color]) {
                dkImageViewDefaults(&colorView, &dk->renderbuffer_images[dk->current_fbo_color]);
                color_found = true;
            }
        } else {
            if (dk->current_fbo_color < SGL_MAX_TEXTURES &&
                dk->texture_initialized[dk->current_fbo_color]) {
                dkImageViewDefaults(&colorView, &dk->textures[dk->current_fbo_color]);
                color_found = true;
            }
        }
        if (!color_found) {
            dk_rebind_default_render_target(dk);
            return;
        }

        DkImageView *pDepthView = NULL;
        DkImageView depthView;
        if (dk->current_fbo_depth > 0) {
            if (dk->current_fbo_depth_is_rb) {
                if (dk->current_fbo_depth < SGL_MAX_RENDERBUFFERS &&
                    dk->renderbuffer_initialized[dk->current_fbo_depth]) {
                    dkImageViewDefaults(&depthView,
                                        &dk->renderbuffer_images[dk->current_fbo_depth]);
                    pDepthView = &depthView;
                }
            } else {
                if (dk->current_fbo_depth < SGL_MAX_TEXTURES &&
                    dk->texture_initialized[dk->current_fbo_depth]) {
                    dkImageViewDefaults(&depthView, &dk->textures[dk->current_fbo_depth]);
                    pDepthView = &depthView;
                }
            }
        }
        /* If no depth, check for stencil-only (GL_STENCIL_INDEX8) */
        if (!pDepthView && dk->current_fbo_stencil > 0 && dk->current_fbo_stencil_is_rb) {
            if (dk->current_fbo_stencil < SGL_MAX_RENDERBUFFERS &&
                dk->renderbuffer_initialized[dk->current_fbo_stencil]) {
                dkImageViewDefaults(&depthView, &dk->renderbuffer_images[dk->current_fbo_stencil]);
                pDepthView = &depthView;
            }
        }
        dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, pDepthView);
    } else {
        dk_rebind_default_render_target(dk);
    }
}

/*
 * Synchronous flush: finish the current command list, submit it, and block
 * until the GPU is idle. The minimal building block used by the synchronous
 * texture/FBO upload and readback paths (dk_texture.c). Does not touch the
 * deferred VBO free list — see dk_submit_and_reset() for the frame path.
 */
void dk_reset_uniform_slot(dk_backend_data_t *dk, int slot) {
    uint32_t per_slot = dk_uniform_slot_size();
    dk->uniform_offset = (uint32_t)slot * per_slot;
    dk->uniform_slot_end = dk->uniform_offset + per_slot;
}

void dk_flush_sync(dk_backend_data_t *dk) {
    SGL_PERF_BEGIN(perf);
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);
    SGL_PERF_END(SGL_PERF_FLUSH_SYNC, perf);
}

/*
 * Return orphaned VBO blocks to the free list once the GPU no longer reads
 * them: those tagged with `slot` after that slot's fence wait, or all of them
 * (slot < 0) after WaitIdle.
 */
static void dk_drain_deferred_free(dk_backend_data_t *dk, int slot) {
    int kept = 0;
    for (int i = 0; i < dk->deferred_free_count; i++) {
        if (slot < 0 || dk->deferred_free[i].slot == slot) {
            dk_vbo_free_insert(dk, dk->deferred_free[i].offset, dk->deferred_free[i].size);
        } else {
            dk->deferred_free[kept++] = dk->deferred_free[i];
        }
    }
    dk->deferred_free_count = kept;
}

/*
 * Submit current command buffer, wait for GPU, and reset for continued use.
 * Shared implementation for dk_flush(), dk_finish(), and orphan overflow recovery.
 */
void dk_submit_and_reset(dk_backend_data_t *dk) {
    if (dkQueueIsInErrorState(dk->queue)) {
        return; /* Don't submit to an errored queue */
    }
    SGL_PERF_BEGIN(perf);

    dk_flush_sync(dk);

    /* GPU is idle after WaitIdle: every deferred VBO block can be reused. */
    dk_drain_deferred_free(dk, -1);

    /* Reset command buffer for continued use */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    /* Reset client array and uniform allocators — safe because WaitIdle
     * ensures all GPU work referencing old data has completed. */
    {
        uint32_t total_client_size = dk->uniform_base - dk->client_array_base;
        uint32_t per_slot_size = total_client_size / SGL_FB_NUM;
        dk->client_array_offset = dk->current_slot * per_slot_size;
        dk->client_array_slot_end = (dk->current_slot + 1) * per_slot_size;
    }
    dk_reset_uniform_slot(dk, dk->current_slot);

    /* Eagerly re-bind descriptor sets after cmdbuf clear (matches legacy pattern) */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    dk_rebind_render_target(dk);

    /* Reset mid-frame flush counter */
    dk->draws_since_flush = 0;

    /* Invalidate GPU L2 cache + TIC/TSC descriptor caches + Zcull after mid-frame reset.
     * L2: client array coherency (CpuUncached data rewritten by CPU).
     * Descriptors: force TIC/TSC to re-read from DRAM on next texture fetch.
     * Zcull: invalidate fast-depth metadata to prevent stale data from prior
     * depth operations causing GPU errors in subsequent depth clears/tests. */
    dk_barrier(dk->cmdbuf, DkBarrier_None,
                    DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors |
                        DkInvalidateFlags_Zcull);
    SGL_PERF_END(SGL_PERF_SUBMIT_RESET, perf);
}

/*
 * Bring dk->cmdbuf back to a clean, recordable state if a frame was already
 * submitted (eglSwapBuffers -> dk_end_frame) but not yet reset by the next
 * dk_wait_fence. Must be called before any synchronous mid-stream operation
 * that records into dk->cmdbuf and then calls dkCmdBufFinishList itself
 * (texture uploads, glReadPixels, blits). See header for rationale.
 */
void dk_ensure_recordable(dk_backend_data_t *dk) {
    if (!dk->cmdbuf_submitted) {
        return;
    }

    if (!dkQueueIsInErrorState(dk->queue)) {
        dkQueueWaitIdle(dk->queue);
    }

    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);

    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    dk_rebind_render_target(dk);

    dk->cmdbuf_submitted = false;
}

/* ============================================================================
 * Frame Management
 * ============================================================================ */

void dk_begin_frame(sgl_backend_t *be, int slot) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    dk->current_slot = slot;
    dk->cmdbuf = dk->cmdbufs[slot];
    dk->current_cmdbuf = slot;

    /* Reset diagnostic counters for new frame */
    dk->diag_orphan_flushes = 0;
    dk->diag_uniform_overflows = 0;
    dk->diag_draw_count = 0;
    dk->diag_texture_binds = 0;
    dk->draws_since_flush = 0;

    /* Reset client array allocator to this slot's sub-region.
     * The client array memory is partitioned per-slot to avoid GPU race conditions:
     * Without this, frame N+1 would overwrite frame N's vertex data in shared memory
     * before the GPU finishes rendering frame N (since vertex data is read directly
     * from shared memory, unlike uniforms which use pushConstants). */
    {
        uint32_t total_client_size = dk->uniform_base - dk->client_array_base;
        uint32_t per_slot_size = total_client_size / SGL_FB_NUM;
        dk->client_array_offset = slot * per_slot_size;
        dk->client_array_slot_end = (slot + 1) * per_slot_size;
    }

    /* Eagerly bind descriptor sets (matches legacy pattern: bind right after cmdbuf setup).
     * The legacy code binds descriptor sets at frame start, NOT lazily at first draw.
     * This ensures the GPU always has valid descriptor set base addresses. */
    dkCmdBufBindImageDescriptorSet(dk->cmdbuf, dk->image_descriptor_addr, SGL_MAX_TEXTURES);
    dkCmdBufBindSamplerDescriptorSet(dk->cmdbuf, dk->sampler_descriptor_addr, SGL_MAX_TEXTURES);
    dk->descriptors_bound = true;

    dk_rebind_default_render_target(dk);

    /* Invalidate GPU L2 cache + TIC/TSC descriptor caches + Zcull at frame start.
     * L2: The client_array region (CpuUncached | GpuCached) is reused every
     * SGL_FB_NUM frames; GPU L2 may have stale data from this slot's prior use.
     * Descriptors: Force TIC/TSC to re-read from DRAM. Descriptors are written
     * via CPU memcpy + DSB to DRAM; per-frame TIC/TSC invalidation ensures the
     * GPU picks up any descriptor changes (new textures, parameter updates).
     * Zcull: NVIDIA's fast-depth metadata persists in GPU SRAM across frames.
     * Stale Zcull data from a previous frame's depth operations can cause GPU
     * errors in the new frame's depth clears/tests. Must invalidate at frame
     * start to ensure clean depth state. */
    dk_barrier(dk->cmdbuf, DkBarrier_None,
                    DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors |
                        DkInvalidateFlags_Zcull);

    SGL_TRACE_BACKEND("begin_frame slot=%d", slot);
}

void dk_end_frame(sgl_backend_t *be, int slot) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Check GPU queue error state before submitting — prevents crash */
    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("end_frame: GPU queue in ERROR STATE — skipping submit for slot %d",
                          slot);
        dk->cmdbuf_submitted = true; /* Mark as submitted to prevent double-submit */
        return;
    }

    /* Print diagnostic counters if any anomalies detected */
    if (dk->diag_orphan_flushes > 0 || dk->diag_uniform_overflows > 0) {
        SGL_TRACE_BACKEND(
            "DIAG frame slot=%d: draws=%u tex_binds=%u ORPHAN_FLUSHES=%u UNIFORM_OVERFLOWS=%u",
            slot, dk->diag_draw_count, dk->diag_texture_binds, dk->diag_orphan_flushes,
            dk->diag_uniform_overflows);
    }

    /* Signal fence before finishing command list */
    dkCmdBufSignalFence(dk->cmdbufs[slot], &dk->fences[slot], false);
    dk->fence_active[slot] = true;

    /* Finish and submit command list */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbufs[slot]);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dk->cmdbuf_submitted = true;

    SGL_TRACE_BACKEND("end_frame slot=%d", slot);
}

void dk_present(sgl_backend_t *be, int slot) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Check GPU queue error state before presenting — prevents crash */
    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("present: GPU queue in ERROR STATE — skipping present for slot %d", slot);
        return;
    }

    dkQueuePresentImage(dk->queue, dk->swapchain, slot);

    SGL_TRACE_BACKEND("present slot=%d", slot);
}

int dk_acquire_image(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    int slot = dkQueueAcquireImage(dk->queue, dk->swapchain);

    SGL_TRACE_BACKEND("acquire_image -> slot=%d", slot);
    return slot;
}

void dk_wait_fence(sgl_backend_t *be, int slot) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    SGL_PERF_BEGIN(perf);
    if (dk->fence_active[slot]) {
        dkFenceWait(&dk->fences[slot], -1);
        dk->fence_active[slot] = false;
    }
    SGL_PERF_END(SGL_PERF_FRAME_START, perf);

    /* Reset command buffer for new frame */
    dkCmdBufClear(dk->cmdbufs[slot]);
    dkCmdBufAddMemory(dk->cmdbufs[slot], dk->cmdbuf_memblock[slot], 0, SGL_CMD_MEM_SIZE);

    /* Reset descriptors_bound flag since command buffer was cleared */
    dk->descriptors_bound = false;
    dk->cmdbuf_submitted = false;

    /* Restart this slot's uniform sub-region: the fence wait above guarantees
     * the GPU finished every draw that read it. */
    dk_reset_uniform_slot(dk, slot);

    /* Same guarantee for VBO blocks orphaned during that slot's frame. */
    dk_drain_deferred_free(dk, slot);

    SGL_TRACE_BACKEND("wait_fence slot=%d", slot);
}

/* ============================================================================
 * Sync Operations
 * ============================================================================ */

void dk_flush(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("flush: GPU queue in ERROR STATE — skipping");
        dk->cmdbuf_submitted = false;
        return;
    }

    /* After eglSwapBuffers the frame is already submitted and kicked off by
     * dkQueuePresentImage: nothing to do. cmdbuf_submitted stays set so that
     * dk_ensure_recordable keeps protecting the synchronous paths. */
    if (dk->cmdbuf_submitted) {
        SGL_TRACE_BACKEND("flush (frame already submitted)");
        return;
    }

    /* Asynchronous glFlush (GLES 2.0 §5.1): submit what is recorded and kick
     * the GPU, without waiting, clearing the cmdbuf or resetting allocators.
     * dkCmdBufFinishList does not rewind the command memory, so recording
     * continues after the submitted range. That memory, and the uniform and
     * client-array space it references, is only reused after the slot fence
     * (dk_wait_fence) or a WaitIdle (dk_submit_and_reset). Only glFinish
     * waits.
     *
     * The command memory, uniforms and client arrays therefore fill up across
     * every glFlush until the next swap: GFXBench off-screen flushes each
     * frame but swaps only every ~100 frames. Left alone, the cmdbuf ran out
     * mid-command (T-Rex: ~1.1 KB per draw, full after ~3500 draws, before
     * the 4000-draw threshold of dk_draw_*) and the overflow callback, which
     * submits, waits and rebinds state from inside a deko3d command, crashed
     * gl_trex_off (2 runs out of 3). Recycle everything here instead, at a
     * clean point between frames, once half of a budget is used: one wait
     * every ~10 T-Rex frames, the frames in between keep overlapping. */
    if (dk->draws_since_flush >= DK_FLUSH_RESET_DRAWS ||
        dk->uniform_slot_end - dk->uniform_offset < dk_uniform_slot_size() / 2 ||
        dk->client_array_slot_end - dk->client_array_offset < dk_client_array_slot_size(dk) / 2) {
        dk_submit_and_reset(dk);
        return;
    }
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    if (cmdlist) {
        dkQueueSubmitCommands(dk->queue, cmdlist);
        dkQueueFlush(dk->queue);
    }
    SGL_TRACE_BACKEND("flush (async)");
}

void dk_finish(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("finish: GPU queue in ERROR STATE — skipping");
        dk->cmdbuf_submitted = false;
        return;
    }

    if (dk->cmdbuf_submitted) {
        /* Already submitted by dk_end_frame — just wait idle,
         * do NOT call dkCmdBufFinishList again. */
        dkQueueWaitIdle(dk->queue);
        dk->cmdbuf_submitted = false;
        SGL_TRACE_BACKEND("finish (already submitted, waited idle)");
        return;
    }

    dk_submit_and_reset(dk);
    SGL_TRACE_BACKEND("finish");
}

