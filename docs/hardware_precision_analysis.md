# Hardware Precision Failures — Tegra X1 Maxwell MUFU/SFU

## Summary
7 dEQP-GLES2 tests fail due to GPU Special Function Unit (MUFU) precision limitations on the Tegra X1. These are **not software bugs** — the GPU meets GLES2 minimum precision requirements but produces results outside dEQP's comparison thresholds.

## Root Causes

### pow/log2 (2 tests)
- `pow(x, y)` decomposes to `EX2(MUL(y, LG2(x)))` — each MUFU op has ~22-bit mantissa precision
- Compound error from 3-instruction chain exceeds the 0.07f fuzzy threshold for edge inputs
- `log2()` uses single MUFU LG2, precision is ~22 bits vs CPU double-precision reference

### Random shaders (5 tests)
- All use tight 0.02f comparison threshold
- scalar_conversion: float-to-int rounding differences near integer boundaries
- conditionals: 1 ULP precision difference flips a branch → completely different pixel
- trigonometric: MUFU sin/cos approximation differs from CPU reference
- texture: GPU texture filtering hardware interpolation differs slightly

## Technical Details
- NV50_IR does NOT lower mediump to F16 — all operations run at F32
- GLES2 spec requires 10-bit mantissa for mediump, 16-bit for highp — GPU provides ~22-23 bits
- The failures are **within the GLES2 spec** but outside dEQP's fixed thresholds
- 497+ similar failures already documented in known_fail.txt from Nouveau baseline

## Conclusion
No software fix possible. These are inherent to the Maxwell MUFU hardware implementation, consistent with the known Nouveau precision profile. The GPU meets GLES2 minimum precision requirements — the failures are in dEQP's comparison tolerance, not in spec non-compliance.
