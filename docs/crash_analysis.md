# dEQP Tests — Console Crash Analysis Report

## Overview

7 dEQP-GLES2 tests crash the entire Nintendo Switch console (not just the application).
This document analyzes the root cause of each crash group and proposes fixes.

---

## Group 1: fragment_ops.interaction.basic_shader.49 and .62

**Tests:** 2 of 100 randomized fragment operation interaction tests.

**What they do:** Generate random shader + random render state (stencil ops, depth func, blend mode, scissor, color mask) from a seed, then draw 25 quads with different random states per draw.

**Seed:** `params.seed = 0x667eacfd ^ deInt32Hash(ndx)` where ndx=49 or 62.

**Why 98/100 pass but these 2 crash:**

The specific seeds produce a state sequence that puts the GPU in an invalid state. The most probable causes:

1. **Blend workaround buffer overflow:** `dk_apply_blend` writes raw GPU commands to the command buffer (register 0x786 workaround). If the cmdbuf is nearly full, the bounds check prevents the write, leaving the blend state incomplete — the GPU gets a wrong dstAlpha factor. With rapid state churn (25 draws × 6+ state changes), the cmdbuf could be nearly full when the workaround runs.

2. **Command buffer exhaustion during state churn:** `sgl_prepare_draw` re-applies ALL state before every draw. With 25 draws, each applying 6+ state commands + shader binds + vertex attrib binds, the cmdbuf could overflow. The `cbAddMem` callback handles this, but if it triggers mid-sequence, the GPU could get a partial/invalid command sequence.

3. **Specific stencil/blend/depth combination:** The random state generator produces stencil references in range -3 to 260 and masks as full uint32. Truncation to uint8 is correct, but a specific combination of ops may trigger a GPU-level bug in Maxwell's fragment processing pipeline.

**Recommended fix:**
- Add cmdbuf space check before the raw blend workaround write
- Flush cmdbuf if <4KB remaining before `sgl_prepare_draw` (not just client_array check)
- Reproduce the exact random state sequence for seeds 49/62 to identify the specific trigger

---

## Group 2: shaders.struct.uniform.sampler_array_vertex/fragment

**Tests:** Shader with `struct S { float a; vec3 b; sampler2D c; }; uniform S s[2];`

**What it does:** Creates 2 textures, binds them to units 0 and 1, sets `s[0].c = unit 1`, `s[1].c = unit 0` via glUniform1i, then renders.

**Why it crashes:**

The crash occurs in the vertex shader variant where the sampler is used in VS. When `vs_shader_binding` is set, `sgl_prepare_draw` calls `bind_texture` for both VS and FS stages. The likely crash vector:

1. Mesa assigns non-sequential or unexpected binding numbers to struct-member samplers
2. If `glGetUniformLocation("s[0].c")` returns the correct sampler index but `glUniform1i(loc, 1)` fails to update `tex_unit` (because the GLES name matching is wrong), both samplers keep `tex_unit=0`
3. The GPU tries to sample from an unbound or misconfigured descriptor slot, causing a GPU hang

The vertex variant crashes because `vs_shader_binding` adds a second binding call to the VS stage, doubling the risk of an invalid descriptor access.

**Recommended fix:**
- Validate `shader_binding` and `vs_shader_binding` are within [0, 15] before `bind_texture`
- Ensure struct member sampler names from Mesa match what `glGetUniformLocation` returns
- Add a guard: if tex_id resolves to an uninitialized descriptor, bind the black fallback instead

---

## Group 3: fbo.completeness.renderable.texture.*.bgra8_ext

**Tests:** Create texture with `glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA8_EXT(0x93A1), ...)`, attach to FBO, check completeness.

**Why it crashes:**

Two issues compound:

1. **GL_BGRA8_EXT not accepted:** `sgl_is_valid_tex_format()` doesn't include GL_BGRA8_EXT (0x93A1). The glTexImage2D validation rejects it with GL_INVALID_VALUE. The texture is never created. This by itself doesn't crash — the test handles GL errors.

2. **BGRA texture as render target:** For the non-EXT test (`GL_BGRA_EXT` = 0x80E1), the texture IS created with `DkImageFormat_BGRA8_Unorm`. When attached to an FBO and the FBO is bound, `dkCmdBufBindRenderTarget` receives a BGRA texture. **BGRA8_Unorm may not be a valid render target format on Tegra X1.** The GPU attempts to render to it and hangs.

The renderbuffer path already maps `GL_BGRA_EXT` to `DkImageFormat_RGBA8_Unorm` to avoid this. The texture path does not.

**Recommended fix:**
- Map `GL_BGRA_EXT` textures to `DkImageFormat_RGBA8_Unorm` with a BGRA→RGBA CPU-side byte swizzle during staging upload
- OR: in `glCheckFramebufferStatus`, report `GL_FRAMEBUFFER_UNSUPPORTED` when a BGRA texture is attached as a render target
- Accept `GL_BGRA8_EXT` as an alias for `GL_BGRA_EXT` in format validation

---

## Group 4: attribute_location.bind_aliasing.max_cond_mat2/3/4 and max_inactive_*

**Tests:** Create shaders with many aliased attributes (conditionally active via `#ifdef`). For mat2/3/4, each attribute consumes multiple location slots.

**Why it crashed (FIXED):**

`GL_MAX_VERTEX_ATTRIBS` was 32 but hardware has 16 native vertex input slots. NV50_IR generated shaders with >16 native inputs. When the GPU executed the shader, it read from non-existent input slots → hardware hang.

**Fix applied:** `GL_MAX_VERTEX_ATTRIBS` now reports 16 (hardware native limit). Internal arrays remain at 32 for aliasing support. With 16 reported:
- mat2: 8 active × 2 slots = 16 → fits
- mat3: 5 active × 3 slots = 15 → fits
- mat4: 4 active × 4 slots = 16 → fits

**Status:** Fix applied but NOT yet verified on hardware. These tests should be re-tested to confirm they no longer crash. If they do, a post-link native-input-count check is needed.

---

## Summary

| Group | Tests | Root Cause | Fix Status |
|-------|-------|-----------|------------|
| 1 | basic_shader.49/.62 | Cmdbuf overflow or blend workaround race during high state churn | **Not fixed** — skip list |
| 2 | sampler_array_vertex/fragment | Invalid descriptor access from wrong struct-member sampler binding | **Not fixed** — skip list |
| 3 | bgra8_ext × 3 | BGRA texture as render target not supported by GPU | **Not fixed** — skip list |
| 4 | bind_aliasing × 11 | >16 native vertex inputs (MAX_VERTEX_ATTRIBS was 32) | **Fixed** (report 16) — verify on hardware |

---

## Group 5: uniform_api.random.11

**Test:** Random uniform API stress test with seed 11. Creates 1 sampler (`u_var0`) and 1 uniform.

**Why it crashes:** The test uses a single sampler2D uniform. During render, the sampler binding path processes `u_var0` with `shader_binding=0, tex_unit=0`. The crash occurs during GPU execution — the shader reads from a descriptor that either:
1. Has an invalid texture handle (texture not properly uploaded/bound for this test's specific configuration)
2. Or the sampler descriptor is misconfigured for the specific texture format the test uses

**Investigation needed:**
- Add traces to capture the EXACT sampler state at draw time: tex_unit, tex_id, descriptor slot, texture format
- Check if the texture referenced by the sampler has been properly initialized and has valid descriptors
- Compare with random.64 (which passes) to identify what's different in the sampler/texture setup

**Status:** Skipped. Needs on-device debug with traces before shader compilation and draw.
