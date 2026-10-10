# GLES 3.0 support plan

Status: October 2026, branch `gles3-groundwork`. This document lists what OpenGL ES 3.0
needs, what SwitchGLES already has, how each missing piece maps onto deko3d and uam, and
the order to build it in small steps that can each be checked with dEQP-GLES3.

Line references are taken from the working trees on that date:
`SGL/` = this repository, `DK/` = `DekoGL/deko3d` (fork), `UAM/` = `DekoGL/uam`,
`CTS/` = `DekoGL/VK-GL-CTS`. `DK/h:N` means `DK/include/deko3d.h` line N.

---

## 1. Ground rules

1. **ES 2.0 behaviour does not change.** Each ES 3.0 entry point or enum checks
   `sgl_ctx_is_es3(ctx)` (`SGL/source/context/sgl_context.h`). In an ES 2.0 context an
   ES 3.0 function sets `GL_INVALID_OPERATION`, the same as Mesa's dispatch for a function
   the context does not support, and an ES 3.0-only `pname` stays `GL_INVALID_ENUM`. Code
   shared with ES 2.0 (draw, texture upload, uniforms, FBO binding) can only be changed in
   steps that are followed by a full dEQP-GLES2 regression.
2. **ES 3.0 contexts are opt-in until the end.** The default build only creates 2.0
   contexts. `make ES3_CONTEXT=1` (`-DSGL_ENABLE_ES3_CONTEXT`) does three things:
   - adds `EGL_OPENGL_ES3_BIT` to both configs;
   - advertises `EGL_KHR_create_context`;
   - lets `eglCreateContext` accept version 3.0.

   `EGL_CONFORMANT` never includes the ES3 bit. The flag goes away once dEQP-GLES3 is
   close to the GLES2 pass rate.
3. **No faked features.** An entry point that is not implemented lives in
   `SGL/source/gl/gl_es3_stubs.c`. It sets `GL_INVALID_OPERATION`, logs its first call
   and returns a neutral value. When a step implements a function, the function leaves
   that file. No ES 3.0 function, implemented or not, is listed by `eglGetProcAddress`
   before step 15: engines that probe entry points (Spearmint, SDL) would otherwise see
   ES 3.0 functions in their ES 2.0 contexts. dEQP-GLES3 links them directly (§7).
4. **`GL_VERSION` stays "OpenGL ES 2.0"** and `GL_SHADING_LANGUAGE_VERSION` stays
   "GLSL ES 1.00", even in an ES 3.0 context, until the last step (§6, step 15).
   dEQP-GLES3 does not parse `GL_VERSION`: it relies on the context type it asked EGL
   for (see §7). So the tests can run long before the strings change.

## 2. What exists today

Before this branch:

- GLES3 headers are present (`SGL/include/GLES3/gl3.h`), and the sized-format constants
  are used throughout.
- `glBlitFramebuffer` (`SGL/source/gl/gl_framebuffer.c:1213`) uses the 2D engine and
  blits the color attachment only. It is **partial**: there is no ES 3.0 validation
  (mask/filter combinations, depth/stencil blits, format matching).
- `glRenderbufferStorageMultisample` (`gl_framebuffer.c:1255`) ignores `samples` and
  falls back to single-sample storage. This is a **fake**; it is fixed in step 12.
- `glGetStringi` returned `""` and `GL_NUM_EXTENSIONS` was 0. Spearmint relies on this
  (`SGL/source/gl/gl_query.c`), and ES 2.0 contexts keep that behaviour.
- `GL_ARB_framebuffer_object`, BGRA, the ETC2/EAC, ASTC and S3TC compressed formats
  (`dk_utils.c:221`) and half-float textures are present through ES 2.0 extensions.
- **Runtime GLSL**: `#version 300 es` sources already reach Mesa through
  `sgl_compile_glsl460` (`gl_shader.c:200`, selected at `gl_shader.c:701`), and Mesa
  accepts them (§5.1). That path does not collect uniform metadata, so a 300 es shader
  with plain uniforms cannot be driven through `glUniform*` today.

This branch adds:

| Commit | Content |
|---|---|
| `91694c4` | Opt-in ES 3.0 context path (`ES3_CONTEXT=1`), `sgl_ctx_is_es3()` |
| `04894c8` | `glGetStringi` and `GL_NUM_EXTENSIONS` for ES 3.0 contexts; the extension list is now a table and `glGetString(GL_EXTENSIONS)` is unchanged (byte-identical) |
| `db00d52` | Fence sync objects (`glFenceSync` … `glGetSynciv`) implemented on `DkFence` |
| `6a762a8` | The other 95 ES 3.0 entry points are `GL_INVALID_OPERATION` stubs, so all 104 functions of `gl3.h` link |
| `19e0752` | `glGetInteger64v`, `GL_MAX_ELEMENT_INDEX`, `GL_MAX_SERVER_WAIT_TIMEOUT` (step 2) |
| `83014c1` | `gl_es3_defaults.c`: the GLES 3.0 state dEQP resets between test cases (`gluStateReset.cpp` resetStateES) accepted at its default value, validated, queryable; non-default values are `GL_INVALID_OPERATION` until their feature exists |

Outside this repository (none pushed, none on a master branch):

| Where | Branch / files | Content |
|---|---|---|
| uam | branch `gles3-es300`: `9a7940a`, `44590dc` | Uniform blocks without a binding get bindings 2, 3, …; shaders after GLSL ES 1.00 get 4 draw buffers (§5.2 items 1, 2, 4) |
| deko3d | branch `transform-feedback`: `70316aa`, `9f60ff7`, `6781084` | Transform feedback registers and API (§3.10) |
| deko3d_review | `tests/tfb_tests.c` (not versioned) | Hardware checks of every unverified TFB fact, built as `build/tfb_tests_tfb.nro` |
| VK-GL-CTS | `targets/switch-gles3/`, `framework/platform/switch/build_nro_gles3.sh` (new, untracked), build dir `build-switch-gles3/` | dEQP-GLES3 for Switch (§7) |

## 3. Inventory

Status values:
- **done**: implemented and correct.
- **partial**: exists, but has gaps.
- **stub**: present in `gl_es3_stubs.c` only.
- **missing**: not present at all.

Effort:
- **S**: up to 1 day.
- **M**: 1 to 3 days.
- **L**: 1 to 2 weeks.

Risk is the chance of regressing ES 2.0, or of hitting an unknown hardware or deko3d
problem.

### 3.1 Context, EGL, queries

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| ES 3.0 context creation (`EGL_KHR_create_context`, `EGL_OPENGL_ES3_BIT`) | done (opt-in) | `egl_context.c`, `egl_impl.c` | S / low | – |
| `glGetStringi`, `GL_NUM_EXTENSIONS` | done (ES3 ctx) | `gl_query.c` | S / low | – |
| `GL_MAJOR_VERSION` / `GL_MINOR_VERSION`, `GL_VERSION` "OpenGL ES 3.0", GLSL "3.00" | missing | `gl_query.c`, final step | S / low | everything |
| ES 3.0 limits: `GL_MAX_3D_TEXTURE_SIZE` ≥256, `GL_MAX_ARRAY_TEXTURE_LAYERS` ≥256, `GL_MAX_DRAW_BUFFERS`/`COLOR_ATTACHMENTS` ≥4, `GL_MAX_SAMPLES` ≥4, `GL_MAX_*_UNIFORM_BLOCKS` ≥12, `GL_MAX_UNIFORM_BLOCK_SIZE` ≥16384, `GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS` ≥32, `GL_MAX_VARYING_COMPONENTS` ≥60, `GL_MAX_ELEMENT_INDEX`, `GL_MAX_SERVER_WAIT_TIMEOUT`, `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT` (256 = `DK_UNIFORM_BUF_ALIGNMENT`, DK/h:154), texel offsets −8/7 | missing | `gl_query.c`, gated on `sgl_ctx_is_es3`; each limit is added in the step that makes it true | S each / low | the feature behind each limit |
| `glGetInteger64v`, `glGetIntegeri_v`, `glGetInteger64i_v`, `glGetBufferParameteri64v` | stub | `gl_query.c` | S / low | indexed bindings (UBO, TF) |
| `glGetInternalformativ` (`GL_SAMPLES`, `GL_NUM_SAMPLE_COUNTS`) | stub | new | S / low | MSAA |
| Current context in TLS, shared contexts | missing (single global, `share_context` ignored, `egl_context.c:21`) | – | – | not required by dEQP |

### 3.2 Buffer objects

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| New targets: `COPY_READ/WRITE`, `PIXEL_PACK/UNPACK`, `UNIFORM`, `TRANSFORM_FEEDBACK` | missing: `glBindBuffer` and `glBufferData` accept ARRAY/ELEMENT only (`gl_buffer.c:182, 287`) | Per-target binding in `sgl_context_t`. VBO storage is already target-agnostic (`dk_buffer.c`) | S / **medium** (`glBindBuffer` is shared with ES2: keep the ES2 errors for ES2 contexts) | – |
| `glMapBufferRange`, `glFlushMappedBufferRange`, `glUnmapBuffer`, `glGetBufferPointerv` | stub | Buffers live in CPU-visible `data_memblock`, so mapping returns `get_data_cpu_ptr` (`sgl_backend.h:62`). `MAP_INVALIDATE_*` maps to orphaning (`buffer_data_orphan`). `MAP_UNSYNCHRONIZED` maps directly; otherwise wait for the GPU (`finish`, or a per-buffer fence later). Write ARM `dsb st` on unmap (`dk_flush_cpu_stores`) | M / medium (orphan-path interactions) | targets |
| `glCopyBufferSubData` | stub | `dkCmdBufCopyBuffer` (DK/h:1326, copy engine, 4 MiB chunks) | S / low | targets |
| PBO pack (`glReadPixels` into `PIXEL_PACK_BUFFER`) | missing | `dkCmdBufCopyImageToBuffer` (DK/h:1331). It does **not** convert formats, so do the conversion on the CPU after a wait, or read back into staging as `read_pixels` already does and copy into the buffer | M / low | targets, `glReadBuffer` |
| PBO unpack (`glTex*Image*` from `PIXEL_UNPACK_BUFFER`) | missing | Source pointer = buffer CPU pointer + offset, through the existing staging path | S / low | targets |
| `glBindBufferBase` / `glBindBufferRange` | stub | Indexed UBO and TF binding tables in context state | S / low | targets |

### 3.3 Vertex arrays and draws

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| Vertex array objects | stub | Move `vertex_attribs[]` and `bound_element_buffer` (`sgl_context.h:39,49`) into an `sgl_vao_t`, with a default VAO 0 per context. The draw path reads through a pointer. **Touches every ES2 draw**, so it needs the dEQP-GLES2 regression and a GFXBench/spearmint check | M / **high** | – |
| `glVertexAttribDivisor`, `glDrawArraysInstanced`, `glDrawElementsInstanced` | stub | `DkVtxBufferState.divisor` (DK/h:1121-1125; today always 0, `dk_draw.c:167,280`), and the `instanceCount` argument of `dkCmdBufDraw`/`DrawIndexed` (DK/h:1318-1321; today 1, `dk_draw.c:446,591`). The divisor applies **per vertex buffer**, so an attribute with a divisor needs its own buffer slot. Client-array instanced attributes must be sized `ceil(instances/divisor)` | M / medium (shared draw path) | VAO preferred, not required |
| `glDrawRangeElements` | stub | `glDrawElements` plus a range check | S / low | – |
| Integer attributes: `glVertexAttribIPointer`, `glVertexAttribI4*`, `glGetVertexAttribI*` | stub | `DkVtxAttribState` with `DkVtxAttribType_Sint/Uint` (DK/h:1102-1103) | S / low | VAO (to store the "integer" flag) |
| New vertex types: `GL_HALF_FLOAT`, `GL_INT_2_10_10_10_REV`, `GL_UNSIGNED_INT_2_10_10_10_REV` | partial (only `GL_HALF_FLOAT_OES`, `dk_utils.c:576`) | `dk_utils.c` format table | S / low | – |
| Primitive restart (`GL_PRIMITIVE_RESTART_FIXED_INDEX`) | missing | `dkCmdBufSetPrimitiveRestart(enable, index)` (DK/h:1307). deko3d has no "fixed index" mode, so set 0xFF/0xFFFF/0xFFFFFFFF per draw from the index type. uint8 indices are converted to uint16 today (`dk_draw.c`), so 0xFF must become 0xFFFF in that conversion | S / medium | – |
| `GL_RASTERIZER_DISCARD` | missing | `DkRasterizerState.rasterizerEnable = 0` (DK/h:806), recorded with the rasterizer group like line width (`apply_raster`). ES 3.0 also discards `glClear*` while it is enabled | S / low (new field in `sgl_raster_state_t`) | – |
| Uint32 indices without the OES extension, `GL_MAX_ELEMENT_INDEX` | done (`GL_OES_element_index_uint`) | – | – | – |

### 3.4 Textures and samplers

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| `GL_TEXTURE_3D`, `GL_TEXTURE_2D_ARRAY` targets and bindings | missing (`glBindTexture` accepts 2D/CUBE only, `gl_texture.c:337`) | `DkImageType_3D`/`_2DArray` (DK/h:357-371); `DkImageLayoutMaker.dimensions[2]` = depth or layers (DK/h:562-576). Per-unit binding arrays as for cube maps (`sgl_context.h:40-41`) | M / medium (`glBindTexture` and the sampler binding at draw are shared) | – |
| `glTexImage3D`, `glTexSubImage3D`, `glCompressedTex(Sub)Image3D`, `glCopyTexSubImage3D` | stub | `dkCmdBufCopyBufferToImage` with `DkImageRect.z/depth` (DK/h:1330, DK/source/dk_image.cpp:800-806). **Gotcha:** `DkCopyBuf.rowLength` is in bytes and `imageHeight` is a byte layer stride (dk_image.cpp:730,734). Copy into a layer: blit/copy with `z` | M / medium | 3D/array targets |
| Immutable storage `glTexStorage2D/3D`, `GL_TEXTURE_IMMUTABLE_FORMAT/LEVELS` | stub | Allocate the full mip chain once, so it also simplifies completeness | S / low | sized formats |
| `GL_TEXTURE_BASE_LEVEL` / `MAX_LEVEL` | missing (`glTexParameteri` handles MIN/MAG/WRAP_S/T only, `gl_texture.c:654-718`) | `DkImageView.mipLevelOffset/mipLevelCount` (DK/h:592-603, tic_generate.cpp:160-174). This is a view change, so the image descriptor must be rewritten | S / medium (completeness rules change) | – |
| `GL_TEXTURE_MIN_LOD` / `MAX_LOD`, `GL_TEXTURE_WRAP_R` | missing | `DkSampler.lodClampMin/Max` (clamped to 0..15, tsc_generate.cpp:66-92) and `wrapMode[2]` (DK/h:665-685) | S / low | – |
| Depth compare: `GL_TEXTURE_COMPARE_MODE/FUNC`, `sampler2DShadow`, `samplerCubeShadow`, `sampler2DArrayShadow` | missing | `DkSampler.compareEnable/compareOp` (DK/h:646, tsc_generate.cpp:74-75). Depth textures need a depth image format, but today every depth format becomes `Z24S8` (`dk_utils.c:164-168`) | M / medium | sized depth formats |
| Texture swizzle `GL_TEXTURE_SWIZZLE_R/G/B/A` | missing | `DkImageView.swizzle[4]`, already used for LUMINANCE/ALPHA. Compose the user swizzle with the format swizzle. Depth formats ignore it and use `dsSource` (tic_generate.cpp:47-56) | S / low | – |
| Sampler objects (`glGenSamplers` … `glGetSamplerParameter*`) | stub | Sampler state is part of the texture today (`sgl_texture_t`, `sgl_gl_types.h:300`) and of a TSC entry per texture. Sampler objects need their own TSC entries, picked per unit at bind time: **this touches the texture bind path** | M / **high** | – |
| Pixel store: `UNPACK_ROW_LENGTH`, `UNPACK_IMAGE_HEIGHT`, `UNPACK_SKIP_*`, `PACK_ROW_LENGTH`, `PACK_SKIP_*` | missing (only the alignments, `sgl_context.h:56-57`) | CPU staging copy with a row pitch | S / low | – |
| `glGenerateMipmap` for arrays/3D | missing | Per-layer blit loop (cube maps already do this per face) | S / low | 3D/array |
| NPOT complete-wrap rules, LOD in vertex shaders | done (ES2 + NPOT ext) | – | – | – |

### 3.5 Formats (ES 3.0 Table 3.13, renderbuffer Table 4.5)

| Item | Status | deko3d (DK/h:392-526) | Effort / risk | Notes |
|---|---|---|---|---|
| Sized unorm: R8, RG8, RGB8, RGBA8, RGB565, RGBA4, RGB5_A1 | partial: unsized format/type only, via `dk_convert_format` (`dk_utils.c:162-211`) | `R8/RG8/RGBA8_Unorm`, `RGB565`, `RGB5A1`. RGB8 → RGBX8 | M / medium (the format function is shared with ES2) | **`RGBA4_Unorm` cannot be rendered to** (format_traits.inc row 48), so back it with RGBA8 and convert on upload/readback |
| Snorm: R8_SNORM … RGBA8_SNORM | missing | `*_Snorm` | S / low | not renderable in ES 3.0 |
| sRGB: SRGB8, SRGB8_ALPHA8 (+ sRGB writes on render targets) | missing | `RGBA8_Unorm_sRGB`, `RGBX8_Unorm_sRGB` (DK/h:440-441), renderable | S / low | `GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING` |
| Integer: R8I … RGBA32UI | missing | `*_Uint/Sint`, renderable; RGB variants → `RGBX*` (DK/h:466-477) | M / low | integer samplers in uam (§5) |
| RGB10_A2, RGB10_A2UI | missing | `RGB10A2_Unorm/Uint` (DK/h:446-447) | S / low | |
| R11F_G11F_B10F, RGB9_E5 | missing | `RG11B10_Float` (renderable), `E5BGR9_Float` (not renderable, which is correct) | S / low | |
| Float: R16F … RGBA32F (textures; rendering needs `EXT_color_buffer_float`) | partial (RGBA16F through the OES half-float path) | `*16_Float`, `*32_Float` | S / low | |
| Depth: DEPTH_COMPONENT16/24/32F, DEPTH24_STENCIL8, DEPTH32F_STENCIL8 | partial (everything → Z24S8) | `Z16`, `Z24X8`, `Z24S8`, `ZF32`, `ZF32_X24S8` (DK/h:434-439) | M / medium (renderbuffers and FBO depth are shared) | |
| ETC2/EAC (mandatory in ES 3.0) | done (`dk_utils.c:284-302`) | – | – | the extension list could drop the OES/EXT names in ES3 contexts |
| Internal format queries (`glGetInternalformativ`) | stub | – | S | MSAA |

### 3.6 Framebuffers

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| Multiple render targets (`GL_COLOR_ATTACHMENT1..3`, `glDrawBuffers`) | missing: `sgl_framebuffer_t` has one color attachment (`sgl_gl_types.h:322`), only `COLOR_ATTACHMENT0` is accepted (`gl_framebuffer.c:325,367,616`), and `GL_MAX_DRAW_BUFFERS = 1` (`gl_query.c:168`) | `dkCmdBufBindRenderTargets(views[], n, depth)` (DK/h:1279, at most 8 = `DK_MAX_RENDER_TARGETS`, DK/h:161). **NULL entries are not allowed** (gpu_3d_base.cpp:241-252), so remap `GL_NONE` holes with a write mask. Per-target write masks `dkColorWriteStateSetMask` (DK/h:929-943); per-target blend `dkCmdBufBindBlendStates` (DK/h:1284), independent blend always on | L / **high** (FBO binding, clears, blend and color mask are all shared) | uam `MaxDrawBuffers` (§5) |
| `glReadBuffer` | stub | read source selection for `ReadPixels` / `CopyTex*` / `Blit` | S / low | MRT |
| `glClearBuffer{iv,uiv,fv,fi}` | stub | `dkCmdBufClearColor(targetId, mask, data)` (DK/h:1313) clears one target with raw float/int data; `dkCmdBufClearDepthStencil` | S / low | MRT, integer formats |
| `glFramebufferTextureLayer` | stub | `DkImageView.layerOffset/layerCount = 1` as the render target. **To verify on hardware:** for array views, the image descriptor appears to apply `layerOffset` twice (tic_generate.cpp:114-115 and :182-184). That affects sampling, not render targets | S / medium | 3D/array |
| Separate read/draw bindings | partial (fields exist, `sgl_context.h:44-45`) | completeness per target | S / low | – |
| `glInvalidateFramebuffer` / `glInvalidateSubFramebuffer` | stub | Validation, then no-op (allowed: it is a hint), or `dkCmdBufDiscardColor/DepthStencil` (DK/h:1315-1316) for a perf win | S / low | – |
| `glBlitFramebuffer` ES 3.0 rules | partial | Add validation (§4.3.3 errors). Depth/stencil blits work through the 2D engine with NEAREST (`ZF32`→R32F, `Z24S8`→BGRA8 surface formats, dk_image.cpp:552-660). Int/depth formats lack `CanUse2DFilter` | M / medium | MSAA for resolve |
| Multisample renderbuffers + resolve blit, `GL_MAX_SAMPLES` ≥ 4 (4x real MSAA, decided Oct 2026) | **fake** today (`gl_framebuffer.c:1255` ignores `samples`) | See "MSAA design" below | L / medium | sized formats, MRT |
| Default framebuffer MSAA (`EGL_SAMPLES`) | missing | swapchain images with msMode, plus a resolve at swap | M / medium | MSAA |

#### MSAA design (step 12b)

The user asked for real multisampling: `GL_MAX_SAMPLES` = 4 (ES 3.0 minimum) and
`GL_SAMPLES`/`GL_NUM_SAMPLE_COUNTS` from `glGetInternalformativ` listing 4 (and 2, both
supported by the hardware; 8 possible later).

- **Storage.** `glRenderbufferStorageMultisample(samples > 0)` rounds `samples` up to
  2 or 4 (ES 3.0 §4.4.2.1: at least the requested count, `GL_INVALID_OPERATION` above
  `GL_MAX_SAMPLES` for the format) and creates a `DkImageType_2DMS` image (DK/h:365)
  with `DkImageLayoutMaker.msMode = DkMsMode_2x/4x` (DK/h:540-543, field at DK/h:568),
  flags `DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine` (DK/h:384), so it can be
  both rendered to and resolved. deko3d lays out 2DMS images itself
  (DK/source/dk_image.cpp:257, 299-302). Depth/stencil renderbuffers use the same
  `msMode` with a depth format. `samples = 0` keeps the current single-sample path, so
  `glRenderbufferStorage` and ES 2.0 are unchanged.
- **Rendering.** An FBO is multisampled when its attachments are (they must all have the
  same sample count, else `GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE`). Binding it records
  `dkCmdBufBindMultisampleState` (DK/h:1281) with `DkMultisampleState.mode =
  rasterizerMode = DkMsMode_4x` (DK/h:840-866) and the standard sample locations; binding
  a single-sample target records the 1x state again. `GL_SAMPLE_COVERAGE` and
  `GL_SAMPLE_ALPHA_TO_COVERAGE`, which are stored but unused today (`sgl_context.h:60-66`),
  become real: `dkCmdBufSetSampleMask` (DK/h:1301) for the coverage mask and the alpha-to-
  coverage field (`alphaToCoverageEnable`, DK/h:844). `GL_SAMPLE_BUFFERS`/`GL_SAMPLES` queries report the bound FBO.
- **Resolve.** `glBlitFramebuffer` from a multisampled read FBO to a single-sample draw
  FBO (ES 3.0 §4.3.3: same rect, no scaling, `GL_INVALID_OPERATION` otherwise):
  `dkCmdBufResolveImage(src, dst)` (DK/h:1329) when the rect covers the whole image
  (the call has no rect; it checks a multisampled source, a single-sample destination,
  non-layered images of the same size, DK/source/dk_image.cpp:662-691). For a sub-rect:
  `dkCmdBufBlitImage`, which reads a multisampled source by scaling the sample
  coordinates (DK/source/dk_image.cpp:602-616), with `DkBlitFlag_FilterNearest`. To be
  checked on hardware: whether that blit averages the samples like a resolve or takes
  one; if it takes one, resolve the whole image into a temporary image, then blit the
  rect.
- **Default framebuffer.** `EGL_SAMPLES = 4` configs need multisampled swapchain-side
  render targets plus a resolve at `eglSwapBuffers` into the presented image. That is a
  separate sub-step (not needed by dEQP-GLES3, which renders to FBOs for multisample
  tests and to the default framebuffer with whatever config it gets).
- **Sampling** a multisampled image is ES 3.1 (`sampler2DMS`), not needed; deko3d would
  use `dkImageDescriptorInitialize(..., decayMS)` (DK/h:1360).
- **Validation:** dEQP-GLES3 `functional.multisample.fbo_4_samples.*`,
  `functional.fbo.msaa.*`, `functional.blit.*` resolve cases,
  `functional.state_query.internal_format.*`.

### 3.7 Programs, uniforms, uniform blocks

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| ES 3.00 shader compile path with metadata | partial: `#version 300 es` goes through `sgl_compile_glsl460` (no metadata, `gl_shader.c:200`) | Route `300 es` through `sgl_compile_es100_mesa` (`gl_shader.c:358`), the path that collects uniforms/samplers/inputs/varyings, and skip `glslt_validate_es100` (ES 1.00-only rules). Mesa already validates ES 3.00 itself (es_shader) | M / low (new branch in `glCompileShader`) | uam work (§5) |
| Unsigned uniforms `glUniform*ui*`, `glGetUniformuiv` | stub | Packed UBO write path (`gl_uniform.c`). Type mapping: uam `base_type` 0 (uint) is currently folded to `GL_INT` (`gl_shader.c:334-341`), so it needs the real `GL_UNSIGNED_INT*` types in ES3 contexts | S / low | 300 es path |
| Non-square matrices `glUniformMatrix{2x3,3x2,2x4,4x2,3x4,4x3}fv` | stub | Same writer as `glUniformMatrix*` with std140 column padding. Type mapping in `uam_base_type_to_gl` only knows square matrices (`gl_shader.c:299-305`) | S / low | 300 es path |
| `glUniformMatrix*` with `transpose = GL_TRUE` (allowed in ES 3.0) | missing (ES2 requires `GL_FALSE`) | gate the error on context version | S / low | – |
| Uniform blocks: `glGetUniformBlockIndex`, `glGetActiveUniformBlock*`, `glGetUniformIndices`, `glGetActiveUniformsiv`, `glUniformBlockBinding` | stub | Reflection from uam (missing, §5). At draw: GL binding point → (buffer, offset, size) → `dkCmdBufBindUniformBuffer(stage, slot, addr, size)` (DK/h:1273). User UBO slot N maps to hardware c[N+2] (DK/source/cmd_bind_common.cpp:199). Slots 0-1 per stage are SwitchGLES's packed UBOs (`SGL_MAX_PACKED_UBOS`, `sgl_gl_types.h:65`), so blocks go to slots 2..13 (12 blocks ≥ the ES 3.0 minimum; 16 slots per stage, DK/h:150). Max 64 KiB per block (DK/h:155) | L / medium (`bind_program` is shared) | uam blocks API, indexed bindings |
| `glGetFragDataLocation` | stub | needs fragment output reflection (uam, §5) | S / low | uam |
| `glGetProgramBinary`, `glProgramBinary`, `glProgramParameteri`, `GL_NUM_PROGRAM_BINARY_FORMATS = 0` | stub | Legal minimum: zero binary formats, so `glProgramBinary` always fails link with `GL_INVALID_ENUM` on the format. A real binary can come later (DKSH + metadata blob, like `.refl`) | S / low | – |
| `glGetActiveUniform` types for ES3 (`GL_UNSIGNED_INT*`, `GL_SAMPLER_3D`, `GL_SAMPLER_2D_ARRAY`, `*_SHADOW`, `GL_INT_SAMPLER_*`, `GL_UNSIGNED_INT_SAMPLER_*`) | missing | uam sampler info only reports 2D/cube (UAM/source/glsl_frontend.cpp:923-925) | S / low | uam |

### 3.8 Queries

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| Query objects `glGenQueries` … `glGetQueryObjectuiv` | stub | `dkCmdBufReportCounter(DkCounter_SamplesPassed, addr)` (DK/h:1332, 16-byte aligned, 16-byte report = u64 value + u64 timestamp, Primer.md:581) at Begin and End, with result = end − begin != 0. A query memory block in the backend; availability by fence or `dkCmdBufReportValue` marker. **Gotcha:** resetting the Timestamp counter resets SamplesPassed (DK/source/dk_variable.cpp:284-285), so never reset counters, use differences | M / low | – |
| `GL_ANY_SAMPLES_PASSED_CONSERVATIVE` | stub | same as `ANY_SAMPLES_PASSED` (exact is allowed) | – | – |
| `GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN` | stub | `DkCounter_TransformFeedbackPrimitivesWritten` exists (stream 0) | S | transform feedback |

### 3.9 Sync objects

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| `glFenceSync`, `glIsSync`, `glDeleteSync`, `glClientWaitSync`, `glWaitSync`, `glGetSynciv` | **done** (this branch) | `SGL/source/gl/gl_sync.c`, `dk_fence_sync`/`dk_wait_sync` (`dk_command.c`). Each sync owns a `DkFence`. The signal is submitted at once because deko3d fills the fence only at submit (DK/source/dk_queue.cpp:227-252) and an unsubmitted (Empty) fence reads as signaled (DK/source/dk_fence.cpp:9-11). `dkFenceWait` takes µs in an s32 (dk_fence.cpp:62-71), so timeouts over ~35 min wait without limit | done | – |
| `GL_MAX_SERVER_WAIT_TIMEOUT` | missing | `glGetInteger64v` → 0 (server waits ignore the timeout anyway) | S | 64-bit queries |
| EGL fence sync (`EGL_KHR_fence_sync`) | missing | could reuse the same backend ops | S / low | – |

### 3.10 Transform feedback

| Item | Status | Where / mechanism | Effort / risk | Deps |
|---|---|---|---|---|
| `glTransformFeedbackVaryings`, `glBegin/End/Pause/ResumeTransformFeedback`, TF objects, `GL_RASTERIZER_DISCARD` | stub | deko3d branch `transform-feedback` (below). In SwitchGLES: varying name → hardware slot `0x20 + 4·location + component` (`DK_TFB_SLOT_GENERIC`) from uam's varying reflection (`uam_get_varying_info` gives the location of each VS output), `gl_Position` → `DK_TFB_SLOT_POSITION`; interleaved = one layout, separate = one layout per buffer | M (GL) / **high** until the hardware test passes | deko3d branch, indexed bindings, queries |

#### Transform feedback in deko3d (branch `transform-feedback`, decided Oct 2026)

Done on the branch, following `deko3d_review/N1-transform-feedback-design.md`. Register
offsets were checked against envytools rnndb `graph/gf100_3d.xml` (TFB stripe `0x0380`,
`TFB_STREAM` `0x0700`, `TFB_ENABLE` `0x0744`, `TFB_VARYING_LOCS` `0x2800`, `DRAW_TFB_*`,
`QUERY_GET` `STREAM` bits 5..7), the same values as the N1 table.

- `70316aa` engine_3d.def: `TfbBuffer[4]` (Enable, Addr, Bytes, Offset), `TfbLayout[4]`
  (Stream, VaryingCount, Stride), `TfbEnable`, `TfbVaryingLocs[128]`, `DrawTfbBase/Stride/
  Bytes`, `SetReportSemaphore.Index` (bits 5..7).
- `9f60ff7` API (`deko3d.h`): `DkTransformFeedbackLayout`, `DK_TFB_SLOT_*`,
  `dkCmdBufBindTransformFeedbackLayouts`, `...Buffers`, `dkCmdBufBegin/EndTransformFeedback`,
  `dkCmdBufSaveTransformFeedbackOffsets` (WaitForIdle + one `TransformFeedbackOffset`
  report per buffer + full barrier), `dkCmdBufResumeTransformFeedback` and
  `dkCmdBufDrawTransformFeedback` (offsets read by the GPU through gpfifo data entries,
  like the indirect draws), in `DK/source/maxwell/gpu_3d_tfb.cpp`.
- `6781084` `dkCmdBufDrawTransformFeedback` clears `DrawArraysFirst` first.

Hardware test: `deko3d_review/tests/tfb_tests.c` → `deko3d_review/build/tfb_tests_tfb.nro`
(linked with the branch's `libdeko3dd.a` copied to `build/tfb/`, shaders compiled at
start-up by uam, no romfs). Each test prints PASS/FAIL; `--only <name>` runs one. What it
must confirm:

| Test | Fact to confirm | Used by |
|---|---|---|
| `basic` | TfbBuffer/TfbLayout/TfbVaryingLocs/TfbEnable offsets; slots `0x1C+c` (gl_Position) and `0x20+c` (output location 0); nothing written past the capture | everything |
| `slots` | slot `0x20 + 4N + c` for locations 1 and 3, any component order | varying mapping in SwitchGLES |
| `skip` | `0xFF` leaves the word untouched (informational, ES 3.0 does not need it) | – |
| `report` | which word of the 16-byte `TransformFeedbackOffset` report holds the offset (deko3d assumes word 1 = byte 4, as nouveau; Primer.md says u64 at byte 0) | Resume, DrawTransformFeedback, `s_tfbOffsetWord` |
| `index` | `SetReportSemaphore` Index bits select the buffer; two buffers captured at once | GL_SEPARATE_ATTRIBS, Save |
| `resume` | `TfbBuffer::Offset` loaded from the saved report by a gpfifo entry; capture continues after pause | glPause/ResumeTransformFeedback |
| `end` | `TfbEnable = 0` stops the capture | glEndTransformFeedback |
| `overflow` | no write past `TfbBuffer::Bytes`; reported offset and primitives written for a full buffer (values printed) | GL overflow checks |
| `counters` | `TransformFeedbackPrimitivesWritten` / `PrimitivesGenerated` count the captured points (word 0 of the report) | GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_PRIMITIVES_GENERATED (ES 3.2) |
| `discard` | capture with `rasterizerEnable = 0`, nothing rasterized | GL_RASTERIZER_DISCARD |
| `draw_tfb` | `DrawTfbBase/Stride/Bytes`: vertex count = saved offset / stride (runs after a draw with a non-zero first vertex) | ES 3.1 / GL 4 draw-from-TF (not ES 3.0), but proves the gpfifo path |
| `align4` | buffer address aligned to 4 bytes only works | `DK_TRANSFORM_FEEDBACK_BUF_ALIGNMENT` (glBindBufferRange offset alignment) |
| `triangles` | vertex order of captured triangles and lines | GL capture order |

If `report` finds the offset in word 0, change `s_tfbOffsetWord` in `gpu_3d_tfb.cpp`; the
`report` and `index` checks then need the same word.

Transform feedback is the only ES 3.0 feature the stack cannot provide today. Every
other item can land without it. dEQP-GLES3 `functional.transform_feedback.*` (and a few
`negative_api` cases) will fail until it exists, so `GL_VERSION` 3.0 is only claimed
once it is done (§6, step 14).

### 3.11 Miscellaneous state

| Item | Status | Mechanism | Effort / risk |
|---|---|---|---|
| `GL_FRAGMENT_SHADER_DERIVATIVE_HINT` | done (OES_standard_derivatives) | – | – |
| `glGetVertexAttrib*(GL_VERTEX_ATTRIB_ARRAY_INTEGER/DIVISOR)` | missing | VAO state | S |
| `GL_PRIMITIVE_RESTART_FIXED_INDEX` in `glEnable`/`glIsEnabled` | missing | §3.3 | S |
| `GL_COPY_READ/WRITE_BUFFER_BINDING`, `GL_*_BUFFER_BINDING` queries | missing | §3.2 | S |
| `GL_READ_BUFFER`, `GL_DRAW_BUFFERi` queries | missing | §3.6 | S |

## 4. deko3d: facts and gotchas that matter

- **Uniform buffer slot mapping:**
  - c[0] is the driver constbuf (DK/source/driver_constbuf.h:20-27).
  - c[1] holds the DKSH constants.
  - User UBO N goes to c[N+2] (cmd_bind_common.cpp:199).
  - Limits: 16 per stage (DK/h:150), 256-byte alignment (DK/h:154), 64 KiB maximum
    (DK/h:155).
- **Render targets:** at most 8 (DK/h:161), and a NULL slot is not allowed.
- **Blit and resolve:**
  - `dkCmdBufBlitImage` needs `DkImageFlags_Usage2DEngine` on both images
    (dk_image.cpp:144) and rejects 3D images (:359).
  - `dkCmdBufResolveImage` resolves the whole image only.
- **Copies and readback:**
  - `dkCmdBufCopyImageToBuffer` does not convert formats.
  - `DkCopyBuf.rowLength` and `imageHeight` are byte strides.
- **Formats:**
  - `RGBA4_Unorm` cannot be rendered to.
  - `RGB32_*` cannot be rendered to (correct for GL).
- **Fences:**
  - A fence is written at submit time; an Empty fence waits as signaled.
  - Timeouts are whole µs held in an s32.
- **Counters:** resetting `Timestamp` resets SamplesPassed (dk_variable.cpp:284-285).
- **To verify on hardware:** array image descriptors may apply `layerOffset` twice
  (tic_generate.cpp:114-115, 182-184).
- **Already known (CLAUDE.md):**
  - barrier call-site crashes (`memory/feedback_barrier_crash.md`);
  - the blend.dst register workaround;
  - ARM `dsb st` after CPU writes.

  New ES 3.0 paths (MRT blend states, buffer mapping, query memory) must follow the same
  rules.

## 5. uam: what ES 3.00 shaders need

### 5.1 What works already

- uam's Mesa context is `API_OPENGL_CORE` with `GLSLVersion = 460`
  (UAM/source/glsl_frontend.cpp:396,438,242), and it enables `ARB_ES3_compatibility`
  (UAM/mesa-imported/glsl/standalone_scaffolding.cpp:300-302).
- As a result, `#version 300 es` is in the supported-version list
  (UAM/mesa-imported/glsl/glsl_parser_extras.cpp:239-256) and compiles today, with ES
  semantics (`es_shader`, :417).
- `layout(location)` on inputs/outputs is accepted (`is_version(330,300)`).
- Integers are native (`NativeIntegers`, glsl_frontend.cpp:243).
- Vertex input reflection reports `base_type` (glsl_frontend.cpp:665-689).
- Sampler auto-binding covers every sampler type (glsl_frontend.cpp:575-612).

### 5.2 What is missing

All of these are uam changes, made in the uam repository. None was committed by this
branch.

1. **Uniform blocks without `binding=`.**
   - A fincs edit makes it a link error for a block to have no binding
     (UAM/mesa-imported/glsl/link_uniform_blocks.cpp:279-281).
   - GLSL ES 3.00 cannot write `layout(binding)` (glsl_parser.yy:1740-1744 needs
     420pack / ES 3.10).
   - Fix: auto-assign block bindings in the same loop as samplers (glsl_frontend.cpp:575),
     **starting at 2** to leave SwitchGLES's packed UBO slots 0-1 free. This also solves
     item 2.
2. **Collision with the remapped driver constbuf.**
   - Bare uniforms are remapped CONST[0] → CONST[1] (compiler_iface.cpp:437,
     nv50_ir_from_tgsi.cpp:1867-1868).
   - A block at binding b uses CONST[b+1] (st_glsl_to_tgsi.cpp:2106,2137).
   - So binding 0 aliases the bare uniforms. Bindings ≥ 1 avoid it; ≥ 2 matches the
     SwitchGLES layout.
3. **Reflection API for blocks:**
   - block name, binding, data size, active member count;
   - per member: name, type, offset, array stride, matrix stride, row-major.

   Mesa computes all of it (`_Packing`, link_uniform_blocks.cpp:293), but uam does not
   export it (UAM/source/uam.h:96-116). The `.refl` format (uam.h:198-243) needs a
   matching record type, with a version bump.
4. **Fragment outputs:** reflection of name → location, and `MaxDrawBuffers` /
   `MaxColorAttachments` raised from 1 to 4 (or 8) (glsl_frontend.cpp:286). Today a
   second output fails to link (linker.cpp:2760).
5. **Limits:** `MaxCombinedTextureImageUnits` 16 → 32 (glsl_frontend.cpp:351), to match
   `GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS` ≥ 32.
6. **Sampler types:** `uam_sampler_info_t.type` is only 2D/cube
   (glsl_frontend.cpp:923-925). Report the dimension, shadow, array and
   int/uint/float.
7. **Varyings:** interpolation qualifiers (flat/centroid) are not reported. Stages
   compile separately (`SeparateShader`, glsl_frontend.cpp:544), so a stage interface
   mismatch (type, qualifier) is never detected. dEQP-GLES3 has link-error tests for
   this, so SwitchGLES must compare the two stages' varying lists at
   `glLinkProgram` time.
8. **Transform feedback varyings:** nothing to do for VS capture (N1 design doc). Only
   the varying → slot reflection is needed, and `uam_get_varying_info` already gives the
   locations.

Items 1, 2 and 4 are done on uam branch `gles3-es300` (not merged, not pushed):

- `9a7940a`: unbound uniform blocks get sequential bindings starting at 2 (in the loop
  that numbers unbound samplers; the members of an unnamed block share one binding, an
  instance array takes one per element). This fixes item 2 too, as bindings ≥ 1 never
  alias the remapped driver constbuf, and 0-1 stay free for SwitchGLES's packed UBOs.
- `44590dc`: `MaxDrawBuffers`/`MaxColorAttachments` set per shader before compiling: 1
  for GLSL ES 1.00 (so `gl_MaxDrawBuffers` stays 1, as `GL_MAX_DRAW_BUFFERS` in ES 2.0
  contexts), 4 for every other version.

Neither changes an ES 1.00 shader (no blocks, limit unchanged). Until step 12, an ES 3.00
shader sees `gl_MaxDrawBuffers` = 4 while `GL_MAX_DRAW_BUFFERS` reports 1 (known
mismatch, fixed by MRT). Built with meson in a worktree; not run on hardware. Items 3 and
6 are API additions, M effort. All need a uam rebuild and a SwitchGLES relink
(`-lSwitchGLES` bundles `libuam.a`).

## 6. Implementation order

Each step is one or a few commits. It is validated on device with dEQP-GLES3 (groups
below) built per §7, and, when marked **[ES2]**, also with the full dEQP-GLES2 regression
(16,691 pass / 129 known fails, Oct 8 baseline). Group names are from
`CTS/android/cts/main/gles3-main-2026-03-01.txt`.

| # | Step | Touches ES2 paths | dEQP-GLES3 groups to check |
|---|---|---|---|
| 0 | **Done.** ES3 context opt-in, `glGetStringi`, sync objects, stubs | no (only the `glGetString(GL_EXTENSIONS)` refactor, byte-identical) | `info.*`, `functional.fence_sync.*`, `functional.negative_api.*` sync cases, `functional.state_query.integers.num_extensions*` |
| 1 | **Built, to run on the console.** dEQP-GLES3 for Switch (CTS side, §7) | no | the run itself: tests start and report, no crash; `info.*`, `functional.prerequisite.*`, `functional.fence_sync.*` |
| 2 | **Done** (`19e0752`): `glGetInteger64v`, `GL_MAX_ELEMENT_INDEX`, `GL_MAX_SERVER_WAIT_TIMEOUT`. `glGetIntegeri_v`/`glGetInteger64i_v` stay stubs until indexed bindings exist (steps 11, 14); `GL_MAJOR/MINOR_VERSION` come with step 15 | no (ES3-gated `pname`s in shared `glGetIntegerv`/`glGetFloatv`) | `functional.state_query.integers64.*` (`max_element_index`, `max_server_wait_timeout`), `functional.state_query.integers.*` for already-supported states |
| 3 | **Done** (`375d58c`, branch `gles3-step3`): buffer targets, `glCopyBufferSubData`, `glMapBufferRange`/`Unmap`/`Flush`, PBO pack and unpack | **[ES2]** `glBindBuffer`/`glBufferData` target switch | `functional.buffer.*`, `functional.state_query.buffers.*` |
| 4 | **Done** (`2aed634`): ES 3.00 shader compile path (route `300 es` to the Mesa metadata path), uint and non-square matrix uniforms, `transpose = GL_TRUE` | **[ES2]** `glCompileShader` dispatch, `uam_base_type_to_gl` | `functional.shaders.*` (constants, operators, conversions, swizzles…), `functional.uniform_api.*` (value.* without blocks) |
| 5 | `glDrawRangeElements` **done** (`25f91a8`, plus the GLES 3.0 limits the reference context queries, `38bf9db`); new vertex types and integer attributes **done** (`9b9e9c3`); primitive restart to do | **[ES2]** draw path | `functional.primitive_restart.*`, `functional.draw.draw_range_elements*`, `functional.vertex_arrays.*` |
| 6 | **Done, moved before step 3** (`bee8632`; the buffer tests verify through a VAO): vertex array objects, implemented by swapping the context state at bind, so the draw path is untouched | **[ES2]** every draw; also GFXBench + spearmint | `functional.vertex_array_objects.*`, `functional.state_query` VAO bindings |
| 7 | Instancing + divisor | **[ES2]** draw path | `functional.instanced.*`, `functional.draw.*instanced*` |
| 8 | (a) **Done** (`fd4a0dd`): sized formats of Table 3.2 for `glTexImage2D`/`glTexSubImage2D`, sRGB, integer, float, snorm, packed float, `DEPTH_COMPONENT32F`; `glTexStorage2D` + immutable queries. Refused with `GL_INVALID_OPERATION` + log (no wrong texels): uploads needing a conversion (float to half, to R11F_G11F_B10F/RGB9_E5, 2_10_10_10 to RGB5_A1), depth data other than 32F (Z24S8 layout unverified), `glCopyTexSubImage2D` into non-RGBA8-backed sized formats, compressed `glTexStorage2D`. Still to do: sized `glCopyTexImage2D`, sized renderbuffers, SRGB8_ALPHA8 and float/integer color-renderability, ES3 `glGenerateMipmap` format rules. (b) texture params (base/max level, LOD, wrap R, swizzle, compare) | **[ES2]** `dk_convert_format`, texture param path | `functional.texture.format.*`, `functional.texture.swizzle.*`, `functional.texture.shadow.*`, `functional.texture.mipmap.*`, `functional.state_query.texture.*` |
| 9 | 3D and 2D array textures, `glTexStorage*`, pixel store, `glFramebufferTextureLayer` | **[ES2]** `glBindTexture`, texture binding at draw | `functional.texture.specification.*`, `functional.texture.filtering.3d/2d_array.*`, `functional.texture.vertex.*` |
| 10 | Sampler objects | **[ES2]** texture binding at draw | `functional.samplers.*`, `functional.state_query.sampler.*` |
| 11 | uam blocks API (§5.2 items 1-3), then uniform blocks in SwitchGLES | **[ES2]** `bind_program` | `functional.ubo.*`, `functional.uniform_api.info_query.*` |
| 12 | (a) MRT (uam `MaxDrawBuffers`: done on the uam branch), `glDrawBuffers`, `glReadBuffer`, `glClearBuffer*`, `glInvalidate*`, `glBlitFramebuffer` validation, depth/stencil blits; (b) real 4x MSAA renderbuffers + resolve (§3.6 "MSAA design") | **[ES2]** FBO bind, clear, blend, color mask | `functional.fbo.*`, `functional.draw_buffers*`, `functional.fragment_out.*`, `functional.blit.*`, `functional.multisample.*`, `functional.color_clear.*` |
| 13 | Query objects (occlusion) | no | `functional.occlusion_query.*` |
| 14 | Transform feedback: deko3d side **done on the fork branch, waiting for `tfb_tests` on the console** (§3.10); then the GL side and `GL_RASTERIZER_DISCARD` | no (new paths) | `functional.transform_feedback.*`, `functional.rasterizer_discard.*` |
| 15 | Flip the version: `GL_VERSION` "OpenGL ES 3.0", GLSL "OpenGL ES GLSL ES 3.00", `GL_MAJOR_VERSION` 3, remove the `ES3_CONTEXT` flag (ES3 contexts by default, `EGL_OPENGL_ES3_BIT` on configs), program binary formats = 0, revisit the extension list for ES3 contexts | yes: default EGL now advertises ES3 | full dEQP-GLES3 + full dEQP-GLES2 + GFXBench es2/es3 + spearmint |

Steps 2, 3, 13 can be done in parallel with the uam work (step 11 prerequisite). Steps 5
and 7 are independent of the texture work (8-10). Steps 6, 7 and 12 are the riskiest for
ES2 because they rework shared draw and FBO code: schedule each with a full GLES2
regression and a GFXBench/spearmint smoke test.

## 7. dEQP-GLES3 on Switch

Done (step 1), without modifying any existing VK-GL-CTS file or the `build-switch`
directory used for dEQP-GLES2:

- `CTS/targets/switch-gles3/switch-gles3.cmake` (new): includes `targets/switch`, sets
  `DEQP_GLES3_LIBRARIES` to `${SWITCHGLES_PATH}/lib/libSwitchGLES.a` (defines
  `DEQP_GLES3_DIRECT_LINK`, so `glw::initES30Direct` takes the address of every ES 2.0
  + 3.0 function: the link succeeds, all 104 ES 3.0 symbols exist), and generates the
  Switch `main()` from `framework/platform/switch/tcuSwitchMain.cpp` with
  `dEQP-GLES3`/`deqp-gles3` and SD names `gles3_caselist_NNN.txt` /
  `gles3_TestResults*.qpa`, so a GLES3 run never overwrites the GLES2 files.
  `switch-toolchain.cmake` there includes the switch one.
- Build directory `CTS/build-switch-gles3/`, configured with:
  ```
  cmake .. -G "Unix Makefiles" -DCMAKE_MAKE_PROGRAM=/c/devkitPro/msys2/usr/bin/make.exe
        -DDEQP_TARGET=switch-gles3 -DDEQP_TARGET_TOOLCHAIN=switch-toolchain
        -DCMAKE_BUILD_TYPE=Release -DSWITCHGLES_PATH=<SwitchGLES tree with ES3_CONTEXT=1 lib>
        -DSELECTED_BUILD_TARGETS=deqp-gles3 -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
        -DDEQP_DISABLE_VK_VIDEO_TESTS=ON -DZLIB_INCLUDE_DIR=/c/devkitPro/portlibs/switch/include
        -DCMAKE_DEPENDS_USE_COMPILER=OFF -DCCACHE_EXECUTABLE=OFF
  make -j8 deqp-gles3
  ```
  Gotchas: without `CMAKE_MAKE_PROGRAM` CMake picks Strawberry's gmake, the compiler ABI
  check fails and `DE_PTR_SIZE` becomes 4 (`#error DE_CPU and DE_PTR_SIZE mismatch`);
  without `CCACHE_EXECUTABLE=OFF` jsoncpp finds Strawberry's ccache, which fails without
  `USERPROFILE`.
- `SWITCHGLES_PATH` points to the `gles3-groundwork` worktree, whose `lib/` holds a
  `make ES3_CONTEXT=1` build linked with the uam `gles3-es300` libuam. The library
  under test in the main checkout (`switchGLES/lib`) is not used.
- `CTS/framework/platform/switch/build_nro_gles3.sh` (new) and `romfs-gles3/` (GLES3
  data + caselist): `build_nro_gles3.sh [caselist.txt]`. The first caselist is
  `gles3_smoke.txt` = `{dEQP-GLES3{info,functional{prerequisite,fence_sync}}}`.
  Output: `CTS/build-switch-gles3/modules/gles3/deqp-gles3.nro`.
- Not done yet: `manage_tests.py` still knows only GLES2 (baseline
  `gles3-main.txt` from `CTS/external/openglcts/data/gl_cts/data/mustpass/gles/aosp_mustpass/3.2.2.x/`,
  `dEQP-GLES3` prefix, separate `lists/gles3/`); until then caselists are written by
  hand, one trie line per file. dEQP only matches **leaf case names**: a caselist
  must list the cases themselves (taken from `CTS/android/cts/main/gles3-main-*.txt`,
  e.g. with `manage_tests.flat_to_trie`), not group names.
- Do **not** advertise `EGL_KHR_get_all_proc_addresses`: it would make
  `egluGLContextFactory.cpp:425-473` load every function through `eglGetProcAddress`,
  for dEQP-GLES2 too.

The context reports `GL_VERSION` 2.0 at this stage. dEQP-GLES3 only logs it
(`dEQP-GLES3.info.version`) and does not parse it. Version-dependent checks use the
context type requested from EGL (`CTS/modules/gles3/tes3Context.hpp:49`,
`tes3Context.cpp:45`). Tests that call a stubbed function get `GL_INVALID_OPERATION`
from it and fail cleanly. A stub never returns a null object that the test could
dereference.

## 8. Decisions (Oct 10, 2026) and open questions

Decided by the user:

1. `GL_VERSION` stays "OpenGL ES 2.0" until step 15.
2. GFXBench es3 losing its own `[gl-stub]` report is fine (SwitchGLES's first-call
   warnings replace it).
3. uam changes go on a uam branch (`gles3-es300`), merged later.
4. Transform feedback is wanted: deko3d side on fork branch `transform-feedback`,
   hardware facts checked by `tfb_tests` on the console.
5. MSAA must be real (4x), not an error (§3.6 "MSAA design", step 12b).

Open:

1. `dkCmdBufBlitImage` from a multisampled source: averaged or single sample? Decides
   the sub-rect resolve path (§3.6).
2. `tfb_tests` results decide `s_tfbOffsetWord` and whether `DK_TFB_SLOT_SKIP` can be
   documented as working.
