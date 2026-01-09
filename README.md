# SwitchGLES

**OpenGL ES 2.0 + EGL 1.4 implementation for Nintendo Switch using deko3d**

[![Version](https://img.shields.io/badge/version-0.1.0-blue.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)]()
[![Tests](https://img.shields.io/badge/tests-100%2F100%20pass-brightgreen.svg)]()

SwitchGLES provides a standards-compliant OpenGL ES 2.0 and EGL 1.4 API for Nintendo Switch homebrew applications. It translates graphics calls to deko3d, the native low-level graphics API.

## Highlights

- **Full OpenGL ES 2.0 core** - All functions implemented and validated
- **EGL 1.4 support** - Standard display/context management
- **SDL2 compatible** - Works with SDL2 for easy porting
- **Comprehensive test suite** - 100 automated tests, all passing

## Features

### EGL 1.4

- Display, surface, and context management
- Window surfaces with double-buffering
- VSync control via `eglSwapInterval`
- Extension query with `eglGetProcAddress`

### OpenGL ES 2.0

- **Programmable Pipeline**
  - Vertex and fragment shaders (precompiled DKSH format)
  - All uniform types (`float`, `int`, `vec2/3/4`, `mat2/3/4`)
  - Uniform buffers with `std140` layout
  - Attribute binding (`glVertexAttribPointer`)

- **Buffer Objects**
  - Vertex Buffer Objects (VBO)
  - Element Buffer Objects (EBO)
  - Client-side vertex arrays (interleaved supported)
  - `glBufferData`, `glBufferSubData`

- **Textures**
  - 2D textures and Cube maps
  - Multiple formats: RGBA, RGB, luminance, alpha
  - Compressed formats: ASTC, ETC2, BC/S3TC
  - Filtering, wrap modes, mipmaps (`glGenerateMipmap`)
  - `glTexSubImage2D`, `glCopyTexImage2D`, `glCopyTexSubImage2D`
  - Multiple texture units (0-7)

- **Framebuffer Objects**
  - Render-to-texture
  - Depth/stencil renderbuffers
  - `glReadPixels` (from default FB and FBOs)

- **State Management**
  - Blending with separate RGB/Alpha factors
  - Depth testing with all compare functions
  - Stencil testing
  - Face culling (front/back)
  - Scissor test
  - Color mask
  - Polygon offset (`glPolygonOffset`)

## Quick Start

### Prerequisites

- **devkitPro** with devkitA64 and libnx
- **deko3d** library (included with devkitPro)
- **UAM** shader compiler (included with devkitPro)

### Build and Install

```bash
# Build the library
make

# Install to devkitPro (recommended)
make install

# Or use from local path (see Installation section)
```

### Minimal Example

```c
#include <EGL/egl.h>
#include <GLES2/gl2.h>

int main() {
    // Initialize EGL
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, NULL, NULL);

    EGLConfig config;
    EGLint num_configs;
    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    eglChooseConfig(display, attribs, &config, 1, &num_configs);

    EGLSurface surface = eglCreateWindowSurface(display, config, NULL, NULL);
    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    eglMakeCurrent(display, surface, surface, context);

    // Render loop
    while (appletMainLoop()) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // ... draw your scene ...
        eglSwapBuffers(display, surface);
    }

    eglTerminate(display);
    return 0;
}
```

## Important: Shader Compilation

**deko3d does not support runtime GLSL compilation.** All shaders must be precompiled at build time using the UAM compiler.

### Compiling Shaders

```bash
# Compile vertex shader
uam -s vert shader.vert.glsl -o shader.vert.dksh

# Compile fragment shader
uam -s frag shader.frag.glsl -o shader.frag.dksh
```

### Loading Shaders at Runtime

```c
#include <GLES2/gl2sgl.h>  // SwitchGLES extensions

GLuint vs = glCreateShader(GL_VERTEX_SHADER);
GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

// Load precompiled DKSH files
sgl_load_shader_from_file(vs, "romfs:/shader.vert.dksh");
sgl_load_shader_from_file(fs, "romfs:/shader.frag.dksh");

// Link program as usual
GLuint program = glCreateProgram();
glAttachShader(program, vs);
glAttachShader(program, fs);
glLinkProgram(program);
glUseProgram(program);
```

### Shader Requirements

All shaders must use GLSL 460 with **UBO syntax** (std140 layout):

```glsl
#version 460

// Uniforms MUST use UBO syntax with std140 layout
layout(std140, binding = 0) uniform MatrixBlock {
    mat4 u_matrix;
};

layout(std140, binding = 1) uniform ColorBlock {
    vec4 u_color;
};

// Explicit locations for attributes
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texcoord;

layout(location = 0) out vec2 v_texcoord;

void main() {
    gl_Position = u_matrix * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
}
```

**Note:** Classic `uniform mat4 u_matrix;` syntax is NOT supported. Use UBO blocks.

### Registering Custom Uniforms

Since deko3d shaders are precompiled with explicit binding numbers, SwitchGLES needs to know which binding to use for each uniform name:

```c
#include <GLES2/gl2sgl.h>

// Register uniforms BEFORE using glGetUniformLocation
// Parameters: (name, stage, binding)
sglRegisterUniform("myModelView",  SGL_STAGE_VERTEX,   0);  // VS binding 0
sglRegisterUniform("myProjection", SGL_STAGE_VERTEX,   1);  // VS binding 1
sglRegisterUniform("myColor",      SGL_STAGE_FRAGMENT, 0);  // FS binding 0

// Now use standard GL calls
GLint mvLoc = glGetUniformLocation(program, "myModelView");
glUniformMatrix4fv(mvLoc, 1, GL_FALSE, modelViewMatrix);
```

**Built-in uniforms:** Common names like `u_mvp`, `u_color`, `u_matrix` are pre-registered.

## Installation

### Option 1: Install to devkitPro (Recommended)

```bash
make install
```

This copies:
- `lib/libSwitchGLES.a` to `$DEVKITPRO/portlibs/switch/lib/`
- Headers to `$DEVKITPRO/portlibs/switch/include/{EGL,GLES2,KHR}/`

After installation:
```makefile
LIBS := -lSwitchGLES -ldeko3d -lnx
```

### Option 2: Use from Local Path

```makefile
SWITCHGLES_DIR := /path/to/SwitchGLES
INCLUDES += $(SWITCHGLES_DIR)/include
LIBDIRS += $(SWITCHGLES_DIR)/lib
LIBS := -lSwitchGLES -ldeko3d -lnx
```

## SDL2 Integration

SwitchGLES works with SDL2 for easy porting of existing applications.

See `SDL/README_SWITCHGLES.md` for complete instructions on building SDL2 with the SwitchGLES backend.

```c
// SDL2 + SwitchGLES example
SDL_Init(SDL_INIT_VIDEO);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);

SDL_Window *window = SDL_CreateWindow("My App", 0, 0, 1280, 720, SDL_WINDOW_OPENGL);
SDL_GLContext context = SDL_GL_CreateContext(window);

// Use standard GL calls...
glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT);
SDL_GL_SwapWindow(window);
```

## API Coverage

### EGL Functions (All Implemented)

| Function | Status |
|----------|--------|
| eglGetDisplay, eglInitialize, eglTerminate | Implemented |
| eglChooseConfig, eglGetConfigs, eglGetConfigAttrib | Implemented |
| eglCreateWindowSurface, eglDestroySurface | Implemented |
| eglCreateContext, eglDestroyContext, eglMakeCurrent | Implemented |
| eglSwapBuffers, eglSwapInterval | Implemented |
| eglGetError, eglQueryString, eglGetProcAddress | Implemented |

### GLES2 Functions (All Core Functions Implemented)

| Category | Functions | Status |
|----------|-----------|--------|
| Clear | glClear, glClearColor, glClearDepthf, glClearStencil | Validated |
| Draw | glDrawArrays, glDrawElements | Validated |
| Buffers | glGenBuffers, glBindBuffer, glBufferData, glBufferSubData | Validated |
| Textures | glGenTextures, glBindTexture, glTexImage2D, glTexSubImage2D | Validated |
| Textures | glTexParameteri, glGenerateMipmap | Validated |
| Textures | glCopyTexImage2D, glCopyTexSubImage2D | Validated |
| Textures | glCompressedTexImage2D, glCompressedTexSubImage2D | Validated |
| Shaders | glCreateShader, glAttachShader, glLinkProgram, glUseProgram | Validated |
| Uniforms | glGetUniformLocation, glUniform{1234}{fi}[v] | Validated |
| Uniforms | glUniformMatrix{234}fv | Validated |
| Attributes | glGetAttribLocation, glVertexAttribPointer, glEnableVertexAttribArray | Validated |
| State | glEnable, glDisable, glIsEnabled | Validated |
| Depth | glDepthFunc, glDepthMask, glDepthRangef | Validated |
| Blend | glBlendFunc, glBlendFuncSeparate, glBlendEquation | Validated |
| Stencil | glStencilFunc, glStencilOp, glStencilMask | Validated |
| Culling | glCullFace, glFrontFace | Validated |
| Scissor | glScissor | Validated |
| Viewport | glViewport | Validated |
| Color | glColorMask | Validated |
| FBO | glGenFramebuffers, glBindFramebuffer, glFramebufferTexture2D | Validated |
| FBO | glGenRenderbuffers, glRenderbufferStorage, glFramebufferRenderbuffer | Validated |
| Readback | glReadPixels | Validated |
| Query | glGetIntegerv, glGetFloatv, glGetString, glGetError | Validated |
| Misc | glPolygonOffset, glLineWidth, glPixelStorei | Validated |

### SwitchGLES Extensions

```c
#include <GLES2/gl2sgl.h>

// Shader loading
bool sgl_load_shader_from_file(GLuint shader, const char *path);

// Uniform registration
void sglRegisterUniform(const char *name, int stage, int binding);
void sglClearUniformRegistry(void);

// Constants
#define SGL_STAGE_VERTEX   0
#define SGL_STAGE_FRAGMENT 1
```

### Known Limitations

| Limitation | Notes |
|------------|-------|
| Runtime shader compilation | Not supported - use precompiled DKSH |
| GL_UNSIGNED_BYTE indices | Auto-converted to 16-bit (Maxwell GPU limitation) |
| Stencil + colorMask(0,0,0,0) | May not work correctly on some operations |

## Technical Details

### Memory Allocation

| Memory Pool | Size | Purpose |
|-------------|------|---------|
| Code memory | 64 KB | Shader code |
| Data memory | 16 MB | Vertex/index buffers, uniforms |
| Texture memory | 32 MB | Texture images |
| Descriptor memory | 4 KB | Texture/sampler descriptors |

### Important: Uniforms Must Be Set Every Frame

SwitchGLES resets uniform memory each frame. Unlike standard OpenGL, ALL uniforms must be set every frame:

```c
// WRONG - uniform set only once
void init() { glUniformMatrix4fv(projLoc, ...); }
void render() { glDrawArrays(...); }  // Frame 2+: garbage!

// CORRECT - set all uniforms every frame
void render() {
    glUniformMatrix4fv(projLoc, ...);
    glDrawArrays(...);
}
```

### Coordinate System

- Depth range: [0, 1]
- Origin: Lower-left
- Y-axis: Points up (OpenGL convention)

## Troubleshooting

### Black screen
- Check shader paths (use romfs:/ prefix)
- Ensure shaders use UBO syntax, not classic uniforms
- Verify `eglSwapBuffers` is called

### Object appears then disappears
- Set ALL uniforms every frame (see above)

### Texture appears white
- Check texture format matches data
- Use GL_NEAREST filtering for debugging

## Examples

| Example | Description |
|---------|-------------|
| `01_textured_quad` | Basic textured quad - texture loading and UV mapping |
| `02_es2gears` | Classic gears demo - animated 3D scene with lighting |
| `03_fbo` | Framebuffer Objects - render-to-texture (14 tests) |
| `04_cubemap` | Cubemap textures - environment mapping |
| `validation_test` | Comprehensive test suite (100 tests) |

Build and run an example:
```bash
cd examples/02_es2gears
make
nxlink -a <switch_ip> -s 02_es2gears.nro
```

## License

MIT License - See LICENSE file

## Credits

- Built on [deko3d](https://github.com/devkitPro/deko3d) by fincs
- Architecture inspired by [GLOVE](https://github.com/aspect/glove) (GL Over Vulkan)
- EGL/GLES2 headers from [Khronos Group](https://www.khronos.org/)
