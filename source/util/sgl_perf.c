/*
 * SwitchGLES - CPU cost counters (see sgl_perf.h). Built only with
 * -DSGL_PERF_STATS.
 */
#include "sgl_perf.h"

#ifdef SGL_PERF_STATS
#include <stdio.h>
#include <string.h>
#include <switch.h>

sgl_perf_t g_sgl_perf;

static uint64_t s_last_report;
static uint64_t s_frames;

static const char *const s_names[SGL_PERF_COUNT] = {
    "draw",   "state",  "program", "textures", "idx_scan", "attribs", "bk_draw",  "indices",
    "uniform", "buffer", "flush_sync", "submit_reset", "glFlush", "glFinish", "swap", "frame_start",
    "bar_full", "bar_l2",
};

uint64_t sgl_perf_now(void) {
    return armGetSystemTick();
}

void sgl_perf_frame(void) {
    uint64_t now = armGetSystemTick();
    if (!s_last_report) {
        s_last_report = now;
        memset(&g_sgl_perf, 0, sizeof(g_sgl_perf));
        return;
    }
    s_frames++;
    uint64_t elapsed = now - s_last_report;
    if (elapsed < 2 * 19200000ull)
        return;

    /* Per-frame averages: time in microseconds, calls per frame. */
    double f = (double)s_frames;
    printf("[PERF] %llu frames, %.2f ms/frame wall |", (unsigned long long)s_frames,
           (double)elapsed / 19200.0 / f);
    for (int i = 0; i < SGL_PERF_COUNT; i++) {
        if (!g_sgl_perf.calls[i])
            continue;
        if (i == SGL_PERF_INDICES || i == SGL_PERF_BARRIER_FULL || i == SGL_PERF_BARRIER_L2)
            printf(" %s=%.0f/f", s_names[i], (double)g_sgl_perf.calls[i] / f);
        else
            printf(" %s=%.3fms(%.0f/f)", s_names[i], (double)g_sgl_perf.ticks[i] / 19200.0 / f,
                   (double)g_sgl_perf.calls[i] / f);
    }
    printf("\n");
    fflush(stdout);
    memset(&g_sgl_perf, 0, sizeof(g_sgl_perf));
    s_frames = 0;
    s_last_report = now;
}
#endif
