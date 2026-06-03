/*
 * glsl_transpiler.c - GLSL ES 1.00 to GLSL 4.60 Core Profile Transpiler
 *
 * Implementation. See glsl_transpiler.h for API documentation.
 *
 * License: MIT
 */

#include "glsl_transpiler.h"
#include "glsl_transpiler_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* ========================================================================== */
/*  Internal constants                                                         */
/* ========================================================================== */

#define MAX_LINE_LEN 2048

/* ========================================================================== */
/*  String buffer (growable output)                                            */
/* ========================================================================== */

typedef struct {
    char *buf;
    int   len;
    int   cap;
    int   failed;   /* set on allocation failure; appends become no-ops */
} strbuf_t;

static void sb_init(strbuf_t *sb) {
    sb->cap = 4096;
    sb->buf = (char *)malloc(sb->cap);
    sb->len = 0;
    sb->failed = 0;
    if (sb->buf) {
        sb->buf[0] = '\0';
    } else {
        sb->cap = 0;
        sb->failed = 1;
    }
}

static void sb_ensure(strbuf_t *sb, int extra) {
    if (sb->failed) return;
    while (sb->len + extra + 1 > sb->cap) {
        int new_cap = sb->cap * 2;
        char *new_buf = (char *)realloc(sb->buf, new_cap);
        if (!new_buf) {
            /* Keep the original buffer (still freed later) and stop growing. */
            sb->failed = 1;
            return;
        }
        sb->buf = new_buf;
        sb->cap = new_cap;
    }
}

static void sb_append(strbuf_t *sb, const char *str) {
    int slen = (int)strlen(str);
    sb_ensure(sb, slen);
    if (sb->failed) return;
    memcpy(sb->buf + sb->len, str, slen + 1);
    sb->len += slen;
}

static void sb_printf(strbuf_t *sb, const char *fmt, ...) {
    char tmp[MAX_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) sb_append(sb, tmp);
}

/* ========================================================================== */
/*  Type utilities                                                             */
/* ========================================================================== */

typedef struct {
    const char   *name;
    glslt_type_t  type;
    int           std140_size;
    int           std140_align;
    int           is_sampler;
} type_info_t;

static const type_info_t s_types[] = {
    { "float",       GLSLT_FLOAT,       4,  4,  0 },
    { "vec2",        GLSLT_VEC2,        8,  8,  0 },
    { "vec3",        GLSLT_VEC3,        12, 16, 0 },
    { "vec4",        GLSLT_VEC4,        16, 16, 0 },
    { "int",         GLSLT_INT,         4,  4,  0 },
    { "ivec2",       GLSLT_IVEC2,       8,  8,  0 },
    { "ivec3",       GLSLT_IVEC3,       12, 16, 0 },
    { "ivec4",       GLSLT_IVEC4,       16, 16, 0 },
    { "bool",        GLSLT_BOOL,        4,  4,  0 },
    { "bvec2",       GLSLT_BVEC2,       8,  8,  0 },
    { "bvec3",       GLSLT_BVEC3,       12, 16, 0 },
    { "bvec4",       GLSLT_BVEC4,       16, 16, 0 },
    { "mat2",        GLSLT_MAT2,        32, 16, 0 },
    { "mat3",        GLSLT_MAT3,        48, 16, 0 },
    { "mat4",        GLSLT_MAT4,        64, 16, 0 },
    { "sampler2D",   GLSLT_SAMPLER2D,   0,  0,  1 },
    { "samplerCube", GLSLT_SAMPLERCUBE, 0,  0,  1 },
    { NULL, 0, 0, 0, 0 }
};

/* ========================================================================== */
/*  Struct uniform support                                                     */
/* ========================================================================== */

#define MAX_STRUCT_FIELDS  16
#define MAX_STRUCT_DEFS    16
#define MAX_STRUCT_REPLS   128

typedef struct {
    char          name[GLSLT_MAX_NAME];
    char          type_name[GLSLT_MAX_NAME];  /* GLSL type or struct type name */
    glslt_type_t  type;                       /* GLSLT_TYPE_COUNT if struct */
    int           is_struct;
    int           array_size;  /* 0 = scalar, >0 = array[N] */
} struct_field_t;

struct struct_def {
    char           name[GLSLT_MAX_NAME];
    struct_field_t fields[MAX_STRUCT_FIELDS];
    int            num_fields;
};

typedef struct {
    char old_ref[GLSLT_MAX_NAME * 4];  /* e.g. "val.a" */
    char new_ref[GLSLT_MAX_NAME * 4];  /* e.g. "val_a" */
} struct_repl_t;

/* Preprocessor #define table — defined here (ahead of glslt_collect_defines)
 * so glslt_ctx_t can hold a pointer to it. */
#define GLSLT_MAX_DEFINES 64
typedef struct {
    char name[GLSLT_MAX_NAME];
    int  value;
} glslt_define_t;
typedef struct {
    glslt_define_t entries[GLSLT_MAX_DEFINES];
    int count;
} glslt_define_table_t;

/* Struct array uniforms — kept as whole structs in UBO (not flattened) */
#define MAX_STRUCT_ARRAY_UNIFORMS 8
typedef struct {
    char struct_type[GLSLT_MAX_NAME];
    char var_name[GLSLT_MAX_NAME];
    int  array_size;
    int  std140_size;  /* Total bytes in std140 layout */
} struct_array_uniform_t;

/* Transpiler context: all mutable per-transpilation state, bundled so it is
 * zeroed (calloc) and freed in exactly one place per call (see the public
 * glslt_transpile / glslt_validate_es100 wrappers). Previously these were
 * file-scope globals that leaked struct/replacement state between shaders when
 * a call bailed on an error path — the root cause of B12. Threading an explicit
 * context also makes the transpiler reentrant. */
struct glslt_ctx {
    struct_def_t            structs[MAX_STRUCT_DEFS];
    int                     num_structs;
    struct_repl_t           replacements[MAX_STRUCT_REPLS];
    int                     num_replacements;
    struct_array_uniform_t  struct_array_uniforms[MAX_STRUCT_ARRAY_UNIFORMS];
    int                     num_struct_array_uniforms;
    const glslt_define_table_t *defines;
};

/* Declarations scanned out of a shader by collect_declarations() and consumed
 * by emit_header() / the layout step. Bundling them keeps the phase helpers to
 * a couple of parameters instead of a dozen. */
typedef struct {
    glslt_uniform_t   uniforms[GLSLT_MAX_UNIFORMS];
    glslt_sampler_t   samplers[GLSLT_MAX_SAMPLERS];
    glslt_attribute_t attributes[GLSLT_MAX_ATTRIBUTES];
    glslt_varying_t   varyings[GLSLT_MAX_VARYINGS];
    int nu, ns, na, nv;
    int has_frag_color;
    int max_frag_data;   /* highest gl_FragData[N] index, -1 if none */
} glslt_decls_t;

static const type_info_t *find_type_info(const char *name) {
    for (const type_info_t *t = s_types; t->name; t++) {
        if (strcmp(t->name, name) == 0) return t;
    }
    return NULL;
}

static const type_info_t *find_type_info_by_enum(glslt_type_t type) {
    for (const type_info_t *t = s_types; t->name; t++) {
        if (t->type == type) return t;
    }
    return NULL;
}

const char *glslt_type_name(glslt_type_t type) {
    const type_info_t *t = find_type_info_by_enum(type);
    return t ? t->name : "unknown";
}

int glslt_type_std140_size(glslt_type_t type) {
    const type_info_t *t = find_type_info_by_enum(type);
    return t ? t->std140_size : 0;
}

int glslt_type_std140_align(glslt_type_t type) {
    const type_info_t *t = find_type_info_by_enum(type);
    return t ? t->std140_align : 0;
}

/* ========================================================================== */
/*  String helpers                                                             */
/* ========================================================================== */

int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *read_word(const char *p, char *out, int out_size) {
    const char *start = p;
    int i = 0;
    while (*p && is_ident_char(*p) && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (i > 0) ? p : start;
}

/* Check if string starts with a word (not part of a longer identifier) */
int starts_with_word(const char *str, const char *word) {
    int len = (int)strlen(word);
    if (strncmp(str, word, len) != 0) return 0;
    if (is_ident_char(str[len])) return 0;
    return 1;
}

/* Replace all occurrences of 'old' with 'new' respecting word boundaries */
static void replace_word(const char *input, const char *old_word,
                         const char *new_word, char *out, int out_size) {
    int old_len = (int)strlen(old_word);
    int new_len = (int)strlen(new_word);
    const char *p = input;
    char *o = out;
    char *end = out + out_size - 1;

    while (*p && o < end) {
        if (strncmp(p, old_word, old_len) == 0) {
            int left_ok = (p == input) || !is_ident_char(*(p - 1));
            int right_ok = !is_ident_char(*(p + old_len));
            if (left_ok && right_ok) {
                if (o + new_len >= end) break;
                memcpy(o, new_word, new_len);
                o += new_len;
                p += old_len;
                continue;
            }
        }
        *o++ = *p++;
    }
    *o = '\0';
}

/* qsort comparator for sorting by name */
static int cmp_by_name_uniform(const void *a, const void *b) {
    return strcmp(((const glslt_uniform_t *)a)->name,
                  ((const glslt_uniform_t *)b)->name);
}

static int cmp_by_name_varying(const void *a, const void *b) {
    return strcmp(((const glslt_varying_t *)a)->name,
                  ((const glslt_varying_t *)b)->name);
}

static int cmp_by_location_attr(const void *a, const void *b) {
    return ((const glslt_attribute_t *)a)->location -
           ((const glslt_attribute_t *)b)->location;
}

static int cmp_by_location_varying(const void *a, const void *b) {
    return ((const glslt_varying_t *)a)->location -
           ((const glslt_varying_t *)b)->location;
}

/* ========================================================================== */
/*  Struct uniform helper functions                                            */
/* ========================================================================== */

/* Find a struct definition by name */
const struct_def_t *find_struct_def(glslt_ctx_t *ctx, const char *name) {
    for (int i = 0; i < ctx->num_structs; i++) {
        if (strcmp(ctx->structs[i].name, name) == 0)
            return &ctx->structs[i];
    }
    return NULL;
}

/* Pre-scan source for struct definitions: struct Name { fields }; */
static void collect_struct_defs(glslt_ctx_t *ctx, const char *source) {
    ctx->num_structs = 0;
    const char *p = source;
    int in_block_comment = 0;

    while (*p) {
        /* Skip block comments */
        if (in_block_comment) {
            if (p[0] == '*' && p[1] == '/') { in_block_comment = 0; p += 2; continue; }
            p++; continue;
        }
        if (p[0] == '/' && p[1] == '*') { in_block_comment = 1; p += 2; continue; }
        /* Skip line comments */
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }

        /* Look for 'struct' keyword */
        if (starts_with_word(p, "struct")) {
            const char *sp = skip_ws(p + 6);
            char sname[GLSLT_MAX_NAME];
            const char *after_name = read_word(sp, sname, sizeof(sname));
            if (after_name == sp || !sname[0]) { p++; continue; }

            sp = skip_ws(after_name);
            if (*sp != '{') { p++; continue; }
            sp++; /* skip '{' */

            if (ctx->num_structs >= MAX_STRUCT_DEFS) { p++; continue; }
            struct_def_t *sd = &ctx->structs[ctx->num_structs];
            memset(sd, 0, sizeof(*sd));
            strncpy(sd->name, sname, GLSLT_MAX_NAME - 1);

            /* Parse fields until '}' */
            while (*sp && *sp != '}' && sd->num_fields < MAX_STRUCT_FIELDS) {
                sp = skip_ws(sp);
                if (*sp == '}' || !*sp) break;

                /* Skip precision qualifier */
                if (starts_with_word(sp, "lowp"))    sp = skip_ws(sp + 4);
                else if (starts_with_word(sp, "mediump")) sp = skip_ws(sp + 7);
                else if (starts_with_word(sp, "highp"))   sp = skip_ws(sp + 5);

                /* Read type name */
                char ftype[GLSLT_MAX_NAME];
                const char *after_ftype = read_word(sp, ftype, sizeof(ftype));
                if (after_ftype == sp) { sp++; continue; }
                sp = skip_ws(after_ftype);

                /* Look up type — if not in s_types, check if it's a known struct */
                const type_info_t *fti = find_type_info(ftype);

                /* Read comma-separated field names */
                while (*sp && *sp != ';' && *sp != '}' && sd->num_fields < MAX_STRUCT_FIELDS) {
                    char fname[GLSLT_MAX_NAME];
                    const char *after_fname = read_word(sp, fname, sizeof(fname));
                    if (after_fname == sp) break;

                    struct_field_t *sf = &sd->fields[sd->num_fields];
                    strncpy(sf->name, fname, GLSLT_MAX_NAME - 1);
                    strncpy(sf->type_name, ftype, GLSLT_MAX_NAME - 1);
                    if (fti) {
                        sf->type = fti->type;
                        sf->is_struct = 0;
                    } else {
                        sf->type = GLSLT_TYPE_COUNT; /* marker for struct type */
                        sf->is_struct = 1;
                    }
                    sf->array_size = 0;
                    sd->num_fields++;

                    sp = skip_ws(after_fname);
                    /* Parse array notation if present: [N] */
                    if (*sp == '[') {
                        sp++;
                        sf->array_size = atoi(sp);
                        while (*sp && *sp != ']') sp++;
                        if (*sp == ']') sp++;
                        sp = skip_ws(sp);
                    }
                    if (*sp == ',') { sp++; sp = skip_ws(sp); }
                }
                if (*sp == ';') sp++;
            }

            ctx->num_structs++;
            /* Advance past closing '}' and ';' */
            if (*sp == '}') sp++;
            sp = skip_ws(sp);
            if (*sp == ';') sp++;
            p = sp;
            continue;
        }
        p++;
    }
}

/* Recursively flatten struct uniform members into individual UBO entries.
 * flat_prefix = "val_sub" (underscore-joined for GLSL 4.60 identifier)
 * gles_prefix = "val.sub" (dot-joined for GLES API name)
 * struct_name = "Struct" → adds flat "val_sub_a", gles "val.sub.a" etc. */
static void flatten_struct_to_uniforms_ex(glslt_ctx_t *ctx, const char *flat_prefix, const char *gles_prefix,
                                           const char *struct_name,
                                           glslt_uniform_t *uniforms, int *nu,
                                           glslt_sampler_t *samplers, int *ns) {
    const struct_def_t *sd = find_struct_def(ctx, struct_name);
    if (!sd) return;

    for (int i = 0; i < sd->num_fields; i++) {
        const struct_field_t *sf = &sd->fields[i];
        char flat_name[GLSLT_MAX_NAME * 4];
        char gles_name[GLSLT_MAX_NAME * 4];
        snprintf(flat_name, sizeof(flat_name), "%s_%s", flat_prefix, sf->name);
        snprintf(gles_name, sizeof(gles_name), "%s.%s", gles_prefix, sf->name);

        if (sf->is_struct) {
            if (sf->array_size > 0) {
                /* Struct field that is an array of structs */
                for (int a = 0; a < sf->array_size; a++) {
                    char flat_arr[GLSLT_MAX_NAME * 4];
                    char gles_arr[GLSLT_MAX_NAME * 4];
                    snprintf(flat_arr, sizeof(flat_arr), "%s_%d", flat_name, a);
                    snprintf(gles_arr, sizeof(gles_arr), "%s[%d]", gles_name, a);
                    flatten_struct_to_uniforms_ex(ctx, flat_arr, gles_arr, sf->type_name,
                                                  uniforms, nu, samplers, ns);
                }
            } else {
                /* Single nested struct */
                flatten_struct_to_uniforms_ex(ctx, flat_name, gles_name, sf->type_name,
                                              uniforms, nu, samplers, ns);
            }
        } else if ((sf->type == GLSLT_SAMPLER2D || sf->type == GLSLT_SAMPLERCUBE) && samplers && ns) {
            /* Sampler field — extract from struct, don't put in UBO */
            if (sf->array_size > 0) {
                /* Sampler array inside struct: expand into individual entries */
                for (int a = 0; a < sf->array_size && *ns < GLSLT_MAX_SAMPLERS; a++) {
                    char s_flat[GLSLT_MAX_NAME * 4];
                    char s_gles[GLSLT_MAX_NAME * 4];
                    snprintf(s_flat, sizeof(s_flat), "%s_%d", flat_name, a);
                    snprintf(s_gles, sizeof(s_gles), "%s[%d]", gles_name, a);
                    strncpy(samplers[*ns].name, s_flat, GLSLT_MAX_NAME - 1);
                    samplers[*ns].name[GLSLT_MAX_NAME - 1] = '\0';
                    strncpy(samplers[*ns].gles_name, s_gles, GLSLT_MAX_NAME - 1);
                    samplers[*ns].gles_name[GLSLT_MAX_NAME - 1] = '\0';
                    samplers[*ns].type = sf->type;
                    samplers[*ns].binding = -1;
                    samplers[*ns].array_index = a;
                    samplers[*ns].array_total = sf->array_size;
                    (*ns)++;
                }
                /* Replacements for each array element: "u_s.tex[i]" → "u_s_tex_i" */
                for (int a = 0; a < sf->array_size && ctx->num_replacements < MAX_STRUCT_REPLS; a++) {
                    char old_ref[GLSLT_MAX_NAME * 4], new_ref[GLSLT_MAX_NAME * 4];
                    snprintf(old_ref, sizeof(old_ref), "%s[%d]", gles_name, a);
                    snprintf(new_ref, sizeof(new_ref), "%s_%d", flat_name, a);
                    strncpy(ctx->replacements[ctx->num_replacements].old_ref, old_ref,
                            sizeof(ctx->replacements[0].old_ref) - 1);
                    strncpy(ctx->replacements[ctx->num_replacements].new_ref, new_ref,
                            sizeof(ctx->replacements[0].new_ref) - 1);
                    ctx->num_replacements++;
                }
            } else {
                /* Single sampler inside struct */
                if (*ns < GLSLT_MAX_SAMPLERS) {
                    strncpy(samplers[*ns].name, flat_name, GLSLT_MAX_NAME - 1);
                    samplers[*ns].name[GLSLT_MAX_NAME - 1] = '\0';
                    strncpy(samplers[*ns].gles_name, gles_name, GLSLT_MAX_NAME - 1);
                    samplers[*ns].gles_name[GLSLT_MAX_NAME - 1] = '\0';
                    samplers[*ns].type = sf->type;
                    samplers[*ns].binding = -1;
                    samplers[*ns].array_index = -1;
                    samplers[*ns].array_total = 0;
                    (*ns)++;
                }
            }
            /* Add replacement for the sampler name: "u_s.tex" → "u_s_tex" */
            if (ctx->num_replacements < MAX_STRUCT_REPLS) {
                strncpy(ctx->replacements[ctx->num_replacements].old_ref, gles_name,
                        sizeof(ctx->replacements[0].old_ref) - 1);
                strncpy(ctx->replacements[ctx->num_replacements].new_ref, flat_name,
                        sizeof(ctx->replacements[0].new_ref) - 1);
                ctx->num_replacements++;
            }
        } else {
            /* Regular uniform field */
            if (*nu < GLSLT_MAX_UNIFORMS) {
                strncpy(uniforms[*nu].name, flat_name, GLSLT_MAX_NAME - 1);
                uniforms[*nu].name[GLSLT_MAX_NAME - 1] = '\0';
                strncpy(uniforms[*nu].gles_name, gles_name, GLSLT_MAX_NAME - 1);
                uniforms[*nu].gles_name[GLSLT_MAX_NAME - 1] = '\0';
                uniforms[*nu].type = sf->type;
                uniforms[*nu].array_size = sf->array_size;
                uniforms[*nu].binding = -1;
                uniforms[*nu].offset = 0;
                uniforms[*nu].size = 0;
                (*nu)++;
            }

            /* Add replacement: "gles_prefix.field" → "flat_prefix_field" */
            if (ctx->num_replacements < MAX_STRUCT_REPLS) {
                strncpy(ctx->replacements[ctx->num_replacements].old_ref, gles_name,
                        sizeof(ctx->replacements[0].old_ref) - 1);
                strncpy(ctx->replacements[ctx->num_replacements].new_ref, flat_name,
                        sizeof(ctx->replacements[0].new_ref) - 1);
                ctx->num_replacements++;
            }
        }
    }
}

/* Convenience wrapper: flatten with same prefix for both flat and gles names.
 * gles_prefix uses dot notation: "prefix.field" */
static void flatten_struct_to_uniforms(glslt_ctx_t *ctx, const char *prefix, const char *struct_name,
                                       glslt_uniform_t *uniforms, int *nu,
                                       glslt_sampler_t *samplers, int *ns) {
    flatten_struct_to_uniforms_ex(ctx, prefix, prefix, struct_name, uniforms, nu, samplers, ns);
}

/* Apply all struct member replacements to a line.
 * Handles "val.a" → "val_a" with left-side identifier boundary checks. */
static void apply_struct_replacements(glslt_ctx_t *ctx, const char *input, char *out, int out_size) {
    char b1[MAX_LINE_LEN], b2[MAX_LINE_LEN];
    const char *src = input;
    char *dst;

    for (int i = 0; i < ctx->num_replacements; i++) {
        dst = (i % 2 == 0) ? b1 : b2;
        int old_len = (int)strlen(ctx->replacements[i].old_ref);
        int new_len = (int)strlen(ctx->replacements[i].new_ref);
        const char *p = src;
        char *o = dst;
        char *end = dst + MAX_LINE_LEN - 1;

        while (*p && o < end) {
            if (strncmp(p, ctx->replacements[i].old_ref, old_len) == 0) {
                /* Check left boundary: must not be preceded by ident char */
                int left_ok = (p == src) || !is_ident_char(*(p - 1));
                /* Check right boundary: must not be followed by ident char
                 * (but '.' is OK — it means further member access like swizzle) */
                int right_ok = !is_ident_char(*(p + old_len));
                if (left_ok && right_ok) {
                    if (o + new_len >= end) break;
                    memcpy(o, ctx->replacements[i].new_ref, new_len);
                    o += new_len;
                    p += old_len;
                    continue;
                }
            }
            *o++ = *p++;
        }
        *o = '\0';
        src = dst;
    }

    if (ctx->num_replacements == 0) {
        strncpy(out, input, out_size);
        out[out_size - 1] = '\0';
    } else {
        strncpy(out, src, out_size);
        out[out_size - 1] = '\0';
    }
}

/* ========================================================================== */
/*  Simple #define resolution (for macro-based array sizes)                    */
/* ========================================================================== */

/* glslt_define_t / glslt_define_table_t are defined earlier (near glslt_ctx_t). */

/* Skip spaces and tabs only (NOT newlines) */
static const char *skip_hws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Pre-scan source for '#define NAME NUMERIC_VALUE' patterns */
static void glslt_collect_defines(const char *source, glslt_define_table_t *table) {
    table->count = 0;
    const char *p = source;
    while (*p) {
        /* Skip leading horizontal whitespace */
        p = skip_hws(p);
        if (strncmp(p, "#define", 7) == 0 && !is_ident_char(p[7])) {
            p = skip_hws(p + 7);
            char name[GLSLT_MAX_NAME];
            const char *after = read_word(p, name, sizeof(name));
            if (after != p && name[0]) {
                const char *vp = skip_hws(after);
                /* Check if value starts with a digit (or minus) */
                if ((*vp >= '0' && *vp <= '9') || (*vp == '-' && vp[1] >= '0' && vp[1] <= '9')) {
                    int val = atoi(vp);
                    if (table->count < GLSLT_MAX_DEFINES) {
                        strncpy(table->entries[table->count].name, name, GLSLT_MAX_NAME - 1);
                        table->entries[table->count].name[GLSLT_MAX_NAME - 1] = '\0';
                        table->entries[table->count].value = val;
                        table->count++;
                    }
                }
            }
        }
        /* Skip to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

/* Look up a define name, returns value or -1 if not found */
static int glslt_resolve_define(const glslt_define_table_t *table, const char *name) {
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->entries[i].name, name) == 0)
            return table->entries[i].value;
    }
    return -1;
}

/* Module-level pointer set during transpilation (avoids threading through all calls) */
/* ctx->defines moved into glslt_ctx_t (ctx->defines). */

/* ========================================================================== */
/*  Line extraction                                                            */
/* ========================================================================== */

/* Extract one line from source (up to \n or \0). Returns length copied. */
static int extract_line(const char *src, char *line, int line_size) {
    int i = 0;
    while (src[i] && src[i] != '\n' && src[i] != '\r' && i < line_size - 1) {
        line[i] = src[i];
        i++;
    }
    line[i] = '\0';
    return i;
}

/* Advance pointer past the current line (past \n or \r\n) */
static const char *next_line(const char *p) {
    while (*p && *p != '\n' && *p != '\r') p++;
    if (*p == '\r') p++;
    if (*p == '\n') p++;
    return p;
}

/* Strip single-line comment for parsing (does not modify original) */
static void strip_comment(const char *line, char *out, int out_size) {
    int i = 0;
    (void)0;
    while (line[i] && i < out_size - 1) {
        if (line[i] == '/' && line[i + 1] == '/') break;
        out[i] = line[i];
        i++;
    }
    out[i] = '\0';
    /* Trim trailing whitespace */
    while (i > 0 && isspace((unsigned char)out[i - 1])) out[--i] = '\0';
}

/* ========================================================================== */
/*  Declaration parsing                                                        */
/* ========================================================================== */

typedef enum {
    DECL_NONE = 0,
    DECL_VERSION,
    DECL_PRECISION,
    DECL_ATTRIBUTE,
    DECL_VARYING,
    DECL_UNIFORM,
    DECL_UNIFORM_STRUCT,
    DECL_EXTENSION
} decl_kind_t;

typedef struct {
    decl_kind_t   kind;
    glslt_type_t  type;
    int           is_sampler;
    char          names[8][GLSLT_MAX_NAME]; /* supports multiple names: uniform float a, b; */
    int           array_sizes[8];
    int           num_names;
    char          struct_type_name[GLSLT_MAX_NAME]; /* for DECL_UNIFORM_STRUCT */
} parsed_decl_t;

/* Skip optional precision qualifier (lowp, mediump, highp) */
static const char *skip_precision(const char *p) {
    p = skip_ws(p);
    if (starts_with_word(p, "lowp"))    return skip_ws(p + 4);
    if (starts_with_word(p, "mediump")) return skip_ws(p + 7);
    if (starts_with_word(p, "highp"))   return skip_ws(p + 5);
    return p;
}

/* Parse: type name[, name2, ...]; from current position */
static int parse_type_and_names(glslt_ctx_t *ctx, const char *p, parsed_decl_t *decl) {
    p = skip_precision(p);

    /* Read type */
    char type_str[64];
    const char *after_type = read_word(p, type_str, sizeof(type_str));
    if (after_type == p) return 0;

    const type_info_t *ti = find_type_info(type_str);
    if (!ti) {
        /* Unknown token before type — may be an unexpanded precision macro. Skip and retry. */
        p = skip_ws(after_type);
        after_type = read_word(p, type_str, sizeof(type_str));
        if (after_type == p) return 0;
        ti = find_type_info(type_str);
        if (!ti) return 0;
    }

    decl->type = ti->type;
    decl->is_sampler = ti->is_sampler;
    decl->num_names = 0;
    p = skip_ws(after_type);

    /* Read names (comma-separated) */
    while (*p && *p != ';' && decl->num_names < 8) {
        char name[GLSLT_MAX_NAME];
        const char *after_name = read_word(p, name, sizeof(name));
        if (after_name == p) break;

        strncpy(decl->names[decl->num_names], name, GLSLT_MAX_NAME - 1);
        decl->names[decl->num_names][GLSLT_MAX_NAME - 1] = '\0';
        decl->array_sizes[decl->num_names] = 0;

        p = skip_ws(after_name);

        /* Check for array: [N], [MACRO_NAME], or [expr] (e.g. [1 + 0]) */
        if (*p == '[') {
            p++;
            p = skip_ws(p);
            int arr_size = 0;
            if (*p >= '0' && *p <= '9') {
                while (*p >= '0' && *p <= '9') {
                    arr_size = arr_size * 10 + (*p - '0');
                    p++;
                }
            } else if (is_ident_char(*p)) {
                /* Macro name — try to resolve from #define table */
                char macro_name[GLSLT_MAX_NAME];
                const char *mp = read_word(p, macro_name, sizeof(macro_name));
                if (ctx->defines) {
                    int resolved = glslt_resolve_define(ctx->defines, macro_name);
                    if (resolved > 0) arr_size = resolved;
                }
                p = mp;
            }
            p = skip_ws(p);
            /* Handle arithmetic expressions: [N + M], [A - B], etc.
             * Evaluate simple integer addition/subtraction chains. */
            while (*p == '+' || *p == '-') {
                char op = *p++;
                p = skip_ws(p);
                int operand = 0;
                if (*p >= '0' && *p <= '9') {
                    while (*p >= '0' && *p <= '9') {
                        operand = operand * 10 + (*p - '0');
                        p++;
                    }
                } else if (is_ident_char(*p)) {
                    char macro_name[GLSLT_MAX_NAME];
                    const char *mp = read_word(p, macro_name, sizeof(macro_name));
                    if (ctx->defines) {
                        int resolved = glslt_resolve_define(ctx->defines, macro_name);
                        if (resolved > 0) operand = resolved;
                    }
                    p = mp;
                }
                if (op == '+') arr_size += operand;
                else           arr_size -= operand;
                p = skip_ws(p);
            }
            if (*p == ']') p++;
            decl->array_sizes[decl->num_names] = arr_size;
            p = skip_ws(p);
        }

        decl->num_names++;
        if (*p == ',') { p++; p = skip_ws(p); }
    }

    return decl->num_names > 0;
}

/* Parse a single line. Returns the declaration kind or DECL_NONE. */
static decl_kind_t parse_line(glslt_ctx_t *ctx, const char *raw_line, parsed_decl_t *decl) {
    char line[MAX_LINE_LEN];
    strip_comment(raw_line, line, sizeof(line));

    memset(decl, 0, sizeof(*decl));
    const char *p = skip_ws(line);

    if (*p == '\0') return DECL_NONE;

    /* #version */
    if (strncmp(p, "#version", 8) == 0) {
        decl->kind = DECL_VERSION;
        return DECL_VERSION;
    }

    /* #extension */
    if (strncmp(p, "#extension", 10) == 0) {
        decl->kind = DECL_EXTENSION;
        return DECL_EXTENSION;
    }

    /* Other preprocessor directives (#define, #if, #ifdef, #ifndef, #else,
     * #elif, #endif, #line, #pragma, etc.) — preserve in output as body code.
     * These are needed by libuam's preprocessor for conditional compilation
     * and constant definitions (e.g. #define M_PI, #if defined(USE_FOG)). */
    if (*p == '#') {
        return DECL_NONE;
    }

    /* precision */
    if (starts_with_word(p, "precision")) {
        decl->kind = DECL_PRECISION;
        return DECL_PRECISION;
    }

    /* attribute */
    if (starts_with_word(p, "attribute")) {
        p = skip_ws(p + 9);
        if (parse_type_and_names(ctx, p, decl)) {
            decl->kind = DECL_ATTRIBUTE;
            return DECL_ATTRIBUTE;
        }
    }

    /* varying or invariant varying */
    if (starts_with_word(p, "invariant")) {
        const char *ip = skip_ws(p + 9);
        if (starts_with_word(ip, "varying")) {
            p = skip_ws(ip + 7);
            if (parse_type_and_names(ctx, p, decl)) {
                decl->kind = DECL_VARYING;
                return DECL_VARYING;
            }
        }
    }
    if (starts_with_word(p, "varying")) {
        p = skip_ws(p + 7);
        if (parse_type_and_names(ctx, p, decl)) {
            decl->kind = DECL_VARYING;
            return DECL_VARYING;
        }
    }

    /* uniform */
    if (starts_with_word(p, "uniform")) {
        p = skip_ws(p + 7);
        if (parse_type_and_names(ctx, p, decl)) {
            decl->kind = DECL_UNIFORM;
            return DECL_UNIFORM;
        }
        /* parse_type_and_names failed — check if it's a struct uniform */
        {
            const char *sp = skip_precision(p);
            char type_str[GLSLT_MAX_NAME];
            const char *after_type = read_word(sp, type_str, sizeof(type_str));
            if (after_type != sp && find_struct_def(ctx, type_str)) {
                strncpy(decl->struct_type_name, type_str, GLSLT_MAX_NAME - 1);
                decl->struct_type_name[GLSLT_MAX_NAME - 1] = '\0';
                decl->num_names = 0;
                memset(decl->array_sizes, 0, sizeof(decl->array_sizes));
                sp = skip_ws(after_type);
                /* Read variable names (comma-separated), with optional [N] array size */
                while (*sp && *sp != ';' && decl->num_names < 8) {
                    char name[GLSLT_MAX_NAME];
                    const char *after_name = read_word(sp, name, sizeof(name));
                    if (after_name == sp) break;
                    strncpy(decl->names[decl->num_names], name, GLSLT_MAX_NAME - 1);
                    decl->names[decl->num_names][GLSLT_MAX_NAME - 1] = '\0';
                    decl->array_sizes[decl->num_names] = 0;
                    sp = skip_ws(after_name);
                    /* Parse array size [N], [MACRO_NAME], or [expr] */
                    if (*sp == '[') {
                        sp++;
                        sp = skip_ws(sp);
                        int arr_size = 0;
                        if (*sp >= '0' && *sp <= '9') {
                            while (*sp >= '0' && *sp <= '9') {
                                arr_size = arr_size * 10 + (*sp - '0');
                                sp++;
                            }
                        } else if (is_ident_char(*sp)) {
                            char macro_name[GLSLT_MAX_NAME];
                            const char *mp = read_word(sp, macro_name, sizeof(macro_name));
                            if (ctx->defines) {
                                int resolved = glslt_resolve_define(ctx->defines, macro_name);
                                if (resolved > 0) arr_size = resolved;
                            }
                            sp = mp;
                        }
                        sp = skip_ws(sp);
                        /* Handle arithmetic expressions: [N + M] etc. */
                        while (*sp == '+' || *sp == '-') {
                            char op = *sp++;
                            sp = skip_ws(sp);
                            int operand = 0;
                            if (*sp >= '0' && *sp <= '9') {
                                while (*sp >= '0' && *sp <= '9') {
                                    operand = operand * 10 + (*sp - '0');
                                    sp++;
                                }
                            } else if (is_ident_char(*sp)) {
                                char macro_name2[GLSLT_MAX_NAME];
                                const char *mp2 = read_word(sp, macro_name2, sizeof(macro_name2));
                                if (ctx->defines) {
                                    int resolved = glslt_resolve_define(ctx->defines, macro_name2);
                                    if (resolved > 0) operand = resolved;
                                }
                                sp = mp2;
                            }
                            if (op == '+') arr_size += operand;
                            else           arr_size -= operand;
                            sp = skip_ws(sp);
                        }
                        if (*sp == ']') sp++;
                        decl->array_sizes[decl->num_names] = arr_size;
                        sp = skip_ws(sp);
                    }
                    decl->num_names++;
                    if (*sp == ',') { sp++; sp = skip_ws(sp); }
                }
                if (decl->num_names > 0) {
                    decl->kind = DECL_UNIFORM_STRUCT;
                    return DECL_UNIFORM_STRUCT;
                }
            }
        }
    }

    return DECL_NONE;
}

/* ========================================================================== */
/*  std140 layout computation                                                  */
/* ========================================================================== */

/* Compute std140 size for a struct type (single element) */
static int compute_struct_std140_size(const struct_def_t *sd) {
    if (!sd) return 16;
    int offset = 0;
    for (int i = 0; i < sd->num_fields; i++) {
        const type_info_t *ti = (sd->fields[i].type < GLSLT_TYPE_COUNT)
            ? find_type_info_by_enum(sd->fields[i].type) : NULL;
        if (!ti) continue;
        int base_align = ti->std140_align;
        int base_size  = ti->std140_size;
        if (sd->fields[i].array_size > 0) {
            base_align = 16;
            int elem_stride = base_size < 16 ? 16 : base_size;
            offset = (offset + base_align - 1) & ~(base_align - 1);
            offset += elem_stride * sd->fields[i].array_size;
        } else {
            offset = (offset + base_align - 1) & ~(base_align - 1);
            offset += base_size;
        }
    }
    /* std140: struct size rounded up to vec4 (16 bytes) */
    return (offset + 15) & ~15;
}

static void compute_std140_layout(glslt_uniform_t *uniforms, int count,
                                  int *out_total_size) {
    int offset = 0;

    for (int i = 0; i < count; i++) {
        const type_info_t *ti = find_type_info_by_enum(uniforms[i].type);
        if (!ti) continue;

        int base_size  = ti->std140_size;
        int base_align = ti->std140_align;

        if (uniforms[i].array_size > 0) {
            /* std140 arrays: each element padded to vec4 (16 bytes minimum) */
            int elem_stride = base_size < 16 ? 16 : base_size;
            base_align = 16;

            offset = (offset + base_align - 1) & ~(base_align - 1);
            uniforms[i].offset = offset;
            uniforms[i].size = elem_stride * uniforms[i].array_size;
            offset += uniforms[i].size;
        } else {
            offset = (offset + base_align - 1) & ~(base_align - 1);
            uniforms[i].offset = offset;
            uniforms[i].size = base_size;
            offset += base_size;
        }
    }

    /* Total size rounded up to 16 bytes */
    *out_total_size = (offset + 15) & ~15;
}

/* ========================================================================== */
/*  Location/binding assignment                                                */
/* ========================================================================== */

static int find_attrib_location(const glslt_options_t *opts, const char *name) {
    for (int i = 0; i < opts->num_attrib_locations; i++) {
        if (strcmp(opts->attrib_locations[i].name, name) == 0)
            return opts->attrib_locations[i].location;
    }
    return -1;
}

static int find_varying_location(const glslt_options_t *opts, const char *name) {
    for (int i = 0; i < opts->num_varying_locations; i++) {
        if (strcmp(opts->varying_locations[i].name, name) == 0)
            return opts->varying_locations[i].location;
    }
    return -1;
}

/* Number of consecutive attribute locations consumed by a type.
 * mat2=2, mat3=3, mat4=4, everything else=1. */
static int type_location_size(glslt_type_t type) {
    switch (type) {
        case GLSLT_MAT2: return 2;
        case GLSLT_MAT3: return 3;
        case GLSLT_MAT4: return 4;
        default: return 1;
    }
}

static void assign_attrib_locations(glslt_attribute_t *attrs, int count,
                                    const glslt_options_t *opts) {
    /* First pass: assign explicit bindings, mark ALL consumed locations */
    int used[32] = {0};
    for (int i = 0; i < count; i++) {
        int loc = find_attrib_location(opts, attrs[i].name);
        if (loc >= 0 && loc < 32) {
            attrs[i].location = loc;
            int sz = type_location_size(attrs[i].type);
            for (int s = 0; s < sz && loc + s < 32; s++)
                used[loc + s] = 1;
        } else {
            attrs[i].location = -1;
        }
    }
    /* Second pass: auto-assign remaining, respecting multi-location types */
    int next = 0;
    for (int i = 0; i < count; i++) {
        if (attrs[i].location >= 0) continue;
        int sz = type_location_size(attrs[i].type);
        /* Find a contiguous block of 'sz' unused locations */
        while (next + sz <= 32) {
            int ok = 1;
            for (int s = 0; s < sz; s++) {
                if (used[next + s]) { ok = 0; break; }
            }
            if (ok) break;
            next++;
        }
        if (next + sz > 32) break; /* No space left */
        attrs[i].location = next;
        for (int s = 0; s < sz; s++)
            used[next + s] = 1;
        next += sz;
    }
}

static void assign_varying_locations(glslt_varying_t *varyings, int count,
                                     const glslt_options_t *opts) {
    /* Sort alphabetically for deterministic assignment */
    qsort(varyings, count, sizeof(glslt_varying_t), cmp_by_name_varying);

    /* First pass: assign explicit bindings */
    int used[32] = {0};
    for (int i = 0; i < count; i++) {
        int loc = find_varying_location(opts, varyings[i].name);
        if (loc >= 0 && loc < 32) {
            varyings[i].location = loc;
            /* Account for multi-location types: arrays AND matrix columns */
            int type_slots = type_location_size(varyings[i].type);
            int array_slots = varyings[i].array_size > 0 ? varyings[i].array_size : 1;
            int slots = type_slots * array_slots;
            for (int s = 0; s < slots && (loc + s) < 32; s++)
                used[loc + s] = 1;
        } else {
            varyings[i].location = -1;
        }
    }
    /* Second pass: auto-assign remaining (alphabetical order),
     * respecting multi-location types (mat2=2, mat3=3, mat4=4) */
    int next = 0;
    for (int i = 0; i < count; i++) {
        if (varyings[i].location >= 0) continue;
        int type_slots = type_location_size(varyings[i].type);
        int array_slots = varyings[i].array_size > 0 ? varyings[i].array_size : 1;
        int slots = type_slots * array_slots;
        /* Find a contiguous block of 'slots' unused locations */
        while (next + slots <= 32) {
            int ok = 1;
            for (int s = 0; s < slots; s++) {
                if (used[next + s]) { ok = 0; break; }
            }
            if (ok) break;
            next++;
        }
        if (next + slots > 32) break;
        varyings[i].location = next;
        for (int s = 0; s < slots && (next + s) < 32; s++)
            used[next + s] = 1;
        next += slots;
    }
}

/* ========================================================================== */
/*  Body text replacements                                                     */
/* ========================================================================== */

/* Translate GLES preprocessor macros in a line.
 * Can't use #define (GL_ prefix is reserved, __ names get warnings).
 * Instead, do targeted text replacement:
 *   #ifdef GL_ES / #ifdef __VERSION__     → #if 1
 *   #ifndef GL_ES / #ifndef __VERSION__   → #if 0
 *   defined(GL_ES), defined(__VERSION__)   → 1
 *   GL_ES (in expressions)                → 1
 *   __VERSION__ (in expressions)          → 100
 */
static void translate_gles_pp_macros(const char *in, char *out, int out_size) {
    const char *sp = skip_ws(in);

    /* #ifdef GL_ES / #ifdef __VERSION__ → #if 1 */
    if (strncmp(sp, "#ifdef", 6) == 0 && !is_ident_char(sp[6])) {
        const char *arg = skip_ws(sp + 6);
        if (starts_with_word(arg, "GL_ES") || starts_with_word(arg, "__VERSION__")) {
            strncpy(out, "#if 1", out_size);
            out[out_size - 1] = '\0';
            return;
        }
    }
    /* #ifndef GL_ES / #ifndef __VERSION__ → #if 0 */
    if (strncmp(sp, "#ifndef", 7) == 0 && !is_ident_char(sp[7])) {
        const char *arg = skip_ws(sp + 7);
        if (starts_with_word(arg, "GL_ES") || starts_with_word(arg, "__VERSION__")) {
            strncpy(out, "#if 0", out_size);
            out[out_size - 1] = '\0';
            return;
        }
    }

    /* For #if / #elif lines, replace defined(GL_ES) and defined(__VERSION__) with 1,
     * then word-replace GL_ES → 1 and __VERSION__ → 100 */
    char b1[MAX_LINE_LEN], b2[MAX_LINE_LEN];

    /* Substring replace: defined(GL_ES) → 1, defined(__VERSION__) → 1 */
    strncpy(b1, in, sizeof(b1));
    b1[sizeof(b1) - 1] = '\0';
    {
        const char *patterns[] = { "defined(GL_ES)", "defined(__VERSION__)",
                                   "defined (GL_ES)", "defined (__VERSION__)" };
        for (int pi = 0; pi < 4; pi++) {
            int plen = (int)strlen(patterns[pi]);
            char *pos;
            while ((pos = strstr(b1, patterns[pi])) != NULL) {
                int prefix = (int)(pos - b1);
                snprintf(b2, sizeof(b2), "%.*s1%s", prefix, b1, pos + plen);
                strncpy(b1, b2, sizeof(b1));
                b1[sizeof(b1) - 1] = '\0';
            }
        }
    }

    /* Word-replace GL_ES → 1, __VERSION__ → 100 */
    replace_word(b1, "GL_ES", "1", b2, sizeof(b2));
    replace_word(b2, "__VERSION__", "100", out, out_size);
}

static void apply_body_replacements(const char *line, char *out, int out_size,
                                    glslt_stage_t stage) {
    /* Chain of word-boundary-aware replacements.
     * Order doesn't matter because word boundary checks prevent partial matches
     * (e.g. "texture2D" won't match inside "texture2DProj"). */
    char b1[MAX_LINE_LEN], b2[MAX_LINE_LEN];

    /* Translate GLES preprocessor macros (GL_ES, __VERSION__).
     * Preprocessor lines need special handling (#ifdef → #if, defined() → 1).
     * Non-preprocessor lines just need word replacement (float(GL_ES) → float(1)). */
    const char *sp = skip_ws(line);
    if (*sp == '#') {
        translate_gles_pp_macros(line, b1, sizeof(b1));
    } else {
        char t1[MAX_LINE_LEN];
        replace_word(line, "GL_ES", "1", t1, sizeof(t1));
        replace_word(t1, "__VERSION__", "100", b1, sizeof(b1));
    }

    replace_word(b1,    "texture2DProjLod", "textureProjLod", b2, sizeof(b2));
    replace_word(b2,    "texture2DProj",    "textureProj",    b1, sizeof(b1));
    replace_word(b1,    "texture2DLod",     "textureLod",     b2, sizeof(b2));
    replace_word(b2,    "textureCubeLod",   "textureLod",     b1, sizeof(b1));
    replace_word(b1,    "shadow2DEXT",      "texture",        b2, sizeof(b2));
    replace_word(b2,    "shadow2DProjEXT",  "textureProj",    b1, sizeof(b1));
    replace_word(b1,    "shadow2D",         "texture",        b2, sizeof(b2));
    replace_word(b2,    "shadow2DProj",     "textureProj",    b1, sizeof(b1));
    replace_word(b1,    "texture2D",        "texture",        b2, sizeof(b2));
    replace_word(b2,    "textureCube",      "texture",        b1, sizeof(b1));

    /* Strip precision qualifiers from body code */
    replace_word(b1, "lowp", "", b2, sizeof(b2));
    replace_word(b2, "mediump", "", b1, sizeof(b1));
    replace_word(b1, "highp", "", b2, sizeof(b2));

    /* Replace gl_Max* built-in constants with literal values matching
     * what SwitchGLES reports via glGetIntegerv().  The GLSL 4.60 compiler
     * (uam) may report different hardware limits, causing dEQP mismatches. */
    replace_word(b2, "gl_MaxVertexAttribs",              "16",  b1, sizeof(b1));
    replace_word(b1, "gl_MaxVertexUniformVectors",       "256", b2, sizeof(b2));
    replace_word(b2, "gl_MaxFragmentUniformVectors",     "256", b1, sizeof(b1));
    replace_word(b1, "gl_MaxVaryingVectors",             "15",  b2, sizeof(b2));
    replace_word(b2, "gl_MaxTextureImageUnits",          "16",  b1, sizeof(b1));
    replace_word(b1, "gl_MaxVertexTextureImageUnits",    "16",  b2, sizeof(b2));
    replace_word(b2, "gl_MaxCombinedTextureImageUnits",  "16",  b1, sizeof(b1));
    replace_word(b1, "gl_MaxDrawBuffers",                "1",   b2, sizeof(b2));
    /* b2 now has all replacements — continue from b2 */

    if (stage == GLSLT_FRAGMENT) {
        replace_word(b2, "gl_FragColor", "fragColor", b1, sizeof(b1));
        /* gl_FragData[N] -> fragColor (N=0) or fragData_N (N>0) */
        char fb[MAX_LINE_LEN];
        strncpy(fb, b1, sizeof(fb));
        fb[sizeof(fb) - 1] = '\0';
        for (int n = 0; n < 8; n++) {
            char old_ref[32], new_ref[32];
            snprintf(old_ref, sizeof(old_ref), "gl_FragData[%d]", n);
            if (n == 0)
                snprintf(new_ref, sizeof(new_ref), "fragColor");
            else
                snprintf(new_ref, sizeof(new_ref), "fragData_%d", n);
            char fb2[MAX_LINE_LEN];
            replace_word(fb, old_ref, new_ref, fb2, sizeof(fb2));
            strncpy(fb, fb2, sizeof(fb));
            fb[sizeof(fb) - 1] = '\0';
        }
        strncpy(out, fb, out_size);
        out[out_size - 1] = '\0';
    } else {
        strncpy(out, b2, out_size);
        out[out_size - 1] = '\0';
    }
}

/* Check if an #extension line is for something core in GLSL 4.60 */
static int is_core_extension(const char *line) {
    /* All ES 1.00 extensions are core in 4.60 */
    if (strstr(line, "GL_OES_"))  return 1;
    if (strstr(line, "GL_EXT_"))  return 1;
    if (strstr(line, "GL_NV_"))   return 1;
    return 0;
}

/* Normalize GLSL source: insert newlines after semicolons so each statement
 * is on its own line.  dEQP generates shaders with multiple declarations
 * jammed on a single line (e.g. "uniform float a;uniform vec2 b;void main(){").
 * The line-based parser needs them separated. */
static char *normalize_source(const char *source) {
    int len = (int)strlen(source);
    char *out = (char *)malloc(len * 2 + 1);
    if (!out) return NULL;

    int paren_depth = 0;
    int brace_depth = 0;
    int j = 0;
    for (int i = 0; i < len; i++) {
        out[j++] = source[i];
        if (source[i] == '(') paren_depth++;
        else if (source[i] == ')') { if (paren_depth > 0) paren_depth--; }
        else if (source[i] == '{' && paren_depth == 0) brace_depth++;
        else if (source[i] == '}' && paren_depth == 0) {
            if (brace_depth > 0) brace_depth--;
            /* After closing brace at depth 0 (end of function body),
             * insert newline if followed by a declaration keyword.
             * Fixes dEQP single-line: "} uniform float ref;" */
            if (brace_depth == 0) {
                int k = i + 1;
                while (k < len && (source[k] == ' ' || source[k] == '\t')) k++;
                if (k < len && source[k] != '\n' && source[k] != '\r' &&
                    isalpha((unsigned char)source[k])) {
                    out[j++] = '\n';
                }
            }
        }
        else if (source[i] == ';' && paren_depth == 0) {
            /* After semicolon outside parens, insert newline if next non-ws
             * char starts a new statement (letter, #, }) */
            int k = i + 1;
            while (k < len && (source[k] == ' ' || source[k] == '\t')) k++;
            if (k < len && source[k] != '\n' && source[k] != '\r' &&
                (isalpha((unsigned char)source[k]) || source[k] == '#' ||
                 source[k] == '}')) {
                out[j++] = '\n';
            }
        }
    }
    out[j] = '\0';
    return out;
}

/* ========================================================================== */
/*  GLES 1.00 semantic validation                                              */
/* ========================================================================== */

/* Public API: validate GLES 1.00 semantics at compile time.
 * Normalizes source before validation (inserts newlines). */
static int glslt_validate_es100_impl(glslt_ctx_t *ctx, const char *source, glslt_stage_t stage,
                         char *error, int error_size) {
    if (!source) {
        snprintf(error, error_size, "source is NULL");
        return 0;
    }
    /* Validate preprocessor directives on raw source (before normalization,
     * since normalization may reformat #version position) */
    if (!validate_preprocessor_directives(source, error, error_size)) {
        return 0;
    }
    /* Check undefined identifiers in #if/#elif (raw source, sequential #define tracking) */
    if (!validate_preprocessor_undefined(source, error, error_size)) {
        return 0;
    }
    char *norm = normalize_source(source);
    if (!norm) {
        snprintf(error, error_size, "out of memory");
        return 0;
    }
    int ok = validate_gles_semantics(norm, stage, error, error_size);
    if (ok) {
        collect_struct_defs(ctx, norm);
        ok = validate_const_initializers(ctx, norm, error, error_size);
    }
    if (ok) {
        ok = validate_qualification_order(norm, error, error_size);
    }
    if (ok) {
        ok = validate_texture_functions(norm, stage, error, error_size);
    }
    free(norm);
    /* collect_struct_defs above populated the module-global struct table;
     * clear it so it cannot leak into a later transpile/validate call. */
    ctx->num_structs = 0;
    ctx->num_replacements = 0;
    ctx->num_struct_array_uniforms = 0;
    return ok;
}

/* ========================================================================== */
/*  Main transpile function                                                    */
/* ========================================================================== */

/* ---- Pass 1: scan the shader source into a glslt_decls_t ---- */
static void collect_declarations(glslt_ctx_t *ctx, const char *source,
                                 glslt_stage_t stage, glslt_decls_t *d) {
    memset(d, 0, sizeof(*d));
    d->max_frag_data = -1;
    int in_block_comment = 0;

    const char *lp = source;
    while (*lp) {
        char line[MAX_LINE_LEN];
        extract_line(lp, line, sizeof(line));

        /* Track block comments */
        {
            const char *p = line;
            while (*p) {
                if (in_block_comment) {
                    if (p[0] == '*' && p[1] == '/') {
                        in_block_comment = 0;
                        p += 2;
                        continue;
                    }
                } else {
                    if (p[0] == '/' && p[1] == '*') {
                        in_block_comment = 1;
                        p += 2;
                        continue;
                    }
                }
                p++;
            }
        }

        if (!in_block_comment) {
            parsed_decl_t decl;
            decl_kind_t kind = parse_line(ctx, line, &decl);

            switch (kind) {
            case DECL_ATTRIBUTE:
                for (int i = 0; i < decl.num_names && d->na < GLSLT_MAX_ATTRIBUTES; i++) {
                    strncpy(d->attributes[d->na].name, decl.names[i], GLSLT_MAX_NAME - 1);
                    d->attributes[d->na].type = decl.type;
                    d->attributes[d->na].location = -1;
                    d->na++;
                }
                break;

            case DECL_VARYING:
                for (int i = 0; i < decl.num_names && d->nv < GLSLT_MAX_VARYINGS; i++) {
                    strncpy(d->varyings[d->nv].name, decl.names[i], GLSLT_MAX_NAME - 1);
                    d->varyings[d->nv].type = decl.type;
                    d->varyings[d->nv].location = -1;
                    d->varyings[d->nv].array_size = decl.array_sizes[i];
                    d->nv++;
                }
                break;

            case DECL_UNIFORM:
                for (int i = 0; i < decl.num_names; i++) {
                    if (decl.is_sampler) {
                        int arr = decl.array_sizes[i];
                        if (arr > 0) {
                            /* Sampler array: expand into individual entries */
                            for (int a = 0; a < arr && d->ns < GLSLT_MAX_SAMPLERS; a++) {
                                snprintf(d->samplers[d->ns].name, GLSLT_MAX_NAME, "%s_%d", decl.names[i], a);
                                snprintf(d->samplers[d->ns].gles_name, GLSLT_MAX_NAME, "%s[%d]", decl.names[i], a);
                                d->samplers[d->ns].type = decl.type;
                                d->samplers[d->ns].binding = -1;
                                d->samplers[d->ns].array_index = a;
                                d->samplers[d->ns].array_total = arr;
                                d->ns++;
                            }
                            /* Add body replacements: s[0] → s_0, s[1] → s_1 */
                            for (int a = 0; a < arr && ctx->num_replacements < MAX_STRUCT_REPLS; a++) {
                                char old_ref[GLSLT_MAX_NAME * 2];
                                char new_ref[GLSLT_MAX_NAME * 2];
                                snprintf(old_ref, sizeof(old_ref), "%s[%d]", decl.names[i], a);
                                snprintf(new_ref, sizeof(new_ref), "%s_%d", decl.names[i], a);
                                strncpy(ctx->replacements[ctx->num_replacements].old_ref, old_ref,
                                        sizeof(ctx->replacements[0].old_ref) - 1);
                                strncpy(ctx->replacements[ctx->num_replacements].new_ref, new_ref,
                                        sizeof(ctx->replacements[0].new_ref) - 1);
                                ctx->num_replacements++;
                            }
                        } else {
                            /* Single sampler */
                            if (d->ns < GLSLT_MAX_SAMPLERS) {
                                strncpy(d->samplers[d->ns].name, decl.names[i], GLSLT_MAX_NAME - 1);
                                strncpy(d->samplers[d->ns].gles_name, decl.names[i], GLSLT_MAX_NAME - 1);
                                d->samplers[d->ns].type = decl.type;
                                d->samplers[d->ns].binding = -1;
                                d->samplers[d->ns].array_index = -1;
                                d->samplers[d->ns].array_total = 0;
                                d->ns++;
                            }
                        }
                    } else {
                        if (d->nu < GLSLT_MAX_UNIFORMS) {
                            strncpy(d->uniforms[d->nu].name, decl.names[i], GLSLT_MAX_NAME - 1);
                            /* For non-struct d->uniforms, gles_name = name */
                            strncpy(d->uniforms[d->nu].gles_name, decl.names[i], GLSLT_MAX_NAME - 1);
                            d->uniforms[d->nu].type = decl.type;
                            d->uniforms[d->nu].array_size = decl.array_sizes[i];
                            d->uniforms[d->nu].binding = -1;
                            d->uniforms[d->nu].offset = 0;
                            d->uniforms[d->nu].size = 0;
                            d->nu++;
                        }
                    }
                }
                break;

            case DECL_UNIFORM_STRUCT:
                for (int i = 0; i < decl.num_names; i++) {
                    int arr_size = decl.array_sizes[i];
                    if (arr_size > 0) {
                        /* Struct array: keep as whole struct in UBO (not flattened).
                         * Flattening breaks dynamic indexing (e.g. u_lights[ndx].field).
                         * The struct definition must be emitted before the UBO block. */
                        if (ctx->num_struct_array_uniforms < MAX_STRUCT_ARRAY_UNIFORMS) {
                            struct_array_uniform_t *sau = &ctx->struct_array_uniforms[ctx->num_struct_array_uniforms++];
                            strncpy(sau->struct_type, decl.struct_type_name, GLSLT_MAX_NAME - 1);
                            sau->struct_type[GLSLT_MAX_NAME - 1] = '\0';
                            strncpy(sau->var_name, decl.names[i], GLSLT_MAX_NAME - 1);
                            sau->var_name[GLSLT_MAX_NAME - 1] = '\0';
                            sau->array_size = arr_size;
                            struct_def_t *sd = find_struct_def(ctx, decl.struct_type_name);
                            sau->std140_size = compute_struct_std140_size(sd) * arr_size;
                        }
                    } else {
                        /* Single struct instance */
                        flatten_struct_to_uniforms(ctx, decl.names[i],
                                                   decl.struct_type_name,
                                                   d->uniforms, &d->nu,
                                                   d->samplers, &d->ns);
                    }
                }
                break;

            default:
                break;
            }

            /* Check for gl_FragColor / gl_FragData usage anywhere */
            if (stage == GLSLT_FRAGMENT) {
                if (strstr(line, "gl_FragColor") || strstr(line, "gl_FragData"))
                    d->has_frag_color = 1;
                /* Track highest gl_FragData[N] index for MRT outputs */
                const char *fd = line;
                while ((fd = strstr(fd, "gl_FragData[")) != NULL) {
                    fd += 12; /* skip "gl_FragData[" */
                    int idx = atoi(fd);
                    if (idx > d->max_frag_data) d->max_frag_data = idx;
                }
            }
        }

        lp = next_line(lp);
    }

}

/* ---- Emit the GLSL 4.60 header: version, ins/outs, UBO block, samplers, outputs ---- */
static void emit_header(strbuf_t *sb, glslt_ctx_t *ctx, const glslt_options_t *opts,
                        glslt_stage_t stage, const glslt_decls_t *d) {
    /* Version */
    sb_printf(sb, "#version %d\n", opts->target_version);

    /* Attributes (vertex shader only) */
    if (stage == GLSLT_VERTEX && d->na > 0) {
        sb_append(sb, "\n");
        for (int i = 0; i < d->na; i++) {
            sb_printf(sb, "layout(location = %d) in %s %s;\n",
                      d->attributes[i].location,
                      glslt_type_name(d->attributes[i].type),
                      d->attributes[i].name);
        }
    }

    /* Varyings */
    if (d->nv > 0) {
        sb_append(sb, "\n");
        const char *dir = (stage == GLSLT_VERTEX) ? "out" : "in";
        for (int i = 0; i < d->nv; i++) {
            if (d->varyings[i].array_size > 0) {
                sb_printf(sb, "layout(location = %d) %s %s %s[%d];\n",
                          d->varyings[i].location, dir,
                          glslt_type_name(d->varyings[i].type),
                          d->varyings[i].name,
                          d->varyings[i].array_size);
            } else {
                sb_printf(sb, "layout(location = %d) %s %s %s;\n",
                          d->varyings[i].location, dir,
                          glslt_type_name(d->varyings[i].type),
                          d->varyings[i].name);
            }
        }
    }

    /* Emit struct definitions needed by struct array d->uniforms (before UBO block) */
    for (int sa = 0; sa < ctx->num_struct_array_uniforms; sa++) {
        struct_def_t *sd = find_struct_def(ctx, ctx->struct_array_uniforms[sa].struct_type);
        if (!sd) continue;
        sb_printf(sb, "\nstruct %s {\n", sd->name);
        for (int f = 0; f < sd->num_fields; f++) {
            const char *tname = sd->fields[f].is_struct
                ? sd->fields[f].type_name
                : glslt_type_name(sd->fields[f].type);
            if (sd->fields[f].array_size > 0) {
                sb_printf(sb, "    %s %s[%d];\n", tname, sd->fields[f].name, sd->fields[f].array_size);
            } else {
                sb_printf(sb, "    %s %s;\n", tname, sd->fields[f].name);
            }
        }
        sb_append(sb, "};\n");
    }

    /* UBO block (includes flattened scalar d->uniforms + struct array d->uniforms) */
    if (d->nu > 0 || ctx->num_struct_array_uniforms > 0) {
        sb_append(sb, "\n");
        sb_printf(sb, "layout(std140, binding = %d) uniform %sUniforms {\n",
                  opts->ubo_binding,
                  (stage == GLSLT_VERTEX) ? "Vertex" : "Fragment");
        for (int i = 0; i < d->nu; i++) {
            if (d->uniforms[i].array_size > 0) {
                sb_printf(sb, "    %s %s[%d];\n",
                          glslt_type_name(d->uniforms[i].type),
                          d->uniforms[i].name,
                          d->uniforms[i].array_size);
            } else {
                sb_printf(sb, "    %s %s;\n",
                          glslt_type_name(d->uniforms[i].type),
                          d->uniforms[i].name);
            }
        }
        /* Struct array d->uniforms (kept as whole structs) */
        for (int i = 0; i < ctx->num_struct_array_uniforms; i++) {
            sb_printf(sb, "    %s %s[%d];\n",
                      ctx->struct_array_uniforms[i].struct_type,
                      ctx->struct_array_uniforms[i].var_name,
                      ctx->struct_array_uniforms[i].array_size);
        }
        sb_append(sb, "};\n");
    }

    /* Samplers */
    if (d->ns > 0) {
        sb_append(sb, "\n");
        for (int i = 0; i < d->ns; i++) {
            sb_printf(sb, "layout(binding = %d) uniform %s %s;\n",
                      d->samplers[i].binding,
                      glslt_type_name(d->samplers[i].type),
                      d->samplers[i].name);
        }
    }

    /* Fragment output(s) */
    if (stage == GLSLT_FRAGMENT && d->has_frag_color) {
        if (d->max_frag_data > 0) {
            /* Multiple render targets: gl_FragData[0]..gl_FragData[N] */
            sb_append(sb, "\n");
            for (int i = 0; i <= d->max_frag_data; i++)
                sb_printf(sb, "layout(location = %d) out vec4 fragData_%d;\n", i, i);
        } else {
            sb_append(sb, "\nlayout(location = 0) out vec4 fragColor;\n");
        }
    }
}

/* ---- Emit the shader body: non-declaration lines with replacements applied ---- */
static void emit_body(glslt_ctx_t *ctx, strbuf_t *sb, const char *source,
                      glslt_stage_t stage) {
    int in_block_comment = 0;
    const char *lp;

    in_block_comment = 0;
    int body_started = 0;
    int inside_struct_def = 0;  /* Suppress collected struct definitions from output */
    lp = source;
    while (*lp) {
        char line[MAX_LINE_LEN];
        extract_line(lp, line, sizeof(line));

        /* Track block comment state (must mirror pass 1) */
        int line_in_comment = in_block_comment;
        {
            const char *p = line;
            while (*p) {
                if (in_block_comment) {
                    if (p[0] == '*' && p[1] == '/') {
                        in_block_comment = 0;
                        p += 2;
                        continue;
                    }
                } else {
                    if (p[0] == '/' && p[1] == '*') {
                        in_block_comment = 1;
                        p += 2;
                        continue;
                    }
                }
                p++;
            }
        }

        int emit = 1;

        /* Suppress collected struct definitions (they're emitted before UBO block
         * for struct array uniforms, or flattened into UBO for scalars). */
        if (!line_in_comment && !inside_struct_def) {
            const char *sp = skip_ws(line);
            if (strncmp(sp, "struct", 6) == 0 && !is_ident_char(sp[6])) {
                sp = skip_ws(sp + 6);
                char sname[GLSLT_MAX_NAME];
                const char *after = read_word(sp, sname, sizeof(sname));
                if (after != sp && find_struct_def(ctx, sname)) {
                    inside_struct_def = 1;
                    emit = 0;
                }
            }
        }
        if (inside_struct_def) {
            emit = 0;
            /* Check if this line closes the struct definition */
            if (strchr(line, '}'))
                inside_struct_def = 0;
        }

        if (!line_in_comment && emit) {
            /* Strip #pragma lines (ES hints not applicable to GLSL 4.60;
             * uam rejects #pragma STDGL invariant(all) in fragment shaders) */
            const char *stripped = skip_ws(line);
            if (stripped[0] == '#') {
                const char *dp = stripped + 1;
                while (*dp == ' ' || *dp == '\t') dp++;
                if (strncmp(dp, "pragma", 6) == 0 && !is_ident_char(dp[6])) {
                    emit = 0;
                }
            }
        }

        if (!line_in_comment && emit) {
            parsed_decl_t decl;
            decl_kind_t kind = parse_line(ctx, line, &decl);

            switch (kind) {
            case DECL_VERSION:
            case DECL_PRECISION:
            case DECL_ATTRIBUTE:
            case DECL_VARYING:
            case DECL_UNIFORM:
            case DECL_UNIFORM_STRUCT:
                emit = 0;
                break;
            case DECL_EXTENSION:
                emit = !is_core_extension(line);
                break;
            default:
                break;
            }
        }

        if (emit) {
            if (!body_started) {
                sb_append(sb, "\n");
                body_started = 1;
            }

            if (line_in_comment || line[0] == '\0') {
                /* Inside block comment or empty line: emit as-is */
                sb_append(sb, line);
            } else {
                char replaced[MAX_LINE_LEN];
                apply_body_replacements(line, replaced, sizeof(replaced), stage);
                if (ctx->num_replacements > 0) {
                    char replaced2[MAX_LINE_LEN];
                    apply_struct_replacements(ctx, replaced, replaced2, sizeof(replaced2));
                    sb_append(sb, replaced2);
                } else {
                    sb_append(sb, replaced);
                }
            }
            sb_append(sb, "\n");
        }

        lp = next_line(lp);
    }
}

static glslt_result_t glslt_transpile_impl(glslt_ctx_t *ctx, const char *source, glslt_stage_t stage,
                               const glslt_options_t *opts) {
    glslt_result_t result;
    memset(&result, 0, sizeof(result));

    /* Reset module-global parse state up front so a prior call that bailed
     * on an error path cannot leak stale structs/replacements into this one. */
    ctx->num_structs = 0;
    ctx->num_replacements = 0;
    ctx->num_struct_array_uniforms = 0;

    if (!source) {
        snprintf(result.error, sizeof(result.error), "source is NULL");
        return result;
    }

    if (!opts) {
        snprintf(result.error, sizeof(result.error), "opts is NULL");
        return result;
    }

    /* Pre-scan source for #define NAME NUMERIC_VALUE (for macro array sizes) */
    glslt_define_table_t defines;
    glslt_collect_defines(source, &defines);
    ctx->defines = &defines;

    /* Check if already modern GLSL - pass through unchanged */
    {
        const char *p = skip_ws(source);
        if (strncmp(p, "#version", 8) == 0) {
            p = skip_ws(p + 8);
            int ver = atoi(p);
            if (ver >= 300 && strstr(p, "es") == NULL) {
                /* Already desktop GLSL 300+ core, pass through unchanged.
                 * DkDeviceFlags_DepthMinusOneToOne handles GL→deko3d depth
                 * natively — no shader-level z transform needed. */
                int src_len = (int)strlen(source);
                result.output = (char *)malloc(src_len + 1);
                if (!result.output) {
                    snprintf(result.error, sizeof(result.error),
                             "out of memory (passthrough)");
                    ctx->defines = NULL;
                    return result;  /* result.success = 0 (from memset) */
                }
                memcpy(result.output, source, src_len + 1);
                result.output_len = src_len;

                result.success = 1;
                ctx->defines = NULL;
                return result;
            }
        }
    }

    /* ---- Preprocessor validation (on raw source, before normalization) ---- */
    if (!validate_preprocessor_directives(source, result.error, sizeof(result.error))) {
        ctx->defines = NULL;
        return result;
    }
    if (!validate_preprocessor_undefined(source, result.error, sizeof(result.error))) {
        ctx->defines = NULL;
        return result;
    }

    /* Normalize source: insert newlines after semicolons so each declaration
     * is on its own line (dEQP puts multiple decls on one line). */
    char *norm_source = normalize_source(source);
    if (!norm_source) {
        snprintf(result.error, sizeof(result.error), "out of memory normalizing source");
        ctx->defines = NULL;
        return result;
    }
    source = norm_source; /* Use normalized source for all subsequent processing */

    /* ---- GLES 1.00 semantic validation ---- */
    if (!validate_gles_semantics(source, stage, result.error, sizeof(result.error))) {
        free(norm_source);
        ctx->defines = NULL;
        return result;  /* result.success = 0 (from memset) */
    }
    if (!validate_const_initializers(ctx, source, result.error, sizeof(result.error))) {
        free(norm_source);
        ctx->defines = NULL;
        return result;
    }
    if (!validate_qualification_order(source, result.error, sizeof(result.error))) {
        free(norm_source);
        ctx->defines = NULL;
        return result;
    }
    if (!validate_texture_functions(source, stage, result.error, sizeof(result.error))) {
        free(norm_source);
        ctx->defines = NULL;
        return result;
    }

    /* ---- Pre-scan: collect struct definitions ---- */
    collect_struct_defs(ctx, source);
    ctx->num_replacements = 0;
    ctx->num_struct_array_uniforms = 0;

    /* ---- Detect gl_DepthRange usage and inject synthetic d.uniforms ---- */
    int has_depth_range = (strstr(source, "gl_DepthRange") != NULL) ? 1 : 0;
    if (has_depth_range) {
        /* Inject uniform declarations into the source so pass 1 collects them.
         * Also register struct-style replacements for member access. */
        const char *dr_decls =
            "uniform float sgl_dr_near;\n"
            "uniform float sgl_dr_far;\n"
            "uniform float sgl_dr_diff;\n";
        int dr_len = (int)strlen(dr_decls);
        int src_len = (int)strlen(source);
        char *new_src = (char *)malloc(src_len + dr_len + 1);
        if (new_src) {
            memcpy(new_src, dr_decls, dr_len);
            memcpy(new_src + dr_len, source, src_len + 1);
            free(norm_source);
            norm_source = new_src;
            source = norm_source;
        }
        /* Register replacements: gl_DepthRange.near → sgl_dr_near etc. */
        if (ctx->num_replacements + 3 <= MAX_STRUCT_REPLS) {
            strncpy(ctx->replacements[ctx->num_replacements].old_ref, "gl_DepthRange.near",
                    sizeof(ctx->replacements[0].old_ref) - 1);
            strncpy(ctx->replacements[ctx->num_replacements].new_ref, "sgl_dr_near",
                    sizeof(ctx->replacements[0].new_ref) - 1);
            ctx->num_replacements++;
            strncpy(ctx->replacements[ctx->num_replacements].old_ref, "gl_DepthRange.far",
                    sizeof(ctx->replacements[0].old_ref) - 1);
            strncpy(ctx->replacements[ctx->num_replacements].new_ref, "sgl_dr_far",
                    sizeof(ctx->replacements[0].new_ref) - 1);
            ctx->num_replacements++;
            strncpy(ctx->replacements[ctx->num_replacements].old_ref, "gl_DepthRange.diff",
                    sizeof(ctx->replacements[0].old_ref) - 1);
            strncpy(ctx->replacements[ctx->num_replacements].new_ref, "sgl_dr_diff",
                    sizeof(ctx->replacements[0].new_ref) - 1);
            ctx->num_replacements++;
        }
    }

    /* ---- Pass 1: collect declarations ---- */
    glslt_decls_t d;
    collect_declarations(ctx, source, stage, &d);

    /* ---- Process: assign locations, compute layout ---- */

    /* Attributes */
    assign_attrib_locations(d.attributes, d.na, opts);
    qsort(d.attributes, d.na, sizeof(glslt_attribute_t), cmp_by_location_attr);

    /* Varyings */
    assign_varying_locations(d.varyings, d.nv, opts);
    qsort(d.varyings, d.nv, sizeof(glslt_varying_t), cmp_by_location_varying);

    /* Uniforms: sort alphabetically, compute std140 layout */
    qsort(d.uniforms, d.nu, sizeof(glslt_uniform_t), cmp_by_name_uniform);
    int ubo_total_size = 0;
    compute_std140_layout(d.uniforms, d.nu, &ubo_total_size);
    for (int i = 0; i < d.nu; i++)
        d.uniforms[i].binding = opts->ubo_binding;

    /* Samplers: keep declaration order, assign bindings */
    for (int i = 0; i < d.ns; i++)
        d.samplers[i].binding = opts->sampler_binding_start + i;


    /* ---- Pass 2: emit output ---- */
    strbuf_t sb;
    sb_init(&sb);
    emit_header(&sb, ctx, opts, stage, &d);
    emit_body(ctx, &sb, source, stage);

    /* DkDeviceFlags_DepthMinusOneToOne handles GL→deko3d depth natively.
     * No shader-level z transform needed. */

    /* ---- Fill result ---- */

    if (sb.failed) {
        /* Output buffer ran out of memory mid-emit: fail instead of
         * returning a silently-truncated (broken) shader. */
        snprintf(result.error, sizeof(result.error),
                 "out of memory building transpiled output");
        free(sb.buf);
        ctx->defines = NULL;
        ctx->num_structs = 0;
        ctx->num_replacements = 0;
        ctx->num_struct_array_uniforms = 0;
        free(norm_source);
        return result;  /* result.success = 0 (from memset) */
    }

    result.output = sb.buf;
    result.output_len = sb.len;
    result.success = 1;

    /* Copy reflection data */
    memcpy(result.uniforms, d.uniforms, d.nu * sizeof(glslt_uniform_t));
    result.num_uniforms = d.nu;
    result.ubo_binding = opts->ubo_binding;
    result.ubo_total_size = ubo_total_size;

    memcpy(result.samplers, d.samplers, d.ns * sizeof(glslt_sampler_t));
    result.num_samplers = d.ns;

    memcpy(result.attributes, d.attributes, d.na * sizeof(glslt_attribute_t));
    result.num_attributes = d.na;

    memcpy(result.varyings, d.varyings, d.nv * sizeof(glslt_varying_t));
    result.num_varyings = d.nv;

    result.has_depth_range = has_depth_range;

    ctx->defines = NULL;
    ctx->num_structs = 0;
    ctx->num_replacements = 0;
    ctx->num_struct_array_uniforms = 0;
    free(norm_source);
    return result;
}

/* ========================================================================== */
/*  Public API helpers                                                         */
/* ========================================================================== */

void glslt_options_init(glslt_options_t *opts) {
    if (!opts) return;
    memset(opts, 0, sizeof(*opts));
    opts->target_version = 460;
    opts->ubo_binding = 0;
    opts->sampler_binding_start = 0;
}

void glslt_set_attrib_location(glslt_options_t *opts, const char *name, int location) {
    if (!opts || !name) return;
    if (opts->num_attrib_locations >= GLSLT_MAX_BINDINGS) return;
    int idx = opts->num_attrib_locations++;
    strncpy(opts->attrib_locations[idx].name, name, GLSLT_MAX_NAME - 1);
    opts->attrib_locations[idx].name[GLSLT_MAX_NAME - 1] = '\0';
    opts->attrib_locations[idx].location = location;
}

void glslt_set_varying_location(glslt_options_t *opts, const char *name, int location) {
    if (!opts || !name) return;
    if (opts->num_varying_locations >= GLSLT_MAX_BINDINGS) return;
    int idx = opts->num_varying_locations++;
    strncpy(opts->varying_locations[idx].name, name, GLSLT_MAX_NAME - 1);
    opts->varying_locations[idx].name[GLSLT_MAX_NAME - 1] = '\0';
    opts->varying_locations[idx].location = location;
}

void glslt_result_free(glslt_result_t *result) {
    if (!result) return;
    if (result->output) {
        free(result->output);
        result->output = NULL;
    }
    result->output_len = 0;
}

/* ==========================================================================
 *  Public entry points
 *
 *  Allocate the per-call transpiler context on the heap (~100 KB, calloc-zeroed
 *  so no field can carry stale state), run the implementation, and free it in
 *  exactly one place. This makes the transpiler reentrant and removes the
 *  global-state reset discipline that was the root cause of B12.
 * ========================================================================== */

glslt_result_t glslt_transpile(const char *source, glslt_stage_t stage,
                               const glslt_options_t *opts) {
    glslt_ctx_t *ctx = (glslt_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        glslt_result_t r;
        memset(&r, 0, sizeof(r));
        snprintf(r.error, sizeof(r.error), "out of memory (transpiler context)");
        return r;
    }
    glslt_result_t r = glslt_transpile_impl(ctx, source, stage, opts);
    free(ctx);
    return r;
}

int glslt_validate_es100(const char *source, glslt_stage_t stage,
                         char *error, int error_size) {
    glslt_ctx_t *ctx = (glslt_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        snprintf(error, error_size, "out of memory (transpiler context)");
        return 0;
    }
    int ok = glslt_validate_es100_impl(ctx, source, stage, error, error_size);
    free(ctx);
    return ok;
}
