/*
 * test_transpiler.c - Test program for glsl_transpiler
 *
 * Compile (any platform):
 *   gcc -o test_transpiler test_transpiler.c glsl_transpiler.c -Wall
 *   cl test_transpiler.c glsl_transpiler.c
 */

#include "../source/transpiler/glsl_transpiler.h"
#include <stdio.h>
#include <string.h>

/* ---- Test helper ---- */

static int s_pass = 0, s_fail = 0;

#define TEST(name) printf("\n=== %s ===\n", name)
#define CHECK(cond, msg) do { \
    if (cond) { s_pass++; printf("  [PASS] %s\n", msg); } \
    else      { s_fail++; printf("  [FAIL] %s\n", msg); } \
} while(0)

/* ---- Test: basic vertex shader ---- */

static void test_basic_vertex(void) {
    TEST("Basic Vertex Shader");

    const char *src =
        "#version 100\n"
        "attribute vec4 a_position;\n"
        "attribute vec2 a_texcoord;\n"
        "uniform mat4 u_mvp;\n"
        "uniform vec4 u_color;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    gl_Position = u_mvp * a_position;\n"
        "    v_texcoord = a_texcoord;\n"
        "}\n";

    glslt_options_t opts;
    glslt_options_init(&opts);
    glslt_set_attrib_location(&opts, "a_position", 0);
    glslt_set_attrib_location(&opts, "a_texcoord", 1);

    glslt_result_t r = glslt_transpile(src, GLSLT_VERTEX, &opts);

    CHECK(r.success, "transpile succeeded");
    CHECK(r.num_attributes == 2, "found 2 attributes");
    CHECK(r.num_varyings == 1, "found 1 varying");
    CHECK(r.num_uniforms == 2, "found 2 uniforms");
    CHECK(r.num_samplers == 0, "found 0 samplers");

    if (r.num_attributes >= 2) {
        CHECK(r.attributes[0].location == 0, "a_position at location 0");
        CHECK(r.attributes[1].location == 1, "a_texcoord at location 1");
    }

    if (r.num_uniforms >= 2) {
        /* Sorted alphabetically: u_color before u_mvp */
        CHECK(strcmp(r.uniforms[0].name, "u_color") == 0, "first uniform is u_color");
        CHECK(strcmp(r.uniforms[1].name, "u_mvp") == 0, "second uniform is u_mvp");
        CHECK(r.uniforms[0].offset == 0, "u_color offset=0");
        CHECK(r.uniforms[0].size == 16, "u_color size=16 (vec4)");
        CHECK(r.uniforms[1].offset == 16, "u_mvp offset=16");
        CHECK(r.uniforms[1].size == 64, "u_mvp size=64 (mat4)");
        CHECK(r.ubo_total_size == 80, "UBO total size=80");
    }

    if (r.output) {
        CHECK(strstr(r.output, "#version 460") != NULL, "output has #version 460");
        CHECK(strstr(r.output, "layout(location = 0) in vec4 a_position") != NULL,
              "attribute a_position with location");
        CHECK(strstr(r.output, "layout(location = 1) in vec2 a_texcoord") != NULL,
              "attribute a_texcoord with location");
        CHECK(strstr(r.output, "layout(std140, binding = 0) uniform VertexUniforms") != NULL,
              "UBO block emitted");
        CHECK(strstr(r.output, "layout(location = 0) out vec2 v_texcoord") != NULL,
              "varying as out");
        CHECK(strstr(r.output, "attribute") == NULL, "no 'attribute' keyword in output");
        CHECK(strstr(r.output, "varying") == NULL, "no 'varying' keyword in output");
        CHECK(strstr(r.output, "#version 100") == NULL, "no '#version 100' in output");
        printf("\n--- Output ---\n%s--- End ---\n", r.output);
    }

    glslt_result_free(&r);
}

/* ---- Test: basic fragment shader ---- */

static void test_basic_fragment(void) {
    TEST("Basic Fragment Shader");

    const char *src =
        "#version 100\n"
        "precision mediump float;\n"
        "uniform sampler2D u_texture;\n"
        "uniform vec4 u_tint;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "    vec4 tex = texture2D(u_texture, v_texcoord);\n"
        "    gl_FragColor = tex * u_tint;\n"
        "}\n";

    glslt_options_t opts;
    glslt_options_init(&opts);

    glslt_result_t r = glslt_transpile(src, GLSLT_FRAGMENT, &opts);

    CHECK(r.success, "transpile succeeded");
    CHECK(r.num_uniforms == 1, "found 1 uniform (u_tint)");
    CHECK(r.num_samplers == 1, "found 1 sampler (u_texture)");
    CHECK(r.num_varyings == 1, "found 1 varying");

    if (r.num_samplers >= 1) {
        CHECK(strcmp(r.samplers[0].name, "u_texture") == 0, "sampler is u_texture");
        CHECK(r.samplers[0].binding == 0, "sampler binding=0");
    }

    if (r.output) {
        CHECK(strstr(r.output, "precision") == NULL, "no precision in output");
        CHECK(strstr(r.output, "texture2D") == NULL, "no texture2D in output");
        CHECK(strstr(r.output, "texture(") != NULL, "has texture() call");
        CHECK(strstr(r.output, "gl_FragColor") == NULL, "no gl_FragColor in output");
        CHECK(strstr(r.output, "fragColor") != NULL, "has fragColor");
        CHECK(strstr(r.output, "layout(location = 0) out vec4 fragColor") != NULL,
              "fragColor declaration");
        CHECK(strstr(r.output, "layout(location = 0) in vec2 v_texcoord") != NULL,
              "varying as in");
        printf("\n--- Output ---\n%s--- End ---\n", r.output);
    }

    glslt_result_free(&r);
}

/* ---- Test: Spearmint-style shader ---- */

static void test_spearmint_vertex(void) {
    TEST("Spearmint Vertex Shader");

    const char *src =
        "#version 100\n"
        "#extension GL_OES_standard_derivatives : enable\n"
        "attribute vec4 attr_Position;\n"
        "attribute vec4 attr_TexCoord0;\n"
        "attribute vec3 attr_Normal;\n"
        "uniform mat4 u_ModelViewProjectionMatrix;\n"
        "uniform mat4 u_ModelViewMatrix;\n"
        "uniform mat3 u_NormalMatrix;\n"
        "uniform float u_Time;\n"
        "varying vec2 var_TexCoord;\n"
        "varying vec3 var_Normal;\n"
        "varying vec3 var_ViewDir;\n"
        "void main() {\n"
        "    gl_Position = u_ModelViewProjectionMatrix * attr_Position;\n"
        "    var_TexCoord = attr_TexCoord0.xy;\n"
        "    var_Normal = u_NormalMatrix * attr_Normal;\n"
        "    var_ViewDir = -(u_ModelViewMatrix * attr_Position).xyz;\n"
        "}\n";

    glslt_options_t opts;
    glslt_options_init(&opts);
    glslt_set_attrib_location(&opts, "attr_Position", 0);
    glslt_set_attrib_location(&opts, "attr_TexCoord0", 1);
    glslt_set_attrib_location(&opts, "attr_Normal", 2);

    glslt_result_t r = glslt_transpile(src, GLSLT_VERTEX, &opts);

    CHECK(r.success, "transpile succeeded");
    CHECK(r.num_attributes == 3, "found 3 attributes");
    CHECK(r.num_varyings == 3, "found 3 varyings");
    CHECK(r.num_uniforms == 4, "found 4 uniforms");
    CHECK(r.num_samplers == 0, "found 0 samplers");

    /* Check std140 layout of uniforms (sorted alphabetically) */
    if (r.num_uniforms == 4) {
        /* u_ModelViewMatrix(mat4), u_ModelViewProjectionMatrix(mat4),
         * u_NormalMatrix(mat3), u_Time(float) */
        CHECK(strcmp(r.uniforms[0].name, "u_ModelViewMatrix") == 0, "uniform[0] = u_ModelViewMatrix");
        CHECK(r.uniforms[0].offset == 0,  "u_ModelViewMatrix offset=0");
        CHECK(r.uniforms[0].size == 64,   "u_ModelViewMatrix size=64");

        CHECK(strcmp(r.uniforms[1].name, "u_ModelViewProjectionMatrix") == 0,
              "uniform[1] = u_ModelViewProjectionMatrix");
        CHECK(r.uniforms[1].offset == 64, "u_ModelViewProjectionMatrix offset=64");
        CHECK(r.uniforms[1].size == 64,   "u_ModelViewProjectionMatrix size=64");

        CHECK(strcmp(r.uniforms[2].name, "u_NormalMatrix") == 0, "uniform[2] = u_NormalMatrix");
        CHECK(r.uniforms[2].offset == 128, "u_NormalMatrix offset=128");
        CHECK(r.uniforms[2].size == 48,    "u_NormalMatrix size=48");

        CHECK(strcmp(r.uniforms[3].name, "u_Time") == 0, "uniform[3] = u_Time");
        CHECK(r.uniforms[3].offset == 176, "u_Time offset=176");
        CHECK(r.uniforms[3].size == 4,     "u_Time size=4");

        CHECK(r.ubo_total_size == 192, "UBO total=192 (180 rounded to 16)");
    }

    if (r.output) {
        CHECK(strstr(r.output, "#extension") == NULL, "extension directive removed");
        printf("\n--- Output ---\n%s--- End ---\n", r.output);
    }

    glslt_result_free(&r);
}

/* ---- Test: passthrough for modern GLSL ---- */

static void test_passthrough(void) {
    TEST("Passthrough Modern GLSL");

    const char *src =
        "#version 460\n"
        "layout(location = 0) in vec4 a_position;\n"
        "void main() { gl_Position = a_position; }\n";

    glslt_options_t opts;
    glslt_options_init(&opts);

    glslt_result_t r = glslt_transpile(src, GLSLT_VERTEX, &opts);

    CHECK(r.success, "transpile succeeded");
    CHECK(r.output != NULL, "output is not NULL");
    CHECK(strcmp(r.output, src) == 0, "output is identical to input (passthrough)");

    glslt_result_free(&r);
}

/* ---- Test: uniform arrays ---- */

static void test_uniform_arrays(void) {
    TEST("Uniform Arrays");

    const char *src =
        "#version 100\n"
        "uniform vec4 u_bones[64];\n"
        "uniform float u_weights[4];\n"
        "attribute vec4 a_position;\n"
        "void main() {\n"
        "    gl_Position = u_bones[0] * u_weights[0];\n"
        "}\n";

    glslt_options_t opts;
    glslt_options_init(&opts);

    glslt_result_t r = glslt_transpile(src, GLSLT_VERTEX, &opts);

    CHECK(r.success, "transpile succeeded");
    CHECK(r.num_uniforms == 2, "found 2 uniforms");

    if (r.num_uniforms == 2) {
        /* Sorted: u_bones, u_weights */
        CHECK(strcmp(r.uniforms[0].name, "u_bones") == 0, "first is u_bones");
        CHECK(r.uniforms[0].array_size == 64, "u_bones array_size=64");
        CHECK(r.uniforms[0].size == 64 * 16, "u_bones size=1024 (64 * vec4)");
        CHECK(r.uniforms[0].offset == 0, "u_bones offset=0");

        CHECK(strcmp(r.uniforms[1].name, "u_weights") == 0, "second is u_weights");
        CHECK(r.uniforms[1].array_size == 4, "u_weights array_size=4");
        /* float array: each element padded to 16 bytes in std140 */
        CHECK(r.uniforms[1].size == 4 * 16, "u_weights size=64 (4 * 16)");
        CHECK(r.uniforms[1].offset == 1024, "u_weights offset=1024");
    }

    if (r.output) {
        CHECK(strstr(r.output, "u_bones[64]") != NULL, "output has u_bones[64]");
        CHECK(strstr(r.output, "u_weights[4]") != NULL, "output has u_weights[4]");
        printf("\n--- Output ---\n%s--- End ---\n", r.output);
    }

    glslt_result_free(&r);
}

/* ---- Test: multiple uniforms on one line ---- */

static void test_multi_uniform(void) {
    TEST("Multiple Uniforms on One Line");

    const char *src =
        "#version 100\n"
        "uniform float a, b, c;\n"
        "attribute vec4 pos;\n"
        "void main() {\n"
        "    gl_Position = pos * a * b * c;\n"
        "}\n";

    glslt_options_t opts;
    glslt_options_init(&opts);

    glslt_result_t r = glslt_transpile(src, GLSLT_VERTEX, &opts);

    CHECK(r.success, "transpile succeeded");
    CHECK(r.num_uniforms == 3, "found 3 uniforms");

    if (r.num_uniforms == 3) {
        /* Sorted: a, b, c */
        CHECK(strcmp(r.uniforms[0].name, "a") == 0, "first is a");
        CHECK(strcmp(r.uniforms[1].name, "b") == 0, "second is b");
        CHECK(strcmp(r.uniforms[2].name, "c") == 0, "third is c");
        /* std140: float=4 bytes, align=4 */
        CHECK(r.uniforms[0].offset == 0, "a offset=0");
        CHECK(r.uniforms[1].offset == 4, "b offset=4");
        CHECK(r.uniforms[2].offset == 8, "c offset=8");
    }

    if (r.output) {
        printf("\n--- Output ---\n%s--- End ---\n", r.output);
    }

    glslt_result_free(&r);
}

/* ---- Test: varying location consistency ---- */

static void test_varying_consistency(void) {
    TEST("Varying Location Consistency (VS -> FS)");

    const char *vs_src =
        "#version 100\n"
        "attribute vec4 a_pos;\n"
        "varying vec3 v_normal;\n"
        "varying vec2 v_texcoord;\n"
        "varying vec4 v_color;\n"
        "void main() { gl_Position = a_pos; }\n";

    const char *fs_src =
        "#version 100\n"
        "precision mediump float;\n"
        "varying vec4 v_color;\n"
        "varying vec3 v_normal;\n"
        "varying vec2 v_texcoord;\n"
        "void main() { gl_FragColor = v_color; }\n";

    glslt_options_t vs_opts, fs_opts;
    glslt_options_init(&vs_opts);
    glslt_options_init(&fs_opts);

    /* Transpile VS first */
    glslt_result_t vs_r = glslt_transpile(vs_src, GLSLT_VERTEX, &vs_opts);
    CHECK(vs_r.success, "VS transpile succeeded");

    /* Pass varying locations to FS */
    for (int i = 0; i < vs_r.num_varyings; i++) {
        glslt_set_varying_location(&fs_opts, vs_r.varyings[i].name,
                                   vs_r.varyings[i].location);
    }

    /* Transpile FS */
    glslt_result_t fs_r = glslt_transpile(fs_src, GLSLT_FRAGMENT, &fs_opts);
    CHECK(fs_r.success, "FS transpile succeeded");

    /* Check that varying locations match */
    CHECK(vs_r.num_varyings == fs_r.num_varyings, "same number of varyings");

    for (int i = 0; i < vs_r.num_varyings; i++) {
        for (int j = 0; j < fs_r.num_varyings; j++) {
            if (strcmp(vs_r.varyings[i].name, fs_r.varyings[j].name) == 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "%s: VS loc=%d == FS loc=%d",
                         vs_r.varyings[i].name,
                         vs_r.varyings[i].location,
                         fs_r.varyings[j].location);
                CHECK(vs_r.varyings[i].location == fs_r.varyings[j].location, msg);
            }
        }
    }

    if (vs_r.output) printf("\n--- VS Output ---\n%s--- End ---\n", vs_r.output);
    if (fs_r.output) printf("\n--- FS Output ---\n%s--- End ---\n", fs_r.output);

    glslt_result_free(&vs_r);
    glslt_result_free(&fs_r);
}

/* ---- Test: no version directive (implicit ES 1.00) ---- */

static void test_no_version(void) {
    TEST("No #version Directive");

    const char *src =
        "attribute vec4 a_pos;\n"
        "void main() { gl_Position = a_pos; }\n";

    glslt_options_t opts;
    glslt_options_init(&opts);

    glslt_result_t r = glslt_transpile(src, GLSLT_VERTEX, &opts);

    CHECK(r.success, "transpile succeeded");
    CHECK(r.num_attributes == 1, "found 1 attribute");
    CHECK(r.output && strstr(r.output, "#version 460") != NULL, "output has #version 460");

    if (r.output) printf("\n--- Output ---\n%s--- End ---\n", r.output);

    glslt_result_free(&r);
}

/* ---- Test: comments preserved ---- */

static void test_comments(void) {
    TEST("Comments Preserved");

    const char *src =
        "#version 100\n"
        "// This is a vertex shader\n"
        "attribute vec4 a_pos;\n"
        "/* Multi-line\n"
        "   comment */\n"
        "void main() {\n"
        "    // Set position\n"
        "    gl_Position = a_pos;\n"
        "}\n";

    glslt_options_t opts;
    glslt_options_init(&opts);

    glslt_result_t r = glslt_transpile(src, GLSLT_VERTEX, &opts);

    CHECK(r.success, "transpile succeeded");
    if (r.output) {
        CHECK(strstr(r.output, "// This is a vertex shader") != NULL,
              "single-line comment preserved");
        CHECK(strstr(r.output, "// Set position") != NULL,
              "inline comment preserved");
        CHECK(strstr(r.output, "/* Multi-line") != NULL,
              "block comment start preserved");
        printf("\n--- Output ---\n%s--- End ---\n", r.output);
    }

    glslt_result_free(&r);
}

/* ---- Main ---- */

int main(void) {
    printf("glsl_transpiler test suite\n");
    printf("==========================\n");

    test_basic_vertex();
    test_basic_fragment();
    test_spearmint_vertex();
    test_passthrough();
    test_uniform_arrays();
    test_multi_uniform();
    test_varying_consistency();
    test_no_version();
    test_comments();

    printf("\n==========================\n");
    printf("Results: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
