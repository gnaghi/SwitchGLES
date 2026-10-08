/*
 * SwitchGLES - shared internals between gl_shader.c and gl_program.c
 *
 * Type-conversion helpers used by both shader compilation (gl_shader.c) and
 * program linking (gl_program.c). Only meaningful with the runtime compiler.
 */

#ifndef SGL_GL_SHADER_INTERNAL_H
#define SGL_GL_SHADER_INTERNAL_H

#include "gl_common.h"

#ifdef SGL_ENABLE_RUNTIME_COMPILER
#include "../transpiler/glsl_transpiler.h"

/* Transpiler reflection type enum -> GL type enum (e.g. GLSLT_VEC4 -> GL_FLOAT_VEC4). */
GLenum glslt_to_gl_type(glslt_type_t type);

/* uam constbuf base type (+ vec/mat dims) -> GL type enum. */
GLenum uam_base_type_to_gl(uint8_t base_type, uint8_t vec_elems, uint8_t mat_cols);

/* Compile a GLSL 4.60 source into a shader's DKSH (used by compile and by the
 * VS-with-attrib-bindings recompile in glLinkProgram). */
bool sgl_compile_glsl460(sgl_context_t *ctx, GLuint shader_id, sgl_shader_t *sh,
                         const char *glsl_source);

/* Compile an ES 1.00 source directly through Mesa (used by compile, and by the
 * FS recompile in glLinkProgram that pins varyings to the VS slots: pass the
 * VS metadata as varying_slots, or NULL for a plain compile). */
bool sgl_compile_es100_mesa(sgl_context_t *ctx, GLuint shader_id, sgl_shader_t *sh,
                            const sgl_mesa_metadata_t *varying_slots);
#endif

#endif /* SGL_GL_SHADER_INTERNAL_H */
