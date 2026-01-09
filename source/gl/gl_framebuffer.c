/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Framebuffer Objects
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

/* GL ES 3.0 constants (GL_DEPTH_COMPONENT24, GL_RGBA8, etc.) available via <GLES3/gl3.h> */

/* Forward declarations */
static bool sgl_is_color_renderable(GLenum fmt);

/* Framebuffer Objects */

GL_APICALL void GL_APIENTRY glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
    GET_CTX();

    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (n == 0 || !framebuffers) return;

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

    if (n < 0) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    if (!framebuffers) return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = framebuffers[i];
        if (id == 0) continue;

        if (ctx->bound_framebuffer == id) ctx->bound_framebuffer = 0;
        if (ctx->bound_read_framebuffer == id) ctx->bound_read_framebuffer = 0;
        if (ctx->bound_draw_framebuffer == id) ctx->bound_draw_framebuffer = 0;

        /* Rebind default framebuffer if the draw target was deleted */
        if (ctx->bound_framebuffer == 0) {
            if (ctx->backend && ctx->backend->ops->bind_framebuffer) {
                ctx->backend->ops->bind_framebuffer(ctx->backend, 0, 0, 0, false, false, 0, false);
            }
        }

        /* Release FBO references to textures and renderbuffers (may trigger deferred GPU cleanup) */
        sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(id);
        if (fbo && fbo->used) {
            GLuint att_ids[3] = { 0, 0, 0 };
            bool is_rb[3] = { false, false, false };
            att_ids[0] = fbo->color_attachment;   is_rb[0] = fbo->color_is_renderbuffer;
            att_ids[1] = fbo->depth_attachment;    is_rb[1] = fbo->depth_is_renderbuffer;
            att_ids[2] = fbo->stencil_attachment;  is_rb[2] = fbo->stencil_is_renderbuffer;
            for (int a = 0; a < 3; a++) {
                if (att_ids[a] == 0) continue;
                if (!is_rb[a]) {
                    sgl_texture_t *tex = GET_TEXTURE_ANY(att_ids[a]);
                    if (tex) {
                        tex->fbo_ref_count--;
                        if (tex->delete_pending && tex->fbo_ref_count <= 0) {
                            if (ctx->backend && ctx->backend->ops && ctx->backend->ops->delete_texture)
                                ctx->backend->ops->delete_texture(ctx->backend, att_ids[a]);
                            sgl_res_mgr_free_texture(&ctx->res_mgr, att_ids[a]);
                        }
                    }
                } else {
                    sgl_renderbuffer_t *rb = GET_RENDERBUFFER_ANY(att_ids[a]);
                    if (rb) {
                        rb->fbo_ref_count--;
                        if (rb->delete_pending && rb->fbo_ref_count <= 0) {
                            if (ctx->backend && ctx->backend->ops && ctx->backend->ops->delete_renderbuffer)
                                ctx->backend->ops->delete_renderbuffer(ctx->backend, att_ids[a]);
                            sgl_res_mgr_free_renderbuffer(&ctx->res_mgr, att_ids[a]);
                        }
                    }
                }
            }
        }

        sgl_res_mgr_free_framebuffer(&ctx->res_mgr, id);

        /* Remove from overflow list if present */
        for (int j = 0; j < ctx->res_mgr.num_overflow_fbos; j++) {
            if (ctx->res_mgr.overflow_fbo_ids[j] == id) {
                ctx->res_mgr.overflow_fbo_ids[j] = ctx->res_mgr.overflow_fbo_ids[--ctx->res_mgr.num_overflow_fbos];
                break;
            }
        }
    }

    SGL_TRACE_FBO("glDeleteFramebuffers(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsFramebuffer(GLuint framebuffer) {
    GET_CTX_RET(GL_FALSE);
    if (framebuffer == 0) return GL_FALSE;
    sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(framebuffer);
    if (fbo && fbo->bound) return GL_TRUE;
    /* Check overflow IDs */
    for (int i = 0; i < ctx->res_mgr.num_overflow_fbos; i++) {
        if (ctx->res_mgr.overflow_fbo_ids[i] == framebuffer)
            return GL_TRUE;
    }
    return GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer) {
    GET_CTX();
    CHECK_BACKEND();

    if (target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (framebuffer != 0) {
        sgl_framebuffer_t *fbo_obj = GET_FRAMEBUFFER(framebuffer);
        if (!fbo_obj) {
            /* GLES2 spec: binding an unused name implicitly creates the object */
            if (framebuffer > 0 && framebuffer < SGL_MAX_FRAMEBUFFERS) {
                memset(&ctx->res_mgr.framebuffers[framebuffer], 0, sizeof(sgl_framebuffer_t));
                ctx->res_mgr.framebuffers[framebuffer].used = true;
                fbo_obj = &ctx->res_mgr.framebuffers[framebuffer];
            } else {
                /* Overflow: track for glIsFramebuffer */
                bool found = false;
                for (int i = 0; i < ctx->res_mgr.num_overflow_fbos; i++) {
                    if (ctx->res_mgr.overflow_fbo_ids[i] == framebuffer) { found = true; break; }
                }
                if (!found && ctx->res_mgr.num_overflow_fbos < SGL_MAX_OVERFLOW_IDS) {
                    ctx->res_mgr.overflow_fbo_ids[ctx->res_mgr.num_overflow_fbos++] = framebuffer;
                }
                /* Don't actually bind overflow FBO (no backing storage) */
                return;
            }
        }
        fbo_obj->bound = true;  /* Mark as object (for glIsFramebuffer) */
    }

    /* Save previous draw framebuffer for redundancy check */
    GLuint prev_draw_fbo = ctx->bound_framebuffer;

    /* Track read/draw framebuffer bindings separately */
    if (target == GL_FRAMEBUFFER) {
        ctx->bound_framebuffer = framebuffer;
        ctx->bound_read_framebuffer = framebuffer;
        ctx->bound_draw_framebuffer = framebuffer;
    } else if (target == GL_READ_FRAMEBUFFER) {
        ctx->bound_read_framebuffer = framebuffer;
        /* READ_FRAMEBUFFER does NOT switch the render target */
        SGL_TRACE_FBO("glBindFramebuffer(GL_READ_FRAMEBUFFER, %u)", framebuffer);
        return;
    } else { /* GL_DRAW_FRAMEBUFFER */
        ctx->bound_draw_framebuffer = framebuffer;
        ctx->bound_framebuffer = framebuffer;
    }

    /* Skip GPU commands if the draw target isn't changing.
     * The GLOVE-pattern flush in dk_clear handles the accumulation problem
     * (NotSupported tests that skip eglSwapBuffers). No need for barriers here. */
    if (framebuffer == prev_draw_fbo) {
        SGL_TRACE_FBO("glBindFramebuffer(0x%X, %u) (no-op, already bound)", target, framebuffer);
        return;
    }

    sgl_ensure_frame_ready();

    /* Insert barrier when switching render targets */
    if (ctx->backend->ops->insert_barrier) {
        ctx->backend->ops->insert_barrier(ctx->backend);
    }

    /* Delegate render target switch to backend */
    if (ctx->backend->ops->bind_framebuffer) {
        sgl_handle_t color_tex = 0;
        sgl_handle_t depth_rb = 0;
        sgl_handle_t stencil_rb = 0;
        bool color_is_rb = false;
        bool depth_is_rb = false;
        bool stencil_is_rb = false;
        if (framebuffer != 0) {
            sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(framebuffer);
            if (fbo) {
                color_tex = fbo->color_attachment;
                depth_rb = fbo->depth_attachment;
                stencil_rb = fbo->stencil_attachment;
                color_is_rb = fbo->color_is_renderbuffer;
                depth_is_rb = fbo->depth_is_renderbuffer;
                stencil_is_rb = fbo->stencil_is_renderbuffer;
                /* Don't pass non-color-renderable attachments as color target
                 * to the backend — they crash the GPU when bound as render targets. */
                if (color_tex != 0) {
                    bool color_ok = true;
                    if (color_is_rb) {
                        sgl_renderbuffer_t *crb = GET_RENDERBUFFER_ANY(color_tex);
                        if (!crb || !sgl_is_color_renderable(crb->internal_format))
                            color_ok = false;
                    } else {
                        sgl_texture_t *ctex = GET_TEXTURE_ANY(color_tex);
                        if (!ctex || !sgl_is_color_renderable(ctex->internal_format))
                            color_ok = false;
                    }
                    if (!color_ok) color_tex = 0;
                }
            }
        }
        ctx->backend->ops->bind_framebuffer(ctx->backend, framebuffer,
                                             color_tex, depth_rb,
                                             color_is_rb, depth_is_rb,
                                             stencil_rb, stencil_is_rb);
    }

    SGL_TRACE_FBO("glBindFramebuffer(0x%X, %u)", target, framebuffer);
}

GL_APICALL void GL_APIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment,
                                                     GLenum textarget, GLuint texture, GLint level) {
    GET_CTX();

    if (target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* GLES2: level must be 0 */
    if (level != 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate textarget and texture existence */
    if (texture != 0) {
        if (textarget != GL_TEXTURE_2D &&
            !(textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
              textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
        }
        sgl_texture_t *tex = GET_TEXTURE(texture);
        if (!tex) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        /* GLES2 §4.4.3: texture must name an existing texture object with a target.
         * A generated-but-never-bound name has target == 0 → GL_INVALID_OPERATION. */
        if (tex->target == 0) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        /* GLES2: textarget must match the texture's actual target type.
         * 2D texture → textarget must be GL_TEXTURE_2D.
         * Cubemap texture → textarget must be a cube face. */
        {
            bool is_cube_face = (textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                                 textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z);
            if (tex->target == GL_TEXTURE_2D && textarget != GL_TEXTURE_2D) {
                sgl_set_error(ctx, GL_INVALID_OPERATION);
                return;
            }
            if (tex->target == GL_TEXTURE_CUBE_MAP && !is_cube_face) {
                sgl_set_error(ctx, GL_INVALID_OPERATION);
                return;
            }
        }
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

    /* Determine old attachment to decrement ref count */
    GLuint old_tex_id = 0;
    bool old_is_rb = false;
    switch (attachment) {
        case GL_COLOR_ATTACHMENT0:  old_tex_id = fbo->color_attachment; old_is_rb = fbo->color_is_renderbuffer; break;
        case GL_DEPTH_ATTACHMENT:   old_tex_id = fbo->depth_attachment; old_is_rb = fbo->depth_is_renderbuffer; break;
        case GL_STENCIL_ATTACHMENT: old_tex_id = fbo->stencil_attachment; old_is_rb = fbo->stencil_is_renderbuffer; break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* Decrement ref count on old texture being replaced.
     * Skip if same texture is being re-attached (no net change). */
    if (old_tex_id != 0 && !old_is_rb && old_tex_id != texture) {
        sgl_texture_t *old_tex = GET_TEXTURE_ANY(old_tex_id);
        if (old_tex) {
            old_tex->fbo_ref_count--;
            /* If pending deletion and no more FBO refs, finalize GPU cleanup */
            if (old_tex->delete_pending && old_tex->fbo_ref_count <= 0) {
                if (ctx->backend && ctx->backend->ops && ctx->backend->ops->delete_texture)
                    ctx->backend->ops->delete_texture(ctx->backend, old_tex_id);
                sgl_res_mgr_free_texture(&ctx->res_mgr, old_tex_id);
            }
        }
    }

    /* Increment ref count on new texture being attached.
     * Skip if same texture is being re-attached (no net change). */
    if (texture != 0 && texture != old_tex_id) {
        sgl_texture_t *new_tex = GET_TEXTURE(texture);
        if (new_tex) {
            new_tex->fbo_ref_count++;
        }
    } else if (texture != 0) {
    }

    switch (attachment) {
        case GL_COLOR_ATTACHMENT0:
            fbo->color_attachment = texture;
            fbo->color_is_renderbuffer = false;
            fbo->color_textarget = (texture != 0) ? textarget : 0;
            break;
        case GL_DEPTH_ATTACHMENT:
            fbo->depth_attachment = texture;
            fbo->depth_is_renderbuffer = false;
            fbo->depth_textarget = (texture != 0) ? textarget : 0;
            break;
        case GL_STENCIL_ATTACHMENT:
            fbo->stencil_attachment = texture;
            fbo->stencil_is_renderbuffer = false;
            fbo->stencil_textarget = (texture != 0) ? textarget : 0;
            break;
        default: break; /* Already validated above */
    }

    /* Re-sync GPU state after any attachment change.
     * Without this, depth/stencil changes and color detach+reattach leave
     * stale render targets bound on the GPU (dEQP fbo.render failures).
     * Always re-sync when the FBO has at least one valid attachment —
     * previously gated on color_attachment!=0 which broke stencil-only FBOs
     * and recreate_colorbuffer tests. */
    if (ctx->backend && ctx->backend->ops->bind_framebuffer &&
        (fbo->color_attachment != 0 || fbo->depth_attachment != 0 || fbo->stencil_attachment != 0)) {
        bool color_ok = false;
        if (fbo->color_attachment != 0) {
            if (fbo->color_is_renderbuffer) {
                sgl_renderbuffer_t *crb = GET_RENDERBUFFER(fbo->color_attachment);
                color_ok = crb && sgl_is_color_renderable(crb->internal_format);
            } else {
                sgl_texture_t *tex_obj = GET_TEXTURE(fbo->color_attachment);
                color_ok = tex_obj && sgl_is_color_renderable(tex_obj->internal_format);
            }
        }
        /* Only call backend if color is valid (GPU needs a color render target)
         * or if we're detaching (color=0), to let backend know state changed. */
        if (color_ok || fbo->color_attachment == 0) {
            sgl_ensure_frame_ready();
            ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                 fbo->color_attachment, fbo->depth_attachment,
                                                 fbo->color_is_renderbuffer,
                                                 fbo->depth_is_renderbuffer,
                                                 fbo->stencil_attachment,
                                                 fbo->stencil_is_renderbuffer);
        }
    }

    SGL_TRACE_FBO("glFramebufferTexture2D(attachment=0x%X, texture=%u)", attachment, texture);
}

/* Helper: check if internalformat is color-renderable */
static bool sgl_is_color_renderable(GLenum fmt) {
    switch (fmt) {
        case GL_RGBA4: case GL_RGB5_A1: case GL_RGB565:
        case GL_RGBA8: case GL_RGB8:
        case GL_RGBA: case GL_RGB:
        case GL_LUMINANCE_ALPHA: case GL_LUMINANCE: case GL_ALPHA:
        case GL_BGRA_EXT: case GL_BGRA8_EXT:
            return true;
        default:
            return false;
    }
}

/* Helper: check if internalformat is depth-renderable (renderbuffers only).
 * GLES2 core: GL_DEPTH_COMPONENT16.
 * OES_depth24: GL_DEPTH_COMPONENT24, GL_DEPTH24_STENCIL8. */
static bool sgl_is_depth_renderable(GLenum fmt) {
    switch (fmt) {
        case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24:
        case GL_DEPTH24_STENCIL8:
            return true;
        default:
            return false;
    }
}

/* Helper: check if internalformat is stencil-renderable */
static bool sgl_is_stencil_renderable(GLenum fmt) {
    switch (fmt) {
        case GL_STENCIL_INDEX8:
        case GL_DEPTH24_STENCIL8: case GL_DEPTH_STENCIL:
            return true;
        default:
            return false;
    }
}

GL_APICALL GLenum GL_APIENTRY glCheckFramebufferStatus(GLenum target) {
    GET_CTX_RET(0);

    if (target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) {
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

    /* Must have at least one attachment (color, depth, or stencil) */
    if (fbo->color_attachment == 0 && fbo->depth_attachment == 0 && fbo->stencil_attachment == 0) {
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }

    GLsizei ref_width = 0, ref_height = 0;
    bool has_ref = false;

    /* Verify color attachment if present.
     * Use _ANY lookups: attachments may reference delete_pending textures/RBs
     * (deleted while FBO was not current — attachment persists per GLES2 §4.4.3). */
    if (fbo->color_attachment != 0) {
        if (fbo->color_is_renderbuffer) {
            sgl_renderbuffer_t *rb = GET_RENDERBUFFER_ANY(fbo->color_attachment);
            if (!rb || rb->width == 0 || rb->height == 0) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            if (!sgl_is_color_renderable(rb->internal_format)) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            ref_width = rb->width; ref_height = rb->height; has_ref = true;
        } else {
            sgl_texture_t *tex = GET_TEXTURE_ANY(fbo->color_attachment);
            if (!tex || tex->width == 0 || tex->height == 0) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            if (!sgl_is_color_renderable(tex->internal_format)) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            ref_width = tex->width; ref_height = tex->height; has_ref = true;
        }
    }

    /* Verify depth attachment if present */
    if (fbo->depth_attachment != 0) {
        if (fbo->depth_is_renderbuffer) {
            sgl_renderbuffer_t *rb = GET_RENDERBUFFER_ANY(fbo->depth_attachment);
            if (!rb || rb->width == 0 || rb->height == 0) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            if (!sgl_is_depth_renderable(rb->internal_format)) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            if (has_ref && (rb->width != ref_width || rb->height != ref_height)) {
                return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
            }
            if (!has_ref) { ref_width = rb->width; ref_height = rb->height; has_ref = true; }
        } else {
            /* Texture depth attachments require OES_depth_texture extension,
             * which we don't advertise. Always report INCOMPLETE. */
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
    }

    /* Verify stencil attachment if present */
    if (fbo->stencil_attachment != 0) {
        if (fbo->stencil_is_renderbuffer) {
            sgl_renderbuffer_t *rb = GET_RENDERBUFFER_ANY(fbo->stencil_attachment);
            if (!rb || rb->width == 0 || rb->height == 0) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            if (!sgl_is_stencil_renderable(rb->internal_format)) {
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            if (has_ref && (rb->width != ref_width || rb->height != ref_height)) {
                return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
            }
        } else {
            /* Texture stencil attachments not supported without extension.
             * Always report INCOMPLETE. */
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
    }

    fbo->is_complete = true;
    return GL_FRAMEBUFFER_COMPLETE;
}

GL_APICALL void GL_APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                                        GLenum renderbuffertarget, GLuint renderbuffer) {
    GET_CTX();

    if (target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* GLES2: renderbuffertarget must be GL_RENDERBUFFER */
    if (renderbuffer != 0 && renderbuffertarget != GL_RENDERBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    /* Check renderbuffer exists and has been bound (dEQP passes -1/0xFFFFFFFF as invalid name).
     * Per GLES2 §4.4.3: renderbuffer must name an existing renderbuffer object.
     * Generated-but-never-bound names are not valid objects. */
    if (renderbuffer != 0) {
        sgl_renderbuffer_t *rb = GET_RENDERBUFFER(renderbuffer);
        if (!rb || !rb->bound) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
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

    /* Decrement ref count on old RB attachment being replaced */
    GLuint old_rb_id = 0;
    switch (attachment) {
        case GL_COLOR_ATTACHMENT0:
            if (fbo->color_is_renderbuffer && fbo->color_attachment != 0) old_rb_id = fbo->color_attachment;
            break;
        case GL_DEPTH_ATTACHMENT:
            if (fbo->depth_is_renderbuffer && fbo->depth_attachment != 0) old_rb_id = fbo->depth_attachment;
            break;
        case GL_STENCIL_ATTACHMENT:
            if (fbo->stencil_is_renderbuffer && fbo->stencil_attachment != 0) old_rb_id = fbo->stencil_attachment;
            break;
        case GL_DEPTH_STENCIL_ATTACHMENT:
            /* Will handle both depth and stencil below */
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }
    if (old_rb_id != 0) {
        sgl_renderbuffer_t *old_rb = GET_RENDERBUFFER_ANY(old_rb_id);
        if (old_rb) {
            old_rb->fbo_ref_count--;
            if (old_rb->delete_pending && old_rb->fbo_ref_count <= 0) {
                if (ctx->backend && ctx->backend->ops && ctx->backend->ops->delete_renderbuffer)
                    ctx->backend->ops->delete_renderbuffer(ctx->backend, old_rb_id);
                sgl_res_mgr_free_renderbuffer(&ctx->res_mgr, old_rb_id);
            }
        }
    }
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        /* Decrement both depth and stencil old RB refs */
        if (fbo->depth_is_renderbuffer && fbo->depth_attachment != 0) {
            sgl_renderbuffer_t *drb = GET_RENDERBUFFER_ANY(fbo->depth_attachment);
            if (drb) { drb->fbo_ref_count--; if (drb->delete_pending && drb->fbo_ref_count <= 0) { if (ctx->backend && ctx->backend->ops && ctx->backend->ops->delete_renderbuffer) ctx->backend->ops->delete_renderbuffer(ctx->backend, fbo->depth_attachment); sgl_res_mgr_free_renderbuffer(&ctx->res_mgr, fbo->depth_attachment); } }
        }
        if (fbo->stencil_is_renderbuffer && fbo->stencil_attachment != 0 &&
            fbo->stencil_attachment != fbo->depth_attachment) {
            sgl_renderbuffer_t *srb = GET_RENDERBUFFER_ANY(fbo->stencil_attachment);
            if (srb) { srb->fbo_ref_count--; if (srb->delete_pending && srb->fbo_ref_count <= 0) { if (ctx->backend && ctx->backend->ops && ctx->backend->ops->delete_renderbuffer) ctx->backend->ops->delete_renderbuffer(ctx->backend, fbo->stencil_attachment); sgl_res_mgr_free_renderbuffer(&ctx->res_mgr, fbo->stencil_attachment); } }
        }
    }

    /* Increment ref count on new RB being attached */
    if (renderbuffer != 0) {
        sgl_renderbuffer_t *new_rb = GET_RENDERBUFFER(renderbuffer);
        if (new_rb) new_rb->fbo_ref_count++;
    }

    switch (attachment) {
        case GL_COLOR_ATTACHMENT0:
            fbo->color_attachment = renderbuffer;
            fbo->color_is_renderbuffer = true;
            fbo->color_textarget = 0;
            break;
        case GL_DEPTH_ATTACHMENT:
            fbo->depth_attachment = renderbuffer;
            fbo->depth_is_renderbuffer = true;
            fbo->depth_textarget = 0;
            break;
        case GL_STENCIL_ATTACHMENT:
            fbo->stencil_attachment = renderbuffer;
            fbo->stencil_is_renderbuffer = true;
            fbo->stencil_textarget = 0;
            break;
        case GL_DEPTH_STENCIL_ATTACHMENT:
            fbo->depth_attachment = renderbuffer;
            fbo->stencil_attachment = renderbuffer;
            fbo->depth_is_renderbuffer = true;
            fbo->stencil_is_renderbuffer = true;
            fbo->depth_textarget = 0;
            fbo->stencil_textarget = 0;
            if (renderbuffer != 0) {
                /* Second ref for the other attachment point */
                sgl_renderbuffer_t *rb2 = GET_RENDERBUFFER(renderbuffer);
                if (rb2) rb2->fbo_ref_count++;
            }
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* Re-sync GPU state after any attachment change.
     * Always re-sync when the FBO has at least one valid attachment —
     * previously gated on color_attachment!=0 which broke stencil-only FBOs. */
    if (ctx->backend && ctx->backend->ops->bind_framebuffer &&
        (fbo->color_attachment != 0 || fbo->depth_attachment != 0 || fbo->stencil_attachment != 0)) {
        bool color_ok = false;
        if (fbo->color_attachment != 0) {
            if (fbo->color_is_renderbuffer) {
                sgl_renderbuffer_t *crb = GET_RENDERBUFFER(fbo->color_attachment);
                color_ok = crb && sgl_is_color_renderable(crb->internal_format);
            } else {
                sgl_texture_t *tex = GET_TEXTURE(fbo->color_attachment);
                color_ok = tex && sgl_is_color_renderable(tex->internal_format);
            }
        }
        if (color_ok || fbo->color_attachment == 0) {
            sgl_ensure_frame_ready();
            ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                 fbo->color_attachment, fbo->depth_attachment,
                                                 fbo->color_is_renderbuffer,
                                                 fbo->depth_is_renderbuffer,
                                                 fbo->stencil_attachment,
                                                 fbo->stencil_is_renderbuffer);
        }
    }

    SGL_TRACE_FBO("glFramebufferRenderbuffer(attachment=0x%X, rb=%u)", attachment, renderbuffer);
}

GL_APICALL void GL_APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                                                    GLenum pname, GLint *params) {
    GET_CTX();

    if (target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!params) return;

    /* Default framebuffer (GLES2 §6.1.3): querying attachment parameters on the
     * default framebuffer generates GL_INVALID_OPERATION. */
    if (ctx->bound_framebuffer == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
    if (!fbo) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
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
        case GL_DEPTH_STENCIL_ATTACHMENT:
            obj = fbo->depth_attachment;
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* Determine if this attachment is a renderbuffer */
    bool is_renderbuffer = false;
    if (attachment == GL_COLOR_ATTACHMENT0) {
        is_renderbuffer = fbo->color_is_renderbuffer;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        is_renderbuffer = fbo->depth_is_renderbuffer;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        is_renderbuffer = fbo->stencil_is_renderbuffer;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        is_renderbuffer = fbo->depth_is_renderbuffer;
    }

    /* When attachment type is GL_NONE (nothing attached), only OBJECT_TYPE is queryable.
     * All other pnames must return GL_INVALID_ENUM per GLES2 spec 6.1.3. */
    if (obj == 0) {
        if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) {
            *params = GL_NONE;
        } else {
            sgl_set_error(ctx, GL_INVALID_ENUM);
        }
        return;
    }

    switch (pname) {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            *params = is_renderbuffer ? GL_RENDERBUFFER : GL_TEXTURE;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            *params = obj;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
            if (is_renderbuffer) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
            } else {
                *params = 0;
            }
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
            if (is_renderbuffer) {
                sgl_set_error(ctx, GL_INVALID_ENUM);
            } else {
                /* Return the textarget if it was a cubemap face, else 0 */
                GLenum face = 0;
                if (attachment == GL_COLOR_ATTACHMENT0) face = fbo->color_textarget;
                else if (attachment == GL_DEPTH_ATTACHMENT) face = fbo->depth_textarget;
                else if (attachment == GL_STENCIL_ATTACHMENT) face = fbo->stencil_textarget;
                /* Only return cube face enums, not GL_TEXTURE_2D */
                if (face >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && face <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
                    *params = face;
                else
                    *params = 0;
            }
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            break;
    }
}

/* Renderbuffer Objects */

GL_APICALL void GL_APIENTRY glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    GET_CTX();

    if (n < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (n == 0 || !renderbuffers) return;

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

    if (n < 0) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }
    if (!renderbuffers) return;

    for (GLsizei i = 0; i < n; i++) {
        GLuint id = renderbuffers[i];
        if (id == 0) continue;

        if (ctx->bound_renderbuffer == id) {
            ctx->bound_renderbuffer = 0;
        }

        /* GLES2 spec §5.4.1: Only detach from the CURRENTLY BOUND framebuffer.
         * "The renderbuffer image is specifically not detached from any other
         * framebuffer objects." Non-bound FBOs keep their attachment references
         * so that re-binding them later still refers to the correct object. */
        bool detached = false;
        sgl_renderbuffer_t *rb_detach = GET_RENDERBUFFER_ANY(id);
        if (ctx->bound_framebuffer != 0) {
            sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
            if (fbo && fbo->used) {
                if (fbo->color_is_renderbuffer && fbo->color_attachment == id) {
                    fbo->color_attachment = 0;
                    detached = true;
                    if (rb_detach) rb_detach->fbo_ref_count--;
                }
                if (fbo->depth_is_renderbuffer && fbo->depth_attachment == id) {
                    fbo->depth_attachment = 0;
                    detached = true;
                    if (rb_detach) rb_detach->fbo_ref_count--;
                }
                if (fbo->stencil_is_renderbuffer && fbo->stencil_attachment == id) {
                    fbo->stencil_attachment = 0;
                    detached = true;
                    if (rb_detach) rb_detach->fbo_ref_count--;
                }
            }
        }

        /* Re-bind FBO to update backend render target BEFORE deleting the
         * renderbuffer. Without this, dk_submit_and_reset during deletion
         * would try to re-bind the stale (freed) renderbuffer as render target. */
        if (detached && ctx->backend && ctx->backend->ops->bind_framebuffer) {
            sgl_framebuffer_t *rebind_fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
            if (rebind_fbo) {
                ctx->backend->ops->bind_framebuffer(ctx->backend, ctx->bound_framebuffer,
                                                     rebind_fbo->color_attachment, rebind_fbo->depth_attachment,
                                                     rebind_fbo->color_is_renderbuffer,
                                                     rebind_fbo->depth_is_renderbuffer,
                                                     rebind_fbo->stencil_attachment,
                                                     rebind_fbo->stencil_is_renderbuffer);
            }
        }

        /* Check if RB is still referenced by non-current FBOs (deferred deletion).
         * If fbo_ref_count > 0, keep GPU resources alive; name will not be
         * recycled because alloc_renderbuffer skips delete_pending entries. */
        sgl_renderbuffer_t *rb = GET_RENDERBUFFER(id);
        if (rb && rb->fbo_ref_count > 0) {
            rb->delete_pending = true;
            rb->used = false;
            SGL_TRACE_FBO("glDeleteRenderbuffers: RB %u deferred (fbo_ref_count=%d)", id, rb->fbo_ref_count);
        } else {
            /* Notify backend to flush GPU and free resources */
            if (ctx->backend && ctx->backend->ops->delete_renderbuffer) {
                ctx->backend->ops->delete_renderbuffer(ctx->backend, id);
            }
            sgl_res_mgr_free_renderbuffer(&ctx->res_mgr, id);
        }

        /* Remove from overflow list if present */
        for (int j = 0; j < ctx->res_mgr.num_overflow_rbos; j++) {
            if (ctx->res_mgr.overflow_rbo_ids[j] == id) {
                ctx->res_mgr.overflow_rbo_ids[j] = ctx->res_mgr.overflow_rbo_ids[--ctx->res_mgr.num_overflow_rbos];
                break;
            }
        }
    }

    SGL_TRACE_FBO("glDeleteRenderbuffers(%d)", n);
}

GL_APICALL GLboolean GL_APIENTRY glIsRenderbuffer(GLuint renderbuffer) {
    GET_CTX_RET(GL_FALSE);
    if (renderbuffer == 0) return GL_FALSE;
    sgl_renderbuffer_t *rb = GET_RENDERBUFFER(renderbuffer);
    if (rb && rb->bound) return GL_TRUE;
    /* Check overflow IDs */
    for (int i = 0; i < ctx->res_mgr.num_overflow_rbos; i++) {
        if (ctx->res_mgr.overflow_rbo_ids[i] == renderbuffer)
            return GL_TRUE;
    }
    return GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    GET_CTX();

    if (target != GL_RENDERBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }

    if (renderbuffer != 0) {
        sgl_renderbuffer_t *rb_obj = GET_RENDERBUFFER(renderbuffer);
        if (!rb_obj) {
            /* GLES2 spec: binding an unused name implicitly creates the object */
            if (renderbuffer > 0 && renderbuffer < SGL_MAX_RENDERBUFFERS) {
                memset(&ctx->res_mgr.renderbuffers[renderbuffer], 0, sizeof(sgl_renderbuffer_t));
                ctx->res_mgr.renderbuffers[renderbuffer].used = true;
                rb_obj = &ctx->res_mgr.renderbuffers[renderbuffer];
            } else {
                /* Overflow: track for glIsRenderbuffer */
                bool found = false;
                for (int i = 0; i < ctx->res_mgr.num_overflow_rbos; i++) {
                    if (ctx->res_mgr.overflow_rbo_ids[i] == renderbuffer) { found = true; break; }
                }
                if (!found && ctx->res_mgr.num_overflow_rbos < SGL_MAX_OVERFLOW_IDS) {
                    ctx->res_mgr.overflow_rbo_ids[ctx->res_mgr.num_overflow_rbos++] = renderbuffer;
                }
                /* Don't actually bind (no backing storage) */
                return;
            }
        }
        rb_obj->bound = true;  /* Mark as object (for glIsRenderbuffer) */
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

    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (width > 8192 || height > 8192) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Validate internalformat: accept all formats our backend supports.
     * GLES2 core + extensions we advertise (BGRA, depth24).
     * Note: GL_DEPTH_COMPONENT32 not accepted (GL_OES_depth32 not advertised). */
    switch (internalformat) {
        case GL_RGBA4: case GL_RGB5_A1: case GL_RGB565:
        case GL_RGBA8: case GL_RGB8:
        case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24:
        case GL_DEPTH24_STENCIL8:
        case GL_STENCIL_INDEX8:
        case GL_BGRA_EXT: case GL_BGRA8_EXT:
            break; /* accepted */
        default:
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

    if (target != GL_RENDERBUFFER) {
        sgl_set_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!params) return;

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
            /* Color-renderable formats return 8 (promoted to RGBA8).
             * Depth/stencil formats have no color components. */
            *params = sgl_is_color_renderable(rb->internal_format) ? 8 : 0;
            break;
        case GL_RENDERBUFFER_DEPTH_SIZE:
            if (rb->internal_format == GL_DEPTH_COMPONENT16)
                *params = 16;
            else if (rb->internal_format == GL_DEPTH_COMPONENT24 ||
                     rb->internal_format == GL_DEPTH24_STENCIL8)
                *params = 24;  /* All map to Z24S8 on hardware */
            else
                *params = 0;
            break;
        case GL_RENDERBUFFER_STENCIL_SIZE:
            *params = (rb->internal_format == GL_STENCIL_INDEX8 ||
                       rb->internal_format == GL_DEPTH24_STENCIL8) ? 8 : 0;
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

    if (width < 0 || height < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (!pixels || width == 0 || height == 0) {
        return;  /* No-op per spec */
    }

    /* Check framebuffer completeness */
    if (ctx->bound_framebuffer != 0) {
        sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
        if (!fbo || !fbo->color_attachment) {
            sgl_set_error(ctx, GL_INVALID_FRAMEBUFFER_OPERATION);
            return;
        }
    }

    /* RGBA + UNSIGNED_BYTE is the implementation-supported combination.
     * GL_BGRA_EXT + UNSIGNED_BYTE added by GL_EXT_texture_format_BGRA8888.
     * Per GLES2 spec: GL_INVALID_OPERATION for valid but unsupported combos,
     * GL_INVALID_ENUM for invalid enum values. */
    if ((format != GL_RGBA && format != GL_BGRA_EXT) || type != GL_UNSIGNED_BYTE) {
        /* Check if format/type are valid GLES2 enums */
        bool valid_format = (format == GL_RGBA || format == GL_RGB ||
                             format == GL_LUMINANCE_ALPHA || format == GL_LUMINANCE ||
                             format == GL_ALPHA || format == GL_BGRA_EXT);
        bool valid_type = (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT_5_6_5 ||
                           type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1);
        sgl_set_error(ctx, (valid_format && valid_type) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }

    /* Delegate to backend for actual GPU readback */
    if (ctx->backend->ops->read_pixels) {
        ctx->backend->ops->read_pixels(ctx->backend, x, y, width, height, format, type, pixels);
    }

    SGL_TRACE_FBO("glReadPixels(%d,%d %dx%d)", x, y, width, height);
}

/* Blit framebuffer (GL_ARB_framebuffer_object / GL 3.0) */

GL_APICALL void GL_APIENTRY glBlitFramebuffer(
    GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
    GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
    GLbitfield mask, GLenum filter)
{
    sgl_ensure_frame_ready();

    GET_CTX();
    CHECK_BACKEND();

    if (!ctx->backend->ops->blit_framebuffer) {
        return;
    }

    /* Resolve read FBO's color texture */
    sgl_handle_t read_fbo = ctx->bound_read_framebuffer;
    sgl_handle_t read_color_tex = 0;
    if (read_fbo != 0) {
        sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(read_fbo);
        if (fbo) read_color_tex = fbo->color_attachment;
    }

    /* Resolve draw FBO's color texture */
    sgl_handle_t write_fbo = ctx->bound_draw_framebuffer;
    sgl_handle_t write_color_tex = 0;
    if (write_fbo != 0) {
        sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(write_fbo);
        if (fbo) write_color_tex = fbo->color_attachment;
    }

    ctx->backend->ops->blit_framebuffer(ctx->backend,
        read_fbo, read_color_tex, write_fbo, write_color_tex,
        srcX0, srcY0, srcX1, srcY1,
        dstX0, dstY0, dstX1, dstY1,
        mask, filter);

    SGL_TRACE_FBO("glBlitFramebuffer(%d,%d,%d,%d -> %d,%d,%d,%d mask=0x%X)",
                  srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask);
}

/* Multisample renderbuffer stub (falls back to non-multisampled storage) */

GL_APICALL void GL_APIENTRY glRenderbufferStorageMultisample(
    GLenum target, GLsizei samples, GLenum internalformat,
    GLsizei width, GLsizei height)
{
    (void)samples;
    glRenderbufferStorage(target, internalformat, width, height);
}
