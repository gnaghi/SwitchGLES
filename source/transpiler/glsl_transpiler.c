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
    DECL_EXTENSION
} decl_kind_t;

typedef struct {
    decl_kind_t   kind;
    glslt_type_t  type;
    int           is_sampler;
    char          names[8][GLSLT_MAX_NAME]; /* supports multiple names: uniform float a, b; */
    int           array_sizes[8];
    int           num_names;
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
    if (!ti) return 0;

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

        /* Check for array: [N] */
        if (*p == '[') {
            p++;
            int arr_size = 0;
            while (*p >= '0' && *p <= '9') {
                arr_size = arr_size * 10 + (*p - '0');
                p++;
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

    /* varying */
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
    }

    return DECL_NONE;
}

/* ========================================================================== */
/*  std140 layout computation                                                  */
/* ========================================================================== */

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

static void assign_attrib_locations(glslt_attribute_t *attrs, int count,
                                    const glslt_options_t *opts) {
    /* First pass: assign explicit bindings */
    int used[32] = {0};
    for (int i = 0; i < count; i++) {
        int loc = find_attrib_location(opts, attrs[i].name);
        if (loc >= 0 && loc < 32) {
            attrs[i].location = loc;
            used[loc] = 1;
        } else {
            attrs[i].location = -1;
        }
    }
    /* Second pass: auto-assign remaining */
    int next = 0;
    for (int i = 0; i < count; i++) {
        if (attrs[i].location >= 0) continue;
        while (next < 32 && used[next]) next++;
        attrs[i].location = next;
        used[next] = 1;
        next++;
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
            used[loc] = 1;
        } else {
            varyings[i].location = -1;
        }
    }
    /* Second pass: auto-assign remaining (alphabetical order) */
    int next = 0;
    for (int i = 0; i < count; i++) {
        if (varyings[i].location >= 0) continue;
        while (next < 32 && used[next]) next++;
        varyings[i].location = next;
        used[next] = 1;
        next++;
    }
}

/* ========================================================================== */
/*  Body text replacements                                                     */
/* ========================================================================== */

static void apply_body_replacements(const char *line, char *out, int out_size,
                                    glslt_stage_t stage) {
    /* Chain of word-boundary-aware replacements.
     * Order doesn't matter because word boundary checks prevent partial matches
     * (e.g. "texture2D" won't match inside "texture2DProj"). */
    char b1[MAX_LINE_LEN], b2[MAX_LINE_LEN];

    replace_word(line,  "texture2DProjLod", "textureProjLod", b1, sizeof(b1));
    replace_word(b1,    "texture2DProj",    "textureProj",    b2, sizeof(b2));
    replace_word(b2,    "texture2DLod",     "textureLod",     b1, sizeof(b1));
    replace_word(b1,    "textureCubeLod",   "textureLod",     b2, sizeof(b2));
    replace_word(b2,    "texture2D",        "texture",        b1, sizeof(b1));
    replace_word(b1,    "textureCube",      "texture",        b2, sizeof(b2));

    if (stage == GLSLT_FRAGMENT) {
        replace_word(b2, "gl_FragColor", "fragColor", b1, sizeof(b1));
        /* gl_FragData[0] -> fragColor (simple text replacement) */
        replace_word(b1, "gl_FragData[0]", "fragColor", out, out_size);
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

    /* Check if already modern GLSL - pass through unchanged */
    {
        const char *p = skip_ws(source);
        if (strncmp(p, "#version", 8) == 0) {
            p = skip_ws(p + 8);
            int ver = atoi(p);
            if (ver >= 300 && strstr(p, "es") == NULL) {
                /* Already desktop GLSL 300+ core, pass through */
                result.output = (char *)malloc(strlen(source) + 1);
                strcpy(result.output, source);
                result.output_len = (int)strlen(source);
                result.success = 1;
                return result;
            }
        }
    }

    /* ---- Pass 1: Collect declarations ---- */

    glslt_uniform_t  uniforms[GLSLT_MAX_UNIFORMS];
    glslt_sampler_t  samplers[GLSLT_MAX_SAMPLERS];
    glslt_attribute_t attributes[GLSLT_MAX_ATTRIBUTES];
    glslt_varying_t  varyings[GLSLT_MAX_VARYINGS];
    int nu = 0, ns = 0, na = 0, nv = 0;
    int has_frag_color = 0;
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
                    nv++;
                }
                break;

            case DECL_UNIFORM:
                for (int i = 0; i < decl.num_names; i++) {
                    if (decl.is_sampler) {
                        if (ns < GLSLT_MAX_SAMPLERS) {
                            strncpy(samplers[ns].name, decl.names[i], GLSLT_MAX_NAME - 1);
                            samplers[ns].type = decl.type;
                            samplers[ns].binding = -1;
                            ns++;
                        }
                    } else {
                        if (nu < GLSLT_MAX_UNIFORMS) {
                            strncpy(uniforms[nu].name, decl.names[i], GLSLT_MAX_NAME - 1);
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

            default:
                break;
            }

            /* Check for gl_FragColor / gl_FragData usage anywhere */
            if (stage == GLSLT_FRAGMENT) {
                if (strstr(line, "gl_FragColor") || strstr(line, "gl_FragData"))
                    has_frag_color = 1;
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
            sb_printf(&sb, "layout(location = %d) %s %s %s;\n",
                      varyings[i].location, dir,
                      glslt_type_name(varyings[i].type),
                      varyings[i].name);
        }
    }

    /* UBO block */
    if (nu > 0) {
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

    /* Fragment output */
    if (stage == GLSLT_FRAGMENT && has_frag_color) {
        sb_append(&sb, "\nlayout(location = 0) out vec4 fragColor;\n");
    }

    /* ---- Body: emit non-declaration lines with replacements ---- */

    in_block_comment = 0;
    int body_started = 0;
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

        if (!line_in_comment) {
            parsed_decl_t decl;
            decl_kind_t kind = parse_line(line, &decl);

            switch (kind) {
            case DECL_VERSION:
            case DECL_PRECISION:
            case DECL_ATTRIBUTE:
            case DECL_VARYING:
            case DECL_UNIFORM:
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
                sb_append(&sb, replaced);
            }
            sb_append(&sb, "\n");
        }

        lp = next_line(lp);
    }

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
