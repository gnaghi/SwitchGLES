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

/* GLES2 §3.7.10 texture completeness check.
 * Returns false (incomplete) if:
 * - NPOT dimensions with REPEAT or MIRRORED_REPEAT wrap mode
 * - NPOT dimensions with mipmap min filter
 * - Cubemap face dimension mismatch (cubemap_incomplete flag) */
static bool sgl_is_texture_complete(const sgl_texture_t *tex) {
    if (!tex || !tex->used) return false;
    if (tex->cubemap_incomplete) return false;

    bool npot = !sgl_is_pot(tex->width) || !sgl_is_pot(tex->height);
    if (npot) {
        /* NPOT with REPEAT/MIRRORED_REPEAT → incomplete */
        if (tex->wrap_s == GL_REPEAT || tex->wrap_s == GL_MIRRORED_REPEAT ||
            tex->wrap_t == GL_REPEAT || tex->wrap_t == GL_MIRRORED_REPEAT) {
            return false;
        }
        /* NPOT with mipmap filtering → incomplete */
        if (tex->min_filter == GL_NEAREST_MIPMAP_NEAREST ||
            tex->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
            tex->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
            tex->min_filter == GL_LINEAR_MIPMAP_LINEAR) {
            return false;
        }
    }
    return true;
}

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
        bs.color[0] = ctx->blend_state.color[0];
        bs.color[1] = ctx->blend_state.color[1];
        bs.color[2] = ctx->blend_state.color[2];
        bs.color[3] = ctx->blend_state.color[3];
        ctx->backend->ops->apply_blend(ctx->backend, &bs);
    }

    /* Apply raster state (culling + polygon offset) */
    if (ctx->backend->ops->apply_raster) {
        sgl_raster_state_t rs;
        rs.cull_enabled = ctx->raster_state.cull_enabled;
        rs.cull_mode = ctx->raster_state.cull_mode;
        rs.front_face = ctx->raster_state.front_face;
        rs.polygon_offset_fill_enabled = ctx->raster_state.polygon_offset_fill_enabled;
        rs.polygon_offset_factor = ctx->raster_state.polygon_offset_factor;
        rs.polygon_offset_units = ctx->raster_state.polygon_offset_units;
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

    /* Populate gl_DepthRange built-in values into packed UBO before binding */
    if (ctx->current_program > 0) {
        sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
        if (prog && prog->has_depth_range) {
            /* Clamp per GLES2 spec */
            float near_val = ctx->viewport_state.depth_near;
            float far_val = ctx->viewport_state.depth_far;
            if (near_val < 0.0f) near_val = 0.0f;
            if (near_val > 1.0f) near_val = 1.0f;
            if (far_val < 0.0f) far_val = 0.0f;
            if (far_val > 1.0f) far_val = 1.0f;
            float diff_val = far_val - near_val;
            float dr_vals[3] = { near_val, far_val, diff_val };
            /* Write to primary locations (VS or transpiler) */
            for (int d = 0; d < 3; d++) {
                GLint loc = prog->depth_range_loc[d];
                if (!loc) continue;
                int stage = (loc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
                int offset = loc & SGL_LOC_OFFSET_MASK;
                sgl_packed_ubo_t *packed = (stage == 0)
                    ? &prog->packed_vertex[0]
                    : &prog->packed_fragment[0];
                if (packed->valid && offset + 4 <= packed->size) {
                    memcpy(packed->data + offset, &dr_vals[d], sizeof(float));
                    packed->dirty = true;
                }
            }
            /* Write to FS mirror locations (when both VS+FS use gl_DepthRange) */
            for (int d = 0; d < 3; d++) {
                GLint loc = prog->depth_range_loc_fs[d];
                if (!loc) continue;
                int stage = (loc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
                int offset = loc & SGL_LOC_OFFSET_MASK;
                sgl_packed_ubo_t *packed = (stage == 0)
                    ? &prog->packed_vertex[0]
                    : &prog->packed_fragment[0];
                if (packed->valid && offset + 4 <= packed->size) {
                    memcpy(packed->data + offset, &dr_vals[d], sizeof(float));
                    packed->dirty = true;
                }
            }
        }
    }

    /* Bind program with shaders FIRST (textures must be bound AFTER shaders in deko3d) */
    if (ctx->current_program > 0) {
        sgl_bind_program_for_draw(ctx, ctx->current_program);
    }

    /* Bind all active texture units AFTER program (deko3d requires bindTextures after bindShaders).
     * Use per-program sampler remap: for each sampler, bind the texture from its tex_unit
     * to the sampler's shader binding slot. This allows multiple samplers to share
     * the same tex_unit (e.g., initial value 0 per GLES2 spec) and each gets its binding. */
    if (ctx->backend->ops->bind_texture) {
        sgl_program_t *prog = (ctx->current_program > 0) ? GET_PROGRAM(ctx->current_program) : NULL;

        if (prog && prog->num_samplers > 0) {
            /* Sampler-driven binding: for each sampler, bind its tex_unit's texture
             * to the sampler's shader_binding slot per stage.
             * When sampler exists in both VS and FS (vs_shader_binding >= 0),
             * bind to each stage at its own binding slot independently. */
            for (int s = 0; s < prog->num_samplers; s++) {
                if (!prog->samplers[s].used) continue;
                int tu = prog->samplers[s].tex_unit;
                if (tu < 0 || tu >= (int)SGL_MAX_TEXTURE_UNITS) continue;
                /* Use sampler type to select correct binding (2D vs cubemap) */
                GLuint tex_id;
                bool is_cubemap_sampler = (prog->samplers[s].gl_type == GL_SAMPLER_CUBE);
                if (is_cubemap_sampler)
                    tex_id = ctx->bound_cubemap_textures[tu];
                else
                    tex_id = ctx->bound_textures[tu];
                if (tex_id == 0) {
                    /* No texture bound: bind black fallback per GLES2 §3.7.10. */
                    sgl_handle_t fallback = is_cubemap_sampler ? 1 : 0;
                    int fs_binding = prog->samplers[s].shader_binding;
                    int vs_binding = prog->samplers[s].vs_shader_binding;
                    if (fs_binding < 0 || fs_binding >= 16) fs_binding = 0;
                    if (vs_binding >= 16) vs_binding = -1;
                    if (vs_binding >= 0) {
                        ctx->backend->ops->bind_texture(ctx->backend, (GLuint)fs_binding, fallback, 1);
                        ctx->backend->ops->bind_texture(ctx->backend, (GLuint)vs_binding, fallback, 0);
                    } else {
                        ctx->backend->ops->bind_texture(ctx->backend, (GLuint)fs_binding, fallback, -1);
                    }
                    continue;
                }
                sgl_texture_t *tex = GET_TEXTURE(tex_id);
                if (!tex || !tex->used) continue;
                /* GLES2 §3.7.10: incomplete textures sample as black fallback */
                if (!sgl_is_texture_complete(tex)) {
                    sgl_handle_t fallback = is_cubemap_sampler ? 1 : 0;
                    int fs_binding = prog->samplers[s].shader_binding;
                    int vs_binding = prog->samplers[s].vs_shader_binding;
                    if (fs_binding < 0 || fs_binding >= 16) fs_binding = 0;
                    if (vs_binding >= 16) vs_binding = -1;
                    if (vs_binding >= 0) {
                        ctx->backend->ops->bind_texture(ctx->backend, (GLuint)fs_binding, fallback, 1);
                        ctx->backend->ops->bind_texture(ctx->backend, (GLuint)vs_binding, fallback, 0);
                    } else {
                        ctx->backend->ops->bind_texture(ctx->backend, (GLuint)fs_binding, fallback, -1);
                    }
                    continue;
                }
                /* Pass texture params to backend for sampler creation */
                GLenum target = tex->target ? tex->target : GL_TEXTURE_2D;
                if (ctx->backend->ops->texture_parameter) {
                    ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_MIN_FILTER, tex->min_filter);
                    ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_MAG_FILTER, tex->mag_filter);
                    ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_WRAP_S, tex->wrap_s);
                    ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_WRAP_T, tex->wrap_t);
                }
                int fs_binding = prog->samplers[s].shader_binding;
                int vs_binding = prog->samplers[s].vs_shader_binding;
                /* Guard: validate binding indices to prevent GPU crash from
                 * invalid descriptor access (max 16 per stage on Tegra X1) */
                if (fs_binding < 0 || fs_binding >= 16) fs_binding = 0;
                if (vs_binding >= 16) vs_binding = -1;
                if (vs_binding >= 0) {
                    /* Sampler in both stages: bind to each stage at its own binding */
                    ctx->backend->ops->bind_texture(ctx->backend, (GLuint)fs_binding, tex_id, 1); /* FS */
                    ctx->backend->ops->bind_texture(ctx->backend, (GLuint)vs_binding, tex_id, 0); /* VS */
                } else {
                    /* Sampler in one stage only: bind to both stages at shader_binding */
                    ctx->backend->ops->bind_texture(ctx->backend, (GLuint)fs_binding, tex_id, -1);
                }
            }
        } else {
            /* No sampler info (precompiled shaders): bind by unit index.
             * Check both 2D and cubemap bindings per unit. */
            for (GLuint unit = 0; unit < SGL_MAX_TEXTURE_UNITS; unit++) {
                GLuint tex_id = ctx->bound_textures[unit];
                if (tex_id == 0) tex_id = ctx->bound_cubemap_textures[unit];
                if (tex_id > 0) {
                    sgl_texture_t *tex = GET_TEXTURE(tex_id);
                    if (tex && tex->used) {
                        /* GLES2 §3.7.10: incomplete textures sample as black fallback */
                        if (!sgl_is_texture_complete(tex)) {
                            bool is_cube = (tex->target == GL_TEXTURE_CUBE_MAP);
                            sgl_handle_t fallback = is_cube ? 1 : 0;
                            ctx->backend->ops->bind_texture(ctx->backend, unit, fallback, -1);
                            continue;
                        }
                        GLenum target = tex->target ? tex->target : GL_TEXTURE_2D;
                        if (ctx->backend->ops->texture_parameter) {
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_MIN_FILTER, tex->min_filter);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_MAG_FILTER, tex->mag_filter);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_WRAP_S, tex->wrap_s);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target, GL_TEXTURE_WRAP_T, tex->wrap_t);
                        }
                        ctx->backend->ops->bind_texture(ctx->backend, unit, tex_id, -1);
                    }
                }
            }
        }
    }

}

GL_APICALL void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    sgl_ensure_frame_ready();
    GET_CTX();
    CHECK_BACKEND();

    /* Validate mode FIRST — dEQP expects GL_INVALID_ENUM before any other error */
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

    if (count < 0 || first < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Check incomplete framebuffer BEFORE program check — dEQP
     * draw_arrays_invalid_program still expects INVALID_FRAMEBUFFER_OPERATION */
    if (ctx->bound_framebuffer != 0) {
        sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
        if (!fbo || !fbo->color_attachment) {
            sgl_set_error(ctx, GL_INVALID_FRAMEBUFFER_OPERATION);
            return;
        }
    }

    if (count == 0) return;

    /* No program bound or program not linked */
    if (ctx->current_program == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    {
        sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
        if (!prog || !prog->linked) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
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
                attr->buffer_data_size = buf->size;
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
    sgl_ensure_frame_ready();
    GET_CTX();
    CHECK_BACKEND();

    /* Validate mode FIRST — dEQP expects GL_INVALID_ENUM before any other error */
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

    if (count < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Check incomplete framebuffer */
    if (ctx->bound_framebuffer != 0) {
        sgl_framebuffer_t *fbo = GET_FRAMEBUFFER(ctx->bound_framebuffer);
        if (!fbo || !fbo->color_attachment) {
            sgl_set_error(ctx, GL_INVALID_FRAMEBUFFER_OPERATION);
            return;
        }
    }

    if (count == 0) return;

    /* No program bound or program not linked */
    if (ctx->current_program == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    {
        sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
        if (!prog || !prog->linked) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
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
                attr->buffer_data_size = buf->size;
            }
        }
    }

    /* Compute actual vertex count needed for client-side array allocation.
     * For glDrawElements, 'count' is the number of INDICES, not vertices.
     * When using client-side vertex arrays, we need max_vertex_index + 1
     * to avoid reading past the end of the vertex arrays.
     * This applies for BOTH client-side indices AND EBO-bound indices,
     * because vertex attributes may still be client pointers. */
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
    } else if (ctx->bound_element_buffer > 0 && ctx->backend->ops->get_data_cpu_ptr) {
        /* EBO-bound indices: scan EBO data for max vertex index.
         * Needed when vertex attributes are client pointers (not VBOs) —
         * the backend must stage enough vertex data for max_index+1 vertices. */
        sgl_buffer_t *ebo_buf = GET_BUFFER(ctx->bound_element_buffer);
        if (ebo_buf) {
            uint32_t ebo_byte_offset = ebo_buf->data_offset + (uint32_t)(uintptr_t)indices;
            const uint8_t *ebo_data = (const uint8_t *)ctx->backend->ops->get_data_cpu_ptr(
                ctx->backend, ebo_byte_offset);
            GLuint max_idx = 0;
            if (type == GL_UNSIGNED_BYTE) {
                for (GLsizei i = 0; i < count; i++) {
                    if (ebo_data[i] > max_idx) max_idx = ebo_data[i];
                }
            } else if (type == GL_UNSIGNED_SHORT) {
                const GLushort *idx16 = (const GLushort *)ebo_data;
                for (GLsizei i = 0; i < count; i++) {
                    if (idx16[i] > max_idx) max_idx = idx16[i];
                }
            } else if (type == GL_UNSIGNED_INT) {
                const GLuint *idx32 = (const GLuint *)ebo_data;
                for (GLsizei i = 0; i < count; i++) {
                    if (idx32[i] > max_idx) max_idx = idx32[i];
                }
            }
            vertex_count = (GLsizei)(max_idx + 1);
            SGL_TRACE_DRAW("EBO_SCAN ebo=%u off=%u count=%d max_idx=%u vtx_count=%d",
                   ctx->bound_element_buffer, (uint32_t)(uintptr_t)indices, count, max_idx, vertex_count);
        }
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
