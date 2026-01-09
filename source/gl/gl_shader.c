/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Shader and Program Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Forward declaration for deferred shader deletion */
static void sgl_shader_try_deferred_delete(sgl_context_t *ctx, GLuint shader);

#ifdef SGL_ENABLE_RUNTIME_COMPILER
#include <uam.h>
#include <malloc.h>  /* memalign — needed for 256-byte aligned DKSH buffer */
#include "../transpiler/glsl_transpiler.h"
#include "gl_shader_cache.h"
/* Forward-declare packed uniform API (defined in gl_uniform.c, declared in gl2sgl.h) */
extern GLboolean sglRegisterPackedUniform(const GLchar *name, GLint stage, GLint binding, GLint byte_offset);
extern void sglSetPackedUBOSize(GLint stage, GLint binding, GLint size);

/* Convert transpiler type enum to GL type enum */
static GLenum glslt_to_gl_type(glslt_type_t type) {
    switch (type) {
        case GLSLT_FLOAT:      return GL_FLOAT;
        case GLSLT_VEC2:       return GL_FLOAT_VEC2;
        case GLSLT_VEC3:       return GL_FLOAT_VEC3;
        case GLSLT_VEC4:       return GL_FLOAT_VEC4;
        case GLSLT_INT:        return GL_INT;
        case GLSLT_IVEC2:      return GL_INT_VEC2;
        case GLSLT_IVEC3:      return GL_INT_VEC3;
        case GLSLT_IVEC4:      return GL_INT_VEC4;
        case GLSLT_BOOL:       return GL_BOOL;
        case GLSLT_BVEC2:      return GL_BOOL_VEC2;
        case GLSLT_BVEC3:      return GL_BOOL_VEC3;
        case GLSLT_BVEC4:      return GL_BOOL_VEC4;
        case GLSLT_MAT2:       return GL_FLOAT_MAT2;
        case GLSLT_MAT3:       return GL_FLOAT_MAT3;
        case GLSLT_MAT4:       return GL_FLOAT_MAT4;
        case GLSLT_SAMPLER2D:  return GL_SAMPLER_2D;
        case GLSLT_SAMPLERCUBE:return GL_SAMPLER_CUBE;
        default:               return GL_FLOAT_VEC4;
    }
}
#endif

/* Shader Objects */

GL_APICALL GLuint GL_APIENTRY glCreateShader(GLenum type) {
    GET_CTX_RET(0);

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
    GET_CTX();
    if (shader == 0) return;

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Per GLES spec: if shader is attached to any program, just flag it
     * for deletion. It will be freed when detached from all programs. */
    if (sh->attach_count > 0) {
        sh->delete_pending = true;
        SGL_TRACE_SHADER("glDeleteShader(%u) - deferred (attach_count=%d)", shader, sh->attach_count);
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
    GET_CTX_RET(GL_FALSE);
    return GET_SHADER(shader) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) {
    GET_CTX();

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
        if (!string[i]) continue;
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
        if (!string[i]) continue;
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
static bool sgl_compile_glsl460(sgl_context_t *ctx, GLuint shader_id,
                                 sgl_shader_t *sh, const char *glsl_source) {
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
                    cache_ok = ctx->backend->ops->load_shader_binary(
                        ctx->backend, shader_id, aligned, cached_size);
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

        SGL_TRACE_SHADER("RT compile shader %u: DKSH size=%zu align256=%d GPRs=%d",
                         shader_id, dksh_size, (int)(dksh_size % 256), uam_get_num_gprs(compiler));

        /* CRITICAL: Buffer MUST be 256-byte aligned for libuam's pa256(). */
        size_t alloc_size = SGL_ALIGN_UP(dksh_size, SGL_PAGE_ALIGNMENT);
        void *dksh = memalign(256, alloc_size);
        if (dksh) {
            memset(dksh, 0, alloc_size);
            uam_write_code(compiler, dksh);

            /* Store compiled DKSH to cache for next launch */
            sgl_shader_cache_store(glsl_source, (int)stage, dksh, dksh_size);

            if (ctx->backend && ctx->backend->ops->load_shader_binary) {
                result = ctx->backend->ops->load_shader_binary(
                    ctx->backend, shader_id, dksh, dksh_size);
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
static GLenum uam_base_type_to_gl(uint8_t base_type, uint8_t vec_elems, uint8_t mat_cols) {
    if (base_type == 2) { /* GLSL_TYPE_FLOAT */
        if (mat_cols > 1) {
            if (mat_cols == 2) return GL_FLOAT_MAT2;
            if (mat_cols == 3) return GL_FLOAT_MAT3;
            return GL_FLOAT_MAT4;
        }
        if (vec_elems == 1) return GL_FLOAT;
        if (vec_elems == 2) return GL_FLOAT_VEC2;
        if (vec_elems == 3) return GL_FLOAT_VEC3;
        return GL_FLOAT_VEC4;
    }
    if (base_type == 1) { /* GLSL_TYPE_INT */
        if (vec_elems == 1) return GL_INT;
        if (vec_elems == 2) return GL_INT_VEC2;
        if (vec_elems == 3) return GL_INT_VEC3;
        return GL_INT_VEC4;
    }
    if (base_type == 11) { /* GLSL_TYPE_BOOL */
        if (vec_elems == 1) return GL_BOOL;
        if (vec_elems == 2) return GL_BOOL_VEC2;
        if (vec_elems == 3) return GL_BOOL_VEC3;
        return GL_BOOL_VEC4;
    }
    if (base_type == 12) return GL_SAMPLER_2D; /* GLSL_TYPE_SAMPLER */
    if (base_type == 0) { /* GLSL_TYPE_UINT — map to int for GLES2 */
        if (vec_elems == 1) return GL_INT;
        if (vec_elems == 2) return GL_INT_VEC2;
        if (vec_elems == 3) return GL_INT_VEC3;
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
static bool sgl_compile_es100_mesa(sgl_context_t *ctx, GLuint shader_id,
                                    sgl_shader_t *sh) {
    DkStage stage;
    if (sh->type == GL_VERTEX_SHADER) {
        stage = DkStage_Vertex;
    } else if (sh->type == GL_FRAGMENT_SHADER) {
        stage = DkStage_Fragment;
    } else {
        return false;
    }

    uam_compiler *compiler = uam_create_compiler(stage);
    if (!compiler) return false;

    bool compiled = uam_compile_dksh(compiler, sh->source);
    if (!compiled) {
        /* Capture Mesa's error log before freeing the compiler.
         * If Mesa reported actual errors, store them in the shader so the
         * caller knows NOT to try the transpiler fallback. */
        const char *mesa_log = uam_get_error_log(compiler);
        if (mesa_log && mesa_log[0]) {
            sh->info_log = strdup(mesa_log);
            SGL_TRACE_SHADER("shader %u: Mesa direct ES 1.00 rejected: %s",
                             shader_id, mesa_log);
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
            meta->uniforms[i].gl_type = uam_base_type_to_gl(info.base_type,
                info.vector_elements, info.matrix_columns);
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
            meta->inputs[i].gl_type = uam_base_type_to_gl(iinfo.base_type,
                iinfo.vector_elements, iinfo.matrix_columns);
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
            result = ctx->backend->ops->load_shader_binary(
                ctx->backend, shader_id, dksh, dksh_size);
        }
        free(dksh);
    }

    if (result) {
        /* Free old metadata if any */
        if (sh->mesa_meta) {
            if (sh->mesa_meta->initial_data) free(sh->mesa_meta->initial_data);
            free(sh->mesa_meta);
        }
        sh->mesa_meta = meta;
        sh->compiled_via_mesa = true;

        const char *log = uam_get_error_log(compiler);
        if (log && log[0] != '\0') {
            if (sh->info_log) free(sh->info_log);
            sh->info_log = strdup(log);
        }

        SGL_TRACE_SHADER("shader %u: Mesa direct ES 1.00 compile OK (%d uniforms, %d samplers, constbuf=%u%s)",
                         shader_id, num_uniforms, num_samplers, meta->constbuf_size,
                         remapped ? ", remapped" : "");
    } else {
        if (meta->initial_data) free(meta->initial_data);
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
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

    if (strncmp(p, "#version", 8) == 0) {
        p += 8;
        while (*p == ' ' || *p == '\t') p++;
        int ver = atoi(p);
        if (ver == 100) return true;
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
        strstr(source, "mediump ") || strstr(source, "mediump\t") ||
        strstr(source, "highp ") || strstr(source, "highp\t") ||
        strstr(source, "lowp ") || strstr(source, "lowp\t")) {
        return true;
    }

    return false;
}
#endif /* SGL_ENABLE_RUNTIME_COMPILER */

GL_APICALL void GL_APIENTRY glCompileShader(GLuint shader) {
    GET_CTX();
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
        glslt_stage_t stage = (sh->type == GL_VERTEX_SHADER)
                              ? GLSLT_VERTEX : GLSLT_FRAGMENT;
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
            SGL_TRACE_SHADER("glCompileShader(%u) - gl_DepthRange detected, using transpiler", shader);
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
                SGL_TRACE_SHADER("glCompileShader(%u) - Mesa rejected (too many attribs), deferring to link", shader);
                return;
            }
            /* Mesa rejects some valid ES 1.00 constructs (const struct constructors,
             * certain initializer patterns). Allow transpiler fallback for these.
             * Pattern: any error about const/initializer/constant. */
            if (strstr(sh->info_log, "initializer") ||
                strstr(sh->info_log, "constant") ||
                strstr(sh->info_log, "const ")) {
                free(sh->info_log);
                sh->info_log = NULL;
                SGL_TRACE_SHADER("glCompileShader(%u) - Mesa const issue, trying transpiler", shader);
                /* Fall through to transpiler below */
            } else {
                sh->compiled = false;
                SGL_TRACE_SHADER("glCompileShader(%u) - Mesa rejected with errors, no transpiler fallback", shader);
                return;
            }
        }
        SGL_TRACE_SHADER("glCompileShader(%u) - Mesa failed silently, trying transpiler", shader);
transpiler_fallback:
        {
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
                SGL_TRACE_SHADER("glCompileShader(%u) - transpiler also failed: %s", shader, tres.error);
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
    GET_CTX();
    if (!params) return;

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

GL_APICALL void GL_APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    GET_CTX();

    if (bufSize < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
        return;
    }

    if (sh->info_log && sh->info_log[0]) {
        GLsizei log_len = (GLsizei)strlen(sh->info_log);
        GLsizei copy_len = (bufSize > 0) ? (bufSize - 1) : 0;
        if (copy_len > log_len) copy_len = log_len;
        if (infoLog && bufSize > 0) {
            if (copy_len > 0) memcpy(infoLog, sh->info_log, copy_len);
            infoLog[copy_len] = '\0';
        }
        if (length) *length = copy_len;
    } else {
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
    }
}

GL_APICALL void GL_APIENTRY glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) {
    GET_CTX();

    if (bufSize < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        if (length) *length = 0;
        if (source && bufSize > 0) source[0] = '\0';
        return;
    }

    if (sh->source && sh->source[0]) {
        GLsizei src_len = (GLsizei)strlen(sh->source);
        GLsizei copy_len = (bufSize > 0) ? (bufSize - 1) : 0;
        if (copy_len > src_len) copy_len = src_len;
        if (source && bufSize > 0) {
            if (copy_len > 0) memcpy(source, sh->source, copy_len);
            source[copy_len] = '\0';
        }
        if (length) *length = copy_len;
    } else {
        if (length) *length = 0;
        if (source && bufSize > 0) source[0] = '\0';
    }
}

/* Program Objects */

GL_APICALL GLuint GL_APIENTRY glCreateProgram(void) {
    GET_CTX_RET(0);

    GLuint id = sgl_res_mgr_alloc_program(&ctx->res_mgr);
    if (id == 0) {
        sgl_set_error(ctx, GL_OUT_OF_MEMORY);
        return 0;
    }

    SGL_TRACE_SHADER("glCreateProgram() = %u", id);
    return id;
}

/* Helper: actually free a program's resources (detach shaders, notify backend, release slot) */
static void sgl_program_do_free(sgl_context_t *ctx, GLuint program) {
    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) return;

    if (prog->info_log) { free(prog->info_log); prog->info_log = NULL; }

    GLuint attached[2] = { prog->vertex_shader, prog->fragment_shader };
    prog->vertex_shader = 0;
    prog->fragment_shader = 0;

    if (ctx->backend && ctx->backend->ops->delete_program) {
        ctx->backend->ops->delete_program(ctx->backend, program);
    }

    sgl_res_mgr_free_program(&ctx->res_mgr, program);

    for (int i = 0; i < 2; i++) {
        if (attached[i] == 0) continue;
        sgl_shader_t *sh = GET_SHADER(attached[i]);
        if (sh && sh->attach_count > 0) sh->attach_count--;
        sgl_shader_try_deferred_delete(ctx, attached[i]);
    }
}

GL_APICALL void GL_APIENTRY glDeleteProgram(GLuint program) {
    GET_CTX();
    if (program == 0) return;

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (ctx->current_program == program) {
        /* GLES2 spec: flag for deletion, program stays current until unbound */
        prog->delete_pending = true;
        SGL_TRACE_SHADER("glDeleteProgram(%u) - deferred (current program)", program);
        return;
    }

    sgl_program_do_free(ctx, program);
    SGL_TRACE_SHADER("glDeleteProgram(%u)", program);
}

GL_APICALL GLboolean GL_APIENTRY glIsProgram(GLuint program) {
    GET_CTX_RET(GL_FALSE);
    /* GLES2: program exists (TRUE) until actually freed (after UseProgram(0)) */
    return GET_PROGRAM(program) ? GL_TRUE : GL_FALSE;
}

/* Helper: actually free a shader (called when attach_count reaches 0 and delete_pending) */
static void sgl_shader_try_deferred_delete(sgl_context_t *ctx, GLuint shader) {
    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) return;
    if (!sh->delete_pending || sh->attach_count > 0) return;

    if (ctx->backend && ctx->backend->ops->delete_shader) {
        ctx->backend->ops->delete_shader(ctx->backend, shader);
    }
    sgl_res_mgr_free_shader(&ctx->res_mgr, shader);
    SGL_TRACE_SHADER("shader %u deferred-deleted (attach_count=0)", shader);
}

GL_APICALL void GL_APIENTRY glAttachShader(GLuint program, GLuint shader) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    /* GLES2: Cannot attach same shader twice (GL_INVALID_OPERATION),
     * and cannot attach another shader of the same type. */
    if (prog->vertex_shader == shader || prog->fragment_shader == shader) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if ((sh->type == GL_VERTEX_SHADER && prog->vertex_shader != 0) ||
        (sh->type == GL_FRAGMENT_SHADER && prog->fragment_shader != 0)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    if (sh->type == GL_VERTEX_SHADER) {
        prog->vertex_shader = shader;
    } else if (sh->type == GL_FRAGMENT_SHADER) {
        prog->fragment_shader = shader;
    }

    sh->attach_count++;
    SGL_TRACE_SHADER("glAttachShader(%u, %u) attach_count=%d", program, shader, sh->attach_count);
}

GL_APICALL void GL_APIENTRY glDetachShader(GLuint program, GLuint shader) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GET_PROGRAM(shader) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    /* Check if shader is actually attached to this program */
    if (prog->vertex_shader != shader && prog->fragment_shader != shader) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    if (prog->vertex_shader == shader) prog->vertex_shader = 0;
    if (prog->fragment_shader == shader) prog->fragment_shader = 0;

    if (sh->attach_count > 0) {
        sh->attach_count--;
    }

    SGL_TRACE_SHADER("glDetachShader(%u, %u)", program, shader);

    /* If shader was flagged for deletion and is now unattached, free it */
    sgl_shader_try_deferred_delete(ctx, shader);
}

GL_APICALL void GL_APIENTRY glLinkProgram(GLuint program) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    /* Clear previous link log on relink */
    if (prog->info_log) { free(prog->info_log); prog->info_log = NULL; }

    /* On relink: remove linker-added attrib_bindings, keep only user-bound ones.
     * Without this, stale linker-assigned entries from the previous link cause
     * conflicts when the new link tries to assign locations. */
    {
        int dst = 0;
        for (int i = 0; i < prog->num_attrib_bindings; i++) {
            if (prog->attrib_bindings[i].used && prog->attrib_bindings[i].user_bound) {
                if (dst != i)
                    prog->attrib_bindings[dst] = prog->attrib_bindings[i];
                dst++;
            }
        }
        prog->num_attrib_bindings = dst;
    }

    /* Reset active attrib count for relink — will be set by Mesa or transpiler path below */
    prog->num_active_attribs = 0;

#ifdef SGL_ENABLE_RUNTIME_COMPILER
    sgl_shader_t *vs_sh = prog->vertex_shader ? GET_SHADER(prog->vertex_shader) : NULL;
    sgl_shader_t *fs_sh = prog->fragment_shader ? GET_SHADER(prog->fragment_shader) : NULL;

    /* Check if shaders were compiled via Mesa direct path */
    bool vs_mesa = vs_sh && vs_sh->compiled_via_mesa && vs_sh->mesa_meta;
    bool fs_mesa = fs_sh && fs_sh->compiled_via_mesa && fs_sh->mesa_meta;
    bool any_mesa = vs_mesa || fs_mesa;
    bool any_transpile = (vs_sh && vs_sh->needs_transpile) ||
                         (fs_sh && fs_sh->needs_transpile);

    /* Mixed Mesa/transpiler programs are not allowed — varying locations would
     * be incompatible.  If one shader is Mesa and the other is transpiler,
     * force both to transpiler so the link path is consistent. */
    if (any_mesa && any_transpile) {
        SGL_TRACE_SHADER("mixed Mesa/transpiler in program %u — forcing transpiler for both", program);
        if (vs_mesa) { vs_sh->compiled_via_mesa = false; vs_mesa = false; vs_sh->needs_transpile = true; }
        if (fs_mesa) { fs_sh->compiled_via_mesa = false; fs_mesa = false; fs_sh->needs_transpile = true; }
        any_mesa = false;
    }

    /* If VS was Mesa-compiled and there are attribute bindings, recompile VS
     * with the bindings so Mesa's linker assigns the correct locations.
     * This is necessary because glBindAttribLocation is called AFTER glCompileShader. */
    if (vs_mesa && vs_sh && vs_sh->source && prog->num_attrib_bindings > 0) {
        SGL_TRACE_SHADER("recompiling VS with %d attrib bindings for program %u",
                         prog->num_attrib_bindings, program);
        uam_compiler *compiler = uam_create_compiler(DkStage_Vertex);
        if (compiler) {
            /* Pass attribute bindings to uam */
            for (int i = 0; i < prog->num_attrib_bindings; i++) {
                if (prog->attrib_bindings[i].used) {
                    uam_set_attrib_binding(compiler,
                        prog->attrib_bindings[i].name,
                        (int)prog->attrib_bindings[i].index);
                }
            }

            if (uam_compile_dksh(compiler, vs_sh->source)) {
                /* Load new binary */
                size_t dksh_size = uam_get_code_size(compiler);
                size_t alloc_size = SGL_ALIGN_UP(dksh_size, SGL_PAGE_ALIGNMENT);
                void *dksh = memalign(256, alloc_size);
                if (dksh) {
                    memset(dksh, 0, alloc_size);
                    uam_write_code(compiler, dksh);

                    if (ctx->backend && ctx->backend->ops->load_shader_binary) {
                        ctx->backend->ops->load_shader_binary(
                            ctx->backend, prog->vertex_shader, dksh, dksh_size);
                    }
                    free(dksh);
                }

                /* Update metadata */
                sgl_mesa_metadata_t *meta = vs_sh->mesa_meta;
                if (meta) {
                    meta->num_uniforms = uam_get_num_uniforms(compiler);
                    meta->constbuf_size = uam_get_constbuf_size(compiler);
                    for (int i = 0; i < meta->num_uniforms && i < SGL_MESA_MAX_UNIFORMS; i++) {
                        uam_uniform_info_t info;
                        if (uam_get_uniform_info(compiler, i, &info)) {
                            strncpy(meta->uniforms[i].name, info.name, SGL_ATTRIB_NAME_MAX - 1);
                            meta->uniforms[i].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                            meta->uniforms[i].offset = info.offset;
                            meta->uniforms[i].size_bytes = info.size_bytes;
                            meta->uniforms[i].gl_type = uam_base_type_to_gl(info.base_type,
                                info.vector_elements, info.matrix_columns);
                            meta->uniforms[i].array_elements = info.array_elements;
                        }
                    }
                    meta->num_inputs = uam_get_num_inputs(compiler);
                    for (int i = 0; i < meta->num_inputs && i < SGL_MESA_MAX_INPUTS; i++) {
                        uam_input_info_t iinfo;
                        if (uam_get_input_info(compiler, i, &iinfo)) {
                            strncpy(meta->inputs[i].name, iinfo.name, SGL_ATTRIB_NAME_MAX - 1);
                            meta->inputs[i].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                            meta->inputs[i].location = iinfo.location;
                            meta->inputs[i].gl_type = uam_base_type_to_gl(iinfo.base_type,
                                iinfo.vector_elements, iinfo.matrix_columns);
                        }
                    }
                    /* Update initial constbuf data */
                    if (meta->initial_data) { free(meta->initial_data); meta->initial_data = NULL; }
                    meta->initial_data_size = 0;
                    uint32_t init_size = 0;
                    const void *init_ptr = uam_get_constbuf_initial_data(compiler, &init_size);
                    if (init_ptr && init_size > 0) {
                        meta->initial_data = (uint8_t *)malloc(init_size);
                        if (meta->initial_data) {
                            memcpy(meta->initial_data, init_ptr, init_size);
                            meta->initial_data_size = init_size;
                        }
                    }
                    /* Update depth range offset */
                    meta->depth_range_offset = uam_get_depth_range_offset(compiler);
                }
                SGL_TRACE_SHADER("VS recompile with bindings OK");
            }
            uam_free_compiler(compiler);
        }
    }

    /* If any shader is Mesa-compiled, populate program metadata from Mesa metadata. */
    if (any_mesa && !any_transpile) {
        SGL_TRACE_SHADER("linking Mesa-compiled shaders for program %u", program);

        prog->num_program_uniforms = 0;
        memset(prog->packed_ubo_sizes, 0, sizeof(prog->packed_ubo_sizes));
        prog->num_samplers = 0;

        /* Process each stage's Mesa metadata */
        for (int stage_idx = 0; stage_idx < 2; stage_idx++) {
            sgl_shader_t *sh = (stage_idx == 0) ? vs_sh : fs_sh;
            bool is_mesa = (stage_idx == 0) ? vs_mesa : fs_mesa;
            if (!is_mesa || !sh->mesa_meta) continue;

            sgl_mesa_metadata_t *meta = sh->mesa_meta;

            /* Set packed UBO size for this stage at binding 0.
             * Must cover the FULL constbuf including embedded shader constants
             * (literal values used in compare functions, etc.), not just user
             * uniforms.  initial_data_size reflects the total ParameterValues
             * buffer from Mesa which the GPU shader reads via constbuf. */
            {
                uint32_t cb_size = meta->constbuf_size;
                if (meta->initial_data_size > cb_size)
                    cb_size = meta->initial_data_size;
                if (cb_size > 0)
                    prog->packed_ubo_sizes[stage_idx][0] = cb_size;
            }

            /* Store uniforms */
            for (int i = 0; i < meta->num_uniforms && i < SGL_MESA_MAX_UNIFORMS; i++) {
                if (prog->num_program_uniforms >= SGL_MAX_PROGRAM_UNIFORMS) break;
                int slot = prog->num_program_uniforms++;
                strncpy(prog->program_uniforms[slot].name,
                        meta->uniforms[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->program_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                /* For Mesa, GLES name = uniform name (no transpiler renaming) */
                strncpy(prog->program_uniforms[slot].gles_name,
                        meta->uniforms[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->program_uniforms[slot].gles_name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                prog->program_uniforms[slot].location = (GLint)(
                    SGL_LOC_PACKED_FLAG |
                    ((unsigned)stage_idx << SGL_LOC_STAGE_SHIFT) |
                    ((unsigned)0 << SGL_LOC_BINDING_SHIFT) |
                    (unsigned)meta->uniforms[i].offset);
                prog->program_uniforms[slot].gl_type = meta->uniforms[i].gl_type;
                prog->program_uniforms[slot].array_size =
                    meta->uniforms[i].array_elements > 0 ? meta->uniforms[i].array_elements : 0;
                /* Mesa constbuf stride: size_bytes / array_elements (NOT std140).
                 * Mesa packs float arrays at 4-byte stride, not 16-byte. */
                if (meta->uniforms[i].array_elements > 1 && meta->uniforms[i].size_bytes > 0)
                    prog->program_uniforms[slot].element_stride =
                        (uint16_t)(meta->uniforms[i].size_bytes / meta->uniforms[i].array_elements);
                else
                    prog->program_uniforms[slot].element_stride = 0;
                prog->program_uniforms[slot].used = true;
            }

            /* Store samplers */
            for (int i = 0; i < meta->num_samplers && i < SGL_MESA_MAX_SAMPLERS; i++) {
                /* Deduplicate: check if same-named sampler exists from other stage */
                bool already = false;
                for (int j = 0; j < prog->num_samplers; j++) {
                    if (prog->samplers[j].used &&
                        strcmp(prog->samplers[j].name, meta->samplers[i].name) == 0) {
                        /* Same sampler in both stages — store each stage's binding.
                         * VS is processed first (stage_idx=0), FS second (stage_idx=1).
                         * shader_binding = FS binding, vs_shader_binding = VS binding. */
                        if (stage_idx == 0)
                            prog->samplers[j].vs_shader_binding = meta->samplers[i].binding;
                        else
                            prog->samplers[j].shader_binding = meta->samplers[i].binding;
                        already = true;
                        break;
                    }
                }
                if (already) continue;
                if (prog->num_samplers >= SGL_MAX_PROGRAM_SAMPLERS) break;
                int slot = prog->num_samplers++;
                strncpy(prog->samplers[slot].name,
                        meta->samplers[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->samplers[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                strncpy(prog->samplers[slot].gles_name,
                        meta->samplers[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->samplers[slot].gles_name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                prog->samplers[slot].shader_binding = meta->samplers[i].binding;
                if (stage_idx == 0)
                    prog->samplers[slot].vs_shader_binding = meta->samplers[i].binding;
                else
                    prog->samplers[slot].vs_shader_binding = -1;
                prog->samplers[slot].tex_unit = 0; /* GLES2 spec default */
                prog->samplers[slot].array_index = -1; /* set below if array */
                prog->samplers[slot].array_total = 0;
                prog->samplers[slot].gl_type = meta->samplers[i].gl_type;
                prog->samplers[slot].used = true;
            }
        }

        /* Detect sampler arrays from Mesa bracket notation.
         * Mesa reports sampler array elements as "u_var[0]", "u_var[1]", etc.
         * Parse bracket to set array_index/array_total for each element.
         * This enables glGetUniformLocation("u_var") → first element,
         * and glGetUniformLocation("u_var[N]") → Nth element. */
        for (int i = 0; i < prog->num_samplers; i++) {
            if (!prog->samplers[i].used) continue;
            const char *sn = prog->samplers[i].gles_name[0]
                ? prog->samplers[i].gles_name : prog->samplers[i].name;
            /* Use strrchr to find LAST bracket — avoids confusing struct
             * array subscripts (u_var[0].m0) with sampler arrays (u_arr[0]).
             * A terminal bracket (no dot after) means sampler array element. */
            const char *br = strrchr(sn, '[');
            if (!br || strchr(br, '.') != NULL) {
                prog->samplers[i].array_index = 0;
                continue;
            }
            int idx = atoi(br + 1);
            prog->samplers[i].array_index = idx;
            /* Count total elements with same base name */
            size_t base_len = br - sn;
            int total = 0;
            for (int j = 0; j < prog->num_samplers; j++) {
                if (!prog->samplers[j].used) continue;
                const char *jn = prog->samplers[j].gles_name[0]
                    ? prog->samplers[j].gles_name : prog->samplers[j].name;
                const char *jbr = strrchr(jn, '[');
                if (!jbr || strchr(jbr, '.') != NULL) continue;
                size_t jbase_len = jbr - jn;
                if (jbase_len == base_len && strncmp(sn, jn, base_len) == 0)
                    total++;
            }
            prog->samplers[i].array_total = total;
        }

        /* Populate attrib bindings from VS input metadata */
        if (vs_mesa && vs_sh->mesa_meta) {
            sgl_mesa_metadata_t *meta = vs_sh->mesa_meta;
            /* GL_ACTIVE_ATTRIBUTES = count of attributes in the compiled shader,
             * NOT the total num_attrib_bindings (which includes inactive user-bound). */
            prog->num_active_attribs = (meta->num_inputs < SGL_MESA_MAX_INPUTS)
                ? meta->num_inputs : SGL_MESA_MAX_INPUTS;
            /* Clear in_shader flag and linked_location for all existing bindings */
            for (int j = 0; j < prog->num_attrib_bindings; j++) {
                prog->attrib_bindings[j].in_shader = false;
                prog->attrib_bindings[j].linked_location = -1;
            }
            for (int i = 0; i < meta->num_inputs && i < SGL_MESA_MAX_INPUTS; i++) {
                bool found = false;
                for (int j = 0; j < prog->num_attrib_bindings; j++) {
                    if (strcmp(prog->attrib_bindings[j].name,
                               meta->inputs[i].name) == 0) {
                        prog->attrib_bindings[j].index = meta->inputs[i].location;
                        prog->attrib_bindings[j].linked_location = (GLint)meta->inputs[i].location;
                        prog->attrib_bindings[j].gl_type = meta->inputs[i].gl_type;
                        prog->attrib_bindings[j].in_shader = true;
                        found = true;
                        break;
                    }
                }
                if (!found && prog->num_attrib_bindings < SGL_MAX_ATTRIB_BINDINGS) {
                    int slot = prog->num_attrib_bindings++;
                    strncpy(prog->attrib_bindings[slot].name,
                            meta->inputs[i].name, SGL_ATTRIB_NAME_MAX - 1);
                    prog->attrib_bindings[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                    prog->attrib_bindings[slot].index = meta->inputs[i].location;
                    prog->attrib_bindings[slot].linked_location = (GLint)meta->inputs[i].location;
                    prog->attrib_bindings[slot].gl_type = meta->inputs[i].gl_type;
                    prog->attrib_bindings[slot].used = true;
                    prog->attrib_bindings[slot].user_bound = false; /* linker-added */
                    prog->attrib_bindings[slot].in_shader = true;
                }
            }
            /* Count active attribs — count ALL attributes with in_shader=true.
             * Aliased attributes (same location via glBindAttribLocation) are each
             * individually active per GLES2 spec and must all appear in
             * glGetActiveAttrib enumeration. */
            {
                int active_count = 0;
                for (int j = 0; j < prog->num_attrib_bindings; j++) {
                    if (prog->attrib_bindings[j].in_shader) active_count++;
                }
                prog->num_active_attribs = active_count;
            }
        }

        /* Pre-configure packed UBOs and load initial constbuf data from Mesa */
        for (int stage = 0; stage < 2; stage++) {
            for (int binding = 0; binding < SGL_MAX_PACKED_UBOS; binding++) {
                int ubo_size = prog->packed_ubo_sizes[stage][binding];
                if (ubo_size > 0 && ubo_size <= SGL_MAX_PACKED_UBO_SIZE) {
                    sgl_packed_ubo_t *packed = (stage == 0)
                        ? &prog->packed_vertex[binding]
                        : &prog->packed_fragment[binding];
                    if (!packed->valid) {
                        packed->size = ubo_size;
                        packed->valid = true;
                        packed->dirty = false;
                        memset(packed->data, 0, ubo_size);
                        /* Load initial constbuf data (Mesa-embedded literals/constants).
                         * Only for binding 0 (the driver constbuf). */
                        if (binding == 0) {
                            sgl_shader_t *sh = (stage == 0) ? vs_sh : fs_sh;
                            if (sh && sh->mesa_meta && sh->mesa_meta->initial_data) {
                                uint32_t copy = sh->mesa_meta->initial_data_size;
                                if (copy > (uint32_t)ubo_size) copy = (uint32_t)ubo_size;
                                memcpy(packed->data, sh->mesa_meta->initial_data, copy);
                            }
                        }
                    }
                }
            }
        }

        /* Set up dual-stage mirrors for uniforms present in both VS and FS */
        prog->num_packed_mirrors = 0;
        for (int i = 0; i < prog->num_program_uniforms; i++) {
            if (!prog->program_uniforms[i].used) continue;
            GLint iloc = prog->program_uniforms[i].location;
            if (!(iloc & SGL_LOC_PACKED_FLAG)) continue;
            int i_stage = (iloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
            if (i_stage != 0) continue;
            for (int j = 0; j < prog->num_program_uniforms; j++) {
                if (j == i || !prog->program_uniforms[j].used) continue;
                GLint jloc = prog->program_uniforms[j].location;
                if (!(jloc & SGL_LOC_PACKED_FLAG)) continue;
                int j_stage = (jloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
                if (j_stage == 0) continue;
                if (strcmp(prog->program_uniforms[j].name,
                          prog->program_uniforms[i].name) == 0) {
                    if (prog->num_packed_mirrors < SGL_MAX_PACKED_MIRRORS) {
                        prog->packed_mirrors[prog->num_packed_mirrors].primary = iloc;
                        prog->packed_mirrors[prog->num_packed_mirrors].mirror = jloc;
                        prog->num_packed_mirrors++;
                    }
                    break;
                }
            }
        }

        /* Debug: dump link-time state */
        /* gl_DepthRange: check if either VS or FS uses it (Mesa state variable).
         * Build packed UBO locations so draw-time code can update near/far/diff. */
        prog->has_depth_range = false;
        prog->depth_range_loc[0] = 0;
        prog->depth_range_loc[1] = 0;
        prog->depth_range_loc[2] = 0;
        prog->depth_range_loc_fs[0] = 0;
        prog->depth_range_loc_fs[1] = 0;
        prog->depth_range_loc_fs[2] = 0;
        for (int stage_idx = 0; stage_idx < 2; stage_idx++) {
            sgl_shader_t *sh = (stage_idx == 0) ? vs_sh : fs_sh;
            bool is_mesa = (stage_idx == 0) ? vs_mesa : fs_mesa;
            if (!is_mesa || !sh->mesa_meta) continue;
            int dr_off = sh->mesa_meta->depth_range_offset;
            if (dr_off < 0) continue;
            prog->has_depth_range = true;
            /* Build packed location: stage | binding 0 | byte offset */
            GLint *dr_locs = (stage_idx == 0) ? prog->depth_range_loc : prog->depth_range_loc_fs;
            for (int d = 0; d < 3; d++) {
                dr_locs[d] = (GLint)(SGL_LOC_PACKED_FLAG
                    | ((uint32_t)stage_idx << SGL_LOC_STAGE_SHIFT)
                    | (0u << SGL_LOC_BINDING_SHIFT)
                    | (uint32_t)(dr_off + d * 4));
            }
        }

        /* Link-time varying type check for Mesa-compiled shaders.
         * Mesa compiles each stage independently — cross-stage type mismatches
         * (e.g. VS "varying float var" vs FS "varying vec2 var") are not caught. */
        if (vs_mesa && fs_mesa && vs_sh->source && fs_sh->source) {
            typedef struct { char name[64]; char type[32]; } vdecl_t;
            vdecl_t vs_v[64], fs_v[64];
            int vs_nv = 0, fs_nv = 0;
            for (int pass = 0; pass < 2; pass++) {
                const char *src = (pass == 0) ? vs_sh->source : fs_sh->source;
                vdecl_t *v = (pass == 0) ? vs_v : fs_v;
                int *nv = (pass == 0) ? &vs_nv : &fs_nv;
                const char *ln = src;
                while (ln && *ln && *nv < 64) {
                    while (*ln == ' ' || *ln == '\t') ln++;
                    const char *p = ln;
                    if (strncmp(p, "varying", 7) == 0 && (p[7] == ' ' || p[7] == '\t')) {
                        p += 7;
                        while (*p == ' ' || *p == '\t') p++;
                        if (strncmp(p, "lowp ", 5) == 0 || strncmp(p, "mediump ", 8) == 0 || strncmp(p, "highp ", 6) == 0) {
                            while (*p && *p != ' ' && *p != '\t') p++;
                            while (*p == ' ' || *p == '\t') p++;
                        }
                        const char *ts = p;
                        while (*p && *p != ' ' && *p != '\t' && *p != ';') p++;
                        size_t tl = p - ts;
                        while (*p == ' ' || *p == '\t') p++;
                        const char *ns = p;
                        while (*p && *p != ' ' && *p != '\t' && *p != ';' && *p != '[') p++;
                        size_t nl = p - ns;
                        if (tl > 0 && tl < 32 && nl > 0 && nl < 64) {
                            memcpy(v[*nv].type, ts, tl); v[*nv].type[tl] = '\0';
                            memcpy(v[*nv].name, ns, nl); v[*nv].name[nl] = '\0';
                            (*nv)++;
                        }
                    }
                    const char *nl = strchr(ln, '\n');
                    ln = nl ? nl + 1 : NULL;
                }
            }
            for (int i = 0; i < vs_nv; i++) {
                for (int j = 0; j < fs_nv; j++) {
                    if (strcmp(vs_v[i].name, fs_v[j].name) == 0 &&
                        strcmp(vs_v[i].type, fs_v[j].type) != 0) {
                        prog->linked = false;
                        if (prog->info_log) free(prog->info_log);
                        char msg[256];
                        snprintf(msg, sizeof(msg), "varying '%s' type mismatch: VS=%s, FS=%s",
                                 vs_v[i].name, vs_v[i].type, fs_v[j].type);
                        prog->info_log = strdup(msg);
                        return;
                    }
                }
            }
        }

        /* Populate active_uniforms[] */
        prog->num_active_uniforms = 0;
        for (int i = 0; i < prog->num_program_uniforms; i++) {
            if (!prog->program_uniforms[i].used) continue;
            const char *uni_name = prog->program_uniforms[i].gles_name[0]
                ? prog->program_uniforms[i].gles_name
                : prog->program_uniforms[i].name;
            int arr = prog->program_uniforms[i].array_size;
            /* Deduplicate — Mesa stores array names with [0] suffix already */
            bool already = false;
            char uni_name_arr[SGL_ATTRIB_NAME_MAX];
            if (arr > 1) {
                if (strchr(uni_name, '['))
                    strncpy(uni_name_arr, uni_name, SGL_ATTRIB_NAME_MAX - 1);
                else
                    snprintf(uni_name_arr, SGL_ATTRIB_NAME_MAX, "%s[0]", uni_name);
                uni_name_arr[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            }
            for (int j = 0; j < prog->num_active_uniforms; j++) {
                if (strcmp(prog->active_uniforms[j].name, uni_name) == 0 ||
                    (arr > 1 && strcmp(prog->active_uniforms[j].name, uni_name_arr) == 0)) {
                    already = true; break;
                }
            }
            if (already) continue;
            if (prog->num_active_uniforms >= SGL_MAX_UNIFORMS * 2) break;
            int slot = prog->num_active_uniforms++;
            if (arr > 1) {
                if (strchr(uni_name, '['))
                    strncpy(prog->active_uniforms[slot].name, uni_name, SGL_ATTRIB_NAME_MAX - 1);
                else
                    snprintf(prog->active_uniforms[slot].name, SGL_ATTRIB_NAME_MAX,
                             "%s[0]", uni_name);
                prog->active_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            } else {
                strncpy(prog->active_uniforms[slot].name, uni_name, SGL_ATTRIB_NAME_MAX - 1);
                prog->active_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            }
            prog->active_uniforms[slot].type = prog->program_uniforms[i].gl_type
                ? prog->program_uniforms[i].gl_type : GL_FLOAT_VEC4;
            prog->active_uniforms[slot].size = (arr > 1) ? arr : 1;
            prog->active_uniforms[slot].location = prog->program_uniforms[i].location;
            prog->active_uniforms[slot].element_stride = prog->program_uniforms[i].element_stride;
            prog->active_uniforms[slot].active = true;
        }
        /* Add samplers as active uniforms.
         * For sampler arrays, add only one entry (first element) with size = array_total.
         * Per GLES spec, glGetActiveUniform returns "s[0]" with size=N for sampler arrays. */
        for (int i = 0; i < prog->num_samplers; i++) {
            if (!prog->samplers[i].used) continue;
            if (prog->num_active_uniforms >= SGL_MAX_UNIFORMS * 2) break;
            /* Skip non-first elements of sampler arrays */
            if (prog->samplers[i].array_total > 0 && prog->samplers[i].array_index > 0)
                continue;
            const char *samp_name = prog->samplers[i].gles_name[0]
                ? prog->samplers[i].gles_name : prog->samplers[i].name;
            /* For arrays, ensure the name has [0] suffix */
            char name_buf[SGL_ATTRIB_NAME_MAX];
            if (prog->samplers[i].array_total > 0 && !strchr(samp_name, '[')) {
                snprintf(name_buf, SGL_ATTRIB_NAME_MAX, "%s[0]", samp_name);
                samp_name = name_buf;
            }
            bool already = false;
            for (int j = 0; j < prog->num_active_uniforms; j++) {
                if (strcmp(prog->active_uniforms[j].name, samp_name) == 0) {
                    already = true; break;
                }
            }
            if (already) continue;
            int slot = prog->num_active_uniforms++;
            strncpy(prog->active_uniforms[slot].name, samp_name, SGL_ATTRIB_NAME_MAX - 1);
            prog->active_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            prog->active_uniforms[slot].type = prog->samplers[i].gl_type
                ? prog->samplers[i].gl_type : GL_SAMPLER_2D;
            prog->active_uniforms[slot].size = prog->samplers[i].array_total > 0
                ? prog->samplers[i].array_total : 1;
            prog->active_uniforms[slot].location = (GLint)(SGL_LOC_SAMPLER_FLAG | (unsigned)i);
            prog->active_uniforms[slot].active = true;
        }

        SGL_TRACE_SHADER("Mesa link: %d uniforms, %d samplers, %d active, %d mirrors",
                         prog->num_program_uniforms, prog->num_samplers,
                         prog->num_active_uniforms, prog->num_packed_mirrors);
    }

    /* Check if any attached shader needs transpilation (GLSL ES 1.00 → 4.60) */
    bool needs_transpile = (vs_sh && vs_sh->needs_transpile) ||
                           (fs_sh && fs_sh->needs_transpile);

    if (needs_transpile) {
        SGL_TRACE_SHADER("transpiling ES 1.00 shaders for program %u", program);

        /* 1. Set up transpiler options with attrib bindings from glBindAttribLocation */
        glslt_options_t vs_opts;
        glslt_options_init(&vs_opts);

        for (int i = 0; i < prog->num_attrib_bindings; i++) {
            if (prog->attrib_bindings[i].used) {
                glslt_set_attrib_location(&vs_opts,
                    prog->attrib_bindings[i].name,
                    (int)prog->attrib_bindings[i].index);
            }
        }

        /* 2. Transpile vertex shader */
        glslt_result_t vs_result;
        memset(&vs_result, 0, sizeof(vs_result));
        vs_result.success = 1; /* default to success if no VS to transpile */

        if (vs_sh && vs_sh->needs_transpile && vs_sh->source) {
            vs_result = glslt_transpile(vs_sh->source, GLSLT_VERTEX, &vs_opts);
            if (!vs_result.success) {
                SGL_TRACE_SHADER("VS transpile failed: %s", vs_result.error);
                if (vs_sh->info_log) free(vs_sh->info_log);
                vs_sh->info_log = strdup(vs_result.error);
                vs_sh->compiled = false;
                prog->linked = false;
                if (prog->info_log) free(prog->info_log);
                prog->info_log = strdup(vs_result.error[0] ? vs_result.error : "VS transpile failed");
                glslt_result_free(&vs_result);
                SGL_TRACE_SHADER("glLinkProgram(%u) - VS transpile FAILED", program);
                return;
            }
            SGL_TRACE_SHADER("VS transpiled: %d uniforms, %d samplers, %d attribs, %d varyings",
                             vs_result.num_uniforms, vs_result.num_samplers,
                             vs_result.num_attributes, vs_result.num_varyings);
        }

        /* 3. Transpile fragment shader (pass VS varying locations for consistency) */
        glslt_result_t fs_result;
        memset(&fs_result, 0, sizeof(fs_result));
        fs_result.success = 1;

        if (fs_sh && fs_sh->needs_transpile && fs_sh->source) {
            glslt_options_t fs_opts;
            glslt_options_init(&fs_opts);

            /* Pass VS varying locations so FS uses matching locations */
            for (int i = 0; i < vs_result.num_varyings; i++) {
                glslt_set_varying_location(&fs_opts,
                    vs_result.varyings[i].name,
                    vs_result.varyings[i].location);
            }

            fs_result = glslt_transpile(fs_sh->source, GLSLT_FRAGMENT, &fs_opts);
            if (!fs_result.success) {
                SGL_TRACE_SHADER("FS transpile failed: %s", fs_result.error);
                if (fs_sh->info_log) free(fs_sh->info_log);
                fs_sh->info_log = strdup(fs_result.error);
                fs_sh->compiled = false;
                prog->linked = false;
                if (prog->info_log) free(prog->info_log);
                prog->info_log = strdup(fs_result.error[0] ? fs_result.error : "FS transpile failed");
                glslt_result_free(&vs_result);
                glslt_result_free(&fs_result);
                SGL_TRACE_SHADER("glLinkProgram(%u) - FS transpile FAILED", program);
                return;
            }
            SGL_TRACE_SHADER("FS transpiled: %d uniforms, %d samplers, %d varyings",
                             fs_result.num_uniforms, fs_result.num_samplers, fs_result.num_varyings);
        }

        /* 3b. Link-time varying type check: VS outputs must match FS inputs */
        if (vs_result.success && fs_result.success) {
            for (int i = 0; i < vs_result.num_varyings; i++) {
                for (int j = 0; j < fs_result.num_varyings; j++) {
                    if (strcmp(vs_result.varyings[i].name,
                              fs_result.varyings[j].name) == 0) {
                        if (vs_result.varyings[i].type != fs_result.varyings[j].type ||
                            vs_result.varyings[i].array_size != fs_result.varyings[j].array_size) {
                            prog->linked = false;
                            if (prog->info_log) free(prog->info_log);
                            char msg[256];
                            snprintf(msg, sizeof(msg),
                                     "varying '%s' type mismatch between VS and FS",
                                     vs_result.varyings[i].name);
                            prog->info_log = strdup(msg);
                            glslt_result_free(&vs_result);
                            glslt_result_free(&fs_result);
                            SGL_TRACE_SHADER("glLinkProgram(%u) - %s", program, msg);
                            return;
                        }
                        break;
                    }
                }
            }
        }

        /* 4. Compile transpiled GLSL 4.60 → DKSH via libuam */
        if (vs_sh && vs_sh->needs_transpile && vs_result.output) {
            if (vs_sh->info_log) { free(vs_sh->info_log); vs_sh->info_log = NULL; }
            vs_sh->compiled = sgl_compile_glsl460(ctx, prog->vertex_shader,
                                                   vs_sh, vs_result.output);
            if (!vs_sh->compiled) {
                SGL_TRACE_SHADER("VS compile failed after transpile");
                prog->linked = false;
                if (prog->info_log) free(prog->info_log);
                prog->info_log = vs_sh->info_log ? strdup(vs_sh->info_log) : strdup("VS compile failed");
                glslt_result_free(&vs_result);
                glslt_result_free(&fs_result);
                SGL_TRACE_SHADER("glLinkProgram(%u) - VS compile FAILED", program);
                return;
            }
            vs_sh->needs_transpile = false;
        }

        if (fs_sh && fs_sh->needs_transpile && fs_result.output) {
            if (fs_sh->info_log) { free(fs_sh->info_log); fs_sh->info_log = NULL; }
            fs_sh->compiled = sgl_compile_glsl460(ctx, prog->fragment_shader,
                                                   fs_sh, fs_result.output);
            if (!fs_sh->compiled) {
                SGL_TRACE_SHADER("FS compile failed after transpile");
                prog->linked = false;
                if (prog->info_log) free(prog->info_log);
                prog->info_log = fs_sh->info_log ? strdup(fs_sh->info_log) : strdup("FS compile failed");
                glslt_result_free(&vs_result);
                glslt_result_free(&fs_result);
                SGL_TRACE_SHADER("glLinkProgram(%u) - FS compile FAILED", program);
                return;
            }
            fs_sh->needs_transpile = false;
        }

        /* 5. Store uniforms PER-PROGRAM from transpiler reflection data.
         * Each program gets its own uniform name→location mapping, because
         * different programs have different std140 layouts (uniforms sorted
         * alphabetically per-shader → different offsets per program). */
        prog->num_program_uniforms = 0;
        memset(prog->packed_ubo_sizes, 0, sizeof(prog->packed_ubo_sizes));

        /* VS uniforms → packed UBO at VS binding 0 */
        if (vs_result.num_uniforms > 0) {
            prog->packed_ubo_sizes[0][vs_opts.ubo_binding] = vs_result.ubo_total_size;
            for (int i = 0; i < vs_result.num_uniforms; i++) {
                if (prog->num_program_uniforms < SGL_MAX_PROGRAM_UNIFORMS) {
                    int slot = prog->num_program_uniforms++;
                    strncpy(prog->program_uniforms[slot].name,
                            vs_result.uniforms[i].name, SGL_ATTRIB_NAME_MAX - 1);
                    prog->program_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                    /* Store GLES name (dot notation for structs) */
                    const char *gn = vs_result.uniforms[i].gles_name[0]
                        ? vs_result.uniforms[i].gles_name : vs_result.uniforms[i].name;
                    strncpy(prog->program_uniforms[slot].gles_name, gn, SGL_ATTRIB_NAME_MAX - 1);
                    prog->program_uniforms[slot].gles_name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                    prog->program_uniforms[slot].location = (GLint)(
                        SGL_LOC_PACKED_FLAG |
                        ((unsigned)0 << SGL_LOC_STAGE_SHIFT) |
                        ((unsigned)vs_opts.ubo_binding << SGL_LOC_BINDING_SHIFT) |
                        (unsigned)vs_result.uniforms[i].offset);
                    prog->program_uniforms[slot].gl_type = glslt_to_gl_type(vs_result.uniforms[i].type);
                    prog->program_uniforms[slot].array_size = vs_result.uniforms[i].array_size;
                    /* Transpiler uses std140: arrays padded to 16-byte minimum */
                    prog->program_uniforms[slot].element_stride = 0; /* 0 = use std140 default */
                    prog->program_uniforms[slot].used = true;
                }
            }
            SGL_TRACE_SHADER("stored %d VS uniforms in program (UBO size=%d)",
                             vs_result.num_uniforms, vs_result.ubo_total_size);
        }
        /* FS uniforms → packed UBO at FS binding 0 */
        if (fs_result.num_uniforms > 0) {
            prog->packed_ubo_sizes[1][0] = fs_result.ubo_total_size;
            for (int i = 0; i < fs_result.num_uniforms; i++) {
                if (prog->num_program_uniforms < SGL_MAX_PROGRAM_UNIFORMS) {
                    int slot = prog->num_program_uniforms++;
                    strncpy(prog->program_uniforms[slot].name,
                            fs_result.uniforms[i].name, SGL_ATTRIB_NAME_MAX - 1);
                    prog->program_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                    /* Store GLES name (dot notation for structs) */
                    const char *gn = fs_result.uniforms[i].gles_name[0]
                        ? fs_result.uniforms[i].gles_name : fs_result.uniforms[i].name;
                    strncpy(prog->program_uniforms[slot].gles_name, gn, SGL_ATTRIB_NAME_MAX - 1);
                    prog->program_uniforms[slot].gles_name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                    prog->program_uniforms[slot].location = (GLint)(
                        SGL_LOC_PACKED_FLAG |
                        ((unsigned)1 << SGL_LOC_STAGE_SHIFT) |
                        ((unsigned)0 << SGL_LOC_BINDING_SHIFT) |
                        (unsigned)fs_result.uniforms[i].offset);
                    prog->program_uniforms[slot].gl_type = glslt_to_gl_type(fs_result.uniforms[i].type);
                    prog->program_uniforms[slot].array_size = fs_result.uniforms[i].array_size;
                    /* Transpiler uses std140: arrays padded to 16-byte minimum */
                    prog->program_uniforms[slot].element_stride = 0; /* 0 = use std140 default */
                    prog->program_uniforms[slot].used = true;
                }
            }
            SGL_TRACE_SHADER("stored %d FS uniforms in program (UBO size=%d)",
                             fs_result.num_uniforms, fs_result.ubo_total_size);
        }

        /* 5b. Pre-configure packed UBOs so they're valid at draw time.
         * Without this, packed UBOs only become valid when glGetUniformLocation
         * is called (which configures them lazily). If an app sets uniforms via
         * cached locations without calling glGetUniformLocation first, the packed
         * UBO stays invalid and draws get no uniform data. */
        for (int stage = 0; stage < 2; stage++) {
            for (int binding = 0; binding < SGL_MAX_PACKED_UBOS; binding++) {
                int ubo_size = prog->packed_ubo_sizes[stage][binding];
                if (ubo_size > 0 && ubo_size <= SGL_MAX_PACKED_UBO_SIZE) {
                    sgl_packed_ubo_t *packed = (stage == 0)
                        ? &prog->packed_vertex[binding]
                        : &prog->packed_fragment[binding];
                    if (!packed->valid) {
                        packed->size = ubo_size;
                        packed->valid = true;
                        packed->dirty = false;
                        memset(packed->data, 0, ubo_size);
                    }
                }
            }
        }

        /* 5c. Set up dual-stage mirrors for uniforms present in both VS and FS.
         * This must happen at link time so that glUniform* writes go to both
         * packed UBOs even if glGetUniformLocation wasn't called first.
         * apply_packed_mirror uses relative offsets, so one mirror per base
         * uniform covers all array elements. */
        prog->num_packed_mirrors = 0;
        for (int i = 0; i < prog->num_program_uniforms; i++) {
            if (!prog->program_uniforms[i].used) continue;
            GLint iloc = prog->program_uniforms[i].location;
            if (!(iloc & SGL_LOC_PACKED_FLAG)) continue;
            int i_stage = (iloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
            if (i_stage != 0) continue; /* Only VS entries as primary */
            /* Find matching FS entry with same name */
            for (int j = 0; j < prog->num_program_uniforms; j++) {
                if (j == i || !prog->program_uniforms[j].used) continue;
                GLint jloc = prog->program_uniforms[j].location;
                if (!(jloc & SGL_LOC_PACKED_FLAG)) continue;
                int j_stage = (jloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
                if (j_stage == 0) continue; /* Must be FS */
                if (strcmp(prog->program_uniforms[j].name,
                          prog->program_uniforms[i].name) == 0) {
                    if (prog->num_packed_mirrors < SGL_MAX_PACKED_MIRRORS) {
                        prog->packed_mirrors[prog->num_packed_mirrors].primary = iloc;
                        prog->packed_mirrors[prog->num_packed_mirrors].mirror = jloc;
                        prog->num_packed_mirrors++;
                    }
                    break;
                }
            }
        }

        /* 5d. Detect gl_DepthRange usage — store packed locations for near/far/diff
         * so the runtime can populate them from viewport state before each draw. */
        prog->has_depth_range = (vs_result.has_depth_range || fs_result.has_depth_range);
        prog->depth_range_loc[0] = 0;
        prog->depth_range_loc[1] = 0;
        prog->depth_range_loc[2] = 0;
        prog->depth_range_loc_fs[0] = 0;
        prog->depth_range_loc_fs[1] = 0;
        prog->depth_range_loc_fs[2] = 0;
        if (prog->has_depth_range) {
            static const char *dr_names[] = { "sgl_dr_near", "sgl_dr_far", "sgl_dr_diff" };
            for (int d = 0; d < 3; d++) {
                for (int u = 0; u < prog->num_program_uniforms; u++) {
                    if (strcmp(prog->program_uniforms[u].name, dr_names[d]) == 0) {
                        prog->depth_range_loc[d] = prog->program_uniforms[u].location;
                        break;
                    }
                }
            }
        }

        /* 6. Store sampler bindings from FS transpiler result.
         * Samplers are separate from UBO uniforms — they stay as
         * layout(binding=N) uniform sampler2D and need special handling
         * so glGetUniformLocation returns a valid location and
         * glUniform1i can remap texture units to shader bindings. */
        prog->num_samplers = 0;
        if (fs_result.num_samplers > 0) {
            for (int i = 0; i < fs_result.num_samplers && i < SGL_MAX_PROGRAM_SAMPLERS; i++) {
                int slot = prog->num_samplers++;
                strncpy(prog->samplers[slot].name,
                        fs_result.samplers[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->samplers[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                /* Store GLES name (e.g. "s[0]" for sampler arrays) */
                const char *sgn = fs_result.samplers[i].gles_name[0]
                    ? fs_result.samplers[i].gles_name : fs_result.samplers[i].name;
                strncpy(prog->samplers[slot].gles_name, sgn, SGL_ATTRIB_NAME_MAX - 1);
                prog->samplers[slot].gles_name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                prog->samplers[slot].shader_binding = fs_result.samplers[i].binding;
                prog->samplers[slot].vs_shader_binding = -1; /* Will be set if VS also has this sampler */
                prog->samplers[slot].tex_unit = 0; /* GLES2 spec: initial sampler value = 0 */
                prog->samplers[slot].array_index = fs_result.samplers[i].array_index;
                prog->samplers[slot].array_total = fs_result.samplers[i].array_total;
                prog->samplers[slot].gl_type = glslt_to_gl_type(fs_result.samplers[i].type);
                prog->samplers[slot].used = true;
            }
            SGL_TRACE_SHADER("stored %d FS samplers in program", fs_result.num_samplers);
        }
        /* Also check VS samplers — deduplicate same-name entries but store VS binding.
         * Per GLES2 spec, a uniform sampler used in both stages has a single location.
         * We store vs_shader_binding so draw code can bind to each stage independently. */
        if (vs_result.num_samplers > 0) {
            for (int i = 0; i < vs_result.num_samplers && prog->num_samplers < SGL_MAX_PROGRAM_SAMPLERS; i++) {
                /* Deduplicate: if same name exists from FS, store VS binding in that entry */
                bool already = false;
                for (int j = 0; j < prog->num_samplers; j++) {
                    if (prog->samplers[j].used &&
                        strcmp(prog->samplers[j].name, vs_result.samplers[i].name) == 0) {
                        /* Store VS binding for per-stage texture binding at draw time */
                        prog->samplers[j].vs_shader_binding = vs_result.samplers[i].binding;
                        already = true;
                        break;
                    }
                }
                if (already) continue;
                /* VS-only sampler (not in FS) */
                int slot = prog->num_samplers++;
                strncpy(prog->samplers[slot].name,
                        vs_result.samplers[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->samplers[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                const char *sgn = vs_result.samplers[i].gles_name[0]
                    ? vs_result.samplers[i].gles_name : vs_result.samplers[i].name;
                strncpy(prog->samplers[slot].gles_name, sgn, SGL_ATTRIB_NAME_MAX - 1);
                prog->samplers[slot].gles_name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                prog->samplers[slot].shader_binding = vs_result.samplers[i].binding;
                prog->samplers[slot].vs_shader_binding = vs_result.samplers[i].binding;
                prog->samplers[slot].tex_unit = 0; /* GLES2 spec: initial sampler value = 0 */
                prog->samplers[slot].array_index = vs_result.samplers[i].array_index;
                prog->samplers[slot].array_total = vs_result.samplers[i].array_total;
                prog->samplers[slot].gl_type = glslt_to_gl_type(vs_result.samplers[i].type);
                prog->samplers[slot].used = true;
            }
        }

        /* 7. Populate active_uniforms[] at link time for glGetActiveUniform/GL_ACTIVE_UNIFORMS.
         * dEQP calls glGetActiveUniform(index) WITHOUT prior glGetUniformLocation(),
         * so active_uniforms must be populated here, not lazily. */
        prog->num_active_uniforms = 0;

        /* Add non-sampler uniforms (deduplicate VS/FS copies with same name) */
        for (int i = 0; i < prog->num_program_uniforms; i++) {
            if (!prog->program_uniforms[i].used) continue;
            /* Deduplicate: same name in both VS and FS → one active uniform.
             * Must check both raw name and name[0] form since arrays are stored
             * with [0] suffix per GLES2 spec. */
            bool already = false;
            const char *uni_name = prog->program_uniforms[i].gles_name[0]
                ? prog->program_uniforms[i].gles_name
                : prog->program_uniforms[i].name;
            int arr = prog->program_uniforms[i].array_size;
            char uni_name_arr[SGL_ATTRIB_NAME_MAX];
            if (arr > 1) {
                if (strchr(uni_name, '['))
                    strncpy(uni_name_arr, uni_name, SGL_ATTRIB_NAME_MAX - 1);
                else
                    snprintf(uni_name_arr, SGL_ATTRIB_NAME_MAX, "%s[0]", uni_name);
                uni_name_arr[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            }
            for (int j = 0; j < prog->num_active_uniforms; j++) {
                if (strcmp(prog->active_uniforms[j].name, uni_name) == 0 ||
                    (arr > 1 && strcmp(prog->active_uniforms[j].name, uni_name_arr) == 0)) {
                    already = true; break;
                }
            }
            if (already) continue;
            if (prog->num_active_uniforms >= SGL_MAX_UNIFORMS * 2) break;
            int slot = prog->num_active_uniforms++;
            /* For arrays, GLES2 spec requires name = "name[0]", size = array_count */
            if (arr > 1) {
                if (strchr(uni_name, '['))
                    strncpy(prog->active_uniforms[slot].name, uni_name, SGL_ATTRIB_NAME_MAX - 1);
                else
                    snprintf(prog->active_uniforms[slot].name, SGL_ATTRIB_NAME_MAX,
                             "%s[0]", uni_name);
                prog->active_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            } else {
                strncpy(prog->active_uniforms[slot].name, uni_name, SGL_ATTRIB_NAME_MAX - 1);
                prog->active_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            }
            prog->active_uniforms[slot].type = prog->program_uniforms[i].gl_type
                ? prog->program_uniforms[i].gl_type : GL_FLOAT_VEC4;
            prog->active_uniforms[slot].size = (arr > 1) ? arr : 1;
            prog->active_uniforms[slot].location = prog->program_uniforms[i].location;
            prog->active_uniforms[slot].element_stride = prog->program_uniforms[i].element_stride;
            prog->active_uniforms[slot].active = true;
        }

        /* Add samplers as active uniforms.
         * For sampler arrays: report ONE active uniform with name "s[0]" and size=array_total.
         * Skip subsequent array elements (array_index > 0). */
        for (int i = 0; i < prog->num_samplers; i++) {
            if (!prog->samplers[i].used) continue;
            if (prog->num_active_uniforms >= SGL_MAX_UNIFORMS * 2) break;
            /* Skip non-first array elements */
            if (prog->samplers[i].array_index > 0) continue;
            /* Use GLES name for API visibility */
            const char *samp_name = prog->samplers[i].gles_name[0]
                ? prog->samplers[i].gles_name : prog->samplers[i].name;
            /* Deduplicate (VS+FS sampler with same name) */
            bool already = false;
            for (int j = 0; j < prog->num_active_uniforms; j++) {
                if (strcmp(prog->active_uniforms[j].name, samp_name) == 0) {
                    already = true; break;
                }
            }
            if (already) continue;
            int slot = prog->num_active_uniforms++;
            strncpy(prog->active_uniforms[slot].name, samp_name, SGL_ATTRIB_NAME_MAX - 1);
            prog->active_uniforms[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            /* Determine sampler type from transpiler metadata */
            GLenum samp_type = prog->samplers[i].gl_type ? prog->samplers[i].gl_type : GL_SAMPLER_2D;
            if (prog->samplers[i].array_total > 0) {
                /* Sampler array: size = array count */
                prog->active_uniforms[slot].size = prog->samplers[i].array_total;
            } else {
                prog->active_uniforms[slot].size = 1;
            }
            prog->active_uniforms[slot].type = samp_type;
            prog->active_uniforms[slot].location = (GLint)(SGL_LOC_SAMPLER_FLAG | (unsigned)i);
            prog->active_uniforms[slot].active = true;
        }

        SGL_TRACE_SHADER("populated %d active uniforms at link time", prog->num_active_uniforms);

        /* 8. Register attrib bindings from transpiler result into program */
        prog->num_active_attribs = vs_result.num_attributes;
        /* Clear in_shader flag and linked_location for all existing bindings */
        for (int j = 0; j < prog->num_attrib_bindings; j++) {
            prog->attrib_bindings[j].in_shader = false;
            prog->attrib_bindings[j].linked_location = -1;
        }
        for (int i = 0; i < vs_result.num_attributes; i++) {
            GLenum attr_gl_type = glslt_to_gl_type(vs_result.attributes[i].type);
            bool found = false;
            for (int j = 0; j < prog->num_attrib_bindings; j++) {
                if (strcmp(prog->attrib_bindings[j].name,
                           vs_result.attributes[i].name) == 0) {
                    prog->attrib_bindings[j].index = vs_result.attributes[i].location;
                    prog->attrib_bindings[j].linked_location = (GLint)vs_result.attributes[i].location;
                    prog->attrib_bindings[j].gl_type = attr_gl_type;
                    prog->attrib_bindings[j].in_shader = true;
                    found = true;
                    break;
                }
            }
            if (!found && prog->num_attrib_bindings < SGL_MAX_ATTRIB_BINDINGS) {
                int slot = prog->num_attrib_bindings++;
                strncpy(prog->attrib_bindings[slot].name,
                        vs_result.attributes[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->attrib_bindings[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                prog->attrib_bindings[slot].index = vs_result.attributes[i].location;
                prog->attrib_bindings[slot].linked_location = (GLint)vs_result.attributes[i].location;
                prog->attrib_bindings[slot].gl_type = attr_gl_type;
                prog->attrib_bindings[slot].used = true;
                prog->attrib_bindings[slot].user_bound = false; /* linker-added */
                prog->attrib_bindings[slot].in_shader = true;
            }
        }

        /* Count active attribs — ALL attributes with in_shader=true (no dedup). */
        {
            int active_count = 0;
            for (int j = 0; j < prog->num_attrib_bindings; j++) {
                if (prog->attrib_bindings[j].in_shader) active_count++;
            }
            prog->num_active_attribs = active_count;
        }

        glslt_result_free(&vs_result);
        glslt_result_free(&fs_result);

        SGL_TRACE_SHADER("transpilation complete for program %u", program);
    }
#endif /* SGL_ENABLE_RUNTIME_COMPILER */

    /* Call backend to copy shader data to per-program storage.
     * This prevents issues when shader IDs are reused after glDeleteShader. */
    bool link_ok = true;
    if (ctx->backend && ctx->backend->ops->link_program) {
        link_ok = ctx->backend->ops->link_program(ctx->backend, program,
                                        prog->vertex_shader, prog->fragment_shader);
    }

    prog->linked = link_ok;
    SGL_TRACE_SHADER("glLinkProgram(%u) - %s", program, link_ok ? "OK" : "FAILED");
}

GL_APICALL void GL_APIENTRY glUseProgram(GLuint program) {
    GET_CTX();

    if (program != 0 && !GET_PROGRAM(program)) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    /* If switching away from a delete_pending program, free it now */
    GLuint old_program = ctx->current_program;
    ctx->current_program = program;

    if (old_program != 0 && old_program != program) {
        sgl_program_t *old_prog = GET_PROGRAM(old_program);
        if (old_prog && old_prog->delete_pending) {
            sgl_program_do_free(ctx, old_program);
        }
    }

    if (program > 0 && ctx->backend && ctx->backend->ops->use_program) {
        sgl_program_t *prog = GET_PROGRAM(program);
        if (prog) {
            ctx->backend->ops->use_program(ctx->backend, prog->backend_handle);
        }
    }

    SGL_TRACE_SHADER("glUseProgram(%u)", program);
}

GL_APICALL void GL_APIENTRY glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    GET_CTX();
    if (!params) return;

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    switch (pname) {
        case GL_LINK_STATUS:
            *params = prog->linked ? GL_TRUE : GL_FALSE;
            break;
        case GL_DELETE_STATUS:
            *params = prog->delete_pending ? GL_TRUE : GL_FALSE;
            break;
        case GL_VALIDATE_STATUS:
            *params = prog->validated ? GL_TRUE : GL_FALSE;
            break;
        case GL_INFO_LOG_LENGTH:
            *params = prog->info_log ? (GLint)strlen(prog->info_log) + 1 : 0;
            break;
        case GL_ATTACHED_SHADERS:
            *params = (prog->vertex_shader ? 1 : 0) + (prog->fragment_shader ? 1 : 0);
            break;
        case GL_ACTIVE_UNIFORMS:
            *params = prog->num_active_uniforms;
            break;
        case GL_ACTIVE_ATTRIBUTES:
            *params = prog->num_active_attribs;
            break;
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: {
            GLint maxlen = 0;
            for (int i = 0; i < prog->num_attrib_bindings; i++) {
                if (prog->attrib_bindings[i].used && prog->attrib_bindings[i].in_shader) {
                    GLint len = (GLint)strlen(prog->attrib_bindings[i].name) + 1;
                    if (len > maxlen) maxlen = len;
                }
            }
            *params = maxlen;
            break;
        }
        case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
            GLint maxlen = 0;
            for (int i = 0; i < prog->num_active_uniforms; i++) {
                if (prog->active_uniforms[i].active) {
                    GLint len = (GLint)strlen(prog->active_uniforms[i].name) + 1;
                    if (len > maxlen) maxlen = len;
                }
            }
            *params = maxlen;
            break;
        }
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

GL_APICALL void GL_APIENTRY glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    GET_CTX();

    if (bufSize < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
        return;
    }

    if (prog->info_log && prog->info_log[0]) {
        GLsizei log_len = (GLsizei)strlen(prog->info_log);
        GLsizei copy_len = (bufSize > 0) ? (bufSize - 1) : 0;
        if (copy_len > log_len) copy_len = log_len;
        if (infoLog && bufSize > 0) {
            if (copy_len > 0) memcpy(infoLog, prog->info_log, copy_len);
            infoLog[copy_len] = '\0';
        }
        if (length) *length = copy_len;
    } else {
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
    }
}

GL_APICALL void GL_APIENTRY glValidateProgram(GLuint program) {
    GET_CTX();
    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    /* Validation status is queried via glGetProgramiv(GL_VALIDATE_STATUS)
     * For precompiled DKSH shaders, linked programs are always valid. */
    prog->validated = prog->linked;
}

GL_APICALL void GL_APIENTRY glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders) {
    GET_CTX();

    if (maxCount < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    int n = 0;
    if (prog->vertex_shader && n < maxCount) {
        if (shaders) shaders[n] = prog->vertex_shader;
        n++;
    }
    if (prog->fragment_shader && n < maxCount) {
        if (shaders) shaders[n] = prog->fragment_shader;
        n++;
    }
    if (count) *count = n;
}

/* Load pre-compiled shader from file - delegates to backend */
bool sgl_load_shader_from_file(GLuint shader_id, const char *path) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend || !ctx->backend->ops) return false;

    sgl_shader_t *shader = sgl_res_mgr_get_shader(&ctx->res_mgr, shader_id);
    if (!shader) return false;

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

/* Bind program and uniforms for drawing - delegates to backend */
bool sgl_bind_program_for_draw(sgl_context_t *ctx, GLuint program_id) {
    if (!ctx || !ctx->backend || !ctx->backend->ops) return false;

    sgl_program_t *prog = sgl_res_mgr_get_program(&ctx->res_mgr, program_id);
    if (!prog || !prog->linked) return false;

    /* Call backend to bind program with uniforms and shader handles */
    if (ctx->backend->ops->bind_program) {
        ctx->backend->ops->bind_program(ctx->backend, program_id,
                                        prog->vertex_shader,
                                        prog->fragment_shader,
                                        prog->vertex_uniforms,
                                        prog->fragment_uniforms,
                                        SGL_MAX_UNIFORMS,
                                        prog->packed_vertex,
                                        prog->packed_fragment,
                                        SGL_MAX_PACKED_UBOS);
    }

    return true;
}
