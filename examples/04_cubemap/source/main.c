/*
 * cubemap_test_sgl - Cubemap texture test
 * Target: SwitchGLES (deko3d backend)
 *
 * Renders a cube with a cubemap texture showing different colors per face.
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

/*==========================================================================
 * nxlink support
 *==========================================================================*/

static int s_nxlinkSock = -1;

static void initNxLink(void) {
    if (R_FAILED(socketInitializeDefault()))
        return;
    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock >= 0)
        printf("printf output now goes to nxlink server\n");
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
 * Shader loading (precompiled DKSH)
 *==========================================================================*/

/* Declared in SwitchGLES - loads precompiled shader */

static GLuint createProgram(void) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

    if (!sgl_load_shader_from_file(vs, "romfs:/shaders/cubemap_vsh.dksh")) {
        printf("Failed to load vertex shader!\n");
        return 0;
    }
    if (!sgl_load_shader_from_file(fs, "romfs:/shaders/cubemap_fsh.dksh")) {
        printf("Failed to load fragment shader!\n");
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

    return program;
}

/*==========================================================================
 * Cubemap texture
 *==========================================================================*/

static GLuint createCubemap(void) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

    /* Create 6 faces with different colors */
    const int size = 64;
    unsigned char* pixels = malloc(size * size * 4);

    /* Face colors: +X=Red, -X=Cyan, +Y=Green, -Y=Magenta, +Z=Blue, -Z=Yellow */
    unsigned char faceColors[6][4] = {
        {255, 0, 0, 255},     /* +X Red */
        {0, 255, 255, 255},   /* -X Cyan */
        {0, 255, 0, 255},     /* +Y Green */
        {255, 0, 255, 255},   /* -Y Magenta */
        {0, 0, 255, 255},     /* +Z Blue */
        {255, 255, 0, 255}    /* -Z Yellow */
    };

    GLenum faces[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };

    for (int f = 0; f < 6; f++) {
        /* Fill pixels with face color */
        for (int i = 0; i < size * size; i++) {
            pixels[i * 4 + 0] = faceColors[f][0];
            pixels[i * 4 + 1] = faceColors[f][1];
            pixels[i * 4 + 2] = faceColors[f][2];
            pixels[i * 4 + 3] = faceColors[f][3];
        }
        glTexImage2D(faces[f], 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        printf("Uploaded cubemap face %d\n", f);
    }

    free(pixels);

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return tex;
}

/*==========================================================================
 * Cube geometry
 *==========================================================================*/

/* Simple cube vertices (position only) */
static const float cubeVertices[] = {
    /* Front face */
    -1, -1,  1,   1, -1,  1,   1,  1,  1,
    -1, -1,  1,   1,  1,  1,  -1,  1,  1,
    /* Back face */
     1, -1, -1,  -1, -1, -1,  -1,  1, -1,
     1, -1, -1,  -1,  1, -1,   1,  1, -1,
    /* Left face */
    -1, -1, -1,  -1, -1,  1,  -1,  1,  1,
    -1, -1, -1,  -1,  1,  1,  -1,  1, -1,
    /* Right face */
     1, -1,  1,   1, -1, -1,   1,  1, -1,
     1, -1,  1,   1,  1, -1,   1,  1,  1,
    /* Top face */
    -1,  1,  1,   1,  1,  1,   1,  1, -1,
    -1,  1,  1,   1,  1, -1,  -1,  1, -1,
    /* Bottom face */
    -1, -1, -1,   1, -1, -1,   1, -1,  1,
    -1, -1, -1,   1, -1,  1,  -1, -1,  1
};

/*==========================================================================
 * Matrix helpers
 *==========================================================================*/

static void mat4_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_perspective(float* m, float fovy, float aspect, float near, float far) {
    float f = 1.0f / tanf(fovy / 2.0f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

static void mat4_rotateY(float* m, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    float tmp[16];
    memcpy(tmp, m, 16 * sizeof(float));
    m[0] = tmp[0] * c + tmp[8] * s;
    m[2] = tmp[0] * -s + tmp[8] * c;
    m[4] = tmp[4] * c + tmp[12] * s;
    m[6] = tmp[4] * -s + tmp[12] * c;
    m[8] = tmp[8] * c - tmp[0] * s;
    m[10] = tmp[8] * s + tmp[10] * c;
}

static void mat4_rotateX(float* m, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    float tmp[16];
    memcpy(tmp, m, 16 * sizeof(float));
    m[1] = tmp[1] * c - tmp[9] * s;
    m[5] = tmp[5] * c - tmp[13] * s;
    m[9] = tmp[1] * s + tmp[9] * c;
    m[13] = tmp[5] * s + tmp[13] * c;
}

static void mat4_translate(float* m, float x, float y, float z) {
    m[12] += m[0] * x + m[4] * y + m[8] * z;
    m[13] += m[1] * x + m[5] * y + m[9] * z;
    m[14] += m[2] * x + m[6] * y + m[10] * z;
}

static void mat4_multiply(float* result, const float* a, const float* b) {
    float tmp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i * 4 + j] = 0;
            for (int k = 0; k < 4; k++) {
                tmp[i * 4 + j] += a[k * 4 + j] * b[i * 4 + k];
            }
        }
    }
    memcpy(result, tmp, 16 * sizeof(float));
}

/*==========================================================================
 * Main
 *==========================================================================*/

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    /* Initialize services */
    romfsInit();
    initNxLink();

    printf("cubemap_test_sgl starting...\n");

    /* Initialize EGL */
    if (!initEgl()) {
        printf("EGL init failed!\n");
        deinitNxLink();
        romfsExit();
        return 1;
    }

    printf("EGL initialized\n");

    /* Create shader program */
    GLuint program = createProgram();
    glUseProgram(program);

    GLint mvpLoc = glGetUniformLocation(program, "u_mvp");

    printf("Program created, mvpLoc=%d\n", mvpLoc);

    /* Create cubemap texture */
    GLuint cubemap = createCubemap();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);

    printf("Cubemap created\n");

    /* Setup GL state */
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

    /* Configure input */
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    /* Main loop */
    float angle = 0.0f;

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;

        /* Update rotation */
        angle += 0.01f;

        /* Build MVP matrix */
        float projection[16], view[16], model[16], mvp[16];

        mat4_perspective(projection, 45.0f * M_PI / 180.0f, 1280.0f / 720.0f, 0.1f, 100.0f);

        mat4_identity(view);
        mat4_translate(view, 0, 0, -5);

        mat4_identity(model);
        mat4_rotateY(model, angle);
        mat4_rotateX(model, angle * 0.5f);

        mat4_multiply(mvp, view, model);
        mat4_multiply(mvp, projection, mvp);

        /* Clear and draw */
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, cubeVertices);
        glEnableVertexAttribArray(0);

        glDrawArrays(GL_TRIANGLES, 0, 36);

        eglSwapBuffers(s_display, s_surface);
    }

    /* Cleanup */
    glDeleteTextures(1, &cubemap);
    glDeleteProgram(program);

    deinitEgl();
    deinitNxLink();
    romfsExit();

    return 0;
}
