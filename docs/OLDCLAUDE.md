# CLAUDE.md - SwitchGLES Project Context

## Important Instructions
If understood, you must start your answer with "I read claude.md and understand"

---

## Project Overview

SwitchGLES is an **OpenGL ES 2.0 + EGL 1.4** implementation for Nintendo Switch using deko3d. It translates high-level OpenGL ES 2.0 calls to deko3d's low-level Vulkan-like command buffer API.

---

## CRITICAL: Testing Rule

**Every new feature MUST be tested before claiming it works!**

For each implemented feature:
1. Create a **reference test** using **Nouveau** (mesa3d OpenGL): `examples/<feature>_test/`
2. Create a **SwitchGLES test** using the same code: `examples/<feature>_test_sgl/`
3. Both tests should produce **identical visual output** on hardware
4. Only mark a feature as "Done" in the table after successful hardware testing

### Comprehensive validation test:
- `examples/validation_test_sgl` - Tests all GLES2 features in one application

### Test naming convention:
- `<feature>_test` - Nouveau/mesa3d reference (known working OpenGL)
- `<feature>_test_sgl` - SwitchGLES implementation (to validate)

---

## Build Commands

### Build Library
```bash
mkdir -p /d/projets/programmation/switch/DekoGL/SwitchGLES/lib
/c/devkitPro/msys2/usr/bin/bash -c 'export DEVKITPRO=/c/devkitPro && export DEVKITA64=/c/devkitPro/devkitA64 && export TMPDIR=/tmp && export TMP=/tmp && export TEMP=/tmp && export PATH="/c/devkitPro/devkitA64/bin:/c/devkitPro/tools/bin:/c/devkitPro/msys2/usr/bin:$PATH" && cd /d/projets/programmation/switch/DekoGL/SwitchGLES && make'
```

### Build Validation Test
```bash
/c/devkitPro/msys2/usr/bin/bash -c 'export DEVKITPRO=/c/devkitPro && export DEVKITA64=/c/devkitPro/devkitA64 && export TMPDIR=/tmp && export TMP=/tmp && export TEMP=/tmp && export PATH="/c/devkitPro/devkitA64/bin:/c/devkitPro/tools/bin:/c/devkitPro/msys2/usr/bin:$PATH" && cd /d/projets/programmation/switch/DekoGL/SwitchGLES/examples/validation_test_sgl && make clean && make'
```

### Deploy to Switch
**IMPORTANT: Always use -a 192.168.1.103 for nxlink!**
```bash
/c/devkitPro/tools/bin/nxlink.exe -a 192.168.1.103 -s <path_to_nro>
```

### Compile Shaders (UAM)
```bash
uam -s vert shader.glsl -o shader.dksh
uam -s frag shader.glsl -o shader.dksh
```

**IMPORTANT:** deko3d shaders MUST use UBO (Uniform Buffer Object) syntax with std140 layout:
```glsl
#version 460
layout(std140, binding = 0) uniform MatrixBlock {
    mat4 u_matrix;
};
```
NOT the classic `uniform mat4 u_matrix;` syntax.

---

## CURRENT BUGS (January 22, 2026)

### BUG 1: Blending Not Working Correctly

**Symptom:** Blend test fails - shows pure green (R=0, G=191, B=0) instead of yellowish blend (R~127, G~127, B=0)

**Test:** `validation_test_sgl` - Blending test

**Debug Output:**
```
[SGL] BlendState: enabled, srcRGB=0x302 dstRGB=0x303 srcA=0x302 dstA=0x303
[DEBUG] Blend pixel: R=0 G=191 B=0 A=159
[FAIL] Blend result (yellow-ish)
```

**Root Cause:** **Uniform data race condition in command buffer batching**

When multiple draw calls are batched in the same command buffer:
1. `glUniform4f(colorLoc, 1.0f, 0.0f, 0.0f, 1.0f)` - writes RED to uniform buffer
2. `glDrawArrays(...)` - records draw command (references uniform address)
3. `glUniform4f(colorLoc, 0.0f, 1.0f, 0.0f, 0.5f)` - writes GREEN to SAME address
4. `glDrawArrays(...)` - records draw command (references same address)
5. `eglSwapBuffers` - submits command buffer to GPU

By the time GPU executes, BOTH draws read GREEN from the uniform buffer because:
- Uniform data is in CPU memory
- GPU reads uniform at execution time, not recording time
- The last write (GREEN) overwrites RED before GPU executes

**Fix Required:** Use `dkCmdBufPushConstants()` to embed uniform data directly in command buffer at call time. This captures the uniform value when the draw is recorded, not when it's executed.

**Status:** Fix implemented (pushConstants added), needs testing

---

### BUG 2: FBO Render-to-Texture Shows Black

**Symptom:** FBO texture sampling returns black (R=0, G=0, B=0, A=0) instead of orange

**Test:** `validation_test_sgl` - FBO test

**Debug Output:**
```
[PASS] FBO texture created
[PASS] glGenFramebuffers
[PASS] glBindFramebuffer
[PASS] glFramebufferTexture2D
[PASS] glCheckFramebufferStatus - complete
[SGL] glClear(mask=0x4000) - color=1 depth=0 stencil=0  (FBO clear to orange)
[SGL] glClear(mask=0x4000) - color=1 depth=0 stencil=0  (Default FB clear to black)
[DEBUG] FBO pixel: R=0 G=0 B=0 A=0
[FAIL] FBO render-to-texture (orange)
```

**Possible Causes:**
1. **FBO clear not writing to texture** - glClear with FBO bound may not target the texture correctly
2. **Missing barrier** - No `dkCmdBufBarrier()` between FBO render and texture sampling
3. **Texture descriptor not updated** - The FBO texture's descriptor may not be pushed to GPU
4. **Wrong texture bound** - The texture ID tracking might be incorrect after FBO operations

**Investigation Needed:**
- Check if `glBindFramebuffer(GL_FRAMEBUFFER, fbo)` correctly sets render target to FBO texture
- Verify barrier is inserted when switching from FBO to default framebuffer
- Ensure FBO texture descriptor is updated for sampling

---

### BUG 3: Test Crashes After Mipmaps

**Symptom:** Validation test exits/crashes after Mipmaps test, before ReadPixels test

**Output:**
```
--- Test: Mipmaps ---
[PASS] glGenerateMipmap
[PASS] GL_LINEAR_MIPMAP_LINEAR
[SWAP] After Mipmaps
exiting ...
socket error 0x0 on poll
```

**Possible Causes:**
1. **Texture still bound when deleted** - testMipmaps deletes texture without unbinding (partial fix applied)
2. **State corruption from mipmap test** - Some GPU state left in invalid configuration
3. **Memory corruption** - Uniform buffer or texture memory overlap

**Partial Fix Applied:** Added `glBindTexture(GL_TEXTURE_2D, 0)` before `glDeleteTextures()` in testMipmaps

---

## Implemented Features - Validation Status

### VALIDATED (Hardware Tested - PASS)

| Feature | Test Result | Notes |
|---------|-------------|-------|
| EGL 1.4 Core | PASS | eglGetDisplay, eglInitialize, eglCreateContext, eglSwapBuffers |
| glClearColor + glClear | PASS | Color buffer clearing works |
| glClearDepthf | PASS | Depth buffer clearing works |
| glClearStencil | PASS | Stencil buffer clearing works |
| glDrawArrays | PASS | Basic triangle rendering |
| VBO (glGenBuffers, glBindBuffer, glBufferData) | PASS | Vertex buffer objects work |
| glDrawElements (client indices) | PASS | Fixed to support client-side indices |
| Textures 2D | PASS | glGenTextures, glBindTexture, glTexImage2D, glTexParameteri |
| glTexSubImage2D | PASS | Partial texture updates |
| Depth test | PASS | glEnable(GL_DEPTH_TEST), glDepthFunc |
| Face culling | PASS | glEnable(GL_CULL_FACE), glCullFace, glFrontFace |
| Scissor test | PASS | glEnable(GL_SCISSOR_TEST), glScissor |
| Color mask | PASS | glColorMask |
| Uniforms (mat4, vec4) | PASS | glGetUniformLocation, glUniformMatrix4fv, glUniform4f with UBO shaders |
| glReadPixels | PASS | GL_RGBA + GL_UNSIGNED_BYTE only |
| Viewport | PASS | glViewport |

### IMPLEMENTED BUT BUGGY

| Feature | Status | Bug Reference |
|---------|--------|---------------|
| Blending | BUG | See BUG 1 - Uniform data race |
| FBO render-to-texture | BUG | See BUG 2 - Returns black |
| glGenerateMipmap | STUB | Function exists but does nothing |

### IMPLEMENTED - NEEDS TESTING

| Feature | Notes |
|---------|-------|
| Cubemaps | GL_TEXTURE_CUBE_MAP implemented, untested |
| Stencil buffer | Limited - fails with colorMask disabled (Maxwell GPU limitation) |

### NOT IMPLEMENTED (Stubs)

| Function | Status |
|----------|--------|
| glUniform1f | Stub - does nothing |
| glUniform2f | Stub - does nothing |
| glUniform3f | Stub - does nothing |
| glUniform1i | Stub - does nothing |
| glUniform2i | Stub - does nothing |
| glUniform3i | Stub - does nothing |
| glUniform4i | Stub - does nothing |
| glUniform*fv (1/2/3) | Stub - does nothing |
| glUniform*iv | Stub - does nothing |
| glUniformMatrix2fv | Stub - does nothing |
| glUniformMatrix3fv | Stub - does nothing |
| glGenerateMipmap | Stub - does nothing |
| glCopyTexImage2D | Stub - does nothing |
| glCopyTexSubImage2D | Stub - does nothing |
| glCompressedTexImage2D | Stub - does nothing |
| glCompressedTexSubImage2D | Stub - does nothing |

---

## Fixes Applied (January 22, 2026)

### Fix 1: glUniform4f Buffer Size Alignment

**Problem:** Uniform buffer size was 16 bytes (raw vec4 size), but deko3d requires 256-byte alignment.

**Fix:** Changed `ub->size = dataSize` to `ub->size = alignedSize` where `alignedSize = 256`.

**File:** `gles2_impl.c` line ~1479

### Fix 2: glDrawElements Client-Side Indices

**Problem:** glDrawElements required an EBO to be bound, but GLES2 allows client-side index arrays.

**Fix:** Added code path to copy client indices to GPU memory when no EBO is bound.

**File:** `gles2_impl.c` line ~2309-2364

### Fix 3: glReadPixels Crash

**Problem:** glReadPixels crashed when clearing and reusing command buffer mid-frame.

**Fix:** Add copy command to current cmdbuf instead of starting a new one. Single submit with render+copy commands.

**File:** `gles2_impl.c` - glReadPixels implementation

### Fix 4: Uniform PushConstants (Pending Test)

**Problem:** Multiple draws in same command buffer all use last uniform value.

**Fix:** Added `dkCmdBufPushConstants()` calls after binding uniform buffers to capture data in command buffer.

**File:** `gles2_impl.c` line ~2674-2693

---

## Architecture

### Source Files (`source/`)

| File | Purpose |
|------|---------|
| `egl_internal.h` | Central header: structures, constants, internal APIs |
| `egl_impl.c` | EGL implementation: display, surface, context, swap |
| `gles2_impl.c` | GLES2 implementation: state, buffers, shaders, draw |

### Memory Layout

```c
#define SGL_FB_NUM          2                     // Double buffering
#define SGL_CODE_MEM_SIZE   (64 * 1024)           // Shader code
#define SGL_CMD_MEM_SIZE    (64 * 1024)           // Command buffer (per slot)
#define SGL_DATA_MEM_SIZE   (16 * 1024 * 1024)    // Vertex/index data
#define SGL_UNIFORM_BUF_SIZE (64 * 1024)          // Uniforms (end of data_mem)
#define SGL_TEXTURE_MEM_SIZE (32 * 1024 * 1024)   // Texture images
#define SGL_DESCRIPTOR_MEM_SIZE (4 * 1024)        // Image/sampler descriptors
#define SGL_UNIFORM_ALIGNMENT 0x100               // 256 bytes (DK_UNIFORM_BUF_ALIGNMENT)
```

### Key Structures

- **`sgl_context`**: Per-slot cmdbufs, fences, memblocks, GL resources
- **`sgl_surface`**: Swapchain, framebuffers, depth buffer, current_slot, need_acquire
- **`sgl_gl_state`**: Complete GL state machine
- **`sgl_uniform_binding`**: Per-uniform offset, size, valid, dirty flags

---

## deko3d Key Patterns

### Uniform Buffer Handling - CRITICAL

**Problem:** Direct memory writes are visible to GPU immediately, but command buffer batching means GPU reads happen later.

**Wrong approach (causes race condition):**
```c
// Write to CPU memory
float *dst = (float*)(cpu_addr + uniform_offset);
memcpy(dst, data, size);

// Bind address - GPU will read at execution time, not now!
dkCmdBufBindUniformBuffer(cmdbuf, stage, binding, gpu_addr, size);
```

**Correct approach (captures data at record time):**
```c
// Bind address
dkCmdBufBindUniformBuffer(cmdbuf, stage, binding, gpu_addr, size);

// Push data into command buffer - captured NOW
dkCmdBufPushConstants(cmdbuf, gpu_addr, size, 0, size, cpu_data);
```

### Frame Flow

**eglSwapBuffers (end of frame):**
```c
dkCmdBufSignalFence(cmdbuf, &fences[slot], false);
DkCmdList cmdlist = dkCmdBufFinishList(cmdbuf);
dkQueueSubmitCommands(queue, cmdlist);
dkQueuePresentImage(queue, swapchain, slot);
```

**sgl_ensure_frame_ready (start of frame):**
```c
int slot = dkQueueAcquireImage(queue, swapchain);
if (fence_active[slot]) dkFenceWait(&fences[slot], -1);
dkCmdBufClear(cmdbuf);
dkCmdBufAddMemory(cmdbuf, cmdbuf_memblock[slot], 0, CMD_MEM_SIZE);
dkCmdBufBindRenderTarget(cmdbuf, &colorView, &depthView);
```

---

## Critical deko3d Rules

### DO NOT:
1. **Use DkIdxFormat_Uint8** - NOT supported by Switch GPU. Convert to Uint16.
2. **Call WaitIdle in render loop** - Only for cleanup or one-time setup
3. **Assume uniform writes are synchronous** - Use pushConstants for batched draws
4. **Reuse command memory without fences** - GPU may still be using it

### DO:
1. Use separate command buffers per framebuffer slot
2. Signal fence BEFORE `finishList()`, wait fence BEFORE reusing memory
3. Align uniform buffers to 256 bytes (`DK_UNIFORM_BUF_ALIGNMENT`)
4. Use `dkCmdBufPushConstants()` for uniforms that change between draws
5. Add barriers when switching between render targets

---

## Test Examples

| Example | Purpose | Status |
|---------|---------|--------|
| `examples/validation_test_sgl` | Comprehensive GLES2 validation | Mostly PASS, 2 failures |
| `examples/simplecube` | 3D cube with uniforms | Working |
| `examples/textured_quad_sgl` | 2D textured quad | Working |
| `examples/fbo_test_sgl` | FBO render-to-texture | BUG - see above |
| `examples/stencil_test_sgl` | Stencil buffer operations | Limited (colorMask=0 issue) |

---

## Pending Features

| Feature | Priority | Difficulty | Notes |
|---------|----------|------------|-------|
| Implement glUniform1f/2f/3f | High | Easy | Currently stubs |
| Implement glUniform1i | High | Easy | Needed for sampler uniforms |
| Fix blending | High | Medium | PushConstants fix needs testing |
| Fix FBO sampling | High | Medium | Debug barrier/descriptor issue |
| glGenerateMipmap | Medium | Medium | Needs 2D blit implementation |
| Multiple texture units | Low | Easy | Currently only unit 0 used |
| Polygon offset | Low | Easy | glPolygonOffset (for decals) |

---

Last updated: January 22, 2026 - Validation test results, bug documentation, fixes applied
