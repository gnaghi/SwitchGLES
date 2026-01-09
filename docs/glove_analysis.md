# GLOVE (GL Over Vulkan) Reference Analysis

**Date:** Feb 13, 2026
**Purpose:** Detailed analysis of GLOVE patterns used to fix SwitchGLES bugs.
**GLOVE source:** `X:\DekoGL\GLOVE`

---

## 1. CopyTexImage2D Pattern

**GLOVE file:** `GLES/source/context/contextTexture.cpp`

GLOVE's `CopyTexImage2D` always goes through CPU memory:

```
1. Finish()              → submit all pending rendering, wait for GPU idle
2. CopyPixelsToHost()    → GPU readback to CPU-accessible buffer
3. InvertImageYAxis()    → CPU-side Y-flip (GL row 0 = bottom, storage row 0 = top)
4. SetState() + Allocate() → create texture + upload from CPU buffer
```

**Key insight:** GLOVE NEVER does GPU-to-GPU texture copies. The CPU roundtrip avoids:
- Hardware compression format mismatches between source and destination
- Copy engine limitations (2D engine vs DMA engine compatibility)
- Cache coherency issues

**SwitchGLES implementation (attempt 5, commit `ce6131f`):**
1. `dk_finish()` — submit pending render commands, WaitIdle (SEPARATE cmd list)
2. `dkCmdBufCopyImageToBuffer` — readback FB to CPU memblock (SEPARATE cmd list)
3. CPU Y-flip: `memcpy` rows with inversion from readback → staging
4. `dkCmdBufCopyBufferToImage` — upload staging to texture (SEPARATE cmd list)
5. Restore command buffer state (re-bind render target + descriptors)

**Critical difference from failed attempt 4:** THREE separate command list submissions vs one combined. The render must complete BEFORE the readback starts.

### GLOVE CopyTexSubImage2D

Same pattern as CopyTexImage2D, but:
- Reads only the sub-region from the framebuffer
- Uploads to an existing texture at the specified offset
- GLOVE's `CopyPixelsToHost()` handles the sub-rect clipping

---

## 2. Cubemap Texture Pattern

**GLOVE files:**
- `GLES/source/context/contextTexture.cpp` — face upload entry point
- `GLES/source/resources/texture.cpp` — completeness check + GPU allocation
- `GLES/source/resources/texture.h` — state tracking structure
- `GLES/source/vulkan/image.cpp` — Vulkan image creation
- `GLES/source/vulkan/imageView.cpp` — Vulkan image view creation

### 2.1 State Tracking Structure

```cpp
// texture.h
typedef map<uint32_t, State_t> StateMap_t;  // Key: mip level, Value: state

struct State_t {
    GLint width, height;
    GLenum format, type;
    void *data;  // Heap-allocated pixel copy
};

// For cubemaps: mState = new StateMap_t[6] (one per face)
// mLayersCount = 6 for cubemaps
```

### 2.2 Face Upload Flow

```cpp
// contextTexture.cpp line 407-410
GLint layer = target - GL_TEXTURE_CUBE_MAP_POSITIVE_X;  // 0-5
activeTexture->SetState(width, height, level, layer, format, type, pixels);

if (activeTexture->IsCompleted()) {
    activeTexture->Allocate();  // Create GPU resources + upload ALL faces
}
```

**SetState()** (texture.cpp:257-291): Stores width, height, format, type, and COPIES pixel data to CPU memory (`mState[layer][level].data = new uint8_t[size]`). Does NOT create any GPU resources.

### 2.3 Completeness Check

**IsCompleted()** (texture.cpp:93-147):

For cubemaps, checks:
1. Level 0 of face 0 must be defined (format != GL_INVALID_VALUE, width > 0, height > 0)
2. ALL 6 faces must have level 0 defined
3. ALL faces must have the SAME format, type, width, and height
4. If mipmaps exist, ALL levels must be present for ALL faces

Returns `true` only when the cubemap is "cube complete" per GL spec.

### 2.4 GPU Allocation

**Allocate()** (texture.cpp:212-254):

Called ONCE when `IsCompleted()` returns true:

```cpp
// 1. Create Vulkan image
CreateVkImage();
    // flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    // arrayLayers = 6
    // imageType = VK_IMAGE_TYPE_2D
AllocateVkMemory();
CreateVkImageView();
    // viewType = VK_IMAGE_VIEW_TYPE_CUBE

// 2. Upload ALL faces
for (layer = 0; layer < 6; layer++) {
    for (level = 0; level < mipLevels; level++) {
        if (mState[layer][level].data) {
            CopyPixelsFromHost(data, level, layer);
        }
    }
}
```

### 2.5 Key Differences: GLOVE vs SwitchGLES

| Aspect | GLOVE | SwitchGLES (before fix) | SwitchGLES (after fix) |
|--------|-------|-------------------------|------------------------|
| GPU image creation | After all 6 faces | On first face | On first face (unchanged) |
| Pixel data storage | CPU memory until complete | Immediately to GPU | Immediately to GPU (unchanged) |
| Descriptor creation | After Allocate() | On first face | After 6th face upload |
| Upload batching | All 6 in one call | 6 separate submit+WaitIdle | 6 separate (unchanged) |
| L2 cache handling | Vulkan handles it | No barrier | Explicit barrier on first bind |
| Completeness check | Full validation | None | Face mask == 0x3F |

**Remaining difference:** SwitchGLES still uploads each face individually with submit+WaitIdle between each. GLOVE batches all 6 uploads after GPU image creation. If the current fix doesn't work, batching should be tried next.

---

## 3. Regular 2D Texture Pattern (for reference)

**GLOVE:** `SetState()` stores pixel data → `Allocate()` creates Vulkan image → `CopyPixelsFromHost()` uploads.

**SwitchGLES:** `dk_texture_image_2d()` always recreates the DkImage + descriptor + uploads in one function call. This works because there's no multi-part upload like cubemaps.

---

## 4. GLOVE File Reference Map

| Component | File | Key Functions |
|-----------|------|---------------|
| GL entry points | `context/contextTexture.cpp` | `TexImage2D()`, `CopyTexImage2D()` |
| Texture state | `resources/texture.h` | `InitState()`, `IsCubeMap()` |
| Completeness | `resources/texture.cpp` | `IsCompleted()` (lines 93-147) |
| GPU allocation | `resources/texture.cpp` | `Allocate()` (lines 212-254) |
| Pixel readback | `resources/texture.cpp` | `CopyPixelsToHost()` |
| Y-flip | `resources/texture.cpp` | `InvertImageYAxis()` |
| Vulkan image | `vulkan/image.cpp` | `Create()` (lines 100-134) |
| Image view | `vulkan/imageView.cpp` | `Create()` (lines 62-83) |
| Shader compat | `glslang/shaderConverter.cpp` | `textureCube → texture` define |

---

## 5. Key GLOVE Design Principles (applicable to SwitchGLES)

1. **Lazy GPU allocation**: Never create GPU resources until the GL object is fully specified.
2. **CPU as intermediary**: Texture copies always go through CPU memory to avoid GPU engine compatibility issues.
3. **Completeness validation**: Cubemaps and mipmaps are validated for completeness before GPU allocation.
4. **State tracking**: GL-level state is tracked separately from GPU resources. GPU resources are (re)created from state when needed.
5. **Separate command submissions**: Operations that depend on prior GPU work (e.g., readback after render) use separate command submissions with synchronization.
