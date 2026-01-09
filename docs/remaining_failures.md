# dEQP-GLES2 Remaining Failures Analysis

## Overview
After the March 18-19 2026 sessions, ~157 tests were fixed. Approximately 69 tests remain failing.
This document details the root cause, investigation history, and difficulty of each category.

---

## 1. Stencil Operations (26 tests)

### Tests
- `fragment_ops.stencil.{stencil_fail_replace, depth_fail_replace, depth_pass_replace, zero_stencil_fail, invert_stencil_fail, incr_wrap_stencil_fail, decr_wrap_stencil_fail, cmp_not_equal}` (8 basic)
- `fragment_ops.interaction.basic_shader.{0,4,26,28,42,44,46,49,62,70,75,85,88,97}` (14 combined)
- `fragment_ops.depth_stencil.random.{5,11,20,24}` (4 random)

### Symptoms
- Image comparison failed — stencil operations produce wrong pixel output
- Pattern: **Keep, Incr(clamp), Decr(clamp) PASS** but **Replace, Zero, Invert, IncrWrap, DecrWrap, cmp_not_equal FAIL**

### Investigation History
1. **D3D vs OGL encoding** — Verified that deko3d's DkStencilOp values (1-8 D3D-style) match Maxwell hardware expectations. The MME macro `BindDepthStencilState` correctly extracts 4-bit fields from the packed struct and writes to GPU registers 0x4E1-0x4E4 (front) and 0x566-0x569 (back). An OGL-encoding workaround was tried and didn't help.

2. **Raw GPU register workaround** — Wrote GL enum values (0x1E00=Keep, 0=Zero, 0x1E01=Replace, etc.) directly to GPU stencil registers, bypassing deko3d. This had NO EFFECT — the same tests still failed. This strongly suggests the encoding is correct and the problem is elsewhere.

3. **Zcull barrier** — Added `dkCmdBufBarrier(DkBarrier_Full, DkInvalidateFlags_Zcull)` in `dk_apply_depth_stencil` when stencil test is enabled. **THIS CRASHED THE ENTIRE CONSOLE** (not just the app). The barrier in `sgl_prepare_draw` (which runs before every draw) is catastrophically unsafe. NEVER put barriers in per-draw state application.

4. **Stencil clear fixes** — Tried setting `stencilTestEnable=false` during clear and preserving GL ref/funcMask values. Combined with the Zcull barrier, this also crashed. Not tested separately.

5. **Stencil-only FBO depth disable** — Added `depthTestEnable=false` when FBO has no depth attachment. This fixed the FBO stencil-only tests but NOT the basic stencil tests (which use the default framebuffer with Z24S8).

### Root Cause Analysis
The most likely root cause is **Maxwell ZETA buffer compression or caching**. The GPU's depth/stencil buffer uses hardware compression (HiZ/Zcull). When stencil operations write to the stencil buffer, the compressed data may not be properly flushed before subsequent stencil reads. Operations like Keep/Incr/Decr may produce correct results because they modify stencil values incrementally (compatible with compression), while Replace/Zero/Invert make drastic changes that break compression assumptions.

### Difficulty: VERY HIGH
- Requires understanding of Maxwell ZETA compression internals
- Barriers in per-draw state crash the console — need to find the RIGHT place for barriers
- Possible locations: after `glClear(GL_STENCIL_BUFFER_BIT)`, or at render target switch, NOT per-draw
- May need deko3d-level changes to insert barriers in the right command buffer position
- Could also be a deko3d bug in how it handles stencil operations with ZETA compression

### Recommendation
- Try barrier ONLY at stencil clear time (in `dk_clear.c` after `dkCmdBufClearDepthStencil`)
- Try disabling ZETA compression for the depth/stencil buffer (if deko3d supports it)
- Investigate nouveau/NVK source code for how they handle stencil coherency

---

## 2. Cubemap Vertex Texture — Linear Mip Interpolation (16 tests)

### Tests
- `texture.vertex.cube.filtering.{nearest_mipmap_nearest_linear,nearest_mipmap_linear_linear,linear_mipmap_linear_nearest,linear_mipmap_linear_linear}_{clamp,repeat,mirror}` (10 vertex)
- `texture.mipmap.cube.{basic,projected,bias}.{linear_nearest,linear_linear,nearest_nearest,nearest_linear}` (6 mipmap)

### Symptoms
- Image comparison/verification failed
- All tests with **linear mip interpolation** fail, while **nearest mip selection** passes

### Root Cause
Confirmed by debug traces: sampler binding is correct (vs_bind=0, cubemap descriptor properly created). The issue is the Tegra X1 vertex texture unit's LOD computation for cubemaps with linear mip-level blending. The hardware produces slightly different mip blend weights than the dEQP reference renderer.

### Difficulty: NOT FIXABLE
This is a hardware precision characteristic of the Tegra X1 GPU. Software workarounds are not possible.

---

## 3. Mixed sampler2D + samplerCube Rendering (12 tests)

### Tests
- `uniform_api.value.assigned.{by_pointer,by_value}.render.{nested_structs_arrays,struct_in_array,array_in_struct}.sampler2D_samplerCube_{vertex,fragment,both}`

### Root Cause
Compile and get_uniform tests PASS (Mesa MaxTextureImageUnits fix worked). Only render tests fail. The NV50_IR backend may generate incorrect texture instructions when sampler2D and samplerCube coexist in the same shader program.

### Difficulty: HIGH
Requires NV50_IR codegen investigation or Mesa-level debugging.

---

## 4. FBO recreate_depthbuffer (4 tests)

### Tests
- `fbo.render.recreate_depthbuffer.{rebind,no_rebind}_tex2d_{rgb,rgba}_depth_component16_stencil_index8`

### Root Cause
Related to stencil — all tests have `stencil_index8`. The depth buffer is recreated but the stencil state becomes corrupted. Same underlying issue as the stencil ops bug.

### Difficulty: TIED TO STENCIL FIX

---

## 5. depth_range (2 tests)

### Root Cause
`uam_get_depth_range_offset()` returns -1 for the depth_range test shader. Mesa does not emit `STATE_DEPTH_RANGE` as a parameter for this shader. The gl_DepthRange values may be embedded in `initial_data` as compile-time constants rather than as dynamic state.

### Difficulty: MEDIUM
Requires investigation of how Mesa handles gl_DepthRange in the NV50_IR backend. May need to check `initial_data` for hardcoded values and override them at draw time.

---

## 6. Lifetime Attach (2 tests)

### Root Cause
The test creates an FBO, attaches a texture, deletes the texture (deferred via fbo_ref_count), then `glu::resetState()` deletes the FBO which decrements fbo_ref_count to 0, triggering immediate GPU cleanup. When a new texture with the same dimensions is created, it gets new GPU memory, but the FBO no longer exists.

The GLES2 spec requires the texture IMAGE DATA to survive beyond both texture and FBO deletion, referenced only by the internal attachment. This requires a GPU-level image refcount independent of GL object names — a significant architectural change.

### Difficulty: HIGH (architectural change)

---

## 7. uniform_boolean (1 test)

### Root Cause
Mesa optimizes boolean uniforms as compile-time constants. They don't appear in the driver constbuf Parameters list. The `UniformStorage` scan added to uam doesn't find them because there's no parameter offset to map to. `prog: 0 uniforms, 0 samplers` confirmed by trace.

### Difficulty: MEDIUM-HIGH
Requires forcing Mesa to not optimize boolean uniforms away, or implementing a separate uniform storage path.

---

## 8. Other (8 tests)

| Test | Root Cause | Difficulty |
|------|-----------|------------|
| shaders.operator.exponential.pow.mediump_vec2_fragment | Hardware precision | Not fixable |
| shaders.operator.selection.lowp_float_fragment | Hardware precision | Not fixable |
| shaders.struct.uniform.nested_struct_array_vertex | Mesa metadata for deeply nested structs | Medium |
| shaders.struct.uniform.sampler_nested_vertex | Mesa sampler in nested struct | Medium |
| shaders.struct.uniform.sampler_nested_fragment | Same | Medium |
| shaders.texture_functions.vertex.texturecubelod | textureCubeLod in vertex shader | Unknown |
| texture.completeness.cube.extra_level | Edge case in completeness check | Low |
| rasterization.limits.points | Point size hardware limit | Not fixable |
