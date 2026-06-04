# Reflection sidecar (`.refl`) — precompiled shader uniform resolution

This documents how SwitchGLES resolves `glGetUniformLocation()` /
`glGetAttribLocation()` for **precompiled** DKSH shaders by consuming the
`.refl` reflection sidecar emitted by uam. It is the SwitchGLES side of
[`UAM_REFLECTION_PLAN.md`](UAM_REFLECTION_PLAN.md); the uam side (format + how to
emit) is documented in `uam/README.md`.

## The problem it solves

`glGetUniformLocation(name)` resolves names in this order:

1. **User registry** — names registered via `sglRegisterUniform()` /
   `sglRegisterPackedUniform()`.
2. **Program reflection** — `prog->program_uniforms` / `prog->samplers`,
   populated at link time from the shader's metadata.
3. ~~Built-in hardcoded name table~~ — **removed** (see below).

Runtime-compiled shaders (the ~99.7% path) fill the program reflection (2) from
live uam/transpiler metadata, so any name resolves. A **precompiled** `.dksh`,
however, carries only numeric bindings — no GL names — so historically it had no
reflection and fell through to a ~120-entry `strcmp` table of hardcoded built-in
names (`u_mvp`, `u_color`, `es2gears`, `SDL_Renderer`, spearmint, dEQP, …). That
table was a maintenance sink and only ever covered names someone had hardcoded.

## How it works now

uam emits a `.refl` sidecar next to each precompiled `.dksh` (see
`uam --reflect`). SwitchGLES loads it and feeds the **same** reflection tables a
runtime-compiled shader would, so step 2 above already covers every name.

### Load path — `sgl_load_shader_from_file()` (`source/gl/gl_shader.c`)

After the backend loads `<name>.dksh`, `sgl_load_reflection_sidecar()` looks for
`<name>.dksh.refl`. If present and valid (magic `SGLR`, matching version), it:

- parses the header + uniform/sampler/input records + initial constbuf data into
  a heap `sgl_mesa_metadata_t` (the very struct the runtime Mesa path produces),
  converting `(base_type, vector_elements, matrix_columns)` to a `GLenum` via the
  shared `uam_base_type_to_gl()`;
- attaches it as `shader->mesa_meta` and sets `shader->compiled_via_mesa = true`.

A missing or malformed sidecar is **not** an error — the shader still loads and
falls back to `sglRegisterUniform()`.

### Link path — `sgl_link_program_mesa()` (`source/gl/gl_program.c`)

Because the shader now looks Mesa-compiled (`compiled_via_mesa` + `mesa_meta`),
the existing `glLinkProgram` Mesa path populates `prog->program_uniforms`,
`prog->samplers` (incl. sampler arrays), `prog->attrib_bindings`, packed-UBO
sizes, dual-stage mirrors and `gl_DepthRange` locations — with **no new code**.
Precompiled and runtime-compiled shaders share one reflection populate path.

## Consequences

- **Built-in `strcmp` table removed.** The ~120-entry uniform name table in
  `glGetUniformLocation()` (`source/gl/gl_uniform.c`) is gone; resolution relies
  entirely on the registry + program reflection.
- **`sglRegisterUniform()` is kept** as the documented fallback for legacy
  precompiled binaries shipped *without* a `.refl`. Do not remove it.
- The whole consumer is under `#ifdef SGL_ENABLE_RUNTIME_COMPILER` (the shipping
  configuration — uam is always bundled into `libSwitchGLES.a`), matching the
  rest of the `mesa_meta` machinery.

## Compatibility matrix

| Shader source | `.refl` present | Resolves via |
|---------------|-----------------|--------------|
| Runtime-compiled (GLSL ES source) | n/a | program reflection (live metadata) |
| Precompiled `.dksh` | yes | program reflection (from `.refl`) |
| Precompiled `.dksh` | no | `sglRegisterUniform()` only |

## Producing a `.refl` for an example

```bash
uam -s vert shader.vert.glsl -o shader.vert.dksh --reflect
uam -s frag shader.frag.glsl -o shader.frag.dksh --reflect
# ships shader.vert.dksh + shader.vert.dksh.refl (+ frag)
```

## Validation checklist (hardware)

The consumer compiles cleanly, but end-to-end behaviour must be confirmed on
device (cannot be exercised by the host build):

1. Build `uam.exe`, regenerate a precompiled example's `.dksh` **with**
   `--reflect`, and verify `glGetUniformLocation()` finds a name that is *not* in
   the old built-in table and was *not* `sglRegisterUniform()`-ed.
2. Run a dEQP-GLES2 regression — the runtime path must be unchanged (the table
   removal only affects precompiled-without-`.refl` shaders).
