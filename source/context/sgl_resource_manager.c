/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Resource Manager Implementation
 */

#include "sgl_resource_manager.h"
#include <string.h>
#include <stdlib.h>

void sgl_res_mgr_init(sgl_resource_manager_t *mgr) {
    memset(mgr, 0, sizeof(sgl_resource_manager_t));
}

/* ============================================================================
 * Buffer Operations
 * ============================================================================ */

GLuint sgl_res_mgr_alloc_buffer(sgl_resource_manager_t *mgr) {
    for (GLuint i = 1; i < SGL_MAX_BUFFERS; i++) {
        if (!mgr->buffers[i].used) {
            memset(&mgr->buffers[i], 0, sizeof(sgl_buffer_t));
            mgr->buffers[i].used = true;
            mgr->buffers[i].usage = GL_STATIC_DRAW; /* GLES2 spec default */
            return i;
        }
    }
    return 0;
}

void sgl_res_mgr_free_buffer(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_BUFFERS && mgr->buffers[id].used) {
        mgr->buffers[id].used = false;
    }
}

sgl_buffer_t *sgl_res_mgr_get_buffer(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_BUFFERS && mgr->buffers[id].used) {
        return &mgr->buffers[id];
    }
    return NULL;
}

/* ============================================================================
 * Shader Operations
 * ============================================================================ */

GLuint sgl_res_mgr_alloc_shader(sgl_resource_manager_t *mgr, GLenum type) {
    for (GLuint i = 1; i < SGL_MAX_SHADERS; i++) {
        if (!mgr->shaders[i].used && mgr->gl_name_type[i] == SGL_NAME_FREE) {
            memset(&mgr->shaders[i], 0, sizeof(sgl_shader_t));
            mgr->shaders[i].used = true;
            mgr->shaders[i].type = type;
            mgr->gl_name_type[i] = SGL_NAME_SHADER;
            return i;
        }
    }
    return 0;
}

void sgl_res_mgr_free_shader(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_SHADERS && mgr->shaders[id].used) {
        if (mgr->shaders[id].source) {
            free(mgr->shaders[id].source);
            mgr->shaders[id].source = NULL;
        }
        if (mgr->shaders[id].info_log) {
            free(mgr->shaders[id].info_log);
            mgr->shaders[id].info_log = NULL;
        }
        if (mgr->shaders[id].mesa_meta) {
            if (mgr->shaders[id].mesa_meta->initial_data)
                free(mgr->shaders[id].mesa_meta->initial_data);
            free(mgr->shaders[id].mesa_meta);
            mgr->shaders[id].mesa_meta = NULL;
        }
        mgr->shaders[id].used = false;
        mgr->gl_name_type[id] = SGL_NAME_FREE;
    }
}

sgl_shader_t *sgl_res_mgr_get_shader(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_SHADERS && mgr->shaders[id].used &&
        mgr->gl_name_type[id] == SGL_NAME_SHADER) {
        return &mgr->shaders[id];
    }
    return NULL;
}

/* ============================================================================
 * Program Operations
 * ============================================================================ */

GLuint sgl_res_mgr_alloc_program(sgl_resource_manager_t *mgr) {
    for (GLuint i = 1; i < SGL_MAX_PROGRAMS; i++) {
        if (!mgr->programs[i].used && mgr->gl_name_type[i] == SGL_NAME_FREE) {
            memset(&mgr->programs[i], 0, sizeof(sgl_program_t));
            mgr->programs[i].used = true;
            mgr->gl_name_type[i] = SGL_NAME_PROGRAM;
            return i;
        }
    }
    return 0;
}

void sgl_res_mgr_free_program(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_PROGRAMS && mgr->programs[id].used) {
        mgr->programs[id].used = false;
        mgr->gl_name_type[id] = SGL_NAME_FREE;
    }
}

sgl_program_t *sgl_res_mgr_get_program(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_PROGRAMS && mgr->programs[id].used &&
        mgr->gl_name_type[id] == SGL_NAME_PROGRAM) {
        return &mgr->programs[id];
    }
    return NULL;
}

/* ============================================================================
 * Texture Operations
 * ============================================================================ */

GLuint sgl_res_mgr_alloc_texture(sgl_resource_manager_t *mgr) {
    /* Start from 2: handles 0 and 1 are reserved for black fallback textures
     * (0 = 2D black, 1 = cubemap black) used for incomplete texture sampling. */
    for (GLuint i = 2; i < SGL_MAX_TEXTURES; i++) {
        if (!mgr->textures[i].used && !mgr->textures[i].delete_pending) {
            memset(&mgr->textures[i], 0, sizeof(sgl_texture_t));
            mgr->textures[i].used = true;
            /* OpenGL defaults for texture parameters */
            mgr->textures[i].min_filter = GL_NEAREST_MIPMAP_LINEAR;
            mgr->textures[i].mag_filter = GL_LINEAR;
            mgr->textures[i].wrap_s = GL_REPEAT;
            mgr->textures[i].wrap_t = GL_REPEAT;
            return i;
        }
    }
    return 0;
}

void sgl_res_mgr_free_texture(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_TEXTURES) {
        mgr->textures[id].used = false;
        mgr->textures[id].delete_pending = false;
        mgr->textures[id].fbo_ref_count = 0;
    }
}

sgl_texture_t *sgl_res_mgr_get_texture(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_TEXTURES && mgr->textures[id].used) {
        return &mgr->textures[id];
    }
    return NULL;
}

/* Returns texture even if pending deletion (for FBO deferred cleanup) */
sgl_texture_t *sgl_res_mgr_get_texture_any(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_TEXTURES &&
        (mgr->textures[id].used || mgr->textures[id].delete_pending)) {
        return &mgr->textures[id];
    }
    return NULL;
}

/* ============================================================================
 * Framebuffer Operations
 * ============================================================================ */

GLuint sgl_res_mgr_alloc_framebuffer(sgl_resource_manager_t *mgr) {
    for (GLuint i = 1; i < SGL_MAX_FRAMEBUFFERS; i++) {
        if (!mgr->framebuffers[i].used) {
            memset(&mgr->framebuffers[i], 0, sizeof(sgl_framebuffer_t));
            mgr->framebuffers[i].used = true;
            return i;
        }
    }
    return 0;
}

void sgl_res_mgr_free_framebuffer(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_FRAMEBUFFERS && mgr->framebuffers[id].used) {
        mgr->framebuffers[id].used = false;
    }
}

sgl_framebuffer_t *sgl_res_mgr_get_framebuffer(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_FRAMEBUFFERS && mgr->framebuffers[id].used) {
        return &mgr->framebuffers[id];
    }
    return NULL;
}

/* ============================================================================
 * Renderbuffer Operations
 * ============================================================================ */

GLuint sgl_res_mgr_alloc_renderbuffer(sgl_resource_manager_t *mgr) {
    for (GLuint i = 1; i < SGL_MAX_RENDERBUFFERS; i++) {
        if (!mgr->renderbuffers[i].used && !mgr->renderbuffers[i].delete_pending) {
            memset(&mgr->renderbuffers[i], 0, sizeof(sgl_renderbuffer_t));
            mgr->renderbuffers[i].used = true;
            mgr->renderbuffers[i].internal_format = GL_RGBA4; /* GLES2 spec default */
            return i;
        }
    }
    return 0;
}

void sgl_res_mgr_free_renderbuffer(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_RENDERBUFFERS) {
        mgr->renderbuffers[id].used = false;
        mgr->renderbuffers[id].delete_pending = false;
        mgr->renderbuffers[id].fbo_ref_count = 0;
    }
}

sgl_renderbuffer_t *sgl_res_mgr_get_renderbuffer(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_RENDERBUFFERS && mgr->renderbuffers[id].used) {
        return &mgr->renderbuffers[id];
    }
    return NULL;
}

/* Returns renderbuffer even if pending deletion (for FBO deferred cleanup) */
sgl_renderbuffer_t *sgl_res_mgr_get_renderbuffer_any(sgl_resource_manager_t *mgr, GLuint id) {
    if (id > 0 && id < SGL_MAX_RENDERBUFFERS &&
        (mgr->renderbuffers[id].used || mgr->renderbuffers[id].delete_pending)) {
        return &mgr->renderbuffers[id];
    }
    return NULL;
}
