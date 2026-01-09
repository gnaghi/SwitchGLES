/*
 * SwitchGLES - Shader Cache
 *
 * Caches compiled DKSH binaries on the SD card to avoid
 * re-compiling shaders via libuam on every startup.
 *
 * Cache key: FNV-1a hash of the GLSL 4.60 source (libuam input).
 * Cache location: sdmc:/switch/switchgles/shader_cache/
 */

#ifndef GL_SHADER_CACHE_H
#define GL_SHADER_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Look up a cached DKSH binary for the given GLSL 4.60 source.
 * Returns malloc'd DKSH data and sets *out_size, or NULL on cache miss.
 * Caller must free() the returned pointer. */
void *sgl_shader_cache_lookup(const char *glsl460_source, int stage,
                               size_t *out_size);

/* Store a compiled DKSH binary in the cache. */
void sgl_shader_cache_store(const char *glsl460_source, int stage,
                             const void *dksh_data, size_t dksh_size);

#endif /* GL_SHADER_CACHE_H */
