# Stratégies pour SwitchGLES sans Compilation Runtime de Shaders

## Contexte

**Contrainte deko3d :** Les shaders doivent être compilés en SASS natif Maxwell via l'outil UAM (offline uniquement). Pas de runtime compilation prête à l'emploi.

**Approche vitaGL (pour comparaison) :**
- FFP : Templates CG avec `sprintf()` → Runtime compilation via vitaShaRK
- Custom shaders : GLSL → Runtime compilation via vitaShaRK
- Cache disque + RAM pour éviter recompilation

**Question :** Comment implémenter OpenGL sur Switch sans pouvoir compiler à la volée ?

---

## Solutions Viables (par ordre de recommandation)

### ⭐ Solution 1 : Approche Hybride "Precompiled + Limited Runtime" (RECOMMANDÉE)

**Principe :** Combiner shaders précompilés pour FFP avec compilation runtime optionnelle pour les custom shaders.

#### Composantes :

##### A. Fixed Function Pipeline : 100% Précompilé

**Comment ça marche :**

```c
// À la compilation du projet (build-time)
// Génération de toutes les combinaisons FFP possibles

// États FFP à gérer :
#define FFP_LIGHTING_OFF       0
#define FFP_LIGHTING_1_4       1  // 1-4 lumières
#define FFP_LIGHTING_5_8       2  // 5-8 lumières

#define FFP_TEX_NONE           0
#define FFP_TEX_1              1
#define FFP_TEX_2              2

#define FFP_FOG_NONE           0
#define FFP_FOG_LINEAR         1
#define FFP_FOG_EXP            2
#define FFP_FOG_EXP2           3

#define FFP_ALPHATEST_OFF      0
#define FFP_ALPHATEST_ON       1

#define FFP_COLORMATERIAL_OFF  0
#define FFP_COLORMATERIAL_ON   1

// Calcul des combinaisons :
// Lighting: 3 × Textures: 3 × Fog: 4 × AlphaTest: 2 × ColorMat: 2 = 144 shaders

// Chaque shader précompilé en DKSH à la compilation
typedef struct {
    const uint8_t *vertex_dksh;
    uint32_t vertex_size;
    const uint8_t *fragment_dksh;
    uint32_t fragment_size;
    uint32_t state_hash;
} ffp_precompiled_shader;

// Array généré automatiquement
extern const ffp_precompiled_shader ffp_shaders[144];
```

**Génération Build-Time :**

```python
# build_ffp_shaders.py
import subprocess
import hashlib

# Templates GLSL
vertex_template = """
#version 450

layout(binding = 0) uniform FFPBlock {
    mat4 modelview;
    mat4 projection;
    mat4 mvp;
    mat4 normal_matrix;

    #if LIGHTING > 0
    vec4 light_pos[LIGHTING * 4];
    vec4 light_diffuse[LIGHTING * 4];
    vec4 light_ambient[LIGHTING * 4];
    vec4 light_specular[LIGHTING * 4];
    vec4 material_ambient;
    vec4 material_diffuse;
    vec4 material_specular;
    float material_shininess;
    #endif
};

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
#if TEXTURES >= 1
layout(location = 2) in vec2 in_texcoord0;
#endif
#if TEXTURES >= 2
layout(location = 3) in vec2 in_texcoord1;
#endif
#if COLORMATERIAL == 1
layout(location = 4) in vec4 in_color;
#endif

layout(location = 0) out vec4 out_color;
#if TEXTURES >= 1
layout(location = 1) out vec2 out_texcoord0;
#endif
#if TEXTURES >= 2
layout(location = 2) out vec2 out_texcoord1;
#endif
#if FOG > 0
layout(location = 3) out float fog_factor;
#endif

void main() {
    vec4 position = vec4(in_position, 1.0);
    gl_Position = mvp * position;

    #if LIGHTING > 0
        vec3 N = normalize((normal_matrix * vec4(in_normal, 0.0)).xyz);
        vec3 eye_pos = (modelview * position).xyz;

        vec4 ambient = material_ambient * light_ambient[0];
        vec4 diffuse = vec4(0.0);
        vec4 specular = vec4(0.0);

        for (int i = 0; i < LIGHTING * 4; i++) {
            vec3 L = normalize(light_pos[i].xyz - eye_pos);
            float diff = max(dot(N, L), 0.0);
            diffuse += light_diffuse[i] * diff;

            if (diff > 0.0) {
                vec3 R = reflect(-L, N);
                vec3 V = normalize(-eye_pos);
                float spec = pow(max(dot(R, V), 0.0), material_shininess);
                specular += light_specular[i] * spec;
            }
        }

        out_color = ambient + material_diffuse * diffuse + material_specular * specular;

        #if COLORMATERIAL == 1
        out_color *= in_color;
        #endif
    #else
        #if COLORMATERIAL == 1
        out_color = in_color;
        #else
        out_color = vec4(1.0);
        #endif
    #endif

    #if TEXTURES >= 1
    out_texcoord0 = in_texcoord0;
    #endif
    #if TEXTURES >= 2
    out_texcoord1 = in_texcoord1;
    #endif

    #if FOG > 0
        float dist = length(eye_pos);
        // Fog calculation (dépend du mode)
        #if FOG == 1  // LINEAR
        fog_factor = (fog_end - dist) / (fog_end - fog_start);
        #elif FOG == 2  // EXP
        fog_factor = exp(-fog_density * dist);
        #elif FOG == 3  // EXP2
        fog_factor = exp(-pow(fog_density * dist, 2.0));
        #endif
        fog_factor = clamp(fog_factor, 0.0, 1.0);
    #endif
}
"""

fragment_template = """
#version 450

#if TEXTURES >= 1
layout(binding = 0) uniform sampler2D tex0;
#endif
#if TEXTURES >= 2
layout(binding = 1) uniform sampler2D tex1;
#endif

#if FOG > 0
layout(binding = 1) uniform FogBlock {
    vec4 fog_color;
    float fog_density;
    float fog_start;
    float fog_end;
};
#endif

layout(location = 0) in vec4 in_color;
#if TEXTURES >= 1
layout(location = 1) in vec2 in_texcoord0;
#endif
#if TEXTURES >= 2
layout(location = 2) in vec2 in_texcoord1;
#endif
#if FOG > 0
layout(location = 3) in float fog_factor;
#endif

layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = in_color;

    #if TEXTURES >= 1
    vec4 tex_color0 = texture(tex0, in_texcoord0);
    color *= tex_color0;
    #endif

    #if TEXTURES >= 2
    vec4 tex_color1 = texture(tex1, in_texcoord1);
    color *= tex_color1;
    #endif

    #if ALPHATEST == 1
    if (color.a < alpha_ref) discard;
    #endif

    #if FOG > 0
    color = mix(fog_color, color, fog_factor);
    #endif

    out_color = color;
}
"""

# Génération de toutes les combinaisons
for lighting in [0, 1, 2]:
    for textures in [0, 1, 2]:
        for fog in [0, 1, 2, 3]:
            for alphatest in [0, 1]:
                for colormaterial in [0, 1]:
                    # Générer le code GLSL avec defines
                    defines = f"-DLIGHTING={lighting} -DTEXTURES={textures} " \
                             f"-DFOG={fog} -DALPHATEST={alphatest} " \
                             f"-DCOLORMATERIAL={colormaterial}"

                    # Sauver dans fichier temporaire
                    vert_file = f"temp_vert_{lighting}_{textures}_{fog}_{alphatest}_{colormaterial}.glsl"
                    frag_file = f"temp_frag_{lighting}_{textures}_{fog}_{alphatest}_{colormaterial}.glsl"

                    with open(vert_file, 'w') as f:
                        f.write(vertex_template)
                    with open(frag_file, 'w') as f:
                        f.write(fragment_template)

                    # Compiler avec UAM
                    subprocess.run([
                        'uam', '-s', 'vert', vert_file,
                        '-o', f'ffp_v_{lighting}_{textures}_{fog}_{alphatest}_{colormaterial}.dksh',
                        defines
                    ])

                    subprocess.run([
                        'uam', '-s', 'frag', frag_file,
                        '-o', f'ffp_f_{lighting}_{textures}_{fog}_{alphatest}_{colormaterial}.dksh',
                        defines
                    ])

                    # Générer le header C pour embedding
                    # ... (convertir DKSH en array uint8_t)

print(f"Généré 144 shaders précompilés")
```

**Runtime Selection :**

```c
// À runtime, sélection du bon shader
uint32_t ffp_compute_state_hash() {
    uint32_t hash = 0;

    // Lighting
    if (!lighting_enabled) {
        hash |= (0 << 0);
    } else if (num_enabled_lights <= 4) {
        hash |= (1 << 0);
    } else {
        hash |= (2 << 0);
    }

    // Textures
    int num_tex = count_enabled_texture_units();
    hash |= ((num_tex & 0x3) << 2);

    // Fog
    if (!fog_enabled) {
        hash |= (0 << 4);
    } else if (fog_mode == GL_LINEAR) {
        hash |= (1 << 4);
    } else if (fog_mode == GL_EXP) {
        hash |= (2 << 4);
    } else {
        hash |= (3 << 4);
    }

    // Alpha test
    hash |= ((alpha_test_enabled ? 1 : 0) << 6);

    // Color material
    hash |= ((color_material_enabled ? 1 : 0) << 7);

    return hash;
}

const ffp_precompiled_shader* ffp_get_shader(uint32_t hash) {
    // Lookup dans l'array précompilé
    for (int i = 0; i < 144; i++) {
        if (ffp_shaders[i].state_hash == hash) {
            return &ffp_shaders[i];
        }
    }
    return NULL;  // Should never happen
}

void ffp_bind_shader() {
    uint32_t hash = ffp_compute_state_hash();
    const ffp_precompiled_shader *shader = ffp_get_shader(hash);

    // Charger en mémoire code si pas déjà fait
    if (!shader->loaded) {
        DkShader_initialize(&shader->dk_vertex,
                           code_mem_alloc(shader->vertex_size),
                           shader->vertex_dksh);
        DkShader_initialize(&shader->dk_fragment,
                           code_mem_alloc(shader->fragment_size),
                           shader->fragment_dksh);
        shader->loaded = true;
    }

    // Bind au pipeline
    dkCmdBufBindShaders(cmdbuf, DkStageFlag_GraphicsMask,
                       &shader->dk_vertex, 1, &shader->dk_fragment, 1);
}
```

**Avantages :**
- ✅ Aucune dépendance runtime
- ✅ Performance optimale (shaders natifs)
- ✅ Embedded dans le binaire ou asset pack
- ✅ Couvre 99% des cas FFP réels

**Inconvénients :**
- ⚠️ 144 shaders = ~2-5 MB d'assets
- ⚠️ Limitations sur combinaisons (ex: max 2 textures au lieu de 8)

---

##### B. Custom Shaders : Approche "On-Demand Compilation"

**Option B1 : UAM via Subprocess (si disponible)**

```c
// Nécessite UAM installé sur la Switch
GLboolean compile_glsl_shader_runtime(const char *source,
                                      shader_stage stage,
                                      DkShader *out_shader) {
    // 1. Calculer hash du source
    uint64_t hash = xxhash64(source);

    // 2. Chercher dans cache disque
    char cache_path[256];
    snprintf(cache_path, sizeof(cache_path),
             "sdmc:/switch/SwitchGLES/shader_cache/%016llx.dksh", hash);

    if (file_exists(cache_path)) {
        return load_cached_dksh(cache_path, out_shader);
    }

    #ifdef HAVE_UAM_RUNTIME
    // 3. Pas en cache → compiler

    // Préprocessing: injecter bindings
    char *processed_source = inject_explicit_bindings(source);

    // Sauver temporairement
    char temp_glsl[256];
    snprintf(temp_glsl, sizeof(temp_glsl), "/tmp/shader_%016llx.glsl", hash);
    write_file(temp_glsl, processed_source);

    // Appeler UAM
    char uam_cmd[512];
    snprintf(uam_cmd, sizeof(uam_cmd),
             "uam -s %s %s -o %s",
             stage == VERTEX ? "vert" : "frag",
             temp_glsl,
             cache_path);

    int result = system(uam_cmd);
    if (result != 0) {
        fprintf(stderr, "UAM compilation failed\n");
        return GL_FALSE;
    }

    // 4. Charger le DKSH compilé
    return load_cached_dksh(cache_path, out_shader);

    #else
    fprintf(stderr, "Runtime shader compilation not available. "
                    "Please precompile shaders or use FFP.\n");
    return GL_FALSE;
    #endif
}

// API OpenGL
void glCompileShader(GLuint shader_id) {
    shader_object *s = &shaders[shader_id - 1];

    if (!compile_glsl_shader_runtime(s->source, s->stage, &s->dk_shader)) {
        s->compile_status = GL_FALSE;
        strcpy(s->info_log, "Shader compilation failed");
    } else {
        s->compile_status = GL_TRUE;
    }
}
```

**Prérequis :**
- UAM doit être installé sur la Switch (possible via homebrew)
- SD card pour le cache

**Avantages :**
- ✅ Vraie programmabilité
- ✅ Compatible avec apps OpenGL existantes
- ✅ Cache permanent

**Inconvénients :**
- ⚠️ Dépendance externe (UAM)
- ⚠️ Latence première compilation (1-5 secondes)
- ⚠️ Nécessite SD card

---

**Option B2 : Shader Database Pré-générée**

```c
// Concept: Base de données massive de shaders pré-compilés
// Indexée par hash du source GLSL

typedef struct {
    uint64_t source_hash;
    uint32_t dksh_offset;  // Offset dans le fichier shader_db.bin
    uint32_t dksh_size;
} shader_db_entry;

// shader_db.idx: Index des shaders (hash → offset)
// shader_db.bin: Données DKSH concaténées

GLboolean lookup_precompiled_shader(const char *source, DkShader *out) {
    uint64_t hash = xxhash64(source);

    // Chercher dans l'index
    shader_db_entry *entry = shader_db_find(hash);
    if (!entry) {
        fprintf(stderr, "Shader not found in database. "
                        "Please add to shader_db and rebuild.\n");
        return GL_FALSE;
    }

    // Charger depuis shader_db.bin
    uint8_t *dksh = load_from_offset("shader_db.bin",
                                     entry->dksh_offset,
                                     entry->dksh_size);

    DkShader_initialize(out, code_mem_alloc(entry->dksh_size), dksh);
    free(dksh);

    return GL_TRUE;
}
```

**Workflow développeur :**
```bash
# 1. Développeur écrit shader GLSL
cat > my_shader.frag.glsl <<EOF
#version 450
uniform sampler2D tex;
in vec2 uv;
out vec4 color;
void main() { color = texture(tex, uv); }
EOF

# 2. Ajout à la database
./add_to_shader_db.sh my_shader.frag.glsl

# 3. Rebuild SwitchGLES avec nouvelle database
make rebuild_shader_db
```

**Avantages :**
- ✅ Aucune dépendance runtime
- ✅ Performance maximale

**Inconvénients :**
- ⚠️ Workflow lourd pour développeurs
- ⚠️ Database peut devenir énorme
- ⚠️ Pas de vraie génération dynamique

---

**Option B3 : Shaders Pré-fournis Communs**

```c
// Fournir une bibliothèque de shaders utiles précompilés

// SwitchGLES/shaders/common/
// - simple_texture.vert/frag
// - phong_lighting.vert/frag
// - normal_mapping.vert/frag
// - skybox.vert/frag
// - etc.

// API étendue
GLuint sglCreateShaderFromLibrary(const char *shader_name) {
    // Lookup dans bibliothèque précompilée
    precompiled_shader *ps = library_find(shader_name);
    if (!ps) return 0;

    GLuint id = glCreateShader(ps->type);
    shader_object *s = &shaders[id - 1];

    // Charger directement le DKSH
    DkShader_initialize(&s->dk_shader,
                       code_mem_alloc(ps->dksh_size),
                       ps->dksh_data);
    s->compile_status = GL_TRUE;

    return id;
}

// Usage
GLuint vert = sglCreateShaderFromLibrary("phong_lighting.vert");
GLuint frag = sglCreateShaderFromLibrary("phong_lighting.frag");
GLuint prog = glCreateProgram();
glAttachShader(prog, vert);
glAttachShader(prog, frag);
glLinkProgram(prog);
```

**Avantages :**
- ✅ Simple pour cas d'usage communs
- ✅ Aucune compilation
- ✅ Peut couvrir 80% des besoins

**Inconvénients :**
- ⚠️ Limité aux shaders fournis
- ⚠️ Pas de customisation

---

### 📊 Tableau Comparatif des Approches

| Approche | Runtime Compilation | Flexibilité | Taille Assets | Complexité | Compatibilité GL |
|----------|---------------------|-------------|---------------|------------|------------------|
| **FFP Précompilé** | ❌ Non | ⭐⭐⭐ Moyenne | 2-5 MB | ⭐⭐ Faible | OpenGL 1.x: 95% |
| **UAM Subprocess** | ✅ Oui | ⭐⭐⭐⭐⭐ Totale | < 1 MB | ⭐⭐⭐ Moyenne | OpenGL 2.x+: 100% |
| **Shader Database** | ❌ Non | ⭐⭐ Faible | Variable | ⭐⭐⭐⭐ Élevée | Dépend DB |
| **Shader Library** | ❌ Non | ⭐ Très faible | 5-10 MB | ⭐ Très faible | Cas spécifiques |

---

## Solution 2 : FFP Uniquement (Scope Réduit)

**Principe :** Implémenter seulement OpenGL 1.x avec FFP, pas de shaders programmables.

**Target :**
- Vieux jeux et démos OpenGL 1.x
- Applications éducatives
- Portages simples

**Implémentation :**
- Exactement comme Solution 1A (144 shaders précompilés)
- API se limite à OpenGL 1.5
- Pas de glCreateShader/glCompileShader

**Avantages :**
- ✅ Simple à implémenter
- ✅ Petit (2-5 MB)
- ✅ Aucune dépendance

**Inconvénients :**
- ⛔ Pas de OpenGL moderne
- ⛔ Limite sévèrement les applications cibles

**Verdict :** Bon pour un MVP rapide, insuffisant à long terme.

---

## Solution 3 : JIT SASS Compiler (Très Ambitieux)

**Principe :** Créer un compilateur JIT GLSL→SASS natif intégré à SwitchGLES.

```c
// Compilateur SASS embarqué
typedef struct {
    // Parser GLSL
    glsl_parser *parser;

    // IR (Intermediate Representation)
    shader_ir *ir;

    // Backend Maxwell SASS
    sass_codegen *codegen;
} jit_compiler;

GLboolean jit_compile_shader(const char *glsl_source,
                             DkShader *out_shader) {
    jit_compiler *jit = get_jit_instance();

    // Parse GLSL
    ast_node *ast = glsl_parse(jit->parser, glsl_source);
    if (!ast) return GL_FALSE;

    // Générer IR
    shader_ir_build(jit->ir, ast);

    // Optimisations
    shader_ir_optimize(jit->ir);

    // Codegen SASS
    uint8_t *sass_code = NULL;
    size_t sass_size = 0;
    sass_generate(jit->codegen, jit->ir, &sass_code, &sass_size);

    // Packager en DKSH
    uint8_t *dksh = dksh_create(sass_code, sass_size);

    // Initialiser shader
    void *code_mem = code_mem_alloc(dksh_size);
    memcpy(code_mem, dksh, dksh_size);
    DkShader_initialize(out_shader, code_mem, dksh);

    return GL_TRUE;
}
```

**Composants à développer :**
1. **Parser GLSL** : Analyse syntaxique (ou réutiliser Mesa glsl_parser)
2. **Semantic analyzer** : Validation types, etc.
3. **IR builder** : Représentation intermédiaire (SPIR-V like)
4. **Optimizer** : Constant folding, dead code elimination
5. **SASS Backend** : Génération code machine Maxwell
6. **DKSH Packager** : Format binaire deko3d

**Effort estimé :** 6-12 mois pour un seul développeur

**Avantages :**
- ✅ Indépendance totale
- ✅ Optimisations custom possibles
- ✅ Pas de dépendances externes

**Inconvénients :**
- ⛔ Complexité extrême
- ⛔ Maintenabilité difficile
- ⛔ Risque de bugs importants
- ⛔ Performance possiblement inférieure à UAM

**Verdict :** Possible mais déraisonnable pour un projet individuel. Envisageable comme projet communautaire à long terme.

---

## Recommandation Finale

### 🎯 Approche Recommandée : **Solution 1 Hybride**

**Phase 1 (MVP - 3 mois) :**
- FFP avec 144 shaders précompilés
- API OpenGL 1.5 complète
- Tests avec applications legacy

**Phase 2 (Extension - 2 mois) :**
- Ajout support UAM subprocess (optionnel)
- Cache intelligent sur SD
- Documentation pour développeurs

**Phase 3 (Polish - 1 mois) :**
- Bibliothèque de shaders communs précompilés
- Outils de workflow pour ajout shaders custom

### Configuration Build-Time

```makefile
# Makefile SwitchGLES

# Options de compilation
ENABLE_RUNTIME_COMPILATION ?= 1  # UAM subprocess
ENABLE_FFP ?= 1                  # Fixed Function Pipeline
ENABLE_SHADER_LIBRARY ?= 1       # Shaders communs précompilés

# Si runtime compilation désactivée
ifeq ($(ENABLE_RUNTIME_COMPILATION),0)
  $(warning "Runtime compilation disabled. Custom shaders not supported.")
  $(warning "Only FFP and shader library will be available.")
endif
```

### Workflow Développeur

**Cas 1 : Application FFP (OpenGL 1.x)**
```c
// Aucun shader custom → Fonctionne out-of-the-box
glEnable(GL_LIGHTING);
glEnable(GL_TEXTURE_2D);
// ... standard OpenGL 1.x
```

**Cas 2 : Application avec shaders custom + UAM disponible**
```c
// Première exécution: compilation lente (cache manquant)
// Exécutions suivantes: instantané (cache hit)
GLuint vert = glCreateShader(GL_VERTEX_SHADER);
glShaderSource(vert, 1, &vert_source, NULL);
glCompileShader(vert);  // → UAM subprocess → cache
```

**Cas 3 : Application avec shaders custom + pas de UAM**
```c
// Option A: Utiliser bibliothèque
GLuint vert = sglCreateShaderFromLibrary("phong.vert");

// Option B: Précompiler et packager
// $ uam -s vert my_shader.vert -o my_shader.vert.dksh
// → Inclure .dksh dans assets app
GLuint vert = sglLoadCompiledShader("assets/my_shader.vert.dksh");
```

---

## Code Exemple : Système de Fallback

```c
// Gestion gracieuse de l'absence de runtime compilation

typedef enum {
    SHADER_MODE_RUNTIME,   // UAM subprocess disponible
    SHADER_MODE_CACHE,     // Seulement cache (pas de UAM)
    SHADER_MODE_FFP_ONLY   // FFP uniquement
} shader_mode;

shader_mode sgl_shader_mode = SHADER_MODE_FFP_ONLY;

void sglInitialize() {
    // Détecter UAM
    if (system("which uam > /dev/null 2>&1") == 0) {
        sgl_shader_mode = SHADER_MODE_RUNTIME;
        printf("SwitchGLES: Runtime shader compilation available\n");
    } else if (dir_exists("sdmc:/switch/SwitchGLES/shader_cache")) {
        sgl_shader_mode = SHADER_MODE_CACHE;
        printf("SwitchGLES: Shader cache available (no runtime compilation)\n");
    } else {
        sgl_shader_mode = SHADER_MODE_FFP_ONLY;
        printf("SwitchGLES: FFP only mode\n");
    }
}

void glCompileShader(GLuint shader_id) {
    shader_object *s = &shaders[shader_id - 1];
    uint64_t hash = xxhash64(s->source);

    // Chercher en cache d'abord
    if (load_from_cache(hash, &s->dk_shader)) {
        s->compile_status = GL_TRUE;
        return;
    }

    // Pas en cache
    switch (sgl_shader_mode) {
        case SHADER_MODE_RUNTIME:
            // Compiler via UAM
            if (compile_with_uam(s->source, &s->dk_shader)) {
                save_to_cache(hash, &s->dk_shader);
                s->compile_status = GL_TRUE;
            } else {
                s->compile_status = GL_FALSE;
                strcpy(s->info_log, "UAM compilation failed");
            }
            break;

        case SHADER_MODE_CACHE:
        case SHADER_MODE_FFP_ONLY:
            // Pas de compilation possible
            s->compile_status = GL_FALSE;
            strcpy(s->info_log,
                   "Shader not in cache and runtime compilation unavailable. "
                   "Please precompile shader or use FFP.");
            break;
    }
}

// Extension pour charger shaders précompilés
GLuint sglLoadCompiledShader(const char *dksh_path) {
    FILE *f = fopen(dksh_path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    GLuint id = glCreateShader(GL_VERTEX_SHADER);  // Type détecté du DKSH
    shader_object *s = &shaders[id - 1];

    void *code_mem = code_mem_alloc(size);
    memcpy(code_mem, data, size);
    DkShader_initialize(&s->dk_shader, code_mem, data);
    free(data);

    s->compile_status = GL_TRUE;

    return id;
}
```

---

## Conclusion

### ✅ **OUI**, SwitchGLES est TOTALEMENT FAISABLE sans runtime compilation

**Stratégie optimale :**
1. **FFP précompilé** couvre OpenGL 1.x (144 shaders, ~3 MB)
2. **UAM subprocess optionnel** pour shaders custom (requiert UAM homebrew)
3. **Fallback gracieux** vers cache ou bibliothèque précompilée
4. **Documentation claire** pour que développeurs comprennent les options

**Impact sur l'adoption :**
- Applications FFP: ✅ Fonctionnent immédiatement
- Applications GL 2.0+: ⚠️ Requièrent UAM ou précompilation manuelle
- Nouvelles applications: ✅ Peuvent cibler FFP ou utiliser shader library

**Compromis acceptables :**
- Pas de génération procédurale complexe de shaders
- Première compilation lente si UAM disponible
- Workflow développeur légèrement différent

La limitation de deko3d n'est **PAS un bloqueur** pour SwitchGLES ! 🎉
