# Stencil Operation Failures — Root Cause Analysis

## Summary
~55 dEQP-GLES2 tests fail due to missing TiledCache barrier on Tegra X1 Maxwell GPU. **Fix applied: `DkBarrier_Tiles` after stencil clears and before stencil-enabled draws.**

## Root Cause: Missing TiledCacheBarrier

Maxwell GPU's Tiled Cache compresses and caches depth/stencil data. `DkBarrier_Full` (used by SwitchGLES) does **NOT** flush this cache — only `DkBarrier_Tiles` issues the `TiledCacheBarrier` GPU command.

After stencil writes (clear or draw), subsequent reads see stale compressed data from the tiled cache instead of actual written values.

### Proof from deko3d source (`gpu_base.cpp`)
```cpp
case DkBarrier_Tiles:
    needsWfi = true;
    w << CmdInline(3D, TiledCacheBarrier{}, 0);  // Line 63
    break;
case DkBarrier_Full:
    // Uses SetReference + split — does NOT issue TiledCacheBarrier
```

## Failure Pattern

### Passing operations (compatible with tile compression)
- `GL_KEEP` — No write, no stale data
- `GL_INCR` — Increment-clamp, predictable incremental change
- `GL_DECR` — Decrement-clamp, predictable incremental change

### Failing operations (break compression assumptions)
- `GL_REPLACE` — Writes arbitrary reference value
- `GL_ZERO` — Writes 0 (drastic change from any non-zero)
- `GL_INVERT` — Bitwise NOT (unpredictable result)
- `GL_INCR_WRAP` — Wraps around 255→0 (breaks incrementality)
- `GL_DECR_WRAP` — Wraps around 0→255 (breaks incrementality)
- `GL_NOTEQUAL` — Stencil comparison on stale data

## Fix Applied

### 1. After depth/stencil clear (`dk_clear.c`)
```c
dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Full,
                DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache | DkInvalidateFlags_Zcull);
dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Tiles, 0);  // NEW: flush tiled cache
```

### 2. Before stencil-enabled draws (`dk_state.c`)
```c
if (state->stencil_test_enabled) {
    dkCmdBufBarrier(dk->cmdbuf, DkBarrier_Tiles, 0);  // NEW: ensure coherency
}
```

## Affected Tests (~55)
- `fragment_ops.stencil.*` (8 tests)
- `fragment_ops.interaction.basic_shader.*` (14 tests)
- `fragment_ops.depth_stencil.random.*` (4 tests)
- `fragment_ops.random.*` (6 tests)
- `fbo.render.stencil.*` (8 tests)
- `fbo.render.resize.*_stencil_index8` (5 tests)
- `fbo.render.recreate_*.*_stencil_index8` (~10 tests)

## Verified NOT the Cause
- Stencil op D3D encoding (values 1-8 correct for Maxwell)
- DkDepthStencilState bitfield layout and MME macro
- GL-to-deko3d state mapping
- Stencil clear path (shadow RAM save/restore correct)
- Combined depth-stencil state (single bind prevents overwrite)
- Stencil write mask / reference / compare mask
