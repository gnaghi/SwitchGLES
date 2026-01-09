# SwitchGLES — Architecture & Design Document

## Overview

SwitchGLES is an OpenGL ES 2.0 + EGL 1.4 implementation for Nintendo Switch that translates GL calls into deko3d commands. It follows a **4-layer architecture** with strict separation of concerns.

```
┌─────────────────────────────────────────────┐
│  Application (GLES2 + EGL calls)            │
├─────────────────────────────────────────────┤
│  Layer 1: GL Entry Points (source/gl/)      │
│  - API validation, error generation         │
│  - State tracking delegation to Layer 2     │
│  - Draw preparation and texture binding     │
├─────────────────────────────────────────────┤
│  Layer 2: Context & State (source/context/) │
│  - GL state objects (blend, depth, raster)  │
│  - Resource manager (textures, buffers, FBOs)│
│  - No GPU knowledge — pure state tracking   │
├─────────────────────────────────────────────┤
│  Layer 3: Backend Interface (sgl_backend.h) │
│  - Function pointer table (ops)             │
│  - Opaque impl_data pointer                 │
│  - GPU-agnostic abstraction                 │
├─────────────────────────────────────────────┤
│  Layer 4: deko3d Backend (source/backend/)  │
│  - Direct deko3d API calls                  │
│  - GPU memory management                   │
│  - Command buffer recording                 │
│  - Shader loading (DKSH binary)             │
└─────────────────────────────────────────────┘
        │
┌───────┴──────────────┐
│  EGL (source/egl_impl.c)                    │
│  - Bridges Layers 2 & 4                     │
│  - Device, swapchain, queue management      │
│  - Frame lifecycle (triple buffering)       │
└──────────────────────────────────────────────┘
        │
┌───────┴──────────────┐
│  Transpiler (source/transpiler/)            │
│  - GLSL ES 1.00 → GLSL 4.60 conversion     │
│  - ES 1.00 validation (reserved ops, etc.)  │
│  - Uniform/sampler reflection               │
└──────────────────────────────────────────────┘
```

---

## Layer 1: GL Entry Points (`source/gl/`)

Each file corresponds to a GLES2 functional area:

| File | Lines | Responsibility |
|------|-------|----------------|
| `gl_shader.c` | 2,097 | glCompileShader, glLinkProgram, glGetUniformLocation — shader compilation orchestration with Mesa direct + transpiler fallback |
| `gl_uniform.c` | 2,146 | glUniform*, glGetActiveUniform, glBindAttribLocation — packed UBO write system |
| `gl_framebuffer.c` | 1,133 | glGenFramebuffers, glFramebufferTexture2D, glCheckFramebufferStatus — FBO management with deferred deletion |
| `gl_texture.c` | 1,031 | glTexImage2D, glTexParameteri, glGenerateMipmap — texture creation and parameter management |
| `gl_query.c` | 903 | glGetIntegerv, glGetString, glGetError — state queries and extension reporting |
| `gl_draw.c` | 560 | glDrawArrays, glDrawElements — draw preparation (state re-application, texture binding) |
| `gl_state.c` | 443 | glEnable, glDisable, glBlendFunc, glDepthFunc — state change delegation |
| `gl_buffer.c` | 252 | glGenBuffers, glBufferData, glBufferSubData — VBO/EBO management |
| `gl_vertex.c` | 225 | glVertexAttribPointer, glEnableVertexAttribArray — vertex attribute setup |
| `gl_clear.c` | 140 | glClear, glClearColor, glDepthRangef — clear operations and depth range |
| `gl_common.h` | 71 | Shared macros: GET_CTX, GET_TEXTURE, CHECK_BACKEND |

### Key Design Decisions

**State re-application per draw:** `sgl_prepare_draw()` re-applies ALL state (viewport, scissor, depth, blend, raster, polygon offset, texture bindings) before every draw call. This avoids dirty-flag complexity and works correctly with triple-buffered command buffers (each buffer starts empty each frame). Performance cost is acceptable for GLES2 workloads.

**Dual shader compilation path:** `glCompileShader` tries Mesa direct compilation first (99.7% success rate for GLES2 shaders). On failure, falls through to the transpiler (GLSL ES 1.00 → 4.60 conversion). This maximizes compatibility while leveraging Mesa's superior semantic analysis.

**Packed UBO system:** Uniform locations encode stage, binding, and byte offset in a single GLint:
```
Bit 30:    SGL_LOC_PACKED_FLAG (1 = packed location)
Bits 24-29: Stage (0=VS, 1=FS)
Bits 16-23: UBO binding
Bits 0-15:  Byte offset within UBO
```
`glUniform*` writes directly to a CPU shadow buffer. At draw time, the entire buffer is pushed to GPU via `dkCmdBufPushConstants`.

---

## Layer 2: Context & State (`source/context/`)

### State Objects (GLOVE Pattern)

Each GL state domain is isolated in its own struct with a consistent interface:

```c
// Each state object provides:
void sgl_state_X_init(sgl_state_X_t *state);             // Reset to GL defaults
bool sgl_state_X_set_Y(sgl_state_X_t *state, args...);   // Returns true if changed
```

| State Object | File | Tracks |
|-------------|------|--------|
| `sgl_state_blend_t` | `sgl_state_blend.c` | Blend enable, equation, factors, color |
| `sgl_state_depth_t` | `sgl_state_depth.c` | Depth test, write mask, function, stencil (front+back) |
| `sgl_state_color_t` | `sgl_state_color.c` | Color write mask, clear color |
| `sgl_state_raster_t` | `sgl_state_raster.c` | Cull face, front face, polygon offset |
| `sgl_state_viewport_t` | `sgl_state_viewport.c` | Viewport rect, scissor rect, depth range |

**No dirty flags.** State is always applied before each draw. State objects return `bool` to indicate whether the value changed (allowing the caller to skip re-application in the future if needed).

### Resource Manager (`sgl_resource_manager.c`)

Fixed-size allocation pools for all GL objects:

```c
sgl_texture_t       textures[SGL_MAX_TEXTURES];          // 256 slots
sgl_buffer_t        buffers[SGL_MAX_BUFFERS];             // 256 slots
sgl_shader_t        shaders[SGL_MAX_SHADERS];             // 128 slots
sgl_program_t       programs[SGL_MAX_PROGRAMS];            // 64 slots
sgl_framebuffer_t   framebuffers[SGL_MAX_FRAMEBUFFERS];    // 64 slots
sgl_renderbuffer_t  renderbuffers[SGL_MAX_RENDERBUFFERS];  // 64 slots
```

**Allocation:** Linear scan for first free slot (O(n), acceptable for small arrays).
**Deferred deletion:** Textures/renderbuffers attached to FBOs are marked `delete_pending` with `fbo_ref_count > 0`. GPU memory stays alive until all FBO references are released.
**Overflow IDs:** For GL names exceeding array bounds (e.g., dEQP uses arbitrary uint32 IDs), a small overflow list (16 entries) tracks `glIsTexture`/`glIsBuffer` responses.

---

## Layer 3: Backend Interface (`sgl_backend.h`)

The backend is accessed exclusively through function pointers:

```c
typedef struct sgl_backend_ops {
    // Buffer operations
    void (*buffer_data)(sgl_backend_t *be, sgl_handle_t handle, ...);

    // Texture operations
    void (*texture_image_2d)(sgl_backend_t *be, sgl_handle_t handle, ...);
    void (*texture_parameter)(sgl_backend_t *be, sgl_handle_t handle, ...);
    void (*bind_texture)(sgl_backend_t *be, GLuint unit, sgl_handle_t handle, int stage);

    // State application
    void (*apply_blend)(sgl_backend_t *be, const sgl_blend_state_t *state);
    void (*apply_depth_stencil)(sgl_backend_t *be, const sgl_depth_stencil_state_t *state);

    // Draw
    void (*draw_arrays)(sgl_backend_t *be, GLenum mode, GLint first, GLsizei count);
    void (*draw_elements)(sgl_backend_t *be, GLenum mode, GLsizei count, ...);

    // ... 40+ operations total
} sgl_backend_ops_t;

typedef struct sgl_backend {
    const sgl_backend_ops_t *ops;
    void *impl_data;  // Opaque — cast to dk_backend_data_t in Layer 4
} sgl_backend_t;
```

**Rule:** Layer 1 and Layer 2 NEVER include deko3d headers. All GPU access goes through this interface.

---

## Layer 4: deko3d Backend (`source/backend/deko3d/`)

### Memory Layout

```
┌──────────────────────── GPU Memory ────────────────────────┐
│                                                             │
│  Code Memory (16 MB)                                        │
│  ├── Shader DKSH binaries (bump allocator)                  │
│                                                             │
│  Data Memory (256 MB) — shared DkMemBlock                   │
│  ├── VBO region (192 MB) — free-list allocator              │
│  │   ├── Active VBOs (glBufferData allocations)             │
│  │   └── Free blocks (first-fit + neighbor coalescing)      │
│  ├── Client array region (63 MB) — per-slot staging         │
│  │   ├── Slot 0: client_array + staging (21 MB)             │
│  │   ├── Slot 1: client_array + staging (21 MB)             │
│  │   └── Slot 2: client_array + staging (21 MB)             │
│  └── Uniform region (1 MB) — bump allocator per frame       │
│                                                             │
│  Texture Memory (128 MB)                                    │
│  ├── DkImage allocations (free-list allocator)              │
│  └── Free blocks (first-fit + coalescing)                   │
│                                                             │
│  Descriptor Memory (~64 KB)                                 │
│  ├── Image descriptors (SGL_MAX_TEXTURES × 32 bytes)        │
│  └── Sampler descriptors (SGL_MAX_TEXTURES × 32 bytes)      │
│                                                             │
│  Command Buffers (4 MB × 3 slots)                           │
│  ├── Slot 0: GPU command recording                          │
│  ├── Slot 1: GPU command recording                          │
│  └── Slot 2: GPU command recording                          │
│                                                             │
│  Depth Buffers (per-slot, Z24S8, framebuffer-sized)         │
│  Renderbuffer Memory (per-RB DkMemBlock)                    │
└─────────────────────────────────────────────────────────────┘
```

### Key Files

| File | Lines | Responsibility |
|------|-------|----------------|
| `dk_texture.c` | 2,748 | Texture creation, upload, cubemaps, mipmaps, completeness, format conversion, free-list allocator |
| `dk_internal.h` | 719 | All struct definitions, function declarations, ARM barrier macro |
| `dk_framebuffer.c` | 645 | FBO render target binding, renderbuffer storage, blit, readpixels |
| `dk_draw.c` | 554 | Vertex attribute upload (client arrays, GL_FIXED conversion), draw dispatch, EBO uint8→uint16 |
| `dk_command.c` | 446 | Command buffer management, submit/reset, render target rebind, deferred VBO frees |
| `dk_utils.c` | 442 | Format conversion tables, stencil/compare op mapping, compressed format lookup |
| `dk_backend.c` | 439 | Device creation, memory pool init/destroy, backend ops vtable |
| `dk_shader.c` | 399 | DKSH binary loading, code memory allocation, program linking, shader binding |
| `dk_state.c` | 347 | Depth/stencil/blend/raster state → deko3d state conversion, blend.dst workaround |
| `dk_clear.c` | 214 | glClear → dkCmdBufClearColor/DepthStencil with scissor, barriers |
| `dk_buffer.c` | 242 | VBO free-list allocator (first-fit + coalescing), buffer data upload |
| `dk_uniform.c` | 81 | Uniform ring buffer allocator (per-frame bump) |

### Critical Patterns

**Triple buffering:** 3 command buffer slots, cycled per frame. Each slot has its own command memory, client array staging region, and fence. Frame N+1 begins by waiting on frame N-2's fence.

**Descriptor management:** Image and sampler descriptors are written directly to GPU memory (CpuUncached) via CPU memcpy, not via per-draw PushData DMA. This eliminates the DMA-vs-TIC/TSC coherency issue that caused texture flickering. An ARM `dsb st` barrier after each write ensures CPU store buffer is flushed.

**Blend workaround:** deko3d 0.5.0 has a copy-paste bug where `dstColorBlendFactor` is written to both RGB and Alpha dst registers. The workaround writes the correct Alpha factor directly to GPU register 0x786 via raw command buffer injection. This accesses deko3d's internal `DkCmdBuf` struct layout (offsets 112/120 for m_cmdPos/m_cmdEnd). This is fragile and version-specific.

**Free-list allocators:** Both VBO and texture memory use the same pattern: first-fit scan of a sorted free list, with neighbor coalescing on free. The free list is bounded (128 entries). On overflow, the allocation leaks the block (logged but not fatal).

---

## EGL Implementation (`source/egl_impl.c`)

The EGL layer bridges Layers 2 and 4:

```c
// EGL manages GPU resources directly (allowed by architecture):
eglInitialize  → dkDeviceCreate, dkQueueCreate, dkMemBlockCreate (all pools)
eglCreateContext → sgl_context_init, sgl_backend_create_deko3d
eglSwapBuffers  → dkQueuePresentImage, fence signal, slot advance
eglTerminate   → memory pool cleanup (device kept alive for anti-fragmentation)
```

**Device persistence:** The DkDevice is created once and kept alive across `eglTerminate`/`eglInitialize` cycles. This prevents GPU address space fragmentation that causes OOM after ~20 cycles (observed in dEQP batch testing).

**Frame lifecycle:**
1. `eglSwapBuffers` → present image, signal fence, advance slot
2. Next frame's `sgl_ensure_frame_ready()` → wait previous slot's fence, clear cmdbuf, re-bind descriptors, re-apply all GL state to fresh command buffer

---

## Transpiler (`source/transpiler/`)

Converts GLSL ES 1.00 to GLSL 4.60 for Mesa/uam compilation:

```
GLSL ES 1.00 source
  → Validation (reserved operators, qualification order, preprocessor, const expressions)
  → Keyword replacement (attribute→in, varying→out, texture2D→texture, etc.)
  → Uniform extraction (struct flattening, sampler array expansion)
  → UBO wrapping (bare uniforms → layout(std140, binding=N) uniform Block { ... })
  → gl_DepthRange injection (synthetic sgl_dr_near/far/diff uniforms)
  → GLSL 4.60 output + reflection metadata (uniforms, samplers, attributes, varyings)
```

**Dual path:** `glCompileShader` tries Mesa first. The transpiler is only used when Mesa silently fails (~0.3% of valid ES 1.00 shaders) or when `gl_DepthRange` is detected (Mesa in API_OPENGL_CORE doesn't emit STATE_DEPTH_RANGE for #version 100).

---

## Shader Compilation Flow

```
glCompileShader(shader)
  │
  ├── Is GLSL ES 1.00? (detect #version 100 or ES keywords)
  │   │
  │   ├── YES: Run glslt_validate_es100() — catch reserved ops, qualification order, etc.
  │   │   │
  │   │   ├── Contains gl_DepthRange? → goto transpiler (Mesa can't handle it)
  │   │   │
  │   │   ├── Try sgl_compile_es100_mesa() — Mesa direct compilation
  │   │   │   ├── Success → compiled_via_mesa = true, capture uniform/sampler metadata
  │   │   │   ├── Error "too many inputs" or "insufficient contiguous locations"
  │   │   │   │   → Mark compiled, defer to link time (attribute aliasing)
  │   │   │   ├── Error about const/initializer → try transpiler
  │   │   │   └── Other error → shader rejected (compiled = false)
  │   │   │
  │   │   └── Mesa silent fail → try glslt_transpile() as fallback
  │   │
  │   └── NO: Compile as GLSL 4.60 directly via sgl_compile_glsl460()
  │
  └── glLinkProgram(program)
      ├── Mesa path: extract packed UBO locations from Mesa metadata
      ├── Transpiler path: extract from transpiler reflection
      ├── Build sampler binding table (VS/FS separate bindings)
      ├── Build gl_DepthRange packed locations (if used)
      └── Compile DKSH binaries, load to GPU code memory
```

---

## Known Architectural Limitations

1. **No dirty flags:** All state is re-applied per draw. Acceptable for GLES2 but would need optimization for GLES3 workloads.

2. **Single device/queue:** One DkDevice, one DkQueue. No multi-threaded command recording.

3. **Fixed pool sizes:** All resource arrays are statically sized. Cannot dynamically grow if an app exceeds limits.

4. **Blend workaround fragility:** Raw cmdbuf injection depends on deko3d internal struct layout. Version-specific.

5. **MaxAttribs mismatch:** Mesa compiles with MaxAttribs=32 (for aliased attributes) but GL reports 16. Causes 2 dEQP failures.

6. **Stencil pipeline:** Hardware limitation — Replace/Zero/Invert/Wrap operations produce incorrect values. 33 dEQP failures.
