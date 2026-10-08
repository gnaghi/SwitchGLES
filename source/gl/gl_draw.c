/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Draw Functions
 *
 * IMPORTANT: This file must NOT include deko3d.h or use any dk*() calls!
 * All GPU operations go through ctx->backend->ops->xxx()
 */

#include "gl_common.h"
#include "../util/sgl_perf.h"
#include <string.h>
#include <stdio.h>

/* GLES2 §3.7.10 texture completeness check.
 * Returns false (incomplete) if:
 * - NPOT dimensions with REPEAT or MIRRORED_REPEAT wrap mode
 * - NPOT dimensions with mipmap min filter
 * - Cubemap face dimension mismatch (cubemap_incomplete flag) */
static bool sgl_is_texture_complete(const sgl_texture_t *tex) {
    if (!tex || !tex->used)
        return false;
    if (tex->cubemap_incomplete)
        return false;

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
    if (!ctx->backend || !ctx->backend->ops)
        return;
    SGL_PERF_BEGIN(perf_state);

    /* Apply viewport - MUST be set before drawing */
    if (ctx->backend->ops->apply_viewport) {
        sgl_viewport_state_t vs = {
            ctx->viewport_state.viewport_x,     ctx->viewport_state.viewport_y,
            ctx->viewport_state.viewport_width, ctx->viewport_state.viewport_height,
            ctx->viewport_state.depth_near,     ctx->viewport_state.depth_far};
        ctx->backend->ops->apply_viewport(ctx->backend, &vs);
    }

    /* Apply combined depth-stencil state (avoids overwrite issues) */
    if (ctx->backend->ops->apply_depth_stencil) {
        sgl_depth_stencil_state_t dss;
        sgl_build_depth_stencil(ctx, &dss);
        ctx->backend->ops->apply_depth_stencil(ctx->backend, &dss);
    } else {
        /* Fallback to separate calls if combined not available */
        if (ctx->backend->ops->apply_depth) {
            sgl_depth_state_t ds = {ctx->depth_state.depth_test_enabled,
                                    ctx->depth_state.depth_write_enabled,
                                    ctx->depth_state.depth_func, ctx->depth_state.clear_depth};
            ctx->backend->ops->apply_depth(ctx->backend, &ds);
        }
    }

    /* Apply blend state */
    if (ctx->backend->ops->apply_blend) {
        sgl_blend_state_t bs;
        sgl_build_blend(ctx, &bs);
        ctx->backend->ops->apply_blend(ctx->backend, &bs);
    }

    /* Apply raster state (culling + polygon offset) */
    if (ctx->backend->ops->apply_raster) {
        sgl_raster_state_t rs;
        sgl_build_raster(ctx, &rs);
        ctx->backend->ops->apply_raster(ctx->backend, &rs);
    }

    /* Apply color mask */
    if (ctx->backend->ops->apply_color_mask) {
        sgl_color_state_t cs;
        sgl_build_color(ctx, &cs);
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
            if (near_val < 0.0f)
                near_val = 0.0f;
            if (near_val > 1.0f)
                near_val = 1.0f;
            if (far_val < 0.0f)
                far_val = 0.0f;
            if (far_val > 1.0f)
                far_val = 1.0f;
            float diff_val = far_val - near_val;
            float dr_vals[3] = {near_val, far_val, diff_val};
            /* Write to primary locations (VS or transpiler), then to the FS
             * mirror locations (when both VS+FS use gl_DepthRange). The block
             * is only marked dirty when a value actually changes, so an
             * unchanged depth range does not force a push at every draw. */
            for (int m = 0; m < 2; m++) {
                const GLint *locs = m ? prog->depth_range_loc_fs : prog->depth_range_loc;
                for (int d = 0; d < 3; d++) {
                    GLint loc = locs[d];
                    if (!loc)
                        continue;
                    int stage = (loc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
                    int offset = loc & SGL_LOC_OFFSET_MASK;
                    sgl_packed_ubo_t *packed =
                        (stage == 0) ? &prog->packed_vertex[0] : &prog->packed_fragment[0];
                    if (packed->valid && (uint32_t)offset + 4 <= packed->size &&
                        memcmp(packed->data + offset, &dr_vals[d], sizeof(float)) != 0) {
                        memcpy(packed->data + offset, &dr_vals[d], sizeof(float));
                        packed->dirty = true;
                    }
                }
            }
        }
    }

    SGL_PERF_END(SGL_PERF_STATE, perf_state);

    /* Bind program with shaders FIRST (textures must be bound AFTER shaders in deko3d) */
    SGL_PERF_BEGIN(perf_prog);
    if (ctx->current_program > 0) {
        sgl_bind_program_for_draw(ctx, ctx->current_program);
    }
    SGL_PERF_END(SGL_PERF_PROGRAM, perf_prog);
    SGL_PERF_BEGIN(perf_tex);

    /* Bind all active texture units AFTER program (deko3d requires bindTextures after bindShaders).
     * Use per-program sampler remap: for each sampler, bind the texture from its tex_unit
     * to the sampler's shader binding slot. This allows multiple samplers to share
     * the same tex_unit (e.g., initial value 0 per GLES2 spec) and each gets its binding. */
    if (ctx->backend->ops->bind_texture) {
        sgl_program_t *prog = (ctx->current_program > 0) ? GET_PROGRAM(ctx->current_program) : NULL;

        if (prog && prog->num_samplers > 0) {
            /* Sampler-driven binding: for each sampler, the texture of its
             * tex_unit goes to the sampler's binding slot of each stage that
             * declares the sampler (stage_mask from link-time reflection).
             * The slots are collected per stage and bound in contiguous runs
             * with one command each (bind_textures), instead of one command
             * per texture and per stage. A stage without samplers is not
             * touched: deko3d binds textures per stage, and a VS that samples
             * nothing never reads its slots. */
            sgl_handle_t stage_handles[2][SGL_MAX_TEXTURE_UNITS];
            uint32_t stage_mask_bound[2] = {0, 0};

            for (int s = 0; s < prog->num_samplers; s++) {
                if (!prog->samplers[s].used)
                    continue;
                int tu = prog->samplers[s].tex_unit;
                if (tu < 0 || tu >= (int)SGL_MAX_TEXTURE_UNITS)
                    continue;
                /* Use sampler type to select correct binding (2D vs cubemap) */
                GLuint tex_id;
                bool is_cubemap_sampler = (prog->samplers[s].gl_type == GL_SAMPLER_CUBE);
                if (is_cubemap_sampler)
                    tex_id = ctx->bound_cubemap_textures[tu];
                else
                    tex_id = ctx->bound_textures[tu];

                sgl_handle_t handle;
                if (tex_id == 0) {
                    /* No texture bound: bind black fallback per GLES2 §3.7.10. */
                    handle = is_cubemap_sampler ? 1 : 0;
                } else {
                    sgl_texture_t *tex = GET_TEXTURE(tex_id);
                    if (!tex || !tex->used)
                        continue;
                    if (!sgl_is_texture_complete(tex)) {
                        /* GLES2 §3.7.10: incomplete textures sample as black fallback */
                        handle = is_cubemap_sampler ? 1 : 0;
                    } else {
                        /* Pass texture params to backend for sampler creation */
                        GLenum target = tex->target ? tex->target : GL_TEXTURE_2D;
                        if (ctx->backend->ops->texture_parameter) {
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_MIN_FILTER,
                                                                 tex->min_filter);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_MAG_FILTER,
                                                                 tex->mag_filter);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_WRAP_S, tex->wrap_s);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_WRAP_T, tex->wrap_t);
                        }
                        handle = tex_id;
                    }
                }

                int fs_binding = prog->samplers[s].shader_binding;
                int vs_binding = prog->samplers[s].vs_shader_binding;
                /* Guard: validate binding indices to prevent GPU crash from
                 * invalid descriptor access (max 16 per stage on Tegra X1) */
                if (fs_binding < 0 || fs_binding >= (int)SGL_MAX_TEXTURE_UNITS)
                    fs_binding = 0;
                if (vs_binding >= (int)SGL_MAX_TEXTURE_UNITS)
                    vs_binding = -1;
                /* vs_shader_binding is only set when the VS declares the
                 * sampler; a VS-only sampler stores its binding in both
                 * fields. Without reflection (stage_mask 0) bind both
                 * stages, as before. */
                unsigned stages = prog->samplers[s].stage_mask;
                if (stages == 0)
                    stages = SGL_SAMPLER_STAGE_VS | SGL_SAMPLER_STAGE_FS;
                if (stages & SGL_SAMPLER_STAGE_FS) {
                    stage_handles[1][fs_binding] = handle;
                    stage_mask_bound[1] |= 1u << fs_binding;
                }
                if (stages & SGL_SAMPLER_STAGE_VS) {
                    int b = (vs_binding >= 0) ? vs_binding : fs_binding;
                    stage_handles[0][b] = handle;
                    stage_mask_bound[0] |= 1u << b;
                }
            }

            for (int st = 0; st < 2; st++) {
                uint32_t mask = stage_mask_bound[st];
                while (mask) {
                    int first = __builtin_ctz(mask);
                    int last = first;
                    while (last + 1 < (int)SGL_MAX_TEXTURE_UNITS && (mask & (1u << (last + 1))))
                        last++;
                    if (ctx->backend->ops->bind_textures) {
                        ctx->backend->ops->bind_textures(ctx->backend, st, (GLuint)first,
                                                         &stage_handles[st][first],
                                                         last - first + 1);
                    } else {
                        for (int b = first; b <= last; b++)
                            ctx->backend->ops->bind_texture(ctx->backend, (GLuint)b,
                                                            stage_handles[st][b], st);
                    }
                    mask &= ~(((1u << (last - first + 1)) - 1u) << first);
                }
            }
        } else {
            /* No sampler info (precompiled shaders): bind by unit index.
             * Check both 2D and cubemap bindings per unit. */
            for (GLuint unit = 0; unit < SGL_MAX_TEXTURE_UNITS; unit++) {
                GLuint tex_id = ctx->bound_textures[unit];
                if (tex_id == 0)
                    tex_id = ctx->bound_cubemap_textures[unit];
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
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_MIN_FILTER,
                                                                 tex->min_filter);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_MAG_FILTER,
                                                                 tex->mag_filter);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_WRAP_S, tex->wrap_s);
                            ctx->backend->ops->texture_parameter(ctx->backend, tex_id, target,
                                                                 GL_TEXTURE_WRAP_T, tex->wrap_t);
                        }
                        ctx->backend->ops->bind_texture(ctx->backend, unit, tex_id, -1);
                    }
                }
            }
        }
    }
    SGL_PERF_END(SGL_PERF_TEXTURES, perf_tex);
}

GL_APICALL void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    sgl_ensure_frame_ready();
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
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

    if (count == 0)
        return;

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
    SGL_PERF_BEGIN(perf_draw);
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
    SGL_PERF_BEGIN(perf_attr);
    if (ctx->backend->ops->bind_vertex_attribs) {
        ctx->backend->ops->bind_vertex_attribs(ctx->backend, prepared_attribs, SGL_MAX_ATTRIBS,
                                               first, count);
    }
    SGL_PERF_END(SGL_PERF_ATTRIBS, perf_attr);

    /* Draw via backend */
    SGL_PERF_BEGIN(perf_bk);
    if (ctx->backend->ops->draw_arrays) {
        ctx->backend->ops->draw_arrays(ctx->backend, mode, first, count);
    }
    SGL_PERF_END(SGL_PERF_BACKEND_DRAW, perf_bk);
    SGL_PERF_END(SGL_PERF_DRAW, perf_draw);

    SGL_TRACE_DRAW("glDrawArrays(mode=0x%X, first=%d, count=%d)", mode, first, count);
}

GL_APICALL void GL_APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type,
                                           const void *indices) {
    sgl_ensure_frame_ready();
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx)
        return;
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

    if (count == 0)
        return;

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
    SGL_PERF_BEGIN(perf_draw);
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
    GLsizei vertex_count = count; /* Default: use index count (safe for VBOs) */
    SGL_PERF_BEGIN(perf_scan);

    /* The backend only uses the vertex count for attributes it has to copy or
     * convert on the CPU (client arrays, GL_FIXED) and for a VBO pointer past the
     * end of its buffer; a regular VBO attribute is bound with the full buffer
     * extent. Skip the max-index scan otherwise: reading every index back from
     * uncached GPU memory cost 10-70 ms per frame in GFXBench T-Rex/Egypt. */
    bool need_vertex_count = false;
    for (int i = 0; i < SGL_MAX_ATTRIBS && !need_vertex_count; i++) {
        const sgl_vertex_attrib_t *attr = &prepared_attribs[i];
        if (!attr->enabled)
            continue;
        if ((attr->buffer == 0 && attr->pointer != NULL) || attr->type == GL_FIXED ||
            (attr->buffer > 0 && attr->buffer_data_size <= (uint32_t)(uintptr_t)attr->pointer))
            need_vertex_count = true;
    }

    if (!need_vertex_count) {
        /* VBO-only draw: vertex_count is not used */
    } else if (ctx->bound_element_buffer == 0 && indices != NULL) {
        /* Client-side indices: scan for max vertex index */
        GLuint max_idx = 0;
        if (type == GL_UNSIGNED_BYTE) {
            const GLubyte *idx8 = (const GLubyte *)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (idx8[i] > max_idx)
                    max_idx = idx8[i];
            }
        } else if (type == GL_UNSIGNED_SHORT) {
            const GLushort *idx16 = (const GLushort *)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (idx16[i] > max_idx)
                    max_idx = idx16[i];
            }
        } else if (type == GL_UNSIGNED_INT) {
            const GLuint *idx32 = (const GLuint *)indices;
            for (GLsizei i = 0; i < count; i++) {
                if (idx32[i] > max_idx)
                    max_idx = idx32[i];
            }
        }
        /* Guard against max_idx+1 wrapping to 0 (would under-allocate) */
        if (max_idx < (GLuint)0x7FFFFFFF) {
            vertex_count = (GLsizei)(max_idx + 1);
        }
    } else if (ctx->bound_element_buffer > 0 && ctx->backend->ops->get_data_cpu_ptr) {
        /* EBO-bound indices: scan EBO data for max vertex index.
         * Needed when vertex attributes are client pointers (not VBOs) —
         * the backend must stage enough vertex data for max_index+1 vertices. */
        sgl_buffer_t *ebo_buf = GET_BUFFER(ctx->bound_element_buffer);
        if (ebo_buf) {
            uint32_t ebo_byte_offset = ebo_buf->data_offset + (uint32_t)(uintptr_t)indices;
            const uint8_t *ebo_data =
                (const uint8_t *)ctx->backend->ops->get_data_cpu_ptr(ctx->backend, ebo_byte_offset);
            /* get_data_cpu_ptr returns NULL for an out-of-range offset — do
             * not dereference it (would crash the scan loop below). */
            if (ebo_data) {
                GLuint max_idx = 0;
                if (type == GL_UNSIGNED_BYTE) {
                    for (GLsizei i = 0; i < count; i++) {
                        if (ebo_data[i] > max_idx)
                            max_idx = ebo_data[i];
                    }
                } else if (type == GL_UNSIGNED_SHORT) {
                    const GLushort *idx16 = (const GLushort *)ebo_data;
                    for (GLsizei i = 0; i < count; i++) {
                        if (idx16[i] > max_idx)
                            max_idx = idx16[i];
                    }
                } else if (type == GL_UNSIGNED_INT) {
                    const GLuint *idx32 = (const GLuint *)ebo_data;
                    for (GLsizei i = 0; i < count; i++) {
                        if (idx32[i] > max_idx)
                            max_idx = idx32[i];
                    }
                }
                /* Guard against max_idx+1 wrapping to 0 (would under-allocate) */
                if (max_idx < (GLuint)0x7FFFFFFF) {
                    vertex_count = (GLsizei)(max_idx + 1);
                }
                SGL_TRACE_DRAW("EBO_SCAN ebo=%u off=%u count=%d max_idx=%u vtx_count=%d",
                               ctx->bound_element_buffer, (uint32_t)(uintptr_t)indices, count,
                               max_idx, vertex_count);
            }
        }
    }

    SGL_PERF_END(SGL_PERF_INDEX_SCAN, perf_scan);

    /* Bind vertex attributes via backend */
    SGL_PERF_BEGIN(perf_attr);
    if (ctx->backend->ops->bind_vertex_attribs) {
        ctx->backend->ops->bind_vertex_attribs(ctx->backend, prepared_attribs, SGL_MAX_ATTRIBS, 0,
                                               vertex_count);
    }
    SGL_PERF_END(SGL_PERF_ATTRIBS, perf_attr);

    /* Compute index buffer offset if EBO is bound */
    uint32_t ebo_data_offset = 0;
    if (ctx->bound_element_buffer > 0) {
        sgl_buffer_t *ebo_buf = GET_BUFFER(ctx->bound_element_buffer);
        if (ebo_buf) {
            /* indices is an offset into the bound EBO */
            ebo_data_offset = ebo_buf->data_offset + (uint32_t)(uintptr_t)indices;
        }
    }

    /* Draw elements via backend - pass ebo_data_offset, backend will copy client indices if ebo=0
     */
    SGL_PERF_BEGIN(perf_bk);
    if (ctx->backend->ops->draw_elements) {
        ctx->backend->ops->draw_elements(ctx->backend, mode, count, type, indices, ebo_data_offset);
    }
    SGL_PERF_END(SGL_PERF_BACKEND_DRAW, perf_bk);
    SGL_PERF_ADD(SGL_PERF_INDICES, count);
    SGL_PERF_END(SGL_PERF_DRAW, perf_draw);

    SGL_TRACE_DRAW("glDrawElements(mode=0x%X, count=%d, type=0x%X)", mode, count, type);
}
