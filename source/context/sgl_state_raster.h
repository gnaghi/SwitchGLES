/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * Rasterizer State Class
 */

#ifndef SGL_STATE_RASTER_H
#define SGL_STATE_RASTER_H

#include <GLES2/gl2.h>
#include <stdbool.h>

typedef struct sgl_state_raster {
    bool cull_enabled;
    GLenum cull_mode;
    GLenum front_face;
    float line_width;
    float polygon_offset_factor;
    float polygon_offset_units;
    bool polygon_offset_fill_enabled;
} sgl_state_raster_t;

/* Initialize to GL defaults */
void sgl_state_raster_init(sgl_state_raster_t *state);

/* Update functions - return true if value changed */
bool sgl_state_raster_set_cull_enabled(sgl_state_raster_t *state, bool enabled);
bool sgl_state_raster_set_cull_mode(sgl_state_raster_t *state, GLenum mode);
bool sgl_state_raster_set_front_face(sgl_state_raster_t *state, GLenum mode);

#endif /* SGL_STATE_RASTER_H */
