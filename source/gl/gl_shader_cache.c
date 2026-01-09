/*
 * SwitchGLES - Shader Cache
 *
 * Caches compiled DKSH shader binaries to SD card so subsequent
 * launches skip the expensive libuam compilation (~160ms per shader).
 *
 * First run:  compile normally + write cache files (~26s + 0.3s write)
 * Next runs:  read cache files (~0.5s total for 161 shaders)
 *
 * Cache files: sdmc:/switch/switchgles/shader_cache/<hash>_<stage>.dksh
 * Each file has a small header for validation (magic, version, source length).
 */

#include "gl_shader_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SGL_CACHE_MAGIC    0x43474C53  /* "SGLC" */
#define SGL_CACHE_VERSION  7
#define SGL_CACHE_DIR      "sdmc:/switch/switchgles/shader_cache"

/* Cache file header (16 bytes) */
typedef struct {
    uint32_t magic;       /* SGL_CACHE_MAGIC */
    uint8_t  version;     /* SGL_CACHE_VERSION */
    uint8_t  stage;       /* 0=vertex, 4=fragment (DkStage values) */
    uint16_t reserved;
    uint32_t source_len;  /* Length of the GLSL source (for collision check) */
    uint32_t dksh_size;   /* Size of the DKSH binary data following the header */
} sgl_cache_header_t;

/* FNV-1a hash — fast, no dependencies, good distribution */
static uint32_t sgl_hash_fnv1a(const char *str) {
    uint32_t hash = 0x811C9DC5;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 0x01000193;
    }
    return hash;
}

static void sgl_cache_path(uint32_t hash, int stage, char *buf, size_t buf_size) {
    snprintf(buf, buf_size, SGL_CACHE_DIR "/%08x_%s.dksh",
             hash, (stage == 0) ? "vert" : "frag");
}

static void ensure_cache_dir(void) {
    mkdir("sdmc:/switch", 0755);
    mkdir("sdmc:/switch/switchgles", 0755);
    mkdir(SGL_CACHE_DIR, 0755);
}

void *sgl_shader_cache_lookup(const char *glsl460_source, int stage,
                               size_t *out_size) {
    if (!glsl460_source || !out_size) return NULL;

    uint32_t hash = sgl_hash_fnv1a(glsl460_source);
    uint32_t source_len = (uint32_t)strlen(glsl460_source);

    char path[256];
    sgl_cache_path(hash, stage, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    /* Read and validate header */
    sgl_cache_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (header.magic != SGL_CACHE_MAGIC ||
        header.version != SGL_CACHE_VERSION ||
        header.stage != (uint8_t)stage ||
        header.source_len != source_len ||
        header.dksh_size == 0) {
        fclose(f);
        return NULL;  /* Stale or invalid cache entry */
    }

    /* Read DKSH binary */
    void *data = malloc(header.dksh_size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, header.dksh_size, f) != header.dksh_size) {
        free(data);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *out_size = header.dksh_size;
    return data;
}

void sgl_shader_cache_store(const char *glsl460_source, int stage,
                             const void *dksh_data, size_t dksh_size) {
    if (!glsl460_source || !dksh_data || dksh_size == 0) return;

    ensure_cache_dir();

    uint32_t hash = sgl_hash_fnv1a(glsl460_source);

    char path[256];
    sgl_cache_path(hash, stage, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) return;  /* SD card full or not mounted — silently skip */

    sgl_cache_header_t header;
    header.magic = SGL_CACHE_MAGIC;
    header.version = SGL_CACHE_VERSION;
    header.stage = (uint8_t)stage;
    header.reserved = 0;
    header.source_len = (uint32_t)strlen(glsl460_source);
    header.dksh_size = (uint32_t)dksh_size;

    fwrite(&header, sizeof(header), 1, f);
    fwrite(dksh_data, 1, dksh_size, f);
    fclose(f);
}
