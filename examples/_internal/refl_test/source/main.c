/*
 * refl_test — on-device validation of the uam reflection sidecar (.refl).
 *
 * Validates the change: precompiled DKSH shaders resolve glGetUniformLocation()
 * for arbitrary uniform names via a `.refl` sidecar, WITHOUT sglRegisterUniform()
 * and WITHOUT the (removed) hardcoded built-in name table.
 *
 * Because the offline uam.exe is unavailable on this host, the test generates
 * the precompiled .dksh + .refl AT RUNTIME using the bundled uam library, writes
 * them to the SD card, then reloads them through the real precompiled file path
 * (sgl_load_shader_from_file → sgl_load_reflection_sidecar).
 *
 * Assertions (non-interactive, printed to nxlink):
 *   A1  WITH .refl:   glGetUniformLocation(prog, CUSTOM)        >= 0
 *   A2  end-to-end:   draw with that location → center pixel == GREEN
 *   A3  NO   .refl:   glGetUniformLocation(prog, CUSTOM)        == -1
 *   A4  table gone:   glGetUniformLocation(prog, "u_color")     == -1
 *
 * CUSTOM is a uniform name that was never in the old built-in strcmp table and
 * is never registered — so only the .refl path can resolve it. A4 queries a name
 * that the OLD table did resolve (u_color), proving the table is truly removed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h> /* memalign — uam_write_code requires a 256-byte aligned buffer */
#include <unistd.h>
#include <switch.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2sgl.h>
#include <uam.h> /* bundled into libSwitchGLES.a — runtime .dksh + .refl generation */

#define CUSTOM_UNIFORM "uReflProbe_Z9" /* never built-in, never registered */

#define VS_PATH        "sdmc:/refltest_vs.dksh"
#define FS_PATH        "sdmc:/refltest_fs.dksh"
#define VS_PATH_NOREFL "sdmc:/refltest_vs_norefl.dksh"
#define FS_PATH_NOREFL "sdmc:/refltest_fs_norefl.dksh"

static const char *VS_SRC =
    "#version 100\n"
    "attribute vec2 aReflPos;\n"
    "void main() { gl_Position = vec4(aReflPos, 0.0, 1.0); }\n";

/* Bare ES 1.00 uniform → Mesa driver constbuf → name captured in reflection. */
static const char *FS_SRC =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform vec4 " CUSTOM_UNIFORM ";\n"
    "void main() { gl_FragColor = " CUSTOM_UNIFORM "; }\n";

/* ========================================================================== */
/* nxlink + EGL boilerplate                                                   */
/* ========================================================================== */
static int s_nxlinkSock = -1;
static EGLDisplay s_dpy;
static EGLSurface s_surf;
static EGLContext s_ctx;

static void initNxLink(void) {
    socketInitializeDefault();
    s_nxlinkSock = nxlinkStdio();
}
static void deinitNxLink(void) {
    if (s_nxlinkSock >= 0) { close(s_nxlinkSock); s_nxlinkSock = -1; }
    socketExit();
}

static bool initEgl(void) {
    s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_dpy == EGL_NO_DISPLAY) { printf("[FAIL] eglGetDisplay\n"); return false; }
    if (!eglInitialize(s_dpy, NULL, NULL)) { printf("[FAIL] eglInitialize\n"); return false; }
    EGLConfig config;
    EGLint numConfigs;
    EGLint attribs[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                         EGL_ALPHA_SIZE, 8, EGL_NONE };
    eglChooseConfig(s_dpy, attribs, &config, 1, &numConfigs);
    if (numConfigs == 0) { printf("[FAIL] eglChooseConfig\n"); return false; }
    s_surf = eglCreateWindowSurface(s_dpy, config, NULL, NULL);
    if (s_surf == EGL_NO_SURFACE) { printf("[FAIL] eglCreateWindowSurface\n"); return false; }
    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    s_ctx = eglCreateContext(s_dpy, config, EGL_NO_CONTEXT, ctxAttribs);
    if (s_ctx == EGL_NO_CONTEXT) { printf("[FAIL] eglCreateContext\n"); return false; }
    eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx);
    printf("[OK] EGL initialized\n");
    return true;
}
static void deinitEgl(void) {
    eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(s_dpy, s_ctx);
    eglDestroySurface(s_dpy, s_surf);
    eglTerminate(s_dpy);
}

/* ========================================================================== */
/* Runtime shader generation via bundled uam                                  */
/* ========================================================================== */

/* Compile `src` with uam, write the DKSH to `dkshPath`, and (if withRefl) the
 * reflection sidecar to `<dkshPath>.refl`. Returns true on success. */
static bool gen_shader(DkStage stage, const char *src, const char *dkshPath, bool withRefl) {
    uam_compiler *c = uam_create_compiler(stage);
    if (!c) { printf("  [uam] create_compiler failed\n"); return false; }

    if (!uam_compile_dksh(c, src)) {
        printf("  [uam] compile failed: %s\n", uam_get_error_log(c));
        uam_free_compiler(c);
        return false;
    }

    size_t sz = uam_get_code_size(c);
    /* uam_write_code aligns DKSH sections to the ABSOLUTE buffer address (256),
     * so the destination MUST be 256-byte aligned or the written binary is
     * corrupt (faults the GPU on draw). The runtime path uses memalign too. */
    void *buf = memalign(256, sz);
    bool ok = false;
    if (buf) {
        uam_write_code(c, buf);
        FILE *f = fopen(dkshPath, "wb");
        if (f) {
            ok = (fwrite(buf, 1, sz, f) == sz);
            fclose(f);
        }
        free(buf);
    }

    if (ok && withRefl) {
        char reflPath[512];
        snprintf(reflPath, sizeof(reflPath), "%s.refl", dkshPath);
        ok = uam_write_reflection(c, reflPath);
        if (!ok) printf("  [uam] write_reflection failed\n");
    }

    uam_free_compiler(c);
    return ok;
}

/* Build + link a program from two precompiled DKSH files (precompiled path).
 * Returns the program (0 on failure); outputs the shader ids for cleanup. */
static GLuint build_precompiled_program(const char *vsPath, const char *fsPath,
                                        GLuint *outVs, GLuint *outFs) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    *outVs = vs; *outFs = fs;

    if (!sgl_load_shader_from_file(vs, vsPath)) {
        printf("  [FAIL] load VS %s\n", vsPath); return 0;
    }
    if (!sgl_load_shader_from_file(fs, fsPath)) {
        printf("  [FAIL] load FS %s\n", fsPath); return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint linkOk = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);
    if (!linkOk) { printf("  [FAIL] link\n"); glDeleteProgram(prog); return 0; }
    return prog;
}

static void cleanup_files(void) {
    remove(VS_PATH);        remove(VS_PATH ".refl");
    remove(FS_PATH);        remove(FS_PATH ".refl");
    remove(VS_PATH_NOREFL); remove(FS_PATH_NOREFL);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    initNxLink();
    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered: last printed line = crash site */
    printf("\n=== SwitchGLES reflection sidecar (.refl) on-device test ===\n\n");

    if (!initEgl()) { deinitNxLink(); return 1; }

    int pass = 0, fail = 0;

    /* Generate precompiled shaders + sidecars at runtime via bundled uam. */
    printf("--- Generating precompiled shaders (runtime uam) ---\n");
    bool gen_ok = true;
    gen_ok &= gen_shader(DkStage_Vertex,   VS_SRC, VS_PATH,        true);
    gen_ok &= gen_shader(DkStage_Fragment, FS_SRC, FS_PATH,        true);
    gen_ok &= gen_shader(DkStage_Vertex,   VS_SRC, VS_PATH_NOREFL, false);
    gen_ok &= gen_shader(DkStage_Fragment, FS_SRC, FS_PATH_NOREFL, false);
    if (!gen_ok) {
        printf("[FAIL] shader generation failed — cannot run tests\n");
        cleanup_files(); deinitEgl(); deinitNxLink();
        return 1;
    }
    printf("[OK] generated .dksh (+ .refl) on SD card\n\n");
    fflush(stdout);

    /* ------------------------------------------------------------------ */
    /* A1: precompiled shader WITH .refl resolves the custom uniform       */
    /* A2: that location matches the one the live runtime path produces    */
    /*     for the identical source (convergence — no GPU draw needed)     */
    /* ------------------------------------------------------------------ */
    printf("--- A1/A2: precompiled + .refl resolves custom uniform ---\n");
    {
        GLuint vs, fs;
        GLuint prog = build_precompiled_program(VS_PATH, FS_PATH, &vs, &fs);
        if (prog) {
            GLint loc = glGetUniformLocation(prog, CUSTOM_UNIFORM);
            printf("  precompiled+.refl location = %d (0x%X)\n", loc, loc);

            /* A1 — the custom name resolves only via the .refl reflection. */
            if (loc >= 0) { printf("[PASS] A1 custom uniform resolved via .refl\n"); pass++; }
            else          { printf("[FAIL] A1 custom uniform unresolved (got %d)\n", loc); fail++; }

            /* A2 — compile the SAME source at runtime and compare locations.
             * Equal locations prove the .refl path yields reflection identical
             * to the live runtime path. Pure metadata check, no draw. */
            GLuint rvs = glCreateShader(GL_VERTEX_SHADER);
            GLuint rfs = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(rvs, 1, &VS_SRC, NULL);
            glShaderSource(rfs, 1, &FS_SRC, NULL);
            glCompileShader(rvs);
            glCompileShader(rfs);
            GLuint rprog = glCreateProgram();
            glAttachShader(rprog, rvs);
            glAttachShader(rprog, rfs);
            glLinkProgram(rprog);
            GLint rLinkOk = 0;
            glGetProgramiv(rprog, GL_LINK_STATUS, &rLinkOk);
            GLint locRuntime = rLinkOk ? glGetUniformLocation(rprog, CUSTOM_UNIFORM) : -2;
            printf("  runtime-compiled location  = %d (0x%X)\n", locRuntime, locRuntime);
            if (rLinkOk && locRuntime >= 0 && locRuntime == loc) {
                printf("[PASS] A2 .refl reflection matches runtime path\n"); pass++;
            } else {
                printf("[FAIL] A2 mismatch: precompiled=%d runtime=%d\n", loc, locRuntime); fail++;
            }
            glDeleteProgram(rprog);
            glDeleteShader(rvs);
            glDeleteShader(rfs);

            glDeleteProgram(prog);
        } else {
            printf("[FAIL] A1/A2 — could not build precompiled program\n"); fail += 2;
        }
        glDeleteShader(vs); glDeleteShader(fs);
        fflush(stdout);
    }

    /* ------------------------------------------------------------------ */
    /* A3 + A4: precompiled shader WITHOUT .refl                           */
    /* ------------------------------------------------------------------ */
    printf("\n--- A3/A4: precompiled WITHOUT .refl — no resolution, table gone ---\n");
    {
        GLuint vs, fs;
        GLuint prog = build_precompiled_program(VS_PATH_NOREFL, FS_PATH_NOREFL, &vs, &fs);
        if (prog) {
            GLint locCustom = glGetUniformLocation(prog, CUSTOM_UNIFORM);
            GLint locBuiltin = glGetUniformLocation(prog, "u_color");
            printf("  glGetUniformLocation(\"%s\") = %d\n", CUSTOM_UNIFORM, locCustom);
            printf("  glGetUniformLocation(\"u_color\")     = %d\n", locBuiltin);

            /* A3 */
            if (locCustom == -1) { printf("[PASS] A3 no .refl → custom unresolved\n"); pass++; }
            else { printf("[FAIL] A3 expected -1, got %d\n", locCustom); fail++; }

            /* A4 — proves the old built-in strcmp table is removed. */
            if (locBuiltin == -1) { printf("[PASS] A4 built-in table removed (u_color → -1)\n"); pass++; }
            else { printf("[FAIL] A4 built-in table still present (u_color → %d)\n", locBuiltin); fail++; }

            glDeleteProgram(prog);
        } else {
            printf("[FAIL] A3/A4 — could not build precompiled program\n"); fail += 2;
        }
        glDeleteShader(vs); glDeleteShader(fs);
        fflush(stdout);
    }

    /* ------------------------------------------------------------------ */
    /* A5: end-to-end DRAW with the precompiled + .refl program.           */
    /*     Exercises the precompiled file-load path on the GPU (regression  */
    /*     guard for the dk_load_shader_file DKSH-padding zeroing fix).      */
    /* ------------------------------------------------------------------ */
    printf("\n--- A5: draw with precompiled + .refl program ---\n");
    {
        GLuint vs, fs;
        GLuint prog = build_precompiled_program(VS_PATH, FS_PATH, &vs, &fs);
        if (prog) {
            GLint loc = glGetUniformLocation(prog, CUSTOM_UNIFORM);

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glViewport(0, 0, 1280, 720);

            printf("  step: clear\n");
            glClearColor(0.0f, 0.0f, 1.0f, 1.0f); /* BLUE background */
            glClear(GL_COLOR_BUFFER_BIT);
            printf("  step: useProgram\n");
            glUseProgram(prog);
            printf("  step: uniform4f loc=%d\n", loc);
            glUniform4f(loc, 0.0f, 1.0f, 0.0f, 1.0f); /* GREEN */

            GLint posLoc = glGetAttribLocation(prog, "aReflPos");
            printf("  step: attrib loc=%d\n", posLoc);
            if (posLoc < 0) posLoc = 0;
            static const float tri[] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
            glVertexAttribPointer((GLuint)posLoc, 2, GL_FLOAT, GL_FALSE, 0, tri);
            glEnableVertexAttribArray((GLuint)posLoc);
            printf("  step: drawArrays\n");
            glDrawArrays(GL_TRIANGLES, 0, 3);
            printf("  step: post-draw\n");

            GLenum err = glGetError();
            GLubyte px[4] = {0};
            glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            printf("  glError=0x%X  center pixel: R=%d G=%d B=%d A=%d\n", err, px[0], px[1], px[2], px[3]);
            if (px[0] < 20 && px[1] > 235 && px[2] < 20) {
                printf("[PASS] A5 precompiled shader draws (GREEN)\n"); pass++;
            } else {
                printf("[FAIL] A5 expected GREEN, got %d,%d,%d\n", px[0], px[1], px[2]); fail++;
            }
            glDisableVertexAttribArray((GLuint)posLoc);
            glDeleteProgram(prog);
        } else {
            printf("[FAIL] A5 — could not build precompiled program\n"); fail++;
        }
        glDeleteShader(vs); glDeleteShader(fs);
        fflush(stdout);
    }

    /* ------------------------------------------------------------------ */
    printf("\n========================================\n");
    printf("  Reflection sidecar on-device test\n");
    printf("  Passed: %d   Failed: %d\n", pass, fail);
    printf("  RESULT: %s\n", fail == 0 ? "ALL PASS" : "FAILURES");
    printf("========================================\n");
    fflush(stdout);

    cleanup_files();
    deinitEgl();
    deinitNxLink();
    return fail == 0 ? 0 : 1;
}
