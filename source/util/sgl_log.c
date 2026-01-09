/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Logging Implementation
 */

#include "sgl_log.h"
#include <stdio.h>
#include <stdarg.h>

/* Log state */
static sgl_log_level_t g_log_level = SGL_LOG_INFO;
static uint32_t g_log_categories = SGL_LOG_CAT_ALL;
static int g_log_initialized = 0;

/* Level names */
static const char *level_names[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

void sgl_log_init(void) {
    g_log_level = SGL_LOG_INFO;
    g_log_categories = SGL_LOG_CAT_ALL;
    g_log_initialized = 1;
}

void sgl_log_set_level(sgl_log_level_t level) {
    g_log_level = level;
}

void sgl_log_set_categories(uint32_t categories) {
    g_log_categories = categories;
}

void sgl_log(sgl_log_level_t level, sgl_log_category_t category,
             const char *fmt, ...) {
    /* Check if this message should be logged */
    if (level < g_log_level) {
        return;
    }

    if (!(g_log_categories & category)) {
        return;
    }

    /* Print level prefix */
    printf("[SGL][%s] ", level_names[level]);

    /* Print message */
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);
}
