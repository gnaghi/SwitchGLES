/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Shader and Program Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

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
    (void)shader; (void)count; (void)string; (void)length;
    /* Runtime compilation not supported - use pre-compiled dksh shaders */
}

GL_APICALL void GL_APIENTRY glCompileShader(GLuint shader) {
    GET_CTX();
    sgl_shader_t *sh = GET_SHADER(shader);
    if (sh) {
        sh->compiled = true;  /* Mark as compiled (actual compilation done offline) */
    }
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
            *params = 0;
            break;
        case GL_SHADER_SOURCE_LENGTH:
            *params = 0;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

GL_APICALL void GL_APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    (void)shader;
    if (length) *length = 0;
    if (infoLog && bufSize > 0) infoLog[0] = '\0';
}

GL_APICALL void GL_APIENTRY glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) {
    (void)shader;
    if (length) *length = 0;
    if (source && bufSize > 0) source[0] = '\0';
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
    if (ctx->backend && ctx->backend->ops->link_program) {
        ctx->backend->ops->link_program(ctx->backend, program,
                                        prog->vertex_shader, prog->fragment_shader);
    }

    prog->linked = true;
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
            *params = SGL_MAX_UNIFORMS;
            break;
        case GL_ACTIVE_ATTRIBUTES:
            *params = SGL_MAX_ATTRIBS;
            break;
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
