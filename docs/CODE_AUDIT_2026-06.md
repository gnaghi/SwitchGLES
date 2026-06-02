# Audit de code SwitchGLES — Juin 2026

Audit complet des ~23 000 lignes de `source/` (4 couches : GL entry points, backend
deko3d, transpileur GLSL, EGL/contexte/util). Objectifs : bugs, maintenabilité,
conformité à un standard de codage strict.

**Standard de style retenu : coding style noyau Linux / Nouveau** (driver DRM en C
pur — c'est la cible la plus naturelle ; Wayland en est une variante proche).

> Constat transversal des 4 analyses : le style actuel (indentation 4 espaces,
> `typedef struct`, accolade de fonction attachée) est **interne­ment cohérent**.
> Le passer en strict noyau est un reformatage mécanique massif (touche chaque
> ligne, casse `git blame`). Voir §4 pour l'arbitrage.

---

## 0. Statut des correctifs (2026-06-02)

**Validation matérielle (2026-06-02) :** chaîne complète recompilée (lib propre →
relink deqp-gles2 → NRO) et déployée sur la Switch. Batch `caselist_000.txt`
(147 tests connus-en-échec) : **16/147 pass vs 14/147 baseline — 0 régression
(PASS→FAIL), +2 (FAIL→PASS)**. Couvre uniformes/draw/shaders. Reste à valider sur
un batch de tests actuellement-PASS ciblant blend/texture/fbo (B4/B8).

**Bugs P0/P1 B1–B12 corrigés** (lib recompilée OK). Détail :

- B1 ✅ `gl_state.c` apply_raster copie maintenant polygon_offset_*.
- B2 ✅ `sgl_res_mgr_destroy()` ajouté + appelé dans `sgl_context_destroy`.
- B3 ✅ `sgl_res_mgr_free_program` libère `info_log`.
- B4 ✅ `dk_ensure_recordable()` ajouté + appelé en entrée des ops texture/FBO
  synchrones (image_2d, sub_image_2d, generate_mipmap, copy_tex_*,
  compressed_*, read_pixels, blit_framebuffer).
- B5 ✅ Bornes packed UBO en 64-bit (float/int/mat2/3/4) + bounds-check `binding`
  manquant dans les 3 chemins matrices.
- B6 ✅ `glBufferSubData` + `glTexSubImage2D` : bornes sans addition débordante.
- B7 ✅ `glDrawElements` : NULL-check `ebo_data` + garde anti-wrap `max_idx+1`.
- B8 ✅ Workaround blend.dst : décision de flush déplacée en amont (séquence
  cohérente), plus de flush mid-record ; `#define` remontés au niveau fichier.
- B9 ✅ Free-list VBO : la waste d'alignement de tête est réinsérée (plus de fuite).
- B10 ✅ Transpileur : `malloc`/`realloc` vérifiés (flag `failed` + propagation).
- B11 partiel : la troncature silencieuse de sortie (`sb`) renvoie désormais une
  erreur (B10) ; les buffers de ligne `MAX_LINE_LEN` restent à durcir.
- B12 ✅ État global transpileur réinitialisé en entrée de `glslt_transpile` et
  en fin de `glslt_validate_es100`.

**P2 EGL corrigés (2026-06-02, lib recompilée OK) :**
- `eglCreateWindowSurface` / `eglCreateContext` valident `config`
  (`sgl_egl_get_config` → `EGL_BAD_CONFIG`) et stockent le `config_id`.
- `eglQuerySurface`/`eglQueryContext` renvoient le vrai `EGL_CONFIG_ID`
  (champs ajoutés à `sgl_surface` et `sgl_context_t`).
- `eglMakeCurrent` : table §3.7.3 (NO_CONTEXT exige NO_SURFACE des deux côtés ;
  contexte non-NULL exige draw+read → `EGL_BAD_MATCH`) + validation des handles
  de surface (`sgl_egl_get_surface` → `EGL_BAD_SURFACE`).
- `eglDestroySurface` : détache la surface de tout contexte la référençant
  (plus de pointeur pendant).
- `eglSwapInterval` : clamp [0,4].

**Reste à faire :** suppression différée stricte EGL (contexte/surface courant —
non bloquant en mono-thread), refactoring §3 et style §4.

---

## 1. Bugs confirmés (par priorité)

### P0 — à corriger en priorité

| # | Fichier:ligne | Problème |
|---|---------------|----------|
| B1 | `gl/gl_state.c:67-75` | `apply_raster` **ne copie pas** les champs `polygon_offset_*` du `sgl_raster_state_t` → on pousse au backend une valeur de **pile non initialisée** à chaque `glCullFace/glFrontFace/glEnable(CULL_FACE)`. La version dans `gl_draw.c:114` les copie, elle. Incohérence directe. |
| B2 | `context/sgl_context.c:77-87` | `sgl_context_destroy` fait un `memset` brut **sans libérer** `shaders[].source/info_log/mesa_meta` ni `programs[].info_log` → fuite heap CPU cumulative à chaque `eglDestroyContext`/`eglTerminate` (pénalisant en boucle dEQP). |
| B3 | `context/sgl_resource_manager.c:105` | `sgl_res_mgr_free_program` ne libère pas `info_log` (contrairement à `free_shader`). Fuite. |
| B4 | `backend/deko3d/dk_texture.c` (×5) + `dk_framebuffer.c:308` | `dkCmdBufFinishList`+`WaitIdle` synchrones **sans garde `cmdbuf_submitted` ni `dkQueueIsInErrorState`**. Si appelé après `dk_end_frame`, c'est le double-`finishList` explicitement documenté comme **crash**. |

### P1 — bugs réels, impact conditionnel

| # | Fichier:ligne | Problème |
|---|---------------|----------|
| B5 | `gl/gl_uniform.c:1470,1682,1900…` | Débordement d'entier dans les bornes packed UBO : `offset + count*stride` calculé sur `count` non borné → wrap-around, la garde passe, `memcpy` hors buffer. |
| B6 | `gl/gl_buffer.c:236`, `gl/gl_texture.c:528` | Garde `offset + size > cap` en arithmétique signée (UB potentiel). Motif sûr : `off > cap \|\| len > cap - off`. SubImage ne plafonne pas non plus les dimensions à 8192. |
| B7 | `gl/gl_draw.c:513` | `glDrawElements` : `ebo_data` déréférencé **sans test NULL**, et `vertex_count = max_idx+1` peut wrap à 0 pour `UNSIGNED_INT`. |
| B8 | `backend/deko3d/dk_state.c:160` | Workaround blend.dst : si overflow, `dk_submit_and_reset` est appelé **au milieu** de l'enregistrement de l'état de blend → color/blend state déjà recordé perdu, registre brut `0x786` écrit dans un cmdbuf incohérent. |
| B9 | `backend/deko3d/dk_buffer.c:137` | Free-list VBO : la `alignment_waste` en tête de bloc n'est **jamais réinsérée** (le commentaire le promet, le code manque). Fuite latente (masquée car offsets déjà alignés 256). |
| B10 | `transpiler/glsl_transpiler.c` (sb_ensure:42, :2209…) | `malloc`/`realloc` **jamais vérifiés** → NULL deref / `result.success=1` malgré OOM. |
| B11 | `transpiler/glsl_transpiler.c:2319` | Lignes > 2048 octets **tronquées silencieusement** (`extract_line`, `sb_printf`, `replace_word`) → shader de sortie cassé sans erreur. |
| B12 | `transpiler/glsl_transpiler.c:122-136` | État **global statique** (`s_structs`, `s_replacements`…) non réinitialisé sur les chemins d'erreur ni par `glslt_validate_es100` → pollution d'état entre shaders + non thread-safe. |

### P2 — conformité EGL / robustesse

- `egl_impl.c:530` `eglCreateWindowSurface` ne valide pas `config` (NULL deref possible, devrait être `EGL_BAD_CONFIG`).
- `egl_impl.c:833` `eglMakeCurrent` : combinaisons contexte/surface de la table EGL §3.7.3 non gérées ; surfaces non validées (`used`).
- `egl_impl.c:662/800` `eglDestroySurface`/`eglDestroyContext` : pas de suppression différée si courant (dangling).
- `egl_impl.c:967` `eglSwapInterval` : pas de clamp [0,4] (négatif → énorme `uint32_t`).
- `egl_impl.c:713/1064` `EGL_CONFIG_ID` codé en dur à 1.

---

## 2. Maintenabilité — duplication & fonctions monstres

| Thème | Localisation | Détail |
|-------|--------------|--------|
| Macro à **flot de contrôle caché** | `gl/gl_common.h:19` | `GET_CTX()` déclare `ctx` **et** contient un `return` caché, utilisée dans ~150 fonctions. Anti-pattern noyau majeur (casse l'analyse statique, masque les sorties). |
| Setters d'uniformes dupliqués | `gl/gl_uniform.c:1398-1767` | `set_float_uniform` / `set_int_uniform` 99 % identiques (~370 lignes). |
| Matrices dupliquées ×3 | `gl/gl_uniform.c:1855-2146` | `glUniformMatrix{2,3,4}fv` copiés-collés (~280 lignes pour ~70 de logique). |
| `glLinkProgram` monstre | `gl/gl_shader.c:866-1896` | ~1030 lignes ; chemins Mesa et transpileur dupliquent ~600 lignes (config UBO packed, mirrors, active_uniforms, attribs). |
| Remplissage d'état ×3 | `gl_state.c` / `gl_draw.c` / `gl_clear.c` | Bloc `sgl_depth_stencil_state_t` (~20 affectations) recopié 3×. Idem `sgl_ensure_frame_ready` (egl_impl.c:128). **C'est la cause-racine de B1.** |
| Bloc staging texture ×6 | `backend/deko3d/dk_texture.c` (2748 l.) | staging→convert→upload→sync→rebind dupliqué 6 fois, indentation déjà cassée. Tout fix (ex. B4) doit être répété 6×. |
| Free-list dupliquée ×3 | `dk_buffer.c` / `dk_command.c` / `dk_texture.c` | Insertion triée + coalescing recopiée 3×. |
| Validation display ×20 | `egl_impl.c` | `if (display != &g_sgl.display …)` copié dans presque chaque entrée EGL. |
| `glslt_transpile` monstre | `transpiler/glsl_transpiler.c:2178-2746` | ~570 lignes ; parseur `[N+M]` dupliqué verbatim (`:696` et `:844`). |
| Built-ins de test en dur | `gl/gl_uniform.c:591-712` | ~120 `strcmp` mappant des noms d'uniformes de tests/apps spécifiques en dur dans le chemin de production. |

---

## 3. Axes d'amélioration structurels

1. **Centraliser la construction d'état GL→backend** : un seul `sgl_build_{dss,blend,raster,color,viewport}()` partagé entre `gl_state.c`, `gl_draw.c`, `gl_clear.c`, `sgl_ensure_frame_ready`. Supprime la triplication **et corrige B1**.
2. **Unifier les setters d'uniformes** : `set_scalar_uniform(is_int, comps, count)` + `set_matrix_uniform(cols, count)`. Supprime ~700 lignes et permet de corriger B5 en un seul point.
3. **Découper `glLinkProgram`** : extraire `finalize_uniforms()`, `setup_packed_mirrors()`, `populate_active_uniforms()` partagées Mesa/transpileur (−600 lignes).
4. **Backend : un seul `dk_sync_submit(dk)`** (vérifie `cmdbuf_submitted` + `dkQueueIsInErrorState`) et un `dk_stage_and_upload()` unique. Corrige B4 et découpe `dk_texture.c`.
5. **Allocateur free-list unifié** (VBO/texture/deferred), corrige B9 une seule fois.
6. **Transpileur** : contexte explicite (`glslt_ctx_t*`) au lieu de l'état global (B12) ; politique uniforme « renvoyer un statut sur dépassement » au lieu de tronquer (B10, B11) ; à terme, vrai tokenizer plutôt que string-matching ligne-par-ligne.
7. **Resource manager** : `sgl_res_mgr_destroy()` libérant tout le heap (corrige B2/B3).

---

## 4. Conformité de style — arbitrage requis

Le style noyau/Nouveau strict impose, par rapport à l'existant :

| Règle noyau | Écart actuel | Coût |
|-------------|--------------|------|
| Tabs 8 colonnes | 4 espaces partout | Mécanique (clang-format) |
| Accolade de fonction sur ligne propre | attachée (K&R) | Mécanique (clang-format) |
| Lignes ~80 colonnes | nombreuses > 100 | Mécanique + manuel |
| **Pas de `typedef struct`** | `typedef … _t` partout | **Très invasif** (chaque type, chaque fichier) |
| Pas de macro à flot caché | `GET_CTX()` ×150 | Manuel, à risque |

Le reformatage pur (tabs, accolades, colonnes) est automatisable via `.clang-format`.
La suppression des `typedef struct` et de `GET_CTX()` est invasive et touche un code
qui passe à 99 % les tests — à faire par étapes vérifiées, pas en un bloc.
