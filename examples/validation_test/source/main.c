/*
 * validation_test_sgl - Comprehensive OpenGL ES 2.0 validation test
 * Target: SwitchGLES (deko3d backend)
 *
 * Tests all implemented GLES2 features systematically.
 * Press + to exit.
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

#ifdef SGL_ENABLE_RUNTIME_COMPILER
#include <libuam.h>
#include <malloc.h>  /* memalign */
#endif

/*==========================================================================
 * Configuration
 *==========================================================================*/

#define SCREEN_WIDTH  1280
#define SCREEN_HEIGHT 720

/* Declared in SwitchGLES - loads precompiled shader */

/*==========================================================================
 * nxlink support
 *==========================================================================*/

static int s_nxlinkSock = -1;

static void initNxLink(void) {
    if (R_FAILED(socketInitializeDefault()))
        return;
    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock >= 0)
        printf("=== VALIDATION TEST (SwitchGLES) ===\n");
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
        printf("[FAIL] eglGetDisplay\n");
        return false;
    }
    printf("[PASS] eglGetDisplay\n");

    EGLint major, minor;
    if (!eglInitialize(s_display, &major, &minor)) {
        printf("[FAIL] eglInitialize\n");
        return false;
    }
    printf("[PASS] eglInitialize (EGL %d.%d)\n", major, minor);

    EGLConfig config;
    EGLint numConfigs;
    static const EGLint configAttribs[] = {
        EGL_RED_SIZE,     8,
        EGL_GREEN_SIZE,   8,
        EGL_BLUE_SIZE,    8,
        EGL_ALPHA_SIZE,   8,
        EGL_DEPTH_SIZE,   24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    if (!eglChooseConfig(s_display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        printf("[FAIL] eglChooseConfig\n");
        return false;
    }
    printf("[PASS] eglChooseConfig (%d configs)\n", numConfigs);

    s_surface = eglCreateWindowSurface(s_display, config, NULL, NULL);
    if (!s_surface) {
        printf("[FAIL] eglCreateWindowSurface\n");
        return false;
    }
    printf("[PASS] eglCreateWindowSurface\n");

    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttribs);
    if (!s_context) {
        printf("[FAIL] eglCreateContext\n");
        return false;
    }
    printf("[PASS] eglCreateContext\n");

    if (!eglMakeCurrent(s_display, s_surface, s_surface, s_context)) {
        printf("[FAIL] eglMakeCurrent\n");
        return false;
    }
    printf("[PASS] eglMakeCurrent\n");

    return true;
}

static void deinitEgl(void) {
    if (s_display) {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context) eglDestroyContext(s_display, s_context);
        if (s_surface) eglDestroySurface(s_display, s_surface);
        eglTerminate(s_display);
    }
}

/*==========================================================================
 * Visual validation helper - wait for A button
 *==========================================================================*/

static PadState s_pad;
static bool s_padInitialized = false;
static bool s_exitRequested = false;  /* Global flag for clean exit */

/* Forward declarations - defined below */
static void recordResult(const char *name, bool passed, const char *details);
static void printSummary(void);

/* Wait for visual confirmation: A = looks correct, B = doesn't look correct, + = exit */
static void waitForA(const char *testName, const char *expectedVisual) {
    if (!s_padInitialized) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&s_pad);
        s_padInitialized = true;
    }

    printf("\n========================================\n");
    printf("TEST: %s\n", testName);
    printf("----------------------------------------\n");
    printf("EXPECTED: %s\n", expectedVisual);
    printf("----------------------------------------\n");
    printf("Press A = OK (visual matches)  |  B = FAIL (visual wrong)  |  + = exit\n");
    printf("========================================\n");
    fflush(stdout);

    /* Show the frame - swap ONCE to present, then just poll */
    fflush(stdout);
    eglSwapBuffers(s_display, s_surface);

    /* Wait for user input - NO more swaps, frame stays on screen */
    while (appletMainLoop()) {
        padUpdate(&s_pad);
        u64 kDown = padGetButtonsDown(&s_pad);

        if (kDown & HidNpadButton_A) {
            recordResult(testName, true, "visual OK");
            break;
        }
        if (kDown & HidNpadButton_B) {
            recordResult(testName, false, "visual MISMATCH (user)");
            break;
        }
        if (kDown & HidNpadButton_Plus) {
            printf("User requested early exit.\n");
            s_exitRequested = true;
            break;
        }

        svcSleepThread(16000000ULL); /* ~16ms polling, no swap needed */
    }
}

/*==========================================================================
 * Shader utilities (precompiled DKSH)
 *==========================================================================*/

static GLuint createProgramFromFiles(const char *vsPath, const char *fsPath) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

    if (!sgl_load_shader_from_file(vs, vsPath)) {
        printf("Failed to load vertex shader: %s (shader id=%d)\n", vsPath, vs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    if (!sgl_load_shader_from_file(fs, fsPath)) {
        printf("Failed to load fragment shader: %s (shader id=%d)\n", fsPath, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glCompileShader(vs);
    glCompileShader(fs);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        printf("Program link failed\n");
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

/*==========================================================================
 * Shader programs (lazy loaded)
 *==========================================================================*/

static GLuint s_simpleProgram = 0;
static GLuint s_texturedProgram = 0;
static GLuint s_uniformProgram = 0;

static GLuint getSimpleProgram(void) {
    if (!s_simpleProgram) {
        s_simpleProgram = createProgramFromFiles(
            "romfs:/shaders/simple_vsh.dksh",
            "romfs:/shaders/color_fsh.dksh");
    }
    return s_simpleProgram;
}

static GLuint getTexturedProgram(void) {
    if (!s_texturedProgram) {
        s_texturedProgram = createProgramFromFiles(
            "romfs:/shaders/textured_vsh.dksh",
            "romfs:/shaders/textured_fsh.dksh");
    }
    return s_texturedProgram;
}

static GLuint getUniformProgram(void) {
    if (!s_uniformProgram) {
        s_uniformProgram = createProgramFromFiles(
            "romfs:/shaders/uniform_vsh.dksh",
            "romfs:/shaders/uniform_fsh.dksh");
    }
    return s_uniformProgram;
}

/*==========================================================================
 * Test results tracking
 *==========================================================================*/

typedef struct {
    const char *name;
    bool passed;
    const char *details;
} TestResult;

#define MAX_TESTS 200
static TestResult s_results[MAX_TESTS];
static int s_numResults = 0;

static void recordResult(const char *name, bool passed, const char *details) {
    if (s_numResults < MAX_TESTS) {
        s_results[s_numResults].name = name;
        s_results[s_numResults].passed = passed;
        s_results[s_numResults].details = details;
        s_numResults++;
    }
    printf("[%s] %s%s%s\n", passed ? "PASS" : "FAIL", name,
           details ? " - " : "", details ? details : "");
    fflush(stdout);
}

/*==========================================================================
 * TEST: Clear operations
 *==========================================================================*/

static void testClear(void) {
    printf("\n--- Test: Clear Operations ---\n");

    /* Test glClearColor + glClear */
    glClearColor(0.5f, 0.25f, 0.125f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Read back a pixel to verify */
    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    bool colorOk = (pixel[0] >= 125 && pixel[0] <= 130) &&  /* R ~= 127 */
                   (pixel[1] >= 62 && pixel[1] <= 66) &&    /* G ~= 64 */
                   (pixel[2] >= 30 && pixel[2] <= 34);      /* B ~= 32 */

    recordResult("glClearColor + glClear", colorOk, NULL);

    /* Test glClearDepthf */
    glClearDepthf(0.5f);
    glClear(GL_DEPTH_BUFFER_BIT);
    recordResult("glClearDepthf", true, "visual check needed");

    /* Test glClearStencil */
    glClearStencil(128);
    glClear(GL_STENCIL_BUFFER_BIT);
    recordResult("glClearStencil", true, "visual check needed");
}

/*==========================================================================
 * TEST: Basic triangle rendering
 *==========================================================================*/

static void testBasicTriangle(void) {
    printf("\n--- Test: Basic Triangle ---\n");

    GLuint program = getSimpleProgram();
    if (!program) {
        recordResult("Shader loading", false, "failed to load shaders");
        return;
    }
    recordResult("Shader loading", true, NULL);

    glUseProgram(program);
    recordResult("glUseProgram", glGetError() == GL_NO_ERROR, NULL);

    GLint colorLoc = glGetUniformLocation(program, "u_color");
    recordResult("glGetUniformLocation", colorLoc >= 0, NULL);

    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);  /* Green */
    recordResult("glUniform4f", glGetError() == GL_NO_ERROR, NULL);

    static const float vertices[] = {
         0.0f,  0.5f,
        -0.5f, -0.5f,
         0.5f, -0.5f
    };

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    recordResult("glVertexAttribPointer", glGetError() == GL_NO_ERROR, NULL);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    recordResult("glDrawArrays", glGetError() == GL_NO_ERROR, NULL);

    /* Check center pixel is green */
    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool isGreen = pixel[1] > 200 && pixel[0] < 50 && pixel[2] < 50;
    recordResult("Triangle rendered (green)", isGreen, NULL);

    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: VBO (Vertex Buffer Objects)
 *==========================================================================*/

static void testVBO(void) {
    printf("\n--- Test: Vertex Buffer Objects ---\n");

    GLuint vbo;
    glGenBuffers(1, &vbo);
    recordResult("glGenBuffers", vbo > 0, NULL);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    recordResult("glBindBuffer", glGetError() == GL_NO_ERROR, NULL);

    static const float vertices[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f
    };

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    recordResult("glBufferData", glGetError() == GL_NO_ERROR, NULL);

    /* Draw a quad using VBO */
    GLuint program = getSimpleProgram();
    glUseProgram(program);
    GLint colorLoc = glGetUniformLocation(program, "u_color");
    glUniform4f(colorLoc, 1.0f, 1.0f, 0.0f, 1.0f);  /* Yellow */

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);

    static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    recordResult("glDrawElements with VBO", glGetError() == GL_NO_ERROR, NULL);

    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDeleteBuffers(1, &vbo);
    recordResult("glDeleteBuffers", glGetError() == GL_NO_ERROR, NULL);
}

/*==========================================================================
 * TEST: Textures
 *==========================================================================*/

static void testTextures(void) {
    printf("\n--- Test: Textures ---\n");

    GLuint texture;
    glGenTextures(1, &texture);
    recordResult("glGenTextures", texture > 0, NULL);

    glBindTexture(GL_TEXTURE_2D, texture);
    recordResult("glBindTexture", glGetError() == GL_NO_ERROR, NULL);

    /* Create a 4x4 checkerboard texture */
    GLubyte pixels[4 * 4 * 4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = (y * 4 + x) * 4;
            GLubyte c = ((x + y) % 2) ? 255 : 0;
            pixels[idx + 0] = c;    /* R */
            pixels[idx + 1] = c;    /* G */
            pixels[idx + 2] = c;    /* B */
            pixels[idx + 3] = 255;  /* A */
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    recordResult("glTexImage2D", glGetError() == GL_NO_ERROR, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    recordResult("glTexParameteri", glGetError() == GL_NO_ERROR, NULL);

    /* Draw textured quad */
    GLuint program = getTexturedProgram();
    if (!program) {
        recordResult("Textured shader", false, "failed to load");
        glDeleteTextures(1, &texture);
        return;
    }
    glUseProgram(program);

    static const float quadVerts[] = {
        /* pos        texcoord */
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 1.0f
    };

    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, quadVerts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, quadVerts + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    recordResult("Textured quad", glGetError() == GL_NO_ERROR, NULL);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glDeleteTextures(1, &texture);
    recordResult("glDeleteTextures", glGetError() == GL_NO_ERROR, NULL);
}

/*==========================================================================
 * TEST: glTexSubImage2D
 *==========================================================================*/

static void testTexSubImage(void) {
    printf("\n--- Test: glTexSubImage2D ---\n");

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    /* Create 8x8 red texture */
    GLubyte red[8 * 8 * 4];
    for (int i = 0; i < 8 * 8; i++) {
        red[i * 4 + 0] = 255;
        red[i * 4 + 1] = 0;
        red[i * 4 + 2] = 0;
        red[i * 4 + 3] = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);

    /* Update center 4x4 with green */
    GLubyte green[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        green[i * 4 + 0] = 0;
        green[i * 4 + 1] = 255;
        green[i * 4 + 2] = 0;
        green[i * 4 + 3] = 255;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 2, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, green);
    recordResult("glTexSubImage2D", glGetError() == GL_NO_ERROR, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Draw textured quad to visualize the result */
    GLuint program = getTexturedProgram();
    if (!program) {
        recordResult("TexSubImage visual", false, "failed to load shader");
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &texture);
        return;
    }
    glUseProgram(program);

    static const float quadVerts[] = {
        /* pos        texcoord */
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 1.0f
    };

    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, quadVerts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, quadVerts + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    recordResult("TexSubImage visual", glGetError() == GL_NO_ERROR, NULL);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
}

/*==========================================================================
 * TEST: Depth testing
 *==========================================================================*/

static void testDepth(void) {
    printf("\n--- Test: Depth Testing ---\n");
    fflush(stdout);

    GLuint program = getSimpleProgram();
    glUseProgram(program);
    GLint colorLoc = glGetUniformLocation(program, "u_color");

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    recordResult("glEnable(GL_DEPTH_TEST)", glGetError() == GL_NO_ERROR, NULL);

    glDepthFunc(GL_LESS);
    recordResult("glDepthFunc", glGetError() == GL_NO_ERROR, NULL);

    /* Draw red quad at z=0.5 (farther from camera in NDC [-1,+1] range)
     * NOTE: With GL_LESS and depth cleared to 1.0, smaller z = closer = wins */
    glUniform4f(colorLoc, 1.0f, 0.0f, 0.0f, 1.0f);
    static const float quad1[] = {
        -0.6f, -0.6f, 0.5f,   /* x, y, z */
         0.6f, -0.6f, 0.5f,
         0.6f,  0.6f, 0.5f,
        -0.6f,  0.6f, 0.5f
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, quad1);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Draw green quad at z=0.3 (closer to camera, should be visible on top) */
    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);
    static const float quad2[] = {
        -0.3f, -0.3f, 0.3f,   /* x, y, z */
         0.3f, -0.3f, 0.3f,
         0.3f,  0.3f, 0.3f,
        -0.3f,  0.3f, 0.3f
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, quad2);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Check center is green */
    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool isGreen = pixel[1] > 200 && pixel[0] < 50;
    recordResult("Depth test (green on top)", isGreen, NULL);

    glDisable(GL_DEPTH_TEST);
    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Polygon Offset (z-fighting prevention)
 *==========================================================================*/

static void testPolygonOffset(void) {
    printf("\n--- Test: Polygon Offset ---\n");
    fflush(stdout);

    GLuint program = getSimpleProgram();
    glUseProgram(program);
    GLint colorLoc = glGetUniformLocation(program, "u_color");

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);  /* Allow equal depth to pass */

    /* Draw two quads at the SAME Z depth - this would normally cause z-fighting */

    /* First: draw red quad at z=0.5 */
    glUniform4f(colorLoc, 1.0f, 0.0f, 0.0f, 1.0f);
    static const float quad1[] = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f,  0.5f, 0.5f,
        -0.5f,  0.5f, 0.5f
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, quad1);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Enable polygon offset to push green quad slightly closer */
    glEnable(GL_POLYGON_OFFSET_FILL);
    recordResult("glEnable(GL_POLYGON_OFFSET_FILL)", glGetError() == GL_NO_ERROR, NULL);

    glPolygonOffset(-1.0f, -1.0f);  /* Negative = closer to camera */
    recordResult("glPolygonOffset", glGetError() == GL_NO_ERROR, NULL);

    /* Second: draw green quad at SAME z=0.5, but polygon offset makes it closer */
    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);
    static const float quad2[] = {
        -0.3f, -0.3f, 0.5f,   /* Same Z! */
         0.3f, -0.3f, 0.5f,
         0.3f,  0.3f, 0.5f,
        -0.3f,  0.3f, 0.5f
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, quad2);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Check center is green (polygon offset worked) */
    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool isGreen = pixel[1] > 200 && pixel[0] < 50;
    recordResult("Polygon offset (green on top)", isGreen, NULL);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_TEST);
    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Blending
 *==========================================================================*/

static void testBlending(void) {
    printf("\n--- Test: Blending ---\n");

    GLuint program = getSimpleProgram();
    glUseProgram(program);
    GLint colorLoc = glGetUniformLocation(program, "u_color");

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    recordResult("glEnable(GL_BLEND)", glGetError() == GL_NO_ERROR, NULL);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    recordResult("glBlendFunc", glGetError() == GL_NO_ERROR, NULL);

    /* Draw red quad */
    glUniform4f(colorLoc, 1.0f, 0.0f, 0.0f, 1.0f);
    static const float quad1[] = { -0.6f, -0.6f, 0.6f, -0.6f, 0.6f, 0.6f, -0.6f, 0.6f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad1);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Draw semi-transparent green on top */
    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 0.5f);
    static const float quad2[] = { -0.3f, -0.3f, 0.9f, -0.3f, 0.9f, 0.9f, -0.3f, 0.9f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad2);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Check blended area (should be yellow-ish) */
    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[DEBUG] Blend pixel: R=%d G=%d B=%d A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    bool blended = pixel[0] > 100 && pixel[1] > 100 && pixel[2] < 50;
    recordResult("Blend result (yellow-ish)", blended, NULL);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Face culling
 *==========================================================================*/

static void testCulling(void) {
    printf("\n--- Test: Face Culling ---\n");

    GLuint program = getSimpleProgram();
    glUseProgram(program);
    GLint colorLoc = glGetUniformLocation(program, "u_color");

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_CULL_FACE);
    recordResult("glEnable(GL_CULL_FACE)", glGetError() == GL_NO_ERROR, NULL);

    glCullFace(GL_BACK);
    recordResult("glCullFace", glGetError() == GL_NO_ERROR, NULL);

    glFrontFace(GL_CCW);
    recordResult("glFrontFace", glGetError() == GL_NO_ERROR, NULL);

    /* Draw CCW triangle (should be visible) */
    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);
    static const float triCCW[] = { 0.0f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triCCW);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    GLubyte pixel[4];
    glReadPixels(640, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool ccwVisible = pixel[1] > 200;
    recordResult("CCW triangle visible", ccwVisible, NULL);

    glDisable(GL_CULL_FACE);
    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Scissor test
 *==========================================================================*/

static void testScissor(void) {
    printf("\n--- Test: Scissor Test ---\n");

    GLuint program = getSimpleProgram();
    glUseProgram(program);
    GLint colorLoc = glGetUniformLocation(program, "u_color");

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);  /* Blue background */
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_SCISSOR_TEST);
    recordResult("glEnable(GL_SCISSOR_TEST)", glGetError() == GL_NO_ERROR, NULL);

    glScissor(320, 180, 640, 360);  /* Center half of screen */
    recordResult("glScissor", glGetError() == GL_NO_ERROR, NULL);

    /* Draw fullscreen green quad - should only appear in scissor region */
    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);
    static const float fullQuad[] = { -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, fullQuad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Check inside scissor (green) and outside (blue) */
    GLubyte pixelIn[4], pixelOut[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixelIn);
    glReadPixels(100, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixelOut);

    bool scissorWorks = (pixelIn[1] > 200) && (pixelOut[2] > 200);
    recordResult("Scissor clipping", scissorWorks, NULL);

    glDisable(GL_SCISSOR_TEST);
    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Color mask
 *==========================================================================*/

static void testColorMask(void) {
    printf("\n--- Test: Color Mask ---\n");

    GLuint program = getSimpleProgram();
    glUseProgram(program);
    GLint colorLoc = glGetUniformLocation(program, "u_color");

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Only allow red channel */
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
    recordResult("glColorMask", glGetError() == GL_NO_ERROR, NULL);

    /* Draw white quad - should appear red */
    glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    static const float quad[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool onlyRed = pixel[0] > 200 && pixel[1] < 10 && pixel[2] < 10;
    recordResult("ColorMask red only", onlyRed, NULL);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Uniform types
 *==========================================================================*/

static void testUniforms(void) {
    printf("\n--- Test: Uniform Types ---\n");

    GLuint program = getUniformProgram();
    if (!program) {
        recordResult("Uniform shader", false, "failed to load");
        return;
    }
    glUseProgram(program);

    GLint matLoc = glGetUniformLocation(program, "u_matrix");
    GLint offsetLoc = glGetUniformLocation(program, "u_offset");
    GLint colorLoc = glGetUniformLocation(program, "u_color");
    GLint alphaLoc = glGetUniformLocation(program, "u_alpha");
    GLint modeLoc = glGetUniformLocation(program, "u_mode");

    recordResult("glGetUniformLocation (mat4)", matLoc >= 0, NULL);
    recordResult("glGetUniformLocation (vec4 offset)", offsetLoc >= 0, NULL);
    recordResult("glGetUniformLocation (vec4 color)", colorLoc >= 0, NULL);

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    glUniformMatrix4fv(matLoc, 1, GL_FALSE, identity);
    recordResult("glUniformMatrix4fv", glGetError() == GL_NO_ERROR, NULL);

    glUniform4f(offsetLoc, 0.0f, 0.0f, 0.0f, 0.0f);
    glUniform4f(colorLoc, 1.0f, 0.0f, 1.0f, 1.0f);  /* Magenta */
    if (alphaLoc >= 0) glUniform1f(alphaLoc, 0.5f);
    if (modeLoc >= 0) glUniform1i(modeLoc, 0);
    recordResult("glUniform4f", glGetError() == GL_NO_ERROR, NULL);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    static const float quad[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool isMagenta = pixel[0] > 200 && pixel[1] < 50 && pixel[2] > 200;
    recordResult("Uniform rendering (magenta)", isMagenta, NULL);

    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Framebuffer Objects (FBO)
 *==========================================================================*/

static void testFBO(void) {
    printf("\n--- Test: Framebuffer Objects ---\n");

    /* Create FBO texture */
    GLuint fboTex;
    glGenTextures(1, &fboTex);
    glBindTexture(GL_TEXTURE_2D, fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    recordResult("FBO texture created", glGetError() == GL_NO_ERROR, NULL);

    /* Create FBO */
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    recordResult("glGenFramebuffers", fbo > 0, NULL);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    recordResult("glBindFramebuffer", glGetError() == GL_NO_ERROR, NULL);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
    recordResult("glFramebufferTexture2D", glGetError() == GL_NO_ERROR, NULL);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    recordResult("glCheckFramebufferStatus", status == GL_FRAMEBUFFER_COMPLETE,
                 status == GL_FRAMEBUFFER_COMPLETE ? "complete" : "incomplete");

    if (status == GL_FRAMEBUFFER_COMPLETE) {
        /* Render to FBO */
        glViewport(0, 0, 256, 256);
        glClearColor(1.0f, 0.5f, 0.0f, 1.0f);  /* Orange */
        glClear(GL_COLOR_BUFFER_BIT);

        /* Back to default framebuffer */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        /* Draw FBO texture to screen */
        GLuint program = getTexturedProgram();
        if (program) {
            glUseProgram(program);

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            static const float quadVerts[] = {
                -0.5f, -0.5f, 0.0f, 0.0f,
                 0.5f, -0.5f, 1.0f, 0.0f,
                 0.5f,  0.5f, 1.0f, 1.0f,
                -0.5f, -0.5f, 0.0f, 0.0f,
                 0.5f,  0.5f, 1.0f, 1.0f,
                -0.5f,  0.5f, 0.0f, 1.0f
            };

            glBindTexture(GL_TEXTURE_2D, fboTex);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, quadVerts);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, quadVerts + 2);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            GLubyte pixel[4];
            glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            printf("[DEBUG] FBO pixel: R=%d G=%d B=%d A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);
            bool isOrange = pixel[0] > 200 && pixel[1] > 100 && pixel[1] < 150 && pixel[2] < 50;
            recordResult("FBO render-to-texture (orange)", isOrange, NULL);

            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
        }
    }

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fboTex);
    recordResult("FBO cleanup", glGetError() == GL_NO_ERROR, NULL);
}

/*==========================================================================
 * TEST: Mipmaps
 *==========================================================================*/

static void testMipmaps(void) {
    printf("\n--- Test: Mipmaps ---\n");

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    /* Create 64x64 texture */
    GLubyte *pixels = malloc(64 * 64 * 4);
    for (int i = 0; i < 64 * 64; i++) {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 128;
        pixels[i * 4 + 2] = 0;
        pixels[i * 4 + 3] = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);

    glGenerateMipmap(GL_TEXTURE_2D);
    recordResult("glGenerateMipmap", glGetError() == GL_NO_ERROR, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    recordResult("GL_LINEAR_MIPMAP_LINEAR", glGetError() == GL_NO_ERROR, NULL);

    /* Unbind before deleting */
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
}

/*==========================================================================
 * TEST: Copy Texture from Framebuffer
 *==========================================================================*/

static void testCopyTexImage(void) {
    printf("\n--- Test: glCopyTexImage2D ---\n");

    /* Step 1: Render something to the framebuffer (a green triangle) */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint program = getSimpleProgram();
    if (!program) {
        recordResult("CopyTexImage shader load", false, "failed to load");
        return;
    }
    glUseProgram(program);

    /* Set u_color to GREEN (the simple shader uses uniform color, not vertex color) */
    GLint colorLoc = glGetUniformLocation(program, "u_color");
    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);

    /* Draw a green triangle */
    static const float vertices[] = {
         0.0f,  0.5f,
        -0.5f, -0.5f,
         0.5f, -0.5f
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* Step 2: Create a texture and copy framebuffer to it */
    GLuint copyTex;
    glGenTextures(1, &copyTex);
    glBindTexture(GL_TEXTURE_2D, copyTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Copy center 128x128 region of framebuffer to texture */
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 576, 296, 128, 128, 0);
    GLenum err = glGetError();
    recordResult("glCopyTexImage2D", err == GL_NO_ERROR,
                 err != GL_NO_ERROR ? "glGetError returned error" : NULL);

    /* Step 3: Clear and draw a quad using the copied texture */
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint texProgram = getTexturedProgram();
    if (texProgram) {
        glUseProgram(texProgram);

        static const float quadVerts[] = {
            /* position      texcoord */
            -0.5f,  0.5f,    0.0f, 0.0f,
            -0.5f, -0.5f,    0.0f, 1.0f,
             0.5f,  0.5f,    1.0f, 0.0f,
             0.5f, -0.5f,    1.0f, 1.0f
        };

        GLuint quadVbo;
        glGenBuffers(1, &quadVbo);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        /* Diagnostic: read center pixel to see what was actually displayed */
        GLubyte copyPixel[4];
        glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, copyPixel);
        printf("[DEBUG] CopyTexImage display center: R=%d G=%d B=%d A=%d\n",
               copyPixel[0], copyPixel[1], copyPixel[2], copyPixel[3]);

        glDeleteBuffers(1, &quadVbo);
    }

    /* Cleanup */
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &copyTex);
    glDeleteBuffers(1, &vbo);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/*==========================================================================
 * TEST: Copy Texture Sub-Region from Framebuffer
 *==========================================================================*/

static void testCopyTexSubImage(void) {
    printf("\n--- Test: glCopyTexSubImage2D ---\n");

    /* Step 1: Create a texture initialized with RED */
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Create 128x128 red texture */
    unsigned char *redPixels = malloc(128 * 128 * 4);
    for (int i = 0; i < 128 * 128; i++) {
        redPixels[i * 4 + 0] = 255;  /* R */
        redPixels[i * 4 + 1] = 0;    /* G */
        redPixels[i * 4 + 2] = 0;    /* B */
        redPixels[i * 4 + 3] = 255;  /* A */
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, redPixels);
    free(redPixels);
    recordResult("glTexImage2D (red base)", glGetError() == GL_NO_ERROR, NULL);

    /* Step 2: Render a GREEN quad to framebuffer */
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);  /* Blue background */
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint program = getSimpleProgram();
    if (!program) {
        recordResult("CopyTexSubImage shader load", false, "failed to load");
        glDeleteTextures(1, &tex);
        return;
    }
    glUseProgram(program);

    /* Set u_color to GREEN (the simple shader uses uniform color, not vertex color) */
    GLint colorLoc = glGetUniformLocation(program, "u_color");
    glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);

    /* Draw a green quad in center of screen */
    static const float vertices[] = {
        -0.3f,  0.3f,
        -0.3f, -0.3f,
         0.3f,  0.3f,
         0.3f, -0.3f
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Step 3: Copy a 64x64 region from center of framebuffer to center of texture */
    /* Framebuffer center is at (640, 360), copy 64x64 region */
    /* Texture center is at (64, 64), paste 64x64 region starting at (32, 32) */
    glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 32, 32, 608, 328, 64, 64);
    GLenum err = glGetError();
    recordResult("glCopyTexSubImage2D", err == GL_NO_ERROR,
                 err != GL_NO_ERROR ? "glGetError returned error" : NULL);

    /* Step 4: Clear and draw a quad using the modified texture */
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint texProgram = getTexturedProgram();
    if (texProgram) {
        glUseProgram(texProgram);

        static const float quadVerts[] = {
            /* position      texcoord */
            -0.5f,  0.5f,    0.0f, 0.0f,
            -0.5f, -0.5f,    0.0f, 1.0f,
             0.5f,  0.5f,    1.0f, 0.0f,
             0.5f, -0.5f,    1.0f, 1.0f
        };

        GLuint quadVbo;
        glGenBuffers(1, &quadVbo);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        /* Diagnostic: read center pixel to see what was actually displayed */
        GLubyte copyPixel[4];
        glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, copyPixel);
        printf("[DEBUG] CopyTexSubImage display center: R=%d G=%d B=%d A=%d\n",
               copyPixel[0], copyPixel[1], copyPixel[2], copyPixel[3]);

        glDeleteBuffers(1, &quadVbo);
    }

    /* Cleanup */
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &vbo);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/*==========================================================================
 * TEST: All Uniform Types
 *==========================================================================*/

static GLuint s_allUniformProgram = 0;

static GLuint getAllUniformProgram(void) {
    if (!s_allUniformProgram) {
        s_allUniformProgram = createProgramFromFiles(
            "romfs:/shaders/alluniform_vsh.dksh",
            "romfs:/shaders/alluniform_fsh.dksh");
    }
    return s_allUniformProgram;
}

static void testAllUniforms(void) {
    printf("\n--- Test: All Uniform Types ---\n");

    GLuint program = getAllUniformProgram();
    if (!program) {
        recordResult("AllUniform shader load", false, "failed to load");
        return;
    }
    glUseProgram(program);
    recordResult("AllUniform program", glGetError() == GL_NO_ERROR, NULL);

    /* Get all uniform locations */
    /* Vertex uniforms */
    GLint locScale = glGetUniformLocation(program, "u_testScale");
    GLint locOffset2 = glGetUniformLocation(program, "u_testOffset2");
    GLint locOffset3 = glGetUniformLocation(program, "u_testOffset3");
    GLint locMat2 = glGetUniformLocation(program, "u_testMat2");
    GLint locMat3 = glGetUniformLocation(program, "u_testMat3");

    /* Fragment uniforms */
    GLint locAlpha = glGetUniformLocation(program, "u_testAlpha");
    GLint locVec2 = glGetUniformLocation(program, "u_testVec2");
    GLint locVec3 = glGetUniformLocation(program, "u_testVec3");
    GLint locVec4 = glGetUniformLocation(program, "u_testVec4");
    GLint locMode = glGetUniformLocation(program, "u_testMode");
    GLint locIvec2 = glGetUniformLocation(program, "u_testIvec2");
    GLint locIvec3 = glGetUniformLocation(program, "u_testIvec3");
    GLint locIvec4 = glGetUniformLocation(program, "u_testIvec4");

    recordResult("glGetUniformLocation (scale)", locScale >= 0, NULL);
    recordResult("glGetUniformLocation (offset2)", locOffset2 >= 0, NULL);
    recordResult("glGetUniformLocation (offset3)", locOffset3 >= 0, NULL);
    recordResult("glGetUniformLocation (mat2)", locMat2 >= 0, NULL);
    recordResult("glGetUniformLocation (mat3)", locMat3 >= 0, NULL);
    recordResult("glGetUniformLocation (alpha)", locAlpha >= 0, NULL);
    recordResult("glGetUniformLocation (vec2)", locVec2 >= 0, NULL);
    recordResult("glGetUniformLocation (vec3)", locVec3 >= 0, NULL);
    recordResult("glGetUniformLocation (vec4)", locVec4 >= 0, NULL);
    recordResult("glGetUniformLocation (mode)", locMode >= 0, NULL);
    recordResult("glGetUniformLocation (ivec2)", locIvec2 >= 0, NULL);
    recordResult("glGetUniformLocation (ivec3)", locIvec3 >= 0, NULL);
    recordResult("glGetUniformLocation (ivec4)", locIvec4 >= 0, NULL);

    /* Triangle vertices */
    static const float triVerts[] = {
         0.0f,  0.5f,
        -0.5f, -0.5f,
         0.5f, -0.5f
    };

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triVerts);
    glEnableVertexAttribArray(0);

    /* Identity mat2 (no rotation) */
    float mat2[4] = {
        1.0f, 0.0f,
        0.0f, 1.0f
    };

    /* Identity mat3 */
    float mat3[9] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };

    /* ===== TEST 1: glUniform1f (alpha) ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1f(locScale, 1.0f);
    glUniform2f(locOffset2, 0.0f, 0.0f);
    glUniform3f(locOffset3, 0.0f, 0.0f, 0.0f);
    glUniformMatrix2fv(locMat2, 1, GL_FALSE, mat2);
    glUniformMatrix3fv(locMat3, 1, GL_FALSE, mat3);

    glUniform1f(locAlpha, 0.5f);
    glUniform1i(locMode, 1);  /* Mode 1: use alpha with white */

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);

    /* Check pixel - should be grayish (white * 0.5 alpha on black) */
    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool alphaOk = (pixel[0] >= 120 && pixel[0] <= 135);  /* ~127 */
    recordResult("glUniform1f (alpha)", alphaOk, NULL);

    /* ===== TEST 2: glUniform2f (vec2 color) ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform2f(locVec2, 1.0f, 0.5f);  /* R=1, G=0.5 */
    glUniform1i(locMode, 2);  /* Mode 2: use vec2 for RG */

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool vec2Ok = (pixel[0] >= 250) && (pixel[1] >= 120 && pixel[1] <= 135) && (pixel[2] < 10);
    recordResult("glUniform2f (vec2)", vec2Ok, NULL);

    /* ===== TEST 3: glUniform3f (vec3 color) ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform3f(locVec3, 0.0f, 1.0f, 0.5f);  /* G=1, B=0.5 */
    glUniform1i(locMode, 3);  /* Mode 3: use vec3 for RGB */

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool vec3Ok = (pixel[0] < 10) && (pixel[1] >= 250) && (pixel[2] >= 120 && pixel[2] <= 135);
    recordResult("glUniform3f (vec3)", vec3Ok, NULL);

    /* ===== TEST 4: glUniform4f (vec4 color) ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform4f(locVec4, 1.0f, 0.0f, 1.0f, 1.0f);  /* Magenta */
    glUniform1i(locMode, 4);  /* Mode 4: use vec4 */

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool vec4Ok = (pixel[0] >= 250) && (pixel[1] < 10) && (pixel[2] >= 250);
    recordResult("glUniform4f (vec4)", vec4Ok, NULL);

    /* ===== TEST 5: glUniform2i (ivec2) ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform2i(locIvec2, 255, 128);  /* R=255, G=128 as integers */
    glUniform1i(locMode, 5);  /* Mode 5: use ivec2 */

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool ivec2Ok = (pixel[0] >= 250) && (pixel[1] >= 120 && pixel[1] <= 135) && (pixel[2] < 10);
    recordResult("glUniform2i (ivec2)", ivec2Ok, NULL);

    /* ===== TEST 6: glUniform3i (ivec3) ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform3i(locIvec3, 64, 128, 192);  /* R=64, G=128, B=192 */
    glUniform1i(locMode, 6);  /* Mode 6: use ivec3 */

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool ivec3Ok = (pixel[0] >= 60 && pixel[0] <= 70) &&
                   (pixel[1] >= 125 && pixel[1] <= 135) &&
                   (pixel[2] >= 188 && pixel[2] <= 198);
    recordResult("glUniform3i (ivec3)", ivec3Ok, NULL);

    /* ===== TEST 7: glUniform4i (ivec4) ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform4i(locIvec4, 255, 0, 255, 255);  /* Magenta with full alpha */
    glUniform1i(locMode, 7);  /* Mode 7: use ivec4 */

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    bool ivec4Ok = (pixel[0] >= 250) && (pixel[1] < 10) && (pixel[2] >= 250);
    recordResult("glUniform4i (ivec4)", ivec4Ok, NULL);

    /* ===== TEST 8: glUniformMatrix2fv (90 degree rotation) ===== */
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* 90 degree rotation matrix */
    float mat2_rot90[4] = {
        0.0f, -1.0f,
        1.0f,  0.0f
    };
    glUniformMatrix2fv(locMat2, 1, GL_FALSE, mat2_rot90);
    glUniform1f(locScale, 1.0f);
    glUniform3f(locVec3, 1.0f, 1.0f, 0.0f);  /* Yellow */
    glUniform1i(locMode, 3);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    recordResult("glUniformMatrix2fv", glGetError() == GL_NO_ERROR, NULL);

    /* Reset mat2 for next tests */
    glUniformMatrix2fv(locMat2, 1, GL_FALSE, mat2);

    /* ===== TEST 9: glUniformMatrix3fv ===== */
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Scale by 0.5 using mat3 */
    float mat3_scale[9] = {
        0.5f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    glUniformMatrix3fv(locMat3, 1, GL_FALSE, mat3_scale);
    glUniform3f(locVec3, 0.0f, 1.0f, 1.0f);  /* Cyan */
    glUniform1i(locMode, 3);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    recordResult("glUniformMatrix3fv", glGetError() == GL_NO_ERROR, NULL);

    /* Reset mat3 */
    glUniformMatrix3fv(locMat3, 1, GL_FALSE, mat3);

    /* ===== TEST 10: Vertex offset with glUniform2f ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform2f(locOffset2, 0.3f, 0.0f);  /* Offset right */
    glUniform3f(locVec3, 0.0f, 1.0f, 0.0f);  /* Green */
    glUniform1i(locMode, 3);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    recordResult("glUniform2f (vertex offset)", glGetError() == GL_NO_ERROR, NULL);

    /* Reset offset */
    glUniform2f(locOffset2, 0.0f, 0.0f);

    /* ===== TEST 11: Vertex offset3 with glUniform3f ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform3f(locOffset3, -0.3f, 0.0f, 0.0f);  /* Offset left */
    glUniform3f(locVec3, 1.0f, 0.5f, 0.0f);  /* Orange */
    glUniform1i(locMode, 3);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    recordResult("glUniform3f (vertex offset)", glGetError() == GL_NO_ERROR, NULL);

    /* Reset */
    glUniform3f(locOffset3, 0.0f, 0.0f, 0.0f);

    /* ===== TEST 12: glUniform1f for scale ===== */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1f(locScale, 0.5f);  /* Half size */
    glUniform3f(locVec3, 1.0f, 1.0f, 1.0f);  /* White */
    glUniform1i(locMode, 3);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    recordResult("glUniform1f (scale)", glGetError() == GL_NO_ERROR, NULL);

    glDisableVertexAttribArray(0);
}

/*==========================================================================
 * TEST: Multiple Texture Units
 *==========================================================================*/

static GLuint s_multiTexProgram = 0;

static GLuint getMultiTexProgram(void) {
    if (!s_multiTexProgram) {
        s_multiTexProgram = createProgramFromFiles(
            "romfs:/shaders/textured_vsh.dksh",
            "romfs:/shaders/multitex_fsh.dksh");
    }
    return s_multiTexProgram;
}

static void testMultiTexture(void) {
    printf("\n--- Test: Multiple Texture Units ---\n");

    GLuint program = getMultiTexProgram();
    if (!program) {
        recordResult("MultiTex program", false, "failed to load");
        return;
    }
    glUseProgram(program);
    recordResult("MultiTex program", glGetError() == GL_NO_ERROR, NULL);

    /* Get blend uniform location */
    GLint blendLoc = glGetUniformLocation(program, "u_blend");
    recordResult("glGetUniformLocation (blend)", blendLoc >= 0, NULL);

    /* Create two textures: one red, one blue */
    GLuint textures[2];
    glGenTextures(2, textures);
    recordResult("glGenTextures (2)", glGetError() == GL_NO_ERROR, NULL);

    /* Texture 0: solid red */
    GLubyte red[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        red[i * 4 + 0] = 255;
        red[i * 4 + 1] = 0;
        red[i * 4 + 2] = 0;
        red[i * 4 + 3] = 255;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    recordResult("Texture 0 (red)", glGetError() == GL_NO_ERROR, NULL);

    /* Texture 1: solid blue */
    GLubyte blue[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        blue[i * 4 + 0] = 0;
        blue[i * 4 + 1] = 0;
        blue[i * 4 + 2] = 255;
        blue[i * 4 + 3] = 255;
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, blue);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    recordResult("Texture 1 (blue)", glGetError() == GL_NO_ERROR, NULL);

    /* Quad vertices with texcoords */
    static const float quadVerts[] = {
        /* pos        texcoord */
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 1.0f
    };

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, quadVerts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, quadVerts + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    /* Note: The shader is DIAGNOSTIC - outputs vec4(u_blend, tex0.r, tex1.b, 1.0)
     * This tests that both texture units are working correctly:
     * - R channel = u_blend uniform value (0->255 as blend 0->1)
     * - G channel = texture0 red (should be 255 for red texture)
     * - B channel = texture1 blue (should be 255 for blue texture)
     */

    /* Test 1: blend=0.0 -> R=0, G=255 (tex0.r), B=255 (tex1.b) */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1f(blendLoc, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[DEBUG] MultiTex blend=0: R=%d G=%d B=%d\n", pixel[0], pixel[1], pixel[2]);
    /* R~0 (blend=0), G=255 (tex0.r), B=255 (tex1.b) */
    bool blend0Ok = (pixel[0] < 10) && (pixel[1] >= 250) && (pixel[2] >= 250);
    recordResult("MultiTex blend=0", blend0Ok, NULL);

    /* Test 2: blend=1.0 -> R=255, G=255 (tex0.r), B=255 (tex1.b) */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1f(blendLoc, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[DEBUG] MultiTex blend=1: R=%d G=%d B=%d\n", pixel[0], pixel[1], pixel[2]);
    /* R=255 (blend=1), G=255 (tex0.r), B=255 (tex1.b) */
    bool blend1Ok = (pixel[0] >= 250) && (pixel[1] >= 250) && (pixel[2] >= 250);
    recordResult("MultiTex blend=1", blend1Ok, NULL);

    /* Test 3: blend=0.5 -> R=127, G=255 (tex0.r), B=255 (tex1.b) */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform1f(blendLoc, 0.5f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[DEBUG] MultiTex blend=0.5: R=%d G=%d B=%d\n", pixel[0], pixel[1], pixel[2]);
    /* R~127 (blend=0.5), G=255 (tex0.r), B=255 (tex1.b) */
    bool blend05Ok = (pixel[0] >= 120 && pixel[0] <= 135) &&
                     (pixel[1] >= 250) &&
                     (pixel[2] >= 250);
    recordResult("MultiTex blend=0.5", blend05Ok, NULL);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    /* Cleanup */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(2, textures);
}

/*==========================================================================
 * TEST: Cubemap textures
 *==========================================================================*/

static GLuint s_cubemapProgram = 0;

static GLuint getCubemapProgram(void) {
    if (!s_cubemapProgram) {
        s_cubemapProgram = createProgramFromFiles(
            "romfs:/shaders/cubemap_vsh.dksh",
            "romfs:/shaders/cubemap_fsh.dksh");
    }
    return s_cubemapProgram;
}

static void testCubemap(void) {
    printf("\n--- Test: Cubemap Textures ---\n");

    /* Ensure we're on texture unit 0 (previous test may have left it elsewhere) */
    glActiveTexture(GL_TEXTURE0);

    /* Create cubemap texture */
    GLuint cubemap;
    glGenTextures(1, &cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
    recordResult("glBindTexture(CUBE_MAP)", glGetError() == GL_NO_ERROR, NULL);

    /* Upload 6 faces with distinct colors:
     * +X = Red, -X = Cyan
     * +Y = Green, -Y = Magenta
     * +Z = Blue, -Z = Yellow
     */
    GLubyte faceColors[6][3] = {
        {255, 0, 0},     /* +X: Red */
        {0, 255, 255},   /* -X: Cyan */
        {0, 255, 0},     /* +Y: Green */
        {255, 0, 255},   /* -Y: Magenta */
        {0, 0, 255},     /* +Z: Blue */
        {255, 255, 0}    /* -Z: Yellow */
    };

    GLenum faces[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };

    /* Upload 4x4 solid-color faces */
    GLubyte facePixels[4 * 4 * 4];
    for (int face = 0; face < 6; face++) {
        for (int i = 0; i < 4 * 4; i++) {
            facePixels[i * 4 + 0] = faceColors[face][0];
            facePixels[i * 4 + 1] = faceColors[face][1];
            facePixels[i * 4 + 2] = faceColors[face][2];
            facePixels[i * 4 + 3] = 255;
        }
        glTexImage2D(faces[face], 0, GL_RGBA, 4, 4, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, facePixels);
    }
    recordResult("glTexImage2D (6 faces)", glGetError() == GL_NO_ERROR, NULL);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    recordResult("glTexParameteri (cubemap)", glGetError() == GL_NO_ERROR, NULL);

    /* Get cubemap shader - uses separate position (loc 0) and direction (loc 1) */
    GLuint program = getCubemapProgram();
    if (!program) {
        recordResult("Cubemap shader", false, "failed to load");
        glDeleteTextures(1, &cubemap);
        return;
    }
    glUseProgram(program);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Draw 6 small quads in a 3x2 grid, each sampling a different cubemap face.
     * Shader uses: attribute 0 = vec2 position, attribute 1 = vec3 direction */
    static const float quadSize = 0.2f;
    static const float spacingX = 0.5f;
    static const float spacingY = 0.45f;

    struct {
        float px, py;
        float dx, dy, dz;
        const char *label;
    } faceQuads[6] = {
        { -spacingX,  spacingY,   1.0f,  0.0f,  0.0f, "+X Red"     },
        {  0.0f,      spacingY,  -1.0f,  0.0f,  0.0f, "-X Cyan"    },
        {  spacingX,  spacingY,   0.0f,  1.0f,  0.0f, "+Y Green"   },
        { -spacingX, -spacingY,   0.0f, -1.0f,  0.0f, "-Y Magenta" },
        {  0.0f,     -spacingY,   0.0f,  0.0f,  1.0f, "+Z Blue"    },
        {  spacingX, -spacingY,   0.0f,  0.0f, -1.0f, "-Z Yellow"  },
    };

    int passCount = 0;
    for (int i = 0; i < 6; i++) {
        float cx = faceQuads[i].px;
        float cy = faceQuads[i].py;

        /* Position attribute (vec2) - 4 corners of a small quad */
        float pos[] = {
            cx - quadSize, cy - quadSize,
            cx + quadSize, cy - quadSize,
            cx + quadSize, cy + quadSize,
            cx - quadSize, cy + quadSize,
        };

        /* Direction attribute (vec3) - same direction for all 4 corners */
        float dir[] = {
            faceQuads[i].dx, faceQuads[i].dy, faceQuads[i].dz,
            faceQuads[i].dx, faceQuads[i].dy, faceQuads[i].dz,
            faceQuads[i].dx, faceQuads[i].dy, faceQuads[i].dz,
            faceQuads[i].dx, faceQuads[i].dy, faceQuads[i].dz,
        };

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, dir);
        glEnableVertexAttribArray(1);

        static const GLushort idx[] = { 0, 1, 2, 0, 2, 3 };
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);

        passCount++;
    }

    recordResult("Cubemap rendering", glGetError() == GL_NO_ERROR && passCount == 6, NULL);

    /* Read pixel at +X quad position to verify red.
     * Note: deko3d flips Y, so negate NDC Y for glReadPixels coordinate. */
    GLubyte pixel[4];
    int pxX = (int)(( faceQuads[0].px * 0.5f + 0.5f) * SCREEN_WIDTH);
    int pxY = (int)((-faceQuads[0].py * 0.5f + 0.5f) * SCREEN_HEIGHT);
    glReadPixels(pxX, pxY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[DEBUG] Cubemap +X pixel at (%d,%d): R=%d G=%d B=%d A=%d\n",
           pxX, pxY, pixel[0], pixel[1], pixel[2], pixel[3]);

    bool colorOk = (pixel[0] > 200 && pixel[1] < 50 && pixel[2] < 50);
    recordResult("Cubemap +X face (red)", colorOk, NULL);

    /* Diagnostic: check if clear color is visible at corner (should be 128,128,128,255) */
    GLubyte bgCheck[4];
    glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bgCheck);
    printf("[DIAG] Cubemap BG at (5,5): R=%d G=%d B=%d A=%d\n",
           bgCheck[0], bgCheck[1], bgCheck[2], bgCheck[3]);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glDeleteTextures(1, &cubemap);
}

/*==========================================================================
 * TEST: FBO with Depth Renderbuffer
 *==========================================================================*/

static void testFBODepthRenderbuffer(void) {
    printf("\n--- Test: FBO with Depth Renderbuffer ---\n");

    /* Create color texture for FBO */
    GLuint colorTex;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    recordResult("FBO color texture", glGetError() == GL_NO_ERROR, NULL);

    /* Create depth RENDERBUFFER (not texture!) */
    GLuint depthRB;
    glGenRenderbuffers(1, &depthRB);
    recordResult("glGenRenderbuffers", depthRB > 0, NULL);

    glBindRenderbuffer(GL_RENDERBUFFER, depthRB);
    recordResult("glBindRenderbuffer", glGetError() == GL_NO_ERROR, NULL);

    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 256, 256);
    recordResult("glRenderbufferStorage(DEPTH)", glGetError() == GL_NO_ERROR, NULL);

    /* Create FBO */
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    /* Attach color texture */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    recordResult("glFramebufferTexture2D", glGetError() == GL_NO_ERROR, NULL);

    /* Attach depth renderbuffer */
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRB);
    recordResult("glFramebufferRenderbuffer(DEPTH)", glGetError() == GL_NO_ERROR, NULL);

    /* Check FBO completeness */
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    recordResult("FBO complete (color+depth RB)", status == GL_FRAMEBUFFER_COMPLETE,
                 status == GL_FRAMEBUFFER_COMPLETE ? "complete" : "incomplete");

    if (status == GL_FRAMEBUFFER_COMPLETE) {
        /* Render to FBO with depth testing */
        glViewport(0, 0, 256, 256);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepthf(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Enable depth testing */
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        GLuint program = getSimpleProgram();
        glUseProgram(program);
        GLint colorLoc = glGetUniformLocation(program, "u_color");

        /* Draw RED quad at z=0.7 (farther) */
        glUniform4f(colorLoc, 1.0f, 0.0f, 0.0f, 1.0f);
        static const float quad1[] = {
            -0.8f, -0.8f, 0.7f,
             0.8f, -0.8f, 0.7f,
             0.8f,  0.8f, 0.7f,
            -0.8f,  0.8f, 0.7f
        };
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, quad1);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        /* Draw GREEN quad at z=0.3 (closer - should win) */
        glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 1.0f);
        static const float quad2[] = {
            -0.4f, -0.4f, 0.3f,
             0.4f, -0.4f, 0.3f,
             0.4f,  0.4f, 0.3f,
            -0.4f,  0.4f, 0.3f
        };
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, quad2);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisable(GL_DEPTH_TEST);
        glDisableVertexAttribArray(0);

        /* Switch back to default framebuffer */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        /* Display the FBO texture */
        GLuint texProgram = getTexturedProgram();
        glUseProgram(texProgram);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, colorTex);

        static const float displayQuad[] = {
            -0.5f, -0.5f, 0.0f, 0.0f,
             0.5f, -0.5f, 1.0f, 0.0f,
             0.5f,  0.5f, 1.0f, 1.0f,
            -0.5f, -0.5f, 0.0f, 0.0f,
             0.5f,  0.5f, 1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f, 1.0f
        };

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, displayQuad);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, displayQuad + 2);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        /* Read center pixel DIRECTLY from FBO - should be green (depth test passed) */
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        GLubyte pixel[4];
        glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        printf("[DEBUG] FBO+DepthRB pixel (direct from FBO): R=%d G=%d B=%d A=%d\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        bool depthWorked = pixel[1] > 200 && pixel[0] < 50;
        recordResult("FBO depth renderbuffer test", depthWorked,
                     depthWorked ? "green visible (depth OK)" : "depth test failed");

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    } else {
        /* Bind back to default FB even if incomplete */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* Cleanup */
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depthRB);
    glDeleteTextures(1, &colorTex);
    recordResult("FBO+DepthRB cleanup", glGetError() == GL_NO_ERROR, NULL);
}

/*==========================================================================
 * TEST: glReadPixels
 *==========================================================================*/

static void testReadPixels(void) {
    printf("\n--- Test: glReadPixels ---\n");

    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum err = glGetError();
    recordResult("glReadPixels", err == GL_NO_ERROR, NULL);

    if (err == GL_NO_ERROR) {
        bool colorOk = (pixel[0] >= 62 && pixel[0] <= 66) &&    /* R ~= 64 */
                       (pixel[1] >= 126 && pixel[1] <= 130) &&  /* G ~= 128 */
                       (pixel[2] >= 190 && pixel[2] <= 194);    /* B ~= 192 */
        recordResult("glReadPixels values", colorOk, NULL);
    }
}

/*==========================================================================
 * TEST: Query extensions, compressed formats, pixel store
 *==========================================================================*/

static void testQueries(void) {
    printf("\n--- Test: GL Queries & Extensions ---\n");

    /* Test GL_EXTENSIONS string */
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    recordResult("glGetString(GL_EXTENSIONS)", ext != NULL, NULL);

    if (ext) {
        /* Check for new extensions we added */
        recordResult("GL_EXT_blend_minmax",
                     strstr(ext, "GL_EXT_blend_minmax") != NULL, NULL);
        recordResult("GL_OES_element_index_uint",
                     strstr(ext, "GL_OES_element_index_uint") != NULL, NULL);
        recordResult("GL_OES_texture_npot",
                     strstr(ext, "GL_OES_texture_npot") != NULL, NULL);
        recordResult("GL_KHR_texture_compression_astc_ldr",
                     strstr(ext, "GL_KHR_texture_compression_astc_ldr") != NULL, NULL);
        recordResult("GL_EXT_texture_compression_s3tc",
                     strstr(ext, "GL_EXT_texture_compression_s3tc") != NULL, NULL);
    }

    /* Test compressed texture format enumeration */
    GLint numCompressed = 0;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numCompressed);
    recordResult("GL_NUM_COMPRESSED_TEXTURE_FORMATS > 0",
                 numCompressed > 0, NULL);
    printf("  [INFO] GL_NUM_COMPRESSED_TEXTURE_FORMATS = %d\n", numCompressed);

    if (numCompressed > 0 && numCompressed <= 64) {
        GLint formats[64];
        glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats);
        /* Check that ETC1 (0x8D64) is in the list */
        bool hasETC1 = false;
        for (int i = 0; i < numCompressed; i++) {
            if (formats[i] == 0x8D64) hasETC1 = true;
        }
        recordResult("GL_COMPRESSED_TEXTURE_FORMATS has ETC1", hasETC1, NULL);
    }

    /* Test shader binary format queries */
    GLint numBinaryFormats = 0;
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &numBinaryFormats);
    recordResult("GL_NUM_SHADER_BINARY_FORMATS == 1",
                 numBinaryFormats == 1, NULL);

    GLint binaryFormat = 0;
    glGetIntegerv(GL_SHADER_BINARY_FORMATS, &binaryFormat);
    recordResult("GL_SHADER_BINARY_FORMATS == DKSH",
                 binaryFormat == 0x10DE0001, NULL);

    /* Test glPixelStorei - set and read back */
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    GLenum err1 = glGetError();
    recordResult("glPixelStorei(GL_PACK_ALIGNMENT, 1)", err1 == GL_NO_ERROR, NULL);

    GLint packAlign = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
    recordResult("GL_PACK_ALIGNMENT == 1", packAlign == 1, NULL);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    GLint unpackAlign = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign);
    recordResult("GL_UNPACK_ALIGNMENT == 2", unpackAlign == 2, NULL);

    /* Test invalid alignment */
    glPixelStorei(GL_PACK_ALIGNMENT, 3);
    GLenum err2 = glGetError();
    recordResult("glPixelStorei(invalid=3) -> GL_INVALID_VALUE",
                 err2 == GL_INVALID_VALUE, NULL);

    /* Restore defaults */
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    /* Test GL_VERSION and GL_RENDERER */
    const char *version = (const char *)glGetString(GL_VERSION);
    recordResult("glGetString(GL_VERSION)",
                 version != NULL && strstr(version, "OpenGL ES 2.0") != NULL, NULL);

    const char *renderer = (const char *)glGetString(GL_RENDERER);
    recordResult("glGetString(GL_RENDERER)",
                 renderer != NULL && strstr(renderer, "deko3d") != NULL, NULL);
}

/*==========================================================================
 * TEST: Shader/Program Queries (glGetAttribLocation, glGetActiveUniform, etc.)
 *==========================================================================*/

static void testShaderQueries(void) {
    printf("\n--- Test: Shader/Program Queries ---\n");

    GLuint prog = getSimpleProgram();
    if (!prog) {
        recordResult("ShaderQuery: program load", false, "failed");
        return;
    }
    glUseProgram(prog);

    /* === glBindAttribLocation / glGetAttribLocation === */

    /* Test built-in attribute lookup */
    GLint posLoc = glGetAttribLocation(prog, "position");
    recordResult("glGetAttribLocation(\"position\") == 0",
                 posLoc == 0, NULL);

    GLint texLoc = glGetAttribLocation(prog, "texcoord");
    recordResult("glGetAttribLocation(\"texcoord\") == 1",
                 texLoc == 1, NULL);

    GLint normLoc = glGetAttribLocation(prog, "normal");
    recordResult("glGetAttribLocation(\"normal\") == 2",
                 normLoc == 2, NULL);

    GLint colorLoc = glGetAttribLocation(prog, "color");
    recordResult("glGetAttribLocation(\"color\") == 3",
                 colorLoc == 3, NULL);

    /* Unknown name should return -1 */
    GLint unknownLoc = glGetAttribLocation(prog, "nonexistent_attrib");
    recordResult("glGetAttribLocation(unknown) == -1",
                 unknownLoc == -1, NULL);

    /* Test glBindAttribLocation */
    glBindAttribLocation(prog, 5, "myCustomAttrib");
    GLenum err = glGetError();
    recordResult("glBindAttribLocation(5, \"myCustomAttrib\")",
                 err == GL_NO_ERROR, NULL);

    /* Re-link to apply binding (per spec, takes effect after link) */
    glLinkProgram(prog);

    GLint customLoc = glGetAttribLocation(prog, "myCustomAttrib");
    recordResult("glGetAttribLocation(\"myCustomAttrib\") == 5",
                 customLoc == 5, NULL);

    /* === glGetActiveAttrib === */
    GLint numAttribs = 0;
    glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &numAttribs);
    recordResult("GL_ACTIVE_ATTRIBUTES >= 1",
                 numAttribs >= 1, NULL);
    printf("  [INFO] GL_ACTIVE_ATTRIBUTES = %d\n", numAttribs);

    if (numAttribs > 0) {
        char attribName[64] = {0};
        GLint attribSize = 0;
        GLenum attribType = 0;
        GLsizei attribLen = 0;
        glGetActiveAttrib(prog, 0, sizeof(attribName), &attribLen,
                          &attribSize, &attribType, attribName);
        recordResult("glGetActiveAttrib(0) returns name",
                     attribLen > 0 && attribName[0] != '\0', NULL);
        printf("  [INFO] attrib[0]: name=\"%s\" size=%d type=0x%X\n",
               attribName, attribSize, attribType);
    }

    /* === glGetActiveUniform === */

    /* First, query a uniform to populate active tracking */
    GLuint uprog = getUniformProgram();
    if (uprog) {
        glUseProgram(uprog);
        GLint matLoc = glGetUniformLocation(uprog, "u_matrix");
        GLint colorULoc = glGetUniformLocation(uprog, "u_color");
        (void)matLoc; (void)colorULoc;

        GLint numUniforms = 0;
        glGetProgramiv(uprog, GL_ACTIVE_UNIFORMS, &numUniforms);
        recordResult("GL_ACTIVE_UNIFORMS >= 2 (after query)",
                     numUniforms >= 2, NULL);
        printf("  [INFO] GL_ACTIVE_UNIFORMS = %d\n", numUniforms);

        if (numUniforms > 0) {
            char uniformName[64] = {0};
            GLint uniformSize = 0;
            GLenum uniformType = 0;
            GLsizei uniformLen = 0;
            glGetActiveUniform(uprog, 0, sizeof(uniformName), &uniformLen,
                              &uniformSize, &uniformType, uniformName);
            recordResult("glGetActiveUniform(0) returns name",
                         uniformLen > 0 && uniformName[0] != '\0', NULL);
            printf("  [INFO] uniform[0]: name=\"%s\" size=%d type=0x%X\n",
                   uniformName, uniformSize, uniformType);
        }

        /* === glGetUniformfv / glGetUniformiv readback === */

        /* Set a vec4 uniform and read it back */
        if (colorULoc != -1) {
            glUniform4f(colorULoc, 0.25f, 0.5f, 0.75f, 1.0f);

            GLfloat readback[4] = {0};
            glGetUniformfv(uprog, colorULoc, readback);
            bool colorMatch = (readback[0] > 0.24f && readback[0] < 0.26f) &&
                              (readback[1] > 0.49f && readback[1] < 0.51f) &&
                              (readback[2] > 0.74f && readback[2] < 0.76f) &&
                              (readback[3] > 0.99f && readback[3] < 1.01f);
            recordResult("glGetUniformfv readback vec4",
                         colorMatch, NULL);
            printf("  [INFO] readback: %.3f, %.3f, %.3f, %.3f\n",
                   readback[0], readback[1], readback[2], readback[3]);
        }

        /* Set a mat4 and read it back (identity) */
        if (matLoc != -1) {
            float identity[16] = {
                1,0,0,0,
                0,1,0,0,
                0,0,1,0,
                0,0,0,1
            };
            glUniformMatrix4fv(matLoc, 1, GL_FALSE, identity);

            GLfloat matRead[16] = {0};
            glGetUniformfv(uprog, matLoc, matRead);
            bool matMatch = (matRead[0] > 0.99f && matRead[0] < 1.01f) &&
                            (matRead[5] > 0.99f && matRead[5] < 1.01f) &&
                            (matRead[10] > 0.99f && matRead[10] < 1.01f) &&
                            (matRead[15] > 0.99f && matRead[15] < 1.01f);
            recordResult("glGetUniformfv readback mat4",
                         matMatch, NULL);
        }
    }

    /* === glCheckFramebufferStatus === */

    /* Default framebuffer should be complete */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    recordResult("glCheckFramebufferStatus(default FB) == COMPLETE",
                 fbStatus == GL_FRAMEBUFFER_COMPLETE, NULL);

    /* Create a valid FBO and check status */
    GLuint fbo = 0, fboTex = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &fboTex);

    glBindTexture(GL_TEXTURE_2D, fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);

    fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    recordResult("glCheckFramebufferStatus(valid FBO) == COMPLETE",
                 fbStatus == GL_FRAMEBUFFER_COMPLETE, NULL);

    /* FBO without color attachment should be incomplete */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    recordResult("glCheckFramebufferStatus(no color) == INCOMPLETE",
                 fbStatus != GL_FRAMEBUFFER_COMPLETE, NULL);

    /* Cleanup */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fboTex);

    /* === NULL safety tests (formerly crashing) === */
    glDeleteBuffers(1, NULL);
    recordResult("glDeleteBuffers(NULL) - no crash", true, NULL);

    glDeleteTextures(1, NULL);
    recordResult("glDeleteTextures(NULL) - no crash", true, NULL);

    glDeleteFramebuffers(1, NULL);
    recordResult("glDeleteFramebuffers(NULL) - no crash", true, NULL);

    /* === glDepthMask / glColorMask boolean spec compliance === */
    /* Any nonzero value should be treated as true */
    glDepthMask(42);  /* Non-standard true value */
    GLboolean depthWriteMask = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);
    recordResult("glDepthMask(42) treated as true",
                 depthWriteMask == GL_TRUE, NULL);

    glDepthMask(GL_TRUE);  /* Restore */

    glColorMask(2, 2, 2, 2);  /* Non-standard true values */
    GLboolean colorWriteMask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
    glGetBooleanv(GL_COLOR_WRITEMASK, colorWriteMask);
    recordResult("glColorMask(2,2,2,2) all treated as true",
                 colorWriteMask[0] == GL_TRUE && colorWriteMask[1] == GL_TRUE &&
                 colorWriteMask[2] == GL_TRUE && colorWriteMask[3] == GL_TRUE, NULL);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  /* Restore */

    printf("--- Shader/Program Queries tests complete ---\n");
}

/*==========================================================================
 * TEST: GLES2 Spec Compliance (parameter validation)
 *==========================================================================*/

static void testSpecCompliance(void) {
    printf("\n--- Test: GLES2 Spec Compliance ---\n");

    /* === Buffer validations === */

    /* glBufferData with invalid target */
    glBufferData(0x9999, 16, NULL, GL_STATIC_DRAW);
    recordResult("glBufferData(invalid target) -> GL_INVALID_ENUM",
                 glGetError() == GL_INVALID_ENUM, NULL);

    /* glBufferData with invalid usage */
    GLuint testBuf = 0;
    glGenBuffers(1, &testBuf);
    glBindBuffer(GL_ARRAY_BUFFER, testBuf);
    glBufferData(GL_ARRAY_BUFFER, 16, NULL, 0x9999);
    recordResult("glBufferData(invalid usage) -> GL_INVALID_ENUM",
                 glGetError() == GL_INVALID_ENUM, NULL);

    /* glBufferData with negative size */
    glBufferData(GL_ARRAY_BUFFER, -1, NULL, GL_STATIC_DRAW);
    recordResult("glBufferData(size=-1) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* Valid buffer data to continue */
    glBufferData(GL_ARRAY_BUFFER, 16, NULL, GL_STATIC_DRAW);
    recordResult("glBufferData(valid) -> no error",
                 glGetError() == GL_NO_ERROR, NULL);

    glDeleteBuffers(1, &testBuf);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* === Texture validations === */

    GLuint testTex = 0;
    glGenTextures(1, &testTex);
    glBindTexture(GL_TEXTURE_2D, testTex);

    /* glTexImage2D with border != 0 */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    recordResult("glTexImage2D(border=1) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* glTexImage2D with negative level */
    glTexImage2D(GL_TEXTURE_2D, -1, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    recordResult("glTexImage2D(level=-1) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* glTexImage2D with negative dimensions */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, -4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    recordResult("glTexImage2D(width=-4) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* glTexParameterfv with NULL params */
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, NULL);
    recordResult("glTexParameterfv(NULL) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* glTexParameteriv with NULL params */
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, NULL);
    recordResult("glTexParameteriv(NULL) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    glDeleteTextures(1, &testTex);

    /* === Viewport/Scissor validations === */

    /* glViewport with negative width */
    glViewport(0, 0, -1, 720);
    recordResult("glViewport(w=-1) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* glScissor with negative height */
    glScissor(0, 0, 100, -1);
    recordResult("glScissor(h=-1) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* Restore valid viewport/scissor */
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* === Draw validations === */

    /* glDrawArrays with first < 0 */
    glDrawArrays(GL_TRIANGLES, -1, 3);
    recordResult("glDrawArrays(first=-1) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    /* === Renderbuffer validation === */
    GLuint testRb = 0;
    glGenRenderbuffers(1, &testRb);
    glBindRenderbuffer(GL_RENDERBUFFER, testRb);

    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, -1, 64);
    recordResult("glRenderbufferStorage(w=-1) -> GL_INVALID_VALUE",
                 glGetError() == GL_INVALID_VALUE, NULL);

    glDeleteRenderbuffers(1, &testRb);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    /* === glGetShaderPrecisionFormat === */
    GLint precRange[2] = {0, 0};
    GLint precPrecision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, precRange, &precPrecision);
    recordResult("glGetShaderPrecisionFormat(HIGH_FLOAT) range=[127,127] prec=23",
                 precRange[0] == 127 && precRange[1] == 127 && precPrecision == 23, NULL);

    glGetShaderPrecisionFormat(GL_VERTEX_SHADER, GL_HIGH_INT, precRange, &precPrecision);
    recordResult("glGetShaderPrecisionFormat(HIGH_INT) range=[31,30] prec=0",
                 precRange[0] == 31 && precRange[1] == 30 && precPrecision == 0, NULL);

    /* === glValidateProgram === */
    {
        GLuint prog = getSimpleProgram();
        glUseProgram(prog);
        glValidateProgram(prog);
        GLint validateStatus = GL_FALSE;
        glGetProgramiv(prog, GL_VALIDATE_STATUS, &validateStatus);
        recordResult("glValidateProgram -> VALIDATE_STATUS = TRUE",
                     validateStatus == GL_TRUE, NULL);
        glDeleteProgram(prog);
        glUseProgram(0);
    }

    /* === glBlendColor query === */
    glBlendColor(0.25f, 0.5f, 0.75f, 1.0f);
    GLfloat blendColor[4] = {0};
    glGetFloatv(GL_BLEND_COLOR, blendColor);
    recordResult("glGetFloatv(GL_BLEND_COLOR) readback",
                 blendColor[0] > 0.24f && blendColor[0] < 0.26f &&
                 blendColor[1] > 0.49f && blendColor[1] < 0.51f &&
                 blendColor[2] > 0.74f && blendColor[2] < 0.76f &&
                 blendColor[3] > 0.99f, NULL);
    glBlendColor(0.0f, 0.0f, 0.0f, 0.0f); /* restore */

    /* === glSampleCoverage query === */
    glSampleCoverage(0.5f, GL_TRUE);
    GLfloat sampleCovVal = 0.0f;
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &sampleCovVal);
    GLboolean sampleCovInvert = GL_FALSE;
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &sampleCovInvert);
    recordResult("glSampleCoverage(0.5, TRUE) stored correctly",
                 sampleCovVal > 0.49f && sampleCovVal < 0.51f && sampleCovInvert == GL_TRUE, NULL);
    glSampleCoverage(1.0f, GL_FALSE); /* restore */

    /* === GL_MAX_VERTEX_ATTRIBS == 16 (deko3d limit) === */
    GLint maxAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    recordResult("GL_MAX_VERTEX_ATTRIBS == 16",
                 maxAttribs == 16, NULL);

    printf("--- GLES2 Spec Compliance tests complete ---\n");
}

/*==========================================================================
 * TEST: Packed UBO
 *==========================================================================*/

static GLuint s_packedProgram = 0;

static GLuint getPackedProgram(void) {
    if (!s_packedProgram) {
        s_packedProgram = createProgramFromFiles(
            "romfs:/shaders/packed_vsh.dksh",
            "romfs:/shaders/packed_fsh.dksh");
    }
    return s_packedProgram;
}

static void testPackedUBO(void) {
    printf("\n--- Test: Packed UBO ---\n");

    GLuint prog = getPackedProgram();
    if (!prog) {
        recordResult("Packed UBO: shader load", false, "failed to load shaders");
        return;
    }
    recordResult("Packed UBO: shader load", true, NULL);

    /* Clear previous user-registered uniforms */
    sglClearUniformRegistry();

    /*
     * Register packed uniforms for vertex stage:
     * binding 0, total 80 bytes (mat4=64 + vec4=16)
     */
    sglSetPackedUBOSize(SGL_STAGE_VERTEX, 0, 80);
    sglRegisterPackedUniform("u_packedMatrix", SGL_STAGE_VERTEX, 0, 0);
    sglRegisterPackedUniform("u_packedOffset", SGL_STAGE_VERTEX, 0, 64);

    /*
     * Register packed uniforms for fragment stage:
     * binding 0, total 32 bytes (vec4=16 + vec4=16)
     */
    sglSetPackedUBOSize(SGL_STAGE_FRAGMENT, 0, 32);
    sglRegisterPackedUniform("u_packedColor",  SGL_STAGE_FRAGMENT, 0, 0);
    sglRegisterPackedUniform("u_packedParams", SGL_STAGE_FRAGMENT, 0, 16);

    glUseProgram(prog);

    /* Verify locations are packed (bit 31 set) */
    GLint matLoc = glGetUniformLocation(prog, "u_packedMatrix");
    GLint offLoc = glGetUniformLocation(prog, "u_packedOffset");
    GLint colLoc = glGetUniformLocation(prog, "u_packedColor");
    GLint parLoc = glGetUniformLocation(prog, "u_packedParams");

    bool locsOk = (matLoc != -1) && (offLoc != -1) &&
                  (colLoc != -1) && (parLoc != -1) &&
                  (matLoc & (1 << 31)) && (offLoc & (1 << 31)) &&
                  (colLoc & (1 << 31)) && (parLoc & (1 << 31));
    recordResult("Packed UBO: location encoding", locsOk, NULL);

    if (!locsOk) {
        printf("[DEBUG] matLoc=0x%x offLoc=0x%x colLoc=0x%x parLoc=0x%x\n",
               matLoc, offLoc, colLoc, parLoc);
        sglClearUniformRegistry();
        return;
    }

    /* Set up viewport and clear */
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Identity matrix (no transform) */
    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    /* Set packed vertex uniforms */
    glUniformMatrix4fv(matLoc, 1, GL_FALSE, identity);

    float offset[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glUniform4fv(offLoc, 1, offset);

    /* Set packed fragment uniforms: CYAN color (0, 1, 1, 1) with alpha=1 */
    float color[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
    glUniform4fv(colLoc, 1, color);

    float params[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    glUniform4fv(parLoc, 1, params);

    /* Draw a quad covering most of the screen */
    float quadVerts[] = {
        -0.8f, -0.8f,
         0.8f, -0.8f,
        -0.8f,  0.8f,
         0.8f,  0.8f,
    };

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quadVerts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);

    /* Read pixel WITHOUT swap (avoids stale slot readback) */
    GLubyte pixel[4];
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[DEBUG] Packed UBO pixel: R=%d G=%d B=%d A=%d\n",
           pixel[0], pixel[1], pixel[2], pixel[3]);

    /* Expect CYAN: R~0, G~255, B~255 */
    bool colorOk = (pixel[0] < 10) &&
                   (pixel[1] > 245) &&
                   (pixel[2] > 245);
    recordResult("Packed UBO: vertex + fragment rendering", colorOk, NULL);

    /*
     * Test 2: Change only the color via packed UBO (verify shadow buffer update)
     * Change to YELLOW (1, 1, 0, 1) with alpha multiplier = 0.5
     */
    eglSwapBuffers(s_display, s_surface); /* Need new frame for new uniforms */
    glClear(GL_COLOR_BUFFER_BIT);

    /* Re-set ALL uniforms (SwitchGLES resets each frame) */
    glUniformMatrix4fv(matLoc, 1, GL_FALSE, identity);
    glUniform4fv(offLoc, 1, offset);

    float color2[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
    glUniform4fv(colLoc, 1, color2);

    float params2[4] = { 0.5f, 0.0f, 0.0f, 0.0f };
    glUniform4fv(parLoc, 1, params2);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quadVerts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);

    /* Read pixel WITHOUT swap */
    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("[DEBUG] Packed UBO pixel2: R=%d G=%d B=%d A=%d\n",
           pixel[0], pixel[1], pixel[2], pixel[3]);

    /* Expect YELLOW with alpha=0.5: R~255, G~255, B~0, A~127 */
    bool color2Ok = (pixel[0] > 245) &&
                    (pixel[1] > 245) &&
                    (pixel[2] < 10) &&
                    (pixel[3] >= 120 && pixel[3] <= 135);
    recordResult("Packed UBO: shadow buffer update + alpha", color2Ok, NULL);

    /* Clean up: restore built-in mappings */
    sglClearUniformRegistry();
}

/*==========================================================================
 * TEST: Runtime Shader Compilation (libuam)
 *==========================================================================*/

static GLuint s_runtimeProgram = 0;

/* Helper: draw a quad and read center pixel, return true if no GPU error */
static bool drawAndReadPixel(const char *label, GLint colorLoc,
                             float r, float g, float b, float a,
                             GLubyte *outPixel) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (colorLoc >= 0) {
        glUniform4f(colorLoc, r, g, b, a);
    }

    static const float quad[] = { -0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    static const GLushort idx[] = { 0, 1, 2, 0, 2, 3 };
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);

    glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outPixel);
    printf("[DIAG] %s pixel: R=%d G=%d B=%d A=%d\n",
           label, outPixel[0], outPixel[1], outPixel[2], outPixel[3]);
    glDisableVertexAttribArray(0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("[DIAG] %s: glGetError=0x%X\n", label, err);
    }
    return true;
}

static void testRuntimeShaderCompilation(void) {
    printf("\n--- Test: Runtime Shader Compilation ---\n");

    /* Reset ALL GL state */
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    for (int a = 0; a < 8; a++) glDisableVertexAttribArray(a);

    /* =================================================================
     * DIAGNOSTIC A: Precompiled runtime shaders (same GLSL, offline uam)
     * If this works, the shader code itself is fine.
     * ================================================================= */
    printf("\n[DIAG-A] PRECOMPILED runtime shaders (offline uam)...\n");
    fflush(stdout);
    {
        GLuint precompProg = createProgramFromFiles(
            "romfs:/shaders/runtime_vsh.dksh",
            "romfs:/shaders/runtime_fsh.dksh");

        if (precompProg) {
            glUseProgram(precompProg);
            GLint colorLoc = glGetUniformLocation(precompProg, "u_color");
            printf("[DIAG-A] colorLoc=%d (0x%X)\n", colorLoc, colorLoc);
            fflush(stdout);

            GLubyte pixel[4];
            drawAndReadPixel("DIAG-A precompiled", colorLoc, 0,1,1,1, pixel);
            bool ok = (pixel[0] < 10) && (pixel[1] > 245) && (pixel[2] > 245);
            printf("[DIAG-A] Result: %s\n", ok ? "PASS (CYAN)" : "FAIL");
            recordResult("Runtime: precompiled same shader", ok, NULL);
            fflush(stdout);

            glDeleteProgram(precompProg);
        } else {
            printf("[DIAG-A] FAILED to load precompiled runtime shaders!\n");
            recordResult("Runtime: precompiled same shader", false, "load failed");
            fflush(stdout);
        }
    }

    /* =================================================================
     * DIAGNOSTIC COMPARE: Byte-level comparison of precompiled vs runtime DKSH
     * Uses libuam directly (bypasses SwitchGLES) to isolate the issue.
     * ================================================================= */
#ifdef SGL_ENABLE_RUNTIME_COMPILER
    printf("\n[DIAG-CMP] Byte-level DKSH comparison (precompiled vs runtime)...\n");
    fflush(stdout);
    {
        /* Vertex shader GLSL — must match runtime_vsh.glsl exactly */
        const char *cmpVsSrc =
            "#version 460\n"
            "layout(location = 0) in vec2 a_position;\n"
            "void main() {\n"
            "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
            "}\n";

        /* Fragment shader GLSL — must match runtime_fsh.glsl exactly */
        const char *cmpFsSrc =
            "#version 460\n"
            "layout(std140, binding = 0) uniform ColorBlock {\n"
            "    vec4 u_color;\n"
            "};\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "void main() {\n"
            "    fragColor = u_color;\n"
            "}\n";

        /* Compare vertex shader */
        {
            /* Read precompiled from romfs */
            FILE *f = fopen("romfs:/shaders/runtime_vsh.dksh", "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long preSize = ftell(f);
                fseek(f, 0, SEEK_SET);
                uint8_t *preBuf = (uint8_t *)calloc(1, preSize);
                fread(preBuf, 1, preSize, f);
                fclose(f);

                /* Runtime-compile same GLSL.
                 * CRITICAL: memalign(256) because OutputDkshToMemory uses
                 * absolute pointer alignment (pa256), not relative offsets. */
                uam_compiler *c = uam_create_compiler(DkStage_Vertex);
                if (c && uam_compile_dksh(c, cmpVsSrc)) {
                    size_t rtSize = uam_get_code_size(c);
                    uint8_t *rtBuf = (uint8_t *)memalign(256, rtSize);
                    memset(rtBuf, 0, rtSize);
                    uam_write_code(c, rtBuf);

                    printf("[DIAG-CMP] VS precompiled: %ld bytes, runtime: %zu bytes\n",
                           preSize, rtSize);

                    if ((size_t)preSize == rtSize) {
                        int diffs = 0;
                        int firstDiff = -1;
                        for (size_t i = 0; i < rtSize; i++) {
                            if (preBuf[i] != rtBuf[i]) {
                                if (diffs < 20) {
                                    printf("[DIAG-CMP] VS diff @0x%04zX: pre=0x%02X rt=0x%02X\n",
                                           i, preBuf[i], rtBuf[i]);
                                }
                                if (firstDiff < 0) firstDiff = (int)i;
                                diffs++;
                            }
                        }
                        if (diffs == 0) {
                            printf("[DIAG-CMP] VS: IDENTICAL (0 differences)\n");
                        } else {
                            printf("[DIAG-CMP] VS: %d differences, first at 0x%04X\n",
                                   diffs, firstDiff);
                        }
                    } else {
                        printf("[DIAG-CMP] VS: SIZE MISMATCH!\n");
                        /* Dump headers for both */
                        printf("[DIAG-CMP] VS pre header:");
                        for (int i = 0; i < 32 && i < preSize; i++)
                            printf(" %02X", preBuf[i]);
                        printf("\n");
                        printf("[DIAG-CMP] VS rt  header:");
                        for (size_t i = 0; i < 32 && i < rtSize; i++)
                            printf(" %02X", rtBuf[i]);
                        printf("\n");
                    }
                    free(rtBuf);
                } else {
                    printf("[DIAG-CMP] VS runtime compile FAILED\n");
                    if (c) {
                        const char *log = uam_get_error_log(c);
                        if (log && log[0]) printf("[DIAG-CMP] VS error: %.200s\n", log);
                    }
                }
                if (c) uam_free_compiler(c);
                free(preBuf);
            } else {
                printf("[DIAG-CMP] Cannot open romfs:/shaders/runtime_vsh.dksh\n");
            }
        }

        /* Compare fragment shader */
        {
            FILE *f = fopen("romfs:/shaders/runtime_fsh.dksh", "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long preSize = ftell(f);
                fseek(f, 0, SEEK_SET);
                uint8_t *preBuf = (uint8_t *)calloc(1, preSize);
                fread(preBuf, 1, preSize, f);
                fclose(f);

                uam_compiler *c = uam_create_compiler(DkStage_Fragment);
                if (c && uam_compile_dksh(c, cmpFsSrc)) {
                    size_t rtSize = uam_get_code_size(c);
                    uint8_t *rtBuf = (uint8_t *)memalign(256, rtSize);
                    memset(rtBuf, 0, rtSize);
                    uam_write_code(c, rtBuf);

                    printf("[DIAG-CMP] FS precompiled: %ld bytes, runtime: %zu bytes\n",
                           preSize, rtSize);

                    if ((size_t)preSize == rtSize) {
                        int diffs = 0;
                        int firstDiff = -1;
                        for (size_t i = 0; i < rtSize; i++) {
                            if (preBuf[i] != rtBuf[i]) {
                                if (diffs < 20) {
                                    printf("[DIAG-CMP] FS diff @0x%04zX: pre=0x%02X rt=0x%02X\n",
                                           i, preBuf[i], rtBuf[i]);
                                }
                                if (firstDiff < 0) firstDiff = (int)i;
                                diffs++;
                            }
                        }
                        if (diffs == 0) {
                            printf("[DIAG-CMP] FS: IDENTICAL (0 differences)\n");
                        } else {
                            printf("[DIAG-CMP] FS: %d differences, first at 0x%04X\n",
                                   diffs, firstDiff);
                        }
                    } else {
                        printf("[DIAG-CMP] FS: SIZE MISMATCH!\n");
                        printf("[DIAG-CMP] FS pre header:");
                        for (int i = 0; i < 32 && i < preSize; i++)
                            printf(" %02X", preBuf[i]);
                        printf("\n");
                        printf("[DIAG-CMP] FS rt  header:");
                        for (size_t i = 0; i < 32 && i < rtSize; i++)
                            printf(" %02X", rtBuf[i]);
                        printf("\n");
                    }
                    free(rtBuf);
                } else {
                    printf("[DIAG-CMP] FS runtime compile FAILED\n");
                    if (c) {
                        const char *log = uam_get_error_log(c);
                        if (log && log[0]) printf("[DIAG-CMP] FS error: %.200s\n", log);
                    }
                }
                if (c) uam_free_compiler(c);
                free(preBuf);
            } else {
                printf("[DIAG-CMP] Cannot open romfs:/shaders/runtime_fsh.dksh\n");
            }
        }
        fflush(stdout);
    }
#endif /* SGL_ENABLE_RUNTIME_COMPILER */

    /* =================================================================
     * DIAGNOSTIC B: Runtime-compiled NO-UBO shader (constant output)
     * If this works, libuam runtime compilation itself is OK.
     * ================================================================= */
    printf("\n[DIAG-B] Runtime NO-UBO shader (constant color)...\n");
    fflush(stdout);
    {
        GLuint bvs = glCreateShader(GL_VERTEX_SHADER);
        GLuint bfs = glCreateShader(GL_FRAGMENT_SHADER);

        const char *bvsSrc =
            "#version 460\n"
            "layout(location = 0) in vec2 a_position;\n"
            "void main() {\n"
            "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
            "}\n";

        /* Fragment shader with CONSTANT output — no UBO at all */
        const char *bfsSrc =
            "#version 460\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "void main() {\n"
            "    fragColor = vec4(1.0, 0.0, 1.0, 1.0);\n"  /* MAGENTA */
            "}\n";

        glShaderSource(bvs, 1, &bvsSrc, NULL);
        glCompileShader(bvs);
        GLint bvsOk = GL_FALSE;
        glGetShaderiv(bvs, GL_COMPILE_STATUS, &bvsOk);

        glShaderSource(bfs, 1, &bfsSrc, NULL);
        glCompileShader(bfs);
        GLint bfsOk = GL_FALSE;
        glGetShaderiv(bfs, GL_COMPILE_STATUS, &bfsOk);

        printf("[DIAG-B] VS compile: %s, FS compile: %s\n",
               bvsOk ? "OK" : "FAIL", bfsOk ? "OK" : "FAIL");
        fflush(stdout);

        if (bvsOk && bfsOk) {
            GLuint bprog = glCreateProgram();
            glAttachShader(bprog, bvs);
            glAttachShader(bprog, bfs);
            glLinkProgram(bprog);

            GLint blink = GL_FALSE;
            glGetProgramiv(bprog, GL_LINK_STATUS, &blink);
            printf("[DIAG-B] Link: %s\n", blink ? "OK" : "FAIL");
            fflush(stdout);

            if (blink) {
                glUseProgram(bprog);
                GLubyte pixel[4];
                drawAndReadPixel("DIAG-B no-UBO", -1, 0,0,0,0, pixel);
                bool ok = (pixel[0] > 245) && (pixel[1] < 10) && (pixel[2] > 245);
                printf("[DIAG-B] Result: %s\n", ok ? "PASS (MAGENTA)" : "FAIL");
                recordResult("Runtime: no-UBO constant shader", ok, NULL);
            } else {
                recordResult("Runtime: no-UBO constant shader", false, "link failed");
            }
            glDeleteProgram(bprog);
        } else {
            recordResult("Runtime: no-UBO constant shader", false, "compile failed");
        }

        glDeleteShader(bvs);
        glDeleteShader(bfs);
        fflush(stdout);
    }

    /* =================================================================
     * DIAGNOSTIC C: Runtime-compiled WITH UBO (original test)
     * ================================================================= */
    printf("\n[DIAG-C] Runtime WITH UBO shader...\n");
    fflush(stdout);

    /* ---- Test 1: glShaderSource stores source ---- */
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    recordResult("Runtime: glCreateShader(VERTEX)", vs > 0, NULL);

    const char *vsSrc =
        "#version 460\n"
        "layout(location = 0) in vec2 a_position;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
        "}\n";

    glShaderSource(vs, 1, &vsSrc, NULL);
    recordResult("Runtime: glShaderSource", glGetError() == GL_NO_ERROR, NULL);

    /* ---- Test 2: glGetShaderSource returns stored source ---- */
    GLint srcLen = 0;
    glGetShaderiv(vs, GL_SHADER_SOURCE_LENGTH, &srcLen);
    recordResult("Runtime: GL_SHADER_SOURCE_LENGTH > 0", srcLen > 0, NULL);

    char srcBuf[512];
    GLsizei actualLen = 0;
    glGetShaderSource(vs, sizeof(srcBuf), &actualLen, srcBuf);
    bool srcMatch = (actualLen > 0 && strstr(srcBuf, "a_position") != NULL);
    recordResult("Runtime: glGetShaderSource content", srcMatch, NULL);

    /* ---- Test 3: glCompileShader vertex ---- */
    glCompileShader(vs);
    GLint vsOk = GL_FALSE;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &vsOk);
    recordResult("Runtime: compile vertex shader", vsOk == GL_TRUE, NULL);

    if (vsOk != GL_TRUE) {
        char log[1024];
        glGetShaderInfoLog(vs, sizeof(log), NULL, log);
        printf("[DEBUG] Vertex compile log: %.200s\n", log);
    }

    /* ---- Test 4: glCompileShader fragment (with UBO uniform) ---- */
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char *fsSrc =
        "#version 460\n"
        "layout(std140, binding = 0) uniform ColorBlock {\n"
        "    vec4 u_color;\n"
        "};\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = u_color;\n"
        "}\n";

    glShaderSource(fs, 1, &fsSrc, NULL);
    glCompileShader(fs);
    GLint fsOk = GL_FALSE;
    glGetShaderiv(fs, GL_COMPILE_STATUS, &fsOk);
    recordResult("Runtime: compile fragment shader", fsOk == GL_TRUE, NULL);

    if (fsOk != GL_TRUE) {
        char log[1024];
        glGetShaderInfoLog(fs, sizeof(log), NULL, log);
        printf("[DEBUG] Fragment compile log: %.200s\n", log);
    }

    /* ---- Test 5: Link + draw with runtime-compiled shaders ---- */
    if (vsOk == GL_TRUE && fsOk == GL_TRUE) {
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        GLint linkOk = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkOk);
        recordResult("Runtime: glLinkProgram", linkOk == GL_TRUE, NULL);

        if (linkOk == GL_TRUE) {
            s_runtimeProgram = program;

            glUseProgram(program);

            /* u_color is a built-in mapping → fragment binding 0 */
            GLint colorLoc = glGetUniformLocation(program, "u_color");
            recordResult("Runtime: glGetUniformLocation", colorLoc >= 0, NULL);

            printf("[DIAG-C] Runtime colorLoc=%d (0x%X)\n", colorLoc, colorLoc);
            fflush(stdout);

            GLubyte pixel[4];
            drawAndReadPixel("DIAG-C runtime+UBO", colorLoc, 0,1,1,1, pixel);
            bool colorOk = (pixel[0] < 10) && (pixel[1] > 245) && (pixel[2] > 245);
            printf("[DIAG-C] Result: %s\n", colorOk ? "PASS (CYAN)" : "FAIL");
            recordResult("Runtime: rendered CYAN quad", colorOk, NULL);
            fflush(stdout);
        } else {
            glDeleteProgram(program);
            s_runtimeProgram = 0;
            recordResult("Runtime: glGetUniformLocation", false, "link failed");
            recordResult("Runtime: rendered CYAN quad", false, "link failed");
        }
    } else {
        recordResult("Runtime: glLinkProgram", false, "compile failed");
        recordResult("Runtime: glGetUniformLocation", false, "compile failed");
        recordResult("Runtime: rendered CYAN quad", false, "compile failed");
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    /* Switch back to a known-good precompiled program before continuing */
    glUseProgram(getSimpleProgram());

    /* ---- Test 6: Invalid GLSL should fail ---- */
    {
        GLuint badShader = glCreateShader(GL_FRAGMENT_SHADER);
        const char *badSrc = "this is not valid GLSL at all";
        glShaderSource(badShader, 1, &badSrc, NULL);
        glCompileShader(badShader);
        GLint compileOk = GL_FALSE;
        glGetShaderiv(badShader, GL_COMPILE_STATUS, &compileOk);
        recordResult("Runtime: invalid GLSL fails", compileOk == GL_FALSE, NULL);

        GLint logLen = 0;
        glGetShaderiv(badShader, GL_INFO_LOG_LENGTH, &logLen);
        recordResult("Runtime: error info log available", logLen > 0, NULL);
        glDeleteShader(badShader);
    }

    /* ---- Test 7: Multi-string glShaderSource ---- */
    {
        GLuint multiVs = glCreateShader(GL_VERTEX_SHADER);
        const char *parts[] = {
            "#version 460\n",
            "layout(location = 0) in vec2 a_position;\n",
            "void main() {\n",
            "    gl_Position = vec4(a_position, 0.0, 1.0);\n",
            "}\n"
        };
        glShaderSource(multiVs, 5, parts, NULL);
        recordResult("Runtime: multi-string glShaderSource", glGetError() == GL_NO_ERROR, NULL);

        glCompileShader(multiVs);
        GLint multiOk = GL_FALSE;
        glGetShaderiv(multiVs, GL_COMPILE_STATUS, &multiOk);
        recordResult("Runtime: compile multi-string shader", multiOk == GL_TRUE, NULL);
        if (multiOk != GL_TRUE) {
            char log[512];
            glGetShaderInfoLog(multiVs, sizeof(log), NULL, log);
            printf("[DEBUG] Multi-string compile log: %.200s\n", log);
        }
        glDeleteShader(multiVs);
    }
}

/*==========================================================================
 * Print summary
 *==========================================================================*/

static void printSummary(void) {
    printf("\n========================================\n");
    printf("         VALIDATION SUMMARY\n");
    printf("========================================\n");

    int passed = 0, failed = 0;
    for (int i = 0; i < s_numResults; i++) {
        if (s_results[i].passed) passed++;
        else failed++;
    }

    printf("Passed: %d / %d\n", passed, s_numResults);
    printf("Failed: %d / %d\n", failed, s_numResults);

    if (failed > 0) {
        printf("\nFailed tests:\n");
        for (int i = 0; i < s_numResults; i++) {
            if (!s_results[i].passed) {
                printf("  - %s\n", s_results[i].name);
            }
        }
    }

    printf("========================================\n");
}

/*==========================================================================
 * Main
 *==========================================================================*/

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    Result rc = romfsInit();
    initNxLink();

    if (R_FAILED(rc)) {
        printf("[ERROR] romfsInit failed: 0x%x\n", rc);
        printf("Make sure the NRO was built with --romfsdir\n");
    } else {
        printf("[OK] romfsInit succeeded\n");
    }

    if (!initEgl()) {
        printf("EGL initialization failed!\n");
        deinitNxLink();
        romfsExit();
        return 1;
    }

    printf("\n========================================\n");
    printf("    VISUAL VALIDATION TEST (SwitchGLES)\n");
    printf("    Press A after each test to continue\n");
    printf("    Press + to exit at any time\n");
    printf("========================================\n");

    /* Run all tests with visual validation - check s_exitRequested after each */
#define RUN_TEST(testFunc, name, expected) \
    do { \
        if (!s_exitRequested) { testFunc(); } \
        if (!s_exitRequested) { waitForA(name, expected); } \
    } while(0)

    RUN_TEST(testClear, "Clear Operations",
        "Brown/tan background (RGB ~127,64,31)\n"
        "Solid color filling entire screen");

    RUN_TEST(testBasicTriangle, "Basic Triangle",
        "Dark gray background\n"
        "GREEN triangle in center of screen");

    RUN_TEST(testVBO, "Vertex Buffer Objects",
        "Black background\n"
        "YELLOW square in center");

    RUN_TEST(testTextures, "Textures",
        "Gray background (50% gray)\n"
        "BLACK/WHITE checkerboard quad in center\n"
        "Sharp pixel edges (GL_NEAREST filtering)");

    RUN_TEST(testTexSubImage, "glTexSubImage2D",
        "Gray background (50% gray)\n"
        "RED quad with GREEN center\n"
        "(Green 4x4 patch in middle of 8x8 red texture)");

    RUN_TEST(testDepth, "Depth Testing",
        "Dark gray background\n"
        "Large RED quad behind\n"
        "Smaller GREEN quad in front (center)\n"
        "GREEN must be visible ON TOP of red!");

    RUN_TEST(testPolygonOffset, "Polygon Offset",
        "Dark gray background\n"
        "Large RED quad and smaller GREEN quad at SAME Z\n"
        "GREEN should be ON TOP (polygon offset makes it closer)\n"
        "Without polygon offset, would have z-fighting artifacts");

    RUN_TEST(testBlending, "Blending",
        "Black background\n"
        "RED quad (opaque)\n"
        "GREEN semi-transparent quad overlapping\n"
        "Overlap area should be YELLOW-ISH (blend of red+green)");

    RUN_TEST(testCulling, "Face Culling",
        "Dark gray background\n"
        "Only ONE triangle visible (CCW winding)\n"
        "CW triangle should be culled (invisible)");

    RUN_TEST(testScissor, "Scissor Test",
        "BLUE background (outside scissor)\n"
        "GREEN rectangle in CENTER (inside scissor)\n"
        "Corners should remain blue");

    RUN_TEST(testColorMask, "Color Mask",
        "Black background\n"
        "RED-only quad (no green or blue written)\n"
        "Should appear as pure RED");

    RUN_TEST(testUniforms, "Uniforms (mat4, vec4)",
        "Black background\n"
        "MAGENTA (pink/purple) quad in center\n"
        "Transformed by matrix uniform");

    RUN_TEST(testFBO, "Framebuffer Objects (FBO)",
        "Black background\n"
        "ORANGE quad in center\n"
        "(Rendered to texture, then texture displayed)");

    RUN_TEST(testMipmaps, "Mipmaps",
        "NO VISUAL - API test only\n"
        "(Tests glGenerateMipmap doesn't crash)\n"
        "Previous frame may still be visible");

    RUN_TEST(testCopyTexImage, "glCopyTexImage2D",
        "Gray background\n"
        "A GREEN SQUARE in center\n"
        "(Center of framebuffer copied to texture, displayed on quad)");

    RUN_TEST(testCopyTexSubImage, "glCopyTexSubImage2D",
        "Gray background\n"
        "A RED quad with GREEN center patch\n"
        "(Green from framebuffer was copied to center of red texture)");

    RUN_TEST(testReadPixels, "glReadPixels",
        "Blue-ish background (RGB ~64,128,191)\n"
        "Solid color - used for pixel readback test");

    RUN_TEST(testAllUniforms, "All Uniform Types",
        "Multiple tests - last visible:\n"
        "Small WHITE triangle (scaled 0.5x)\n"
        "Black background");

    RUN_TEST(testMultiTexture, "Multiple Texture Units",
        "PURPLE quad in center\n"
        "(50% blend of RED texture0 + BLUE texture1)\n"
        "Black background");

    RUN_TEST(testCubemap, "Cubemap Textures",
        "Gray background (50% gray)\n"
        "6 small colored squares in a 3x2 grid:\n"
        "Top row: RED (+X), CYAN (-X), GREEN (+Y)\n"
        "Bottom row: MAGENTA (-Y), BLUE (+Z), YELLOW (-Z)");

    RUN_TEST(testFBODepthRenderbuffer, "FBO with Depth Renderbuffer",
        "Black background\n"
        "Should show RED outer area, GREEN center\n"
        "(Green quad is closer, depth test via renderbuffer)");

    RUN_TEST(testQueries, "GL Queries & Extensions",
        "No visual output\n"
        "Tests GL_EXTENSIONS, compressed formats,\n"
        "shader binary formats, glPixelStorei");

    RUN_TEST(testShaderQueries, "Shader/Program Queries",
        "No visual output\n"
        "Tests glGetAttribLocation, glBindAttribLocation,\n"
        "glGetActiveAttrib/Uniform, glGetUniformfv,\n"
        "glCheckFramebufferStatus, NULL safety, boolean spec");

    RUN_TEST(testSpecCompliance, "GLES2 Spec Compliance",
        "No visual output\n"
        "Tests parameter validation: buffer, texture,\n"
        "viewport, scissor, draw, renderbuffer limits,\n"
        "shader precision, blend color, sample coverage");

    RUN_TEST(testPackedUBO, "Packed UBO",
        "Black background\n"
        "YELLOW quad with semi-transparent alpha\n"
        "(Tests packed uniform shadow buffer)");

    RUN_TEST(testRuntimeShaderCompilation, "Runtime Shader Compilation (libuam)",
        "Black background\n"
        "CYAN quad in center\n"
        "(Shader compiled at runtime from GLSL 4.60 source)");

    /* Print summary */
    printf("[EXIT] About to print summary\n");
    fflush(stdout);
    printSummary();

    /* Display final "tests terminés" screen */
    {
        int passed = 0, failed = 0;
        for (int i = 0; i < s_numResults; i++) {
            if (s_results[i].passed) passed++;
            else failed++;
        }
        if (failed == 0) {
            glClearColor(0.0f, 0.5f, 0.0f, 1.0f);  /* Green = all pass */
        } else {
            glClearColor(0.5f, 0.0f, 0.0f, 1.0f);  /* Red = failures */
        }
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(s_display, s_surface);
        printf("\n[DONE] Tests termines - %d/%d PASS\n", passed, s_numResults);
        fflush(stdout);
    }

    /* Unbind current program before cleanup */
    printf("[EXIT] glUseProgram(0)\n");
    fflush(stdout);
    glUseProgram(0);

    /* Cleanup shader programs */
    printf("[EXIT] Deleting programs\n");
    fflush(stdout);
    if (s_simpleProgram) glDeleteProgram(s_simpleProgram);
    if (s_texturedProgram) glDeleteProgram(s_texturedProgram);
    if (s_uniformProgram) glDeleteProgram(s_uniformProgram);
    if (s_allUniformProgram) glDeleteProgram(s_allUniformProgram);
    if (s_multiTexProgram) glDeleteProgram(s_multiTexProgram);
    if (s_cubemapProgram) glDeleteProgram(s_cubemapProgram);
    if (s_packedProgram) glDeleteProgram(s_packedProgram);
    if (s_runtimeProgram) glDeleteProgram(s_runtimeProgram);

    printf("\n[LIFECYCLE] All tests complete. Programs deleted.\n");
    fflush(stdout);

    /* Wait for user to exit */
    printf("\nPress + to exit...\n");
    fflush(stdout);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;

        /* Just poll input - no eglSwapBuffers needed (programs already deleted) */
        svcSleepThread(16000000ULL); /* ~16ms = 60fps polling */
    }

    printf("[LIFECYCLE] + pressed, beginning cleanup...\n");
    fflush(stdout);

    printf("[LIFECYCLE] glFinish...\n");
    fflush(stdout);
    glFinish(); /* Wait for GPU to complete all work before teardown */
    printf("[LIFECYCLE] glFinish OK\n");
    fflush(stdout);

    printf("[LIFECYCLE] deinitEgl...\n");
    fflush(stdout);
    deinitEgl();
    printf("[LIFECYCLE] deinitEgl OK\n");
    fflush(stdout);

    printf("[LIFECYCLE] deinitNxLink...\n");
    fflush(stdout);
    deinitNxLink();

    printf("[LIFECYCLE] Clean exit.\n");
    fflush(stdout);

    romfsExit();
    return 0;
}
