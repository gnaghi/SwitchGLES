/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * deko3d Backend - Shader Operations
 *
 * This module handles:
 * - Shader loading from .dksh files
 * - Program linking (copies shaders to per-program storage)
 * - Program binding for rendering
 *
 * Shader storage strategy:
 * - Shaders are loaded into temporary storage indexed by shader handle
 * - At link time, shaders are COPIED to per-program storage
 * - This allows independent program binding without shared shader state
 * - Original shaders can be deleted after linking
 *
 * Uniform binding:
 * - Uses pushConstants to capture uniform data at draw time
 * - Prevents race conditions when multiple draws use different uniform values
 */

#include "dk_internal.h"

/* ============================================================================
 * Shader Loading
 *
 * Loads a pre-compiled .dksh shader file into GPU code memory.
 * The shader code is stored at the handle index for later linking.
 * ============================================================================ */

bool dk_load_shader_file(sgl_backend_t *be, sgl_handle_t handle, const char *path) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Validate handle */
    if (handle == 0 || handle >= SGL_MAX_SHADERS) {
        SGL_ERROR_BACKEND("Invalid shader handle: %u", handle);
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        SGL_ERROR_BACKEND("Failed to open shader: %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || (uint32_t)size > SGL_CODE_MEM_SIZE - dk->code_offset) {
        fclose(f);
        SGL_ERROR_BACKEND("Shader too large: %s", path);
        return false;
    }

    /* Align code offset to 256 bytes */
    uint32_t aligned_offset = (dk->code_offset + 255) & ~255;
    uint8_t *code_ptr = (uint8_t*)dkMemBlockGetCpuAddr(dk->code_memblock) + aligned_offset;

    if (fread(code_ptr, 1, size, f) != (size_t)size) {
        fclose(f);
        SGL_ERROR_BACKEND("Failed to read shader: %s", path);
        return false;
    }
    fclose(f);

    DkGpuAddr code_addr = dkMemBlockGetGpuAddr(dk->code_memblock) + aligned_offset;

    /* Initialize shader at the handle index */
    DkShaderMaker shaderMaker;
    dkShaderMakerDefaults(&shaderMaker, dk->code_memblock, aligned_offset);
    dkShaderInitialize(&dk->dk_shaders[handle], &shaderMaker);
    dk->shader_loaded[handle] = true;
    dk->code_offset = aligned_offset + ((size + 255) & ~255);

    SGL_TRACE_SHADER("load_shader_file: handle=%u path=%s at 0x%lx",
                     handle, path, (unsigned long)code_addr);
    return true;
}

/* ============================================================================
 * Program Linking
 *
 * Links vertex and fragment shaders into a program.
 * COPIES shader data to per-program storage for independent binding.
 * ============================================================================ */

bool dk_link_program(sgl_backend_t *be, sgl_handle_t program,
                     sgl_handle_t vertex_shader, sgl_handle_t fragment_shader) {
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    /* Validate handles */
    if (program == 0 || program >= SGL_MAX_PROGRAMS) {
        SGL_ERROR_BACKEND("Invalid program handle: %u", program);
        return false;
    }

    /* Initialize program shader slots as invalid */
    dk->program_shader_valid[program][0] = false;
    dk->program_shader_valid[program][1] = false;

    /* Copy vertex shader to program storage */
    if (vertex_shader > 0 && vertex_shader < SGL_MAX_SHADERS && dk->shader_loaded[vertex_shader]) {
        /* Copy the entire DkShader structure */
        memcpy(&dk->program_shaders[program][0], &dk->dk_shaders[vertex_shader], sizeof(DkShader));
        dk->program_shader_valid[program][0] = true;
    }

    /* Copy fragment shader to program storage */
    if (fragment_shader > 0 && fragment_shader < SGL_MAX_SHADERS && dk->shader_loaded[fragment_shader]) {
        /* Copy the entire DkShader structure */
        memcpy(&dk->program_shaders[program][1], &dk->dk_shaders[fragment_shader], sizeof(DkShader));
        dk->program_shader_valid[program][1] = true;
    }

    SGL_TRACE_SHADER("link_program prog=%u vs=%u fs=%u", program, vertex_shader, fragment_shader);
    return true;
}

/* ============================================================================
 * Program Binding
 *
 * Binds a program for rendering:
 * - Binds per-program shader copies
 * - Binds uniform buffers
 * - Uses pushConstants to capture uniform data NOW (prevents race conditions)
 * ============================================================================ */

void dk_bind_program(sgl_backend_t *be, sgl_handle_t program,
                     sgl_handle_t vertex_shader, sgl_handle_t fragment_shader,
                     const sgl_uniform_binding_t *vertex_uniforms,
                     const sgl_uniform_binding_t *fragment_uniforms,
                     int max_uniforms) {
    (void)vertex_shader;  /* Not used - we use per-program shader copies */
    (void)fragment_shader;
    dk_backend_data_t *dk = (dk_backend_data_t *)be->impl_data;

    DK_VERBOSE_PRINT("[DK] bind_program: prog=%u\n", program);

    if (program == 0 || program >= SGL_MAX_PROGRAMS) {
        SGL_ERROR_BACKEND("bind_program: Invalid program handle %u", program);
        return;
    }

    /* Bind shaders using per-program copies (captured at link time) */
    DkShader const* shaders[2];
    int numShaders = 0;

    if (dk->program_shader_valid[program][0]) {
        shaders[numShaders++] = &dk->program_shaders[program][0];
    }
    if (dk->program_shader_valid[program][1]) {
        shaders[numShaders++] = &dk->program_shaders[program][1];
    }

    if (numShaders > 0) {
        dkCmdBufBindShaders(dk->cmdbuf, DkStageFlag_GraphicsMask, shaders, numShaders);
    }

    DkGpuAddr uniform_gpu_base = dkMemBlockGetGpuAddr(dk->data_memblock) + dk->uniform_base;

    /* Bind vertex stage uniforms with pushConstants */
    for (int i = 0; i < max_uniforms; i++) {
        const sgl_uniform_binding_t *ub = &vertex_uniforms[i];
        if (ub->valid && ub->size > 0) {
            DkGpuAddr gpu_addr = uniform_gpu_base + ub->offset;
            dkCmdBufBindUniformBuffer(dk->cmdbuf, DkStage_Vertex, i, gpu_addr, ub->size);

            /* CRITICAL: pushConstants captures data NOW, not at GPU execution time
             * This prevents race conditions when multiple draws use different uniform values
             * in the same command buffer.
             * Use data_size (actual data) not size (256-byte aligned) to avoid reading garbage. */
            uint8_t *cpu_addr = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock);
            void *uniform_data = cpu_addr + dk->uniform_base + ub->offset;
            uint32_t push_size = ub->data_size > 0 ? ub->data_size : ub->size;
            dkCmdBufPushConstants(dk->cmdbuf, gpu_addr, ub->size, 0, push_size, uniform_data);
        }
    }

    /* Bind fragment stage uniforms with pushConstants */
    for (int i = 0; i < max_uniforms; i++) {
        const sgl_uniform_binding_t *ub = &fragment_uniforms[i];
        if (ub->valid && ub->size > 0) {
            DkGpuAddr gpu_addr = uniform_gpu_base + ub->offset;
            dkCmdBufBindUniformBuffer(dk->cmdbuf, DkStage_Fragment, i, gpu_addr, ub->size);

            /* CRITICAL: pushConstants captures data NOW
             * Use data_size (actual data) not size (256-byte aligned) to avoid reading garbage. */
            uint8_t *cpu_addr = (uint8_t*)dkMemBlockGetCpuAddr(dk->data_memblock);
            void *uniform_data = cpu_addr + dk->uniform_base + ub->offset;
            uint32_t push_size = ub->data_size > 0 ? ub->data_size : ub->size;

            dkCmdBufPushConstants(dk->cmdbuf, gpu_addr, ub->size, 0, push_size, uniform_data);
        }
    }

    SGL_TRACE_SHADER("bind_program with uniforms");
}
