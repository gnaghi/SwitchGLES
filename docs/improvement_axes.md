# SwitchGLES — Axes d'Amélioration

## 1. Conformance dEQP (Priorité Haute)

### 1.1 Stencil Operations (26 tests)
**Statut:** Non résolu. Barrier Zcull per-draw crash la console.
**Approche recommandée:**
- Investiguer si deko3d a un mode pour désactiver la compression ZETA sur le depth/stencil buffer
- Essayer un barrier UNIQUEMENT après `glClear(GL_STENCIL_BUFFER_BIT)`, pas per-draw
- Comparer avec Nouveau/NVK pour voir comment ils gèrent la cohérence stencil sur Maxwell
- Étudier si le problème est spécifique à certaines combinaisons d'ops (Replace+Always vs Replace+Never)

### 1.2 Cubemap Vertex Texture LOD (16 tests)
**Statut:** Binding correct (confirmé par traces), problème hardware LOD precision.
**Approche:** Possiblement non fixable. Tegra X1 vertex texture unit a une précision LOD différente pour le blending linéaire entre mip levels. Accepter comme limitation hardware.

### 1.3 Uniform Boolean (1 test)
**Statut:** Mesa optimise les booleans en constantes, ils ne sont pas dans le constbuf.
**Approche:** Modifier uam pour forcer Mesa à ne pas optimiser les boolean uniforms (`lower_uniform_to_const = false`).

### 1.4 gl_DepthRange (2 tests)
**Statut:** `depth_range_offset=-1` pour les shaders du test. Mesa n'émet pas STATE_DEPTH_RANGE.
**Approche:** Investiguer dans uam pourquoi `glsl_program_get_depth_range_offset()` retourne -1. Peut-être que Mesa inline les valeurs dans `initial_data`.

### 1.5 Lifetime Attach (2 tests)
**Statut:** Architectural — besoin de refcount GPU-level des images.
**Approche:** Implémenter un refcount sur les DkImage backend indépendant des GL names.

## 2. Performance (Priorité Moyenne)

### 2.1 Dirty Flags pour State Application
Actuellement, TOUT l'état est ré-appliqué avant chaque draw. Avec un système de dirty flags qui track les changements d'état ET les transitions de command buffer, on pourrait réduire le CPU overhead de 50-70%.

### 2.2 Code Memory Free List
Le bump allocator pour le code shader ne libère JAMAIS la mémoire de shaders individuels — elle n'est récupérée que quand TOUS les shaders sont supprimés. Pour des applications qui créent/détruisent beaucoup de shaders (comme dEQP), le pool de 16MB finit par se remplir.
**Solution:** Implémenter un free-list similaire au texture memory allocator.

### 2.3 Texture Upload Asynchrone
Actuellement, chaque `glTexImage2D` fait un `dkQueueWaitIdle()` synchrone. On pourrait utiliser un DMA transfer queue séparé pour les uploads texture, permettant au rendu de continuer pendant les copies.

### 2.4 Command Buffer Overflow
Le callback `cbAddMem` est appelé quand le cmdbuf est plein. Actuellement il submit et réinitialise, mais un double-buffering de cmdbuf pourrait éviter la pause GPU.

## 3. Extensions GLES (Priorité Moyenne)

### 3.1 GL_OES_depth_texture
Le GPU supporte les textures depth (Z24S8). Nécessaire pour les shadow maps. Quelques tests dEQP l'utilisent.

### 3.2 GL_EXT_texture_rg
Formats R8/RG8 pour les textures. Le GPU les supporte nativement. Nécessaire pour certains tests fbo.completeness.

### 3.3 GL_OES_texture_float
Similaire à half-float mais pour 32-bit float. GPU supporte RGBA32F.

### 3.4 GLES 3.0 Partial
- `glBlitFramebuffer` : déjà implémenté
- Instanced rendering (`glDrawArraysInstanced`) : deko3d le supporte
- Transform feedback : non supporté par deko3d (pas de stream out)
- MRT (Multiple Render Targets) : supporté par le hardware

## 4. Robustesse (Priorité Moyenne)

### 4.1 Validation Renforcée
- `glGenTextures` n'est pas atomique (allocation partielle possible)
- `glCopyTexSubImage2D` utilise les dimensions base-level pour les bounds checks de mip levels
- Les pointeurs vertex client sans stride calculé correctement peuvent dépasser le client array

### 4.2 Gestion Mémoire GPU
- `dk_delete_texture` ne fait pas de WaitIdle avant de libérer la mémoire GPU — risque de lecture GPU sur mémoire réallouée
- Le texture memory free-list ne coalesce pas les blocs adjacents (fragmentation)

### 4.3 Thread Safety
- Les globals statiques dans `gl_uniform.c` (registry) ne sont pas thread-safe
- Le contexte courant est un global (`sgl_get_current_context`) — pas de TLS

## 5. Intégration SDL3 (Priorité selon besoin)

### 5.1 Ce qui est réutilisable
- Memory pool patterns (5 régions GPU)
- VBO free-list allocator avec coalescence
- Texture staging avec ARM write barriers
- Descriptor management (TIC/TSC)
- Blend workaround (register 0x786)
- Format conversion tables
- Cubemap upload handling

### 5.2 Ce qui doit changer
- SDL_GPU utilise des render passes explicites (vs. implicit state)
- Pipeline objects (vs. per-draw state)
- Deferred destruction avec refcount atomique (vs. delete_pending)
- Transfer queue séparé (vs. synchronous upload)
- Pre-compiled shaders DKSH (vs. runtime compilation)

## 6. Qualité du Code (Priorité Basse)

### 6.1 Fragmentation du Blend Workaround
Le workaround blend.dst utilise des offsets hardcodés dans la struct interne DkCmdBuf (offset 112, 120). Si deko3d est mis à jour, ça casse silencieusement. Idéalement, upstreamer le fix dans deko3d.

### 6.2 Documentation Inline
Les fichiers backend (dk_texture.c, dk_shader.c) sont longs (2000+ lignes) et manquent de documentation sur les invariants et les pré-conditions.

### 6.3 Tests Unitaires
Aucun test unitaire pour le transpiler, le resource manager, ou la conversion de formats. Les tests se font uniquement via dEQP et le validation_test.
