/*
 * SwitchGLES - transpiler internals shared between glsl_transpiler.c (parse +
 * transpile) and glsl_validate.c (GLES 1.00 validators).
 */

#ifndef SGL_GLSL_TRANSPILER_INTERNAL_H
#define SGL_GLSL_TRANSPILER_INTERNAL_H

#include "glsl_transpiler.h"

/* Completed in glsl_transpiler.c; opaque (pointer-only) to other units. */
typedef struct glslt_ctx glslt_ctx_t;
typedef struct struct_def struct_def_t;

/* Lexing + struct helpers shared with the validators (defined in glsl_transpiler.c). */
int is_ident_char(char c);
int starts_with_word(const char *str, const char *word);
const struct_def_t *find_struct_def(glslt_ctx_t *ctx, const char *name);

/* GLES 1.00 compile-time validators (defined in glsl_validate.c, driven by
 * glslt_validate_es100_impl in glsl_transpiler.c). */
int validate_gles_semantics(const char *source, glslt_stage_t stage, char *error, int error_size);
int validate_const_initializers(glslt_ctx_t *ctx, const char *source, char *error, int error_size);
int validate_preprocessor_directives(const char *source, char *error, int error_size);
int validate_preprocessor_undefined(const char *source, char *error, int error_size);
int validate_qualification_order(const char *source, char *error, int error_size);
int validate_texture_functions(const char *source, glslt_stage_t stage, char *error, int error_size);

#endif /* SGL_GLSL_TRANSPILER_INTERNAL_H */
