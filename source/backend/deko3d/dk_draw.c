/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Draw Operations
 *
 * This module handles:
 * - Vertex attribute binding (glVertexAttribPointer state)
 * - Draw arrays (glDrawArrays)
 * - Draw elements (glDrawElements)
 *
 * Vertex data handling:
 * - VBO path: Uses pre-uploaded GPU buffer data
 * - Client array path: Copies client memory to GPU staging area per-frame
 */

#include "dk_internal.h"

/* ============================================================================
 * Vertex Attribute Binding
 *
 * Configures vertex attribute formats and buffer bindings for the next draw.
 * Handles both VBO-based and client-side vertex arrays.
 * ============================================================================ */

void dk_bind_vertex_attribs(sgl_backend_t *be, const sgl_vertex_attrib_t *attribs,
                            int num_attribs, GLint first, GLsizei count) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DkVtxAttribState attribStates[SGL_MAX_ATTRIBS];
    DkVtxBufferState bufferStates[SGL_MAX_ATTRIBS];
    DkBufExtents bufferExtents[SGL_MAX_ATTRIBS];
    GLuint boundBuffers[SGL_MAX_ATTRIBS];
    int numAttribs = 0;
    int numBuffers = 0;

    DkGpuAddr data_gpu_base = dkMemBlockGetGpuAddr(dk->data_memblock);
    uint8_t *data_cpu_base = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock);

    /* Find highest enabled attribute to determine numAttribs */
    int maxAttribIdx = -1;
    for (int i = 0; i < num_attribs && i < SGL_MAX_ATTRIBS; i++) {
        if (attribs[i].enabled) {
            maxAttribIdx = i;
        }
    }

    if (maxAttribIdx < 0) {
        return;  /* No enabled attributes */
    }

    numAttribs = maxAttribIdx + 1;

    /* Initialize arrays to zero */
    memset(attribStates, 0, sizeof(attribStates));
    memset(bufferStates, 0, sizeof(bufferStates));
    memset(bufferExtents, 0, sizeof(bufferExtents));
    memset(boundBuffers, 0, sizeof(boundBuffers));

    /* Track base address for each buffer slot to compute relative offsets */
    DkGpuAddr bufferBaseAddrs[SGL_MAX_ATTRIBS];
    memset(bufferBaseAddrs, 0, sizeof(bufferBaseAddrs));

    /* Track original client pointers for computing offsets in interleaved data */
    uintptr_t bufferClientPtrs[SGL_MAX_ATTRIBS];
    memset(bufferClientPtrs, 0, sizeof(bufferClientPtrs));

    /* Build attribute and buffer states */
    for (int i = 0; i < numAttribs; i++) {
        const sgl_vertex_attrib_t *attr = &attribs[i];

        if (!attr->enabled) {
            /* Disabled attribute - use fixed value like legacy */
            attribStates[i].bufferId = 0;
            attribStates[i].isFixed = 1;
            attribStates[i].offset = 0;
            attribStates[i].size = DkVtxAttribSize_1x32;
            attribStates[i].type = DkVtxAttribType_Float;
            attribStates[i].isBgra = 0;
            continue;
        }

        /* Calculate effective stride (0 means tightly packed) */
        GLsizei effectiveStride = attr->stride;
        if (effectiveStride == 0) {
            effectiveStride = attr->size * dk_get_type_size(attr->type);
        }

        /*
         * For interleaved vertex data, multiple attributes share the same buffer
         * but with different pointer offsets. We need to:
         * 1. Use the buffer's BASE address (without pointer offset) for the extent
         * 2. Store the pointer offset in attribStates[i].offset
         *
         * For VBOs: buffer_offset = buf->data_offset + pointer
         *           base = buf->data_offset
         *           attr_offset = pointer
         *
         * For client arrays: we copy data starting from the pointer, so offset = 0
         */

        uint32_t attrOffset = 0;

        if (attr->buffer > 0) {
            /* VBO path: pointer value is the offset within the buffer */
            attrOffset = (uint32_t)(uintptr_t)attr->pointer;
        } else if (attr->pointer != NULL) {
            /* Client-side array - will copy to GPU, offset is 0 */
            attrOffset = 0;
        } else {
            continue;  /* Skip this attribute */
        }

        /* Find or add buffer to our list
         * Match by: buffer ID AND stride (attributes with same buffer & stride share a slot)
         * Different strides need different buffer states, even if same VBO
         *
         * For client-side arrays (buffer == 0), we also check if pointers are within
         * the same interleaved data region (pointer difference < stride). */
        int bufIdx = -1;
        for (int j = 0; j < numBuffers; j++) {
            if (boundBuffers[j] == attr->buffer &&
                bufferStates[j].stride == (uint32_t)effectiveStride) {
                /* For client-side arrays, check if this pointer is part of the same
                 * interleaved buffer (difference from base pointer < stride) */
                if (attr->buffer == 0 && bufferClientPtrs[j] != 0) {
                    uintptr_t basePtr = bufferClientPtrs[j];
                    uintptr_t thisPtr = (uintptr_t)attr->pointer;
                    /* Pointers should be within stride of each other for interleaved data */
                    if (thisPtr >= basePtr && (thisPtr - basePtr) < (uintptr_t)effectiveStride) {
                        bufIdx = j;
                        /* Compute offset for this attribute within the interleaved data */
                        attrOffset = (uint32_t)(thisPtr - basePtr);
                        break;
                    }
                } else {
                    bufIdx = j;
                    break;
                }
            }
        }

        if (bufIdx < 0) {
            /* New buffer - add it */
            bufIdx = numBuffers;
            boundBuffers[numBuffers] = attr->buffer;

            bufferStates[numBuffers].stride = effectiveStride;
            bufferStates[numBuffers].divisor = 0;

            /* Buffer extent - GPU address and size */
            if (attr->buffer > 0) {
                /* VBO path - use BASE address (data_offset only, not including pointer) */
                /* buffer_offset includes pointer, so subtract it to get base */
                uint32_t baseOffset = attr->buffer_offset - (uint32_t)(uintptr_t)attr->pointer;
                bufferExtents[numBuffers].addr = data_gpu_base + baseOffset;
                bufferBaseAddrs[numBuffers] = bufferExtents[numBuffers].addr;
                /* Size estimate based on count + max offset */
                bufferExtents[numBuffers].size = (first + count) * effectiveStride + attrOffset;
            } else if (attr->pointer != NULL) {
                /*
                 * Client-side vertex array - copy data to GPU memory.
                 * Use bump allocator that persists until frame end.
                 */
                GLsizei totalVertices = first + count;
                GLsizei dataSize = totalVertices * effectiveStride;

                /* Align current offset to 256 bytes */
                uint32_t alignedOffset = (dk->client_array_offset + 255) & ~255;
                uint32_t clientArrayAddr = dk->client_array_base + alignedOffset;

                /* Check we have space in this slot's sub-region */
                if (alignedOffset + dataSize <= dk->client_array_slot_end) {
                    /* Copy vertex data from client memory to GPU memory */
                    void *dst = data_cpu_base + clientArrayAddr;
                    memcpy(dst, attr->pointer, dataSize);

                    bufferExtents[numBuffers].addr = data_gpu_base + clientArrayAddr;
                    bufferBaseAddrs[numBuffers] = bufferExtents[numBuffers].addr;
                    bufferExtents[numBuffers].size = dataSize;

                    /* Store client pointer for computing offsets in interleaved data */
                    bufferClientPtrs[numBuffers] = (uintptr_t)attr->pointer;

                    /* Advance bump allocator */
                    dk->client_array_offset = alignedOffset + dataSize;
                } else {
                    SGL_ERROR_BACKEND("bind_vertex_attribs: out of client array memory");
                }
            }

            numBuffers++;
        }

        /* Get deko3d format */
        DkVtxAttribSize attrSize;
        DkVtxAttribType attrType;
        dk_get_attrib_format(attr->type, attr->size, attr->normalized, &attrSize, &attrType);

        /* Attribute state */
        attribStates[i].bufferId = (uint32_t)bufIdx;
        attribStates[i].isFixed = 0;
        /* Use the pointer value as the offset within the buffer */
        attribStates[i].offset = attrOffset;
        attribStates[i].size = attrSize;
        attribStates[i].type = attrType;
        attribStates[i].isBgra = 0;
    }

    if (numBuffers == 0) {
        return;  /* No valid buffers */
    }

    /* Bind in same order as legacy:
     * 1. dkCmdBufBindVtxAttribState - attribute format descriptions
     * 2. dkCmdBufBindVtxBufferState - buffer stride/divisor
     * 3. dkCmdBufBindVtxBuffers - actual GPU addresses (plural!)
     */
    DK_VERBOSE_PRINT("[DK] bind_vertex_attribs: numAttribs=%d numBuffers=%d\n", numAttribs, numBuffers);

    dkCmdBufBindVtxAttribState(dk->cmdbuf, attribStates, numAttribs);
    dkCmdBufBindVtxBufferState(dk->cmdbuf, bufferStates, numBuffers);
    dkCmdBufBindVtxBuffers(dk->cmdbuf, 0, bufferExtents, numBuffers);

    SGL_TRACE_DRAW("bind_vertex_attribs numAttribs=%d numBuffers=%d first=%d count=%d",
                   numAttribs, numBuffers, first, count);
}

/* ============================================================================
 * Draw Arrays
 * ============================================================================ */

void dk_draw_arrays(sgl_backend_t *be, GLenum mode, GLint first, GLsizei count) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (count <= 0) {
        return;
    }

    DkPrimitive prim = dk_convert_primitive(mode);

    dkCmdBufDraw(dk->cmdbuf, prim, count, 1, first, 0);

    /* Insert barrier after FBO draws to ensure proper synchronization */
    if (dk->current_fbo != 0) {
        dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full, 0);
    }

    DK_VERBOSE_PRINT("[DK] draw_arrays: mode=0x%X first=%d count=%d\n", mode, first, count);
    SGL_TRACE_DRAW("draw_arrays mode=0x%X first=%d count=%d", mode, first, count);
}

/* ============================================================================
 * Draw Elements
 * ============================================================================ */

void dk_draw_elements(sgl_backend_t *be, GLenum mode, GLsizei count,
                      GLenum type, const void *indices, sgl_handle_t ebo) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    if (count <= 0) {
        return;
    }

    DkPrimitive prim = dk_convert_primitive(mode);

    /* Determine index format and size */
    DkIdxFormat idxFormat;
    size_t idxSize;
    switch (type) {
        case GL_UNSIGNED_BYTE:
            /* DkIdxFormat_Uint8 is NOT supported by Maxwell GPU!
             * Must convert to 16-bit indices. */
            idxFormat = DkIdxFormat_Uint16;
            idxSize = 2;  /* Will convert */
            break;
        case GL_UNSIGNED_SHORT:
            idxFormat = DkIdxFormat_Uint16;
            idxSize = 2;
            break;
        case GL_UNSIGNED_INT:
            idxFormat = DkIdxFormat_Uint32;
            idxSize = 4;
            break;
        default:
            SGL_ERROR_BACKEND("draw_elements: unsupported index type 0x%X", type);
            return;
    }

    DkGpuAddr idxAddr;

    if (ebo != 0) {
        /* EBO bound - indices pointer is actually an offset */
        DkGpuAddr data_gpu_base = dkMemBlockGetGpuAddr(dk->data_memblock);
        uintptr_t offset = (uintptr_t)indices;
        idxAddr = data_gpu_base + (uint32_t)ebo + (uint32_t)offset;
    } else {
        /* Client-side indices - copy to GPU staging area */
        uint8_t *cpu_base = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock);
        DkGpuAddr gpu_base = dkMemBlockGetGpuAddr(dk->data_memblock);

        /* Align offset */
        uint32_t alignedOffset = (dk->client_array_offset + 255) & ~255;
        uint32_t clientAddr = dk->client_array_base + alignedOffset;

        if (type == GL_UNSIGNED_BYTE) {
            /* Convert 8-bit indices to 16-bit */
            size_t size16 = count * 2;
            if (alignedOffset + size16 > dk->client_array_slot_end) {
                SGL_ERROR_BACKEND("draw_elements: out of client array memory for index conversion");
                return;
            }

            uint16_t *dst = (uint16_t*)(cpu_base + clientAddr);
            const uint8_t *src = (const uint8_t*)indices;
            for (GLsizei i = 0; i < count; i++) {
                dst[i] = src[i];
            }

            dk->client_array_offset = alignedOffset + size16;
            idxAddr = gpu_base + clientAddr;
        } else {
            /* Direct copy for 16-bit and 32-bit indices */
            size_t dataSize = count * idxSize;
            if (alignedOffset + dataSize > dk->client_array_slot_end) {
                SGL_ERROR_BACKEND("draw_elements: out of client array memory");
                return;
            }

            memcpy(cpu_base + clientAddr, indices, dataSize);
            dk->client_array_offset = alignedOffset + dataSize;
            idxAddr = gpu_base + clientAddr;
        }
    }

    /* Bind index buffer and draw */
    dkCmdBufBindIdxBuffer(dk->cmdbuf, idxFormat, idxAddr);
    dkCmdBufDrawIndexed(dk->cmdbuf, prim, count, 1, 0, 0, 0);

    DK_VERBOSE_PRINT("[DK] draw_elements: mode=0x%X count=%d type=0x%X ebo=%u\n",
                     mode, count, type, ebo);
    SGL_TRACE_DRAW("draw_elements mode=0x%X count=%d type=0x%X ebo=%u",
                   mode, count, type, ebo);
}
