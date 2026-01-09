/*
 * fbo_test_sgl - Test Framebuffer Objects (render-to-texture)
 * Target: SwitchGLES (deko3d backend)
 *
 * Tests:
 * 1. FBO creation and completion check
 * 2. FBO clear - clear to specific color and verify
 * 3. FBO render - render content and verify
 * 4. FBO sampling - sample FBO texture on screen and verify
 * 5. Interactive mode - rotating triangle for visual verification
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <switch.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2sgl.h>  /* SwitchGLES extensions */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FBO_WIDTH  256
#define FBO_HEIGHT 256
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

/*==========================================================================
 * Test results tracking
 *==========================================================================*/

static int g_passed = 0;
static int g_failed = 0;

static void recordResult(const char *name, bool passed) {
    if (passed) {
        printf("[PASS] %s\n", name);
        g_passed++;
    } else {
        printf("[FAIL] %s\n", name);
        g_failed++;
    }
    fflush(stdout);
}

/*==========================================================================
 * nxlink support
 *==========================================================================*/

static int s_nxlinkSock = -1;

static void initNxLink(void) {
    if (R_FAILED(socketInitializeDefault()))
        return;
    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock >= 0)
        printf("=== FBO TEST (SwitchGLES) ===\n");
    else
        socketExit();
}

static void deinitNxLink(void) {
    if (s_nxlinkSock >= 0) {
        close(s_nxlinkSock);
        socketExit();
        s_nxlinkSock = -1;
    }
}

/*==========================================================================
 * EGL state
 *==========================================================================*/

static EGLDisplay s_display;
static EGLContext s_context;
static EGLSurface s_surface;

static bool initEgl(void) {
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!s_display) {
        printf("Could not connect to display! error: %d\n", eglGetError());
        return false;
    }

    eglInitialize(s_display, NULL, NULL);

    EGLConfig config;
    EGLint numConfigs;
    static const EGLint framebufferAttributeList[] = {
        EGL_RED_SIZE,     8,
        EGL_GREEN_SIZE,   8,
        EGL_BLUE_SIZE,    8,
        EGL_ALPHA_SIZE,   8,
        EGL_DEPTH_SIZE,   24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    eglChooseConfig(s_display, framebufferAttributeList, &config, 1, &numConfigs);
    if (numConfigs == 0) {
        printf("No config found! error: %d\n", eglGetError());
        eglTerminate(s_display);
        return false;
    }

    s_surface = eglCreateWindowSurface(s_display, config, NULL, NULL);
    if (!s_surface) {
        printf("Surface creation failed! error: %d\n", eglGetError());
        eglTerminate(s_display);
        return false;
    }

    static const EGLint contextAttributeList[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttributeList);
    if (!s_context) {
        printf("Context creation failed! error: %d\n", eglGetError());
        eglDestroySurface(s_display, s_surface);
        eglTerminate(s_display);
        return false;
    }

    eglMakeCurrent(s_display, s_surface, s_surface, s_context);
    recordResult("EGL initialization", true);
    return true;
}

static void deinitEgl(void) {
    if (s_display) {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context) eglDestroyContext(s_display, s_context);
        if (s_surface) eglDestroySurface(s_display, s_surface);
        eglTerminate(s_display);
        s_display = NULL;
    }
}

/*==========================================================================
 * GL objects
 *==========================================================================*/

static GLuint g_triangleProgram = 0;
static GLint g_triangleMvpLoc = -1;

static GLuint g_quadProgram = 0;

static GLuint g_fboTexture = 0;
static GLuint g_fbo = 0;

/* External function from SwitchGLES to load precompiled dksh shaders */

static bool initShaders(void) {
    /* Triangle program */
    GLuint tvs = glCreateShader(GL_VERTEX_SHADER);
    GLuint tfs = glCreateShader(GL_FRAGMENT_SHADER);

    if (!sgl_load_shader_from_file(tvs, "romfs:/shaders/triangle_vsh.dksh")) {
        printf("Failed to load triangle vertex shader!\n");
        return false;
    }
    if (!sgl_load_shader_from_file(tfs, "romfs:/shaders/triangle_fsh.dksh")) {
        printf("Failed to load triangle fragment shader!\n");
        return false;
    }

    g_triangleProgram = glCreateProgram();
    glAttachShader(g_triangleProgram, tvs);
    glAttachShader(g_triangleProgram, tfs);
    glLinkProgram(g_triangleProgram);

    GLint linked;
    glGetProgramiv(g_triangleProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        printf("Triangle program link failed!\n");
        return false;
    }

    glDeleteShader(tvs);
    glDeleteShader(tfs);

    g_triangleMvpLoc = glGetUniformLocation(g_triangleProgram, "u_mvp");
    printf("Triangle MVP uniform location: %d\n", g_triangleMvpLoc);
    recordResult("Triangle shader program", g_triangleMvpLoc >= 0);

    /* Quad program */
    GLuint qvs = glCreateShader(GL_VERTEX_SHADER);
    GLuint qfs = glCreateShader(GL_FRAGMENT_SHADER);

    if (!sgl_load_shader_from_file(qvs, "romfs:/shaders/quad_vsh.dksh")) {
        printf("Failed to load quad vertex shader!\n");
        return false;
    }
    if (!sgl_load_shader_from_file(qfs, "romfs:/shaders/quad_fsh.dksh")) {
        printf("Failed to load quad fragment shader!\n");
        return false;
    }

    g_quadProgram = glCreateProgram();
    glAttachShader(g_quadProgram, qvs);
    glAttachShader(g_quadProgram, qfs);
    glLinkProgram(g_quadProgram);

    glGetProgramiv(g_quadProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        printf("Quad program link failed!\n");
        return false;
    }

    glDeleteShader(qvs);
    glDeleteShader(qfs);

    recordResult("Quad shader program", true);
    return true;
}

static bool initFBO(void) {
    /* Create texture for FBO */
    glGenTextures(1, &g_fboTexture);
    recordResult("glGenTextures (FBO)", g_fboTexture > 0);

    glBindTexture(GL_TEXTURE_2D, g_fboTexture);
    /* Pass NULL for pixels to create a renderable texture */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FBO_WIDTH, FBO_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    recordResult("glTexImage2D (FBO texture)", glGetError() == GL_NO_ERROR);

    /* Create FBO */
    glGenFramebuffers(1, &g_fbo);
    recordResult("glGenFramebuffers", g_fbo > 0);

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    recordResult("glBindFramebuffer", glGetError() == GL_NO_ERROR);

    /* Attach texture to FBO */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fboTexture, 0);
    recordResult("glFramebufferTexture2D", glGetError() == GL_NO_ERROR);

    /* Check FBO status */
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    recordResult("glCheckFramebufferStatus (complete)", status == GL_FRAMEBUFFER_COMPLETE);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO incomplete! Status: 0x%x\n", status);
        return false;
    }

    /* Unbind FBO */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

/*==========================================================================
 * Matrix helpers
 *==========================================================================*/

static void mat4_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_rotate_z(float *m, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    float r[16];
    mat4_identity(r);
    r[0] = c;  r[1] = s;
    r[4] = -s; r[5] = c;

    float tmp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i*4+j] = 0;
            for (int k = 0; k < 4; k++) {
                tmp[i*4+j] += m[i*4+k] * r[k*4+j];
            }
        }
    }
    memcpy(m, tmp, sizeof(tmp));
}

/*==========================================================================
 * Drawing
 *==========================================================================*/

/* Triangle vertices: position (xyz) + color (rgb) */
static const float triangleData[] = {
     0.0f,  0.7f, 0.0f,  1.0f, 0.0f, 0.0f,
    -0.6f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
     0.6f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f,
};

/* Fullscreen quad: position (xy) + texcoord (uv) */
static const float quadData[] = {
    -0.8f, -0.8f,  0.0f, 0.0f,
     0.8f, -0.8f,  1.0f, 0.0f,
    -0.8f,  0.8f,  0.0f, 1.0f,
     0.8f,  0.8f,  1.0f, 1.0f,
};

static void renderToFBO(int frame) {
    /* IMPORTANT: Unbind the FBO texture before rendering TO it */
    /* You cannot read from and write to the same texture simultaneously */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Bind FBO */
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);

    /* Clear with dark blue */
    glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Draw rotating triangle */
    glUseProgram(g_triangleProgram);

    float mvp[16];
    mat4_identity(mvp);
    mat4_rotate_z(mvp, frame * 0.03f);
    glUniformMatrix4fv(g_triangleMvpLoc, 1, GL_FALSE, mvp);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), triangleData);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), triangleData + 3);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    /* Unbind FBO */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void renderToScreen(void) {
    /* Render to screen */
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Clear with dark gray */
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Draw textured quad with FBO texture */
    glUseProgram(g_quadProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_fboTexture);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/*==========================================================================
 * Validation Tests
 *==========================================================================*/

static void runValidationTests(void) {
    printf("\n========================================\n");
    printf("       FBO VALIDATION TESTS\n");
    printf("========================================\n\n");

    GLubyte pixel[4];

    /* ========================================
     * TEST 1: FBO Clear Test
     * Clear FBO to orange and verify
     * ======================================== */
    printf("--- Test 1: FBO Clear ---\n");

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);
    glClearColor(1.0f, 0.5f, 0.0f, 1.0f);  /* Orange */
    glClear(GL_COLOR_BUFFER_BIT);

    /* Switch back to default FB to sample FBO texture */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Clear screen to black */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Draw FBO texture to screen */
    glUseProgram(g_quadProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_fboTexture);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    /* Read pixel from center of screen (where FBO texture is displayed) */
    glReadPixels(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("FBO Clear pixel: R=%d G=%d B=%d A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    /* Orange: R~255, G~127, B~0 */
    bool orangeOk = (pixel[0] >= 240) &&
                    (pixel[1] >= 115 && pixel[1] <= 140) &&
                    (pixel[2] < 20);
    recordResult("FBO clear to orange", orangeOk);

    eglSwapBuffers(s_display, s_surface);

    /* ========================================
     * TEST 2: FBO Clear Different Color
     * Clear FBO to cyan and verify
     * ======================================== */
    printf("\n--- Test 2: FBO Clear Different Color ---\n");

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);
    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);  /* Cyan */
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_quadProgram);
    glBindTexture(GL_TEXTURE_2D, g_fboTexture);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glReadPixels(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("FBO Cyan pixel: R=%d G=%d B=%d A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    /* Cyan: R~0, G~255, B~255 */
    bool cyanOk = (pixel[0] < 20) &&
                  (pixel[1] >= 240) &&
                  (pixel[2] >= 240);
    recordResult("FBO clear to cyan", cyanOk);

    eglSwapBuffers(s_display, s_surface);

    /* ========================================
     * TEST 3: FBO Render Triangle
     * Render triangle to FBO and verify non-black content
     * ======================================== */
    printf("\n--- Test 3: FBO Render Triangle ---\n");

    /* IMPORTANT: Unbind texture before rendering to it */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);

    /* Clear to dark blue */
    glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Draw triangle */
    glUseProgram(g_triangleProgram);
    float mvp[16];
    mat4_identity(mvp);
    glUniformMatrix4fv(g_triangleMvpLoc, 1, GL_FALSE, mvp);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), triangleData);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), triangleData + 3);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_quadProgram);
    glBindTexture(GL_TEXTURE_2D, g_fboTexture);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    /* Read pixel from center - should be inside the triangle */
    glReadPixels(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("FBO Triangle center pixel: R=%d G=%d B=%d A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    /* The center of the triangle should have interpolated vertex colors (R+G+B mix)
     * NOT the dark blue background (25, 25, 76). At center, expect ~(85, 85, 85) */
    bool triangleOk = (pixel[0] > 50 || pixel[1] > 50) && (pixel[2] < 200);
    recordResult("FBO render triangle (interpolated colors, not bg)", triangleOk);

    /* Read pixel from corner - should be dark blue background */
    glReadPixels(SCREEN_WIDTH / 4, SCREEN_HEIGHT / 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("FBO Triangle corner pixel: R=%d G=%d B=%d A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    /* Dark blue: R~25, G~25, B~76 (0.1, 0.1, 0.3) */
    bool bgOk = (pixel[0] >= 20 && pixel[0] <= 35) &&
                (pixel[1] >= 20 && pixel[1] <= 35) &&
                (pixel[2] >= 65 && pixel[2] <= 85);
    recordResult("FBO render background (dark blue)", bgOk);

    eglSwapBuffers(s_display, s_surface);

    /* ========================================
     * TEST 4: Multiple FBO Operations
     * Bind FBO multiple times and verify consistency
     * ======================================== */
    printf("\n--- Test 4: Multiple FBO Operations ---\n");

    /* First pass: clear to red */
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Second pass: clear to green */
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Third pass: clear to blue */
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_quadProgram);
    glBindTexture(GL_TEXTURE_2D, g_fboTexture);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quadData + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glReadPixels(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("FBO Multi-op pixel: R=%d G=%d B=%d A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    /* Should be blue (last clear) */
    bool blueOk = (pixel[0] < 20) && (pixel[1] < 20) && (pixel[2] >= 240);
    recordResult("FBO multiple clears (final=blue)", blueOk);

    eglSwapBuffers(s_display, s_surface);

    /* Print summary */
    printf("\n========================================\n");
    printf("       FBO TEST SUMMARY\n");
    printf("========================================\n");
    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
    printf("========================================\n\n");

    if (g_failed == 0) {
        printf("*** ALL FBO TESTS PASSED! ***\n\n");
    } else {
        printf("*** %d FBO TESTS FAILED ***\n\n", g_failed);
    }
    fflush(stdout);
}

/*==========================================================================
 * Main
 *==========================================================================*/

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    romfsInit();
    initNxLink();

    printf("\n===========================================\n");
    printf("  fbo_test_sgl - Framebuffer Object Test\n");
    printf("  Using SwitchGLES (deko3d backend)\n");
    printf("===========================================\n\n");

    if (!initEgl()) {
        printf("FATAL: EGL initialization failed!\n");
        deinitNxLink();
        romfsExit();
        return EXIT_FAILURE;
    }

    if (!initShaders()) {
        printf("FATAL: Shader initialization failed!\n");
        deinitEgl();
        deinitNxLink();
        romfsExit();
        return EXIT_FAILURE;
    }

    if (!initFBO()) {
        printf("FATAL: FBO initialization failed!\n");
        deinitEgl();
        deinitNxLink();
        romfsExit();
        return EXIT_FAILURE;
    }

    /* Run automated validation tests */
    runValidationTests();

    /* Interactive mode */
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("Interactive mode: rotating triangle\n");
    printf("Press + to exit...\n\n");
    fflush(stdout);

    int frame = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;

        /* Render triangle to FBO texture */
        renderToFBO(frame);

        /* Render textured quad to screen */
        renderToScreen();

        eglSwapBuffers(s_display, s_surface);

        frame++;
        if (frame == 1) {
            printf("First interactive frame rendered!\n");
            fflush(stdout);
        }
    }

    printf("Exiting...\n");
    glDeleteFramebuffers(1, &g_fbo);
    glDeleteTextures(1, &g_fboTexture);
    glDeleteProgram(g_triangleProgram);
    glDeleteProgram(g_quadProgram);
    deinitEgl();
    deinitNxLink();
    romfsExit();
    return EXIT_SUCCESS;
}
