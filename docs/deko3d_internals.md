# deko3d Internals Analysis

**Date:** Feb 13, 2026
**Purpose:** Document deko3d internal behavior discovered during bug investigation.
**deko3d source:** `X:\DekoGL\deko3d`

---

## 1. Transfer Engines

deko3d has two transfer engines on Maxwell (Tegra X1):

### 1.1 BlitCopyEngine (DMA)

**File:** `source/maxwell/gpu_transfer.cpp`
**Used by:** `dkCmdBufCopyImage`, `dkCmdBufCopyImageToBuffer`, `dkCmdBufCopyBufferToImage`

- Performs tiling/untiling format conversion (linear ↔ block-linear)
- Handles layered images: adjusts `dstIova += dstZ * dst.m_layerStride` for layered views
- For non-layered views (DkImageType_2D override): does NOT adjust by z, relies on `fromImageView` having already adjusted iova by `layerOffset * layerSize`
- **NO explicit HwCompression support** — cannot read HwCompressed framebuffers directly
- **Writes bypass L2 cache** — data goes directly to DRAM

### 1.2 Blit2DEngine (2D Blit)

**Used by:** `dkCmdBufBlitImage`
**Requires:** `DkImageFlags_Usage2DEngine` on source AND destination
- Has `SetCompressionEnable{}, 1` — can handle HwCompression
- Can do format conversion, scaling, flipping
- More capable but more restrictive (requires Usage2DEngine flag on images)

### 1.3 Cache Coherency Impact

| Operation | Writes to | L2 Cache |
|-----------|-----------|----------|
| 3D Rendering | Framebuffer (via L2) | Coherent |
| DMA CopyBufferToImage | DRAM directly | **Bypasses L2** |
| DMA CopyImageToBuffer | DRAM directly | **Bypasses L2** |
| 2D BlitImage | Via 2D engine | Coherent (has compression support) |
| Texture sampling | Reads via L2 | May read **stale data** after DMA writes |

**CRITICAL:** After DMA writes to texture memory, an L2 cache invalidation barrier is needed before the texture sampler can read the new data. Without it, the sampler may read zeros (from memblock initialization) or old data from L2.

This barrier is only needed once (after the DMA, before first sampling). The `cubemap_needs_barrier` flag implements this.

---

## 2. Image View System

**File:** `source/dk_image.cpp` — `ImageInfo::fromImageView()`

### 2.1 Type Override Behavior

When an `DkImageView` has `type != DkImageType_None`, it overrides the image's native type:

| View type override | Layered? | IOVA adjustment |
|--------------------|----------|-----------------|
| `DkImageType_2D` | false | `iova += layerOffset * layerSize` |
| `DkImageType_2DArray` | false (same as 2D) | `iova += layerOffset * layerSize` |
| `DkImageType_Cubemap` | true | No iova adjustment (uses arrayMode) |
| `DkImageType_CubemapArray` | true | No iova adjustment |
| `DkImageType_None` | Uses image native | Depends on image type |

**For cubemap face uploads:** Using `DkImageType_2D` + `layerOffset = face_index` correctly targets a single face. The DMA engine writes to `image_base + face_index * layer_size`.

### 2.2 dkImageViewDefaults

```c
void dkImageViewDefaults(DkImageView *view, DkImage const *image) {
    view->pImage = image;
    view->type = DkImageType_None;    // No type override
    view->format = 0;                  // Use image's format
    view->swizzle[0..3] = identity;
    view->dsSource = DkDsSource_Depth;
    view->layerOffset = 0;
    view->layerCount = 0;              // 0 = use image's layer count
    view->mipLevelOffset = 0;
    view->mipLevelCount = 0;           // 0 = use image's mip count
}
```

---

## 3. Texture Descriptor (TIC) Generation

**File:** `source/maxwell/tic_generate.cpp` — `dkImageDescriptorInitialize()`

### 3.1 Cubemap-Specific Fields

```cpp
// texture_type mapping
DkImageType_Cubemap → TextureType_Cubemap (value 3)

// depth calculation
if (usesLoadOrStore) {
    depth_minus_one = (layerCount ?: image->m_layerCount) - 1;
} else {
    depth_minus_one = 0;  // Regular cubemap sampling (not load/store)
}

// IOVA: NOT adjusted for cubemap layerOffset (only for array types)
// view_layer_base set from view->layerOffset (usually 0 for full cubemap)
```

### 3.2 When to use usesLoadOrStore = true

- For image load/store operations (GL_SHADER_STORAGE, compute shaders)
- For cubemap arrays where depth > 1
- For SwitchGLES: always `false` (ES 2.0 doesn't have image load/store)

---

## 4. Cubemap Image Layout

**File:** `source/dk_image.cpp` — `dkImageLayoutInitialize()`

```cpp
case DkImageType_Cubemap:
    obj->m_dimensions[2] = 6;  // Always 6 faces, regardless of input dimensions[2]
    obj->m_hasLayers = 1;
    break;
```

- User passes `dimensions[2] = 1`, deko3d overrides to 6
- `m_layerSize` = size of one face (includes alignment/padding for block-linear)
- Total image size = 6 * m_layerSize (approximately, with alignment)

---

## 5. Key deko3d Functions for SwitchGLES

### 5.1 Texture Upload

```c
// Create image layout
dkImageLayoutMakerDefaults(&maker, device);
maker.type = DkImageType_Cubemap;  // or DkImageType_2D
maker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
maker.format = DkImageFormat_RGBA8_Unorm;
maker.dimensions[0] = width;
maker.dimensions[1] = height;
maker.dimensions[2] = 1;  // deko3d overrides to 6 for cubemaps
dkImageLayoutInitialize(&layout, &maker);

// Allocate + initialize
dkImageInitialize(&image, &layout, memblock, offset);

// Upload via DMA (per-face for cubemaps)
DkImageView view;
dkImageViewDefaults(&view, &image);
view.type = DkImageType_2D;          // Override for per-face upload
view.layerOffset = face_index;        // Which face to target
view.layerCount = 1;
dkCmdBufCopyBufferToImage(cmdbuf, &src, &view, &rect, 0);
```

### 5.2 Texture Descriptor Creation

```c
// For sampling: create descriptor with cubemap type
DkImageView view;
dkImageViewDefaults(&view, &image);
view.type = DkImageType_Cubemap;  // Cubemap sampling mode
dkImageDescriptorInitialize(&descriptor, &view, false, false);
```

### 5.3 Texture Binding

```c
// Push descriptor to GPU memory at correct slot
dkCmdBufPushData(cmdbuf, descAddr + unit * sizeof(DkImageDescriptor),
                 &descriptor, sizeof(DkImageDescriptor));

// Create and push sampler
dkSamplerDefaults(&sampler);
sampler.minFilter = DkFilter_Nearest;
sampler.magFilter = DkFilter_Nearest;
sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
dkSamplerDescriptorInitialize(&samplerDesc, &sampler);
dkCmdBufPushData(cmdbuf, sampAddr + unit * sizeof(DkSamplerDescriptor),
                 &samplerDesc, sizeof(DkSamplerDescriptor));

// Bind texture handle
DkResHandle handle = dkMakeTextureHandle(unit, unit);
dkCmdBufBindTexture(cmdbuf, DkStage_Fragment, unit, handle);
```

---

## 6. Swapchain Framebuffer Properties

- Has `DkImageFlags_HwCompression` — hardware-compressed for efficient rendering
- Has `DkImageFlags_Usage2DEngine` (added in SwitchGLES attempt 3)
- The 2D blit engine CAN read HwCompressed sources (it has `SetCompressionEnable`)
- The DMA copy engine CANNOT read HwCompressed sources without decompression
- `dk_read_pixels` works because it uses `CopyImageToBuffer` which does NOT require decompression (the DMA engine handles the tiling transparently, but compression is separate)

**Wait — this is confusing.** `dk_read_pixels` uses `CopyImageToBuffer` (DMA) from the framebuffer, and it WORKS. So either:
- DMA can handle HwCompression after all (unlikely given no `SetCompressionEnable`)
- The framebuffer's HwCompression is compatible with DMA reads
- DMA reads go through a decompression stage transparently on Maxwell

**Note:** This needs more investigation if CopyTexImage2D attempt 5 fails.

---

## 7. Command Buffer Lifecycle

```
dk_begin_frame(slot):
    cmdbuf = cmdbufs[slot]
    client_array_offset = slot * per_slot_size  // Reset per-slot
    bind render target

dk_wait_fence(slot):
    dkFenceWait(&fences[slot])
    dkCmdBufClear(cmdbufs[slot])
    dkCmdBufAddMemory(cmdbufs[slot], ...)
    descriptors_bound = false
    uniform_offset = 0

dk_end_frame(slot):
    dkCmdBufSignalFence(cmdbuf, &fences[slot])
    cmdlist = dkCmdBufFinishList(cmdbuf)
    dkQueueSubmitCommands(queue, cmdlist)

dk_present(slot):
    dkQueuePresentImage(queue, swapchain, slot)
```

**Mid-frame submit pattern** (used by texture uploads, readPixels, CopyTexImage2D):
```
cmdlist = dkCmdBufFinishList(cmdbuf)
dkQueueSubmitCommands(queue, cmdlist)
dkQueueWaitIdle(queue)
dkCmdBufClear(cmdbuf)
dkCmdBufAddMemory(cmdbuf, ...)
descriptors_bound = false
// Re-bind render target
```

This is expensive (GPU stall) but necessary for operations that need immediate results.

---

## 8. Known Limitations / Gotchas

1. **DkIdxFormat_Uint8 not supported** — Maxwell GPU limitation. Must convert to Uint16.
2. **Uniform buffers must be 256-byte aligned** (`DK_UNIFORM_BUF_ALIGNMENT`)
3. **pushConstants captures data at record time** — not at execution time. This is correct behavior.
4. **descriptors_bound must be reset** after command buffer clear — otherwise descriptor set binding is stale.
5. **L2 cache invalidation needed** after DMA writes to texture memory, before sampling.
6. **DkImageFlags_Usage2DEngine** affects image memory layout. Only add if the image will actually be used with the 2D blit engine.
7. **Cubemap layerOffset in DkImageView** — adjusts GPU address by `layerOffset * image->m_layerSize` for DkImageType_2D views, but NOT for DkImageType_Cubemap views (which use `view_layer_base` in the TIC descriptor instead).
