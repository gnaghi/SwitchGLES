# Migration Analysis: OpenGL ES 2.0 + EGL for Nintendo Switch

## Executive Summary

This document analyzes the transformation of SwitchGLES from an OpenGL 1.x implementation to an **OpenGL ES 2.0 + EGL** implementation for Nintendo Switch homebrew, using deko3d as the graphics backend.

---

## 1. API Scope

### 1.1 EGL Functions (34 functions)

**EGL 1.0 Core (19 functions)**
| Function | Priority | Notes |
|----------|----------|-------|
| `eglGetDisplay` | Critical | Returns display handle |
| `eglInitialize` | Critical | Initializes EGL, creates deko3d device |
| `eglTerminate` | Critical | Cleanup |
| `eglGetConfigs` | Critical | List available configs |
| `eglChooseConfig` | Critical | Select config based on attributes |
| `eglGetConfigAttrib` | Critical | Query config properties |
| `eglCreateWindowSurface` | Critical | Create render surface (swapchain) |
| `eglCreatePbufferSurface` | Medium | Offscreen rendering |
| `eglCreatePixmapSurface` | Low | Rarely used |
| `eglDestroySurface` | Critical | Cleanup surface |
| `eglCreateContext` | Critical | Create GL context |
| `eglDestroyContext` | Critical | Cleanup context |
| `eglMakeCurrent` | Critical | Bind context to surfaces |
| `eglSwapBuffers` | Critical | Present to screen |
| `eglQueryContext` | Medium | Query context attributes |
| `eglQuerySurface` | Medium | Query surface attributes |
| `eglQueryString` | Medium | Query EGL strings |
| `eglGetError` | Critical | Error handling |
| `eglGetProcAddress` | High | Extension loading |

**EGL 1.1+ Extensions (15 functions)**
| Function | Priority | Notes |
|----------|----------|-------|
| `eglBindAPI` | High | Select OpenGL ES API |
| `eglQueryAPI` | Medium | Query current API |
| `eglSwapInterval` | High | VSync control |
| `eglGetCurrentContext` | Medium | Query current context |
| `eglGetCurrentDisplay` | Medium | Query current display |
| `eglGetCurrentSurface` | Medium | Query current surface |
| `eglWaitGL` / `eglWaitClient` | Low | Synchronization |
| `eglWaitNative` | Low | Native sync |
| `eglBindTexImage` / `eglReleaseTexImage` | Low | Texture from surface |
| `eglReleaseThread` | Low | Thread cleanup |
| `eglSurfaceAttrib` | Low | Surface attributes |
| `eglCopyBuffers` | Low | Copy to pixmap |

### 1.2 OpenGL ES 2.0 Functions (142 functions)

**State Management (~20 functions)**
- `glEnable`, `glDisable`, `glIsEnabled`
- `glBlendFunc`, `glBlendFuncSeparate`, `glBlendEquation`, `glBlendEquationSeparate`, `glBlendColor`
- `glDepthFunc`, `glDepthMask`, `glDepthRangef`
- `glStencilFunc`, `glStencilFuncSeparate`, `glStencilMask`, `glStencilMaskSeparate`, `glStencilOp`, `glStencilOpSeparate`
- `glCullFace`, `glFrontFace`
- `glColorMask`, `glPolygonOffset`, `glLineWidth`
- `glViewport`, `glScissor`

**Clear Operations (~4 functions)**
- `glClear`, `glClearColor`, `glClearDepthf`, `glClearStencil`

**Texture Functions (~15 functions)**
- `glGenTextures`, `glDeleteTextures`, `glBindTexture`, `glIsTexture`
- `glActiveTexture`
- `glTexImage2D`, `glTexSubImage2D`
- `glCompressedTexImage2D`, `glCompressedTexSubImage2D`
- `glCopyTexImage2D`, `glCopyTexSubImage2D`
- `glTexParameterf`, `glTexParameterfv`, `glTexParameteri`, `glTexParameteriv`
- `glGetTexParameterfv`, `glGetTexParameteriv`
- `glGenerateMipmap`

**Buffer Functions (~8 functions)**
- `glGenBuffers`, `glDeleteBuffers`, `glBindBuffer`, `glIsBuffer`
- `glBufferData`, `glBufferSubData`
- `glGetBufferParameteriv`

**Framebuffer Functions (~12 functions)**
- `glGenFramebuffers`, `glDeleteFramebuffers`, `glBindFramebuffer`, `glIsFramebuffer`
- `glGenRenderbuffers`, `glDeleteRenderbuffers`, `glBindRenderbuffer`, `glIsRenderbuffer`
- `glFramebufferTexture2D`, `glFramebufferRenderbuffer`
- `glRenderbufferStorage`
- `glCheckFramebufferStatus`
- `glGetFramebufferAttachmentParameteriv`, `glGetRenderbufferParameteriv`

**Shader Functions (~25 functions)**
- `glCreateShader`, `glDeleteShader`, `glIsShader`
- `glShaderSource`, `glCompileShader`, `glShaderBinary`
- `glGetShaderiv`, `glGetShaderInfoLog`, `glGetShaderSource`
- `glGetShaderPrecisionFormat`, `glReleaseShaderCompiler`
- `glCreateProgram`, `glDeleteProgram`, `glIsProgram`
- `glAttachShader`, `glDetachShader`, `glLinkProgram`, `glValidateProgram`, `glUseProgram`
- `glGetProgramiv`, `glGetProgramInfoLog`, `glGetAttachedShaders`
- `glBindAttribLocation`, `glGetAttribLocation`
- `glGetActiveAttrib`, `glGetActiveUniform`
- `glGetUniformLocation`, `glGetUniformfv`, `glGetUniformiv`

**Uniform Functions (~24 functions)**
- `glUniform1f`, `glUniform2f`, `glUniform3f`, `glUniform4f`
- `glUniform1fv`, `glUniform2fv`, `glUniform3fv`, `glUniform4fv`
- `glUniform1i`, `glUniform2i`, `glUniform3i`, `glUniform4i`
- `glUniform1iv`, `glUniform2iv`, `glUniform3iv`, `glUniform4iv`
- `glUniformMatrix2fv`, `glUniformMatrix3fv`, `glUniformMatrix4fv`

**Vertex Attribute Functions (~18 functions)**
- `glVertexAttrib1f`, `glVertexAttrib2f`, `glVertexAttrib3f`, `glVertexAttrib4f`
- `glVertexAttrib1fv`, `glVertexAttrib2fv`, `glVertexAttrib3fv`, `glVertexAttrib4fv`
- `glVertexAttribPointer`
- `glEnableVertexAttribArray`, `glDisableVertexAttribArray`
- `glGetVertexAttribfv`, `glGetVertexAttribiv`, `glGetVertexAttribPointerv`

**Drawing Functions (~2 functions)**
- `glDrawArrays`, `glDrawElements`

**Query/Misc Functions (~15 functions)**
- `glGetBooleanv`, `glGetFloatv`, `glGetIntegerv`
- `glGetString`, `glGetError`
- `glPixelStorei`, `glReadPixels`
- `glHint`, `glFlush`, `glFinish`
- `glSampleCoverage`

---

## 2. Key Differences from OpenGL 1.x

### 2.1 Removed Features (Not in GLES2)

| Feature | OpenGL 1.x | GLES 2.0 |
|---------|-----------|----------|
| Fixed Function Pipeline | Yes | **NO** |
| Immediate Mode (glBegin/glEnd) | Yes | **NO** |
| Matrix Stack (glPushMatrix, etc.) | Yes | **NO** |
| glVertex, glColor, glNormal, glTexCoord | Yes | **NO** |
| Built-in Lighting | Yes | **NO** |
| Built-in Fog | Yes | **NO** |
| Display Lists | Yes | **NO** |
| Alpha Test (glAlphaFunc) | Yes | **NO** (use discard) |
| GL_QUADS primitive | Yes | **NO** |

### 2.2 Required Features (GLES2 Only)

| Feature | Description |
|---------|-------------|
| Programmable Shaders | Vertex + Fragment shaders required |
| VBOs | Vertex Buffer Objects for geometry |
| Uniforms | All parameters via uniforms |
| Vertex Attributes | Generic vertex attributes |
| Separate Blend/Stencil per face | StencilFuncSeparate, etc. |
| Framebuffer Objects | FBO for render-to-texture |
| glClearDepthf | Float version (not glClearDepth) |
| glDepthRangef | Float version |

---

## 3. Code Reuse Analysis

### 3.1 Fully Reusable (~70%)

| Component | File | Notes |
|-----------|------|-------|
| deko3d Initialization | sgl_init.c | Device, queue, swapchain |
| Memory Management | sgl_init.c | DkMemBlock allocation |
| Command Buffer System | sgl_init.c | Double-buffered cmdbuf |
| Texture Management | sgl_draw.c | glTexImage2D, descriptors |
| Buffer Objects | sgl_draw.c | VBO/EBO handling |
| State Conversion | sgl_state.c | GL→deko3d conversions |
| Framebuffer Objects | sgl_draw.c | FBO implementation |
| Renderbuffers | sgl_draw.c | RBO implementation |
| Blend State | sgl_state.c | Full blend support |
| Depth/Stencil | sgl_state.c | Full depth/stencil support |
| Viewport/Scissor | sgl_state.c | Already implemented |
| VAO/Vertex Attribs | sgl_draw.c | Already implemented |

### 3.2 Partially Reusable (~20%)

| Component | Changes Needed |
|-----------|----------------|
| Shader Objects | Add glShaderBinary, improve compilation stub |
| Program Objects | Add proper uniform tracking |
| Uniform System | Add full glUniform* family |
| glDrawArrays/Elements | Keep core, remove FFP path |

### 3.3 To Remove (~10%)

| Component | Reason |
|-----------|--------|
| Matrix Stack | Not in GLES2 |
| Immediate Mode | glBegin/glEnd not in GLES2 |
| Lighting System | Not in GLES2 |
| Fog System | Not in GLES2 |
| FFP Shaders | Not needed for GLES2 |
| Color Material | Not in GLES2 |
| Alpha Test | Not in GLES2 (use shader discard) |

---

## 4. Shader Compilation Strategy

### 4.1 The Problem

deko3d **does not support runtime GLSL compilation**. It only accepts precompiled DKSH (deko3d shader) binaries containing native Maxwell SASS code.

### 4.2 The Solution: glShaderBinary

OpenGL ES 2.0 provides `glShaderBinary()` for loading precompiled shader binaries:

```c
void glShaderBinary(GLsizei count, const GLuint *shaders,
                    GLenum binaryFormat, const void *binary, GLsizei length);
```

**Our Implementation:**
- Define custom binary format: `GL_SHADER_BINARY_DKSH` (0x10000)
- `glShaderBinary` loads DKSH data directly
- `glCompileShader` returns `GL_FALSE` with info log explaining to use glShaderBinary
- `glLinkProgram` creates deko3d shader pipeline from DKSH data

### 4.3 Workflow for Users

```bash
# At build time: compile GLSL to DKSH
uam -s vert shader.vert.glsl -o shader.vert.dksh
uam -s frag shader.frag.glsl -o shader.frag.dksh
```

```c
// At runtime: load binary shaders
GLuint vs = glCreateShader(GL_VERTEX_SHADER);
GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

// Load DKSH from file
void *vs_data = load_file("shader.vert.dksh", &vs_size);
void *fs_data = load_file("shader.frag.dksh", &fs_size);

// Use glShaderBinary instead of glShaderSource + glCompileShader
glShaderBinary(1, &vs, GL_SHADER_BINARY_DKSH, vs_data, vs_size);
glShaderBinary(1, &fs, GL_SHADER_BINARY_DKSH, fs_data, fs_size);

// Link as usual
GLuint program = glCreateProgram();
glAttachShader(program, vs);
glAttachShader(program, fs);
glLinkProgram(program);
glUseProgram(program);
```

### 4.4 glCompileShader Behavior

```c
void glCompileShader(GLuint shader) {
    sgl_shader_obj *s = &g_sgl.shaders[shader];
    s->compiled = GL_FALSE;
    snprintf(s->info_log, sizeof(s->info_log),
        "ERROR: Runtime GLSL compilation not supported on Nintendo Switch.\n"
        "Use glShaderBinary() with DKSH format instead.\n"
        "Compile shaders offline with: uam -s <stage> input.glsl -o output.dksh");
}
```

---

## 5. New Architecture

### 5.1 File Structure

```
SwitchGLES/
├── include/
│   ├── EGL/
│   │   ├── egl.h              (Khronos standard)
│   │   ├── eglext.h           (Khronos standard)
│   │   └── eglplatform.h      (Switch-specific types)
│   ├── GLES2/
│   │   ├── gl2.h              (Khronos standard)
│   │   ├── gl2ext.h           (Khronos standard)
│   │   └── gl2platform.h      (Khronos standard)
│   └── KHR/
│       └── khrplatform.h      (Khronos base types)
├── source/
│   ├── sgl_internal.h         (Internal structures) [MODIFY]
│   ├── sgl_context.c          (Context management) [NEW]
│   ├── sgl_egl.c              (EGL implementation) [NEW]
│   ├── sgl_gles2.c            (GLES2 core) [NEW/MODIFY]
│   ├── sgl_shader.c           (Shader system) [MODIFY]
│   ├── sgl_texture.c          (Textures) [EXTRACT]
│   ├── sgl_buffer.c           (VBOs) [EXTRACT]
│   ├── sgl_framebuffer.c      (FBOs) [EXTRACT]
│   ├── sgl_state.c            (State management) [MODIFY]
│   └── sgl_draw.c             (Drawing) [MODIFY]
├── Makefile
└── README.md
```

### 5.2 Internal Structures (Updated)

```c
/* EGL Display */
typedef struct {
    bool initialized;
    DkDevice device;
    int major_version;
    int minor_version;
} sgl_egl_display;

/* EGL Surface */
typedef struct {
    bool used;
    EGLint width, height;
    DkSwapchain swapchain;
    DkMemBlock framebuffer_mem;
    DkImage framebuffers[SGL_NUM_FRAMEBUFFERS];
    DkImageView framebuffer_views[SGL_NUM_FRAMEBUFFERS];
    int current_slot;
} sgl_egl_surface;

/* EGL Context */
typedef struct {
    bool used;
    EGLint client_version;  /* 2 for GLES2 */
    DkQueue queue;
    DkMemBlock cmd_mem[SGL_NUM_FRAMEBUFFERS];
    DkCmdBuf cmdbuf[SGL_NUM_FRAMEBUFFERS];
    DkFence fence[SGL_NUM_FRAMEBUFFERS];
    int current_frame;

    /* GL state */
    sgl_gles2_state state;

    /* Resources */
    sgl_texture textures[SGL_MAX_TEXTURES];
    sgl_buffer buffers[SGL_MAX_BUFFERS];
    sgl_shader shaders[SGL_MAX_SHADERS];
    sgl_program programs[SGL_MAX_PROGRAMS];
    sgl_framebuffer framebuffers[SGL_MAX_FRAMEBUFFERS];
    sgl_renderbuffer renderbuffers[SGL_MAX_RENDERBUFFERS];
} sgl_egl_context;

/* Shader object (GLES2) */
typedef struct {
    bool used;
    GLenum type;           /* GL_VERTEX_SHADER or GL_FRAGMENT_SHADER */
    char *source;          /* GLSL source (for glShaderSource) */
    void *binary;          /* DKSH binary data (for glShaderBinary) */
    size_t binary_size;
    bool compiled;         /* True only if loaded via glShaderBinary */
    char info_log[512];
} sgl_shader;

/* Program object (GLES2) */
typedef struct {
    bool used;
    GLuint vertex_shader;
    GLuint fragment_shader;
    bool linked;
    char info_log[512];

    /* deko3d shaders */
    DkShader dk_vertex_shader;
    DkShader dk_fragment_shader;
    DkMemBlock shader_code_mem;

    /* Uniform tracking */
    sgl_uniform uniforms[SGL_MAX_UNIFORMS];
    int num_uniforms;

    /* Attribute tracking */
    sgl_attrib_binding attribs[SGL_MAX_VERTEX_ATTRIBS];
    int num_attribs;
} sgl_program;
```

---

## 6. Implementation Phases

### Phase 1: Foundation (EGL Core) - DONE
- [x] Create KHR/khrplatform.h
- [x] Implement eglGetDisplay, eglInitialize, eglTerminate
- [x] Implement eglGetConfigs, eglChooseConfig, eglGetConfigAttrib
- [x] Implement eglCreateWindowSurface, eglDestroySurface
- [x] Implement eglCreateContext, eglDestroyContext, eglMakeCurrent
- [x] Implement eglSwapBuffers, eglGetError, eglQueryString

### Phase 2: GLES2 State - DONE
- [x] Implement all glEnable/glDisable caps (depth, blend, cull, scissor)
- [x] Implement blend, depth, stencil state functions
- [x] Implement viewport, scissor, clear functions
- [x] State applied via sgl_apply_* functions in sgl_bind_program_shaders

### Phase 3: Buffers - DONE / Textures - PENDING
- [x] VBO code (glGenBuffers, glBindBuffer, glBufferData)
- [x] IBO code (glDrawElements with GL_UNSIGNED_BYTE→UNSIGNED_SHORT conversion)
- [ ] Texture code (glGenTextures, glTexImage2D, glBindTexture)
- [ ] Sampler binding to shader

### Phase 4: Shader System - DONE
- [x] Implement sgl_load_shader_from_file (loads DKSH binary)
- [x] Stub glCompileShader (always fails - no runtime compilation)
- [x] Implement glCreateProgram, glLinkProgram, glUseProgram
- [x] Implement glUniformMatrix4fv (std140 uniform buffer)
- [x] Implement glGetUniformLocation (returns binding for known uniforms)

### Phase 5: Drawing - DONE
- [x] glDrawArrays for GLES2
- [x] glDrawElements for GLES2 (auto-converts UNSIGNED_BYTE to UNSIGNED_SHORT)
- [x] glVertexAttribPointer
- [x] glEnableVertexAttribArray/glDisableVertexAttribArray

### Phase 6: Framebuffers - PENDING
- [ ] FBO code for GLES2
- [ ] glCheckFramebufferStatus
- [ ] Render-to-texture

### Phase 7: Polish - PENDING
- [ ] Remaining query functions
- [ ] Extension support (eglGetProcAddress)
- [ ] Test with real GLES2 applications
- [ ] Performance optimization

---

## 7. Compatibility Notes

### 7.1 What Will Work
- Any GLES2 application using precompiled shaders
- SDL2 applications (with SDL_VIDEODRIVER=switch)
- PPSSPP, RetroArch cores using GLES2
- Custom engines with shader precompilation

### 7.2 What Won't Work
- Applications requiring runtime GLSL compilation
- OpenGL ES 1.x applications (no FFP)
- Applications using unsupported extensions

### 7.3 Shader Requirements
All shaders must:
1. Be compiled with UAM at build time
2. Use explicit binding qualifiers: `layout(binding = N)`
3. Target GLSL 450 (deko3d requirement)
4. Be loaded via glShaderBinary at runtime

---

## 8. Testing Strategy

### 8.1 test_unit Levels (Validated on Switch Hardware)

| Level | Feature | Status |
|-------|---------|--------|
| 1 | EGL + glClear | PASS |
| 2 | VBO Create | PASS |
| 3 | VBO Bind + Attribs | PASS |
| 4 | Shader Load (DKSH) | PASS |
| 5 | glDrawArrays (interleaved) | PASS |
| 6 | Two separate VBOs | PASS |
| 7 | glDrawElements (IBO) | PASS |
| 8 | Depth Test | PASS |
| 9 | Uniform Buffer (MVP) | PASS |
| 10 | Textures | TODO |

### 8.2 Integration Tests
- [x] Triangle rendering (test_unit level 5-6)
- [ ] Textured quad (test_unit level 10)
- [ ] Rotating cube (simplecube example)
- [ ] FBO render-to-texture
- [ ] Complex scene (gears example)

### 8.3 Compatibility Tests
- [ ] SDL2 + GLES2 sample
- [ ] Simple game port

### 8.4 Known Limitations

1. **No runtime GLSL compilation** - Use precompiled DKSH shaders
2. **GL_UNSIGNED_BYTE indices converted** - Performance cost for conversion
3. **Uniform names hardcoded** - glGetUniformLocation only recognizes known names (u_mvp, etc.)

---

*Document created: January 11, 2026*
*Last updated: January 18, 2026*
*Project: SwitchGLES - OpenGL ES 2.0 + EGL for Nintendo Switch*
