/*
 * SwitchGLES - GLSL ES 1.00 compile-time validators.
 * Split out of glsl_transpiler.c; driven by glslt_validate_es100_impl there.
 */

#include "glsl_transpiler_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

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
static const char *find_nonconst_in_expr(glslt_ctx_t *ctx, const char *expr, int expr_len,
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
                if (find_struct_def(ctx, tmp)) continue;
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
int validate_const_initializers(glslt_ctx_t *ctx, const char *source,
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
                    const char *bad = find_nonconst_in_expr(ctx,
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
int validate_gles_semantics(const char *source, glslt_stage_t stage,
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
int validate_preprocessor_directives(const char *source,
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
int validate_preprocessor_undefined(const char *source,
                                            char *error, int error_size) {
    typedef struct { char name[64]; int value; } ppdef_t;
    ppdef_t defs[256];
    int ndefs = 0;
    int in_block_comment = 0;

    /* Pre-define built-in macros (use snprintf for safety) */
    snprintf(defs[ndefs].name, sizeof(defs[0].name), "GL_ES"); defs[ndefs].value = 1; ndefs++;
    snprintf(defs[ndefs].name, sizeof(defs[0].name), "__VERSION__"); defs[ndefs].value = 100; ndefs++;
    snprintf(defs[ndefs].name, sizeof(defs[0].name), "__LINE__"); defs[ndefs].value = 1; ndefs++;
    snprintf(defs[ndefs].name, sizeof(defs[0].name), "__FILE__"); defs[ndefs].value = 0; ndefs++;

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
int validate_qualification_order(const char *source,
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
int validate_texture_functions(const char *source, glslt_stage_t stage,
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

