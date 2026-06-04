> **STATUS — IMPLEMENTED (Jun 2026).** Both the uam emitter and the SwitchGLES
> consumer are done and compile cleanly; the ~120-entry built-in `strcmp` table
> has been removed. See [`REFLECTION_SIDECAR.md`](REFLECTION_SIDECAR.md) for how
> the consumer works, and `uam/README.md` for the format and CLI. Remaining:
> on-device validation (regenerate an example's `.dksh` with `uam --reflect`,
> then run a dEQP-GLES2 regression). `sglRegisterUniform()` is kept as the
> fallback for legacy binaries shipped without a `.refl`.

# Plan : reflection embarquée pour shaders précompilés (uam → SwitchGLES)

**But.** Supprimer la béquille des noms d'uniformes codés en dur dans
`glGetUniformLocation` (≈120 `strcmp`, `gl_uniform.c`) en faisant porter au
binaire shader sa propre table `nom GL → binding/offset`, comme le fait un vrai
pilote. Élimine **à la fois** le fallback en dur **et** l'obligation d'appeler
`sglRegisterUniform()` pour les shaders précompilés.

## Contexte / pourquoi
`glGetUniformLocation` résout dans cet ordre :
1. uniformes enregistrés via `sglRegisterUniform()`,
2. **reflection du programme** (table remplie par le transpileur/Mesa au runtime),
3. table de noms en dur — **consultée uniquement si le programme n'a PAS de
   reflection** (cf. commentaire « Skip built-in table for transpiled programs »).

Donc le problème ne concerne QUE les shaders **deko3d précompilés (.dksh)** : leur
binaire ne contient que des `binding = N` numériques, aucun nom GL. Un nouveau
programme précompilé avec un nom d'uniforme inédit, sans `sglRegisterUniform()`,
renvoie -1. Le chemin runtime (≈99,7 %) est déjà correct grâce à la reflection.

## Conception retenue (solution 1)
uam connaît déjà le mapping `nom GL → binding/offset` à la compilation (reflection
Mesa interne ; déjà exposée au runtime via `uam_get_num_uniforms()` /
`uam_get_uniform_info()` / `uam_get_sampler_info()`). Il faut **sérialiser** cette
table pour le chemin précompilé.

### Côté uam (projet `D:\projets\programmation\switch\DekoGL\uam`)
- Ajouter une émission de reflection à la compilation `.dksh`.
- **Format recommandé : sidecar** `<output>.refl` à côté du `.dksh` (n'invasif :
  ne touche pas au parseur DKSH de deko3d). Alternative plus propre mais plus
  risquée : section custom appondue au `.dksh`.
- Contenu (binaire simple ou texte) :
  - en-tête : magic + version + stage (vert/frag) + counts.
  - par uniforme : `name`, `gl_type` (GLenum), `array_size`, `ubo_binding`,
    `byte_offset` std140, `size`.
  - par sampler : `name`, `gl_type`, `binding`, infos tableau (`array_index`,
    `array_total`, `gles_name`).
- Flag CLI : `--emit-reflection` (ou émission systématique).
- Réutiliser exactement les structures déjà produites pour le runtime, pour que
  les deux chemins convergent.

### Côté SwitchGLES
- `sgl_load_shader_from_file()` / `glShaderBinary()` : après chargement du `.dksh`,
  chercher `<path>.refl` ; si présent, parser et attacher au `sgl_shader_t`.
- Au link (`glLinkProgram`), fusionner cette reflection dans les tables du
  programme — **mêmes champs que le chemin transpileur** (`prog->uniforms`,
  `prog->samplers`, `active_uniforms`, packed UBO, mirrors VS/FS). Idéalement
  factoriser la « populate program reflection » pour que transpileur, Mesa direct
  et précompilé alimentent un seul code.
- Résultat : la résolution 2a/2b/2c (reflection) couvre tout nom → étape 3
  (table en dur) devient inutile.

### Nettoyage une fois en place (= item D de l'audit)
- Supprimer les ≈120 `strcmp` de `glGetUniformLocation` (`gl_uniform.c` ~596-712),
  y compris les noms d'apps/tests spécifiques (`u_testScale`, `es2gears`,
  `SDL_Renderer`, spearmint…).
- Garder éventuellement un mini-jeu de conventions documentées (`u_mvp`, `u_color`)
  pré-enregistrées comme **données** (table seedée dans le registre), pas comme code.
- Mettre à jour la table « Built-in Mappings » de CLAUDE.md (deviendrait optionnelle).

## Compatibilité
- Apps runtime-compilées : inchangées (reflection déjà présente).
- Apps précompilées AVEC `.refl` : marchent sans `sglRegisterUniform()`.
- Apps précompilées SANS `.refl` (anciens binaires) : `sglRegisterUniform()` reste
  le mécanisme de secours — ne PAS le retirer.

## Validation
- Recompiler un shader de test avec uam `--emit-reflection`, vérifier que
  `glGetUniformLocation` trouve un nom inédit sans enregistrement.
- Régression dEQP (chemin runtime ne doit pas bouger).
- `examples/01_textured_quad` ou un exemple à shaders précompilés comme cas réel.
