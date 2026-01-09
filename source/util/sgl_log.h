/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Logging System
 */

#ifndef SGL_LOG_H
#define SGL_LOG_H

#include <stdint.h>

/* Log levels */
typedef enum {
    SGL_LOG_TRACE = 0,
    SGL_LOG_DEBUG,
    SGL_LOG_INFO,
    SGL_LOG_WARN,
    SGL_LOG_ERROR
} sgl_log_level_t;

/* Log categories (bitmask) */
typedef enum {
    SGL_LOG_CAT_CORE    = (1 << 0),
    SGL_LOG_CAT_STATE   = (1 << 1),
    SGL_LOG_CAT_BUFFER  = (1 << 2),
    SGL_LOG_CAT_TEXTURE = (1 << 3),
    SGL_LOG_CAT_SHADER  = (1 << 4),
    SGL_LOG_CAT_DRAW    = (1 << 5),
    SGL_LOG_CAT_FBO     = (1 << 6),
    SGL_LOG_CAT_UNIFORM = (1 << 7),
    SGL_LOG_CAT_BACKEND = (1 << 8),
    SGL_LOG_CAT_EGL     = (1 << 9),
    SGL_LOG_CAT_VERTEX  = (1 << 10),
    SGL_LOG_CAT_ALL     = 0xFFFFFFFF
} sgl_log_category_t;

/* Initialize logging system */
void sgl_log_init(void);

/* Set minimum log level */
void sgl_log_set_level(sgl_log_level_t level);

/* Set enabled categories (bitmask) */
void sgl_log_set_categories(uint32_t categories);

/* Core logging function */
void sgl_log(sgl_log_level_t level, sgl_log_category_t category,
             const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Convenience macros */
#ifdef SGL_DEBUG

#define SGL_TRACE(cat, fmt, ...) sgl_log(SGL_LOG_TRACE, cat, fmt, ##__VA_ARGS__)
#define SGL_DEBUG_LOG(cat, fmt, ...) sgl_log(SGL_LOG_DEBUG, cat, fmt, ##__VA_ARGS__)
#define SGL_INFO(cat, fmt, ...) sgl_log(SGL_LOG_INFO, cat, fmt, ##__VA_ARGS__)

/* Category-specific trace macros */
#define SGL_TRACE_CORE(fmt, ...) SGL_TRACE(SGL_LOG_CAT_CORE, "[CORE] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_STATE(fmt, ...) SGL_TRACE(SGL_LOG_CAT_STATE, "[STATE] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_BUFFER(fmt, ...) SGL_TRACE(SGL_LOG_CAT_BUFFER, "[BUFFER] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_TEXTURE(fmt, ...) SGL_TRACE(SGL_LOG_CAT_TEXTURE, "[TEXTURE] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_SHADER(fmt, ...) SGL_TRACE(SGL_LOG_CAT_SHADER, "[SHADER] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_DRAW(fmt, ...) SGL_TRACE(SGL_LOG_CAT_DRAW, "[DRAW] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_FBO(fmt, ...) SGL_TRACE(SGL_LOG_CAT_FBO, "[FBO] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_UNIFORM(fmt, ...) SGL_TRACE(SGL_LOG_CAT_UNIFORM, "[UNIFORM] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_BACKEND(fmt, ...) SGL_TRACE(SGL_LOG_CAT_BACKEND, "[BACKEND] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_EGL(fmt, ...) SGL_TRACE(SGL_LOG_CAT_EGL, "[EGL] " fmt, ##__VA_ARGS__)
#define SGL_TRACE_VERTEX(fmt, ...) SGL_TRACE(SGL_LOG_CAT_VERTEX, "[VERTEX] " fmt, ##__VA_ARGS__)

#else

#define SGL_TRACE(cat, fmt, ...)
#define SGL_DEBUG_LOG(cat, fmt, ...)
#define SGL_INFO(cat, fmt, ...)
#define SGL_TRACE_CORE(fmt, ...)
#define SGL_TRACE_STATE(fmt, ...)
#define SGL_TRACE_BUFFER(fmt, ...)
#define SGL_TRACE_TEXTURE(fmt, ...)
#define SGL_TRACE_SHADER(fmt, ...)
#define SGL_TRACE_DRAW(fmt, ...)
#define SGL_TRACE_FBO(fmt, ...)
#define SGL_TRACE_UNIFORM(fmt, ...)
#define SGL_TRACE_BACKEND(fmt, ...)
#define SGL_TRACE_EGL(fmt, ...)
#define SGL_TRACE_VERTEX(fmt, ...)

#endif

/* Always available macros */
#define SGL_WARN(cat, fmt, ...) sgl_log(SGL_LOG_WARN, cat, fmt, ##__VA_ARGS__)
#define SGL_ERROR(cat, fmt, ...) sgl_log(SGL_LOG_ERROR, cat, fmt, ##__VA_ARGS__)

/* Category-specific error macros */
#define SGL_ERROR_CORE(fmt, ...) SGL_ERROR(SGL_LOG_CAT_CORE, "[CORE] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_STATE(fmt, ...) SGL_ERROR(SGL_LOG_CAT_STATE, "[STATE] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_BUFFER(fmt, ...) SGL_ERROR(SGL_LOG_CAT_BUFFER, "[BUFFER] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_TEXTURE(fmt, ...) SGL_ERROR(SGL_LOG_CAT_TEXTURE, "[TEXTURE] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_SHADER(fmt, ...) SGL_ERROR(SGL_LOG_CAT_SHADER, "[SHADER] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_DRAW(fmt, ...) SGL_ERROR(SGL_LOG_CAT_DRAW, "[DRAW] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_FBO(fmt, ...) SGL_ERROR(SGL_LOG_CAT_FBO, "[FBO] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_UNIFORM(fmt, ...) SGL_ERROR(SGL_LOG_CAT_UNIFORM, "[UNIFORM] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_BACKEND(fmt, ...) SGL_ERROR(SGL_LOG_CAT_BACKEND, "[BACKEND] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_EGL(fmt, ...) SGL_ERROR(SGL_LOG_CAT_EGL, "[EGL] " fmt, ##__VA_ARGS__)
#define SGL_ERROR_VERTEX(fmt, ...) SGL_ERROR(SGL_LOG_CAT_VERTEX, "[VERTEX] " fmt, ##__VA_ARGS__)

#endif /* SGL_LOG_H */
