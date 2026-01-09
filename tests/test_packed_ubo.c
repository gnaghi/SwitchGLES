/*
 * SwitchGLES - Packed UBO Validation Test
 *
 * This test validates the CPU-side logic of the packed UBO implementation
 * without requiring GPU hardware. It checks:
 * - Location encoding (packed vs legacy)
 * - Shadow buffer byte-level correctness
 * - std140 layout compliance for all types
 * - Registration system (register, lookup, clear)
 * - Bone matrix size limits
 * - Uniform buffer capacity estimation
 * - Matrix transpose handling
 *
 * Build (host, no GPU):
 *   gcc -I../../include -I../../source/context -DSGL_TEST_HOST \
 *       test_packed_ubo.c -o test_packed_ubo -lm
 *
 * Or on Switch devkit (cross-compile):
 *   aarch64-none-elf-gcc -I../../include -I../../source/context \
 *       -DSGL_TEST_HOST test_packed_ubo.c -o test_packed_ubo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Minimal reimplementation of switchGLES types and functions for testing.
 * This avoids pulling in deko3d headers on host builds.
 * ============================================================================ */

/* From gl2.h - just the types we need */
typedef unsigned int GLenum;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;

#define GL_TRUE  1
#define GL_FALSE 0
#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30

/* From sgl_gl_types.h */
#define SGL_MAX_PACKED_UBO_SIZE  4096
#define SGL_MAX_PACKED_UBOS      2
#define SGL_MAX_UNIFORMS         16
#define SGL_UNIFORM_BUF_SIZE     (64 * 1024)
#define SGL_UNIFORM_ALIGNMENT    0x100

/* From gl2sgl.h */
#define SGL_STAGE_VERTEX   0
#define SGL_STAGE_FRAGMENT 1
#define SGL_VERT SGL_STAGE_VERTEX
#define SGL_FRAG SGL_STAGE_FRAGMENT

/* Packed UBO shadow buffer */
typedef struct sgl_packed_ubo {
    uint8_t data[SGL_MAX_PACKED_UBO_SIZE];
    uint32_t size;
    bool dirty;
    bool valid;
} sgl_packed_ubo_t;

/* Uniform binding info */
typedef struct sgl_uniform_binding {
    bool valid;
    uint32_t offset;
    uint32_t size;
    uint32_t data_size;
    bool dirty;
} sgl_uniform_binding_t;

/* ============================================================================
 * Reimplementation of the registration system (mirrors gl_uniform.c)
 * ============================================================================ */

#define SGL_MAX_REGISTERED_UNIFORMS 256
#define SGL_UNIFORM_NAME_MAX 64

typedef struct {
    char name[SGL_UNIFORM_NAME_MAX];
    int stage;
    int binding;
    int byte_offset;
    bool used;
} sgl_uniform_entry_t;

static sgl_uniform_entry_t s_registered_uniforms[SGL_MAX_REGISTERED_UNIFORMS];
static int s_registered_count = 0;
static bool s_registry_initialized = false;
static int s_packed_ubo_sizes[2][SGL_MAX_PACKED_UBOS];

static void sglClearUniformRegistry(void) {
    for (int i = 0; i < s_registered_count; i++) {
        s_registered_uniforms[i].used = false;
    }
    s_registered_count = 0;
    memset(s_packed_ubo_sizes, 0, sizeof(s_packed_ubo_sizes));
}

static GLboolean sglRegisterUniform(const char *name, GLint stage, GLint binding) {
    if (!name || stage < 0 || stage > 1 || binding < 0 || binding >= SGL_MAX_UNIFORMS)
        return GL_FALSE;
    size_t len = strlen(name);
    if (len == 0 || len >= SGL_UNIFORM_NAME_MAX) return GL_FALSE;

    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used && strcmp(s_registered_uniforms[i].name, name) == 0) {
            s_registered_uniforms[i].stage = stage;
            s_registered_uniforms[i].binding = binding;
            s_registered_uniforms[i].byte_offset = -1;
            return GL_TRUE;
        }
    }

    if (s_registered_count >= SGL_MAX_REGISTERED_UNIFORMS) return GL_FALSE;
    int slot = s_registered_count++;
    strcpy(s_registered_uniforms[slot].name, name);
    s_registered_uniforms[slot].stage = stage;
    s_registered_uniforms[slot].binding = binding;
    s_registered_uniforms[slot].byte_offset = -1;
    s_registered_uniforms[slot].used = true;
    s_registry_initialized = true;
    return GL_TRUE;
}

static void sglSetPackedUBOSize(GLint stage, GLint binding, GLint size) {
    if (stage < 0 || stage > 1) return;
    if (binding < 0 || binding >= SGL_MAX_PACKED_UBOS) return;
    if (size < 0 || size > SGL_MAX_PACKED_UBO_SIZE) return;
    s_packed_ubo_sizes[stage][binding] = size;
}

static GLboolean sglRegisterPackedUniform(const char *name, GLint stage,
                                           GLint binding, GLint byte_offset) {
    if (!name || stage < 0 || stage > 1) return GL_FALSE;
    if (binding < 0 || binding >= SGL_MAX_PACKED_UBOS) return GL_FALSE;
    if (byte_offset < 0 || byte_offset >= SGL_MAX_PACKED_UBO_SIZE) return GL_FALSE;

    size_t len = strlen(name);
    if (len == 0 || len >= SGL_UNIFORM_NAME_MAX) return GL_FALSE;

    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used && strcmp(s_registered_uniforms[i].name, name) == 0) {
            s_registered_uniforms[i].stage = stage;
            s_registered_uniforms[i].binding = binding;
            s_registered_uniforms[i].byte_offset = byte_offset;
            return GL_TRUE;
        }
    }

    if (s_registered_count >= SGL_MAX_REGISTERED_UNIFORMS) return GL_FALSE;
    int slot = s_registered_count++;
    strcpy(s_registered_uniforms[slot].name, name);
    s_registered_uniforms[slot].stage = stage;
    s_registered_uniforms[slot].binding = binding;
    s_registered_uniforms[slot].byte_offset = byte_offset;
    s_registered_uniforms[slot].used = true;
    s_registry_initialized = true;
    return GL_TRUE;
}

/* Location flag bits — must match sgl_gl_types.h.
 * CRITICAL: bit 31 must stay 0 so GLint is always >= 0.
 * Apps (including dEQP) check "loc < 0" to skip inactive uniforms. */
#define TEST_LOC_PACKED_FLAG  (1u << 30)
#define TEST_LOC_STAGE_SHIFT  24
#define TEST_LOC_STAGE_MASK   0x1F

static GLint lookup_registered_uniform(const char *name) {
    if (!s_registry_initialized) return -1;
    for (int i = 0; i < s_registered_count; i++) {
        if (s_registered_uniforms[i].used && strcmp(s_registered_uniforms[i].name, name) == 0) {
            if (s_registered_uniforms[i].byte_offset >= 0) {
                return (GLint)(TEST_LOC_PACKED_FLAG |
                       ((unsigned)s_registered_uniforms[i].stage << TEST_LOC_STAGE_SHIFT) |
                       ((unsigned)s_registered_uniforms[i].binding << 16) |
                       (unsigned)s_registered_uniforms[i].byte_offset);
            } else {
                return (s_registered_uniforms[i].stage << 16) | s_registered_uniforms[i].binding;
            }
        }
    }
    return -1;
}

/* ============================================================================
 * Packed UBO write simulation (mirrors gl_uniform.c logic)
 * ============================================================================ */

static void packed_write_float(sgl_packed_ubo_t *packed, int offset, int num_components, const float *values) {
    uint32_t dataSize = num_components * sizeof(float);
    if (!packed->valid || (uint32_t)offset + dataSize > packed->size) return;
    memcpy(packed->data + offset, values, dataSize);
    packed->dirty = true;
}

static void packed_write_int(sgl_packed_ubo_t *packed, int offset, int num_components, const int *values) {
    uint32_t dataSize = num_components * sizeof(int);
    if (!packed->valid || (uint32_t)offset + dataSize > packed->size) return;
    memcpy(packed->data + offset, values, dataSize);
    packed->dirty = true;
}

static void packed_write_mat4(sgl_packed_ubo_t *packed, int offset, int count,
                               GLboolean transpose, const float *value) {
    uint32_t dataSize = 64 * count;
    if (!packed->valid || (uint32_t)offset + dataSize > packed->size) return;
    if (transpose == GL_FALSE) {
        memcpy(packed->data + offset, value, dataSize);
    } else {
        for (int m = 0; m < count; m++) {
            const float *src = value + m * 16;
            float *dst = (float *)(packed->data + offset + m * 64);
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++)
                    dst[i * 4 + j] = src[j * 4 + i];
        }
    }
    packed->dirty = true;
}

static void packed_write_mat3(sgl_packed_ubo_t *packed, int offset, int count,
                               GLboolean transpose, const float *value) {
    uint32_t dataSize = 48 * count;
    if (!packed->valid || (uint32_t)offset + dataSize > packed->size) return;
    for (int m = 0; m < count; m++) {
        const float *src = value + m * 9;
        float *dst = (float *)(packed->data + offset + m * 48);
        if (transpose == GL_FALSE) {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 0.0f;
            dst[4] = src[3]; dst[5] = src[4]; dst[6] = src[5]; dst[7] = 0.0f;
            dst[8] = src[6]; dst[9] = src[7]; dst[10] = src[8]; dst[11] = 0.0f;
        } else {
            dst[0] = src[0]; dst[1] = src[3]; dst[2] = src[6]; dst[3] = 0.0f;
            dst[4] = src[1]; dst[5] = src[4]; dst[6] = src[7]; dst[7] = 0.0f;
            dst[8] = src[2]; dst[9] = src[5]; dst[10] = src[8]; dst[11] = 0.0f;
        }
    }
    packed->dirty = true;
}

/* ============================================================================
 * Test Framework
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST: %-60s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("[PASS]\n"); } while(0)
#define FAIL(msg) do { tests_failed++; printf("[FAIL] %s\n", msg); } while(0)

#define ASSERT_EQ(a, b, msg) do { if ((a) != (b)) { FAIL(msg); return; } } while(0)
#define ASSERT_TRUE(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)
#define ASSERT_FLOAT_EQ(a, b, msg) do { if (fabsf((a) - (b)) > 1e-6f) { FAIL(msg); return; } } while(0)

/* ============================================================================
 * Test: Location Encoding
 * ============================================================================ */

static void test_packed_location_encoding(void) {
    TEST("Packed location: packed flag set, stage/binding/offset correct, loc >= 0");

    sglClearUniformRegistry();
    sglSetPackedUBOSize(SGL_VERT, 0, 576);
    sglRegisterPackedUniform("u_ModelViewProjectionMatrix", SGL_VERT, 0, 0);
    sglRegisterPackedUniform("u_Time", SGL_VERT, 0, 232);

    GLint loc_mvp = lookup_registered_uniform("u_ModelViewProjectionMatrix");
    GLint loc_time = lookup_registered_uniform("u_Time");

    /* Packed locations MUST be >= 0 (bit 31 = 0) so apps checking "loc < 0" don't skip them */
    ASSERT_TRUE(loc_mvp >= 0, "MVP location must be >= 0");
    ASSERT_TRUE(loc_time >= 0, "Time location must be >= 0");
    ASSERT_TRUE(loc_mvp & TEST_LOC_PACKED_FLAG, "MVP should have packed flag set");
    ASSERT_TRUE(loc_time & TEST_LOC_PACKED_FLAG, "Time should have packed flag set");

    /* Extract fields */
    int mvp_stage = (loc_mvp >> TEST_LOC_STAGE_SHIFT) & TEST_LOC_STAGE_MASK;
    int mvp_binding = (loc_mvp >> 16) & 0xFF;
    int mvp_offset = loc_mvp & 0xFFFF;

    ASSERT_EQ(mvp_stage, SGL_VERT, "MVP stage should be vertex");
    ASSERT_EQ(mvp_binding, 0, "MVP binding should be 0");
    ASSERT_EQ(mvp_offset, 0, "MVP offset should be 0");

    int time_offset = loc_time & 0xFFFF;
    ASSERT_EQ(time_offset, 232, "Time offset should be 232");

    PASS();
}

static void test_legacy_location_encoding(void) {
    TEST("Legacy location: stage<<16 | binding");

    sglClearUniformRegistry();
    sglRegisterUniform("u_DiffuseMap", SGL_FRAG, 0);
    sglRegisterUniform("u_LightMap", SGL_FRAG, 1);

    GLint loc_diffuse = lookup_registered_uniform("u_DiffuseMap");
    GLint loc_light = lookup_registered_uniform("u_LightMap");

    /* Legacy should NOT have packed flag set */
    ASSERT_TRUE(!(loc_diffuse & TEST_LOC_PACKED_FLAG), "DiffuseMap should be legacy (no packed flag)");

    int diffuse_stage = (loc_diffuse >> 16) & 0xFFFF;
    int diffuse_binding = loc_diffuse & 0xFFFF;
    ASSERT_EQ(diffuse_stage, SGL_FRAG, "DiffuseMap stage should be fragment");
    ASSERT_EQ(diffuse_binding, 0, "DiffuseMap binding should be 0");

    int light_binding = loc_light & 0xFFFF;
    ASSERT_EQ(light_binding, 1, "LightMap binding should be 1");

    PASS();
}

/* ============================================================================
 * Test: Generic Shader Registration
 * ============================================================================ */

static void test_generic_shader_registration(void) {
    TEST("Generic shader: all 33 vertex + 3 fragment uniforms registered");

    sglClearUniformRegistry();
    sglSetPackedUBOSize(SGL_VERT, 0, 576);
    /* Register all Generic vertex uniforms */
    sglRegisterPackedUniform("u_ModelViewProjectionMatrix", SGL_VERT, 0, 0);
    sglRegisterPackedUniform("u_ModelMatrix",               SGL_VERT, 0, 64);
    sglRegisterPackedUniform("u_Color",                     SGL_VERT, 0, 128);
    sglRegisterPackedUniform("u_BaseColor",                 SGL_VERT, 0, 144);
    sglRegisterPackedUniform("u_VertColor",                 SGL_VERT, 0, 160);
    sglRegisterPackedUniform("u_FogDistance",               SGL_VERT, 0, 176);
    sglRegisterPackedUniform("u_FogDepth",                  SGL_VERT, 0, 192);
    sglRegisterPackedUniform("u_FogColorMask",              SGL_VERT, 0, 208);
    sglRegisterPackedUniform("u_FogEyeT",                   SGL_VERT, 0, 224);
    sglRegisterPackedUniform("u_FogType",                   SGL_VERT, 0, 228);
    sglRegisterPackedUniform("u_Time",                      SGL_VERT, 0, 232);
    sglRegisterPackedUniform("u_VertexLerp",                SGL_VERT, 0, 236);
    sglRegisterPackedUniform("u_LocalViewOrigin",           SGL_VERT, 0, 240);
    sglRegisterPackedUniform("u_ModelLightDir",             SGL_VERT, 0, 256);
    sglRegisterPackedUniform("u_TCGen0",                    SGL_VERT, 0, 272);
    sglRegisterPackedUniform("u_TCGen0Vector0",             SGL_VERT, 0, 288);
    sglRegisterPackedUniform("u_TCGen0Vector1",             SGL_VERT, 0, 304);
    sglRegisterPackedUniform("u_DeformGen",                 SGL_VERT, 0, 320);
    sglRegisterPackedUniform("u_DeformParams",              SGL_VERT, 0, 336);
    sglRegisterPackedUniform("u_DeformSpread",              SGL_VERT, 0, 352);
    sglRegisterPackedUniform("u_ColorGen",                  SGL_VERT, 0, 356);
    sglRegisterPackedUniform("u_AlphaGen",                  SGL_VERT, 0, 360);
    sglRegisterPackedUniform("u_PortalRange",               SGL_VERT, 0, 364);
    sglRegisterPackedUniform("u_DiffuseColor",              SGL_VERT, 0, 368);
    sglRegisterPackedUniform("u_FireRiseDir",               SGL_VERT, 0, 384);
    sglRegisterPackedUniform("u_ZFadeLowest",               SGL_VERT, 0, 400);
    sglRegisterPackedUniform("u_ZFadeHighest",              SGL_VERT, 0, 404);
    sglRegisterPackedUniform("u_AmbientLight",              SGL_VERT, 0, 416);
    sglRegisterPackedUniform("u_DirectedLight",             SGL_VERT, 0, 432);
    sglRegisterPackedUniform("u_DiffuseTexMatrix0",         SGL_VERT, 0, 448);
    sglRegisterPackedUniform("u_DiffuseTexMatrix1",         SGL_VERT, 0, 464);
    sglRegisterPackedUniform("u_DiffuseTexMatrix2",         SGL_VERT, 0, 480);
    sglRegisterPackedUniform("u_DiffuseTexMatrix3",         SGL_VERT, 0, 496);
    sglRegisterPackedUniform("u_DiffuseTexMatrix4",         SGL_VERT, 0, 512);
    sglRegisterPackedUniform("u_DiffuseTexMatrix5",         SGL_VERT, 0, 528);
    sglRegisterPackedUniform("u_DiffuseTexMatrix6",         SGL_VERT, 0, 544);
    sglRegisterPackedUniform("u_DiffuseTexMatrix7",         SGL_VERT, 0, 560);
    /* Bones */
    int max_bones = 80;
    sglSetPackedUBOSize(SGL_VERT, 1, 16 * 4 * max_bones);
    sglRegisterPackedUniform("u_BoneMatrix",                SGL_VERT, 1, 0);
    /* Fragment */
    sglSetPackedUBOSize(SGL_FRAG, 0, 16);
    sglRegisterPackedUniform("u_AlphaTest",                 SGL_FRAG, 0, 0);
    sglRegisterPackedUniform("u_AlphaTestRef",              SGL_FRAG, 0, 4);
    sglRegisterPackedUniform("u_Texture1Env",               SGL_FRAG, 0, 8);
    /* Samplers */
    sglRegisterUniform("u_DiffuseMap", SGL_FRAG, 0);
    sglRegisterUniform("u_LightMap",   SGL_FRAG, 1);

    /* Verify a selection of locations */
    GLint loc;

    loc = lookup_registered_uniform("u_ModelViewProjectionMatrix");
    ASSERT_TRUE(loc & TEST_LOC_PACKED_FLAG, "MVP packed");
    ASSERT_EQ(loc & 0xFFFF, 0, "MVP offset 0");

    loc = lookup_registered_uniform("u_DiffuseTexMatrix7");
    ASSERT_EQ(loc & 0xFFFF, 560, "TexMatrix7 offset 560");

    loc = lookup_registered_uniform("u_AlphaTest");
    ASSERT_TRUE(loc & TEST_LOC_PACKED_FLAG, "AlphaTest packed");
    ASSERT_EQ((loc >> TEST_LOC_STAGE_SHIFT) & TEST_LOC_STAGE_MASK, SGL_FRAG, "AlphaTest fragment stage");
    ASSERT_EQ(loc & 0xFFFF, 0, "AlphaTest offset 0");

    loc = lookup_registered_uniform("u_DiffuseMap");
    ASSERT_TRUE(!(loc & TEST_LOC_PACKED_FLAG), "DiffuseMap legacy");

    PASS();
}

/* ============================================================================
 * Test: Shadow Buffer Writes
 * ============================================================================ */

static void test_shadow_buffer_float_write(void) {
    TEST("Shadow buffer: float uniform write at correct offset");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 576;
    packed.valid = true;

    /* Write u_FogEyeT at offset 224 (float) */
    float fogEyeT = 1.5f;
    packed_write_float(&packed, 224, 1, &fogEyeT);

    float *result = (float *)(packed.data + 224);
    ASSERT_FLOAT_EQ(*result, 1.5f, "FogEyeT should be 1.5");
    ASSERT_TRUE(packed.dirty, "Packed should be dirty after write");

    PASS();
}

static void test_shadow_buffer_vec4_write(void) {
    TEST("Shadow buffer: vec4 uniform write at correct offset");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 576;
    packed.valid = true;

    /* Write u_FogDistance at offset 176 (vec4) */
    float fogDist[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    packed_write_float(&packed, 176, 4, fogDist);

    float *result = (float *)(packed.data + 176);
    ASSERT_FLOAT_EQ(result[0], 1.0f, "FogDist[0]");
    ASSERT_FLOAT_EQ(result[1], 2.0f, "FogDist[1]");
    ASSERT_FLOAT_EQ(result[2], 3.0f, "FogDist[2]");
    ASSERT_FLOAT_EQ(result[3], 4.0f, "FogDist[3]");

    PASS();
}

static void test_shadow_buffer_int_write(void) {
    TEST("Shadow buffer: int uniform write at correct offset");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 576;
    packed.valid = true;

    /* Write u_FogType at offset 228 (int) */
    int fogType = 3;
    packed_write_int(&packed, 228, 1, &fogType);

    int *result = (int *)(packed.data + 228);
    ASSERT_EQ(*result, 3, "FogType should be 3");

    PASS();
}

static void test_shadow_buffer_mat4_write(void) {
    TEST("Shadow buffer: mat4 write (identity) at offset 0");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 576;
    packed.valid = true;

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    packed_write_mat4(&packed, 0, 1, GL_FALSE, identity);

    float *result = (float *)(packed.data);
    ASSERT_FLOAT_EQ(result[0], 1.0f, "mat4[0][0]");
    ASSERT_FLOAT_EQ(result[5], 1.0f, "mat4[1][1]");
    ASSERT_FLOAT_EQ(result[10], 1.0f, "mat4[2][2]");
    ASSERT_FLOAT_EQ(result[15], 1.0f, "mat4[3][3]");
    ASSERT_FLOAT_EQ(result[1], 0.0f, "mat4[0][1] should be 0");
    ASSERT_FLOAT_EQ(result[4], 0.0f, "mat4[1][0] should be 0");

    PASS();
}

static void test_shadow_buffer_mat4_transpose(void) {
    TEST("Shadow buffer: mat4 with transpose=GL_TRUE");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 576;
    packed.valid = true;

    /* Row-major matrix */
    float row_major[16] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16
    };
    packed_write_mat4(&packed, 0, 1, GL_TRUE, row_major);

    float *result = (float *)(packed.data);
    /* After transpose: column 0 = {1, 5, 9, 13}, column 1 = {2, 6, 10, 14}, etc. */
    ASSERT_FLOAT_EQ(result[0], 1.0f, "Transposed [0][0]");
    ASSERT_FLOAT_EQ(result[1], 5.0f, "Transposed [0][1] = src[1][0]");
    ASSERT_FLOAT_EQ(result[4], 2.0f, "Transposed [1][0] = src[0][1]");
    ASSERT_FLOAT_EQ(result[5], 6.0f, "Transposed [1][1]");

    PASS();
}

static void test_shadow_buffer_mat3_std140(void) {
    TEST("Shadow buffer: mat3 std140 layout (3 x vec4 = 48 bytes)");

    sgl_packed_ubo_t packed;
    memset(&packed, 0xCC, sizeof(packed));
    packed.size = 576;
    packed.valid = true;
    packed.dirty = false;

    /* 3x3 identity */
    float mat3_identity[9] = {
        1, 0, 0,
        0, 1, 0,
        0, 0, 1
    };
    packed_write_mat3(&packed, 0, 1, GL_FALSE, mat3_identity);

    float *result = (float *)(packed.data);
    /* std140: column 0 = (1, 0, 0, pad) at offset 0 */
    ASSERT_FLOAT_EQ(result[0], 1.0f, "mat3 col0[0]");
    ASSERT_FLOAT_EQ(result[1], 0.0f, "mat3 col0[1]");
    ASSERT_FLOAT_EQ(result[2], 0.0f, "mat3 col0[2]");
    ASSERT_FLOAT_EQ(result[3], 0.0f, "mat3 col0 pad");
    /* std140: column 1 = (0, 1, 0, pad) at offset 16 */
    ASSERT_FLOAT_EQ(result[4], 0.0f, "mat3 col1[0]");
    ASSERT_FLOAT_EQ(result[5], 1.0f, "mat3 col1[1]");
    ASSERT_FLOAT_EQ(result[6], 0.0f, "mat3 col1[2]");
    ASSERT_FLOAT_EQ(result[7], 0.0f, "mat3 col1 pad");
    /* std140: column 2 = (0, 0, 1, pad) at offset 32 */
    ASSERT_FLOAT_EQ(result[8], 0.0f, "mat3 col2[0]");
    ASSERT_FLOAT_EQ(result[9], 0.0f, "mat3 col2[1]");
    ASSERT_FLOAT_EQ(result[10], 1.0f, "mat3 col2[2]");
    ASSERT_FLOAT_EQ(result[11], 0.0f, "mat3 col2 pad");

    PASS();
}

static void test_shadow_buffer_bone_matrices(void) {
    TEST("Shadow buffer: bone matrices (count > 1, mat4)");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 4096;
    packed.valid = true;

    /* Write 3 bone matrices */
    float bones[3 * 16];
    memset(bones, 0, sizeof(bones));
    /* Bone 0: identity */
    bones[0] = 1; bones[5] = 1; bones[10] = 1; bones[15] = 1;
    /* Bone 1: translate x=10 */
    bones[16] = 1; bones[21] = 1; bones[26] = 1; bones[31] = 1;
    bones[28] = 10.0f; /* mat[3][0] = 10 */
    /* Bone 2: scale 2x */
    bones[32] = 2; bones[37] = 2; bones[42] = 2; bones[47] = 1;

    packed_write_mat4(&packed, 0, 3, GL_FALSE, bones);

    float *result = (float *)(packed.data);
    /* Bone 0 at offset 0 */
    ASSERT_FLOAT_EQ(result[0], 1.0f, "Bone0 [0][0]");
    ASSERT_FLOAT_EQ(result[15], 1.0f, "Bone0 [3][3]");
    /* Bone 1 at offset 64 */
    ASSERT_FLOAT_EQ(result[16], 1.0f, "Bone1 [0][0]");
    ASSERT_FLOAT_EQ(result[28], 10.0f, "Bone1 translate x");
    /* Bone 2 at offset 128 */
    ASSERT_FLOAT_EQ(result[32], 2.0f, "Bone2 scale x");
    ASSERT_FLOAT_EQ(result[37], 2.0f, "Bone2 scale y");

    PASS();
}

/* ============================================================================
 * Test: Float5 (DeformParams + DeformSpread) Split
 * ============================================================================ */

static void test_float5_split(void) {
    TEST("Float5: DeformParams(vec4) + DeformSpread(float) split write");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 576;
    packed.valid = true;

    float vec5[5] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };

    /* Simulate GLSL_SetUniformFloat5 on Switch:
     * First 4 floats as vec4 at DeformParams offset (336)
     * 5th float at DeformSpread offset (352) */
    packed_write_float(&packed, 336, 4, &vec5[0]);   /* u_DeformParams */
    packed_write_float(&packed, 352, 1, &vec5[4]);   /* u_DeformSpread */

    float *params = (float *)(packed.data + 336);
    float *spread = (float *)(packed.data + 352);

    ASSERT_FLOAT_EQ(params[0], 1.0f, "DeformParams[0]");
    ASSERT_FLOAT_EQ(params[1], 2.0f, "DeformParams[1]");
    ASSERT_FLOAT_EQ(params[2], 3.0f, "DeformParams[2]");
    ASSERT_FLOAT_EQ(params[3], 4.0f, "DeformParams[3]");
    ASSERT_FLOAT_EQ(*spread, 5.0f, "DeformSpread");

    PASS();
}

/* ============================================================================
 * Test: Boundary / Overflow Checks
 * ============================================================================ */

static void test_write_beyond_ubo_size(void) {
    TEST("Boundary: write beyond packed UBO size is rejected");

    sgl_packed_ubo_t packed;
    memset(&packed, 0xAA, sizeof(packed));
    packed.size = 16;
    packed.valid = true;
    packed.dirty = false;

    /* Try writing 16 bytes at offset 8 → extends to 24, exceeds size 16 */
    float vec4[4] = { 1, 2, 3, 4 };
    packed_write_float(&packed, 8, 4, vec4);

    /* Should NOT have been written */
    ASSERT_TRUE(!packed.dirty, "Should not write beyond UBO size");

    /* Write at offset 0 should succeed (0+16 = 16 = size, OK) */
    packed_write_float(&packed, 0, 4, vec4);
    ASSERT_TRUE(packed.dirty, "Write at offset 0 should succeed");

    PASS();
}

static void test_write_to_invalid_ubo(void) {
    TEST("Boundary: write to invalid (unconfigured) packed UBO is rejected");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.valid = false;

    float val = 1.0f;
    packed_write_float(&packed, 0, 1, &val);

    ASSERT_TRUE(!packed.dirty, "Should not write to invalid UBO");

    PASS();
}

/* ============================================================================
 * Test: CRITICAL - Bone UBO Size Overflow
 * ============================================================================ */

static void test_bone_ubo_size_overflow(void) {
    TEST("CRITICAL: Bone UBO size > SGL_MAX_PACKED_UBO_SIZE (4096)");

    sglClearUniformRegistry();

    /* 80 bones * 64 bytes = 5120 > 4096 */
    int max_bones = 80;
    int bone_size = 16 * 4 * max_bones; /* = 5120 */

    printf("\n    [INFO] Bone UBO size for %d bones = %d bytes (max = %d)\n",
           max_bones, bone_size, SGL_MAX_PACKED_UBO_SIZE);

    sglSetPackedUBOSize(SGL_VERT, 1, bone_size);

    /* Check if it was actually set */
    bool registered = (s_packed_ubo_sizes[SGL_VERT][1] == bone_size);
    printf("    ");

    if (bone_size > SGL_MAX_PACKED_UBO_SIZE) {
        /* Expected: registration FAILS silently */
        ASSERT_TRUE(!registered,
            "Bone size > MAX should fail (current: silently rejected)");
        printf("    [WARN] Bone UBO for %d bones REJECTED! Max bones = %d\n",
               max_bones, SGL_MAX_PACKED_UBO_SIZE / 64);
        printf("    [FIX]  Increase SGL_MAX_PACKED_UBO_SIZE to %d or limit bones to %d\n",
               bone_size, SGL_MAX_PACKED_UBO_SIZE / 64);
    } else {
        ASSERT_TRUE(registered, "Bone size should be accepted");
    }
}

static void test_bone_ubo_size_ok(void) {
    TEST("Bone UBO size within limits (64 bones)");

    sglClearUniformRegistry();

    int max_bones = 64;
    int bone_size = 16 * 4 * max_bones; /* = 4096, exactly at limit */

    sglSetPackedUBOSize(SGL_VERT, 1, bone_size);
    ASSERT_EQ(s_packed_ubo_sizes[SGL_VERT][1], bone_size, "64 bones should be accepted");

    PASS();
}

/* ============================================================================
 * Test: CRITICAL - Uniform Buffer Capacity
 * ============================================================================ */

static void test_uniform_buffer_capacity(void) {
    TEST("CRITICAL: Uniform buffer capacity for typical frame");

    /* Estimate bytes per draw call with packed UBOs */
    int packed_vert_size = 576;
    int packed_vert_aligned = (packed_vert_size + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);
    int packed_frag_size = 16;
    int packed_frag_aligned = (packed_frag_size + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);

    /* Legacy sampler uniforms (wasted but allocated) */
    int sampler_uniforms = 2; /* DiffuseMap + LightMap */
    int sampler_cost = sampler_uniforms * SGL_UNIFORM_ALIGNMENT;

    int per_draw = packed_vert_aligned + packed_frag_aligned + sampler_cost;
    int max_draws = SGL_UNIFORM_BUF_SIZE / per_draw;

    printf("\n    [INFO] Per-draw uniform allocation:\n");
    printf("           Packed vertex: %d -> %d aligned\n", packed_vert_size, packed_vert_aligned);
    printf("           Packed fragment: %d -> %d aligned\n", packed_frag_size, packed_frag_aligned);
    printf("           Legacy samplers: %d x %d = %d (wasted)\n",
           sampler_uniforms, SGL_UNIFORM_ALIGNMENT, sampler_cost);
    printf("           Total per draw: %d bytes\n", per_draw);
    printf("           Uniform buffer: %d bytes\n", SGL_UNIFORM_BUF_SIZE);
    printf("           Max draws: %d\n", max_draws);
    printf("    ");

    /* Spearmint typically needs 100-200 draws per frame */
    if (max_draws < 100) {
        FAIL("Uniform buffer too small for 100 draws!");
        printf("    [FIX]  Increase SGL_UNIFORM_BUF_SIZE from %d to at least %d\n",
               SGL_UNIFORM_BUF_SIZE, per_draw * 200);
    } else if (max_draws < 200) {
        printf("[WARN] Only %d draws possible (target: 200)\n", max_draws);
        printf("    [FIX]  Consider increasing SGL_UNIFORM_BUF_SIZE to %d\n",
               per_draw * 200);
        tests_passed++;
    } else {
        PASS();
    }
}

/* ============================================================================
 * Test: Clear Registry
 * ============================================================================ */

static void test_clear_registry(void) {
    TEST("Registry: clear removes all entries");

    sglClearUniformRegistry();
    sglRegisterPackedUniform("test1", SGL_VERT, 0, 0);
    sglRegisterPackedUniform("test2", SGL_FRAG, 0, 4);

    ASSERT_TRUE(lookup_registered_uniform("test1") >= 0, "test1 should exist");
    ASSERT_TRUE(lookup_registered_uniform("test2") >= 0, "test2 should exist");

    sglClearUniformRegistry();

    ASSERT_EQ(lookup_registered_uniform("test1"), -1, "test1 should be gone");
    ASSERT_EQ(lookup_registered_uniform("test2"), -1, "test2 should be gone");

    PASS();
}

static void test_registry_update_existing(void) {
    TEST("Registry: updating existing entry changes offset");

    sglClearUniformRegistry();
    sglRegisterPackedUniform("u_Color", SGL_VERT, 0, 100);

    GLint loc1 = lookup_registered_uniform("u_Color");
    ASSERT_EQ(loc1 & 0xFFFF, 100, "First offset should be 100");

    /* Update same name to different offset */
    sglRegisterPackedUniform("u_Color", SGL_VERT, 0, 200);

    GLint loc2 = lookup_registered_uniform("u_Color");
    ASSERT_EQ(loc2 & 0xFFFF, 200, "Updated offset should be 200");

    PASS();
}

/* ============================================================================
 * Test: GLSL Offset Verification (matches generic_vp.glsl)
 * ============================================================================ */

typedef struct {
    const char *name;
    int expected_offset;
    int type_size; /* bytes for this uniform type */
} glsl_offset_check_t;

static void test_glsl_offset_alignment(void) {
    TEST("GLSL offsets: verify std140 alignment for all Generic uniforms");

    sglClearUniformRegistry();
    sglSetPackedUBOSize(SGL_VERT, 0, 576);

    /* All uniforms from generic_vp.glsl with their std140 offsets */
    glsl_offset_check_t checks[] = {
        { "u_ModelViewProjectionMatrix", 0,   64 },  /* mat4 */
        { "u_ModelMatrix",               64,  64 },  /* mat4 */
        { "u_Color",                     128, 16 },  /* vec4 */
        { "u_BaseColor",                 144, 16 },  /* vec4 */
        { "u_VertColor",                 160, 16 },  /* vec4 */
        { "u_FogDistance",               176, 16 },  /* vec4 */
        { "u_FogDepth",                  192, 16 },  /* vec4 */
        { "u_FogColorMask",              208, 16 },  /* vec4 */
        { "u_FogEyeT",                  224,  4 },  /* float */
        { "u_FogType",                   228,  4 },  /* int */
        { "u_Time",                      232,  4 },  /* float */
        { "u_VertexLerp",                236,  4 },  /* float */
        { "u_LocalViewOrigin",           240, 16 },  /* vec4 (vec3 padded) */
        { "u_ModelLightDir",             256, 16 },  /* vec4 (vec3 padded) */
        { "u_TCGen0",                    272,  4 },  /* int */
        /* padding 276-287 */
        { "u_TCGen0Vector0",             288, 16 },  /* vec4 (vec3 padded) */
        { "u_TCGen0Vector1",             304, 16 },  /* vec4 (vec3 padded) */
        { "u_DeformGen",                 320,  4 },  /* int */
        /* padding 324-335 */
        { "u_DeformParams",              336, 16 },  /* vec4 */
        { "u_DeformSpread",              352,  4 },  /* float */
        { "u_ColorGen",                  356,  4 },  /* int */
        { "u_AlphaGen",                  360,  4 },  /* int */
        { "u_PortalRange",               364,  4 },  /* float */
        { "u_DiffuseColor",              368, 16 },  /* vec4 (vec3 padded) */
        { "u_FireRiseDir",               384, 16 },  /* vec4 (vec3 padded) */
        { "u_ZFadeLowest",               400,  4 },  /* float */
        { "u_ZFadeHighest",              404,  4 },  /* float */
        /* padding 408-415 */
        { "u_AmbientLight",              416, 16 },  /* vec4 (vec3 padded) */
        { "u_DirectedLight",             432, 16 },  /* vec4 (vec3 padded) */
        { "u_DiffuseTexMatrix0",         448, 16 },  /* vec4 */
        { "u_DiffuseTexMatrix1",         464, 16 },  /* vec4 */
        { "u_DiffuseTexMatrix2",         480, 16 },  /* vec4 */
        { "u_DiffuseTexMatrix3",         496, 16 },  /* vec4 */
        { "u_DiffuseTexMatrix4",         512, 16 },  /* vec4 */
        { "u_DiffuseTexMatrix5",         528, 16 },  /* vec4 */
        { "u_DiffuseTexMatrix6",         544, 16 },  /* vec4 */
        { "u_DiffuseTexMatrix7",         560, 16 },  /* vec4 */
    };
    int num_checks = sizeof(checks) / sizeof(checks[0]);

    /* Register all uniforms */
    for (int i = 0; i < num_checks; i++) {
        sglRegisterPackedUniform(checks[i].name, SGL_VERT, 0, checks[i].expected_offset);
    }

    /* Verify each offset and check no overlap */
    for (int i = 0; i < num_checks; i++) {
        GLint loc = lookup_registered_uniform(checks[i].name);
        if (!(loc & TEST_LOC_PACKED_FLAG)) {
            FAIL("Uniform should be packed");
            return;
        }
        int actual_offset = loc & 0xFFFF;
        if (actual_offset != checks[i].expected_offset) {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: expected offset %d, got %d",
                     checks[i].name, checks[i].expected_offset, actual_offset);
            FAIL(msg);
            return;
        }
        /* Check this uniform doesn't overlap with the next */
        if (i < num_checks - 1) {
            int end = checks[i].expected_offset + checks[i].type_size;
            int next_start = checks[i + 1].expected_offset;
            if (end > next_start) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%s overlaps %s (end %d > start %d)",
                         checks[i].name, checks[i+1].name, end, next_start);
                FAIL(msg);
                return;
            }
        }
    }

    /* Check total fits in declared UBO size */
    int last_idx = num_checks - 1;
    int total = checks[last_idx].expected_offset + checks[last_idx].type_size;
    if (total > 576) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Total %d exceeds declared UBO size 576", total);
        FAIL(msg);
        return;
    }

    PASS();
}

/* ============================================================================
 * Test: Packed UBO Initialization via glGetUniformLocation flow
 * ============================================================================ */

static void test_packed_ubo_init_on_first_access(void) {
    TEST("Packed UBO: initialized on first location lookup");

    sglClearUniformRegistry();
    sglSetPackedUBOSize(SGL_VERT, 0, 576);
    sglRegisterPackedUniform("u_Color", SGL_VERT, 0, 128);

    /* Simulate what glGetUniformLocation does */
    GLint loc = lookup_registered_uniform("u_Color");
    ASSERT_TRUE(loc & TEST_LOC_PACKED_FLAG, "Should be packed");

    int stage = (loc >> TEST_LOC_STAGE_SHIFT) & TEST_LOC_STAGE_MASK;
    int binding = (loc >> 16) & 0xFF;
    ASSERT_EQ(stage, SGL_VERT, "Vertex stage");
    ASSERT_EQ(binding, 0, "Binding 0");

    /* In real code, glGetUniformLocation initializes the packed UBO on program */
    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));

    int ubo_size = s_packed_ubo_sizes[stage][binding];
    if (ubo_size > 0 && ubo_size <= SGL_MAX_PACKED_UBO_SIZE) {
        packed.size = ubo_size;
        packed.valid = true;
        packed.dirty = false;
    }

    ASSERT_TRUE(packed.valid, "Packed UBO should be initialized");
    ASSERT_EQ((int)packed.size, 576, "Size should be 576");

    PASS();
}

/* ============================================================================
 * Test: DiffuseTexMatrix Array
 * ============================================================================ */

static void test_diffuse_tex_matrix_array(void) {
    TEST("DiffuseTexMatrix[0-7]: contiguous vec4 array at offset 448-575");

    sgl_packed_ubo_t packed;
    memset(&packed, 0, sizeof(packed));
    packed.size = 576;
    packed.valid = true;

    /* Write 8 vec4 values as individual uniforms (matching Spearmint pattern) */
    for (int i = 0; i < 8; i++) {
        float texmat[4] = { (float)(i * 4 + 1), (float)(i * 4 + 2),
                            (float)(i * 4 + 3), (float)(i * 4 + 4) };
        packed_write_float(&packed, 448 + i * 16, 4, texmat);
    }

    /* Verify array layout matches GLSL vec4 u_DiffuseTexMatrix[8] */
    float *arr = (float *)(packed.data + 448);
    for (int i = 0; i < 8; i++) {
        float expected = (float)(i * 4 + 1);
        if (fabsf(arr[i * 4] - expected) > 1e-6f) {
            FAIL("TexMatrix array element mismatch");
            return;
        }
    }

    /* Check total size: 8 * 16 = 128 bytes, ending at offset 576 */
    ASSERT_EQ(448 + 8 * 16, 576, "Array ends at exactly 576");

    PASS();
}

/* ============================================================================
 * Test: Uniform buffer capacity without sampler waste
 * ============================================================================ */

static void test_capacity_without_sampler_waste(void) {
    TEST("Capacity: draws if legacy sampler waste eliminated");

    int packed_vert_aligned = (576 + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);
    int packed_frag_aligned = (16 + SGL_UNIFORM_ALIGNMENT - 1) & ~(SGL_UNIFORM_ALIGNMENT - 1);

    /* Without sampler waste */
    int per_draw_no_waste = packed_vert_aligned + packed_frag_aligned;
    int max_draws_no_waste = SGL_UNIFORM_BUF_SIZE / per_draw_no_waste;

    printf("\n    [INFO] Without sampler waste: %d bytes/draw, %d max draws\n",
           per_draw_no_waste, max_draws_no_waste);
    printf("    ");

    /* Still may not be enough */
    if (max_draws_no_waste >= 200) {
        PASS();
    } else {
        printf("[WARN] %d draws (want 200). Need SGL_UNIFORM_BUF_SIZE = %d\n",
               max_draws_no_waste, per_draw_no_waste * 200);
        tests_passed++; /* Informational */
    }
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("========================================================\n");
    printf("  SwitchGLES Packed UBO Validation Tests\n");
    printf("========================================================\n\n");

    printf("--- Location Encoding ---\n");
    test_packed_location_encoding();
    test_legacy_location_encoding();

    printf("\n--- Registration System ---\n");
    test_generic_shader_registration();
    test_clear_registry();
    test_registry_update_existing();

    printf("\n--- Shadow Buffer Writes ---\n");
    test_shadow_buffer_float_write();
    test_shadow_buffer_vec4_write();
    test_shadow_buffer_int_write();
    test_shadow_buffer_mat4_write();
    test_shadow_buffer_mat4_transpose();
    test_shadow_buffer_mat3_std140();
    test_shadow_buffer_bone_matrices();

    printf("\n--- Float5 Split ---\n");
    test_float5_split();

    printf("\n--- GLSL Offset Verification ---\n");
    test_glsl_offset_alignment();
    test_diffuse_tex_matrix_array();

    printf("\n--- Packed UBO Lifecycle ---\n");
    test_packed_ubo_init_on_first_access();

    printf("\n--- Boundary Checks ---\n");
    test_write_beyond_ubo_size();
    test_write_to_invalid_ubo();

    printf("\n--- CRITICAL: Resource Limits ---\n");
    test_bone_ubo_size_overflow();
    test_bone_ubo_size_ok();
    test_uniform_buffer_capacity();
    test_capacity_without_sampler_waste();

    printf("\n========================================================\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_run);
    printf("========================================================\n");

    if (tests_failed > 0) {
        printf("\n  ISSUES TO FIX:\n");
        printf("  1. Increase SGL_MAX_PACKED_UBO_SIZE from 4096 to 8192\n");
        printf("     (for bone matrices with > 64 bones)\n");
        printf("  2. Increase SGL_UNIFORM_BUF_SIZE from 64KB to 256KB+\n");
        printf("     (for 200+ draw calls per frame)\n");
        printf("  3. Skip legacy UBO allocation for sampler uniforms on Switch\n");
        printf("     (avoids wasting 256 bytes per sampler per draw)\n");
        printf("\n");
    }

    return tests_failed > 0 ? 1 : 0;
}
