/*
 * glsl_transpiler.h - GLSL ES 1.00 to GLSL 4.60 Core Profile Transpiler
 *
 * A lightweight, standalone C library for converting OpenGL ES 2.0 shaders
 * to modern OpenGL 4.60 core profile with std140 UBO layout and full
 * reflection output.
 *
 * No dependencies beyond standard C library (stdlib.h, string.h, stdio.h, ctype.h).
 * Designed for easy integration into any project - just copy this .h and the .c file.
 *
 * Transformations performed:
 *   #version 100              -> #version 460
 *   precision mediump/highp   -> (removed)
 *   attribute type name       -> layout(location=N) in type name
 *   varying type name         -> layout(location=N) out type name  (vertex)
 *   varying type name         -> layout(location=N) in type name   (fragment)
 *   uniform type name         -> packed into layout(std140, binding=N) uniform Block { ... }
 *   uniform sampler2D name    -> layout(binding=N) uniform sampler2D name
 *   gl_FragColor              -> fragColor  (+ output declaration added)
 *   gl_FragData[N]            -> fragData_N (+ output declarations added)
 *   texture2D(...)            -> texture(...)
 *   texture2DProj(...)        -> textureProj(...)
 *   texture2DLod(...)         -> textureLod(...)
 *   texture2DProjLod(...)     -> textureProjLod(...)
 *   textureCube(...)          -> texture(...)
 *   textureCubeLod(...)       -> textureLod(...)
 *   #extension GL_OES_...     -> (removed, core in 4.60)
 *
 * Reflection output:
 *   - Attributes: name, type, assigned location
 *   - Varyings:   name, type, assigned location
 *   - Uniforms:   name, type, array size, UBO binding, std140 byte offset, byte size
 *   - Samplers:   name, type, assigned binding
 *
 * Usage example:
 *   glslt_options_t opts;
 *   glslt_options_init(&opts);
 *   glslt_set_attrib_location(&opts, "attr_Position", 0);
 *   glslt_set_attrib_location(&opts, "attr_TexCoord0", 1);
 *
 *   glslt_result_t r = glslt_transpile(es_source, GLSLT_VERTEX, &opts);
 *   if (r.success) {
 *       compile_shader(r.output);  // Feed to libuam, glslang, etc.
 *       for (int i = 0; i < r.num_uniforms; i++)
 *           printf("uniform %s: binding=%d offset=%d size=%d\n",
 *               r.uniforms[i].name, r.uniforms[i].binding,
 *               r.uniforms[i].offset, r.uniforms[i].size);
 *   }
 *   glslt_result_free(&r);
 *
 * Varying location consistency:
 *   Transpile VS first, then pass its varying locations to the FS options:
 *     for (int i = 0; i < vs_result.num_varyings; i++)
 *         glslt_set_varying_location(&fs_opts,
 *             vs_result.varyings[i].name, vs_result.varyings[i].location);
 *   Or rely on default alphabetical assignment (works if both shaders
 *   declare the same set of varyings).
 *
 * License: MIT
 */

#ifndef GLSL_TRANSPILER_H
#define GLSL_TRANSPILER_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  Constants                                                                  */
/* -------------------------------------------------------------------------- */

#define GLSLT_MAX_NAME          64
#define GLSLT_MAX_UNIFORMS      64
#define GLSLT_MAX_SAMPLERS      16
#define GLSLT_MAX_ATTRIBUTES    16
#define GLSLT_MAX_VARYINGS      32
#define GLSLT_MAX_BINDINGS      32

/* -------------------------------------------------------------------------- */
/*  Types                                                                      */
/* -------------------------------------------------------------------------- */

typedef enum {
    GLSLT_VERTEX   = 0,
    GLSLT_FRAGMENT = 1
} glslt_stage_t;

typedef enum {
    GLSLT_FLOAT = 0,
    GLSLT_VEC2,
    GLSLT_VEC3,
    GLSLT_VEC4,
    GLSLT_INT,
    GLSLT_IVEC2,
    GLSLT_IVEC3,
    GLSLT_IVEC4,
    GLSLT_BOOL,
    GLSLT_BVEC2,
    GLSLT_BVEC3,
    GLSLT_BVEC4,
    GLSLT_MAT2,
    GLSLT_MAT3,
    GLSLT_MAT4,
    GLSLT_SAMPLER2D,
    GLSLT_SAMPLERCUBE,
    GLSLT_TYPE_COUNT
} glslt_type_t;

/* Reflected uniform (non-sampler, packed into UBO) */
typedef struct {
    char          name[GLSLT_MAX_NAME];
    glslt_type_t  type;
    int           array_size;   /* 0 = scalar, >0 = array[N] */
    int           binding;      /* UBO binding number */
    int           offset;       /* byte offset within UBO (std140) */
    int           size;         /* byte size in UBO (std140) */
} glslt_uniform_t;

/* Reflected sampler */
typedef struct {
    char          name[GLSLT_MAX_NAME];
    glslt_type_t  type;         /* GLSLT_SAMPLER2D or GLSLT_SAMPLERCUBE */
    int           binding;      /* sampler binding number */
} glslt_sampler_t;

/* Reflected attribute (vertex shader input) */
typedef struct {
    char          name[GLSLT_MAX_NAME];
    glslt_type_t  type;
    int           location;
} glslt_attribute_t;

/* Reflected varying (VS output / FS input) */
typedef struct {
    char          name[GLSLT_MAX_NAME];
    glslt_type_t  type;
    int           location;
} glslt_varying_t;

/* -------------------------------------------------------------------------- */
/*  Options                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    int target_version;         /* GLSL version to emit (default: 460) */
    int ubo_binding;            /* binding number for the UBO (default: 0) */
    int sampler_binding_start;  /* first binding number for samplers (default: 0) */

    /* Explicit attribute location bindings (from glBindAttribLocation) */
    struct { char name[GLSLT_MAX_NAME]; int location; } attrib_locations[GLSLT_MAX_BINDINGS];
    int num_attrib_locations;

    /* Explicit varying location bindings (from VS transpilation result) */
    struct { char name[GLSLT_MAX_NAME]; int location; } varying_locations[GLSLT_MAX_BINDINGS];
    int num_varying_locations;
} glslt_options_t;

/* -------------------------------------------------------------------------- */
/*  Result                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    /* Transpiled source (caller must free with glslt_result_free) */
    char         *output;
    int           output_len;

    /* Reflection: non-sampler uniforms (packed into one UBO) */
    glslt_uniform_t  uniforms[GLSLT_MAX_UNIFORMS];
    int               num_uniforms;
    int               ubo_binding;      /* which binding the UBO was assigned */
    int               ubo_total_size;   /* total UBO size in bytes (std140, 16-aligned) */

    /* Reflection: samplers */
    glslt_sampler_t   samplers[GLSLT_MAX_SAMPLERS];
    int               num_samplers;

    /* Reflection: attributes (vertex shader only) */
    glslt_attribute_t attributes[GLSLT_MAX_ATTRIBUTES];
    int               num_attributes;

    /* Reflection: varyings */
    glslt_varying_t   varyings[GLSLT_MAX_VARYINGS];
    int               num_varyings;

    /* Status */
    int           success;      /* 1 = ok, 0 = error */
    char          error[256];   /* error message if !success */
} glslt_result_t;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                 */
/* -------------------------------------------------------------------------- */

/* Initialize options with defaults (version=460, bindings start at 0) */
void glslt_options_init(glslt_options_t *opts);

/* Register an attribute name -> location binding */
void glslt_set_attrib_location(glslt_options_t *opts, const char *name, int location);

/* Register a varying name -> location binding (for VS/FS consistency) */
void glslt_set_varying_location(glslt_options_t *opts, const char *name, int location);

/* Transpile GLSL ES 1.00 source to GLSL 4.60 core profile.
 * Returns result with transpiled source and reflection data.
 * If input is already #version 460, passes through unchanged. */
glslt_result_t glslt_transpile(const char *source, glslt_stage_t stage,
                               const glslt_options_t *opts);

/* Free the output buffer in a result */
void glslt_result_free(glslt_result_t *result);

/* -------------------------------------------------------------------------- */
/*  Utilities                                                                  */
/* -------------------------------------------------------------------------- */

/* Return the GLSL type name string for a type enum (e.g. "vec4") */
const char *glslt_type_name(glslt_type_t type);

/* Return the std140 byte size for a type (scalar, not array) */
int glslt_type_std140_size(glslt_type_t type);

/* Return the std140 alignment for a type */
int glslt_type_std140_align(glslt_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* GLSL_TRANSPILER_H */
