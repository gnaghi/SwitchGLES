# Bug Analysis: Black Model Textures in Spearmint

## Problem
3D player models in spearmint (Quake 3) render as correctly-shaped **black silhouettes**.
Models have correct geometry, lighting uniforms are non-zero, shaders are intact.

## Root Cause: Mip Level Dimension Overwrite in `gl_texture.c`

### The Bug
`glTexImage2D` unconditionally updates `tex->width` and `tex->height` for ALL mip levels:

```c
// gl_texture.c line 238-242 (BEFORE FIX)
tex->width = width;      // OVERWRITTEN FOR EVERY MIP LEVEL
tex->height = height;    // OVERWRITTEN FOR EVERY MIP LEVEL
tex->internal_format = internalformat;
```

### How Spearmint Triggers It
Spearmint's `R_CreateImage2` (tr_image.c:2428-2451) pre-allocates ALL mip levels with NULL data:

```c
// spearmint R_CreateImage2
for (i = 0; i < numMips; i++) {
    qglTextureImage2DEXT(image->texnum, GL_TEXTURE_2D, i,
                          internalFormat, mipWidth, mipHeight, 0,
                          GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    mipWidth  = MAX(1, mipWidth  >> 1);
    mipHeight = MAX(1, mipHeight >> 1);
}
```

After this loop, for a 256x256 texture with 9 mip levels:
- Level 0: glTexImage2D(256, 256, NULL) -> tex->width=256, tex->height=256
- Level 1: glTexImage2D(128, 128, NULL) -> tex->width=128, tex->height=128
- ...
- Level 8: glTexImage2D(1, 1, NULL) -> tex->width=1, tex->height=1

**Final state: tex->width=1, tex->height=1**

### The Cascading Failure
When spearmint then uploads actual pixel data via `glTexSubImage2D(level=0, 256x256, data)`:

```c
// gl_texture.c line 299-304 (BEFORE FIX)
if (xoffset + width > tex->width || yoffset + height > tex->height) {
    sgl_set_error(ctx, GL_INVALID_VALUE);  // 0 + 256 > 1 -> REJECTED!
    return;
}
```

**ALL pixel uploads silently fail.** GPU texture memory stays zeroed = BLACK.

## Fix Applied

### Fix 1: `gl_texture.c` glTexImage2D (lines 238-249)
Only update `tex->width`, `tex->height`, `tex->internal_format`, and `tex->target` for level 0:

```c
tex->used = true;
if (level == 0) {
    tex->width = width;
    tex->height = height;
    tex->internal_format = internalformat;
}
```

### Fix 2: `gl_texture.c` glTexSubImage2D bounds check (lines 298-310)
Compute mip-level dimensions for bounds checking instead of using base dimensions:

```c
GLsizei level_w = (GLsizei)tex->width;
GLsizei level_h = (GLsizei)tex->height;
for (GLint l = 0; l < level; l++) {
    level_w = level_w > 1 ? level_w >> 1 : 1;
    level_h = level_h > 1 ? level_h >> 1 : 1;
}
if (xoffset + width > level_w || yoffset + height > level_h) { ... }
```

### Backend: Already Correct
`dk_texture_image_2d` (dk_texture.c:265-328) already handles mip levels correctly:
- Uses `imageView.mipLevelOffset = level` for the DkImageView
- Uploads to the correct mip via DMA
- No changes needed in the backend

## Other Findings (NOT causing the black textures)

### Investigated and Cleared
| Area | Finding |
|------|---------|
| Uniform values | All lighting uniforms non-zero, packed UBO binding correct |
| Shader code | CalcColor function intact, USE_RGBAGEN preserved by transpiler |
| Packed UBO pre-configuration | Valid at link time, shadow buffer copied correctly |
| Sampler binding/remap | Correct mapping from tex_unit to shader_binding |
| DSA fallback chain | GLDSA_* functions call glActiveTexture + glBindTexture correctly |
| eglGetProcAddress | Returns SwitchGLES functions |
| Descriptor timing | Descriptors are metadata, creation before data upload is fine |
| descriptors_bound flag | Correctly forces re-bind after cmdbuf clear |

### Minor Concerns (non-blocking)
- **DSA cubemap caching**: GL_BindMultiTexture caches texnum per TMU, could skip bind for same-texnum cubemap face uploads. Not relevant for 2D textures.
- **sgl_prepare_draw re-applies ALL state**: No dirty flag optimization yet. Works correctly but is wasteful. Not a bug.

## Lesson Learned
**Mip level handling**: GL texture objects track base (level 0) dimensions. The GL layer must only store width/height from level 0. Mip-level-aware bounds checking uses `max(1, base >> level)` formula.
