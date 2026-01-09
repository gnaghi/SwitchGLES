/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Framebuffer Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

/* GLES2 extension constants (not in standard headers) */
#ifndef GL_FRAMEBUFFER_UNDEFINED
#define GL_FRAMEBUFFER_UNDEFINED 0x8219
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif

/* Framebuffer Objects */

GL_APICALL void GL_APIENTRY glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
    GET_CTX();

    if (n < 0 || !framebuffers) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    for (GLsizei i = 0; i < n; i++) {
        framebuffers[i] = sgl_res_mgr_alloc_framebuffer(&ctx->res_mgr);
        if (framebuffers[i] == 0) {
            sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }

    SGL_TRACE_FBO("glGenFramebuffers(%d)", n);
}

GL_APICALL void GL_APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    GET_CTX();

    if (n < 0) return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = framebuffers[i];
        if (id == 0) continue;

        if (ctx->bound_framebuffer == id) {
            ctx->bound_framebuffer = 0;
        }

        sgl_res_mgr_free_framebuffer(&ctx->res_mgr, id);
    }

    SGL_TRACE_FBO("glDeleteFramebuffers(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsFramebuffer(GLuint framebuffer) {
    GET_CTX_RET(GL_FALSE);
    return GET_FRAMEBUFFER(framebuffer) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer) {
    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (framebuffer != 0 && !GET_FRAMEBUFFER(framebuffer)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Insert barrier when switching render targets */
    if (ctx->bound_framebuffer != framebuffer && ctx->backend->ops->insert_barrier) {
        ctx->backend->ops->insert_barrier(ctx->backend);
    }

    ctx->bound_framebuffer = framebuffer;

    /* Delegate render target switch to backend */
    if (ctx->backend->ops->bind_framebuffer) {
        sgl_handle_t color_tex = 0;
        sgl_handle_t depth_rb = 0;
        if (framebuffer != 0) {
            sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(framebuffer);
            if (fbo) {
                color_tex = fbo->color_attachment;
                depth_rb = fbo->depth_attachment;
            }
        }
        ctx->backend->ops->bind_framebuffer(ctx->backend, framebuffer, color_tex, depth_rb);
    }

    SGL_TRACE_FBO("glBindFramebuffer(0x%X, %u)", target, framebuffer);
}

GL_APICALL void GL_APIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment,
                                                     GLenum textarget, GLuint texture, GLint level) {
    GET_CTX();
    (void)textarget;
    (void)level;

    if (target != GL_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (ctx->bound_framebuffer == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
    if (!fbo) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    switch (attachment) {
        case GL_COLOR_ATTACHMENT0:
            fbo->color_attachment = texture;
            /* Update render target binding immediately */
            if (ctx->backend && ctx->backend->ops->bind_framebuffer) {
                ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                     texture, fbo->depth_attachment);
            }
            break;
        case GL_DEPTH_ATTACHMENT:
            fbo->depth_attachment = texture;
            /* Re-bind to update depth attachment */
            if (ctx->backend && ctx->backend->ops->bind_framebuffer && fbo->color_attachment) {
                ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                     fbo->color_attachment, texture);
            }
            break;
        case GL_STENCIL_ATTACHMENT:
            fbo->stencil_attachment = texture;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    SGL_TRACE_FBO("glFramebufferTexture2D(attachment=0x%X, texture=%u)", attachment, texture);
}

GL_APICALL GLenum GL_APIENTRY glCheckFramebufferStatus(GLenum target) {
    GET_CTX_RET(0);

    if (target != GL_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return 0;
    }

    if (ctx->bound_framebuffer == 0) {
        return GL_FRAMEBUFFER_COMPLETE;  /* Default framebuffer always complete */
    }

    sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
    if (!fbo) {
        return GL_FRAMEBUFFER_UNDEFINED;
    }

    /* Check if we have at least one color attachment */
    if (fbo->color_attachment == 0) {
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }

    /* Verify the attachment is a valid texture */
    sgl_texture_t *tex = GET_TEXTURE(fbo->color_attachment);
    if (!tex || !tex->used) {
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }

    fbo->is_complete = true;
    return GL_FRAMEBUFFER_COMPLETE;
}

GL_APICALL void GL_APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                                        GLenum renderbuffertarget, GLuint renderbuffer) {
    GET_CTX();
    (void)renderbuffertarget;

    if (target != GL_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (ctx->bound_framebuffer == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
    if (!fbo) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    switch (attachment) {
        case GL_DEPTH_ATTACHMENT:
            fbo->depth_attachment = renderbuffer;
            /* Re-bind to update depth attachment */
            if (ctx->backend && ctx->backend->ops->bind_framebuffer && fbo->color_attachment) {
                ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                     fbo->color_attachment, renderbuffer);
            }
            break;
        case GL_STENCIL_ATTACHMENT:
            fbo->stencil_attachment = renderbuffer;
            break;
        case GL_DEPTH_STENCIL_ATTACHMENT:
            fbo->depth_attachment = renderbuffer;
            fbo->stencil_attachment = renderbuffer;
            /* Re-bind to update depth attachment */
            if (ctx->backend && ctx->backend->ops->bind_framebuffer && fbo->color_attachment) {
                ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                     fbo->color_attachment, renderbuffer);
            }
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    SGL_TRACE_FBO("glFramebufferRenderbuffer(attachment=0x%X, rb=%u)", attachment, renderbuffer);
}

GL_APICALL void GL_APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                                                    GLenum pname, GLint *params) {
    GET_CTX();

    if (target != GL_FRAMEBUFFER || !params) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (ctx->bound_framebuffer == 0) {
        *params = 0;
        return;
    }

    sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
    if (!fbo) {
        *params = 0;
        return;
    }

    GLuint obj = 0;
    switch (attachment) {
        case GL_COLOR_ATTACHMENT0:
            obj = fbo->color_attachment;
            break;
        case GL_DEPTH_ATTACHMENT:
            obj = fbo->depth_attachment;
            break;
        case GL_STENCIL_ATTACHMENT:
            obj = fbo->stencil_attachment;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    switch (pname) {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            if (obj == 0) {
                *params = GL_NONE;
            } else {
                *params = GL_TEXTURE;  /* We only support texture attachments */
            }
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            *params = obj;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
            *params = 0;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

/* Renderbuffer Objects */

GL_APICALL void GL_APIENTRY glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    GET_CTX();

    if (n < 0 || !renderbuffers) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    for (GLsizei i = 0; i < n; i++) {
        renderbuffers[i] = sgl_res_mgr_alloc_renderbuffer(&ctx->res_mgr);
        if (renderbuffers[i] == 0) {
            sgl_set_error(ctx, GL_OUT_OF_MEMORY);
            return;
        }
    }

    SGL_TRACE_FBO("glGenRenderbuffers(%d)", n);
}

GL_APICALL void GL_APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
    GET_CTX();

    if (n < 0) return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = renderbuffers[i];
        if (id == 0) continue;

        if (ctx->bound_renderbuffer == id) {
            ctx->bound_renderbuffer = 0;
        }

        sgl_res_mgr_free_renderbuffer(&ctx->res_mgr, id);
    }

    SGL_TRACE_FBO("glDeleteRenderbuffers(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsRenderbuffer(GLuint renderbuffer) {
    GET_CTX_RET(GL_FALSE);
    return GET_RENDERBUFFER(renderbuffer) ? GL_TRUE : GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    GET_CTX();

    if (target != GL_RENDERBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (renderbuffer != 0 && !GET_RENDERBUFFER(renderbuffer)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    ctx->bound_renderbuffer = renderbuffer;

    SGL_TRACE_FBO("glBindRenderbuffer(%u)", renderbuffer);
}

GL_APICALL void GL_APIENTRY glRenderbufferStorage(GLenum target, GLenum internalformat,
                                                    GLsizei width, GLsizei height) {
    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_RENDERBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (ctx->bound_renderbuffer == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_renderbuffer_t *rb = GET_RENDERBUFFER(ctx->bound_renderbuffer);
    if (!rb) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    rb->internal_format = internalformat;
    rb->width = width;
    rb->height = height;

    /* Allocate GPU storage for depth/stencil renderbuffers */
    if (ctx->backend->ops->renderbuffer_storage) {
        ctx->backend->ops->renderbuffer_storage(ctx->backend, ctx->bound_renderbuffer,
                                                 internalformat, width, height);
    }

    SGL_TRACE_FBO("glRenderbufferStorage(format=0x%X, %dx%d)", internalformat, width, height);
}

GL_APICALL void GL_APIENTRY glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    GET_CTX();

    if (target != GL_RENDERBUFFER || !params) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (ctx->bound_renderbuffer == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_renderbuffer_t *rb = GET_RENDERBUFFER(ctx->bound_renderbuffer);
    if (!rb) {
        *params = 0;
        return;
    }

    switch (pname) {
        case GL_RENDERBUFFER_WIDTH:
            *params = rb->width;
            break;
        case GL_RENDERBUFFER_HEIGHT:
            *params = rb->height;
            break;
        case GL_RENDERBUFFER_INTERNAL_FORMAT:
            *params = rb->internal_format;
            break;
        case GL_RENDERBUFFER_RED_SIZE:
        case GL_RENDERBUFFER_GREEN_SIZE:
        case GL_RENDERBUFFER_BLUE_SIZE:
        case GL_RENDERBUFFER_ALPHA_SIZE:
            *params = 8;
            break;
        case GL_RENDERBUFFER_DEPTH_SIZE:
            *params = (rb->internal_format == GL_DEPTH_COMPONENT16) ? 16 : 0;
            break;
        case GL_RENDERBUFFER_STENCIL_SIZE:
            *params = (rb->internal_format == GL_STENCIL_INDEX8) ? 8 : 0;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

/* ReadPixels */

GL_APICALL void GL_APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                                          GLenum format, GLenum type, void *pixels) {
    GET_CTX();
    CHECK_BACKEND();

    if (!pixels || width <= 0 || height <= 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Delegate to backend for actual GPU readback */
    if (ctx->backend->ops->read_pixels) {
        ctx->backend->ops->read_pixels(ctx->backend, x, y, width, height, format, type, pixels);
    }

    SGL_TRACE_FBO("glReadPixels(%d,%d %dx%d)", x, y, width, height);
}
