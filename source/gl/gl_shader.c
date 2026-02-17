/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Shader and Program Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef SGL_ENABLE_RUNTIME_COMPILER
#include <libuam.h>
#include <malloc.h>  /* memalign — needed for 256-byte aligned DKSH buffer */
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

    SGL_TRACE_SHADER("glShaderSource(%u, %d) - %zu bytes", shader, count, total);
}

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

    /* If no source set, mark as compiled for precompiled path (glShaderBinary) */
    if (!sh->source) {
        sh->compiled = true;
        return;
    }

#ifdef SGL_ENABLE_RUNTIME_COMPILER
    /* Map GL shader type to DkStage */
    DkStage stage;
    if (sh->type == GL_VERTEX_SHADER) {
        stage = DkStage_Vertex;
    } else if (sh->type == GL_FRAGMENT_SHADER) {
        stage = DkStage_Fragment;
    } else {
        sh->info_log = strdup("ERROR: Unsupported shader type\n");
        sh->compiled = false;
        return;
    }

    /* Create compiler and compile GLSL to DKSH */
    uam_compiler *compiler = uam_create_compiler(stage);
    if (!compiler) {
        sh->info_log = strdup("ERROR: Failed to create shader compiler\n");
        sh->compiled = false;
        return;
    }

    if (uam_compile_dksh(compiler, sh->source)) {
        /* Get compiled DKSH binary */
        size_t dksh_size = uam_get_code_size(compiler);

        printf("[SGL-RT] shader %u: DKSH size=%zu align256=%d GPRs=%d\n",
               shader, dksh_size, (int)(dksh_size % 256), uam_get_num_gprs(compiler));
        fflush(stdout);

        /* CRITICAL: libuam OutputDkshToMemory uses pa256() which aligns to
         * ABSOLUTE memory addresses, not relative to buffer start. If the buffer
         * is not 256-byte aligned, the code section ends up at the wrong offset
         * within the DKSH, causing dkShaderInitialize to read garbage → GPU fault.
         * Fix: use memalign(256, ...) to ensure 256-byte alignment. */
        size_t alloc_size = (dksh_size + 4095) & ~(size_t)4095;
        void *dksh = memalign(256, alloc_size);
        if (dksh) {
            memset(dksh, 0, alloc_size);
            uam_write_code(compiler, dksh);

            /* Diagnostic: dump DKSH header (first 32 bytes) for comparison */
            {
                const uint8_t *hdr = (const uint8_t *)dksh;
                printf("[SGL-RT] DKSH header:");
                for (int ii = 0; ii < 32 && ii < (int)dksh_size; ii++)
                    printf(" %02X", hdr[ii]);
                printf("\n");
                fflush(stdout);
            }

            /* Load DKSH into backend */
            if (ctx->backend && ctx->backend->ops->load_shader_binary) {
                sh->compiled = ctx->backend->ops->load_shader_binary(
                    ctx->backend, shader, dksh, dksh_size);
            } else {
                sh->compiled = false;
                sh->info_log = strdup("ERROR: Backend does not support shader loading\n");
            }

            free(dksh);
        } else {
            sh->compiled = false;
            sh->info_log = strdup("ERROR: Out of memory for compiled shader\n");
        }

        /* Store any warnings from compilation */
        const char *log = uam_get_error_log(compiler);
        if (log && log[0] != '\0' && !sh->info_log) {
            sh->info_log = strdup(log);
        }
    } else {
        /* Compilation failed - store error log */
        const char *log = uam_get_error_log(compiler);
        sh->info_log = (log && log[0]) ? strdup(log) : strdup("ERROR: Compilation failed\n");
        sh->compiled = false;
    }

    uam_free_compiler(compiler);
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

    /* CRITICAL: Call backend to copy shader data to per-program storage.
     * This prevents issues when shader IDs are reused after glDeleteShader. */
    bool link_ok = true;
    if (ctx->backend && ctx->backend->ops->link_program) {
        link_ok = ctx->backend->ops->link_program(ctx->backend, program,
                                        prog->vertex_shader, prog->fragment_shader);
    }

    prog->linked = link_ok;
    SGL_TRACE_SHADER("glLinkProgram(%u)", program);
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
            *params = GL_TRUE;
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
    (void)program;
    if (length) *length = 0;
    if (infoLog && bufSize > 0) infoLog[0] = '\0';
}

GL_APICALL void GL_APIENTRY glValidateProgram(GLuint program) {
    (void)program;
    /* Always valid */
}

GL_APICALL void GL_APIENTRY glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) return;

    int n = 0;
    if (prog->vertex_shader && n < maxCount) shaders[n++] = prog->vertex_shader;
    if (prog->fragment_shader && n < maxCount) shaders[n++] = prog->fragment_shader;
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
