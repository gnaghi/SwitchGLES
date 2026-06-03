/*
 * SwitchGLES - deko3d Backend - Texture internal helpers
 *
 * Shared between the texture translation units:
 *   - dk_texture.c            (core: alloc, lifecycle, bind, parameters, upload)
 *   - dk_texture_copy.c       (glCopyTexImage2D / glCopyTexSubImage2D)
 *   - dk_texture_compressed.c (glCompressedTexImage2D / SubImage2D)
 *
 * Helpers and macros used by more than one of those files live here. Helpers
 * private to a single file (pixel-format conversion, cubemap upload, texture
 * completeness) stay file-local in dk_texture.c.
 */

#ifndef SGL_DK_TEXTURE_INTERNAL_H
#define SGL_DK_TEXTURE_INTERNAL_H

#include "dk_internal.h"

/* deko3d requires linear buffer row strides to be 32-byte aligned */
#define DK_LINEAR_STRIDE_ALIGNMENT 32

/* Bitmask for all 6 cubemap faces uploaded (bits 0-5) */
#define DK_CUBEMAP_ALL_FACES 0x3F

/* Apply LUMINANCE/ALPHA/etc. channel swizzle to an image view for sampling. */
void dk_apply_format_swizzle(DkImageView *view, GLenum gl_format);

/* Bytes-per-pixel for an uncompressed GL format/type pair. */
uint32_t dk_gl_format_bpp(GLenum gl_format, GLenum gl_type);

/* True if target is one of the 6 GL_TEXTURE_CUBE_MAP_* faces. */
bool dk_is_cubemap_face(GLenum target);

/* Face index 0..5 for a cubemap face target (-1 if not a face). */
int dk_cubemap_face_index(GLenum target);

/* Write a texture's image/sampler descriptor into the GPU descriptor set. */
void dk_write_image_descriptor_to_gpu(dk_backend_data_t *dk, sgl_handle_t handle);
void dk_write_sampler_descriptor_to_gpu(dk_backend_data_t *dk, sgl_handle_t handle);

#endif /* SGL_DK_TEXTURE_INTERNAL_H */
