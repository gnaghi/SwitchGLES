/*
 * glsl_transpiler.c - GLSL ES 1.00 to GLSL 4.60 Core Profile Transpiler
 *
 * Implementation. See glsl_transpiler.h for API documentation.
 *
 * License: MIT
 */

#include "glsl_transpiler.h"
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
} strbuf_t;

static void sb_init(strbuf_t *sb) {
    sb->cap = 4096;
    sb->buf = (char *)malloc(sb->cap);
    sb->len = 0;
    if (sb->buf) sb->buf[0] = '\0';
}

static void sb_ensure(strbuf_t *sb, int extra) {
    while (sb->len + extra + 1 > sb->cap) {
        sb->cap *= 2;
        sb->buf = (char *)realloc(sb->buf, sb->cap);
    }
}

static void sb_append(strbuf_t *sb, const char *str) {
    int slen = (int)strlen(str);
    sb_ensure(sb, slen);
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

typedef struct {
    char           name[GLSLT_MAX_NAME];
    struct_field_t fields[MAX_STRUCT_FIELDS];
    int            num_fields;
} struct_def_t;

typedef struct {
    char old_ref[GLSLT_MAX_NAME * 4];  /* e.g. "val.a" */
    char new_ref[GLSLT_MAX_NAME * 4];  /* e.g. "val_a" */
} struct_repl_t;

static struct_def_t  s_structs[MAX_STRUCT_DEFS];
static int           s_num_structs = 0;
static struct_repl_t s_replacements[MAX_STRUCT_REPLS];
static int           s_num_replacements = 0;

/* Struct array uniforms — kept as whole structs in UBO (not flattened) */
#define MAX_STRUCT_ARRAY_UNIFORMS 8
typedef struct {
    char struct_type[GLSLT_MAX_NAME];
    char var_name[GLSLT_MAX_NAME];
    int  array_size;
    int  std140_size;  /* Total bytes in std140 layout */
} struct_array_uniform_t;
static struct_array_uniform_t s_struct_array_uniforms[MAX_STRUCT_ARRAY_UNIFORMS];
static int s_num_struct_array_uniforms = 0;

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

static int is_ident_char(char c) {
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
static int starts_with_word(const char *str, const char *word) {
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
static const struct_def_t *find_struct_def(const char *name) {
    for (int i = 0; i < s_num_structs; i++) {
        if (strcmp(s_structs[i].name, name) == 0)
            return &s_structs[i];
    }
    return NULL;
}

/* Pre-scan source for struct definitions: struct Name { fields }; */
static void collect_struct_defs(const char *source) {
    s_num_structs = 0;
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

            if (s_num_structs >= MAX_STRUCT_DEFS) { p++; continue; }
            struct_def_t *sd = &s_structs[s_num_structs];
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

            s_num_structs++;
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
static void flatten_struct_to_uniforms_ex(const char *flat_prefix, const char *gles_prefix,
                                           const char *struct_name,
                                           glslt_uniform_t *uniforms, int *nu,
                                           glslt_sampler_t *samplers, int *ns) {
    const struct_def_t *sd = find_struct_def(struct_name);
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
                    flatten_struct_to_uniforms_ex(flat_arr, gles_arr, sf->type_name,
                                                  uniforms, nu, samplers, ns);
                }
            } else {
                /* Single nested struct */
                flatten_struct_to_uniforms_ex(flat_name, gles_name, sf->type_name,
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
                for (int a = 0; a < sf->array_size && s_num_replacements < MAX_STRUCT_REPLS; a++) {
                    char old_ref[GLSLT_MAX_NAME * 4], new_ref[GLSLT_MAX_NAME * 4];
                    snprintf(old_ref, sizeof(old_ref), "%s[%d]", gles_name, a);
                    snprintf(new_ref, sizeof(new_ref), "%s_%d", flat_name, a);
                    strncpy(s_replacements[s_num_replacements].old_ref, old_ref,
                            sizeof(s_replacements[0].old_ref) - 1);
                    strncpy(s_replacements[s_num_replacements].new_ref, new_ref,
                            sizeof(s_replacements[0].new_ref) - 1);
                    s_num_replacements++;
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
            if (s_num_replacements < MAX_STRUCT_REPLS) {
                strncpy(s_replacements[s_num_replacements].old_ref, gles_name,
                        sizeof(s_replacements[0].old_ref) - 1);
                strncpy(s_replacements[s_num_replacements].new_ref, flat_name,
                        sizeof(s_replacements[0].new_ref) - 1);
                s_num_replacements++;
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
            if (s_num_replacements < MAX_STRUCT_REPLS) {
                strncpy(s_replacements[s_num_replacements].old_ref, gles_name,
                        sizeof(s_replacements[0].old_ref) - 1);
                strncpy(s_replacements[s_num_replacements].new_ref, flat_name,
                        sizeof(s_replacements[0].new_ref) - 1);
                s_num_replacements++;
            }
        }
    }
}

/* Convenience wrapper: flatten with same prefix for both flat and gles names.
 * gles_prefix uses dot notation: "prefix.field" */
static void flatten_struct_to_uniforms(const char *prefix, const char *struct_name,
                                       glslt_uniform_t *uniforms, int *nu,
                                       glslt_sampler_t *samplers, int *ns) {
    flatten_struct_to_uniforms_ex(prefix, prefix, struct_name, uniforms, nu, samplers, ns);
}

/* Apply all struct member replacements to a line.
 * Handles "val.a" → "val_a" with left-side identifier boundary checks. */
static void apply_struct_replacements(const char *input, char *out, int out_size) {
    char b1[MAX_LINE_LEN], b2[MAX_LINE_LEN];
    const char *src = input;
    char *dst;

    for (int i = 0; i < s_num_replacements; i++) {
        dst = (i % 2 == 0) ? b1 : b2;
        int old_len = (int)strlen(s_replacements[i].old_ref);
        int new_len = (int)strlen(s_replacements[i].new_ref);
        const char *p = src;
        char *o = dst;
        char *end = dst + MAX_LINE_LEN - 1;

        while (*p && o < end) {
            if (strncmp(p, s_replacements[i].old_ref, old_len) == 0) {
                /* Check left boundary: must not be preceded by ident char */
                int left_ok = (p == src) || !is_ident_char(*(p - 1));
                /* Check right boundary: must not be followed by ident char
                 * (but '.' is OK — it means further member access like swizzle) */
                int right_ok = !is_ident_char(*(p + old_len));
                if (left_ok && right_ok) {
                    if (o + new_len >= end) break;
                    memcpy(o, s_replacements[i].new_ref, new_len);
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

    if (s_num_replacements == 0) {
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

#define GLSLT_MAX_DEFINES 64

typedef struct {
    char name[GLSLT_MAX_NAME];
    int  value;
} glslt_define_t;

typedef struct {
    glslt_define_t entries[GLSLT_MAX_DEFINES];
    int count;
} glslt_define_table_t;

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
static const glslt_define_table_t *s_current_defines = NULL;

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
static int parse_type_and_names(const char *p, parsed_decl_t *decl) {
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
                if (s_current_defines) {
                    int resolved = glslt_resolve_define(s_current_defines, macro_name);
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
                    if (s_current_defines) {
                        int resolved = glslt_resolve_define(s_current_defines, macro_name);
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
static decl_kind_t parse_line(const char *raw_line, parsed_decl_t *decl) {
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
        if (parse_type_and_names(p, decl)) {
            decl->kind = DECL_ATTRIBUTE;
            return DECL_ATTRIBUTE;
        }
    }

    /* varying or invariant varying */
    if (starts_with_word(p, "invariant")) {
        const char *ip = skip_ws(p + 9);
        if (starts_with_word(ip, "varying")) {
            p = skip_ws(ip + 7);
            if (parse_type_and_names(p, decl)) {
                decl->kind = DECL_VARYING;
                return DECL_VARYING;
            }
        }
    }
    if (starts_with_word(p, "varying")) {
        p = skip_ws(p + 7);
        if (parse_type_and_names(p, decl)) {
            decl->kind = DECL_VARYING;
            return DECL_VARYING;
        }
    }

    /* uniform */
    if (starts_with_word(p, "uniform")) {
        p = skip_ws(p + 7);
        if (parse_type_and_names(p, decl)) {
            decl->kind = DECL_UNIFORM;
            return DECL_UNIFORM;
        }
        /* parse_type_and_names failed — check if it's a struct uniform */
        {
            const char *sp = skip_precision(p);
            char type_str[GLSLT_MAX_NAME];
            const char *after_type = read_word(sp, type_str, sizeof(type_str));
            if (after_type != sp && find_struct_def(type_str)) {
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
                            if (s_current_defines) {
                                int resolved = glslt_resolve_define(s_current_defines, macro_name);
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
                                if (s_current_defines) {
                                    int resolved = glslt_resolve_define(s_current_defines, macro_name2);
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

/* Check if name (of length len) is a GLSL built-in type name */
static int is_glsl_type_name(const char *name, int len) {
    static const char *types[] = {
        "bool", "int", "float", "void",
        "vec2", "vec3", "vec4",
        "ivec2", "ivec3", "ivec4",
        "bvec2", "bvec3", "bvec4",
        "mat2", "mat3", "mat4",
        "sampler2D", "samplerCube",
        NULL
    };
    for (int i = 0; types[i]; i++)
        if ((int)strlen(types[i]) == len && strncmp(name, types[i], len) == 0)
            return 1;
    return 0;
}

/* Check if name (of length len) is a GLSL ES 1.00 built-in function.
 * Constant expressions may include built-in function calls per §5.10. */
static int is_glsl_builtin_func(const char *name, int len) {
    static const char *funcs[] = {
        "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan",
        "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt",
        "abs", "sign", "floor", "ceil", "fract", "mod", "min", "max",
        "clamp", "mix", "step", "smoothstep",
        "length", "distance", "dot", "cross", "normalize",
        "faceforward", "reflect", "refract",
        "matrixCompMult",
        "lessThan", "lessThanEqual", "greaterThan", "greaterThanEqual",
        "equal", "notEqual", "any", "all", "not",
        NULL
    };
    for (int i = 0; funcs[i]; i++)
        if ((int)strlen(funcs[i]) == len && strncmp(name, funcs[i], len) == 0)
            return 1;
    return 0;
}

/* Scan an expression for non-constant identifier references.
 * Returns pointer to first non-const identifier, or NULL if all OK. */
static const char *find_nonconst_in_expr(const char *expr, int expr_len,
                                          char const_names[][64], int num_consts) {
    const char *p = expr;
    const char *end = expr + expr_len;

    while (p < end) {
        /* Skip non-identifier characters; handle '.' as member access */
        if (!is_ident_char(*p)) {
            if (*p == '.') {
                p++;
                /* Skip member/swizzle name (e.g. .xyz, .field) */
                while (p < end && is_ident_char(*p)) p++;
            } else {
                p++;
            }
            continue;
        }

        /* Numeric literal starting with digit — skip */
        if (*p >= '0' && *p <= '9') {
            while (p < end && (is_ident_char(*p) || *p == '.')) p++;
            continue;
        }

        /* Identifier */
        const char *id = p;
        while (p < end && is_ident_char(*p)) p++;
        int id_len = (int)(p - id);

        /* true/false */
        if ((id_len == 4 && strncmp(id, "true", 4) == 0) ||
            (id_len == 5 && strncmp(id, "false", 5) == 0))
            continue;

        /* Built-in type name (constructor) */
        if (is_glsl_type_name(id, id_len))
            continue;

        /* gl_Max* built-in constants */
        if (id_len > 6 && strncmp(id, "gl_Max", 6) == 0)
            continue;

        /* gl_DepthRange built-in struct */
        if (id_len == 12 && strncmp(id, "gl_DepthRange", 12) == 0)
            continue;

        /* Known const variable */
        int found = 0;
        for (int i = 0; i < num_consts; i++) {
            if ((int)strlen(const_names[i]) == id_len &&
                strncmp(const_names[i], id, id_len) == 0) {
                found = 1;
                break;
            }
        }
        if (found) continue;

        /* Check if followed by '(' — function call */
        const char *q = p;
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        if (q < end && *q == '(') {
            if (is_glsl_builtin_func(id, id_len) || is_glsl_type_name(id, id_len))
                continue;
            /* Check struct constructor: S(...) is a constant expression per ES 1.00 §5.10
             * if all arguments are constant. find_struct_def needs collect_struct_defs
             * to have been called first (done in glslt_validate_es100 before this). */
            {
                char tmp[64];
                int tl = id_len < 63 ? id_len : 63;
                memcpy(tmp, id, tl); tmp[tl] = '\0';
                if (find_struct_def(tmp)) continue;
            }
            return id; /* User function call — not constant */
        }

        /* Unknown non-const identifier */
        return id;
    }

    return NULL;
}

/* Validate that local const variables are initialized from constant expressions
 * per GLES 1.00 §5.10. Source must be normalized (one statement per line).
 * Returns 1 if valid, 0 if invalid. */
static int validate_const_initializers(const char *source,
                                        char *error, int error_size) {
    char const_names[128][64];
    int num_consts = 0;
    int brace_depth = 0;
    int paren_depth = 0;
    int in_block_comment = 0;

    const char *line = source;
    while (*line) {
        const char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - line);

        /* Handle ongoing block comment */
        if (in_block_comment) {
            const char *close = NULL;
            for (const char *s = line; s < line + line_len - 1; s++) {
                if (s[0] == '*' && s[1] == '/') { close = s; break; }
            }
            if (close) {
                in_block_comment = 0;
                /* Track braces in rest of line after comment close */
                for (const char *b = close + 2; b < line + line_len; b++) {
                    if (*b == '{') brace_depth++;
                    else if (*b == '}') { if (brace_depth > 0) brace_depth--; }
                    else if (*b == '(') paren_depth++;
                    else if (*b == ')') { if (paren_depth > 0) paren_depth--; }
                }
            }
            line = (*eol) ? eol + 1 : eol;
            continue;
        }

        /* Find effective start (skip leading whitespace) */
        const char *p = line;
        while (p < line + line_len && (*p == ' ' || *p == '\t')) p++;
        if (p >= line + line_len) { line = (*eol) ? eol + 1 : eol; continue; }

        /* Skip preprocessor directives */
        if (*p == '#') { line = (*eol) ? eol + 1 : eol; continue; }

        /* Check for line comment */
        const char *lc = NULL;
        for (const char *s = p; s < line + line_len - 1; s++) {
            if (s[0] == '/' && s[1] == '/') { lc = s; break; }
        }
        int effective_len = lc ? (int)(lc - line) : line_len;

        /* Check for block comment start */
        for (const char *s = p; s < line + effective_len - 1; s++) {
            if (s[0] == '/' && s[1] == '*') {
                const char *close = NULL;
                for (const char *t = s + 2; t < line + effective_len - 1; t++) {
                    if (t[0] == '*' && t[1] == '/') { close = t; break; }
                }
                if (!close) {
                    in_block_comment = 1;
                    effective_len = (int)(s - line);
                } else {
                    /* Single-line block comment — just ignore for simplicity,
                     * the brace tracking below still scans the full line */
                }
                break;
            }
        }

        /* Track braces and parens (for function parameters vs declarations) */
        for (const char *b = line; b < line + effective_len; b++) {
            if (*b == '{') brace_depth++;
            else if (*b == '}') { if (brace_depth > 0) brace_depth--; }
            else if (*b == '(') paren_depth++;
            else if (*b == ')') { if (paren_depth > 0) paren_depth--; }
        }

        /* Only process const declarations when not inside parentheses
         * (avoids matching `const` in function parameters) */
        if (paren_depth == 0 && starts_with_word(p, "const")) {
            const char *c = p + 5;
            while (*c == ' ' || *c == '\t') c++;

            /* Skip type */
            const char *type_start = c;
            while (c < line + effective_len && is_ident_char(*c)) c++;
            if (c == type_start) goto next_line;
            while (*c == ' ' || *c == '\t') c++;

            /* Get variable name */
            const char *name_start = c;
            while (c < line + effective_len && is_ident_char(*c)) c++;
            int name_len = (int)(c - name_start);
            if (name_len == 0 || name_len >= 64) goto next_line;

            while (*c == ' ' || *c == '\t') c++;

            if (*c == '=' && brace_depth > 0) {
                c++; /* skip = */
                /* Find semicolon */
                const char *semi = c;
                while (semi < line + effective_len && *semi != ';') semi++;

                if (semi < line + effective_len) {
                    int expr_len = (int)(semi - c);
                    const char *bad = find_nonconst_in_expr(
                        c, expr_len, const_names, num_consts);
                    if (bad) {
                        snprintf(error, error_size,
                                 "'const' variable initializer must be a "
                                 "constant expression");
                        return 0;
                    }
                }
            }

            /* Add name to known consts (valid for subsequent declarations) */
            if (num_consts < 128) {
                strncpy(const_names[num_consts], name_start, name_len);
                const_names[num_consts][name_len] = '\0';
                num_consts++;
            }
        }

    next_line:
        line = (*eol) ? eol + 1 : eol;
    }

    return 1;
}

/* Validate GLES 1.00 rules that are stricter than GLSL 4.60.
 * Must run on normalized source before transpilation.
 * Returns 1 if valid, 0 if invalid (error message written). */
static int validate_gles_semantics(const char *source, glslt_stage_t stage,
                                    char *error, int error_size) {
    int brace_depth = 0;
    int in_block_comment = 0;
    int in_line_comment = 0;
    int has_frag_color_write = 0;
    int has_frag_data_write = 0;
    const char *p = source;

    while (*p) {
        /* Reset line comment on newline */
        if (*p == '\n') { in_line_comment = 0; p++; continue; }

        /* Skip line comments */
        if (in_line_comment) { p++; continue; }

        /* Handle block comments */
        if (in_block_comment) {
            if (p[0] == '*' && p[1] == '/') { in_block_comment = 0; p += 2; continue; }
            p++; continue;
        }

        /* Detect comment starts */
        if (p[0] == '/' && p[1] == '/') { in_line_comment = 1; p += 2; continue; }
        if (p[0] == '/' && p[1] == '*') { in_block_comment = 1; p += 2; continue; }

        /* Skip string literals */
        if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\') p++; p++; } if (*p) p++; continue; }

        /* Skip preprocessor directives (they're valid anywhere) */
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        /* Track braces */
        if (*p == '{') { brace_depth++; p++; continue; }
        if (*p == '}') { if (brace_depth > 0) brace_depth--; p++; continue; }

        /* GLES 1.00 §5.9: Reserved operators must cause compile error.
         * These are valid in desktop GLSL but not in ES 1.00. */
        if (*p == '%' && p[1] != '=') {
            /* % (modulus) — not %= (handled below) */
            snprintf(error, error_size, "reserved operator '%%' in GLSL ES 1.00");
            return 0;
        }
        if (*p == '~') {
            snprintf(error, error_size, "reserved operator '~' in GLSL ES 1.00");
            return 0;
        }
        if (*p == '^') {
            if (p[1] == '^') { p += 2; continue; } /* ^^ logical XOR, allowed — skip both */
            if (p[1] == '=') { snprintf(error, error_size, "reserved operator '^=' in GLSL ES 1.00"); return 0; }
            snprintf(error, error_size, "reserved operator '^' in GLSL ES 1.00");
            return 0;
        }
        if (*p == '&') {
            if (p[1] == '&') { p += 2; continue; } /* && logical AND, allowed — skip both */
            if (p[1] == '=') { snprintf(error, error_size, "reserved operator '&=' in GLSL ES 1.00"); return 0; }
            snprintf(error, error_size, "reserved operator '&' in GLSL ES 1.00");
            return 0;
        }
        if (*p == '|') {
            if (p[1] == '|') { p += 2; continue; } /* || logical OR, allowed — skip both */
            if (p[1] == '=') { snprintf(error, error_size, "reserved operator '|=' in GLSL ES 1.00"); return 0; }
            snprintf(error, error_size, "reserved operator '|' in GLSL ES 1.00");
            return 0;
        }
        /* Two-char reserved operators */
        if (*p == '<' && p[1] == '<') {
            snprintf(error, error_size, "reserved operator '<<' in GLSL ES 1.00");
            return 0;
        }
        if (*p == '>' && p[1] == '>') {
            snprintf(error, error_size, "reserved operator '>>' in GLSL ES 1.00");
            return 0;
        }
        /* Assignment variants: %= is the only one not already caught above
         * (&=, |=, ^= handled in the &/|/^ blocks) */
        if (*p == '%' && p[1] == '=') {
            snprintf(error, error_size, "reserved operator '%%=' in GLSL ES 1.00");
            return 0;
        }
        if ((*p == '<' && p[1] == '<' && p[2] == '=') ||
            (*p == '>' && p[1] == '>' && p[2] == '=')) {
            snprintf(error, error_size, "reserved operator '%c%c=' in GLSL ES 1.00", p[0], p[1]);
            return 0;
        }

        /* Only check at start of identifiers (word boundary) */
        if (!is_ident_char(*p) || (p > source && is_ident_char(*(p - 1)))) {
            p++; continue;
        }

        /* At word boundary — check for storage qualifier keywords */
        if (starts_with_word(p, "attribute")) {
            if (stage == GLSLT_FRAGMENT) {
                snprintf(error, error_size,
                         "'attribute' qualifier not allowed in fragment shader");
                return 0;
            }
            if (brace_depth > 0) {
                snprintf(error, error_size,
                         "'attribute' cannot be declared inside a function");
                return 0;
            }
        }
        if (starts_with_word(p, "varying")) {
            if (brace_depth > 0) {
                snprintf(error, error_size,
                         "'varying' cannot be declared inside a function");
                return 0;
            }
            /* GLES 1.00: varyings cannot have struct type (§4.3.5) */
            const char *after_v = p + 7; /* skip "varying" */
            while (*after_v == ' ' || *after_v == '\t') after_v++;
            if (starts_with_word(after_v, "struct")) {
                snprintf(error, error_size,
                         "struct type not allowed for varying");
                return 0;
            }
        }
        if (starts_with_word(p, "uniform") && brace_depth > 0) {
            snprintf(error, error_size,
                     "'uniform' cannot be declared inside a function");
            return 0;
        }

        /* Track gl_FragColor / gl_FragData usage (GLES2 §3.9.2:
         * cannot statically write to both in the same shader) */
        if (stage == GLSLT_FRAGMENT) {
            if (starts_with_word(p, "gl_FragColor"))
                has_frag_color_write = 1;
            else if (starts_with_word(p, "gl_FragData"))
                has_frag_data_write = 1;
        }

        /* Skip over identifier */
        while (*p && is_ident_char(*p)) p++;
    }

    /* GLES2 §3.9.2: shader must not statically write to both gl_FragColor
     * and gl_FragData (even in dead code or unused functions) */
    if (has_frag_color_write && has_frag_data_write) {
        snprintf(error, error_size,
                 "cannot write to both gl_FragColor and gl_FragData");
        return 0;
    }

    return 1; /* valid */
}

/* Validate GLES 1.00 preprocessor directives (§3.4):
 * - #version must be first non-whitespace/non-comment line
 * - #version must be exactly 100
 * - #error must cause compile failure */
static int validate_preprocessor_directives(const char *source,
                                             char *error, int error_size) {
    int in_block_comment = 0;
    int found_noncomment_line = 0;  /* Have we seen a non-whitespace/non-comment line? */
    (void)0;  /* found_version tracking done inline */
    const char *lp = source;

    while (*lp) {
        /* Extract line */
        const char *eol = lp;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - lp);

        /* Check block comment state */
        const char *p = lp;
        const char *line_end = lp + line_len;

        /* Process block comments and find non-whitespace content */
        int has_content = 0;
        int is_preprocessor = 0;
        const char *pp_start = NULL;

        while (p < line_end) {
            if (in_block_comment) {
                if (p + 1 < line_end && p[0] == '*' && p[1] == '/') {
                    in_block_comment = 0;
                    p += 2;
                } else {
                    p++;
                }
                continue;
            }
            if (p + 1 < line_end && p[0] == '/' && p[1] == '*') {
                in_block_comment = 1;
                p += 2;
                continue;
            }
            if (p + 1 < line_end && p[0] == '/' && p[1] == '/') {
                break;  /* Rest of line is comment */
            }
            if (*p != ' ' && *p != '\t' && *p != '\r') {
                has_content = 1;
                if (*p == '#' && !pp_start) {
                    is_preprocessor = 1;
                    pp_start = p;
                }
            }
            p++;
        }

        if (has_content && is_preprocessor && pp_start) {
            const char *dp = pp_start + 1;
            while (*dp == ' ' || *dp == '\t') dp++;

            /* Check #error directive — must cause compile failure */
            if (strncmp(dp, "error", 5) == 0 && !is_ident_char(dp[5])) {
                snprintf(error, error_size, "#error directive");
                return 0;
            }

            /* Check #version directive */
            if (strncmp(dp, "version", 7) == 0 && !is_ident_char(dp[7])) {
                if (found_noncomment_line) {
                    snprintf(error, error_size,
                             "#version must be the first statement in a shader");
                    return 0;
                }
                const char *vp = dp + 7;
                while (*vp == ' ' || *vp == '\t') vp++;
                /* Must have a numeric version number */
                if (*vp < '0' || *vp > '9') {
                    snprintf(error, error_size,
                             "invalid #version directive");
                    return 0;
                }
                int version_num = 0;
                while (*vp >= '0' && *vp <= '9') {
                    version_num = version_num * 10 + (*vp - '0');
                    vp++;
                }
                /* Check for invalid tokens after version number (e.g. "100.0", "100 foobar") */
                if (*vp == '.') {
                    snprintf(error, error_size,
                             "invalid #version directive (float literal)");
                    return 0;
                }
                /* Skip whitespace, check for extra tokens */
                while (*vp == ' ' || *vp == '\t') vp++;
                if (*vp && *vp != '\n' && *vp != '\r' &&
                    !(vp[0] == '/' && (vp[1] == '/' || vp[1] == '*'))) {
                    snprintf(error, error_size,
                             "extra tokens after #version %d", version_num);
                    return 0;
                }
                /* Version must be exactly 100 */
                if (version_num != 100) {
                    snprintf(error, error_size,
                             "unsupported GLSL version %d (expected 100)", version_num);
                    return 0;
                }
            }
        }

        if (has_content && !in_block_comment) {
            found_noncomment_line = 1;
        }

        lp = (*eol) ? eol + 1 : eol;
    }

    return 1;
}

/* Validate GLES 1.00 §3.4: undefined identifiers in #if/#elif are errors.
 * Tracks #define/#undef and checks that all identifiers in preprocessor
 * conditional expressions are defined (except as arguments to 'defined').
 * Handles short-circuit: "NONZERO || rest" and "ZERO && rest" skip rest. */
static int validate_preprocessor_undefined(const char *source,
                                            char *error, int error_size) {
    typedef struct { char name[64]; int value; } ppdef_t;
    ppdef_t defs[256];
    int ndefs = 0;
    int in_block_comment = 0;

    /* Pre-define built-in macros */
    strcpy(defs[ndefs].name, "GL_ES"); defs[ndefs].value = 1; ndefs++;
    strcpy(defs[ndefs].name, "__VERSION__"); defs[ndefs].value = 100; ndefs++;
    strcpy(defs[ndefs].name, "__LINE__"); defs[ndefs].value = 1; ndefs++;
    strcpy(defs[ndefs].name, "__FILE__"); defs[ndefs].value = 0; ndefs++;

    const char *line = source;
    while (*line) {
        const char *eol = line;
        while (*eol && *eol != '\n') eol++;

        /* Handle block comments spanning lines */
        if (in_block_comment) {
            for (const char *c = line; c < eol - 1; c++) {
                if (c[0] == '*' && c[1] == '/') { in_block_comment = 0; break; }
            }
            line = (*eol) ? eol + 1 : eol;
            continue;
        }

        /* Check for block comment start on this line */
        for (const char *c = line; c < eol - 1; c++) {
            if (c[0] == '/' && c[1] == '/') break;  /* line comment — stop */
            if (c[0] == '/' && c[1] == '*') {
                /* Check if closed on same line */
                int closed = 0;
                for (const char *d = c + 2; d < eol - 1; d++) {
                    if (d[0] == '*' && d[1] == '/') { closed = 1; break; }
                }
                if (!closed) in_block_comment = 1;
                break;
            }
        }
        if (in_block_comment) { line = (*eol) ? eol + 1 : eol; continue; }

        const char *p = line;
        while (p < eol && (*p == ' ' || *p == '\t')) p++;
        if (p >= eol || *p != '#') { line = (*eol) ? eol + 1 : eol; continue; }
        p++;
        while (p < eol && (*p == ' ' || *p == '\t')) p++;

        if (strncmp(p, "define", 6) == 0 && (p + 6 >= eol || !is_ident_char(p[6]))) {
            p += 6;
            while (p < eol && (*p == ' ' || *p == '\t')) p++;
            const char *ns = p;
            while (p < eol && is_ident_char(*p)) p++;
            int nl = (int)(p - ns);
            if (nl > 0 && nl < 64 && ndefs < 256) {
                while (p < eol && (*p == ' ' || *p == '\t')) p++;
                /* Skip function-like macro parens: #define FOO(x) ... */
                if (p < eol && *p == '(') {
                    while (p < eol && *p != ')') p++;
                    if (p < eol) p++;
                    while (p < eol && (*p == ' ' || *p == '\t')) p++;
                }
                int val = 1;
                if (p < eol && ((*p >= '0' && *p <= '9') || *p == '-')) {
                    val = 0; int neg = 0;
                    if (*p == '-') { neg = 1; p++; }
                    while (p < eol && *p >= '0' && *p <= '9')
                        val = val * 10 + (*p++ - '0');
                    if (neg) val = -val;
                }
                int found = -1;
                for (int i = 0; i < ndefs; i++) {
                    if ((int)strlen(defs[i].name) == nl &&
                        strncmp(defs[i].name, ns, nl) == 0) { found = i; break; }
                }
                if (found >= 0) { defs[found].value = val; }
                else {
                    strncpy(defs[ndefs].name, ns, nl);
                    defs[ndefs].name[nl] = '\0';
                    defs[ndefs].value = val;
                    ndefs++;
                }
            }
        }
        else if (strncmp(p, "undef", 5) == 0 && (p + 5 >= eol || !is_ident_char(p[5]))) {
            p += 5;
            while (p < eol && (*p == ' ' || *p == '\t')) p++;
            const char *ns = p;
            while (p < eol && is_ident_char(*p)) p++;
            int nl = (int)(p - ns);
            for (int i = 0; i < ndefs; i++) {
                if ((int)strlen(defs[i].name) == nl &&
                    strncmp(defs[i].name, ns, nl) == 0) {
                    defs[i] = defs[--ndefs];
                    break;
                }
            }
        }
        else if ((strncmp(p, "if", 2) == 0 && (p + 2 >= eol || !is_ident_char(p[2]))) ||
                 (strncmp(p, "elif", 4) == 0 && (p + 4 >= eol || !is_ident_char(p[4])))) {
            p += (p[0] == 'e') ? 4 : 2;
            while (p < eol && (*p == ' ' || *p == '\t')) p++;

            const char *expr_end = eol;
            for (const char *c = p; c < eol; c++) {
                if (c + 1 < eol && c[0] == '/' && (c[1] == '/' || c[1] == '*'))
                    { expr_end = c; break; }
            }

            /* Simple short-circuit: "LITERAL || rest" (nonzero) or "LITERAL && rest" (zero) */
            const char *scan_end = expr_end;
            const char *sp = p;
            while (sp < expr_end && (*sp == ' ' || *sp == '\t')) sp++;
            if (sp < expr_end && sp[0] >= '0' && sp[0] <= '9') {
                int lit = 0;
                const char *lp = sp;
                while (lp < expr_end && *lp >= '0' && *lp <= '9')
                    lit = lit * 10 + (*lp++ - '0');
                const char *ap = lp;
                while (ap < expr_end && (*ap == ' ' || *ap == '\t')) ap++;
                if (ap + 1 < expr_end && ap[0] == '|' && ap[1] == '|' && lit != 0)
                    scan_end = ap;
                else if (ap + 1 < expr_end && ap[0] == '&' && ap[1] == '&' && lit == 0)
                    scan_end = ap;
            }

            /* Scan for undefined identifiers */
            const char *s = p;
            while (s < scan_end) {
                if (!(isalpha((unsigned char)*s) || *s == '_')) { s++; continue; }
                const char *is = s;
                while (s < scan_end && is_ident_char(*s)) s++;
                int il = (int)(s - is);

                /* Skip 'defined' keyword and its argument */
                if (il == 7 && strncmp(is, "defined", 7) == 0) {
                    while (s < scan_end && (*s == ' ' || *s == '\t')) s++;
                    if (s < scan_end && *s == '(') {
                        s++;
                        while (s < scan_end && (*s == ' ' || *s == '\t')) s++;
                        while (s < scan_end && is_ident_char(*s)) s++;
                        while (s < scan_end && (*s == ' ' || *s == '\t')) s++;
                        if (s < scan_end && *s == ')') s++;
                    } else {
                        while (s < scan_end && is_ident_char(*s)) s++;
                    }
                    continue;
                }

                int found = 0;
                for (int i = 0; i < ndefs; i++) {
                    if ((int)strlen(defs[i].name) == il &&
                        strncmp(defs[i].name, is, il) == 0) { found = 1; break; }
                }
                if (!found) {
                    char buf[64];
                    int cl = il < 63 ? il : 63;
                    strncpy(buf, is, cl); buf[cl] = '\0';
                    snprintf(error, error_size,
                             "undefined identifier '%s' in preprocessor expression", buf);
                    return 0;
                }
            }
        }

        line = (*eol) ? eol + 1 : eol;
    }
    return 1;
}

/* Validate GLES 1.00 qualification order (§4.5 / §4.3 / §4.6).
 * Variables: invariant → storage(attribute/varying/uniform) → precision → type
 * Parameters: storage(const) → parameter(in/out/inout) → precision → type
 * Any reordering of qualifier groups is a compile error. */
static int validate_qualification_order(const char *source,
                                         char *error, int error_size) {
    int brace_depth = 0;
    int paren_depth = 0;
    int in_comment = 0;  /* 0=none, 1=line, 2=block */
    int last_qc = -1;    /* last qualifier class seen */
    int in_params = 0;   /* inside function parameter list at global scope */

    const char *p = source;
    while (*p) {
        if (*p == '\n') {
            if (in_comment == 1) in_comment = 0;
            p++; continue;
        }
        if (in_comment == 1) { p++; continue; }
        if (in_comment == 2) {
            if (p[0] == '*' && p[1] == '/') { in_comment = 0; p += 2; }
            else p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '/') { in_comment = 1; p += 2; continue; }
        if (p[0] == '/' && p[1] == '*') { in_comment = 2; p += 2; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        if (*p == '(') {
            if (brace_depth == 0) { in_params = 1; last_qc = -1; }
            paren_depth++; p++; continue;
        }
        if (*p == ')') {
            if (paren_depth > 0) paren_depth--;
            if (paren_depth == 0) { in_params = 0; last_qc = -1; }
            p++; continue;
        }
        if (*p == '{') { brace_depth++; last_qc = -1; p++; continue; }
        if (*p == '}') { if (brace_depth > 0) brace_depth--; last_qc = -1; p++; continue; }
        if (*p == ';') { last_qc = -1; p++; continue; }
        if (*p == ',' && in_params) { last_qc = -1; p++; continue; }

        if (!(isalpha((unsigned char)*p) || *p == '_')) { p++; continue; }
        if (p > source && is_ident_char(*(p - 1))) { p++; continue; }

        const char *w = p;
        while (*p && is_ident_char(*p)) p++;
        int wl = (int)(p - w);

        /* Only check at global scope (not inside function bodies) */
        if (brace_depth > 0) continue;

        int qc = -1;
        if (!in_params) {
            /* Global variable: invariant=0, storage=1, precision=2 */
            if (wl == 9 && strncmp(w, "invariant", 9) == 0) qc = 0;
            else if (wl == 9 && strncmp(w, "attribute", 9) == 0) qc = 1;
            else if (wl == 7 && strncmp(w, "varying", 7) == 0) qc = 1;
            else if (wl == 7 && strncmp(w, "uniform", 7) == 0) qc = 1;
            else if (wl == 4 && strncmp(w, "lowp", 4) == 0) qc = 2;
            else if (wl == 7 && strncmp(w, "mediump", 7) == 0) qc = 2;
            else if (wl == 5 && strncmp(w, "highp", 5) == 0) qc = 2;
        } else {
            /* Function parameter: storage(const)=0, param(in/out/inout)=1, precision=2 */
            if (wl == 5 && strncmp(w, "const", 5) == 0) qc = 0;
            else if (wl == 5 && strncmp(w, "inout", 5) == 0) qc = 1;
            else if (wl == 3 && strncmp(w, "out", 3) == 0) qc = 1;
            else if (wl == 2 && strncmp(w, "in", 2) == 0) qc = 1;
            else if (wl == 4 && strncmp(w, "lowp", 4) == 0) qc = 2;
            else if (wl == 7 && strncmp(w, "mediump", 7) == 0) qc = 2;
            else if (wl == 5 && strncmp(w, "highp", 5) == 0) qc = 2;
        }

        if (qc >= 0) {
            if (qc < last_qc) {
                char qb[32];
                int cl = wl < 31 ? wl : 31;
                strncpy(qb, w, cl); qb[cl] = '\0';
                snprintf(error, error_size,
                         "incorrect qualification order: '%s' cannot appear after "
                         "a higher-precedence qualifier", qb);
                return 0;
            }
            last_qc = qc;
        } else {
            last_qc = -1;  /* non-qualifier word → reset */
        }
    }
    return 1;
}

/* Validate GLES 1.00 §8.7 texture function stage restrictions:
 * - texture2D/textureCube with bias (3 args) → fragment only
 * - texture2DLod/textureCubeLod → vertex only
 * - texture2DProj with bias → fragment only
 * - texture2DProjLod → vertex only */
static int validate_texture_functions(const char *source, glslt_stage_t stage,
                                       char *error, int error_size) {
    int in_comment = 0;
    const char *p = source;

    while (*p) {
        if (*p == '\n') { if (in_comment == 1) in_comment = 0; p++; continue; }
        if (in_comment == 1) { p++; continue; }
        if (in_comment == 2) {
            if (p[0] == '*' && p[1] == '/') { in_comment = 0; p += 2; }
            else p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '/') { in_comment = 1; p += 2; continue; }
        if (p[0] == '/' && p[1] == '*') { in_comment = 2; p += 2; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        if (!(isalpha((unsigned char)*p) || *p == '_') ||
            (p > source && is_ident_char(*(p - 1)))) { p++; continue; }

        const char *w = p;
        while (*p && is_ident_char(*p)) p++;
        int wl = (int)(p - w);

        /* Skip to check if followed by '(' */
        const char *a = p;
        while (*a == ' ' || *a == '\t') a++;
        if (*a != '(') continue;

        if (stage == GLSLT_FRAGMENT) {
            /* Fragment: reject Lod variants (vertex-only) */
            if ((wl == 12 && strncmp(w, "texture2DLod", 12) == 0) ||
                (wl == 14 && strncmp(w, "textureCubeLod", 14) == 0) ||
                (wl == 16 && strncmp(w, "texture2DProjLod", 16) == 0)) {
                char nb[32];
                int cl = wl < 31 ? wl : 31;
                strncpy(nb, w, cl); nb[cl] = '\0';
                snprintf(error, error_size,
                         "'%s' is not available in fragment shader", nb);
                return 0;
            }
        }
        else if (stage == GLSLT_VERTEX) {
            /* Vertex: reject bias variants (3-arg texture2D/textureCube/texture2DProj) */
            if ((wl == 9 && strncmp(w, "texture2D", 9) == 0) ||
                (wl == 11 && strncmp(w, "textureCube", 11) == 0) ||
                (wl == 13 && strncmp(w, "texture2DProj", 13) == 0)) {
                /* Count commas at top paren level to determine arg count */
                const char *cp = a + 1;
                int depth = 1, commas = 0;
                while (*cp && depth > 0) {
                    if (*cp == '(') depth++;
                    else if (*cp == ')') depth--;
                    else if (*cp == ',' && depth == 1) commas++;
                    cp++;
                }
                if (commas >= 2) {
                    char nb[32];
                    int cl = wl < 31 ? wl : 31;
                    strncpy(nb, w, cl); nb[cl] = '\0';
                    snprintf(error, error_size,
                             "'%s' with bias parameter is not available in vertex shader", nb);
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Public API: validate GLES 1.00 semantics at compile time.
 * Normalizes source before validation (inserts newlines). */
int glslt_validate_es100(const char *source, glslt_stage_t stage,
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
        collect_struct_defs(norm);
        ok = validate_const_initializers(norm, error, error_size);
    }
    if (ok) {
        ok = validate_qualification_order(norm, error, error_size);
    }
    if (ok) {
        ok = validate_texture_functions(norm, stage, error, error_size);
    }
    free(norm);
    return ok;
}

/* ========================================================================== */
/*  Main transpile function                                                    */
/* ========================================================================== */

glslt_result_t glslt_transpile(const char *source, glslt_stage_t stage,
                               const glslt_options_t *opts) {
    glslt_result_t result;
    memset(&result, 0, sizeof(result));

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
    s_current_defines = &defines;

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
                memcpy(result.output, source, src_len + 1);
                result.output_len = src_len;

                result.success = 1;
                s_current_defines = NULL;
                return result;
            }
        }
    }

    /* ---- Preprocessor validation (on raw source, before normalization) ---- */
    if (!validate_preprocessor_directives(source, result.error, sizeof(result.error))) {
        s_current_defines = NULL;
        return result;
    }
    if (!validate_preprocessor_undefined(source, result.error, sizeof(result.error))) {
        s_current_defines = NULL;
        return result;
    }

    /* Normalize source: insert newlines after semicolons so each declaration
     * is on its own line (dEQP puts multiple decls on one line). */
    char *norm_source = normalize_source(source);
    if (!norm_source) {
        snprintf(result.error, sizeof(result.error), "out of memory normalizing source");
        s_current_defines = NULL;
        return result;
    }
    source = norm_source; /* Use normalized source for all subsequent processing */

    /* ---- GLES 1.00 semantic validation ---- */
    if (!validate_gles_semantics(source, stage, result.error, sizeof(result.error))) {
        free(norm_source);
        s_current_defines = NULL;
        return result;  /* result.success = 0 (from memset) */
    }
    if (!validate_const_initializers(source, result.error, sizeof(result.error))) {
        free(norm_source);
        s_current_defines = NULL;
        return result;
    }
    if (!validate_qualification_order(source, result.error, sizeof(result.error))) {
        free(norm_source);
        s_current_defines = NULL;
        return result;
    }
    if (!validate_texture_functions(source, stage, result.error, sizeof(result.error))) {
        free(norm_source);
        s_current_defines = NULL;
        return result;
    }

    /* ---- Pre-scan: collect struct definitions ---- */
    collect_struct_defs(source);
    s_num_replacements = 0;
    s_num_struct_array_uniforms = 0;

    /* ---- Detect gl_DepthRange usage and inject synthetic uniforms ---- */
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
        if (s_num_replacements + 3 <= MAX_STRUCT_REPLS) {
            strncpy(s_replacements[s_num_replacements].old_ref, "gl_DepthRange.near",
                    sizeof(s_replacements[0].old_ref) - 1);
            strncpy(s_replacements[s_num_replacements].new_ref, "sgl_dr_near",
                    sizeof(s_replacements[0].new_ref) - 1);
            s_num_replacements++;
            strncpy(s_replacements[s_num_replacements].old_ref, "gl_DepthRange.far",
                    sizeof(s_replacements[0].old_ref) - 1);
            strncpy(s_replacements[s_num_replacements].new_ref, "sgl_dr_far",
                    sizeof(s_replacements[0].new_ref) - 1);
            s_num_replacements++;
            strncpy(s_replacements[s_num_replacements].old_ref, "gl_DepthRange.diff",
                    sizeof(s_replacements[0].old_ref) - 1);
            strncpy(s_replacements[s_num_replacements].new_ref, "sgl_dr_diff",
                    sizeof(s_replacements[0].new_ref) - 1);
            s_num_replacements++;
        }
    }

    /* ---- Pass 1: Collect declarations ---- */

    glslt_uniform_t  uniforms[GLSLT_MAX_UNIFORMS];
    glslt_sampler_t  samplers[GLSLT_MAX_SAMPLERS];
    glslt_attribute_t attributes[GLSLT_MAX_ATTRIBUTES];
    glslt_varying_t  varyings[GLSLT_MAX_VARYINGS];
    int nu = 0, ns = 0, na = 0, nv = 0;
    int has_frag_color = 0;
    int max_frag_data = -1;  /* Highest gl_FragData[N] index seen (-1 = none) */
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
            decl_kind_t kind = parse_line(line, &decl);

            switch (kind) {
            case DECL_ATTRIBUTE:
                for (int i = 0; i < decl.num_names && na < GLSLT_MAX_ATTRIBUTES; i++) {
                    strncpy(attributes[na].name, decl.names[i], GLSLT_MAX_NAME - 1);
                    attributes[na].type = decl.type;
                    attributes[na].location = -1;
                    na++;
                }
                break;

            case DECL_VARYING:
                for (int i = 0; i < decl.num_names && nv < GLSLT_MAX_VARYINGS; i++) {
                    strncpy(varyings[nv].name, decl.names[i], GLSLT_MAX_NAME - 1);
                    varyings[nv].type = decl.type;
                    varyings[nv].location = -1;
                    varyings[nv].array_size = decl.array_sizes[i];
                    nv++;
                }
                break;

            case DECL_UNIFORM:
                for (int i = 0; i < decl.num_names; i++) {
                    if (decl.is_sampler) {
                        int arr = decl.array_sizes[i];
                        if (arr > 0) {
                            /* Sampler array: expand into individual entries */
                            for (int a = 0; a < arr && ns < GLSLT_MAX_SAMPLERS; a++) {
                                snprintf(samplers[ns].name, GLSLT_MAX_NAME, "%s_%d", decl.names[i], a);
                                snprintf(samplers[ns].gles_name, GLSLT_MAX_NAME, "%s[%d]", decl.names[i], a);
                                samplers[ns].type = decl.type;
                                samplers[ns].binding = -1;
                                samplers[ns].array_index = a;
                                samplers[ns].array_total = arr;
                                ns++;
                            }
                            /* Add body replacements: s[0] → s_0, s[1] → s_1 */
                            for (int a = 0; a < arr && s_num_replacements < MAX_STRUCT_REPLS; a++) {
                                char old_ref[GLSLT_MAX_NAME * 2];
                                char new_ref[GLSLT_MAX_NAME * 2];
                                snprintf(old_ref, sizeof(old_ref), "%s[%d]", decl.names[i], a);
                                snprintf(new_ref, sizeof(new_ref), "%s_%d", decl.names[i], a);
                                strncpy(s_replacements[s_num_replacements].old_ref, old_ref,
                                        sizeof(s_replacements[0].old_ref) - 1);
                                strncpy(s_replacements[s_num_replacements].new_ref, new_ref,
                                        sizeof(s_replacements[0].new_ref) - 1);
                                s_num_replacements++;
                            }
                        } else {
                            /* Single sampler */
                            if (ns < GLSLT_MAX_SAMPLERS) {
                                strncpy(samplers[ns].name, decl.names[i], GLSLT_MAX_NAME - 1);
                                strncpy(samplers[ns].gles_name, decl.names[i], GLSLT_MAX_NAME - 1);
                                samplers[ns].type = decl.type;
                                samplers[ns].binding = -1;
                                samplers[ns].array_index = -1;
                                samplers[ns].array_total = 0;
                                ns++;
                            }
                        }
                    } else {
                        if (nu < GLSLT_MAX_UNIFORMS) {
                            strncpy(uniforms[nu].name, decl.names[i], GLSLT_MAX_NAME - 1);
                            /* For non-struct uniforms, gles_name = name */
                            strncpy(uniforms[nu].gles_name, decl.names[i], GLSLT_MAX_NAME - 1);
                            uniforms[nu].type = decl.type;
                            uniforms[nu].array_size = decl.array_sizes[i];
                            uniforms[nu].binding = -1;
                            uniforms[nu].offset = 0;
                            uniforms[nu].size = 0;
                            nu++;
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
                        if (s_num_struct_array_uniforms < MAX_STRUCT_ARRAY_UNIFORMS) {
                            struct_array_uniform_t *sau = &s_struct_array_uniforms[s_num_struct_array_uniforms++];
                            strncpy(sau->struct_type, decl.struct_type_name, GLSLT_MAX_NAME - 1);
                            sau->struct_type[GLSLT_MAX_NAME - 1] = '\0';
                            strncpy(sau->var_name, decl.names[i], GLSLT_MAX_NAME - 1);
                            sau->var_name[GLSLT_MAX_NAME - 1] = '\0';
                            sau->array_size = arr_size;
                            struct_def_t *sd = find_struct_def(decl.struct_type_name);
                            sau->std140_size = compute_struct_std140_size(sd) * arr_size;
                        }
                    } else {
                        /* Single struct instance */
                        flatten_struct_to_uniforms(decl.names[i],
                                                   decl.struct_type_name,
                                                   uniforms, &nu,
                                                   samplers, &ns);
                    }
                }
                break;

            default:
                break;
            }

            /* Check for gl_FragColor / gl_FragData usage anywhere */
            if (stage == GLSLT_FRAGMENT) {
                if (strstr(line, "gl_FragColor") || strstr(line, "gl_FragData"))
                    has_frag_color = 1;
                /* Track highest gl_FragData[N] index for MRT outputs */
                const char *fd = line;
                while ((fd = strstr(fd, "gl_FragData[")) != NULL) {
                    fd += 12; /* skip "gl_FragData[" */
                    int idx = atoi(fd);
                    if (idx > max_frag_data) max_frag_data = idx;
                }
            }
        }

        lp = next_line(lp);
    }

    /* ---- Process: assign locations, compute layout ---- */

    /* Attributes */
    assign_attrib_locations(attributes, na, opts);
    qsort(attributes, na, sizeof(glslt_attribute_t), cmp_by_location_attr);

    /* Varyings */
    assign_varying_locations(varyings, nv, opts);
    qsort(varyings, nv, sizeof(glslt_varying_t), cmp_by_location_varying);

    /* Uniforms: sort alphabetically, compute std140 layout */
    qsort(uniforms, nu, sizeof(glslt_uniform_t), cmp_by_name_uniform);
    int ubo_total_size = 0;
    compute_std140_layout(uniforms, nu, &ubo_total_size);
    for (int i = 0; i < nu; i++)
        uniforms[i].binding = opts->ubo_binding;

    /* Samplers: keep declaration order, assign bindings */
    for (int i = 0; i < ns; i++)
        samplers[i].binding = opts->sampler_binding_start + i;

    /* ---- Pass 2: Emit output ---- */

    strbuf_t sb;
    sb_init(&sb);

    /* Version */
    sb_printf(&sb, "#version %d\n", opts->target_version);

    /* Attributes (vertex shader only) */
    if (stage == GLSLT_VERTEX && na > 0) {
        sb_append(&sb, "\n");
        for (int i = 0; i < na; i++) {
            sb_printf(&sb, "layout(location = %d) in %s %s;\n",
                      attributes[i].location,
                      glslt_type_name(attributes[i].type),
                      attributes[i].name);
        }
    }

    /* Varyings */
    if (nv > 0) {
        sb_append(&sb, "\n");
        const char *dir = (stage == GLSLT_VERTEX) ? "out" : "in";
        for (int i = 0; i < nv; i++) {
            if (varyings[i].array_size > 0) {
                sb_printf(&sb, "layout(location = %d) %s %s %s[%d];\n",
                          varyings[i].location, dir,
                          glslt_type_name(varyings[i].type),
                          varyings[i].name,
                          varyings[i].array_size);
            } else {
                sb_printf(&sb, "layout(location = %d) %s %s %s;\n",
                          varyings[i].location, dir,
                          glslt_type_name(varyings[i].type),
                          varyings[i].name);
            }
        }
    }

    /* Emit struct definitions needed by struct array uniforms (before UBO block) */
    for (int sa = 0; sa < s_num_struct_array_uniforms; sa++) {
        struct_def_t *sd = find_struct_def(s_struct_array_uniforms[sa].struct_type);
        if (!sd) continue;
        sb_printf(&sb, "\nstruct %s {\n", sd->name);
        for (int f = 0; f < sd->num_fields; f++) {
            const char *tname = sd->fields[f].is_struct
                ? sd->fields[f].type_name
                : glslt_type_name(sd->fields[f].type);
            if (sd->fields[f].array_size > 0) {
                sb_printf(&sb, "    %s %s[%d];\n", tname, sd->fields[f].name, sd->fields[f].array_size);
            } else {
                sb_printf(&sb, "    %s %s;\n", tname, sd->fields[f].name);
            }
        }
        sb_append(&sb, "};\n");
    }

    /* UBO block (includes flattened scalar uniforms + struct array uniforms) */
    if (nu > 0 || s_num_struct_array_uniforms > 0) {
        sb_append(&sb, "\n");
        sb_printf(&sb, "layout(std140, binding = %d) uniform %sUniforms {\n",
                  opts->ubo_binding,
                  (stage == GLSLT_VERTEX) ? "Vertex" : "Fragment");
        for (int i = 0; i < nu; i++) {
            if (uniforms[i].array_size > 0) {
                sb_printf(&sb, "    %s %s[%d];\n",
                          glslt_type_name(uniforms[i].type),
                          uniforms[i].name,
                          uniforms[i].array_size);
            } else {
                sb_printf(&sb, "    %s %s;\n",
                          glslt_type_name(uniforms[i].type),
                          uniforms[i].name);
            }
        }
        /* Struct array uniforms (kept as whole structs) */
        for (int i = 0; i < s_num_struct_array_uniforms; i++) {
            sb_printf(&sb, "    %s %s[%d];\n",
                      s_struct_array_uniforms[i].struct_type,
                      s_struct_array_uniforms[i].var_name,
                      s_struct_array_uniforms[i].array_size);
        }
        sb_append(&sb, "};\n");
    }

    /* Samplers */
    if (ns > 0) {
        sb_append(&sb, "\n");
        for (int i = 0; i < ns; i++) {
            sb_printf(&sb, "layout(binding = %d) uniform %s %s;\n",
                      samplers[i].binding,
                      glslt_type_name(samplers[i].type),
                      samplers[i].name);
        }
    }

    /* Fragment output(s) */
    if (stage == GLSLT_FRAGMENT && has_frag_color) {
        if (max_frag_data > 0) {
            /* Multiple render targets: gl_FragData[0]..gl_FragData[N] */
            sb_append(&sb, "\n");
            for (int i = 0; i <= max_frag_data; i++)
                sb_printf(&sb, "layout(location = %d) out vec4 fragData_%d;\n", i, i);
        } else {
            sb_append(&sb, "\nlayout(location = 0) out vec4 fragColor;\n");
        }
    }

    /* ---- Body: emit non-declaration lines with replacements ---- */

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
                if (after != sp && find_struct_def(sname)) {
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
            decl_kind_t kind = parse_line(line, &decl);

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
                sb_append(&sb, "\n");
                body_started = 1;
            }

            if (line_in_comment || line[0] == '\0') {
                /* Inside block comment or empty line: emit as-is */
                sb_append(&sb, line);
            } else {
                char replaced[MAX_LINE_LEN];
                apply_body_replacements(line, replaced, sizeof(replaced), stage);
                if (s_num_replacements > 0) {
                    char replaced2[MAX_LINE_LEN];
                    apply_struct_replacements(replaced, replaced2, sizeof(replaced2));
                    sb_append(&sb, replaced2);
                } else {
                    sb_append(&sb, replaced);
                }
            }
            sb_append(&sb, "\n");
        }

        lp = next_line(lp);
    }

    /* DkDeviceFlags_DepthMinusOneToOne handles GL→deko3d depth natively.
     * No shader-level z transform needed. */

    /* ---- Fill result ---- */

    result.output = sb.buf;
    result.output_len = sb.len;
    result.success = 1;

    /* Copy reflection data */
    memcpy(result.uniforms, uniforms, nu * sizeof(glslt_uniform_t));
    result.num_uniforms = nu;
    result.ubo_binding = opts->ubo_binding;
    result.ubo_total_size = ubo_total_size;

    memcpy(result.samplers, samplers, ns * sizeof(glslt_sampler_t));
    result.num_samplers = ns;

    memcpy(result.attributes, attributes, na * sizeof(glslt_attribute_t));
    result.num_attributes = na;

    memcpy(result.varyings, varyings, nv * sizeof(glslt_varying_t));
    result.num_varyings = nv;

    result.has_depth_range = has_depth_range;

    s_current_defines = NULL;
    s_num_structs = 0;
    s_num_replacements = 0;
    s_num_struct_array_uniforms = 0;
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
