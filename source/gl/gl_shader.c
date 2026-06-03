/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Shader and Program Objects
 */

#include "gl_common.h"
#include "gl_shader_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef SGL_ENABLE_RUNTIME_COMPILER
#include <uam.h>
#include <malloc.h> /* memalign — needed for 256-byte aligned DKSH buffer */
#include "../transpiler/glsl_transpiler.h"
#include "gl_shader_cache.h"
/* Forward-declare packed uniform API (defined in gl_uniform.c, declared in gl2sgl.h) */
extern GLboolean sglRegisterPackedUniform(const GLchar *name, GLint stage, GLint binding,
                                          GLint byte_offset);
extern void sglSetPackedUBOSize(GLint stage, GLint binding, GLint size);

/* Convert transpiler type enum to GL type enum */
GLenum glslt_to_gl_type(glslt_type_t type) {
    switch (type) {
        case GLSLT_FLOAT:
            return GL_FLOAT;
        case GLSLT_VEC2:
            return GL_FLOAT_VEC2;
        case GLSLT_VEC3:
            return GL_FLOAT_VEC3;
        case GLSLT_VEC4:
            return GL_FLOAT_VEC4;
        case GLSLT_INT:
            return GL_INT;
        case GLSLT_IVEC2:
            return GL_INT_VEC2;
        case GLSLT_IVEC3:
            return GL_INT_VEC3;
        case GLSLT_IVEC4:
            return GL_INT_VEC4;
        case GLSLT_BOOL:
            return GL_BOOL;
        case GLSLT_BVEC2:
            return GL_BOOL_VEC2;
        case GLSLT_BVEC3:
            return GL_BOOL_VEC3;
        case GLSLT_BVEC4:
            return GL_BOOL_VEC4;
        case GLSLT_MAT2:
            return GL_FLOAT_MAT2;
        case GLSLT_MAT3:
            return GL_FLOAT_MAT3;
        case GLSLT_MAT4:
            return GL_FLOAT_MAT4;
        case GLSLT_SAMPLER2D:
            return GL_SAMPLER_2D;
        case GLSLT_SAMPLERCUBE:
            return GL_SAMPLER_CUBE;
        default:
            return GL_FLOAT_VEC4;
    }
}
#endif

/* Shader Objects */

GL_APICALL GLuint GL_APIENTRY glCreateShader(GLenum type) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return 0;

    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return 0;
    }

    GLuint id = sgl_res_mgr_alloc_shader(&ctx->res_mgr, type);
    if (id == 0) {
        sgl_set_error(ctx, GL_OUT_OF_MEMORY);
        return 0;
    }

    SGL_TRACE_SHADER("glCreateShader(0x%X) = %u", type, id);
    return id;
}

GL_APICALL void GL_APIENTRY glDeleteShader(GLuint shader) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (shader == 0)
        return;

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Per GLES spec: if shader is attached to any program, just flag it
     * for deletion. It will be freed when detached from all programs. */
    if (sh->attach_count > 0) {
        sh->delete_pending = true;
        SGL_TRACE_SHADER("glDeleteShader(%u) - deferred (attach_count=%d)", shader,
                         sh->attach_count);
        return;
    }

    /* Not attached — delete immediately */
    if (ctx->backend && ctx->backend->ops->delete_shader) {
        ctx->backend->ops->delete_shader(ctx->backend, shader);
    }

    sgl_res_mgr_free_shader(&ctx->res_mgr, shader);
    SGL_TRACE_SHADER("glDeleteShader(%u)", shader);
}

GL_APICALL GLboolean GL_APIENTRY glIsShader(GLuint shader) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return GL_FALSE;
    return GET_SHADER(shader) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glShaderSource(GLuint shader, GLsizei count,
                                           const GLchar *const *string, const GLint *length) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    /* Check shader validity FIRST — dEQP expects GL_INVALID_OPERATION for
     * a program handle even when count/string are also bad. */
    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    if (count < 0 || !string) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Free previous source */
    if (sh->source) {
        free(sh->source);
        sh->source = NULL;
    }

    /* Calculate total length */
    size_t total = 0;
    for (GLsizei i = 0; i < count; i++) {
        if (!string[i])
            continue;
        if (length && length[i] >= 0) {
            total += (size_t)length[i];
        } else {
            total += strlen(string[i]);
        }
    }

    /* Concatenate all strings */
    sh->source = (char *)malloc(total + 1);
    if (!sh->source) {
        sgl_set_error(ctx, GL_OUT_OF_MEMORY);
        return;
    }

    char *dst = sh->source;
    for (GLsizei i = 0; i < count; i++) {
        if (!string[i])
            continue;
        size_t len;
        if (length && length[i] >= 0) {
            len = (size_t)length[i];
        } else {
            len = strlen(string[i]);
        }
        memcpy(dst, string[i], len);
        dst += len;
    }
    *dst = '\0';

    /* Reset compilation state */
    sh->compiled = false;
    sh->needs_transpile = false;

    SGL_TRACE_SHADER("glShaderSource(%u, %d) - %zu bytes", shader, count, total);
}

#ifdef SGL_ENABLE_RUNTIME_COMPILER
/*
 * sgl_compile_glsl460 - Compile GLSL 4.60 source to DKSH and load into backend.
 *
 * Shared helper used by both glCompileShader (for direct GLSL 460 source)
 * and glLinkProgram (for transpiled ES 1.00 → 460 source).
 *
 * Returns true on success. Sets sh->info_log on failure.
 */
bool sgl_compile_glsl460(sgl_context_t *ctx, GLuint shader_id, sgl_shader_t *sh,
                         const char *glsl_source) {
    DkStage stage;
    if (sh->type == GL_VERTEX_SHADER) {
        stage = DkStage_Vertex;
    } else if (sh->type == GL_FRAGMENT_SHADER) {
        stage = DkStage_Fragment;
    } else {
        sh->info_log = strdup("ERROR: Unsupported shader type\n");
        return false;
    }

    /* Try shader cache first — skip libuam if we have a cached DKSH */
    {
        size_t cached_size = 0;
        void *cached_dksh = sgl_shader_cache_lookup(glsl_source, (int)stage, &cached_size);
        if (cached_dksh) {
            bool cache_ok = false;
            size_t alloc_size = SGL_ALIGN_UP(cached_size, SGL_PAGE_ALIGNMENT);
            void *aligned = memalign(256, alloc_size);
            if (aligned) {
                memset(aligned, 0, alloc_size);
                memcpy(aligned, cached_dksh, cached_size);
                free(cached_dksh);
                if (ctx->backend && ctx->backend->ops->load_shader_binary) {
                    cache_ok = ctx->backend->ops->load_shader_binary(ctx->backend, shader_id,
                                                                     aligned, cached_size);
                }
                free(aligned);
                if (cache_ok) {
                    SGL_TRACE_SHADER("shader %u: cache HIT (size=%zu)", shader_id, cached_size);
                    return true;
                }
            } else {
                free(cached_dksh);
            }
            /* Cache hit but load failed — fall through to recompile */
        }
    }

    uam_compiler *compiler = uam_create_compiler(stage);
    if (!compiler) {
        sh->info_log = strdup("ERROR: Failed to create shader compiler\n");
        return false;
    }

    bool result = false;
    bool compiled = uam_compile_dksh(compiler, glsl_source);

    if (compiled) {
        size_t dksh_size = uam_get_code_size(compiler);

        SGL_TRACE_SHADER("RT compile shader %u: DKSH size=%zu align256=%d GPRs=%d", shader_id,
                         dksh_size, (int)(dksh_size % 256), uam_get_num_gprs(compiler));

        /* CRITICAL: Buffer MUST be 256-byte aligned for libuam's pa256(). */
        size_t alloc_size = SGL_ALIGN_UP(dksh_size, SGL_PAGE_ALIGNMENT);
        void *dksh = memalign(256, alloc_size);
        if (dksh) {
            memset(dksh, 0, alloc_size);
            uam_write_code(compiler, dksh);

            /* Store compiled DKSH to cache for next launch */
            sgl_shader_cache_store(glsl_source, (int)stage, dksh, dksh_size);

            if (ctx->backend && ctx->backend->ops->load_shader_binary) {
                result =
                    ctx->backend->ops->load_shader_binary(ctx->backend, shader_id, dksh, dksh_size);
            } else {
                sh->info_log = strdup("ERROR: Backend does not support shader loading\n");
            }

            free(dksh);
        } else {
            sh->info_log = strdup("ERROR: Out of memory for compiled shader\n");
        }

        const char *log = uam_get_error_log(compiler);
        if (log && log[0] != '\0' && !sh->info_log) {
            sh->info_log = strdup(log);
        }
    } else {
        const char *log = uam_get_error_log(compiler);
        sh->info_log = (log && log[0]) ? strdup(log) : strdup("ERROR: Compilation failed\n");
    }

    uam_free_compiler(compiler);
    return result;
}

/*
 * Convert uam uniform base_type + dimensions to GL type enum.
 * base_type values from Mesa's glsl_base_type enum (glsl_types.h):
 *   0=uint, 1=int, 2=float, 3=float16, 4=double,
 *   5=uint8, 6=int8, 7=uint16, 8=int16, 9=uint64, 10=int64,
 *   11=bool, 12=sampler
 */
GLenum uam_base_type_to_gl(uint8_t base_type, uint8_t vec_elems, uint8_t mat_cols) {
    if (base_type == 2) { /* GLSL_TYPE_FLOAT */
        if (mat_cols > 1) {
            if (mat_cols == 2)
                return GL_FLOAT_MAT2;
            if (mat_cols == 3)
                return GL_FLOAT_MAT3;
            return GL_FLOAT_MAT4;
        }
        if (vec_elems == 1)
            return GL_FLOAT;
        if (vec_elems == 2)
            return GL_FLOAT_VEC2;
        if (vec_elems == 3)
            return GL_FLOAT_VEC3;
        return GL_FLOAT_VEC4;
    }
    if (base_type == 1) { /* GLSL_TYPE_INT */
        if (vec_elems == 1)
            return GL_INT;
        if (vec_elems == 2)
            return GL_INT_VEC2;
        if (vec_elems == 3)
            return GL_INT_VEC3;
        return GL_INT_VEC4;
    }
    if (base_type == 11) { /* GLSL_TYPE_BOOL */
        if (vec_elems == 1)
            return GL_BOOL;
        if (vec_elems == 2)
            return GL_BOOL_VEC2;
        if (vec_elems == 3)
            return GL_BOOL_VEC3;
        return GL_BOOL_VEC4;
    }
    if (base_type == 12)
        return GL_SAMPLER_2D; /* GLSL_TYPE_SAMPLER */
    if (base_type == 0) {     /* GLSL_TYPE_UINT — map to int for GLES2 */
        if (vec_elems == 1)
            return GL_INT;
        if (vec_elems == 2)
            return GL_INT_VEC2;
        if (vec_elems == 3)
            return GL_INT_VEC3;
        return GL_INT_VEC4;
    }
    return GL_FLOAT_VEC4; /* fallback */
}

/*
 * sgl_compile_es100_mesa - Compile ES 1.00 shader directly via Mesa in uam.
 *
 * Tries to compile the ES 1.00 source directly through Mesa's GLSL compiler
 * (bypassing the transpiler). Captures uniform/sampler metadata on success.
 * Returns true on success; sets sh->mesa_meta with metadata.
 * Returns false on failure (caller should fall back to transpiler).
 */
static bool sgl_compile_es100_mesa(sgl_context_t *ctx, GLuint shader_id, sgl_shader_t *sh) {
    DkStage stage;
    if (sh->type == GL_VERTEX_SHADER) {
        stage = DkStage_Vertex;
    } else if (sh->type == GL_FRAGMENT_SHADER) {
        stage = DkStage_Fragment;
    } else {
        return false;
    }

    uam_compiler *compiler = uam_create_compiler(stage);
    if (!compiler)
        return false;

    bool compiled = uam_compile_dksh(compiler, sh->source);
    if (!compiled) {
        /* Capture Mesa's error log before freeing the compiler.
         * If Mesa reported actual errors, store them in the shader so the
         * caller knows NOT to try the transpiler fallback. */
        const char *mesa_log = uam_get_error_log(compiler);
        if (mesa_log && mesa_log[0]) {
            sh->info_log = strdup(mesa_log);
            SGL_TRACE_SHADER("shader %u: Mesa direct ES 1.00 rejected: %s", shader_id, mesa_log);
        } else {
            SGL_TRACE_SHADER("shader %u: Mesa direct ES 1.00 compile failed (no error log)",
                             shader_id);
        }
        uam_free_compiler(compiler);
        return false;
    }

    /* Check if this shader has bare uniforms/samplers (ES 1.00 indicator) */
    int num_uniforms = uam_get_num_uniforms(compiler);
    int num_samplers = uam_get_num_samplers(compiler);
    bool remapped = uam_is_constbuf_remapped(compiler);

    /* Capture metadata */
    sgl_mesa_metadata_t *meta = (sgl_mesa_metadata_t *)calloc(1, sizeof(sgl_mesa_metadata_t));
    if (!meta) {
        uam_free_compiler(compiler);
        return false;
    }

    meta->num_uniforms = num_uniforms;
    meta->constbuf_size = uam_get_constbuf_size(compiler);
    for (int i = 0; i < num_uniforms && i < SGL_MESA_MAX_UNIFORMS; i++) {
        uam_uniform_info_t info;
        if (uam_get_uniform_info(compiler, i, &info)) {
            strncpy(meta->uniforms[i].name, info.name, SGL_ATTRIB_NAME_MAX - 1);
            meta->uniforms[i].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            meta->uniforms[i].offset = info.offset;
            meta->uniforms[i].size_bytes = info.size_bytes;
            meta->uniforms[i].gl_type =
                uam_base_type_to_gl(info.base_type, info.vector_elements, info.matrix_columns);
            meta->uniforms[i].array_elements = info.array_elements;
        }
    }

    meta->num_samplers = num_samplers;
    for (int i = 0; i < num_samplers && i < SGL_MESA_MAX_SAMPLERS; i++) {
        uam_sampler_info_t sinfo;
        if (uam_get_sampler_info(compiler, i, &sinfo)) {
            strncpy(meta->samplers[i].name, sinfo.name, SGL_ATTRIB_NAME_MAX - 1);
            meta->samplers[i].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            meta->samplers[i].binding = sinfo.binding;
            meta->samplers[i].gl_type = (sinfo.type == 1) ? GL_SAMPLER_CUBE : GL_SAMPLER_2D;
        }
    }

    /* Capture vertex input (attribute) metadata */
    int num_inputs = uam_get_num_inputs(compiler);
    meta->num_inputs = num_inputs;
    for (int i = 0; i < num_inputs && i < SGL_MESA_MAX_INPUTS; i++) {
        uam_input_info_t iinfo;
        if (uam_get_input_info(compiler, i, &iinfo)) {
            strncpy(meta->inputs[i].name, iinfo.name, SGL_ATTRIB_NAME_MAX - 1);
            meta->inputs[i].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            meta->inputs[i].location = iinfo.location;
            meta->inputs[i].gl_type =
                uam_base_type_to_gl(iinfo.base_type, iinfo.vector_elements, iinfo.matrix_columns);
        }
    }

    /* Capture initial constbuf data (Mesa embeds literal constants here).
     * Must copy before uam_free_compiler() invalidates the pointer. */
    {
        uint32_t init_size = 0;
        const void *init_ptr = uam_get_constbuf_initial_data(compiler, &init_size);
        if (init_ptr && init_size > 0) {
            meta->initial_data = (uint8_t *)malloc(init_size);
            if (meta->initial_data) {
                memcpy(meta->initial_data, init_ptr, init_size);
                meta->initial_data_size = init_size;
            }
        }
    }

    /* Capture gl_DepthRange offset */
    meta->depth_range_offset = uam_get_depth_range_offset(compiler);

    /* Load the compiled DKSH binary */
    size_t dksh_size = uam_get_code_size(compiler);
    size_t alloc_size = SGL_ALIGN_UP(dksh_size, SGL_PAGE_ALIGNMENT);
    void *dksh = memalign(256, alloc_size);
    bool result = false;
    if (dksh) {
        memset(dksh, 0, alloc_size);
        uam_write_code(compiler, dksh);

        /* Store to shader cache */
        sgl_shader_cache_store(sh->source, (int)stage, dksh, dksh_size);

        if (ctx->backend && ctx->backend->ops->load_shader_binary) {
            result =
                ctx->backend->ops->load_shader_binary(ctx->backend, shader_id, dksh, dksh_size);
        }
        free(dksh);
    }

    if (result) {
        /* Free old metadata if any */
        if (sh->mesa_meta) {
            if (sh->mesa_meta->initial_data)
                free(sh->mesa_meta->initial_data);
            free(sh->mesa_meta);
        }
        sh->mesa_meta = meta;
        sh->compiled_via_mesa = true;

        const char *log = uam_get_error_log(compiler);
        if (log && log[0] != '\0') {
            if (sh->info_log)
                free(sh->info_log);
            sh->info_log = strdup(log);
        }

        SGL_TRACE_SHADER(
            "shader %u: Mesa direct ES 1.00 compile OK (%d uniforms, %d samplers, constbuf=%u%s)",
            shader_id, num_uniforms, num_samplers, meta->constbuf_size,
            remapped ? ", remapped" : "");
    } else {
        if (meta->initial_data)
            free(meta->initial_data);
        free(meta);
    }

    uam_free_compiler(compiler);
    return result;
}

/*
 * Detect if shader source is GLSL ES 1.00 (needs transpilation).
 * Returns true if #version 100, or if no #version but source contains
 * ES-specific keywords (attribute, varying, gl_FragColor, precision).
 * Garbage text with no #version returns false so libuam can report the error.
 */
static bool sgl_is_es100_source(const char *source) {
    const char *p = source;
    /* Skip leading whitespace and empty lines */
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;

    if (strncmp(p, "#version", 8) == 0) {
        p += 8;
        while (*p == ' ' || *p == '\t')
            p++;
        int ver = atoi(p);
        if (ver == 100)
            return true;
        /* #version 300 es could also be transpiled in future */
        return false;
    }

    /* No #version directive — only treat as ES 1.00 if source contains
     * ES-specific keywords that distinguish it from garbage or GLSL 460.
     * mediump/highp/lowp are precision qualifiers never valid in GLSL 4.60 core. */
    if (strstr(source, "attribute ") || strstr(source, "attribute\t") ||
        strstr(source, "varying ") || strstr(source, "varying\t") ||
        strstr(source, "gl_FragColor") || strstr(source, "gl_FragData") ||
        strstr(source, "precision ") || strstr(source, "precision\t") ||
        strstr(source, "mediump ") || strstr(source, "mediump\t") || strstr(source, "highp ") ||
        strstr(source, "highp\t") || strstr(source, "lowp ") || strstr(source, "lowp\t")) {
        return true;
    }

    return false;
}
#endif /* SGL_ENABLE_RUNTIME_COMPILER */

GL_APICALL void GL_APIENTRY glCompileShader(GLuint shader) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    /* Free previous info log */
    if (sh->info_log) {
        free(sh->info_log);
        sh->info_log = NULL;
    }

    sh->needs_transpile = false;

    /* If no source set, mark as compiled for precompiled path (glShaderBinary) */
    if (!sh->source) {
        sh->compiled = true;
        return;
    }

#ifdef SGL_ENABLE_RUNTIME_COMPILER
    sh->compiled_via_mesa = false;

    /* Check if source is GLSL ES 1.00 */
    if (sgl_is_es100_source(sh->source)) {
        /* Run GLES 1.00 semantic validation FIRST — catches constructs that
         * Mesa accepts (it compiles as GLSL 4.60 core) but GLES 1.00 forbids:
         * reserved operators, qualification order, preprocessor restrictions, etc. */
        glslt_stage_t stage = (sh->type == GL_VERTEX_SHADER) ? GLSLT_VERTEX : GLSLT_FRAGMENT;
        char val_error[256];
        if (!glslt_validate_es100(sh->source, stage, val_error, sizeof(val_error))) {
            sh->compiled = false;
            sh->info_log = strdup(val_error);
            SGL_TRACE_SHADER("glCompileShader(%u) - ES validation failed: %s", shader, val_error);
            return;
        }

        /* Force transpiler path for shaders using gl_DepthRange.
         * Mesa in API_OPENGL_CORE mode doesn't emit STATE_DEPTH_RANGE as a
         * program parameter for #version 100, so the built-in uniform values
         * never reach the shader. The transpiler injects synthetic uniforms
         * (sgl_dr_near/far/diff) that work via the packed UBO system. */
        if (strstr(sh->source, "gl_DepthRange")) {
            SGL_TRACE_SHADER("glCompileShader(%u) - gl_DepthRange detected, using transpiler",
                             shader);
            goto transpiler_fallback;
        }

        /* Try Mesa direct compilation (handles full GLSL ES 1.00 spec) */
        if (sgl_compile_es100_mesa(ctx, shader, sh)) {
            sh->compiled = true;
            sh->needs_transpile = false;
            SGL_TRACE_SHADER("glCompileShader(%u) - ES 1.00 Mesa direct OK", shader);
            return;
        }

        /* Mesa failed. If Mesa reported actual errors (info_log set by
         * sgl_compile_es100_mesa), the shader is genuinely invalid — do NOT
         * try the transpiler, which lacks semantic analysis and would
         * incorrectly accept invalid shaders (fixes 72 dEQP failures).
         * Only fall back to transpiler when Mesa silently failed (no error log),
         * which happens for the ~0.3% of valid ES 1.00 shaders Mesa can't handle. */
        if (sh->info_log) {
            /* Mesa merges compile+link. Errors about exceeding MAX_VERTEX_ATTRIBS
             * are link-time issues in GLES2 (aliasing resolves at link). Mark the
             * shader as compiled — it will be recompiled at link time with bindings. */
            if (sh->type == GL_VERTEX_SHADER &&
                (strstr(sh->info_log, "too many vertex shader inputs") ||
                 strstr(sh->info_log, "insufficient contiguous locations"))) {
                sh->compiled = true;
                sh->compiled_via_mesa = true;
                free(sh->info_log);
                sh->info_log = NULL;
                SGL_TRACE_SHADER(
                    "glCompileShader(%u) - Mesa rejected (too many attribs), deferring to link",
                    shader);
                return;
            }
            /* Mesa rejects some valid ES 1.00 constructs (const struct constructors,
             * certain initializer patterns). Allow transpiler fallback for these.
             * Pattern: any error about const/initializer/constant. */
            if (strstr(sh->info_log, "initializer") || strstr(sh->info_log, "constant") ||
                strstr(sh->info_log, "const ")) {
                free(sh->info_log);
                sh->info_log = NULL;
                SGL_TRACE_SHADER("glCompileShader(%u) - Mesa const issue, trying transpiler",
                                 shader);
                /* Fall through to transpiler below */
            } else {
                sh->compiled = false;
                SGL_TRACE_SHADER(
                    "glCompileShader(%u) - Mesa rejected with errors, no transpiler fallback",
                    shader);
                return;
            }
        }
        SGL_TRACE_SHADER("glCompileShader(%u) - Mesa failed silently, trying transpiler", shader);
    transpiler_fallback: {
        glslt_options_t topts;
        glslt_options_init(&topts);
        glslt_result_t tres = glslt_transpile(sh->source, stage, &topts);
        if (tres.success) {
            sh->needs_transpile = true;
            sh->compiled = true;
            SGL_TRACE_SHADER("glCompileShader(%u) - transpiler fallback OK", shader);
            glslt_result_free(&tres);
        } else {
            /* Both Mesa and transpiler rejected — shader is genuinely invalid */
            sh->compiled = false;
            sh->info_log = strdup(tres.error);
            SGL_TRACE_SHADER("glCompileShader(%u) - transpiler also failed: %s", shader,
                             tres.error);
            glslt_result_free(&tres);
        }
    }
        return;
    }

    /* GLSL 4.60 source → compile directly */
    sh->compiled = sgl_compile_glsl460(ctx, shader, sh, sh->source);
    SGL_TRACE_SHADER("glCompileShader(%u) - %s", shader, sh->compiled ? "OK" : "FAILED");
#else
    /* No runtime compiler - source shaders cannot be compiled */
    sh->info_log = strdup("ERROR: Runtime shader compilation not available. "
                          "Use precompiled DKSH shaders via glShaderBinary().\n");
    sh->compiled = false;
    SGL_TRACE_SHADER("glCompileShader(%u) - no runtime compiler", shader);
#endif
}

GL_APICALL void GL_APIENTRY glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
    if (!params)
        return;

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    switch (pname) {
        case GL_SHADER_TYPE:
            *params = sh->type;
            break;
        case GL_COMPILE_STATUS:
            *params = sh->compiled ? GL_TRUE : GL_FALSE;
            break;
        case GL_DELETE_STATUS:
            *params = sh->delete_pending ? GL_TRUE : GL_FALSE;
            break;
        case GL_INFO_LOG_LENGTH:
            *params = sh->info_log ? (GLint)(strlen(sh->info_log) + 1) : 0;
            break;
        case GL_SHADER_SOURCE_LENGTH:
            *params = sh->source ? (GLint)(strlen(sh->source) + 1) : 0;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

GL_APICALL void GL_APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length,
                                               GLchar *infoLog) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (bufSize < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        if (length)
            *length = 0;
        if (infoLog && bufSize > 0)
            infoLog[0] = '\0';
        return;
    }

    if (sh->info_log && sh->info_log[0]) {
        GLsizei log_len = (GLsizei)strlen(sh->info_log);
        GLsizei copy_len = (bufSize > 0) ? (bufSize - 1) : 0;
        if (copy_len > log_len)
            copy_len = log_len;
        if (infoLog && bufSize > 0) {
            if (copy_len > 0)
                memcpy(infoLog, sh->info_log, copy_len);
            infoLog[copy_len] = '\0';
        }
        if (length)
            *length = copy_len;
    } else {
        if (length)
            *length = 0;
        if (infoLog && bufSize > 0)
            infoLog[0] = '\0';
    }
}

GL_APICALL void GL_APIENTRY glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length,
                                              GLchar *source) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;

    if (bufSize < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        if (length)
            *length = 0;
        if (source && bufSize > 0)
            source[0] = '\0';
        return;
    }

    if (sh->source && sh->source[0]) {
        GLsizei src_len = (GLsizei)strlen(sh->source);
        GLsizei copy_len = (bufSize > 0) ? (bufSize - 1) : 0;
        if (copy_len > src_len)
            copy_len = src_len;
        if (source && bufSize > 0) {
            if (copy_len > 0)
                memcpy(source, sh->source, copy_len);
            source[copy_len] = '\0';
        }
        if (length)
            *length = copy_len;
    } else {
        if (length)
            *length = 0;
        if (source && bufSize > 0)
            source[0] = '\0';
    }
}

/* Load pre-compiled shader from file - delegates to backend */
bool sgl_load_shader_from_file(GLuint shader_id, const char *path) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend || !ctx->backend->ops)
        return false;

    sgl_shader_t *shader = sgl_res_mgr_get_shader(&ctx->res_mgr, shader_id);
    if (!shader)
        return false;

    /* Call backend to load shader */
    if (ctx->backend->ops->load_shader_file) {
        bool result = ctx->backend->ops->load_shader_file(ctx->backend, shader_id, path);
        if (result) {
            shader->compiled = true;
        }
        return result;
    }

    return false;
}
