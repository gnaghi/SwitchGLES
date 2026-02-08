/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Core Implementation
 *
 * This is the main backend file containing:
 * - Backend creation and destruction
 * - Backend operations table
 * - Initialization and shutdown
 *
 * The implementation is split across multiple files for maintainability:
 * - dk_backend.c    - This file (core, ops table, lifecycle)
 * - dk_command.c    - Frame management, sync operations
 * - dk_state.c      - State application (viewport, blend, depth, etc.)
 * - dk_clear.c      - Clear operations
 * - dk_buffer.c     - Buffer operations
 * - dk_draw.c       - Draw operations, vertex attributes
 * - dk_uniform.c    - Uniform buffer operations
 * - dk_shader.c     - Shader loading, program linking/binding
 * - dk_texture.c    - Texture operations
 * - dk_framebuffer.c - Framebuffer operations, read pixels
 * - dk_utils.c      - Conversion helpers
 */

#include "dk_internal.h"

/* ============================================================================
 * Backend Operations Table
 *
 * This table maps abstract backend operations to deko3d implementations.
 * Functions are implemented in their respective module files.
 * ============================================================================ */

const sgl_backend_ops_t dk_backend_ops = {
    /* Lifecycle (dk_backend.c) */
    .init = dk_init,
    .shutdown = dk_shutdown,

    /* Frame Management (dk_command.c) */
    .begin_frame = dk_begin_frame,
    .end_frame = dk_end_frame,
    .present = dk_present,
    .acquire_image = dk_acquire_image,
    .wait_fence = dk_wait_fence,

    /* State Application (dk_state.c) */
    .apply_viewport = dk_apply_viewport,
    .apply_scissor = dk_apply_scissor,
    .apply_blend = dk_apply_blend,
    .apply_depth = dk_apply_depth,
    .apply_stencil = dk_apply_stencil,
    .apply_depth_stencil = dk_apply_depth_stencil,
    .apply_raster = dk_apply_raster,
    .apply_color_mask = dk_apply_color_mask,

    /* Clear Operations (dk_clear.c) */
    .clear = dk_clear,

    /* Buffer Operations (dk_buffer.c) */
    .create_buffer = dk_create_buffer,
    .delete_buffer = dk_delete_buffer,
    .buffer_data = dk_buffer_data,
    .buffer_sub_data = dk_buffer_sub_data,

    /* Texture Operations (dk_texture.c) */
    .create_texture = NULL,  /* Handled at GL layer */
    .delete_texture = NULL,  /* Handled at GL layer */
    .texture_image_2d = dk_texture_image_2d,
    .texture_sub_image_2d = dk_texture_sub_image_2d,
    .texture_parameter = dk_texture_parameter,
    .bind_texture = dk_bind_texture,
    .generate_mipmap = dk_generate_mipmap,
    .copy_tex_image_2d = dk_copy_tex_image_2d,
    .copy_tex_sub_image_2d = dk_copy_tex_sub_image_2d,
    .compressed_texture_image_2d = dk_compressed_texture_image_2d,
    .compressed_texture_sub_image_2d = dk_compressed_texture_sub_image_2d,

    /* Shader Operations (dk_shader.c) */
    .create_shader = NULL,   /* Handled at GL layer */
    .delete_shader = NULL,   /* Handled at GL layer */
    .load_shader_binary = dk_load_shader_binary,
    .load_shader_file = dk_load_shader_file,

    /* Program Operations (dk_shader.c) */
    .create_program = NULL,  /* Handled at GL layer */
    .delete_program = NULL,  /* Handled at GL layer */
    .attach_shader = NULL,   /* Handled at GL layer */
    .link_program = dk_link_program,
    .use_program = NULL,     /* Handled at GL layer */
    .bind_program = dk_bind_program,

    /* Uniform Operations (dk_uniform.c) */
    .set_uniform_4f = NULL,
    .set_uniform_matrix4fv = NULL,
    .alloc_uniform = dk_alloc_uniform,
    .write_uniform = dk_write_uniform,

    /* Vertex Attribute Operations (dk_draw.c) */
    .bind_vertex_attribs = dk_bind_vertex_attribs,

    /* Draw Operations (dk_draw.c) */
    .draw_arrays = dk_draw_arrays,
    .draw_elements = dk_draw_elements,

    /* Framebuffer Operations (dk_framebuffer.c) */
    .create_framebuffer = NULL,        /* Handled at GL layer */
    .delete_framebuffer = NULL,        /* Handled at GL layer */
    .bind_framebuffer = dk_bind_framebuffer,
    .framebuffer_texture = NULL,       /* Handled at GL layer */
    .check_framebuffer_status = NULL,  /* Handled at GL layer */

    /* Renderbuffer Operations (dk_framebuffer.c) */
    .renderbuffer_storage = dk_renderbuffer_storage,
    .delete_renderbuffer = dk_delete_renderbuffer,

    .read_pixels = dk_read_pixels,

    /* Sync Operations (dk_command.c) */
    .flush = dk_flush,
    .finish = dk_finish,
    .insert_barrier = dk_insert_barrier,

    /* Misc Operations (dk_state.c) */
    .set_line_width = NULL,
    .set_depth_bias = dk_set_depth_bias,
    .set_blend_color = NULL,
};

/* ============================================================================
 * Backend Creation and Destruction
 * ============================================================================ */

/**
 * Create a new deko3d backend instance.
 *
 * @param device    Deko3d device handle
 * @return Backend pointer, or NULL on failure
 */
sgl_backend_t *dk_backend_create(DkDevice device) {
    sgl_backend_t *be = (sgl_backend_t *)malloc(sizeof(sgl_backend_t));
    if (!be) return NULL;

    dk_backend_data_t *dk = (dk_backend_data_t *)malloc(sizeof(dk_backend_data_t));
    if (!dk) {
        free(be);
        return NULL;
    }

    memset(dk, 0, sizeof(dk_backend_data_t));
    dk->device = device;

    be->ops = &dk_backend_ops;
    be->impl_data = dk;

    SGL_TRACE_BACKEND("deko3d backend created");
    return be;
}

/**
 * Destroy a deko3d backend instance.
 *
 * @param be    Backend pointer
 */
void dk_backend_destroy(sgl_backend_t *be) {
    if (!be) return;

    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    if (dk) {
        dk_shutdown(be);
        free(dk);
    }

    free(be);
    SGL_TRACE_BACKEND("deko3d backend destroyed");
}

/**
 * Create backend for external use (called from sgl_backend.h interface).
 */
sgl_backend_t *sgl_backend_create_deko3d(void *device) {
    return dk_backend_create((DkDevice)device);
}

/**
 * Destroy backend (generic interface).
 */
void sgl_backend_destroy(sgl_backend_t *be) {
    dk_backend_destroy(be);
}

/* ============================================================================
 * Lifecycle Operations
 * ============================================================================ */

/**
 * Initialize the deko3d backend.
 *
 * Allocates GPU memory for:
 * - Command buffers (one per framebuffer slot for triple buffering)
 * - Shader code memory
 * - Data memory (vertices, indices, uniforms)
 * - Texture memory
 * - Descriptor memory (image and sampler descriptors)
 *
 * @param be        Backend pointer
 * @param device    Deko3d device handle (unused, already stored)
 * @return 0 on success, -1 on failure
 */
int dk_init(sgl_backend_t *be, void *device) {
    (void)device;  /* Device already stored in create */
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    printf("[DK] Initializing deko3d backend\n");
    fflush(stdout);

    /* Create GPU queue */
    DkQueueMaker queueMaker;
    dkQueueMakerDefaults(&queueMaker, dk->device);
    queueMaker.flags = DkQueueFlags_Graphics;
    dk->queue = dkQueueCreate(&queueMaker);
    if (!dk->queue) {
        SGL_ERROR_BACKEND("Failed to create GPU queue");
        return -1;
    }

    /* Create command buffer memory and command buffers for each slot */
    for (int i = 0; i < SGL_FB_NUM; i++) {
        DkMemBlockMaker memMaker;
        dkMemBlockMakerDefaults(&memMaker, dk->device, SGL_CMD_MEM_SIZE);
        memMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        dk->cmdbuf_memblock[i] = dkMemBlockCreate(&memMaker);
        if (!dk->cmdbuf_memblock[i]) {
            SGL_ERROR_BACKEND("Failed to create command buffer memory for slot %d", i);
            return -1;
        }

        DkCmdBufMaker cmdMaker;
        dkCmdBufMakerDefaults(&cmdMaker, dk->device);
        dk->cmdbufs[i] = dkCmdBufCreate(&cmdMaker);
        if (!dk->cmdbufs[i]) {
            SGL_ERROR_BACKEND("Failed to create command buffer for slot %d", i);
            return -1;
        }

        dkCmdBufAddMemory(dk->cmdbufs[i], dk->cmdbuf_memblock[i], 0, SGL_CMD_MEM_SIZE);
        dk->fence_active[i] = false;
    }

    /* Set initial command buffer */
    dk->cmdbuf = dk->cmdbufs[0];
    dk->current_cmdbuf = 0;
    dk->current_slot = 0;

    /* Create shader code memory */
    DkMemBlockMaker codeMaker;
    dkMemBlockMakerDefaults(&codeMaker, dk->device, SGL_CODE_MEM_SIZE);
    codeMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
    dk->code_memblock = dkMemBlockCreate(&codeMaker);
    if (!dk->code_memblock) {
        SGL_ERROR_BACKEND("Failed to create shader code memory");
        return -1;
    }
    dk->code_offset = 0;

    /* Create data memory (vertices, indices, uniforms)
     * Memory layout:
     * [0 ... client_array_base]: Static VBO/EBO data
     * [client_array_base ... uniform_base]: Per-frame client arrays
     * [uniform_base ... end]: Uniform buffers
     */
    DkMemBlockMaker dataMaker;
    dkMemBlockMakerDefaults(&dataMaker, dk->device, SGL_DATA_MEM_SIZE);
    dataMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    dk->data_memblock = dkMemBlockCreate(&dataMaker);
    if (!dk->data_memblock) {
        SGL_ERROR_BACKEND("Failed to create data memory");
        return -1;
    }
    dk->data_offset = 256;  /* Reserve offset 0 as error indicator */

    /* Reserve regions within data memory */
    dk->uniform_base = SGL_DATA_MEM_SIZE - SGL_UNIFORM_BUF_SIZE;
    dk->uniform_offset = 0;
    dk->client_array_base = dk->uniform_base - (4 * 1024 * 1024);  /* 4MB for client arrays */
    dk->client_array_offset = 0;
    dk->client_array_slot_end = dk->uniform_base - dk->client_array_base;  /* Full region initially */

    /* Create texture memory */
    DkMemBlockMaker texMaker;
    dkMemBlockMakerDefaults(&texMaker, dk->device, SGL_TEXTURE_MEM_SIZE);
    texMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    dk->texture_memblock = dkMemBlockCreate(&texMaker);
    if (!dk->texture_memblock) {
        SGL_ERROR_BACKEND("Failed to create texture memory");
        return -1;
    }
    dk->texture_offset = 0;

    /* Create descriptor memory */
    DkMemBlockMaker descMaker;
    dkMemBlockMakerDefaults(&descMaker, dk->device, SGL_DESCRIPTOR_MEM_SIZE);
    descMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    dk->descriptor_memblock = dkMemBlockCreate(&descMaker);
    if (!dk->descriptor_memblock) {
        SGL_ERROR_BACKEND("Failed to create descriptor memory");
        return -1;
    }

    /* Set up descriptor addresses
     * First half: image descriptors, second half: sampler descriptors */
    DkGpuAddr descBase = dkMemBlockGetGpuAddr(dk->descriptor_memblock);
    dk->image_descriptor_addr = descBase;
    dk->sampler_descriptor_addr = descBase + SGL_MAX_TEXTURES * sizeof(DkImageDescriptor);
    dk->descriptors_bound = false;

    /* Initialize texture tracking */
    memset(dk->texture_initialized, 0, sizeof(dk->texture_initialized));
    memset(dk->texture_is_cubemap, 0, sizeof(dk->texture_is_cubemap));
    memset(dk->texture_used_as_rt, 0, sizeof(dk->texture_used_as_rt));
    memset(dk->shader_loaded, 0, sizeof(dk->shader_loaded));
    memset(dk->program_shader_valid, 0, sizeof(dk->program_shader_valid));

    /* Initialize renderbuffer tracking */
    memset(dk->renderbuffer_memblocks, 0, sizeof(dk->renderbuffer_memblocks));
    memset(dk->renderbuffer_initialized, 0, sizeof(dk->renderbuffer_initialized));
    memset(dk->renderbuffer_width, 0, sizeof(dk->renderbuffer_width));
    memset(dk->renderbuffer_height, 0, sizeof(dk->renderbuffer_height));

    dk->state_initialized = true;

    printf("[DK] Backend initialized successfully\n");
    printf("[DK]   Queue: %p\n", (void*)dk->queue);
    printf("[DK]   Code memory: %lu KB\n", (unsigned long)(SGL_CODE_MEM_SIZE / 1024));
    printf("[DK]   Data memory: %lu MB\n", (unsigned long)(SGL_DATA_MEM_SIZE / (1024 * 1024)));
    printf("[DK]   Texture memory: %lu MB\n", (unsigned long)(SGL_TEXTURE_MEM_SIZE / (1024 * 1024)));
    printf("[DK]   Uniform region: offset=%u size=%u\n", dk->uniform_base, SGL_UNIFORM_BUF_SIZE);
    printf("[DK]   Client array region: offset=%u\n", dk->client_array_base);
    fflush(stdout);

    SGL_TRACE_BACKEND("deko3d backend initialized");
    return 0;
}

/**
 * Shutdown the deko3d backend.
 *
 * Releases all GPU memory and resources.
 *
 * @param be    Backend pointer
 */
void dk_shutdown(sgl_backend_t *be) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;
    if (!dk || !dk->state_initialized) return;

    printf("[DK] Shutting down deko3d backend\n");
    fflush(stdout);

    /* Wait for GPU to finish */
    if (dk->queue) {
        dkQueueWaitIdle(dk->queue);
    }

    /* Destroy command buffers and their memory */
    for (int i = 0; i < SGL_FB_NUM; i++) {
        if (dk->cmdbufs[i]) {
            dkCmdBufDestroy(dk->cmdbufs[i]);
            dk->cmdbufs[i] = NULL;
        }
        if (dk->cmdbuf_memblock[i]) {
            dkMemBlockDestroy(dk->cmdbuf_memblock[i]);
            dk->cmdbuf_memblock[i] = NULL;
        }
    }

    /* Destroy renderbuffer memory blocks */
    for (int i = 0; i < SGL_MAX_RENDERBUFFERS; i++) {
        if (dk->renderbuffer_memblocks[i]) {
            dkMemBlockDestroy(dk->renderbuffer_memblocks[i]);
            dk->renderbuffer_memblocks[i] = NULL;
        }
    }

    /* Destroy memory blocks */
    if (dk->descriptor_memblock) {
        dkMemBlockDestroy(dk->descriptor_memblock);
        dk->descriptor_memblock = NULL;
    }
    if (dk->texture_memblock) {
        dkMemBlockDestroy(dk->texture_memblock);
        dk->texture_memblock = NULL;
    }
    if (dk->data_memblock) {
        dkMemBlockDestroy(dk->data_memblock);
        dk->data_memblock = NULL;
    }
    if (dk->code_memblock) {
        dkMemBlockDestroy(dk->code_memblock);
        dk->code_memblock = NULL;
    }

    /* Destroy queue */
    if (dk->queue) {
        dkQueueDestroy(dk->queue);
        dk->queue = NULL;
    }

    dk->state_initialized = false;

    printf("[DK] Backend shutdown complete\n");
    fflush(stdout);

    SGL_TRACE_BACKEND("deko3d backend shutdown");
}
