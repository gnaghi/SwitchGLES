/*
 * 05_spearmint_patterns v4 — Comprehensive GL Pattern Validation
 *
 * Interactive test: for each pattern, draws content and waits for user
 * to press A (PASS) or B (FAIL). Detects normal vs crash exit.
 *
 * CONTROLS:
 *   A = I see the expected result (PASS)
 *   B = I don't see it / screen is black / wrong (FAIL)
 *   + = Skip remaining tests and show summary
 *
 * 21 tests covering: client arrays, VBOs, non-interleaved VBOs, EBO offsets,
 * dynamic orphaning, GL_UNSIGNED_SHORT attribs, non-contiguous attribs,
 * blending, depth test, scissor, cull face, color mask, textures, luminance,
 * bulk texture load (staging stress), GL_RGB textures, cinematic-style
 * per-frame glTexSubImage2D, multi-program switching, full Q3 frame sim.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2sgl.h>

#define SCR_W 1280
#define SCR_H 720
#define QX 340
#define QY 110
#define QW 600
#define QH 500

/* ================================================================ */
static int s_nxlinkSock = -1;
static void initNxLink(void) {
    if (R_FAILED(socketInitializeDefault())) return;
    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock < 0) socketExit();
}
static void deinitNxLink(void) {
    if (s_nxlinkSock >= 0) { close(s_nxlinkSock); socketExit(); s_nxlinkSock = -1; }
}

/* ================================================================ */
static EGLDisplay s_dpy;
static EGLContext s_ctx;
static EGLSurface s_srf;

static bool initEgl(void) {
    s_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!s_dpy) return false;
    eglInitialize(s_dpy, NULL, NULL);
    EGLConfig cfg; EGLint n;
    static const EGLint ca[] = {
        EGL_RED_SIZE,8, EGL_GREEN_SIZE,8, EGL_BLUE_SIZE,8, EGL_ALPHA_SIZE,8,
        EGL_DEPTH_SIZE,24, EGL_STENCIL_SIZE,8, EGL_NONE };
    eglChooseConfig(s_dpy, ca, &cfg, 1, &n);
    if (!n) { eglTerminate(s_dpy); return false; }
    s_srf = eglCreateWindowSurface(s_dpy, cfg, NULL, NULL);
    if (!s_srf) { eglTerminate(s_dpy); return false; }
    static const EGLint ctxa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    s_ctx = eglCreateContext(s_dpy, cfg, EGL_NO_CONTEXT, ctxa);
    if (!s_ctx) return false;
    eglMakeCurrent(s_dpy, s_srf, s_srf, s_ctx);
    return true;
}

/* ================================================================
 * Shader sources (GLSL ES 1.00, runtime compiled)
 * ================================================================ */
static const char *color_vs =
    "#version 100\n"
    "attribute vec3 attr_Position;\n"
    "attribute vec4 attr_Color;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec4 v_Color;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(attr_Position, 1.0);\n"
    "    v_Color = attr_Color;\n"
    "}\n";

static const char *color_fs =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec4 v_Color;\n"
    "void main() {\n"
    "    gl_FragColor = v_Color;\n"
    "}\n";

static const char *tex_vs =
    "#version 100\n"
    "attribute vec3 attr_Position;\n"
    "attribute vec2 attr_TexCoord;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_TexCoord;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(attr_Position, 1.0);\n"
    "    v_TexCoord = attr_TexCoord;\n"
    "}\n";

static const char *tex_fs =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "varying vec2 v_TexCoord;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(tex, v_TexCoord);\n"
    "}\n";

/* textureColor shader — like spearmint's texturecolor (tex * uniform color) */
static const char *texcolor_vs =
    "#version 100\n"
    "attribute vec4 attr_Position;\n"
    "attribute vec2 attr_TexCoord;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_TexCoord;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * attr_Position;\n"
    "    v_TexCoord = attr_TexCoord;\n"
    "}\n";

static const char *texcolor_fs =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform vec4 u_color;\n"
    "varying vec2 v_TexCoord;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(tex, v_TexCoord) * u_color;\n"
    "}\n";

/* ================================================================ */
static GLuint make_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[512]; glGetShaderInfoLog(s,512,NULL,l); printf("Shader err: %s\n",l); }
    return s;
}

static GLuint make_program(const char *vs, const char *fs,
                           const char *a0, int l0,
                           const char *a1, int l1) {
    GLuint v = make_shader(GL_VERTEX_SHADER, vs);
    GLuint f = make_shader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    if (a0) glBindAttribLocation(p, l0, a0);
    if (a1) glBindAttribLocation(p, l1, a1);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char l[512]; glGetProgramInfoLog(p,512,NULL,l); printf("Link err: %s\n",l); }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

static void mat4_ortho(float *m, float l, float r, float b, float t, float n, float f) {
    memset(m, 0, 64);
    m[0]=2.f/(r-l); m[5]=2.f/(t-b); m[10]=-2.f/(f-n);
    m[12]=-(r+l)/(r-l); m[13]=-(t+b)/(t-b); m[14]=-(f+n)/(f-n); m[15]=1.f;
}

/* ================================================================
 * Globals
 * ================================================================ */
static PadState s_pad;
static float s_mvp[16];

static GLuint s_cprog;   /* color: pos(0) + color(1) */
static GLuint s_gprog;   /* gap:   pos(0) + color(5) */
static GLuint s_tprog;   /* tex:   pos(0) + texcoord(1) */
static GLuint s_tcprog;  /* texcolor: pos(0) + texcoord(1) + uniform color */
static GLint s_cmvp, s_gmvp, s_tmvp, s_tcmvp, s_tccolor;

typedef struct { float x,y,z; float r,g,b,a; } CVertex;

/* Static VBO resources */
static GLuint vbo_il, ibo_il;       /* interleaved */
static GLuint vbo_ni, ibo_ni;       /* non-interleaved */
static GLuint vbo_fi, ibo_fi;       /* firstIndex */
static GLuint vbo_dyn, ibo_dyn;     /* dynamic */
static GLuint vbo_us;               /* ushort color */

/* Textures */
static GLuint tex_rgba, tex_lum;
static GLuint tex_rgb;            /* GL_RGB texture (cinematic-style) */
static GLuint tex_bulk[100];      /* bulk texture stress test */
static int    tex_bulk_count = 0;
static int    cin_frame = 0;      /* cinematic frame counter */

/* Test counters */
static int s_tnum = 0, s_pass = 0, s_fail = 0;
static bool s_abort = false;

/* ================================================================
 * Helpers
 * ================================================================ */
static void reset_state(void) {
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glViewport(0, 0, SCR_W, SCR_H);
    glScissor(0, 0, SCR_W, SCR_H);
}

static void draw_quad(float x, float y, float w, float h,
                      float r, float g, float b, float a, float z) {
    float pos[] = { x,y,z, x+w,y,z, x+w,y+h,z, x,y,z, x+w,y+h,z, x,y+h,z };
    float col[] = { r,g,b,a, r,g,b,a, r,g,b,a, r,g,b,a, r,g,b,a, r,g,b,a };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, pos);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, col);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

static void draw_tex_quad(float x, float y, float w, float h, GLuint texid) {
    float pos[] = { x,y,0, x+w,y,0, x+w,y+h,0, x,y,0, x+w,y+h,0, x,y+h,0 };
    float tc[]  = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texid);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, pos);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, tc);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/* ================================================================
 * Run one test: render in a loop, wait for A/B
 * ================================================================ */
typedef void (*draw_fn_t)(void);

static void run_test(const char *name, const char *expected, draw_fn_t draw) {
    if (s_abort) return;

    printf("\n--- TEST %d: %s ---\n", s_tnum, name);
    printf("Expected: %s\n", expected);
    printf("Press A=PASS, B=FAIL, +=ABORT\n");

    int frames = 0;
    while (appletMainLoop()) {
        padUpdate(&s_pad);
        u64 down = padGetButtonsDown(&s_pad);

        if (down & HidNpadButton_A) {
            s_pass++;
            printf(">>> TEST %d: PASS\n", s_tnum);
            s_tnum++;
            return;
        }
        if (down & HidNpadButton_B) {
            s_fail++;
            printf(">>> TEST %d: FAIL\n", s_tnum);
            s_tnum++;
            return;
        }
        if (down & HidNpadButton_Plus) {
            printf(">>> ABORTED at test %d\n", s_tnum);
            s_abort = true;
            return;
        }

        glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        draw();

        if (frames == 0) {
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
                printf("  [GL error after first draw: 0x%X]\n", err);
        }

        eglSwapBuffers(s_dpy, s_srf);
        frames++;
    }
    s_abort = true;
}

/* ================================================================
 * Resource initialization (called once)
 * ================================================================ */
static void init_resources(void) {
    printf("Initializing resources...\n");

    /* Test 3: Interleaved VBO + EBO */
    {
        CVertex v[] = {
            {QX,QY,0, 1,0,0,1}, {QX+QW,QY,0, 1,0,0,1},
            {QX+QW,QY+QH,0, 1,0,0,1}, {QX,QY+QH,0, 1,0,0,1},
        };
        GLushort idx[] = {0,1,2, 0,2,3};
        glGenBuffers(1, &vbo_il); glGenBuffers(1, &ibo_il);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_il);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_il);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        printf("  interleaved: vbo=%u ibo=%u\n", vbo_il, ibo_il);
    }

    /* Test 4: Non-interleaved VBO (pos@0, color@256) */
    {
        float pos[] = { QX,QY,0, QX+QW,QY,0, QX+QW,QY+QH,0, QX,QY+QH,0 };
        float col[] = { 0,1,0,1, 0,1,0,1, 0,1,0,1, 0,1,0,1 };
        GLushort idx[] = {0,1,2, 0,2,3};
        glGenBuffers(1, &vbo_ni); glGenBuffers(1, &ibo_ni);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_ni);
        glBufferData(GL_ARRAY_BUFFER, 512, NULL, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pos), pos);
        glBufferSubData(GL_ARRAY_BUFFER, 256, sizeof(col), col);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_ni);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        printf("  non-interleaved: vbo=%u ibo=%u\n", vbo_ni, ibo_ni);
    }

    /* Test 5: EBO firstIndex */
    {
        CVertex v[] = {
            {-99,-99,0, 0,0,0,1}, {-98,-99,0, 0,0,0,1},
            {-98,-98,0, 0,0,0,1}, {-99,-98,0, 0,0,0,1},
            {QX,QY,0, 0,0,1,1}, {QX+QW,QY,0, 0,0,1,1},
            {QX+QW,QY+QH,0, 0,0,1,1}, {QX,QY+QH,0, 0,0,1,1},
        };
        GLushort idx[] = { 0,1,2,0,2,3, 4,5,6,4,6,7 };
        glGenBuffers(1, &vbo_fi); glGenBuffers(1, &ibo_fi);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_fi);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_fi);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        printf("  firstIndex: vbo=%u ibo=%u\n", vbo_fi, ibo_fi);
    }

    /* Test 6: Dynamic VBO */
    {
        glGenBuffers(1, &vbo_dyn); glGenBuffers(1, &ibo_dyn);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
        glBufferData(GL_ARRAY_BUFFER, 512, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_dyn);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, 64, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        printf("  dynamic: vbo=%u ibo=%u\n", vbo_dyn, ibo_dyn);
    }

    /* Test 7: GL_UNSIGNED_SHORT color VBO (pos@0 as float, color@256 as ushort) */
    {
        float pos[] = { QX,QY,0, QX+QW,QY,0, QX+QW,QY+QH,0, QX,QY+QH,0 };
        GLushort col[] = { 0,65535,65535,65535, 0,65535,65535,65535,
                           0,65535,65535,65535, 0,65535,65535,65535 };
        glGenBuffers(1, &vbo_us);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_us);
        glBufferData(GL_ARRAY_BUFFER, 512, NULL, GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pos), pos);
        glBufferSubData(GL_ARRAY_BUFFER, 256, sizeof(col), col);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        printf("  ushort: vbo=%u\n", vbo_us);
    }

    /* RGBA 8x8 checkerboard texture */
    {
        unsigned char rgba[8*8*4];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int i = (y*8+x)*4;
                int chk = ((x/2) + (y/2)) % 2;
                rgba[i+0] = 255;
                rgba[i+1] = chk ? 255 : 0;
                rgba[i+2] = chk ? 255 : 0;
                rgba[i+3] = 255;
            }
        glGenTextures(1, &tex_rgba);
        glBindTexture(GL_TEXTURE_2D, tex_rgba);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        printf("  tex_rgba=%u\n", tex_rgba);
    }

    /* LUMINANCE 4x4 gradient texture */
    {
        unsigned char lum[4*4];
        for (int i = 0; i < 16; i++) lum[i] = (unsigned char)(i * 17);
        glGenTextures(1, &tex_lum);
        glBindTexture(GL_TEXTURE_2D, tex_lum);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 4, 4, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, lum);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        printf("  tex_lum=%u\n", tex_lum);
    }

    /* GL_RGB 256x256 texture (cinematic-style) */
    {
        unsigned char *rgb = (unsigned char*)malloc(256 * 256 * 3);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                int i = (y * 256 + x) * 3;
                rgb[i+0] = (unsigned char)x;
                rgb[i+1] = (unsigned char)y;
                rgb[i+2] = 128;
            }
        glGenTextures(1, &tex_rgb);
        glBindTexture(GL_TEXTURE_2D, tex_rgb);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 256, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, rgb);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        free(rgb);
        printf("  tex_rgb=%u (256x256 GL_RGB)\n", tex_rgb);
    }

    /* Bulk texture load stress test — like Quake 3 loading 100+ textures at startup */
    {
        printf("  Loading %d bulk textures...\n", 100);
        int sizes[] = { 64, 128, 256, 512, 256, 128, 64, 256, 512, 128 };
        int nsizes = sizeof(sizes) / sizeof(sizes[0]);
        int loaded = 0, failed = 0;
        /* Distinct solid colors for displayed textures */
        unsigned char colors[][3] = {
            {255,0,0}, {0,255,0}, {0,0,255}, {255,255,0},
            {255,0,255}, {0,255,255}, {255,128,0}, {128,0,255},
            {0,255,128}, {255,128,128}
        };
        glGenTextures(100, tex_bulk);
        for (int i = 0; i < 100; i++) {
            int sz = sizes[i % nsizes];
            /* Alternate between GL_RGB and GL_RGBA */
            GLenum fmt = (i % 3 == 0) ? GL_RGB : GL_RGBA;
            int bpp = (fmt == GL_RGB) ? 3 : 4;
            unsigned char *data = (unsigned char*)malloc(sz * sz * bpp);
            /* Solid color fill — visually distinct per texture */
            unsigned char r = colors[i % 10][0];
            unsigned char g = colors[i % 10][1];
            unsigned char b = colors[i % 10][2];
            for (int p = 0; p < sz * sz; p++) {
                data[p * bpp + 0] = r;
                data[p * bpp + 1] = g;
                data[p * bpp + 2] = b;
                if (bpp == 4) data[p * bpp + 3] = 255;
            }
            glBindTexture(GL_TEXTURE_2D, tex_bulk[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, sz, sz, 0,
                         fmt, GL_UNSIGNED_BYTE, data);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                printf("  FAIL: bulk tex %d (%dx%d %s) error=0x%X\n",
                       i, sz, sz, fmt == GL_RGB ? "RGB" : "RGBA", err);
                failed++;
            } else {
                loaded++;
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            free(data);
        }
        tex_bulk_count = 100;
        printf("  Bulk load: %d loaded, %d failed\n", loaded, failed);
    }

    printf("Resources OK.\n");
}

/* ================================================================
 * Test draw functions (each is self-contained)
 * ================================================================ */

/* 1: Client array DrawArrays — WHITE */
static void draw_t1(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    draw_quad(QX, QY, QW, QH, 1,1,1,1, 0);
}

/* 2: Client array DrawElements — ORANGE */
static void draw_t2(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    float pos[] = { QX,QY,0, QX+QW,QY,0, QX+QW,QY+QH,0, QX,QY+QH,0 };
    float col[] = { 1,.5f,0,1, 1,.5f,0,1, 1,.5f,0,1, 1,.5f,0,1 };
    GLushort idx[] = {0,1,2, 0,2,3};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, pos);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, col);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

/* 3: VBO interleaved + EBO — RED */
static void draw_t3(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_il);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_il);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(CVertex), (void*)(3*sizeof(float)));
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* 4: VBO non-interleaved + EBO — GREEN */
static void draw_t4(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_ni);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_ni);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 16, (void*)256);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* 5: EBO firstIndex — BLUE */
static void draw_t5(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_fi);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_fi);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(CVertex), (void*)(3*sizeof(float)));
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT,
                   (void*)(uintptr_t)(6 * sizeof(GLushort)));
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* 6: Dynamic VBO orphaning — YELLOW */
static void draw_t6(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
    glBufferData(GL_ARRAY_BUFFER, 512, NULL, GL_DYNAMIC_DRAW);
    CVertex v[] = {
        {QX,QY,0, 1,1,0,1}, {QX+QW,QY,0, 1,1,0,1},
        {QX+QW,QY+QH,0, 1,1,0,1}, {QX,QY+QH,0, 1,1,0,1},
    };
    GLushort idx[] = {0,1,2, 0,2,3};
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_dyn);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 64, NULL, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(idx), idx);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(CVertex), (void*)(3*sizeof(float)));
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* 7: GL_UNSIGNED_SHORT normalized color — CYAN */
static void draw_t7(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_us);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_ni);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_SHORT, GL_TRUE, 8, (void*)256);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* 8: Non-contiguous attribs (pos@0, skip 1-4, color@5) — MAGENTA */
static void draw_t8(void) {
    glUseProgram(s_gprog);
    glUniformMatrix4fv(s_gmvp, 1, GL_FALSE, s_mvp);
    float pos[] = { QX,QY,0, QX+QW,QY,0, QX+QW,QY+QH,0,
                    QX,QY,0, QX+QW,QY+QH,0, QX,QY+QH,0 };
    float col[] = { 1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1 };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, pos);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 0, col);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(5);
}

/* 9: Alpha blending — semi-transparent RED over GREEN */
static void draw_t9(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    draw_quad(QX, QY, QW, QH, 0, 0.6f, 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_quad(QX+100, QY+100, QW-200, QH-200, 1, 0, 0, 0.5f, 0);
    glDisable(GL_BLEND);
}

/* 10: Depth test — RED in front of BLUE */
static void draw_t10(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    draw_quad(QX, QY, (int)(QW*0.7f), QH, 0, 0, 1, 1, -0.5f);
    draw_quad(QX + (int)(QW*0.3f), QY, (int)(QW*0.7f), QH, 1, 0, 0, 1, 0.5f);
    glDisable(GL_DEPTH_TEST);
}

/* 11: Scissor test — left half only (full-screen quad for max visibility) */
static void draw_t11(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, SCR_W / 2, SCR_H);
    /* Draw full-screen white quad — scissor clips right half */
    draw_quad(0, 0, SCR_W, SCR_H, 1, 1, 1, 1, 0);
    glDisable(GL_SCISSOR_TEST);
}

/* 12: CullFace — green front-facing visible, red back-facing culled */
static void draw_t12(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    /* Our ortho has Y-flipped (b=720, t=0) → m[5] is negative → winding is
       inverted in clip space. Use GL_CW so screen-space CW = front-facing. */
    glFrontFace(GL_CW);

    /* Screen-CW triangle (front-facing with GL_CW → visible) — GREEN */
    float p1[] = { QX,QY+QH,0, QX+QW/2.f,QY,0, QX+QW,QY+QH,0 };
    float c1[] = { 0,1,0,1, 0,1,0,1, 0,1,0,1 };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, p1);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, c1);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* Screen-CCW triangle (back-facing with GL_CW → culled) — RED */
    float p2[] = { QX,QY,0, QX+QW/2.f,QY+QH,0, QX+QW,QY,0 };
    float c2[] = { 1,0,0,1, 1,0,0,1, 1,0,0,1 };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, p2);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, c2);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisable(GL_CULL_FACE);
}

/* 13: ColorMask — white quad, red channel only */
static void draw_t13(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
    draw_quad(QX, QY, QW, QH, 1, 1, 1, 1, 0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

/* 14: RGBA texture — checkerboard */
static void draw_t14(void) {
    glUseProgram(s_tprog);
    glUniformMatrix4fv(s_tmvp, 1, GL_FALSE, s_mvp);
    draw_tex_quad(QX, QY, QW, QH, tex_rgba);
}

/* 15: LUMINANCE texture — grayscale gradient */
static void draw_t15(void) {
    glUseProgram(s_tprog);
    glUniformMatrix4fv(s_tmvp, 1, GL_FALSE, s_mvp);
    draw_tex_quad(QX, QY, QW, QH, tex_lum);
}

/* 16: Multiple draw calls — RED, GREEN, BLUE side by side */
static void draw_t16(void) {
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    float w3 = (QW - 20) / 3.f;
    draw_quad(QX,             QY, w3, QH, 1,0,0,1, 0);
    draw_quad(QX + w3 + 10,   QY, w3, QH, 0,1,0,1, 0);
    draw_quad(QX + 2*(w3+10), QY, w3, QH, 0,0,1,1, 0);
}

/* 17: Bulk Texture Load — show 4 bulk textures with distinct colors */
static void draw_t17(void) {
    glUseProgram(s_tprog);
    glUniformMatrix4fv(s_tmvp, 1, GL_FALSE, s_mvp);
    /* 2x2 grid of bulk textures: RED, GREEN, BLUE, YELLOW */
    float hw = QW / 2.f - 5;
    float hh = QH / 2.f - 5;
    draw_tex_quad(QX,          QY,          hw, hh, tex_bulk[0]); /* RED */
    draw_tex_quad(QX + hw + 10, QY,          hw, hh, tex_bulk[1]); /* GREEN */
    draw_tex_quad(QX,          QY + hh + 10, hw, hh, tex_bulk[2]); /* BLUE */
    draw_tex_quad(QX + hw + 10, QY + hh + 10, hw, hh, tex_bulk[3]); /* YELLOW */
}

/* 18: GL_RGB Texture — gradient pattern (cinematic format) */
static void draw_t18(void) {
    glUseProgram(s_tprog);
    glUniformMatrix4fv(s_tmvp, 1, GL_FALSE, s_mvp);
    draw_tex_quad(QX, QY, QW, QH, tex_rgb);
}

/* 19: Cinematic Frame Update — glTexSubImage2D every frame */
static void draw_t19(void) {
    /* Generate a new "video frame" each render — smooth scrolling gradient.
     * Mimics RoQ cinematic: 256x256 GL_RGB, glTexSubImage2D every frame. */
    unsigned char rgb[256 * 256 * 3];
    int scroll = cin_frame * 2;  /* slow smooth scroll */
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int i = (y * 256 + x) * 3;
            /* Scrolling color gradient — smooth, no flashing */
            rgb[i+0] = (unsigned char)((x + scroll) & 0xFF);
            rgb[i+1] = (unsigned char)((y + scroll / 2) & 0xFF);
            rgb[i+2] = (unsigned char)(128 + (scroll & 0x7F));
        }
    cin_frame++;

    /* Update the existing GL_RGB texture (like cinematic frame update) */
    glBindTexture(GL_TEXTURE_2D, tex_rgb);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256,
                    GL_RGB, GL_UNSIGNED_BYTE, rgb);

    /* Draw fullscreen like cinematic */
    glUseProgram(s_tcprog);
    glUniformMatrix4fv(s_tcmvp, 1, GL_FALSE, s_mvp);
    glUniform4f(s_tccolor, 1.0f, 1.0f, 1.0f, 1.0f);
    draw_tex_quad(0, 0, SCR_W, SCR_H, tex_rgb);
}

/* 20: Multi-Program Per Frame — switch programs mid-frame like Q3 menu */
static void draw_t20(void) {
    float w3 = (QW - 20) / 3.f;

    /* Program 1: Color shader — red quad */
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    draw_quad(QX, QY, w3, QH, 1, 0, 0, 1, 0);

    /* Program 2: Texture shader — checkerboard */
    glUseProgram(s_tprog);
    glUniformMatrix4fv(s_tmvp, 1, GL_FALSE, s_mvp);
    draw_tex_quad(QX + w3 + 10, QY, w3, QH, tex_rgba);

    /* Program 3: TexColor shader — texture tinted green */
    glUseProgram(s_tcprog);
    glUniformMatrix4fv(s_tcmvp, 1, GL_FALSE, s_mvp);
    glUniform4f(s_tccolor, 0.0f, 1.0f, 0.0f, 1.0f);
    draw_tex_quad(QX + 2 * (w3 + 10), QY, w3, QH, tex_rgba);
}

/* 21: Full Q3 Frame — background + textured quad + blended overlay */
static void draw_t21(void) {
    /* Step 1: Draw textured background (like cinematic or world) */
    glUseProgram(s_tcprog);
    glUniformMatrix4fv(s_tcmvp, 1, GL_FALSE, s_mvp);
    glUniform4f(s_tccolor, 1.0f, 1.0f, 1.0f, 1.0f);
    draw_tex_quad(0, 0, SCR_W, SCR_H, tex_rgb);

    /* Step 2: Update a small portion with glTexSubImage2D (like lightmap/dynamic) */
    unsigned char patch[64 * 64 * 3];
    for (int i = 0; i < 64 * 64 * 3; i += 3) {
        patch[i] = 255; patch[i+1] = 255; patch[i+2] = 0; /* yellow */
    }
    glBindTexture(GL_TEXTURE_2D, tex_rgb);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 96, 96, 64, 64,
                    GL_RGB, GL_UNSIGNED_BYTE, patch);

    /* Redraw background with updated texture */
    draw_tex_quad(0, 0, SCR_W, SCR_H, tex_rgb);

    /* Step 3: Switch to color shader for UI overlay (like Q3 menu text) */
    glUseProgram(s_cprog);
    glUniformMatrix4fv(s_cmvp, 1, GL_FALSE, s_mvp);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* Semi-transparent dark bar across bottom (like Q3 console) */
    draw_quad(0, SCR_H - 150, SCR_W, 150, 0, 0, 0, 0.7f, 0);
    /* "Menu items" — colored rectangles */
    draw_quad(100, SCR_H - 130, 200, 40, 1, 1, 1, 0.9f, 0);
    draw_quad(100, SCR_H - 80,  200, 40, 0.8f, 0.8f, 0.2f, 0.9f, 0);
    glDisable(GL_BLEND);
}

/* ================================================================
 * Main
 * ================================================================ */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    initNxLink();

    printf("\n========================================\n");
    printf("  Spearmint GL Pattern Test v4\n");
    printf("  21 interactive tests\n");
    printf("========================================\n\n");

    if (!initEgl()) { printf("FATAL: EGL init failed!\n"); goto done; }
    printf("GL: %s\n", (const char*)glGetString(GL_VERSION));
    printf("Renderer: %s\n", (const char*)glGetString(GL_RENDERER));

    /* Create 3 shader programs */
    printf("\nCreating shaders...\n");
    s_cprog  = make_program(color_vs,    color_fs,    "attr_Position", 0, "attr_Color", 1);
    s_gprog  = make_program(color_vs,    color_fs,    "attr_Position", 0, "attr_Color", 5);
    s_tprog  = make_program(tex_vs,      tex_fs,      "attr_Position", 0, "attr_TexCoord", 1);
    s_tcprog = make_program(texcolor_vs, texcolor_fs, "attr_Position", 0, "attr_TexCoord", 1);

    s_cmvp    = glGetUniformLocation(s_cprog,  "u_mvp");
    s_gmvp    = glGetUniformLocation(s_gprog,  "u_mvp");
    s_tmvp    = glGetUniformLocation(s_tprog,  "u_mvp");
    s_tcmvp   = glGetUniformLocation(s_tcprog, "u_mvp");
    s_tccolor = glGetUniformLocation(s_tcprog, "u_color");
    printf("  color    prog=%u mvp=%d\n", s_cprog, s_cmvp);
    printf("  gap      prog=%u mvp=%d\n", s_gprog, s_gmvp);
    printf("  tex      prog=%u mvp=%d\n", s_tprog, s_tmvp);
    printf("  texcolor prog=%u mvp=%d color=%d\n", s_tcprog, s_tcmvp, s_tccolor);

    if (s_cmvp < 0 || s_gmvp < 0 || s_tmvp < 0 || s_tcmvp < 0)
        printf("  WARNING: MVP uniform not found in one or more programs!\n");

    /* Ortho: origin top-left, Z range [-1,1] for depth tests */
    mat4_ortho(s_mvp, 0, SCR_W, SCR_H, 0, -1, 1);

    init_resources();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);

    printf("\n========================================\n");
    printf("  A = PASS, B = FAIL, + = Abort\n");
    printf("========================================\n");

    /* --- A. Basic Rendering --- */
    reset_state();
    run_test("Client Array DrawArrays",
             "A WHITE quad on dark gray background", draw_t1);

    reset_state();
    run_test("Client Array DrawElements",
             "An ORANGE quad on dark gray background", draw_t2);

    reset_state();
    run_test("VBO Interleaved + EBO",
             "A RED quad on dark gray background", draw_t3);

    reset_state();
    run_test("VBO Non-Interleaved + EBO",
             "A GREEN quad (spearmint tess buffer pattern)", draw_t4);

    reset_state();
    run_test("EBO FirstIndex",
             "A BLUE quad (draws 2nd quad from index buffer)", draw_t5);

    reset_state();
    run_test("Dynamic VBO Orphaning",
             "A YELLOW quad (orphan+subdata each frame)", draw_t6);

    /* --- B. Vertex Formats --- */
    reset_state();
    run_test("GL_UNSIGNED_SHORT Color",
             "A CYAN quad (ushort normalized vertex color)", draw_t7);

    reset_state();
    run_test("Non-Contiguous Attribs",
             "A MAGENTA quad (attr 0=pos, 5=color, 1-4 disabled)", draw_t8);

    /* --- C. GL State --- */
    reset_state();
    run_test("Alpha Blending",
             "GREEN border, brownish-RED center (semi-transparent red over green)", draw_t9);

    reset_state();
    run_test("Depth Test",
             "BLUE on left, RED on right+overlap (red is closer)", draw_t10);

    reset_state();
    run_test("Scissor Test",
             "LEFT HALF of screen is WHITE, right half is dark gray", draw_t11);

    reset_state();
    run_test("CullFace",
             "GREEN triangle ONLY (red triangle is culled/invisible)", draw_t12);

    reset_state();
    run_test("ColorMask",
             "RED quad on BLACK background (white drawn with red-only mask)", draw_t13);

    /* --- D. Textures --- */
    reset_state();
    run_test("RGBA Texture",
             "Checkerboard: RED and WHITE squares", draw_t14);

    reset_state();
    run_test("LUMINANCE Texture",
             "Grayscale gradient (dark to bright blocks)", draw_t15);

    /* --- E. Complex --- */
    reset_state();
    run_test("Multiple Draw Calls",
             "Three quads side by side: RED, GREEN, BLUE", draw_t16);

    /* --- F. Spearmint Cinematic / Menu --- */
    reset_state();
    run_test("Bulk Texture Load (100 textures)",
             "2x2 grid: top-left RED, top-right GREEN, bottom-left BLUE, bottom-right YELLOW", draw_t17);

    reset_state();
    run_test("GL_RGB Texture (cinematic format)",
             "Gradient quad: red/green gradient with blue tint", draw_t18);

    reset_state();
    run_test("Cinematic Frame Update (SubImage per frame)",
             "SMOOTH scrolling gradient filling screen (colors shift slowly)", draw_t19);

    reset_state();
    run_test("Multi-Program Per Frame (menu pattern)",
             "3 quads: RED (color shader), CHECKER (tex), GREEN-TINTED CHECKER (texcolor)", draw_t20);

    reset_state();
    run_test("Full Q3 Frame (texture + overlay + blend)",
             "Gradient background with YELLOW patch, dark bar + white/yellow rects at bottom", draw_t21);

    /* --- Summary --- */
    printf("\n========================================\n");
    printf("  RESULTS: %d PASS, %d FAIL", s_pass, s_fail);
    if (s_abort) printf(" (aborted at test %d)", s_tnum);
    printf("\n  Total: %d / %d\n", s_pass, s_pass + s_fail);
    printf("========================================\n");

    if (!s_abort) {
        printf("\nAll tests done. Press + to exit.\n");
        printf("Screen is GREEN = all pass, RED = failures.\n");
        while (appletMainLoop()) {
            padUpdate(&s_pad);
            if (padGetButtonsDown(&s_pad) & HidNpadButton_Plus) break;
            glClearColor(s_fail == 0 ? 0.f : .5f,
                         s_fail == 0 ? .5f : 0.f, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            eglSwapBuffers(s_dpy, s_srf);
        }
    }

    /* Cleanup */
    glDeleteBuffers(1, &vbo_il);  glDeleteBuffers(1, &ibo_il);
    glDeleteBuffers(1, &vbo_ni);  glDeleteBuffers(1, &ibo_ni);
    glDeleteBuffers(1, &vbo_fi);  glDeleteBuffers(1, &ibo_fi);
    glDeleteBuffers(1, &vbo_dyn); glDeleteBuffers(1, &ibo_dyn);
    glDeleteBuffers(1, &vbo_us);
    glDeleteTextures(1, &tex_rgba);
    glDeleteTextures(1, &tex_lum);
    glDeleteTextures(1, &tex_rgb);
    if (tex_bulk_count > 0) glDeleteTextures(tex_bulk_count, tex_bulk);
    glDeleteProgram(s_cprog);
    glDeleteProgram(s_gprog);
    glDeleteProgram(s_tprog);
    glDeleteProgram(s_tcprog);

done:
    printf("\n=== NORMAL EXIT ===\n");

    if (s_dpy) {
        eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_ctx) eglDestroyContext(s_dpy, s_ctx);
        if (s_srf) eglDestroySurface(s_dpy, s_srf);
        eglTerminate(s_dpy);
    }
    deinitNxLink();
    return 0;
}
