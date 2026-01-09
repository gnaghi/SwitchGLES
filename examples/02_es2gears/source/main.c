/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 *
 * Ported to GLES2 by Kristian Høgsberg <krh@bitplanet.net>
 * Adapted for Nintendo Switch (SwitchGLES/deko3d)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
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
    printf("EGL initialized successfully\n");
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
 * Gear data structures
 *==========================================================================*/

#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 34
#define GEAR_VERTEX_STRIDE 6

struct vertex_strip {
    GLint first;
    GLint count;
};

typedef GLfloat GearVertex[GEAR_VERTEX_STRIDE];

struct gear {
    GearVertex *vertices;
    int nvertices;
    struct vertex_strip *strips;
    int nstrips;
    GLuint vbo;
};

/*==========================================================================
 * Global state
 *==========================================================================*/

static GLfloat view_rot[3] = { 20.0, 30.0, 0.0 };
static struct gear *gear1, *gear2, *gear3;
static GLfloat angle = 0.0;
static GLuint ModelViewProjectionMatrix_location;
static GLuint NormalMatrix_location;
static GLuint LightSourcePosition_location;
static GLuint MaterialColor_location;
static GLfloat ProjectionMatrix[16];
static const GLfloat LightSourcePosition[4] = { 5.0, 5.0, 10.0, 1.0 };
static GLuint shaderProgram;

/*==========================================================================
 * Gear creation
 *==========================================================================*/

static GearVertex *
vert(GearVertex *v, GLfloat x, GLfloat y, GLfloat z, GLfloat n[3])
{
    v[0][0] = x;
    v[0][1] = y;
    v[0][2] = z;
    v[0][3] = n[0];
    v[0][4] = n[1];
    v[0][5] = n[2];
    return v + 1;
}

static struct gear *
create_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
            GLint teeth, GLfloat tooth_depth)
{
    GLfloat r0, r1, r2;
    GLfloat da;
    GearVertex *v;
    struct gear *gear;
    double s[5], c[5];
    GLfloat normal[3];
    int cur_strip = 0;
    int i;

    gear = malloc(sizeof *gear);
    if (!gear) return NULL;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0;
    r2 = outer_radius + tooth_depth / 2.0;

    da = 2.0 * M_PI / teeth / 4.0;

    gear->nstrips = STRIPS_PER_TOOTH * teeth;
    gear->strips = calloc(gear->nstrips, sizeof(*gear->strips));
    gear->vertices = calloc(VERTICES_PER_TOOTH * teeth, sizeof(*gear->vertices));
    v = gear->vertices;

    for (i = 0; i < teeth; i++) {
        sincos(i * 2.0 * M_PI / teeth, &s[0], &c[0]);
        sincos(i * 2.0 * M_PI / teeth + da, &s[1], &c[1]);
        sincos(i * 2.0 * M_PI / teeth + da * 2, &s[2], &c[2]);
        sincos(i * 2.0 * M_PI / teeth + da * 3, &s[3], &c[3]);
        sincos(i * 2.0 * M_PI / teeth + da * 4, &s[4], &c[4]);

#define GEAR_POINT(r, da) { (r) * c[(da)], (r) * s[(da)] }
#define SET_NORMAL(x, y, z) do { normal[0] = (x); normal[1] = (y); normal[2] = (z); } while(0)
#define GEAR_VERT(v, point, sign) vert((v), p[(point)].x, p[(point)].y, (sign) * width * 0.5, normal)
#define START_STRIP do { gear->strips[cur_strip].first = v - gear->vertices; } while(0)
#define END_STRIP do { gear->strips[cur_strip].count = (v - gear->vertices) - gear->strips[cur_strip].first; cur_strip++; } while(0)
#define QUAD_WITH_NORMAL(p1, p2) do { \
    SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
    v = GEAR_VERT(v, (p1), -1); v = GEAR_VERT(v, (p1), 1); \
    v = GEAR_VERT(v, (p2), -1); v = GEAR_VERT(v, (p2), 1); \
} while(0)

        struct point { GLfloat x, y; };
        struct point p[7] = {
            GEAR_POINT(r2, 1), GEAR_POINT(r2, 2), GEAR_POINT(r1, 0),
            GEAR_POINT(r1, 3), GEAR_POINT(r0, 0), GEAR_POINT(r1, 4), GEAR_POINT(r0, 4),
        };

        START_STRIP; SET_NORMAL(0, 0, 1.0);
        v = GEAR_VERT(v, 0, +1); v = GEAR_VERT(v, 1, +1); v = GEAR_VERT(v, 2, +1);
        v = GEAR_VERT(v, 3, +1); v = GEAR_VERT(v, 4, +1); v = GEAR_VERT(v, 5, +1);
        v = GEAR_VERT(v, 6, +1); END_STRIP;

        START_STRIP; QUAD_WITH_NORMAL(4, 6); END_STRIP;

        START_STRIP; SET_NORMAL(0, 0, -1.0);
        v = GEAR_VERT(v, 6, -1); v = GEAR_VERT(v, 5, -1); v = GEAR_VERT(v, 4, -1);
        v = GEAR_VERT(v, 3, -1); v = GEAR_VERT(v, 2, -1); v = GEAR_VERT(v, 1, -1);
        v = GEAR_VERT(v, 0, -1); END_STRIP;

        START_STRIP; QUAD_WITH_NORMAL(0, 2); END_STRIP;
        START_STRIP; QUAD_WITH_NORMAL(1, 0); END_STRIP;
        START_STRIP; QUAD_WITH_NORMAL(3, 1); END_STRIP;
        START_STRIP; QUAD_WITH_NORMAL(5, 3); END_STRIP;
    }

    gear->nvertices = (v - gear->vertices);

    glGenBuffers(1, &gear->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
    glBufferData(GL_ARRAY_BUFFER, gear->nvertices * sizeof(GearVertex), gear->vertices, GL_STATIC_DRAW);

    return gear;
}

/*==========================================================================
 * Matrix operations
 *==========================================================================*/

static void multiply(GLfloat *m, const GLfloat *n) {
    GLfloat tmp[16];
    for (int i = 0; i < 16; i++) {
        tmp[i] = 0;
        int row = (i / 4) * 4;
        int col = i % 4;
        for (int j = 0; j < 4; j++)
            tmp[i] += n[row + j] * m[col + j * 4];
    }
    memcpy(m, tmp, sizeof(tmp));
}

static void rotate(GLfloat *m, GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    double s, c;
    sincos(angle, &s, &c);
    GLfloat r[16] = {
        x*x*(1-c)+c,     y*x*(1-c)+z*s, x*z*(1-c)-y*s, 0,
        x*y*(1-c)-z*s, y*y*(1-c)+c,     y*z*(1-c)+x*s, 0,
        x*z*(1-c)+y*s, y*z*(1-c)-x*s, z*z*(1-c)+c,     0,
        0, 0, 0, 1
    };
    multiply(m, r);
}

static void translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z) {
    GLfloat t[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1 };
    multiply(m, t);
}

static void identity(GLfloat *m) {
    GLfloat t[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    memcpy(m, t, sizeof(t));
}

static void transpose(GLfloat *m) {
    GLfloat t[16] = {
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]
    };
    memcpy(m, t, sizeof(t));
}

static void invert(GLfloat *m) {
    GLfloat t[16];
    identity(t);
    t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];
    m[12] = m[13] = m[14] = 0;
    transpose(m);
    multiply(m, t);
}

static void perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar) {
    GLfloat tmp[16];
    identity(tmp);
    double sine, cosine, cotangent, deltaZ;
    GLfloat radians = fovy / 2 * M_PI / 180;
    deltaZ = zFar - zNear;
    sincos(radians, &sine, &cosine);
    if (deltaZ == 0 || sine == 0 || aspect == 0) return;
    cotangent = cosine / sine;
    tmp[0] = cotangent / aspect;
    tmp[5] = cotangent;
    tmp[10] = -(zFar + zNear) / deltaZ;
    tmp[11] = -1;
    tmp[14] = -2 * zNear * zFar / deltaZ;
    tmp[15] = 0;
    memcpy(m, tmp, sizeof(tmp));
}

/*==========================================================================
 * Drawing
 *==========================================================================*/

static void draw_gear(struct gear *gear, GLfloat *transform,
                      GLfloat x, GLfloat y, GLfloat gearAngle, const GLfloat color[4])
{
    GLfloat model_view[16];
    GLfloat normal_matrix[16];
    GLfloat model_view_projection[16];

    printf("  [draw_gear] vbo=%u nstrips=%d\n", gear->vbo, gear->nstrips); fflush(stdout);

    memcpy(model_view, transform, sizeof(model_view));
    translate(model_view, x, y, 0);
    rotate(model_view, 2 * M_PI * gearAngle / 360.0, 0, 0, 1);

    memcpy(model_view_projection, ProjectionMatrix, sizeof(model_view_projection));
    multiply(model_view_projection, model_view);

    printf("  [draw_gear] glUniformMatrix4fv MVP\n"); fflush(stdout);
    glUniformMatrix4fv(ModelViewProjectionMatrix_location, 1, GL_FALSE, model_view_projection);

    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);
    printf("  [draw_gear] glUniformMatrix4fv Normal\n"); fflush(stdout);
    glUniformMatrix4fv(NormalMatrix_location, 1, GL_FALSE, normal_matrix);

    printf("  [draw_gear] glUniform4fv Color\n"); fflush(stdout);
    glUniform4fv(MaterialColor_location, 1, color);

    printf("  [draw_gear] glBindBuffer\n"); fflush(stdout);
    glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
    printf("  [draw_gear] glVertexAttribPointer\n"); fflush(stdout);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    printf("  [draw_gear] drawing %d strips...\n", gear->nstrips); fflush(stdout);
    for (int n = 0; n < gear->nstrips; n++) {
        if (n % 10 == 0) {
            printf("  [draw_gear] strip %d/%d\n", n, gear->nstrips); fflush(stdout);
        }
        glDrawArrays(GL_TRIANGLE_STRIP, gear->strips[n].first, gear->strips[n].count);
    }
    printf("  [draw_gear] done\n"); fflush(stdout);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

static void gears_draw(void) {
    static const GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
    static const GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
    static const GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };
    GLfloat transform[16];

    printf("[GEARS] glClear start\n"); fflush(stdout);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    printf("[GEARS] glClear done\n"); fflush(stdout);

    identity(transform);
    translate(transform, 0, 0, -20);
    rotate(transform, 2 * M_PI * view_rot[0] / 360.0, 1, 0, 0);
    rotate(transform, 2 * M_PI * view_rot[1] / 360.0, 0, 1, 0);
    rotate(transform, 2 * M_PI * view_rot[2] / 360.0, 0, 0, 1);

    printf("[GEARS] draw_gear 1 (red)\n"); fflush(stdout);
    draw_gear(gear1, transform, -3.0, -2.0, angle, red);
    printf("[GEARS] draw_gear 2 (green)\n"); fflush(stdout);
    draw_gear(gear2, transform, 3.1, -2.0, -2 * angle - 9.0, green);
    printf("[GEARS] draw_gear 3 (blue)\n"); fflush(stdout);
    draw_gear(gear3, transform, -3.1, 4.2, -2 * angle - 25.0, blue);
    printf("[GEARS] all gears drawn\n"); fflush(stdout);
}

/*==========================================================================
 * Shader loading (SwitchGLES - precompiled DKSH)
 *==========================================================================*/


static bool loadShaders(void) {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

    if (!sgl_load_shader_from_file(vs, "romfs:/shaders/gears_vsh.dksh")) {
        printf("Failed to load vertex shader!\n");
        return false;
    }

    if (!sgl_load_shader_from_file(fs, "romfs:/shaders/gears_fsh.dksh")) {
        printf("Failed to load fragment shader!\n");
        return false;
    }

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);

    glUseProgram(shaderProgram);

    ModelViewProjectionMatrix_location = glGetUniformLocation(shaderProgram, "ModelViewProjectionMatrix");
    NormalMatrix_location = glGetUniformLocation(shaderProgram, "NormalMatrix");
    LightSourcePosition_location = glGetUniformLocation(shaderProgram, "LightSourcePosition");
    MaterialColor_location = glGetUniformLocation(shaderProgram, "MaterialColor");

    /* Set constant light position */
    glUniform4fv(LightSourcePosition_location, 1, LightSourcePosition);

    return true;
}

/*==========================================================================
 * Initialization
 *==========================================================================*/

static bool gears_init(void) {
    if (!loadShaders()) {
        return false;
    }

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    gear1 = create_gear(1.0, 4.0, 1.0, 20, 0.7);
    gear2 = create_gear(0.5, 2.0, 2.0, 10, 0.7);
    gear3 = create_gear(1.3, 2.0, 0.5, 10, 0.7);

    perspective(ProjectionMatrix, 60.0, 1280.0 / 720.0, 1.0, 1024.0);
    glViewport(0, 0, 1280, 720);
    glClearColor(0.0, 0.0, 0.0, 1.0);

    return true;
}

static void gears_cleanup(void) {
    if (gear1) { glDeleteBuffers(1, &gear1->vbo); free(gear1->strips); free(gear1->vertices); free(gear1); }
    if (gear2) { glDeleteBuffers(1, &gear2->vbo); free(gear2->strips); free(gear2->vertices); free(gear2); }
    if (gear3) { glDeleteBuffers(1, &gear3->vbo); free(gear3->strips); free(gear3->vertices); free(gear3); }
    glDeleteProgram(shaderProgram);
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
    printf("  es2gears - Classic Gears Demo\n");
    printf("  Using SwitchGLES (deko3d backend)\n");
    printf("===========================================\n");

    if (!initEgl()) {
        printf("FATAL: EGL initialization failed!\n");
        romfsExit();
        deinitNxLink();
        return EXIT_FAILURE;
    }

    if (!gears_init()) {
        printf("FATAL: Gears initialization failed!\n");
        deinitEgl();
        romfsExit();
        deinitNxLink();
        return EXIT_FAILURE;
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("Starting render loop (press + to exit)...\n");

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    int frames = 0;

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;

        /* Update angle based on time */
        gettimeofday(&t1, NULL);
        double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1000000.0;
        t0 = t1;
        angle += 70.0 * dt;
        if (angle > 3600.0) angle -= 3600.0;

        printf("[FRAME %d] gears_draw start\n", frames); fflush(stdout);
        gears_draw();
        printf("[FRAME %d] eglSwapBuffers start\n", frames); fflush(stdout);
        eglSwapBuffers(s_display, s_surface);
        printf("[FRAME %d] eglSwapBuffers done\n", frames); fflush(stdout);

        frames++;
        if (frames == 60) {
            printf("60 frames rendered, angle=%.1f\n", angle);
        }
    }

    printf("Exiting...\n");
    gears_cleanup();
    deinitEgl();
    romfsExit();
    deinitNxLink();
    return EXIT_SUCCESS;
}
