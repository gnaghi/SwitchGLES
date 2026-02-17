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

/* ============================================================================
 * Frame Management
 * ============================================================================ */

void dk_begin_frame(sgl_backend_t *be, int slot) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    dk->current_slot = slot;
    dk->cmdbuf = dk->cmdbufs[slot];
    dk->current_cmdbuf = slot;

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

    /* Bind render target - use per-slot depth buffer */
    if (dk->framebuffers) {
        DkImageView colorView, depthView;
        dkImageViewDefaults(&colorView, &dk->framebuffers[slot]);
        if (dk->depth_images[slot]) {
            dkImageViewDefaults(&depthView, dk->depth_images[slot]);
            dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, &depthView);
        } else {
            dkCmdBufBindRenderTarget(dk->cmdbuf, &colorView, NULL);
        }
    }

    SGL_TRACE_BACKEND("begin_frame slot=%d", slot);
}

void dk_end_frame(sgl_backend_t *be, int slot) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Check GPU queue error state before submitting — prevents crash */
    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("end_frame: GPU queue in ERROR STATE — skipping submit for slot %d", slot);
        dk->cmdbuf_submitted = true;  /* Mark as submitted to prevent double-submit */
        return;
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

    if (dk->fence_active[slot]) {
        dkFenceWait(&dk->fences[slot], -1);
        dk->fence_active[slot] = false;
    }

    /* Reset command buffer for new frame */
    dkCmdBufClear(dk->cmdbufs[slot]);
    dkCmdBufAddMemory(dk->cmdbufs[slot], dk->cmdbuf_memblock[slot], 0, SGL_CMD_MEM_SIZE);

    /* Reset descriptors_bound flag since command buffer was cleared */
    dk->descriptors_bound = false;
    dk->cmdbuf_submitted = false;

    /* Reset uniform allocator for new frame.
     * This is safe because pushConstants copied uniform data into the command buffer
     * at record time, so the GPU no longer references the CPU uniform memory. */
    dk->uniform_offset = 0;

    SGL_TRACE_BACKEND("wait_fence slot=%d", slot);
}

/* ============================================================================
 * Sync Operations
 * ============================================================================ */

void dk_flush(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Check GPU queue error state before any operation */
    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("flush: GPU queue in ERROR STATE — skipping");
        dk->cmdbuf_submitted = false;
        return;
    }

    if (dk->cmdbuf_submitted) {
        /* Already submitted by dk_end_frame — just wait idle */
        dkQueueWaitIdle(dk->queue);
        dk->cmdbuf_submitted = false;
        SGL_TRACE_BACKEND("flush (already submitted, waited idle)");
        return;
    }

    /* Submit and wait for GPU to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Reset command buffer for continued use */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dk->descriptors_bound = false;

    /* Re-bind render target - use per-slot depth buffer */
    if (dk->framebuffers) {
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

    SGL_TRACE_BACKEND("flush");
}

void dk_finish(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Check GPU queue error state before any operation */
    if (dkQueueIsInErrorState(dk->queue)) {
        SGL_ERROR_BACKEND("finish: GPU queue in ERROR STATE — skipping");
        dk->cmdbuf_submitted = false;
        return;
    }

    if (dk->cmdbuf_submitted) {
        /* Cmdbuf was already finished and submitted by dk_end_frame (eglSwapBuffers).
         * Just wait for the queue to become idle — do NOT call dkCmdBufFinishList again. */
        dkQueueWaitIdle(dk->queue);
        dk->cmdbuf_submitted = false;
        SGL_TRACE_BACKEND("finish (already submitted, waited idle)");
        return;
    }

    /* Submit and wait for GPU to complete */
    DkCmdList cmdlist = dkCmdBufFinishList(dk->cmdbuf);
    dkQueueSubmitCommands(dk->queue, cmdlist);
    dkQueueWaitIdle(dk->queue);

    /* Reset command buffer for continued use */
    dkCmdBufClear(dk->cmdbuf);
    dkCmdBufAddMemory(dk->cmdbuf, dk->cmdbuf_memblock[dk->current_slot], 0, SGL_CMD_MEM_SIZE);
    dk->descriptors_bound = false;

    /* Re-bind render target - use per-slot depth buffer */
    if (dk->framebuffers) {
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

    SGL_TRACE_BACKEND("finish");
}

void dk_insert_barrier(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);

    SGL_TRACE_BACKEND("insert_barrier");
}
