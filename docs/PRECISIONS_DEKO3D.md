# Précisions Importantes de deko3d (d'après Primer.md)

## ⚠️ Confirmation Absolue sur les Shaders

**Citation officielle (Primer.md, ligne 434) :**

> deko3d **only** accepts native GPU code, i.e. SASS (Streaming Assembler) for Shader Model 5.3; which is the instruction set architecture (ISA) implemented by second-generation Maxwell GPUs. There is absolutely **no way** to use non-native shader languages which require runtime compilation such as GLSL or SPIR-V with deko3d. Users who need to JIT shaders at runtime **must** target the Maxwell 2nd gen ISA (SM53) in some way, shape or form instead of expecting deko3d to accept code written in a foreign language.

**Implications pour SwitchGLES :**
- ✅ Les stratégies que j'ai proposées sont les SEULES options viables
- ✅ UAM subprocess est la solution la plus réaliste pour runtime compilation
- ✅ Un JIT SASS compiler serait la seule alternative, mais extrêmement complexe
- ✅ Shaders précompilés pour FFP est la voie obligatoire

---

## UAM - L'Outil Officiel de Compilation

### Utilisation de UAM

```bash
Usage: uam [options] file
Options:
  -o, --out=<file>   Specifies the output deko3d shader module file (.dksh)
  -r, --raw=<file>   Specifies the file to which output raw Maxwell bytecode
  -t, --tgsi=<file>  Specifies the file to which output intermediary TGSI code
  -s, --stage=<name> Specifies the pipeline stage of the shader
                     (vert, tess_ctrl, tess_eval, geom, frag, comp)
  -v, --version      Displays version information
```

### Dialecte GLSL Accepté par UAM

**Caractéristiques importantes :**

1. **Bindings EXPLICITES OBLIGATOIRES** (ligne 469) :
```glsl
// ✅ CORRECT - Obligatoire
layout(binding = 0) uniform sampler2D myTexture;
layout(binding = 1) uniform MyUBO {
    mat4 mvp;
};

// ❌ ERREUR - UAM refusera de compiler
uniform sampler2D myTexture;  // Pas de binding explicite
```

2. **Limites de ressources par stage :**
- 16 UBOs (bindings 0-15)
- 16 SSBOs (bindings 0-15)
- 32 "samplers" combinés image+sampler (bindings 0-31)
- 8 images (bindings 0-7)

**⚠️ Note compute shaders :** UBO bindings 0-5 sont natifs, 6-15 sont émulés comme SSBOs

3. **Symbole préprocesseur disponible :**
```glsl
#if defined(DEKO3D)
// Code spécifique deko3d
#endif
// DEKO3D vaut 100
```

4. **Default uniforms NON supportés :**
```glsl
// ❌ ERREUR - Pas supporté dans deko3d
uniform mat4 mvp;
uniform vec4 color;

// ✅ CORRECT - Utiliser un UBO
layout(binding = 0) uniform Uniforms {
    mat4 mvp;
    vec4 color;
};
```

5. **Limitations et warnings :**
- `layout(origin_upper_left)` ignoré (warning)
- `layout(pixel_center_integer)` non supporté (erreur)
- Divisions entières non-constantes → float division (warning)
- Divisions 64-bit float et sqrt approximées (warning)
- Transform feedback non supporté
- Shader subroutines non supportés
- Separable programs toujours actifs (pas de linking)

---

## Format DKSH - Structure Binaire

### Structure du Fichier

```c
#define DKSH_MAGIC 0x48534B44  // "DKSH"

struct DkshHeader {
    uint32_t magic;        // DKSH_MAGIC
    uint32_t header_sz;    // sizeof(DkshHeader)
    uint32_t control_sz;   // Taille section control (multiple de 256)
    uint32_t code_sz;      // Taille section code (multiple de 256)
    uint32_t programs_off;
    uint32_t num_programs;
};
```

### Deux Sections Importantes

**1. Section CONTROL (metadata) :**
- Informations pour deko3d
- Peut être chargée en RAM normale (pas de mémoire GPU)
- Parsée à l'initialisation puis libérable

**2. Section CODE (machine code) :**
- Code SASS natif Maxwell
- **DOIT** être dans un DkMemBlock avec flag `DkMemBlockFlags_Code`
- **DOIT** être dans le "code segment" GPU (32-bit address space)

### Optimisation : Chargement Séparé

```c
// Algorithme recommandé (Primer.md ligne 499-510)

// 1. Ouvrir DKSH
FILE *f = fopen("shader.dksh", "rb");

// 2. Lire header
DkshHeader header;
fread(&header, sizeof(header), 1, f);

// 3. Allouer buffer temporaire pour control
uint8_t *control_buf = malloc(header.control_sz);
fseek(f, 0, SEEK_SET);
fread(control_buf, header.control_sz, 1, f);

// 4. Allouer mémoire GPU pour code
void *code_mem = code_mem_alloc(header.code_sz);  // Dans code segment

// 5. Lire section code directement en GPU mem
fread(code_mem, header.code_sz, 1, f);
fclose(f);

// 6. Initialiser shader
DkShaderMaker maker;
dkShaderMakerDefaults(&maker, code_mem_block, code_offset);
maker.control = control_buf;  // Pointer vers control section

DkShader shader;
dkShaderInitialize(&shader, &maker);

// 7. Libérer control (plus besoin)
free(control_buf);
```

**Avantage :** Économie de mémoire GPU précieuse (control section ~quelques KB)

---

## Memory Blocks - Détails Critiques

### Flags Recommandés pour Command Memory

```c
// Pour command buffers (Primer.md ligne 313)
DkMemBlockFlags flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_CpuUncached;
```

**Alignement requis :**
- Taille : multiple de `DK_CMDMEM_ALIGNMENT`
- Adresse : alignée à `DK_CMDMEM_ALIGNMENT`

### Code Segment (32-bit window)

**Caractéristiques importantes :**

1. **Taille limitée :** Maximum 4 GiB total
2. **Flag obligatoire :** `DkMemBlockFlags_Code`
3. **Mapping générique** placé dans ce segment spécial
4. **⚠️ Bug matériel** : Les derniers `DK_SHADER_CODE_UNUSABLE_SIZE` bytes d'un block sont inutilisables

```c
// Allocation pour shaders
DkMemBlockMaker maker;
dkMemBlockMakerDefaults(&maker, device, 16*1024*1024);  // 16 MB
maker.flags = DkMemBlockFlags_Code |
              DkMemBlockFlags_CpuUncached |
              DkMemBlockFlags_GpuCached;

DkMemBlock code_block = dkMemBlockCreate(&maker);

// ⚠️ Taille utilisable
size_t usable = dkMemBlockGetSize(code_block) - DK_SHADER_CODE_UNUSABLE_SIZE;
```

4. **⚠️ Limitation actuelle** (ligne 287) :
> Currently deko3d is unable to reuse previously-reserved mappings inside the code segment even if they're freed. Users are advised to reuse old code memory blocks instead of freeing them.

**Implication pour SwitchGLES :** Implémenter un allocateur avec défragmentation et réutilisation

### Flags Image

```c
// Pour textures
DkMemBlockFlags flags = DkMemBlockFlags_Image |
                       DkMemBlockFlags_GpuCached |
                       DkMemBlockFlags_CpuUncached;
```

**Effet :** Crée 2 mappings GPU supplémentaires avec attributs mémoire spéciaux pour :
- Accès image normaux
- Accès image "compressés"

### CPU Caching (Attention !)

**Ligne 289 warning important :**

> Memory blocks with CPU cacheability (`DkMemBlockFlags_CpuCached`) can be used. However if the memory block also has GPU cacheability (`DkMemBlockFlags_GpuCached`) care must be taken so that the GPU side caches are invalidated before accessing the memory. There is also no support for invalidating the CPU-side cache as it is a dangerous (and privileged!) operation; so users should **avoid** using CpuCached memory for GPU→CPU communication.

**Recommandation :** Utiliser `DkMemBlockFlags_CpuUncached` pour éviter les problèmes de cohérence

---

## Command Buffers - Lifecycle Détaillé

### Workflow Typique

```c
// 1. Création
DkCmdBufMaker maker;
dkCmdBufMakerDefaults(&maker, device);
maker.cbAddMem = my_add_mem_callback;  // Optionnel
DkCmdBuf cmdbuf = dkCmdBufCreate(&maker);

// 2. Ajout de backing memory
DkMemBlock cmd_mem = allocate_command_memory(1*1024*1024);  // 1 MB
dkCmdBufAddMemory(cmdbuf, cmd_mem, 0, dkMemBlockGetSize(cmd_mem));

// 3. Enregistrement commandes
dkCmdBufBindShaders(cmdbuf, ...);
dkCmdBufBindUniformBuffers(cmdbuf, ...);
dkCmdBufDraw(cmdbuf, ...);

// 4. Finalisation → obtenir handle
DkCmdList cmdlist = dkCmdBufFinishList(cmdbuf);

// 5. Soumission (peut être fait plusieurs fois)
dkQueueSubmitCommands(queue, cmdlist);

// 6. cmdlist valide tant que :
//    - cmdbuf existe
//    - backing memory pas écrasée
//    - pas de dkCmdBufClear()

// 7. Réutilisation du cmdbuf
//    Option A: Enregistrer nouveau cmdlist (si memory disponible)
dkCmdBufDraw(cmdbuf, ...);
DkCmdList cmdlist2 = dkCmdBufFinishList(cmdbuf);

//    Option B: Clear pour tout réinitialiser
dkCmdBufClear(cmdbuf);  // Invalide tous les cmdlists précédents !
```

### Callback cbAddMem

**Appelé quand la backing memory est pleine** (ligne 317-319) :

```c
void my_add_mem_callback(void* userData, DkCmdBuf cmdbuf, size_t minReqSize) {
    // Allouer nouveau backing memory
    DkMemBlock new_mem = allocate_command_memory(MAX(minReqSize, 1*1024*1024));

    // Ajouter au cmdbuf (continue l'enregistrement au nouveau offset)
    dkCmdBufAddMemory(cmdbuf, new_mem, 0, dkMemBlockGetSize(new_mem));

    // Si pas assez ajouté → fatal error
}
```

**Si pas de callback :** Fatal error immédiate quand plus de mémoire

### Sublists (ligne 323)

```c
// Enregistrer un sublist réutilisable
dkCmdBufBindTextures(cmdbuf, ...);
dkCmdBufBindSamplers(cmdbuf, ...);
DkCmdList texture_setup = dkCmdBufFinishList(cmdbuf);

// Réutiliser dans plusieurs command lists
dkCmdBufCallList(parent_cmdbuf, texture_setup);
dkCmdBufDraw(parent_cmdbuf, ...);
DkCmdList final_list = dkCmdBufFinishList(parent_cmdbuf);

// ⚠️ texture_setup doit rester valide tant que final_list est utilisé
```

---

## Queues - État Indépendant et Gestion Lazy

### Flags de Création

```c
DkQueueMaker maker;
dkQueueMakerDefaults(&maker, device);

// Capacités de la queue
maker.flags = DkQueueFlags_Graphics |   // Commandes graphics
              DkQueueFlags_Compute |    // Commandes compute
              DkQueueFlags_MediumPrio | // Priorité moyenne
              DkQueueFlags_EnableZcull; // Zcull activé

// Mémoire interne (ring buffer)
maker.commandMemorySize = 256*1024;  // 256 KB (min: DK_QUEUE_MIN_CMDMEM_SIZE)
maker.flushThreshold = 32*1024;      // Flush auto après 32 KB

// Compute scratch memory
maker.perWarpScratchMemorySize = 4*DK_PER_WARP_SCRATCH_MEM_ALIGNMENT;
maker.maxConcurrentComputeJobs = 8;

DkQueue queue = dkQueueCreate(&maker);
```

### État Complètement Indépendant (ligne 379)

**Chaque queue maintient son propre état :**
- Render targets bindés
- Shaders bindés
- Uniform/Vertex/Index buffers
- Textures/Samplers
- Descriptors sets
- Tous les state structs (rasterizer, blend, depth, etc.)

**Conséquence :** Deux queues = Deux contextes complètement séparés

### Gestion Lazy + Flush Automatique (ligne 404-409)

**Work items enqueués, pas soumis immédiatement**

Flush se produit quand :
1. `dkQueueFlush()` appelé manuellement
2. `dkQueuePresentImage()` (appelle flush internalement)
3. Liste de work items pleine
4. Seuil `flushThreshold` atteint dans command memory interne

**Après flush :** Barrière automatique invalidant :
- Image cache
- Shader cache
- Descriptor cache
- L2 cache

**Implication :** Ressources CPU-modifiées (VBO, textures, descriptors) valides entre batches

### GPU Error State (ligne 411-413)

```c
// Vérifier si queue en erreur
if (dkQueueIsInErrorState(queue)) {
    // Queue toast, seule opération légale:
    dkQueueDestroy(queue);
}
```

**⚠️ Warning OS (ligne 413) :**
> Even though deko3d can recover from GPU errors, the operating system seems to be programmed to kill processes that have crashed the GPU a few seconds afterwards.

**Grâce :** Quelques secondes pour sauvegarder avant kill

---

## Fences et Synchronisation

### Fences (DkFence)

**Structs opaques pour synchronisation GPU/CPU** (ligne 327-342)

```c
DkFence fence;
memset(&fence, 0, sizeof(fence));  // Initialiser à zéro si potentiellement wait avant signal

// Dans command buffer ou queue
dkQueueSignalFence(queue, &fence, true);  // flush=true invalide caches

// Wait CPU
dkFenceWait(&fence, -1);  // timeout infini

// Wait GPU (dans command list)
dkCmdBufWaitFence(cmdbuf, &fence);
```

**⚠️ Warning Important (ligne 341) :**
> Fence wait/signal commands recorded to a command list keep a **pointer** to the fence struct in the command buffer's bookkeeping memory. Please make sure the struct remains at the same valid memory address for the lifetime of the command list handle.

**Implication :** Fences doivent être allouées statiquement ou en heap stable

### Variables (DkVariable)

**Variables GPU-accessibles** pour synchronisation avancée (ligne 343-350)

```c
DkVariable var;
dkVariableInitialize(&var, mem_block, offset);

// Lecture CPU
uint32_t value = dkVariableRead(&var);

// Écriture GPU (dans command list)
dkCmdBufSignalVariable(cmdbuf, &var, DkVarOp_Add, 1, DkPipelinePos_Top);

// Wait conditionnel GPU
dkCmdBufWaitVariable(cmdbuf, &var, DkVarCompareOp_GreaterOrEqual, 10);
```

---

## Device Flags Importants

### Coordinate Systems (ligne 200-207)

```c
DkDeviceMaker maker;
dkDeviceMakerDefaults(&maker);

// Pour compatibilité OpenGL
maker.flags = DkDeviceFlags_DepthMinusOneToOne |  // Z clip space [-1, 1]
              DkDeviceFlags_OriginLowerLeft |     // Origin (0,0) en bas à gauche
              DkDeviceFlags_YAxisPointsUp;        // Y+ vers le haut

DkDevice device = dkDeviceCreate(&maker);
```

**Defaults deko3d :**
- Depth: [0, 1] (Vulkan/D3D style)
- Origin: Upper-left
- Y-axis: Points up

**Pour OpenGL :** Utiliser les flags ci-dessus

**⚠️ Note gl_FragCoord (ligne 222) :**
> `gl_FragCoord` in fragment shaders obeys the device origin mode when it comes to the Y axis and has pixel centers at half-integers, **with GLSL layout qualifiers having absolutely no effect**.

---

## Limitations et Warnings Supplémentaires

### Graphics Pipeline

1. **Transform feedback NON supporté** (ligne 672)
   - Alternative recommandée : Compute shaders

2. **Extensions GLSL partiellement supportées** :
   - ✅ `NV_geometry_shader_passthrough` (mentionné ligne 668)
   - ❌ Mais pas encore dans shader compiler

3. **Clip distances** (ligne 692) :
   - Les 8 clip distances activées par défaut
   - Valeur par défaut 0.0 si non écrites (= pas d'effet)

### Capture/Replay Commands (ligne 584-606)

**Feature avancée pour optimisation :**

```c
uint32_t captured[1024];

// Capture
dkCmdBufBeginCaptureCmds(cmdbuf, captured, 1024);
dkCmdBufBindTextures(cmdbuf, ...);
dkCmdBufSetViewports(cmdbuf, ...);
uint32_t num_words = dkCmdBufEndCaptureCmds(cmdbuf);

// Replay (zero overhead)
dkCmdBufReplayCmds(other_cmdbuf, captured, num_words);
```

**Limitations en mode capture :**
- Pas de gestion cmdbuf (AddMemory, FinishList, Clear)
- Pas de compute pipeline
- Pas de fences
- Pas d'indirect draw/dispatch
- Pas de Barrier Full
- Pas de CallList

---

## Recommandations pour SwitchGLES

### 1. Gestion Mémoire

```c
// Stratégie recommandée
typedef struct {
    DkMemBlock code_mem;      // 16 MB, flags=Code|GpuCached|CpuUncached
    DkMemBlock vertex_mem;    // 32 MB, flags=GpuCached|CpuUncached
    DkMemBlock uniform_mem;   // 8 MB,  flags=GpuCached|CpuUncached
    DkMemBlock texture_mem;   // 64 MB, flags=Image|GpuCached|CpuUncached
    DkMemBlock cmd_mem[3];    // 1 MB chacun, triple-buffer
} sgl_memory_pools;
```

### 2. Command Buffers

```c
// Triple buffering
typedef struct {
    DkCmdBuf cmdbuf[3];
    DkFence fence[3];
    DkMemBlock backing_mem[3];
    int current_frame;
} sgl_cmdbuf_pool;

void sgl_begin_frame() {
    int frame = pool.current_frame % 3;

    // Wait frame N-3
    dkFenceWait(&pool.fence[frame], -1);

    // Clear cmdbuf pour réutiliser la memory
    dkCmdBufClear(pool.cmdbuf[frame]);
}
```

### 3. Shaders

```c
// Précompilés avec UAM à build-time
extern const uint8_t ffp_shader_data[];
extern const size_t ffp_shader_sizes[];

// Runtime (si UAM disponible)
int compile_glsl_to_dksh(const char *glsl, uint8_t **dksh_out) {
    // 1. Injecter bindings explicites
    char *processed = inject_explicit_bindings(glsl);

    // 2. Sauver temporaire
    write_temp_file("/tmp/shader.glsl", processed);

    // 3. Appeler UAM
    system("uam -s frag /tmp/shader.glsl -o /tmp/shader.dksh");

    // 4. Charger résultat
    return load_dksh("/tmp/shader.dksh", dksh_out);
}
```

### 4. État OpenGL → deko3d

```c
void sgl_apply_state() {
    // Collecter tous les state changes depuis dernier draw
    if (state_dirty.blend) {
        DkBlendState blend;
        // Remplir selon glBlendFunc/glBlendEquation
        dkCmdBufBindBlendStates(cmdbuf, 0, &blend, 1);
    }

    if (state_dirty.depth) {
        DkDepthStencilState ds;
        // Remplir selon glDepthFunc/glDepthMask
        dkCmdBufBindDepthStencilState(cmdbuf, &ds);
    }

    // etc...

    state_dirty = (sgl_state_dirty){0};  // Clear flags
}
```

---

## Conclusion

Le Primer.md confirme que SwitchGLES est faisable avec la stratégie hybride :

1. **FFP précompilé** - Obligatoire, aucune alternative
2. **UAM subprocess** - Seule option réaliste pour custom shaders runtime
3. **Gestion mémoire sophistiquée** - Code segment, pools, triple-buffering
4. **Command buffer lifecycle** - Gestion manuelle mais bien documentée

La difficulté principale reste le système de shaders, mais le Primer.md confirme qu'il n'y a **aucune** possibilité de compilation runtime intégrée - donc mes recommandations sont les bonnes.
