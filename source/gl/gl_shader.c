/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Shader and Program Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdlib.h>

#ifdef SGL_ENABLE_RUNTIME_COMPILER
#include <libuam.h>
#include <malloc.h>  /* memalign — needed for 256-byte aligned DKSH buffer */
#include "../transpiler/glsl_transpiler.h"
/* Forward-declare packed uniform API (defined in gl_uniform.c, declared in gl2sgl.h) */
extern GLboolean sglRegisterPackedUniform(const GLchar *name, GLint stage, GLint binding, GLint byte_offset);
extern void sglSetPackedUBOSize(GLint stage, GLint binding, GLint size);
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
    sgl_res_mgr_free_shader(&ctx->res_mgr, shader);
    SGL_TRACE_SHADER("glDeleteShader(%u)", shader);
}

GL_APICALL GLboolean GL_APIENTRY glIsShader(GLuint shader) {
    GET_CTX_RET(GL_FALSE);
    return GET_SHADER(shader) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) {
    GET_CTX();

    if (count < 0 || !string) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
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

    uam_compiler *compiler = uam_create_compiler(stage);
    if (!compiler) {
        sh->info_log = strdup("ERROR: Failed to create shader compiler\n");
        return false;
    }

    bool result = false;

    if (uam_compile_dksh(compiler, glsl_source)) {
        size_t dksh_size = uam_get_code_size(compiler);

        SGL_TRACE_SHADER("RT compile shader %u: DKSH size=%zu align256=%d GPRs=%d",
                         shader_id, dksh_size, (int)(dksh_size % 256), uam_get_num_gprs(compiler));

        /* CRITICAL: Buffer MUST be 256-byte aligned for libuam's pa256(). */
        size_t alloc_size = SGL_ALIGN_UP(dksh_size, SGL_PAGE_ALIGNMENT);
        void *dksh = memalign(256, alloc_size);
        if (dksh) {
            memset(dksh, 0, alloc_size);
            uam_write_code(compiler, dksh);

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
 * Detect if shader source is GLSL ES 1.00 (needs transpilation).
 * Returns true if #version 100 or no #version directive found.
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

    /* No #version directive at all → treat as ES 1.00 */
    return true;
}
#endif /* SGL_ENABLE_RUNTIME_COMPILER */

GL_APICALL void GL_APIENTRY glCompileShader(GLuint shader) {
    GET_CTX();
    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
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
    /* Check if source is GLSL ES 1.00 → defer compilation to glLinkProgram */
    if (sgl_is_es100_source(sh->source)) {
        sh->needs_transpile = true;
        sh->compiled = true;  /* Report success; actual compilation deferred to link time */
        SGL_TRACE_SHADER("glCompileShader(%u) - ES 1.00 detected, deferred to link time", shader);
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
        sgl_set_error(ctx, GL_INVALID_VALUE);
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
            *params = GL_FALSE;
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
    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
        return;
    }

    if (sh->info_log && sh->info_log[0]) {
        GLsizei log_len = (GLsizei)strlen(sh->info_log);
        GLsizei copy_len = (bufSize > 0) ? (bufSize - 1) : 0;
        if (copy_len > log_len) copy_len = log_len;
        if (infoLog && copy_len > 0) {
            memcpy(infoLog, sh->info_log, copy_len);
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
    sgl_shader_t *sh = GET_SHADER(shader);
    if (!sh) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        if (length) *length = 0;
        if (source && bufSize > 0) source[0] = '\0';
        return;
    }

    if (sh->source && sh->source[0]) {
        GLsizei src_len = (GLsizei)strlen(sh->source);
        GLsizei copy_len = (bufSize > 0) ? (bufSize - 1) : 0;
        if (copy_len > src_len) copy_len = src_len;
        if (source && copy_len > 0) {
            memcpy(source, sh->source, copy_len);
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

GL_APICALL void GL_APIENTRY glDeleteProgram(GLuint program) {
    GET_CTX();
    if (program == 0) return;

    if (ctx->current_program == program) {
        ctx->current_program = 0;
    }

    sgl_res_mgr_free_program(&ctx->res_mgr, program);
    SGL_TRACE_SHADER("glDeleteProgram(%u)", program);
}

GL_APICALL GLboolean GL_APIENTRY glIsProgram(GLuint program) {
    GET_CTX_RET(GL_FALSE);
    return GET_PROGRAM(program) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glAttachShader(GLuint program, GLuint shader) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    sgl_shader_t *sh = GET_SHADER(shader);

    if (!prog || !sh) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (sh->type == GL_VERTEX_SHADER) {
        prog->vertex_shader = shader;
    } else if (sh->type == GL_FRAGMENT_SHADER) {
        prog->fragment_shader = shader;
    }

    SGL_TRACE_SHADER("glAttachShader(%u, %u)", program, shader);
}

GL_APICALL void GL_APIENTRY glDetachShader(GLuint program, GLuint shader) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (prog->vertex_shader == shader) prog->vertex_shader = 0;
    if (prog->fragment_shader == shader) prog->fragment_shader = 0;

    SGL_TRACE_SHADER("glDetachShader(%u, %u)", program, shader);
}

GL_APICALL void GL_APIENTRY glLinkProgram(GLuint program) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

#ifdef SGL_ENABLE_RUNTIME_COMPILER
    /* Check if any attached shader needs transpilation (GLSL ES 1.00 → 4.60) */
    sgl_shader_t *vs_sh = prog->vertex_shader ? GET_SHADER(prog->vertex_shader) : NULL;
    sgl_shader_t *fs_sh = prog->fragment_shader ? GET_SHADER(prog->fragment_shader) : NULL;
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
                glslt_result_free(&vs_result);
                glslt_result_free(&fs_result);
                SGL_TRACE_SHADER("glLinkProgram(%u) - FS transpile FAILED", program);
                return;
            }
            SGL_TRACE_SHADER("FS transpiled: %d uniforms, %d samplers, %d varyings",
                             fs_result.num_uniforms, fs_result.num_samplers, fs_result.num_varyings);
        }

        /* 4. Compile transpiled GLSL 4.60 → DKSH via libuam */
        if (vs_sh && vs_sh->needs_transpile && vs_result.output) {
            if (vs_sh->info_log) { free(vs_sh->info_log); vs_sh->info_log = NULL; }
            vs_sh->compiled = sgl_compile_glsl460(ctx, prog->vertex_shader,
                                                   vs_sh, vs_result.output);
            if (!vs_sh->compiled) {
                SGL_TRACE_SHADER("VS compile failed after transpile");
                prog->linked = false;
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
                glslt_result_free(&vs_result);
                glslt_result_free(&fs_result);
                SGL_TRACE_SHADER("glLinkProgram(%u) - FS compile FAILED", program);
                return;
            }
            fs_sh->needs_transpile = false;
        }

        /* 5. Auto-register uniforms from transpiler reflection data */
        /* VS uniforms → packed UBO at VS binding 0 */
        if (vs_result.num_uniforms > 0) {
            sglSetPackedUBOSize(0, vs_opts.ubo_binding, vs_result.ubo_total_size);
            for (int i = 0; i < vs_result.num_uniforms; i++) {
                sglRegisterPackedUniform(vs_result.uniforms[i].name,
                                         0, vs_opts.ubo_binding,
                                         vs_result.uniforms[i].offset);
            }
            SGL_TRACE_SHADER("registered %d VS uniforms (UBO size=%d)",
                             vs_result.num_uniforms, vs_result.ubo_total_size);
        }
        /* FS uniforms → packed UBO at FS binding 0 */
        if (fs_result.num_uniforms > 0) {
            sglSetPackedUBOSize(1, 0, fs_result.ubo_total_size);
            for (int i = 0; i < fs_result.num_uniforms; i++) {
                sglRegisterPackedUniform(fs_result.uniforms[i].name,
                                         1, 0,
                                         fs_result.uniforms[i].offset);
            }
            SGL_TRACE_SHADER("registered %d FS uniforms (UBO size=%d)",
                             fs_result.num_uniforms, fs_result.ubo_total_size);
        }

        /* 6. Register attrib bindings from transpiler result into program */
        for (int i = 0; i < vs_result.num_attributes; i++) {
            bool found = false;
            for (int j = 0; j < prog->num_attrib_bindings; j++) {
                if (strcmp(prog->attrib_bindings[j].name,
                           vs_result.attributes[i].name) == 0) {
                    prog->attrib_bindings[j].index = vs_result.attributes[i].location;
                    found = true;
                    break;
                }
            }
            if (!found && prog->num_attrib_bindings < SGL_MAX_ATTRIBS) {
                int slot = prog->num_attrib_bindings++;
                strncpy(prog->attrib_bindings[slot].name,
                        vs_result.attributes[i].name, SGL_ATTRIB_NAME_MAX - 1);
                prog->attrib_bindings[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                prog->attrib_bindings[slot].index = vs_result.attributes[i].location;
                prog->attrib_bindings[slot].used = true;
            }
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
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    ctx->current_program = program;

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
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    switch (pname) {
        case GL_LINK_STATUS:
            *params = prog->linked ? GL_TRUE : GL_FALSE;
            break;
        case GL_DELETE_STATUS:
            *params = GL_FALSE;
            break;
        case GL_VALIDATE_STATUS:
            *params = prog->validated ? GL_TRUE : GL_FALSE;
            break;
        case GL_INFO_LOG_LENGTH:
            *params = 0;
            break;
        case GL_ATTACHED_SHADERS:
            *params = (prog->vertex_shader ? 1 : 0) + (prog->fragment_shader ? 1 : 0);
            break;
        case GL_ACTIVE_UNIFORMS:
            *params = prog->num_active_uniforms;
            break;
        case GL_ACTIVE_ATTRIBUTES:
            *params = prog->num_attrib_bindings;
            break;
        case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: {
            GLint maxlen = 0;
            for (int i = 0; i < prog->num_attrib_bindings; i++) {
                if (prog->attrib_bindings[i].used) {
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

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        if (length) *length = 0;
        if (infoLog && bufSize > 0) infoLog[0] = '\0';
        return;
    }

    /* Precompiled DKSH shaders: link always succeeds, no error log */
    if (length) *length = 0;
    if (infoLog && bufSize > 0) infoLog[0] = '\0';
}

GL_APICALL void GL_APIENTRY glValidateProgram(GLuint program) {
    GET_CTX();
    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
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
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    int n = 0;
    if (shaders) {
        if (prog->vertex_shader && n < maxCount) shaders[n++] = prog->vertex_shader;
        if (prog->fragment_shader && n < maxCount) shaders[n++] = prog->fragment_shader;
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
