/*
 * SwitchGLES — Minimal Example: Clear Screen
 *
 * Initializes EGL + OpenGL ES 2.0 and clears the screen with a solid color.
 * This is the simplest possible SwitchGLES program — use it as a template
 * for new projects.
 *
 * Build:  make
 * Deploy: nxlink -a <switch_ip> -s 00_clear_screen.nro
 */

#include <switch.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <math.h>

static EGLDisplay s_display;
static EGLSurface s_surface;
static EGLContext  s_context;

static bool initEGL(void) {
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_display == EGL_NO_DISPLAY)
        return false;

    if (!eglInitialize(s_display, NULL, NULL))
        return false;

    EGLConfig config;
    EGLint num_configs;
    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    if (!eglChooseConfig(s_display, attribs, &config, 1, &num_configs) || num_configs == 0)
        return false;

    s_surface = eglCreateWindowSurface(s_display, config, (EGLNativeWindowType)"", NULL);
    if (s_surface == EGL_NO_SURFACE)
        return false;

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (s_context == EGL_NO_CONTEXT)
        return false;

    eglMakeCurrent(s_display, s_surface, s_surface, s_context);
    return true;
}

static void deinitEGL(void) {
    if (s_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context != EGL_NO_CONTEXT)
            eglDestroyContext(s_display, s_context);
        if (s_surface != EGL_NO_SURFACE)
            eglDestroySurface(s_display, s_surface);
        eglTerminate(s_display);
    }
}

int main(int argc, char *argv[]) {
    /* Initialize console for debug output (optional — remove for pure GL apps) */
    consoleInit(NULL);
    printf("SwitchGLES — Clear Screen Example\n");
    printf("Press + to exit\n\n");
    consoleUpdate(NULL);

    /* Initialize EGL + GL ES 2.0 */
    if (!initEGL()) {
        printf("EGL initialization failed!\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&(PadState){0});
        }
        consoleExit(NULL);
        return 1;
    }

    printf("GL_VENDOR:   %s\n", glGetString(GL_VENDOR));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));
    consoleUpdate(NULL);

    /* Configure input */
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    /* Main render loop */
    float time = 0.0f;

    while (appletMainLoop()) {
        /* Input */
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus)
            break;

        /* Animate clear color: smooth RGB cycle */
        float r = 0.5f + 0.5f * sinf(time);
        float g = 0.5f + 0.5f * sinf(time + 2.094f);  /* +120 degrees */
        float b = 0.5f + 0.5f * sinf(time + 4.189f);  /* +240 degrees */

        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        eglSwapBuffers(s_display, s_surface);
        time += 0.016f;  /* ~60 FPS */
    }

    /* Cleanup */
    deinitEGL();
    consoleExit(NULL);
    return 0;
}
