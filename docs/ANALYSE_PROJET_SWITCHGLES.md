# Analyse du Projet SwitchGLES
## Implémentation OpenGL au-dessus de deko3d pour Nintendo Switch

**Date:** 6 janvier 2026
**Objectif:** Créer une couche de compatibilité OpenGL pour Nintendo Switch homebrew, similaire à vitaGL pour PS Vita

---

## Table des Matières
1. [Résumé Exécutif](#résumé-exécutif)
2. [Analyse de Faisabilité](#analyse-de-faisabilité)
3. [Évaluation de la Difficulté](#évaluation-de-difficulté)
4. [Architecture Proposée](#architecture-proposée)
5. [Plan d'Implémentation Détaillé](#plan-dimplémentation-détaillé)
6. [Défis Techniques Majeurs](#défis-techniques-majeurs)
7. [Ressources Requises](#ressources-requises)

---

## Résumé Exécutif

### Verdict: **FAISABLE MAIS AMBITIEUX**

Le projet SwitchGLES est techniquement réalisable en s'inspirant fortement de l'architecture de vitaGL. Cependant, plusieurs différences fondamentales entre sceGxm et deko3d augmentent significativement la complexité:

**Points Positifs:**
- ✅ vitaGL fournit une architecture de référence éprouvée
- ✅ deko3d expose toutes les capacités nécessaires (pipeline programmable complet)
- ✅ Le GPU Maxwell de la Switch est plus puissant que le PowerVR de la Vita
- ✅ Documentation deko3d excellente (Primer.md)
- ✅ Communauté homebrew Switch active

**Défis Majeurs:**
- ⚠️ deko3d est significativement plus bas niveau que sceGxm
- ⚠️ Paradigme Vulkan-like vs API traditionnelle
- ⚠️ Nécessite traduction GLSL → GLSL moderne avec bindings explicites
- ⚠️ Gestion mémoire GPU plus complexe
- ⚠️ Pas de runtime shader compilation simple

**Estimation:**
- **Difficulté:** 8/10
- **Durée estimée:** 6-12 mois pour un développeur expérimenté
- **Lignes de code attendues:** 20,000-30,000

---

## Analyse de Faisabilité

### 2.1 Comparaison des APIs Sous-jacentes

| Aspect | sceGxm (Vita) | deko3d (Switch) | Impact sur SwitchGLES |
|--------|---------------|-----------------|---------------------|
| **Paradigme** | API traditionnelle | Vulkan-inspired | ⚠️ Adaptation majeure requise |
| **Niveau d'abstraction** | Moyen | Très bas | ⚠️ Plus de code boilerplate |
| **Shaders** | Runtime compilation CG | Offline GLSL→SASS | ⚠️⚠️ Système de cache critique |
| **Mémoire** | Pools pré-définis | 40-bit address space explicite | ⚠️ Complexité accrue |
| **Command buffers** | Implicite | Explicite avec lifecycle | ⚠️ Gestion manuelle requise |
| **Synchronisation** | Relativement simple | Fences + Variables + Barriers | ⚠️ Plus complexe |
| **État GPU** | Contexte unique | Multi-queues indépendantes | ✅ Plus flexible |

### 2.2 Capacités du GPU

**GPU Maxwell (Switch) vs PowerVR SGX543MP4+ (Vita):**

| Feature | Vita | Switch | Avantage |
|---------|------|--------|----------|
| Tessellation | ❌ | ✅ | Switch |
| Geometry Shaders | ❌ | ✅ | Switch |
| Compute Shaders | Limité | ✅ Full | Switch |
| Render Targets | 4 | 8 | Switch |
| Texture Units | 8 | 32 par stage | Switch |
| UBOs | 16 | 16 par stage | Switch |
| MSAA | 2x/4x | 1x/2x/4x/8x | Switch |
| Compression | PVRTC/DXT | BC/ASTC/ETC2 | Switch |

**Conclusion:** La Switch dispose d'un GPU significativement plus capable, ce qui facilite l'implémentation de fonctionnalités OpenGL avancées.

### 2.3 Mapping OpenGL → deko3d

#### ✅ **Mappings Directs (Faciles)**

- **Types de primitives:** GL_TRIANGLES → DkPrimitive_Triangles (correspondance 1:1)
- **Formats de texture:** Excellent support des formats standards
- **Tests:** Depth, Stencil, Alpha test natifs
- **Blending:** API de blending très complète
- **Scissor/Viewport:** Support complet avec 16 instances

#### ⚠️ **Mappings Moyens (Modérés)**

- **Vertex Arrays:** Nécessite création de vertex buffers + attribute setup
- **Textures:** Nécessite DkImage + DkImageLayout + DkImageDescriptor
- **Uniforms:** Mapping vers UBOs avec bindings explicites
- **FBOs:** Construction de render pass avec DkImageView

#### ⚠️⚠️ **Mappings Complexes (Difficiles)**

- **Shaders GLSL:** Nécessite:
  1. Préprocessing et analyse
  2. Injection de bindings explicites
  3. Compilation offline via UAM
  4. Packaging DKSH
  5. Loading dynamique en mémoire code

- **Display Lists:** Très difficile avec command buffer model

- **Immediate Mode (glBegin/glEnd):** Nécessite buffering avec flush vers vertex buffers

- **Fixed Function Pipeline:** Génération complète de shaders (même approche que vitaGL mais vers GLSL moderne)

### 2.4 Contraintes Spécifiques deko3d

**Code Segment Management:**
```
Problème: Les shaders doivent résider dans un "code segment" 32-bit
Solution: Allocateur de mémoire dédié avec défragmentation
```

**Command Buffer Lifecycle:**
```
Problème: Les command lists doivent rester valides jusqu'à soumission GPU
Solution: Pool de command buffers avec rotation frame-based
```

**Memory Block Layout:**
```
Problème: Images requièrent block-linear layout
Solution: Wrapper avec conversion automatique si nécessaire
```

---

## Évaluation de la Difficulté

### 3.1 Échelle de Difficulté par Composant

| Composant | Difficulté (1-10) | Raison |
|-----------|-------------------|--------|
| **Init/Device Management** | 6 | Setup deko3d verbeux mais bien documenté |
| **Texture Management** | 7 | DkImage avec layouts complexes |
| **Vertex Arrays/VBOs** | 6 | Mapping conceptuel clair mais verbeux |
| **Matrix Stacks** | 3 | Math pure, indépendant de l'API |
| **Blending/Tests** | 4 | API deko3d bien conçue |
| **Framebuffers/FBOs** | 7 | Render targets + gestion multisample |
| **Shader System** | 9 | **POINT CRITIQUE** - compilation offline complexe |
| **Fixed Function Pipeline** | 8 | Génération de shaders GLSL modernes |
| **Command Buffer Management** | 7 | Lifecycle explicite + synchronisation |
| **Memory Management** | 7 | 40-bit address space + code segment |
| **Extensions** | 5-8 | Variable selon extension |

### 3.2 Comparaison vitaGL → SwitchGLES

**Ce qui sera PLUS FACILE:**
- ✅ GPU plus puissant = moins de workarounds
- ✅ Meilleure compression texture (ASTC)
- ✅ Documentation deko3d plus complète
- ✅ Tessellation/Geometry shaders natifs

**Ce qui sera PLUS DIFFICILE:**
- ⚠️ API plus bas niveau (3x plus de boilerplate estimé)
- ⚠️ Pas de runtime shader compilation facile
- ⚠️ Gestion mémoire GPU plus complexe
- ⚠️ Synchronisation explicite requise
- ⚠️ Command buffer lifecycle management

**Multiplicateur de Complexité Estimé:** 1.5x à 2x par rapport à vitaGL

---

## Architecture Proposée

### 4.1 Structure Globale

```
SwitchGLES/
├── include/
│   └── SwitchGLES.h              # API publique OpenGL
├── source/
│   ├── core/
│   │   ├── device.c            # Init deko3d, device, queues
│   │   ├── memory.c            # Allocateur mémoire GPU
│   │   ├── state.c             # Machine à états OpenGL
│   │   └── cmdbuf.c            # Gestion command buffers
│   ├── pipeline/
│   │   ├── ffp.c               # Fixed Function Pipeline
│   │   ├── shaders.c           # Gestion shaders programmables
│   │   ├── shader_compiler.c   # Traduction GLSL + UAM wrapper
│   │   └── shader_cache.c      # Cache DKSH sur disque
│   ├── resources/
│   │   ├── textures.c          # Gestion DkImage
│   │   ├── buffers.c           # VBO/UBO/IBO
│   │   ├── framebuffers.c      # FBO/RBO
│   │   └── vao.c               # Vertex Array Objects
│   ├── draw/
│   │   ├── draw.c              # Draw calls
│   │   ├── immediate.c         # glBegin/glEnd
│   │   └── arrays.c            # Vertex arrays
│   ├── transform/
│   │   ├── matrices.c          # Matrix stacks
│   │   └── viewport.c          # Viewport/Scissor
│   ├── fragment/
│   │   ├── blending.c          # Blending operations
│   │   ├── tests.c             # Depth/Stencil/Alpha
│   │   └── texenv.c            # Texture environment (FFP)
│   ├── utils/
│   │   ├── get_info.c          # glGet* queries
│   │   ├── extensions.c        # Extensions registry
│   │   └── error.c             # Error handling
│   └── shaders_ffp/
│       ├── ffp_vertex.glsl     # Templates shaders FFP
│       ├── ffp_fragment.glsl
│       └── texcombine/         # Combiners texture
└── tools/
    ├── shader_cache_builder/   # Build-time shader prep
    └── uam_wrapper/            # Wrapper UAM compiler
```

### 4.2 Flux de Rendu Typique

```
Application
    ↓
[OpenGL Call: glDrawElements(...)]
    ↓
SwitchGLES State Machine
    ├→ Check state changes
    ├→ Bind appropriate shader
    ├→ Update UBOs (matrices, materials)
    └→ Setup vertex attributes
    ↓
Command Buffer Recording
    ├→ dkCmdBufBindShaders()
    ├→ dkCmdBufBindUniformBuffers()
    ├→ dkCmdBufBindTextures()
    ├→ dkCmdBufBindVtxBuffers()
    ├→ dkCmdBufSetViewports()
    ├→ dkCmdBufSetScissors()
    ├→ dkCmdBufSetRasterizerState()
    ├→ dkCmdBufSetColorState()
    ├→ dkCmdBufSetDepthStencilState()
    └→ dkCmdBufDraw()
    ↓
[Fin de frame: glSwapBuffers()]
    ↓
dkCmdBufFinishList() → DkCmdList
    ↓
dkQueueSubmitCommands()
    ↓
GPU Execution
```

### 4.3 Système de Shaders - Design Critique

#### Approche Hybride Proposée

**Niveau 1: Shaders Pré-compilés (Build-time)**
```c
// Pour FFP et shaders communs
struct ffp_shader {
    const uint8_t *dksh_data;  // Embedded DKSH
    uint32_t size;
    uint32_t hash;
};

// Générés à la compilation:
extern ffp_shader ffp_notex_nolight;
extern ffp_shader ffp_1tex_nolight;
extern ffp_shader ffp_1tex_4lights;
// ... combinaisons fréquentes
```

**Niveau 2: Runtime Compilation (avec cache)**
```c
// Pour shaders GLSL custom
GLuint glCompileShader(source) {
    hash = xxhash(source);

    // Check cache disque
    if (file_exists(cache_dir + hash + ".dksh")) {
        return load_cached_shader(hash);
    }

    // Compilation via subprocess ou JIT
    #ifdef SWITCH_HAS_UAM_RUNTIME
        glsl_modified = inject_bindings(source);
        dksh = uam_compile(glsl_modified);
        cache_to_disk(hash, dksh);
    #else
        error("Runtime compilation not available");
    #endif
}
```

**Niveau 3: Générateur FFP**
```c
// Génération dynamique pour Fixed Function Pipeline
char* generate_ffp_shader(state) {
    // Analyse du state actuel
    bool lighting = state.lighting_enabled;
    int num_lights = count_enabled_lights(state);
    int num_textures = count_enabled_textures(state);
    bool fog = state.fog_enabled;

    // Template GLSL moderne
    sprintf(shader_source, ffp_template,
        lighting_code[num_lights],
        texture_code[num_textures],
        fog_code[fog]);

    return shader_source;
}
```

### 4.4 Gestion Mémoire GPU

```c
typedef enum {
    SGLMEM_CODE,       // Shaders (code segment)
    SGLMEM_VERTEX,     // Vertex/Index buffers
    SGLMEM_UNIFORM,    // UBOs (fréquemment mis à jour)
    SGLMEM_TEXTURE,    // Textures (rarement modifiées)
    SGLMEM_RENDER,     // Render targets
    SGLMEM_STAGING     // Transferts CPU→GPU
} sglMemType;

typedef struct {
    DkMemBlock block;
    void *cpu_addr;
    DkGpuAddr gpu_addr;
    size_t size;
    size_t used;
    sglMemType type;
} sglMemPool;

// Allocateur par type avec stratégies différentes
void* sgl_mem_alloc(size_t size, sglMemType type);
void sgl_mem_free(void *ptr);
void sgl_mem_gc(void); // Garbage collection frame-based
```

---

## Plan d'Implémentation Détaillé

### Phase 1: Infrastructure de Base (3-4 semaines)

**Objectif:** Avoir un triangle coloré à l'écran

#### Étape 1.1: Setup Projet
- [ ] Structure répertoires
- [ ] Makefile avec intégration devkitPro
- [ ] Squelette API avec stubs
- [ ] Application test minimale

#### Étape 1.2: Initialisation deko3d
```c
// Implémenter:
void sglInitialize(void);
DkDevice sgl_device;
DkQueue sgl_queue;
DkSwapchain sgl_swapchain;
```
- [ ] Création device
- [ ] Création queue (avec Zcull)
- [ ] Setup swapchain
- [ ] Allocation code segment
- [ ] Pools mémoire de base

#### Étape 1.3: Command Buffer Management
```c
// Système de rotation triple-buffering
typedef struct {
    DkCmdBuf cmdbuf[3];
    DkFence fence[3];
    int current_frame;
} sglCmdBufPool;
```
- [ ] Pool command buffers
- [ ] Système de fences
- [ ] Begin/End frame

#### Étape 1.4: Premier Draw Call
- [ ] glClear() → dkCmdBufClear()
- [ ] Vertex buffer statique
- [ ] Shader hardcodé (triangle coloré)
- [ ] glDrawArrays() basique
- [ ] Swap buffers

**Milestone:** Triangle coloré s'affiche

---

### Phase 2: Système de Shaders (4-6 semaines)

**Objectif:** Compiler et utiliser des shaders GLSL simples

#### Étape 2.1: Parser GLSL Basique
- [ ] Lexer/Parser pour extraire:
  - Uniforms
  - Attributes
  - Varying/in/out
  - Sampler2D
- [ ] Détection version GLSL

#### Étape 2.2: Injection Bindings
```glsl
// Input:
uniform mat4 modelview;
uniform sampler2D tex0;

// Output:
layout(binding = 0) uniform UniformBlock {
    mat4 modelview;
};
layout(binding = 0) uniform sampler2D tex0;
```
- [ ] Réassignation bindings UBO
- [ ] Réassignation bindings textures
- [ ] Conversion syntaxe GLSL 1.20 → 4.50

#### Étape 2.3: Wrapper UAM
```c
int uam_compile(const char *glsl_source,
                shader_stage stage,
                uint8_t **dksh_out,
                size_t *size_out);
```
- [ ] Appel subprocess vers UAM
- [ ] Parsing erreurs compilation
- [ ] Validation DKSH output

#### Étape 2.4: Cache Shaders
- [ ] Hash XXHash3 du source
- [ ] Sauvegarde/chargement DKSH
- [ ] Invalidation cache
- [ ] Loading dans code segment

#### Étape 2.5: API Shaders OpenGL
```c
GLuint glCreateShader(GLenum type);
void glShaderSource(...);
void glCompileShader(GLuint shader);
GLuint glCreateProgram();
void glAttachShader(...);
void glLinkProgram(GLuint program);
```
- [ ] Gestion objets shader
- [ ] Compilation lazy
- [ ] Link = création DkShader
- [ ] Binding au pipeline

**Milestone:** Shader GLSL custom fonctionne

---

### Phase 3: Textures (3-4 semaines)

**Objectif:** Texturing 2D complet

#### Étape 3.1: DkImage Wrapper
```c
typedef struct {
    GLuint id;
    DkImage image;
    DkImageDescriptor descriptor;
    DkMemBlock mem;
    GLenum target;    // GL_TEXTURE_2D, etc.
    GLenum format;
    int width, height;
    bool has_mipmaps;
} sglTexture;
```

#### Étape 3.2: Création Textures
- [ ] glGenTextures/glDeleteTextures
- [ ] glBindTexture
- [ ] Calcul DkImageLayout
- [ ] Allocation DkMemBlock
- [ ] Création DkImage

#### Étape 3.3: Upload Données
- [ ] glTexImage2D
- [ ] glTexSubImage2D
- [ ] Conversion formats si nécessaire
- [ ] Layout block-linear via deko3d helpers

#### Étape 3.4: Sampling
```c
typedef struct {
    DkSampler sampler;
    GLenum min_filter;
    GLenum mag_filter;
    GLenum wrap_s, wrap_t;
} sglSampler;
```
- [ ] glTexParameteri → DkSampler
- [ ] Binding texture + sampler
- [ ] Multitexturing (8 units minimum)

#### Étape 3.5: Mipmaps
- [ ] glGenerateMipmap
- [ ] Upload pyramide complète
- [ ] Filtrage mipmap

#### Étape 3.6: Formats Avancés
- [ ] Compressed (BC1/BC3/BC5/BC7)
- [ ] ASTC
- [ ] Floating-point

**Milestone:** Texture 2D avec mipmaps et filtrage

---

### Phase 4: Vertex Processing (2-3 semaines)

#### Étape 4.1: Vertex Arrays
```c
void glVertexAttribPointer(GLuint index,
                           GLint size,
                           GLenum type,
                           GLboolean normalized,
                           GLsizei stride,
                           const void *pointer);
```
- [ ] Tracking attributes
- [ ] Conversion formats (ex: GL_BYTE → DkVtxAttribSize_8)
- [ ] Client-side arrays (copie vers VBO temp)

#### Étape 4.2: VBO/IBO
```c
void glGenBuffers(GLsizei n, GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size,
                  const void *data, GLenum usage);
```
- [ ] Allocation dans pools GPU
- [ ] Mapping CPU→GPU
- [ ] STATIC/DYNAMIC/STREAM handling

#### Étape 4.3: VAO
```c
void glGenVertexArrays(GLsizei n, GLuint *arrays);
void glBindVertexArray(GLuint array);
```
- [ ] State capture
- [ ] Binding rapide

#### Étape 4.4: Draw Calls
- [ ] glDrawArrays
- [ ] glDrawElements (16/32-bit indices)
- [ ] glDrawRangeElements
- [ ] Primitive restart

**Milestone:** Meshes complexes avec indices

---

### Phase 5: Matrix Stacks & Transformations (1-2 semaines)

```c
void glMatrixMode(GLenum mode);
void glLoadIdentity();
void glLoadMatrixf(const GLfloat *m);
void glMultMatrixf(const GLfloat *m);
void glPushMatrix();
void glPopMatrix();

// Helpers
void glRotatef(GLfloat angle, ...);
void glTranslatef(GLfloat x, y, z);
void glScalef(GLfloat x, y, z);
void glFrustum(...);
void glOrtho(...);
```

- [ ] Stacks MODELVIEW, PROJECTION, TEXTURE
- [ ] Math matrice 4x4 (peut réutiliser code vitaGL)
- [ ] Upload vers UBOs avant draw

**Milestone:** Transformations 3D fonctionnelles

---

### Phase 6: Fragment Operations (2-3 semaines)

#### Étape 6.1: Tests
```c
// Depth
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LESS);
glDepthMask(GL_TRUE);

// Stencil
glEnable(GL_STENCIL_TEST);
glStencilFunc(GL_ALWAYS, ref, mask);
glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

// Scissor
glEnable(GL_SCISSOR_TEST);
glScissor(x, y, w, h);
```

- [ ] Mapping vers DkDepthStencilState
- [ ] DkRasterizerState pour scissor

#### Étape 6.2: Blending
```c
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
glBlendEquation(GL_FUNC_ADD);
glBlendFuncSeparate(...);
```

- [ ] Mapping vers DkColorState
- [ ] Support dual-source blending

#### Étape 6.3: Alpha Test
```c
glEnable(GL_ALPHA_TEST);
glAlphaFunc(GL_GREATER, 0.5f);
```

- [ ] Support natif Maxwell ou émulation shader

#### Étape 6.4: Face Culling
```c
glEnable(GL_CULL_FACE);
glCullFace(GL_BACK);
glFrontFace(GL_CCW);
```

- [ ] DkRasterizerState configuration

**Milestone:** Pipeline fragment complet

---

### Phase 7: Framebuffer Objects (2-3 semaines)

#### Étape 7.1: Renderbuffers
```c
void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
void glRenderbufferStorage(GLenum target, GLenum internalformat,
                           GLsizei width, GLsizei height);
```

- [ ] Allocation render targets
- [ ] Formats depth/stencil

#### Étape 7.2: FBO
```c
void glGenFramebuffers(GLsizei n, GLuint *framebuffers);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glFramebufferTexture2D(GLenum target, GLenum attachment,
                            GLenum textarget, GLuint texture,
                            GLint level);
void glFramebufferRenderbuffer(...);
GLenum glCheckFramebufferStatus(GLenum target);
```

- [ ] Configuration render targets
- [ ] Support MRT (8 attachments)
- [ ] Validation complétude

#### Étape 7.3: Blit
```c
void glBlitFramebuffer(GLint srcX0, GLint srcY0,
                       GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0,
                       GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter);
```

**Milestone:** Render-to-texture fonctionnel

---

### Phase 8: Fixed Function Pipeline (4-6 semaines)

**Objectif:** Compatibilité OpenGL 1.x

#### Étape 8.1: Lighting
```c
glEnable(GL_LIGHTING);
glEnable(GL_LIGHT0);
glLightfv(GL_LIGHT0, GL_POSITION, position);
glLightfv(GL_LIGHT0, GL_DIFFUSE, color);
glMaterialfv(GL_FRONT, GL_AMBIENT, color);
```

**Génération shader:**
```glsl
// Template FFP avec 4 lumières
#version 450

layout(binding = 0) uniform FFPUniforms {
    mat4 modelview;
    mat4 projection;
    mat4 normal_matrix;
    vec4 light_pos[4];
    vec4 light_diffuse[4];
    vec4 material_ambient;
    vec4 material_diffuse;
    vec4 material_specular;
    float material_shininess;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec2 frag_texcoord;

void main() {
    // Lighting calculation
    vec3 N = normalize((normal_matrix * vec4(normal, 0.0)).xyz);
    vec4 total_diffuse = vec4(0.0);

    for (int i = 0; i < 4; i++) {
        vec3 L = normalize(light_pos[i].xyz);
        float diff = max(dot(N, L), 0.0);
        total_diffuse += light_diffuse[i] * diff;
    }

    frag_color = material_ambient + material_diffuse * total_diffuse;
    frag_texcoord = texcoord;
    gl_Position = projection * modelview * vec4(position, 1.0);
}
```

- [ ] Générateur de templates
- [ ] Combinaisons: 0-8 lights × 0-2 textures × fog on/off
- [ ] Cache des variations

#### Étape 8.2: Texture Environment
```c
glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
// Modes: REPLACE, MODULATE, DECAL, BLEND, ADD, COMBINE
```

- [ ] Fragment shader variants
- [ ] GL_COMBINE avec opérateurs

#### Étape 8.3: Fog
```c
glEnable(GL_FOG);
glFogi(GL_FOG_MODE, GL_LINEAR);
glFogfv(GL_FOG_COLOR, color);
glFogf(GL_FOG_START, 10.0f);
glFogf(GL_FOG_END, 100.0f);
```

- [ ] Intégration dans shaders FFP

#### Étape 8.4: Immediate Mode
```c
glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glTexCoord2f(0, 0);
    glVertex3f(0, 0, 0);
    // ...
glEnd();
```

- [ ] Buffer temporaire
- [ ] Flush vers VBO
- [ ] Limitation: pas de display lists complexes

**Milestone:** Démos OpenGL 1.x legacy fonctionnent

---

### Phase 9: Extensions & Optimisations (3-4 semaines)

#### Extensions Prioritaires:
- [ ] GL_EXT_framebuffer_object
- [ ] GL_ARB_vertex_buffer_object
- [ ] GL_ARB_fragment_shader
- [ ] GL_ARB_multitexture
- [ ] GL_EXT_texture_filter_anisotropic
- [ ] GL_ARB_texture_compression
- [ ] GL_EXT_texture_compression_s3tc
- [ ] GL_KHR_texture_compression_astc_ldr

#### Optimisations:
- [ ] State change batching
- [ ] Draw call merging
- [ ] Persistent mapping buffers
- [ ] Circular vertex pools
- [ ] Shader warmup cache
- [ ] Garbage collector tuning

**Milestone:** Performance production-ready

---

### Phase 10: Testing & Polish (2-3 semaines)

- [ ] Suite de tests unitaires
- [ ] Applications de référence (glxgears, etc.)
- [ ] Profiling GPU (deko3d stats)
- [ ] Documentation API
- [ ] Examples samples
- [ ] Debugging hooks

---

## Défis Techniques Majeurs

### 6.1 Compilation Shaders Runtime

**Problème:**
deko3d nécessite des shaders compilés en SASS natif via UAM (outil offline). OpenGL attend pouvoir compiler à runtime.

**Solutions Possibles:**

#### Option A: Subprocess UAM (Recommandée)
```c
// Appeler UAM comme subprocess
system("uam -s vert vertex.glsl -o shader.vert.dksh");
system("uam -s frag fragment.glsl -o shader.frag.dksh");
load_dksh("shader.vert.dksh");
```
**Pros:** Réutilise UAM officiel
**Cons:** Overhead subprocess, nécessite UAM sur Switch

#### Option B: JIT SASS (Ambitieux)
Créer un JIT compiler GLSL→SASS
**Pros:** Aucune dépendance externe
**Cons:** Extrêmement complexe (mois de travail)

#### Option C: Precompile + Runtime Selection
Précompiler toutes les variations FFP + ne supporter que shaders statiques
**Pros:** Zero overhead runtime
**Cons:** Limité, pas de vraie programmabilité

**Recommandation:** Option A avec fallback C pour FFP

### 6.2 Memory Layout Conversion

**Problème:**
deko3d utilise block-linear layout (format tiled Nvidia). OpenGL assume linear layout.

**Solution:**
```c
void upload_texture_data(void *linear_data, DkImage *image) {
    // Allouer staging buffer pitch-linear
    DkMemBlock staging = allocate_staging(size, DkMemAccess_CpuUncached);

    // Copier données
    memcpy(staging.cpu_addr, linear_data, size);

    // Copy engine: linear → block-linear
    dkCmdBufCopyBufferToImage(cmdbuf,
        staging.gpu_addr,
        image,
        &layout,
        flags);
}
```

### 6.3 Command Buffer Overflow

**Problème:**
Command buffers ont taille fixe. Scènes complexes peuvent overflow.

**Solution:**
```c
void check_cmdbuf_space() {
    if (dkCmdBufGetSize(cmdbuf) > THRESHOLD) {
        // Submit current commands
        dkCmdBufFinishList(cmdbuf);
        dkQueueSubmitCommands(queue, list);

        // Start new cmdbuf
        dkCmdBufClear(cmdbuf);
        // Re-bind toutes les resources
        rebind_pipeline_state();
    }
}
```

### 6.4 Synchronization

**Problème:**
OpenGL assume un modèle single-threaded implicit. deko3d nécessite fences explicites.

**Solution:**
```c
// Triple buffering
void sglSwapBuffers() {
    int frame = current_frame % 3;

    // Attendre que frame N-3 soit fini
    dkFenceWait(&frame_fence[frame], -1);

    // Submit commands de cette frame
    DkCmdList list = dkCmdBufFinishList(cmdbuf[frame]);
    dkQueueSubmitCommands(queue, list, &frame_fence[frame]);

    // Present
    dkQueuePresentImage(queue, swapchain, slot);

    current_frame++;
}
```

### 6.5 Code Segment Fragmentation

**Problème:**
Code segment limité à 4GB (32-bit). Shaders dynamiques peuvent fragmenter.

**Solution:**
```c
typedef struct {
    uint8_t *base;
    uint32_t size;
    uint32_t used;
    // Allocator avec defragmentation
    shader_slot_t *slots[MAX_SHADERS];
} code_segment_allocator;

void defragment_code_segment() {
    // Compacter shaders actifs
    // Réallouer slots
    // Update DkShader GPU addresses
}
```

---

## Ressources Requises

### 7.1 Connaissances Techniques

**Essentielles:**
- ✅ Maîtrise C (pointeurs, memory management)
- ✅ OpenGL API (1.x, 2.x, 3.x)
- ✅ Graphismes 3D (pipelines, matrices, textures)
- ✅ deko3d API (lecture Primer.md obligatoire)

**Souhaitables:**
- 🟡 Vulkan (concepts command buffers, descriptors)
- 🟡 Compilateurs GLSL
- 🟡 Architecture GPU (Maxwell)
- 🟡 Switch homebrew ecosystem

### 7.2 Outils

**Obligatoires:**
- devkitPro avec devkitA64
- libnx
- deko3d headers et lib
- UAM (Universal Assembly for Maxwell)
- Émulateur Switch (Yuzu/Ryujinx) ou console hackée

**Utiles:**
- NSight Graphics (profiling GPU)
- RenderDoc (si support Switch)
- Git pour version control
- VSCode avec C/C++ extensions

### 7.3 Références

**Documentation:**
- deko3d Primer.md
- vitaGL source code (référence architecture)
- OpenGL 3.3 Core Specification
- OpenGL ES 2.0 Specification
- Maxwell GPU Documentation (nouveau/envytools)

**Communautés:**
- SwitchBrew Discord
- /r/SwitchHacks
- GBAtemp forums

---

## Conclusion et Recommandations

### Verdict Final: **FAISABLE**

Le projet SwitchGLES est techniquement réalisable et représente un ajout précieux à l'écosystème homebrew Switch. Cependant, il s'agit d'un projet ambitieux nécessitant:

### Prérequis pour Succès:
1. **Expérience solide** en C et graphismes 3D
2. **Compréhension profonde** de vitaGL (lire tout le code)
3. **Maîtrise de deko3d** (implémenter plusieurs samples d'abord)
4. **Engagement à long terme** (6-12 mois minimum)

### Stratégie Recommandée:

**Phase MVP (2-3 mois):**
- Implémentation minimale: OpenGL 2.0 core
- Shaders programmables uniquement (pas de FFP)
- Texturing 2D basique
- FBOs simples
- Tests avec applications simples

**Phase Extension (3-6 mois):**
- Fixed Function Pipeline
- Extensions prioritaires
- Optimisations performance
- Tests avec apps complexes

**Phase Production (2-3 mois):**
- Polish
- Documentation
- Samples
- Release

### Alternatives à Considérer:

**Option 1: Fork mesa/nouveau**
- Réutiliser driver OpenGL existant
- Adapter pour Switch
- **Cons:** Mesa est énorme (~8MB vs ~500KB deko3d)

**Option 2: Wrapper OpenGL→Vulkan + deko3d**
- Utiliser ANGLE ou Zink
- **Cons:** Overhead supplémentaire

**Option 3: SwitchGLES minimal**
- Subset OpenGL pour cas d'usage spécifiques
- Exemple: OpenGL ES 2.0 uniquement
- **Pros:** Plus rapide à développer

### Prochaines Étapes Immédiates:

1. **Valider l'approche:**
   - Créer un POC: triangle avec shader GLSL custom
   - Valider chaîne de compilation UAM
   - Mesurer overhead deko3d

2. **Préparer l'infrastructure:**
   - Setup repo Git
   - Créer structure projet
   - Makefile de base
   - Premier test sur matériel

3. **Commencer Phase 1:**
   - Suivre le plan d'implémentation
   - Documenter les difficultés rencontrées
   - Itérer sur l'architecture si nécessaire

---

**Bonne chance pour ce projet ambitieux ! La communauté homebrew Switch bénéficiera grandement d'une couche de compatibilité OpenGL de qualité.**
