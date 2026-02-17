/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Uniform Functions
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

/*
 * ============================================================================
 * UNIFORM REGISTRATION SYSTEM
 *
 * Allows end users to register their own uniform name -> binding mappings
 * without modifying the library source code.
 * ============================================================================
 */

#define SGL_MAX_REGISTERED_UNIFORMS 256
#define SGL_UNIFORM_NAME_MAX 64

typedef struct {
    char name[SGL_UNIFORM_NAME_MAX];
    int stage;       /* 0 = vertex, 1 = fragment */
    int binding;
    int byte_offset; /* -1 for legacy, >=0 for packed */
    bool used;
} sgl_uniform_entry_t;

static sgl_uniform_entry_t s_registered_uniforms[SGL_MAX_REGISTERED_UNIFORMS];
static int s_registered_count = 0;
static bool s_registry_initialized = false;

/* Packed UBO size registry (set via sglSetPackedUBOSize, applied to programs at glGetUniformLocation time) */
static int s_packed_ubo_sizes[2][SGL_MAX_PACKED_UBOS]; /* [stage][binding] = size in bytes */

/*
 * sglRegisterUniform - Register a uniform name to a specific shader binding
 */
GL_APICALL GLboolean GL_APIENTRY sglRegisterUniform(const GLchar *name, GLint stage, GLint binding) {
    if (!name || stage < 0 || stage > 1 || binding < 0 || binding >= SGL_MAX_UNIFORMS) {
        return GL_FALSE;
    }

    size_t len = strlen(name);
    if (len == 0 || len >= SGL_UNIFORM_NAME_MAX) {
        return GL_FALSE;
    }

    /* Check if already registered - update if so */
    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used &&
            strcmp(s_registered_uniforms[i].name, name) == 0) {
            s_registered_uniforms[i].stage = stage;
            s_registered_uniforms[i].binding = binding;
            s_registered_uniforms[i].byte_offset = -1; /* legacy mode */
            return GL_TRUE;
        }
    }

    /* Find empty slot or add new entry */
    int slot = -1;
    for (int i = 0; i < s_registered_count; i++) {
        if (!s_registered_uniforms[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (s_registered_count >= SGL_MAX_REGISTERED_UNIFORMS) {
            return GL_FALSE;
        }
        slot = s_registered_count++;
    }

    strcpy(s_registered_uniforms[slot].name, name);
    s_registered_uniforms[slot].stage = stage;
    s_registered_uniforms[slot].binding = binding;
    s_registered_uniforms[slot].byte_offset = -1; /* legacy mode */
    s_registered_uniforms[slot].used = true;
    s_registry_initialized = true;

    return GL_TRUE;
}

/*
 * sglClearUniformRegistry - Clear all user-registered uniform mappings
 */
GL_APICALL void GL_APIENTRY sglClearUniformRegistry(void) {
    for (int i = 0; i < s_registered_count; i++) {
        s_registered_uniforms[i].used = false;
    }
    s_registered_count = 0;
    /* Clear packed UBO sizes */
    memset(s_packed_ubo_sizes, 0, sizeof(s_packed_ubo_sizes));
    /* Note: built-in mappings are still available via hardcoded checks */
}

/*
 * sglSetPackedUBOSize - Set the total size of a packed UBO binding
 */
GL_APICALL void GL_APIENTRY sglSetPackedUBOSize(GLint stage, GLint binding, GLint size) {
    if (stage < 0 || stage > 1) return;
    if (binding < 0 || binding >= SGL_MAX_PACKED_UBOS) return;
    if (size < 0 || size > SGL_MAX_PACKED_UBO_SIZE) return;
    s_packed_ubo_sizes[stage][binding] = size;
}

/*
 * sglRegisterPackedUniform - Register a uniform into a packed UBO
 */
GL_APICALL GLboolean GL_APIENTRY sglRegisterPackedUniform(const GLchar *name,
                                                           GLint stage,
                                                           GLint binding,
                                                           GLint byte_offset) {
    if (!name || stage < 0 || stage > 1) return GL_FALSE;
    if (binding < 0 || binding >= SGL_MAX_PACKED_UBOS) return GL_FALSE;
    if (byte_offset < 0 || byte_offset >= SGL_MAX_PACKED_UBO_SIZE) return GL_FALSE;

    size_t len = strlen(name);
    if (len == 0 || len >= SGL_UNIFORM_NAME_MAX) return GL_FALSE;

    /* Check if already registered - update if so */
    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used &&
            strcmp(s_registered_uniforms[i].name, name) == 0) {
            s_registered_uniforms[i].stage = stage;
            s_registered_uniforms[i].binding = binding;
            s_registered_uniforms[i].byte_offset = byte_offset;
            return GL_TRUE;
        }
    }

    /* Find empty slot or add new entry */
    int slot = -1;
    for (int i = 0; i < s_registered_count; i++) {
        if (!s_registered_uniforms[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (s_registered_count >= SGL_MAX_REGISTERED_UNIFORMS) return GL_FALSE;
        slot = s_registered_count++;
    }

    strcpy(s_registered_uniforms[slot].name, name);
    s_registered_uniforms[slot].stage = stage;
    s_registered_uniforms[slot].binding = binding;
    s_registered_uniforms[slot].byte_offset = byte_offset;
    s_registered_uniforms[slot].used = true;
    s_registry_initialized = true;

    return GL_TRUE;
}

/*
 * Check user-registered uniforms first.
 * Returns legacy encoding (stage << 16 | binding) or
 * packed encoding (1 << 31 | stage << 24 | binding << 16 | byte_offset).
 */
static GLint lookup_registered_uniform(const GLchar *name) {
    if (!s_registry_initialized) return -1;

    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used &&
            strcmp(s_registered_uniforms[i].name, name) == 0) {
            if (s_registered_uniforms[i].byte_offset >= 0) {
                /* Packed mode encoding */
                return (GLint)((1u << 31) |
                       ((unsigned)s_registered_uniforms[i].stage << 24) |
                       ((unsigned)s_registered_uniforms[i].binding << 16) |
                       (unsigned)s_registered_uniforms[i].byte_offset);
            } else {
                /* Legacy encoding */
                return (s_registered_uniforms[i].stage << 16) | s_registered_uniforms[i].binding;
            }
        }
    }
    return -1;
}

GL_APICALL GLint GL_APIENTRY glGetUniformLocation(GLuint program, const GLchar *name) {
    GET_CTX_RET(-1);

    if (program == 0 || !name) return -1;

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog || !prog->linked) return -1;

    /*
     * For pre-compiled deko3d shaders with std140 uniform blocks:
     * Location = (stage << 16) | binding
     * Stage: 0 = vertex, 1 = fragment
     */

    /* Check user-registered uniforms FIRST (allows overriding built-ins) */
    GLint registered = lookup_registered_uniform(name);
    if (registered != -1) {
        /* If packed mode, configure the program's packed UBO size */
        if (registered & (1 << 31)) {
            int stage = (registered >> 24) & 0x7F;
            int binding = (registered >> 16) & 0xFF;
            sgl_packed_ubo_t *packed = (stage == 0)
                ? &prog->packed_vertex[binding]
                : &prog->packed_fragment[binding];
            if (!packed->valid) {
                int ubo_size = s_packed_ubo_sizes[stage][binding];
                if (ubo_size > 0 && ubo_size <= SGL_MAX_PACKED_UBO_SIZE) {
                    packed->size = ubo_size;
                    packed->valid = true;
                    packed->dirty = false;
                    memset(packed->data, 0, ubo_size);
                }
            }
        }
        return registered;
    }

    /* ========== BUILT-IN VERTEX STAGE UNIFORMS ========== */

    /* Vertex binding 0: matrices and scale */
    if (strcmp(name, "u_mvp") == 0 ||
        strcmp(name, "Transforms") == 0 ||
        strcmp(name, "u_modelViewProj") == 0 ||
        strcmp(name, "u_mvpMatrix") == 0 ||
        strcmp(name, "u_matrix") == 0 ||
        strcmp(name, "u_projection") == 0 ||   /* SDL_Renderer */
        strcmp(name, "u_testScale") == 0 ||
        strcmp(name, "ModelViewProjectionMatrix") == 0) {  /* es2gears */
        return (0 << 16) | 0;
    }

    /* Vertex binding 1: offset vec2/vec4 or NormalMatrix or Model matrix */
    if (strcmp(name, "u_offset") == 0 ||
        strcmp(name, "u_normalMatrix") == 0 ||
        strcmp(name, "u_testOffset2") == 0 ||
        strcmp(name, "u_model") == 0 ||        /* PBR model matrix */
        strcmp(name, "Model") == 0 ||          /* UBO block name */
        strcmp(name, "ModelMatrix") == 0 ||    /* blinn_phong block name */
        strcmp(name, "NormalMatrix") == 0) {   /* es2gears */
        return (0 << 16) | 1;
    }

    /* Vertex binding 2: offset3 vec3 or LightSourcePosition */
    if (strcmp(name, "u_testOffset3") == 0 ||
        strcmp(name, "LightSourcePosition") == 0) {  /* es2gears */
        return (0 << 16) | 2;
    }

    /* Vertex binding 3: mat2 or MaterialColor */
    if (strcmp(name, "u_testMat2") == 0 ||
        strcmp(name, "MaterialColor") == 0) {  /* es2gears */
        return (0 << 16) | 3;
    }

    /* Vertex binding 4: mat3 */
    if (strcmp(name, "u_testMat3") == 0) {
        return (0 << 16) | 4;
    }

    /* ========== BUILT-IN FRAGMENT STAGE UNIFORMS ========== */

    /* Fragment binding 0: color vec4 or alpha float or blend */
    if (strcmp(name, "u_color") == 0 ||
        strcmp(name, "FragUniforms") == 0 ||
        strcmp(name, "u_baseColor") == 0 ||
        strcmp(name, "u_testAlpha") == 0 ||
        strcmp(name, "u_blend") == 0) {
        return (1 << 16) | 0;
    }

    /* Fragment binding 1: vec2/vec4 or time or Material block (skybox) */
    if (strcmp(name, "u_testVec2") == 0 ||
        strcmp(name, "u_alpha") == 0 ||
        strcmp(name, "u_time") == 0 ||
        strcmp(name, "Material") == 0) {       /* UBO block name for skybox */
        return (1 << 16) | 1;
    }

    /* Fragment binding 2: vec3/vec4 or mode or material params (PBR) */
    if (strcmp(name, "u_testVec3") == 0 ||
        strcmp(name, "u_mode") == 0 ||
        strcmp(name, "u_material") == 0 ||     /* PBR material params */
        strcmp(name, "u_light") == 0 ||        /* Blinn-Phong light uniform */
        strcmp(name, "LightParams") == 0) {    /* Blinn-Phong light UBO block */
        return (1 << 16) | 2;
    }

    /* Fragment binding 3: vec4 */
    if (strcmp(name, "u_testVec4") == 0) {
        return (1 << 16) | 3;
    }

    /* Fragment binding 4: int mode (for alluniform shader) */
    if (strcmp(name, "u_testMode") == 0) {
        return (1 << 16) | 4;
    }

    /* Fragment binding 5: ivec2 */
    if (strcmp(name, "u_testIvec2") == 0) {
        return (1 << 16) | 5;
    }

    /* Fragment binding 6: ivec3 */
    if (strcmp(name, "u_testIvec3") == 0) {
        return (1 << 16) | 6;
    }

    /* Fragment binding 7: ivec4 */
    if (strcmp(name, "u_testIvec4") == 0) {
        return (1 << 16) | 7;
    }

    return -1;
}

GL_APICALL GLint GL_APIENTRY glGetAttribLocation(GLuint program, const GLchar *name) {
    (void)program; (void)name;
    return -1;
}

GL_APICALL void GL_APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
    (void)program; (void)index; (void)name;
}

GL_APICALL void GL_APIENTRY glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                                               GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    (void)program; (void)index; (void)bufSize;
    (void)length; (void)size; (void)type; (void)name;
}

GL_APICALL void GL_APIENTRY glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                                                GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    (void)program; (void)index; (void)bufSize;
    (void)length; (void)size; (void)type; (void)name;
}

GL_APICALL void GL_APIENTRY glGetUniformfv(GLuint program, GLint location, GLfloat *params) {
    (void)program; (void)location; (void)params;
}

GL_APICALL void GL_APIENTRY glGetUniformiv(GLuint program, GLint location, GLint *params) {
    (void)program; (void)location; (void)params;
}

/*
 * Helper to set a float uniform (1-4 components)
 * std140 layout: all uniforms are padded to 16 bytes (vec4)
 *
 * IMPORTANT: We allocate a NEW offset for each glUniform call to avoid
 * data races when multiple draws use different values in the same frame.
 * pushConstants copies data to the GPU address, but if multiple draws
 * use the same address, later draws overwrite earlier ones before the
 * GPU executes them.
 */
static void set_float_uniform(GLint location, int num_components, GLsizei count, const GLfloat *values) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;
    if (location == -1) return;

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog) return;

    /* Packed mode: write directly to shadow buffer */
    if (location & (1 << 31)) {
        int stage = (location >> 24) & 0x7F;
        int binding = (location >> 16) & 0xFF;
        int offset = location & 0xFFFF;

        if (binding >= SGL_MAX_PACKED_UBOS) return;  /* Bounds check */

        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];

        if (count == 1) {
            /* Single element: write exact bytes (no array padding) */
            uint32_t dataSize = num_components * sizeof(float);
            if (!packed->valid || offset + dataSize > packed->size) return;
            memcpy(packed->data + offset, values, dataSize);
        } else {
            /* Array: each element padded to vec4 (16 bytes) in std140 */
            uint32_t totalSize = count * 16;
            if (!packed->valid || offset + totalSize > packed->size) return;
            for (GLsizei e = 0; e < count; e++) {
                float elem[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (int j = 0; j < num_components && j < 4; j++) {
                    elem[j] = values[e * num_components + j];
                }
                memcpy(packed->data + offset + e * 16, elem, 16);
            }
        }
        packed->dirty = true;
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;

    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* Clamp count to stack buffer limit */
    GLsizei clampedCount = count > 64 ? 64 : count;

    /* std140: each array element padded to 16 bytes (vec4) */
    uint32_t dataSize = clampedCount * 16;
    uint32_t alignedSize = (dataSize + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);

    /* ALWAYS allocate a new offset for each uniform write.
     * This ensures each draw gets unique uniform data, preventing
     * data races in the GPU command buffer. */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    /* Write data via backend - pad each element to vec4 */
    if (ub->valid && ctx->backend->ops->write_uniform) {
        float array_data[4 * 64]; /* support up to 64 elements on stack */
        memset(array_data, 0, clampedCount * 16);
        for (GLsizei e = 0; e < clampedCount; e++) {
            for (int j = 0; j < num_components && j < 4; j++) {
                array_data[e * 4 + j] = values[e * num_components + j];
            }
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, array_data, clampedCount * 16);
    }

    ub->dirty = true;
}

GL_APICALL void GL_APIENTRY glUniform1f(GLint location, GLfloat v0) {
    GLfloat values[1] = { v0 };
    set_float_uniform(location, 1, 1, values);
    SGL_TRACE_UNIFORM("glUniform1f(loc=%d, %.2f)", location, v0);
}

GL_APICALL void GL_APIENTRY glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    GLfloat values[2] = { v0, v1 };
    set_float_uniform(location, 2, 1, values);
    SGL_TRACE_UNIFORM("glUniform2f(loc=%d, %.2f, %.2f)", location, v0, v1);
}

GL_APICALL void GL_APIENTRY glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    GLfloat values[3] = { v0, v1, v2 };
    set_float_uniform(location, 3, 1, values);
    SGL_TRACE_UNIFORM("glUniform3f(loc=%d, %.2f, %.2f, %.2f)", location, v0, v1, v2);
}

GL_APICALL void GL_APIENTRY glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4] = { v0, v1, v2, v3 };
    set_float_uniform(location, 4, 1, values);
    SGL_TRACE_UNIFORM("glUniform4f(loc=%d, %.2f, %.2f, %.2f, %.2f)", location, v0, v1, v2, v3);
}

/*
 * Helper to set an integer uniform (1-4 components)
 * std140 layout: integers are also 4 bytes each, padded to 16 bytes
 * Note: For samplers (glUniform1i), the value is the texture unit index
 *
 * IMPORTANT: We allocate a NEW offset for each glUniform call to avoid
 * data races when multiple draws use different values in the same frame.
 */
static void set_int_uniform(GLint location, int num_components, GLsizei count, const GLint *values) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;
    if (location == -1) return;

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog) return;

    /* Packed mode: write directly to shadow buffer */
    if (location & (1 << 31)) {
        int stage = (location >> 24) & 0x7F;
        int binding = (location >> 16) & 0xFF;
        int offset = location & 0xFFFF;

        if (binding >= SGL_MAX_PACKED_UBOS) return;  /* Bounds check */

        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];

        if (count == 1) {
            /* Single element: write exact bytes (no array padding) */
            uint32_t dataSize = num_components * sizeof(int32_t);
            if (!packed->valid || offset + dataSize > packed->size) return;
            memcpy(packed->data + offset, values, dataSize);
        } else {
            /* Array: each element padded to ivec4 (16 bytes) in std140 */
            uint32_t totalSize = count * 16;
            if (!packed->valid || offset + totalSize > packed->size) return;
            for (GLsizei e = 0; e < count; e++) {
                int32_t elem[4] = { 0, 0, 0, 0 };
                for (int j = 0; j < num_components && j < 4; j++) {
                    elem[j] = values[e * num_components + j];
                }
                memcpy(packed->data + offset + e * 16, elem, 16);
            }
        }
        packed->dirty = true;
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;

    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* Clamp count to stack buffer limit */
    GLsizei clampedCount = count > 64 ? 64 : count;

    /* std140: each array element padded to 16 bytes (ivec4) */
    uint32_t dataSize = clampedCount * 16;
    uint32_t alignedSize = (dataSize + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    /* Write data via backend - pad each element to ivec4 */
    if (ub->valid && ctx->backend->ops->write_uniform) {
        int32_t array_data[4 * 64]; /* support up to 64 elements on stack */
        memset(array_data, 0, clampedCount * 16);
        for (GLsizei e = 0; e < clampedCount; e++) {
            for (int j = 0; j < num_components && j < 4; j++) {
                array_data[e * 4 + j] = values[e * num_components + j];
            }
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, array_data, clampedCount * 16);
    }

    ub->dirty = true;
}

GL_APICALL void GL_APIENTRY glUniform1i(GLint location, GLint v0) {
    GLint values[1] = { v0 };
    set_int_uniform(location, 1, 1, values);
    SGL_TRACE_UNIFORM("glUniform1i(loc=%d, %d)", location, v0);
}

GL_APICALL void GL_APIENTRY glUniform2i(GLint location, GLint v0, GLint v1) {
    GLint values[2] = { v0, v1 };
    set_int_uniform(location, 2, 1, values);
    SGL_TRACE_UNIFORM("glUniform2i(loc=%d, %d, %d)", location, v0, v1);
}

GL_APICALL void GL_APIENTRY glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    GLint values[3] = { v0, v1, v2 };
    set_int_uniform(location, 3, 1, values);
    SGL_TRACE_UNIFORM("glUniform3i(loc=%d, %d, %d, %d)", location, v0, v1, v2);
}

GL_APICALL void GL_APIENTRY glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4] = { v0, v1, v2, v3 };
    set_int_uniform(location, 4, 1, values);
    SGL_TRACE_UNIFORM("glUniform4i(loc=%d, %d, %d, %d, %d)", location, v0, v1, v2, v3);
}

/* Vector variants (fv) - support count>1 for uniform arrays */
GL_APICALL void GL_APIENTRY glUniform1fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count <= 0 || !value) return;
    set_float_uniform(location, 1, count, value);
    SGL_TRACE_UNIFORM("glUniform1fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform2fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count <= 0 || !value) return;
    set_float_uniform(location, 2, count, value);
    SGL_TRACE_UNIFORM("glUniform2fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count <= 0 || !value) return;
    set_float_uniform(location, 3, count, value);
    SGL_TRACE_UNIFORM("glUniform3fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count <= 0 || !value) return;
    set_float_uniform(location, 4, count, value);
    SGL_TRACE_UNIFORM("glUniform4fv(loc=%d, count=%d)", location, count);
}

/* Vector variants (iv) - support count>1 for uniform arrays */
GL_APICALL void GL_APIENTRY glUniform1iv(GLint location, GLsizei count, const GLint *value) {
    if (count <= 0 || !value) return;
    set_int_uniform(location, 1, count, value);
    SGL_TRACE_UNIFORM("glUniform1iv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform2iv(GLint location, GLsizei count, const GLint *value) {
    if (count <= 0 || !value) return;
    set_int_uniform(location, 2, count, value);
    SGL_TRACE_UNIFORM("glUniform2iv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform3iv(GLint location, GLsizei count, const GLint *value) {
    if (count <= 0 || !value) return;
    set_int_uniform(location, 3, count, value);
    SGL_TRACE_UNIFORM("glUniform3iv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform4iv(GLint location, GLsizei count, const GLint *value) {
    if (count <= 0 || !value) return;
    set_int_uniform(location, 4, count, value);
    SGL_TRACE_UNIFORM("glUniform4iv(loc=%d, count=%d)", location, count);
}

/*
 * Matrix uniforms
 * std140 layout: mat2 = 2 vec4 (32 bytes), mat3 = 3 vec4 (48 bytes), mat4 = 4 vec4 (64 bytes)
 */
GL_APICALL void GL_APIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;
    if (location == -1 || count <= 0 || !value) return;

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog || !prog->linked) return;

    /* Packed mode: write std140 mat2 to shadow buffer */
    if (location & (1 << 31)) {
        int stage = (location >> 24) & 0x7F;
        int binding = (location >> 16) & 0xFF;
        int offset = location & 0xFFFF;
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        uint32_t dataSize = 32 * count; /* mat2 std140: 2 vec4 = 32 bytes */
        if (!packed->valid || offset + dataSize > packed->size) return;
        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 4;
            float *dst = (float *)(packed->data + offset + m * 32);
            if (transpose == GL_FALSE) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = 0.0f; dst[3] = 0.0f;
                dst[4] = src[2]; dst[5] = src[3]; dst[6] = 0.0f; dst[7] = 0.0f;
            } else {
                dst[0] = src[0]; dst[1] = src[2]; dst[2] = 0.0f; dst[3] = 0.0f;
                dst[4] = src[1]; dst[5] = src[3]; dst[6] = 0.0f; dst[7] = 0.0f;
            }
        }
        packed->dirty = true;
        SGL_TRACE_UNIFORM("glUniformMatrix2fv(packed loc=0x%X, count=%d)", location, count);
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* mat2 in std140: 2 columns of vec4 (padded from vec2) = 32 bytes */
    uint32_t dataSize = 32 * count;
    uint32_t alignedSize = (dataSize + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    if (ub->valid && ctx->backend->ops->write_uniform) {
        /* Convert mat2 (4 floats) to std140 layout (2 vec4 = 8 floats) */
        float std140_data[8 * 4]; /* Support up to 4 matrices */
        if (count > 4) count = 4;
        dataSize = 32 * count;  /* Recompute after clamping to avoid buffer over-read */

        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 4;
            float *dst = std140_data + m * 8;

            if (transpose == GL_FALSE) {
                /* Column 0 */
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = 0.0f; dst[3] = 0.0f;
                /* Column 1 */
                dst[4] = src[2]; dst[5] = src[3]; dst[6] = 0.0f; dst[7] = 0.0f;
            } else {
                /* Transposed: swap rows/cols */
                dst[0] = src[0]; dst[1] = src[2]; dst[2] = 0.0f; dst[3] = 0.0f;
                dst[4] = src[1]; dst[5] = src[3]; dst[6] = 0.0f; dst[7] = 0.0f;
            }
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, std140_data, dataSize);
    }

    ub->dirty = true;
    SGL_TRACE_UNIFORM("glUniformMatrix2fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;
    if (location == -1 || count <= 0 || !value) return;

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog || !prog->linked) return;

    /* Packed mode: write std140 mat3 to shadow buffer */
    if (location & (1 << 31)) {
        int stage = (location >> 24) & 0x7F;
        int binding = (location >> 16) & 0xFF;
        int offset = location & 0xFFFF;
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        uint32_t dataSize = 48 * count; /* mat3 std140: 3 vec4 = 48 bytes */
        if (!packed->valid || offset + dataSize > packed->size) return;
        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 9;
            float *dst = (float *)(packed->data + offset + m * 48);
            if (transpose == GL_FALSE) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 0.0f;
                dst[4] = src[3]; dst[5] = src[4]; dst[6] = src[5]; dst[7] = 0.0f;
                dst[8] = src[6]; dst[9] = src[7]; dst[10] = src[8]; dst[11] = 0.0f;
            } else {
                dst[0] = src[0]; dst[1] = src[3]; dst[2] = src[6]; dst[3] = 0.0f;
                dst[4] = src[1]; dst[5] = src[4]; dst[6] = src[7]; dst[7] = 0.0f;
                dst[8] = src[2]; dst[9] = src[5]; dst[10] = src[8]; dst[11] = 0.0f;
            }
        }
        packed->dirty = true;
        SGL_TRACE_UNIFORM("glUniformMatrix3fv(packed loc=0x%X, count=%d)", location, count);
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* mat3 in std140: 3 columns of vec4 (padded from vec3) = 48 bytes */
    uint32_t dataSize = 48 * count;
    uint32_t alignedSize = (dataSize + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    if (ub->valid && ctx->backend->ops->write_uniform) {
        /* Convert mat3 (9 floats) to std140 layout (3 vec4 = 12 floats) */
        float std140_data[12 * 4]; /* Support up to 4 matrices */
        if (count > 4) count = 4;
        dataSize = 48 * count;  /* Recompute after clamping to avoid buffer over-read */

        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 9;
            float *dst = std140_data + m * 12;

            if (transpose == GL_FALSE) {
                /* Column 0 */
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 0.0f;
                /* Column 1 */
                dst[4] = src[3]; dst[5] = src[4]; dst[6] = src[5]; dst[7] = 0.0f;
                /* Column 2 */
                dst[8] = src[6]; dst[9] = src[7]; dst[10] = src[8]; dst[11] = 0.0f;
            } else {
                /* Transposed */
                dst[0] = src[0]; dst[1] = src[3]; dst[2] = src[6]; dst[3] = 0.0f;
                dst[4] = src[1]; dst[5] = src[4]; dst[6] = src[7]; dst[7] = 0.0f;
                dst[8] = src[2]; dst[9] = src[5]; dst[10] = src[8]; dst[11] = 0.0f;
            }
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, std140_data, dataSize);
    }

    ub->dirty = true;
    SGL_TRACE_UNIFORM("glUniformMatrix3fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;
    if (location == -1 || count <= 0 || !value) return;

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog || !prog->linked) return;

    /* Packed mode: write std140 mat4 to shadow buffer */
    if (location & (1 << 31)) {
        int stage = (location >> 24) & 0x7F;
        int binding = (location >> 16) & 0xFF;
        int offset = location & 0xFFFF;
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        uint32_t dataSize = 64 * count; /* mat4 std140: 4 vec4 = 64 bytes */
        if (!packed->valid || offset + dataSize > packed->size) return;
        if (transpose == GL_FALSE) {
            memcpy(packed->data + offset, value, dataSize);
        } else {
            for (GLsizei m = 0; m < count; m++) {
                const float *src = value + m * 16;
                float *dst = (float *)(packed->data + offset + m * 64);
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++)
                        dst[i * 4 + j] = src[j * 4 + i];
            }
        }
        packed->dirty = true;
        SGL_TRACE_UNIFORM("glUniformMatrix4fv(packed loc=0x%X, count=%d)", location, count);
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* mat4 in std140: 4 columns of vec4 = 64 bytes */
    uint32_t data_size = 64 * count;
    uint32_t aligned_size = (data_size + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, aligned_size);
        ub->size = aligned_size;
        ub->data_size = data_size;
        ub->valid = true;
    }

    if (!ub->valid) return;

    /* Handle transpose if needed, then write via backend */
    if (ub->valid && ctx->backend->ops->write_uniform) {
        if (transpose == GL_FALSE) {
            ctx->backend->ops->write_uniform(ctx->backend, ub->offset, value, data_size);
        } else {
            /* Transpose the matrix before writing */
            float transposed[16 * 4]; /* Support up to 4 matrices */
            if (count > 4) count = 4; /* Clamp to avoid overflow */
            data_size = 64 * count;  /* Recompute after clamping to avoid buffer over-read */
            for (GLsizei m = 0; m < count; m++) {
                const float *src = value + m * 16;
                float *d = transposed + m * 16;
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        d[i * 4 + j] = src[j * 4 + i];
                    }
                }
            }
            ctx->backend->ops->write_uniform(ctx->backend, ub->offset, transposed, data_size);
        }
    }

    ub->dirty = true;

    SGL_TRACE_UNIFORM("glUniformMatrix4fv(loc=%d, count=%d)", location, count);
}
