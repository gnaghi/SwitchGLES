/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - Uniform Functions
 */

#include "gl_common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations */
static int sgl_uniform_type_components(GLenum type);
static bool sgl_is_bool_uniform_type(GLenum type);
static int uniform_type_std140_size(GLenum type);
static bool is_valid_uniform_location(sgl_program_t *prog, GLint location);

/*
 * ============================================================================
 * UNIFORM REGISTRATION SYSTEM
 *
 * Allows end users to register their own uniform name -> binding mappings
 * without modifying the library source code.
 * ============================================================================
 */

#define SGL_MAX_REGISTERED_UNIFORMS 256
#define SGL_UNIFORM_NAME_MAX 64

typedef struct {
    char name[SGL_UNIFORM_NAME_MAX];
    int stage;       /* 0 = vertex, 1 = fragment */
    int binding;
    int byte_offset; /* -1 for legacy, >=0 for packed */
    bool used;
} sgl_uniform_entry_t;

static sgl_uniform_entry_t s_registered_uniforms[SGL_MAX_REGISTERED_UNIFORMS];
static int s_registered_count = 0;
static bool s_registry_initialized = false;

/* Packed UBO size registry (set via sglSetPackedUBOSize, applied to programs at glGetUniformLocation time) */
static int s_packed_ubo_sizes[2][SGL_MAX_PACKED_UBOS]; /* [stage][binding] = size in bytes */

/*
 * sglRegisterUniform - Register a uniform name to a specific shader binding
 */
GL_APICALL GLboolean GL_APIENTRY sglRegisterUniform(const GLchar *name, GLint stage, GLint binding) {
    if (!name || stage < 0 || stage > 1 || binding < 0 || binding >= SGL_MAX_UNIFORMS) {
        return GL_FALSE;
    }

    size_t len = strlen(name);
    if (len == 0 || len >= SGL_UNIFORM_NAME_MAX) {
        return GL_FALSE;
    }

    /* Check if already registered - update if so */
    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used &&
            strcmp(s_registered_uniforms[i].name, name) == 0) {
            s_registered_uniforms[i].stage = stage;
            s_registered_uniforms[i].binding = binding;
            s_registered_uniforms[i].byte_offset = -1; /* legacy mode */
            return GL_TRUE;
        }
    }

    /* Find empty slot or add new entry */
    int slot = -1;
    for (int i = 0; i < s_registered_count; i++) {
        if (!s_registered_uniforms[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (s_registered_count >= SGL_MAX_REGISTERED_UNIFORMS) {
            return GL_FALSE;
        }
        slot = s_registered_count++;
    }

    strncpy(s_registered_uniforms[slot].name, name, SGL_UNIFORM_NAME_MAX - 1);
    s_registered_uniforms[slot].name[SGL_UNIFORM_NAME_MAX - 1] = '\0';
    s_registered_uniforms[slot].stage = stage;
    s_registered_uniforms[slot].binding = binding;
    s_registered_uniforms[slot].byte_offset = -1; /* legacy mode */
    s_registered_uniforms[slot].used = true;
    s_registry_initialized = true;

    return GL_TRUE;
}

/*
 * sglClearUniformRegistry - Clear all user-registered uniform mappings
 */
GL_APICALL void GL_APIENTRY sglClearUniformRegistry(void) {
    for (int i = 0; i < s_registered_count; i++) {
        s_registered_uniforms[i].used = false;
    }
    s_registered_count = 0;
    /* Clear packed UBO sizes */
    memset(s_packed_ubo_sizes, 0, sizeof(s_packed_ubo_sizes));
    /* Note: built-in mappings are still available via hardcoded checks */
}

/*
 * sglSetPackedUBOSize - Set the total size of a packed UBO binding
 */
GL_APICALL void GL_APIENTRY sglSetPackedUBOSize(GLint stage, GLint binding, GLint size) {
    if (stage < 0 || stage > 1) return;
    if (binding < 0 || binding >= SGL_MAX_PACKED_UBOS) return;
    if (size < 0 || size > SGL_MAX_PACKED_UBO_SIZE) return;
    s_packed_ubo_sizes[stage][binding] = size;
}

/*
 * sglRegisterPackedUniform - Register a uniform into a packed UBO
 */
GL_APICALL GLboolean GL_APIENTRY sglRegisterPackedUniform(const GLchar *name,
                                                           GLint stage,
                                                           GLint binding,
                                                           GLint byte_offset) {
    if (!name || stage < 0 || stage > 1) return GL_FALSE;
    if (binding < 0 || binding >= SGL_MAX_PACKED_UBOS) return GL_FALSE;
    if (byte_offset < 0 || byte_offset >= SGL_MAX_PACKED_UBO_SIZE) return GL_FALSE;

    size_t len = strlen(name);
    if (len == 0 || len >= SGL_UNIFORM_NAME_MAX) return GL_FALSE;

    /* Check if already registered for same name+stage — update if so.
     * NOTE: same name can be registered for BOTH stages (dual-stage uniforms). */
    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used &&
            strcmp(s_registered_uniforms[i].name, name) == 0 &&
            s_registered_uniforms[i].stage == stage) {
            s_registered_uniforms[i].binding = binding;
            s_registered_uniforms[i].byte_offset = byte_offset;
            return GL_TRUE;
        }
    }

    /* Find empty slot or add new entry */
    int slot = -1;
    for (int i = 0; i < s_registered_count; i++) {
        if (!s_registered_uniforms[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (s_registered_count >= SGL_MAX_REGISTERED_UNIFORMS) return GL_FALSE;
        slot = s_registered_count++;
    }

    strncpy(s_registered_uniforms[slot].name, name, SGL_UNIFORM_NAME_MAX - 1);
    s_registered_uniforms[slot].name[SGL_UNIFORM_NAME_MAX - 1] = '\0';
    s_registered_uniforms[slot].stage = stage;
    s_registered_uniforms[slot].binding = binding;
    s_registered_uniforms[slot].byte_offset = byte_offset;
    s_registered_uniforms[slot].used = true;
    s_registry_initialized = true;

    return GL_TRUE;
}

/*
 * Check user-registered uniforms first.
 * Returns legacy encoding (stage << 16 | binding) or
 * packed encoding (bit30 | stage << 24 | binding << 16 | byte_offset).
 */
static GLint lookup_registered_uniform(const GLchar *name) {
    if (!s_registry_initialized) return -1;

    GLint vs_loc = -1, fs_loc = -1;
    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used &&
            strcmp(s_registered_uniforms[i].name, name) == 0) {
            GLint loc;
            if (s_registered_uniforms[i].byte_offset >= 0) {
                loc = (GLint)(SGL_LOC_PACKED_FLAG |
                       ((unsigned)s_registered_uniforms[i].stage << SGL_LOC_STAGE_SHIFT) |
                       ((unsigned)s_registered_uniforms[i].binding << SGL_LOC_BINDING_SHIFT) |
                       (unsigned)s_registered_uniforms[i].byte_offset);
            } else {
                loc = (s_registered_uniforms[i].stage << 16) | s_registered_uniforms[i].binding;
            }
            if (s_registered_uniforms[i].stage == 0)
                vs_loc = loc;
            else
                fs_loc = loc;
        }
    }
    /* Prefer VS, fallback to FS */
    if (vs_loc != -1) return vs_loc;
    return fs_loc;
}

/*
 * Look up the "mirror" (other stage) for a dual-stage packed uniform.
 * Returns -1 if no mirror exists.
 */
static GLint lookup_registered_uniform_mirror(const GLchar *name, int primary_stage) {
    if (!s_registry_initialized) return -1;

    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used &&
            strcmp(s_registered_uniforms[i].name, name) == 0 &&
            s_registered_uniforms[i].byte_offset >= 0 &&
            s_registered_uniforms[i].stage != primary_stage) {
            return (GLint)(SGL_LOC_PACKED_FLAG |
                   ((unsigned)s_registered_uniforms[i].stage << SGL_LOC_STAGE_SHIFT) |
                   ((unsigned)s_registered_uniforms[i].binding << SGL_LOC_BINDING_SHIFT) |
                   (unsigned)s_registered_uniforms[i].byte_offset);
        }
    }
    return -1;
}

/*
 * Track a uniform as "active" in the program when glGetUniformLocation returns a valid location.
 * This is used by glGetActiveUniform and glGetProgramiv(GL_ACTIVE_UNIFORMS).
 */
static void sgl_track_active_uniform(sgl_program_t *prog, const GLchar *name,
                                      GLint location, GLenum type, GLint size) {
    if (!prog || !name) return;

    /* Check if already tracked */
    for (int i = 0; i < prog->num_active_uniforms; i++) {
        if (prog->active_uniforms[i].active &&
            strcmp(prog->active_uniforms[i].name, name) == 0) {
            return; /* Already tracked */
        }
    }

    /* Add new entry */
    if (prog->num_active_uniforms < SGL_MAX_UNIFORMS * 2) {
        int slot = prog->num_active_uniforms++;
        size_t len = strlen(name);
        if (len >= SGL_ATTRIB_NAME_MAX) len = SGL_ATTRIB_NAME_MAX - 1;
        memcpy(prog->active_uniforms[slot].name, name, len);
        prog->active_uniforms[slot].name[len] = '\0';
        prog->active_uniforms[slot].location = location;
        prog->active_uniforms[slot].type = type;
        prog->active_uniforms[slot].size = size;
        prog->active_uniforms[slot].active = true;
    }
}

/*
 * Helper: configure a program's packed UBO from a packed location.
 * Uses per-program sizes first, falls back to global registry.
 */
static void configure_packed_ubo(sgl_program_t *prog, GLint location) {
    int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
    int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;

    sgl_packed_ubo_t *packed = (stage == 0)
        ? &prog->packed_vertex[binding]
        : &prog->packed_fragment[binding];

    if (!packed->valid) {
        /* Try per-program size first (from transpiler), then global (from sglSetPackedUBOSize) */
        int ubo_size = prog->packed_ubo_sizes[stage][binding];
        if (ubo_size <= 0)
            ubo_size = s_packed_ubo_sizes[stage][binding];
        if (ubo_size > 0 && ubo_size <= SGL_MAX_PACKED_UBO_SIZE) {
            packed->size = ubo_size;
            packed->valid = true;
            packed->dirty = false;
            memset(packed->data, 0, ubo_size);
        }
    }
}

/* Compute the effective array element stride for a program_uniform entry.
 * Mesa-compiled shaders use the actual constbuf stride (element_stride field).
 * Transpiler shaders use std140 layout (16-byte minimum for arrays). */
static int get_element_stride(const sgl_program_uniform_loc_t *u) {
    if (u->element_stride > 0)
        return (int)u->element_stride;
    /* Fallback: std140 stride (for transpiler-generated UBOs) */
    int sz = uniform_type_std140_size(u->gl_type);
    return (sz < 16) ? 16 : sz;
}

/* Same for active_uniform entries */
static int get_active_element_stride(const sgl_active_uniform_info_t *u) {
    if (u->element_stride > 0)
        return (int)u->element_stride;
    int sz = uniform_type_std140_size(u->type);
    return (sz < 16) ? 16 : sz;
}

/* Look up the element stride for a packed location by searching program_uniforms.
 * Returns the stride (element_stride or std140 fallback), or 16 if not found. */
static int lookup_element_stride(sgl_program_t *prog, GLint location) {
    if (!(location & SGL_LOC_PACKED_FLAG)) return 16;
    int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
    int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
    int offset = location & SGL_LOC_OFFSET_MASK;
    /* Exact base match */
    for (int i = 0; i < prog->num_program_uniforms; i++) {
        if (!prog->program_uniforms[i].used) continue;
        if (prog->program_uniforms[i].location == location)
            return get_element_stride(&prog->program_uniforms[i]);
    }
    /* Array range match */
    for (int i = 0; i < prog->num_program_uniforms; i++) {
        if (!prog->program_uniforms[i].used) continue;
        if (prog->program_uniforms[i].array_size <= 1) continue;
        GLint bloc = prog->program_uniforms[i].location;
        if (!(bloc & SGL_LOC_PACKED_FLAG)) continue;
        int b_stage = (bloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int b_binding = (bloc >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int b_offset = bloc & SGL_LOC_OFFSET_MASK;
        if (b_stage != stage || b_binding != binding) continue;
        int es = get_element_stride(&prog->program_uniforms[i]);
        int array_end = b_offset + prog->program_uniforms[i].array_size * es;
        if (offset >= b_offset && offset < array_end)
            return es;
    }
    return 16; /* fallback */
}

/* Return the std140 byte size for a GL uniform type.
 * For matrices, returns the std140 padded size (columns padded to vec4). */
static int uniform_type_std140_size(GLenum type) {
    switch (type) {
        case GL_FLOAT: case GL_INT: case GL_BOOL:
        case GL_SAMPLER_2D: case GL_SAMPLER_CUBE:
            return 4;
        case GL_FLOAT_VEC2: case GL_INT_VEC2: case GL_BOOL_VEC2:
            return 8;
        case GL_FLOAT_VEC3: case GL_INT_VEC3: case GL_BOOL_VEC3:
            return 12;
        case GL_FLOAT_VEC4: case GL_INT_VEC4: case GL_BOOL_VEC4:
            return 16;
        case GL_FLOAT_MAT2:
            return 32;  /* 2 columns × 16 bytes (vec4-padded) */
        case GL_FLOAT_MAT3:
            return 48;  /* 3 columns × 16 bytes (vec4-padded) */
        case GL_FLOAT_MAT4:
            return 64;  /* 4 columns × 16 bytes */
        default:
            return 4;
    }
}

/* Forward declarations */
static const sgl_active_uniform_info_t *find_active_uniform_by_location(
    sgl_program_t *prog, GLint location);

/* Find the uniform type for a packed location by searching program_uniforms[].
 * For array element locations (offset differs from base), matches the base
 * uniform whose offset range covers the given offset. Returns 0 if not found. */
static GLenum find_packed_uniform_type(sgl_program_t *prog, GLint location) {
    /* Exact match first (fast path) */
    for (int i = 0; i < prog->num_program_uniforms; i++) {
        if (prog->program_uniforms[i].used &&
            prog->program_uniforms[i].location == location)
            return prog->program_uniforms[i].gl_type;
    }
    /* Array element: match by stage+binding, find base uniform covering this offset */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        for (int i = 0; i < prog->num_program_uniforms; i++) {
            if (!prog->program_uniforms[i].used) continue;
            GLint bloc = prog->program_uniforms[i].location;
            if (!(bloc & SGL_LOC_PACKED_FLAG)) continue;
            int b_stage = (bloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
            int b_binding = (bloc >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
            int b_offset = bloc & SGL_LOC_OFFSET_MASK;
            if (b_stage != stage || b_binding != binding) continue;
            if (prog->program_uniforms[i].array_size <= 1) continue;
            /* Use actual element stride (Mesa constbuf or std140) */
            int elem_stride = get_element_stride(&prog->program_uniforms[i]);
            int array_end = b_offset + prog->program_uniforms[i].array_size * elem_stride;
            if (offset >= b_offset && offset < array_end)
                return prog->program_uniforms[i].gl_type;
        }
    }
    return 0;
}

GL_APICALL GLint GL_APIENTRY glGetUniformLocation(GLuint program, const GLchar *name) {
    GET_CTX_RET(-1);

    if (program == 0 || !name) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return -1;
    }

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return -1;
    }
    if (!prog->linked) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return -1;
    }

    /*
     * Lookup order:
     * 1. Global registry (manual sglRegisterUniform/sglRegisterPackedUniform — user overrides)
     * 2. Per-program table (auto-discovered from transpiler at glLinkProgram time)
     * 3. Built-in hardcoded table (for precompiled shaders)
     */

    /* 1. Check user-registered uniforms FIRST (allows overriding built-ins) */
    GLint registered = lookup_registered_uniform(name);
    if (registered != -1) {
        if (registered & SGL_LOC_PACKED_FLAG) {
            configure_packed_ubo(prog, registered);
            /* Check for dual-stage mirror (same uniform in both VS and FS UBOs) */
            int primary_stage = (registered >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
            GLint mirror = lookup_registered_uniform_mirror(name, primary_stage);
            if (mirror != -1) {
                configure_packed_ubo(prog, mirror);
                if (prog->num_packed_mirrors < SGL_MAX_PACKED_MIRRORS) {
                    prog->packed_mirrors[prog->num_packed_mirrors].primary = registered;
                    prog->packed_mirrors[prog->num_packed_mirrors].mirror = mirror;
                    prog->num_packed_mirrors++;
                }
            }
        }
        sgl_track_active_uniform(prog, name, registered, GL_FLOAT_VEC4, 1);
        return registered;
    }

    /* 2a. Check per-program sampler table (from transpiler reflection).
     * Matches on: exact name, gles_name (e.g. "s[0]"), or base array name (e.g. "s" → first element).
     * NOTE: Do NOT call sgl_track_active_uniform here — active_uniforms are populated at link time. */
    for (int i = 0; i < prog->num_samplers; i++) {
        if (!prog->samplers[i].used) continue;
        if (strcmp(prog->samplers[i].name, name) == 0 ||
            (prog->samplers[i].gles_name[0] && strcmp(prog->samplers[i].gles_name, name) == 0)) {
            GLint loc = (GLint)(SGL_LOC_SAMPLER_FLAG | (unsigned)i);
            return loc;
        }
    }
    /* Check if name matches the base name of a sampler array (e.g. "s" → first element "s[0]") */
    for (int i = 0; i < prog->num_samplers; i++) {
        if (!prog->samplers[i].used) continue;
        if (prog->samplers[i].array_index == 0 && prog->samplers[i].array_total > 0) {
            /* Extract base name from gles_name "s[0]" → "s" */
            char base[SGL_ATTRIB_NAME_MAX];
            const char *src_name = prog->samplers[i].gles_name[0]
                ? prog->samplers[i].gles_name : prog->samplers[i].name;
            strncpy(base, src_name, SGL_ATTRIB_NAME_MAX - 1);
            base[SGL_ATTRIB_NAME_MAX - 1] = '\0';
            char *bracket = strchr(base, '[');
            if (bracket) *bracket = '\0';
            if (strcmp(base, name) == 0) {
                GLint loc = (GLint)(SGL_LOC_SAMPLER_FLAG | (unsigned)i);
                return loc;
            }
        }
    }

    /* 2b. Check per-program uniform table (from transpiler reflection).
     * Matches on: flattened name, gles_name (dot notation for structs),
     * and array element access (name[N]).
     * NOTE: Do NOT call sgl_track_active_uniform — active_uniforms are populated at link time.
     * NOTE: Mirrors are set up at link time in gl_shader.c, not here. */
    for (int i = 0; i < prog->num_program_uniforms; i++) {
        if (!prog->program_uniforms[i].used) continue;
        const char *uname = prog->program_uniforms[i].name;
        const char *ugles = prog->program_uniforms[i].gles_name;
        if (strcmp(uname, name) == 0 ||
            (ugles[0] && strcmp(ugles, name) == 0)) {
            GLint loc = prog->program_uniforms[i].location;
            if (loc & SGL_LOC_PACKED_FLAG) {
                configure_packed_ubo(prog, loc);
            }
            return loc;
        }
        /* GLES2 spec: "u_float" (bare name) returns first element of "u_float[0]".
         * Mesa stores array uniform names with [0] suffix — strip it for matching. */
        if (prog->program_uniforms[i].array_size > 0) {
            const char *check = ugles[0] ? ugles : uname;
            const char *br = strrchr(check, '[');
            if (br) {
                size_t blen = br - check;
                if (blen == strlen(name) && strncmp(check, name, blen) == 0) {
                    GLint loc = prog->program_uniforms[i].location;
                    if (loc & SGL_LOC_PACKED_FLAG) {
                        configure_packed_ubo(prog, loc);
                    }
                    return loc;
                }
            }
        }
    }

    /* 2c. Array element access: "name[N]" → find base uniform and offset location.
     * Use strrchr to find the LAST '[' — handles nested struct/array paths like
     * "u_var0.m2[0].m0[3]" where the last [3] is the array index we want.
     * NOTE: Do NOT call sgl_track_active_uniform — active_uniforms are populated at link time. */
    {
        const char *bracket = strrchr(name, '[');
        if (bracket) {
            size_t base_len = bracket - name;
            int idx = atoi(bracket + 1);
            if (base_len > 0 && base_len < SGL_ATTRIB_NAME_MAX && idx >= 0) {
                char base_name[SGL_ATTRIB_NAME_MAX];
                memcpy(base_name, name, base_len);
                base_name[base_len] = '\0';
                for (int i = 0; i < prog->num_program_uniforms; i++) {
                    if (!prog->program_uniforms[i].used) continue;
                    const char *match_name = prog->program_uniforms[i].gles_name[0]
                        ? prog->program_uniforms[i].gles_name : prog->program_uniforms[i].name;
                    /* Strip trailing [N] suffix from stored name for comparison.
                     * Mesa stores array names as "u_float[0]" but we need "u_float".
                     * Use strrchr to find the LAST bracket, but only strip it if it's
                     * terminal (no dot after — distinguishes "arr[0]" from "s[0].field"). */
                    char match_base[SGL_ATTRIB_NAME_MAX];
                    const char *mbr = strrchr(match_name, '[');
                    if (mbr && strchr(mbr, '.')) mbr = NULL; /* not terminal, skip */
                    if (mbr) {
                        size_t mblen = mbr - match_name;
                        if (mblen >= SGL_ATTRIB_NAME_MAX) mblen = SGL_ATTRIB_NAME_MAX - 1;
                        memcpy(match_base, match_name, mblen);
                        match_base[mblen] = '\0';
                        match_name = match_base;
                    }
                    if (strcmp(match_name, base_name) != 0) continue;
                    int arr = prog->program_uniforms[i].array_size;
                    if (arr <= 0 || idx >= arr) continue;
                    GLint base_loc = prog->program_uniforms[i].location;
                    if (!(base_loc & SGL_LOC_PACKED_FLAG)) continue;
                    /* Use actual element stride from metadata (Mesa constbuf or std140) */
                    int elem_stride = get_element_stride(&prog->program_uniforms[i]);
                    int base_offset = base_loc & SGL_LOC_OFFSET_MASK;
                    int elem_offset = base_offset + idx * elem_stride;
                    GLint loc = (GLint)((base_loc & ~(GLint)SGL_LOC_OFFSET_MASK) | (unsigned)elem_offset);
                    configure_packed_ubo(prog, loc);
                    return loc;
                }
                /* Also check sampler arrays: "s[N]" */
                for (int i = 0; i < prog->num_samplers; i++) {
                    if (!prog->samplers[i].used) continue;
                    if (prog->samplers[i].array_total <= 0) continue;
                    /* Extract base from gles_name */
                    char sbase[SGL_ATTRIB_NAME_MAX];
                    const char *sgn = prog->samplers[i].gles_name[0]
                        ? prog->samplers[i].gles_name : prog->samplers[i].name;
                    strncpy(sbase, sgn, SGL_ATTRIB_NAME_MAX - 1);
                    sbase[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                    char *sb = strchr(sbase, '[');
                    if (sb) *sb = '\0';
                    if (strcmp(sbase, base_name) != 0) continue;
                    if (idx >= prog->samplers[i].array_total) continue;
                    /* Find the sampler entry for this index */
                    for (int si = 0; si < prog->num_samplers; si++) {
                        if (prog->samplers[si].used &&
                            prog->samplers[si].array_index == idx &&
                            prog->samplers[si].array_total == prog->samplers[i].array_total) {
                            /* Verify same base name */
                            char sbase2[SGL_ATTRIB_NAME_MAX];
                            const char *sgn2 = prog->samplers[si].gles_name[0]
                                ? prog->samplers[si].gles_name : prog->samplers[si].name;
                            strncpy(sbase2, sgn2, SGL_ATTRIB_NAME_MAX - 1);
                            sbase2[SGL_ATTRIB_NAME_MAX - 1] = '\0';
                            char *sb2 = strchr(sbase2, '[');
                            if (sb2) *sb2 = '\0';
                            if (strcmp(sbase2, base_name) == 0) {
                                GLint loc = (GLint)(SGL_LOC_SAMPLER_FLAG | (unsigned)si);
                                return loc;
                            }
                        }
                    }
                    break; /* Found array but no matching index */
                }
            }
        }
    }

    /* Skip built-in table for transpiled programs — they have complete uniform
     * metadata from the transpiler. Falling through would return phantom locations
     * for names like "u_color" that don't exist in the shader, causing spurious
     * non-packed uniform bindings that interfere with the packed UBO path. */
    if (prog->num_program_uniforms > 0) {
        /* Debug: dump stored names when lookup fails */
        return -1;
    }

    /* ========== BUILT-IN VERTEX STAGE UNIFORMS ========== */

    /* Vertex binding 0: matrices and scale */
    if (strcmp(name, "u_mvp") == 0 ||
        strcmp(name, "Transforms") == 0 ||
        strcmp(name, "u_modelViewProj") == 0 ||
        strcmp(name, "u_mvpMatrix") == 0 ||
        strcmp(name, "u_matrix") == 0 ||
        strcmp(name, "u_projection") == 0 ||   /* SDL_Renderer */
        strcmp(name, "u_testScale") == 0 ||
        strcmp(name, "ModelViewProjectionMatrix") == 0) {  /* es2gears */
        GLint loc = (0 << 16) | 0;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_MAT4, 1);
        return loc;
    }

    /* Vertex binding 1: offset vec2/vec4 or NormalMatrix or Model matrix */
    if (strcmp(name, "u_offset") == 0 ||
        strcmp(name, "u_normalMatrix") == 0 ||
        strcmp(name, "u_testOffset2") == 0 ||
        strcmp(name, "u_model") == 0 ||        /* PBR model matrix */
        strcmp(name, "Model") == 0 ||          /* UBO block name */
        strcmp(name, "ModelMatrix") == 0 ||    /* blinn_phong block name */
        strcmp(name, "NormalMatrix") == 0) {   /* es2gears */
        GLint loc = (0 << 16) | 1;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_MAT4, 1);
        return loc;
    }

    /* Vertex binding 2: offset3 vec3 or LightSourcePosition */
    if (strcmp(name, "u_testOffset3") == 0 ||
        strcmp(name, "LightSourcePosition") == 0) {  /* es2gears */
        GLint loc = (0 << 16) | 2;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_VEC4, 1);
        return loc;
    }

    /* Vertex binding 3: mat2 or MaterialColor */
    if (strcmp(name, "u_testMat2") == 0 ||
        strcmp(name, "MaterialColor") == 0) {  /* es2gears */
        GLint loc = (0 << 16) | 3;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_VEC4, 1);
        return loc;
    }

    /* Vertex binding 4: mat3 */
    if (strcmp(name, "u_testMat3") == 0) {
        GLint loc = (0 << 16) | 4;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_MAT3, 1);
        return loc;
    }

    /* ========== BUILT-IN FRAGMENT STAGE UNIFORMS ========== */

    /* Fragment binding 0: color vec4 or alpha float or blend */
    if (strcmp(name, "u_color") == 0 ||
        strcmp(name, "FragUniforms") == 0 ||
        strcmp(name, "u_baseColor") == 0 ||
        strcmp(name, "u_testAlpha") == 0 ||
        strcmp(name, "u_blend") == 0) {
        GLint loc = (1 << 16) | 0;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_VEC4, 1);
        return loc;
    }

    /* Fragment binding 1: vec2/vec4 or time or Material block (skybox) */
    if (strcmp(name, "u_testVec2") == 0 ||
        strcmp(name, "u_alpha") == 0 ||
        strcmp(name, "u_time") == 0 ||
        strcmp(name, "Material") == 0) {       /* UBO block name for skybox */
        GLint loc = (1 << 16) | 1;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_VEC4, 1);
        return loc;
    }

    /* Fragment binding 2: vec3/vec4 or mode or material params (PBR) */
    if (strcmp(name, "u_testVec3") == 0 ||
        strcmp(name, "u_mode") == 0 ||
        strcmp(name, "u_material") == 0 ||     /* PBR material params */
        strcmp(name, "u_light") == 0 ||        /* Blinn-Phong light uniform */
        strcmp(name, "LightParams") == 0) {    /* Blinn-Phong light UBO block */
        GLint loc = (1 << 16) | 2;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_VEC4, 1);
        return loc;
    }

    /* Fragment binding 3: vec4 */
    if (strcmp(name, "u_testVec4") == 0) {
        GLint loc = (1 << 16) | 3;
        sgl_track_active_uniform(prog, name, loc, GL_FLOAT_VEC4, 1);
        return loc;
    }

    /* Fragment binding 4: int mode (for alluniform shader) */
    if (strcmp(name, "u_testMode") == 0) {
        GLint loc = (1 << 16) | 4;
        sgl_track_active_uniform(prog, name, loc, GL_INT, 1);
        return loc;
    }

    /* Fragment binding 5: ivec2 */
    if (strcmp(name, "u_testIvec2") == 0) {
        GLint loc = (1 << 16) | 5;
        sgl_track_active_uniform(prog, name, loc, GL_INT_VEC2, 1);
        return loc;
    }

    /* Fragment binding 6: ivec3 */
    if (strcmp(name, "u_testIvec3") == 0) {
        GLint loc = (1 << 16) | 6;
        sgl_track_active_uniform(prog, name, loc, GL_INT_VEC3, 1);
        return loc;
    }

    /* Fragment binding 7: ivec4 */
    if (strcmp(name, "u_testIvec4") == 0) {
        GLint loc = (1 << 16) | 7;
        sgl_track_active_uniform(prog, name, loc, GL_INT_VEC4, 1);
        return loc;
    }

    return -1;
}

/*
 * Built-in attribute name → location mapping (for precompiled shaders).
 * Shaders use layout(location = N) which must match these defaults.
 */
static GLint lookup_builtin_attrib(const GLchar *name) {
    /* Location 0: position */
    if (strcmp(name, "position") == 0 || strcmp(name, "a_position") == 0 ||
        strcmp(name, "vPosition") == 0 || strcmp(name, "aPosition") == 0 ||
        strcmp(name, "in_position") == 0 || strcmp(name, "inPosition") == 0)
        return 0;

    /* Location 1: texcoord */
    if (strcmp(name, "texcoord") == 0 || strcmp(name, "a_texcoord") == 0 ||
        strcmp(name, "vTexCoord") == 0 || strcmp(name, "aTexCoord") == 0 ||
        strcmp(name, "in_texcoord") == 0 || strcmp(name, "inTexCoord") == 0 ||
        strcmp(name, "a_texCoord") == 0)
        return 1;

    /* Location 2: normal */
    if (strcmp(name, "normal") == 0 || strcmp(name, "a_normal") == 0 ||
        strcmp(name, "vNormal") == 0 || strcmp(name, "aNormal") == 0 ||
        strcmp(name, "in_normal") == 0 || strcmp(name, "inNormal") == 0)
        return 2;

    /* Location 3: color */
    if (strcmp(name, "color") == 0 || strcmp(name, "a_color") == 0 ||
        strcmp(name, "vColor") == 0 || strcmp(name, "aColor") == 0 ||
        strcmp(name, "in_color") == 0 || strcmp(name, "inColor") == 0)
        return 3;

    /* Location 4: tangent */
    if (strcmp(name, "tangent") == 0 || strcmp(name, "a_tangent") == 0 ||
        strcmp(name, "vTangent") == 0 || strcmp(name, "aTangent") == 0)
        return 4;

    return -1;
}

GL_APICALL GLint GL_APIENTRY glGetAttribLocation(GLuint program, const GLchar *name) {
    GET_CTX_RET(-1);

    if (program == 0 || !name) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return -1;
    }

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return -1;
    }
    if (!prog->linked) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return -1;
    }

    /* Return the location from the last link (not the pending binding).
     * Per GLES2 §2.10.4, glGetAttribLocation returns the location assigned
     * at link time.  glBindAttribLocation only takes effect at next link. */
    for (int i = 0; i < prog->num_attrib_bindings; i++) {
        if (prog->attrib_bindings[i].used &&
            prog->attrib_bindings[i].in_shader &&
            strcmp(prog->attrib_bindings[i].name, name) == 0) {
            return prog->attrib_bindings[i].linked_location;
        }
    }

    /* Fall back to built-in attribute name table */
    return lookup_builtin_attrib(name);
}

GL_APICALL void GL_APIENTRY glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
    GET_CTX();

    if (index >= 16) {  /* GL_MAX_VERTEX_ATTRIBS = 16 */
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (program == 0 || !name) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* GLES2: names starting with "gl_" are reserved */
    if (name[0] == 'g' && name[1] == 'l' && name[2] == '_') {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }

    size_t len = strlen(name);
    if (len == 0 || len >= SGL_ATTRIB_NAME_MAX) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Check if already bound - update if so */
    for (int i = 0; i < prog->num_attrib_bindings; i++) {
        if (prog->attrib_bindings[i].used &&
            strcmp(prog->attrib_bindings[i].name, name) == 0) {
            prog->attrib_bindings[i].index = index;
            prog->attrib_bindings[i].user_bound = true;  /* Promote linker-added to user-bound */
            SGL_TRACE_SHADER("glBindAttribLocation(%u, %u, \"%s\") - updated", program, index, name);
            return;
        }
    }

    /* Add new binding */
    if (prog->num_attrib_bindings < SGL_MAX_ATTRIB_BINDINGS) {
        int slot = prog->num_attrib_bindings++;
        strncpy(prog->attrib_bindings[slot].name, name, SGL_ATTRIB_NAME_MAX - 1);
        prog->attrib_bindings[slot].name[SGL_ATTRIB_NAME_MAX - 1] = '\0';
        prog->attrib_bindings[slot].index = index;
        prog->attrib_bindings[slot].linked_location = -1;  /* Not yet linked */
        prog->attrib_bindings[slot].used = true;
        prog->attrib_bindings[slot].user_bound = true;
    }

    SGL_TRACE_SHADER("glBindAttribLocation(%u, %u, \"%s\")", program, index, name);
}

GL_APICALL void GL_APIENTRY glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                                               GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    if (!prog->linked) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Only count attributes that are actually in the compiled shader */
    if (index >= (GLuint)prog->num_active_attribs) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Find the Nth active (in_shader) attribute.
     * Aliased attributes (same location) are each individually active
     * and must all be enumerable per GLES2 spec. */
    int count = 0;
    for (int i = 0; i < prog->num_attrib_bindings; i++) {
        if (!prog->attrib_bindings[i].used || !prog->attrib_bindings[i].in_shader) continue;
        if (count == (int)index) {
            if (name && bufSize > 0) {
                GLsizei namelen = (GLsizei)strlen(prog->attrib_bindings[i].name);
                GLsizei copylen = (bufSize - 1 < namelen) ? bufSize - 1 : namelen;
                memcpy(name, prog->attrib_bindings[i].name, copylen);
                name[copylen] = '\0';
                if (length) *length = copylen;
            } else if (length) {
                *length = 0;
            }
            if (size) *size = 1;
            if (type) *type = prog->attrib_bindings[i].gl_type ? prog->attrib_bindings[i].gl_type : GL_FLOAT_VEC4;
            return;
        }
        count++;
    }

    sgl_set_error(ctx, GL_INVALID_VALUE);
}

GL_APICALL void GL_APIENTRY glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                                                GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    GET_CTX();

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    if (!prog->linked) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (index >= (GLuint)prog->num_active_uniforms) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* Find the Nth active uniform */
    int count = 0;
    for (int i = 0; i < prog->num_active_uniforms; i++) {
        if (!prog->active_uniforms[i].active) continue;
        if (count == (int)index) {
            if (name && bufSize > 0) {
                GLsizei namelen = (GLsizei)strlen(prog->active_uniforms[i].name);
                GLsizei copylen = (bufSize - 1 < namelen) ? bufSize - 1 : namelen;
                memcpy(name, prog->active_uniforms[i].name, copylen);
                name[copylen] = '\0';
                if (length) *length = copylen;
            } else if (length) {
                *length = 0;
            }
            if (size) *size = prog->active_uniforms[i].size;
            if (type) *type = prog->active_uniforms[i].type;
            return;
        }
        count++;
    }

    sgl_set_error(ctx, GL_INVALID_VALUE);
}

GL_APICALL void GL_APIENTRY glGetUniformfv(GLuint program, GLint location, GLfloat *params) {
    GET_CTX();

    if (!params) return;
    if (location == -1) return;  /* "not found" location — silently no-op */

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    if (!prog->linked) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Per GLES2 spec: GL_INVALID_OPERATION if location doesn't correspond
     * to a valid uniform variable for the specified program. */
    if (!is_valid_uniform_location(prog, location)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Sampler readback as float (check before sign test — sampler flag is bit 30) */
    if (location & SGL_LOC_SAMPLER_FLAG) {
        int idx = location & 0xFFFF;
        if (idx >= 0 && idx < prog->num_samplers)
            *params = (GLfloat)prog->samplers[idx].tex_unit;
        return;
    }

    /* Packed mode readback (check before sign test — packed flag is bit 31, negative as signed) */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        if (binding >= SGL_MAX_PACKED_UBOS) return;
        /* Ensure packed UBO is configured (setter does this, getter must too) */
        configure_packed_ubo(prog, location);
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        if (packed->valid && (uint32_t)offset + 4 <= packed->size) {
            /* Look up uniform type to copy the full value (not just 4 bytes).
             * Use dual lookup (same as set_*_uniform) for robustness. */
            GLenum uni_type = find_packed_uniform_type(prog, location);
            if (!uni_type) {
                const sgl_active_uniform_info_t *ainfo = find_active_uniform_by_location(prog, location);
                if (ainfo) uni_type = ainfo->type;
            }
            if (uni_type == GL_BOOL || uni_type == GL_BOOL_VEC2 ||
                uni_type == GL_BOOL_VEC3 || uni_type == GL_BOOL_VEC4) {
                /* Bool stored as uint32 — convert to float (0.0 or 1.0) */
                int nc = sgl_uniform_type_components(uni_type);
                for (int j = 0; j < nc; j++) {
                    uint32_t bval;
                    memcpy(&bval, packed->data + offset + j * 4, 4);
                    params[j] = (bval != 0) ? 1.0f : 0.0f;
                }
            } else if (uni_type == GL_INT || uni_type == GL_INT_VEC2 ||
                       uni_type == GL_INT_VEC3 || uni_type == GL_INT_VEC4) {
                /* Int stored as int32 — convert to float */
                int nc = sgl_uniform_type_components(uni_type);
                for (int j = 0; j < nc; j++) {
                    int32_t ival;
                    memcpy(&ival, packed->data + offset + j * 4, 4);
                    params[j] = (GLfloat)ival;
                }
            } else if (uni_type == GL_FLOAT_MAT2 || uni_type == GL_FLOAT_MAT3 || uni_type == GL_FLOAT_MAT4) {
                /* Matrices in std140: columns padded to vec4 (16 bytes each).
                 * Must de-pad when reading back to contiguous float array. */
                int cols = (uni_type == GL_FLOAT_MAT2) ? 2 : (uni_type == GL_FLOAT_MAT3) ? 3 : 4;
                int rows = cols;
                for (int c = 0; c < cols; c++) {
                    int src_off = offset + c * 16; /* std140: each column at 16-byte stride */
                    if ((uint32_t)src_off + (uint32_t)rows * 4 > packed->size) break;
                    memcpy(&params[c * rows], packed->data + src_off, rows * 4);
                }
            } else {
                int std140_sz = uniform_type_std140_size(uni_type);
                if (std140_sz <= 0) std140_sz = 4;
                if ((uint32_t)offset + (uint32_t)std140_sz > packed->size)
                    std140_sz = packed->size - offset;
                memcpy(params, packed->data + offset, std140_sz);
            }
        }
        return;
    }

    /* Legacy mode readback from shadow buffer */
    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    if (ub->valid && ub->shadow_size > 0) {
        uint32_t copy_size = ub->shadow_size;
        if (copy_size > 64) copy_size = 64;
        if (ub->shadow_type == GL_FLOAT || ub->shadow_type == 0) {
            memcpy(params, ub->shadow, copy_size);
        } else {
            /* Convert int shadow to float */
            uint32_t count = copy_size / sizeof(int32_t);
            const int32_t *idata = (const int32_t *)ub->shadow;
            for (uint32_t i = 0; i < count; i++) {
                params[i] = (GLfloat)idata[i];
            }
        }
    }
}

GL_APICALL void GL_APIENTRY glGetUniformiv(GLuint program, GLint location, GLint *params) {
    GET_CTX();

    if (!params) return;
    if (location == -1) return;  /* "not found" location — silently no-op */

    sgl_program_t *prog = GET_PROGRAM(program);
    if (!prog) {
        sgl_set_error(ctx, GET_SHADER(program) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return;
    }
    if (!prog->linked) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Per GLES2 spec: GL_INVALID_OPERATION if location doesn't correspond
     * to a valid uniform variable for the specified program. */
    if (!is_valid_uniform_location(prog, location)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Sampler readback as int (check before sign test — sampler flag is bit 30) */
    if (location & SGL_LOC_SAMPLER_FLAG) {
        int idx = location & 0xFFFF;
        if (idx >= 0 && idx < prog->num_samplers)
            *params = (GLint)prog->samplers[idx].tex_unit;
        return;
    }

    /* Packed mode readback (check before sign test — packed flag is bit 31, negative as signed) */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        if (binding >= SGL_MAX_PACKED_UBOS) return;
        /* Ensure packed UBO is configured (setter does this, getter must too) */
        configure_packed_ubo(prog, location);
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        if (packed->valid && (uint32_t)offset + 4 <= packed->size) {
            /* Look up uniform type to determine readback size.
             * Use dual lookup (same as set_*_uniform) for robustness. */
            GLenum uni_type = find_packed_uniform_type(prog, location);
            if (!uni_type) {
                const sgl_active_uniform_info_t *ainfo = find_active_uniform_by_location(prog, location);
                if (ainfo) uni_type = ainfo->type;
            }
            /* For bool types, data is stored as uint32 in std140 */
            if (uni_type == GL_FLOAT || uni_type == GL_FLOAT_VEC2 ||
                uni_type == GL_FLOAT_VEC3 || uni_type == GL_FLOAT_VEC4 ||
                uni_type == GL_FLOAT_MAT2 || uni_type == GL_FLOAT_MAT3 ||
                uni_type == GL_FLOAT_MAT4) {
                /* Convert float data to int */
                if (uni_type == GL_FLOAT_MAT2 || uni_type == GL_FLOAT_MAT3 || uni_type == GL_FLOAT_MAT4) {
                    int cols = (uni_type == GL_FLOAT_MAT2) ? 2 : (uni_type == GL_FLOAT_MAT3) ? 3 : 4;
                    int rows = cols;
                    for (int c = 0; c < cols; c++) {
                        int src_off = offset + c * 16;
                        if ((uint32_t)src_off + (uint32_t)rows * 4 > packed->size) break;
                        const float *fdata = (const float *)(packed->data + src_off);
                        for (int r = 0; r < rows; r++)
                            params[c * rows + r] = (GLint)fdata[r];
                    }
                } else {
                    int std140_sz = uniform_type_std140_size(uni_type);
                    if (std140_sz <= 0) std140_sz = 4;
                    if ((uint32_t)offset + (uint32_t)std140_sz > packed->size)
                        std140_sz = packed->size - offset;
                    int nf = std140_sz / 4;
                    const float *fdata = (const float *)(packed->data + offset);
                    for (int fi = 0; fi < nf; fi++)
                        params[fi] = (GLint)fdata[fi];
                }
            } else if (uni_type == GL_BOOL || uni_type == GL_BOOL_VEC2 ||
                       uni_type == GL_BOOL_VEC3 || uni_type == GL_BOOL_VEC4) {
                /* Bool stored as 0xFFFFFFFF/0x00000000 in packed UBO (Mesa UniformBooleanTrue).
                 * Read as uint32_t and convert to 1/0. */
                int nc = sgl_uniform_type_components(uni_type);
                for (int j = 0; j < nc; j++) {
                    uint32_t bval;
                    memcpy(&bval, packed->data + offset + j * 4, 4);
                    params[j] = (bval != 0) ? 1 : 0;
                }
            } else {
                /* Integer types — copy directly */
                int std140_sz = uniform_type_std140_size(uni_type);
                if (std140_sz <= 0) std140_sz = 4;
                if ((uint32_t)offset + (uint32_t)std140_sz > packed->size)
                    std140_sz = packed->size - offset;
                memcpy(params, packed->data + offset, std140_sz);
            }
        }
        return;
    }

    /* Legacy mode readback from shadow buffer */
    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    if (ub->valid && ub->shadow_size > 0) {
        uint32_t copy_size = ub->shadow_size;
        if (copy_size > 64) copy_size = 64;
        if (ub->shadow_type == GL_INT) {
            memcpy(params, ub->shadow, copy_size);
        } else {
            /* Convert float shadow to int */
            uint32_t count = copy_size / sizeof(float);
            const float *fdata = (const float *)ub->shadow;
            for (uint32_t i = 0; i < count; i++) {
                params[i] = (GLint)fdata[i];
            }
        }
    }
}

/*
 * Helper: write data to a packed UBO mirror location (dual-stage uniforms).
 * Copies the same data written to the primary packed UBO to the other stage.
 */
static void apply_packed_mirror(sgl_program_t *prog, GLint primary_loc,
                                 const void *data, uint32_t size) {
    if (!(primary_loc & SGL_LOC_PACKED_FLAG)) return;
    int p_stage = (primary_loc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
    int p_binding = (primary_loc >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
    int p_offset = primary_loc & SGL_LOC_OFFSET_MASK;

    for (int m = 0; m < prog->num_packed_mirrors; m++) {
        GLint prim = prog->packed_mirrors[m].primary;
        int m_p_stage = (prim >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int m_p_binding = (prim >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int m_p_offset = prim & SGL_LOC_OFFSET_MASK;
        if (m_p_stage != p_stage || m_p_binding != p_binding) continue;

        /* Compute relative offset from this mirror's base.
         * This allows array element writes (offset > base) to mirror correctly. */
        int rel_offset = p_offset - m_p_offset;
        if (rel_offset < 0) continue;

        GLint mirror = prog->packed_mirrors[m].mirror;
        int mirror_stage = (mirror >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int mirror_binding = (mirror >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int mirror_offset = (mirror & SGL_LOC_OFFSET_MASK) + rel_offset;
        if (mirror_binding >= SGL_MAX_PACKED_UBOS) continue;
        sgl_packed_ubo_t *mp = (mirror_stage == 0)
            ? &prog->packed_vertex[mirror_binding]
            : &prog->packed_fragment[mirror_binding];
        if (mp->valid && (uint32_t)mirror_offset + size <= mp->size) {
            memcpy(mp->data + mirror_offset, data, size);
            mp->dirty = true;
        }
        break;
    }
}

/* Check if a location corresponds to a valid uniform in this program.
 * Returns true if valid (either exact match or within an array range).
 * Used by glGetUniform*v to generate GL_INVALID_OPERATION for bad locations. */
static bool is_valid_uniform_location(sgl_program_t *prog, GLint location) {
    if (location == -1) return false;
    /* Sampler location */
    if (location & SGL_LOC_SAMPLER_FLAG) {
        int idx = location & 0xFFFF;
        return idx >= 0 && idx < prog->num_samplers && prog->samplers[idx].used;
    }
    /* Packed location — must match a program_uniform entry */
    if (location & SGL_LOC_PACKED_FLAG) {
        for (int i = 0; i < prog->num_program_uniforms; i++) {
            if (!prog->program_uniforms[i].used) continue;
            if (prog->program_uniforms[i].location == location) return true;
        }
        /* Check array ranges */
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        for (int i = 0; i < prog->num_program_uniforms; i++) {
            if (!prog->program_uniforms[i].used) continue;
            GLint bloc = prog->program_uniforms[i].location;
            if (!(bloc & SGL_LOC_PACKED_FLAG)) continue;
            int b_stage = (bloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
            int b_binding = (bloc >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
            int b_offset = bloc & SGL_LOC_OFFSET_MASK;
            if (b_stage != stage || b_binding != binding) continue;
            if (prog->program_uniforms[i].array_size <= 1) continue;
            int elem_stride = get_element_stride(&prog->program_uniforms[i]);
            int array_end = b_offset + prog->program_uniforms[i].array_size * elem_stride;
            if (offset >= b_offset && offset < array_end &&
                (offset - b_offset) % elem_stride == 0)
                return true;
        }
        return false;
    }
    /* Legacy location: stage/binding encoding */
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return false;
    int stage = (location >> 16) & 0xFFFF;
    sgl_uniform_binding_t *uniforms = (stage == 0)
        ? prog->vertex_uniforms : prog->fragment_uniforms;
    return uniforms[binding].valid;
}

/*
 * Uniform type validation helpers (for dEQP conformance).
 * Only validates when we have transpiler-provided type metadata.
 */
static const sgl_active_uniform_info_t *find_active_uniform_by_location(
    sgl_program_t *prog, GLint location)
{
    /* Exact match first (fast path) */
    for (int i = 0; i < prog->num_active_uniforms; i++) {
        if (prog->active_uniforms[i].active &&
            prog->active_uniforms[i].location == location)
            return &prog->active_uniforms[i];
    }
    /* Array element: match by stage+binding, check offset within array range */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        for (int i = 0; i < prog->num_active_uniforms; i++) {
            if (!prog->active_uniforms[i].active) continue;
            if (prog->active_uniforms[i].size <= 1) continue;
            GLint aloc = prog->active_uniforms[i].location;
            if (!(aloc & SGL_LOC_PACKED_FLAG)) continue;
            int a_stage = (aloc >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
            int a_binding = (aloc >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
            int a_offset = aloc & SGL_LOC_OFFSET_MASK;
            if (a_stage != stage || a_binding != binding) continue;
            int elem_stride = get_active_element_stride(&prog->active_uniforms[i]);
            int array_end = a_offset + prog->active_uniforms[i].size * elem_stride;
            if (offset >= a_offset && offset < array_end)
                return &prog->active_uniforms[i];
        }
    }
    return NULL;
}

/* Check if a GL type is a float type (float, vecN, matN) */
static bool sgl_is_float_uniform_type(GLenum type) {
    switch (type) {
        case GL_FLOAT: case GL_FLOAT_VEC2: case GL_FLOAT_VEC3: case GL_FLOAT_VEC4:
        case GL_FLOAT_MAT2: case GL_FLOAT_MAT3: case GL_FLOAT_MAT4:
        /* Per GLES2 spec: glUniform*f functions also accept bool/bvec types */
        case GL_BOOL: case GL_BOOL_VEC2: case GL_BOOL_VEC3: case GL_BOOL_VEC4:
            return true;
        default:
            return false;
    }
}

/* Check if a GL type is an integer type (int, ivecN, bool, bvecN) */
static bool sgl_is_int_uniform_type(GLenum type) {
    switch (type) {
        case GL_INT: case GL_INT_VEC2: case GL_INT_VEC3: case GL_INT_VEC4:
        case GL_BOOL: case GL_BOOL_VEC2: case GL_BOOL_VEC3: case GL_BOOL_VEC4:
            return true;
        default:
            return false;
    }
}

/* Check if a GL type is a sampler type */
static bool sgl_is_sampler_type(GLenum type) {
    return type == GL_SAMPLER_2D || type == GL_SAMPLER_CUBE;
}

/* Check if a GL type is a boolean type */
static bool sgl_is_bool_uniform_type(GLenum type) {
    return type == GL_BOOL || type == GL_BOOL_VEC2 ||
           type == GL_BOOL_VEC3 || type == GL_BOOL_VEC4;
}

/* Get the expected component count for a GL type */
static int sgl_uniform_type_components(GLenum type) {
    switch (type) {
        case GL_FLOAT: case GL_INT: case GL_BOOL: case GL_SAMPLER_2D: case GL_SAMPLER_CUBE: return 1;
        case GL_FLOAT_VEC2: case GL_INT_VEC2: case GL_BOOL_VEC2: return 2;
        case GL_FLOAT_VEC3: case GL_INT_VEC3: case GL_BOOL_VEC3: return 3;
        case GL_FLOAT_VEC4: case GL_INT_VEC4: case GL_BOOL_VEC4: return 4;
        case GL_FLOAT_MAT2: return 4;  /* 2x2 */
        case GL_FLOAT_MAT3: return 9;  /* 3x3 */
        case GL_FLOAT_MAT4: return 16; /* 4x4 */
        default: return 4;
    }
}

/* Validate a glUniform*f[v] call. Returns true if valid, false if GL_INVALID_OPERATION should be set. */
static bool sgl_validate_float_uniform(sgl_program_t *prog, GLint location,
                                        int num_components, GLsizei count)
{
    const sgl_active_uniform_info_t *info = find_active_uniform_by_location(prog, location);
    if (!info) return true; /* No metadata = legacy path, allow */

    /* Float calls on samplers → GL_INVALID_OPERATION */
    if (sgl_is_sampler_type(info->type)) return false;
    /* Float calls on int/bool uniforms → GL_INVALID_OPERATION */
    if (!sgl_is_float_uniform_type(info->type)) return false;
    /* Component count mismatch (e.g. glUniform1f on vec4) */
    if (sgl_uniform_type_components(info->type) != num_components) return false;
    /* Count > 1 on non-array uniform */
    if (count > 1 && info->size <= 1) return false;
    return true;
}

/* Validate a glUniform*i[v] call. Returns true if valid. */
static bool sgl_validate_int_uniform(sgl_program_t *prog, GLint location,
                                      int num_components, GLsizei count)
{
    const sgl_active_uniform_info_t *info = find_active_uniform_by_location(prog, location);
    if (!info) return true; /* No metadata = legacy path, allow */

    /* glUniform1i/1iv on sampler is valid (texture unit assignment, including arrays) */
    if (sgl_is_sampler_type(info->type) && num_components == 1) return true;
    /* Int calls on sampler with wrong signature → GL_INVALID_OPERATION */
    if (sgl_is_sampler_type(info->type)) return false;
    /* Int calls on float uniforms → GL_INVALID_OPERATION */
    if (!sgl_is_int_uniform_type(info->type)) return false;
    /* Component count mismatch */
    if (sgl_uniform_type_components(info->type) != num_components) return false;
    /* Count > 1 on non-array */
    if (count > 1 && info->size <= 1) return false;
    return true;
}

/* Validate a glUniformMatrix*fv call. Returns true if valid. */
static bool sgl_validate_matrix_uniform(sgl_program_t *prog, GLint location,
                                         GLenum expected_type, GLsizei count)
{
    const sgl_active_uniform_info_t *info = find_active_uniform_by_location(prog, location);
    if (!info) return true; /* No metadata = legacy path, allow */

    /* Matrix calls on non-matrix or wrong matrix size → GL_INVALID_OPERATION */
    if (info->type != expected_type) return false;
    /* Count > 1 on non-array */
    if (count > 1 && info->size <= 1) return false;
    return true;
}

/*
 * Helper to set a float uniform (1-4 components)
 * std140 layout: all uniforms are padded to 16 bytes (vec4)
 *
 * IMPORTANT: We allocate a NEW offset for each glUniform call to avoid
 * data races when multiple draws use different values in the same frame.
 * pushConstants copies data to the GPU address, but if multiple draws
 * use the same address, later draws overwrite earlier ones before the
 * GPU executes them.
 */
static void set_float_uniform(GLint location, int num_components, GLsizei count, const GLfloat *values) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;

    /* GLES2: count < 0 → GL_INVALID_VALUE */
    if (count < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* No program in use → GL_INVALID_OPERATION (must check BEFORE location == -1) */
    if (ctx->current_program == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    if (location == -1) return;

    /* Reject invalid locations: both packed+sampler flags set is impossible
     * for real locations (catches -2, -3, etc. from dEQP negative tests). */
    if ((location & SGL_LOC_PACKED_FLAG) && (location & SGL_LOC_SAMPLER_FLAG)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Sampler locations: float writes are GL_INVALID_OPERATION per spec */
    if (location & SGL_LOC_SAMPLER_FLAG) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Validate type/count against declared uniform metadata (if available) */
    if (!sgl_validate_float_uniform(prog, location, num_components, count)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Packed mode: write directly to shadow buffer */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;

        if (binding >= SGL_MAX_PACKED_UBOS) return;  /* Bounds check */

        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];

        if (!packed->valid) return;

        /* Bool uniforms: convert to Mesa's native boolean representation.
         * Mesa sets UniformBooleanTrue = ~0U (0xFFFFFFFF), so compiled shaders
         * expect true = 0xFFFFFFFF, false = 0x00000000 in the constant buffer.
         * Use two lookup methods for robustness. */
        const sgl_active_uniform_info_t *ainfo = find_active_uniform_by_location(prog, location);
        bool is_bool = ainfo && sgl_is_bool_uniform_type(ainfo->type);
        if (!is_bool) {
            GLenum ptype = find_packed_uniform_type(prog, location);
            if (sgl_is_bool_uniform_type(ptype))
                is_bool = true;
        }

        if (count == 1) {
            /* Single value: write exact bytes (no array padding) */
            uint32_t dataSize = num_components * sizeof(float);
            if (offset + dataSize > packed->size) return;
            if (is_bool) {
                uint32_t bvals[4];
                for (int j = 0; j < num_components; j++)
                    bvals[j] = (values[j] != 0.0f) ? 0xFFFFFFFFu : 0x00000000u;
                memcpy(packed->data + offset, bvals, dataSize);
            } else {
                memcpy(packed->data + offset, values, dataSize);
            }
        } else {
            /* Array: stride depends on compilation path (Mesa constbuf vs std140 UBO) */
            int stride = lookup_element_stride(prog, location);
            uint32_t totalSize = count * stride;
            if (offset + totalSize > packed->size) return;
            uint32_t elemBytes = num_components * sizeof(float);
            for (GLsizei e = 0; e < count; e++) {
                uint32_t eoff = offset + e * stride;
                memset(packed->data + eoff, 0, stride);
                if (is_bool) {
                    uint32_t bvals[4];
                    for (int j = 0; j < num_components; j++)
                        bvals[j] = (values[e * num_components + j] != 0.0f) ? 0xFFFFFFFFu : 0x00000000u;
                    memcpy(packed->data + eoff, bvals, num_components * sizeof(uint32_t));
                } else {
                    memcpy(packed->data + eoff,
                           values + e * num_components,
                           elemBytes);
                }
            }
        }
        packed->dirty = true;
        {
            int stride = lookup_element_stride(prog, location);
            uint32_t writtenSize = (count == 1)
                ? num_components * sizeof(float)
                : (uint32_t)count * stride;
            apply_packed_mirror(prog, location, packed->data + offset, writtenSize);
        }
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;

    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* Clamp count to stack buffer limit */
    GLsizei clampedCount = count > 64 ? 64 : count;

    /* std140: each array element padded to 16 bytes (vec4) */
    uint32_t dataSize = clampedCount * 16;
    uint32_t alignedSize = SGL_ALIGN_UP(dataSize, SGL_UNIFORM_ALIGNMENT);

    /* ALWAYS allocate a new offset for each uniform write.
     * This ensures each draw gets unique uniform data, preventing
     * data races in the GPU command buffer. */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    /* Write data via backend - pad each element to vec4 */
    if (ub->valid && ctx->backend->ops->write_uniform) {
        float array_data[4 * 64]; /* support up to 64 elements on stack */
        memset(array_data, 0, clampedCount * 16);
        for (GLsizei e = 0; e < clampedCount; e++) {
            for (int j = 0; j < num_components && j < 4; j++) {
                array_data[e * 4 + j] = values[e * num_components + j];
            }
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, array_data, clampedCount * 16);

        /* Save shadow copy for glGetUniformfv readback (first element only) */
        uint32_t shadow_bytes = (uint32_t)num_components * sizeof(float);
        if (shadow_bytes > 64) shadow_bytes = 64;
        memcpy(ub->shadow, values, shadow_bytes);
        ub->shadow_size = shadow_bytes;
        ub->shadow_components = num_components;
        ub->shadow_type = GL_FLOAT;
    }

    ub->dirty = true;
}

GL_APICALL void GL_APIENTRY glUniform1f(GLint location, GLfloat v0) {
    GLfloat values[1] = { v0 };
    set_float_uniform(location, 1, 1, values);
    SGL_TRACE_UNIFORM("glUniform1f(loc=%d, %.2f)", location, v0);
}

GL_APICALL void GL_APIENTRY glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    GLfloat values[2] = { v0, v1 };
    set_float_uniform(location, 2, 1, values);
    SGL_TRACE_UNIFORM("glUniform2f(loc=%d, %.2f, %.2f)", location, v0, v1);
}

GL_APICALL void GL_APIENTRY glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    GLfloat values[3] = { v0, v1, v2 };
    set_float_uniform(location, 3, 1, values);
    SGL_TRACE_UNIFORM("glUniform3f(loc=%d, %.2f, %.2f, %.2f)", location, v0, v1, v2);
}

GL_APICALL void GL_APIENTRY glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    GLfloat values[4] = { v0, v1, v2, v3 };
    set_float_uniform(location, 4, 1, values);
    SGL_TRACE_UNIFORM("glUniform4f(loc=%d, %.2f, %.2f, %.2f, %.2f)", location, v0, v1, v2, v3);
}

/*
 * Helper to set an integer uniform (1-4 components)
 * std140 layout: integers are also 4 bytes each, padded to 16 bytes
 * Note: For samplers (glUniform1i), the value is the texture unit index
 *
 * IMPORTANT: We allocate a NEW offset for each glUniform call to avoid
 * data races when multiple draws use different values in the same frame.
 */
static void set_int_uniform(GLint location, int num_components, GLsizei count, const GLint *values) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;

    /* GLES2: count < 0 → GL_INVALID_VALUE */
    if (count < 0) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    /* No program in use → GL_INVALID_OPERATION (must check BEFORE location == -1) */
    if (ctx->current_program == 0) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    if (location == -1) return;

    /* Reject invalid locations: both packed+sampler flags set is impossible
     * for real locations (catches -2, -3, etc. from dEQP negative tests). */
    if ((location & SGL_LOC_PACKED_FLAG) && (location & SGL_LOC_SAMPLER_FLAG)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Sampler mode: glUniform1i/1iv sets texture unit (num_components must be 1) */
    if (location & SGL_LOC_SAMPLER_FLAG) {
        if (num_components != 1) {
            sgl_set_error(ctx, GL_INVALID_OPERATION);
            return;
        }
        int sampler_idx = location & 0xFFFF;
        /* Set tex_unit for each element (count=1 for single, count>1 for sampler arrays) */
        for (GLsizei e = 0; e < count; e++) {
            int si = sampler_idx + e;
            if (si >= 0 && si < prog->num_samplers) {
                prog->samplers[si].tex_unit = values[e];
                SGL_TRACE_UNIFORM("sampler[%d] '%s': binding=%d -> tex_unit=%d",
                                  si, prog->samplers[si].name,
                                  prog->samplers[si].shader_binding, values[e]);
            }
        }
        return;
    }

    /* Validate type/count against declared uniform metadata (if available) */
    if (!sgl_validate_int_uniform(prog, location, num_components, count)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Packed mode: write directly to shadow buffer */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;

        if (binding >= SGL_MAX_PACKED_UBOS) return;  /* Bounds check */

        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];

        if (!packed->valid) return;

        /* Bool uniforms: convert to Mesa's native boolean representation.
         * Mesa sets UniformBooleanTrue = ~0U (0xFFFFFFFF), so compiled shaders
         * expect true = 0xFFFFFFFF, false = 0x00000000 in the constant buffer.
         * The shader's `bool == bool` comparison uses integer equality (ISETP.EQ),
         * so the exact bit pattern matters — 1.0f (0x3F800000) would fail.
         * Writing raw int 1 (0x00000001) is also wrong (Maxwell flushes denormals).
         * Use two lookup methods for robustness: active_uniforms (primary)
         * and program_uniforms (fallback). */
        const sgl_active_uniform_info_t *ainfo = find_active_uniform_by_location(prog, location);
        bool is_bool = ainfo && sgl_is_bool_uniform_type(ainfo->type);
        if (!is_bool) {
            /* Fallback: search program_uniforms[] directly */
            GLenum ptype = find_packed_uniform_type(prog, location);
            if (sgl_is_bool_uniform_type(ptype))
                is_bool = true;
        }

        if (count == 1) {
            /* Single value: write exact bytes (no array padding) */
            uint32_t dataSize = num_components * sizeof(int32_t);
            if (offset + dataSize > packed->size) return;
            if (is_bool) {
                uint32_t bvals[4];
                for (int j = 0; j < num_components; j++)
                    bvals[j] = (values[j] != 0) ? 0xFFFFFFFFu : 0x00000000u;
                memcpy(packed->data + offset, bvals, dataSize);
            } else {
                memcpy(packed->data + offset, values, dataSize);
            }
        } else {
            /* Array: stride depends on compilation path (Mesa constbuf vs std140 UBO) */
            int stride = lookup_element_stride(prog, location);
            uint32_t totalSize = count * stride;
            if (offset + totalSize > packed->size) return;
            uint32_t elemBytes = num_components * sizeof(int32_t);
            for (GLsizei e = 0; e < count; e++) {
                uint32_t eoff = offset + e * stride;
                memset(packed->data + eoff, 0, stride);
                if (is_bool) {
                    uint32_t bvals[4];
                    for (int j = 0; j < num_components; j++)
                        bvals[j] = (values[e * num_components + j] != 0) ? 0xFFFFFFFFu : 0x00000000u;
                    memcpy(packed->data + eoff, bvals, num_components * sizeof(uint32_t));
                } else {
                    memcpy(packed->data + eoff,
                           values + e * num_components,
                           elemBytes);
                }
            }
        }
        packed->dirty = true;
        {
            int stride = lookup_element_stride(prog, location);
            uint32_t writtenSize = (count == 1)
                ? num_components * sizeof(int32_t)
                : (uint32_t)count * stride;
            apply_packed_mirror(prog, location, packed->data + offset, writtenSize);
        }
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;

    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* Clamp count to stack buffer limit */
    GLsizei clampedCount = count > 64 ? 64 : count;

    /* std140: each array element padded to 16 bytes (ivec4) */
    uint32_t dataSize = clampedCount * 16;
    uint32_t alignedSize = SGL_ALIGN_UP(dataSize, SGL_UNIFORM_ALIGNMENT);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    /* Write data via backend - pad each element to ivec4 */
    if (ub->valid && ctx->backend->ops->write_uniform) {
        int32_t array_data[4 * 64]; /* support up to 64 elements on stack */
        memset(array_data, 0, clampedCount * 16);
        for (GLsizei e = 0; e < clampedCount; e++) {
            for (int j = 0; j < num_components && j < 4; j++) {
                array_data[e * 4 + j] = values[e * num_components + j];
            }
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, array_data, clampedCount * 16);

        /* Save shadow copy for glGetUniformiv readback (first element only) */
        uint32_t shadow_bytes = (uint32_t)num_components * sizeof(int32_t);
        if (shadow_bytes > 64) shadow_bytes = 64;
        memcpy(ub->shadow, values, shadow_bytes);
        ub->shadow_size = shadow_bytes;
        ub->shadow_components = num_components;
        ub->shadow_type = GL_INT;
    }

    ub->dirty = true;
}

GL_APICALL void GL_APIENTRY glUniform1i(GLint location, GLint v0) {
    GLint values[1] = { v0 };
    set_int_uniform(location, 1, 1, values);
    SGL_TRACE_UNIFORM("glUniform1i(loc=%d, %d)", location, v0);
}

GL_APICALL void GL_APIENTRY glUniform2i(GLint location, GLint v0, GLint v1) {
    GLint values[2] = { v0, v1 };
    set_int_uniform(location, 2, 1, values);
    SGL_TRACE_UNIFORM("glUniform2i(loc=%d, %d, %d)", location, v0, v1);
}

GL_APICALL void GL_APIENTRY glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    GLint values[3] = { v0, v1, v2 };
    set_int_uniform(location, 3, 1, values);
    SGL_TRACE_UNIFORM("glUniform3i(loc=%d, %d, %d, %d)", location, v0, v1, v2);
}

GL_APICALL void GL_APIENTRY glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    GLint values[4] = { v0, v1, v2, v3 };
    set_int_uniform(location, 4, 1, values);
    SGL_TRACE_UNIFORM("glUniform4i(loc=%d, %d, %d, %d, %d)", location, v0, v1, v2, v3);
}

/* Vector variants (fv) - support count>1 for uniform arrays */
GL_APICALL void GL_APIENTRY glUniform1fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_float_uniform(location, 1, count, value);
    SGL_TRACE_UNIFORM("glUniform1fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform2fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_float_uniform(location, 2, count, value);
    SGL_TRACE_UNIFORM("glUniform2fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_float_uniform(location, 3, count, value);
    SGL_TRACE_UNIFORM("glUniform3fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_float_uniform(location, 4, count, value);
    SGL_TRACE_UNIFORM("glUniform4fv(loc=%d, count=%d)", location, count);
}

/* Vector variants (iv) - support count>1 for uniform arrays */
GL_APICALL void GL_APIENTRY glUniform1iv(GLint location, GLsizei count, const GLint *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_int_uniform(location, 1, count, value);
    SGL_TRACE_UNIFORM("glUniform1iv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform2iv(GLint location, GLsizei count, const GLint *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_int_uniform(location, 2, count, value);
    SGL_TRACE_UNIFORM("glUniform2iv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform3iv(GLint location, GLsizei count, const GLint *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_int_uniform(location, 3, count, value);
    SGL_TRACE_UNIFORM("glUniform3iv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniform4iv(GLint location, GLsizei count, const GLint *value) {
    if (count < 0) { sgl_context_t *c = sgl_get_current_context(); if (c) sgl_set_error(c, GL_INVALID_VALUE); return; }
    if (count == 0 || !value) return;
    set_int_uniform(location, 4, count, value);
    SGL_TRACE_UNIFORM("glUniform4iv(loc=%d, count=%d)", location, count);
}

/*
 * Matrix uniforms
 * std140 layout: mat2 = 2 vec4 (32 bytes), mat3 = 3 vec4 (48 bytes), mat4 = 4 vec4 (64 bytes)
 */
GL_APICALL void GL_APIENTRY glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;

    /* GLES2 spec: transpose must be GL_FALSE */
    if (transpose != GL_FALSE) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (count < 0) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }

    /* Program check BEFORE location == -1 early return (dEQP requires this) */
    if (ctx->current_program == 0) { sgl_set_error(ctx, GL_INVALID_OPERATION); return; }

    if (location == -1 || count == 0 || !value) return;

    /* Reject invalid locations (both flags set = impossible for real locations) */
    if ((location & SGL_LOC_PACKED_FLAG) && (location & SGL_LOC_SAMPLER_FLAG)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (location & SGL_LOC_SAMPLER_FLAG) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog || !prog->linked) { sgl_set_error(ctx, GL_INVALID_OPERATION); return; }

    /* Validate type/count against declared uniform metadata */
    if (!sgl_validate_matrix_uniform(prog, location, GL_FLOAT_MAT2, count)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Packed mode: write std140 mat2 to shadow buffer */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        uint32_t dataSize = 32 * count; /* mat2 std140: 2 vec4 = 32 bytes */
        if (!packed->valid || offset + dataSize > packed->size) return;
        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 4;
            float *dst = (float *)(packed->data + offset + m * 32);
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = 0.0f; dst[3] = 0.0f;
            dst[4] = src[2]; dst[5] = src[3]; dst[6] = 0.0f; dst[7] = 0.0f;
        }
        packed->dirty = true;
        apply_packed_mirror(prog, location, packed->data + offset, dataSize);
        SGL_TRACE_UNIFORM("glUniformMatrix2fv(packed loc=0x%X, count=%d)", location, count);
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* mat2 in std140: 2 columns of vec4 (padded from vec2) = 32 bytes */
    uint32_t dataSize = 32 * count;
    uint32_t alignedSize = SGL_ALIGN_UP(dataSize, SGL_UNIFORM_ALIGNMENT);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    if (ub->valid && ctx->backend->ops->write_uniform) {
        /* Convert mat2 (4 floats) to std140 layout (2 vec4 = 8 floats) */
        float std140_data[8 * 4]; /* Support up to 4 matrices */
        if (count > 4) count = 4;
        dataSize = 32 * count;  /* Recompute after clamping to avoid buffer over-read */

        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 4;
            float *dst = std140_data + m * 8;
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = 0.0f; dst[3] = 0.0f;
            dst[4] = src[2]; dst[5] = src[3]; dst[6] = 0.0f; dst[7] = 0.0f;
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, std140_data, dataSize);

        /* Save shadow copy (first matrix = 32 bytes std140) */
        memcpy(ub->shadow, std140_data, 32);
        ub->shadow_size = 32;
        ub->shadow_components = 4;
        ub->shadow_type = GL_FLOAT;
    }

    ub->dirty = true;
    SGL_TRACE_UNIFORM("glUniformMatrix2fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;

    /* GLES2 spec: transpose must be GL_FALSE */
    if (transpose != GL_FALSE) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (count < 0) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }

    /* Program check BEFORE location == -1 early return (dEQP requires this) */
    if (ctx->current_program == 0) { sgl_set_error(ctx, GL_INVALID_OPERATION); return; }

    if (location == -1 || count == 0 || !value) return;

    /* Reject invalid locations (both flags set = impossible for real locations) */
    if ((location & SGL_LOC_PACKED_FLAG) && (location & SGL_LOC_SAMPLER_FLAG)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (location & SGL_LOC_SAMPLER_FLAG) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog || !prog->linked) { sgl_set_error(ctx, GL_INVALID_OPERATION); return; }

    /* Validate type/count against declared uniform metadata */
    if (!sgl_validate_matrix_uniform(prog, location, GL_FLOAT_MAT3, count)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Packed mode: write std140 mat3 to shadow buffer */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        uint32_t dataSize = 48 * count; /* mat3 std140: 3 vec4 = 48 bytes */
        if (!packed->valid || offset + dataSize > packed->size) return;
        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 9;
            float *dst = (float *)(packed->data + offset + m * 48);
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 0.0f;
            dst[4] = src[3]; dst[5] = src[4]; dst[6] = src[5]; dst[7] = 0.0f;
            dst[8] = src[6]; dst[9] = src[7]; dst[10] = src[8]; dst[11] = 0.0f;
        }
        packed->dirty = true;
        apply_packed_mirror(prog, location, packed->data + offset, dataSize);
        SGL_TRACE_UNIFORM("glUniformMatrix3fv(packed loc=0x%X, count=%d)", location, count);
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* mat3 in std140: 3 columns of vec4 (padded from vec3) = 48 bytes */
    uint32_t dataSize = 48 * count;
    uint32_t alignedSize = SGL_ALIGN_UP(dataSize, SGL_UNIFORM_ALIGNMENT);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, alignedSize);
        ub->size = alignedSize;
        ub->data_size = dataSize;
        ub->valid = true;
    }

    if (ub->valid && ctx->backend->ops->write_uniform) {
        /* Convert mat3 (9 floats) to std140 layout (3 vec4 = 12 floats) */
        float std140_data[12 * 4]; /* Support up to 4 matrices */
        if (count > 4) count = 4;
        dataSize = 48 * count;  /* Recompute after clamping to avoid buffer over-read */

        for (GLsizei m = 0; m < count; m++) {
            const float *src = value + m * 9;
            float *dst = std140_data + m * 12;
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 0.0f;
            dst[4] = src[3]; dst[5] = src[4]; dst[6] = src[5]; dst[7] = 0.0f;
            dst[8] = src[6]; dst[9] = src[7]; dst[10] = src[8]; dst[11] = 0.0f;
        }
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, std140_data, dataSize);

        /* Save shadow copy (first matrix = 48 bytes std140) */
        memcpy(ub->shadow, std140_data, 48);
        ub->shadow_size = 48;
        ub->shadow_components = 9;
        ub->shadow_type = GL_FLOAT;
    }

    ub->dirty = true;
    SGL_TRACE_UNIFORM("glUniformMatrix3fv(loc=%d, count=%d)", location, count);
}

GL_APICALL void GL_APIENTRY glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    sgl_context_t *ctx = sgl_get_current_context();
    if (!ctx || !ctx->backend) return;

    /* GLES2 spec: transpose must be GL_FALSE */
    if (transpose != GL_FALSE) {
        sgl_set_error(ctx, GL_INVALID_VALUE);
        return;
    }

    if (count < 0) { sgl_set_error(ctx, GL_INVALID_VALUE); return; }

    /* Program check BEFORE location == -1 early return (dEQP requires this) */
    if (ctx->current_program == 0) { sgl_set_error(ctx, GL_INVALID_OPERATION); return; }

    if (location == -1 || count == 0 || !value) return;

    /* Reject invalid locations (both flags set = impossible for real locations) */
    if ((location & SGL_LOC_PACKED_FLAG) && (location & SGL_LOC_SAMPLER_FLAG)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    if (location & SGL_LOC_SAMPLER_FLAG) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    sgl_program_t *prog = GET_PROGRAM(ctx->current_program);
    if (!prog || !prog->linked) { sgl_set_error(ctx, GL_INVALID_OPERATION); return; }

    /* Validate type/count against declared uniform metadata */
    if (!sgl_validate_matrix_uniform(prog, location, GL_FLOAT_MAT4, count)) {
        sgl_set_error(ctx, GL_INVALID_OPERATION);
        return;
    }

    /* Packed mode: write std140 mat4 to shadow buffer */
    if (location & SGL_LOC_PACKED_FLAG) {
        int stage = (location >> SGL_LOC_STAGE_SHIFT) & SGL_LOC_STAGE_MASK;
        int binding = (location >> SGL_LOC_BINDING_SHIFT) & SGL_LOC_BINDING_MASK;
        int offset = location & SGL_LOC_OFFSET_MASK;
        sgl_packed_ubo_t *packed = (stage == 0)
            ? &prog->packed_vertex[binding]
            : &prog->packed_fragment[binding];
        uint32_t dataSize = 64 * count; /* mat4 std140: 4 vec4 = 64 bytes */
        if (!packed->valid || offset + dataSize > packed->size) return;
        memcpy(packed->data + offset, value, dataSize);
        packed->dirty = true;
        apply_packed_mirror(prog, location, value, dataSize);
        SGL_TRACE_UNIFORM("glUniformMatrix4fv(packed loc=0x%X, count=%d)", location, count);
        return;
    }

    int stage = (location >> 16) & 0xFFFF;
    int binding = location & 0xFFFF;
    if (binding >= SGL_MAX_UNIFORMS) return;

    sgl_uniform_binding_t *uniforms = (stage == 0) ? prog->vertex_uniforms : prog->fragment_uniforms;
    sgl_uniform_binding_t *ub = &uniforms[binding];

    /* mat4 in std140: 4 columns of vec4 = 64 bytes */
    uint32_t data_size = 64 * count;
    uint32_t aligned_size = SGL_ALIGN_UP(data_size, SGL_UNIFORM_ALIGNMENT);

    /* ALWAYS allocate new offset to avoid data races between draws */
    if (ctx->backend->ops->alloc_uniform) {
        ub->offset = ctx->backend->ops->alloc_uniform(ctx->backend, aligned_size);
        ub->size = aligned_size;
        ub->data_size = data_size;
        ub->valid = true;
    }

    if (!ub->valid) return;

    if (ub->valid && ctx->backend->ops->write_uniform) {
        ctx->backend->ops->write_uniform(ctx->backend, ub->offset, value, data_size);
        memcpy(ub->shadow, value, 64);
        ub->shadow_size = 64;
        ub->shadow_components = 16;
        ub->shadow_type = GL_FLOAT;
    }

    ub->dirty = true;

    SGL_TRACE_UNIFORM("glUniformMatrix4fv(loc=%d, count=%d)", location, count);
}
