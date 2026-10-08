/*
 * SwitchGLES - CPU cost counters for performance investigations.
 *
 * Compiled in only with -DSGL_PERF_STATS (no cost otherwise). Each phase
 * accumulates CPU ticks (armGetSystemTick, 19.2 MHz) and a call count; every
 * ~2 seconds sgl_perf_report() prints the per-frame averages, a "frame" being
 * one glFlush, glFinish or eglSwapBuffers (GFXBench off-screen flushes once
 * per frame, on-screen swaps once per frame).
 */
#ifndef SGL_PERF_H
#define SGL_PERF_H

#include <stdint.h>

enum {
    SGL_PERF_DRAW,         /* whole glDrawArrays/glDrawElements */
    SGL_PERF_STATE,        /* sgl_prepare_draw: fixed-function state */
    SGL_PERF_PROGRAM,      /* sgl_prepare_draw: program bind + uniforms */
    SGL_PERF_TEXTURES,     /* sgl_prepare_draw: sampler/texture binding */
    SGL_PERF_INDEX_SCAN,   /* glDrawElements: max-index scan (client or EBO) */
    SGL_PERF_ATTRIBS,      /* backend bind_vertex_attribs */
    SGL_PERF_BACKEND_DRAW, /* backend draw_arrays/draw_elements */
    SGL_PERF_INDICES,      /* count only: indices drawn */
    SGL_PERF_UNIFORM,      /* glUniform* calls */
    SGL_PERF_BUFFER,       /* glBufferData/glBufferSubData */
    SGL_PERF_FLUSH_SYNC,   /* dk_flush_sync (submit + WaitIdle) */
    SGL_PERF_SUBMIT_RESET, /* dk_submit_and_reset (whole) */
    SGL_PERF_GLFLUSH,      /* glFlush */
    SGL_PERF_GLFINISH,     /* glFinish */
    SGL_PERF_SWAP,         /* eglSwapBuffers (end frame + present) */
    SGL_PERF_FRAME_START,  /* acquire + slot fence wait */
    SGL_PERF_BARRIER_FULL, /* count only: DkBarrier_Full barriers recorded */
    SGL_PERF_BARRIER_L2,   /* count only: barriers invalidating the whole L2 */
    SGL_PERF_COUNT
};

#ifdef SGL_PERF_STATS
typedef struct {
    uint64_t ticks[SGL_PERF_COUNT];
    uint64_t calls[SGL_PERF_COUNT];
} sgl_perf_t;
extern sgl_perf_t g_sgl_perf;
uint64_t sgl_perf_now(void);
void sgl_perf_frame(void); /* count a frame, print every ~2 s */
#define SGL_PERF_BEGIN(var) uint64_t var = sgl_perf_now()
#define SGL_PERF_END(slot, var)                                                                    \
    do {                                                                                           \
        g_sgl_perf.ticks[slot] += sgl_perf_now() - (var);                                          \
        g_sgl_perf.calls[slot]++;                                                                  \
    } while (0)
#define SGL_PERF_ADD(slot, n) (g_sgl_perf.calls[slot] += (uint64_t)(n))
#define SGL_PERF_FRAME() sgl_perf_frame()
#else
#define SGL_PERF_BEGIN(var) ((void)0)
#define SGL_PERF_END(slot, var) ((void)0)
#define SGL_PERF_ADD(slot, n) ((void)0)
#define SGL_PERF_FRAME() ((void)0)
#endif

#endif /* SGL_PERF_H */
