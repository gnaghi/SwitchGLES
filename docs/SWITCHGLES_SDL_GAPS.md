# SwitchGLES - Analyse des Fonctionnalités pour SDL

Ce document analyse les fonctionnalités de SwitchGLES nécessaires pour un support SDL fonctionnel.

**Dernière mise à jour : 18 février 2026**

---

## Approche : Transparence Totale OpenGL ES 2.0

**SwitchGLES est une implémentation complète d'OpenGL ES 2.0 + EGL 1.4.** SDL n'a besoin d'aucun code spécifique à SwitchGLES : uniquement des appels EGL/GLES2 standards. SwitchGLES gère automatiquement la transpilation et la compilation des shaders.

### Pipeline de Compilation des Shaders

SwitchGLES supporte trois chemins pour les shaders :

| Chemin | Entrée | Mécanisme | Transparent |
|--------|--------|-----------|-------------|
| **Auto-transpilation** | GLSL ES 1.00 (`#version 100`) | Transpileur intégré → libuam → DKSH | ✅ Oui |
| **Compilation directe** | GLSL 4.60 (`#version 460`) | libuam → DKSH | ✅ Oui |
| **Binaires précompilés** | Fichiers `.dksh` | `glShaderBinary()` ou `sgl_load_shader_from_file()` | Non (API propriétaire) |

Le chemin **auto-transpilation** est celui qui rend SDL transparent. Quand une application (SDL ou autre) appelle le flux standard GLES2 :

```c
glShaderSource(shader, 1, &glsl_es_source, NULL);
glCompileShader(shader);
```

SwitchGLES :
1. Détecte que le source est GLSL ES 1.00 (présence de `#version 100`, `attribute`, `varying`, ou `precision`)
2. Stocke le source pour transpilation différée
3. Au moment de `glLinkProgram()` :
   - Récupère les `glBindAttribLocation()` enregistrés sur le programme
   - Transpile le vertex shader (GLSL ES 1.00 → GLSL 4.60 core profile)
   - Passe les locations de varyings du VS au FS pour cohérence
   - Transpile le fragment shader
   - Enregistre automatiquement les uniforms (packed UBO) depuis la réflexion du transpileur
   - Compile les deux shaders transpilés via libuam → DKSH
   - Charge les binaires DKSH dans le backend deko3d

### Conséquence pour SDL

Le renderer GLES2 de SDL (`SDL_render_gles2.c`) utilise `glShaderSource()` + `glCompileShader()` + `glLinkProgram()` avec ses shaders GLSL ES internes. Le pipeline d'auto-transpilation est **entièrement implémenté** dans SwitchGLES, y compris la gestion des macros préprocesseur de précision. **Aucune modification n'est nécessaire côté SDL** — le `#ifdef SDL_VIDEO_SWITCH_SGL` dans `GLES2_CacheShader()` peut être supprimé.

---

## Architecture

```
┌─────────────────────────────────────────────┐
│              Application                     │
│   SDL_CreateWindow + SDL_CreateRenderer      │
│   ou SDL_GL_CreateContext + GLES2 direct     │
└──────────────────┬──────────────────────────┘
                   │ Appels GLES2 / EGL standards
┌──────────────────▼──────────────────────────┐
│                SDL2 / SDL3                   │
│  - Gestion fenêtres (EGL)                   │
│  - Renderer GLES2 (shaders GLSL ES 1.00)    │
│  - Entrées (joysticks, touch, clavier)      │
│  - Audio                                    │
└──────────────────┬──────────────────────────┘
                   │ Appels GLES2 / EGL standards
┌──────────────────▼──────────────────────────┐
│              SwitchGLES                      │
│  - EGL 1.4                                  │
│  - OpenGL ES 2.0 complet                    │
│  - Transpileur GLSL ES 1.00 → GLSL 4.60    │
│  - Compilateur runtime (libuam)             │
│  - Packed uniforms (std140 UBO)             │
└──────────────────┬──────────────────────────┘
                   │
┌──────────────────▼──────────────────────────┐
│               deko3d                         │
└─────────────────────────────────────────────┘
```

**Point clé :** Aucune flèche "spéciale" entre SDL et SwitchGLES. SDL voit SwitchGLES comme une implémentation OpenGL ES 2.0 / EGL 1.4 standard.

---

## Ce Qui Fonctionne

| Fonctionnalité | Status | Notes |
|----------------|--------|-------|
| SDL_Init(SDL_INIT_VIDEO) | ✅ | |
| SDL_Init(SDL_INIT_JOYSTICK) | ✅ | |
| SDL_CreateWindow(SDL_WINDOW_OPENGL) | ✅ | |
| SDL_GL_CreateContext | ✅ | Contexte EGL standard |
| SDL_GL_SwapWindow | ✅ | eglSwapBuffers |
| **SDL_CreateRenderer** | ✅ | **Via auto-transpilation des shaders GLSL ES** |
| **SDL_RenderFillRect / SDL_RenderCopy / SDL_RenderPresent** | ✅ | **Renderer GLES2 complet** |
| SDL_PollEvent | ✅ | Joystick, touch, clavier, souris |
| SDL_JoystickOpen | ✅ | 8 manettes max |
| Appels GLES2 directs | ✅ | Shaders GLSL ES 1.00 ou GLSL 4.60 |

---

## Transpileur GLSL Intégré

Le transpileur (`source/transpiler/glsl_transpiler.{h,c}`) convertit GLSL ES 1.00 en GLSL 4.60 core profile compatible deko3d.

### Transformations effectuées

| GLSL ES 1.00 | GLSL 4.60 |
|---------------|-----------|
| `#version 100` | `#version 460` |
| `precision mediump float;` | *(supprimé)* |
| `attribute vec2 a_position;` | `layout(location=0) in vec2 a_position;` |
| `varying vec2 v_texCoord;` (VS) | `layout(location=0) out vec2 v_texCoord;` |
| `varying vec2 v_texCoord;` (FS) | `layout(location=0) in vec2 v_texCoord;` |
| `uniform mat4 u_projection;` | `layout(std140, binding=0) uniform UBO_0 { mat4 u_projection; };` |
| `uniform sampler2D u_texture;` | `layout(binding=0) uniform sampler2D u_texture;` |
| `gl_FragColor = ...;` | `fragColor = ...;` + déclaration `layout(location=0) out vec4 fragColor;` |
| `texture2D(tex, uv)` | `texture(tex, uv)` |
| `textureCube(tex, dir)` | `texture(tex, dir)` |
| `#extension GL_OES_...` | *(supprimé, natif en 4.60)* |

### Réflexion

Le transpileur produit des données de réflexion complètes :
- **Attributs** : nom, type, location assignée
- **Varyings** : nom, type, location assignée
- **Uniforms** : nom, type, taille array, binding UBO, offset std140, taille en bytes
- **Samplers** : nom, type, binding assigné

Ces données sont utilisées par `glLinkProgram()` pour enregistrer automatiquement les packed uniforms.

---

## Fonctions GLES2 - État Complet

### Fonctions Shader

| Fonction | Status | Notes |
|----------|--------|-------|
| glCreateShader | ✅ | Vertex et Fragment |
| glDeleteShader | ✅ | |
| glIsShader | ✅ | |
| glShaderSource | ✅ | Stocke le source GLSL (ES 1.00 ou 4.60) |
| glCompileShader | ✅ | GLSL 4.60 : compile via libuam. GLSL ES 1.00 : transpilation différée à glLinkProgram |
| glGetShaderiv | ✅ | GL_COMPILE_STATUS, GL_SHADER_TYPE, GL_INFO_LOG_LENGTH, GL_SHADER_SOURCE_LENGTH |
| glGetShaderInfoLog | ✅ | Messages d'erreur libuam réels |
| glGetShaderSource | ✅ | Retourne le source stocké |
| glShaderBinary | ✅ | Format DKSH (`GL_DKSH_BINARY_FORMAT_NX = 0x10DE0001`) |
| glGetShaderPrecisionFormat | ✅ | Précision Tegra X1 |
| glReleaseShaderCompiler | ✅ | No-op |

### Fonctions Programme

| Fonction | Status | Notes |
|----------|--------|-------|
| glCreateProgram | ✅ | |
| glDeleteProgram | ✅ | |
| glIsProgram | ✅ | |
| glAttachShader | ✅ | |
| glDetachShader | ✅ | |
| glLinkProgram | ✅ | Déclenche la transpilation si shaders GLSL ES, auto-enregistre les uniforms |
| glUseProgram | ✅ | |
| glValidateProgram | ✅ | |
| glGetProgramiv | ✅ | GL_LINK_STATUS, GL_ACTIVE_UNIFORMS, GL_ACTIVE_ATTRIBUTES, etc. |
| glGetProgramInfoLog | ✅ | |
| glGetAttachedShaders | ✅ | |

### Fonctions Uniform

| Fonction | Status | Notes |
|----------|--------|-------|
| glGetUniformLocation | ✅ | Résout via réflexion du transpileur, registre utilisateur, ou table built-in |
| glGetActiveUniform | ✅ | Nom, type, taille des uniforms actifs |
| glGetUniformfv | ✅ | Readback via shadow buffer |
| glGetUniformiv | ✅ | Readback via shadow buffer |
| glUniform1f/2f/3f/4f | ✅ | Allocation per-call (pas de data race) |
| glUniform1i/2i/3i/4i | ✅ | |
| glUniform1fv/2fv/3fv/4fv | ✅ | Support arrays (count > 1) |
| glUniform1iv/2iv/3iv/4iv | ✅ | |
| glUniformMatrix2fv | ✅ | Conversion std140 (2 vec4 = 32 bytes) |
| glUniformMatrix3fv | ✅ | Conversion std140 (3 vec4 = 48 bytes) |
| glUniformMatrix4fv | ✅ | 4 vec4 = 64 bytes, support transpose |

### Fonctions Vertex

| Fonction | Status | Notes |
|----------|--------|-------|
| glBindAttribLocation | ✅ | Utilisé par le transpileur lors de glLinkProgram |
| glGetAttribLocation | ✅ | Bindings du programme, puis table built-in |
| glGetActiveAttrib | ✅ | |
| glEnableVertexAttribArray | ✅ | |
| glDisableVertexAttribArray | ✅ | |
| glVertexAttribPointer | ✅ | |
| glVertexAttrib1f/2f/3f/4f | ✅ | |
| glVertexAttrib1fv/2fv/3fv/4fv | ✅ | |
| glGetVertexAttribfv/iv | ✅ | |
| glGetVertexAttribPointerv | ✅ | |

### Fonctions Texture

| Fonction | Status | Notes |
|----------|--------|-------|
| glGenTextures | ✅ | |
| glDeleteTextures | ✅ | |
| glIsTexture | ✅ | |
| glBindTexture | ✅ | GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP |
| glActiveTexture | ✅ | Units 0-7 |
| glTexImage2D | ✅ | Max 8192x8192, 2D + cubemap faces |
| glTexSubImage2D | ✅ | |
| glTexParameteri/f/iv/fv | ✅ | |
| glGetTexParameteriv/fv | ✅ | |
| glPixelStorei | ✅ | GL_PACK/UNPACK_ALIGNMENT |
| glGenerateMipmap | ✅ | Via dkCmdBufBlitImage |
| glCopyTexImage2D | ✅ | Level 0, GL_TEXTURE_2D |
| glCopyTexSubImage2D | ✅ | |
| glCompressedTexImage2D | ✅ | |
| glCompressedTexSubImage2D | ✅ | |

### Fonctions Buffer

| Fonction | Status | Notes |
|----------|--------|-------|
| glGenBuffers | ✅ | |
| glDeleteBuffers | ✅ | |
| glIsBuffer | ✅ | |
| glBindBuffer | ✅ | GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER |
| glBufferData | ✅ | |
| glBufferSubData | ✅ | |
| glGetBufferParameteriv | ✅ | |

### Fonctions Draw

| Fonction | Status | Notes |
|----------|--------|-------|
| glDrawArrays | ✅ | GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_LINES, GL_LINE_STRIP, GL_POINTS |
| glDrawElements | ✅ | Uint16, Uint32, client-side ou EBO |

### Fonctions État

| Fonction | Status | Notes |
|----------|--------|-------|
| glEnable / glDisable | ✅ | DEPTH_TEST, STENCIL_TEST, BLEND, CULL_FACE, SCISSOR_TEST, POLYGON_OFFSET_FILL |
| glIsEnabled | ✅ | |
| glBlendFunc | ✅ | |
| glBlendFuncSeparate | ✅ | |
| glBlendEquation | ✅ | |
| glBlendEquationSeparate | ✅ | |
| glBlendColor | ✅ | |
| glDepthFunc | ✅ | |
| glDepthMask | ✅ | |
| glDepthRangef | ✅ | |
| glCullFace | ✅ | |
| glFrontFace | ✅ | |
| glColorMask | ✅ | |
| glStencilFunc / FuncSeparate | ✅ | |
| glStencilMask / MaskSeparate | ✅ | |
| glStencilOp / OpSeparate | ✅ | |
| glScissor | ✅ | |
| glViewport | ✅ | |
| glPolygonOffset | ✅ | |
| glLineWidth | ✅ | |
| glSampleCoverage | ✅ | (MSAA non supporté) |

### Fonctions Clear

| Fonction | Status | Notes |
|----------|--------|-------|
| glClear | ✅ | COLOR, DEPTH, STENCIL |
| glClearColor | ✅ | |
| glClearDepthf | ✅ | |
| glClearStencil | ✅ | |

### Fonctions Framebuffer

| Fonction | Status | Notes |
|----------|--------|-------|
| glGenFramebuffers | ✅ | |
| glDeleteFramebuffers | ✅ | |
| glIsFramebuffer | ✅ | |
| glBindFramebuffer | ✅ | |
| glFramebufferTexture2D | ✅ | |
| glFramebufferRenderbuffer | ✅ | |
| glCheckFramebufferStatus | ✅ | |
| glGetFramebufferAttachmentParameteriv | ✅ | |
| glGenRenderbuffers | ✅ | |
| glDeleteRenderbuffers | ✅ | |
| glIsRenderbuffer | ✅ | |
| glBindRenderbuffer | ✅ | |
| glRenderbufferStorage | ✅ | |
| glGetRenderbufferParameteriv | ✅ | |
| glReadPixels | ✅ | GL_RGBA + GL_UNSIGNED_BYTE |

### Fonctions Query

| Fonction | Status | Notes |
|----------|--------|-------|
| glGetString | ✅ | GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS |
| glGetIntegerv | ✅ | Requêtes d'état complètes |
| glGetBooleanv | ✅ | |
| glGetFloatv | ✅ | |
| glGetError | ✅ | |
| glFinish | ✅ | |
| glFlush | ✅ | |
| glHint | ✅ | No-op (tous les hints ignorés) |

### Limitations connues

| Fonctionnalité | Status | Notes |
|----------------|--------|-------|
| MSAA (Multisampling) | ❌ | Non supporté par le backend deko3d |
| glBlitFramebuffer | ❌ | Stub (lié au MSAA) |
| glRenderbufferStorageMultisample | ❌ | Fallback vers non-multisampled |
| eglGetPlatformDisplay | ❌ | EGL 1.5, non implémenté |
| eglCreatePbufferSurface | ❌ | Stub |
| glGetStringi | ❌ | Stub |

---

## Fonctions EGL

| Fonction | Status | Notes |
|----------|--------|-------|
| eglGetDisplay | ✅ | EGL_DEFAULT_DISPLAY (singleton) |
| eglInitialize | ✅ | Crée DkDevice |
| eglTerminate | ✅ | Cleanup complet |
| eglChooseConfig | ✅ | 2 configs : RGBA8, RGBA8+D24S8 |
| eglGetConfigAttrib | ✅ | |
| eglGetConfigs | ✅ | |
| eglCreateWindowSurface | ✅ | Utilise nwindowGetDefault() en interne |
| eglDestroySurface | ✅ | |
| eglQuerySurface | ✅ | |
| eglCreateContext | ✅ | GLES2 uniquement (client_version=2) |
| eglDestroyContext | ✅ | |
| eglMakeCurrent | ✅ | |
| eglQueryContext | ✅ | |
| eglGetCurrentContext | ✅ | |
| eglGetCurrentSurface | ✅ | |
| eglGetCurrentDisplay | ✅ | |
| eglSwapBuffers | ✅ | Submit command buffer + present |
| eglSwapInterval | ✅ | Via dkSwapchainSetSwapInterval |
| eglGetProcAddress | ✅ | Table de ~120 fonctions |
| eglQueryString | ✅ | |
| eglGetError | ✅ | |
| eglBindAPI | ✅ | EGL_OPENGL_ES_API |
| eglWaitClient | ✅ | Flush backend |
| eglWaitGL | ✅ | Flush backend |
| eglWaitNative | ✅ | No-op |
| eglReleaseThread | ✅ | No-op |
| eglSurfaceAttrib | ✅ | No-op |

---

## Notes d'Implémentation Importantes

### Uniforms : Allocation Per-Call

SwitchGLES alloue un **nouveau offset mémoire** pour chaque appel `glUniform*()`. Cela évite les "data races" quand plusieurs draw calls sont enregistrés dans le même command buffer.

**Conséquence :** La mémoire uniform (256KB par frame) peut s'épuiser si trop d'appels uniform sont faits. Chaque appel utilise au minimum 256 bytes (alignement deko3d).

**Bonnes pratiques :**
- Le transpileur regroupe automatiquement les uniforms dans des UBOs std140
- Éviter d'appeler `glUniform*()` en boucle pour des valeurs statiques
- Les uniforms sont réinitialisés à chaque frame (pas de persistance)

### Packed Uniforms

Le transpileur génère des packed UBOs : plusieurs uniforms partagent un seul binding UBO.
L'encodage de location est :
- **Legacy** : `location = (stage << 16) | binding` — un UBO par uniform
- **Packed** : `location = (1 << 31) | (stage << 24) | (binding << 16) | byte_offset` — offset dans le UBO

Le mode packed est utilisé automatiquement par l'auto-transpilation. Les applications n'ont pas besoin de s'en soucier.

### Command Buffer Triple-Buffering

SwitchGLES utilise 2-3 command buffers avec synchronisation par fences :
- Chaque framebuffer slot a son propre command buffer
- Les fences empêchent la réutilisation de mémoire GPU en cours d'utilisation
- `eglSwapBuffers()` soumet le command buffer et présente l'image

### Lazy State Application

Les appels `glEnable()`, `glBlendFunc()`, etc. ne modifient que l'état interne. L'application réelle au GPU se fait au moment du `glDrawArrays()` / `glDrawElements()`.

---

## État de l'Implémentation dans gl_shader.c

L'auto-transpilation est **déjà entièrement implémentée** dans `source/gl/gl_shader.c`. Voici le détail :

### 1. Transpilation différée dans glLinkProgram — ✅ FAIT

`glLinkProgram()` (lignes 394-582) implémente le pipeline complet en 6 étapes :
1. Configure les options du transpileur avec les bindings d'attributs (`glBindAttribLocation`)
2. Transpile le VS via `glslt_transpile(source, GLSLT_VERTEX, &vs_opts)`
3. Passe les locations de varyings du VS au FS pour cohérence
4. Transpile le FS via `glslt_transpile(source, GLSLT_FRAGMENT, &fs_opts)`
5. Enregistre automatiquement les packed uniforms via `sglRegisterPackedUniform()` et `sglSetPackedUBOSize()`
6. Compile les deux shaders transpilés via `sgl_compile_glsl460()` (libuam → DKSH)

### 2. glCompileShader : détection GLSL ES — ✅ FAIT

`glCompileShader()` (lignes 202-243) implémente :
- Détection GLSL ES 1.00 via `sgl_is_es100_source()` (cherche `#version 100`, absence de `#version`, ou mots-clés `attribute`/`varying`)
- Si ES 1.00 : `needs_transpile = true`, `compiled = true`, source conservé, transpilation différée
- Si GLSL 4.60 : compilation directe via `sgl_compile_glsl460()`
- Si binaire DKSH (`glShaderBinary`) : marqué comme compilé

### 3. Gestion des samplers — ✅ FONCTIONNE pour SDL

Le transpileur assigne des bindings aux samplers en ordre d'apparition (binding 0, 1, 2...). `glGetUniformLocation()` retourne `-1` pour les samplers (ils ne sont pas dans la table built-in ni dans le registre). Cela fonctionne pour SDL car :
- SDL ne remappe pas les texture units via `glUniform1i(samplerLoc, textureUnit)`
- Les bindings dans les shaders transpilés correspondent aux texture units par défaut (sampler0 → unit 0, etc.)

**Limitation** : Une application GLES2 qui remappe les samplers via `glUniform1i` ne fonctionnera pas correctement. Pour un support complet, il faudrait que `glGetUniformLocation` retourne une location valide pour les samplers et que `glUniform1i` mette à jour le binding du sampler. Ceci n'est pas nécessaire pour SDL.

---

## Gestion des Macros Préprocesseur dans le Transpileur — ✅ FAIT

Le transpileur gère correctement les directives préprocesseur et les macros non résolues :

1. **Directives `#ifdef`/`#define`/`#else`/`#endif`** : Toute ligne commençant par `#` qui n'est ni `#version` ni `#extension` est supprimée (classée `DECL_PRECISION`). Ces directives sont typiquement liées à la précision, un concept inexistant en GLSL 460.

2. **Macros non résolues avant un type** : Si `parse_type_and_names()` rencontre un token inconnu avant le type (ex: `varying MACRO vec2 name;`), il le saute et réessaie avec le mot suivant. Cela gère les macros de précision non résolues de manière générique.

### Note : Retry de compilation

Certaines applications GLES2 ont un mécanisme de retry : si la compilation échoue, elles réessaient avec des macros de précision différentes. Avec la transpilation différée, `glCompileShader()` retourne toujours succès pour le GLSL ES 1.00 (la vraie compilation est à `glLinkProgram`), donc le retry ne se déclenche jamais. Grâce à la suppression des directives préprocesseur par le transpileur, cela ne pose pas de problème.

---

## Extensions Propriétaires SwitchGLES (Optionnelles)

Ces fonctions ne sont **pas nécessaires** pour SDL. Elles sont disponibles pour les applications qui veulent un contrôle direct sur les shaders précompilés.

**Header :** `#include <GLES2/gl2sgl.h>` (à inclure après les headers GLES2 standards)

```c
// Charger un shader DKSH précompilé depuis un fichier
extern bool sgl_load_shader_from_file(GLuint shader, const char *path);

// Enregistrement manuel d'uniforms (legacy : un UBO par uniform)
extern GLboolean sglRegisterUniform(const GLchar *name, GLint stage, GLint binding);
extern void sglClearUniformRegistry(void);

// Enregistrement d'uniforms packés (plusieurs uniforms dans un UBO)
extern GLboolean sglRegisterPackedUniform(const GLchar *name, GLint stage, GLint binding, GLint byte_offset);
extern void sglSetPackedUBOSize(GLint stage, GLint binding, GLint size);

// Constantes
#define SGL_STAGE_VERTEX   0
#define SGL_STAGE_FRAGMENT 1
```

---

## Tests Disponibles

| Test | Description | Emplacement |
|------|-------------|-------------|
| validation_test | Validation complète GLES2 (100/100 tests) | SwitchGLES/examples/validation_test/ |
| 01_textured_quad | Quad texturé | SwitchGLES/examples/01_textured_quad/ |
| 02_es2gears | es2gears classique | SwitchGLES/examples/02_es2gears/ |
| 03_fbo | Framebuffer objects | SwitchGLES/examples/03_fbo/ |
| 04_cubemap | Cubemap rendering | SwitchGLES/examples/04_cubemap/ |
| sdl_test | Test GLES2 avec SDL | SDL/examples/sdl_test/ |
| sdl2-simple | Rectangles via GLES2 direct | SDL/examples/sdl2-simple/ |

---

## Modifications SDL Requises

### Aucune modification spécifique à SwitchGLES

Avec l'auto-transpilation dans SwitchGLES, SDL traite SwitchGLES comme n'importe quelle implémentation OpenGL ES 2.0 standard. Les seules modifications SDL nécessaires sont celles liées à la **plateforme Switch** (pas à SwitchGLES) :

- **Video driver Switch** (`SDL_switchvideo.c`) : gestion des fenêtres, display modes, événements
- **Backend EGL Switch** (`SDL_switchgl.c`) : appels EGL standards (statiquement liés car Switch n'a pas de chargement dynamique de bibliothèques)
- **Input Switch** : joystick, touch, clavier, souris — spécifiques à la plateforme, pas à SwitchGLES

Le renderer GLES2 de SDL (`SDL_render_gles2.c`) ne nécessite **aucun `#ifdef` SwitchGLES**. Il utilise le flux standard `glShaderSource` → `glCompileShader` → `glLinkProgram`.

### Code SwitchGLES-spécifique à supprimer dans SDL

Le code suivant peut être supprimé de SDL :

- **`SDL_render_gles2.c`** : Le bloc `#if defined(SDL_VIDEO_SWITCH_SGL)` dans `GLES2_CacheShader()` (lignes 507-529) qui charge les binaires DKSH précompilés — le chemin standard `glShaderSource`+`glCompileShader` fonctionnera
- **`SDL_shaders_dksh.h`** : Header contenant les shaders DKSH précompilés — plus nécessaire
- **Fichiers DKSH** dans `src/render/opengles2/dksh_shaders/` : Les shaders précompilés `.dksh` et les sources GLSL 460 — plus nécessaires
- **Le flag `SDL_VIDEO_SWITCH_SGL`** : Plus besoin de distinguer SwitchGLES d'un autre backend GLES2

### Build

SwitchGLES doit être compilé avec le runtime compiler :
```bash
# Dans le Makefile de SwitchGLES (déjà activé par défaut)
CFLAGS += -DSGL_ENABLE_RUNTIME_COMPILER
```

SDL doit linker SwitchGLES et ses dépendances :
```bash
LDFLAGS += -lSwitchGLES -luam -ldeko3d -lstdc++ -lnx
```

---

*Document mis à jour : 18 février 2026*
