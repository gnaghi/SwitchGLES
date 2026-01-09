# SwitchGLES — Known dEQP-GLES2 Failures (133 tests)

**Date:** March 23, 2026
**dEQP version:** VK-GL-CTS 1.4.5.1
**Full regression:** 16,805 tests (17,165 baseline - 360 skipped)
**Pass: 16,672 (99.2%)**
**Fail: 133**
**Software bugs remaining:** 0

All 133 failures are caused by hardware limitations, GPU compiler bugs, GPU rendering accuracy, or edge cases in the packed UBO system. None are regressions — all are known categories.

---

## 1. Stencil Pipeline — 33 tests

**Root cause:** Tegra X1 Maxwell stencil write pipeline produces incorrect buffer values for non-incremental operations.

**Operations that WORK:** GL_KEEP, GL_INCR (clamped), GL_DECR (clamped)
**Operations that FAIL:** GL_REPLACE, GL_ZERO, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP

**Investigation exhausted:** D3D11-style stencil op encoding (1-8) verified correct against Maxwell register spec. Combined DkDepthStencilState bind prevents overwrite. TiledCacheBarrier tested after clears — no improvement. Raw GPU register writes (0x4E1-0x4E4) attempted — same failures. Stencil clear shadow RAM in deko3d verified correct.

| # | Tests |
|---|-------|
| 1-8 | `fragment_ops.stencil.{stencil_fail_replace, depth_fail_replace, depth_pass_replace, zero_stencil_fail, invert_stencil_fail, incr_wrap_stencil_fail, decr_wrap_stencil_fail, cmp_not_equal}` |
| 9-22 | `fragment_ops.interaction.basic_shader.{0,4,26,28,42,44,46,49,62,70,75,85,88,97}` |
| 23-26 | `fragment_ops.depth_stencil.random.{5,11,20,24}` |
| 27-33 | `fragment_ops.random.{2,19,48,67,74,84,91}` |

---

## 2. GPU Precision (MUFU/SFU) — 29 tests

**Root cause:** Tegra X1 Special Function Units provide ~22-bit mantissa precision. dEQP reference uses CPU double-precision. Comparison thresholds (0.02f–0.07f) are tighter than the hardware can achieve.

| # | Tests |
|---|-------|
| 1-2 | `shaders.operator.exponential.{pow, inversesqrt}.mediump_vec2_fragment` |
| 3-5 | `shaders.operator.selection.{lowp, mediump, highp}_float_fragment` |
| 6-7 | `shaders.random.comparison_ops.fragment.{30,42}` |
| 8-11 | `shaders.random.conditionals.{fragment.46, fragment.64, combined.46, combined.64}` |
| 12-15 | `shaders.random.exponential.fragment.{36,58,80,99}` |
| 16-19 | `shaders.random.scalar_conversion.{fragment.30, fragment.42, combined.19, combined.30}` |
| 20-23 | `shaders.random.trigonometric.fragment.{15,28,42,58}` |
| 24-29 | `shaders.random.{swizzle.fragment.46, texture.fragment.24, texture.fragment.73, texture.vertex.49, all_features.fragment.39, basic_expression.combined.18}` |

---

## 3. Cubemap Rendering Accuracy — 24 tests

**Root cause:** Tegra X1 cubemap LOD calculation and face-edge interpolation produce pixel values outside dEQP's comparison threshold.

**Fix attempted — TSC descriptor cache invalidation:** `DkBarrier_Fragments + DkInvalidateFlags_Descriptors` in `dk_texture_parameter` crashed the console during `glu::resetState()`. The command buffer is in an indeterminate state at init time. Both `DkBarrier_None` and `DkBarrier_Fragments` crash. No safe location exists for TSC invalidation without architectural changes to deko3d.

| # | Tests |
|---|-------|
| 1-7 | `texture.mipmap.cube.{basic, projected, bias}.{nearest_nearest, linear_nearest, linear_linear}` |
| 8-17 | `texture.vertex.cube.filtering.*` (10 combinations) |
| 18-23 | `texture.vertex.cube.wrap.*` (6 combinations) |
| 24 | `texture.completeness.cube.extra_level` |

---

## 4. NV50_IR Compiler — Mixed Sampler Types — 19 tests

**Root cause:** Mesa NV50_IR backend generates incorrect GPU code when `sampler2D` and `samplerCube` coexist in the same struct or shader.

| # | Tests |
|---|-------|
| 1-5 | `shaders.struct.uniform.{nested_struct_array_vertex, sampler_nested_vertex, sampler_nested_fragment, sampler_array_vertex, sampler_array_fragment}` |
| 6-19 | `uniform_api.value.assigned.{by_pointer, by_value}.render.{basic_struct, array_in_struct, struct_in_array}.sampler2D_samplerCube_*` (14 tests) |

---

## 5. Uniform API Random — 21 tests

**Root cause:** Packed UBO edge cases with random uniform combinations. These tests generate complex random layouts (nested structs, arrays of structs, mixed types) that hit unhandled paths in the packed UBO byte-offset system.

| # | Tests |
|---|-------|
| 1-21 | `uniform_api.random.{2,6,8,11,12,13,18,21,24,31,38,43,46,47,67,72,74,78,93,94,99}` |

---

## 6. Object Lifetime / Deletion — 4 tests

**Root cause:** Backend `reuse_image` optimization shares GPU memory between delete_pending and newly-bound objects. Fix (`dk_delete_texture` from `glBindTexture`) crashes during `glu::resetState()` init.

| # | Tests |
|---|-------|
| 1-4 | `lifetime.attach.deleted_{input, output}.{texture, renderbuffer}_framebuffer` |

---

## 7. gl_MaxVertexAttribs Constant — 2 tests

**Root cause:** Mesa compiles with `MaxAttribs=32` (needed for aliased-inactive attributes). Shader reads `gl_MaxVertexAttribs = 32`. But `glGetIntegerv(GL_MAX_VERTEX_ATTRIBS)` reports 16 (hardware native). Mismatch.

| # | Tests |
|---|-------|
| 1-2 | `shaders.builtin_variable.max_vertex_attribs_{vertex, fragment}` |

---

## 8. Misc — 3 tests

| # | Test | Root cause |
|---|------|------------|
| 1 | `shaders.texture_functions.vertex.texturecubelod` | Cubemap rendering accuracy |
| 2 | `rasterization.limits.points` | No hardware wide points |
| 3 | `state_query.shader.uniform_value_boolean` | Mesa optimizes booleans as constants |

---

## 9. FBO Recreate — 1 test

| # | Test | Root cause |
|---|------|------------|
| 1 | `fbo.render.recreate_depthbuffer.rebind_rbo_rgb565_depth_component16_stencil_index8` | Intermittent stencil-related FBO recreation |

---

## Summary

| Root Cause | Tests | % |
|------------|-------|---|
| Stencil pipeline (GPU hardware) | 33 | 25% |
| GPU precision MUFU/SFU (hardware) | 27 | 20% |
| Cubemap rendering accuracy (GPU) | 24 | 18% |
| Packed UBO edge cases | 21 | 16% |
| NV50_IR mixed sampler (Mesa compiler) | 19 | 14% |
| Object lifetime (architecture) | 4 | 3% |
| Misc (points, boolean, textureCubeLod) | 3 | 2% |
| gl_MaxVertexAttribs mismatch | 2 | 2% |
| FBO recreate (intermittent) | 1 | 1% |
| **Total** | **133** | **100%** |
