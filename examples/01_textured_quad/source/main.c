/*
 * textured_quad_sgl - Simple textured quad for testing SwitchGLES texture support
 *
 * This is the SwitchGLES version of textured_quad.
 * Uses precompiled DKSH shaders instead of runtime GLSL compilation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2sgl.h>  /* SwitchGLES extensions */

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
    printf("EGL initialized successfully\n");
    return true;
}

static void deinitEgl(void) {
    if (s_display) {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context) {
            eglDestroyContext(s_display, s_context);
            s_context = NULL;
        }
        if (s_surface) {
            eglDestroySurface(s_display, s_surface);
            s_surface = NULL;
        }
        eglTerminate(s_display);
        s_display = NULL;
    }
}

/*==========================================================================
 * GL objects
 *==========================================================================*/

static GLuint g_vbo = 0;
static GLuint g_program = 0;
static GLuint g_texture = 0;
static GLint g_textureLoc = -1;

/* Quad vertices: position (x,y) + texcoord (u,v) */
static const float vertices[] = {
    /* Position      TexCoord */
    -0.8f, -0.8f,    0.0f, 0.0f,  /* bottom-left */
     0.8f, -0.8f,    1.0f, 0.0f,  /* bottom-right */
     0.8f,  0.8f,    1.0f, 1.0f,  /* top-right */

    -0.8f, -0.8f,    0.0f, 0.0f,  /* bottom-left */
     0.8f,  0.8f,    1.0f, 1.0f,  /* top-right */
    -0.8f,  0.8f,    0.0f, 1.0f,  /* top-left */
};

/*==========================================================================
 * Shader loading (SwitchGLES - precompiled DKSH)
 *==========================================================================*/

/* External function from SwitchGLES to load precompiled shaders */

static bool loadShaders(void) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

    printf("Loading vertex shader...\n");
    if (!sgl_load_shader_from_file(vs, "romfs:/shaders/quad_vsh.dksh")) {
        printf("Failed to load vertex shader!\n");
        return false;
    }

    printf("Loading fragment shader...\n");
    if (!sgl_load_shader_from_file(fs, "romfs:/shaders/quad_fsh.dksh")) {
        printf("Failed to load fragment shader!\n");
        return false;
    }

    printf("Shaders loaded OK\n");

    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);
    printf("Program linked: %u\n", g_program);

    return true;
}

/*==========================================================================
 * Texture creation - procedural checkerboard
 *==========================================================================*/

#define TEXTURE_SIZE 64
#define CHECK_SIZE 8

static void createCheckerboardTexture(void) {
    /* Generate RGBA checkerboard pattern */
    unsigned char* pixels = malloc(TEXTURE_SIZE * TEXTURE_SIZE * 4);

    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            int idx = (y * TEXTURE_SIZE + x) * 4;
            /* Checkerboard pattern */
            int check = ((x / CHECK_SIZE) + (y / CHECK_SIZE)) % 2;
            if (check) {
                /* White */
                pixels[idx + 0] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = 255;
            } else {
                /* Red */
                pixels[idx + 0] = 255;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 0;
                pixels[idx + 3] = 255;
            }
        }
    }

    glGenTextures(1, &g_texture);
    printf("Generated texture ID: %u\n", g_texture);

    glBindTexture(GL_TEXTURE_2D, g_texture);
    printf("Bound texture\n");

    /* Upload texture data */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXTURE_SIZE, TEXTURE_SIZE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    printf("Uploaded texture data (%dx%d)\n", TEXTURE_SIZE, TEXTURE_SIZE);

    /* Set texture parameters */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    printf("Set texture parameters\n");

    free(pixels);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("GL error after texture creation: 0x%x\n", err);
    }
}

/*==========================================================================
 * Scene initialization
 *==========================================================================*/

static bool initScene(void) {
    printf("Initializing scene...\n");

    /* Load shaders */
    if (!loadShaders()) {
        return false;
    }

    /* Get uniform location for sampler */
    g_textureLoc = glGetUniformLocation(g_program, "u_texture");
    printf("Texture uniform location: %d\n", g_textureLoc);

    /* Create VBO */
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    printf("VBO created: %u\n", g_vbo);

    /* Create texture */
    createCheckerboardTexture();

    /* Setup GL state */
    glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
    glViewport(0, 0, 1280, 720);

    printf("Scene initialized successfully\n");
    return true;
}

/*==========================================================================
 * Rendering
 *==========================================================================*/

static void drawFrame(void) {
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program);

    /* Bind texture to texture unit 0 */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_texture);

    /* Set sampler uniform to texture unit 0 */
    glUniform1i(g_textureLoc, 0);

    /* Bind VBO and set vertex attributes */
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    /* Position attribute (location 0): 2 floats, stride 16, offset 0 */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    /* Texcoord attribute (location 1): 2 floats, stride 16, offset 8 */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    /* Draw quad (2 triangles = 6 vertices) */
    glDrawArrays(GL_TRIANGLES, 0, 6);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("GL error in drawFrame: 0x%x\n", err);
    }
}

/*==========================================================================
 * Main
 *==========================================================================*/

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    initNxLink();

    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        printf("romfsInit failed: 0x%x\n", rc);
    }

    printf("===========================================\n");
    printf("  textured_quad_sgl - Texture Test\n");
    printf("  Using SwitchGLES (deko3d backend)\n");
    printf("===========================================\n");

    if (!initEgl()) {
        printf("FATAL: EGL initialization failed!\n");
        romfsExit();
        deinitNxLink();
        return EXIT_FAILURE;
    }

    if (!initScene()) {
        printf("FATAL: Scene initialization failed!\n");
        deinitEgl();
        romfsExit();
        deinitNxLink();
        return EXIT_FAILURE;
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("Starting render loop (press + to exit)...\n");

    int frame = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;

        drawFrame();
        eglSwapBuffers(s_display, s_surface);

        frame++;
        if (frame == 1) {
            printf("First frame rendered!\n");
        } else if (frame % 300 == 0) {
            printf("Frame %d\n", frame);
        }
    }

    printf("Exiting...\n");

    glDeleteBuffers(1, &g_vbo);
    glDeleteTextures(1, &g_texture);
    glDeleteProgram(g_program);

    deinitEgl();
    romfsExit();
    deinitNxLink();
    return EXIT_SUCCESS;
}
