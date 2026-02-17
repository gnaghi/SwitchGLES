/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Draw Functions
 *
 * IMPORTANT: This file must NOT include deko3d.h or use any dk*() calls!
 * All GPU operations go through ctx->backend->ops->xxx()
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>

/* Prepare state before draw - delegates to backend */
static void sgl_prepare_draw(sgl_context_t *ctx) {
    if (!ctx->backend || !ctx->backend->ops) return;

    /* Apply viewport - MUST be set before drawing */
    if (ctx->backend->ops->apply_viewport) {
        sgl_viewport_state_t vs = {
            ctx->viewport_state.viewport_x,
            ctx->viewport_state.viewport_y,
            ctx->viewport_state.viewport_width,
            ctx->viewport_state.viewport_height,
            ctx->viewport_state.depth_near,
            ctx->viewport_state.depth_far
        };
        ctx->backend->ops->apply_viewport(ctx->backend, &vs);
    }

    /* Apply combined depth-stencil state (avoids overwrite issues) */
    if (ctx->backend->ops->apply_depth_stencil) {
        sgl_depth_stencil_state_t dss;
        /* Depth state */
        dss.depth_test_enabled = ctx->depth_state.depth_test_enabled;
        dss.depth_write_enabled = ctx->depth_state.depth_write_enabled;
        dss.depth_func = ctx->depth_state.depth_func;
        dss.depth_clear_value = ctx->depth_state.clear_depth;
        /* Stencil state */
        dss.stencil_test_enabled = ctx->depth_state.stencil_test_enabled;
        dss.stencil_front.func = ctx->depth_state.front.func;
        dss.stencil_front.ref = ctx->depth_state.front.ref;
        dss.stencil_front.func_mask = ctx->depth_state.front.func_mask;
        dss.stencil_front.write_mask = ctx->depth_state.front.write_mask;
        dss.stencil_front.fail_op = ctx->depth_state.front.fail_op;
        dss.stencil_front.zfail_op = ctx->depth_state.front.zfail_op;
        dss.stencil_front.zpass_op = ctx->depth_state.front.zpass_op;
        dss.stencil_back.func = ctx->depth_state.back.func;
        dss.stencil_back.ref = ctx->depth_state.back.ref;
        dss.stencil_back.func_mask = ctx->depth_state.back.func_mask;
        dss.stencil_back.write_mask = ctx->depth_state.back.write_mask;
        dss.stencil_back.fail_op = ctx->depth_state.back.fail_op;
        dss.stencil_back.zfail_op = ctx->depth_state.back.zfail_op;
        dss.stencil_back.zpass_op = ctx->depth_state.back.zpass_op;
        dss.stencil_clear_value = ctx->depth_state.clear_stencil;
        ctx->backend->ops->apply_depth_stencil(ctx->backend, &dss);
    } else {
        /* Fallback to separate calls if combined not available */
        if (ctx->backend->ops->apply_depth) {
            sgl_depth_state_t ds = {
                ctx->depth_state.depth_test_enabled,
                ctx->depth_state.depth_write_enabled,
                ctx->depth_state.depth_func,
                ctx->depth_state.clear_depth
            };
            ctx->backend->ops->apply_depth(ctx->backend, &ds);
        }
    }

    /* Apply blend state */
    if (ctx->backend->ops->apply_blend) {
        sgl_blend_state_t bs;
        bs.enabled = ctx->blend_state.enabled;
        bs.src_rgb = ctx->blend_state.src_rgb;
        bs.dst_rgb = ctx->blend_state.dst_rgb;
        bs.src_alpha = ctx->blend_state.src_alpha;
        bs.dst_alpha = ctx->blend_state.dst_alpha;
        bs.equation_rgb = ctx->blend_state.equation_rgb;
        bs.equation_alpha = ctx->blend_state.equation_alpha;
        ctx->backend->ops->apply_blend(ctx->backend, &bs);
    }

    /* Apply raster state (culling) */
    if (ctx->backend->ops->apply_raster) {
        sgl_raster_state_t rs;
        rs.cull_enabled = ctx->raster_state.cull_enabled;
        rs.cull_mode = ctx->raster_state.cull_mode;
        rs.front_face = ctx->raster_state.front_face;
        ctx->backend->ops->apply_raster(ctx->backend, &rs);
    }

    /* Apply color mask */
    if (ctx->backend->ops->apply_color_mask) {
        sgl_color_state_t cs;
        cs.mask[0] = ctx->color_state.mask[0];
        cs.mask[1] = ctx->color_state.mask[1];
        cs.mask[2] = ctx->color_state.mask[2];
        cs.mask[3] = ctx->color_state.mask[3];
        cs.clear_color[0] = ctx->color_state.clear_color[0];
        cs.clear_color[1] = ctx->color_state.clear_color[1];
        cs.clear_color[2] = ctx->color_state.clear_color[2];
        cs.clear_color[3] = ctx->color_state.clear_color[3];
        ctx->backend->ops->apply_color_mask(ctx->backend, &cs);
    }

    /* Apply scissor state */
    if (ctx->backend->ops->apply_scissor) {
        sgl_scissor_state_t ss;
        if (ctx->viewport_state.scissor_enabled) {
            ss.x = ctx->viewport_state.scissor_x;
            ss.y = ctx->viewport_state.scissor_y;
            ss.width = ctx->viewport_state.scissor_width;
            ss.height = ctx->viewport_state.scissor_height;
        } else {
            /* Scissor disabled - use full viewport */
            ss.x = ctx->viewport_state.viewport_x;
            ss.y = ctx->viewport_state.viewport_y;
            ss.width = ctx->viewport_state.viewport_width;
            ss.height = ctx->viewport_state.viewport_height;
        }
        ss.enabled = ctx->viewport_state.scissor_enabled;
        ctx->backend->ops->apply_scissor(ctx->backend, &ss);
    }

    /* Bind program with shaders FIRST (textures must be bound AFTER shaders in deko3d) */
    if (ctx->current_program > 0) {
        sgl_bind_program_for_draw(ctx, ctx->current_program);
    }

    /* Bind all active texture units AFTER program (deko3d requires bindTextures after bindShaders) */
    if (ctx->backend->ops->bind_texture) {
        for (GLuint unit = 0; unit < SGL_MAX_TEXTURE_UNITS; unit++) {
            GLuint tex_id = ctx->bound_textures[unit];
            if (tex_id > 0) {
                sgl_texture_t *tex = GET_TEXTURE(tex_id);
                if (tex && tex->used) {
                    /* Pass texture params to backend for sampler creation */
                    GLenum target = tex->target ? tex->target : GL_TEXTURE_2D;
                    if (ctx->backend->ops->texture_parameter) {
                        ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_MIN_FILTER, tex->min_filter);
                        ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_MAG_FILTER, tex->mag_filter);
                        ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_WRAP_S, tex->wrap_s);
                        ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_WRAP_T, tex->wrap_t);
                    }
                    ctx->backend->ops->bind_texture(ctx->backend, unit, tex_id);
                }
            }
        }
    }
}

GL_APICALL void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    GET_CTX();
    CHECK_BACKEND();

    if (count < 0 || first < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (count == 0) return;

    /* No program bound */
    if (ctx->current_program == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Validate mode */
    switch (mode) {
        case GL_POINTS:
        case GL_LINES:
        case GL_LINE_LOOP:
        case GL_LINE_STRIP:
        case GL_TRIANGLES:
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* Prepare state */
    sgl_prepare_draw(ctx);

    /* Prepare vertex attributes with buffer offsets */
    sgl_vertex_attrib_t prepared_attribs[SGL_MAX_ATTRIBS];
    memcpy(prepared_attribs, ctx->vertex_attribs, sizeof(prepared_attribs));

    for (int i = 0; i < SGL_MAX_ATTRIBS; i++) {
        sgl_vertex_attrib_t *attr = &prepared_attribs[i];
        if (attr->enabled && attr->buffer > 0) {
            sgl_buffer_t *buf = GET_BUFFER(attr->buffer);
            if (buf) {
                /* Compute GPU offset: buffer's data_offset + pointer offset */
                attr->buffer_offset = buf->data_offset + (uint32_t)(uintptr_t)attr->pointer;
            }
        }
    }

    /* Bind vertex attributes via backend */
    if (ctx->backend->ops->bind_vertex_attribs) {
        ctx->backend->ops->bind_vertex_attribs(ctx->backend, prepared_attribs,
                                               SGL_MAX_ATTRIBS, first, count);
    }

    /* Draw via backend */
    if (ctx->backend->ops->draw_arrays) {
        ctx->backend->ops->draw_arrays(ctx->backend, mode, first, count);
    }

    SGL_TRACE_DRAW("glDrawArrays(mode=0x%X, first=%d, count=%d)", mode, first, count);
}

GL_APICALL void GL_APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    GET_CTX();
    CHECK_BACKEND();

    if (count < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (count == 0) return;

    /* No program bound */
    if (ctx->current_program == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Validate mode */
    switch (mode) {
        case GL_POINTS:
        case GL_LINES:
        case GL_LINE_LOOP:
        case GL_LINE_STRIP:
        case GL_TRIANGLES:
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* Validate type */
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_UNSIGNED_SHORT:
        case GL_UNSIGNED_INT:
            break;
        default:
            sgl_set_error(ctx, GL_INVALID_ENUM);
            return;
    }

    /* Prepare state */
    sgl_prepare_draw(ctx);

    /* Prepare vertex attributes with buffer offsets */
    sgl_vertex_attrib_t prepared_attribs[SGL_MAX_ATTRIBS];
    memcpy(prepared_attribs, ctx->vertex_attribs, sizeof(prepared_attribs));

    for (int i = 0; i < SGL_MAX_ATTRIBS; i++) {
        sgl_vertex_attrib_t *attr = &prepared_attribs[i];
        if (attr->enabled && attr->buffer > 0) {
            sgl_buffer_t *buf = GET_BUFFER(attr->buffer);
            if (buf) {
                /* Compute GPU offset: buffer's data_offset + pointer offset */
                attr->buffer_offset = buf->data_offset + (uint32_t)(uintptr_t)attr->pointer;
            }
        }
    }

    /* Compute actual vertex count needed for client-side array allocation.
     * For glDrawElements, 'count' is the number of INDICES, not vertices.
     * When using client-side vertex arrays, we need max_vertex_index + 1
     * to avoid reading past the end of the vertex arrays. */
    GLsizei vertex_count = count;  /* Default: use index count (safe for VBOs) */
    if (ctx->bound_element_buffer == 0 && indices != NULL) {
        /* Client-side indices: scan for max vertex index */
        GLuint max_idx = 0;
        if (type == GL_UNSIGNED_BYTE) {
            const GLubyte *idx8 = (const GLubyte *)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (idx8[i] > max_idx) max_idx = idx8[i];
            }
        } else if (type == GL_UNSIGNED_SHORT) {
            const GLushort *idx16 = (const GLushort *)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (idx16[i] > max_idx) max_idx = idx16[i];
            }
        } else if (type == GL_UNSIGNED_INT) {
            const GLuint *idx32 = (const GLuint *)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (idx32[i] > max_idx) max_idx = idx32[i];
            }
        }
        vertex_count = (GLsizei)(max_idx + 1);
    }

    /* Bind vertex attributes via backend */
    if (ctx->backend->ops->bind_vertex_attribs) {
        ctx->backend->ops->bind_vertex_attribs(ctx->backend, prepared_attribs,
                                               SGL_MAX_ATTRIBS, 0, vertex_count);
    }

    /* Compute index buffer offset if EBO is bound */
    uint32_t ebo_data_offset = 0;
    if (ctx->bound_element_buffer > 0) {
        sgl_buffer_t *ebo_buf = GET_BUFFER(ctx->bound_element_buffer);
        if (ebo_buf) {
            /* indices is an offset into the bound EBO */
            ebo_data_offset = ebo_buf->data_offset + (uint32_t)(uintptr_t)indices;
        }
    }

    /* Draw elements via backend - pass ebo_data_offset, backend will copy client indices if ebo=0 */
    if (ctx->backend->ops->draw_elements) {
        ctx->backend->ops->draw_elements(ctx->backend, mode, count, type,
                                         indices, ebo_data_offset);
    }

    SGL_TRACE_DRAW("glDrawElements(mode=0x%X, count=%d, type=0x%X)", mode, count, type);
}
